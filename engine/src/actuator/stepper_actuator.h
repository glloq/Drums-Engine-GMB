#ifndef STEPPER_ACTUATOR_H
#define STEPPER_ACTUATOR_H

#include "actuator.h"
#include "../hal/gpio_driver.h"

// ============================================================================
// StepperActuator - Moteur pas a pas via driver STEP/DIR (DRV8825, A4988, TMC…)
// ============================================================================
// Cinquieme type d'actionneur, a cote de SOLENOID / SERVO / PWM_MOTOR /
// MOTOR_OPTICAL. Il n'est indispensable a aucune batterie classique, mais il
// ouvre les mecanismes ou la REPETABILITE de la position compte davantage que
// la vitesse de frappe :
//   - maillet rotatif, mecanisme de roulement ;
//   - deplacement d'une mailloche, changement de zone de frappe ;
//   - reglage mecanique de tension (timbale), pedale mecanique.
//
// Comportements :
//   STEPPER_POSITION : position absolue en pas. La valeur de commande 0..127
//                      est mappee sur [paramMin, paramMax].
//   STEPPER_STRIKE   : aller a paramMax puis retour automatique a paramDefault.
//                      La velocite pilote la vitesse d'attaque.
//   STEPPER_ROTATE   : rotation continue. La valeur 0..127 est mappee sur
//                      [paramMin, paramMax] % de stepperMaxSps ; 0 = arret.
//
// Cablage (bus GPIO_DIRECT obligatoire) :
//   hwPin       = STEP        hwAddress   = DIR
//   enablePin   = /ENABLE (actif bas, 0xFF = driver toujours actif)
//   endStopPin1 = butee mini / origine     endStopPin2 = butee maxi
//
// Generation des pas
// ------------------
// Les impulsions sont produites depuis service(), appele a chaque passe de la
// boucle temps reel (~1 kHz). Une passe peut emettre une RAFALE bornee de
// STEPPER_MAX_STEPS_PER_PASS pas, ce qui porte le plafond a ~8 kpas/s tout en
// bornant le temps passe dans la boucle RT : 8 pas x (4 us haut + 4 us bas)
// = ~64 us par passe dans le pire cas, soit ~6 % d'une passe de 1 ms.
// C'est le compromis assume : pas de timer materiel dedie, mais une charge
// deterministe et un plafond de vitesse annonce (STEPPER_MAX_SPEED_SPS).
//
// Securite :
//   - Butees mini/maxi coupent le mouvement et recalent la position ;
//   - prise d'origine obligatoire avant tout positionnement absolu si une butee
//     mini est declaree (sinon la position de depart est supposee = repos) ;
//   - MOTOR_MAX_CONTINUOUS_US borne un mouvement qui n'aboutit pas ;
//   - stop() coupe le driver via /ENABLE quand la pin est cablee.
// ============================================================================

class StepperActuator : public Actuator {
public:
  StepperActuator()
    : _gpio(nullptr), _currentSteps(0), _targetSteps(0),
      _currentSps(0), _targetSps(0), _lastStepUs(0), _lastServiceUs(0),
      _dirForward(true), _homed(false), _homing(false), _homingStartUs(0),
      _rotating(false), _returnAfterMove(false), _returnTargetSteps(0) {}
  explicit StepperActuator(GpioDriver* gpio);

  void init() override;
  void execute(const ActuatorCommand& cmd) override;
  void stop() override;
  bool checkTimeout(uint32_t nowUs) override;
  bool checkEndStops(uint32_t nowUs) override;

  bool needsService() const override { return true; }
  void service(uint32_t nowUs) override;

  // Introspection (API /api/actuators, calibration)
  int32_t getCurrentSteps() const { return _currentSteps; }
  int32_t getTargetSteps() const { return _targetSteps; }
  bool isHomed() const { return _homed; }

private:
  GpioDriver* _gpio;

  int32_t _currentSteps;      // Position courante en pas (0 = origine)
  int32_t _targetSteps;       // Position visee
  uint16_t _currentSps;       // Vitesse instantanee, pas/seconde
  uint16_t _targetSps;        // Vitesse visee (rampe d'acceleration)
  uint32_t _lastStepUs;
  uint32_t _lastServiceUs;

  bool _dirForward;
  bool _homed;
  bool _homing;
  uint32_t _homingStartUs;
  bool _rotating;             // STEPPER_ROTATE: tourne sans cible de position

  bool _returnAfterMove;      // STEPPER_STRIKE: retour au repos une fois arrive
  int32_t _returnTargetSteps;

  void _setEnabled(bool on);
  void _setDirection(bool forward);
  void _emitStep();
  // `remainingSteps` = 0xFFFFFFFF pour un mouvement sans cible (rotation
  // continue, prise d'origine) : la deceleration d'approche est alors inactive.
  void _updateSpeed(uint32_t nowUs, uint32_t remainingSteps);
  void _beginMove(int32_t targetSteps, uint16_t sps);
  void _finishMove();
  bool _endStopHit(uint8_t pin) const;

  // Bornes de deplacement en pas, dans l'ordre croissant.
  int32_t _minSteps() const {
    return (int32_t)(_config.paramMin < _config.paramMax ? _config.paramMin : _config.paramMax);
  }
  int32_t _maxSteps() const {
    return (int32_t)(_config.paramMin < _config.paramMax ? _config.paramMax : _config.paramMin);
  }
  uint16_t _maxSps() const {
    return _config.stepperMaxSps > STEPPER_MAX_SPEED_SPS ? (uint16_t)STEPPER_MAX_SPEED_SPS
                                                         : _config.stepperMaxSps;
  }
};

#endif // STEPPER_ACTUATOR_H
