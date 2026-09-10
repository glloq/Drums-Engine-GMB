#ifndef GMB_SYSEX_H
#define GMB_SYSEX_H

#include <Arduino.h>

// ============================================================================
// GMB v2 SysEx wire protocol — codec pur, sans etat
// ============================================================================
// Reference normative : General-Midi-Boop/docs/SYSEX_IDENTITY.md (protocole
// v2), et surtout le code qui le PARSE reellement aujourd'hui cote hote :
//   src/midi/devices/DeviceManager.js  parseGmbHandshake()      (bloc 0x01)
//                                      parseDescriptorChunk()   (bloc 0x10)
//                                      parseChangeNotification()(bloc 0x11)
// Les tailles et les masques ci-dessous sont ceux de ces trois parseurs, pas
// ceux d'une version anterieure de la documentation : le handshake fait
// exactement 24 octets (pas 26, pas 52), et le 5e octet d'un entier 32 bits
// porte un demi-octet (0x0f), pas 3 bits.
//
// Toutes les fonctions sont pures : elles n'allouent rien, ne touchent ni au
// materiel ni au systeme de fichiers, et sont donc testables en natif
// (test/test_gmb_sysex).
// ============================================================================

// --- Cadre commun : F0 7D 00 <block> <direction> ... F7 ---
constexpr uint8_t GMB_SYSEX_START   = 0xF0;
constexpr uint8_t GMB_SYSEX_END     = 0xF7;
constexpr uint8_t GMB_MANUFACTURER  = 0x7D;  // ID "non commercial" reserve
constexpr uint8_t GMB_SUB_ID        = 0x00;

constexpr uint8_t GMB_BLOCK_HANDSHAKE   = 0x01;
constexpr uint8_t GMB_BLOCK_DESCRIPTOR  = 0x10;
constexpr uint8_t GMB_BLOCK_CHANGED     = 0x11;

constexpr uint8_t GMB_DIR_REQUEST      = 0x00;
constexpr uint8_t GMB_DIR_RESPONSE     = 0x01;
constexpr uint8_t GMB_DIR_NOTIFICATION = 0x02;

constexpr uint8_t GMB_PROTOCOL_VERSION = 0x02;

// Bits de `flags` du handshake (offset 22).
constexpr uint8_t GMB_FLAG_HTTP = 0x01;  // GET /gmb/descriptor.json disponible
constexpr uint8_t GMB_FLAG_PUSH = 0x02;  // notifications bloc 0x11 emises

// Bits de `change_flags` du bloc 0x11.
constexpr uint8_t GMB_CHANGE_IDENTITY    = 0x01;
constexpr uint8_t GMB_CHANGE_INSTRUMENTS = 0x02;
constexpr uint8_t GMB_CHANGE_TIMING      = 0x04;
constexpr uint8_t GMB_CHANGE_RESTART     = 0x08;

// Tailles de trame imposees par les parseurs de l'hote.
constexpr size_t GMB_HANDSHAKE_SIZE    = 24;
constexpr size_t GMB_NOTIFICATION_SIZE = 12;
// 200 octets, choisi cote GMB pour rester sous la MTU de reassemblage BLE-MIDI.
// Message complet = 9 octets d'en-tete + payload + F7 = 210 au maximum.
constexpr size_t GMB_CHUNK_PAYLOAD_MAX = 200;
constexpr size_t GMB_CHUNK_FRAME_MAX   = 10 + GMB_CHUNK_PAYLOAD_MAX;

// ----------------------------------------------------------------------------
// Encodage 7 bits
// ----------------------------------------------------------------------------
// Un octet SysEx ne peut pas avoir le bit 7 arme : les entiers voyagent en
// petit-boutiste sur des groupes de 7 bits. Le 5e octet d'un entier 32 bits
// porte les bits 28..31 (masque 0x0f) — c'est exactement ce que decode le
// `dec32()` de parseGmbHandshake(). Le codec generique decode7BitTo32Bit() de
// GMB masque lui ce 5e octet a 0x07 et perdrait le bit 31 : ne pas s'en servir
// comme reference.

inline void gmbEncode32(uint32_t value, uint8_t* out) {
  out[0] = (uint8_t)(value & 0x7F);
  out[1] = (uint8_t)((value >> 7) & 0x7F);
  out[2] = (uint8_t)((value >> 14) & 0x7F);
  out[3] = (uint8_t)((value >> 21) & 0x7F);
  out[4] = (uint8_t)((value >> 28) & 0x0F);
}

