#include "world/systems/JobAbilitySystem.h"

#include "world/SkillRuntime.h"

#include <algorithm>

namespace world::systems
{

void JobAbilitySystem::update(float dt, SystemContext &context)
{
    bool changed = false;

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
        if (job.cooldown != beforeCooldown || job.endlag != beforeEndlag ||
            job.warrior.stumbleTimer != beforeStumble || job.archer.holdTimer != beforeHold ||
            job.archer.focusReady != beforeFocusReady ||
            job.shield.tauntTimer != beforeTaunt || job.shield.selfSlowTimer != beforeSlow)
        {
            changed = true;
        }
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

