// ============================================================================
// Native unit tests for the EventProcessor — the layer that decides what fires
// ============================================================================
// This is the trunk of the engine and it had no direct test: it held a concrete
// Scheduler, which holds an ActuatorManager, which holds Actuators, which hold
// I2C drivers. Testing "what does a NoteOn actually produce?" therefore required
// the whole hardware stack — so nobody did.
//
// EventProcessor now talks to a CommandSink (scheduler/command_sink.h). Here we
// plug in a recorder instead of a queue, which makes the decisions observable:
// what got scheduled, in what order, and — just as important — what did NOT.
#include <unity.h>
#include <atomic>
#include <vector>

#include "../../src/event/event_processor.cpp"

// The panic latch is an extern owned by main.cpp on the target.
std::atomic<bool> g_panicActive{false};

// ---------------------------------------------------------------------------
// Recording sink
// ---------------------------------------------------------------------------
struct RecordedBatch {
  std::vector<ActionStep> steps;
  uint8_t velocity;
  uint8_t activeGroup;
};

class RecordingSink : public CommandSink {
public:
  std::vector<ActuatorCommand> commands;
  std::vector<RecordedBatch> batches;
  // When false, every scheduling attempt is refused — the saturated-queue case.
  bool accept = true;

  void reset() { commands.clear(); batches.clear(); accept = true; }

  bool scheduleCommand(const ActuatorCommand& cmd) override {
    if (!accept) return false;
    commands.push_back(cmd);
    return true;
  }

  bool schedulePulseAt(uint8_t actuatorId, uint16_t value,
                       uint32_t durationUs, uint32_t executeAt) override {
    if (!accept) return false;
    ActuatorCommand on{};
    on.actuator_id = actuatorId;
    on.command_type = (uint8_t)CommandType::PULSE;
    on.value = value;
    on.duration = (uint16_t)(durationUs / 100);
    on.execute_at = executeAt;
    commands.push_back(on);
    return true;
  }

  bool scheduleActionSteps(const ActionStep* steps, uint8_t stepCount,
                           uint8_t velocity, uint32_t timestamp,
                           const uint16_t* globalVars,
                           uint8_t activeGroup) override {
    (void)timestamp; (void)globalVars;
    if (!accept) return false;
    RecordedBatch b;
    for (uint8_t i = 0; i < stepCount; i++) b.steps.push_back(steps[i]);
    b.velocity = velocity;
    b.activeGroup = activeGroup;
    batches.push_back(b);
    return true;
  }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
static RecordingSink sink;
static EngineState state;
static EventProcessor* proc = nullptr;

static void addPipeline(uint8_t channel, uint8_t note, uint8_t actuatorId,
                        RetriggerMode retrigger = RetriggerMode::IGNORE,
                        uint8_t alternateGroups = 0) {
  PipelineLookup& lk = proc->getLookup();
  uint8_t idx = lk.pipeline_count++;
  CompiledPipeline& p = lk.pipelines[idx];
  p = CompiledPipeline();
  p.midi_note = note;
  p.midi_channel = channel;
  p.retrigger_mode = (uint8_t)retrigger;
  p.alternate_group_count = alternateGroups;
  p.output_actuator_id = actuatorId;

  p.note_on_count = 1;
  p.note_on_actions[0].actuator_id = actuatorId;
  p.note_on_actions[0].command_type = (uint8_t)CommandType::PULSE;
  p.note_on_actions[0].value_source = (uint8_t)ValueSource::VELOCITY;

  p.note_off_count = 1;
  p.note_off_actions[0].actuator_id = actuatorId;
  p.note_off_actions[0].command_type = (uint8_t)CommandType::OFF;
  p.note_off_actions[0].value_source = (uint8_t)ValueSource::FIXED;

  lk.note_to_pipeline[channel - 1][note] = idx;
}

static MidiEvent noteOn(uint8_t ch, uint8_t note, uint8_t vel, uint32_t ts = 1000) {
  MidiEvent e{};
  e.type = MIDI_EVT_NOTE_ON; e.channel = ch; e.data1 = note; e.data2 = vel;
  e.timestamp = ts;
  return e;
}
static MidiEvent noteOff(uint8_t ch, uint8_t note, uint32_t ts = 2000) {
  MidiEvent e{};
  e.type = MIDI_EVT_NOTE_OFF; e.channel = ch; e.data1 = note; e.data2 = 64;
  e.timestamp = ts;
  return e;
}
static MidiEvent ccEvent(uint8_t ch, uint8_t cc, uint8_t val, uint32_t ts) {
  MidiEvent e{};
  e.type = MIDI_EVT_CC; e.channel = ch; e.data1 = cc; e.data2 = val;
  e.timestamp = ts;
  return e;
}

static uint32_t batchCount() { return (uint32_t)sink.batches.size(); }

static uint8_t activeFor(uint8_t note) {
  uint8_t notes[128];
  proc->getNoteActive(notes);
  return notes[note];
}

void setUp() {
  sink.reset();
  state = EngineState();
  g_panicActive.store(false);
  static EventProcessor instance(&sink, &state);
  proc = &instance;
  proc->getLookup() = PipelineLookup();
  proc->panicReset();
  proc->recompileLookup();
}
void tearDown() {}

// --- Routage de base ------------------------------------------------------

void test_noteon_on_mapped_channel_schedules_its_actions() {
  addPipeline(10, 36, 7);
  proc->processMidiEvent(noteOn(10, 36, 100));

  TEST_ASSERT_EQUAL_UINT32(1, batchCount());
  TEST_ASSERT_EQUAL_UINT8(7, sink.batches[0].steps[0].actuator_id);
  TEST_ASSERT_EQUAL_UINT8(100, sink.batches[0].velocity);
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));
}

