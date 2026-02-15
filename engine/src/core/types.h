#ifndef ENGINE_TYPES_H
#define ENGINE_TYPES_H

#include <Arduino.h>

// ============================================================================
// Drums Engine - Types fondamentaux
// ============================================================================
// Structures ultra-legeres, zero allocation dynamique
// Toutes les unites temporelles sont en microsecondes
// ============================================================================

// --- Types d'entree ---
enum class InputType : uint8_t {
  NOTE = 0,
  CC
};

// --- Types de blocs pipeline ---
enum class BlockType : uint8_t {
  CONDITION = 0,
  TRANSFORM,
  TIME,
  OUTPUT
};

// --- Types d'actionneurs ---
enum class ActuatorType : uint8_t {
  SERVO = 0,
  SOLENOID,
  PWM_MOTOR,
  MOTOR_OPTICAL,
  TYPE_COUNT
};

// --- Sous-types d'actionneurs (comportement specifique) ---
enum class ActuatorBehavior : uint8_t {
  SOLENOID_STRIKE = 0,   // Frappe simple on/off
  SOLENOID_HOLD,         // Maintien avec PWM
  SERVO_POSITION,        // Position continue (hi-hat, choke)
  SERVO_STRIKE,          // Oscillation (maracas, shaker)
  COMB_BRUSH,            // Peigne/brosse
  PITCH_BEND,            // Tension de peau
  MOTOR_OPTICAL_TRACK,    // Moteur avec capteur optique
  HIHAT_CONTROLLER,       // Servo hi-hat pedal controller (CC#4, splash detection)
  BEHAVIOR_COUNT
};

// --- Type de bus hardware ---
enum class HardwareBus : uint8_t {
  MCP23017 = 0,          // GPIO via I2C
  PCA9685,               // PWM via I2C
  GPIO_DIRECT,           // GPIO direct ESP32
  LEDC_PWM               // PWM direct ESP32 (LEDC)
};

// --- Types de commandes actuator ---
enum class CommandType : uint8_t {
  PULSE = 0,             // Impulsion (solenoide)
  POSITION,              // Position (servo)
  PWM,                   // PWM continu (moteur)
  OFF                    // Arret
};

enum class ValueSource : uint8_t {
  VELOCITY = 0,
  FIXED,
  CC_VAR,
  INVERTED_VELOCITY
};

enum class RetriggerMode : uint8_t {
  IGNORE = 0,
  RESET,
  STACK
};

// --- Evenement MIDI interne ---
struct MidiEvent {
  uint8_t type;          // 0=note_on, 1=note_off, 2=cc, 3=pitch_bend, 4=aftertouch
  uint8_t channel;
  uint8_t data1;         // note or CC number
  uint8_t data2;         // velocity or CC value
  uint32_t timestamp;    // microseconds (micros())
};

// MIDI event types
constexpr uint8_t MIDI_EVT_NOTE_ON     = 0;
constexpr uint8_t MIDI_EVT_NOTE_OFF    = 1;
constexpr uint8_t MIDI_EVT_CC         = 2;
constexpr uint8_t MIDI_EVT_PITCH_BEND = 3;
constexpr uint8_t MIDI_EVT_AFTERTOUCH = 4;

// --- Commande Actuator (structure universelle) ---
struct ActuatorCommand {
  uint8_t actuator_id;
  uint8_t command_type;    // CommandType
  uint16_t value;          // valeur (angle, PWM, intensite)
  uint16_t duration;       // duree en microsecondes / 100 (pour tenir dans 16 bits)
  uint32_t execute_at;     // timestamp d'execution en microsecondes
};

struct ActionStep {
  uint8_t actuator_id;
  uint8_t command_type;
  uint8_t value_source;
  uint8_t value_fixed;
  uint8_t cc_index;
  uint16_t delay_ms;
  uint16_t duration_min_ms;
  uint16_t duration_max_ms;
  uint8_t velocity_curve;

  ActionStep()
    : actuator_id(0xFF), command_type((uint8_t)CommandType::PULSE),
      value_source((uint8_t)ValueSource::VELOCITY), value_fixed(0), cc_index(0),
      delay_ms(0), duration_min_ms(0), duration_max_ms(0), velocity_curve(0) {}
};

struct CCBinding {
  uint8_t cc_number;
  uint8_t actuator_id;
  uint8_t command_type;
  uint8_t range_min;
  uint8_t range_max;
  uint8_t curve;
  bool inverted;

  CCBinding()
    : cc_number(0), actuator_id(0xFF), command_type((uint8_t)CommandType::POSITION),
      range_min(0), range_max(127), curve(0), inverted(false) {}
};

