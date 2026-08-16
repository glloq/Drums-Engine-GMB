// ============================================================================
// Native unit tests for (channel, note) MIDI routing
// ============================================================================
// Regression cover for the routing bug these tests were written against: the
// note map used to be indexed by NOTE ALONE, so the MIDI channel was not a real
// key. Two instruments on the same note in different channels could not
// coexist, and in pipeline mode the channel was ignored outright — a note 36 on
// channel 1 fired the channel 10 kick.
#include <unity.h>
#include "../../src/core/midi_routing.h"

// Table de test: MIDI_CHANNEL_COUNT x 128, index int16 pour distinguer
// clairement "vide" (-1) des index valides.
static int16_t table[MIDI_CHANNEL_COUNT][128];

struct Entry {
  uint8_t channel;
  uint8_t note;
  bool valid;
};

static Entry entries[16];
static uint16_t entryCount = 0;

static void reset() {
  entryCount = 0;
  memset(entries, 0, sizeof(entries));
}

static void add(uint8_t channel, uint8_t note, bool valid = true) {
  entries[entryCount++] = { channel, note, valid };
}

static uint16_t build() {
  return midiBuildNoteMap<int16_t>(
      table, -1, entryCount,
      [](uint16_t i) -> MidiBinding {
        return { entries[i].channel, entries[i].note, entries[i].valid };
      });
}

static int16_t at(uint8_t channel, uint8_t note) {
  return midiLookupNote<int16_t>(table, -1, channel, note);
}

void setUp() { reset(); }
void tearDown() {}

// --- Le bug d'origine ---------------------------------------------------

void test_same_note_on_two_channels_are_independent() {
  add(10, 36);   // 0: kick du kit batterie
  add(11, 36);   // 1: percussion latino sur la meme note
  TEST_ASSERT_EQUAL_UINT16(0, build());

  TEST_ASSERT_EQUAL_INT16(0, at(10, 36));
  TEST_ASSERT_EQUAL_INT16(1, at(11, 36));
}

void test_note_on_unmapped_channel_does_not_fire() {
  add(10, 36);
  build();

  // Avant le correctif, une note 36 sur N'IMPORTE quel canal atteignait le kick.
  TEST_ASSERT_EQUAL_INT16(-1, at(1, 36));
  TEST_ASSERT_EQUAL_INT16(-1, at(9, 36));
  TEST_ASSERT_EQUAL_INT16(-1, at(16, 36));
  TEST_ASSERT_EQUAL_INT16(0, at(10, 36));
}

void test_full_gm_kit_across_two_channels() {
  // 8 articulations sur le canal 10 et les MEMES notes sur le canal 11 :
  // c'est exactement la configuration que l'ancienne table ne pouvait pas
  // representer.
  for (uint8_t i = 0; i < 8; i++) add(10, (uint8_t)(35 + i));
  for (uint8_t i = 0; i < 8; i++) add(11, (uint8_t)(35 + i));
  TEST_ASSERT_EQUAL_UINT16(0, build());

  for (uint8_t i = 0; i < 8; i++) {
    TEST_ASSERT_EQUAL_INT16(i, at(10, (uint8_t)(35 + i)));
    TEST_ASSERT_EQUAL_INT16(8 + i, at(11, (uint8_t)(35 + i)));
  }
}

// --- OMNI ---------------------------------------------------------------

void test_omni_entry_answers_on_every_channel() {
  add(0, 42);    // OMNI
  build();

  for (uint8_t ch = 1; ch <= MIDI_CHANNEL_COUNT; ch++) {
    TEST_ASSERT_EQUAL_INT16(0, at(ch, 42));
  }
}

void test_explicit_channel_wins_over_omni_declared_first() {
  add(0, 38);    // 0: OMNI
  add(10, 38);   // 1: canal explicite
  build();

  TEST_ASSERT_EQUAL_INT16(1, at(10, 38));   // le canal explicite prend la main
  TEST_ASSERT_EQUAL_INT16(0, at(3, 38));    // OMNI garde les autres canaux
}

void test_explicit_channel_wins_over_omni_declared_last() {
  // Meme resultat quel que soit l'ordre dans la configuration : c'est le point
  // de la construction en deux passes.
  add(10, 38);   // 0: canal explicite
  add(0, 38);    // 1: OMNI
  build();

  TEST_ASSERT_EQUAL_INT16(0, at(10, 38));
  TEST_ASSERT_EQUAL_INT16(1, at(3, 38));
}

// --- Doublons -----------------------------------------------------------

void test_duplicate_explicit_mapping_keeps_first_and_reports() {
  add(10, 36);
  add(10, 36);   // doublon exact
  TEST_ASSERT_EQUAL_UINT16(1, build());

  TEST_ASSERT_EQUAL_INT16(0, at(10, 36));   // la premiere garde la place
}

void test_duplicate_omni_mapping_reports_shadowed() {
  add(0, 36);
  add(0, 36);
  TEST_ASSERT_EQUAL_UINT16(1, build());
  TEST_ASSERT_EQUAL_INT16(0, at(5, 36));
}

// --- Bornes -------------------------------------------------------------

void test_invalid_entries_are_skipped() {
  add(10, 36, /*valid=*/false);   // desactivee
  add(10, 200);                   // note hors bornes
  add(17, 36);                    // canal hors bornes
  TEST_ASSERT_EQUAL_UINT16(0, build());

  TEST_ASSERT_EQUAL_INT16(-1, at(10, 36));
}

void test_out_of_range_channel_lookup_is_rejected_not_folded() {
  add(1, 36);
  build();

  // Le canal 0 est OMNI cote configuration, mais ce n'est jamais un canal du
  // fil : il ne doit pas se replier sur le canal 1.
  TEST_ASSERT_EQUAL_INT16(-1, at(0, 36));
  TEST_ASSERT_EQUAL_INT16(-1, at(17, 36));
  TEST_ASSERT_EQUAL_INT16(-1, at(255, 36));
  TEST_ASSERT_EQUAL_INT16(-1, at(1, 128));
  TEST_ASSERT_EQUAL_INT16(-1, at(1, 255));
}

void test_empty_map_is_all_empty() {
  TEST_ASSERT_EQUAL_UINT16(0, build());
  for (uint8_t ch = 1; ch <= MIDI_CHANNEL_COUNT; ch++) {
    for (uint16_t n = 0; n < 128; n++) {
      TEST_ASSERT_EQUAL_INT16(-1, at(ch, (uint8_t)n));
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_same_note_on_two_channels_are_independent);
  RUN_TEST(test_note_on_unmapped_channel_does_not_fire);
  RUN_TEST(test_full_gm_kit_across_two_channels);
  RUN_TEST(test_omni_entry_answers_on_every_channel);
  RUN_TEST(test_explicit_channel_wins_over_omni_declared_first);
  RUN_TEST(test_explicit_channel_wins_over_omni_declared_last);
  RUN_TEST(test_duplicate_explicit_mapping_keeps_first_and_reports);
  RUN_TEST(test_duplicate_omni_mapping_reports_shadowed);
  RUN_TEST(test_invalid_entries_are_skipped);
  RUN_TEST(test_out_of_range_channel_lookup_is_rejected_not_folded);
  RUN_TEST(test_empty_map_is_all_empty);
  return UNITY_END();
}
