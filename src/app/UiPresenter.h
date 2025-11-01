#pragma once

#include "events/EventBus.h"
#include "events/FormationEvents.h"
#include "events/MoraleEvents.h"
#include "events/JobEvents.h"

#include <memory>
#include <string>
#include <vector>

class TelemetrySink;

namespace world
{
struct LegacySimulation;
}

struct FormationHudStatus
{
    Formation formation = Formation::Swarm;
    FormationAlignmentState state = FormationAlignmentState::Idle;
    float progress = 0.0f;
    float secondsRemaining = 0.0f;
    std::size_t followers = 0;
    std::string label;
    struct AlignmentCountdown
    {
        bool active = false;
        std::string label;
        float secondsRemaining = 0.0f;
        float progress = 0.0f;
        std::size_t followers = 0;
    } countdown;
};

struct MoraleHudStatus
{
    std::vector<MoraleHudIcon> icons;
    std::size_t panicCount = 0;
    std::size_t mesomesoCount = 0;
    struct Summary
    {
        MoraleState commanderState = MoraleState::Stable;
        float leaderDownTimer = 0.0f;
        float commanderBarrierTimer = 0.0f;
        std::size_t panicCount = 0;
        std::size_t mesomesoCount = 0;
    } summary;
};

struct JobHudEntryStatus
{
    UnitJob job = UnitJob::Warrior;
    std::size_t total = 0;
    std::size_t ready = 0;
    float maxCooldown = 0.0f;
    float maxEndlag = 0.0f;
    bool specialActive = false;
    float specialTimer = 0.0f;
};

struct JobHudSkillStatus
{
    std::string id;
    std::string label;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
    bool toggled = false;
};

struct JobHudStatus
{
    std::vector<JobHudEntryStatus> jobs;
    std::vector<JobHudSkillStatus> skills;
};

class UiPresenter
{
  public:
    UiPresenter();
    ~UiPresenter();

    void setEventBus(std::shared_ptr<EventBus> bus);
    void setTelemetrySink(std::shared_ptr<TelemetrySink> sink);
    void bindSimulation(world::LegacySimulation *simulation);

    void showWarningMessage(const std::string &message, float durationSeconds = 0.0f);

    const FormationHudStatus &formationHud() const { return m_formationHud; }
    const MoraleHudStatus &moraleHud() const { return m_moraleHud; }
    const JobHudStatus &jobHud() const { return m_jobHud; }
    const std::string &lastWarningMessage() const { return m_lastWarningMessage; }
    float lastWarningDuration() const { return m_lastWarningDuration; }

  private:
    void subscribe();
    void unsubscribe();
    void updateUnconsumedEvents();

    void handleFormationChanged(const FormationChangedEvent &event);
    void handleFormationProgress(const FormationProgressEvent &event);
    void handleFormationCountdown(const FormationCountdownEvent &event);
    void handleMoraleUpdate(const MoraleUpdateEvent &event);
    void handleMoraleStatus(const MoraleStatusEvent &event);
    void handleJobSummary(const JobHudSummaryEvent &event);
    void updateTelemetryText(const std::string &text);

    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<TelemetrySink> m_telemetry;
    world::LegacySimulation *m_simulation = nullptr;
    FormationHudStatus m_formationHud;
    MoraleHudStatus m_moraleHud;
    JobHudStatus m_jobHud;
    EventBus::SubscriptionToken m_changedHandler;
    EventBus::SubscriptionToken m_progressHandler;
    EventBus::SubscriptionToken m_moraleHandler;
    EventBus::SubscriptionToken m_countdownHandler;
    EventBus::SubscriptionToken m_moraleStatusHandler;
    EventBus::SubscriptionToken m_jobSummaryHandler;
    std::string m_lastWarningMessage;
    float m_lastWarningDuration = 0.0f;
};
