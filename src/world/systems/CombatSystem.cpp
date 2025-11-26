#include "world/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
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

    StageRuntimeState &stageState = sim.stageState();
    const bool stageEnabled = stageState.enabled;
    const bool stageHasAllyBases = stageEnabled && !stageState.allyBases.empty();
    const bool stageHasEnemyBases = stageEnabled && !stageState.enemyBases.empty();
    auto insideAllyAura = [&](const Vec2 &pos) -> bool {
        if (!sim.stage.enabled)
        {
            return false;
        }
        for (const StageAllyBaseState &base : sim.stage.allyBases)
        {
            if (base.destroyed)
            {
                continue;
            }
            const float radius = base.auraRadiusPx > 0.0f ? base.auraRadiusPx : 128.0f;
            if (lengthSq(pos - base.pos) <= radius * radius)
            {
                return true;
            }
        }
        return false;
    };
    auto nearestActiveAllyBase = [&](const Vec2 &pos, Vec2 &outPos) -> bool {
        if (!stageHasAllyBases)
        {
            return false;
        }
        bool found = false;
        float bestDist = std::numeric_limits<float>::max();
        for (const StageAllyBaseState &base : stageState.allyBases)
        {
            if (base.destroyed)
            {
                continue;
            }
            const float dist = lengthSq(base.pos - pos);
            if (!found || dist < bestDist)
            {
                bestDist = dist;
                outPos = base.pos;
                found = true;
            }
        }
        return found;
    };

    auto enemyBaseRadius = [&](const StageEnemyBaseState &base) -> float {
        if (base.radiusPx > 0.0f)
        {
            return base.radiusPx;
        }
        const float baseRadiusUnits = std::max(sim.config.base_aabb.x, sim.config.base_aabb.y) * 0.5f;
        return std::max(baseRadiusUnits, 48.0f);
    };

    const float defenseMultiplier =
        sim.formationAlignTimer > 0.0f ? std::max(sim.formationDefenseMul, 0.01f) : 1.0f;
    const float formationDamageScale = 1.0f / defenseMultiplier;

    const int configuredTileSize = sim.mapDefs.tile_size > 0 ? sim.mapDefs.tile_size : 16;
    const float cellSize = std::max(1.0f, static_cast<float>(configuredTileSize));
    m_grid.configure(sim.worldMin, sim.worldMax, cellSize);

    std::vector<const Unit *> panicTargets;
    panicTargets.reserve(yunas.size());
    for (const Unit &yuna : yunas)
    {
        const bool rawPanic = yuna.forcePanic || yuna.temperament.panicTimer > 0.0f;
        if (!rawPanic || yuna.panicTokkouActive)
        {
            continue;
        }
        panicTargets.push_back(&yuna);
    }

    auto pickPanicTarget = [&](const Vec2 &origin, float radius) -> const Unit * {
        if (panicTargets.empty() || radius <= 0.0f)
        {
            return nullptr;
        }
        const float radiusSq = radius * radius;
        const Unit *best = nullptr;
        float bestDist = radiusSq;
        for (const Unit *candidate : panicTargets)
        {
            const float distSq = lengthSq(candidate->pos - origin);
            if (distSq < bestDist)
            {
                bestDist = distSq;
                best = candidate;
            }
        }
        return best;
    };

    auto pickNearestUnit = [&](const Vec2 &origin) -> const Unit * {
        const Unit *best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        for (const Unit &yuna : yunas)
        {
            const float distSq = lengthSq(yuna.pos - origin);
            if (distSq < bestDist)
            {
                bestDist = distSq;
                best = &yuna;
            }
        }
        return best;
    };
    auto focusedUnit = [&](const EnemyUnit &enemy) -> const Unit * {
        if (enemy.focusUnitIndex < 0 || enemy.focusUnitIndex >= static_cast<int>(yunas.size()))
        {
            return nullptr;
        }
        const Unit &candidate = yunas[static_cast<std::size_t>(enemy.focusUnitIndex)];
        if (candidate.hp <= 0.0f)
        {
            return nullptr;
        }
        return &candidate;
    };

    auto panicRadiusForType = [&](EnemyArchetype type) -> float {
        switch (type)
        {
        case EnemyArchetype::Goblin: return sim.config.pixels_per_unit * 35.0f;
        case EnemyArchetype::Magician: return sim.config.pixels_per_unit * 25.0f;
        case EnemyArchetype::Bat: return sim.config.pixels_per_unit * 20.0f;
        default: return 0.0f;
        }
    };

    for (EnemyUnit &enemy : enemies)
    {
        bool taunted = enemy.tauntTimer > 0.0f;
        if (taunted)
        {
            enemy.tauntTimer = std::max(0.0f, enemy.tauntTimer - dt);
        }
        else if (enemy.focusTimer > 0.0f)
        {
            enemy.focusTimer = std::max(0.0f, enemy.focusTimer - dt);
            if (enemy.focusTimer <= 0.0f)
            {
                enemy.focusUnitIndex = -1;
            }
        }
        else
        {
            enemy.focusUnitIndex = -1;
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
        if (!taunted)
        {
            switch (enemy.type)
            {
            case EnemyArchetype::Goblin:
            {
                const float chaseRadius = panicRadiusForType(enemy.type);
                const Unit *panic = pickPanicTarget(enemy.pos, chaseRadius);
                if (!panic)
                {
                    panic = focusedUnit(enemy);
                }
                if (panic)
                {
                    target = panic->pos;
                    if (!yunas.empty())
                    {
                        const Unit *begin = yunas.data();
                        const Unit *end = begin + yunas.size();
                        if (panic >= begin && panic < end)
                        {
                            enemy.focusUnitIndex = static_cast<int>(panic - begin);
                            enemy.focusTimer = std::max(enemy.focusTimer, 1.5f);
                        }
                    }
                }
                else if (const Unit *nearest = pickNearestUnit(enemy.pos))
                {
                    target = nearest->pos;
                    enemy.focusUnitIndex = -1;
                }
                break;
            }
            case EnemyArchetype::Magician:
            {
                const float chaseRadius = panicRadiusForType(enemy.type);
                if (const Unit *panic = pickPanicTarget(enemy.pos, chaseRadius))
                {
                    target = panic->pos;
                }
                else if (const Unit *nearest = pickNearestUnit(enemy.pos))
                {
                    target = nearest->pos;
                }
                else
                {
                    Vec2 basePos;
                    if (nearestActiveAllyBase(enemy.pos, basePos))
                    {
                        target = basePos;
                    }
                }
                break;
            }
            case EnemyArchetype::Bat:
            {
                if (const Unit *nearest = pickNearestUnit(enemy.pos))
                {
                    target = nearest->pos;
                }
                else if (const Unit *panic = pickPanicTarget(enemy.pos, panicRadiusForType(enemy.type)))
                {
                    target = panic->pos;
                }
                break;
            }
            case EnemyArchetype::Toritori:
            {
                Vec2 basePos = sim.basePos;
                Vec2 preferred;
                if (nearestActiveAllyBase(enemy.pos, preferred))
                {
                    basePos = preferred;
                }
                target = basePos;
                break;
            }
            default:
            {
                if (enemy.type != EnemyArchetype::Wallbreaker)
                {
                    if (const Unit *panic = pickPanicTarget(enemy.pos, panicRadiusForType(enemy.type)))
                    {
                        target = panic->pos;
                    }
                }
                break;
            }
            }
        }

        Vec2 delta = target - enemy.pos;
        float targetDistance = length(delta);
        Vec2 dir{0.0f, 0.0f};
        if (targetDistance > 0.0001f)
        {
            dir = delta / targetDistance;
        }
        if (!taunted && enemy.type == EnemyArchetype::Magician)
        {
            const float attackRange = std::max(enemy.attackRangePx, enemy.radius);
            const float keepNear = attackRange * 0.65f;
            const float keepFar = attackRange * 0.95f;
            if (targetDistance < keepNear && targetDistance > 0.0001f)
            {
                Vec2 away = enemy.pos - target;
                const float len = length(away);
                dir = len > 0.0001f ? away / len : Vec2{0.0f, 0.0f};
            }
            else if (targetDistance > attackRange && targetDistance > 0.0001f)
            {
                dir = delta / targetDistance;
            }
            else if (targetDistance > keepFar && targetDistance > 0.0001f)
            {
                dir = delta / targetDistance;
            }
            else if (targetDistance <= keepFar && targetDistance >= keepNear)
            {
                dir = {0.0f, 0.0f};
            }
        }
        float speedPx = enemy.speedPx;
        if (speedPx <= 0.0f)
        {
            const float speedUnits = enemy.type == EnemyArchetype::Wallbreaker ? sim.wallbreakerStats.speed_u_s
                                                                               : sim.slimeStats.speed_u_s;
            speedPx = speedUnits * sim.config.pixels_per_unit;
        }
        if (enemy.type == EnemyArchetype::Bat)
        {
            const float rushThreshold = 96.0f;
            const float dashMul = targetDistance > rushThreshold ? 1.45f : 1.15f;
            speedPx *= dashMul;
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
            const float queryRadius = std::max(enemies[i].radius, enemies[i].attackRangePx);
            m_grid.insertEnemy(i, enemies[i].pos, queryRadius);
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
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx);
            const float combined = commander.radius + enemyReach;
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
        if (stageHasEnemyBases)
        {
            for (StageEnemyBaseState &base : stageState.enemyBases)
            {
                if (base.sealed)
                {
                    continue;
                }
                const float combined = commander.radius + enemyBaseRadius(base);
                if (lengthSq(commander.pos - base.pos) <= combined * combined)
                {
                    sim.damageEnemyBase(base, sim.commanderStats.dps * dt);
                }
            }
        }
    }

    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        Unit &yuna = yunas[i];
        const bool panicState = yuna.forcePanic || yuna.temperament.panicTimer > 0.0f;
        const bool panicActive = panicState && !yuna.panicTokkouActive;
        const bool insideAura = insideAllyAura(yuna.pos);
        const float auraAttackMul =
            insideAura ? sim.chibiPersonalityConfig.auraAttackMultiplier : 1.0f;
        const float auraDamageMul =
            insideAura ? sim.chibiPersonalityConfig.auraDamageMultiplier : 1.0f;
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
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx);
            const float combined = yuna.radius + enemyReach;
            if (lengthSq(yuna.pos - enemy.pos) <= combined * combined)
            {
                if (!panicActive && !yuna.panicClingActive)
                {
                    float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
                    if (yuna.panicTokkouActive)
                    {
                        intervalMul *= std::max(0.01f, sim.chibiPersonalityConfig.tokkou.cooldownMultiplier);
                    }
                    float attackDps =
                        (sim.yunaStats.dps * std::max(0.01f, yuna.moraleAccuracyMultiplier)) / intervalMul;
                    attackDps *= auraAttackMul;
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
                    if (yuna.panicTokkouActive)
                    {
                        attackDps *= sim.chibiPersonalityConfig.tokkou.attackMultiplier;
                    }

                    enemy.hp -= attackDps * dt;
                    if (burstDamage > 0.0f)
                    {
                        enemy.hp -= burstDamage;
                    }
                }
                float incoming = enemy.dpsUnit * dt * formationDamageScale;
                incoming /= std::max(0.01f, yuna.moraleDefenseMultiplier);
                float incomingMul = 1.0f;
                if (yuna.panicTokkouActive)
                {
                    incomingMul *= sim.chibiPersonalityConfig.tokkou.takenMultiplier;
                }
                else if (yuna.panicKaitenActive && insideAura)
                {
                    incomingMul *= sim.chibiPersonalityConfig.kaiten.damageMultiplier;
                }
                else if (yuna.panicRunActive)
                {
                    incomingMul *= sim.chibiPersonalityConfig.runAround.damageMultiplier;
                }
                incomingMul *= auraDamageMul;
                incoming *= incomingMul;
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
                if (!panicActive && !yuna.panicClingActive)
                {
                    float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
                    if (yuna.panicTokkouActive)
                    {
                        intervalMul *= std::max(0.01f, sim.chibiPersonalityConfig.tokkou.cooldownMultiplier);
                    }
                    float attackDps =
                        (sim.yunaStats.dps * std::max(0.01f, yuna.moraleAccuracyMultiplier)) / intervalMul;
                    attackDps *= auraAttackMul;
                    if (yuna.panicTokkouActive)
                    {
                        attackDps *= sim.chibiPersonalityConfig.tokkou.attackMultiplier;
                    }
                    gate.hp = std::max(0.0f, gate.hp - attackDps * dt);
                    if (gate.hp <= 0.0f)
                    {
                        sim.destroyGate(gate);
                    }
                }
            }
        }
        if (stageHasEnemyBases && !panicActive && !yuna.panicClingActive)
        {
            for (StageEnemyBaseState &base : stageState.enemyBases)
            {
                if (base.sealed)
                {
                    continue;
                }
                const float combined = yuna.radius + enemyBaseRadius(base);
                if (lengthSq(yuna.pos - base.pos) <= combined * combined)
                {
                    float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
                    if (yuna.panicTokkouActive)
                    {
                        intervalMul *=
                            std::max(0.01f, sim.chibiPersonalityConfig.tokkou.cooldownMultiplier);
                    }
                    float attackDps =
                        (sim.yunaStats.dps * std::max(0.01f, yuna.moraleAccuracyMultiplier)) /
                        intervalMul;
                    attackDps *= auraAttackMul;
                    if (yuna.panicTokkouActive)
                    {
                        attackDps *= sim.chibiPersonalityConfig.tokkou.attackMultiplier;
                    }
                    sim.damageEnemyBase(base, attackDps * dt);
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
        std::vector<Unit> survivors;
        survivors.reserve(yunas.size());
        for (std::size_t i = 0; i < yunas.size(); ++i)
        {
            Unit &yuna = yunas[i];
            if (yuna.hp <= 0.0f)
            {
                sim.deathFx.push_back({yuna.pos, 1.0f, 1.0f});
                sim.enqueueYunaRespawn(0.0f);
                continue;
            }
            if (sim.chibiPersonalityConfig.auraRegenPerSecond > 0.0f && insideAllyAura(yuna.pos))
            {
                yuna.hp = std::min(yuna.maxHp,
                                   yuna.hp + sim.chibiPersonalityConfig.auraRegenPerSecond * dt);
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
                    sim.deathFx.push_back({yuna.pos, 1.0f, 1.0f});
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

    if (stageHasAllyBases || stageHasEnemyBases)
    {
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }

            if (stageHasAllyBases)
            {
                StageAllyBaseState *targetBase = nullptr;
                for (StageAllyBaseState &base : stageState.allyBases)
                {
                    if (base.destroyed)
                    {
                        continue;
                    }
                    const float enemyReach = std::max(enemy.radius, enemy.attackRangePx);
                    const float auraEffective =
                        base.auraRadiusPx > 0.0f ? std::max(base.auraRadiusPx * 0.25f, baseRadius) : baseRadius;
                    const float radius = std::max(baseRadius, auraEffective);
                    const float combined = radius + enemyReach;
                    if (lengthSq(enemy.pos - base.pos) <= combined * combined)
                    {
                        targetBase = &base;
                        break;
                    }
                }
                if (targetBase)
                {
                    sim.damageAllyBase(*targetBase, enemy.dpsBase * dt);
                }
            }

            // Enemy bases are damaged by allies elsewhere.
        }
    }
    else
    {
        gatherEnemiesNear(sim.basePos, baseRadius, m_enemyScratch);
        for (std::size_t enemyIndex : m_enemyScratch)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx);
            const float combined = baseRadius + enemyReach;
            if (lengthSq(enemy.pos - sim.basePos) <= combined * combined)
            {
                const float mitigation = std::clamp(sim.baseDamageReduction, 0.0f, 0.95f);
                const float damage = enemy.dpsBase * dt * (1.0f - mitigation);
                context.baseHp -= damage;
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
    }

    for (const EnemyUnit &enemy : enemies)
    {
        if (enemy.hp <= 0.0f)
        {
            sim.handleEnemyDefeated(enemy);
        }
    }

    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const EnemyUnit &e) {
        return e.hp <= 0.0f;
    }), enemies.end());

    walls.erase(std::remove_if(walls.begin(), walls.end(), [](const WallSegment &wall) {
        return wall.hp <= 0.0f;
    }), walls.end());

    if (!sim.deathFx.empty())
    {
        std::vector<LegacySimulation::DeathFx> next;
        next.reserve(sim.deathFx.size());
        for (auto fx : sim.deathFx)
        {
            fx.timer -= dt;
            if (fx.timer > 0.0f)
            {
                next.push_back(fx);
            }
        }
        sim.deathFx.swap(next);
    }

    context.requestComponentSync();
}

} // namespace world::systems
