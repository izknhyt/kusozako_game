#include "world/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <random>
#include <utility>
#include <vector>
#include <iostream>

namespace world::systems
{

namespace
{

// Lock units in place for almost a full swing; tuned heavier per feedback.
constexpr float kCommanderAttackLockSeconds = 1.10f;
constexpr float kDefaultChibiAttackLockSeconds = 1.10f;

float chibiAttackLockSeconds(const Unit &yuna)
{
    float seconds = kDefaultChibiAttackLockSeconds;
    if (yuna.panicTokkouActive)
    {
        seconds *= 0.85f; // Slightly shorter while Tokkou is active
    }
    return seconds;
}

float enemyAttackLockSeconds(const EnemyUnit &enemy)
{
    switch (enemy.type)
    {
    case EnemyArchetype::Magician: return 0.70f;
    case EnemyArchetype::Bat: return 0.70f;
    case EnemyArchetype::Golem: return 1.40f;
    case EnemyArchetype::Boss: return 1.20f;
    default: return 1.00f; // Slime/Goblin/Toritori/Wallbreaker
    }
}

float triggerWarriorSwing(Unit &yuna, LegacySimulation &sim, float baseDps)
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
        return baseDps / intervalMul;
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
    // Debug: track unexpected ally base count drops at runtime
    static std::size_t s_lastAllyBaseCount = 0;
    if (stageEnabled && stageState.allyBases.size() != s_lastAllyBaseCount)
    {
        std::cerr << "[stage] allyBases size changed at runtime: " << s_lastAllyBaseCount << " -> "
                  << stageState.allyBases.size() << '\n';
        s_lastAllyBaseCount = stageState.allyBases.size();
    }
    bool stageHasAllyBasesAlive = false;
    if (stageHasAllyBases)
    {
        for (const StageAllyBaseState &base : stageState.allyBases)
        {
            if (!base.destroyed)
            {
                stageHasAllyBasesAlive = true;
                break;
            }
        }
    }
    const bool mainBaseAlive = stageHasAllyBasesAlive || context.baseHp > 0.0f;
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

    auto applyDragonBreath = [&](EnemyUnit &boss) {
        if (boss.breathCooldown <= 0.0f)
        {
            boss.breathCooldown = 10.0f; // safety fallback
        }
        boss.breathTimer = std::max(0.0f, boss.breathTimer - dt);
        if (boss.breathTimer > 0.0f || boss.breathCooldown <= 0.0f)
        {
            return;
        }
        // decide facing toward nearest living ally (including commander)
        Vec2 targetPos = boss.pos;
        float bestDist = std::numeric_limits<float>::max();
        if (commander.alive)
        {
            bestDist = lengthSq(commander.pos - boss.pos);
            targetPos = commander.pos;
        }
        for (const Unit &ally : yunas)
        {
            if (ally.hp <= 0.0f)
            {
                continue;
            }
            const float distSq = lengthSq(ally.pos - boss.pos);
            if (distSq < bestDist)
            {
                bestDist = distSq;
                targetPos = ally.pos;
            }
        }
        auto normalizedDirectionSafe = [](const Vec2 &from, const Vec2 &to) -> Vec2 {
            Vec2 delta = to - from;
            const float len = length(delta);
            return len > 0.0001f ? delta / len : Vec2{0.0f, 0.0f};
        };

        Vec2 forward = normalizedDirectionSafe(boss.pos, targetPos);
        if (lengthSq(forward) <= 0.0f)
        {
            forward = {1.0f, 0.0f};
        }
        const float range = 216.0f;
        const float cosHalf = 0.5f; // 120deg cone => 60deg half-angle, cos60 = 0.5
        auto inCone = [&](const Vec2 &pos, float radius) -> bool {
            Vec2 to = pos - boss.pos;
            const float dist = length(to);
            if (dist <= 0.0f || dist - radius > range)
            {
                return false;
            }
            Vec2 dir = to / std::max(dist, 0.0001f);
            const float c = dot(forward, dir);
            return c >= cosHalf;
        };

        const float breathDamage = 18.0f;
        // Commander
        if (commander.alive && inCone(commander.pos, commander.radius))
        {
            commander.hp = std::max(0.0f, commander.hp - breathDamage);
        }
        // Bases (ally)
        const float baseRadius = std::max(sim.config.base_aabb.x, sim.config.base_aabb.y) * 0.5f;
        for (StageAllyBaseState &base : stageState.allyBases)
        {
            if (base.destroyed)
            {
                continue;
            }
            const float radius = base.auraRadiusPx > 0.0f ? base.auraRadiusPx : baseRadius;
            if (inCone(base.pos, radius))
            {
                sim.damageAllyBase(base, breathDamage);
            }
        }
        // Allies
        for (Unit &yuna : yunas)
        {
            if (yuna.hp <= 0.0f)
            {
                continue;
            }
            if (inCone(yuna.pos, yuna.radius))
            {
                yuna.hp = std::max(0.0f, yuna.hp - breathDamage);
            }
        }

        // Visual effect
        LegacySimulation::Effect fx;
        fx.pos = boss.pos;
        fx.radius = range;
        fx.r = 255.0f;
        fx.g = 90.0f;
        fx.b = 50.0f;
        fx.a = 160.0f;
        fx.ttl = 1.5f;
        fx.cone = true;
        fx.dir = forward;
        fx.length = range;
        fx.nearWidth = 40.0f;
        fx.farWidth = range * 1.2f;
        sim.effects.push_back(fx);
        sim.pushTelemetry("Dragon breath");

        boss.breathTimer = boss.breathCooldown;
    };

