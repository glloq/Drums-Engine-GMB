#ifndef ACTUATOR_H
#define ACTUATOR_H

#include <Arduino.h>

/**
 * Actuator states
 */
enum class ActuatorState : uint8_t {
    IDLE = 0,       // Idle/Rest
    ACTIVE = 1,     // Active
    WAITING = 2,    // Waiting (e.g., servo moving)
    COOLDOWN = 3    // Cooldown (anti-bounce)
};

/**
 * IActuator interface - Physical actuator abstraction
 *
 * Contract:
 * - activate() : triggers the actuator
 * - deactivate() : stops the actuator
 * - update(time) : temporal update (scheduler)
 * - getState() : current state
 *
 * Implementations: Solenoid, ServoActuator, MotorActuator, etc.
 */
class IActuator {
  public:
    virtual ~IActuator() = default;

    /**
     * Activation with parameter (velocity, angle, duration...)
     * @param param Activation parameter (interpretation depends on type)
     */
    virtual void activate(uint8_t param) = 0;

    /**
     * Deactivation
     */
    virtual void deactivate() = 0;

    /**
     * Temporal update (called by Scheduler)
     * @param currentTime Current time (millis())
     * @return true if state changed (needs callback)
     */
    virtual bool update(unsigned long currentTime) = 0;

    /**
     * Current state
     */
    virtual ActuatorState getState() const = 0;

    /**
     * Name for debugging
     */
    virtual const char* getName() const = 0;
};

#endif // ACTUATOR_H
