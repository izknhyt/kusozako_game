#include "world/systems/JobAbilitySystem.h"

#include "config/AppConfig.h"
#include "core/Vec2.h"
#include "input/ActionBuffer.h"
#include "world/SkillRuntime.h"
#include "world/WorldState.h"

#include <cmath>
#include <iostream>

namespace
{

bool almostEqual(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) <= epsilon;
}

bool testUpdateTimersAndFlags()
{
    world::WorldState world;
    world.reset();
    auto &sim = world.legacy();
    sim.skills.clear();

    RuntimeSkill spawnSkill{};
    spawnSkill.def.id = "spawn";
    spawnSkill.def.type = SkillType::SpawnRate;
    spawnSkill.def.cooldown = 5.0f;
    spawnSkill.def.duration = 1.0f;
    spawnSkill.def.multiplier = 2.5f;
    spawnSkill.cooldownRemaining = 4.0f;
    spawnSkill.activeTimer = 1.0f;
    sim.skills.push_back(spawnSkill);

    sim.spawnRateMultiplier = 3.0f;
    sim.spawnSlowTimer = 0.25f;
    sim.spawnSlowMultiplier = 0.5f;
    sim.commanderInvulnTimer = 0.75f;
    sim.commander.alive = true;
    sim.rallyState = true;
    sim.moraleSummary.rallySuppressed = true;

    world::systems::JobAbilitySystem system;
    ActionBuffer actions;
    auto context = world.makeSystemContext(actions);
    context.componentsDirty = false;

    system.update(0.5f, context);

    bool success = true;
    const RuntimeSkill &afterFirst = sim.skills[0];
    if (!almostEqual(afterFirst.cooldownRemaining, 3.5f))
    {
        std::cerr << "Skill cooldown did not tick down" << '\n';
        success = false;
    }
    if (!almostEqual(afterFirst.activeTimer, 0.5f))
    {
        std::cerr << "Skill active timer did not tick down" << '\n';
        success = false;
    }
    if (!almostEqual(sim.spawnSlowTimer, 0.0f))
    {
        std::cerr << "Spawn slow timer not cleared" << '\n';
        success = false;
    }
    if (!almostEqual(sim.spawnSlowMultiplier, 1.0f))
    {
        std::cerr << "Spawn slow multiplier not reset" << '\n';
        success = false;
    }
    if (!almostEqual(sim.commanderInvulnTimer, 0.25f))
    {
        std::cerr << "Commander invulnerability did not tick down" << '\n';
        success = false;
    }
    if (sim.rallyState)
    {
        std::cerr << "Rally state not suppressed" << '\n';
        success = false;
    }
    if (!context.componentsDirty)
    {
        std::cerr << "Update did not mark components dirty" << '\n';
        success = false;
    }

    context.componentsDirty = false;
    system.update(0.5f, context);
    const RuntimeSkill &afterSecond = sim.skills[0];
    if (!almostEqual(afterSecond.cooldownRemaining, 3.0f))
    {
        std::cerr << "Skill cooldown did not continue ticking" << '\n';
        success = false;
    }
    if (!almostEqual(afterSecond.activeTimer, 0.0f))
    {
        std::cerr << "Skill active timer did not expire" << '\n';
        success = false;
    }
    if (!almostEqual(sim.spawnRateMultiplier, 1.0f))
    {
        std::cerr << "Spawn rate multiplier not restored" << '\n';
        success = false;
    }
    if (!context.componentsDirty)
    {
        std::cerr << "Second update did not mark components dirty" << '\n';
        success = false;
    }

    return success;
}

bool testToggleRallySkill()
{
    world::WorldState world;
    world.reset();
    auto &sim = world.legacy();
    sim.skills.clear();

    RuntimeSkill toggleSkill{};
    toggleSkill.def.id = "toggle";
    toggleSkill.def.type = SkillType::ToggleFollow;
    toggleSkill.def.cooldown = 7.0f;
    sim.skills.push_back(toggleSkill);

    world::systems::JobAbilitySystem system;
    ActionBuffer actions;
    auto context = world.makeSystemContext(actions);
    context.componentsDirty = false;

    systems::SkillCommand command{0, sim.commander.pos + Vec2{16.0f, 0.0f}};

    sim.moraleSummary.rallySuppressed = true;
    system.triggerSkill(context, command);
    if (sim.rallyState)
    {
        std::cerr << "Rally activated while suppressed" << '\n';
        return false;
    }
    if (!almostEqual(sim.skills[0].cooldownRemaining, 0.0f))
    {
        std::cerr << "Suppressed rally altered cooldown" << '\n';
        return false;
    }
    if (context.componentsDirty)
    {
        std::cerr << "Suppressed rally marked components dirty" << '\n';
        return false;
    }

    sim.moraleSummary.rallySuppressed = false;
    context.componentsDirty = false;
    system.triggerSkill(context, command);
    if (!sim.rallyState)
    {
        std::cerr << "Rally did not activate" << '\n';
        return false;
    }
    if (!almostEqual(sim.skills[0].cooldownRemaining, toggleSkill.def.cooldown))
    {
        std::cerr << "Rally cooldown not applied" << '\n';
        return false;
    }
    if (!context.componentsDirty)
    {
        std::cerr << "Rally activation did not mark components dirty" << '\n';
        return false;
    }

    return true;
}

bool testSpawnRateTrigger()
{
    world::WorldState world;
    world.reset();
    auto &sim = world.legacy();
    sim.skills.clear();

    RuntimeSkill spawnSkill{};
    spawnSkill.def.id = "surge";
    spawnSkill.def.type = SkillType::SpawnRate;
    spawnSkill.def.cooldown = 8.0f;
    spawnSkill.def.duration = 3.0f;
    spawnSkill.def.multiplier = 2.0f;
    sim.skills.push_back(spawnSkill);

    world::systems::JobAbilitySystem system;
    ActionBuffer actions;
    auto context = world.makeSystemContext(actions);
    context.componentsDirty = false;

    systems::SkillCommand command{0, sim.commander.pos};
    system.triggerSkill(context, command);

    const RuntimeSkill &updated = sim.skills[0];
    bool success = true;
    if (!almostEqual(updated.cooldownRemaining, spawnSkill.def.cooldown))
    {
        std::cerr << "Spawn rate cooldown not applied" << '\n';
        success = false;
    }
    if (!almostEqual(updated.activeTimer, spawnSkill.def.duration))
    {
        std::cerr << "Spawn rate duration not applied" << '\n';
        success = false;
    }
    if (!almostEqual(sim.spawnRateMultiplier, spawnSkill.def.multiplier))
    {
        std::cerr << "Spawn rate multiplier not applied" << '\n';
        success = false;
    }
    if (!context.componentsDirty)
    {
        std::cerr << "Spawn rate activation did not mark components dirty" << '\n';
        success = false;
    }

    return success;
}

} // namespace

int main()
{
    bool success = true;
    if (!testUpdateTimersAndFlags())
    {
        success = false;
    }
    if (!testToggleRallySkill())
    {
        success = false;
    }
    if (!testSpawnRateTrigger())
    {
        success = false;
    }
    return success ? 0 : 1;
}
