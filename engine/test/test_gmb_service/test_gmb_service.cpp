// ============================================================================
// Tests natifs du service GMB : descripteur, transfert 0x10, revision, 0x11
// ============================================================================
// On verifie ici les proprietes dont depend l'hote :
//   - le descripteur est du JSON ASCII valide, y compris sans configuration ;
//   - le transfert segmente se reassemble a l'octet, dans n'importe quel ordre,
//     et un index hors bornes ne casse rien ;
//   - la revision n'augmente QUE si les capacites changent reellement, et elle
//     ne bouge pas au redemarrage a configuration egale ;
//   - le trafic de decouverte ne planifie aucune frappe et ne touche pas au
//     moteur temps reel.
#include <unity.h>
#include <atomic>
#include <string>
#include <vector>

#include "gmb_test_fixture.h"
#include "../../src/gmb/gmb_sysex.cpp"
#include "../../src/gmb/gmb_identity.cpp"
#include "../../src/gmb/gmb_capabilities.cpp"
#include "../../src/gmb/gmb_descriptor.cpp"
#include "../../src/gmb/gmb_sysex_service.cpp"
#include "../../src/event/event_processor.cpp"

// Le verrou d'arret d'urgence appartient a main.cpp sur la cible.
std::atomic<bool> g_panicActive{false};

static GmbFixture fx;
static GmbSysExService svc;

void setUp() { fx.reset(); }
void tearDown() {}

static GmbBuildInputs inputs() {
  GmbBuildInputs in;
  in.lookup = &fx.lookup;
  in.actuatorLookup = GmbFixture::resolve;
  in.actuatorCtx = &fx;
  in.channelMask = 0xFFFF;
  in.power.maxConcurrent = 8;
  return in;
}

static std::string descriptorOf(const GmbSysExService& s) {
  return std::string(s.descriptor(), s.descriptorSize());
}

// Demander un segment et renvoyer sa charge utile, ou "" si pas de reponse.
static bool requestChunk(uint16_t index, uint32_t nowMs,
                         uint16_t& total, uint16_t& gotIndex, std::string& payload) {
  uint8_t req[8] = {0xF0, 0x7D, 0x00, 0x10, 0x00, 0, 0, 0xF7};
  gmbEncode14(index, &req[5]);
  uint8_t out[GmbSysExService::MAX_RESPONSE];
  const size_t n = svc.handleSysEx(req, sizeof(req), out, sizeof(out), nowMs);
  if (n == 0) return false;
  TEST_ASSERT_EQUAL_UINT8(0x10, out[3]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[4]);
  TEST_ASSERT_EQUAL_UINT8(0xF7, out[n - 1]);
  total = gmbDecode14(&out[5]);
  gotIndex = gmbDecode14(&out[7]);
  payload.assign((const char*)&out[9], n - 10);
  return true;
}

// ---------------------------------------------------------------------------
// Descripteur
// ---------------------------------------------------------------------------
void test_descriptor_declares_version_two_and_the_configured_notes() {
  fx.buildGmKit(10);
  svc.begin(0x1234ABCD, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  const std::string d = descriptorOf(svc);
  TEST_ASSERT_TRUE(d.find("\"gmb_descriptor\":2") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"channel\":9") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"configured\":true") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"mode\":\"discrete\",\"list\":[36,38,42,46,49]") != std::string::npos);
  // Vocabulaire de InstrumentTypeConfig.js, pas une chaine inventee.
  TEST_ASSERT_TRUE(d.find("\"type\":\"drums\"") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"subtype\":\"standard_kit\"") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"model\":\"Drums-Engine-GMB\"") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"expression\":{\"cc\":[4]") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"family\":\"percussion\"") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"pedal_cc\":4") != std::string::npos);
}

void test_descriptor_is_pure_ascii_and_balanced() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  const std::string d = descriptorOf(svc);
  int braces = 0, brackets = 0, quotes = 0;
  for (char c : d) {
    TEST_ASSERT_TRUE_MESSAGE((unsigned char)c < 0x80, "le descripteur doit rester ASCII");
    if (c == '{') braces++;
    if (c == '}') braces--;
    if (c == '[') brackets++;
    if (c == ']') brackets--;
    if (c == '"') quotes++;
    TEST_ASSERT_TRUE(braces >= 0);
    TEST_ASSERT_TRUE(brackets >= 0);
  }
  TEST_ASSERT_EQUAL_INT(0, braces);
  TEST_ASSERT_EQUAL_INT(0, brackets);
  TEST_ASSERT_EQUAL_INT(0, quotes % 2);
}

