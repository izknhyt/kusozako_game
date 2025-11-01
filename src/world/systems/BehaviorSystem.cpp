#include "world/systems/BehaviorSystem.h"

#include "core/Vec2.h"
#include "world/LegacySimulation.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <random>
#include <vector>
#include <type_traits>

namespace world::systems
{

namespace
{

constexpr float kMoraleIgnoreDecisionInterval = 0.6f;

template <typename Container>
Vec2 computeTemperamentVelocity(LegacySimulation &sim,
                                Unit &yuna,
                                float dt,
                                float baseSpeed,
                                const std::function<EnemyUnit *(const Vec2 &)> &nearestEnemy,
                                const std::function<EnemyUnit *(const Vec2 &)> &nearestEnemyUnbounded,
                                const Container &raidTargets)
{
    static_assert(std::is_same_v<typename Container::value_type, Vec2>,
                  "raidTargets container must hold Vec2 values");
    TemperamentState &state = yuna.temperament;
    if (!state.definition)
    {
        return {0.0f, 0.0f};
    }
    const TemperamentDefinition &def = *state.definition;

    const float moraleMultiplier = std::max(0.01f, yuna.moraleSpeedMultiplier);
    const float effectiveBaseSpeed = baseSpeed * moraleMultiplier;

    TemperamentConfig &temperamentConfig = sim.temperamentConfig;
    CommanderUnit &commander = sim.commander;
    const Vec2 &basePos = sim.basePos;

    if (def.behavior == TemperamentBehavior::Mimic)
    {
        if (state.mimicActive)
        {
            state.mimicDuration -= dt;
            if (state.mimicDuration <= 0.0f)
            {
                state.mimicActive = false;
                state.currentBehavior = def.mimicDefault;
                state.mimicCooldown = sim.randomRange(def.mimicEvery);
            }
        }
        if (!state.mimicActive)
        {
            if (state.mimicCooldown > 0.0f)
            {
                state.mimicCooldown = std::max(0.0f, state.mimicCooldown - dt);
            }
            if (state.mimicCooldown <= 0.0f && !def.mimicPool.empty())
            {
                std::uniform_int_distribution<std::size_t> pick(0, def.mimicPool.size() - 1);
                state.mimicBehavior = def.mimicPool[pick(sim.rng)];
                state.currentBehavior = state.mimicBehavior;
                state.mimicActive = true;
                state.mimicDuration = sim.randomRange(def.mimicDuration);
                if (state.mimicDuration <= 0.0f)
                {
                    state.mimicDuration = def.mimicDuration.max > 0.0f ? def.mimicDuration.max : 1.0f;
                }
            }
            else if (!state.mimicActive)
            {
                state.currentBehavior = def.mimicDefault;
            }
        }
    }
    else
    {
        state.currentBehavior = def.behavior;
    }

    if (state.lastBehavior != state.currentBehavior)
    {
        if (state.currentBehavior == TemperamentBehavior::ChargeNearest)
        {
            state.chargeDashTimer = temperamentConfig.chargeDash.duration;
        }
        if (state.currentBehavior == TemperamentBehavior::Wander ||
            state.currentBehavior == TemperamentBehavior::Homebound ||
            state.currentBehavior == TemperamentBehavior::GuardBase)
        {
            state.wanderDirection = sim.randomUnitVector();
            state.wanderTimer = sim.randomRange(temperamentConfig.wanderTurnInterval);
        }
        state.lastBehavior = state.currentBehavior;
    }

    float dashTime = state.chargeDashTimer;
    if (state.chargeDashTimer > 0.0f)
    {
        state.chargeDashTimer = std::max(0.0f, state.chargeDashTimer - dt);
    }
    float catchupTime = state.catchupTimer;
    if (state.catchupTimer > 0.0f)
    {
        state.catchupTimer = std::max(0.0f, state.catchupTimer - dt);
    }
    bool panicking = state.panicTimer > 0.0f;
    if (state.panicTimer > 0.0f)
    {
        state.panicTimer = std::max(0.0f, state.panicTimer - dt);
    }

    const bool dozing = state.currentBehavior == TemperamentBehavior::Doze;
    if (dozing)
    {
        if (state.sleeping)
        {
            state.sleepRemaining = std::max(0.0f, state.sleepRemaining - dt);
            if (state.sleepRemaining <= 0.0f)
            {
                state.sleeping = false;
                state.sleepTimer = sim.randomRange(temperamentConfig.sleepEvery);
                state.sleepRemaining = temperamentConfig.sleepDuration;
            }
        }
        else
        {
            state.sleepTimer = std::max(0.0f, state.sleepTimer - dt);
            if (state.sleepTimer <= 0.0f)
            {
                state.sleeping = true;
                state.sleepRemaining = temperamentConfig.sleepDuration;
            }
        }
    }
    else
    {
        state.sleeping = false;
    }

    if (def.cryPauseEvery.max > 0.0f)
    {
        if (state.crying)
        {
            state.cryPauseTimer = std::max(0.0f, state.cryPauseTimer - dt);
            if (state.cryPauseTimer <= 0.0f)
            {
                state.crying = false;
                state.cryTimer = sim.randomRange(def.cryPauseEvery);
            }
        }
        else
        {
            state.cryTimer = std::max(0.0f, state.cryTimer - dt);
            if (state.cryTimer <= 0.0f)
            {
                state.crying = true;
                state.cryPauseTimer = def.cryPauseDuration > 0.0f ? def.cryPauseDuration : 0.1f;
            }
        }
    }
    else
    {
        state.crying = false;
    }

    if (state.sleeping || state.crying)
    {
        return {0.0f, 0.0f};
    }

    if (panicking)
    {
        if (EnemyUnit *threat = nearestEnemy(yuna.pos))
        {
            Vec2 dir = normalize(yuna.pos - threat->pos);
            if (lengthSq(dir) > 0.0f)
            {
                return dir * effectiveBaseSpeed;
            }
        }
    }

    float speed = effectiveBaseSpeed;
    auto ensureWander = [&]() {
        if (state.wanderTimer <= 0.0f || lengthSq(state.wanderDirection) < 0.0001f)
        {
            state.wanderDirection = sim.randomUnitVector();
            state.wanderTimer = sim.randomRange(temperamentConfig.wanderTurnInterval);
        }
    };
    if (state.wanderTimer > 0.0f)
    {
        state.wanderTimer = std::max(0.0f, state.wanderTimer - dt);
    }

    switch (state.currentBehavior)
    {
    case TemperamentBehavior::ChargeNearest:
    {
        if (EnemyUnit *target = nearestEnemyUnbounded(yuna.pos))
        {
            Vec2 dir = normalize(target->pos - yuna.pos);
            if (dashTime > 0.0f)
            {
                speed *= temperamentConfig.chargeDash.multiplier;
            }
            return dir * speed;
        }
        Vec2 fallback = basePos;
        float closest = std::numeric_limits<float>::max();
        for (const Vec2 &candidate : raidTargets)
        {
            const float distSq = lengthSq(candidate - yuna.pos);
            if (distSq < closest)
            {
                closest = distSq;
                fallback = candidate;
            }
        }
        Vec2 dir = normalize(fallback - yuna.pos);
        if (lengthSq(dir) < 0.0001f)
        {
            dir = normalize(basePos - yuna.pos);
        }
        return dir * speed;
    }
    case TemperamentBehavior::FleeNearest:
    {
        if (EnemyUnit *threat = nearestEnemy(yuna.pos))
        {
            const float fear = temperamentConfig.fearRadius;
            if (fear <= 0.0f || lengthSq(threat->pos - yuna.pos) <= fear * fear)
            {
                Vec2 dir = normalize(yuna.pos - threat->pos);
                if (lengthSq(dir) > 0.0f)
                {
                    return dir * speed;
                }
            }
        }
        Vec2 dir = normalize(basePos - yuna.pos);
        return dir * speed;
    }
    case TemperamentBehavior::FollowYuna:
    {
        Vec2 target = commander.alive ? commander.pos : basePos;
        Vec2 toTarget = target - yuna.pos;
        const float distSq = lengthSq(toTarget);
        if (commander.alive &&
            distSq > temperamentConfig.followCatchup.distance * temperamentConfig.followCatchup.distance)
        {
            if (state.catchupTimer <= 0.0f)
            {
                state.catchupTimer = temperamentConfig.followCatchup.duration;
            }
            catchupTime = std::max(catchupTime, state.catchupTimer);
        }
        if (catchupTime > 0.0f || state.catchupTimer > 0.0f)
        {
            speed *= temperamentConfig.followCatchup.multiplier;
        }
        if (distSq > 1.0f)
        {
            return normalize(toTarget) * speed;
        }
        return Vec2{0.0f, 0.0f};
    }
    case TemperamentBehavior::RaidGate:
    {
        Vec2 target = basePos;
        float best = std::numeric_limits<float>::max();
        for (const Vec2 &candidate : raidTargets)
        {
            const float distSq = lengthSq(candidate - yuna.pos);
            if (distSq < best)
            {
                best = distSq;
                target = candidate;
            }
        }
        if (best == std::numeric_limits<float>::max())
        {
            if (EnemyUnit *enemy = nearestEnemy(yuna.pos))
            {
                target = enemy->pos;
            }
        }
        Vec2 dir = normalize(target - yuna.pos);
        return dir * speed;
    }
    case TemperamentBehavior::Homebound:
    {
        const float homeRadius = def.homeRadius > 0.0f ? def.homeRadius : 48.0f;
        const float avoidRadius = def.avoidEnemyRadius > 0.0f ? def.avoidEnemyRadius : homeRadius * 2.0f;
        Vec2 toBase = basePos - yuna.pos;
        if (lengthSq(toBase) > homeRadius * homeRadius)
        {
            return normalize(toBase) * speed;
        }
        if (EnemyUnit *threat = nearestEnemy(basePos))
        {
            if (lengthSq(threat->pos - basePos) <= avoidRadius * avoidRadius)
            {
                Vec2 away = basePos - threat->pos;
                if (lengthSq(away) > 0.0f)
                {
                    Vec2 target = basePos + normalize(away) * std::max(homeRadius, 8.0f);
                    return normalize(target - yuna.pos) * speed;
                }
            }
        }
        ensureWander();
        Vec2 wander = normalize(state.wanderDirection);
        Vec2 desired = normalize(wander * homeRadius + toBase * 0.3f);
        if (lengthSq(desired) < 0.0001f)
        {
            desired = normalize(toBase);
        }
        return desired * speed;
    }
    case TemperamentBehavior::Wander:
    case TemperamentBehavior::Doze:
    {
        ensureWander();
        Vec2 dir = normalize(state.wanderDirection);
        return dir * speed;
    }
    case TemperamentBehavior::GuardBase:
    {
        const float guardRadius = sim.mapDefs.tile_size * 22.0f;
        if (EnemyUnit *target = nearestEnemy(basePos))
        {
            if (lengthSq(target->pos - basePos) <= guardRadius * guardRadius)
            {
                return normalize(target->pos - yuna.pos) * speed;
            }
        }
        ensureWander();
        Vec2 dir = normalize(state.wanderDirection);
        Vec2 guardTarget{basePos.x + dir.x * 120.0f, basePos.y + dir.y * 80.0f};
        return normalize(guardTarget - yuna.pos) * speed;
    }
    case TemperamentBehavior::TargetTag:
    {
        if (EnemyUnit *target = sim.findTargetByTags(yuna.pos, def.targetTags))
        {
            return normalize(target->pos - yuna.pos) * speed;
        }
        if (EnemyUnit *enemy = nearestEnemy(yuna.pos))
        {
            return normalize(enemy->pos - yuna.pos) * speed;
        }
        Vec2 dir = normalize(basePos - yuna.pos);
        return dir * speed;
    }
    case TemperamentBehavior::Mimic:
    {
        ensureWander();
        Vec2 dir = normalize(state.wanderDirection);
        return dir * speed;
    }
    }
    return {0.0f, 0.0f};
}

} // namespace

void BehaviorSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    auto &yunas = context.yunaUnits;
    auto &enemies = context.enemyUnits;

