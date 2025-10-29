#include "world/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace world::systems
{

namespace
{

float triggerWarriorSwing(Unit &yuna, LegacySimulation &sim)
{
    if (yuna.job.job != UnitJob::Warrior)
    {
        return 0.0f;
    }
    JobRuntimeState &job = yuna.job;
    if (job.cooldown > 0.0f || job.endlag > 0.0f || job.warrior.stumbleTimer > 0.0f)
    {
        return 0.0f;
    }
    const float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
    job.cooldown = std::max(0.0f, sim.config.warriorJob.cooldown * intervalMul);
    job.endlag = std::max(job.endlag, sim.config.jobCommon.endlagSeconds);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(sim.rng) < sim.config.jobCommon.fizzleChance)
    {
        sim.pushTelemetry("Warrior skill fizzled");
        return 0.0f;
    }
    if (dist(sim.rng) <= sim.config.warriorJob.accuracyMultiplier)
    {
        sim.pushTelemetry("Warrior swing!");
        return sim.yunaStats.dps / intervalMul;
    }
    job.warrior.stumbleTimer = sim.config.warriorJob.stumbleSeconds;
    sim.pushTelemetry("Warrior stumbled");
    return 0.0f;
}

void triggerArcherFocus(Unit &yuna, LegacySimulation &sim)
{
    if (yuna.job.job != UnitJob::Archer)
    {
        return;
    }
    JobRuntimeState &job = yuna.job;
    if (job.cooldown > 0.0f || job.endlag > 0.0f || job.archer.focusReady)
    {
        return;
    }
    const float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
    job.cooldown = std::max(0.0f, sim.config.archerJob.cooldown * intervalMul);
    job.endlag = std::max(job.endlag, sim.config.jobCommon.endlagSeconds);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(sim.rng) < sim.config.jobCommon.fizzleChance)
    {
        job.archer.focusReady = false;
        job.archer.holdTimer = 0.0f;
        sim.pushTelemetry("Archer focus fizzled");
        return;
    }
    job.archer.focusReady = true;
    job.archer.holdTimer = sim.config.archerJob.holdSeconds;
    sim.pushTelemetry("Archer focus");
}

void triggerShieldTaunt(Unit &yuna, LegacySimulation &sim, std::vector<EnemyUnit> &enemies)
{
    if (yuna.job.job != UnitJob::Shield)
    {
        return;
    }
    JobRuntimeState &job = yuna.job;
    if (job.cooldown > 0.0f || job.endlag > 0.0f)
    {
        return;
    }
    const float radiusUnits = sim.config.shieldJob.radiusUnits;
    if (radiusUnits <= 0.0f)
    {
        return;
    }
    const float radiusPx = radiusUnits * sim.config.pixels_per_unit;
    const float radiusSq = radiusPx * radiusPx;
    std::vector<EnemyUnit *> affected;
    affected.reserve(enemies.size());
    for (EnemyUnit &enemy : enemies)
    {
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        if (lengthSq(enemy.pos - yuna.pos) <= radiusSq)
        {
            affected.push_back(&enemy);
        }
    }
    if (affected.empty())
    {
        return;
    }
    const float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
    job.cooldown = std::max(0.0f, sim.config.shieldJob.cooldown * intervalMul);
    job.endlag = std::max(job.endlag, sim.config.jobCommon.endlagSeconds);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    if (dist(sim.rng) < sim.config.jobCommon.fizzleChance)
    {
        sim.pushTelemetry("Shield taunt fizzled");
        return;
    }
    const float duration = sim.config.shieldJob.durationSeconds;
    job.shield.tauntTimer = duration;
    job.shield.selfSlowTimer = duration;
    for (EnemyUnit *enemy : affected)
    {
        enemy->tauntTarget = yuna.pos;
        enemy->tauntTimer = duration;
    }
    sim.pushTelemetry("Shield taunt");
}

} // namespace

void CombatSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    auto &yunas = context.yunaUnits;
    auto &enemies = context.enemyUnits;
    auto &walls = context.wallSegments;
    auto &gates = context.gates;

    const float defenseMultiplier =
        sim.formationAlignTimer > 0.0f ? std::max(sim.formationDefenseMul, 0.01f) : 1.0f;
    const float formationDamageScale = 1.0f / defenseMultiplier;

    const int configuredTileSize = sim.mapDefs.tile_size > 0 ? sim.mapDefs.tile_size : 16;
    const float cellSize = std::max(1.0f, static_cast<float>(configuredTileSize));
    m_grid.configure(sim.worldMin, sim.worldMax, cellSize);

    for (EnemyUnit &enemy : enemies)
    {
        bool taunted = enemy.tauntTimer > 0.0f;
        if (taunted)
        {
            enemy.tauntTimer = std::max(0.0f, enemy.tauntTimer - dt);
        }
        Vec2 target = taunted ? enemy.tauntTarget : sim.basePos;
        if (!taunted && enemy.type == EnemyArchetype::Wallbreaker)
        {
            const float preferRadius = sim.wallbreakerStats.preferWallRadiusPx;
            if (preferRadius > 0.0f)
            {
                const float preferRadiusSq = preferRadius * preferRadius;
                WallSegment *bestWall = nullptr;
                float bestDistSq = preferRadiusSq;
                for (WallSegment &wall : walls)
                {
                    if (wall.hp <= 0.0f)
                    {
                        continue;
                    }
                    const float distSq = lengthSq(wall.pos - enemy.pos);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        bestWall = &wall;
                    }
                }
                if (bestWall)
                {
                    target = bestWall->pos;
                }
            }
        }

        Vec2 dir = normalize(target - enemy.pos);
        float speedPx = enemy.speedPx;
        if (speedPx <= 0.0f)
        {
            const float speedUnits = enemy.type == EnemyArchetype::Wallbreaker ? sim.wallbreakerStats.speed_u_s
                                                                               : sim.slimeStats.speed_u_s;
            speedPx = speedUnits * sim.config.pixels_per_unit;
        }
        enemy.pos += dir * (speedPx * dt);
    }

    m_grid.clear();

    auto ensureMarkerSize = [](std::vector<std::uint32_t> &marker, std::size_t size) {
        if (marker.size() < size)
        {
            marker.resize(size, 0);
        }
    };

    ensureMarkerSize(m_wallVisit, walls.size());
    ensureMarkerSize(m_enemyVisit, enemies.size());

    if (!walls.empty())
    {
        for (std::size_t i = 0; i < walls.size(); ++i)
        {
            m_grid.insertWall(i, walls[i].pos, walls[i].radius);
        }
    }

    auto nextStamp = [](std::uint32_t &stamp, std::vector<std::uint32_t> &markers) {
        ++stamp;
        if (stamp == 0)
        {
            std::fill(markers.begin(), markers.end(), 0);
            ++stamp;
        }
    };

    auto gatherWallsNear = [&](const Vec2 &pos, float radius, std::vector<std::size_t> &out) {
        out.clear();
        if (walls.empty())
        {
            return;
        }
        m_grid.queryCells(pos, radius, m_cellScratch);
        nextStamp(m_wallStamp, m_wallVisit);
        for (std::size_t cellIndex : m_cellScratch)
        {
            const auto &cell = m_grid.cell(cellIndex);
            for (std::size_t idx : cell.walls)
            {
                if (idx >= walls.size())
                {
                    continue;
                }
                if (m_wallVisit[idx] != m_wallStamp)
                {
                    m_wallVisit[idx] = m_wallStamp;
                    out.push_back(idx);
                }
            }
        }
    };

    for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex)
    {
        EnemyUnit &enemy = enemies[enemyIndex];
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        gatherWallsNear(enemy.pos, enemy.radius, m_wallScratch);
        for (std::size_t wallIndex : m_wallScratch)
        {
            if (wallIndex >= walls.size())
            {
                continue;
            }
            WallSegment &wall = walls[wallIndex];
            if (wall.hp <= 0.0f)
            {
                continue;
            }
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

    if (!enemies.empty())
    {
        for (std::size_t i = 0; i < enemies.size(); ++i)
        {
            m_grid.insertEnemy(i, enemies[i].pos, enemies[i].radius);
        }
    }

    if (!yunas.empty())
    {
        for (std::size_t i = 0; i < yunas.size(); ++i)
        {
            m_grid.insertUnit(i, yunas[i].pos, yunas[i].radius);
        }
    }

    auto gatherEnemiesNear = [&](const Vec2 &pos, float radius, std::vector<std::size_t> &out) {
        out.clear();
        if (enemies.empty())
        {
            return;
        }
        m_grid.queryCells(pos, radius, m_cellScratch);
        nextStamp(m_enemyStamp, m_enemyVisit);
        for (std::size_t cellIndex : m_cellScratch)
        {
            const auto &cell = m_grid.cell(cellIndex);
            for (std::size_t idx : cell.enemies)
            {
                if (idx >= enemies.size())
                {
                    continue;
                }
                if (m_enemyVisit[idx] != m_enemyStamp)
                {
                    m_enemyVisit[idx] = m_enemyStamp;
                    out.push_back(idx);
                }
            }
        }
    };

    FrameAllocator::Allocator<float> damageAlloc(context.frameAllocator);
    std::vector<float, FrameAllocator::Allocator<float>> yunaDamage(yunas.size(), 0.0f, damageAlloc);
    float commanderDamage = 0.0f;

    if (commander.alive)
    {
        gatherEnemiesNear(commander.pos, commander.radius, m_enemyScratch);
        for (std::size_t enemyIndex : m_enemyScratch)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float combined = commander.radius + enemy.radius;
            if (lengthSq(commander.pos - enemy.pos) <= combined * combined)
            {
                enemy.hp -= sim.commanderStats.dps * dt;
                if (context.commanderInvulnTimer <= 0.0f)
                {
                    commanderDamage += enemy.dpsUnit * dt * formationDamageScale;
                }
            }
        }
        for (GateRuntime &gate : gates)
        {
            if (gate.destroyed)
            {
                continue;
            }
            const float combined = commander.radius + gate.radius;
            if (lengthSq(commander.pos - gate.pos) <= combined * combined)
            {
                gate.hp = std::max(0.0f, gate.hp - sim.commanderStats.dps * dt);
                if (gate.hp <= 0.0f)
                {
                    sim.destroyGate(gate);
                }
            }
        }
    }

    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        Unit &yuna = yunas[i];
        if (yuna.job.job == UnitJob::Shield)
        {
            triggerShieldTaunt(yuna, sim, enemies);
        }
        gatherEnemiesNear(yuna.pos, yuna.radius, m_enemyScratch);
        for (std::size_t enemyIndex : m_enemyScratch)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float combined = yuna.radius + enemy.radius;
            if (lengthSq(yuna.pos - enemy.pos) <= combined * combined)
            {
                const float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
                float attackDps =
                    (sim.yunaStats.dps * std::max(0.01f, yuna.moraleAccuracyMultiplier)) / intervalMul;
                float burstDamage = 0.0f;
                if (yuna.job.job == UnitJob::Warrior)
                {
                    burstDamage = triggerWarriorSwing(yuna, sim);
                }
                if (yuna.job.job == UnitJob::Archer)
                {
                    triggerArcherFocus(yuna, sim);
                    if (yuna.job.archer.focusReady)
                    {
                        attackDps *= 1.0f + sim.config.archerJob.critBonus;
                        yuna.job.archer.focusReady = false;
                    }
                }

                enemy.hp -= attackDps * dt;
                if (burstDamage > 0.0f)
                {
                    enemy.hp -= burstDamage;
                }
                float incoming = enemy.dpsUnit * dt * formationDamageScale;
                incoming /= std::max(0.01f, yuna.moraleDefenseMultiplier);
                yunaDamage[i] += incoming;
            }
        }
        for (GateRuntime &gate : gates)
        {
            if (gate.destroyed)
            {
                continue;
            }
            const float combined = yuna.radius + gate.radius;
            if (lengthSq(yuna.pos - gate.pos) <= combined * combined)
            {
                const float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
                const float attackDps =
                    (sim.yunaStats.dps * std::max(0.01f, yuna.moraleAccuracyMultiplier)) / intervalMul;
                gate.hp = std::max(0.0f, gate.hp - attackDps * dt);
                if (gate.hp <= 0.0f)
                {
                    sim.destroyGate(gate);
                }
            }
        }
    }

    if (commander.alive && commanderDamage > 0.0f)
    {
        const float hpBefore = commander.hp;
        commander.hp -= commanderDamage;
        if (commander.hp <= 0.0f)
        {
            const float overkill = std::max(0.0f, commanderDamage - std::max(hpBefore, 0.0f));
            const float ratio = sim.clampOverkillRatio(overkill, sim.commanderStats.hp);
            sim.scheduleCommanderRespawn(1.0f, 0.0f, ratio);
        }
    }

    if (!yunaDamage.empty())
    {
        FrameAllocator::Allocator<Unit> survivorAlloc(context.frameAllocator);
        std::vector<Unit, FrameAllocator::Allocator<Unit>> survivors(survivorAlloc);
        survivors.reserve(yunas.size());
        for (std::size_t i = 0; i < yunas.size(); ++i)
        {
            Unit &yuna = yunas[i];
            if (yuna.hp <= 0.0f)
            {
                sim.enqueueYunaRespawn(0.0f);
                continue;
            }
            if (yunaDamage[i] > 0.0f)
            {
                const float hpBefore = yuna.hp;
                yuna.hp -= yunaDamage[i];
                if (yuna.hp <= 0.0f)
                {
                    const float overkill = std::max(0.0f, yunaDamage[i] - std::max(hpBefore, 0.0f));
                    const float ratio = sim.clampOverkillRatio(overkill, sim.yunaStats.hp);
                    sim.enqueueYunaRespawn(ratio);
                    continue;
                }
                if (yuna.temperament.definition && yuna.temperament.definition->panicOnHit > 0.0f)
                {
                    yuna.temperament.panicTimer = std::max(
                        yuna.temperament.panicTimer, yuna.temperament.definition->panicOnHit);
                }
            }
            survivors.push_back(yuna);
        }
        yunas.swap(survivors);
    }

    const float baseRadius = std::max(sim.config.base_aabb.x, sim.config.base_aabb.y) * 0.5f;
    gatherEnemiesNear(sim.basePos, baseRadius, m_enemyScratch);
    for (std::size_t enemyIndex : m_enemyScratch)
    {
        EnemyUnit &enemy = enemies[enemyIndex];
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        const float combined = baseRadius + enemy.radius;
        if (lengthSq(enemy.pos - sim.basePos) <= combined * combined)
        {
            context.baseHp -= enemy.dpsBase * dt;
            if (context.baseHp <= 0.0f)
            {
                context.baseHp = 0.0f;
                if (!context.mission.hasMission || context.mission.fail.baseHpZero)
                {
                    sim.setResult(GameResult::Defeat, "Defeat");
                }
                break;
            }
        }
    }

    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const EnemyUnit &e) {
        return e.hp <= 0.0f;
    }), enemies.end());

    walls.erase(std::remove_if(walls.begin(), walls.end(), [](const WallSegment &wall) {
        return wall.hp <= 0.0f;
    }), walls.end());

    context.requestComponentSync();
}

} // namespace world::systems

