#ifndef ACTUATOR_TYPES_H
#define ACTUATOR_TYPES_H

#include <Arduino.h>

// ============================================================================
// Actuator type definitions for MidiPercussion V4
// ============================================================================

// Types d'actionneurs physiques
enum ActuatorType : uint8_t {
  ACT_SOLENOID_STRIKE = 0,   // Frappe simple on/off (MCP23017 GPIO)
  ACT_SOLENOID_HOLD,         // Solénoïde avec maintien PWM (PCA9685)
  ACT_SERVO_POSITION,        // Servo position (ouvert/fermé) - hi-hat, choke
  ACT_SERVO_STRIKE,          // Servo oscillant (maracas, shaker)
  ACT_COMB_BRUSH,            // Peigne/brosse - servo + petites baguettes plastique
  ACT_PITCH_BEND,            // Tension de peau pour pitch bend (servo/linear)
  ACT_TYPE_COUNT              // Nombre total de types
};

// Quand l'actionneur s'exécute par rapport au trigger
enum ActuatorTiming : uint8_t {
  TIMING_PRE_STRIKE = 0,     // Avant la frappe principale (ex: position hi-hat)
  TIMING_ON_STRIKE,          // Avec la frappe (ex: solénoïde kick)
  TIMING_POST_STRIKE,        // Après la frappe (ex: choke cymbal)
  TIMING_ON_RELEASE,         // Sur note off (ex: relâchement)
  TIMING_CONTINUOUS          // Continu tant que note active (ex: pitch bend)
};

// Source de contrôle pour l'actionneur
enum ActuatorControl : uint8_t {
  CTRL_NOTE_VELOCITY = 0,    // Vélocité de la note MIDI
  CTRL_CC,                   // Control Change MIDI
  CTRL_PITCH_BEND,           // Pitch Bend MIDI
  CTRL_AFTERTOUCH,           // Aftertouch/pression
  CTRL_FIXED                 // Valeur fixe (pas de contrôle dynamique)
};

// Type de bus hardware
enum HardwareBus : uint8_t {
  BUS_MCP23017 = 0,          // GPIO via I2C (solénoïdes on/off)
  BUS_PCA9685,               // PWM via I2C (servos, solénoïdes PWM)
  BUS_GPIO_DIRECT,           // GPIO direct ESP32
  BUS_LEDC_PWM               // PWM direct ESP32 (LEDC)
};

// Configuration complète d'un actionneur
struct ActuatorConfig {
  char name[24];              // Nom lisible ("Frappe principale", "Choke", etc.)
  ActuatorType type;          // Type d'actionneur
  ActuatorTiming timing;      // Quand s'exécuter
  ActuatorControl control;    // Source de contrôle
  HardwareBus bus;            // Bus hardware

  uint8_t hwAddress;          // Adresse I2C du module (0x20-0x27, 0x40-0x7F)
  uint8_t hwPin;              // Pin/channel sur le module (0-15)

  uint16_t paramMin;          // Valeur min (durée ms ou angle °)
  uint16_t paramMax;          // Valeur max (durée ms ou angle °)
  uint16_t paramDefault;      // Valeur par défaut

  int16_t delayMs;            // Délai relatif au trigger (ms)

  uint8_t ccNumber;           // Numéro CC si control=CTRL_CC (0-127)
  bool enabled;               // Actionneur actif ou non

  // Valeurs par défaut
  ActuatorConfig() : type(ACT_SOLENOID_STRIKE), timing(TIMING_ON_STRIKE),
    control(CTRL_NOTE_VELOCITY), bus(BUS_MCP23017),
    hwAddress(0x20), hwPin(0), paramMin(8), paramMax(30), paramDefault(15),
    delayMs(0), ccNumber(0), enabled(true) {
    name[0] = '\0';
  }
};

// Configuration d'un instrument complet
struct InstrumentConfig {
  uint8_t id;                 // ID unique (0-255)
  char name[32];              // Nom ("Grosse Caisse", "Hi-Hat", etc.)
  char category[16];          // Catégorie ("drums", "cymbals", "latin", "other")

  uint8_t midiNote;           // Note MIDI principale (0-127)
  uint8_t midiChannel;        // Canal MIDI (0=all, 1-16)

  uint8_t actuatorCount;      // Nombre d'actionneurs
  ActuatorConfig actuators[MAX_ACTUATORS_PER_INST];

  bool enabled;               // Instrument actif

