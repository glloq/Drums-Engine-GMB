#include "wifi_manager.h"

WiFiManager::WiFiManager()
  : _apMode(false),
    _hostname(WIFI_HOSTNAME),
    _dnsRunning(false) {}

// ============================================================================
// begin() - Point d'entree principal
// ============================================================================

bool WiFiManager::begin() {
  // Charger les credentials depuis LittleFS
  bool hasCredentials = _loadCredentials();

  if (hasCredentials) {
    // Tenter la connexion STA
    if (_connectSTA()) {
      _startMDNS();
      return true;
    }
  }

  // Pas de credentials ou connexion echouee -> mode AP
  DBGLN("[WiFi] No valid credentials or connection failed, starting AP mode");
  _startAP();
  _startMDNS();
  return true;
}

// ============================================================================
// update() - Traiter les requetes DNS captives (appeler regulierement)
// ============================================================================

void WiFiManager::update() {
  if (_dnsRunning) {
    _dnsServer.processNextRequest();
  }
}

// ============================================================================
// switchToAP() - Basculer en mode AP depuis STA (bouton BOOT)
// ============================================================================

void WiFiManager::switchToAP() {
  if (_apMode) return;  // Deja en mode AP

  DBGLN("[WiFi] Switching to AP mode (button press)");

  // Deconnecter le STA
  WiFi.disconnect(true);
  delay(100);

  // Demarrer le mode AP + DNS captif
  _startAP();
  _startMDNS();
}

// ============================================================================
// registerRoutes() - Enregistrer les endpoints API WiFi
// ============================================================================

void WiFiManager::registerRoutes(AsyncWebServer* server) {
  server->on("/api/wifi", HTTP_GET,
    [this](AsyncWebServerRequest* req) { _handleGetWifi(req); });

  server->on("/api/wifi", HTTP_POST,
    [](AsyncWebServerRequest* req) {},
    nullptr,
    [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      if (index == 0) _handlePostWifi(req, data, len);
    });

  // Captive portal: rediriger les requetes de detection de portail
  // Android/iOS/Windows envoient des requetes specifiques pour detecter les portails captifs
  server->on("/generate_204", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->redirect("http://" + getIP() + "/");
  });
  server->on("/hotspot-detect.html", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->redirect("http://" + getIP() + "/");
  });
  server->on("/connecttest.txt", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->redirect("http://" + getIP() + "/");
  });
  server->on("/canonical.html", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->redirect("http://" + getIP() + "/");
  });
  server->on("/success.txt", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->send(200, "text/plain", "success");
  });
  server->on("/ncsi.txt", HTTP_GET, [this](AsyncWebServerRequest* req) {
    req->redirect("http://" + getIP() + "/");
  });
}

// ============================================================================
// Etat
// ============================================================================

bool WiFiManager::isConnected() const {
  return !_apMode && WiFi.status() == WL_CONNECTED;
}

bool WiFiManager::isAPMode() const {
  return _apMode;
}

String WiFiManager::getSSID() const {
  if (_apMode) return String(WIFI_AP_SSID);
  return WiFi.SSID();
}

String WiFiManager::getIP() const {
  if (_apMode) return WiFi.softAPIP().toString();
  return WiFi.localIP().toString();
}

String WiFiManager::getHostname() const {
  return _hostname;
}

// ============================================================================
// Charger les credentials depuis /wifi.json
// ============================================================================