    // Dragon breath processing (boss only)
    for (EnemyUnit &enemy : enemies)
    {
        if (enemy.hp <= 0.0f || enemy.type != EnemyArchetype::Boss)
        {
            continue;
        }
        applyDragonBreath(enemy);
    }

    float maxEnemyReach = 0.0f;
    for (const EnemyUnit &enemy : enemies)
    {
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        const float reach = std::max(enemy.radius, enemy.attackRangePx + 6.0f);
        if (reach > maxEnemyReach)
        {
            maxEnemyReach = reach;
        }
    }

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

    struct AllyCandidate
    {
        Vec2 pos{0.0f, 0.0f};
        int unitIndex = -1; // -1 = commander
        bool isCommander = false;
    };

    auto pickNearestAlly = [&](const Vec2 &origin) -> std::optional<AllyCandidate> {
        AllyCandidate best{};
        float bestDist = std::numeric_limits<float>::max();
        if (commander.alive)
        {
            bestDist = lengthSq(commander.pos - origin);
            best.pos = commander.pos;
            best.unitIndex = -1;
            best.isCommander = true;
        }
        for (std::size_t i = 0; i < yunas.size(); ++i)
        {
            const Unit &yuna = yunas[i];
            if (yuna.hp <= 0.0f)
            {
                continue;
            }
            const float distSq = lengthSq(yuna.pos - origin);
            if (distSq < bestDist)
            {
                bestDist = distSq;
                best.pos = yuna.pos;
                best.unitIndex = static_cast<int>(i);
                best.isCommander = false;
            }
        }
        if (bestDist < std::numeric_limits<float>::max())
        {
            return best;
        }
        return std::nullopt;
    };
    auto unitIndex = [&](const Unit *u) -> int {
        if (!u)
        {
            return -1;
        }
        if (u >= yunas.data() && u < yunas.data() + yunas.size())
        {
            return static_cast<int>(u - yunas.data());
        }
        return -1;
    };
    auto panicRadiusForType = [&](EnemyArchetype type) -> float {
        switch (type)
        {
        case EnemyArchetype::Goblin: return sim.config.pixels_per_unit * 35.0f;
        case EnemyArchetype::Magician: return sim.config.pixels_per_unit * 25.0f;
        case EnemyArchetype::Bat: return sim.config.pixels_per_unit * 20.0f;
        case EnemyArchetype::Golem: return sim.config.pixels_per_unit * 28.0f;
        default: return sim.config.pixels_per_unit * 30.0f;
        }
    };

    struct BaseThreat
    {
        Vec2 pos{0.0f, 0.0f};
        int unitIndex = -1; // -1 = commander
        bool isCommander = false;
    };

    auto pickThreatNearEnemyBase = [&](float extraRadius) -> std::optional<BaseThreat> {
        if (!stageHasEnemyBases)
        {
            return std::nullopt;
        }
        BaseThreat best{};
        float bestScore = std::numeric_limits<float>::max();
        for (const StageEnemyBaseState &base : stageState.enemyBases)
        {
            if (base.sealed)
            {
                continue;
            }
            const float defendRadius = enemyBaseRadius(base) + extraRadius;
            const float defendRadiusSq = defendRadius * defendRadius;
            auto consider = [&](const Vec2 &pos, int unitIdx, bool isCmd) {
                const float distSq = lengthSq(pos - base.pos);
                if (distSq > defendRadiusSq)
                {
                    return;
                }
                if (distSq < bestScore)
                {
                    bestScore = distSq;
                    best.pos = pos;
                    best.unitIndex = unitIdx;
                    best.isCommander = isCmd;
                }
            };
            if (commander.alive)
            {
                consider(commander.pos, -1, true);
            }
            for (std::size_t i = 0; i < yunas.size(); ++i)
            {
                const Unit &yuna = yunas[i];
                if (yuna.hp <= 0.0f)
                {
                    continue;
                }
                consider(yuna.pos, static_cast<int>(i), false);
            }
        }
        if (bestScore < std::numeric_limits<float>::max())
        {
            return best;
        }
        return std::nullopt;
    };

    // 拠点接触判定の中心と半径（当たり判定ベース。Auraは攻撃優先判定のみで使用）
    const float baseRadius = std::max(sim.config.base_aabb.x, sim.config.base_aabb.y) * 0.5f;
    Vec2 baseContactPos = sim.basePos;
    float mainBaseContactRadius = baseRadius + 4.0f;
    if (stageHasAllyBasesAlive)
    {
        float auraRadius = baseRadius;
        float bestDistSq = std::numeric_limits<float>::max();
        for (const StageAllyBaseState &base : stageState.allyBases)
        {
            if (!base.destroyed)
            {
                const float distSq = commander.alive ? lengthSq(base.pos - commander.pos) : 0.0f;
                if (!commander.alive || distSq < bestDistSq)
                {
                    bestDistSq = distSq;
                    baseContactPos = base.pos;
                }
                const float r = base.auraRadiusPx > 0.0f ? base.auraRadiusPx : 128.0f;
                auraRadius = std::max(auraRadius, r);
            }
        }
        // 攻撃可能判定は本体サイズ＋わずかなバッファのみ
        mainBaseContactRadius = baseRadius + 4.0f;
    }

