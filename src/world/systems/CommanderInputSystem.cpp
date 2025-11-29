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
        // Auto-pursue target if set
        const int targetIndex = commander.attackTargetIndex;
        if (targetIndex >= 0 && targetIndex < static_cast<int>(context.enemyUnits.size()))
        {
            const EnemyUnit &enemy = context.enemyUnits[targetIndex];
            if (enemy.hp > 0.0f)
            {
                Vec2 to = enemy.pos - commander.pos;
                const float distSq = lengthSq(to);
                if (distSq > 1.0f)
                {
                    const float invLen = 1.0f / std::sqrt(distSq);
                    commander.moveIntent = to * invLen;
                    commander.hasMoveIntent = true;
                }
            }
        }
        else
        {
            commander.attackTargetIndex = -1;
        }
        return;
    }

    // Movement input cancels current attack swing/lock so movement responds immediately
    commander.attackLockTimer = 0.0f;
    commander.attackSwingTimer = 0.0f;
    commander.attackSwingDuration = 0.0f;
    commander.attackTargetIndex = -1;

    const float invLen = 1.0f / std::sqrt(lenSq);
    commander.moveIntent = {input.x * invLen, input.y * invLen};
    commander.hasMoveIntent = true;
}

} // namespace world::systems
