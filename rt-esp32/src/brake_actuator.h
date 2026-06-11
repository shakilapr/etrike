#pragma once
// Brake actuator — solenoid/relay on GPIO.
// ESTOP → engage, brake lever → engage, normal → release.

namespace sys {

class BrakeActuator {
public:
    BrakeActuator() = default;

    void init();
    void engage();
    void release();
    bool is_engaged() const { return m_engaged; }

private:
    bool m_engaged = false;
};

}  // namespace sys
