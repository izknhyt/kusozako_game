#include "world/systems/CommanderInputSystem.h"

#include "core/Vec2.h"
#include "input/ActionBuffer.h"
#include "world/LegacySimulation.h"

#include <cmath>

namespace world::systems
{

void CommanderInputSystem::update(float, SystemContext &context)
{
    CommanderUnit &commander = context.commander;
    commander.moveIntent = {0.0f, 0.0f};
    commander.hasMoveIntent = false;

    if (!commander.alive)
    {
        return;
    }

    Vec2 input = context.actions.commanderMoveVector();
    const float lenSq = input.x * input.x + input.y * input.y;
    if (lenSq <= 0.0001f)
    {
        return;
    }

    const float invLen = 1.0f / std::sqrt(lenSq);
    commander.moveIntent = {input.x * invLen, input.y * invLen};
    commander.hasMoveIntent = true;
}

} // namespace world::systems