void test_noteon_on_unmapped_channel_schedules_nothing() {
  addPipeline(10, 36, 7);
  proc->processMidiEvent(noteOn(1, 36, 100));
  proc->processMidiEvent(noteOn(11, 36, 100));

  TEST_ASSERT_EQUAL_UINT32(0, batchCount());
  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
}

void test_same_note_two_channels_are_independent() {
  addPipeline(10, 36, 7);
  addPipeline(11, 36, 9);

  proc->processMidiEvent(noteOn(11, 36, 90));
  TEST_ASSERT_EQUAL_UINT32(1, batchCount());
  TEST_ASSERT_EQUAL_UINT8(9, sink.batches[0].steps[0].actuator_id);

  // Releasing on channel 11 must not release channel 10 (which never fired).
  proc->processMidiEvent(noteOff(10, 36));
  TEST_ASSERT_EQUAL_UINT32(1, batchCount());
}

void test_out_of_range_note_is_ignored() {
  addPipeline(10, 36, 7);
  MidiEvent e = noteOn(10, 36, 100);
  e.data1 = 200;                      // /api/test/note can inject a raw uint8_t
  proc->processMidiEvent(e);
  TEST_ASSERT_EQUAL_UINT32(0, batchCount());
}

// --- Retrigger ------------------------------------------------------------

void test_retrigger_ignore_drops_the_second_hit() {
  addPipeline(10, 36, 7, RetriggerMode::IGNORE);
  proc->processMidiEvent(noteOn(10, 36, 100));
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT32(1, batchCount());
}

void test_retrigger_reset_releases_then_refires() {
  addPipeline(10, 36, 7, RetriggerMode::RESET);
  proc->processMidiEvent(noteOn(10, 36, 100));
  proc->processMidiEvent(noteOn(10, 36, 110));
  // release + re-strike on top of the first strike
  TEST_ASSERT_EQUAL_UINT32(3, batchCount());
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));
}

void test_retrigger_stack_counts_instances_and_releases_once() {
  addPipeline(10, 36, 7, RetriggerMode::STACK);
  proc->processMidiEvent(noteOn(10, 36, 100));
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT8(2, activeFor(36));

  proc->processMidiEvent(noteOff(10, 36));      // one instance left, no release
  TEST_ASSERT_EQUAL_UINT32(2, batchCount());
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));

  proc->processMidiEvent(noteOff(10, 36));      // last one releases
  TEST_ASSERT_EQUAL_UINT32(3, batchCount());
  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
}

void test_spurious_noteoff_is_ignored() {
  addPipeline(10, 36, 7);
  proc->processMidiEvent(noteOff(10, 36));
  TEST_ASSERT_EQUAL_UINT32(0, batchCount());
}

void test_noteon_velocity_zero_is_a_release() {
  addPipeline(10, 36, 7);
  proc->processMidiEvent(noteOn(10, 36, 100));
  proc->processMidiEvent(noteOn(10, 36, 0));    // NoteOn vel=0 == NoteOff
  TEST_ASSERT_EQUAL_UINT32(2, batchCount());
  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
}

// --- Refus de lot : la propriete la plus recemment changee ----------------

