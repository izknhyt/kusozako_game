#include "world/WorldState.h"
#include "world/FrameAllocator.h"

#include "config/AppConfig.h"
#include "input/ActionBuffer.h"
#include "world/MoraleTypes.h"
#include "world/SpatialGrid.h"
#include "world/systems/CombatSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <new>
#include <random>

namespace
{

bool almostEqual(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) <= epsilon;
}

struct NaiveCombatState
{
    CommanderUnit commander;
    std::vector<Unit> yunas;
    std::vector<EnemyUnit> enemies;
    std::vector<WallSegment> walls;
    float baseHp = 0.0f;
    float commanderInvulnTimer = 0.0f;
};

NaiveCombatState runNaiveCombatStep(const LegacySimulation &sim,
                                    const std::vector<GateRuntime> &gates,
                                    NaiveCombatState state,
                                    float dt,
                                    float formationDamageScale)
{
    std::vector<float> yunaDamage(state.yunas.size(), 0.0f);
    float commanderDamage = 0.0f;

    for (EnemyUnit &enemy : state.enemies)
    {
        for (WallSegment &wall : state.walls)
        {
            const float combined = enemy.radius + wall.radius;
            const float distSq = lengthSq(enemy.pos - wall.pos);
            if (distSq <= combined * combined)
            {
                float dist = std::sqrt(std::max(distSq, 0.0001f));
                Vec2 normal = dist > 0.0f ? (enemy.pos - wall.pos) / dist : Vec2{1.0f, 0.0f};
                const float overlap = combined - dist;
                enemy.pos += normal * overlap;
                wall.hp -= enemy.dpsWall * dt;
            }
        }
    }

    if (state.commander.alive)
    {
        for (EnemyUnit &enemy : state.enemies)
        {
            const float combined = state.commander.radius + enemy.radius;
            if (lengthSq(state.commander.pos - enemy.pos) <= combined * combined)
            {
                enemy.hp -= sim.commanderStats.dps * dt;
                if (state.commanderInvulnTimer <= 0.0f)
                {
                    commanderDamage += enemy.dpsUnit * dt * formationDamageScale;
                }
            }
        }
        for (const GateRuntime &gate : gates)
        {
            if (gate.destroyed)
            {
                continue;
            }
            const float combined = state.commander.radius + gate.radius;
            if (lengthSq(state.commander.pos - gate.pos) <= combined * combined)
            {
                // Gate resolution is ignored for the parity harness to avoid side effects.
            }
        }
    }

    for (std::size_t i = 0; i < state.yunas.size(); ++i)
    {
        Unit &yuna = state.yunas[i];
        for (EnemyUnit &enemy : state.enemies)
        {
            const float combined = yuna.radius + enemy.radius;
            if (lengthSq(yuna.pos - enemy.pos) <= combined * combined)
            {
                float attackDps = sim.yunaStats.dps * std::max(0.01f, yuna.moraleAccuracyMultiplier);
                enemy.hp -= attackDps * dt;
                float incoming = enemy.dpsUnit * dt * formationDamageScale;
                incoming /= std::max(0.01f, yuna.moraleDefenseMultiplier);
                yunaDamage[i] += incoming;
            }
        }
        for (const GateRuntime &gate : gates)
        {
            if (gate.destroyed)
            {
                continue;
            }
            const float combined = yuna.radius + gate.radius;
            if (lengthSq(yuna.pos - gate.pos) <= combined * combined)
            {
                // The harness does not simulate gate destruction, mirroring the empty gate setup.
            }
        }
    }

    state.enemies.erase(std::remove_if(state.enemies.begin(), state.enemies.end(), [](const EnemyUnit &enemy) {
        return enemy.hp <= 0.0f;
    }), state.enemies.end());

    if (state.commander.alive && commanderDamage > 0.0f)
    {
        state.commander.hp -= commanderDamage;
    }

    if (!state.yunas.empty())
    {
        std::vector<Unit> survivors;
        survivors.reserve(state.yunas.size());
        for (std::size_t i = 0; i < state.yunas.size(); ++i)
        {
            Unit yuna = state.yunas[i];
            if (yunaDamage[i] > 0.0f)
            {
                yuna.hp -= yunaDamage[i];
            }
            if (yuna.hp > 0.0f)
            {
                survivors.push_back(yuna);
            }
        }
        state.yunas.swap(survivors);
    }

    const float baseRadius = std::max(sim.config.base_aabb.x, sim.config.base_aabb.y) * 0.5f;
    for (const EnemyUnit &enemy : state.enemies)
    {
        const float combined = baseRadius + enemy.radius;
        if (lengthSq(enemy.pos - sim.basePos) <= combined * combined)
        {
            state.baseHp -= enemy.dpsBase * dt;
        }
    }

    state.walls.erase(std::remove_if(state.walls.begin(), state.walls.end(), [](const WallSegment &wall) {
        return wall.hp <= 0.0f;
    }), state.walls.end());

    return state;
}

