#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

struct CaptureRuntime;

#include "world/systems/JobAbilitySystem.h"

#include "config/AppConfig.h"
#include "core/Vec2.h"
#include "input/ActionBuffer.h"
#include "world/SkillRuntime.h"
#include "world/WorldState.h"
#include "world/LegacyTypes.h"

namespace world
{
using ::RuntimeSkill;
using ::SkillDef;
} // namespace world

namespace
{

using world::systems::JobAbilitySystem;
using world::systems::MissionContext;
using world::systems::SystemContext;
using world::CaptureRuntime;

struct ContextHarness
{
    world::EntityRegistry registry;
    world::ComponentPool<Unit> allies;
    world::ComponentPool<EnemyUnit> enemies;
    world::ComponentPool<WallSegment> walls;
    world::ComponentPool<CaptureRuntime> missionZones;
    HUDState hud{};
    world::FrameAllocator frameAllocator{};
    MissionContext mission;
    SystemContext context;

    ContextHarness(world::LegacySimulation &sim, const ActionBuffer &actions)
        : mission{sim.hasMission,
                  sim.missionConfig,
                  sim.missionMode,
                  sim.missionUI,
                  sim.missionFail,
                  sim.missionTimer,
                  sim.missionVictoryCountdown},
          context{sim,
                  registry,
                  allies,
                  enemies,
                  walls,
                  missionZones,
                  sim.commander,
                  hud,
                  sim.baseHp,
                  sim.orderActive,
                  sim.orderTimer,
                  sim.waveScriptComplete,
                  sim.spawnerIdle,
                  sim.timeSinceLastEnemySpawn,
                  sim.skills,
                  sim.selectedSkill,
                  sim.rallyState,
                  sim.spawnRateMultiplier,
                  sim.spawnSlowMultiplier,
                  sim.spawnSlowTimer,
                  sim.yunas,
                  sim.enemies,
                  sim.walls,
                  sim.gates,
                  sim.yunaRespawns,
                  sim.commanderRespawnTimer,
                  sim.commanderInvulnTimer,
                  frameAllocator,
                  mission,
                  actions,
                  nullptr,
                  nullptr}
    {}
};

bool almostEqual(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) <= epsilon;
}

bool findDefaultSkill(const std::string &id, world::SkillDef &out)
{
    const std::vector<world::SkillDef> defaults = buildDefaultSkills();
    const auto it = std::find_if(defaults.begin(), defaults.end(), [&id](const world::SkillDef &def) {
        return def.id == id;
    });
    if (it == defaults.end())
    {
        return false;
    }
    out = *it;
    return true;
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
    ContextHarness harness(sim, actions);
    auto &context = harness.context;
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
    world::SkillDef rallyDef{};
    if (!findDefaultSkill("rally", rallyDef))
    {
        std::cerr << "Default rally skill not found" << '\n';
        return false;
    }
    world.configureSkills(std::vector<world::SkillDef>{rallyDef});
    auto &sim = world.legacy();

    world::systems::JobAbilitySystem system;
    ActionBuffer actions;
    ContextHarness harness(sim, actions);
    auto &context = harness.context;
    context.componentsDirty = false;

    world::systems::SkillCommand command{0, sim.commander.pos + Vec2{16.0f, 0.0f}};

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
    if (!almostEqual(sim.skills[0].cooldownRemaining, rallyDef.cooldown))
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
    world::SkillDef surgeDef{};
    if (!findDefaultSkill("surge", surgeDef))
    {
        std::cerr << "Default surge skill not found" << '\n';
        return false;
    }
    world.configureSkills(std::vector<world::SkillDef>{surgeDef});
    auto &sim = world.legacy();

    world::systems::JobAbilitySystem system;
    ActionBuffer actions;
    ContextHarness harness(sim, actions);
    auto &context = harness.context;
    context.componentsDirty = false;

    world::systems::SkillCommand command{0, sim.commander.pos};
    system.triggerSkill(context, command);

    const RuntimeSkill &updated = sim.skills[0];
    bool success = true;
    if (!almostEqual(updated.cooldownRemaining, surgeDef.cooldown))
    {
        std::cerr << "Spawn rate cooldown not applied" << '\n';
        success = false;
    }
    if (!almostEqual(updated.activeTimer, surgeDef.duration))
    {
        std::cerr << "Spawn rate duration not applied" << '\n';
        success = false;
    }
    if (!almostEqual(sim.spawnRateMultiplier, surgeDef.multiplier))
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

bool testRegisteredHandlerDispatch()
{
    world::WorldState world;
    world.reset();
    auto &sim = world.legacy();
    sim.skills.clear();

    RuntimeSkill customSkill{};
    customSkill.def.id = "custom";
    customSkill.def.type = SkillType::ToggleFollow;
    sim.skills.push_back(customSkill);

    bool handlerInvoked = false;
    JobAbilitySystem::clearSkillHandlers();
    JobAbilitySystem::registerSkillHandler(
        customSkill.def.id,
        [&handlerInvoked](JobAbilitySystem &, world::systems::SystemContext &context, RuntimeSkill &skill,
                          const world::systems::SkillCommand &) {
            handlerInvoked = true;
            skill.cooldownRemaining = 1.5f;
            context.requestComponentSync();
        });

    world::systems::JobAbilitySystem system;
    ActionBuffer actions;
    ContextHarness harness(sim, actions);
    auto &context = harness.context;
    context.componentsDirty = false;

    world::systems::SkillCommand command{0, sim.commander.pos};
    system.triggerSkill(context, command);

    bool success = true;
    if (!handlerInvoked)
    {
        std::cerr << "Registered handler did not fire" << '\n';
        success = false;
    }
    if (!almostEqual(sim.skills[0].cooldownRemaining, 1.5f))
    {
        std::cerr << "Registered handler did not update cooldown" << '\n';
        success = false;
    }
    if (!context.componentsDirty)
    {
        std::cerr << "Registered handler did not mark components dirty" << '\n';
        success = false;
    }

    handlerInvoked = false;
    context.componentsDirty = false;
    system.triggerSkill(context, command);
    if (handlerInvoked)
    {
        std::cerr << "Handler invoked despite cooldown" << '\n';
        success = false;
    }
    if (context.componentsDirty)
    {
        std::cerr << "Cooldown prevented trigger but still marked components dirty" << '\n';
        success = false;
    }

    JobAbilitySystem::clearSkillHandlers();
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
    if (!testRegisteredHandlerDispatch())
    {
        success = false;
    }
    return success ? 0 : 1;
}