bool WiFiManager::_loadCredentials() {
  if (!LittleFS.exists(WIFI_CONFIG_FILE)) {
    DBGLN("[WiFi] No wifi.json found");
    return false;
  }

  File file = LittleFS.open(WIFI_CONFIG_FILE, "r");
  if (!file) {
    DBGLN("[WiFi] Failed to open wifi.json");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();

  if (err) {
    DBGF("[WiFi] JSON parse error in wifi.json: %s\n", err.c_str());
    return false;
  }

  const char* ssid = doc["ssid"] | "";
  const char* password = doc["password"] | "";
  const char* hostname = doc["hostname"] | WIFI_HOSTNAME;

  if (strlen(ssid) == 0) {
    DBGLN("[WiFi] wifi.json has empty SSID");
    return false;
  }

  _ssid = String(ssid);
  _password = String(password);
  _hostname = String(hostname);

  DBGF("[WiFi] Loaded credentials for '%s' (hostname: %s)\n",
       _ssid.c_str(), _hostname.c_str());
  return true;
}

// ============================================================================
// Sauvegarder les credentials dans /wifi.json
// ============================================================================

bool WiFiManager::_saveCredentials(const char* ssid, const char* password, const char* hostname) {
  JsonDocument doc;
  doc["ssid"] = ssid;
  doc["password"] = password;
  doc["hostname"] = (hostname && strlen(hostname) > 0) ? hostname : WIFI_HOSTNAME;

  File file = LittleFS.open(WIFI_CONFIG_FILE, "w");
  if (!file) {
    DBGLN("[WiFi] Failed to open wifi.json for writing");
    return false;
  }

  size_t written = serializeJson(doc, file);
  file.close();

  DBGF("[WiFi] Saved credentials to wifi.json (%d bytes)\n", written);
  return written > 0;
}

// ============================================================================
// Connexion STA
// ============================================================================

bool WiFiManager::_connectSTA() {
  DBGF("[WiFi] Connecting to '%s'...\n", _ssid.c_str());

  // Arreter le DNS captif si actif
  _stopDNS();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(_hostname.c_str());
  WiFi.begin(_ssid.c_str(), _password.c_str());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < WIFI_MAX_ATTEMPTS) {
    vTaskDelay(pdMS_TO_TICKS(500));
    DBG(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    DBGLN("");
    DBGF("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
    _apMode = false;
    return true;
  }

  DBGLN("\n[WiFi] STA connection failed!");
  WiFi.disconnect(true);
  return false;
}

// ============================================================================
// Mode AP + DNS captif
// ============================================================================

void WiFiManager::_startAP() {
  DBGF("[WiFi] Starting AP: '%s'\n", WIFI_AP_SSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
  _apMode = true;

  // Demarrer le serveur DNS captif : toutes les requetes DNS -> IP du AP
  _dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  _dnsRunning = true;

  DBGF("[WiFi] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  DBGLN("[WiFi] Captive DNS started - all domains redirect to AP");
}

void WiFiManager::_stopDNS() {
  if (_dnsRunning) {
    _dnsServer.stop();
    _dnsRunning = false;
    DBGLN("[WiFi] Captive DNS stopped");
  }
}

// ============================================================================
// mDNS
// ============================================================================

void WiFiManager::_startMDNS() {
  if (MDNS.begin(_hostname.c_str())) {
    MDNS.addService("http", "tcp", WEB_PORT);
    if (!_apMode) {
      MDNS.addService("apple-midi", "udp", RTP_MIDI_PORT);
    }
    DBGF("[mDNS] %s.local\n", _hostname.c_str());
  }
}

// ============================================================================
// API Handlers
// ============================================================================

void WiFiManager::_handleGetWifi(AsyncWebServerRequest* req) {
  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();

  obj["mode"] = _apMode ? "ap" : "sta";
  obj["ssid"] = getSSID();
  obj["ip"] = getIP();
  obj["hostname"] = _hostname;

  if (!_apMode && WiFi.status() == WL_CONNECTED) {
    obj["rssi"] = WiFi.RSSI();
    obj["connected"] = true;
  } else {
    obj["connected"] = false;
  }

  if (_apMode) {
    obj["ap_ssid"] = WIFI_AP_SSID;
    obj["ap_clients"] = WiFi.softAPgetStationNum();
  }

  _sendJson(req, 200, doc);
}

void WiFiManager::_handlePostWifi(AsyncWebServerRequest* req, uint8_t* data, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, data, len)) {
    _sendError(req, 400, "Invalid JSON");
    return;
  }

  const char* ssid = doc["ssid"] | "";
  const char* password = doc["password"] | "";
  const char* hostname = doc["hostname"] | _hostname.c_str();

  if (strlen(ssid) == 0) {
    _sendError(req, 400, "SSID is required");
    return;
  }

  if (strlen(ssid) > 32) {
    _sendError(req, 400, "SSID too long (max 32 chars)");
    return;
  }

  if (strlen(password) > 63) {
    _sendError(req, 400, "Password too long (max 63 chars)");
    return;
  }

  // Sauvegarder les nouveaux credentials
  if (!_saveCredentials(ssid, password, hostname)) {
    _sendError(req, 500, "Failed to save WiFi credentials");
    return;
  }

  // Repondre avant de redemarrer
  JsonDocument resp;
  resp["message"] = "WiFi credentials saved. Restarting...";
  resp["ssid"] = ssid;
  resp["hostname"] = hostname;
  _sendJson(req, 200, resp);

  // Laisser le temps a la reponse HTTP d'etre envoyee
  DBGF("[WiFi] New credentials saved for '%s', restarting in 1s...\n", ssid);
  delay(1000);
  ESP.restart();
}

// ============================================================================
// Helpers
// ============================================================================

void WiFiManager::_sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc) {
  String json;
  serializeJson(doc, json);
  req->send(code, "application/json", json);
}

void WiFiManager::_sendError(AsyncWebServerRequest* req, int code, const char* msg) {
  JsonDocument doc;
  doc["message"] = msg;
  _sendJson(req, code, doc);
}