void test_no_configuration_still_produces_a_valid_descriptor() {
  // Le validateur de l'hote refuse un tableau `instruments` vide. Un moteur sans
  // percussion utilisable declare donc un instrument explicitement NON
  // configure : GMB repasse en saisie manuelle sans rien ecraser.
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  const std::string d = descriptorOf(svc);
  TEST_ASSERT_TRUE(svc.descriptorSize() > 0);
  TEST_ASSERT_TRUE(d.find("\"instruments\":[{") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"configured\":false") != std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"configured\":true") == std::string::npos);
  TEST_ASSERT_TRUE(d.find("\"notes\"") == std::string::npos);
  // Et le handshake annonce bien un descripteur non vide, donc niveau 1.
  TEST_ASSERT_TRUE(svc.totalChunks() >= 1);
}

void test_descriptor_cache_is_a_valid_c_string() {
  // La route HTTP passe le cache a Arduino String : il doit toujours etre
  // termine par un NUL, et descriptorSize() doit en etre la longueur exacte.
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  TEST_ASSERT_EQUAL_size_t(svc.descriptorSize(), strlen(svc.descriptor()));
  TEST_ASSERT_TRUE(svc.descriptorSize() < GMB_DESCRIPTOR_MAX);
  TEST_ASSERT_EQUAL_CHAR('\0', svc.descriptor()[svc.descriptorSize()]);
}

void test_descriptor_size_matches_the_handshake_field() {
  fx.buildGmKit(10);
  svc.begin(0xDEADBEEF, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  const uint8_t req[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  uint8_t out[GmbSysExService::MAX_RESPONSE];
  const size_t n = svc.handleSysEx(req, sizeof(req), out, sizeof(out), 10);
  TEST_ASSERT_EQUAL_size_t(GMB_HANDSHAKE_SIZE, n);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)svc.descriptorSize(), gmbDecode21(&out[14]));
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, gmbDecode32(&out[6]));
  TEST_ASSERT_EQUAL_UINT32(svc.revision(), gmbDecode32(&out[17]));
  TEST_ASSERT_EQUAL_UINT8(2, out[5]);
  // Notifications push toujours annoncees ; HTTP seulement si la route existe.
  TEST_ASSERT_TRUE((out[22] & GMB_FLAG_PUSH) != 0);
  TEST_ASSERT_TRUE((out[22] & GMB_FLAG_HTTP) == 0);
  svc.setHttpAvailable(true);
  svc.handleSysEx(req, sizeof(req), out, sizeof(out), 20);
  TEST_ASSERT_TRUE((out[22] & GMB_FLAG_HTTP) != 0);
}

// ---------------------------------------------------------------------------
// Bloc 0x10 — transfert segmente
// ---------------------------------------------------------------------------
void test_single_chunk_descriptor_is_served_whole() {
  // Une configuration minuscule tient dans un seul segment.
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(a, 8, 25));
  fx.finish();
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  uint16_t total = 0, idx = 0;
  std::string payload;
  TEST_ASSERT_TRUE(requestChunk(0, 10, total, idx, payload));
  TEST_ASSERT_EQUAL_UINT16(svc.totalChunks(), total);
  TEST_ASSERT_EQUAL_UINT16(0, idx);
  if (total == 1) TEST_ASSERT_EQUAL_STRING(descriptorOf(svc).c_str(), payload.c_str());
}

