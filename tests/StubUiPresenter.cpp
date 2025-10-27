#include "app/UiPresenter.h"

#include <utility>

UiPresenter::UiPresenter() = default;

UiPresenter::~UiPresenter() = default;

void UiPresenter::setEventBus(std::shared_ptr<EventBus> bus)
{
    m_eventBus = std::move(bus);
}

void UiPresenter::setTelemetrySink(std::shared_ptr<TelemetrySink> sink)
{
    m_telemetry = std::move(sink);
}

void UiPresenter::bindSimulation(world::LegacySimulation *simulation)
{
    m_simulation = simulation;
}

void UiPresenter::showWarningMessage(const std::string &message, float durationSeconds)
{
    float duration = durationSeconds;
    if (duration <= 0.0f)
    {
        duration = 1.5f;
    }

    m_lastWarningMessage = message;
    m_lastWarningDuration = duration;
}