    const float yunaSpeedPx = sim.yunaStats.speed_u_s * sim.config.pixels_per_unit;
    const float followerSnapDistSq = 16.0f;

    const std::size_t totalFollowers =
        std::count_if(yunas.begin(), yunas.end(), [](const Unit &unit) { return unit.effectiveFollower; });
    const std::size_t totalDefenders = yunas.size() > totalFollowers ? yunas.size() - totalFollowers : 0;
    const std::size_t safeDefenders = totalDefenders > 0 ? totalDefenders : 1;
    std::size_t defendIndex = 0;
    std::size_t supportIndex = 0;

    Unit *detectionUnit = nullptr;
    const float baseDetectionRadius = std::max(sim.config.morale.detectionRadius, 0.0f);

    auto nearestEnemy = [&](const Vec2 &pos) -> EnemyUnit * {
        EnemyUnit *best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        float limitSq = -1.0f;
        bool applyLimit = false;
        if (detectionUnit && baseDetectionRadius > 0.0f)
        {
            const float mul = std::max(0.0f, detectionUnit->moraleDetectionRadiusMultiplier);
            if (mul > 0.0f)
            {
                const float radius = baseDetectionRadius * mul;
                limitSq = radius * radius;
                applyLimit = lengthSq(pos - detectionUnit->pos) <= limitSq + 0.0001f;
            }
            else
            {
                limitSq = 0.0f;
                applyLimit = true;
            }
        }
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float dist = lengthSq(enemy.pos - pos);
            if (applyLimit)
            {
                const float unitDistSq = lengthSq(enemy.pos - detectionUnit->pos);
                if (unitDistSq > limitSq + 0.0001f)
                {
                    continue;
                }
            }
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &enemy;
            }
        }
        return best;
    };

    auto nearestEnemyUnbounded = [&](const Vec2 &pos) -> EnemyUnit * {
        EnemyUnit *best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float dist = lengthSq(enemy.pos - pos);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &enemy;
            }
        }
        return best;
    };

    FrameAllocator::Allocator<Vec2> raidAlloc(context.frameAllocator);
    std::vector<Vec2, FrameAllocator::Allocator<Vec2>> raidTargets(raidAlloc);
    sim.collectRaidTargets(raidTargets);

    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        Unit &yuna = yunas[i];
        yuna.desiredVelocity = {0.0f, 0.0f};
        yuna.hasDesiredVelocity = false;
        const float unitSpeed = yunaSpeedPx * std::max(0.01f, yuna.moraleSpeedMultiplier);
        const bool immobilized =
            yuna.job.endlag > 0.0f || yuna.job.warrior.stumbleTimer > 0.0f || yuna.job.archer.holdTimer > 0.0f;
        float jobSpeedMultiplier = 1.0f;
        if (yuna.job.shield.selfSlowTimer > 0.0f)
        {
            jobSpeedMultiplier *= std::clamp(sim.config.shieldJob.selfSlowMultiplier, 0.0f, 1.0f);
        }
        const float retreatSpeedMultiplier = yuna.moraleRetreatActive
                                                 ? std::max(yuna.moraleRetreatSpeedMultiplier, 0.0f)
                                                 : 1.0f;
        float effectiveSpeed = immobilized ? 0.0f : unitSpeed * jobSpeedMultiplier * retreatSpeedMultiplier;
        detectionUnit = &yuna;
        Vec2 temperamentVelocity =
            computeTemperamentVelocity(sim, yuna, dt, yunaSpeedPx, nearestEnemy, nearestEnemyUnbounded, raidTargets);
        Vec2 velocity{0.0f, 0.0f};
        const bool panicActive = yuna.temperament.panicTimer > 0.0f;

        const bool retreatActive = yuna.moraleRetreatActive;

        const float effectiveIgnoreChance =
            std::clamp(yuna.moraleIgnoreOrdersChance - yuna.moraleCommandObeyBonus, 0.0f, 1.0f);
        const float decisionInterval =
            kMoraleIgnoreDecisionInterval * std::max(0.01f, yuna.moraleRetargetCooldownMultiplier);
        if (effectiveIgnoreChance > 0.0f)
        {
            if (yuna.moraleIgnoreOrdersTimer <= 0.0f)
            {
                std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                yuna.moraleIgnoringOrders = roll(sim.rng) < effectiveIgnoreChance;
                yuna.moraleIgnoreOrdersTimer = decisionInterval;
            }
            else
            {
                yuna.moraleIgnoreOrdersTimer = std::max(0.0f, yuna.moraleIgnoreOrdersTimer - dt);
            }
        }
        else
        {
            yuna.moraleIgnoringOrders = false;
            yuna.moraleIgnoreOrdersTimer = 0.0f;
        }

        if (retreatActive)
        {
            Vec2 retreatDir{0.0f, 0.0f};
            Vec2 away{0.0f, 0.0f};
            if (EnemyUnit *threat = nearestEnemy(yuna.pos))
            {
                away = normalize(yuna.pos - threat->pos);
            }
            Vec2 toBase = normalize(sim.basePos - yuna.pos);
            const float bias = std::clamp(yuna.moraleRetreatHomewardBias, 0.0f, 1.0f);
            if (lengthSq(away) > 0.0f && bias < 1.0f)
            {
                retreatDir += away * (1.0f - bias);
            }
            if (lengthSq(toBase) > 0.0f && bias > 0.0f)
            {
                retreatDir += toBase * bias;
            }
            if (lengthSq(retreatDir) < 0.0001f)
            {
                retreatDir = lengthSq(toBase) > 0.0f ? toBase : away;
            }
            if (lengthSq(retreatDir) > 0.0f)
            {
                velocity = normalize(retreatDir) * unitSpeed;
            }
        }
        else if (panicActive)
        {
            velocity = temperamentVelocity;
        }
        else if (yuna.effectiveFollower && commander.alive)
        {
            Vec2 desiredPos = commander.pos + yuna.formationOffset;
            Vec2 toTarget = desiredPos - yuna.pos;
            if (lengthSq(toTarget) > followerSnapDistSq)
            {
                velocity = normalize(toTarget) * unitSpeed;
            }
            else
            {
                velocity = temperamentVelocity;
            }
        }
        else if (context.orderActive && !yuna.moraleIgnoringOrders)
        {
            switch (sim.stance)
            {
            case ArmyStance::RushNearest:
            {
                if (EnemyUnit *target = nearestEnemy(yuna.pos))
                {
                    velocity = normalize(target->pos - yuna.pos) * unitSpeed;
                }
                else
                {
                    velocity = temperamentVelocity;
                }
                break;
            }
            case ArmyStance::PushForward:
            {
                Vec2 target = sim.basePos;
                target.x += 512.0f;
                velocity = normalize(target - yuna.pos) * unitSpeed;
                break;
            }
            case ArmyStance::FollowLeader:
            {
                velocity = temperamentVelocity;
                break;
            }
            case ArmyStance::DefendBase:
            {
                if (EnemyUnit *target = nearestEnemy(sim.basePos))
                {
                    const float dist = lengthSq(target->pos - sim.basePos);
                    if (dist > 0.0f)
                    {
                        const float ratio = static_cast<float>(supportIndex) / static_cast<float>(safeDefenders);
                        const float angle = ratio * 2.0f * 3.14159265358979323846f;
                        Vec2 ringTarget{sim.basePos.x + std::cos(angle) * 72.0f,
                                        sim.basePos.y + std::sin(angle) * 48.0f};
                        ++supportIndex;
                        velocity = normalize(ringTarget - yuna.pos) * unitSpeed;
                    }
                    else
                    {
                        velocity = normalize(target->pos - yuna.pos) * unitSpeed;
                    }
                }
                else
                {
                    const float angle = 2.0f * 3.14159265358979323846f *
                                        (static_cast<float>(defendIndex) / static_cast<float>(safeDefenders));
                    Vec2 ringTarget{sim.basePos.x + std::cos(angle) * 120.0f,
                                    sim.basePos.y + std::sin(angle) * 80.0f};
                    ++defendIndex;
                    velocity = normalize(ringTarget - yuna.pos) * unitSpeed;
                }
                break;
            }
            }
        }
        else
        {
            velocity = temperamentVelocity;
        }

        if (velocity.x != 0.0f || velocity.y != 0.0f)
        {
            if (effectiveSpeed <= 0.0f)
            {
                velocity = {0.0f, 0.0f};
            }
            else
            {
                const float currentSpeed = std::sqrt(lengthSq(velocity));
                if (currentSpeed > 0.0001f)
                {
                    const float targetSpeed = std::min(effectiveSpeed, currentSpeed);
                    const float scale = targetSpeed / currentSpeed;
                    velocity = velocity * scale;
                }
            }
        }

        if (velocity.x != 0.0f || velocity.y != 0.0f)
        {
            yuna.desiredVelocity = velocity;
            yuna.hasDesiredVelocity = true;
        }

        detectionUnit = nullptr;
    }
}

} // namespace world::systems