// --- Configuration d'un actionneur ---
struct ActuatorConfig {
  uint8_t id;              // ID unique global
  char name[24];
  ActuatorType type;
  ActuatorBehavior behavior;
  HardwareBus bus;

  uint8_t hwAddress;       // Adresse I2C du module
  uint8_t hwPin;           // Pin/channel sur le module

  uint16_t paramMin;       // Valeur min
  uint16_t paramMax;       // Valeur max
  uint16_t paramDefault;   // Valeur repos

  uint16_t cooldownUs;     // Temps minimum entre activations (us/100)
  uint32_t lastActivation; // Timestamp derniere activation

  bool enabled;
  bool inverted;

  ActuatorConfig()
    : id(0), type(ActuatorType::SOLENOID), behavior(ActuatorBehavior::SOLENOID_STRIKE),
      bus(HardwareBus::MCP23017), hwAddress(0x20), hwPin(0),
      paramMin(8), paramMax(30), paramDefault(15),
      cooldownUs(200), lastActivation(0),
      enabled(true), inverted(false) {
    name[0] = '\0';
  }
};

// --- Configuration d'un instrument ---
struct InstrumentConfig {
  uint8_t id;
  char name[32];
  char category[16];       // "drums", "cymbals", "latin", "other"

  uint8_t midiNote;        // Note MIDI principale (0-127)
  uint8_t midiChannel;     // Canal MIDI (0=all, 1-16)

  uint8_t actuatorIds[MAX_ACTUATORS_PER_INST];  // IDs des actionneurs
  uint8_t actuatorCount;

  uint8_t pipelineId;      // Index dans la table des pipelines

  ActionStep noteOnActions[MAX_ACTIONS_PER_EVENT];
  uint8_t noteOnCount;

  ActionStep noteOffActions[MAX_ACTIONS_PER_EVENT];
  uint8_t noteOffCount;

  CCBinding ccBindings[MAX_CC_BINDINGS];
  uint8_t ccBindingCount;

  uint8_t retriggerMode;

  bool enabled;

  InstrumentConfig()
    : id(0), midiNote(36), midiChannel(10),
      actuatorCount(0), pipelineId(0xFF), noteOnCount(0), noteOffCount(0),
      ccBindingCount(0), retriggerMode((uint8_t)RetriggerMode::IGNORE), enabled(true) {
    name[0] = '\0';
    strcpy(category, "drums");
    memset(actuatorIds, 0xFF, sizeof(actuatorIds));
  }
};

// --- Bloc compile pour pipeline ---
struct CompiledBlock {
  uint8_t type;            // BlockType
  uint8_t subtype;         // Sous-type specifique au bloc
  uint8_t param1;
  uint16_t param2;
};

// Sous-types pour CONDITION
constexpr uint8_t COND_THRESHOLD     = 0;  // Seuil
constexpr uint8_t COND_ROUND_ROBIN   = 1;  // Alternance
constexpr uint8_t COND_VELOCITY_SPLIT = 2; // Split velocite

// Sous-types pour TRANSFORM
constexpr uint8_t TRANS_VELOCITY_CURVE = 0; // Courbe velocite
constexpr uint8_t TRANS_GAIN          = 1;  // Gain/scale
constexpr uint8_t TRANS_CLAMP         = 2;  // Limiteur

// Sous-types pour TIME
constexpr uint8_t TIME_PULSE   = 0;  // Impulsion
constexpr uint8_t TIME_DELAY   = 1;  // Delai
constexpr uint8_t TIME_RAMP    = 2;  // Ramping

// Sous-types pour OUTPUT
constexpr uint8_t OUT_PULSE    = 0;  // Sortie impulsion
constexpr uint8_t OUT_POSITION = 1;  // Sortie position
constexpr uint8_t OUT_PWM      = 2;  // Sortie PWM

// Courbes de velocite
constexpr uint8_t CURVE_LINEAR = 0;
constexpr uint8_t CURVE_EXPO   = 1;
constexpr uint8_t CURVE_LOG    = 2;

// --- Pipeline compile ---


struct CCRoutingEntry {
  uint8_t actuator_id;
  uint8_t command_type;
  uint8_t range_min;
  uint8_t range_max;
  uint8_t curve;
  bool inverted;

  CCRoutingEntry()
    : actuator_id(0xFF), command_type((uint8_t)CommandType::POSITION),
      range_min(0), range_max(127), curve(0), inverted(false) {}
};