void test_multi_chunk_descriptor_reassembles_byte_for_byte() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  const uint16_t total = svc.totalChunks();
  TEST_ASSERT_TRUE_MESSAGE(total > 1, "le kit de test doit tenir sur plusieurs segments");

  std::string reassembled;
  for (uint16_t i = 0; i < total; i++) {
    uint16_t t = 0, idx = 0;
    std::string payload;
    TEST_ASSERT_TRUE(requestChunk(i, 100 + i, t, idx, payload));
    TEST_ASSERT_EQUAL_UINT16(total, t);
    TEST_ASSERT_EQUAL_UINT16(i, idx);
    TEST_ASSERT_TRUE(payload.size() >= 1);
    TEST_ASSERT_TRUE(payload.size() <= GMB_CHUNK_PAYLOAD_MAX);
    // Tous les segments sauf le dernier sont pleins : le decoupage est
    // deterministe, donc un segment redemande est identique.
    if (i + 1 < total) TEST_ASSERT_EQUAL_size_t(GMB_CHUNK_PAYLOAD_MAX, payload.size());
    reassembled += payload;
  }
  TEST_ASSERT_EQUAL_STRING(descriptorOf(svc).c_str(), reassembled.c_str());
}

void test_first_middle_and_last_chunks_can_be_requested_in_any_order() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  const uint16_t total = svc.totalChunks();
  const uint16_t middle = (uint16_t)(total / 2);

  uint16_t t = 0, idx = 0;
  std::string last, mid, first;
  TEST_ASSERT_TRUE(requestChunk((uint16_t)(total - 1), 10, t, idx, last));
  TEST_ASSERT_EQUAL_UINT16(total - 1, idx);
  TEST_ASSERT_TRUE(requestChunk(middle, 11, t, idx, mid));
  TEST_ASSERT_EQUAL_UINT16(middle, idx);
  TEST_ASSERT_TRUE(requestChunk(0, 12, t, idx, first));
  TEST_ASSERT_EQUAL_UINT16(0, idx);

  const std::string d = descriptorOf(svc);
  TEST_ASSERT_EQUAL_STRING(d.substr(0, first.size()).c_str(), first.c_str());
  TEST_ASSERT_EQUAL_STRING(d.substr((size_t)middle * GMB_CHUNK_PAYLOAD_MAX, mid.size()).c_str(),
                           mid.c_str());
  TEST_ASSERT_EQUAL_STRING(d.substr((size_t)(total - 1) * GMB_CHUNK_PAYLOAD_MAX).c_str(),
                           last.c_str());
}

void test_repeating_a_chunk_request_returns_the_same_bytes() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  uint16_t t1 = 0, i1 = 0, t2 = 0, i2 = 0;
  std::string a, b;
  TEST_ASSERT_TRUE(requestChunk(1, 10, t1, i1, a));
  TEST_ASSERT_TRUE(requestChunk(1, 11, t2, i2, b));
  TEST_ASSERT_EQUAL_UINT16(t1, t2);
  TEST_ASSERT_EQUAL_UINT16(i1, i2);
  TEST_ASSERT_EQUAL_STRING(a.c_str(), b.c_str());
}

void test_out_of_range_chunk_gets_no_answer_and_is_counted() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  const uint32_t before = svc.diagnostics().outOfRangeChunks;

  uint16_t t = 0, idx = 0;
  std::string payload;
  // Repondre un segment replie ou vide ferait diverger le reassemblage cote
  // hote : ne rien repondre le laisse expirer proprement.
  TEST_ASSERT_FALSE(requestChunk(svc.totalChunks(), 10, t, idx, payload));
  TEST_ASSERT_FALSE(requestChunk(16000, 11, t, idx, payload));
  TEST_ASSERT_EQUAL_UINT32(before + 2, svc.diagnostics().outOfRangeChunks);
  // Et le descripteur reste servable juste apres.
  TEST_ASSERT_TRUE(requestChunk(0, 12, t, idx, payload));
}

void test_malformed_request_is_counted_and_answered_with_nothing() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  uint8_t out[GmbSysExService::MAX_RESPONSE];
  const uint8_t bad[] = {0xF0, 0x7D, 0x00, 0x10, 0x00, 0x03, 0xF7};   // index tronque
  TEST_ASSERT_EQUAL_size_t(0, svc.handleSysEx(bad, sizeof(bad), out, sizeof(out), 10));
  TEST_ASSERT_EQUAL_UINT32(1, svc.diagnostics().invalidPackets);

  const uint8_t foreign[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
  TEST_ASSERT_EQUAL_size_t(0, svc.handleSysEx(foreign, sizeof(foreign), out, sizeof(out), 11));
  TEST_ASSERT_EQUAL_UINT32(1, svc.diagnostics().invalidPackets);  // toujours 1
}

