#include "world/systems/MovementSystem.h"

#include "core/Vec2.h"
#include "world/LegacySimulation.h"

namespace world::systems
{

void MovementSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    CommanderUnit &commander = context.commander;
    bool anyMovement = false;

    if (commander.alive && commander.hasMoveIntent)
    {
        const float speedPx = sim.commanderStats.speed_u_s * sim.config.pixels_per_unit;
        commander.pos += commander.moveIntent * (speedPx * dt);
        sim.clampToWorld(commander.pos, commander.radius);
        anyMovement = true;
    }

    commander.moveIntent = {0.0f, 0.0f};
    commander.hasMoveIntent = false;

    auto &yunas = context.yunaUnits;
    for (Unit &yuna : yunas)
    {
        if (yuna.hasDesiredVelocity)
        {
            yuna.pos += yuna.desiredVelocity * dt;
            sim.clampToWorld(yuna.pos, yuna.radius);
            anyMovement = true;
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