struct CollisionCounts
{
    std::size_t enemyWall = 0;
    std::size_t yunaEnemy = 0;
};

CollisionCounts countNaivePairs(const std::vector<Unit> &yunas,
                                const std::vector<EnemyUnit> &enemies,
                                const std::vector<WallSegment> &walls)
{
    CollisionCounts counts{};
    for (const EnemyUnit &enemy : enemies)
    {
        for (const WallSegment &wall : walls)
        {
            ++counts.enemyWall;
            volatile float distSq = lengthSq(enemy.pos - wall.pos);
            (void)distSq;
        }
    }
    for (const Unit &yuna : yunas)
    {
        for (const EnemyUnit &enemy : enemies)
        {
            ++counts.yunaEnemy;
            volatile float distSq = lengthSq(yuna.pos - enemy.pos);
            (void)distSq;
        }
    }
    return counts;
}

struct GridMetrics
{
    std::size_t enemyWallChecks = 0;
    std::size_t yunaEnemyChecks = 0;
};

GridMetrics countGridPairs(const LegacySimulation &sim,
                           const std::vector<Unit> &yunas,
                           const std::vector<EnemyUnit> &enemies,
                           const std::vector<WallSegment> &walls)
{
    GridMetrics metrics{};
    if (enemies.empty() && walls.empty())
    {
        return metrics;
    }

    world::SpatialGrid grid;
    const int configuredTileSize = sim.mapDefs.tile_size > 0 ? sim.mapDefs.tile_size : 16;
    const float cellSize = std::max(1.0f, static_cast<float>(configuredTileSize));
    grid.configure(sim.worldMin, sim.worldMax, cellSize);
    grid.clear();

    for (std::size_t i = 0; i < walls.size(); ++i)
    {
        grid.insertWall(i, walls[i].pos, walls[i].radius);
    }
    for (std::size_t i = 0; i < enemies.size(); ++i)
    {
        grid.insertEnemy(i, enemies[i].pos, enemies[i].radius);
    }
    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        grid.insertUnit(i, yunas[i].pos, yunas[i].radius);
    }

    std::vector<std::size_t> cellScratch;
    std::vector<std::size_t> indexScratch;
    std::vector<std::size_t> enemyScratch;
    std::vector<std::uint32_t> wallMarks(walls.size(), 0);
    std::vector<std::uint32_t> enemyMarks(enemies.size(), 0);
    std::uint32_t wallStamp = 1;
    std::uint32_t enemyStamp = 1;

    auto nextStamp = [](std::uint32_t &stamp, std::vector<std::uint32_t> &marks) {
        ++stamp;
        if (stamp == 0)
        {
            std::fill(marks.begin(), marks.end(), 0);
            ++stamp;
        }
    };

    for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex)
    {
        const EnemyUnit &enemy = enemies[enemyIndex];
        grid.queryCells(enemy.pos, enemy.radius, cellScratch);
        nextStamp(wallStamp, wallMarks);
        indexScratch.clear();
        for (std::size_t cellIndex : cellScratch)
        {
            const auto &cell = grid.cell(cellIndex);
            for (std::size_t idx : cell.walls)
            {
                if (idx >= walls.size())
                {
                    continue;
                }
                if (wallMarks[idx] != wallStamp)
                {
                    wallMarks[idx] = wallStamp;
                    indexScratch.push_back(idx);
                }
            }
        }
        for (std::size_t wallIndex : indexScratch)
        {
            ++metrics.enemyWallChecks;
            volatile float distSq = lengthSq(enemy.pos - walls[wallIndex].pos);
            (void)distSq;
        }
    }

    for (const Unit &yuna : yunas)
    {
        grid.queryCells(yuna.pos, yuna.radius, cellScratch);
        nextStamp(enemyStamp, enemyMarks);
        enemyScratch.clear();
        for (std::size_t cellIndex : cellScratch)
        {
            const auto &cell = grid.cell(cellIndex);
            for (std::size_t idx : cell.enemies)
            {
                if (idx >= enemies.size())
                {
                    continue;
                }
                if (enemyMarks[idx] != enemyStamp)
                {
                    enemyMarks[idx] = enemyStamp;
                    enemyScratch.push_back(idx);
                }
            }
        }
        for (std::size_t enemyIndex : enemyScratch)
        {
            ++metrics.yunaEnemyChecks;
            volatile float distSq = lengthSq(yuna.pos - enemies[enemyIndex].pos);
            (void)distSq;
        }
    }

    return metrics;
}

