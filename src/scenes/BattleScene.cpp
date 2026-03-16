#include "scenes/BattleScene.h"

#include "app/GameApplication.h"
#include "app/RenderUtils.h"
#include "assets/AssetManager.h"
#include "config/AppConfig.h"
#include "config/AppConfigLoader.h"
#include "events/EventBus.h"
#include "game/CampaignState.h"
#include "gfx/WorldRenderer.h"
#include "input/InputMapper.h"
#include "scenes/CampScene.h"
#include "scenes/SceneStack.h"
#include "services/ServiceLocator.h"
#include "telemetry/TelemetrySink.h"
#include "world/ComponentPool.h"
#include "world/LegacySimulation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

bool g_showChibiLabels = false;

using world::LegacySimulation;

BattleScene::BattleScene(std::shared_ptr<CampaignState> campaign, bool startPausedForCamp)
    : m_campaign(std::move(campaign)), m_debugAccessor(*this), m_startPausedForCamp(startPausedForCamp)
{
    m_showChibiLabels = g_showChibiLabels;
}

void BattleScene::onEnter(GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (m_initialized)
    {
        return;
    }

    m_screenWidth = app.windowWidth();
    m_screenHeight = app.windowHeight();
    ServiceLocator &locator = ServiceLocator::instance();
    m_telemetry = locator.getService<TelemetrySink>();
    m_eventBus = locator.getService<EventBus>();
    m_assetService = locator.getService<AssetManager>();

    if (!m_uiServiceHandle)
    {
        m_uiServiceHandle = std::shared_ptr<UiPresenter>(&m_ui, [](UiPresenter *) {});
    }
    locator.registerService<UiPresenter>(m_uiServiceHandle);

    if (!m_assetService)
    {
        std::cerr << "AssetManager service not available.\n";
        if (m_telemetry)
        {
            TelemetrySink::Payload payload{{"scene", "BattleScene"}, {"reason", "asset_manager_missing"}};
            m_telemetry->recordEvent("scene.warning", payload);
        }
        return;
    }

    applyAppConfig(app);

    if (m_startPausedForCamp)
    {
        m_pausedForCamp = true;
    }

    m_initialized = true;

    if (m_eventBus)
    {
        EventContext context;
        context.payload = std::string("battle_scene_initialized");
        m_eventBus->dispatch("battle.scene.entered", context);
    }
}

void BattleScene::onExit(GameApplication &app, SceneStack &stack)
{
    (void)app;
    (void)stack;

    if (m_debugController.active())
    {
        if (m_cursorRestoreState != SDL_QUERY)
        {
            SDL_ShowCursor(m_cursorRestoreState);
        }
        m_debugController.toggle();
    }
    m_cursorRestoreState = SDL_QUERY;
    m_showTelemetryOverlay = false;
    m_debugController.bindWorld({});
    ServiceLocator &locator = ServiceLocator::instance();

    m_ui.bindSimulation(nullptr);
    m_ui.setEventBus(nullptr);
    m_ui.setTelemetrySink(nullptr);
    m_atlas.texture.reset();
    m_tileMap.tileset.reset();
    m_hudFont.unload();
    m_debugFont.unload();
    m_initialized = false;
    if (m_eventBus)
    {
        EventContext context;
        context.payload = std::string("battle_scene_exited");
        m_eventBus->dispatch("battle.scene.exited", context);
    }
    m_assetService.reset();
    m_eventBus.reset();
    m_telemetry.reset();
    locator.unregisterService<UiPresenter>();
    m_uiServiceHandle.reset();
}

void BattleScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    (void)app;
    (void)stack;
    if (!m_initialized)
    {
        return;
    }
    if (m_resultOverlayActive)
    {
        if (event.type == SDL_KEYDOWN)
        {
            handleResultOverlayKey(event.key.keysym.sym, app, stack);
        }
        return;
    }
    if (m_pausedForCamp)
    {
        return;
    }
    if (event.type == SDL_KEYDOWN)
    {
        if (event.key.keysym.sym == SDLK_l)
        {
            m_showChibiLabels = !m_showChibiLabels;
            g_showChibiLabels = m_showChibiLabels;
            return;
        }
    }
    m_debugController.handleEvent(event);
}

