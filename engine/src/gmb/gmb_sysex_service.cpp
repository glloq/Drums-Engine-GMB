#include "gmb_sysex_service.h"

uint32_t gmbHashBytes(const void* data, size_t len) {
  const uint8_t* p = (const uint8_t*)data;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    hash ^= (uint32_t)p[i];
    hash *= 16777619u;
  }
  return hash;
}

// Le nom et le modele ne dependent pas de la configuration : `model` est une
// etiquette d'affichage dont GMB ne deduit AUCUNE capacite (SYSEX_IDENTITY.md
// §5), tout le reste vient du descripteur.
static const GmbDeviceInfo kDevice = { "Drums Engine", "Drums-Engine-GMB" };

void GmbSysExService::begin(uint32_t instanceId, uint32_t revision, uint32_t stateHash) {
  _instanceId = instanceId;
  _revision = revision;
  _stateHash = stateHash;
  _built = false;
  _persistPending = false;
  _notifyPending = false;
  _tokens = RATE_BURST;
  _lastRefillMs = 0;
  // Les compteurs de diagnostique repartent avec le service : ils decrivent
  // l'activite depuis le demarrage, pas depuis la mise sous tension d'une
  // instance precedente.
  _diag = Diagnostics();
}

uint32_t GmbSysExService::_timingHash() const {
  // Hachage champ par champ : le padding d'une struct n'est pas initialise de
  // facon garantie et ferait varier un hachage memoire brut sans qu'aucune
  // capacite ait bouge.
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) {
    for (uint8_t i = 0; i < 4; i++) {
      h ^= (uint32_t)((v >> (i * 8)) & 0xFF);
      h *= 16777619u;
    }
  };
  mix(_snapshot.instrumentCount);
  for (uint8_t i = 0; i < _snapshot.instrumentCount; i++) {
    const GmbInstrumentCaps& e = _snapshot.instruments[i];
    mix(e.channel);
    mix(e.timing.prepareMaxMs);
    mix(e.timing.exciteLatencyMs);
    mix(e.timing.rearticulationMs);
    mix(e.timing.releaseMs);
    mix(e.timing.jitterMs);
    mix(e.timing.known);
  }
  return h;
}

bool GmbSysExService::rebuild(const GmbBuildInputs& in, uint32_t nowMs) {
  gmbBuildCapabilities(in, _snapshot);

  // Passe 1 : rendre le descripteur avec revision = 0. Le resultat ne depend
  // alors QUE des capacites, ce qui fait de son empreinte le critere exact de
  // "les capacites ont-elles change ?" — sans avoir a maintenir a la main la
  // liste des champs qui comptent (§26).
  // Un octet est reserve au terminateur : le cache est ainsi toujours une
  // chaine C valide, ce dont depend la route HTTP.
  GmbSerializeResult res =
      gmbSerializeDescriptor(_descriptor, GMB_DESCRIPTOR_MAX - 1, _snapshot, 0, kDevice);
  const uint32_t hash = gmbHashBytes(_descriptor, res.length);
  const uint32_t timingHash = _timingHash();

  const bool changed = (hash != _stateHash);
  const bool firstBuild = !_built;

  if (changed) {
    _revision++;
    _stateHash = hash;
    _persistPending = true;
    if (!firstBuild) {
      // Au premier build (demarrage), inutile de pousser une notification :
      // GMB interroge le bloc 1 a la connexion et y lit la revision courante.
      _notifyPending = true;
      _notifyFlags = GMB_CHANGE_INSTRUMENTS;
      if (timingHash != _lastTimingHash) _notifyFlags |= GMB_CHANGE_TIMING;
      _diag.lastNotificationMs = nowMs;
    }
  }
  _lastTimingHash = timingHash;

  // Passe 2 : le document definitif, avec la revision retenue. Le cache est
  // desormais fige jusqu'a la prochaine activation de configuration : les
  // requetes de segments n'en declenchent aucune reconstruction.
  res = gmbSerializeDescriptor(_descriptor, GMB_DESCRIPTOR_MAX - 1, _snapshot, _revision, kDevice);
  _descriptorLen = res.length;
  _descriptor[_descriptorLen] = '\0';
  _detail = res.detail;
  _instrumentsDropped = res.instrumentsDropped;

  _built = true;
  _diag.rebuilds++;
  _diag.lastRebuildMs = nowMs;
  return changed;
}

