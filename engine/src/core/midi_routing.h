#ifndef ENGINE_MIDI_ROUTING_H
#define ENGINE_MIDI_ROUTING_H

#include <Arduino.h>
#include "config.h"

// ============================================================================
// Construction de la table de routage (canal, note) -> cible
// ============================================================================
// Le moteur possede DEUX tables de ce genre : celle des pipelines compiles
// (EventProcessor, chemin principal) et celle des instruments (InstrumentManager,
// chemin legacy). Toutes deux ont souffert du meme defaut — indexees par la note
// seule, le canal MIDI n'etait pas une vraie cle — et toutes deux doivent donc
// appliquer exactement les MEMES regles. Les faire diverger, c'est reintroduire
// le bug d'un cote sans s'en apercevoir : la logique vit ici, une seule fois, et
// se teste sans materiel ni ArduinoJson (test/test_midi_routing).
//
// Regles :
//   1. midiChannel == 0 signifie OMNI : l'entree est deplie sur les 16 lignes.
//   2. Un canal explicite l'emporte TOUJOURS sur une entree OMNI reclamant la
//      meme note, quel que soit leur ordre dans la configuration.
//   3. Entre deux entrees de meme rang (deux OMNI, ou deux fois le meme canal),
//      la PREMIERE garde la place ; les suivantes sont comptees comme masquees
//      pour que l'appelant puisse le signaler plutot que de l'ignorer.
// ============================================================================

// Description d'une entree a router.
struct MidiBinding {
  uint8_t channel;   // 0 = OMNI, 1..16 = canal explicite
  uint8_t note;      // 0..127
  bool valid;        // false = entree a ignorer (desactivee, note hors bornes...)
};

// Remplit `table` (MIDI_CHANNEL_COUNT lignes de 128 colonnes) a partir de
// `count` entrees fournies par `binding(i)`. `empty` est la valeur "aucune
// cible". Retourne le nombre d'entrees masquees par une entree de meme rang
// deja en place (regle 3).
//
// SlotT doit pouvoir representer tous les index [0, count) ainsi que `empty`.
template <typename SlotT, typename BindingFn>
uint16_t midiBuildNoteMap(SlotT table[MIDI_CHANNEL_COUNT][128], SlotT empty,
                          uint16_t count, BindingFn binding) {
  for (uint8_t ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
    for (uint16_t n = 0; n < 128; n++) table[ch][n] = empty;
  }

  uint16_t shadowed = 0;

  // Passe 1 : les entrees OMNI occupent toutes les lignes encore libres.
  for (uint16_t i = 0; i < count; i++) {
    MidiBinding b = binding(i);
    if (!b.valid || b.note >= 128 || b.channel != 0) continue;
    bool placed = false;
    for (uint8_t ch = 0; ch < MIDI_CHANNEL_COUNT; ch++) {
      if (table[ch][b.note] == empty) {
        table[ch][b.note] = (SlotT)i;
        placed = true;
      }
    }
    if (!placed) shadowed++;   // une autre entree OMNI tient deja cette note
  }

  // Passe 2 : un canal explicite reprend sa ligne a une entree OMNI, mais pas a
  // une autre entree explicite.
  for (uint16_t i = 0; i < count; i++) {
    MidiBinding b = binding(i);
    if (!b.valid || b.note >= 128) continue;
    if (b.channel == 0 || b.channel > MIDI_CHANNEL_COUNT) continue;

    SlotT& slot = table[b.channel - 1][b.note];
    if (slot != empty) {
      MidiBinding held = binding((uint16_t)slot);
      if (held.channel != 0) {     // deja pris par un canal explicite
        shadowed++;
        continue;
      }
    }
    slot = (SlotT)i;
  }

  return shadowed;
}

// Lookup correspondant. `channel` est le canal du fil (1..16) : une valeur hors
// plage est refusee plutot que repliee sur le canal 1.
template <typename SlotT>
SlotT midiLookupNote(const SlotT table[MIDI_CHANNEL_COUNT][128], SlotT empty,
                     uint8_t channel, uint8_t note) {
  if (channel < 1 || channel > MIDI_CHANNEL_COUNT || note >= 128) return empty;
  return table[channel - 1][note];
}

#endif // ENGINE_MIDI_ROUTING_H
