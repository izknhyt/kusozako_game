#include "world/systems/MoraleSystem.h"

#include "config/AppConfig.h"
#include "events/EventBus.h"
#include "events/MoraleEvents.h"
#include "world/LegacySimulation.h"
#include "world/LegacyTypes.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <vector>

namespace world::systems
{
namespace
{
MoraleModifiers clampModifiers(const MoraleModifiers &mods)
{
    MoraleModifiers clamped = mods;
    if (!std::isfinite(clamped.speed) || clamped.speed <= 0.0f)
    {
        clamped.speed = 0.01f;
    }
    if (!std::isfinite(clamped.accuracy) || clamped.accuracy <= 0.0f)
    {
        clamped.accuracy = 0.01f;
    }
    if (!std::isfinite(clamped.defense) || clamped.defense <= 0.0f)
    {
        clamped.defense = 0.01f;
    }
    return clamped;
}

const MoraleStateConfig *lookupStateConfig(const MoraleConfig &config, MoraleState state)
{
    switch (state)
    {
    case MoraleState::Panic:
        return &config.panic;
    case MoraleState::Mesomeso:
        return &config.mesomeso;
    case MoraleState::Recovering:
        return &config.recovering;
    case MoraleState::Shielded:
        return &config.shielded;
    default:
        break;
    }
    return nullptr;
}

} // namespace

void MoraleSystem::update(float dt, SystemContext &context)
{
    LegacySimulation &sim = context.simulation;
    HUDState &hud = context.hud;

    auto tickTimer = [dt](float &timer) {
        if (timer <= 0.0f)
        {
            return false;
        }
        const float before = timer;
        timer = std::max(0.0f, timer - dt);
        return before != timer;
    };

    bool requestSync = false;
    if (tickTimer(hud.telemetryTimer))
    {
        requestSync = true;
    }
    if (tickTimer(hud.resultTimer))
    {
        requestSync = true;
    }

    if (context.orderActive)
    {
        if (tickTimer(context.orderTimer))
        {
            requestSync = true;
        }
        if (context.orderTimer <= 0.0f)
        {
            context.orderActive = false;
            sim.stance = sim.defaultStance;
            requestSync = true;
        }
    }

    if (sim.result != GameResult::Playing)
    {
        if (requestSync)
        {
            context.requestComponentSync();
        }
        return;
    }

    if (sim.missionMode == MissionMode::None)
    {
        const bool wavesFinished = context.waveScriptComplete && context.spawnerIdle;
        const bool noEnemies = sim.enemies.empty();
        if (wavesFinished && noEnemies && context.timeSinceLastEnemySpawn >= sim.config.victory_grace)
        {
            sim.setResult(GameResult::Victory, "Victory");
            requestSync = true;
        }
    }

    std::vector<Unit> &yunas = context.yunaUnits;
    if (m_lastStates.size() != yunas.size())
    {
        m_lastStates.assign(yunas.size(), MoraleState::Stable);
        moraleChanged = true;
    }
    if (m_knownUnits > yunas.size())
    {
        m_knownUnits = yunas.size();
        moraleChanged = true;
    }

    MoraleUpdateEvent moraleEvent;
    bool moraleChanged = false;

    const MoraleConfig &moraleCfg = sim.config.morale;
    auto assignModifiers = [&](Unit &unit, const MoraleModifiers &mods) {
        MoraleModifiers clamped = clampModifiers(mods);
        unit.moraleSpeedMultiplier = clamped.speed;
        unit.moraleAccuracyMultiplier = clamped.accuracy;
        unit.moraleDefenseMultiplier = clamped.defense;
    };

    auto setState = [&](Unit &unit, MoraleState state, float overrideDuration, bool comfortShield) {
        bool changed = unit.moraleState != state || unit.moraleComfortShield != comfortShield;
        unit.moraleState = state;
        unit.moraleComfortShield = comfortShield;
        if (comfortShield)
        {
            unit.moraleImmunityTimer = 0.0f;
            unit.moraleBarrierActive = false;
        }

        MoraleModifiers modifiers = moraleCfg.stable;
        const MoraleStateConfig *configState = lookupStateConfig(moraleCfg, state);
        float desiredDuration = overrideDuration;

        switch (state)
        {
        case MoraleState::Stable:
            desiredDuration = overrideDuration >= 0.0f ? overrideDuration : 0.0f;
            modifiers = moraleCfg.stable;
            break;
        case MoraleState::LeaderDown:
            desiredDuration = overrideDuration >= 0.0f ? overrideDuration : moraleCfg.leaderDownWindow;
            modifiers = moraleCfg.leaderDown;
            break;
        default:
            if (configState)
            {
                if (desiredDuration < 0.0f)
                {
                    desiredDuration = configState->duration;
                }
                modifiers = configState->modifiers;
            }
            break;
        }

        if (desiredDuration < 0.0f)
        {
            desiredDuration = 0.0f;
        }

        if (state == MoraleState::Shielded)
        {
            if (comfortShield)
            {
                desiredDuration = 0.0f;
            }
            else if (moraleCfg.reviveBarrier > 0.0f)
            {
                unit.moraleImmunityTimer = std::max(unit.moraleImmunityTimer, moraleCfg.reviveBarrier);
                unit.moraleBarrierActive = true;
                if (desiredDuration <= 0.0f)
                {
                    desiredDuration = std::max(moraleCfg.reviveBarrier, moraleCfg.shielded.duration);
                }
            }
        }

        if (std::fabs(unit.moraleTimer - desiredDuration) > 0.0001f)
        {
            unit.moraleTimer = desiredDuration;
            changed = true;
        }
        else if (state == MoraleState::Shielded && comfortShield)
        {
            unit.moraleTimer = 0.0f;
        }

        assignModifiers(unit, modifiers);
        return changed;
    };

    const bool commanderAlive = context.commander.alive;
    std::string telemetryMessage;

    if (!commanderAlive && m_commanderAlive)
    {
        m_commanderAlive = false;
        m_leaderDownTimer = moraleCfg.leaderDownWindow;
        m_commanderBarrierTimer = 0.0f;
        m_applyReviveBarrier = false;
        m_announcedPanic = false;
        m_announcedRecovery = false;
        moraleChanged = true;
        if (telemetryMessage.empty())
        {
            telemetryMessage = "Commander down! Morale wavering.";
        }
        m_announcedLeaderDown = true;
    }
    else if (commanderAlive && !m_commanderAlive)
    {
        m_commanderAlive = true;
        m_leaderDownTimer = 0.0f;
        m_commanderBarrierTimer = moraleCfg.reviveBarrier;
        m_applyReviveBarrier = moraleCfg.reviveBarrier > 0.0f;
        m_announcedLeaderDown = false;
        m_announcedPanic = false;
        m_announcedRecovery = false;
        moraleChanged = true;
        if (telemetryMessage.empty())
        {
            telemetryMessage = "Commander revived! Rally back.";
        }
    }

    if (m_leaderDownTimer > 0.0f)
    {
        float before = m_leaderDownTimer;
        m_leaderDownTimer = std::max(0.0f, m_leaderDownTimer - dt);
        if (before != m_leaderDownTimer)
        {
            moraleChanged = true;
        }
    }
    if (m_commanderBarrierTimer > 0.0f)
    {
        float before = m_commanderBarrierTimer;
        m_commanderBarrierTimer = std::max(0.0f, m_commanderBarrierTimer - dt);
        if (before != m_commanderBarrierTimer)
        {
            moraleChanged = true;
        }
    }

    MoraleState commanderState = MoraleState::Stable;
    if (!commanderAlive)
    {
        commanderState = MoraleState::LeaderDown;
    }
    else if (m_commanderBarrierTimer > 0.0f)
    {
        commanderState = MoraleState::Shielded;
    }

    if (commanderState != MoraleState::Stable)
    {
        moraleEvent.icons.push_back(MoraleHudIcon{true, 0u, commanderState});
    }
    if (commanderState != m_lastCommanderState)
    {
        moraleChanged = true;
    }

    const float comfortRadiusSq = moraleCfg.comfortZoneRadius > 0.0f
                                      ? moraleCfg.comfortZoneRadius * moraleCfg.comfortZoneRadius
                                      : -1.0f;

    float totalSpeed = 0.0f;
    float totalAccuracy = 0.0f;
    float totalDefense = 0.0f;
    std::size_t panicCount = 0;
    std::size_t mesomesoCount = 0;

    for (std::size_t i = 0; i < yunas.size(); ++i)
    {
        Unit &unit = yunas[i];
        const bool isNewUnit = i >= m_knownUnits;
        if (isNewUnit)
        {
            sim.resetUnitMorale(unit);
            moraleChanged = true;
        }

        if (unit.moraleImmunityTimer > 0.0f)
        {
            float before = unit.moraleImmunityTimer;
            unit.moraleImmunityTimer = std::max(0.0f, unit.moraleImmunityTimer - dt);
            if (before != unit.moraleImmunityTimer)
            {
                moraleChanged = true;
            }
            if (unit.moraleImmunityTimer <= 0.0f)
            {
                unit.moraleBarrierActive = false;
            }
        }

        if (m_applyReviveBarrier && moraleCfg.reviveBarrier > 0.0f && !unit.moraleComfortShield)
        {
            if (setState(unit, MoraleState::Shielded,
                         std::max(moraleCfg.reviveBarrier, moraleCfg.shielded.duration), false))
            {
                moraleChanged = true;
            }
        }

        bool inComfort = false;
        if (comfortRadiusSq >= 0.0f)
        {
            const float distSq = lengthSq(unit.pos - sim.basePos);
            inComfort = distSq <= comfortRadiusSq;
        }

        if (inComfort)
        {
            if (setState(unit, MoraleState::Shielded, 0.0f, true))
            {
                moraleChanged = true;
            }
        }
        else if (unit.moraleComfortShield)
        {
            unit.moraleComfortShield = false;
            moraleChanged = true;
        }

        const bool immune = unit.moraleComfortShield || unit.moraleImmunityTimer > 0.0f;

        if (!commanderAlive && !immune)
        {
            if (m_leaderDownTimer > 0.0f)
            {
                if (unit.moraleState != MoraleState::LeaderDown)
                {
                    if (setState(unit, MoraleState::LeaderDown, m_leaderDownTimer, false))
                    {
                        moraleChanged = true;
                    }
                }
            }
            else
            {
                MoraleState target = unit.effectiveFollower ? MoraleState::Panic : MoraleState::Mesomeso;
                if (unit.moraleState != target)
                {
                    if (setState(unit, target, -1.0f, false))
                    {
                        moraleChanged = true;
                    }
                }
            }
        }
        else if (commanderAlive && !immune)
        {
            if (unit.moraleState == MoraleState::LeaderDown)
            {
                if (setState(unit, MoraleState::Recovering, moraleCfg.recovering.duration, false))
                {
                    moraleChanged = true;
                }
            }
        }

        if (unit.moraleTimer > 0.0f)
        {
            float before = unit.moraleTimer;
            unit.moraleTimer = std::max(0.0f, unit.moraleTimer - dt);
            if (before != unit.moraleTimer)
            {
                moraleChanged = true;
            }
        }

        if (unit.moraleTimer <= 0.0f && !immune)
        {
            switch (unit.moraleState)
            {
            case MoraleState::LeaderDown:
                if (!commanderAlive)
                {
                    {
                        MoraleState target = unit.effectiveFollower ? MoraleState::Panic : MoraleState::Mesomeso;
                        if (setState(unit, target, -1.0f, false))
                        {
                            moraleChanged = true;
                        }
                    }
                }
                else
                {
                    if (setState(unit, MoraleState::Recovering, moraleCfg.recovering.duration, false))
                    {
                        moraleChanged = true;
                    }
                }
                break;
            case MoraleState::Panic:
            case MoraleState::Mesomeso:
                if (setState(unit, MoraleState::Recovering, moraleCfg.recovering.duration, false))
                {
                    moraleChanged = true;
                }
                break;
            case MoraleState::Recovering:
                if (setState(unit, MoraleState::Stable, 0.0f, false))
                {
                    moraleChanged = true;
                }
                break;
            case MoraleState::Shielded:
                if (!commanderAlive)
                {
                    if (m_leaderDownTimer > 0.0f)
                    {
                        if (setState(unit, MoraleState::LeaderDown, m_leaderDownTimer, false))
                        {
                            moraleChanged = true;
                        }
                    }
                    else
                    {
                        MoraleState target = unit.effectiveFollower ? MoraleState::Panic : MoraleState::Mesomeso;
                        if (setState(unit, target, -1.0f, false))
                        {
                            moraleChanged = true;
                        }
                    }
                }
                else
                {
                    if (setState(unit, MoraleState::Recovering, moraleCfg.recovering.duration, false))
                    {
                        moraleChanged = true;
                    }
                }
                break;
            default:
                break;
            }
        }

        totalSpeed += unit.moraleSpeedMultiplier;
        totalAccuracy += unit.moraleAccuracyMultiplier;
        totalDefense += unit.moraleDefenseMultiplier;

        if (unit.moraleState == MoraleState::Panic)
        {
            ++panicCount;
        }
        else if (unit.moraleState == MoraleState::Mesomeso)
        {
            ++mesomesoCount;
        }

        if (unit.moraleState != MoraleState::Stable)
        {
            moraleEvent.icons.push_back(MoraleHudIcon{false, i, unit.moraleState});
        }

        if (m_lastStates[i] != unit.moraleState)
        {
            moraleChanged = true;
            m_lastStates[i] = unit.moraleState;
        }
    }

    moraleEvent.panicCount = panicCount;
    moraleEvent.mesomesoCount = mesomesoCount;

    if (yunas.empty())
    {
        sim.moraleSummary.averageSpeedMul = 1.0f;
        sim.moraleSummary.averageAccuracyMul = 1.0f;
        sim.moraleSummary.averageDefenseMul = 1.0f;
    }
    else
    {
        const float invCount = 1.0f / static_cast<float>(yunas.size());
        sim.moraleSummary.averageSpeedMul = totalSpeed * invCount;
        sim.moraleSummary.averageAccuracyMul = totalAccuracy * invCount;
        sim.moraleSummary.averageDefenseMul = totalDefense * invCount;
    }
    sim.moraleSummary.panicCount = panicCount;
    sim.moraleSummary.mesomesoCount = mesomesoCount;
    sim.moraleSummary.commanderState = commanderState;
    sim.moraleSummary.rallySuppressed = panicCount > 0;

    if (!commanderAlive)
    {
        m_announcedRecovery = false;
    }

    if (panicCount > 0)
    {
        if (!m_announcedPanic && telemetryMessage.empty())
        {
            telemetryMessage = "Allies are panicking!";
        }
        m_announcedPanic = true;
        m_announcedRecovery = false;
    }
    else
    {
        m_announcedPanic = false;
    }

    if (commanderAlive && panicCount == 0 && mesomesoCount == 0 && !m_announcedRecovery)
    {
        if (telemetryMessage.empty())
        {
            telemetryMessage = "Morale stabilized.";
        }
        m_announcedRecovery = true;
    }
    else if (panicCount > 0 || mesomesoCount > 0)
    {
        m_announcedRecovery = false;
    }

    moraleEvent.telemetry = telemetryMessage;

    m_knownUnits = yunas.size();
    m_lastCommanderState = commanderState;
    m_applyReviveBarrier = false;

    const float hudLeaderTimer = std::max(m_leaderDownTimer, 0.0f);
    const float hudBarrierTimer = std::max(m_commanderBarrierTimer, 0.0f);
    const bool statusChanged = std::fabs(m_lastHudLeaderDownTimer - hudLeaderTimer) > 0.05f ||
                               std::fabs(m_lastHudBarrierTimer - hudBarrierTimer) > 0.05f ||
                               m_lastHudCommanderState != commanderState ||
                               m_lastHudPanic != panicCount ||
                               m_lastHudMesomeso != mesomesoCount;
    if (statusChanged)
    {
        m_lastHudLeaderDownTimer = hudLeaderTimer;
        m_lastHudBarrierTimer = hudBarrierTimer;
        m_lastHudCommanderState = commanderState;
        m_lastHudPanic = panicCount;
        m_lastHudMesomeso = mesomesoCount;

        MoraleStatusEvent statusEvent;
        statusEvent.commanderState = commanderState;
        statusEvent.leaderDownTimer = hudLeaderTimer;
        statusEvent.commanderBarrierTimer = hudBarrierTimer;
        statusEvent.panicCount = panicCount;
        statusEvent.mesomesoCount = mesomesoCount;

        if (context.eventBus)
        {
            EventContext statusCtx;
            statusCtx.payload = statusEvent;
            context.eventBus->dispatch(MoraleStatusEventName, statusCtx);
        }
        if (context.telemetry)
        {
            std::ostringstream leaderStream;
            leaderStream << std::fixed << std::setprecision(2) << hudLeaderTimer;
            std::ostringstream barrierStream;
            barrierStream << std::fixed << std::setprecision(2) << hudBarrierTimer;
            TelemetrySink::Payload payload{{"commander_state", std::string(moraleStateLabel(commanderState))},
                                           {"panic", std::to_string(panicCount)},
                                           {"mesomeso", std::to_string(mesomesoCount)},
                                           {"leader_down_s", leaderStream.str()},
                                           {"barrier_s", barrierStream.str()}};
            context.telemetry->recordEvent("hud.morale_status", payload);
        }
    }

    if (moraleChanged)
    {
        requestSync = true;
    }

    if (context.eventBus && (moraleChanged || !moraleEvent.icons.empty() || !moraleEvent.telemetry.empty()))
    {
        EventContext eventContext;
        eventContext.payload = std::move(moraleEvent);
        context.eventBus->dispatch(MoraleUpdateEventName, eventContext);
    }

    if (requestSync)
    {
        context.requestComponentSync();
    }
}

} // namespace world::systems

