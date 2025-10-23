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

    if (hud.telemetryTimer > 0.0f)
    {
        hud.telemetryTimer = std::max(0.0f, hud.telemetryTimer - dt);
    }
    if (hud.resultTimer > 0.0f)
    {
        hud.resultTimer = std::max(0.0f, hud.resultTimer - dt);
    }

    if (context.orderActive)
    {
        if (context.orderTimer > 0.0f)
        {
            context.orderTimer = std::max(0.0f, context.orderTimer - dt);
        }
        if (context.orderTimer <= 0.0f)
        {
            context.orderActive = false;
            sim.stance = sim.defaultStance;
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
    }
}

} // namespace world::systems