uint16_t GmbSysExService::totalChunks() const {
  if (_descriptorLen == 0) return 0;
  return (uint16_t)((_descriptorLen + GMB_CHUNK_PAYLOAD_MAX - 1) / GMB_CHUNK_PAYLOAD_MAX);
}

uint8_t GmbSysExService::flags() const {
  uint8_t f = GMB_FLAG_PUSH;   // le firmware emet bien le bloc 0x11
  if (_httpAvailable) f |= GMB_FLAG_HTTP;
  return f;
}

bool GmbSysExService::_allow(uint32_t nowMs) {
  // Seau a jetons. Le reliquat de temps est conserve (on n'avance
  // _lastRefillMs que de ce qui a reellement produit des jetons), sans quoi une
  // rafale de requetes rapprochees empecherait tout rechargement.
  const uint32_t elapsed = nowMs - _lastRefillMs;
  if (elapsed > 0) {
    const uint32_t add = (elapsed * RATE_PER_SEC) / 1000u;
    if (add > 0) {
      uint32_t t = (uint32_t)_tokens + add;
      _tokens = (t > RATE_BURST) ? RATE_BURST : (uint16_t)t;
      _lastRefillMs += (add * 1000u) / RATE_PER_SEC;
    }
  }
  if (_tokens == 0) return false;
  _tokens--;
  return true;
}

size_t GmbSysExService::handleSysEx(const uint8_t* msg, size_t len,
                                    uint8_t* out, size_t outCap, uint32_t nowMs) {
  if (!out || outCap == 0) return 0;

  const GmbRequest req = gmbParseRequest(msg, len);

  switch (req.type) {
    case GmbRequestType::NONE:
      // SysEx d'un autre fabricant, ou reponse/notification d'un autre appareil
      // sur le meme fil : ce n'est pas une erreur, on ne la compte pas.
      return 0;

    case GmbRequestType::MALFORMED:
      _diag.invalidPackets++;
      return 0;

    case GmbRequestType::HANDSHAKE: {
      if (!_allow(nowMs)) { _diag.rateLimited++; return 0; }
      _diag.handshakeRequests++;
      _diag.lastRequestMs = nowMs;
      return gmbBuildHandshake(out, outCap, _instanceId,
                               FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR,
                               FIRMWARE_VERSION_PATCH,
                               (uint32_t)_descriptorLen, _revision, flags());
    }

    case GmbRequestType::DESCRIPTOR_CHUNK: {
      if (!_allow(nowMs)) { _diag.rateLimited++; return 0; }
      _diag.chunkRequests++;
      _diag.lastRequestMs = nowMs;

      const uint16_t total = totalChunks();
      if (total == 0 || req.chunkIndex >= total) {
        // Index hors bornes : pas de reponse. Repondre un segment vide ou
        // replie ferait diverger le reassemblage cote hote ; l'absence de
        // reponse le laisse expirer proprement puis retomber au niveau 0.
        _diag.outOfRangeChunks++;
        return 0;
      }

      const size_t offset = (size_t)req.chunkIndex * GMB_CHUNK_PAYLOAD_MAX;
      size_t payload = _descriptorLen - offset;
      if (payload > GMB_CHUNK_PAYLOAD_MAX) payload = GMB_CHUNK_PAYLOAD_MAX;

      const size_t n = gmbBuildChunk(out, outCap, total, req.chunkIndex,
                                     _descriptor + offset, payload);
      if (n > 0) _diag.lastTransferMs = nowMs;
      return n;
    }
  }
  return 0;
}

size_t GmbSysExService::takeNotification(uint8_t* out, size_t cap, uint32_t nowMs) {
  if (!_notifyPending || !out) return 0;
  const size_t n = gmbBuildNotification(out, cap, _revision, _notifyFlags);
  if (n == 0) return 0;   // tampon trop court : on retentera au prochain passage
  _notifyPending = false;
  _diag.notificationsSent++;
  _diag.lastNotificationMs = nowMs;
  return n;
}
