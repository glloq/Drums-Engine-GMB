// ============================================================================
// Tests natifs du protocole SysEx GMB v2 et de l'identite physique
// ============================================================================
// Ces tests encodent en dur ce que le PARSEUR DE L'HOTE attend aujourd'hui
// (General-Midi-Boop/src/midi/devices/DeviceManager.js) : longueur exacte des
// trames, position de chaque champ, et surtout le masque 0x0f du 5e octet des
// entiers 32 bits. Le codec generique de GMB masque ce meme octet a 0x07 ; s'en
// inspirer diviserait par deux l'espace d'identifiants sans que rien ne le
// signale, d'ou une verification bit a bit ici.
#include <unity.h>
#include "../../src/gmb/gmb_sysex.cpp"
#include "../../src/gmb/gmb_identity.cpp"

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------------------
// Encodage 7 bits
// ---------------------------------------------------------------------------
void test_encode32_is_little_endian_7bit_with_full_nibble() {
  uint8_t out[5];
  gmbEncode32(0xDEADBEEFu, out);
  // 0xDEADBEEF = 1101_1110_1010_1101_1011_1110_1110_1111
  TEST_ASSERT_EQUAL_UINT8(0x6F, out[0]);   // bits 0..6
  TEST_ASSERT_EQUAL_UINT8(0x7D, out[1]);   // bits 7..13
  TEST_ASSERT_EQUAL_UINT8(0x36, out[2]);   // bits 14..20
  TEST_ASSERT_EQUAL_UINT8(0x75, out[3]);   // bits 21..27
  TEST_ASSERT_EQUAL_UINT8(0x0D, out[4]);   // bits 28..31 — un DEMI-octet
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, gmbDecode32(out));
}

void test_encode32_keeps_bit31() {
  // Le piege exact : un masque 0x07 sur le 5e octet perdrait ce bit.
  uint8_t out[5];
  gmbEncode32(0x80000000u, out);
  TEST_ASSERT_EQUAL_UINT8(0x08, out[4]);
  TEST_ASSERT_EQUAL_UINT32(0x80000000u, gmbDecode32(out));

  gmbEncode32(0xFFFFFFFFu, out);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, gmbDecode32(out));
  for (uint8_t i = 0; i < 5; i++) TEST_ASSERT_TRUE((out[i] & 0x80) == 0);
}

void test_encode21_and_14_roundtrip_and_stay_7bit() {
  uint8_t three[3];
  gmbEncode21(2097151u, three);              // 21 bits pleins
  TEST_ASSERT_EQUAL_UINT32(2097151u, gmbDecode21(three));
  for (uint8_t i = 0; i < 3; i++) TEST_ASSERT_TRUE((three[i] & 0x80) == 0);

  uint8_t two[2];
  gmbEncode14(16383u, two);                  // 14 bits pleins
  TEST_ASSERT_EQUAL_UINT16(16383u, gmbDecode14(two));
  for (uint8_t i = 0; i < 2; i++) TEST_ASSERT_TRUE((two[i] & 0x80) == 0);
}

// ---------------------------------------------------------------------------
// Bloc 0x01 — handshake
// ---------------------------------------------------------------------------
void test_handshake_request_is_recognised() {
  const uint8_t req[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0xF7};
  GmbRequest r = gmbParseRequest(req, sizeof(req));
  TEST_ASSERT_TRUE(r.type == GmbRequestType::HANDSHAKE);
}

