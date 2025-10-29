#include "world/systems/JobAbilitySystem.h"

#include "world/SkillRuntime.h"
#include "events/EventBus.h"
#include "events/JobEvents.h"
#include "telemetry/TelemetrySink.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace world::systems
{

void JobAbilitySystem::update(float dt, SystemContext &context)
{
    bool changed = false;

    std::array<std::size_t, UnitJobCount> totals{};
    std::array<std::size_t, UnitJobCount> ready{};
    std::array<float, UnitJobCount> maxCooldown{};
    std::array<float, UnitJobCount> maxEndlag{};
    std::array<float, UnitJobCount> specialTimer{};
    std::array<bool, UnitJobCount> specialActive{};

    for (RuntimeSkill &skill : context.skills)
    {
        if (skill.cooldownRemaining > 0.0f)
        {
            const float before = skill.cooldownRemaining;
            skill.cooldownRemaining = std::max(0.0f, skill.cooldownRemaining - dt);
            if (skill.cooldownRemaining != before)
            {
                changed = true;
            }
        }
        if (skill.activeTimer > 0.0f)
        {
            const float before = skill.activeTimer;
            skill.activeTimer = std::max(0.0f, skill.activeTimer - dt);
            if (skill.activeTimer <= 0.0f && skill.def.type == SkillType::SpawnRate)
            {
                context.spawnRateMultiplier = 1.0f;
                changed = true;
            }
            if (skill.activeTimer != before)
            {
                changed = true;
            }
        }
    }

    if (context.spawnSlowTimer > 0.0f)
    {
        const float before = context.spawnSlowTimer;
        context.spawnSlowTimer = std::max(0.0f, context.spawnSlowTimer - dt);
        if (context.spawnSlowTimer <= 0.0f)
        {
            context.spawnSlowMultiplier = 1.0f;
            changed = true;
        }
        if (context.spawnSlowTimer != before)
        {
            changed = true;
        }
    }

    if (context.commanderInvulnTimer > 0.0f && context.commander.alive)
    {
        const float before = context.commanderInvulnTimer;
        context.commanderInvulnTimer = std::max(0.0f, context.commanderInvulnTimer - dt);
        if (context.commanderInvulnTimer != before)
        {
            changed = true;
        }
    }

    if (context.rallyState && context.simulation.moraleSummary.rallySuppressed)
    {
        context.rallyState = false;
        changed = true;
    }

    for (Unit &unit : context.yunaUnits)
    {
        JobRuntimeState &job = unit.job;
        const float beforeCooldown = job.cooldown;
        const float beforeEndlag = job.endlag;
        const float beforeStumble = job.warrior.stumbleTimer;
        const float beforeHold = job.archer.holdTimer;
        const bool beforeFocusReady = job.archer.focusReady;
        const float beforeTaunt = job.shield.tauntTimer;
        const float beforeSlow = job.shield.selfSlowTimer;
        if (job.cooldown > 0.0f)
        {
            job.cooldown = std::max(0.0f, job.cooldown - dt);
        }
        if (job.endlag > 0.0f)
        {
            job.endlag = std::max(0.0f, job.endlag - dt);
        }
        if (job.warrior.stumbleTimer > 0.0f)
        {
            job.warrior.stumbleTimer = std::max(0.0f, job.warrior.stumbleTimer - dt);
        }
        if (job.archer.holdTimer > 0.0f)
        {
            job.archer.holdTimer = std::max(0.0f, job.archer.holdTimer - dt);
            if (job.archer.holdTimer <= 0.0f && job.archer.focusReady)
            {
                job.archer.focusReady = false;
            }
        }
        if (job.shield.tauntTimer > 0.0f)
        {
            job.shield.tauntTimer = std::max(0.0f, job.shield.tauntTimer - dt);
        }
        if (job.shield.selfSlowTimer > 0.0f)
        {
            job.shield.selfSlowTimer = std::max(0.0f, job.shield.selfSlowTimer - dt);
        }
        const std::size_t jobIndex = unitJobIndex(job.job);
        if (jobIndex < UnitJobCount)
        {
            ++totals[jobIndex];
            if (job.cooldown <= 0.0f && job.endlag <= 0.0f)
            {
                ++ready[jobIndex];
            }
            maxCooldown[jobIndex] = std::max(maxCooldown[jobIndex], job.cooldown);
            maxEndlag[jobIndex] = std::max(maxEndlag[jobIndex], job.endlag);
            float jobSpecialTimer = 0.0f;
            bool jobSpecialActive = false;
            switch (job.job)
            {
            case UnitJob::Warrior:
                jobSpecialTimer = job.warrior.stumbleTimer;
                jobSpecialActive = jobSpecialTimer > 0.0f;
                break;
            case UnitJob::Archer:
                jobSpecialTimer = job.archer.holdTimer;
                jobSpecialActive = job.archer.focusReady || jobSpecialTimer > 0.0f;
                break;
            case UnitJob::Shield:
                jobSpecialTimer = std::max(job.shield.tauntTimer, job.shield.selfSlowTimer);
                jobSpecialActive = jobSpecialTimer > 0.0f;
                break;
            }
            if (jobSpecialActive)
            {
                specialActive[jobIndex] = true;
            }
            specialTimer[jobIndex] = std::max(specialTimer[jobIndex], jobSpecialTimer);
        }
        if (job.cooldown != beforeCooldown || job.endlag != beforeEndlag ||
            job.warrior.stumbleTimer != beforeStumble || job.archer.holdTimer != beforeHold ||
            job.archer.focusReady != beforeFocusReady ||
            job.shield.tauntTimer != beforeTaunt || job.shield.selfSlowTimer != beforeSlow)
        {
            changed = true;
        }
    }

    FrameAllocator::Allocator<JobHudSnapshot::Skill> skillAlloc(context.frameAllocator);
    std::vector<JobHudSnapshot::Skill, FrameAllocator::Allocator<JobHudSnapshot::Skill>> skillSnapshot(
        skillAlloc);
    skillSnapshot.reserve(context.skills.size());
    for (const RuntimeSkill &skill : context.skills)
    {
        JobHudSnapshot::Skill snapshot;
        snapshot.cooldown = skill.cooldownRemaining;
        snapshot.active = skill.activeTimer;
        snapshot.toggled = (skill.def.type == SkillType::ToggleFollow) ? context.rallyState : false;
        skillSnapshot.push_back(snapshot);
    }

    auto floatChanged = [](float lhs, float rhs, float eps) {
        return std::fabs(lhs - rhs) > eps;
    };

    bool hudChanged = !m_hudInitialized;
    for (std::size_t i = 0; i < UnitJobCount; ++i)
    {
        if (m_lastHud.total[i] != totals[i] || m_lastHud.ready[i] != ready[i] ||
            floatChanged(m_lastHud.maxCooldown[i], maxCooldown[i], 0.05f) ||
            floatChanged(m_lastHud.maxEndlag[i], maxEndlag[i], 0.05f) ||
            floatChanged(m_lastHud.specialTimer[i], specialTimer[i], 0.05f) ||
            m_lastHud.specialActive[i] != specialActive[i])
        {
            hudChanged = true;
            break;
        }
    }
    if (!hudChanged)
    {
        if (m_lastHud.skills.size() != skillSnapshot.size())
        {
            hudChanged = true;
        }
        else
        {
            for (std::size_t i = 0; i < skillSnapshot.size(); ++i)
            {
                const auto &prev = m_lastHud.skills[i];
                const auto &curr = skillSnapshot[i];
                if (floatChanged(prev.cooldown, curr.cooldown, 0.05f) ||
                    floatChanged(prev.active, curr.active, 0.05f) || prev.toggled != curr.toggled)
                {
                    hudChanged = true;
                    break;
                }
            }
        }
    }

    m_lastHud.total = totals;
    m_lastHud.ready = ready;
    m_lastHud.maxCooldown = maxCooldown;
    m_lastHud.maxEndlag = maxEndlag;
    m_lastHud.specialTimer = specialTimer;
    m_lastHud.specialActive = specialActive;
    m_lastHud.skills = skillSnapshot;

    if (hudChanged)
    {
        if (context.eventBus)
        {
            JobHudSummaryEvent summaryEvent;
            summaryEvent.jobs.reserve(UnitJobCount);
            for (std::size_t i = 0; i < UnitJobCount; ++i)
            {
                JobHudSummaryEntry entry;
                entry.job = AllUnitJobs[i];
                entry.total = totals[i];
                entry.ready = ready[i];
                entry.maxCooldown = maxCooldown[i];
                entry.maxEndlag = maxEndlag[i];
                entry.specialActive = specialActive[i];
                entry.specialTimer = specialTimer[i];
                summaryEvent.jobs.push_back(entry);
            }
            summaryEvent.skills.reserve(context.skills.size());
            for (std::size_t i = 0; i < context.skills.size(); ++i)
            {
                const RuntimeSkill &skill = context.skills[i];
                JobHudSkillEntry entry;
                entry.id = skill.def.id;
                entry.label = skill.def.displayName;
                entry.cooldownRemaining = skill.cooldownRemaining;
                entry.activeTimer = skill.activeTimer;
                entry.toggled = skillSnapshot[i].toggled;
                summaryEvent.skills.push_back(std::move(entry));
            }
            EventContext ctxEvent;
            ctxEvent.payload = summaryEvent;
            context.eventBus->dispatch(JobHudSummaryEventName, ctxEvent);
        }
        if (context.telemetry)
        {
            std::ostringstream jobSummary;
            for (std::size_t i = 0; i < UnitJobCount; ++i)
            {
                if (i > 0)
                {
                    jobSummary << ' ';
                }
                jobSummary << unitJobToString(AllUnitJobs[i]) << ':' << ready[i] << '/' << totals[i];
            }
            std::ostringstream skillSummary;
            for (std::size_t i = 0; i < context.skills.size(); ++i)
            {
                if (i > 0)
                {
                    skillSummary << ' ';
                }
                const RuntimeSkill &skill = context.skills[i];
                skillSummary << skill.def.id << '(' << std::fixed << std::setprecision(1)
                             << std::max(skill.cooldownRemaining, 0.0f);
                if (skillSnapshot[i].toggled)
                {
                    skillSummary << " toggled";
                }
                if (skillSnapshot[i].active > 0.0f)
                {
                    skillSummary << " active=" << std::fixed << std::setprecision(1) << skillSnapshot[i].active;
                }
                skillSummary << ')';
            }
            TelemetrySink::Payload payload{{"jobs", jobSummary.str()}};
            if (!context.skills.empty())
            {
                payload.emplace("skills", skillSummary.str());
            }
            context.telemetry->recordEvent("hud.jobs", payload);
        }
        m_hudInitialized = true;
    }

    if (changed)
    {
        context.requestComponentSync();
    }
}

void JobAbilitySystem::triggerSkill(SystemContext &context, const SkillCommand &command)
{
    if (command.index < 0 || command.index >= static_cast<int>(context.skills.size()))
    {
        return;
    }

    RuntimeSkill &skill = context.skills[static_cast<std::size_t>(command.index)];
    if (skill.cooldownRemaining > 0.0f)
    {
        return;
    }

    switch (skill.def.type)
    {
    case SkillType::ToggleFollow:
        toggleRally(context, skill, command);
        break;
    case SkillType::MakeWall:
        context.simulation.spawnWallSegments(skill.def, command.worldTarget);
        skill.cooldownRemaining = skill.def.cooldown;
        context.requestComponentSync();
        break;
    case SkillType::SpawnRate:
        activateSpawnRate(context, skill);
        break;
    case SkillType::Detonate:
        context.simulation.detonateCommander(skill.def);
        skill.cooldownRemaining = skill.def.cooldown;
        context.requestComponentSync();
        break;
    }
}

void JobAbilitySystem::toggleRally(SystemContext &context, RuntimeSkill &skill, const SkillCommand &command)
{
    if (context.simulation.moraleSummary.rallySuppressed)
    {
        context.simulation.pushTelemetry("Allies are panicking! Rally unavailable.");
        return;
    }
    const bool newState = !context.rallyState;
    context.simulation.applyRallyState(newState, skill.def, command.worldTarget);
    context.rallyState = newState;
    skill.cooldownRemaining = skill.def.cooldown;
    context.requestComponentSync();
}

void JobAbilitySystem::activateSpawnRate(SystemContext &context, RuntimeSkill &skill)
{
    context.spawnRateMultiplier = skill.def.multiplier;
    skill.activeTimer = skill.def.duration;
    skill.cooldownRemaining = skill.def.cooldown;
    context.simulation.pushTelemetry("Spawn surge");
    context.requestComponentSync();
}

} // namespace world::systems