inline uint32_t gmbDecode32(const uint8_t* in) {
  return ((uint32_t)(in[0] & 0x7F)) |
         ((uint32_t)(in[1] & 0x7F) << 7) |
         ((uint32_t)(in[2] & 0x7F) << 14) |
         ((uint32_t)(in[3] & 0x7F) << 21) |
         ((uint32_t)(in[4] & 0x0F) << 28);
}

// `descriptor_size` tient sur 3 octets, soit 21 bits (2 097 151 au maximum).
inline void gmbEncode21(uint32_t value, uint8_t* out) {
  out[0] = (uint8_t)(value & 0x7F);
  out[1] = (uint8_t)((value >> 7) & 0x7F);
  out[2] = (uint8_t)((value >> 14) & 0x7F);
}

inline uint32_t gmbDecode21(const uint8_t* in) {
  return ((uint32_t)(in[0] & 0x7F)) |
         ((uint32_t)(in[1] & 0x7F) << 7) |
         ((uint32_t)(in[2] & 0x7F) << 14);
}

// `total_chunks` / `chunk_index` tiennent sur 2 octets, soit 14 bits.
inline void gmbEncode14(uint16_t value, uint8_t* out) {
  out[0] = (uint8_t)(value & 0x7F);
  out[1] = (uint8_t)((value >> 7) & 0x7F);
}

inline uint16_t gmbDecode14(const uint8_t* in) {
  return (uint16_t)((in[0] & 0x7F) | ((in[1] & 0x7F) << 7));
}

// ----------------------------------------------------------------------------
// Requetes entrantes
// ----------------------------------------------------------------------------
enum class GmbRequestType : uint8_t {
  NONE = 0,          // pas une trame GMB (a ignorer sans bruit)
  MALFORMED,         // en-tete GMB reconnu mais trame invalide
  HANDSHAKE,         // bloc 0x01 direction 0x00
  DESCRIPTOR_CHUNK   // bloc 0x10 direction 0x00
};

struct GmbRequest {
  GmbRequestType type = GmbRequestType::NONE;
  uint16_t chunkIndex = 0;
};

// Analyser un message SysEx COMPLET (F0 ... F7 inclus).
//
// Retourne NONE pour tout ce qui n'est pas adresse a GMB — un SysEx d'un autre
// fabricant ne doit produire ni reponse ni compteur d'erreur. Retourne
// MALFORMED quand l'en-tete est bien `F0 7D 00` mais que la suite ne tient pas
// debout : c'est ce cas, et lui seul, que la diagnostique compte comme paquet
// invalide.
GmbRequest gmbParseRequest(const uint8_t* msg, size_t len);

// ----------------------------------------------------------------------------
// Trames sortantes
// ----------------------------------------------------------------------------
// Chaque constructeur ecrit dans `out` et renvoie le nombre d'octets ecrits,
// ou 0 si `cap` est insuffisant. Aucune trame n'est jamais emise partiellement.

// Bloc 0x01 reponse — exactement GMB_HANDSHAKE_SIZE octets.
size_t gmbBuildHandshake(uint8_t* out, size_t cap,
                         uint32_t instanceId,
                         uint8_t fwMajor, uint8_t fwMinor, uint8_t fwPatch,
                         uint32_t descriptorSize,
                         uint32_t revision,
                         uint8_t flags);

// Bloc 0x10 reponse. `payloadLen` doit valoir 1..GMB_CHUNK_PAYLOAD_MAX : le
// parseur de l'hote exige au moins 10 octets, donc un segment vide serait
// rejete. Tout octet de charge utile hors ASCII 7 bits est remplace par '?'
// plutot que d'emettre un SysEx illegal — le serialiseur de descripteur
// garantit deja l'ASCII, ce filet n'existe que pour ne jamais violer le fil.
size_t gmbBuildChunk(uint8_t* out, size_t cap,
                     uint16_t totalChunks, uint16_t chunkIndex,
                     const char* payload, size_t payloadLen);

// Bloc 0x11 notification — exactement GMB_NOTIFICATION_SIZE octets.
size_t gmbBuildNotification(uint8_t* out, size_t cap,
                            uint32_t revision, uint8_t changeFlags);

#endif // GMB_SYSEX_H
