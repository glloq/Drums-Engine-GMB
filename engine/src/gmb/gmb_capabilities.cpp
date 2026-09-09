#include "gmb_capabilities.h"

// ----------------------------------------------------------------------------
// Role d'un comportement d'actionneur
// ----------------------------------------------------------------------------
GmbRole gmbRoleForBehavior(ActuatorBehavior behavior) {
  switch (behavior) {
    // Mecanismes qui font du bruit : ce sont eux, et eux seuls, qui limitent la
    // polyphonie et portent la velocite.
    case ActuatorBehavior::SOLENOID_STRIKE:
    case ActuatorBehavior::SOLENOID_HOLD:
    case ActuatorBehavior::SERVO_STRIKE:
    case ActuatorBehavior::COMB_BRUSH:
    case ActuatorBehavior::MOTOR_OPTICAL_TRACK:
    case ActuatorBehavior::MOTOR_TIMED:
    case ActuatorBehavior::MOTOR_SPEED:
    case ActuatorBehavior::MOTOR_SWEEP:
    case ActuatorBehavior::MOTOR_ALTERNATE:
    case ActuatorBehavior::STEPPER_STRIKE:
    case ActuatorBehavior::STEPPER_ROTATE:
      return GmbRole::STRIKE;

    // Etouffoirs. SERVO_POSITION en fait partie : c'est le comportement que le
    // modele "cymbal_mute" utilise comme etouffoir, pose sur le NoteOff.
    case ActuatorBehavior::SERVO_MUTE:
    case ActuatorBehavior::SOLENOID_MUTE:
    case ActuatorBehavior::SERVO_POSITION:
      return GmbRole::DAMP;

    case ActuatorBehavior::HIHAT_CONTROLLER:
      return GmbRole::PEDAL;

    case ActuatorBehavior::PITCH_BEND:
      return GmbRole::TUNE;

    case ActuatorBehavior::STEPPER_POSITION:
      return GmbRole::POSITION;

    default:
      return GmbRole::STRIKE;
  }
}

const char* gmbRoleName(GmbRole role) {
  switch (role) {
    case GmbRole::STRIKE:   return "strike";
    case GmbRole::DAMP:     return "damp";
    case GmbRole::PEDAL:    return "pedal";
    case GmbRole::POSITION: return "position";
    case GmbRole::TUNE:     return "tune";
    default:                return "unknown";
  }
}

// ----------------------------------------------------------------------------
// Durees effectives d'une impulsion, apres application des memes replis que
// Scheduler::scheduleActionSteps() : durees a 0/0 -> parametres de l'actionneur,
// et min/max remis dans l'ordre.
// ----------------------------------------------------------------------------
static void pulseBounds(const ActionStep& step, const ActuatorConfig& cfg,
                        uint16_t& minMs, uint16_t& maxMs) {
  minMs = step.duration_min_ms;
  maxMs = step.duration_max_ms;
  if (minMs == 0 && maxMs == 0) {
    minMs = cfg.paramMin;
    maxMs = cfg.paramMax;
  }
  if (maxMs < minMs) { uint16_t t = minMs; minMs = maxMs; maxMs = t; }
}

bool gmbStepIsVelocitySensitive(const ActionStep& step, const ActuatorConfig& cfg) {
  const ValueSource src = (ValueSource)step.value_source;
  if (src != ValueSource::VELOCITY && src != ValueSource::INVERTED_VELOCITY) return false;

  switch ((CommandType)step.command_type) {
    case CommandType::PULSE: {
      // SOLENOID_HOLD n'utilise pas cmd.value : l'actionneur ecrit paramMin en
      // PWM d'activation, quelle que soit la velocite.
      if (cfg.behavior == ActuatorBehavior::SOLENOID_HOLD) return false;
      uint16_t minMs, maxMs;
      pulseBounds(step, cfg, minMs, maxMs);
      return maxMs > minMs;   // durees egales = frappe toujours identique
    }
    case CommandType::POSITION:
      // SOLENOID_MUTE compare la valeur a un seuil (>= 64) : deux etats, pas de
      // nuance.
      return cfg.behavior != ActuatorBehavior::SOLENOID_MUTE;
    case CommandType::PWM:
      return true;
    default:
      return false;           // OFF
  }
}

