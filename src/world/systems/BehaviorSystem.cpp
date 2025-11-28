#include "world/systems/BehaviorSystem.h"

#include "core/Vec2.h"
#include "world/LegacySimulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

namespace world::systems
{
namespace
{
constexpr float kMoraleIgnoreDecisionInterval = 0.6f;
constexpr std::size_t kChibiActionCount = static_cast<std::size_t>(ChibiAction::Wander) + 1;

EnemyUnit *nearestEnemy(const Vec2 &pos, const std::vector<EnemyUnit> &enemies)
{
    EnemyUnit *best = nullptr;
    float bestDist = std::numeric_limits<float>::max();
    for (EnemyUnit &enemy : const_cast<std::vector<EnemyUnit> &>(enemies))
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
}

std::optional<Vec2> nearestEnemyBase(const LegacySimulation &sim, const Vec2 &pos)
{
    float bestDist = std::numeric_limits<float>::max();
    std::optional<Vec2> result;
    auto consider = [&](const Vec2 &target) {
        const float dist = lengthSq(target - pos);
        if (dist < bestDist)
        {
            bestDist = dist;
            result = target;
        }
    };
    if (sim.stage.enabled)
    {
        for (const StageEnemyBaseState &base : sim.stage.enemyBases)
        {
            if (base.sealed)
            {
                continue;
            }
            consider(base.pos);
        }
    }
    if (!result)
    {
        std::vector<Vec2> raidTargets = sim.collectRaidTargets();
        for (const Vec2 &target : raidTargets)
        {
            consider(target);
        }
    }
    return result;
}

const Unit *nearestAlly(const std::vector<Unit> &yunas, std::size_t index)
{
    if (yunas.empty())
    {
        return nullptr;
    }
    const Unit &self = yunas[index];
    const Unit *best = nullptr;
    float bestDist = std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        if (i == index)
        {
            continue;
        }
        const Unit &other = yunas[i];
        if (other.hp <= 0.0f)
        {
            continue;
        }
        const float dist = lengthSq(other.pos - self.pos);
        if (dist < bestDist)
        {
            bestDist = dist;
            best = &other;
        }
    }

    return best;
}

Vec2 normalizedDirection(const Vec2 &from, const Vec2 &to)
{
    const Vec2 delta = to - from;
    const float len = length(delta);
    if (len <= 0.0001f)
    {
        return {0.0f, 0.0f};
    }
    return delta / len;
}

Vec2 rotateVec(const Vec2 &v, float degrees)
{
    const float rad = degrees * (3.1415926535f / 180.0f);
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c};
}

Vec2 computeBoidAdjustment(const Unit &self,
                           const std::vector<Unit> &allies,
                           float cohesionStrength,
                           float separationStrength,
                           const ChibiAiParams &params)
{
    Vec2 cohesion{0.0f, 0.0f};
    Vec2 separation{0.0f, 0.0f};
    int cohesionCount = 0;
    const float cohesionRadiusSq = params.cohesionRadius * params.cohesionRadius;
    const float separationRadiusSq = params.separationRadius * params.separationRadius;

    for (const Unit &ally : allies)
    {
        if (&ally == &self || ally.hp <= 0.0f)
        {
            continue;
        }
        const Vec2 delta = ally.pos - self.pos;
        const float distSq = lengthSq(delta);
        if (distSq <= cohesionRadiusSq)
        {
            cohesion += ally.pos;
            ++cohesionCount;
        }
        if (distSq > 0.0001f && distSq <= separationRadiusSq)
        {
            separation = separation - delta / std::max(distSq, 1.0f);
        }
    }

    Vec2 adjustment{0.0f, 0.0f};
    if (cohesionCount > 0)
    {
        Vec2 avgPos = cohesion / static_cast<float>(cohesionCount);
        Vec2 dir = normalizedDirection(self.pos, avgPos);
        adjustment += dir * cohesionStrength;
    }
    if (lengthSq(separation) > 0.0f)
    {
        Vec2 dir = normalizedDirection({0.0f, 0.0f}, separation);
        adjustment += dir * separationStrength;
    }
    return adjustment;
}

std::size_t actionIndex(ChibiAction action)
{
    return std::clamp(static_cast<std::size_t>(action), std::size_t(0), kChibiActionCount - 1);
}

std::string formatRatio(float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value));
    return buffer;
}

float normalizedAxisValue(int axis)
{
    return std::clamp((static_cast<float>(axis) + 2.0f) * 0.25f, 0.0f, 1.0f);
}

float axisLerp(float low, float high, float t)
{
    return low + (high - low) * std::clamp(t, 0.0f, 1.0f);
}

struct Hazard
{
    Vec2 pos{0.0f, 0.0f};
    float radius = 0.0f;
    float weight = 1.0f;
};

Vec2 computeAoeAvoidance(const Vec2 &pos,
                         const std::vector<Hazard> &hazards,
                         const ChibiAiParams &params,
                         float multiplier)
{
    if (hazards.empty() || params.aoeAvoidStrength <= 0.0f)
    {
        return {0.0f, 0.0f};
    }
    Vec2 avoidance{0.0f, 0.0f};
    for (const Hazard &hazard : hazards)
    {
        const float radius = hazard.radius > 0.0f ? hazard.radius : params.aoeAvoidRadius;
        if (radius <= 0.0f)
        {
            continue;
        }
        Vec2 delta = pos - hazard.pos;
        const float distSq = lengthSq(delta);
        const float radiusSq = radius * radius;
        if (distSq >= radiusSq || distSq <= 0.0001f)
        {
            continue;
        }
        const float dist = std::sqrt(distSq);
        float weight = (radius - dist) / radius;
        if (weight <= 0.0f)
        {
            continue;
        }
        if (params.aoeAvoidFalloff > 0.0f)
        {
            weight = std::pow(weight, params.aoeAvoidFalloff);
        }
        Vec2 dir = delta / dist;
        avoidance += dir * weight * hazard.weight;
    }
    return avoidance * (params.aoeAvoidStrength * multiplier);
}

float computeCrowdRadius(const std::vector<Unit> &units, std::size_t limit)
{
    if (units.empty() || limit == 0)
    {
        return 0.0f;
    }
    const std::size_t sampleCount = std::min(limit, units.size());
    Vec2 overall{0.0f, 0.0f};
    for (const Unit &unit : units)
    {
        overall += unit.pos;
    }
    overall = overall / static_cast<float>(units.size());

    std::vector<std::size_t> indices(units.size());
    std::iota(indices.begin(), indices.end(), 0);
    std::nth_element(
        indices.begin(),
        indices.begin() + static_cast<std::ptrdiff_t>(sampleCount),
        indices.end(),
        [&units, &overall](std::size_t lhs, std::size_t rhs) {
            return lengthSq(units[lhs].pos - overall) < lengthSq(units[rhs].pos - overall);
        });

    Vec2 centroid{0.0f, 0.0f};
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        centroid += units[indices[i]].pos;
    }
    centroid = centroid / static_cast<float>(sampleCount);

    float maxDist = 0.0f;
    for (std::size_t i = 0; i < sampleCount; ++i)
    {
        const float dist = std::sqrt(lengthSq(units[indices[i]].pos - centroid));
        if (dist > maxDist)
        {
            maxDist = dist;
        }
    }
    return maxDist;
}

} // namespace

void BehaviorSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    auto &yunas = context.yunaUnits;
    auto &enemies = context.enemyUnits;

    // Decay short-lived visual effects
    auto &effects = sim.effects;
    effects.erase(std::remove_if(effects.begin(), effects.end(), [&](LegacySimulation::Effect &fx) {
                      fx.ttl -= dt;
                      return fx.ttl <= 0.0f;
                  }),
                  effects.end());

    sim.decayDynamicHazards(dt);

    const float decisionInterval = std::max(sim.chibiAiParams.tickSeconds, 0.1f);
    m_tickAccumulator += dt;
    if (m_tickAccumulator >= decisionInterval)
    {
        m_tickAccumulator = std::fmod(m_tickAccumulator, decisionInterval);
    }
    m_tokkouWarningTimer = std::max(0.0f, m_tokkouWarningTimer - dt);

    const float commanderSpeedPx = sim.commanderStats.speed_u_s * sim.config.pixels_per_unit;

    Unit *detectionUnit = nullptr;
    bool forceUnlimitedEnemySearch = false;
    const float baseDetectionRadius = std::max(sim.config.morale.detectionRadius, 0.0f);

    auto nearestEnemyLimited = [&](const Vec2 &pos) -> EnemyUnit * {
        EnemyUnit *best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        float limitSq = -1.0f;
        bool applyLimit = false;
        if (!forceUnlimitedEnemySearch && detectionUnit && baseDetectionRadius > 0.0f)
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

    FrameAllocator::Allocator<Vec2> raidAlloc(context.frameAllocator);
    std::vector<Vec2, FrameAllocator::Allocator<Vec2>> raidTargets(raidAlloc);
    sim.collectRaidTargets(raidTargets);
    std::vector<Hazard> hazards;
    hazards.reserve(4);
    const std::size_t allyCount = yunas.size();
    std::vector<int> tokkouUnitIds;
    std::vector<int> clingUnitIds;
    std::vector<int> kaitenUnitIds;
    std::vector<int> runUnitIds;
    struct PanicRecord
    {
        int index = -1;
        const char *state = "";
        float timer = 0.0f;
    };
    std::vector<PanicRecord> panicRecords;
    tokkouUnitIds.reserve(allyCount);
    clingUnitIds.reserve(allyCount);
    kaitenUnitIds.reserve(allyCount);
    runUnitIds.reserve(allyCount);
    panicRecords.reserve(allyCount);
    const EnemyUnit *focusTargetEnemy = nullptr;
    Vec2 focusTargetPos{0.0f, 0.0f};
    if (commander.focusTargetIndex >= 0 && commander.focusTargetIndex < static_cast<int>(enemies.size()))
    {
        const EnemyUnit &candidate = enemies[commander.focusTargetIndex];
        if (candidate.hp > 0.0f)
        {
            focusTargetEnemy = &candidate;
            focusTargetPos = candidate.pos;
        }
    }
    const ChibiTokkouConfig &tokkouCfg = sim.chibiPersonalityConfig.tokkou;
    const ChibiClingConfig &clingCfg = sim.chibiPersonalityConfig.cling;
    const ChibiRunAroundConfig &runCfg = sim.chibiPersonalityConfig.runAround;
    const int maxTokkou = std::max(tokkouCfg.maxSimultaneous, 0);
    int activeTokkou = 0;
    for (const LegacySimulation::DynamicHazard &hazard : sim.dynamicHazards)
    {
        hazards.push_back({hazard.pos, hazard.radius, hazard.weight});
    }

    for (const Unit &unit : yunas)
    {
        if (unit.panicTokkouActive)
        {
            ++activeTokkou;
        }
    }

    auto appendBossHazard = [&]() {
        if (!sim.boss.active || !sim.boss.inWindup || sim.boss.mechanic.radius <= 0.0f)
        {
            return;
        }
        for (EnemyUnit &enemy : enemies)
        {
            if (enemy.hp <= 0.0f || enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            hazards.push_back({enemy.pos, sim.boss.mechanic.radius, 1.0f});
            break;
        }
    };
    appendBossHazard();
    for (const StageHazardState &hazard : sim.stage.hazards)
    {
        if (hazard.active)
        {
            hazards.push_back({hazard.pos, hazard.radius, hazard.weight});
        }
    }
    sim.actionTelemetry.hazardCountAccum += static_cast<float>(hazards.size());
    ++sim.actionTelemetry.hazardSamples;

    auto releaseTokkou = [&](Unit &unit) {
        if (unit.panicTokkouActive)
        {
            unit.panicTokkouActive = false;
            unit.panicTokkouTimer = 0.0f;
            if (activeTokkou > 0)
            {
                --activeTokkou;
            }
        }
        if (unit.temperament.panicMode == PanicMode::Tokkou)
        {
            unit.temperament.panicMode = PanicMode::None;
        }
    };
    auto releaseCling = [](Unit &unit) {
        if (unit.panicClingActive)
        {
            unit.panicClingActive = false;
            unit.panicClingAuraTimer = 0.0f;
        }
        if (unit.temperament.panicMode == PanicMode::Cling)
        {
            unit.temperament.panicMode = PanicMode::None;
        }
    };

    std::array<int, kChibiActionCount> actionFrameCounts{};
    int panicFrameCount = 0;

    auto recordPanic = [&](int index, const char *state, float timer) {
        PanicRecord rec;
        rec.index = index;
        rec.state = state;
        rec.timer = timer;
        panicRecords.push_back(rec);
    };

    auto releaseKaiten = [](Unit &unit) {
        if (unit.panicKaitenActive)
        {
            unit.panicKaitenActive = false;
            unit.panicKaitenTimer = 0.0f;
            unit.panicKaitenGuardTimer = 0.0f;
            unit.panicKaitenGuardDirection = {1.0f, 0.0f};
        }
        if (unit.temperament.panicMode == PanicMode::Kaiten)
        {
            unit.temperament.panicMode = PanicMode::None;
        }
    };

    auto releaseRunAround = [](Unit &unit) {
        if (unit.panicRunActive)
        {
            unit.panicRunActive = false;
            unit.panicRunDashTimer = 0.0f;
            unit.panicRunJitterTimer = 0.0f;
            unit.panicRunRepathTimer = 0.0f;
            unit.panicRunAuraTimer = 0.0f;
            unit.panicRunDirection = {};
            unit.panicRunJitterDirection = {};
        }
        if (unit.temperament.panicMode == PanicMode::RunAround)
        {
            unit.temperament.panicMode = PanicMode::None;
        }
    };

    auto computeRunAroundDirection = [&](const Vec2 &pos) -> Vec2 {
        if (EnemyUnit *threat = nearestEnemyLimited(pos))
        {
            Vec2 dir = normalizedDirection(threat->pos, pos);
            if (lengthSq(dir) > 0.0f)
            {
                return dir;
            }
        }
        return sim.randomUnitVector();
    };

    auto isWithinAllyAura = [&](const Vec2 &pos) -> bool {
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

    auto computeClingTarget = [&](const Unit &, std::size_t unitIndex) -> Vec2 {
        if (!commander.alive)
        {
            return sim.basePos;
        }
        Vec2 threatDir{1.0f, 0.0f};
        if (EnemyUnit *threat = nearestEnemyLimited(commander.pos))
        {
            threatDir = normalizedDirection(commander.pos, threat->pos);
        }
        Vec2 behind = {-threatDir.x, -threatDir.y};
        if (lengthSq(behind) < 0.0001f)
        {
            behind = {0.0f, -1.0f};
        }
        const float radiusRange = std::max(0.0f, clingCfg.radiusMax - clingCfg.radiusMin);
        const float slot = static_cast<float>(unitIndex % 4) / 3.0f;
        const float radius = clingCfg.radiusMin + radiusRange * slot;
        const int lateralSlot = static_cast<int>(unitIndex % 3) - 1;
        const float maxAngle = std::max(0.0f, clingCfg.rearDegrees * 0.5f);
        const float angleStep = maxAngle > 0.0f ? maxAngle * 0.33f : 0.0f;
        Vec2 offset = rotateVec(behind, lateralSlot * angleStep);
        offset = normalize(offset);
        return commander.pos + offset * radius;
    };

    auto nearestAllyBaseInfo = [&](const Vec2 &pos) -> std::optional<std::pair<Vec2, float>> {
        if (!sim.stage.enabled)
        {
            return std::nullopt;
        }
        float bestDist = std::numeric_limits<float>::max();
        std::optional<std::pair<Vec2, float>> result;
        for (const StageAllyBaseState &base : sim.stage.allyBases)
        {
            if (base.destroyed)
            {
                continue;
            }
            const float dist = lengthSq(base.pos - pos);
            if (dist < bestDist)
            {
                bestDist = dist;
                const float radius = base.auraRadiusPx > 0.0f ? base.auraRadiusPx : 128.0f;
                result = std::make_pair(base.pos, radius);
            }
        }
        return result;
    };

    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        Unit &yuna = yunas[i];
        yuna.desiredVelocity = {0.0f, 0.0f};
        yuna.hasDesiredVelocity = false;
        const AllyLevelingParams levelParams = kCommanderLevelingParams;
        const int namedLevel = yuna.isNamed ? sim.namedLevel(yuna.name) : 1;
        const float levelDelta = static_cast<float>(std::max(namedLevel - 1, 0));
        const float namedSpeedMul = yuna.isNamed ? (1.0f + levelParams.speedPerLevel * levelDelta) : 1.0f;
        const float yunaSpeedPx = sim.yunaStats.speed_u_s * sim.config.pixels_per_unit * namedSpeedMul;
        // Named units are tuned to match the commander’s movement speed
        const float baseSpeed = yuna.isNamed ? commanderSpeedPx : yunaSpeedPx;
        const float unitSpeed = baseSpeed * std::max(0.01f, yuna.moraleSpeedMultiplier);
        const bool immobilized =
            yuna.job.endlag > 0.0f || yuna.job.warrior.stumbleTimer > 0.0f || yuna.job.archer.holdTimer > 0.0f;
        float jobSpeedMultiplier = 1.0f;
        if (!yuna.isNamed && yuna.job.shield.selfSlowTimer > 0.0f)
        {
            jobSpeedMultiplier *= std::clamp(sim.config.shieldJob.selfSlowMultiplier, 0.0f, 1.0f);
        }
        const bool aggressiveRoleBase = yuna.temperament.baseRole == ChibiAction::AssaultEnemy ||
                                        yuna.temperament.baseRole == ChibiAction::AssaultBase;
        const float retreatSpeedMultiplier = yuna.moraleRetreatActive
                                                 ? std::max(yuna.moraleRetreatSpeedMultiplier, 0.0f)
                                                 : 1.0f;
        float effectiveSpeed = immobilized ? 0.0f : unitSpeed * jobSpeedMultiplier * retreatSpeedMultiplier;
        detectionUnit = &yuna;
        forceUnlimitedEnemySearch = aggressiveRoleBase;
        TemperamentState &temperamentState = yuna.temperament;
        if (!yuna.name.empty())
        {
            temperamentState.panicTimer = 0.0f;
            yuna.forcePanic = false;
        }
        else if (temperamentState.panicTimer > 0.0f)
        {
            temperamentState.panicTimer = std::max(0.0f, temperamentState.panicTimer - dt);
        }
        const float panicMinDuration = sim.panicMinimumDurationFor(yuna);
        const float denom = std::max(yuna.maxHp, 0.0001f);
        const float hpRatio = denom > 0.0f ? std::max(0.0f, yuna.hp / denom) : 0.0f;
        const bool commanderAlive = commander.alive;
        const bool isCoward = yuna.braveryAxis <= -2 && yuna.wisdomAxis <= -2;
        const float fleeThreshold = sim.panicThresholdFor(yuna);
        const float recoverTarget =
            std::clamp(std::max(sim.chibiPersonalityConfig.kaiten.hpSafe, fleeThreshold + runCfg.exitHpBonus),
                       0.0f,
                       0.98f);
        bool hpRetreatActive = yuna.hpRetreatActive;
        const float retreatTrigger = std::clamp(fleeThreshold - 0.05f, 0.05f, 0.95f);
        if (!hpRetreatActive && hpRatio <= retreatTrigger)
        {
            yuna.hpRetreatActive = true;
            hpRetreatActive = true;
        }
        if (hpRetreatActive)
        {
            const bool recovered = hpRatio >= recoverTarget && isWithinAllyAura(yuna.pos);
            if (recovered)
            {
                yuna.hpRetreatActive = false;
                hpRetreatActive = false;
            }
        }
        const bool needsBaseRecovery = hpRetreatActive;
        const float shelterGap = needsBaseRecovery ? std::max(0.0f, recoverTarget - hpRatio) : 0.0f;
        const float shelterUrgency = needsBaseRecovery ? std::clamp(shelterGap * 3.0f, 0.0f, 2.5f) : 0.0f;
        (void)shelterUrgency;

        if (yuna.forcePanic)
        {
            temperamentState.panicTimer = std::max(temperamentState.panicTimer, panicMinDuration);
        }
        else if (hpRatio <= sim.panicThresholdFor(yuna))
        {
            temperamentState.panicTimer = std::max(temperamentState.panicTimer, panicMinDuration);
        }

        const bool rawPanic = yuna.forcePanic || temperamentState.panicTimer > 0.0f;

        if (rawPanic)
        {
            ++panicFrameCount;
        }

        if (!rawPanic || yuna.forcePanic)
        {
            releaseTokkou(yuna);
            releaseCling(yuna);
            releaseKaiten(yuna);
            releaseRunAround(yuna);
        }
        if (yuna.panicTokkouActive)
        {
            if (yuna.forcePanic)
            {
                releaseTokkou(yuna);
            }
            else
            {
                yuna.panicTokkouTimer = std::max(0.0f, yuna.panicTokkouTimer - dt);
                if (yuna.panicTokkouTimer <= 0.0f)
                {
                    releaseTokkou(yuna);
                }
            }
        }
        if (yuna.panicClingActive)
        {
            if (!commander.alive)
            {
                releaseCling(yuna);
                if (!yuna.panicKaitenActive &&
                    yuna.wisdomAxis >= sim.chibiPersonalityConfig.kaiten.wisdomMin)
                {
                    yuna.panicKaitenActive = true;
                    yuna.panicKaitenTimer = sim.chibiPersonalityConfig.kaiten.minStaySeconds;
                    yuna.panicKaitenGuardTimer = sim.chibiPersonalityConfig.kaiten.guardBiasSeconds;
                    yuna.panicKaitenGuardDirection = {1.0f, 0.0f};
                }
            }
            else if (yuna.forcePanic)
            {
                releaseCling(yuna);
            }
            else
            {
                if (clingCfg.exitHpBonus > 0.0f &&
                    hpRatio >= sim.panicThresholdFor(yuna) + clingCfg.exitHpBonus)
                {
                    releaseCling(yuna);
                }
                else if (clingCfg.exitAuraSeconds > 0.0f)
                {
                    if (isWithinAllyAura(yuna.pos))
                    {
                        yuna.panicClingAuraTimer += dt;
                        if (yuna.panicClingAuraTimer >= clingCfg.exitAuraSeconds)
                        {
                            releaseCling(yuna);
                        }
                    }
                    else
                    {
                        yuna.panicClingAuraTimer = 0.0f;
                    }
                }
            }
        }

        if (yuna.panicKaitenActive)
        {
            const bool insideAura = isWithinAllyAura(yuna.pos);
            if (insideAura)
            {
                if (yuna.panicKaitenTimer > 0.0f)
                {
                    yuna.panicKaitenTimer = std::max(0.0f, yuna.panicKaitenTimer - dt);
                }
                else if (yuna.panicKaitenGuardTimer > 0.0f)
                {
                    yuna.panicKaitenGuardTimer = std::max(0.0f, yuna.panicKaitenGuardTimer - dt);
                }
            }
            const float fleeThreshold = sim.panicThresholdFor(yuna);
            const float exitHpTarget =
                std::min(sim.chibiPersonalityConfig.kaiten.hpSafe,
                         fleeThreshold + sim.chibiPersonalityConfig.kaiten.exitHpBonus);
            const bool hpRecovered = hpRatio >= exitHpTarget;
            if (!rawPanic || hpRecovered)
            {
                releaseKaiten(yuna);
            }
        }

        if (yuna.panicRunActive)
        {
            if (!rawPanic)
            {
                releaseRunAround(yuna);
            }
            else
            {
                if (yuna.panicRunDashTimer > 0.0f)
                {
                    yuna.panicRunDashTimer = std::max(0.0f, yuna.panicRunDashTimer - dt);
                    if (runCfg.repathSeconds > 0.0f)
                    {
                        yuna.panicRunRepathTimer = std::max(0.0f, yuna.panicRunRepathTimer - dt);
                        if (yuna.panicRunRepathTimer <= 0.0f)
                        {
                            yuna.panicRunRepathTimer = runCfg.repathSeconds;
                            yuna.panicRunDirection = computeRunAroundDirection(yuna.pos);
                        }
                    }
                }
                else if (yuna.panicRunJitterTimer > 0.0f)
                {
                    yuna.panicRunJitterTimer = std::max(0.0f, yuna.panicRunJitterTimer - dt);
                }
                else
                {
                    yuna.panicRunDashTimer = std::max(runCfg.dashSeconds, 0.0f);
                    yuna.panicRunJitterTimer = std::max(runCfg.jitterSeconds, 0.0f);
                    yuna.panicRunRepathTimer = std::max(runCfg.repathSeconds, 0.0f);
                    Vec2 dashDir = computeRunAroundDirection(yuna.pos);
                    if (lengthSq(dashDir) <= 0.0001f)
                    {
                        dashDir = {1.0f, 0.0f};
                    }
                    yuna.panicRunDirection = normalize(dashDir);
                    yuna.panicRunJitterDirection = sim.randomUnitVector();
                    if (lengthSq(yuna.panicRunJitterDirection) <= 0.0001f)
                    {
                        yuna.panicRunJitterDirection = {-yuna.panicRunDirection.y, yuna.panicRunDirection.x};
                    }
                }
                const float exitHp =
                    std::min(0.95f, sim.panicThresholdFor(yuna) + runCfg.exitHpBonus);
                if (hpRatio >= exitHp)
                {
                    releaseRunAround(yuna);
                }
                else if (runCfg.exitAuraSeconds > 0.0f)
                {
                    if (isWithinAllyAura(yuna.pos))
                    {
                        yuna.panicRunAuraTimer += dt;
                        if (yuna.panicRunAuraTimer >= runCfg.exitAuraSeconds)
                        {
                            releaseRunAround(yuna);
                        }
                    }
                    else
                    {
                        yuna.panicRunAuraTimer = 0.0f;
                    }
                }
            }
        }

        if (rawPanic && !yuna.forcePanic && !yuna.panicTokkouActive)
        {
            const bool meetsTokkou =
                yuna.braveryAxis >= tokkouCfg.braveryMin && yuna.braveryAxis <= tokkouCfg.braveryMax &&
                yuna.wisdomAxis >= tokkouCfg.wisdomMin && yuna.wisdomAxis <= tokkouCfg.wisdomMax;
            if (tokkouCfg.duration > 0.0f && meetsTokkou &&
                (maxTokkou == 0 || activeTokkou < maxTokkou))
            {
                yuna.panicTokkouActive = true;
                yuna.panicTokkouTimer = tokkouCfg.duration;
                ++activeTokkou;
            }
            else if (meetsTokkou && maxTokkou > 0 && activeTokkou >= maxTokkou && m_tokkouWarningTimer <= 0.0f)
            {
                sim.pushTelemetry("Tokkou limit reached (" + std::to_string(maxTokkou) + ")");
                sim.pushTip("Tokkou limit reached (" + std::to_string(maxTokkou) + ")");
                m_tokkouWarningTimer = 2.0f;
            }
            else if (!yuna.panicClingActive)
            {
                const bool meetsCling =
                    yuna.braveryAxis <= clingCfg.braveryMax && yuna.wisdomAxis >= clingCfg.wisdomMin;
                if (meetsCling && commander.alive)
                {
                    yuna.panicClingActive = true;
                    yuna.panicClingAuraTimer = 0.0f;
                }
                else if (!yuna.panicKaitenActive &&
                         yuna.wisdomAxis >= sim.chibiPersonalityConfig.kaiten.wisdomMin)
                {
                    yuna.panicKaitenActive = true;
                    yuna.panicKaitenTimer = sim.chibiPersonalityConfig.kaiten.minStaySeconds;
                    yuna.panicKaitenGuardTimer = sim.chibiPersonalityConfig.kaiten.guardBiasSeconds;
                    yuna.panicKaitenGuardDirection = {1.0f, 0.0f};
                }
                else if (!yuna.panicRunActive && yuna.wisdomAxis <= runCfg.wisdomMax)
                {
                    yuna.panicRunActive = true;
                    yuna.panicRunDashTimer = std::max(runCfg.dashSeconds, 0.0f);
                    yuna.panicRunJitterTimer = std::max(runCfg.jitterSeconds, 0.0f);
                    yuna.panicRunRepathTimer = std::max(runCfg.repathSeconds, 0.0f);
                    yuna.panicRunAuraTimer = 0.0f;
                    Vec2 dashDir = computeRunAroundDirection(yuna.pos);
                    if (lengthSq(dashDir) <= 0.0001f)
                    {
                        dashDir = {1.0f, 0.0f};
                    }
                    yuna.panicRunDirection = normalize(dashDir);
                    yuna.panicRunJitterDirection = sim.randomUnitVector();
                    if (lengthSq(yuna.panicRunJitterDirection) <= 0.0001f)
                    {
                        yuna.panicRunJitterDirection = {-yuna.panicRunDirection.y, yuna.panicRunDirection.x};
                    }
                }
            }
        }

        if (yuna.panicClingActive)
        {
            sim.actionTelemetry.clingUnitTime += dt;
        }
        if (yuna.panicKaitenActive)
        {
            sim.actionTelemetry.kaitenUnitTime += dt;
        }
        if (yuna.panicRunActive)
        {
            sim.actionTelemetry.runUnitTime += dt;
        }
        if (yuna.panicTokkouActive)
        {
            tokkouUnitIds.push_back(static_cast<int>(i));
            recordPanic(static_cast<int>(i), "Tokkou", yuna.panicTokkouTimer);
        }
        if (yuna.panicClingActive)
        {
            clingUnitIds.push_back(static_cast<int>(i));
            recordPanic(static_cast<int>(i), "Cling", yuna.temperament.panicTimer);
        }
        if (yuna.panicKaitenActive)
        {
            kaitenUnitIds.push_back(static_cast<int>(i));
            recordPanic(static_cast<int>(i), "Kaiten", yuna.panicKaitenTimer);
        }
        if (yuna.panicRunActive)
        {
            runUnitIds.push_back(static_cast<int>(i));
            recordPanic(static_cast<int>(i), "Run", yuna.panicRunDashTimer + yuna.panicRunJitterTimer);
        }

        const bool panicFlee =
            rawPanic && !yuna.panicTokkouActive && !yuna.panicClingActive && !yuna.panicKaitenActive &&
            !yuna.panicRunActive;
        if (panicFlee)
        {
            recordPanic(static_cast<int>(i), "Panic", yuna.temperament.panicTimer);
        }
        const bool panicForceFlee = panicFlee && (!commanderAlive || isCoward);
        const bool panicShelter = panicFlee && !panicForceFlee;

        if (temperamentState.wanderTimer > 0.0f)
        {
            temperamentState.wanderTimer = std::max(0.0f, temperamentState.wanderTimer - dt);
        }
        else
        {
            temperamentState.wanderDirection = sim.randomUnitVector();
            temperamentState.wanderTimer = sim.randomRange(sim.temperamentConfig.wanderTurnInterval);
        }

        temperamentState.microActionDecisionTimer =
            std::max(0.0f, temperamentState.microActionDecisionTimer - dt);
        temperamentState.previousMicroActionTimer =
            std::max(0.0f, temperamentState.previousMicroActionTimer - dt);
        if (temperamentState.microActionStickyTimer > 0.0f)
        {
            temperamentState.microActionStickyTimer =
                std::max(0.0f, temperamentState.microActionStickyTimer - dt);
        }

        const float wisdomNorm = normalizedAxisValue(yuna.wisdomAxis);
        const float cohesionStrength =
            axisLerp(sim.chibiAiParams.cohesionStrengthLow, sim.chibiAiParams.cohesionStrengthHigh, wisdomNorm);
        const float separationStrength =
            axisLerp(sim.chibiAiParams.separationStrengthLow, sim.chibiAiParams.separationStrengthHigh, wisdomNorm);
        Vec2 boidAdjustment =
            computeBoidAdjustment(yuna, yunas, cohesionStrength, separationStrength, sim.chibiAiParams);
        if (aggressiveRoleBase)
        {
            boidAdjustment = boidAdjustment * sim.chibiAiParams.aggressiveCohesionMultiplier;
        }
        const bool followBehaviorBase = temperamentState.baseRole == ChibiAction::FollowCommander ||
                                        temperamentState.baseRole == ChibiAction::DefendBase;
        if (followBehaviorBase)
        {
            boidAdjustment = boidAdjustment * sim.chibiAiParams.followCohesionMultiplier;
        }
        const float aoeMultiplier =
            axisLerp(sim.chibiAiParams.aoeAvoidMultiplierLow, sim.chibiAiParams.aoeAvoidMultiplierHigh, wisdomNorm);
        Vec2 aoeAdjustment = computeAoeAvoidance(yuna.pos, hazards, sim.chibiAiParams, aoeMultiplier);
        const bool stanceFollower = yuna.followByStance;
        const bool skillFollower = yuna.followBySkill;
        bool formationFollower = stanceFollower || skillFollower;
        const bool retreatActive = yuna.moraleRetreatActive;

        const float effectiveIgnoreChance =
            std::clamp(yuna.moraleIgnoreOrdersChance - yuna.moraleCommandObeyBonus, 0.0f, 1.0f);
        const float moraleDecisionInterval =
            kMoraleIgnoreDecisionInterval * std::max(0.01f, yuna.moraleRetargetCooldownMultiplier);
        if (effectiveIgnoreChance > 0.0f)
        {
            if (yuna.moraleIgnoreOrdersTimer <= 0.0f)
            {
                std::uniform_real_distribution<float> roll(0.0f, 1.0f);
                yuna.moraleIgnoringOrders = roll(sim.rng) < effectiveIgnoreChance;
                yuna.moraleIgnoreOrdersTimer = moraleDecisionInterval;
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
        const float braveryFactor = std::clamp(static_cast<float>(yuna.braveryAxis) / 2.0f, -1.0f, 1.0f);
        const float wisdomFactor = std::clamp(static_cast<float>(yuna.wisdomAxis) / 2.0f, -1.0f, 1.0f);

        (void)braveryFactor;
        (void)wisdomFactor;

        if (!temperamentState.roleAssigned)
        {
            auto mapBehaviorToAction = [](TemperamentBehavior behavior) -> ChibiAction {
                switch (behavior)
                {
                case TemperamentBehavior::ChargeNearest: return ChibiAction::AssaultEnemy;
                case TemperamentBehavior::RaidGate: return ChibiAction::AssaultBase;
                case TemperamentBehavior::GuardBase: return ChibiAction::DefendBase;
                case TemperamentBehavior::Homebound: return ChibiAction::DefendBase;
                case TemperamentBehavior::FollowYuna: return ChibiAction::FollowCommander;
                case TemperamentBehavior::FleeNearest: return ChibiAction::Flee;
                case TemperamentBehavior::TargetTag: return ChibiAction::AssaultEnemy;
                case TemperamentBehavior::Doze:
                case TemperamentBehavior::Wander: return ChibiAction::AssaultEnemy;
                case TemperamentBehavior::Mimic: return ChibiAction::AssaultEnemy;
                default: return ChibiAction::AssaultEnemy;
                }
            };
            ChibiAction nextAction = mapBehaviorToAction(temperamentState.currentBehavior);
            if (temperamentState.currentBehavior == TemperamentBehavior::Mimic)
            {
                nextAction = mapBehaviorToAction(temperamentState.mimicBehavior);
            }
            temperamentState.previousMicroAction = temperamentState.microAction;
            temperamentState.previousMicroActionTimer = sim.chibiAiParams.hysteresisDuration;
            temperamentState.microAction = nextAction;
            temperamentState.baseRole = nextAction;
            temperamentState.microActionDecisionTimer = decisionInterval;
            temperamentState.microActionStickyTimer = sim.chibiAiParams.hysteresisDuration;
            temperamentState.roleAssigned = true;
        }
        temperamentState.microAction = temperamentState.baseRole;

        ChibiAction desiredAction = temperamentState.baseRole;
        const bool aggressiveBehavior = temperamentState.baseRole == ChibiAction::AssaultEnemy ||
                                        temperamentState.baseRole == ChibiAction::AssaultBase;
        if (panicForceFlee)
        {
            desiredAction = ChibiAction::Flee;
        }
        else if (panicShelter || needsBaseRecovery)
        {
            desiredAction = ChibiAction::DefendBase;
        }
        else if (aggressiveBehavior)
        {
            // 突撃/拠点攻撃ロールは途中でフォローに切り替えない
            desiredAction = temperamentState.baseRole;
        }
        else if (!yuna.isNamed)
        {
            // 非ネームドは基本ロール固定（パニック/HP退避以外で変えない）
            desiredAction = temperamentState.baseRole;
        }
        // 指定ターゲットがいる場合は非ネームドを突撃に振る
        if (!yuna.isNamed && focusTargetEnemy)
        {
            desiredAction = ChibiAction::AssaultEnemy;
        }
        // Named units stay back: follow commander / base, avoid前線
        if (yuna.isNamed)
        {
            const EnemyUnit *nearestNamedTarget = nearestEnemy(yuna.pos, enemies);
            const float threatRadius = sim.config.pixels_per_unit * 6.0f; // 約96px
            if (nearestNamedTarget &&
                lengthSq(nearestNamedTarget->pos - yuna.pos) <= threatRadius * threatRadius)
            {
                desiredAction = ChibiAction::DefendBase;
            }
            else
            {
                desiredAction = ChibiAction::FollowCommander;
            }
            // Named skill発火（簡易）
            if (yuna.namedSkillCooldown > 0.0f)
            {
                yuna.namedSkillCooldown = std::max(0.0f, yuna.namedSkillCooldown - dt);
            }
            if (yuna.namedSkillCooldown <= 0.0f)
            {
                if (yuna.name == "Milly")
                {
                    if (const EnemyUnit *target = nearestEnemy(yuna.pos, enemies))
                    {
                        const float range = sim.config.pixels_per_unit * 14.0f; // ~224px
                        if (lengthSq(target->pos - yuna.pos) <= range * range &&
                            sim.spawnProjectile(yuna.pos, target->pos, 6.0f, 180.0f, 5.0f))
                        {
                            // かすみ弾のエフェクト
                            LegacySimulation::Effect fx;
                            fx.pos = target->pos;
                            fx.radius = 16.0f;
                            fx.r = 255.0f;
                            fx.g = 180.0f;
                            fx.b = 255.0f;
                            fx.a = 220.0f;
                            fx.ttl = 0.4f;
                            sim.effects.push_back(fx);
                            sim.pushTelemetry("Milly arrow");
                    yuna.namedSkillCooldown = 1.2f;
                }
            }
        }

        // Wanderは使わない方針: 何らかの理由でWanderになった場合も突撃へ上書き
        if (desiredAction == ChibiAction::Wander)
        {
            desiredAction = ChibiAction::AssaultEnemy;
        }
                else if (yuna.name == "Mary")
                {
                    Unit *bestAlly = nullptr;
                    float bestHpRatio = 1.1f;
                    const float range = sim.config.pixels_per_unit * 10.0f; // 160px
                    const float rangeSq = range * range;
                    for (Unit &ally : yunas)
                    {
                        if (&ally == &yuna || ally.hp <= 0.0f || ally.maxHp <= 0.0f)
                        {
                            continue;
                        }
                        const float distSq = lengthSq(ally.pos - yuna.pos);
                        const float ratio = ally.hp / ally.maxHp;
                        if (distSq <= rangeSq && ratio < bestHpRatio)
                        {
                            bestHpRatio = ratio;
                            bestAlly = &ally;
                        }
                    }
                    CommanderUnit &commanderRef = context.commander;
                    const float commanderRatio = commanderRef.hp / std::max(sim.commanderStats.hp, 0.0001f);
                    if (commanderRef.alive && commanderRatio < bestHpRatio &&
                        lengthSq(commanderRef.pos - yuna.pos) <= rangeSq)
                    {
                        bestHpRatio = commanderRatio;
                        bestAlly = nullptr; // signal commander
                    }
                    if (bestHpRatio < 1.0f - 1e-3f)
                    {
                        const float heal = 10.0f;
                        if (bestAlly)
                        {
                            bestAlly->hp = std::min(bestAlly->maxHp, bestAlly->hp + heal);
                        }
                        else
                        {
                            commanderRef.hp = std::min(sim.commanderStats.hp, commanderRef.hp + heal);
                        }
                        // ヒールエフェクト
                        LegacySimulation::Effect fx;
                        fx.pos = bestAlly ? bestAlly->pos : commanderRef.pos;
                        fx.radius = 26.0f;
                        fx.r = 110.0f;
                        fx.g = 235.0f;
                        fx.b = 180.0f;
                        fx.a = 210.0f;
                        fx.ttl = 0.6f;
                        sim.effects.push_back(fx);
                        sim.pushTelemetry("Mary heal");
                        yuna.namedSkillCooldown = 1.5f;
                    }
                }
                else if (yuna.name == "Coco")
                {
                    if (const EnemyUnit *target = nearestEnemy(yuna.pos, enemies))
                    {
                        const float range = sim.config.pixels_per_unit * 10.0f; // 160px
                        if (lengthSq(target->pos - yuna.pos) <= range * range)
                        {
                            const float bombRadius = 60.0f;
                            const float bombRadiusSq = bombRadius * bombRadius;
                            for (EnemyUnit &enemy : enemies)
                            {
                                if (enemy.hp <= 0.0f)
                                {
                                    continue;
                                }
                                if (lengthSq(enemy.pos - target->pos) <= bombRadiusSq)
                                {
                                    enemy.hp -= 6.0f;
                                    // ノックバックを少し与える
                                    Vec2 away = normalizedDirection(target->pos, enemy.pos);
                                    if (lengthSq(away) > 0.0f)
                                    {
                                        enemy.pos += away * 108.0f;
                                    }
                                }
                            }
                            // 爆風エフェクト
                            LegacySimulation::Effect fx;
                            fx.pos = target->pos;
                            fx.radius = bombRadius + 10.0f;
                            fx.r = 255.0f;
                            fx.g = 140.0f;
                            fx.b = 90.0f;
                            fx.a = 200.0f;
                            fx.ttl = 0.6f;
                            sim.effects.push_back(fx);
                            sim.pushTelemetry("Coco bomb");
                            yuna.namedSkillCooldown = 4.0f;
                        }
                    }
                }
            }
        }

        auto makeVelocity = [&](const Vec2 &dir) -> Vec2 {
            if (lengthSq(dir) < 0.0001f)
            {
                return {0.0f, 0.0f};
            }
            return dir * unitSpeed;
        };

        Vec2 velocity{0.0f, 0.0f};
        if (retreatActive)
        {
            Vec2 retreatDir{0.0f, 0.0f};
            Vec2 away{0.0f, 0.0f};
            if (EnemyUnit *threat = nearestEnemyLimited(yuna.pos))
            {
                away = normalizedDirection(threat->pos, yuna.pos);
            }
            Vec2 toBase = normalizedDirection(yuna.pos, sim.basePos);
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
            velocity = makeVelocity(normalize(retreatDir));
        }
        else if (panicForceFlee)
        {
            EnemyUnit *threat = nearestEnemyLimited(yuna.pos);
            Vec2 dir{0.0f, 0.0f};
            if (threat)
            {
                dir = normalizedDirection(threat->pos, yuna.pos);
            }
            else
            {
                dir = normalizedDirection(yuna.pos, sim.basePos);
            }
            velocity = makeVelocity(dir);
        }
        else if (panicShelter)
        {
            Vec2 defendSpot = sim.basePos;
            if (auto info = nearestAllyBaseInfo(yuna.pos))
            {
                defendSpot = info->first;
            }
            velocity = makeVelocity(normalizedDirection(yuna.pos, defendSpot));
        }
        else if (yuna.panicTokkouActive)
        {
            Vec2 dir{0.0f, 0.0f};
            if (EnemyUnit *target = nearestEnemy(yuna.pos, enemies))
            {
                dir = normalizedDirection(yuna.pos, target->pos);
            }
            else if (auto base = nearestEnemyBase(sim, yuna.pos))
            {
                dir = normalizedDirection(yuna.pos, *base);
            }
            else if (!raidTargets.empty())
            {
                dir = normalizedDirection(yuna.pos, raidTargets.front());
            }
            else
            {
                dir = normalizedDirection(yuna.pos, sim.basePos);
            }
            velocity = makeVelocity(dir);
            velocity = velocity * sim.chibiPersonalityConfig.tokkou.speedMultiplier;
        }
        else if (yuna.panicClingActive)
        {
            Vec2 target = computeClingTarget(yuna, i);
            velocity = makeVelocity(normalizedDirection(yuna.pos, target));
        }
        else if (yuna.panicKaitenActive)
        {
            Vec2 defendSpot = sim.basePos;
            float auraRadius = 128.0f;
            if (auto info = nearestAllyBaseInfo(yuna.pos))
            {
                defendSpot = info->first;
                auraRadius = info->second;
            }
            const float auraSq = auraRadius * auraRadius;
            const Vec2 toBase = defendSpot - yuna.pos;
            const bool inside = lengthSq(toBase) <= auraSq;
            if (!inside)
            {
                velocity = makeVelocity(normalizedDirection(yuna.pos, defendSpot));
            }
            else if (yuna.panicKaitenTimer > 0.0f)
            {
                Vec2 inward = normalizedDirection(yuna.pos, defendSpot);
                velocity = makeVelocity(inward);
            }
            else
            {
                Vec2 guardDir = yuna.panicKaitenGuardDirection;
                const bool needsRefresh = yuna.panicKaitenGuardTimer <= 0.0f || lengthSq(guardDir) <= 0.0001f;
                if (needsRefresh)
                {
                    bool guardValid = false;
                    if (EnemyUnit *threat = nearestEnemy(defendSpot, enemies))
                    {
                        Vec2 threatDir = normalizedDirection(defendSpot, threat->pos);
                        if (lengthSq(threatDir) > 0.0f)
                        {
                            guardDir = threatDir;
                            guardValid = true;
                        }
                    }
                    if (!guardValid)
                    {
                        Vec2 wanderDir = normalizedDirection(Vec2{0.0f, 0.0f}, temperamentState.wanderDirection);
                        if (lengthSq(wanderDir) > 0.0f)
                        {
                            guardDir = wanderDir;
                            guardValid = true;
                        }
                    }
                    if (!guardValid)
                    {
                        guardDir = {1.0f, 0.0f};
                    }
                    guardDir = normalize(guardDir);
                    yuna.panicKaitenGuardDirection = guardDir;
                    const float guardBias = std::max(sim.chibiPersonalityConfig.kaiten.guardBiasSeconds, 0.1f);
                    yuna.panicKaitenGuardTimer = guardBias;
                }
                else
                {
                    guardDir = normalize(guardDir);
                    if (lengthSq(guardDir) <= 0.0001f)
                    {
                        guardDir = {1.0f, 0.0f};
                    }
                }

                const float ringRadius = std::max(auraRadius * 0.75f, auraRadius - 24.0f);
                Vec2 guardPos = defendSpot + guardDir * ringRadius;
                Vec2 toGuard = guardPos - yuna.pos;
                if (lengthSq(toGuard) > 9.0f)
                {
                    velocity = makeVelocity(normalize(toGuard));
                }
                else
                {
                    Vec2 tangent{-guardDir.y, guardDir.x};
                    if (lengthSq(tangent) < 0.0001f)
                    {
                        tangent = {guardDir.y, -guardDir.x};
                    }
                    velocity = makeVelocity(normalize(tangent));
                }
            }
        }
        else if (yuna.panicRunActive)
        {
            Vec2 dir{0.0f, 0.0f};
            if (yuna.panicRunDashTimer > 0.0f)
            {
                dir = yuna.panicRunDirection;
            }
            else if (yuna.panicRunJitterTimer > 0.0f)
            {
                Vec2 jitter = yuna.panicRunJitterDirection;
                if (lengthSq(jitter) <= 0.0001f)
                {
                    jitter = sim.randomUnitVector();
                    yuna.panicRunJitterDirection = jitter;
                }
                dir = normalize(jitter);
                if (lengthSq(dir) <= 0.0001f)
                {
                    dir = {1.0f, 0.0f};
                }
            }
            else
            {
                dir = yuna.panicRunDirection;
            }
            velocity = makeVelocity(dir);
        }
        else if (skillFollower && commander.alive)
        {
            Vec2 desiredPos = commander.pos + yuna.formationOffset;
            Vec2 toTarget = desiredPos - yuna.pos;
            if (lengthSq(toTarget) > 16.0f)
            {
                velocity = makeVelocity(normalize(toTarget));
            }
            else
            {
                velocity = makeVelocity(normalize(temperamentState.wanderDirection));
            }
        }
        else
        {
            switch (desiredAction)
            {
            case ChibiAction::AssaultEnemy:
            {
                if (!yuna.isNamed && focusTargetEnemy)
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, focusTargetPos));
                }
                else if (EnemyUnit *target = nearestEnemy(yuna.pos, enemies))
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, target->pos));
                }
                else if (auto base = nearestEnemyBase(sim, yuna.pos))
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, *base));
                }
                break;
            }
            case ChibiAction::AssaultBase:
            {
                if (auto base = nearestEnemyBase(sim, yuna.pos))
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, *base));
                }
                else if (!raidTargets.empty())
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, raidTargets.front()));
                }
                else if (EnemyUnit *target = nearestEnemy(yuna.pos, enemies))
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, target->pos));
                }
                break;
            }
            case ChibiAction::Flee:
            {
                // 敵からひたすら離れる（拠点への逃げ込みはしない）
                if (EnemyUnit *target = nearestEnemy(yuna.pos, enemies))
                {
                    Vec2 away = normalizedDirection(target->pos, yuna.pos);
                    // 拠点オーラ内なら拠点の反対側へ強めに押し出す
                    if (auto info = nearestAllyBaseInfo(yuna.pos))
                    {
                        const float auraSq = info->second * info->second;
                        if (lengthSq(yuna.pos - info->first) <= auraSq)
                        {
                            Vec2 baseAway = normalizedDirection(info->first, yuna.pos);
                            away = normalize(away + baseAway * 0.9f);
                        }
                    }
                    // 逃げ仲間との反発で団子を避ける
                    Vec2 repel{0.0f, 0.0f};
                    for (std::size_t j = 0; j < yunas.size(); ++j)
                    {
                        if (j == i)
                        {
                            continue;
                        }
                        const Unit &other = yunas[j];
                        if (other.hp <= 0.0f || other.temperament.baseRole != ChibiAction::Flee)
                        {
                            continue;
                        }
                        const float distSq = lengthSq(other.pos - yuna.pos);
                        const float repelRadius = 140.0f;
                        if (distSq > 0.0001f && distSq <= repelRadius * repelRadius)
                        {
                            const float dist = std::sqrt(distSq);
                            const float weight = std::clamp(1.0f - dist / repelRadius, 0.0f, 1.0f);
                            repel += normalizedDirection(other.pos, yuna.pos) * weight;
                        }
                    }
                    // 左右に大きく散らすオフセット
                    const float angleDeg = (static_cast<int>(i % 2 == 0 ? 1 : -1)) * 45.0f;
                    Vec2 jittered = rotateVec(away, angleDeg);
                    Vec2 runDir = jittered + repel * 0.8f;
                    if (lengthSq(runDir) <= 0.0001f)
                    {
                        runDir = jittered;
                    }
                    velocity = makeVelocity(normalize(runDir));
                }
                else
                {
                    // 敵が見えないときはランダム徘徊方向で距離を稼ぐ
                    velocity = makeVelocity(normalize(temperamentState.wanderDirection));
                }
                break;
            }
            case ChibiAction::FollowCommander:
            {
                Vec2 target = commander.alive ? commander.pos : sim.basePos;
                if (commander.alive)
                {
                    if (yuna.isNamed)
                    {
                        // 後衛: 敵とコマンダーを挟んでコマンダー背後にポジション
                        if (const EnemyUnit *threat = nearestEnemy(commander.pos, enemies))
                        {
                            Vec2 away = normalizedDirection(threat->pos, commander.pos);
                            if (lengthSq(away) <= 0.0f)
                            {
                                away = {0.0f, -1.0f};
                            }
                            const float arcRadius = 100.0f;
                            float angleDeg = 0.0f;
                            if (yuna.name == "Milly") angleDeg = -20.0f;
                            else if (yuna.name == "Mary") angleDeg = 0.0f;
                            else if (yuna.name == "Coco") angleDeg = 20.0f;
                            Vec2 offsetDir = rotateVec(away, angleDeg);
                            target = commander.pos + offsetDir * arcRadius;
                        }
                    }
                    if (formationFollower && !yuna.isNamed)
                    {
                        target = commander.pos + yuna.formationOffset;
                    }
                    if (!yuna.isNamed)
                    {
                        // Commander追従: 軌道をなぞるように後ろに付く / 待機時は近距離で待つ（ちび専用）
                        const float moveLenSq = lengthSq(commander.lastVelocity);
                        const float pixelsPerUnit =
                            sim.config.pixels_per_unit > 0.0f ? sim.config.pixels_per_unit : 16.0f;
                        const float trailDist = pixelsPerUnit * 3.2f;   // ~51px
                        const float idleRadius = pixelsPerUnit * 1.5f;  // ~24px
                        Vec2 trailDir{0.0f, 0.0f};
                        if (moveLenSq > 1.0f)
                        {
                            trailDir = commander.lastVelocity / std::sqrt(moveLenSq);
                        }
                        else if (lengthSq(commander.lastMoveDir) > 0.0001f)
                        {
                            trailDir = normalize(commander.lastMoveDir);
                        }
                        if (lengthSq(trailDir) > 0.0f)
                        {
                            target = commander.pos - trailDir * trailDist;
                        }

                        const float distSq = lengthSq(yuna.pos - commander.pos);
                        if (distSq < idleRadius * idleRadius)
                        {
                            // 十分近いときはその場待機
                            velocity = {0.0f, 0.0f};
                            break;
                        }
                    }
                }
                Vec2 dir = normalizedDirection(yuna.pos, target);
                velocity = makeVelocity(dir);
                // 近距離フォロー時は群れ補正を弱めるため係数をゼロにする
                if (lengthSq(target - yuna.pos) < 64.0f * 64.0f)
                {
                    boidAdjustment = {0.0f, 0.0f};
                }
                break;
            }
            case ChibiAction::FollowAlly:
            {
                if (const Unit *ally = nearestAlly(yunas, i))
                {
                    velocity = makeVelocity(normalizedDirection(yuna.pos, ally->pos));
                }
                break;
            }
            case ChibiAction::DefendBase:
            {
                Vec2 defendSpot = sim.basePos;
                float auraRadius = 128.0f;
                if (auto info = nearestAllyBaseInfo(yuna.pos))
                {
                    defendSpot = info->first;
                    auraRadius = info->second;
                }
                EnemyUnit *intruder = nullptr;
                const float auraSq = auraRadius * auraRadius;
                float closest = std::numeric_limits<float>::max();
                for (EnemyUnit &enemy : enemies)
                {
                    if (enemy.hp <= 0.0f)
                    {
                        continue;
                    }
                    const float distSq = lengthSq(enemy.pos - defendSpot);
                    if (distSq <= auraSq && distSq < closest)
                    {
                        closest = distSq;
                        intruder = &enemy;
                    }
                }
                if (intruder)
                {
                    Vec2 target = intruder->pos;
                    Vec2 fromCenter = target - defendSpot;
                    const float dist = length(fromCenter);
                    const float clampRadius = std::max(auraRadius - 8.0f, auraRadius * 0.8f);
                    if (dist > clampRadius && dist > 0.0001f)
                    {
                        target = defendSpot + normalize(fromCenter) * clampRadius;
                    }
                    velocity = makeVelocity(normalizedDirection(yuna.pos, target));
                }
                else
                {
                    Vec2 threatDir{0.0f, 0.0f};
                    if (EnemyUnit *threat = nearestEnemy(defendSpot, enemies))
                    {
                        threatDir = normalizedDirection(defendSpot, threat->pos);
                    }
                    if (lengthSq(threatDir) <= 0.0f)
                    {
                        // 敵不在ならその場待機
                        velocity = {0.0f, 0.0f};
                        break;
                    }
                    constexpr float kRingMin = 70.0f;
                    constexpr float kRingMax = 110.0f;
                    float ringRadius = auraRadius > 0.0f ? std::max(auraRadius - 24.0f, kRingMin) : kRingMin;
                    ringRadius = std::clamp(ringRadius, kRingMin, kRingMax);
                    Vec2 guardPos = defendSpot + threatDir * ringRadius; // 敵側へ前進して壁になる
                    Vec2 toGuard = guardPos - yuna.pos;
                    if (lengthSq(toGuard) > 36.0f)
                    {
                        velocity = makeVelocity(normalize(toGuard));
                    }
                    else
                    {
                        Vec2 tangent{-threatDir.y, threatDir.x};
                        if (lengthSq(tangent) <= 0.0f)
                        {
                            tangent = {0.0f, 1.0f};
                        }
                        velocity = makeVelocity(normalize(tangent));
                    }
                }
                break;
            }
            case ChibiAction::Wander:
            default:
                velocity = makeVelocity(normalize(temperamentState.wanderDirection));
                break;
            }
        }

        actionFrameCounts[actionIndex(desiredAction)]++;

        if (lengthSq(boidAdjustment) > 0.0f)
        {
            Vec2 dir = normalizedDirection({0.0f, 0.0f}, boidAdjustment);
            velocity += dir * effectiveSpeed;
        }
        if (lengthSq(aoeAdjustment) > 0.0f)
        {
            Vec2 dir = normalizedDirection({0.0f, 0.0f}, aoeAdjustment);
            velocity += dir * effectiveSpeed;
            const float aoeMag = std::sqrt(lengthSq(aoeAdjustment));
            sim.actionTelemetry.aoeAvoidUnitTime += dt;
            sim.actionTelemetry.aoeAvoidMagnitudeAccum += aoeMag;
            ++sim.actionTelemetry.aoeAvoidMagnitudeSamples;
        }

        if (velocity.x != 0.0f || velocity.y != 0.0f)
        {
            const float currentSpeed = std::sqrt(lengthSq(velocity));
            if (currentSpeed > 0.0001f)
            {
                const float targetSpeed = std::min(effectiveSpeed, currentSpeed);
                const float scale = targetSpeed / currentSpeed;
                velocity = velocity * scale;
            }
        }

        if (velocity.x != 0.0f || velocity.y != 0.0f)
        {
            yuna.desiredVelocity = velocity;
            yuna.hasDesiredVelocity = true;
        }
    }

    sim.actionTelemetry.tokkouActive = activeTokkou;
    sim.actionTelemetry.tokkouPeak = std::max(sim.actionTelemetry.tokkouPeak, activeTokkou);

    const std::size_t boidSampleCount = std::min<std::size_t>(20, yunas.size());
    if (boidSampleCount > 0)
    {
        const float radius = computeCrowdRadius(yunas, boidSampleCount);
        if (radius > 0.0f)
        {
            sim.actionTelemetry.boidRadiusAccum += radius;
            ++sim.actionTelemetry.boidRadiusSamples;
            if (radius > 96.0f)
            {
                ++sim.actionTelemetry.boidRadiusExceed;
            }
        }
    }

    for (std::size_t i = 0; i < actionFrameCounts.size(); ++i)
    {
        sim.actionTelemetry.unitTime[i] += static_cast<float>(actionFrameCounts[i]) * dt;
    }
    sim.actionTelemetry.panicUnitTime += static_cast<float>(panicFrameCount) * dt;
    sim.actionTelemetry.totalUnitTime += static_cast<float>(yunas.size()) * dt;
    sim.actionTelemetry.timer += dt;

    constexpr float kTelemetryInterval = 1.0f;
    if (sim.actionTelemetry.timer >= kTelemetryInterval)
    {
        if (sim.actionTelemetry.totalUnitTime > 0.0f)
        {
            TelemetrySink::Payload payload;
            const float denom = std::max(sim.actionTelemetry.totalUnitTime, 0.0001f);
            std::array<std::pair<float, ChibiAction>, kChibiActionCount> ratios{};
            std::array<float, kChibiActionCount> ratioValues{};
            ratioValues.fill(0.0f);
            for (std::size_t i = 0; i < sim.actionTelemetry.unitTime.size(); ++i)
            {
                const float ratio = sim.actionTelemetry.unitTime[i] / denom;
                const std::string key = std::string("ai.actions.") + chibiActionKey(static_cast<ChibiAction>(i));
                payload[key] = formatRatio(ratio);
                ratios[i] = {ratio, static_cast<ChibiAction>(i)};
                ratioValues[i] = ratio;
            }
            const float panicRatio = sim.actionTelemetry.panicUnitTime / denom;
            payload["ai.actions.panic_ratio"] = formatRatio(panicRatio);
            payload["ai.actions.unit_seconds"] = formatRatio(sim.actionTelemetry.totalUnitTime);
            float avgRadius = 0.0f;
            float exceedRatio = 0.0f;
            if (sim.actionTelemetry.boidRadiusSamples > 0)
            {
                avgRadius = sim.actionTelemetry.boidRadiusAccum /
                            static_cast<float>(sim.actionTelemetry.boidRadiusSamples);
                exceedRatio = static_cast<float>(sim.actionTelemetry.boidRadiusExceed) /
                              static_cast<float>(sim.actionTelemetry.boidRadiusSamples);
            }
            payload["ai.boids.radius_avg"] = formatRatio(avgRadius);
            payload["ai.boids.radius_exceed_ratio"] = formatRatio(exceedRatio);
            payload["ai.actions.tokkou_active"] = std::to_string(sim.actionTelemetry.tokkouActive);
            payload["ai.actions.tokkou_peak"] = std::to_string(sim.actionTelemetry.tokkouPeak);
            const float clingRatio = sim.actionTelemetry.clingUnitTime / denom;
            const float kaitenRatio = sim.actionTelemetry.kaitenUnitTime / denom;
            const float runRatio = sim.actionTelemetry.runUnitTime / denom;
            const float aoeRatio = sim.actionTelemetry.aoeAvoidUnitTime / denom;
            float aoeMag = 0.0f;
            if (sim.actionTelemetry.aoeAvoidMagnitudeSamples > 0)
            {
                aoeMag = sim.actionTelemetry.aoeAvoidMagnitudeAccum /
                         static_cast<float>(sim.actionTelemetry.aoeAvoidMagnitudeSamples);
            }
            float hazardAvg = 0.0f;
            if (sim.actionTelemetry.hazardSamples > 0)
            {
                hazardAvg = sim.actionTelemetry.hazardCountAccum /
                            static_cast<float>(sim.actionTelemetry.hazardSamples);
            }
            payload["ai.actions.cling_ratio"] = formatRatio(clingRatio);
            payload["ai.actions.kaiten_ratio"] = formatRatio(kaitenRatio);
            payload["ai.actions.run_ratio"] = formatRatio(runRatio);
            payload["ai.aoe.avoid_ratio"] = formatRatio(aoeRatio);
            payload["ai.aoe.avoid_mag"] = formatRatio(aoeMag);
            payload["ai.hazard.active_avg"] = formatRatio(hazardAvg);
            if (context.telemetry)
            {
                context.telemetry->recordEvent("ai.actions", payload);
            }

            std::sort(ratios.begin(), ratios.end(), [](const auto &lhs, const auto &rhs) {
                return lhs.first > rhs.first;
            });
            std::string summary = "行動分布 ";
            const int topCount = 3;
            int appended = 0;
            for (const auto &entry : ratios)
            {
                if (entry.first <= 0.0f)
                {
                    continue;
                }
                if (appended >= topCount)
                {
                    break;
                }
                if (appended > 0)
                {
                    summary += " / ";
                }
                summary += chibiActionLabel(entry.second);
                summary += ":";
                summary += formatRatio(entry.first * 100.0f);
                summary += "%";
                ++appended;
            }
            summary += " Panic:";
            summary += formatRatio(panicRatio * 100.0f);
            summary += "%";
            summary += " | 集団R:";
            summary += formatRatio(avgRadius);
            summary += "px";
            summary += " (>96 ";
            summary += formatRatio(exceedRatio * 100.0f);
            summary += "%) Tokkou:";
            summary += std::to_string(sim.actionTelemetry.tokkouActive);
            summary += "/";
            summary += std::to_string(sim.actionTelemetry.tokkouPeak);
            summary += " AoE:";
            summary += formatRatio(aoeRatio * 100.0f);
            summary += "% Hazards:";
            summary += formatRatio(hazardAvg);
            sim.pushTelemetry(summary);
            auto joinUnitList = [](const std::vector<int> &indices) -> std::string {
                if (indices.empty())
                {
                    return {};
                }
                std::string joined;
                joined.reserve(indices.size() * 4);
                for (std::size_t idx = 0; idx < indices.size(); ++idx)
                {
                    if (idx > 0)
                    {
                        joined.push_back(',');
                    }
                    joined += std::to_string(indices[idx]);
                }
                return joined;
            };
            auto joinPanicList = [](const std::vector<PanicRecord> &records) -> std::string {
                if (records.empty())
                {
                    return {};
                }
                std::string joined;
                joined.reserve(records.size() * 8);
                for (std::size_t idx = 0; idx < records.size(); ++idx)
                {
                    if (idx > 0)
                    {
                        joined.push_back(',');
                    }
                    joined += std::to_string(records[idx].index);
                    joined.push_back(':');
                    joined += records[idx].state ? records[idx].state : "";
                    joined.push_back(':');
                    joined += formatRatio(records[idx].timer);
                }
                return joined;
            };
            const std::string tokkouList = joinUnitList(tokkouUnitIds);
            const std::string clingList = joinUnitList(clingUnitIds);
            const std::string kaitenList = joinUnitList(kaitenUnitIds);
            const std::string runList = joinUnitList(runUnitIds);
            const std::string panicList = joinPanicList(panicRecords);
            sim.appendAiTelemetryCsv(
                ratioValues, panicRatio, denom, avgRadius, exceedRatio, sim.actionTelemetry.tokkouActive,
                sim.actionTelemetry.tokkouPeak, clingRatio, kaitenRatio, runRatio, hazardAvg, aoeRatio, aoeMag,
                tokkouList, clingList, kaitenList, runList, panicList);
            sim.hud.aiDebug.available = true;
            sim.hud.aiDebug.panicRatio = panicRatio;
            sim.hud.aiDebug.avgRadius = avgRadius;
            sim.hud.aiDebug.exceedRatio = exceedRatio;
            sim.hud.aiDebug.clingRatio = clingRatio;
            sim.hud.aiDebug.kaitenRatio = kaitenRatio;
            sim.hud.aiDebug.runRatio = runRatio;
            sim.hud.aiDebug.hazardAverage = hazardAvg;
            sim.hud.aiDebug.aoeRatio = aoeRatio;
            sim.hud.aiDebug.aoeMagnitude = aoeMag;
            for (std::size_t i = 0; i < sim.hud.aiDebug.actionRatios.size() && i < ratioValues.size(); ++i)
            {
                sim.hud.aiDebug.actionRatios[i] = ratioValues[i];
            }
            sim.hud.aiDebug.ratioHistory.push_back(ratioValues);
            if (sim.hud.aiDebug.ratioHistory.size() > 24)
            {
                sim.hud.aiDebug.ratioHistory.pop_front();
            }
            sim.hud.aiDebug.history.push_back(summary);
            if (sim.hud.aiDebug.history.size() > 6)
            {
                sim.hud.aiDebug.history.erase(sim.hud.aiDebug.history.begin());
            }
            std::sort(panicRecords.begin(), panicRecords.end(), [](const PanicRecord &lhs, const PanicRecord &rhs) {
                return lhs.timer > rhs.timer;
            });
            const std::size_t maxPanic = 6;
            sim.hud.aiDebug.panicEntries.clear();
            sim.hud.aiDebug.panicEntries.reserve(std::min(maxPanic, panicRecords.size()));
            for (std::size_t idx = 0; idx < panicRecords.size() && idx < maxPanic; ++idx)
            {
                HUDState::AiDebugInfo::PanicEntry entry;
                entry.unitIndex = panicRecords[idx].index;
                entry.state = panicRecords[idx].state ? panicRecords[idx].state : "";
                entry.timer = panicRecords[idx].timer;
                sim.hud.aiDebug.panicEntries.push_back(std::move(entry));
            }
        }
        sim.actionTelemetry.unitTime.fill(0.0f);
        sim.actionTelemetry.panicUnitTime = 0.0f;
        sim.actionTelemetry.totalUnitTime = 0.0f;
        sim.actionTelemetry.timer = 0.0f;
        sim.actionTelemetry.boidRadiusAccum = 0.0f;
        sim.actionTelemetry.boidRadiusSamples = 0;
        sim.actionTelemetry.boidRadiusExceed = 0;
        sim.actionTelemetry.tokkouPeak = sim.actionTelemetry.tokkouActive;
        sim.actionTelemetry.clingUnitTime = 0.0f;
        sim.actionTelemetry.kaitenUnitTime = 0.0f;
        sim.actionTelemetry.runUnitTime = 0.0f;
        sim.actionTelemetry.hazardCountAccum = 0.0f;
        sim.actionTelemetry.hazardSamples = 0;
        sim.actionTelemetry.aoeAvoidUnitTime = 0.0f;
        sim.actionTelemetry.aoeAvoidMagnitudeAccum = 0.0f;
        sim.actionTelemetry.aoeAvoidMagnitudeSamples = 0;
    }
}

} // namespace world::systems
