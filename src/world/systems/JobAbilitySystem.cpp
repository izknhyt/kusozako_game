#include "world/systems/JobAbilitySystem.h"

#include "world/SkillRuntime.h"

#include <algorithm>

namespace world::systems
{

void JobAbilitySystem::update(float dt, SystemContext &context)
{
    for (RuntimeSkill &skill : context.skills)
    {
        if (skill.cooldownRemaining > 0.0f)
        {
            skill.cooldownRemaining = std::max(0.0f, skill.cooldownRemaining - dt);
        }
        if (skill.activeTimer > 0.0f)
        {
            skill.activeTimer = std::max(0.0f, skill.activeTimer - dt);
            if (skill.activeTimer <= 0.0f && skill.def.type == SkillType::SpawnRate)
            {
                context.spawnRateMultiplier = 1.0f;
            }
        }
    }

    if (context.spawnSlowTimer > 0.0f)
    {
        context.spawnSlowTimer = std::max(0.0f, context.spawnSlowTimer - dt);
        if (context.spawnSlowTimer <= 0.0f)
        {
            context.spawnSlowMultiplier = 1.0f;
        }
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
        break;
    case SkillType::SpawnRate:
        activateSpawnRate(context, skill);
        break;
    case SkillType::Detonate:
        context.simulation.detonateCommander(skill.def);
        skill.cooldownRemaining = skill.def.cooldown;
        break;
    }
}

void JobAbilitySystem::toggleRally(SystemContext &context, RuntimeSkill &skill, const SkillCommand &command)
{
    const bool newState = !context.rallyState;
    context.simulation.applyRallyState(newState, skill.def, command.worldTarget);
    context.rallyState = newState;
    skill.cooldownRemaining = skill.def.cooldown;
}

void JobAbilitySystem::activateSpawnRate(SystemContext &context, RuntimeSkill &skill)
{
    context.spawnRateMultiplier = skill.def.multiplier;
    skill.activeTimer = skill.def.duration;
    skill.cooldownRemaining = skill.def.cooldown;
    context.simulation.pushTelemetry("Spawn surge");
}

} // namespace world::systems