// Un evenement refuse ne doit RIEN laisser derriere lui : ni note active, ni
// tour de round-robin consomme.
void test_rejected_batch_leaves_no_trace() {
  addPipeline(10, 36, 7);
  sink.accept = false;

  proc->processMidiEvent(noteOn(10, 36, 100));

  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
  TEST_ASSERT_EQUAL_UINT32(1, proc->getCcDispatchStats().rejected_batches);

  // La note n'etant pas active, un NoteOff est spurious et ne joue rien.
  sink.accept = true;
  proc->processMidiEvent(noteOff(10, 36));
  TEST_ASSERT_EQUAL_UINT32(0, batchCount());

  // Et la frappe suivante part normalement.
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT32(1, batchCount());
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));
}

// Le round-robin ne doit avancer qu'apres acceptation : sinon une frappe
// refusee sauterait un striker a la frappe suivante.
void test_round_robin_does_not_advance_on_rejection() {
  addPipeline(10, 36, 7, RetriggerMode::STACK, /*alternateGroups=*/2);

  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT8(1, sink.batches[0].activeGroup);   // groupe 1

  sink.accept = false;
  proc->processMidiEvent(noteOn(10, 36, 100));               // refusee
  sink.accept = true;

  proc->processMidiEvent(noteOn(10, 36, 100));
  // Sans la correction, ce serait le groupe 1 : le tour aurait ete consomme
  // par l'evenement refuse et le second striker saute.
  TEST_ASSERT_EQUAL_UINT8(2, sink.batches[1].activeGroup);
}

void test_round_robin_cycles_on_success() {
  addPipeline(10, 36, 7, RetriggerMode::STACK, /*alternateGroups=*/2);
  for (uint8_t i = 0; i < 4; i++) proc->processMidiEvent(noteOn(10, 36, 100));

  TEST_ASSERT_EQUAL_UINT32(4, batchCount());
  TEST_ASSERT_EQUAL_UINT8(1, sink.batches[0].activeGroup);
  TEST_ASSERT_EQUAL_UINT8(2, sink.batches[1].activeGroup);
  TEST_ASSERT_EQUAL_UINT8(1, sink.batches[2].activeGroup);
  TEST_ASSERT_EQUAL_UINT8(2, sink.batches[3].activeGroup);
}

// Un RESET dont la release est refusee ne doit pas re-declencher par-dessus une
// note toujours physiquement active.
void test_reset_retrigger_aborts_when_release_is_rejected() {
  addPipeline(10, 36, 7, RetriggerMode::RESET);
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));

  sink.accept = false;
  proc->processMidiEvent(noteOn(10, 36, 100));
  sink.accept = true;

  // La note reste comptee active : on n'a pas oublie qu'elle est en cours.
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));
  TEST_ASSERT_EQUAL_UINT32(1, proc->getCcDispatchStats().rejected_batches);
}

// Un NoteOff refuse doit laisser le compteur a 1 pour qu'un nouveau NoteOff
// reessaie, au lieu de laisser l'actionneur sous tension avec un compteur a 0.
void test_rejected_noteoff_stays_retryable() {
  addPipeline(10, 36, 7);
  proc->processMidiEvent(noteOn(10, 36, 100));

  sink.accept = false;
  proc->processMidiEvent(noteOff(10, 36));
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));   // pas oublie

  sink.accept = true;
  proc->processMidiEvent(noteOff(10, 36));
  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
}

// --- Arret d'urgence ------------------------------------------------------

void test_panic_blocks_noteon_but_allows_release() {
  addPipeline(10, 36, 7);
  proc->processMidiEvent(noteOn(10, 36, 100));
  uint32_t before = batchCount();

  g_panicActive.store(true);
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT32(before, batchCount());     // aucune activation

  proc->processMidiEvent(noteOff(10, 36));            // la release passe
  TEST_ASSERT_EQUAL_UINT32(before + 1, batchCount());
  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
}

void test_panic_reset_clears_note_counters() {
  addPipeline(10, 36, 7, RetriggerMode::IGNORE);
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT8(1, activeFor(36));

  proc->panicReset();
  TEST_ASSERT_EQUAL_UINT8(0, activeFor(36));
  // IGNORE ne bloque plus la frappe suivante apres re-armement.
  proc->processMidiEvent(noteOn(10, 36, 100));
  TEST_ASSERT_EQUAL_UINT32(2, batchCount());
}

// --- CC -------------------------------------------------------------------