void test_serving_chunks_never_rebuilds_the_descriptor() {
  // C'est la propriete qui garantit qu'aucun JSON n'est genere pendant le
  // fonctionnement temps reel, et que l'hote ne peut pas melanger deux versions
  // du profil au milieu d'un transfert.
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  const uint32_t rebuilds = svc.diagnostics().rebuilds;
  const uint32_t revision = svc.revision();
  const std::string before = descriptorOf(svc);

  for (uint16_t i = 0; i < svc.totalChunks(); i++) {
    uint16_t t = 0, idx = 0;
    std::string payload;
    requestChunk(i, 10 + i, t, idx, payload);
  }
  const uint8_t hs[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  uint8_t out[GmbSysExService::MAX_RESPONSE];
  svc.handleSysEx(hs, sizeof(hs), out, sizeof(out), 100);

  TEST_ASSERT_EQUAL_UINT32(rebuilds, svc.diagnostics().rebuilds);
  TEST_ASSERT_EQUAL_UINT32(revision, svc.revision());
  TEST_ASSERT_EQUAL_STRING(before.c_str(), descriptorOf(svc).c_str());
}

// ---------------------------------------------------------------------------
// Revision et notification
// ---------------------------------------------------------------------------
void test_first_build_sets_a_revision_and_asks_to_persist_but_does_not_notify() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  TEST_ASSERT_TRUE(svc.rebuild(in, 0));
  TEST_ASSERT_EQUAL_UINT32(1, svc.revision());
  TEST_ASSERT_TRUE(svc.persistPending());
  // Au demarrage, GMB lit le bloc 1 a la connexion : pousser une notification
  // que personne n'ecoute n'apporte rien.
  TEST_ASSERT_FALSE(svc.notificationPending());
}

void test_changing_a_note_mapping_bumps_the_revision_and_notifies() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  svc.markPersisted();
  const uint32_t rev = svc.revision();
  const std::string before = descriptorOf(svc);

  // La caisse claire passe de 38 a 40 (Electric Snare).
  for (uint8_t p = 0; p < fx.lookup.pipeline_count; p++) {
    if (fx.lookup.pipelines[p].midi_note == 38) fx.lookup.pipelines[p].midi_note = 40;
  }
  fx.finish();
  TEST_ASSERT_TRUE(svc.rebuild(in, 500));

  TEST_ASSERT_EQUAL_UINT32(rev + 1, svc.revision());
  TEST_ASSERT_TRUE(svc.persistPending());
  TEST_ASSERT_TRUE(svc.notificationPending());
  const std::string after = descriptorOf(svc);
  TEST_ASSERT_TRUE(after != before);
  TEST_ASSERT_TRUE(after.find("[36,40,42,46,49]") != std::string::npos);

  // La trame 0x11 porte la nouvelle revision et le drapeau instruments.
  uint8_t frame[GMB_NOTIFICATION_SIZE];
  const size_t n = svc.takeNotification(frame, sizeof(frame), 500);
  TEST_ASSERT_EQUAL_size_t(GMB_NOTIFICATION_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(0x11, frame[3]);
  TEST_ASSERT_EQUAL_UINT8(0x02, frame[4]);
  TEST_ASSERT_EQUAL_UINT32(svc.revision(), gmbDecode32(&frame[5]));
  TEST_ASSERT_TRUE((frame[10] & GMB_CHANGE_INSTRUMENTS) != 0);
  // Consommee une fois, pas deux.
  TEST_ASSERT_FALSE(svc.notificationPending());
  TEST_ASSERT_EQUAL_size_t(0, svc.takeNotification(frame, sizeof(frame), 501));
}

