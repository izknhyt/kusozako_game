#pragma once

#include "events/FormationEvents.h"
#include "world/systems/SystemContext.h"

#include <memory>

class EventBus;
class TelemetrySink;

namespace world::systems
{

class FormationSystem : public ISystem
{
  public:
    FormationSystem();

    void update(float dt, SystemContext &context) override;

    void setEventBus(std::weak_ptr<EventBus> bus);
    void setTelemetrySink(std::weak_ptr<TelemetrySink> sink);

    void cycleFormation(int direction, LegacySimulation &simulation);
    void issueOrder(ArmyStance stance, LegacySimulation &simulation);
    void reset(const LegacySimulation &simulation);

  private:
    std::weak_ptr<EventBus> m_eventBus;
    std::weak_ptr<TelemetrySink> m_telemetry;
    FormationAlignmentState m_state;
    float m_progress;
    Formation m_lastFormation;
    float m_lastProgressSent;
    FormationAlignmentState m_lastStateSent;
    std::size_t m_lastFollowerCount;
    float m_lastSecondsRemaining;

    void emitFormationChanged(Formation formation);
    void emitFormationProgress(Formation formation, FormationAlignmentState state, float progress,
                               float secondsRemaining, std::size_t followers);
};

} // namespace world::systems

