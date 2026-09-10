#include "gmb_sysex.h"

// ----------------------------------------------------------------------------
// Requetes
// ----------------------------------------------------------------------------
GmbRequest gmbParseRequest(const uint8_t* msg, size_t len) {
  GmbRequest req;
  if (!msg || len < 6) return req;                       // trop court pour F0 7D 00 b d F7
  if (msg[0] != GMB_SYSEX_START) return req;
  if (msg[len - 1] != GMB_SYSEX_END) return req;
  if (msg[1] != GMB_MANUFACTURER || msg[2] != GMB_SUB_ID) return req;

  // A partir d'ici la trame se reclame de GMB : tout defaut est une erreur, pas
  // un message pour quelqu'un d'autre.
  const uint8_t block = msg[3];
  const uint8_t dir = msg[4];

  // Le firmware ne repond qu'aux requetes. Une reponse ou une notification
  // recue (echo de boucle MIDI, autre instrument sur le meme fil) est ignoree
  // sans etre comptee comme invalide : elle est parfaitement bien formee.
  if (dir != GMB_DIR_REQUEST) return req;

  if (block == GMB_BLOCK_HANDSHAKE) {
    if (len != 6) { req.type = GmbRequestType::MALFORMED; return req; }
    req.type = GmbRequestType::HANDSHAKE;
    return req;
  }

  if (block == GMB_BLOCK_DESCRIPTOR) {
    // F0 7D 00 10 00 <chunk_index[2]> F7
    if (len != 8) { req.type = GmbRequestType::MALFORMED; return req; }
    if ((msg[5] & 0x80) || (msg[6] & 0x80)) {
      req.type = GmbRequestType::MALFORMED;
      return req;
    }
    req.type = GmbRequestType::DESCRIPTOR_CHUNK;
    req.chunkIndex = gmbDecode14(&msg[5]);
    return req;
  }

  // Bloc GMB inconnu : la trame nous etait bien adressee mais ne veut rien dire
  // pour cette version du firmware.
  req.type = GmbRequestType::MALFORMED;
  return req;
}

// ----------------------------------------------------------------------------
// Trames sortantes
// ----------------------------------------------------------------------------
size_t gmbBuildHandshake(uint8_t* out, size_t cap,
                         uint32_t instanceId,
                         uint8_t fwMajor, uint8_t fwMinor, uint8_t fwPatch,
                         uint32_t descriptorSize,
                         uint32_t revision,
                         uint8_t flags) {
  if (!out || cap < GMB_HANDSHAKE_SIZE) return 0;

  size_t i = 0;
  out[i++] = GMB_SYSEX_START;      // 0
  out[i++] = GMB_MANUFACTURER;     // 1
  out[i++] = GMB_SUB_ID;           // 2
  out[i++] = GMB_BLOCK_HANDSHAKE;  // 3
  out[i++] = GMB_DIR_RESPONSE;     // 4
  out[i++] = GMB_PROTOCOL_VERSION; // 5
  gmbEncode32(instanceId, &out[i]); i += 5;              // 6..10
  out[i++] = (uint8_t)(fwMajor & 0x7F);                  // 11
  out[i++] = (uint8_t)(fwMinor & 0x7F);                  // 12
  out[i++] = (uint8_t)(fwPatch & 0x7F);                  // 13
  gmbEncode21(descriptorSize, &out[i]); i += 3;          // 14..16
  gmbEncode32(revision, &out[i]); i += 5;                // 17..21
  out[i++] = (uint8_t)(flags & 0x7F);                    // 22
  out[i++] = GMB_SYSEX_END;                              // 23
  return i;                                              // == GMB_HANDSHAKE_SIZE
}

size_t gmbBuildChunk(uint8_t* out, size_t cap,
                     uint16_t totalChunks, uint16_t chunkIndex,
                     const char* payload, size_t payloadLen) {
  if (!out || !payload) return 0;
  if (payloadLen == 0 || payloadLen > GMB_CHUNK_PAYLOAD_MAX) return 0;
  const size_t frameLen = 10 + payloadLen;
  if (cap < frameLen) return 0;

  size_t i = 0;
  out[i++] = GMB_SYSEX_START;
  out[i++] = GMB_MANUFACTURER;
  out[i++] = GMB_SUB_ID;
  out[i++] = GMB_BLOCK_DESCRIPTOR;
  out[i++] = GMB_DIR_RESPONSE;
  gmbEncode14(totalChunks, &out[i]); i += 2;
  gmbEncode14(chunkIndex, &out[i]); i += 2;
  for (size_t p = 0; p < payloadLen; p++) {
    uint8_t b = (uint8_t)payload[p];
    out[i++] = (b & 0x80) ? (uint8_t)'?' : b;
  }
  out[i++] = GMB_SYSEX_END;
  return i;
}

size_t gmbBuildNotification(uint8_t* out, size_t cap,
                            uint32_t revision, uint8_t changeFlags) {
  if (!out || cap < GMB_NOTIFICATION_SIZE) return 0;

  size_t i = 0;
  out[i++] = GMB_SYSEX_START;         // 0
  out[i++] = GMB_MANUFACTURER;        // 1
  out[i++] = GMB_SUB_ID;              // 2
  out[i++] = GMB_BLOCK_CHANGED;       // 3
  out[i++] = GMB_DIR_NOTIFICATION;    // 4
  gmbEncode32(revision, &out[i]); i += 5;             // 5..9
  out[i++] = (uint8_t)(changeFlags & 0x7F);           // 10
  out[i++] = GMB_SYSEX_END;                           // 11
  return i;                                           // == GMB_NOTIFICATION_SIZE
}
