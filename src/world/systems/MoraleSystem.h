#pragma once

#include "world/MoraleTypes.h"
#include "world/systems/SystemContext.h"

#include <vector>

namespace world::systems
{

class MoraleSystem : public ISystem
{
  public:
    MoraleSystem() = default;

    void update(float dt, SystemContext &context) override;

  private:
    bool m_commanderAlive = true;
    float m_leaderDownTimer = 0.0f;
    float m_commanderBarrierTimer = 0.0f;
    std::size_t m_knownUnits = 0;
    std::vector<MoraleState> m_lastStates;
    MoraleState m_lastCommanderState = MoraleState::Stable;
    bool m_announcedLeaderDown = false;
    bool m_announcedPanic = false;
    bool m_announcedRecovery = true;
    bool m_applyReviveBarrier = false;
};

} // namespace world::systems

