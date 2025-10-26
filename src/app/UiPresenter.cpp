#include "app/UiPresenter.h"

#include "events/EventBus.h"
#include "events/MoraleEvents.h"
#include "telemetry/TelemetrySink.h"
#include "world/LegacySimulation.h"

#include <any>
#include <utility>

UiPresenter::UiPresenter() = default;

UiPresenter::~UiPresenter()
{
    unsubscribe();
}

void UiPresenter::setEventBus(std::shared_ptr<EventBus> bus)
{
    if (m_eventBus == bus)
    {
        return;
    }
    unsubscribe();
    m_eventBus = std::move(bus);
    subscribe();
}

void UiPresenter::setTelemetrySink(std::shared_ptr<TelemetrySink> sink)
{
    m_telemetry = std::move(sink);
}

void UiPresenter::bindSimulation(world::LegacySimulation *simulation)
{
    m_simulation = simulation;
}

void UiPresenter::subscribe()
{
    if (!m_eventBus)
    {
        return;
    }

    m_changedHandler = m_eventBus->subscribe(FormationChangedEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<FormationChangedEvent>(&ctx.payload))
        {
            handleFormationChanged(*payload);
        }
    });

    m_progressHandler = m_eventBus->subscribe(FormationProgressEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<FormationProgressEvent>(&ctx.payload))
        {
            handleFormationProgress(*payload);
        }
    });

    m_moraleHandler = m_eventBus->subscribe(MoraleUpdateEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<MoraleUpdateEvent>(&ctx.payload))
        {
            handleMoraleUpdate(*payload);
        }
    });
}

void UiPresenter::unsubscribe()
{
    if (!m_eventBus)
    {
        m_changedHandler = 0;
        m_progressHandler = 0;
        m_moraleHandler = 0;
        return;
    }
    if (m_changedHandler != 0)
    {
        m_eventBus->unsubscribe(FormationChangedEventName, m_changedHandler);
        m_changedHandler = 0;
    }
    if (m_progressHandler != 0)
    {
        m_eventBus->unsubscribe(FormationProgressEventName, m_progressHandler);
        m_progressHandler = 0;
    }
    if (m_moraleHandler != 0)
    {
        m_eventBus->unsubscribe(MoraleUpdateEventName, m_moraleHandler);
        m_moraleHandler = 0;
    }
}

void UiPresenter::handleFormationChanged(const FormationChangedEvent &event)
{
    m_formationHud.formation = event.formation;
    m_formationHud.label = event.label;
    m_formationHud.state = FormationAlignmentState::Aligning;
    m_formationHud.progress = 0.0f;
    m_formationHud.secondsRemaining = 0.0f;
    m_formationHud.followers = 0;

    updateTelemetryText(std::string("Formation: ") + event.label);

    if (m_telemetry)
    {
        TelemetrySink::Payload payload{{"type", "formation"}, {"label", event.label}};
        m_telemetry->recordEvent("hud.telemetry", payload);
    }
}

void UiPresenter::handleFormationProgress(const FormationProgressEvent &event)
{
    m_formationHud.formation = event.formation;
    m_formationHud.state = event.state;
    m_formationHud.progress = event.progress;
    m_formationHud.secondsRemaining = event.secondsRemaining;
    m_formationHud.followers = event.followers;
}

void UiPresenter::handleMoraleUpdate(const MoraleUpdateEvent &event)
{
    m_moraleHud.icons = event.icons;
    m_moraleHud.panicCount = event.panicCount;
    m_moraleHud.mesomesoCount = event.mesomesoCount;

    if (!event.telemetry.empty())
    {
        updateTelemetryText(event.telemetry);
        if (m_telemetry)
        {
            TelemetrySink::Payload payload{{"type", "morale"},
                                           {"message", event.telemetry},
                                           {"panic", std::to_string(event.panicCount)},
                                           {"mesomeso", std::to_string(event.mesomesoCount)}};
            m_telemetry->recordEvent("hud.telemetry", payload);
        }
    }
}

void UiPresenter::updateTelemetryText(const std::string &text)
{
    if (!m_simulation)
    {
        return;
    }
    m_simulation->hud.telemetryText = text;
    m_simulation->hud.telemetryTimer = m_simulation->config.telemetry_duration;
}

