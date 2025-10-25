#include "world/systems/CombatSystem.h"

#include <algorithm>
#include <cmath>
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

    const float defenseMultiplier =
        sim.formationAlignTimer > 0.0f ? std::max(sim.formationDefenseMul, 0.01f) : 1.0f;
    const float formationDamageScale = 1.0f / defenseMultiplier;

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
        for (EnemyUnit &enemy : enemies)
        {
            const float combined = yuna.radius + enemy.radius;
            if (lengthSq(yuna.pos - enemy.pos) <= combined * combined)
            {
                enemy.hp -= sim.yunaStats.dps * dt;
                yunaDamage[i] += enemy.dpsUnit * dt * formationDamageScale;
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

    context.requestComponentSync();
}

} // namespace world::systems

