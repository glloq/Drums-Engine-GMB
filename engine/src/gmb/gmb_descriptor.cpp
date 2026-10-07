#include "gmb_descriptor.h"

namespace {

// ----------------------------------------------------------------------------
// Ecrivain JSON minimal, ASCII garanti, sans allocation
// ----------------------------------------------------------------------------
// `overflow` est collant : une fois arme, plus rien n'est ecrit et l'appelant
// jette le resultat entier. C'est ce qui interdit structurellement d'emettre du
// JSON tronque.
struct Writer {
  char* buf;
  size_t cap;
  size_t len = 0;
  bool overflow = false;

  Writer(char* b, size_t c) : buf(b), cap(c) {}

  void ch(char c) {
    if (overflow) return;
    if (len >= cap) { overflow = true; return; }
    buf[len++] = c;
  }

  void raw(const char* s) {
    while (*s && !overflow) ch(*s++);
  }

  void num(uint32_t v) {
    char tmp[11];
    uint8_t n = 0;
    do { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } while (v && n < sizeof(tmp));
    while (n) ch(tmp[--n]);
  }

  void boolean(bool v) { raw(v ? "true" : "false"); }

  // Chaine JSON ASCII. Les guillemets, l'antislash et les caracteres de
  // controle sont echappes ; toute sequence UTF-8 est convertie en \uXXXX pour
  // que le descripteur reste 7-bit safe sans packing.
  void str(const char* s) {
    ch('"');
    if (!s) { ch('"'); return; }
    const uint8_t* p = (const uint8_t*)s;
    while (*p && !overflow) {
      const uint8_t c = *p;
      if (c == '"' || c == '\\') { ch('\\'); ch((char)c); p++; continue; }
      if (c == '\n') { raw("\\n"); p++; continue; }
      if (c == '\r') { raw("\\r"); p++; continue; }
      if (c == '\t') { raw("\\t"); p++; continue; }
      if (c < 0x20) { unicode(c); p++; continue; }
      if (c < 0x80) { ch((char)c); p++; continue; }

      // UTF-8 -> \uXXXX (BMP). Une sequence invalide est remplacee par U+FFFD
      // plutot que d'emettre un octet >= 0x80 sur le fil.
      uint32_t cp = 0xFFFD;
      if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
        cp = (uint32_t)((c & 0x1F) << 6) | (uint32_t)(p[1] & 0x3F);
        p += 2;
      } else if ((c & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
        cp = (uint32_t)((c & 0x0F) << 12) | (uint32_t)((p[1] & 0x3F) << 6) |
             (uint32_t)(p[2] & 0x3F);
        p += 3;
      } else {
        // 4 octets (hors BMP) ou octet isole : on ne sait pas le representer sur
        // un seul \uXXXX, on saute la sequence.
        p++;
        while ((*p & 0xC0) == 0x80) p++;
      }
      unicode(cp);
    }
    ch('"');
  }

  void unicode(uint32_t cp) {
    static const char* kHex = "0123456789abcdef";
    raw("\\u");
    ch(kHex[(cp >> 12) & 0xF]);
    ch(kHex[(cp >> 8) & 0xF]);
    ch(kHex[(cp >> 4) & 0xF]);
    ch(kHex[cp & 0xF]);
  }

