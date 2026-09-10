#ifndef GMB_DESCRIPTOR_H
#define GMB_DESCRIPTOR_H

#include "gmb_capabilities.h"

// ============================================================================
// Serialisation du descripteur de capacites v2
// ============================================================================
// Le descripteur est du JSON restreint a l'ASCII (SYSEX_IDENTITY.md §3) : tout
// caractere non-ASCII est echappe en \uXXXX. Chaque octet est donc deja 7-bit
// safe et part sur le fil SysEx sans aucun packing.
//
// L'ecriture se fait a la main dans un tampon fixe, sans ArduinoJson :
//   - garantie d'ASCII a la source plutot qu'apres coup ;
//   - aucune allocation, donc rien a rater quand le tas est fragmente ;
//   - et surtout : testable en natif, ou ArduinoJson n'existe pas.
//
// Degradation plutot que troncature
// ---------------------------------
// Un JSON tronque est un JSON invalide, et un descripteur invalide fait
// retomber GMB au niveau 0 — l'utilisateur perd la reconnaissance automatique
// sans savoir pourquoi. Le serialiseur essaie donc des niveaux de detail
// decroissants et ne rend que le premier qui TIENT ENTIER. Les sections
// abandonnees sont des extensions optionnelles ; le socle (canal, notes, type)
// survit jusqu'au bout.
// ============================================================================

enum class GmbDetail : uint8_t {
  FULL = 0,       // + physical (hi-hat, choke) + voices
  NO_PHYSICAL,    // - physical
  CORE,           // - voices : canal, notes, timing, expression, polyphonie
  MINIMAL,        // canal, notes, type/subtype
  PLACEHOLDER     // canal + configured:false (tient toujours)
};

struct GmbDeviceInfo {
  const char* name;
  const char* model;
};

struct GmbSerializeResult {
  size_t length;          // octets ecrits (0 = echec total, ne devrait pas arriver)
  GmbDetail detail;       // niveau de detail effectivement rendu
  uint8_t instrumentsEmitted;
  uint8_t instrumentsDropped;   // canaux sacrifies faute de place : a signaler
};

// Serialiser `snap` dans `out` (non termine par un octet nul obligatoire, mais
// le serialiseur en ajoute un quand la place le permet, pour le debug).
GmbSerializeResult gmbSerializeDescriptor(char* out, size_t cap,
                                          const CapabilitySnapshot& snap,
                                          uint32_t revision,
                                          const GmbDeviceInfo& device);

#endif // GMB_DESCRIPTOR_H