struct BenchmarkScenario
{
    std::vector<Unit> yunas;
    std::vector<EnemyUnit> enemies;
    std::vector<WallSegment> walls;
};

BenchmarkScenario makeBenchmarkScenario()
{
    BenchmarkScenario scenario;
    scenario.yunas.reserve(320);
    scenario.enemies.reserve(320);
    scenario.walls.reserve(150);

    const std::size_t rows = 20;
    const std::size_t cols = 16;
    for (std::size_t row = 0; row < rows; ++row)
    {
        for (std::size_t col = 0; col < cols; ++col)
        {
            const float baseX = 220.0f + static_cast<float>(col) * 28.0f;
            const float baseY = 220.0f + static_cast<float>(row) * 28.0f;

            Unit yuna{};
            yuna.pos = {baseX, baseY};
            yuna.radius = 8.0f;
            yuna.hp = 100.0f;
            yuna.moraleAccuracyMultiplier = 1.0f;
            yuna.moraleDefenseMultiplier = 1.0f;
            scenario.yunas.push_back(yuna);

            EnemyUnit enemy{};
            enemy.pos = {baseX + 18.0f, baseY + 6.0f};
            enemy.radius = 10.0f;
            enemy.hp = 120.0f;
            enemy.dpsUnit = 5.0f;
            enemy.dpsBase = 3.0f;
            enemy.dpsWall = 4.0f;
            enemy.type = EnemyArchetype::Slime;
            scenario.enemies.push_back(enemy);
        }
    }

    for (std::size_t row = 0; row < 6; ++row)
    {
        for (std::size_t col = 0; col < 25; ++col)
        {
            WallSegment wall{};
            wall.pos = {360.0f + static_cast<float>(col) * 32.0f,
                        180.0f + static_cast<float>(row) * 40.0f};
            wall.radius = 14.0f;
            wall.hp = 240.0f;
            wall.life = 1.0f;
            scenario.walls.push_back(wall);
        }
    }

    return scenario;
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

bool testCombatSpatialGridParity()
{
    LegacySimulation sim{};
    sim.worldMin = {0.0f, 0.0f};
    sim.worldMax = {1024.0f, 768.0f};
    sim.basePos = {512.0f, 384.0f};
    sim.mapDefs.tile_size = 32;
    sim.config.base_aabb = {64.0f, 64.0f};
    sim.config.pixels_per_unit = 1.0f;
    sim.config.jobCommon.fizzleChance = 0.0f;
    sim.config.warriorJob.cooldown = 0.0f;
    sim.config.warriorJob.accuracyMultiplier = 0.0f;
    sim.config.warriorJob.stumbleSeconds = 0.0f;
    sim.slimeStats.speed_u_s = 0.0f;
    sim.wallbreakerStats.speed_u_s = 0.0f;
    sim.yunaStats.dps = 9.0f;
    sim.yunaStats.hp = 110.0f;
    sim.commanderStats.dps = 12.0f;
    sim.commanderStats.hp = 240.0f;

    Unit warrior{};
    warrior.pos = {500.0f, 380.0f};
    warrior.radius = 10.0f;
    warrior.hp = 120.0f;
    warrior.moraleAccuracyMultiplier = 1.0f;
    warrior.moraleDefenseMultiplier = 1.0f;
    warrior.job.job = UnitJob::Warrior;
    warrior.job.cooldown = 1.0f;
    warrior.job.endlag = 0.5f;
    sim.yunas = {warrior};
    warrior.pos = {528.0f, 392.0f};
    sim.yunas.push_back(warrior);

    EnemyUnit enemy{};
    enemy.pos = {510.0f, 384.0f};
    enemy.radius = 12.0f;
    enemy.hp = 140.0f;
    enemy.dpsUnit = 4.0f;
    enemy.dpsBase = 3.0f;
    enemy.dpsWall = 6.0f;
    enemy.type = EnemyArchetype::Slime;
    sim.enemies = {enemy};
    enemy.pos = {540.0f, 386.0f};
    sim.enemies.push_back(enemy);

    WallSegment wall{};
    wall.pos = {522.0f, 382.0f};
    wall.radius = 16.0f;
    wall.hp = 200.0f;
    wall.life = 1.0f;
    sim.walls = {wall};
    wall.pos = {555.0f, 386.0f};
    sim.walls.push_back(wall);

    sim.commander.pos = {508.0f, 380.0f};
    sim.commander.radius = 14.0f;
    sim.commander.hp = 220.0f;
    sim.commander.alive = true;

    float baseHp = 900.0f;
    bool orderActive = false;
    float orderTimer = 0.0f;
    bool waveScriptComplete = false;
    bool spawnerIdle = true;
    float timeSinceLastEnemySpawn = 0.0f;
    std::vector<RuntimeSkill> skills;
    int selectedSkill = -1;
    bool rallyState = false;
    float spawnRateMultiplier = 1.0f;
    float spawnSlowMultiplier = 1.0f;
    float spawnSlowTimer = 0.0f;
    std::vector<LegacySimulation::PendingRespawn> respawns;
    float commanderRespawnTimer = 0.0f;
    float commanderInvulnTimer = 0.0f;

    bool hasMission = false;
    MissionConfig missionConfig{};
    MissionMode missionMode = MissionMode::None;
    MissionUIOptions missionUI{};
    MissionFailConditions missionFail{};
    float missionTimer = 0.0f;
    float missionVictoryCountdown = -1.0f;

    ActionBuffer actions;
    std::vector<GateRuntime> gates;

    LegacySimulation naiveSim = sim;
    NaiveCombatState naiveState{naiveSim.commander, naiveSim.yunas, naiveSim.enemies, naiveSim.walls, baseHp,
                                commanderInvulnTimer};
    const float defenseMultiplier =
        naiveSim.formationAlignTimer > 0.0f ? std::max(naiveSim.formationDefenseMul, 0.01f) : 1.0f;
    const float formationDamageScale = 1.0f / defenseMultiplier;
    const float dt = 0.6f;

    naiveState = runNaiveCombatStep(naiveSim, gates, std::move(naiveState), dt, formationDamageScale);

    EntityRegistry registry;
    ComponentPool<Unit> alliesPool;
    ComponentPool<EnemyUnit> enemyPool;
    ComponentPool<WallSegment> wallPool;
    using CaptureRuntime = world::LegacySimulation::CaptureRuntime;
    ComponentPool<CaptureRuntime> missionPool;
    HUDState hud;

    world::systems::CombatSystem system;
    world::systems::SystemContext context{
        sim,
        registry,
        alliesPool,
        enemyPool,
        wallPool,
        missionPool,
        sim.commander,
        hud,
        baseHp,
        orderActive,
        orderTimer,
        waveScriptComplete,
        spawnerIdle,
        timeSinceLastEnemySpawn,
        skills,
        selectedSkill,
        rallyState,
        spawnRateMultiplier,
        spawnSlowMultiplier,
        spawnSlowTimer,
        sim.yunas,
        sim.enemies,
        sim.walls,
        gates,
        respawns,
        commanderRespawnTimer,
        commanderInvulnTimer,
        {hasMission, missionConfig, missionMode, missionUI, missionFail, missionTimer, missionVictoryCountdown},
        actions,
        nullptr,
        nullptr};

    system.update(dt, context);

    bool success = true;
    if (!almostEqual(sim.commander.hp, naiveState.commander.hp))
    {
        std::cerr << "Commander HP mismatch" << '\n';
        success = false;
    }
    if (!almostEqual(context.baseHp, naiveState.baseHp))
    {
        std::cerr << "Base HP mismatch" << '\n';
        success = false;
    }
    if (sim.yunas.size() != naiveState.yunas.size())
    {
        std::cerr << "Yuna count mismatch" << '\n';
        success = false;
    }
    else
    {
        for (std::size_t i = 0; i < sim.yunas.size(); ++i)
        {
            if (!almostEqual(sim.yunas[i].hp, naiveState.yunas[i].hp))
            {
                std::cerr << "Yuna HP mismatch" << '\n';
                success = false;
                break;
            }
        }
    }
    if (sim.enemies.size() != naiveState.enemies.size())
    {
        std::cerr << "Enemy count mismatch" << '\n';
        success = false;
    }
    else
    {
        for (std::size_t i = 0; i < sim.enemies.size(); ++i)
        {
            if (!almostEqual(sim.enemies[i].hp, naiveState.enemies[i].hp))
            {
                std::cerr << "Enemy HP mismatch" << '\n';
                success = false;
                break;
            }
            if (!almostEqual(sim.enemies[i].pos.x, naiveState.enemies[i].pos.x) ||
                !almostEqual(sim.enemies[i].pos.y, naiveState.enemies[i].pos.y))
            {
                std::cerr << "Enemy position mismatch" << '\n';
                success = false;
                break;
            }
        }
    }
    if (sim.walls.size() != naiveState.walls.size())
    {
        std::cerr << "Wall count mismatch" << '\n';
        success = false;
    }
    else
    {
        for (std::size_t i = 0; i < sim.walls.size(); ++i)
        {
            if (!almostEqual(sim.walls[i].hp, naiveState.walls[i].hp))
            {
                std::cerr << "Wall HP mismatch" << '\n';
                success = false;
                break;
            }
        }
    }
    return success;
}

bool benchmarkCombatSpatialGrid()
{
    LegacySimulation sim{};
    sim.worldMin = {0.0f, 0.0f};
    sim.worldMax = {1600.0f, 900.0f};
    sim.mapDefs.tile_size = 32;

    BenchmarkScenario scenario = makeBenchmarkScenario();

    const CollisionCounts naiveCounts = countNaivePairs(scenario.yunas, scenario.enemies, scenario.walls);
    const GridMetrics gridCounts = countGridPairs(sim, scenario.yunas, scenario.enemies, scenario.walls);

    if (gridCounts.enemyWallChecks >= naiveCounts.enemyWall)
    {
        std::cerr << "Grid did not reduce enemy-wall checks" << '\n';
        return false;
    }
    if (gridCounts.yunaEnemyChecks >= naiveCounts.yunaEnemy)
    {
        std::cerr << "Grid did not reduce yuna-enemy checks" << '\n';
        return false;
    }

    constexpr int iterations = 4;
    std::size_t naiveTotal = 0;
    std::size_t gridTotal = 0;

    const auto naiveStart = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        const CollisionCounts counts = countNaivePairs(scenario.yunas, scenario.enemies, scenario.walls);
        naiveTotal += counts.enemyWall + counts.yunaEnemy;
    }
    const double naiveMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - naiveStart).count();

    const auto gridStart = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i)
    {
        const GridMetrics counts = countGridPairs(sim, scenario.yunas, scenario.enemies, scenario.walls);
        gridTotal += counts.enemyWallChecks + counts.yunaEnemyChecks;
    }
    const double gridMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gridStart).count();

    if (!(gridMs < naiveMs))
    {
        std::cerr << "Spatial grid was not faster than naive iteration" << '\n';
        return false;
    }
    if (!(gridTotal < naiveTotal))
    {
        std::cerr << "Spatial grid processed more pairs than expected" << '\n';
        return false;
    }

    return true;
}