  // "key": — le separateur precedent est a la charge de l'appelant.
  void key(const char* k) { str(k); ch(':'); }
};

// "id" d'une voix : derive de l'identifiant d'actionneur, donc stable d'un
// descripteur au suivant tant que le cablage ne bouge pas (le diff des
// surcharges cote GMB en depend).
void writeMechId(Writer& w, uint8_t actuatorId) {
  w.ch('"');
  w.ch('a');
  w.num(actuatorId);
  w.ch('"');
}

void writeNoteList(Writer& w, const GmbNoteSet& notes) {
  w.raw("{\"mode\":\"discrete\",\"list\":[");
  bool first = true;
  for (uint16_t n = 0; n < 128; n++) {
    if (!notes.has((uint8_t)n)) continue;
    if (!first) w.ch(',');
    w.num(n);
    first = false;
  }
  w.raw("]}");
}

void writeCcList(Writer& w, const GmbNoteSet& ccs) {
  w.ch('[');
  bool first = true;
  for (uint16_t c = 0; c < 128; c++) {
    if (!ccs.has((uint8_t)c)) continue;
    if (!first) w.ch(',');
    w.num(c);
    first = false;
  }
  w.ch(']');
}

void writeVoices(Writer& w, const CapabilitySnapshot& snap, const GmbInstrumentCaps& e) {
  w.raw(",\"voices\":[");
  bool first = true;
  for (uint8_t m = e.mechFirst; m < e.mechFirst + e.mechCount; m++) {
    const GmbMechanism& mech = snap.mechanisms[m];
    // Une voix est une unite physique d'EMISSION (§5.4) : un etouffoir ou une
    // pedale n'en est pas une, ils vivent dans `physical`.
    if ((GmbRole)mech.role != GmbRole::STRIKE) continue;
    if (mech.notes.empty()) continue;
    if (!first) w.ch(',');
    w.raw("{\"id\":");
    writeMechId(w, mech.actuatorId);
    w.raw(",\"notes\":");
    writeNoteList(w, mech.notes);
    if (mech.rearticulationMs > 0) {
      w.raw(",\"rearticulation_ms\":");
      w.num(mech.rearticulationMs);
    }
    w.raw(",\"velocity\":");
    w.boolean((mech.flags & GMB_MECH_VELOCITY) != 0);
    w.ch('}');
    first = false;
  }
  w.ch(']');
}

bool hasStrikeVoice(const CapabilitySnapshot& snap, const GmbInstrumentCaps& e) {
  for (uint8_t m = e.mechFirst; m < e.mechFirst + e.mechCount; m++) {
    const GmbMechanism& mech = snap.mechanisms[m];
    if ((GmbRole)mech.role == GmbRole::STRIKE && !mech.notes.empty()) return true;
  }
  return false;
}

void writePolyphony(Writer& w, const CapabilitySnapshot& snap, const GmbInstrumentCaps& e,
                    bool voicesDeclared) {
  if (e.polyphonyMax == 0 && snap.globalMaxSimultaneous == 0) return;
  w.raw(",\"polyphony\":{");
  bool needComma = false;
  if (e.polyphonyMax > 0) {
    w.raw("\"max\":");
    w.num(e.polyphonyMax);
    needComma = true;
  }
  if (needComma) w.ch(',');
  w.raw("\"constraints\":[");
  bool first = true;
  if (voicesDeclared) {
    w.raw("{\"type\":\"one_note_per_voice\"}");
    first = false;
  }
  if (snap.globalMaxSimultaneous > 0) {
    if (!first) w.ch(',');
    // Le budget electrique est GLOBAL : il plafonne les activations simultanees
    // toutes voix et tous canaux confondus, pas seulement dans cet instrument.
    w.raw("{\"type\":\"max_simultaneous_per_group\",\"group\":\"power_supply\",\"max\":");
    w.num(snap.globalMaxSimultaneous);
    w.ch('}');
  }
  w.raw("]}");
}

void writeTiming(Writer& w, const GmbInstrumentCaps& e) {
  if (e.timing.known == 0) return;
  w.raw(",\"timing\":{");
  bool needComma = false;
  if (e.timing.known & GMB_T_PREPARE) {
    // Positionnement mecanique inaudible pose avant la frappe : GMB peut
    // l'anticiper avec son lookahead, il n'entre pas dans sync_delay.
    // base_ms == max_ms : la duree ne depend pas d'un intervalle de notes comme
    // sur un instrument frette, elle est fixee par le pipeline.
    w.raw("\"prepare\":{\"base_ms\":");
    w.num(e.timing.prepareMaxMs);
    w.raw(",\"max_ms\":");
    w.num(e.timing.prepareMaxMs);
    w.raw(",\"silent\":true}");
    needComma = true;
  }
  if (e.timing.known & GMB_T_EXCITE) {
    if (needComma) w.ch(',');
    w.raw("\"excite\":{\"latency_ms\":");
    w.num(e.timing.exciteLatencyMs);
    w.raw(",\"jitter_ms\":");
    w.num(e.timing.jitterMs);
    w.ch('}');
    needComma = true;
  }
  if (e.timing.known & GMB_T_REARTICULATION) {
    if (needComma) w.ch(',');
    w.raw("\"rearticulation_ms\":");
    w.num(e.timing.rearticulationMs);
    needComma = true;
  }
  if (e.timing.known & GMB_T_RELEASE) {
    if (needComma) w.ch(',');
    w.raw("\"release_ms\":");
    w.num(e.timing.releaseMs);
  }
  w.ch('}');
  // `min_note_ms` est volontairement absent : une frappe se coupe toute seule,
  // la duree de la note MIDI ne change rien au son. Champ absent = inconnu.
}

void writeExpression(Writer& w, const GmbInstrumentCaps& e) {
  w.raw(",\"expression\":{\"cc\":");
  writeCcList(w, e.ccs);
  w.raw(",\"velocity\":");
  w.boolean((e.flags & GMB_INST_VELOCITY) != 0);
  w.raw(",\"channel_aftertouch\":");
  w.boolean((e.flags & GMB_INST_AFTERTOUCH) != 0);
  // Le moteur n'installe aucun gestionnaire d'aftertouch polyphonique.
  w.raw(",\"poly_aftertouch\":false");
  if (e.flags & GMB_INST_PITCHBEND) {
    // `range_semitones` est omis : le moteur mappe le pitch bend sur une
    // position d'actionneur (tension de peau), il ne connait pas l'intervalle
    // musical que cela produit.
    w.raw(",\"pitch_bend\":{\"supported\":true}");
  }
  w.ch('}');
}

void writeMessages(Writer& w, const GmbInstrumentCaps& e) {
  // Capacites semantiques : un `true` signifie qu'un message a un effet reel
  // dans le pipeline compile actif. Les messages realtime restent omis ici tant
  // que le moteur ne fournit pas de preuve fonctionnelle : absence = inconnu
  // cote GMB, donc comportement retrocompatible et permissif.
  w.raw(",\"messages\":{\"note_on\":true,\"note_off\":true");
  w.raw(",\"control_change\":");
  w.boolean(!e.ccs.empty());
  w.raw(",\"program_change\":false");
  w.raw(",\"pitch_bend\":");
  w.boolean((e.flags & GMB_INST_PITCHBEND) != 0);
  w.raw(",\"channel_aftertouch\":");
  w.boolean((e.flags & GMB_INST_AFTERTOUCH) != 0);
  w.raw(",\"poly_aftertouch\":false}");
}

// Extensions percussion (§5.9). Espace de noms libre, ignore par GMB s'il ne le
// connait pas : c'est la place des comportements que les champs generiques ne
// savent pas dire — relation pedale/hi-hat, groupes de choke.
void writePhysical(Writer& w, const CapabilitySnapshot& snap, const GmbInstrumentCaps& e) {
  // Y a-t-il quelque chose a dire ?
  bool hasHihat = (e.hihatPedalCc != 0xFF);
  bool hasDamp = false;
  for (uint8_t m = e.mechFirst; m < e.mechFirst + e.mechCount; m++) {
    const GmbMechanism& mech = snap.mechanisms[m];
    if ((GmbRole)mech.role == GmbRole::DAMP && !mech.notes.empty()) hasDamp = true;
  }
  w.raw(",\"physical\":{\"family\":\"percussion\"");

  if (hasHihat) {
    w.raw(",\"hihat\":{\"pedal_cc\":");
    w.num(e.hihatPedalCc);
    // Notes servies par le controleur de pedale : c'est ce qui relie le CC
    // continu aux articulations ouvert/ferme/pedale. Absent si la pedale n'est
    // rattachee a aucune note — un tableau vide se lirait comme "aucune", ce
    // qui n'est pas la meme chose que "pas d'information".
    for (uint8_t m = e.mechFirst; m < e.mechFirst + e.mechCount; m++) {
      const GmbMechanism& mech = snap.mechanisms[m];
      if ((GmbRole)mech.role != GmbRole::PEDAL || mech.notes.empty()) continue;
      w.raw(",\"notes\":");
      writeNoteList(w, mech.notes);
      break;
    }
    w.ch('}');
  }

  if (hasDamp) {
    // Un etouffoir partage par plusieurs notes EST la relation de choke : les
    // notes du groupe s'interrompent mutuellement, puisqu'un seul mecanisme les
    // etouffe.
    w.raw(",\"choke_groups\":[");
    bool first = true;
    for (uint8_t m = e.mechFirst; m < e.mechFirst + e.mechCount; m++) {
      const GmbMechanism& mech = snap.mechanisms[m];
      if ((GmbRole)mech.role != GmbRole::DAMP || mech.notes.empty()) continue;
      if (!first) w.ch(',');
      w.raw("{\"id\":");
      writeMechId(w, mech.actuatorId);
      w.raw(",\"notes\":");
      writeNoteList(w, mech.notes);
      w.ch('}');
      first = false;
    }
    w.ch(']');
  }
  w.ch('}');
}

void writeInstrument(Writer& w, const CapabilitySnapshot& snap,
                     const GmbInstrumentCaps& e, GmbDetail detail) {
  w.raw("{\"channel\":");
  w.num(e.channel);
  const bool configured = (e.flags & GMB_INST_CONFIGURED) != 0;
  w.raw(",\"configured\":");
  w.boolean(configured);
  if (!configured || detail == GmbDetail::PLACEHOLDER) { w.ch('}'); return; }

  // `type` / `subtype` reprennent les cles textuelles de
  // General-Midi-Boop/src/midi/adaptation/InstrumentTypeConfig.js : `drums` est
  // la famille "Batterie / Percussion", `standard_kit` son sous-type de kit.
  // Ce sont des cles existantes, pas des chaines inventees.
  w.raw(",\"type\":\"drums\",\"subtype\":\"standard_kit\"");
  w.raw(",\"notes\":");
  writeNoteList(w, e.notes);

  if (detail == GmbDetail::MINIMAL) { w.ch('}'); return; }

  const bool voices = (detail == GmbDetail::FULL || detail == GmbDetail::NO_PHYSICAL) &&
                      hasStrikeVoice(snap, e);
  if (voices) writeVoices(w, snap, e);
  writePolyphony(w, snap, e, voices);
  writeTiming(w, e);
  writeExpression(w, e);
  writeMessages(w, e);
  if (detail == GmbDetail::FULL) writePhysical(w, snap, e);
  w.ch('}');
}

// Rendre le document entier. `limit` borne le nombre d'instruments emis (dernier
// recours quand meme le niveau MINIMAL ne tient pas).
bool writeDocument(Writer& w, const CapabilitySnapshot& snap, uint32_t revision,
                   const GmbDeviceInfo& device, GmbDetail detail, uint8_t limit,
                   uint8_t& emitted) {
  emitted = 0;
  w.raw("{\"gmb_descriptor\":2,\"revision\":");
  w.num(revision);
  w.raw(",\"device\":{");
  w.key("name");
  w.str(device.name);
  w.ch(',');
  w.key("model");
  w.str(device.model);
  w.raw("},\"instruments\":[");

  if (snap.instrumentCount == 0) {
    // Le validateur de l'hote exige un tableau NON VIDE
    // (DescriptorProtocol.js). Un moteur sans configuration percussion
    // utilisable declare donc un instrument explicitement non configure sur le
    // canal de percussion : GMB bascule en saisie manuelle sans ecraser ce que
    // l'utilisateur avait deja renseigne (§5.1). C'est exactement le cas "un
    // PlayMode au premier boot".
    w.raw("{\"channel\":");
    w.num(GMB_DEFAULT_CHANNEL);
    w.raw(",\"configured\":false}");
    emitted = 1;
  } else {
    for (uint8_t i = 0; i < snap.instrumentCount && i < limit; i++) {
      if (i > 0) w.ch(',');
      writeInstrument(w, snap, snap.instruments[i], detail);
      emitted++;
    }
  }
  w.raw("]}");
  return !w.overflow;
}

}  // namespace