uint16_t gmbStepRearticulationMs(const ActionStep& step, const ActuatorConfig& cfg) {
  // cooldownUs est stocke en us/100 (voir ActuatorConfig).
  uint32_t cooldownMs = ((uint32_t)cfg.cooldownUs * 100u) / 1000u;
  uint32_t pulseMs = 0;
  if ((CommandType)step.command_type == CommandType::PULSE &&
      cfg.behavior != ActuatorBehavior::SOLENOID_HOLD) {
    uint16_t minMs, maxMs;
    pulseBounds(step, cfg, minMs, maxMs);
    pulseMs = maxMs;
  }
  uint32_t v = (cooldownMs > pulseMs) ? cooldownMs : pulseMs;
  return (v > 0xFFFF) ? (uint16_t)0xFFFF : (uint16_t)v;
}

// ----------------------------------------------------------------------------
// Construction
// ----------------------------------------------------------------------------
namespace {

struct Builder {
  const GmbBuildInputs& in;
  CapabilitySnapshot& out;
  // Index actionneur -> mecanisme, remis a zero pour chaque instrument logique.
  uint8_t mechOf[MAX_ACTUATORS];

  Builder(const GmbBuildInputs& i, CapabilitySnapshot& o) : in(i), out(o) {
    resetMechMap();
  }

  const ActuatorConfig* actuator(uint8_t id) const {
    if (id == 0xFF || !in.actuatorLookup) return nullptr;
    const ActuatorConfig* cfg = in.actuatorLookup(in.actuatorCtx, id);
    if (!cfg || !cfg->enabled) return nullptr;
    return cfg;
  }

  // Un pipeline est utilisable s'il declenche vraiment quelque chose : au moins
  // une action NoteOn visant un actionneur qui existe ET qui est active. Un
  // pipeline vide (instrument dont tous les actionneurs ont ete desactives
  // depuis) est route mais muet — il ne doit pas produire de note annoncee.
  bool pipelineUsable(const CompiledPipeline& p) const {
    for (uint8_t i = 0; i < p.note_on_count; i++) {
      if (actuator(p.note_on_actions[i].actuator_id)) return true;
    }
    return false;
  }

  void resetMechMap() { memset(mechOf, 0xFF, sizeof(mechOf)); }

  // Obtenir (ou creer) le mecanisme de cet actionneur pour l'instrument en
  // cours. Renvoie nullptr si le pool est plein — le cas est COMPTE.
  GmbMechanism* mechanism(uint8_t actuatorId, const ActuatorConfig& cfg) {
    if (actuatorId >= MAX_ACTUATORS) return nullptr;
    if (mechOf[actuatorId] != 0xFF) return &out.mechanisms[mechOf[actuatorId]];
    if (out.mechanismCount >= GMB_MAX_MECHANISMS) {
      out.droppedMechanisms++;
      return nullptr;
    }
    GmbMechanism& m = out.mechanisms[out.mechanismCount];
    m.actuatorId = actuatorId;
    m.role = (uint8_t)gmbRoleForBehavior(cfg.behavior);
    m.flags = 0;
    m.rearticulationMs = 0;
    m.currentMa = cfg.currentMa;
    m.notes.clear();
    mechOf[actuatorId] = out.mechanismCount;
    out.mechanismCount++;
    return &m;
  }
};

// Plus petit bit arme (1..16), 0 si aucun.
uint8_t lowestChannel(uint16_t mask) {
  for (uint8_t ch = 1; ch <= MIDI_CHANNEL_COUNT; ch++) {
    if (mask & (uint16_t)(1u << (ch - 1))) return ch;
  }
  return 0;
}

}  // namespace

