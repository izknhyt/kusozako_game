#include "app/UiPresenter.h"

#include "events/EventBus.h"
#include "events/MoraleEvents.h"
#include "telemetry/TelemetrySink.h"
#include "world/LegacySimulation.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <iomanip>
#include <sstream>
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
    updateUnconsumedEvents();
}

void UiPresenter::setTelemetrySink(std::shared_ptr<TelemetrySink> sink)
{
    m_telemetry = std::move(sink);
}

void UiPresenter::bindSimulation(world::LegacySimulation *simulation)
{
    m_simulation = simulation;
    updateUnconsumedEvents();
}

void UiPresenter::showWarningMessage(const std::string &message, float durationSeconds)
{
    float duration = durationSeconds;
    if (m_simulation)
    {
        if (duration <= 0.0f)
        {
            duration = std::max(m_simulation->config.telemetry_duration, 1.5f);
        }

        HUDState &hud = m_simulation->hud;
        hud.performance.active = true;
        hud.performance.message = message;
        hud.performance.timer = duration;
    }
    else if (duration <= 0.0f)
    {
        duration = 1.5f;
    }

    m_lastWarningMessage = message;
    m_lastWarningDuration = duration;
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
        updateUnconsumedEvents();
    });

    m_progressHandler = m_eventBus->subscribe(FormationProgressEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<FormationProgressEvent>(&ctx.payload))
        {
            handleFormationProgress(*payload);
        }
        updateUnconsumedEvents();
    });

    m_countdownHandler = m_eventBus->subscribe(FormationCountdownEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<FormationCountdownEvent>(&ctx.payload))
        {
            handleFormationCountdown(*payload);
        }
        updateUnconsumedEvents();
    });

    m_moraleHandler = m_eventBus->subscribe(MoraleUpdateEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<MoraleUpdateEvent>(&ctx.payload))
        {
            handleMoraleUpdate(*payload);
        }
        updateUnconsumedEvents();
    });

    m_moraleStatusHandler = m_eventBus->subscribe(MoraleStatusEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<MoraleStatusEvent>(&ctx.payload))
        {
            handleMoraleStatus(*payload);
        }
        updateUnconsumedEvents();
    });

    m_jobSummaryHandler = m_eventBus->subscribe(JobHudSummaryEventName, [this](const EventContext &ctx) {
        if (const auto *payload = std::any_cast<JobHudSummaryEvent>(&ctx.payload))
        {
            handleJobSummary(*payload);
        }
        updateUnconsumedEvents();
    });
}

void UiPresenter::unsubscribe()
{
    m_changedHandler.reset();
    m_progressHandler.reset();
    m_moraleHandler.reset();
    m_countdownHandler.reset();
    m_moraleStatusHandler.reset();
    m_jobSummaryHandler.reset();
}

