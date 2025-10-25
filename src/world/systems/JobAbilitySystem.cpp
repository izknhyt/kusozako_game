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

