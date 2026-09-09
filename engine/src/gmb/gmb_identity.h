#ifndef GMB_IDENTITY_H
#define GMB_IDENTITY_H

#include <Arduino.h>

// ============================================================================
// Identite physique stable de la carte (`instance_id` du bloc 0x01)
// ============================================================================
// C'est le pivot du protocole : General-Midi-Boop rattache une configuration
// enregistree a UN EXEMPLAIRE grace a cet identifiant. Le piege documente cote
// GMB (SYSEX_IDENTITY.md §2) est le firmware qui renvoie
//   uint8_t deviceId[5] = {0, 0, 0, 0, 0};
// pour toutes les cartes : GMB applique alors la meme calibration et la meme
// latence mesuree a deux machines differentes.
//
// La source retenue sur ESP32 est l'adresse MAC de base gravee en eFuse
// (esp_efuse_mac_get_default). Elle est :
//   - stable apres reboot et apres reflash ;
//   - unique par puce (Espressif alloue les OUI) ;
//   - independante des noms d'instruments, des profils utilisateur et de la
//     configuration Wi-Fi — ce n'est PAS l'adresse MAC de l'interface STA/AP,
//     qui derive de la base mais que la pile reseau peut se voir surcharger.
//
// Le repliement 48 -> 32 bits est une fonction pure, donc testable en natif
// (test/test_gmb_identity) : c'est la partie ou une erreur se voit sur le fil.
// ============================================================================

// Replier une adresse MAC 6 octets en un identifiant 32 bits.
//
// FNV-1a 32 bits : diffusion correcte sur les 32 bits (les 3 premiers octets
// d'une MAC Espressif sont un OUI constant — un simple XOR ou une troncature
// laisserait des cartes voisines partager la moitie haute).
//
// Renvoie toujours une valeur non nulle : 0 est la valeur qu'affiche un
// firmware qui n'a pas d'identite, et deux cartes ne doivent jamais s'y
// retrouver ensemble par accident.
uint32_t gmbInstanceIdFromMac(const uint8_t mac[6]);

// Identifiant de CETTE carte. Sur ESP32, lit la MAC de base eFuse une fois puis
// met en cache. Hors ESP32 (tests natifs), renvoie 0 : aucun materiel a lire.
uint32_t gmbInstanceId();

#endif // GMB_IDENTITY_H
