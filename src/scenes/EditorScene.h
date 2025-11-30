#pragma once

#include "scenes/Scene.h"

#include <filesystem>
#include <memory>
#include <string>

#include "world/StageConfig.h"

class TextRenderer;
struct TileMap;
struct Atlas;
struct Camera;
namespace world
{
struct LegacySimulation;
}

class EditorScene : public Scene
{
  public:
    EditorScene() = default;

    void onEnter(GameApplication &app, SceneStack &stack) override;
    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override;
    void update(double deltaSeconds, GameApplication &app, SceneStack &stack) override;
    void render(SDL_Renderer *renderer, GameApplication &app) override;

  private:
    void loadResources(GameApplication &app);
    void renderStatus(SDL_Renderer *renderer, GameApplication &app);
    void handleCameraInput(double deltaSeconds);
    void handleEditingInput(const SDL_Event &event, GameApplication &app);
    void reload(GameApplication &app);
    void clampSelection();
    void showFeedback(const std::string &text, double seconds);

    bool m_loaded = false;
    bool m_showDebug = false;
    double m_time = 0.0;
    std::string m_status;
    std::string m_feedback;
    double m_feedbackTimer = 0.0;
    std::unique_ptr<TextRenderer> m_font;
    std::unique_ptr<TextRenderer> m_debugFont;
    std::unique_ptr<TileMap> m_tileMap;
    std::unique_ptr<Atlas> m_atlas;
    std::unique_ptr<Camera> m_camera;
    std::unique_ptr<world::LegacySimulation> m_sim;
    StageConfig m_stageConfig{};
    bool m_hasStageConfig = false;
    std::filesystem::path m_stageConfigPath;
    int m_selectedEnemyBase = 0;
    int m_selectedAllyBase = 0;
    int m_selectedHazard = 0;
};