void test_handshake_response_is_exactly_24_bytes_and_well_placed() {
  uint8_t out[64];
  const size_t n = gmbBuildHandshake(out, sizeof(out), 0x12345678u, 1, 2, 3,
                                     1056, 42, GMB_FLAG_HTTP | GMB_FLAG_PUSH);
  TEST_ASSERT_EQUAL_size_t(24, n);
  TEST_ASSERT_EQUAL_size_t(GMB_HANDSHAKE_SIZE, n);

  TEST_ASSERT_EQUAL_UINT8(0xF0, out[0]);
  TEST_ASSERT_EQUAL_UINT8(0x7D, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0x00, out[2]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[3]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[4]);        // direction = reponse
  TEST_ASSERT_EQUAL_UINT8(2, out[5]);           // proto_ver == 2
  TEST_ASSERT_EQUAL_UINT32(0x12345678u, gmbDecode32(&out[6]));
  TEST_ASSERT_EQUAL_UINT8(1, out[11]);
  TEST_ASSERT_EQUAL_UINT8(2, out[12]);
  TEST_ASSERT_EQUAL_UINT8(3, out[13]);
  TEST_ASSERT_EQUAL_UINT32(1056u, gmbDecode21(&out[14]));
  TEST_ASSERT_EQUAL_UINT32(42u, gmbDecode32(&out[17]));
  TEST_ASSERT_EQUAL_UINT8(0x03, out[22]);
  TEST_ASSERT_EQUAL_UINT8(0xF7, out[23]);       // F7 est bien a l'offset 23
}

void test_handshake_response_is_7bit_safe_everywhere() {
  uint8_t out[64];
  const size_t n = gmbBuildHandshake(out, sizeof(out), 0xFFFFFFFFu, 127, 127, 127,
                                     2097151u, 0xFFFFFFFFu, 0x7F);
  TEST_ASSERT_EQUAL_size_t(24, n);
  for (size_t i = 1; i < n - 1; i++) {
    TEST_ASSERT_TRUE_MESSAGE((out[i] & 0x80) == 0, "un octet de charge a le bit 7 arme");
  }
}

void test_handshake_refuses_a_short_buffer_rather_than_truncating() {
  uint8_t out[23];
  TEST_ASSERT_EQUAL_size_t(0, gmbBuildHandshake(out, sizeof(out), 1, 1, 0, 0, 0, 0, 0));
}

// ---------------------------------------------------------------------------
// Bloc 0x10 — descripteur segmente
// ---------------------------------------------------------------------------
void test_chunk_request_decodes_a_14bit_index() {
  const uint8_t req[] = {0xF0, 0x7D, 0x00, 0x10, 0x00, 0x03, 0x01, 0xF7};
  GmbRequest r = gmbParseRequest(req, sizeof(req));
  TEST_ASSERT_TRUE(r.type == GmbRequestType::DESCRIPTOR_CHUNK);
  TEST_ASSERT_EQUAL_UINT16(131, r.chunkIndex);   // 3 + (1 << 7)
}

void test_chunk_response_layout_matches_the_host_parser() {
  uint8_t out[GMB_CHUNK_FRAME_MAX];
  const char payload[] = "{\"a\":1}";
  const size_t n = gmbBuildChunk(out, sizeof(out), 6, 2, payload, strlen(payload));
  TEST_ASSERT_EQUAL_size_t(10 + strlen(payload), n);
  TEST_ASSERT_EQUAL_UINT8(0x10, out[3]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[4]);
  TEST_ASSERT_EQUAL_UINT16(6, gmbDecode14(&out[5]));
  TEST_ASSERT_EQUAL_UINT16(2, gmbDecode14(&out[7]));
  TEST_ASSERT_EQUAL_MEMORY(payload, &out[9], strlen(payload));
  TEST_ASSERT_EQUAL_UINT8(0xF7, out[n - 1]);
  // Le parseur de l'hote exige au moins 10 octets : un segment non vide en
  // produit toujours plus.
  TEST_ASSERT_TRUE(n >= 10);
}

void test_chunk_refuses_empty_and_oversized_payloads() {
  uint8_t out[GMB_CHUNK_FRAME_MAX + 16];
  char big[GMB_CHUNK_PAYLOAD_MAX + 1];
  memset(big, 'x', sizeof(big));
  TEST_ASSERT_EQUAL_size_t(0, gmbBuildChunk(out, sizeof(out), 1, 0, "x", 0));
  TEST_ASSERT_EQUAL_size_t(0, gmbBuildChunk(out, sizeof(out), 1, 0, big, sizeof(big)));
  // 200 octets pile passent, et la trame reste sous 210.
  const size_t n = gmbBuildChunk(out, sizeof(out), 1, 0, big, GMB_CHUNK_PAYLOAD_MAX);
  TEST_ASSERT_EQUAL_size_t(210, n);
}