void gmbBuildCapabilities(const GmbBuildInputs& in, CapabilitySnapshot& out) {
  out.clear();
  for (uint8_t i = 0; i < GMB_MAX_LOGICAL_INSTRUMENTS; i++) {
    out.instruments[i].hihatPedalCc = 0xFF;
  }
  if (!in.lookup) return;

  const PipelineLookup& lk = *in.lookup;
  Builder b(in, out);

  // Utilisabilite des pipelines, calculee UNE fois. La reponse ne depend que du
  // pipeline, et la boucle qui suit la consulte jusqu'a 16 x 128 fois : la
  // recalculer a chaque note allongerait d'autant la fenetre pendant laquelle
  // le coeur temps reel est gare.
  bool usable[MAX_PIPELINES];
  for (uint8_t p = 0; p < MAX_PIPELINES; p++) {
    usable[p] = (p < lk.pipeline_count) && b.pipelineUsable(lk.pipelines[p]);
  }

  // Plafond global de simultaneite : le budget electrique, tel que l'applique
  // ActuatorManager (core/power_budget.h). C'est une contrainte transverse,
  // pas une propriete d'un instrument.
  out.globalMaxSimultaneous = in.power.maxConcurrent;

  // --- 1. Quels canaux exposer ? --------------------------------------------
  // Un instrument logique = un canal MIDI. Les canaux explicitement declares
  // par au moins un pipeline utilisable font foi. Les pipelines OMNI
  // (midiChannel == 0) repondent sur les 16 canaux : les declarer 16 fois
  // produirait 16 entrees identiques et saturerait le plafond de l'hote, donc
  // ils sont rattaches aux canaux deja declares — et, si la configuration
  // n'en declare aucun, au canal de percussion General MIDI.
  uint16_t explicitChannels = 0;
  bool hasOmni = false;
  for (uint8_t p = 0; p < lk.pipeline_count; p++) {
    const CompiledPipeline& pipe = lk.pipelines[p];
    if (!usable[p]) continue;
    if (pipe.midi_channel == 0) hasOmni = true;
    else if (pipe.midi_channel <= MIDI_CHANNEL_COUNT) {
      explicitChannels |= (uint16_t)(1u << (pipe.midi_channel - 1));
    }
  }

  uint16_t emit = (uint16_t)(explicitChannels & in.channelMask);
  if (emit == 0 && hasOmni) {
    const uint16_t preferred = (uint16_t)(1u << in.defaultChannel);
    if (in.channelMask & preferred) {
      emit = preferred;
    } else {
      uint8_t ch = lowestChannel(in.channelMask);
      emit = ch ? (uint16_t)(1u << (ch - 1)) : 0;
    }
  }
  if (emit == 0) return;   // rien de routable : descripteur "non configure"

  // --- 2. Un instrument logique par canal ------------------------------------
  for (uint8_t ch = 1; ch <= MIDI_CHANNEL_COUNT; ch++) {
    if (!(emit & (uint16_t)(1u << (ch - 1)))) continue;
    if (out.instrumentCount >= GMB_MAX_LOGICAL_INSTRUMENTS) break;

    GmbInstrumentCaps& e = out.instruments[out.instrumentCount];
    e.channel = (uint8_t)(ch - 1);
    e.mechFirst = out.mechanismCount;
    b.resetMechMap();

    uint16_t prepareMaxMs = 0;
    uint16_t exciteMaxMs = 0;
    uint16_t releaseMaxMs = 0;
    bool anyNoteOff = false;

    // --- Notes jouables. La table (canal, note) a deja applique OMNI,
    // precedence et deduplication : une note y a au plus un pipeline, donc
    // elle ne peut pas etre annoncee deux fois meme si plusieurs actionneurs
    // se cachent derriere.
    for (uint16_t n = 0; n < 128; n++) {
      const uint8_t pIdx = lk.pipelineFor(ch, (uint8_t)n);
      if (pIdx == 0xFF || pIdx >= lk.pipeline_count || !usable[pIdx]) continue;
      const CompiledPipeline& pipe = lk.pipelines[pIdx];

      e.notes.add((uint8_t)n);

      // Delais, en ms, tels que le scheduler les appliquera.
      bool haveStrike = false, havePrep = false, haveAny = false;
      uint16_t strikeDelay = 0, prepDelay = 0, anyDelay = 0;

      for (uint8_t s = 0; s < pipe.note_on_count; s++) {
        const ActionStep& step = pipe.note_on_actions[s];
        const ActuatorConfig* cfg = b.actuator(step.actuator_id);
        if (!cfg) continue;

        GmbMechanism* m = b.mechanism(step.actuator_id, *cfg);
        if (m) {
          m->notes.add((uint8_t)n);
          if ((GmbRole)m->role == GmbRole::STRIKE) {
            if (gmbStepIsVelocitySensitive(step, *cfg)) m->flags |= GMB_MECH_VELOCITY;
            uint16_t r = gmbStepRearticulationMs(step, *cfg);
            if (r > m->rearticulationMs) m->rearticulationMs = r;
          }
        }

        const GmbRole role = gmbRoleForBehavior(cfg->behavior);
        if (!haveAny || step.delay_ms < anyDelay) { anyDelay = step.delay_ms; haveAny = true; }
        if (role == GmbRole::STRIKE) {
          if (!haveStrike || step.delay_ms < strikeDelay) { strikeDelay = step.delay_ms; haveStrike = true; }
        } else {
          if (!havePrep || step.delay_ms < prepDelay) { prepDelay = step.delay_ms; havePrep = true; }
        }
      }

      for (uint8_t s = 0; s < pipe.note_off_count; s++) {
        const ActionStep& step = pipe.note_off_actions[s];
        const ActuatorConfig* cfg = b.actuator(step.actuator_id);
        if (!cfg) continue;
        anyNoteOff = true;
        if (step.delay_ms > releaseMaxMs) releaseMaxMs = step.delay_ms;
        // L'etouffoir du modele "cymbal_mute" n'apparait QUE sur le NoteOff :
        // sans cette passe, la relation de choke serait invisible.
        GmbMechanism* m = b.mechanism(step.actuator_id, *cfg);
        if (m && (GmbRole)m->role != GmbRole::STRIKE) m->notes.add((uint8_t)n);
      }

      // Fenetre de preparation silencieuse : un positionnement pose AVANT la
      // frappe (etouffoir qui se retire, servo qui se met en place, stepper qui
      // rejoint sa zone). GMB peut l'anticiper avec son lookahead, elle
      // n'entre donc pas dans la compensation de latence.
      if (!haveStrike && haveAny) { strikeDelay = anyDelay; haveStrike = true; }
      uint16_t prepareWindow = 0;
      if (haveStrike && havePrep && strikeDelay > prepDelay) {
        prepareWindow = (uint16_t)(strikeDelay - prepDelay);
      }
      const uint16_t noteExcite = haveStrike ? (uint16_t)(strikeDelay - prepareWindow) : 0;
      if (prepareWindow > prepareMaxMs) prepareMaxMs = prepareWindow;
      if (noteExcite > exciteMaxMs) exciteMaxMs = noteExcite;
    }

    // --- CC reellement routes sur ce canal. La table compilee fait foi : une
    // liaison que le compilateur a laissee tomber (table pleine) n'est pas une
    // capacite.
    for (uint16_t cc = 0; cc < 128; cc++) {
      const uint8_t first = lk.cc_to_first[cc];
      const uint8_t cnt = lk.cc_to_count[cc];
      if (first == 0xFF || cnt == 0) continue;
      for (uint8_t r = first; r < first + cnt && r < lk.cc_route_count; r++) {
        const CCRoutingEntry& route = lk.cc_routes[r];
        if (route.channel != 0 && route.channel != ch) continue;
        const ActuatorConfig* cfg = b.actuator(route.actuator_id);
        if (!cfg) continue;

        // Les CC virtuels ne sont pas des CC sur le fil : ce sont les canaux
        // internes par lesquels le moteur fait passer l'aftertouch et le pitch
        // bend. Les annoncer comme CC#125/126 tromperait l'hote — ils
        // alimentent les champs dedies de `expression`.
        if (cc == VIRTUAL_CC_AFTERTOUCH)      e.flags |= GMB_INST_AFTERTOUCH;
        else if (cc == VIRTUAL_CC_PITCH_BEND) e.flags |= GMB_INST_PITCHBEND;
        else                                  e.ccs.add((uint8_t)cc);

        if (cfg->behavior == ActuatorBehavior::HIHAT_CONTROLLER &&
            cc != VIRTUAL_CC_AFTERTOUCH && cc != VIRTUAL_CC_PITCH_BEND) {
          e.hihatPedalCc = (uint8_t)cc;
        }
        b.mechanism(route.actuator_id, *cfg);
      }
    }

    e.mechCount = (uint8_t)(out.mechanismCount - e.mechFirst);

    // --- Velocite et reaticulation au niveau de l'instrument -----------------
    uint16_t rearticulation = 0;
    uint8_t strikeCount = 0;
    uint16_t strikeCurrents[GMB_MAX_MECHANISMS];
    for (uint8_t m = e.mechFirst; m < out.mechanismCount; m++) {
      const GmbMechanism& mech = out.mechanisms[m];
      if ((GmbRole)mech.role != GmbRole::STRIKE) continue;
      if (mech.flags & GMB_MECH_VELOCITY) e.flags |= GMB_INST_VELOCITY;
      if (mech.rearticulationMs > rearticulation) rearticulation = mech.rearticulationMs;
      strikeCurrents[strikeCount++] = mech.currentMa;
    }

    // --- Polyphonie ----------------------------------------------------------
    // Ce n'est PAS le nombre d'actionneurs. C'est le nombre d'evenements MIDI
    // simultanes que la machine peut reellement executer :
    //   - un actionneur ne frappe qu'une note a la fois (plafond en nombre de
    //     mecanismes sonores) ;
    //   - ActuatorManager refuse au-dela de maxConcurrent ;
    //   - et au-dela du courant de crete declare, quel que soit le compte.
    uint8_t poly = strikeCount;
    if (in.power.maxConcurrent > 0 && poly > in.power.maxConcurrent) {
      poly = in.power.maxConcurrent;
    }
    if (in.power.maxPeakMa > 0 && strikeCount > 0) {
      // Tri croissant (insertion, <= 32 elements) : le meilleur des cas est
      // d'allumer d'abord les actionneurs les moins gourmands.
      for (uint8_t i = 1; i < strikeCount; i++) {
        uint16_t key = strikeCurrents[i];
        int16_t j = (int16_t)i - 1;
        while (j >= 0 && strikeCurrents[j] > key) { strikeCurrents[j + 1] = strikeCurrents[j]; j--; }
        strikeCurrents[j + 1] = key;
      }
      uint32_t sum = 0;
      uint8_t fit = 0;
      for (uint8_t i = 0; i < strikeCount; i++) {
        // Courant non declare = ne consomme pas de budget et n'est jamais
        // refuse, exactement comme dans powerBudgetAdmit().
        if (strikeCurrents[i] == 0) { fit++; continue; }
        if (sum + strikeCurrents[i] > in.power.maxPeakMa) break;
        sum += strikeCurrents[i];
        fit++;
      }
      if (fit < poly) poly = fit;
    }
    e.polyphonyMax = poly;

    // --- Modele temporel -----------------------------------------------------
    e.timing.prepareMaxMs = prepareMaxMs;
    e.timing.exciteLatencyMs = exciteMaxMs;
    e.timing.rearticulationMs = rearticulation;
    e.timing.releaseMs = releaseMaxMs;
    // Le scheduler dispatche sur un timer materiel a SCHEDULER_TICK_HZ : la
    // granularite d'un tick est la seule incertitude que le firmware connaisse
    // de lui-meme. Le reste (jeu mecanique, transit du solenoide) se mesure au
    // micro, cote GMB, et ne se declare pas ici.
    e.timing.jitterMs = (SCHEDULER_TICK_HZ >= 1000) ? 1 : (uint8_t)(1000 / SCHEDULER_TICK_HZ);
    e.timing.known = 0;
    if (prepareMaxMs > 0)   e.timing.known |= GMB_T_PREPARE;
    if (!e.notes.empty())   e.timing.known |= GMB_T_EXCITE;   // 0 ms est une valeur, pas une ignorance
    if (rearticulation > 0) e.timing.known |= GMB_T_REARTICULATION;
    if (anyNoteOff)         e.timing.known |= GMB_T_RELEASE;

    // --- Etat declare --------------------------------------------------------
    // `configured` ne dit pas "une entree existe en base" : il dit "cet
    // instrument est utilisable tel quel". Une entree sans note jouable est
    // declaree non configuree, ce qui renvoie GMB a la saisie manuelle SANS
    // ecraser ce que l'utilisateur y avait deja mis (SYSEX_IDENTITY.md §5.1).
    if (!e.notes.empty()) e.flags |= GMB_INST_CONFIGURED;

    out.instrumentCount++;
  }
}
