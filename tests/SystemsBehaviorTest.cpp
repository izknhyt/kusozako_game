#include "world/WorldState.h"

#include "config/AppConfig.h"
#include "input/ActionBuffer.h"
#include "world/MoraleTypes.h"

#include <cmath>
#include <iostream>
#include <random>

namespace
{

bool almostEqual(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) <= epsilon;
}

bool testFormationSwitchAlignment()
{
    world::WorldState world;
    auto &sim = world.legacy();
    sim.formationDefaults.alignDuration = 2.5f;
    sim.formationDefaults.defenseMultiplier = 1.5f;
    sim.formationAlignTimer = 0.0f;
    sim.formationDefenseMul = 1.0f;

    // Ensure we have some followers so alignment logic runs.
    sim.yunas.clear();
    sim.yunas.resize(3);
    for (Unit &unit : sim.yunas)
    {
        unit.followBySkill = true;
    }

    world.cycleFormation(1);
    if (!almostEqual(sim.formationAlignTimer, sim.formationDefaults.alignDuration))
    {
        std::cerr << "Formation align timer not reset" << '\n';
        return false;
    }
    if (!almostEqual(sim.formationDefenseMul, sim.formationDefaults.defenseMultiplier))
    {
        std::cerr << "Formation defense multiplier not applied" << '\n';
        return false;
    }

    ActionBuffer actions;
    world.step(1.0f, actions);
    if (!almostEqual(sim.formationAlignTimer, 1.5f))
    {
        std::cerr << "Formation align timer did not tick down" << '\n';
        return false;
    }

    world.step(2.0f, actions);
    if (!almostEqual(sim.formationAlignTimer, 0.0f))
    {
        std::cerr << "Formation align timer did not expire" << '\n';
        return false;
    }
    if (!almostEqual(sim.formationDefenseMul, 1.0f))
    {
        std::cerr << "Formation defense multiplier did not reset" << '\n';
        return false;
    }

    return true;
}

bool testCommanderDeathMorale()
{
    world::WorldState world;
    world.reset();
    auto &sim = world.legacy();
    sim.config.morale.leaderDownWindow = 1.0f;

    sim.yunas.clear();
    sim.yunas.resize(2);
    sim.yunas[0].followBySkill = true;
    sim.yunas[1].followBySkill = false;

    ActionBuffer actions;
    world.step(0.1f, actions);

    sim.commander.alive = false;
    world.step(0.1f, actions);

    if (sim.hud.morale.commanderState != MoraleState::LeaderDown)
    {
        std::cerr << "Commander morale icon not updated" << '\n';
        return false;
    }
    for (const Unit &unit : sim.yunas)
    {
        if (unit.moraleState != MoraleState::LeaderDown)
        {
            std::cerr << "Unit did not enter leader-down state" << '\n';
            return false;
        }
    }

    world.step(1.1f, actions);

    if (sim.yunas[0].moraleState != MoraleState::Panic)
    {
        std::cerr << "Follower did not panic after leader down window" << '\n';
        return false;
    }
    if (sim.yunas[1].moraleState != MoraleState::Mesomeso)
    {
        std::cerr << "Non-follower did not enter mesomeso state" << '\n';
        return false;
    }
    if (sim.hud.morale.panicCount == 0 || sim.hud.morale.mesomesoCount == 0)
    {
        std::cerr << "HUD morale counters not updated" << '\n';
        return false;
    }

    return true;
}

bool testSpawnPityWeighting()
{
    world::LegacySimulation sim;
    sim.config.jobSpawn.pity.repeatLimit = 3;
    sim.config.jobSpawn.pity.unseenBoost = 5.0f;
    sim.config.jobSpawn.weights = {1.0f, 1.0f, 1.0f};
    sim.jobHistoryLimit = 8;
    sim.jobHistory.clear();
    for (int i = 0; i < 3; ++i)
    {
        sim.jobHistory.push_back(UnitJob::Warrior);
    }

    const float baseWeight = sim.config.jobSpawn.weight(UnitJob::Warrior);
    const float boostedWeight = baseWeight * sim.config.jobSpawn.pity.unseenBoost;
    const float totalWeight = baseWeight + boostedWeight * 2.0f;
    std::uniform_real_distribution<float> pickDist(0.0f, totalWeight);

    std::uint32_t seed = 0;
    for (std::uint32_t candidate = 1; candidate < 100000; ++candidate)
    {
        std::mt19937 preview(candidate);
        const float pick = pickDist(preview);
        if (pick > baseWeight && pick <= baseWeight + boostedWeight)
        {
            seed = candidate;
            break;
        }
    }

    if (seed == 0)
    {
        std::cerr << "Failed to find deterministic seed for pity test" << '\n';
        return false;
    }

    sim.rng.seed(seed);
    const UnitJob selected = sim.chooseSpawnJob();
    if (selected != UnitJob::Archer)
    {
        std::cerr << "Pity weighting did not prefer unseen job" << '\n';
        return false;
    }
    if (sim.jobHistory.back() != selected)
    {
        std::cerr << "Spawn history not updated" << '\n';
        return false;
    }

    return true;
}

} // namespace

int main()
{
    bool success = true;
    if (!testFormationSwitchAlignment())
    {
        success = false;
    }
    if (!testCommanderDeathMorale())
    {
        success = false;
    }
    if (!testSpawnPityWeighting())
    {
        success = false;
    }
    return success ? 0 : 1;
}
