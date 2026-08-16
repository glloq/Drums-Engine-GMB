#ifndef ACTUATOR_FACTORY_H
#define ACTUATOR_FACTORY_H

#include "../core/types.h"
#include "../core/config.h"
#include "actuator.h"
#include "actuator_manager.h"
#include "solenoid_actuator.h"
#include "servo_actuator.h"
#include "motor_actuator.h"
#include "stepper_actuator.h"
#include <new>
#include <stddef.h>
#include "../hal/mcp23017_driver.h"
#include "../hal/pca9685_driver.h"
#include "../hal/gpio_driver.h"
#include "../hal/ledc_driver.h"

// ============================================================================
// ActuatorFactory - Creation et enregistrement des actionneurs
// ============================================================================
// Cree les instances Actuator a partir des ActuatorConfig,
// les lie aux bons drivers HAL, et les enregistre dans l'ActuatorManager.
//
// Pool statique pre-alloue (zero allocation dynamique en runtime).
//
// Le pool est TYPE-EFFACE : MAX_ACTUATORS emplacements, chacun dimensionne sur
// le plus gros type d'actionneur, construits par placement new. Auparavant la
// factory tenait MAX_ACTUATORS exemplaires de CHAQUE classe cote a cote, donc
// 4 x MAX_ACTUATORS objets pour au plus MAX_ACTUATORS actionneurs reels — un
// gaspillage qui aurait ete multiplie par deux en passant la limite a 64, et
// aggrave encore par l'ajout d'un cinquieme type.
// ============================================================================

// Dimensions du plus gros actionneur. Au namespace et non dans la classe : une
// fonction membre constexpr ne peut pas etre appelee dans une expression
// constante tant que sa classe est incomplete.
namespace actuator_pool {
constexpr size_t maxOf(size_t a, size_t b) { return a > b ? a : b; }
constexpr size_t kSlotSize =
    maxOf(maxOf(sizeof(SolenoidActuator), sizeof(ServoActuator)),
          maxOf(sizeof(MotorActuator), sizeof(StepperActuator)));
constexpr size_t kSlotAlign =
    maxOf(maxOf(alignof(SolenoidActuator), alignof(ServoActuator)),
          maxOf(alignof(MotorActuator), alignof(StepperActuator)));
}  // namespace actuator_pool

// Un emplacement de pool : de l'octet brut aligne, plus le pointeur vers
// l'objet construit dedans (nullptr = libre).
struct ActuatorSlot {
  alignas(actuator_pool::kSlotAlign) unsigned char storage[actuator_pool::kSlotSize];
  Actuator* live = nullptr;

  // Construire un actionneur de type T dans cet emplacement.
  template <typename T, typename... Args>
  T* emplace(Args&&... args) {
    static_assert(sizeof(T) <= actuator_pool::kSlotSize,
                  "actuator type larger than the pool slot — add it to kSlotSize");
    static_assert(alignof(T) <= actuator_pool::kSlotAlign,
                  "actuator type needs stricter alignment than the pool slot");
    destroy();
    T* obj = new (storage) T(static_cast<Args&&>(args)...);
    live = obj;
    return obj;
  }

  // Detruire l'objet en place. Actuator a un destructeur virtuel, donc le bon
  // destructeur de classe derivee est bien appele (le MotorActuator libere par
  // exemple son emplacement d'ISR optique).
  void destroy() {
    if (live) {
      live->~Actuator();
      live = nullptr;
    }
  }
};

class ActuatorFactory {
public:
  // `mcpSlots`/`pcaSlots` are the SIZES of the driver arrays (MCP_MAX_MODULES /
  // PCA_MAX_MODULES), not the number of modules that answered the I2C scan.
  // Drivers are indexed by (address - base), so a gap in the populated
  // addresses (e.g. 0x20 absent, 0x21 present) must not shrink the addressable
  // range — readiness is decided per slot by MCP23017Driver::isReady().
  ActuatorFactory(ActuatorManager* manager,
                  MCP23017Driver* mcpDrivers, uint8_t mcpSlots,
                  PCA9685Driver* pcaDrivers, uint8_t pcaSlots,
                  GpioDriver* gpioDriver, LedcDriver* ledcDriver);

  // Creer un actionneur a partir de sa config et l'enregistrer
  // Retourne le pointeur vers l'actuateur cree, ou nullptr si erreur
  Actuator* createAndRegister(const ActuatorConfig& config);

  // Creer tous les actionneurs depuis un tableau de configs
  uint8_t createAll(const ActuatorConfig* configs, uint8_t count);

  // Nombre d'actionneurs crees
  uint8_t getCreatedCount() const { return _createdCount; }

  // Acces au pool de configs (pour serialisation)
  const ActuatorConfig& getConfig(uint8_t index) const { return _configPool[index]; }
  ActuatorConfig& getConfig(uint8_t index) { return _configPool[index]; }
  uint8_t getConfigCount() const { return _configCount; }

  // Ajouter une config au pool (depuis JSON, etc.)
  int addConfig(const ActuatorConfig& config);
  bool removeConfig(uint8_t id);
  ActuatorConfig* findConfig(uint8_t id);

  // review #13 (follow-up): shared hardware-resource conflict check.
  // Returns true if `candidate` would fight over a physical GPIO, MCP23017 pin
  // or PCA9685 channel already claimed by another config. `excludeId` is the id
  // to ignore (0xFF for none) — an UPDATE must not conflict with itself.
  // On conflict, `conflictId` receives the id of the offending config.
  //
  // Both the create path (addConfig) and the REST update path go through this,
  // so editing an actuator can no longer move it onto a pin that is already in
  // use — a check that previously existed only at creation time.
  bool hasResourceConflict(const ActuatorConfig& candidate, uint8_t excludeId,
                           uint8_t& conflictId) const;

  // Reconstruire tous les actionneurs depuis le pool de configs.
  // Retourne le nombre d'actionneurs crees, ou REBUILD_BUSY (<0) si le coeur
  // temps reel n'a pas pu etre mis en pause — dans ce cas RIEN n'a ete modifie
  // et l'appelant doit renvoyer une erreur plutot que de supposer le succes.
  static const int REBUILD_BUSY = -1;
  int rebuildAll();

private:
  ActuatorManager* _manager;

  // HAL drivers references. The counts are ARRAY SIZES (addressable slots), not
  // the number of modules present on the bus — see the constructor comment.
  MCP23017Driver* _mcpDrivers;
  uint8_t _mcpSlots;
  PCA9685Driver* _pcaDrivers;
  uint8_t _pcaSlots;
  GpioDriver* _gpioDriver;
  LedcDriver* _ledcDriver;

  // Pool statique d'actionneurs, tous types confondus (pas de new/delete global)
  ActuatorSlot _pool[MAX_ACTUATORS];
  uint8_t _poolIdx;

  // Pool de configs
  ActuatorConfig _configPool[MAX_ACTUATORS];
  uint8_t _configCount;

  uint8_t _createdCount;

  // Trouver le bon driver HAL pour un bus et adresse donnee
  HalDriver* _getDriver(HardwareBus bus, uint8_t hwAddress);
  PCA9685Driver* _getPcaDriver(uint8_t hwAddress);
};

#endif // ACTUATOR_FACTORY_H