struct CompiledPipeline {
  uint8_t block_count;
  uint8_t output_actuator_id;   // Actionneur de sortie
  CompiledBlock blocks[MAX_BLOCKS_PER_PIPELINE];
  ActionStep note_on_actions[MAX_ACTIONS_PER_EVENT];
  uint8_t note_on_count;
  ActionStep note_off_actions[MAX_ACTIONS_PER_EVENT];
  uint8_t note_off_count;
  CCBinding cc_bindings[MAX_CC_BINDINGS];
  uint8_t cc_binding_count;
  uint8_t retrigger_mode;

  CompiledPipeline()
    : block_count(0), output_actuator_id(0xFF),
      note_on_count(0), note_off_count(0), cc_binding_count(0),
      retrigger_mode((uint8_t)RetriggerMode::IGNORE) {
    memset(blocks, 0, sizeof(blocks));
  }
};

// --- Table de lookup note -> pipeline O(1) ---
struct PipelineLookup {
  uint8_t note_to_pipeline[128];     // -1 (0xFF) = pas de pipeline
  CompiledPipeline pipelines[MAX_PIPELINES];
  uint8_t pipeline_count;

  CCRoutingEntry cc_routes[MAX_CC_ROUTES];
  uint8_t cc_route_count;
  uint8_t cc_to_first[128];          // 0xFF = aucun
  uint8_t cc_to_count[128];          // nombre d'entrees consecutives
  uint16_t cc_route_dropped;         // routes ignorees (table pleine)

  PipelineLookup() : pipeline_count(0), cc_route_count(0), cc_route_dropped(0) {
    memset(note_to_pipeline, 0xFF, sizeof(note_to_pipeline));
    memset(cc_to_first, 0xFF, sizeof(cc_to_first));
    memset(cc_to_count, 0, sizeof(cc_to_count));
  }
};

// --- Etat global accessible aux blocs CONDITION ---
struct GlobalState {
  uint16_t variables[MAX_GLOBAL_VARS];
  uint8_t round_robin_counters[MAX_PIPELINES]; // Compteurs alternance

  GlobalState() {
    memset(variables, 0, sizeof(variables));
    memset(round_robin_counters, 0, sizeof(round_robin_counters));
  }
};

// --- Loop structures ---
struct LoopEvent {
  uint16_t tickPosition;
  uint8_t midiNote;
  uint8_t velocity;
  uint8_t channel;
};

struct Loop {
  uint8_t id;
  char name[32];
  uint16_t bpm;
  uint8_t bars;
  uint8_t beatsPerBar;
  uint8_t beatValue;
  uint16_t ppq;
  uint16_t eventCount;
  LoopEvent events[MAX_LOOP_EVENTS];
  bool active;
};

// --- Noms lisibles pour l'UI ---
inline const char* actuatorTypeName(ActuatorType t) {
  switch(t) {
    case ActuatorType::SERVO:      return "Servo";
    case ActuatorType::SOLENOID:   return "Solenoid";
    case ActuatorType::PWM_MOTOR:  return "PWM Motor";
    case ActuatorType::MOTOR_OPTICAL: return "Motor Optical";
    default:                       return "Unknown";
  }
}

inline const char* actuatorBehaviorName(ActuatorBehavior b) {
  switch(b) {
    case ActuatorBehavior::SOLENOID_STRIKE: return "Solenoid Strike";
    case ActuatorBehavior::SOLENOID_HOLD:   return "Solenoid Hold";
    case ActuatorBehavior::SERVO_POSITION:  return "Servo Position";
    case ActuatorBehavior::SERVO_STRIKE:    return "Servo Strike";
    case ActuatorBehavior::COMB_BRUSH:      return "Comb/Brush";
    case ActuatorBehavior::PITCH_BEND:      return "Pitch Bend";
    case ActuatorBehavior::MOTOR_OPTICAL_TRACK: return "Motor Optical";
    case ActuatorBehavior::HIHAT_CONTROLLER:    return "Hi-Hat Controller";
    default:                               return "Unknown";
  }
}

inline const char* hardwareBusName(HardwareBus b) {
  switch(b) {
    case HardwareBus::MCP23017:    return "MCP23017 (I2C GPIO)";
    case HardwareBus::PCA9685:     return "PCA9685 (I2C PWM)";
    case HardwareBus::GPIO_DIRECT: return "ESP32 GPIO";
    case HardwareBus::LEDC_PWM:    return "ESP32 LEDC PWM";
    default:                       return "Unknown";
  }
}

// Notes MIDI GM Percussion
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
constexpr uint8_t GM_DRUM_NOTES_COUNT = sizeof(GM_DRUM_NOTES) / sizeof(GM_DRUM_NOTES[0]);

#endif // ENGINE_TYPES_H