void test_a_timing_change_sets_the_timing_flag() {
  const uint8_t a = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE, 8, 25, 200);
  GmbFixture::addNoteOn(fx.addPipeline(10, 36), GmbFixture::strike(a, 8, 25));
  fx.finish();
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  svc.markPersisted();

  fx.actuators[0].cooldownUs = 600;   // 60 ms de reaticulation au lieu de 20
  TEST_ASSERT_TRUE(svc.rebuild(in, 100));
  uint8_t frame[GMB_NOTIFICATION_SIZE];
  TEST_ASSERT_EQUAL_size_t(GMB_NOTIFICATION_SIZE, svc.takeNotification(frame, sizeof(frame), 100));
  TEST_ASSERT_TRUE((frame[10] & GMB_CHANGE_TIMING) != 0);
}

void test_a_change_that_does_not_affect_capabilities_bumps_nothing() {
  // Recablage, position de repos, priorite d'arbitrage, nom de module : rien de
  // tout cela ne change ce que la machine sait jouer en MIDI.
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  svc.markPersisted();
  const uint32_t rev = svc.revision();
  const uint32_t hash = svc.stateHash();
  const std::string before = descriptorOf(svc);

  fx.actuators[0].hwPin = 7;
  fx.actuators[0].hwAddress = 0x21;
  fx.actuators[0].paramDefault = 99;
  fx.actuators[0].priority = (uint8_t)ActuatorPriority::PRIO_HIGH;
  fx.actuators[0].inverted = true;
  strcpy(fx.actuators[0].name, "Grosse caisse (recablee)");

  TEST_ASSERT_FALSE(svc.rebuild(in, 900));
  TEST_ASSERT_EQUAL_UINT32(rev, svc.revision());
  TEST_ASSERT_EQUAL_UINT32(hash, svc.stateHash());
  TEST_ASSERT_FALSE(svc.persistPending());
  TEST_ASSERT_FALSE(svc.notificationPending());
  TEST_ASSERT_EQUAL_STRING(before.c_str(), descriptorOf(svc).c_str());
}

void test_a_reboot_on_an_unchanged_configuration_keeps_the_revision() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  const uint32_t revision = svc.revision();
  const uint32_t hash = svc.stateHash();

  // Redemarrage : le service repart de la revision et de l'empreinte persistees.
  GmbSysExService rebooted;
  rebooted.begin(1, revision, hash);
  TEST_ASSERT_FALSE(rebooted.rebuild(in, 0));
  TEST_ASSERT_EQUAL_UINT32(revision, rebooted.revision());
  TEST_ASSERT_FALSE(rebooted.persistPending());
  TEST_ASSERT_FALSE(rebooted.notificationPending());
  // Et le descripteur reconstruit porte bien la revision relue.
  TEST_ASSERT_TRUE(descriptorOf(rebooted).find("\"revision\":" + std::to_string(revision)) !=
                   std::string::npos);
}

void test_a_reboot_after_an_offline_edit_moves_the_revision_forward() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  const uint32_t revision = svc.revision();
  const uint32_t hash = svc.stateHash();

  // Le fichier a change entre-temps : au boot suivant, la revision AVANCE.
  // Elle ne recule jamais, ce qui est la seule propriete dont GMB depend pour
  // ne pas manquer une mise a jour.
  const uint8_t extra = fx.addActuator(ActuatorType::SOLENOID, ActuatorBehavior::SOLENOID_STRIKE);
  GmbFixture::addNoteOn(fx.addPipeline(10, 45), GmbFixture::strike(extra, 8, 25));
  fx.finish();

  GmbSysExService rebooted;
  rebooted.begin(1, revision, hash);
  TEST_ASSERT_TRUE(rebooted.rebuild(in, 0));
  TEST_ASSERT_EQUAL_UINT32(revision + 1, rebooted.revision());
  TEST_ASSERT_TRUE(rebooted.persistPending());
  TEST_ASSERT_FALSE(rebooted.notificationPending());   // premier build apres boot
}