bool testFrameAllocatorReuseAndLimit()
{
    world::FrameAllocator allocator(1024);
    std::uint8_t *firstBlock = allocator.allocateArray<std::uint8_t>(256);
    std::fill(firstBlock, firstBlock + 256, 0xCD);

    allocator.reset();
    std::uint8_t *secondBlock = allocator.allocateArray<std::uint8_t>(256);
    if (firstBlock != secondBlock)
    {
        std::cerr << "Frame allocator did not reuse storage" << '\n';
        return false;
    }
    if (!std::all_of(secondBlock, secondBlock + 256, [](std::uint8_t value) { return value == 0; }))
    {
        std::cerr << "Frame allocator did not clear memory on reuse" << '\n';
        return false;
    }

    allocator.reset();
    bool threw = false;
    try
    {
        (void)allocator.allocateArray<std::uint8_t>(1025);
    }
    catch (const std::bad_alloc &)
    {
        threw = true;
    }
    if (!threw)
    {
        std::cerr << "Frame allocator failed to enforce capacity" << '\n';
        return false;
    }

    allocator.reset();
    threw = false;
    try
    {
        (void)allocator.allocateArray<std::uint8_t>(800);
        (void)allocator.allocateArray<std::uint8_t>(400);
    }
    catch (const std::bad_alloc &)
    {
        threw = true;
    }
    if (!threw)
    {
        std::cerr << "Frame allocator allowed cumulative overflow" << '\n';
        return false;
    }

    return true;
}