  InstrumentConfig() : id(0), midiNote(36), midiChannel(10),
    actuatorCount(0), enabled(true) {
    name[0] = '\0';
    strcpy(category, "drums");
  }
};

// Noms lisibles pour l'UI
inline const char* actuatorTypeName(ActuatorType t) {
  switch(t) {
    case ACT_SOLENOID_STRIKE: return "Solenoid Strike";
    case ACT_SOLENOID_HOLD:   return "Solenoid Hold";
    case ACT_SERVO_POSITION:  return "Servo Position";
    case ACT_SERVO_STRIKE:    return "Servo Strike";
    case ACT_COMB_BRUSH:      return "Comb/Brush";
    case ACT_PITCH_BEND:      return "Pitch Bend";
    default:                  return "Unknown";
  }
}

inline const char* actuatorTimingName(ActuatorTiming t) {
  switch(t) {
    case TIMING_PRE_STRIKE:  return "Pre-Strike";
    case TIMING_ON_STRIKE:   return "On Strike";
    case TIMING_POST_STRIKE: return "Post-Strike";
    case TIMING_ON_RELEASE:  return "On Release";
    case TIMING_CONTINUOUS:  return "Continuous";
    default:                 return "Unknown";
  }
}

inline const char* actuatorControlName(ActuatorControl c) {
  switch(c) {
    case CTRL_NOTE_VELOCITY: return "Note Velocity";
    case CTRL_CC:            return "Control Change";
    case CTRL_PITCH_BEND:    return "Pitch Bend";
    case CTRL_AFTERTOUCH:    return "Aftertouch";
    case CTRL_FIXED:         return "Fixed Value";
    default:                 return "Unknown";
  }
}

inline const char* hardwareBusName(HardwareBus b) {
  switch(b) {
    case BUS_MCP23017:   return "MCP23017 (I2C GPIO)";
    case BUS_PCA9685:    return "PCA9685 (I2C PWM)";
    case BUS_GPIO_DIRECT: return "ESP32 GPIO";
    case BUS_LEDC_PWM:   return "ESP32 LEDC PWM";
    default:             return "Unknown";
  }
}

// Notes MIDI General MIDI Percussion (pour référence dans l'UI)
struct MidiNoteInfo {
  uint8_t note;
  const char* name;
};

const MidiNoteInfo GM_DRUM_NOTES[] = {
  {27, "High Q"}, {28, "Slap"}, {29, "Scratch Push"}, {30, "Scratch Pull"},
  {31, "Sticks"}, {32, "Square Click"}, {33, "Metronome Click"}, {34, "Metronome Bell"},
  {35, "Acoustic Bass Drum"}, {36, "Bass Drum 1"}, {37, "Side Stick"},
  {38, "Acoustic Snare"}, {39, "Hand Clap"}, {40, "Electric Snare"},
  {41, "Low Floor Tom"}, {42, "Closed Hi-Hat"}, {43, "High Floor Tom"},
  {44, "Pedal Hi-Hat"}, {45, "Low Tom"}, {46, "Open Hi-Hat"},
  {47, "Low-Mid Tom"}, {48, "Hi-Mid Tom"}, {49, "Crash Cymbal 1"},
  {50, "High Tom"}, {51, "Ride Cymbal 1"}, {52, "Chinese Cymbal"},
  {53, "Ride Bell"}, {54, "Tambourine"}, {55, "Splash Cymbal"},
  {56, "Cowbell"}, {57, "Crash Cymbal 2"}, {58, "Vibraslap"},
  {59, "Ride Cymbal 2"}, {60, "Hi Bongo"}, {61, "Low Bongo"},
  {62, "Mute Hi Conga"}, {63, "Open Hi Conga"}, {64, "Low Conga"},
  {65, "High Timbale"}, {66, "Low Timbale"}, {67, "High Agogo"},
  {68, "Low Agogo"}, {69, "Cabasa"}, {70, "Maracas"},
  {71, "Short Whistle"}, {72, "Long Whistle"}, {73, "Short Guiro"},
  {74, "Long Guiro"}, {75, "Claves"}, {76, "Hi Wood Block"},
  {77, "Low Wood Block"}, {78, "Mute Cuica"}, {79, "Open Cuica"},
  {80, "Mute Triangle"}, {81, "Open Triangle"}
};
const uint8_t GM_DRUM_NOTES_COUNT = sizeof(GM_DRUM_NOTES) / sizeof(GM_DRUM_NOTES[0]);

#endif // ACTUATOR_TYPES_H
