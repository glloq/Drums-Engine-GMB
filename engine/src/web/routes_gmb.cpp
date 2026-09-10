#include "web_server.h"
#include "../core/reconfig_barrier.h"
#include "../gmb/gmb_identity.h"

// ============================================================================
// Routes GMB — descripteur HTTP + diagnostique
// ============================================================================
// GET /gmb/descriptor.json sert EXACTEMENT les memes octets que le bloc 0x10.
// C'est le point important : les deux transports lisent le meme cache, donc il
// ne peut pas exister une "version SysEx" et une "version HTTP" du descripteur
// qui divergeraient. La seule difference est le decoupage en segments.
//
// La route est publique, comme les autres GET de l'API : un descripteur de
// capacites n'est pas un secret, et General-Midi-Boop le recupere sans jeton —
// c'est tout l'interet du drapeau HTTP du handshake.
// ============================================================================

void WebServerManager::_setupGmbRoutes() {
  if (!_gmb) return;

  _server.on("/gmb/descriptor.json", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetGmbDescriptor(req); });

  _server.on("/api/gmb/status", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetGmbStatus(req); });

  // Le drapeau HTTP du handshake n'est arme QUE maintenant : annoncer une route
  // qui n'existe pas enverrait GMB sur un GET qui echoue, et lui ferait perdre
  // un aller-retour avant de retomber sur le transfert SysEx.
  _gmb->setHttpAvailable(true);
}

void WebServerManager::_handleGetGmbDescriptor(AsyncWebServerRequest* req) {
  if (!_gmb || _gmb->descriptorSize() == 0) {
    _sendError(req, 503, "GMB descriptor not built yet");
    return;
  }
  _gmb->noteHttpRead(millis());

  // Copie explicite dans la reponse : le cache est un tampon statique que la
  // prochaine activation de configuration reecrira, et une reponse asynchrone
  // peut encore etre en cours d'emission a ce moment-la.
  String body;
  body.reserve(_gmb->descriptorSize() + 1);
  body.concat(_gmb->descriptor());   // le cache est toujours termine par un NUL

  AsyncWebServerResponse* resp = req->beginResponse(200, "application/json", body);
  // Le descripteur change avec la revision : l'ETag permet a un hote qui l'a
  // deja de ne pas le retelecharger, exactement comme `revision` cote SysEx.
  char etag[16];
  snprintf(etag, sizeof(etag), "\"%u\"", (unsigned)_gmb->revision());
  resp->addHeader("ETag", etag);
  resp->addHeader("Cache-Control", "no-cache");
  req->send(resp);
}