void BattleScene::handleActionFrame(const ActionBuffer::Frame &frame, GameApplication &app)
{
    if (m_pausedForCamp || m_resultOverlayActive)
    {
        return;
    }
    if (m_haveProcessedSequence && frame.sequence == m_lastProcessedSequence)
    {
        return;
    }
    m_haveProcessedSequence = true;
    m_lastProcessedSequence = frame.sequence;

    LegacySimulation &sim = m_world.legacy();

    auto handleSkillSelect = [this](ActionId id) {
        const int baseIndex = static_cast<int>(ActionId::SelectSkill1);
        const int actionIndex = static_cast<int>(id);
        const int skillOffset = actionIndex - baseIndex;
        if (skillOffset >= 0)
        {
            const int hotkey = skillOffset + 1;
            m_world.selectSkillByHotkey(hotkey);
        }
    };

    for (const ActionEvent &evt : frame.events)
    {
        if (!evt.pressed && !evt.released && evt.id != ActionId::ActivateSkill)
        {
            continue;
        }

        if (evt.id == ActionId::ToggleDebugOverlay && evt.pressed)
        {
            const bool wasActive = m_debugController.active();
            if (!wasActive)
            {
                m_cursorRestoreState = SDL_ShowCursor(SDL_QUERY);
            }
            m_debugController.handleActionToggle();
            const bool nowActive = m_debugController.active();
            if (nowActive)
            {
                SDL_ShowCursor(SDL_ENABLE);
            }
            else if (wasActive && m_cursorRestoreState != SDL_QUERY)
            {
                SDL_ShowCursor(m_cursorRestoreState);
                m_cursorRestoreState = SDL_QUERY;
            }
            continue;
        }

        if (m_debugController.active())
        {
            continue;
        }

        switch (evt.id)
        {
        case ActionId::CommanderOrderRushNearest:
            m_world.issueOrder(ArmyStance::RushNearest);
            break;
        case ActionId::CommanderOrderPushForward:
            m_world.issueOrder(ArmyStance::PushForward);
            break;
        case ActionId::CommanderOrderFollowLeader:
            m_world.issueOrder(ArmyStance::FollowLeader);
            break;
        case ActionId::CommanderOrderDefendBase:
            m_world.issueOrder(ArmyStance::DefendBase);
            break;
        case ActionId::CycleFormationPrevious:
            m_world.cycleFormation(-1);
            break;
        case ActionId::CycleFormationNext:
            m_world.cycleFormation(1);
            break;
        case ActionId::ToggleDebugHud:
            m_showDebugHud = !m_showDebugHud;
            break;
        case ActionId::ReloadConfig:
#if !defined(NDEBUG)
        {
            const bool success = app.reloadConfig();
            const auto &result = app.appConfigResult();
            std::string message = success ? std::string("Config reloaded") : std::string("Config reload errors");
            if (!success && !result.errors.empty())
            {
                message += " (";
                message += std::to_string(result.errors.size());
                message += ')';
            }
            showTelemetryMessage(message);
        }
#endif
            break;
        case ActionId::DumpSpawnHistory:
#if !defined(NDEBUG)
        {
            auto dumpResult = m_world.dumpSpawnHistory();
            if (dumpResult.success)
            {
                std::string message = "Spawn history saved";
                if (!dumpResult.path.empty())
                {
                    message += ": ";
                    message += dumpResult.path.filename().string();
                }
                showTelemetryMessage(message);
            }
            else
            {
                std::string message = "Spawn history failed";
                if (!dumpResult.error.empty())
                {
                    message += " (";
                    message += dumpResult.error;
                    message += ')';
                }
                showTelemetryMessage(message);
            }
        }
#endif
            break;
        case ActionId::RestartScenario:
            if (sim.result != GameResult::Playing && m_world.canRestart())
            {
                m_world.reset();
                m_baseCameraTarget = {sim.basePos.x - m_screenWidth * 0.5f, sim.basePos.y - m_screenHeight * 0.5f};
                m_introFocus = leftmostGateWorld(sim.mapDefs);
                m_introCameraTarget = {m_introFocus.x - m_screenWidth * 0.5f,
                                       m_introFocus.y - m_screenHeight * 0.5f};
                m_camera.position = m_introCameraTarget;
                m_introTimer = m_introDuration;
                m_introActive = true;
            }
            break;
        case ActionId::SelectSkill1:
        case ActionId::SelectSkill2:
        case ActionId::SelectSkill3:
        case ActionId::SelectSkill4:
        case ActionId::SelectSkill5:
        case ActionId::SelectSkill6:
        case ActionId::SelectSkill7:
        case ActionId::SelectSkill8:
            handleSkillSelect(evt.id);
            break;
        case ActionId::FocusCommander:
            m_camera.position = {sim.commander.pos.x - m_screenWidth * 0.5f,
                                 sim.commander.pos.y - m_screenHeight * 0.5f};
            m_introActive = false;
            m_introTimer = 0.0f;
            break;
        case ActionId::FocusBase:
            m_camera.position = {sim.basePos.x - m_screenWidth * 0.5f, sim.basePos.y - m_screenHeight * 0.5f};
            m_introActive = false;
            m_introTimer = 0.0f;
            break;
        case ActionId::CommanderCastFireBall:
            if (evt.pressed)
            {
                m_world.castFireBall(120.0f, 4.0f);
            }
            break;
        case ActionId::CommanderGuardHold:
            m_world.setCommanderGuard(evt.pressed);
            break;
        case ActionId::CommanderCancelTarget:
            if (evt.pressed)
            {
                const bool hadSelection = !m_selectedChibis.empty();
                m_draggingSelection = false;
                if (hadSelection)
                {
                    m_world.clearChibiManualOrders(m_selectedChibis);
                    m_selectedChibis.clear();
                }
                else
                {
                    m_world.clearChibiManualOrders(m_selectedChibis);
                }
                m_world.commanderCancelTarget();
            }
            break;
        case ActionId::CommanderFocusTarget:
            if (evt.pressed)
            {
                m_world.commanderFocusTarget();
            }
            break;
        case ActionId::CommanderAttack:
        {
            if (!evt.pointer)
            {
                break;
            }
            if (evt.pointer->pressed)
            {
                m_draggingSelection = true;
                m_dragStart = {evt.pointer->x, evt.pointer->y};
                m_dragCurrent = m_dragStart;
                break;
            }
            if (evt.pointer->released)
            {
                if (m_draggingSelection)
                {
                    m_dragCurrent = {evt.pointer->x, evt.pointer->y};
                    const int dx = m_dragCurrent.x - m_dragStart.x;
                    const int dy = m_dragCurrent.y - m_dragStart.y;
                    const int dragThreshold = 6;
                    const bool dragged = std::abs(dx) >= dragThreshold || std::abs(dy) >= dragThreshold;
                    if (dragged)
                    {
                        SDL_Rect rect{};
                        rect.x = std::min(m_dragStart.x, m_dragCurrent.x);
                        rect.y = std::min(m_dragStart.y, m_dragCurrent.y);
                        rect.w = std::abs(dx);
                        rect.h = std::abs(dy);
                        selectChibisInRect(rect);
                        m_draggingSelection = false;
                        break;
                    }
                }

                Vec2 worldPos = screenToWorld(evt.pointer->x, evt.pointer->y, m_camera);
                pruneSelection();
                if (!m_selectedChibis.empty())
                {
                    const int enemyIdx = enemyIndexAtScreen(evt.pointer->x, evt.pointer->y);
                    if (enemyIdx >= 0)
                    {
                        m_world.commandChibiAttack(m_selectedChibis, enemyIdx);
                    }
                    else
                    {
                        m_world.commandChibiMove(m_selectedChibis, worldPos);
                    }
                }
                else
                {
                    m_world.commanderAttack(worldPos);
                }
                m_draggingSelection = false;
            }
            break;
        }
        case ActionId::ActivateSkill:
            if (evt.pointer && evt.pointer->pressed)
            {
                Vec2 worldPos = screenToWorld(evt.pointer->x, evt.pointer->y, m_camera);
                m_world.activateSelectedSkill(worldPos);
            }
            break;
        case ActionId::ToggleGameSpeed:
            cycleGameSpeed();
            break;
        case ActionId::QuitGame:
            app.requestQuit();
            break;
        default:
            break;
        }
    }

    if (m_draggingSelection && frame.pointer.hasPosition)
    {
        m_dragCurrent = {frame.pointer.x, frame.pointer.y};
    }

    // Cursor feedback: crosshair when hovering an enemy during battle
    bool hoverEnemy = false;
    if (frame.pointer.hasPosition)
    {
        Vec2 worldPos = screenToWorld(frame.pointer.x, frame.pointer.y, m_camera);
        const LegacySimulation &sim = m_world.legacy();
        const float pad = 8.0f;
        for (const EnemyUnit &enemy : sim.enemies)
        {
            if (enemy.hp <= 0.0f)
            {
                continue;
            }
            const float reach = enemy.radius + pad;
            if (lengthSq(enemy.pos - worldPos) <= reach * reach)
            {
                hoverEnemy = true;
                break;
            }
        }
    }
    static SDL_Cursor *crosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    static SDL_Cursor *arrow = SDL_GetDefaultCursor();
    if (hoverEnemy && !m_cursorCrosshairActive)
    {
        SDL_SetCursor(crosshair);
        m_cursorCrosshairActive = true;
    }
    else if (!hoverEnemy && m_cursorCrosshairActive)
    {
        SDL_SetCursor(arrow);
        m_cursorCrosshairActive = false;
    }
}

