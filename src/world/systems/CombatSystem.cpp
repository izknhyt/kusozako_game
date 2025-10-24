#include "world/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace world::systems
{

void CombatSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    auto &yunas = context.yunaUnits;
    auto &enemies = context.enemyUnits;
    auto &walls = context.wallSegments;
    auto &gates = context.gates;

    const float yunaSpeedPx = sim.yunaStats.speed_u_s * sim.config.pixels_per_unit;
    const float followerSnapDistSq = 16.0f;

    const std::size_t totalFollowers = std::count_if(yunas.begin(), yunas.end(), [](const Unit &unit) {
        return unit.effectiveFollower;
    });
    const std::size_t totalDefenders = yunas.size() > totalFollowers ? yunas.size() - totalFollowers : 0;
    const std::size_t safeDefenders = totalDefenders > 0 ? totalDefenders : 1;
    std::size_t defendIndex = 0;
    std::size_t supportIndex = 0;

    auto nearestEnemy = [&enemies](const Vec2 &pos) -> EnemyUnit * {
        EnemyUnit *best = nullptr;
        float bestDist = std::numeric_limits<float>::max();
        for (EnemyUnit &enemy : enemies)
        {
            const float dist = lengthSq(enemy.pos - pos);
            if (dist < bestDist)
            {
                bestDist = dist;
                best = &enemy;
            }
        }
        return best;
    };

    std::vector<Vec2> raidTargets = sim.collectRaidTargets();

    for (Unit &yuna : yunas)
    {
        Vec2 temperamentVelocity = sim.computeTemperamentVelocity(yuna, dt, yunaSpeedPx, nearestEnemy, raidTargets);
        Vec2 velocity{0.0f, 0.0f};
        const bool panicActive = yuna.temperament.panicTimer > 0.0f;

        if (panicActive)
        {
            velocity = temperamentVelocity;
        }
        else if (yuna.effectiveFollower && commander.alive)
        {
            Vec2 desiredPos = commander.pos + yuna.formationOffset;
            Vec2 toTarget = desiredPos - yuna.pos;
            if (lengthSq(toTarget) > followerSnapDistSq)
            {
                velocity = normalize(toTarget) * yunaSpeedPx;
            }
            else
            {
                velocity = temperamentVelocity;
            }
        }
        else if (context.orderActive)
        {
            switch (sim.stance)
            {
            case ArmyStance::RushNearest:
            {
                if (EnemyUnit *target = nearestEnemy(yuna.pos))
                {
                    velocity = normalize(target->pos - yuna.pos) * yunaSpeedPx;
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
                velocity = normalize(target - yuna.pos) * yunaSpeedPx;
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
                        Vec2 ringTarget{sim.basePos.x + std::cos(angle) * 72.0f, sim.basePos.y + std::sin(angle) * 48.0f};
                        ++supportIndex;
                        velocity = normalize(ringTarget - yuna.pos) * yunaSpeedPx;
                    }
                    else
                    {
                        velocity = normalize(target->pos - yuna.pos) * yunaSpeedPx;
                    }
                }
                else
                {
                    const float angle = 2.0f * 3.14159265358979323846f *
                                        (static_cast<float>(defendIndex) / static_cast<float>(safeDefenders));
                    Vec2 ringTarget{sim.basePos.x + std::cos(angle) * 120.0f,
                                    sim.basePos.y + std::sin(angle) * 80.0f};
                    ++defendIndex;
                    velocity = normalize(ringTarget - yuna.pos) * yunaSpeedPx;
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
            yuna.pos += velocity * dt;
            sim.clampToWorld(yuna.pos, yuna.radius);
        }
    }

    for (EnemyUnit &enemy : enemies)
    {
        Vec2 target = sim.basePos;
        if (enemy.type == EnemyArchetype::Wallbreaker)
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

    for (EnemyUnit &enemy : enemies)
    {
        for (WallSegment &wall : walls)
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

    std::vector<float> yunaDamage(yunas.size(), 0.0f);
    float commanderDamage = 0.0f;

    if (commander.alive)
    {
        for (EnemyUnit &enemy : enemies)
        {
            const float combined = commander.radius + enemy.radius;
            if (lengthSq(commander.pos - enemy.pos) <= combined * combined)
            {
                enemy.hp -= sim.commanderStats.dps * dt;
                if (context.commanderInvulnTimer <= 0.0f)
                {
                    commanderDamage += enemy.dpsUnit * dt;
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
        for (EnemyUnit &enemy : enemies)
        {
            const float combined = yuna.radius + enemy.radius;
            if (lengthSq(yuna.pos - enemy.pos) <= combined * combined)
            {
                enemy.hp -= sim.yunaStats.dps * dt;
                yunaDamage[i] += enemy.dpsUnit * dt;
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
                gate.hp = std::max(0.0f, gate.hp - sim.yunaStats.dps * dt);
                if (gate.hp <= 0.0f)
                {
                    sim.destroyGate(gate);
                }
            }
        }
    }

    enemies.erase(std::remove_if(enemies.begin(), enemies.end(), [](const EnemyUnit &e) {
        return e.hp <= 0.0f;
    }), enemies.end());

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
    for (EnemyUnit &enemy : enemies)
    {
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

    walls.erase(std::remove_if(walls.begin(), walls.end(), [](const WallSegment &wall) {
        return wall.hp <= 0.0f;
    }), walls.end());
}

} // namespace world::systems