void WebServerManager::_handleGetGmbStatus(AsyncWebServerRequest* req) {
  JsonDocument doc;
  if (!_gmb) {
    doc["enabled"] = false;
    _sendJson(req, 200, doc);
    return;
  }

  const CapabilitySnapshot& snap = _gmb->snapshot();
  const GmbSysExService::Diagnostics& d = _gmb->diagnostics();

  doc["enabled"] = true;
  doc["protocol"] = GMB_PROTOCOL_VERSION;

  char idHex[11];
  snprintf(idHex, sizeof(idHex), "0x%08X", (unsigned)_gmb->instanceId());
  doc["instanceId"] = idHex;

  char fw[16];
  snprintf(fw, sizeof(fw), "%d.%d.%d",
           FIRMWARE_VERSION_MAJOR, FIRMWARE_VERSION_MINOR, FIRMWARE_VERSION_PATCH);
  doc["firmware"] = fw;

  doc["revision"] = _gmb->revision();
  doc["descriptorSize"] = (uint32_t)_gmb->descriptorSize();
  doc["descriptorChunks"] = _gmb->totalChunks();
  doc["descriptorMax"] = GMB_DESCRIPTOR_MAX;
  switch (_gmb->detail()) {
    case GmbDetail::FULL:        doc["detail"] = "full"; break;
    case GmbDetail::NO_PHYSICAL: doc["detail"] = "no_physical"; break;
    case GmbDetail::CORE:        doc["detail"] = "core"; break;
    case GmbDetail::MINIMAL:     doc["detail"] = "minimal"; break;
    default:                     doc["detail"] = "placeholder"; break;
  }
  doc["instrumentsDropped"] = _gmb->instrumentsDropped();
  doc["mechanismsDropped"] = snap.droppedMechanisms;
  doc["flags"] = _gmb->flags();
  doc["httpDescriptor"] = (_gmb->flags() & GMB_FLAG_HTTP) != 0;
  doc["pushNotifications"] = (_gmb->flags() & GMB_FLAG_PUSH) != 0;

  // Un transport ne compte que s'il peut REPONDRE. rtpMIDI est bidirectionnel,
  // c'est aujourd'hui le seul du moteur — ne rien annoncer d'autre.
  JsonArray transports = doc["transports"].to<JsonArray>();
  transports.add("rtpMIDI");

  JsonObject counters = doc["counters"].to<JsonObject>();
  counters["identityRequests"] = d.handshakeRequests;
  counters["descriptorChunkRequests"] = d.chunkRequests;
  counters["invalidSysEx"] = d.invalidPackets;
  counters["rateLimited"] = d.rateLimited;
  counters["outOfRangeChunks"] = d.outOfRangeChunks;
  counters["notificationsSent"] = d.notificationsSent;
  counters["httpDescriptorReads"] = d.httpDescriptorReads;
  counters["rebuilds"] = d.rebuilds;
  counters["sysexSent"] = _midiEngine ? _midiEngine->getSysExSent() : 0;

  // Horodatages bruts + `now` : c'est l'interface qui calcule les "il y a N s",
  // sans avoir a supposer une horloge murale que le moteur n'a pas.
  JsonObject last = doc["last"].to<JsonObject>();
  last["now"] = (uint32_t)millis();
  last["requestMs"] = d.lastRequestMs;
  last["transferMs"] = d.lastTransferMs;
  last["notificationMs"] = d.lastNotificationMs;
  last["rebuildMs"] = d.lastRebuildMs;

  JsonArray insts = doc["instruments"].to<JsonArray>();
  for (uint8_t i = 0; i < snap.instrumentCount; i++) {
    const GmbInstrumentCaps& e = snap.instruments[i];
    JsonObject o = insts.add<JsonObject>();
    o["channel"] = e.channel;             // 0-based, comme le descripteur
    o["userChannel"] = e.channel + 1;     // 1-based, comme l'affichage MIDI usuel
    o["configured"] = (e.flags & GMB_INST_CONFIGURED) != 0;
    o["noteCount"] = e.notes.count();
    o["ccCount"] = e.ccs.count();
    o["polyphony"] = e.polyphonyMax;
    o["velocity"] = (e.flags & GMB_INST_VELOCITY) != 0;
    if (e.hihatPedalCc != 0xFF) o["hihatPedalCc"] = e.hihatPedalCc;

    JsonArray notes = o["notes"].to<JsonArray>();
    for (uint16_t n = 0; n < 128; n++) if (e.notes.has((uint8_t)n)) notes.add(n);
    JsonArray ccs = o["ccs"].to<JsonArray>();
    for (uint16_t c = 0; c < 128; c++) if (e.ccs.has((uint8_t)c)) ccs.add(c);

    JsonObject t = o["timing"].to<JsonObject>();
    if (e.timing.known & GMB_T_PREPARE)        t["prepareMaxMs"] = e.timing.prepareMaxMs;
    if (e.timing.known & GMB_T_EXCITE)         t["exciteLatencyMs"] = e.timing.exciteLatencyMs;
    if (e.timing.known & GMB_T_REARTICULATION) t["rearticulationMs"] = e.timing.rearticulationMs;
    if (e.timing.known & GMB_T_RELEASE)        t["releaseMs"] = e.timing.releaseMs;
    t["jitterMs"] = e.timing.jitterMs;

    JsonArray voices = o["voices"].to<JsonArray>();
    for (uint8_t m = e.mechFirst; m < e.mechFirst + e.mechCount; m++) {
      const GmbMechanism& mech = snap.mechanisms[m];
      JsonObject v = voices.add<JsonObject>();
      v["actuatorId"] = mech.actuatorId;
      v["role"] = gmbRoleName((GmbRole)mech.role);
      v["noteCount"] = mech.notes.count();
      v["velocity"] = (mech.flags & GMB_MECH_VELOCITY) != 0;
      if (mech.rearticulationMs > 0) v["rearticulationMs"] = mech.rearticulationMs;
    }
  }

  _sendJson(req, 200, doc);
}

void WebServerManager::_refreshGmbCapabilities() {
  if (!_gmbRefreshCb) return;
  // Meme verrou que la recompilation des pipelines : le cache du descripteur
  // est lu depuis le coeur temps reel (reponses SysEx), il ne doit jamais etre
  // reecrit sous les pieds d'une lecture. Le verrou est reentrant, donc
  // l'appel depuis compilePipelines() reste correct.
  ReconfigLock lock;
  _gmbRefreshCb();
}
