#pragma once
// External watchdog toggle. Architecture.md §8.6. Toggle GPIO23 at 20 Hz.
namespace sys {
class WdtToggle {
public:
    void init() { m_state=false; }
    bool tick() { m_state=!m_state; return m_state; }
private:
    bool m_state=false;
};
}