    for (EnemyUnit &enemy : enemies)
    {
        if (enemy.attackLockTimer > 0.0f)
        {
            enemy.attackLockTimer = std::max(0.0f, enemy.attackLockTimer - dt);
        }
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
        Vec2 target = taunted ? enemy.tauntTarget : baseContactPos;
        if (!mainBaseAlive && !taunted)
        {
            if (commander.alive)
            {
                target = commander.pos;
            }
            else if (const auto nearest = pickNearestAlly(enemy.pos))
            {
                target = nearest->pos;
            }
        }
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
            const Unit *panicAny = pickPanicTarget(enemy.pos, panicRadiusForType(enemy.type));

            struct TargetChoice
            {
                Vec2 pos{};
                bool isBase = false;
                bool isAlly = false;
                int focusIndex = -1;
                float focusTimer = 0.0f;
            };

            TargetChoice choice{};
            choice.pos = target;
            choice.isBase = stageHasAllyBasesAlive;

            auto nearestAlly = pickNearestAlly(enemy.pos);
            const float nearestDistSq = nearestAlly ? lengthSq(nearestAlly->pos - enemy.pos)
                                                    : std::numeric_limits<float>::max();
            const bool forceBaseTarget = sim.debugForceBaseTarget && mainBaseAlive && !taunted;

            auto chooseFocus = [&](const AllyCandidate &ally, float timer) {
                choice.pos = ally.pos;
                choice.isBase = false;
                choice.isAlly = true;
                choice.focusIndex = ally.unitIndex;
                choice.focusTimer = timer;
            };
            auto chooseCommander = [&](float timer) {
                if (!commander.alive)
                {
                    return;
                }
                chooseFocus({commander.pos, -1, true}, timer);
            };
            auto chooseBase = [&]() {
                Vec2 basePos = baseContactPos;
                Vec2 preferred;
                if (stageHasAllyBasesAlive && nearestActiveAllyBase(enemy.pos, preferred))
                {
                    basePos = preferred;
                }
                else if (!stageHasAllyBasesAlive && mainBaseAlive)
                {
                    basePos = sim.basePos;
                }
                else if (!mainBaseAlive)
                {
                    if (commander.alive)
                    {
                        basePos = commander.pos;
                        choice.isAlly = true;
                        choice.focusIndex = -1;
                        choice.focusTimer = 0.0f;
                    }
                    else if (nearestAlly)
                    {
                        basePos = nearestAlly->pos;
                        choice.isAlly = true;
                        choice.focusIndex = nearestAlly->unitIndex;
                        choice.focusTimer = 1.5f;
                    }
                }
                choice.pos = basePos;
                choice.isBase = mainBaseAlive || stageHasAllyBasesAlive;
                if (choice.isBase)
                {
                    choice.isAlly = false;
                    choice.focusIndex = -1;
                    choice.focusTimer = 0.0f;
                }
            };

            switch (enemy.type)
            {
            case EnemyArchetype::Slime:
            {
                const float engageRadius = 64.0f;
                if (nearestAlly && nearestDistSq <= engageRadius * engageRadius)
                {
                    chooseFocus(*nearestAlly, 1.5f);
                }
                else if (panicAny)
                {
                    chooseFocus({panicAny->pos, unitIndex(panicAny), false}, 2.0f);
                }
                else
                {
                    chooseBase();
                }
                break;
            }
            case EnemyArchetype::Goblin:
            {
                if (panicAny)
                {
                    chooseFocus({panicAny->pos, unitIndex(panicAny), false}, 2.0f);
                }
                else if (nearestAlly)
                {
                    chooseFocus(*nearestAlly, 1.5f);
                }
                else
                {
                    chooseBase();
                }
                break;
            }
            case EnemyArchetype::Golem:
            {
                const float defendMargin = 300.0f;
                if (stageHasEnemyBases)
                {
                    if (auto threat = pickThreatNearEnemyBase(defendMargin))
                    {
                        // 優先: 自軍（敵側）拠点近くのユウナ/司令官を殴る
                        chooseFocus({threat->pos, threat->unitIndex, threat->isCommander}, 2.5f);
                        break;
                    }
                }
                if (stageHasAllyBasesAlive)
                {
                    chooseBase();
                    const float engageRadius = 96.0f;
                    if (nearestAlly && nearestDistSq <= engageRadius * engageRadius)
                    {
                        chooseFocus(*nearestAlly, 2.0f);
                    }
                }
                else if (commander.alive)
                {
                    chooseCommander(0.0f);
                }
                else if (nearestAlly)
                {
                    chooseFocus(*nearestAlly, 2.0f);
                }
                break;
            }
            case EnemyArchetype::Boss:
            {
                if (stageHasAllyBasesAlive)
                {
                    chooseBase();
                    const float engageRadius = 96.0f;
                    if (nearestAlly && nearestDistSq <= engageRadius * engageRadius)
                    {
                        chooseFocus(*nearestAlly, 2.5f);
                    }
                }
                else if (commander.alive)
                {
                    chooseCommander(0.0f);
                }
                else if (nearestAlly)
                {
                    chooseFocus(*nearestAlly, 2.5f);
                }
                break;
            }
            case EnemyArchetype::Magician:
            {
                const float engageRadius = 128.0f;
                if (nearestAlly && nearestDistSq <= engageRadius * engageRadius)
                {
                    chooseFocus(*nearestAlly, 1.5f);
                }
                else
                {
                    chooseBase();
                }
                break;
            }
            case EnemyArchetype::Bat:
            {
                const float engageRadius = 128.0f;
                if (nearestAlly && nearestDistSq <= engageRadius * engageRadius)
                {
                    chooseFocus(*nearestAlly, 1.5f);
                }
                else
                {
                    chooseBase();
                }
                break;
            }
            case EnemyArchetype::Toritori:
            {
                chooseBase();
                break;
            }
            case EnemyArchetype::Wallbreaker:
            {
                break;
            }
            default:
            {
                if (forceBaseTarget)
                {
                    chooseBase();
                }
                else if (panicAny && enemy.type != EnemyArchetype::Wallbreaker)
                {
                    chooseFocus({panicAny->pos, unitIndex(panicAny), false}, 2.0f);
                }
                else
                {
                    chooseBase();
                }
                break;
            }
            }

            if (forceBaseTarget)
            {
                chooseBase();
            }

            target = choice.pos;
            bool targetingBase = choice.isBase;
            if (choice.focusIndex >= 0)
            {
                enemy.focusUnitIndex = choice.focusIndex;
                enemy.focusTimer = choice.focusTimer;
            }
            else
            {
                enemy.focusUnitIndex = -1;
                enemy.focusTimer = choice.isAlly ? std::max(0.0f, choice.focusTimer) : 0.0f;
            }

            Vec2 targetFinal = target;
            // 拠点を狙っていない場合のみ、行軍ずらしを適用
            if (!taunted && enemy.focusUnitIndex < 0 && !targetingBase)
            {
                targetFinal += enemy.laneOffset;
            }
            Vec2 delta = targetFinal - enemy.pos;
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
            if (enemy.attackLockTimer <= 0.0f)
            {
                enemy.pos += dir * (speedPx * dt);
            }
        }
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
    ensureMarkerSize(m_unitVisit, yunas.size());