void BattleScene::update(double deltaSeconds, GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (!m_initialized)
    {
        return;
    }

    if (m_pausedForCamp)
    {
        m_debugController.update(deltaSeconds);
        return;
    }

    m_debugController.update(deltaSeconds);
    updateDebugToggles();

    if (m_pendingBudgetCheck)
    {
        evaluatePerformanceBudgets(app);
        m_pendingBudgetCheck = false;
    }

    LegacySimulation &sim = m_world.legacy();
    if (!m_resultOverlayActive && sim.result != GameResult::Playing)
    {
        if (!m_resultRecorded && m_campaign)
        {
            m_campaign->recordRunOutcome(sim);
            m_campaign->saveToDisk();
            m_resultRecorded = true;
        }
        m_resultOverlayActive = true;
        m_resultSummary = sim.hud.resultText.empty() ? "RESULT" : sim.hud.resultText;
        if (sim.hud.resultStats.available)
        {
            m_resultDetails.available = true;
            m_resultDetails.result = sim.hud.resultStats.result;
            m_resultDetails.durationSeconds = sim.hud.resultStats.durationSeconds;
            m_resultDetails.chibiDeaths = sim.hud.resultStats.chibiDeaths;
            m_resultDetails.chibiSurvivors = sim.hud.resultStats.chibiSurvivors;
            m_resultDetails.enemyKills = sim.hud.resultStats.enemyKills;
            m_resultDetails.manaEarned = sim.hud.resultStats.manaEarned;
            m_resultDetails.manaCap = sim.hud.resultStats.manaCap;
            m_resultDetails.manaBonusPercent = sim.hud.resultStats.manaBonusPercent;
            m_resultDetails.basesSealed = sim.hud.resultStats.basesSealed;
            m_resultDetails.basesTotal = sim.hud.resultStats.basesTotal;
        }
    }

    if (m_resultOverlayActive)
    {
        return;
    }

    const float debugScale = std::clamp(m_debugController.timeScale(), 0.25f, 4.0f);
    const float userScale = std::clamp(m_userTimeScale, 0.25f, 4.0f);
    const float timeScale = debugScale * userScale;
    m_lastFrameSeconds = deltaSeconds;
    m_accumulator += deltaSeconds * timeScale;
    m_fpsTimer += deltaSeconds;
    ++m_frames;
    if (m_fpsTimer >= 1.0)
    {
        m_currentFps = static_cast<float>(m_frames / m_fpsTimer);
        m_fpsTimer = 0.0;
        m_frames = 0;
    }

    const float dt = sim.config.fixed_dt;
    const double baseInputTimestamp = static_cast<double>(SDL_GetTicks64());
    const Uint64 updateStart = SDL_GetPerformanceCounter();
    const double tickToMs = m_frequency > 0.0 ? 1000.0 / m_frequency : 0.0;
    std::size_t stepIndex = 0;
    bool producedFrame = false;
    double inputMsAccum = 0.0;
    while (m_accumulator >= dt)
    {
        const Uint64 inputStart = SDL_GetPerformanceCounter();
        const double frameTimestamp = baseInputTimestamp +
                                      static_cast<double>(stepIndex) * (static_cast<double>(dt) * 1000.0);
        app.inputMapper().sampleFrame(!m_introActive,
                                      frameTimestamp,
                                      m_inputSequence++,
                                      m_actionBuffer);
        if (const ActionBuffer::Frame *frame = m_actionBuffer.latest())
        {
            handleActionFrame(*frame, app);
        }
        const Uint64 inputEnd = SDL_GetPerformanceCounter();
        if (tickToMs > 0.0)
        {
            inputMsAccum += (inputEnd - inputStart) * tickToMs;
        }
        m_world.step(dt, m_actionBuffer);
        m_accumulator -= dt;
        ++stepIndex;
        producedFrame = true;
    }
    if (!producedFrame)
    {
        const Uint64 inputStart = SDL_GetPerformanceCounter();
        app.inputMapper().sampleFrame(!m_introActive,
                                      baseInputTimestamp,
                                      m_inputSequence++,
                                      m_actionBuffer);
        if (const ActionBuffer::Frame *frame = m_actionBuffer.latest())
        {
            handleActionFrame(*frame, app);
        }
        const Uint64 inputEnd = SDL_GetPerformanceCounter();
        if (tickToMs > 0.0)
        {
            inputMsAccum += (inputEnd - inputStart) * tickToMs;
        }
    }
    const Uint64 updateEnd = SDL_GetPerformanceCounter();
    const double updateMs = (updateEnd - updateStart) * 1000.0 / m_frequency;
    m_lastUpdateMs = updateMs;
    m_framePerf.msUpdate = static_cast<float>(updateMs);
    m_lastInputMs = inputMsAccum;
    m_framePerf.msInput = static_cast<float>(inputMsAccum);
    m_lastStageTimings.updateMs = updateMs;
    m_lastStageTimings.inputMs = inputMsAccum;

    const float frameSeconds = static_cast<float>(deltaSeconds);
    if (m_introActive)
    {
        m_introTimer = std::max(0.0f, m_introTimer - frameSeconds);
        const float t = std::clamp(1.0f - (m_introTimer / m_introDuration), 0.0f, 1.0f);
        const float eased = t * t * (3.0f - 2.0f * t);
        m_camera.position = lerp(m_introCameraTarget, m_baseCameraTarget, eased);
        if (m_introTimer <= 0.0f)
        {
            m_introActive = false;
            m_camera.position = lerp(m_introCameraTarget, m_baseCameraTarget, 1.0f);
        }
    }
    else
    {
        Vec2 targetCamera{sim.commander.pos.x - m_screenWidth * 0.5f,
                          sim.commander.pos.y - m_screenHeight * 0.5f};
        const float followFactor = std::clamp(frameSeconds * 6.0f, 0.0f, 1.0f);
        m_camera.position = lerp(m_camera.position, targetCamera, followFactor);
    }

    if (m_eventBus)
    {
        sim.hud.unconsumedEvents = m_eventBus->unconsumedCount();
    }
    else
    {
        sim.hud.unconsumedEvents = 0;
    }

    m_framePerf.fps = m_currentFps;
}