void test_chunk_never_emits_a_byte_with_bit7_set() {
  uint8_t out[GMB_CHUNK_FRAME_MAX];
  const char dirty[] = { 'a', (char)0xC3, (char)0xA9, 'b', 0 };
  const size_t n = gmbBuildChunk(out, sizeof(out), 1, 0, dirty, 4);
  TEST_ASSERT_TRUE(n > 0);
  for (size_t i = 1; i < n - 1; i++) TEST_ASSERT_TRUE((out[i] & 0x80) == 0);
}

// ---------------------------------------------------------------------------
// Bloc 0x11 — notification
// ---------------------------------------------------------------------------
void test_notification_is_exactly_12_bytes() {
  uint8_t out[32];
  const size_t n = gmbBuildNotification(out, sizeof(out), 7,
                                        GMB_CHANGE_INSTRUMENTS | GMB_CHANGE_TIMING);
  TEST_ASSERT_EQUAL_size_t(12, n);
  TEST_ASSERT_EQUAL_size_t(GMB_NOTIFICATION_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(0x11, out[3]);
  TEST_ASSERT_EQUAL_UINT8(0x02, out[4]);        // direction = notification
  TEST_ASSERT_EQUAL_UINT32(7u, gmbDecode32(&out[5]));
  TEST_ASSERT_EQUAL_UINT8(0x06, out[10]);
  TEST_ASSERT_EQUAL_UINT8(0xF7, out[11]);
}

// ---------------------------------------------------------------------------
// Robustesse de l'analyse
// ---------------------------------------------------------------------------
void test_foreign_sysex_is_ignored_not_counted_as_invalid() {
  // Identity Request universelle MIDI : parfaitement valide, mais pas pour nous.
  const uint8_t universal[] = {0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7};
  TEST_ASSERT_TRUE(gmbParseRequest(universal, sizeof(universal)).type == GmbRequestType::NONE);

  const uint8_t roland[] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0xF7};
  TEST_ASSERT_TRUE(gmbParseRequest(roland, sizeof(roland)).type == GmbRequestType::NONE);
}

void test_our_own_responses_are_not_reprocessed() {
  // Un echo de boucle MIDI ne doit pas declencher une reponse a une reponse.
  uint8_t frame[64];
  const size_t n = gmbBuildHandshake(frame, sizeof(frame), 1, 1, 0, 0, 10, 1, 3);
  TEST_ASSERT_TRUE(gmbParseRequest(frame, n).type == GmbRequestType::NONE);

  const size_t m = gmbBuildNotification(frame, sizeof(frame), 1, 1);
  TEST_ASSERT_TRUE(gmbParseRequest(frame, m).type == GmbRequestType::NONE);
}

void test_malformed_gmb_frames_are_reported() {
  const uint8_t noEnd[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0x00};
  TEST_ASSERT_TRUE(gmbParseRequest(noEnd, sizeof(noEnd)).type == GmbRequestType::NONE);

  const uint8_t badLen[] = {0xF0, 0x7D, 0x00, 0x01, 0x00, 0x00, 0xF7};
  TEST_ASSERT_TRUE(gmbParseRequest(badLen, sizeof(badLen)).type == GmbRequestType::MALFORMED);

  const uint8_t shortChunk[] = {0xF0, 0x7D, 0x00, 0x10, 0x00, 0x03, 0xF7};
  TEST_ASSERT_TRUE(gmbParseRequest(shortChunk, sizeof(shortChunk)).type == GmbRequestType::MALFORMED);

  const uint8_t unknownBlock[] = {0xF0, 0x7D, 0x00, 0x42, 0x00, 0xF7};
  TEST_ASSERT_TRUE(gmbParseRequest(unknownBlock, sizeof(unknownBlock)).type == GmbRequestType::MALFORMED);

  const uint8_t truncated[] = {0xF0, 0x7D, 0xF7};
  TEST_ASSERT_TRUE(gmbParseRequest(truncated, sizeof(truncated)).type == GmbRequestType::NONE);

  TEST_ASSERT_TRUE(gmbParseRequest(nullptr, 0).type == GmbRequestType::NONE);
}