GmbSerializeResult gmbSerializeDescriptor(char* out, size_t cap,
                                          const CapabilitySnapshot& snap,
                                          uint32_t revision,
                                          const GmbDeviceInfo& device) {
  GmbSerializeResult res{0, GmbDetail::FULL, 0, 0};
  if (!out || cap == 0) return res;

  static const GmbDetail kTiers[] = {
    GmbDetail::FULL, GmbDetail::NO_PHYSICAL, GmbDetail::CORE,
    GmbDetail::MINIMAL, GmbDetail::PLACEHOLDER
  };

  const uint8_t total = (snap.instrumentCount == 0) ? 1 : snap.instrumentCount;

  for (uint8_t t = 0; t < sizeof(kTiers) / sizeof(kTiers[0]); t++) {
    Writer w(out, cap);
    uint8_t emitted = 0;
    if (writeDocument(w, snap, revision, device, kTiers[t], total, emitted)) {
      if (w.len < cap) out[w.len] = '\0';   // confort de debug, hors longueur
      res.length = w.len;
      res.detail = kTiers[t];
      res.instrumentsEmitted = emitted;
      res.instrumentsDropped = 0;
      return res;
    }
  }

  // Dernier recours : reduire le NOMBRE d'instruments au niveau le plus
  // compact. Un canal sacrifie est signale (l'appelant le journalise et
  // l'expose dans la diagnostique) — jamais tu, et jamais au prix d'un JSON
  // invalide.
  for (uint8_t limit = total; limit > 0; limit--) {
    Writer w(out, cap);
    uint8_t emitted = 0;
    if (writeDocument(w, snap, revision, device, GmbDetail::PLACEHOLDER, limit, emitted)) {
      if (w.len < cap) out[w.len] = '\0';
      res.length = w.len;
      res.detail = GmbDetail::PLACEHOLDER;
      res.instrumentsEmitted = emitted;
      res.instrumentsDropped = (uint8_t)(total - emitted);
      return res;
    }
  }
  return res;   // cap est absurdement petit : rien n'est emis, plutot qu'un JSON casse
}