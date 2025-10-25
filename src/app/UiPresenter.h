#pragma once

#include "events/EventBus.h"
#include "events/FormationEvents.h"

#include <memory>
#include <string>

class TelemetrySink;

namespace world
{
class LegacySimulation;
}

struct FormationHudStatus
{
    Formation formation = Formation::Swarm;
    FormationAlignmentState state = FormationAlignmentState::Idle;
    float progress = 0.0f;
    float secondsRemaining = 0.0f;
    std::size_t followers = 0;
    std::string label;
};

class UiPresenter
{
  public:
    UiPresenter();
    ~UiPresenter();

    void setEventBus(std::shared_ptr<EventBus> bus);
    void setTelemetrySink(std::shared_ptr<TelemetrySink> sink);
    void bindSimulation(world::LegacySimulation *simulation);

    const FormationHudStatus &formationHud() const { return m_formationHud; }

  private:
    void subscribe();
    void unsubscribe();

    void handleFormationChanged(const FormationChangedEvent &event);
    void handleFormationProgress(const FormationProgressEvent &event);
    void updateTelemetryText(const std::string &text);

    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<TelemetrySink> m_telemetry;
    world::LegacySimulation *m_simulation = nullptr;
    FormationHudStatus m_formationHud;
    EventBus::HandlerId m_changedHandler = 0;
    EventBus::HandlerId m_progressHandler = 0;
};

