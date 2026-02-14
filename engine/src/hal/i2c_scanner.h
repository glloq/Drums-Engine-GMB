#ifndef I2C_SCANNER_H
#define I2C_SCANNER_H

#include <Arduino.h>
#include <Wire.h>
#include "../core/config.h"

// ============================================================================
// I2C Bus Scanner - Detection des modules presents
// ============================================================================

class I2CScanner {
public:
  // Initialiser le bus I2C
  static void begin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(I2C_FREQ);
    DBGF("[I2C] Bus initialized (SDA=%d, SCL=%d, %dHz)\n", I2C_SDA, I2C_SCL, I2C_FREQ);
  }

  // Scanner et retourner la liste des peripheriques detectes
  static String scan() {
    String result = "";
    for (uint8_t addr = 1; addr < 127; addr++) {
      Wire.beginTransmission(addr);
      if (Wire.endTransmission() == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "0x%02X", addr);
        if (result.length() > 0) result += ", ";
        result += buf;

        if (addr >= 0x20 && addr <= 0x27) {
          result += " (MCP23017)";
        } else if (addr >= 0x40 && addr <= 0x7F) {
          result += " (PCA9685)";
        }
      }
    }
    return result.length() > 0 ? result : "No devices found";
  }

  // Verifier si un peripherique est present a une adresse
  static bool isPresent(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  }
};

#endif // I2C_SCANNER_H
