#include "gmb_identity.h"

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#include <esp_system.h>
#include <esp_mac.h>
#endif

uint32_t gmbInstanceIdFromMac(const uint8_t mac[6]) {
  if (!mac) return 0x474D4201u;  // "GMB\x01" — jamais 0, jamais une vraie MAC

  uint32_t hash = 2166136261u;   // FNV-1a offset basis
  for (uint8_t i = 0; i < 6; i++) {
    hash ^= (uint32_t)mac[i];
    hash *= 16777619u;           // FNV-1a prime
  }
  // 0 est reserve a "pas d'identite" : le repliement ne doit jamais y tomber.
  if (hash == 0) hash = 0x474D4201u;
  return hash;
}

uint32_t gmbInstanceId() {
#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
  static uint32_t cached = 0;
  if (cached != 0) return cached;

  uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  // MAC de BASE (eFuse), pas celle de l'interface Wi-Fi : elle est gravee en
  // usine et ne bouge ni avec le mode STA/AP ni avec une MAC surchargee.
  if (esp_efuse_mac_get_default(mac) != ESP_OK) {
    // Repli : esp_read_mac lit la meme source par un autre chemin.
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
  }
  cached = gmbInstanceIdFromMac(mac);
  return cached;
#else
  return 0;
#endif
}
