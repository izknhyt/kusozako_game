#include "world/systems/MovementSystem.h"

#include "core/Vec2.h"
#include "world/LegacySimulation.h"

#include <algorithm>
#include <cmath>

namespace world::systems
{

void MovementSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    bool anyMovement = false;

    if (commander.attackLockTimer > 0.0f)
    {
        commander.attackLockTimer = std::max(0.0f, commander.attackLockTimer - dt);
        // Failsafe: 付近に敵がいないのにロックが残っていたら解除
        if (commander.attackLockTimer > 0.0f)
        {
            const float clearRadius = sim.config.pixels_per_unit * 4.0f; // ~64px 以内
            const float clearSq = clearRadius * clearRadius;
            bool enemyNearby = false;
            for (const EnemyUnit &enemy : context.enemyUnits)
            {
                if (enemy.hp <= 0.0f)
                {
                    continue;
                }
                if (lengthSq(enemy.pos - commander.pos) <= clearSq)
                {
                    enemyNearby = true;
                    break;
                }
            }
            if (!enemyNearby)
            {
                commander.attackLockTimer = 0.0f;
            }
        }
    }

    const bool commanderLocked = commander.attackLockTimer > 0.0f;

    if (commander.alive && commander.hasMoveIntent && !commanderLocked)
    {
        const float speedPx = sim.commanderStats.speed_u_s * sim.config.pixels_per_unit;
        Vec2 vel = commander.moveIntent * speedPx;
        commander.pos += vel * dt;
        commander.lastVelocity = vel;
        const float intentLenSq = lengthSq(commander.moveIntent);
        if (intentLenSq > 0.0001f)
        {
            Vec2 dir = commander.moveIntent / std::sqrt(intentLenSq);
            // Smooth追従用に進行方向を残す
            const float blend = 0.35f;
            Vec2 blended = commander.lastMoveDir * (1.0f - blend) + dir * blend;
            const float blendedLen = std::sqrt(std::max(lengthSq(blended), 0.0f));
            commander.lastMoveDir = blendedLen > 0.0001f ? blended / blendedLen : dir;
        }
        sim.clampToWorld(commander.pos, commander.radius);
        anyMovement = true;
    }
    else
    {
        commander.lastVelocity = {0.0f, 0.0f};
    }

    commander.moveIntent = {0.0f, 0.0f};
    commander.hasMoveIntent = false;

    auto &yunas = context.yunaUnits;
    for (Unit &yuna : yunas)
    {
        if (yuna.attackLockTimer > 0.0f)
        {
            yuna.attackLockTimer = std::max(0.0f, yuna.attackLockTimer - dt);
        }
        const bool locked = yuna.attackLockTimer > 0.0f;
        if (yuna.hasDesiredVelocity && !locked)
        {
            yuna.pos += yuna.desiredVelocity * dt;
            yuna.lastVelocity = yuna.desiredVelocity;
            if (std::abs(yuna.desiredVelocity.x) > 0.01f)
            {
                yuna.facingX = yuna.desiredVelocity.x > 0.0f ? 1.0f : -1.0f;
            }
            sim.clampToWorld(yuna.pos, yuna.radius);
            anyMovement = true;
        }
        else
        {
            yuna.lastVelocity = {0.0f, 0.0f};
        }
        yuna.desiredVelocity = {0.0f, 0.0f};
        yuna.hasDesiredVelocity = false;
    }

    if (anyMovement)
    {
        context.requestComponentSync();
    }
}

} // namespace world::systems