void test_disabling_an_instrument_changes_the_capabilities() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);
  svc.markPersisted();
  const uint32_t rev = svc.revision();

  fx.disableActuator(0);   // la grosse caisse
  TEST_ASSERT_TRUE(svc.rebuild(in, 200));
  TEST_ASSERT_EQUAL_UINT32(rev + 1, svc.revision());
  TEST_ASSERT_TRUE(descriptorOf(svc).find("[38,42,46,49]") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Limiteur de debit
// ---------------------------------------------------------------------------
void test_a_sysex_flood_is_dropped_once_the_bucket_is_empty() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  const uint8_t hs[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  uint8_t out[GmbSysExService::MAX_RESPONSE];
  uint16_t answered = 0;
  for (uint16_t i = 0; i < GmbSysExService::RATE_BURST + 20; i++) {
    if (svc.handleSysEx(hs, sizeof(hs), out, sizeof(out), 0) > 0) answered++;
  }
  TEST_ASSERT_EQUAL_UINT16(GmbSysExService::RATE_BURST, answered);
  TEST_ASSERT_EQUAL_UINT32(20, svc.diagnostics().rateLimited);

  // Une seconde plus tard, le seau s'est recharge de RATE_PER_SEC jetons.
  answered = 0;
  for (uint16_t i = 0; i < GmbSysExService::RATE_PER_SEC + 5; i++) {
    if (svc.handleSysEx(hs, sizeof(hs), out, sizeof(out), 1000) > 0) answered++;
  }
  TEST_ASSERT_EQUAL_UINT16(GmbSysExService::RATE_PER_SEC, answered);
}

void test_a_full_descriptor_transfer_fits_inside_the_burst() {
  // Le limiteur protege le plan de controle ; il ne doit pas empecher un
  // transfert legitime, qui arrive en rafale par nature.
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  TEST_ASSERT_TRUE(svc.totalChunks() + 1 <= GmbSysExService::RATE_BURST);
  const uint8_t hs[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  uint8_t out[GmbSysExService::MAX_RESPONSE];
  TEST_ASSERT_TRUE(svc.handleSysEx(hs, sizeof(hs), out, sizeof(out), 0) > 0);
  for (uint16_t i = 0; i < svc.totalChunks(); i++) {
    uint16_t t = 0, idx = 0;
    std::string payload;
    TEST_ASSERT_TRUE(requestChunk(i, 0, t, idx, payload));
  }
  TEST_ASSERT_EQUAL_UINT32(0, svc.diagnostics().rateLimited);
}

// ---------------------------------------------------------------------------
// Non-regression temps reel
// ---------------------------------------------------------------------------
class RecordingSink : public CommandSink {
public:
  std::vector<ActuatorCommand> commands;
  uint32_t batches = 0;

  bool scheduleCommand(const ActuatorCommand& cmd) override {
    commands.push_back(cmd);
    return true;
  }
  bool schedulePulseAt(uint8_t id, uint16_t value, uint32_t durationUs, uint32_t at) override {
    ActuatorCommand c{};
    c.actuator_id = id;
    c.value = value;
    c.execute_at = at;
    commands.push_back(c);
    return true;
  }
  bool scheduleActionSteps(const ActionStep* steps, uint8_t count, uint8_t velocity,
                           uint32_t timestamp, const uint16_t* vars,
                           uint8_t activeGroup) override {
    (void)velocity; (void)timestamp; (void)vars; (void)activeGroup;
    batches++;
    for (uint8_t i = 0; i < count; i++) {
      ActuatorCommand c{};
      c.actuator_id = steps[i].actuator_id;
      commands.push_back(c);
    }
    return true;
  }
};

void test_gmb_traffic_schedules_nothing_and_leaves_the_engine_untouched() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  RecordingSink sink;
  EngineState state;
  EventProcessor proc(&sink, &state);
  proc.getLookup() = fx.lookup;

  // Photo de l'etat temps reel AVANT tout trafic GMB.
  PipelineLookup lookupBefore = proc.getLookup();
  uint8_t activeBefore[128];
  proc.getNoteActive(activeBefore);

  // Deluge de decouverte : handshakes, segments valides, index hors bornes,
  // trames malformees, SysEx etrangers.
  uint8_t out[GmbSysExService::MAX_RESPONSE];
  const uint8_t hs[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  const uint8_t bad[] = {0xF0, 0x7D, 0x00, 0x10, 0x00, 0x03, 0xF7};
  const uint8_t foreign[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
  for (uint16_t round = 0; round < 5; round++) {
    const uint32_t now = (uint32_t)round * 1000;
    svc.handleSysEx(hs, sizeof(hs), out, sizeof(out), now);
    svc.handleSysEx(bad, sizeof(bad), out, sizeof(out), now);
    svc.handleSysEx(foreign, sizeof(foreign), out, sizeof(out), now);
    for (uint16_t i = 0; i < svc.totalChunks() + 2; i++) {
      uint16_t t = 0, idx = 0;
      std::string payload;
      requestChunk(i, now, t, idx, payload);
    }
  }

  // Aucune commande planifiee, aucun lot, aucune note active, et la table de
  // routage est intacte octet pour octet.
  TEST_ASSERT_EQUAL_size_t(0, sink.commands.size());
  TEST_ASSERT_EQUAL_UINT32(0, sink.batches);
  uint8_t activeAfter[128];
  proc.getNoteActive(activeAfter);
  TEST_ASSERT_EQUAL_MEMORY(activeBefore, activeAfter, sizeof(activeBefore));
  TEST_ASSERT_EQUAL_MEMORY(&lookupBefore, &proc.getLookup(), sizeof(PipelineLookup));
}

void test_note_processing_still_works_after_gmb_traffic() {
  fx.buildGmKit(10);
  svc.begin(1, 0, 0);
  GmbBuildInputs in = inputs();
  svc.rebuild(in, 0);

  RecordingSink sink;
  EngineState state;
  EventProcessor proc(&sink, &state);
  proc.getLookup() = fx.lookup;

  uint8_t out[GmbSysExService::MAX_RESPONSE];
  const uint8_t hs[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  for (uint16_t i = 0; i < 10; i++) svc.handleSysEx(hs, sizeof(hs), out, sizeof(out), i);

  MidiEvent ev{};
  ev.type = MIDI_EVT_NOTE_ON;
  ev.channel = 10;
  ev.data1 = 36;
  ev.data2 = 100;
  ev.timestamp = 1000;
  proc.processMidiEvent(ev);

  TEST_ASSERT_EQUAL_UINT32(1, sink.batches);
  TEST_ASSERT_TRUE(sink.commands.size() >= 1);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_descriptor_declares_version_two_and_the_configured_notes);
  RUN_TEST(test_descriptor_is_pure_ascii_and_balanced);
  RUN_TEST(test_no_configuration_still_produces_a_valid_descriptor);
  RUN_TEST(test_descriptor_cache_is_a_valid_c_string);
  RUN_TEST(test_descriptor_size_matches_the_handshake_field);
  RUN_TEST(test_single_chunk_descriptor_is_served_whole);
  RUN_TEST(test_multi_chunk_descriptor_reassembles_byte_for_byte);
  RUN_TEST(test_first_middle_and_last_chunks_can_be_requested_in_any_order);
  RUN_TEST(test_repeating_a_chunk_request_returns_the_same_bytes);
  RUN_TEST(test_out_of_range_chunk_gets_no_answer_and_is_counted);
  RUN_TEST(test_malformed_request_is_counted_and_answered_with_nothing);
  RUN_TEST(test_serving_chunks_never_rebuilds_the_descriptor);
  RUN_TEST(test_first_build_sets_a_revision_and_asks_to_persist_but_does_not_notify);
  RUN_TEST(test_changing_a_note_mapping_bumps_the_revision_and_notifies);
  RUN_TEST(test_a_timing_change_sets_the_timing_flag);
  RUN_TEST(test_a_change_that_does_not_affect_capabilities_bumps_nothing);
  RUN_TEST(test_a_reboot_on_an_unchanged_configuration_keeps_the_revision);
  RUN_TEST(test_a_reboot_after_an_offline_edit_moves_the_revision_forward);
  RUN_TEST(test_disabling_an_instrument_changes_the_capabilities);
  RUN_TEST(test_a_sysex_flood_is_dropped_once_the_bucket_is_empty);
  RUN_TEST(test_a_full_descriptor_transfer_fits_inside_the_burst);
  RUN_TEST(test_gmb_traffic_schedules_nothing_and_leaves_the_engine_untouched);
  RUN_TEST(test_note_processing_still_works_after_gmb_traffic);
  return UNITY_END();
}