bool testWorldFrameAllocatorReset()
{
    world::WorldState world;
    auto &sim = world.legacy();
    world.reset();

    sim.config.pixels_per_unit = 1.0f;
    sim.basePos = {0.0f, 0.0f};
    sim.commander.alive = true;
    sim.commander.hp = 100.0f;
    sim.commander.radius = 12.0f;
    sim.commander.pos = {150.0f, 140.0f};

    sim.yunas.clear();
    for (int i = 0; i < 24; ++i)
    {
        Unit unit{};
        unit.pos = {150.0f + static_cast<float>(i) * 6.0f, 180.0f};
        unit.radius = 8.0f;
        unit.hp = 50.0f;
        unit.followBySkill = (i % 2) == 0;
        unit.moraleAccuracyMultiplier = 1.0f;
        unit.moraleDefenseMultiplier = 1.0f;
        sim.yunas.push_back(unit);
    }

    sim.enemies.clear();
    for (int i = 0; i < 12; ++i)
    {
        EnemyUnit enemy{};
        enemy.pos = {220.0f + static_cast<float>(i) * 8.0f, 210.0f};
        enemy.radius = 9.0f;
        enemy.hp = 80.0f;
        enemy.dpsUnit = 5.0f;
        enemy.dpsWall = 2.0f;
        enemy.dpsBase = 1.0f;
        sim.enemies.push_back(enemy);
    }

    sim.walls.clear();
    for (int i = 0; i < 4; ++i)
    {
        WallSegment wall{};
        wall.pos = {260.0f + static_cast<float>(i) * 16.0f, 200.0f};
        wall.radius = 12.0f;
        wall.hp = 120.0f;
        wall.life = 1.0f;
        sim.walls.push_back(wall);
    }

    ActionBuffer actions;
    world.step(0.016f, actions);
    const std::size_t firstUsage = world.frameAllocatorUsage();
    if (firstUsage == 0)
    {
        std::cerr << "Frame allocator reported zero usage after systems ran" << '\n';
        return false;
    }
    if (firstUsage > world.frameAllocatorCapacity())
    {
        std::cerr << "Frame allocator exceeded capacity" << '\n';
        return false;
    }

    world.step(0.016f, actions);
    const std::size_t secondUsage = world.frameAllocatorUsage();
    if (secondUsage != firstUsage)
    {
        std::cerr << "Frame allocator usage changed across identical frames" << '\n';
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
    if (!testCombatSpatialGridParity())
    {
        success = false;
    }
    if (!benchmarkCombatSpatialGrid())
    {
        success = false;
    }
    if (!testFrameAllocatorReuseAndLimit())
    {
        success = false;
    }
    if (!testWorldFrameAllocatorReset())
    {
        success = false;
    }
    return success ? 0 : 1;
}
