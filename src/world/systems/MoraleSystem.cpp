#include "world/systems/MoraleSystem.h"

#include "config/AppConfig.h"
#include "world/LegacyTypes.h"

#include <algorithm>

namespace world::systems
{

void MoraleSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    HUDState &hud = context.hud;

    bool changed = false;

    if (hud.telemetryTimer > 0.0f)
    {
        const float before = hud.telemetryTimer;
        hud.telemetryTimer = std::max(0.0f, hud.telemetryTimer - dt);
        if (hud.telemetryTimer != before)
        {
            changed = true;
        }
    }
    if (hud.resultTimer > 0.0f)
    {
        const float before = hud.resultTimer;
        hud.resultTimer = std::max(0.0f, hud.resultTimer - dt);
        if (hud.resultTimer != before)
        {
            changed = true;
        }
    }

    if (context.orderActive)
    {
        if (context.orderTimer > 0.0f)
        {
            const float before = context.orderTimer;
            context.orderTimer = std::max(0.0f, context.orderTimer - dt);
            if (context.orderTimer != before)
            {
                changed = true;
            }
        }
        if (context.orderTimer <= 0.0f)
        {
            context.orderActive = false;
            sim.stance = sim.defaultStance;
            changed = true;
        }
    }

    if (sim.result != GameResult::Playing)
    {
        return;
    }
    if (sim.missionMode != MissionMode::None)
    {
        return;
    }

    const bool wavesFinished = context.waveScriptComplete && context.spawnerIdle;
    const bool noEnemies = sim.enemies.empty();
    if (wavesFinished && noEnemies &&
        context.timeSinceLastEnemySpawn >= sim.config.victory_grace)
    {
        sim.setResult(GameResult::Victory, "Victory");
        changed = true;
    }

    if (changed)
    {
        context.requestComponentSync();
    }
}

} // namespace world::systems