static void addCcRoute(uint8_t cc, uint8_t channel, uint8_t actuatorId) {
  PipelineLookup& lk = proc->getLookup();
  uint8_t idx = lk.cc_route_count++;
  CCRoutingEntry& r = lk.cc_routes[idx];
  r = CCRoutingEntry();
  r.actuator_id = actuatorId;
  r.command_type = (uint8_t)CommandType::POSITION;
  r.channel = channel;
  if (lk.cc_to_first[cc] == 0xFF) lk.cc_to_first[cc] = idx;
  lk.cc_to_count[cc]++;
}

void test_cc_only_reaches_routes_on_its_channel() {
  addCcRoute(4, 10, 7);
  proc->processMidiEvent(ccEvent(11, 4, 100, 10000));
  TEST_ASSERT_EQUAL_UINT32(0, sink.commands.size());

  proc->processMidiEvent(ccEvent(10, 4, 100, 20000));
  TEST_ASSERT_EQUAL_UINT32(1, sink.commands.size());
  TEST_ASSERT_EQUAL_UINT8(7, sink.commands[0].actuator_id);
}

void test_omni_cc_route_answers_on_any_channel() {
  addCcRoute(4, 0, 7);                                  // 0 = OMNI
  proc->processMidiEvent(ccEvent(3, 4, 100, 10000));
  TEST_ASSERT_EQUAL_UINT32(1, sink.commands.size());
}

// Un flood non route sur un autre canal ne doit pas affamer le canal qui, lui,
// a une route : c'est pour ca que l'horodatage n'est pose qu'apres avoir trouve
// une route correspondante.
void test_unrouted_flood_on_another_channel_does_not_starve() {
  addCcRoute(4, 10, 7);
  for (uint32_t t = 0; t < 10; t++) {
    proc->processMidiEvent(ccEvent(1, 4, 60, 10000 + t * 100));   // ch1: pas de route
  }
  TEST_ASSERT_EQUAL_UINT32(0, sink.commands.size());

  proc->processMidiEvent(ccEvent(10, 4, 100, 10500));   // dans la fenetre de 5 ms
  TEST_ASSERT_EQUAL_UINT32(1, sink.commands.size());
}

void test_cc_throttle_limits_a_routed_flood() {
  addCcRoute(4, 10, 7);
  proc->processMidiEvent(ccEvent(10, 4, 10, 100000));
  proc->processMidiEvent(ccEvent(10, 4, 20, 101000));   // +1 ms -> throttle
  proc->processMidiEvent(ccEvent(10, 4, 30, 108000));   // +8 ms -> passe

  TEST_ASSERT_EQUAL_UINT32(2, sink.commands.size());
  TEST_ASSERT_EQUAL_UINT32(1, proc->getCcDispatchStats().throttled);
}

void test_cc_on_invalid_channel_is_dropped() {
  addCcRoute(4, 0, 7);
  proc->processMidiEvent(ccEvent(0, 4, 100, 10000));    // 0 n'est pas un canal du fil
  proc->processMidiEvent(ccEvent(17, 4, 100, 20000));
  TEST_ASSERT_EQUAL_UINT32(0, sink.commands.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_noteon_on_mapped_channel_schedules_its_actions);
  RUN_TEST(test_noteon_on_unmapped_channel_schedules_nothing);
  RUN_TEST(test_same_note_two_channels_are_independent);
  RUN_TEST(test_out_of_range_note_is_ignored);
  RUN_TEST(test_retrigger_ignore_drops_the_second_hit);
  RUN_TEST(test_retrigger_reset_releases_then_refires);
  RUN_TEST(test_retrigger_stack_counts_instances_and_releases_once);
  RUN_TEST(test_spurious_noteoff_is_ignored);
  RUN_TEST(test_noteon_velocity_zero_is_a_release);
  RUN_TEST(test_rejected_batch_leaves_no_trace);
  RUN_TEST(test_round_robin_does_not_advance_on_rejection);
  RUN_TEST(test_round_robin_cycles_on_success);
  RUN_TEST(test_reset_retrigger_aborts_when_release_is_rejected);
  RUN_TEST(test_rejected_noteoff_stays_retryable);
  RUN_TEST(test_panic_blocks_noteon_but_allows_release);
  RUN_TEST(test_panic_reset_clears_note_counters);
  RUN_TEST(test_cc_only_reaches_routes_on_its_channel);
  RUN_TEST(test_omni_cc_route_answers_on_any_channel);
  RUN_TEST(test_unrouted_flood_on_another_channel_does_not_starve);
  RUN_TEST(test_cc_throttle_limits_a_routed_flood);
  RUN_TEST(test_cc_on_invalid_channel_is_dropped);
  return UNITY_END();
}