void BattleScene::updateDebugToggles()
{
    if (m_debugController.consumeHudToggle())
    {
        m_showDebugHud = !m_showDebugHud;
    }
    if (m_debugController.consumeTelemetryToggle())
    {
        m_showTelemetryOverlay = !m_showTelemetryOverlay;
    }
}

void BattleScene::initializeDebugBindings(GameApplication &app)
{
    (void)app;
    debug::DebugBindings bindings;
    bindings.simulation = &m_world.legacy();
    bindings.accessor = &m_debugAccessor;
    m_debugController.bindWorld(bindings);
    m_debugController.onConfigReloaded();
}

void BattleScene::DebugSimulationAccessor::markComponentsDirty()
{
    m_scene.m_world.markComponentsDirty();
}

void BattleScene::DebugSimulationAccessor::setEnemySpawnMultiplier(float multiplier)
{
    m_scene.m_world.setEnemySpawnMultiplier(multiplier);
}

float BattleScene::DebugSimulationAccessor::enemySpawnMultiplier() const
{
    return m_scene.m_world.enemySpawnMultiplier();
}

bool BattleScene::DebugSimulationAccessor::skipNextWave()
{
    return m_scene.m_world.skipNextWave();
}

void BattleScene::render(SDL_Renderer *renderer, GameApplication &app)
{
    (void)app;
    if (!m_initialized)
    {
        return;
    }

    const LegacySimulation &sim = m_world.legacy();
    const Uint64 renderStart = SDL_GetPerformanceCounter();

    const Vec2 cameraOffset = m_camera.position;
    Camera renderCamera = m_camera;
    renderCamera.position = cameraOffset;
    const auto &allyPool = m_world.allies();
    const auto &enemyPool = m_world.enemies();
    m_framePerf.entities = static_cast<int>(allyPool.size() + enemyPool.size() + (sim.commander.alive ? 1 : 0));
    const FormationHudStatus &formationHud = m_ui.formationHud();
    const MoraleHudStatus &moraleHud = m_ui.moraleHud();
    const JobHudStatus &jobHud = m_ui.jobHud();
    double hudMs = 0.0;
    RenderStats renderStats{};

    if (renderer)
    {
        const UiView::Dependencies &deps = m_uiView.dependencies();
        if (deps.renderer != renderer || deps.hudFont != &m_hudFont || deps.debugFont != &m_debugFont ||
            deps.screenWidth != m_screenWidth || deps.screenHeight != m_screenHeight)
        {
            UiView::Dependencies updated = deps;
            updated.renderer = renderer;
            updated.hudFont = &m_hudFont;
            updated.debugFont = &m_debugFont;
            updated.screenWidth = m_screenWidth;
            updated.screenHeight = m_screenHeight;
            m_uiView.setDependencies(updated);
        }
    }

    renderWorld(renderer,
                sim,
                &formationHud,
                &moraleHud,
                &jobHud,
                renderCamera,
                m_hudFont,
                m_debugFont,
                m_tileMap,
                m_atlas,
                m_screenWidth,
                m_screenHeight,
                renderStats);

    if (renderer)
    {
        pruneSelection();
        if (!m_selectedChibis.empty())
        {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 70, 180, 255, 120);
            for (int idx : m_selectedChibis)
            {
                if (idx < 0 || static_cast<std::size_t>(idx) >= sim.yunas.size())
                {
                    continue;
                }
                const Unit &unit = sim.yunas[static_cast<std::size_t>(idx)];
                if (unit.hp <= 0.0f)
                {
                    continue;
                }
                Vec2 screenPos = worldToScreen(unit.pos, renderCamera);
                const float ringRadius = unit.radius + 8.0f;
                drawFilledCircle(renderer, screenPos, ringRadius, renderStats);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
        if (m_draggingSelection)
        {
            SDL_Rect rect{};
            rect.x = std::min(m_dragStart.x, m_dragCurrent.x);
            rect.y = std::min(m_dragStart.y, m_dragCurrent.y);
            rect.w = std::abs(m_dragCurrent.x - m_dragStart.x);
            rect.h = std::abs(m_dragCurrent.y - m_dragStart.y);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 70, 180, 255, 70);
            countedRenderFillRect(renderer, &rect, renderStats);
            SDL_SetRenderDrawColor(renderer, 70, 180, 255, 200);
            countedRenderDrawRect(renderer, &rect, renderStats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
    }

    Uint64 hudSectionStart = 0;
    if (m_frequency > 0.0)
    {
        hudSectionStart = SDL_GetPerformanceCounter();
    }

    UiView::DrawContext::InputDiagnosticsState inputDiagnostics{};
    inputDiagnostics.bufferCapacity = m_actionBuffer.capacity();
    inputDiagnostics.bufferedFrames = m_actionBuffer.size();
    inputDiagnostics.configuredBufferFrames = app.inputMapper().bufferFrames();
    inputDiagnostics.bufferExpiryMs = app.inputMapper().bufferExpiryMs();
    if (const ActionBuffer::Frame *latest = m_actionBuffer.latest())
    {
        inputDiagnostics.hasLatestFrame = true;
        inputDiagnostics.latestSequence = latest->sequence;
        inputDiagnostics.latestDeviceTimestampMs = latest->deviceTimestampMs;
        inputDiagnostics.hasPointerState = true;
        inputDiagnostics.pointerState.hasPosition = latest->pointer.hasPosition;
        inputDiagnostics.pointerState.x = latest->pointer.x;
        inputDiagnostics.pointerState.y = latest->pointer.y;
        inputDiagnostics.pointerState.left = latest->pointer.left;
        inputDiagnostics.pointerState.right = latest->pointer.right;
        inputDiagnostics.pointerState.middle = latest->pointer.middle;
        inputDiagnostics.latestEvents.reserve(latest->events.size());
        for (const ActionEvent &evt : latest->events)
        {
            UiView::DrawContext::InputDiagnosticsState::Event diagEvt;
            diagEvt.id = evt.id;
            diagEvt.value = evt.value;
            diagEvt.pressed = evt.pressed;
            diagEvt.released = evt.released;
            if (evt.pointer)
            {
                diagEvt.hasPointer = true;
                diagEvt.pointerX = evt.pointer->x;
                diagEvt.pointerY = evt.pointer->y;
                diagEvt.pointerPressed = evt.pointer->pressed;
                diagEvt.pointerReleased = evt.pointer->released;
            }
            inputDiagnostics.latestEvents.push_back(diagEvt);
        }
    }

    UiView::DrawContext hudContext{};
    hudContext.simulation = &sim;
    hudContext.formationHud = &formationHud;
    hudContext.moraleHud = &moraleHud;
    hudContext.jobHud = &jobHud;
    hudContext.framePerf = &m_framePerf;
    hudContext.renderStats = &renderStats;
    hudContext.showDebugHud = m_showDebugHud;
    hudContext.performanceFrequency = m_frequency;
    hudContext.hudTimeMs = &hudMs;
    hudContext.inputDiagnostics = &inputDiagnostics;
    hudContext.timeScale = m_userTimeScale;
    hudContext.showSpeedIndicator = m_speedSteps.size() > 1;
    m_uiView.render(hudContext);

    if (renderer && m_debugController.active())
    {
        m_debugController.gatherDisplay(m_debugDisplayState);
        m_debugDisplayState.showTelemetry = m_showTelemetryOverlay;
        m_debugOverlay.render(renderer,
                              m_debugFont,
                              m_debugFont,
                              m_debugDisplayState,
                              m_framePerf,
                              renderStats,
                              m_screenWidth,
                              m_screenHeight);
    }

    if (m_resultOverlayActive)
    {
        renderResultOverlay(renderer, app);
    }

    if (hudSectionStart != 0)
    {
        const Uint64 hudEnd = SDL_GetPerformanceCounter();
        hudMs += (hudEnd - hudSectionStart) * 1000.0 / m_frequency;
    }

    const Uint64 renderEnd = SDL_GetPerformanceCounter();
    const double renderMs = (renderEnd - renderStart) * 1000.0 / m_frequency;
    m_framePerf.msRender = static_cast<float>(renderMs);
    m_framePerf.msHud = static_cast<float>(hudMs);
    m_framePerf.drawCalls = renderStats.drawCalls;
    m_lastStageTimings.renderMs = renderMs;
    m_lastStageTimings.hudMs = hudMs;

    m_perfLogTimer += m_lastFrameSeconds;
    m_updateAccum += m_lastUpdateMs;
    m_renderAccum += renderMs;
    m_entityAccum += static_cast<double>(m_framePerf.entities);
    ++m_perfLogFrames;
    m_pendingBudgetCheck = true;
    if (m_perfLogTimer >= 1.0 && m_perfLogFrames > 0)
    {
        const double avgFps = static_cast<double>(m_perfLogFrames) / m_perfLogTimer;
        const double avgUpdate = m_updateAccum / m_perfLogFrames;
        const double avgRender = m_renderAccum / m_perfLogFrames;
        const double avgEntities = m_entityAccum / m_perfLogFrames;
        const bool spike = (avgUpdate + avgRender) > 9.0;
        if (m_telemetry)
        {
            auto formatDouble = [](double value, int precision) {
                std::ostringstream oss;
                oss << std::fixed << std::setprecision(precision) << value;
                return oss.str();
            };

            TelemetrySink::Payload payload;
            payload.emplace("fps", formatDouble(avgFps, 1));
            payload.emplace("update_ms", formatDouble(avgUpdate, 2));
            payload.emplace("render_ms", formatDouble(avgRender, 2));
            payload.emplace("entities", std::to_string(static_cast<int>(std::round(avgEntities))));
            payload.emplace("spike", spike ? "true" : "false");
            m_telemetry->recordEvent("battle.performance", payload);
        }
        m_perfLogTimer = 0.0;
        m_updateAccum = 0.0;
        m_renderAccum = 0.0;
        m_entityAccum = 0.0;
        m_perfLogFrames = 0;
    }
}

void BattleScene::renderResultOverlay(SDL_Renderer *renderer, GameApplication &app)
{
    (void)app;
    if (!m_resultOverlayActive || !renderer || !m_hudFont.isLoaded())
    {
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
    SDL_Rect overlay{0, 0, m_screenWidth, m_screenHeight};
    SDL_RenderFillRect(renderer, &overlay);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    auto formatDuration = [](float seconds) -> std::string {
        if (seconds < 0.0f || !std::isfinite(seconds))
        {
            return "-";
        }
        const int total = static_cast<int>(std::round(seconds));
        const int mins = total / 60;
        const int secs = total % 60;
        std::ostringstream oss;
        oss << mins << "m " << std::setfill('0') << std::setw(2) << secs << "s";
        return oss.str();
    };

    std::vector<std::string> lines;
    lines.push_back(m_resultSummary.empty() ? std::string("RESULT") : m_resultSummary);
    if (m_resultDetails.available)
    {
        lines.push_back("戦闘時間: " + formatDuration(m_resultDetails.durationSeconds));
        std::ostringstream manaLine;
        manaLine << "マナ: " << m_resultDetails.manaEarned << " / " << m_resultDetails.manaCap;
        if (m_resultDetails.manaBonusPercent > 0)
        {
            manaLine << " (+" << m_resultDetails.manaBonusPercent << "%)";
        }
        lines.push_back(manaLine.str());
        std::ostringstream chibiLine;
        chibiLine << "ちびわふ: 生存 " << m_resultDetails.chibiSurvivors << " / 死亡 "
                  << m_resultDetails.chibiDeaths;
        lines.push_back(chibiLine.str());
        std::ostringstream enemyLine;
        enemyLine << "撃破: " << m_resultDetails.enemyKills;
        if (m_resultDetails.basesTotal > 0)
        {
            enemyLine << " / 拠点封鎖 " << m_resultDetails.basesSealed << "/" << m_resultDetails.basesTotal;
        }
        lines.push_back(enemyLine.str());
    }
    lines.push_back("[Enter] キャンプへ戻る");
    lines.push_back("[R] もう一度挑戦");
    lines.push_back("[Esc] タイトルへ戻る");
    const TextRenderer &font = m_hudFont;
    const int lineAdvance = font.getLineHeight() + 8;
    int totalHeight = static_cast<int>(lines.size()) * lineAdvance;
    int y = m_screenHeight / 2 - totalHeight / 2;
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string &line = lines[i];
        const int textWidth = font.measureText(line);
        const int x = std::max(32, (m_screenWidth - textWidth) / 2);
        SDL_Color color = i == 0 ? SDL_Color{255, 240, 180, 255} : SDL_Color{230, 230, 255, 255};
        font.drawText(renderer, line, x, y, nullptr, color);
        y += lineAdvance;
    }
}

void BattleScene::handleResultOverlayKey(SDL_Keycode key, GameApplication &app, SceneStack &stack)
{
    switch (key)
    {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (!m_pausedForCamp)
        {
            m_pausedForCamp = true;
            m_resultOverlayActive = false;
            stack.push(std::make_unique<CampScene>(m_campaign, this));
        }
        break;
    case SDLK_r:
        m_resultOverlayActive = false;
        startRunFromCamp(app);
        break;
    case SDLK_ESCAPE:
        app.requestQuit();
        break;
    default:
        break;
    }
}

void BattleScene::cycleGameSpeed(int direction)
{
    if (m_speedSteps.empty())
    {
        return;
    }
    const int count = static_cast<int>(m_speedSteps.size());
    direction = direction >= 0 ? 1 : -1;
    m_speedIndex = (m_speedIndex + count + direction) % count;
    m_userTimeScale = std::clamp(m_speedSteps[m_speedIndex], 0.25f, 4.0f);
    std::ostringstream oss;
    oss << "Speed x" << std::fixed << std::setprecision(m_userTimeScale >= 2.0f ? 0 : 1) << m_userTimeScale;
    showTelemetryMessage(oss.str());
}

void BattleScene::selectChibisInRect(const SDL_Rect &rect)
{
    m_selectedChibis.clear();
    const LegacySimulation &sim = m_world.legacy();
    for (std::size_t i = 0; i < sim.yunas.size(); ++i)
    {
        const Unit &unit = sim.yunas[i];
        if (unit.hp <= 0.0f || unit.isNamed)
        {
            continue;
        }
        Vec2 screenPos = worldToScreen(unit.pos, m_camera);
        SDL_Point pt{static_cast<int>(std::round(screenPos.x)), static_cast<int>(std::round(screenPos.y))};
        if (pt.x >= rect.x && pt.x <= rect.x + rect.w && pt.y >= rect.y && pt.y <= rect.y + rect.h)
        {
            m_selectedChibis.push_back(static_cast<int>(i));
        }
    }
    pruneSelection();
}

int BattleScene::enemyIndexAtScreen(int screenX, int screenY) const
{
    const LegacySimulation &sim = m_world.legacy();
    const Vec2 worldPos = screenToWorld(screenX, screenY, m_camera);
    const float clickPad = 6.0f;
    int found = -1;
    float bestDist = std::numeric_limits<float>::max();
    for (std::size_t i = 0; i < sim.enemies.size(); ++i)
    {
        const EnemyUnit &enemy = sim.enemies[i];
        if (enemy.hp <= 0.0f)
        {
            continue;
        }
        const float pad = enemy.radius + clickPad;
        const float distSq = lengthSq(enemy.pos - worldPos);
        if (distSq <= pad * pad && distSq < bestDist)
        {
            found = static_cast<int>(i);
            bestDist = distSq;
        }
    }
    return found;
}

void BattleScene::pruneSelection()
{
    const LegacySimulation &sim = m_world.legacy();
    const std::size_t allyCount = sim.yunas.size();
    m_selectedChibis.erase(std::remove_if(m_selectedChibis.begin(),
                                          m_selectedChibis.end(),
                                          [&](int idx) {
                                              return idx < 0 || static_cast<std::size_t>(idx) >= allyCount ||
                                                     sim.yunas[static_cast<std::size_t>(idx)].hp <= 0.0f ||
                                                     sim.yunas[static_cast<std::size_t>(idx)].isNamed;
                                          }),
                           m_selectedChibis.end());
}

void BattleScene::resetResultState()
{
    m_resultOverlayActive = false;
    m_resultRecorded = false;
    m_resultSummary.clear();
    m_resultDetails = {};
}

void BattleScene::evaluatePerformanceBudgets(GameApplication &app)
{
    (void)app;
    m_framePerf.budgetExceeded = false;
    m_framePerf.budgetStage.clear();
    m_framePerf.budgetSampleMs = 0.0f;
    m_framePerf.budgetTargetMs = 0.0f;

    telemetry::StageTimingSample sample{};
    sample.updateMs = m_lastStageTimings.updateMs;
    sample.renderMs = m_lastStageTimings.renderMs;
    sample.inputMs = m_lastStageTimings.inputMs;
    sample.hudMs = m_lastStageTimings.hudMs;

    if (auto violation = m_budgetMonitor.evaluate(sample))
    {
        raisePerformanceWarning(*violation, app);
    }
}

void BattleScene::raisePerformanceWarning(const telemetry::BudgetViolation &violation, GameApplication &app)
{
    std::string stageLabel = violation.stage;
    std::transform(stageLabel.begin(), stageLabel.end(), stageLabel.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    auto formatMs = [](double value) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << value;
        return oss.str();
    };

    const std::string warningText = "Performance spike: " + stageLabel + ' ' + formatMs(violation.sampleMs) +
                                    "ms (budget " + formatMs(violation.budgetMs) + "ms)";

    m_framePerf.budgetExceeded = true;
    m_framePerf.budgetStage = stageLabel;
    m_framePerf.budgetSampleMs = static_cast<float>(violation.sampleMs);
    m_framePerf.budgetTargetMs = static_cast<float>(violation.budgetMs);

    LegacySimulation &sim = m_world.legacy();
    HUDState &hud = sim.hud;
    hud.performance.active = true;
    hud.performance.message = warningText;
    hud.performance.timer = std::max(sim.config.telemetry_duration, 1.5f);

    if (m_telemetry)
    {
        TelemetrySink::Payload payload;
        payload.emplace("stage", violation.stage);
        payload.emplace("sample_ms", formatMs(violation.sampleMs));
        payload.emplace("budget_ms", formatMs(violation.budgetMs));
        payload.emplace("tolerance_ms", formatMs(m_performanceBudget.toleranceMs));
        m_telemetry->recordEvent("battle.performance.budget_exceeded", payload);

        const Uint64 now = SDL_GetTicks64();
        if (m_lastBudgetWarningTick == 0 || now - m_lastBudgetWarningTick >= BudgetWarningCooldownMs)
        {
            m_telemetry->requestFrameCapture();
            m_lastBudgetWarningTick = now;
        }
    }

    (void)app;
}


void BattleScene::applyAppConfig(GameApplication &app)
{
    if (!m_assetService)
    {
        return;
    }

    auto telemetryNotify = [this](std::string reason, std::string detail = {}) {
        if (!m_telemetry)
        {
            return;
        }
        TelemetrySink::Payload payload{{"scene", "BattleScene"}, {"reason", std::move(reason)}};
        if (!detail.empty())
        {
            payload.emplace("detail", std::move(detail));
        }
        m_telemetry->recordEvent("scene.warning", payload);
    };

    AssetManager &assets = *m_assetService;
    const AppConfig &appConfig = app.appConfig();
    const AppConfigLoadResult &configResult = app.appConfigResult();
    if (!configResult.success)
    {
        std::cerr << "AppConfig loaded with errors, running with fallback values.\n";
        telemetryNotify("app_config_errors", std::to_string(configResult.errors.size()));
    }

    m_tileMap = {};
    if (!loadTileMap(assets, appConfig.game.map_path, m_tileMap))
    {
        std::cerr << "Continuing without tilemap visuals.\n";
        telemetryNotify("tilemap_missing", appConfig.game.map_path);
    }

    m_atlas = {};
    if (!loadAtlas(assets, appConfig.atlasPath, m_atlas))
    {
        std::cerr << "Continuing without atlas visuals.\n";
        telemetryNotify("atlas_missing", appConfig.atlasPath);
    }

    LegacySimulation &sim = m_world.legacy();
    sim = LegacySimulation{};
    sim.config = appConfig.game;
    sim.temperamentConfig = appConfig.temperament;
    sim.chibiPersonalityConfig = appConfig.chibiPersonality;
    sim.chibiAiParams = appConfig.chibiAiParams;
    sim.yunaStats = appConfig.entityCatalog.yuna;
    sim.slimeStats = appConfig.entityCatalog.slime;
    sim.goblinStats = appConfig.entityCatalog.goblin;
    sim.magicianStats = appConfig.entityCatalog.magician;
    sim.batStats = appConfig.entityCatalog.bat;
    sim.toritoriStats = appConfig.entityCatalog.toritori;
    sim.golemStats = appConfig.entityCatalog.golem;
    sim.wallbreakerStats = appConfig.entityCatalog.wallbreaker;
    sim.commanderStats = appConfig.entityCatalog.commander;
    sim.mapDefs = appConfig.mapDefs;
    sim.spawnScript = appConfig.spawnScript;
    sim.economyConfig = appConfig.economy;
    sim.formationDefaults = appConfig.game.formationDefaults;
    sim.formationAlignTimer = 0.0f;
    sim.formationDefenseMul = 1.0f;
    if (appConfig.stageConfig)
    {
        sim.configureStage(*appConfig.stageConfig);
    }
    else
    {
        std::cerr << "[stage] stageConfig missing; clearing stage configuration\n";
        sim.clearStageConfiguration();
    }
    const std::size_t allyBaseCount = sim.stage.allyBases.size();
    if (allyBaseCount == 0)
    {
        std::cerr << "[stage] allyBases is empty after configuration (stageConfig="
                  << (appConfig.stageConfig ? "loaded" : "none") << ")\n";
    }
    m_speedSteps = sim.stage.speed.steps;
    if (m_speedSteps.empty())
    {
        m_speedSteps = {1.0f};
    }
    m_speedIndex = 0;
    m_userTimeScale = std::clamp(m_speedSteps[m_speedIndex], 0.25f, 4.0f);
    if (appConfig.mission && appConfig.mission->mode != MissionMode::None)
    {
        sim.hasMission = true;
        sim.missionConfig = *appConfig.mission;
    }
    else
    {
        sim.hasMission = false;
    }

    m_world.setTelemetrySink(m_telemetry);
    m_world.setEventBus(m_eventBus);
    m_ui.setTelemetrySink(m_telemetry);
    m_ui.setEventBus(m_eventBus);
    m_ui.bindSimulation(&m_world.legacy());

    if (m_tileMap.width > 0 && m_tileMap.height > 0)
    {
        m_world.setWorldBounds(static_cast<float>(m_tileMap.width * m_tileMap.tileWidth),
                               static_cast<float>(m_tileMap.height * m_tileMap.tileHeight));
    }
    else
    {
        m_world.setWorldBounds(static_cast<float>(m_screenWidth), static_cast<float>(m_screenHeight));
    }

    std::vector<SkillDef> skillDefs = appConfig.skills.empty() ? buildDefaultSkills() : appConfig.skills;
    m_world.configureSkills(skillDefs);
    applyCampaignModifiers(app);
    initializeRun(app);
    resetResultState();

    if (!m_hudFont.load(assets, "assets/ui/NotoSansJP-Regular.ttf", 22))
    {
        std::cerr << "Failed to load HUD font (NotoSansJP-Regular.ttf).\n";
        telemetryNotify("hud_font_missing", "NotoSansJP-Regular.ttf");
    }
    if (!m_debugFont.load(assets, "assets/ui/NotoSansJP-Regular.ttf", 18))
    {
        std::cerr << "Failed to load debug font fallback, using HUD font size.\n";
        telemetryNotify("debug_font_missing", "NotoSansJP-Regular.ttf");
    }

    UiView::Dependencies uiDeps;
    uiDeps.renderer = app.renderer();
    uiDeps.hudFont = &m_hudFont;
    uiDeps.debugFont = &m_debugFont;
    uiDeps.screenWidth = m_screenWidth;
    uiDeps.screenHeight = m_screenHeight;
    m_uiView.setDependencies(uiDeps);

    initializeDebugBindings(app);
}

void BattleScene::applyCampaignModifiers(GameApplication &app)
{
    if (!m_campaign)
    {
        return;
    }
    m_campaign->applyPersistentUpgrades(app.appConfig(), m_world.legacy());
}

void BattleScene::initializeRun(GameApplication &app)
{
    const AppConfig &appConfig = app.appConfig();
    LegacySimulation &sim = m_world.legacy();
    m_world.reset();
    if (m_campaign)
    {
        m_campaign->applyRunStart(appConfig, sim);
    }

    m_actionBuffer.clear();
    m_actionBuffer.setCapacity(static_cast<std::size_t>(std::max(1, appConfig.input.bufferFrames)));
    m_inputSequence = 0;
    m_haveProcessedSequence = false;
    m_lastProcessedSequence = 0;

    m_camera = {};
    m_baseCameraTarget = {sim.basePos.x - m_screenWidth * 0.5f, sim.basePos.y - m_screenHeight * 0.5f};
    m_introFocus = leftmostGateWorld(sim.mapDefs);
    m_introCameraTarget = {m_introFocus.x - m_screenWidth * 0.5f, m_introFocus.y - m_screenHeight * 0.5f};
    m_camera.position = m_introCameraTarget;
    m_introTimer = m_introDuration;
    m_introActive = true;

    m_accumulator = 0.0;
    m_fpsTimer = 0.0;
    m_frames = 0;
    m_currentFps = 60.0f;
    m_framePerf = {};
    m_framePerf.fps = m_currentFps;
    m_perfLogTimer = 0.0;
    m_updateAccum = 0.0;
    m_renderAccum = 0.0;
    m_entityAccum = 0.0;
    m_perfLogFrames = 0;
    m_performanceBudget = appConfig.game.performance;
    m_budgetMonitor.setBudget(m_performanceBudget);
    m_lastBudgetWarningTick = 0;
    m_lastStageTimings = {};
    m_pendingBudgetCheck = false;
    if (!m_initialized)
    {
        m_showDebugHud = false;
    }
    m_frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    m_lastFrameSeconds = 0.0;
    m_lastUpdateMs = 0.0;
}

void BattleScene::startRunFromCamp(GameApplication &app)
{
    applyCampaignModifiers(app);
    initializeRun(app);
    if (m_campaign)
    {
        m_campaign->saveToDisk();
    }
    m_pausedForCamp = false;
    resetResultState();
}


void BattleScene::onConfigReloaded(GameApplication &app, SceneStack &stack)
{
    (void)stack;
    if (!m_initialized)
    {
        return;
    }
    m_screenWidth = app.windowWidth();
    m_screenHeight = app.windowHeight();
    applyAppConfig(app);
}

void BattleScene::showTelemetryMessage(const std::string &message)
{
    if (message.empty())
    {
        return;
    }
    m_world.legacy().pushTelemetry(message);
}