    // Per-frame contact scratch
    std::vector<std::vector<std::size_t>> enemyWallContacts(enemies.size());
    std::vector<std::vector<std::size_t>> enemyYunaContacts(enemies.size());
    std::vector<bool> enemyCommanderContact(enemies.size(), false);
    std::vector<std::vector<std::size_t>> enemyAllyBaseContacts(enemies.size());
    std::vector<bool> enemyMainBaseContact(enemies.size(), false);

    for (auto &v : enemyWallContacts)
    {
        v.clear();
    }
    for (auto &v : enemyYunaContacts)
    {
        v.clear();
    }
    for (auto &v : enemyAllyBaseContacts)
    {
        v.clear();
    }
    std::fill(enemyCommanderContact.begin(), enemyCommanderContact.end(), false);
    std::fill(enemyMainBaseContact.begin(), enemyMainBaseContact.end(), false);

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
                enemyWallContacts[enemyIndex].push_back(wallIndex);
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

    auto gatherUnitsNear = [&](const Vec2 &pos, float radius, std::vector<std::size_t> &out) {
        out.clear();
        if (yunas.empty())
        {
            return;
        }
        m_grid.queryCells(pos, radius, m_cellScratch);
        auto nextUnitStamp = [&]() {
            ++m_unitStamp;
            if (m_unitStamp == 0)
            {
                std::fill(m_unitVisit.begin(), m_unitVisit.end(), 0);
                ++m_unitStamp;
            }
        };
        nextUnitStamp();
        for (std::size_t cellIndex : m_cellScratch)
        {
            const auto &cell = m_grid.cell(cellIndex);
            for (std::size_t idx : cell.units)
            {
                if (idx >= yunas.size())
                {
                    continue;
                }
                if (m_unitVisit[idx] != m_unitStamp)
                {
                    m_unitVisit[idx] = m_unitStamp;
                    out.push_back(idx);
                }
            }
        }
    };

