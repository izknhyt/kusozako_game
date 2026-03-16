#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "app/FramePerf.h"
#include "app/TextRenderer.h"
#include "app/UiPresenter.h"
#include "app/UiView.h"
#include "assets/LevelAssets.h"
#include "debug/DebugController.h"
#include "debug/DebugOverlayView.h"
#include "gfx/Camera.h"
#include "input/ActionBuffer.h"
#include "scenes/Scene.h"
#include "telemetry/PerformanceBudgetMonitor.h"
#include "world/LegacyTypes.h"
#include "world/WorldState.h"

class AssetManager;
struct CampaignState;
class CampScene;
class EventBus;
class GameApplication;
class SceneStack;
class TelemetrySink;

extern bool g_showChibiLabels;

class BattleScene : public Scene
{
  public:
    explicit BattleScene(std::shared_ptr<CampaignState> campaign, bool startPausedForCamp = false);

    void onEnter(GameApplication &app, SceneStack &stack) override;
    void onExit(GameApplication &app, SceneStack &stack) override;
    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override;
    void update(double deltaSeconds, GameApplication &app, SceneStack &stack) override;
    void render(SDL_Renderer *renderer, GameApplication &app) override;
    void onConfigReloaded(GameApplication &app, SceneStack &stack) override;
    void startRunFromCamp(GameApplication &app);

  private:
    void handleActionFrame(const ActionBuffer::Frame &frame, GameApplication &app);
    void applyAppConfig(GameApplication &app);
    void showTelemetryMessage(const std::string &message);
    void evaluatePerformanceBudgets(GameApplication &app);
    void raisePerformanceWarning(const telemetry::BudgetViolation &violation, GameApplication &app);

    class DebugSimulationAccessor : public debug::DebugBindings::SimulationAccessor
    {
      public:
        explicit DebugSimulationAccessor(BattleScene &scene) : m_scene(scene) {}

        void markComponentsDirty() override;
        void setEnemySpawnMultiplier(float multiplier) override;
        float enemySpawnMultiplier() const override;
        bool skipNextWave() override;

      private:
        BattleScene &m_scene;
    };

    bool m_initialized = false;
    std::shared_ptr<CampaignState> m_campaign;
    world::WorldState m_world;
    TileMap m_tileMap;
    Atlas m_atlas;
    TextRenderer m_hudFont;
    TextRenderer m_debugFont;
    Camera m_camera;
    Vec2 m_baseCameraTarget{};
    Vec2 m_introCameraTarget{};
    Vec2 m_introFocus{};
    bool m_introActive = true;
    float m_introTimer = 0.0f;
    static constexpr float m_introDuration = 3.0f;
    bool m_showDebugHud = false;
    double m_accumulator = 0.0;
    double m_fpsTimer = 0.0;
    int m_frames = 0;
    float m_currentFps = 60.0f;
    FramePerf m_framePerf{};
    double m_perfLogTimer = 0.0;
    double m_updateAccum = 0.0;
    double m_renderAccum = 0.0;
    double m_entityAccum = 0.0;
    int m_perfLogFrames = 0;
    double m_frequency = 0.0;
    double m_lastFrameSeconds = 0.0;
    double m_lastUpdateMs = 0.0;
    double m_lastInputMs = 0.0;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    std::shared_ptr<TelemetrySink> m_telemetry;
    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<AssetManager> m_assetService;
    UiPresenter m_ui;
    UiView m_uiView;
    std::shared_ptr<UiPresenter> m_uiServiceHandle;
    ActionBuffer m_actionBuffer;
    std::uint64_t m_inputSequence = 0;
    std::uint64_t m_lastProcessedSequence = 0;
    bool m_haveProcessedSequence = false;
    bool m_pendingBudgetCheck = false;
    struct StageTimings
    {
        double updateMs = 0.0;
        double renderMs = 0.0;
        double inputMs = 0.0;
        double hudMs = 0.0;
    } m_lastStageTimings{};
    PerformanceBudgetConfig m_performanceBudget{};
    telemetry::PerformanceBudgetMonitor m_budgetMonitor{};
    Uint64 m_lastBudgetWarningTick = 0;
    static constexpr Uint64 BudgetWarningCooldownMs = 1000;
    debug::DebugController m_debugController;
    debug::DebugOverlayView m_debugOverlay;
    debug::DisplayState m_debugDisplayState;
    DebugSimulationAccessor m_debugAccessor;
    bool m_showTelemetryOverlay = false;
    int m_cursorRestoreState = SDL_QUERY;
    bool m_startPausedForCamp = false;
    bool m_pausedForCamp = false;
    bool m_showChibiLabels = false;
    bool m_cursorCrosshairActive = false;
    bool m_resultOverlayActive = false;
    bool m_resultRecorded = false;
    std::string m_resultSummary;
    struct ResultDetails
    {
        bool available = false;
        GameResult result = GameResult::Playing;
        float durationSeconds = 0.0f;
        int chibiDeaths = 0;
        int chibiSurvivors = 0;
        int enemyKills = 0;
        int manaEarned = 0;
        int manaCap = 0;
        int manaBonusPercent = 0;
        int basesSealed = 0;
        int basesTotal = 0;
    } m_resultDetails{};
    std::vector<float> m_speedSteps{1.0f};
    int m_speedIndex = 0;
    float m_userTimeScale = 1.0f;
    bool m_draggingSelection = false;
    SDL_Point m_dragStart{0, 0};
    SDL_Point m_dragCurrent{0, 0};
    std::vector<int> m_selectedChibis;

    void initializeDebugBindings(GameApplication &app);
    void updateDebugToggles();
    void applyCampaignModifiers(GameApplication &app);
    void initializeRun(GameApplication &app);
    void renderResultOverlay(SDL_Renderer *renderer, GameApplication &app);
    void handleResultOverlayKey(SDL_Keycode key, GameApplication &app, SceneStack &stack);
    void cycleGameSpeed(int direction = 1);
    void resetResultState();
    void selectChibisInRect(const SDL_Rect &rect);
    int enemyIndexAtScreen(int screenX, int screenY) const;
    void pruneSelection();
};
