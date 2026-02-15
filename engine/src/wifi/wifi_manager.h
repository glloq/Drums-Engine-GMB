#ifndef ENGINE_WIFI_MANAGER_H
#define ENGINE_WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "../core/config.h"

// ============================================================================
// WiFiManager - Gestion WiFi avec credentials LittleFS
// ============================================================================
// Charge les credentials WiFi depuis /wifi.json sur LittleFS.
// Si le fichier n'existe pas ou la connexion echoue, demarre en mode AP
// avec portail captif pour permettre la configuration via l'API REST.
//
// API REST :
//   GET  /api/wifi   -> statut WiFi (ssid, ip, mode, rssi)
//   POST /api/wifi   -> configurer WiFi ({"ssid":"...","password":"..."})
//
// Format /wifi.json :
//   {"ssid": "MyNetwork", "password": "MyPassword", "hostname": "midipercussion"}
// ============================================================================

#define WIFI_CONFIG_FILE  "/wifi.json"
#define WIFI_MAX_ATTEMPTS 30    // 30 x 500ms = 15s timeout

class WiFiManager {
public:
  WiFiManager();

  // Initialiser le WiFi (charger credentials, connecter ou demarrer AP)
  // Doit etre appele apres LittleFS.begin()
  bool begin();

  // Enregistrer les routes API sur un serveur web existant
  void registerRoutes(AsyncWebServer* server);

  // Etat de la connexion
  bool isConnected() const;
  bool isAPMode() const;
  String getSSID() const;
  String getIP() const;
  String getHostname() const;

private:
  bool _apMode;
  String _ssid;
  String _password;
  String _hostname;

  // Charger les credentials depuis /wifi.json
  bool _loadCredentials();

  // Sauvegarder les credentials dans /wifi.json
  bool _saveCredentials(const char* ssid, const char* password, const char* hostname);

  // Tenter la connexion STA
  bool _connectSTA();

  // Demarrer le mode AP
  void _startAP();

  // Demarrer mDNS avec les services
  void _startMDNS();

  // Handlers API
  void _handleGetWifi(AsyncWebServerRequest* req);
  void _handlePostWifi(AsyncWebServerRequest* req, uint8_t* data, size_t len);

  // Helpers
  void _sendJson(AsyncWebServerRequest* req, int code, const JsonDocument& doc);
  void _sendError(AsyncWebServerRequest* req, int code, const char* msg);
};

#endif // ENGINE_WIFI_MANAGER_H