    // Enemy vs ally separation (except Bat/Toritori which can pass through)
    for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex)
    {
        EnemyUnit &enemy = enemies[enemyIndex];
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        if (enemy.type == EnemyArchetype::Bat || enemy.type == EnemyArchetype::Toritori)
        {
            continue;
        }
        const float queryRadius = enemy.radius + sim.yunaStats.radius + 4.0f;
        gatherUnitsNear(enemy.pos, queryRadius, m_unitScratch);
        for (std::size_t unitIndex : m_unitScratch)
        {
            if (unitIndex >= yunas.size())
            {
                continue;
            }
            const Unit &ally = yunas[unitIndex];
            if (ally.hp <= 0.0f)
            {
                continue;
            }
            const float combined = enemy.radius + ally.radius;
            const float distSq = lengthSq(enemy.pos - ally.pos);
            if (distSq <= combined * combined)
            {
                float dist = std::sqrt(std::max(distSq, 0.0001f));
                Vec2 normal = dist > 0.0f ? (enemy.pos - ally.pos) / dist : Vec2{1.0f, 0.0f};
                const float overlap = combined - dist;
                enemy.pos += normal * overlap * 0.65f; // bias push on enemy
            }
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
        std::vector<std::size_t> commanderEnemyContacts;
        commanderEnemyContacts.reserve(m_enemyScratch.size());
        std::vector<std::size_t> commanderGateContacts;
        commanderGateContacts.reserve(gates.size());
        std::vector<std::size_t> commanderBaseContacts;
        commanderBaseContacts.reserve(stageState.enemyBases.size());

        gatherEnemiesNear(commander.pos, commander.radius + maxEnemyReach, m_enemyScratch);
        for (std::size_t enemyIndex : m_enemyScratch)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx + 6.0f);
            const float combined = commander.radius + enemyReach;
            const bool inRange = lengthSq(commander.pos - enemy.pos) <= combined * combined;
            if (inRange)
            {
                commanderEnemyContacts.push_back(enemyIndex);
                enemyCommanderContact[enemyIndex] = true;
            }
        }

        for (std::size_t gateIndex = 0; gateIndex < gates.size(); ++gateIndex)
        {
            GateRuntime &gate = gates[gateIndex];
            if (gate.destroyed)
            {
                continue;
            }
            const float combined = commander.radius + gate.radius;
            if (lengthSq(commander.pos - gate.pos) <= combined * combined)
            {
                commanderGateContacts.push_back(gateIndex);
            }
        }

        if (stageHasEnemyBases)
        {
            for (std::size_t baseIndex = 0; baseIndex < stageState.enemyBases.size(); ++baseIndex)
            {
                StageEnemyBaseState &base = stageState.enemyBases[baseIndex];
                if (base.sealed)
                {
                    continue;
                }
                const float combined = commander.radius + enemyBaseRadius(base);
                if (lengthSq(commander.pos - base.pos) <= combined * combined)
                {
                    commanderBaseContacts.push_back(baseIndex);
                }
            }
        }