void UiPresenter::updateUnconsumedEvents()
{
    if (!m_simulation)
    {
        return;
    }
    if (m_eventBus)
    {
        const std::size_t unconsumed = m_eventBus->unconsumedCount();
        m_simulation->hud.unconsumedEvents = unconsumed;

        constexpr std::size_t WarningThreshold = 10;
        if (unconsumed >= WarningThreshold)
        {
            const std::string warningText = std::string("Events lost ") + std::to_string(unconsumed);
            const HUDState::PerformanceWarning &performanceHud = m_simulation->hud.performance;

            const bool warningAlreadyActive = performanceHud.active && performanceHud.timer > 0.0f &&
                                              performanceHud.message == warningText;

            if (!warningAlreadyActive)
            {
                showWarningMessage(warningText);
            }
        }
    }
    else
    {
        m_simulation->hud.unconsumedEvents = 0;
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

void UiPresenter::handleFormationCountdown(const FormationCountdownEvent &event)
{
    auto floatChanged = [](float lhs, float rhs, float eps) {
        return std::fabs(lhs - rhs) > eps;
    };

    bool changed = m_formationHud.countdown.active != event.active ||
                   m_formationHud.countdown.label != event.label ||
                   floatChanged(m_formationHud.countdown.secondsRemaining, event.secondsRemaining, 0.05f) ||
                   floatChanged(m_formationHud.countdown.progress, event.progress, 0.01f) ||
                   m_formationHud.countdown.followers != event.followers;

    m_formationHud.countdown.active = event.active;
    m_formationHud.countdown.label = event.label;
    m_formationHud.countdown.secondsRemaining = event.secondsRemaining;
    m_formationHud.countdown.progress = event.progress;
    m_formationHud.countdown.followers = event.followers;

    if (m_simulation)
    {
        HUDState::AlignmentCountdown &hudCountdown = m_simulation->hud.alignment;
        hudCountdown.active = event.active;
        hudCountdown.label = event.label;
        hudCountdown.secondsRemaining = event.secondsRemaining;
        hudCountdown.progress = event.progress;
        hudCountdown.followers = event.followers;
    }

    if (changed && m_telemetry)
    {
        std::ostringstream seconds;
        seconds << std::fixed << std::setprecision(2) << std::max(event.secondsRemaining, 0.0f);
        std::ostringstream progress;
        progress << std::fixed << std::setprecision(2) << std::clamp(event.progress, 0.0f, 1.0f);
        TelemetrySink::Payload payload{{"active", event.active ? "true" : "false"},
                                       {"seconds", seconds.str()},
                                       {"progress", progress.str()},
                                       {"followers", std::to_string(event.followers)}};
        if (!event.label.empty())
        {
            payload.emplace("label", event.label);
        }
        m_telemetry->recordEvent("hud.alignment", payload);
    }
}

void UiPresenter::handleMoraleUpdate(const MoraleUpdateEvent &event)
{
    m_moraleHud.icons = event.icons;
    m_moraleHud.panicCount = event.panicCount;
    m_moraleHud.mesomesoCount = event.mesomesoCount;

    if (m_simulation)
    {
        m_simulation->hud.morale.panicCount = event.panicCount;
        m_simulation->hud.morale.mesomesoCount = event.mesomesoCount;
    }

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

void UiPresenter::handleMoraleStatus(const MoraleStatusEvent &event)
{
    auto floatChanged = [](float lhs, float rhs, float eps) {
        return std::fabs(lhs - rhs) > eps;
    };

    bool changed = m_moraleHud.summary.commanderState != event.commanderState ||
                   m_moraleHud.summary.panicCount != event.panicCount ||
                   m_moraleHud.summary.mesomesoCount != event.mesomesoCount ||
                   floatChanged(m_moraleHud.summary.leaderDownTimer, event.leaderDownTimer, 0.05f) ||
                   floatChanged(m_moraleHud.summary.commanderBarrierTimer, event.commanderBarrierTimer, 0.05f);

    m_moraleHud.summary.commanderState = event.commanderState;
    m_moraleHud.summary.leaderDownTimer = event.leaderDownTimer;
    m_moraleHud.summary.commanderBarrierTimer = event.commanderBarrierTimer;
    m_moraleHud.summary.panicCount = event.panicCount;
    m_moraleHud.summary.mesomesoCount = event.mesomesoCount;

    if (m_simulation)
    {
        HUDState::MoraleStatus &hudMorale = m_simulation->hud.morale;
        hudMorale.commanderState = event.commanderState;
        hudMorale.leaderDownTimer = event.leaderDownTimer;
        hudMorale.commanderBarrierTimer = event.commanderBarrierTimer;
        hudMorale.panicCount = event.panicCount;
        hudMorale.mesomesoCount = event.mesomesoCount;
    }

    if (changed && m_telemetry)
    {
        std::ostringstream leaderDown;
        leaderDown << std::fixed << std::setprecision(2) << std::max(event.leaderDownTimer, 0.0f);
        std::ostringstream barrier;
        barrier << std::fixed << std::setprecision(2) << std::max(event.commanderBarrierTimer, 0.0f);
        TelemetrySink::Payload payload{{"commander_state", std::string(moraleStateLabel(event.commanderState))},
                                       {"panic", std::to_string(event.panicCount)},
                                       {"mesomeso", std::to_string(event.mesomesoCount)},
                                       {"leader_down_s", leaderDown.str()},
                                       {"barrier_s", barrier.str()}};
        m_telemetry->recordEvent("hud.morale_status", payload);
    }
}

void UiPresenter::handleJobSummary(const JobHudSummaryEvent &event)
{
    auto floatChanged = [](float lhs, float rhs, float eps) {
        return std::fabs(lhs - rhs) > eps;
    };

    bool changed = m_jobHud.jobs.size() != event.jobs.size() ||
                   m_jobHud.skills.size() != event.skills.size();

    for (std::size_t i = 0; !changed && i < event.jobs.size(); ++i)
    {
        const auto &src = event.jobs[i];
        if (i >= m_jobHud.jobs.size())
        {
            changed = true;
            break;
        }
        const auto &dst = m_jobHud.jobs[i];
        if (dst.job != src.job || dst.total != src.total || dst.ready != src.ready ||
            floatChanged(dst.maxCooldown, src.maxCooldown, 0.05f) ||
            floatChanged(dst.maxEndlag, src.maxEndlag, 0.05f) || dst.specialActive != src.specialActive ||
            floatChanged(dst.specialTimer, src.specialTimer, 0.05f))
        {
            changed = true;
        }
    }

    for (std::size_t i = 0; !changed && i < event.skills.size(); ++i)
    {
        if (i >= m_jobHud.skills.size())
        {
            changed = true;
            break;
        }
        const auto &src = event.skills[i];
        const auto &dst = m_jobHud.skills[i];
        if (dst.id != src.id || dst.label != src.label || dst.toggled != src.toggled ||
            floatChanged(dst.cooldownRemaining, src.cooldownRemaining, 0.05f) ||
            floatChanged(dst.activeTimer, src.activeTimer, 0.05f))
        {
            changed = true;
        }
    }

    m_jobHud.jobs.clear();
    m_jobHud.jobs.reserve(event.jobs.size());
    for (const auto &entry : event.jobs)
    {
        JobHudEntryStatus status;
        status.job = entry.job;
        status.total = entry.total;
        status.ready = entry.ready;
        status.maxCooldown = entry.maxCooldown;
        status.maxEndlag = entry.maxEndlag;
        status.specialActive = entry.specialActive;
        status.specialTimer = entry.specialTimer;
        m_jobHud.jobs.push_back(status);
    }

    m_jobHud.skills.clear();
    m_jobHud.skills.reserve(event.skills.size());
    for (const auto &skill : event.skills)
    {
        JobHudSkillStatus status;
        status.id = skill.id;
        status.label = skill.label;
        status.cooldownRemaining = skill.cooldownRemaining;
        status.activeTimer = skill.activeTimer;
        status.toggled = skill.toggled;
        m_jobHud.skills.push_back(status);
    }

    if (m_simulation)
    {
        HUDState::JobSummary &hudJobs = m_simulation->hud.jobs;
        for (std::size_t i = 0; i < hudJobs.entries.size(); ++i)
        {
            hudJobs.entries[i].job = AllUnitJobs[i];
            hudJobs.entries[i].total = 0;
            hudJobs.entries[i].ready = 0;
            hudJobs.entries[i].maxCooldown = 0.0f;
            hudJobs.entries[i].maxEndlag = 0.0f;
            hudJobs.entries[i].specialActive = false;
            hudJobs.entries[i].specialTimer = 0.0f;
        }
        for (const auto &entry : event.jobs)
        {
            const std::size_t index = unitJobIndex(entry.job);
            if (index < hudJobs.entries.size())
            {
                auto &dst = hudJobs.entries[index];
                dst.job = entry.job;
                dst.total = entry.total;
                dst.ready = entry.ready;
                dst.maxCooldown = entry.maxCooldown;
                dst.maxEndlag = entry.maxEndlag;
                dst.specialActive = entry.specialActive;
                dst.specialTimer = entry.specialTimer;
            }
        }
        hudJobs.skills.clear();
        hudJobs.skills.reserve(event.skills.size());
        for (const auto &skill : event.skills)
        {
            HUDState::JobSummary::Skill hudSkill;
            hudSkill.id = skill.id;
            hudSkill.label = skill.label;
            hudSkill.cooldownRemaining = skill.cooldownRemaining;
            hudSkill.activeTimer = skill.activeTimer;
            hudSkill.toggled = skill.toggled;
            hudJobs.skills.push_back(std::move(hudSkill));
        }
    }

    if (changed && m_telemetry)
    {
        std::ostringstream jobSummary;
        for (const auto &entry : event.jobs)
        {
            if (!jobSummary.str().empty())
            {
                jobSummary << ' ';
            }
            jobSummary << unitJobToString(entry.job) << ':' << entry.ready << '/' << entry.total;
        }
        std::ostringstream skillSummary;
        for (const auto &skill : event.skills)
        {
            if (!skillSummary.str().empty())
            {
                skillSummary << ' ';
            }
            skillSummary << skill.id << '(';
            skillSummary << std::fixed << std::setprecision(1) << std::max(skill.cooldownRemaining, 0.0f);
            if (skill.toggled)
            {
                skillSummary << " toggled";
            }
            if (skill.activeTimer > 0.0f)
            {
                skillSummary << " active=" << std::fixed << std::setprecision(1) << skill.activeTimer;
            }
            skillSummary << ')';
        }
        TelemetrySink::Payload payload;
        payload.emplace("jobs", jobSummary.str());
        if (!event.skills.empty())
        {
            payload.emplace("skills", skillSummary.str());
        }
        m_telemetry->recordEvent("hud.jobs", payload);
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