// ---------------------------------------------------------------------------
// Identite physique
// ---------------------------------------------------------------------------
void test_instance_id_is_never_zero() {
  const uint8_t zeros[6] = {0, 0, 0, 0, 0, 0};
  TEST_ASSERT_NOT_EQUAL(0u, gmbInstanceIdFromMac(zeros));
  TEST_ASSERT_NOT_EQUAL(0u, gmbInstanceIdFromMac(nullptr));
}

void test_instance_id_is_stable_for_the_same_mac() {
  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC};
  TEST_ASSERT_EQUAL_UINT32(gmbInstanceIdFromMac(mac), gmbInstanceIdFromMac(mac));
}

void test_two_boards_of_the_same_batch_get_different_ids() {
  // Les trois premiers octets sont l'OUI Espressif, identique sur toutes les
  // cartes : c'est exactement le cas ou une troncature ou un XOR naif
  // collisionnerait.
  const uint8_t a[6] = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x01};
  const uint8_t b[6] = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x02};
  TEST_ASSERT_NOT_EQUAL(gmbInstanceIdFromMac(a), gmbInstanceIdFromMac(b));
}

void test_instance_id_diffuses_over_the_high_bits() {
  // Un identifiant qui ne bougerait que dans son octet bas ne remplirait qu'un
  // 256e de l'espace : on verifie que la moitie haute varie aussi.
  const uint8_t a[6] = {0x24, 0x6F, 0x28, 0x11, 0x22, 0x33};
  const uint8_t b[6] = {0x24, 0x6F, 0x28, 0x11, 0x22, 0x34};
  const uint32_t ia = gmbInstanceIdFromMac(a);
  const uint32_t ib = gmbInstanceIdFromMac(b);
  TEST_ASSERT_NOT_EQUAL(ia >> 16, ib >> 16);
}

void test_instance_id_survives_the_7bit_roundtrip() {
  const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0xDE, 0xAD, 0xBE};
  const uint32_t id = gmbInstanceIdFromMac(mac);
  uint8_t enc[5];
  gmbEncode32(id, enc);
  TEST_ASSERT_EQUAL_UINT32(id, gmbDecode32(enc));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_encode32_is_little_endian_7bit_with_full_nibble);
  RUN_TEST(test_encode32_keeps_bit31);
  RUN_TEST(test_encode21_and_14_roundtrip_and_stay_7bit);
  RUN_TEST(test_handshake_request_is_recognised);
  RUN_TEST(test_handshake_response_is_exactly_24_bytes_and_well_placed);
  RUN_TEST(test_handshake_response_is_7bit_safe_everywhere);
  RUN_TEST(test_handshake_refuses_a_short_buffer_rather_than_truncating);
  RUN_TEST(test_chunk_request_decodes_a_14bit_index);
  RUN_TEST(test_chunk_response_layout_matches_the_host_parser);
  RUN_TEST(test_chunk_refuses_empty_and_oversized_payloads);
  RUN_TEST(test_chunk_never_emits_a_byte_with_bit7_set);
  RUN_TEST(test_notification_is_exactly_12_bytes);
  RUN_TEST(test_foreign_sysex_is_ignored_not_counted_as_invalid);
  RUN_TEST(test_our_own_responses_are_not_reprocessed);
  RUN_TEST(test_malformed_gmb_frames_are_reported);
  RUN_TEST(test_instance_id_is_never_zero);
  RUN_TEST(test_instance_id_is_stable_for_the_same_mac);
  RUN_TEST(test_two_boards_of_the_same_batch_get_different_ids);
  RUN_TEST(test_instance_id_diffuses_over_the_high_bits);
  RUN_TEST(test_instance_id_survives_the_7bit_roundtrip);
  return UNITY_END();
}