        const bool hasContact = !commanderEnemyContacts.empty() || !commanderGateContacts.empty() ||
                                !commanderBaseContacts.empty();
        if (hasContact)
        {
            commander.attackLockTimer =
                std::max(commander.attackLockTimer, kCommanderAttackLockSeconds);
            commander.attackSwingDuration = kCommanderAttackLockSeconds;
            commander.attackSwingTimer += dt;
            if (commander.attackSwingTimer >= commander.attackSwingDuration)
            {
                const float swingDamage = sim.commanderStats.dps * commander.attackSwingDuration;
                for (std::size_t enemyIndex : commanderEnemyContacts)
                {
                    if (enemyIndex < enemies.size())
                    {
                        enemies[enemyIndex].hp -= swingDamage;
                        enemies[enemyIndex].attackLockTimer =
                            std::max(enemies[enemyIndex].attackLockTimer,
                                     enemyAttackLockSeconds(enemies[enemyIndex]));
                    }
                }
                for (std::size_t gateIndex : commanderGateContacts)
                {
                    if (gateIndex < gates.size())
                    {
                        GateRuntime &gate = gates[gateIndex];
                        if (!gate.destroyed)
                        {
                            gate.hp = std::max(0.0f, gate.hp - swingDamage);
                            if (gate.hp <= 0.0f)
                            {
                                sim.destroyGate(gate);
                            }
                        }
                    }
                }
                for (std::size_t baseIndex : commanderBaseContacts)
                {
                    if (baseIndex < stageState.enemyBases.size())
                    {
                        StageEnemyBaseState &base = stageState.enemyBases[baseIndex];
                        if (!base.sealed)
                        {
                            sim.damageEnemyBase(base, swingDamage);
                        }
                    }
                }
                commander.attackSwingTimer -= commander.attackSwingDuration;
            }
        }
        else
        {
            commander.attackSwingTimer = 0.0f;
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
        if (yuna.job.job == UnitJob::Shield)
        {
            triggerShieldTaunt(yuna, sim, enemies);
        }
        const AllyLevelingParams levelParams = kCommanderLevelingParams;
        const int level = std::max(yuna.level, 1);
        const int levelDelta = std::max(level - 1, 0);
        const float perLevelDps =
            yuna.isNamed ? levelParams.dpsPerLevel : levelParams.dpsPerLevel * 0.5f;
        float baseDps = yuna.isNamed ? sim.commanderStats.dps : sim.yunaStats.dps;
        baseDps += perLevelDps * static_cast<float>(levelDelta);
        baseDps = std::max(baseDps, 0.0f);
        std::vector<std::size_t> enemyContacts;
        enemyContacts.reserve(m_enemyScratch.size());
        std::vector<std::size_t> gateContacts;
        gateContacts.reserve(gates.size());
        std::vector<std::size_t> baseContacts;
        baseContacts.reserve(stageState.enemyBases.size());

        gatherEnemiesNear(yuna.pos, yuna.radius + maxEnemyReach, m_enemyScratch);
        for (std::size_t enemyIndex : m_enemyScratch)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx);
            const float combined = yuna.radius + enemyReach;
            const bool inRange = lengthSq(yuna.pos - enemy.pos) <= combined * combined;
            if (inRange)
            {
                enemyContacts.push_back(enemyIndex);
                if (enemyIndex < enemyYunaContacts.size())
                {
                    enemyYunaContacts[enemyIndex].push_back(i);
                }
            }
        }

        for (std::size_t gateIndex = 0; gateIndex < gates.size(); ++gateIndex)
        {
            GateRuntime &gate = gates[gateIndex];
            if (gate.destroyed)
            {
                continue;
            }
            const float combined = yuna.radius + gate.radius;
            if (lengthSq(yuna.pos - gate.pos) <= combined * combined)
            {
                gateContacts.push_back(gateIndex);
            }
        }

        if (stageHasEnemyBases)
        {
            for (std::size_t baseIndex = 0; baseIndex < stageState.enemyBases.size(); ++baseIndex)
            {
                StageEnemyBaseState &base = stageState.enemyBases[baseIndex];
                if (base.sealed)
                {
                    continue;
                }
                const float combined = yuna.radius + enemyBaseRadius(base);
                if (lengthSq(yuna.pos - base.pos) <= combined * combined)
                {
                    baseContacts.push_back(baseIndex);
                }
            }
        }

        const bool canAttack = !panicActive && !yuna.panicClingActive;
        const bool hasContact =
            canAttack && (!enemyContacts.empty() || !gateContacts.empty() || !baseContacts.empty());

        if (hasContact)
        {
            yuna.attackLockTimer = std::max(yuna.attackLockTimer, chibiAttackLockSeconds(yuna));
            yuna.attackSwingDuration = chibiAttackLockSeconds(yuna);
            yuna.attackSwingTimer += dt;

            if (yuna.attackSwingTimer >= yuna.attackSwingDuration)
            {
                float intervalMul = std::max(0.01f, yuna.moraleAttackIntervalMultiplier);
                if (yuna.panicTokkouActive)
                {
                    intervalMul *= std::max(0.01f, sim.chibiPersonalityConfig.tokkou.cooldownMultiplier);
                }
                float attackDps =
                    (baseDps * std::max(0.01f, yuna.moraleAccuracyMultiplier)) / intervalMul;
                attackDps *= auraAttackMul;
                float burstDamage = 0.0f;
                if (yuna.job.job == UnitJob::Warrior)
                {
                    burstDamage = triggerWarriorSwing(yuna, sim, baseDps);
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

                const float attackDamage = attackDps * yuna.attackSwingDuration;

                for (std::size_t enemyIndex : enemyContacts)
                {
                    if (enemyIndex < enemies.size())
                    {
                        enemies[enemyIndex].hp -= attackDamage;
                        if (burstDamage > 0.0f)
                        {
                            enemies[enemyIndex].hp -= burstDamage;
                        }
                        enemies[enemyIndex].attackLockTimer =
                            std::max(enemies[enemyIndex].attackLockTimer,
                                     enemyAttackLockSeconds(enemies[enemyIndex]));
                    }
                }
                for (std::size_t gateIndex : gateContacts)
                {
                    if (gateIndex < gates.size())
                    {
                        GateRuntime &gate = gates[gateIndex];
                        if (!gate.destroyed)
                        {
                            gate.hp = std::max(0.0f, gate.hp - attackDamage);
                            if (gate.hp <= 0.0f)
                            {
                                sim.destroyGate(gate);
                            }
                        }
                    }
                }
                for (std::size_t baseIndex : baseContacts)
                {
                    if (baseIndex < stageState.enemyBases.size())
                    {
                        StageEnemyBaseState &base = stageState.enemyBases[baseIndex];
                        if (!base.sealed)
                        {
                            sim.damageEnemyBase(base, attackDamage);
                        }
                    }
                }

                yuna.attackSwingTimer -= yuna.attackSwingDuration;
            }
        }
        else
        {
            yuna.attackSwingTimer = 0.0f;
        }
    }

    // Enemy swings: apply damage at end of swing to any contacts gathered above.
    for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex)
    {
        EnemyUnit &enemy = enemies[enemyIndex];
        if (enemy.hp <= 0.0f)
        {
            continue;
        }

        const bool contactCommander =
            enemyIndex < enemyCommanderContact.size() && enemyCommanderContact[enemyIndex];
        const bool contactYunas =
            enemyIndex < enemyYunaContacts.size() && !enemyYunaContacts[enemyIndex].empty();
        const bool contactWalls =
            enemyIndex < enemyWallContacts.size() && !enemyWallContacts[enemyIndex].empty();
        const bool contactAllyBases =
            enemyIndex < enemyAllyBaseContacts.size() && !enemyAllyBaseContacts[enemyIndex].empty();
        const bool contactMainBase =
            enemyIndex < enemyMainBaseContact.size() && enemyMainBaseContact[enemyIndex];
        const float enemyReach = std::max(enemy.radius, enemy.attackRangePx + 6.0f);
        bool contactMainBaseFinal = contactMainBase;
        if (!contactMainBaseFinal && mainBaseAlive)
        {
            const float combinedMain = mainBaseContactRadius + enemyReach;
            const float distSq = lengthSq(enemy.pos - baseContactPos);
            if (distSq <= combinedMain * combinedMain || sim.debugForceBaseContact)
            {
                contactMainBaseFinal = true; // 予備判定で強制接触
            }
        }

        const bool hasContact =
            contactCommander || contactYunas || contactWalls || contactAllyBases || contactMainBaseFinal;
        if (!hasContact)
        {
            enemy.attackSwingTimer = 0.0f;
            continue;
        }

        enemy.attackLockTimer = std::max(enemy.attackLockTimer, enemyAttackLockSeconds(enemy));
        enemy.attackSwingDuration = enemyAttackLockSeconds(enemy);
        enemy.attackSwingTimer += dt;
        if (enemy.attackSwingTimer < enemy.attackSwingDuration)
        {
            continue;
        }

        const float swingDuration = enemy.attackSwingDuration;
        enemy.attackSwingTimer -= enemy.attackSwingDuration;

        if (contactCommander && commander.alive)
        {
            const bool commanderInAura = insideAllyAura(commander.pos);
            const float auraDamageMul =
                commanderInAura ? sim.chibiPersonalityConfig.auraDamageMultiplier : 1.0f;
            if (context.commanderInvulnTimer <= 0.0f)
            {
                commanderDamage += enemy.dpsUnit * swingDuration * formationDamageScale * auraDamageMul;
            }
        }

        if (contactYunas)
        {
            for (std::size_t yIndex : enemyYunaContacts[enemyIndex])
            {
                if (yIndex >= yunas.size())
                {
                    continue;
                }
                Unit &yuna = yunas[yIndex];
                if (yuna.hp <= 0.0f)
                {
                    continue;
                }
                const bool insideAura = insideAllyAura(yuna.pos);
                float incoming = enemy.dpsUnit * swingDuration * formationDamageScale;
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
                incomingMul *= insideAura ? sim.chibiPersonalityConfig.auraDamageMultiplier : 1.0f;
                incoming *= incomingMul;
                if (yIndex < yunaDamage.size())
                {
                    yunaDamage[yIndex] += incoming;
                }
            }
        }

        if (contactWalls)
        {
            for (std::size_t wallIndex : enemyWallContacts[enemyIndex])
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
                wall.hp -= enemy.dpsWall * swingDuration;
            }
        }

        if (contactAllyBases)
        {
            for (std::size_t baseIndex : enemyAllyBaseContacts[enemyIndex])
            {
                if (baseIndex >= stageState.allyBases.size())
                {
                    continue;
                }
                StageAllyBaseState &base = stageState.allyBases[baseIndex];
                if (!base.destroyed)
                {
                    sim.damageAllyBase(base, enemy.dpsBase * swingDuration);
                }
            }
        }

        if (contactMainBaseFinal)
        {
            const float mitigation = std::clamp(sim.baseDamageReduction, 0.0f, 0.5f);
            float damage = enemy.dpsBase * swingDuration * (1.0f - mitigation);
            if (damage > 0.0f && damage < 1.0f)
            {
                damage = 1.0f; // ensure minimum chip damage per swing
            }

            if (stageHasAllyBases)
            {
                // ダメージ対象として最寄りの未破壊拠点を選ぶ
                int targetIndex = -1;
                float bestDistSq = std::numeric_limits<float>::max();
                for (std::size_t baseIdx = 0; baseIdx < stageState.allyBases.size(); ++baseIdx)
                {
                    const StageAllyBaseState &base = stageState.allyBases[baseIdx];
                    if (base.destroyed)
                    {
                        continue;
                    }
                    const float distSq = lengthSq(enemy.pos - base.pos);
                    if (distSq < bestDistSq)
                    {
                        bestDistSq = distSq;
                        targetIndex = static_cast<int>(baseIdx);
                    }
                }
                if (targetIndex >= 0)
                {
                    StageAllyBaseState &base = stageState.allyBases[static_cast<std::size_t>(targetIndex)];
                    sim.damageAllyBase(base, damage);
                }
                else
                {
                    // 拠点リストはあるが全滅（または空）している場合はメイン拠点HPを削る
                    context.baseHp -= damage;
                    if (context.baseHp <= 0.0f)
                    {
                        context.baseHp = 0.0f;
                        if (!commander.alive &&
                            (!context.mission.hasMission || context.mission.fail.baseHpZero))
                        {
                            sim.setResult(GameResult::Defeat, "Defeat");
                        }
                    }
                }

                // 全拠点破壊チェック（司令官も倒れている場合のみ敗北）
                bool allDestroyed = stageState.allyBases.empty();
                for (const StageAllyBaseState &base : stageState.allyBases)
                {
                    if (!base.destroyed)
                    {
                        allDestroyed = false;
                        break;
                    }
                }
                // Defeat only if拠点が全滅かつ司令官が倒れている
                if (allDestroyed && !commander.alive &&
                    (!context.mission.hasMission || context.mission.fail.baseHpZero))
                {
                    sim.setResult(GameResult::Defeat, "Defeat");
                }
            }
            else
            {
                context.baseHp -= damage;
                if (context.baseHp <= 0.0f)
                {
                    context.baseHp = 0.0f;
                    // Defeat only if司令官が倒れている
                    if (!commander.alive && (!context.mission.hasMission || context.mission.fail.baseHpZero))
                    {
                        sim.setResult(GameResult::Defeat, "Defeat");
                    }
                }
            }
        }
    }

    const bool commanderInAura = commander.alive && insideAllyAura(commander.pos);
    if (commander.alive && commanderInAura && sim.chibiPersonalityConfig.auraRegenPerSecond > 0.0f)
    {
        commander.hp =
            std::min(sim.commanderStats.hp,
                     commander.hp + sim.chibiPersonalityConfig.auraRegenPerSecond * dt);
    }
    // Aura内ではMPリジェネを追加で1×分上乗せ（合計2×相当）
    if (commander.alive && commanderInAura && commander.mpRegen > 0.0f && commander.mpMax > 0.0f)
    {
        commander.mp = std::min(commander.mpMax, commander.mp + commander.mpRegen * dt);
    }

    if (commander.alive && commander.guardActive && commanderDamage > 0.0f)
    {
        const float cost = 1.0f + std::ceil(commanderDamage * 0.2f);
        if (commander.mp >= cost)
        {
            commander.mp = std::max(0.0f, commander.mp - cost);
            commanderDamage = 0.0f;
        }
        else
        {
            commander.guardActive = false;
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
                if (!yuna.isNamed)
                {
                    sim.deathFx.push_back({yuna.pos, 1.0f, 1.0f, yuna.facingX, false});
                    ++sim.statChibiDeaths;
                }
                if (yuna.respawnAllowed)
                {
                    sim.enqueueYunaRespawn(0.0f);
                }
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
                    if (yuna.respawnAllowed)
                    {
                        sim.enqueueYunaRespawn(ratio);
                    }
                    if (!yuna.isNamed)
                    {
                        sim.deathFx.push_back({yuna.pos, 1.0f, 1.0f, yuna.facingX, false});
                    }
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

    if (stageHasAllyBases || stageHasEnemyBases)
    {
        for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }

        if (stageHasAllyBases)
        {
            for (std::size_t baseIndex = 0; baseIndex < stageState.allyBases.size(); ++baseIndex)
            {
                StageAllyBaseState &base = stageState.allyBases[baseIndex];
                if (base.destroyed)
                    {
                        continue;
                    }
                    const float enemyReach = std::max(enemy.radius, enemy.attackRangePx + 6.0f);
                    const float baseHitRadius = baseRadius + 4.0f;
                    const float combined = baseHitRadius + enemyReach;
                    if (lengthSq(enemy.pos - base.pos) <= combined * combined)
                    {
                        if (enemyIndex < enemyAllyBaseContacts.size())
                        {
                            enemyAllyBaseContacts[enemyIndex].push_back(baseIndex);
                        }
                        if (enemyIndex < enemyMainBaseContact.size())
                        {
                            enemyMainBaseContact[enemyIndex] = true;
                        }
                    }
                }
            }

            // Always allow main base contact even when ally bases exist
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx + 6.0f);
            const float combinedMain = mainBaseContactRadius + enemyReach;
            if (lengthSq(enemy.pos - baseContactPos) <= combinedMain * combinedMain)
            {
                if (enemyIndex < enemyMainBaseContact.size())
                {
                    enemyMainBaseContact[enemyIndex] = true;
                }
            }

            // Enemy bases are damaged by allies elsewhere.
        }
    }
    else
    {
        // ステージ拠点リストが無い場合も、メイン拠点半径（オーラ基準）で判定する
        gatherEnemiesNear(baseContactPos, mainBaseContactRadius + maxEnemyReach, m_enemyScratch);
        for (std::size_t enemyIndex : m_enemyScratch)
        {
            EnemyUnit &enemy = enemies[enemyIndex];
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float enemyReach = std::max(enemy.radius, enemy.attackRangePx + 6.0f);
            const float combined = mainBaseContactRadius + enemyReach;
            if (lengthSq(enemy.pos - baseContactPos) <= combined * combined)
            {
                if (enemyIndex < enemyMainBaseContact.size())
                {
                    enemyMainBaseContact[enemyIndex] = true;
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

    // Invalidate commander target/focus if enemy went away
    auto clearCommanderTargetsIfInvalid = [&](int &index) {
        if (index < 0)
        {
            return;
        }
        if (static_cast<std::size_t>(index) >= enemies.size() || enemies[index].hp <= 0.0f)
        {
            index = -1;
        }
    };
    clearCommanderTargetsIfInvalid(commander.attackTargetIndex);
    clearCommanderTargetsIfInvalid(commander.focusTargetIndex);

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
