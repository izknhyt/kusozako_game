#include "scenes/EditorScene.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <sstream>

#include "app/GameApplication.h"
#include "app/TextRenderer.h"
#include "assets/AssetManager.h"
#include "assets/LevelAssets.h"
#include "config/AppConfig.h"
#include "config/StageConfigSerializer.h"
#include "gfx/Camera.h"
#include "gfx/WorldRenderer.h"
#include "scenes/SceneStack.h"
#include "world/LegacySimulation.h"
#include "world/StageConfig.h"

void EditorScene::onEnter(GameApplication &, SceneStack &)
{
    m_time = 0.0;
    m_loaded = false;
    m_status.clear();
    m_feedback.clear();
    m_feedbackTimer = 0.0;
    m_showDebug = false;
    m_hasStageConfig = false;
}

void EditorScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    if (event.type == SDL_QUIT)
    {
        app.requestQuit();
        return;
    }
    if (event.type == SDL_KEYDOWN)
    {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE)
        {
            // Pop editor scene; if nothing left, quit.
            stack.pop();
            if (stack.empty())
            {
                app.requestQuit();
            }
        }
        else if (key == SDLK_TAB)
        {
            m_showDebug = !m_showDebug;
        }
    }

    handleEditingInput(event, app);
}

void EditorScene::update(double deltaSeconds, GameApplication &, SceneStack &)
{
    m_time += deltaSeconds;
    handleCameraInput(deltaSeconds);
    if (m_feedbackTimer > 0.0)
    {
        m_feedbackTimer = std::max(0.0, m_feedbackTimer - deltaSeconds);
        if (m_feedbackTimer == 0.0)
        {
            m_feedback.clear();
        }
    }
}

void EditorScene::render(SDL_Renderer *renderer, GameApplication &app)
{
    if (!renderer)
    {
        return;
    }
    if (!m_loaded)
    {
        loadResources(app);
    }
    if (!m_loaded)
    {
        renderStatus(renderer, app);
        return;
    }

    RenderStats stats{};
    static TextRenderer emptyFont{};
    const TextRenderer *hudFont = (m_font && m_font->isLoaded()) ? m_font.get() : &emptyFont;
    const TextRenderer *debugFont = (m_debugFont && m_debugFont->isLoaded()) ? m_debugFont.get() : hudFont;
    renderWorld(renderer,
                *m_sim,
                nullptr,
                nullptr,
                nullptr,
                *m_camera,
                *hudFont,
                *debugFont,
                *m_tileMap,
                *m_atlas,
                app.windowWidth(),
                app.windowHeight(),
                stats);

    if (m_font && m_font->isLoaded())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 12, 18, 28, 180);
        SDL_Rect panel{12, 12, 340, 150};
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        int y = 20;
        m_font->drawText(renderer, "[Editor] Arrow/WASD: pan, Shift=slow", 18, y, nullptr, SDL_Color{220, 240, 255, 255});
        y += m_font->getLineHeight() + 4;
        m_font->drawText(renderer, "1/2: select enemy base | +/- size | arrows move | ,/. rate | ;/' rateMax", 18, y, nullptr,
                         SDL_Color{220, 240, 255, 255});
        y += m_font->getLineHeight() + 4;
        m_font->drawText(renderer, "3/4: select ally base  | R: reload config | Ctrl+S: save", 18, y, nullptr,
                         SDL_Color{220, 240, 255, 255});
        y += m_font->getLineHeight() + 4;
        m_font->drawText(renderer, "5/6: select hazard | +/- radius | arrows move | H toggle | [/] weight", 18, y, nullptr,
                         SDL_Color{220, 240, 255, 255});
        y += m_font->getLineHeight() + 6;
        if (m_hasStageConfig && !m_stageConfig.enemyBases.empty() && m_selectedEnemyBase >= 0)
        {
            const auto &base = m_stageConfig.enemyBases[static_cast<std::size_t>(m_selectedEnemyBase)];
            std::ostringstream oss;
            oss << "Enemy base[" << m_selectedEnemyBase << "] pos=(" << static_cast<int>(base.position.x) << ","
                << static_cast<int>(base.position.y) << ") r=" << static_cast<int>(base.radiusPx);
            m_font->drawText(renderer, oss.str(), 18, y, nullptr, SDL_Color{255, 220, 180, 255});
            y += m_font->getLineHeight() + 4;
        }
        if (m_hasStageConfig && !m_stageConfig.enemyBases.empty() && m_selectedEnemyBase >= 0)
        {
            const auto &base = m_stageConfig.enemyBases[static_cast<std::size_t>(m_selectedEnemyBase)];
            std::ostringstream oss2;
            oss2 << "  rate=" << base.ratePerSecond << " max=" << base.rateMax;
            m_font->drawText(renderer, oss2.str(), 28, y, nullptr, SDL_Color{200, 230, 255, 255});
            y += m_font->getLineHeight() + 2;
        }
        if (m_hasStageConfig && !m_stageConfig.hazards.empty() && m_selectedHazard >= 0)
        {
            const auto &haz = m_stageConfig.hazards[static_cast<std::size_t>(m_selectedHazard)];
            std::ostringstream oss;
            oss << "Hazard[" << m_selectedHazard << "] pos=(" << static_cast<int>(haz.position.x) << ","
                << static_cast<int>(haz.position.y) << ") r=" << static_cast<int>(haz.radiusPx);
            m_font->drawText(renderer, oss.str(), 18, y, nullptr, SDL_Color{180, 235, 180, 255});
            y += m_font->getLineHeight() + 4;
            std::ostringstream hs;
            hs << "  weight=" << haz.weight << " enabled=" << (haz.enabled ? "true" : "false");
            m_font->drawText(renderer, hs.str(), 28, y, nullptr, SDL_Color{180, 235, 200, 255});
            y += m_font->getLineHeight() + 2;
        }
        if (m_showDebug)
        {
            std::ostringstream oss;
            oss << "draw calls: " << stats.drawCalls;
            m_font->drawText(renderer, oss.str(), 18, y, nullptr, SDL_Color{200, 240, 255, 255});
        }
        if (!m_feedback.empty() && m_feedbackTimer > 0.0)
        {
            y += m_font->getLineHeight() + 4;
            m_font->drawText(renderer, m_feedback, 18, y, nullptr, SDL_Color{255, 240, 180, 255});
        }
    }
}

void EditorScene::loadResources(GameApplication &app)
{
    m_status.clear();
    m_tileMap = std::make_unique<TileMap>();
    m_atlas = std::make_unique<Atlas>();
    m_camera = std::make_unique<Camera>();
    m_font = std::make_unique<TextRenderer>();
    m_debugFont = std::make_unique<TextRenderer>();
    m_sim = std::make_unique<world::LegacySimulation>();

    AssetManager &assets = app.assetManager();
    const AppConfig &cfg = app.appConfig();

    if (!m_font->load(assets, "ui/NotoSansJP-Regular.ttf", 20))
    {
        m_status = "Failed to load UI font";
    }
    if (!m_debugFont->load(assets, "ui/NotoSansJP-Regular.ttf", 16))
    {
        m_status = "Failed to load debug font";
    }

    if (!loadTileMap(assets, cfg.game.map_path, *m_tileMap))
    {
        m_status = "Failed to load map";
    }
    if (!loadAtlas(assets, cfg.atlasPath, *m_atlas))
    {
        m_status = "Failed to load atlas";
    }

    if (!cfg.stageConfig)
    {
        m_status = "stage_config not loaded";
        return;
    }
    m_stageConfigPath = cfg.stageConfigPath;
    m_stageConfig = *cfg.stageConfig;
    m_hasStageConfig = true;

    m_sim->config = cfg.game;
    m_sim->mapDefs = cfg.mapDefs;
    m_sim->spawnScript = cfg.spawnScript;
    m_sim->economyConfig = cfg.economy;
    m_sim->configureStage(m_stageConfig);
    m_sim->basePos = {m_tileMap->width * m_tileMap->tileWidth * 0.5f, m_tileMap->height * m_tileMap->tileHeight * 0.5f};

    m_camera->position = {m_sim->basePos.x - app.windowWidth() * 0.5f, m_sim->basePos.y - app.windowHeight() * 0.5f};

    m_loaded = m_status.empty();
    clampSelection();
    showFeedback("Stage loaded", 2.0);
}

void EditorScene::renderStatus(SDL_Renderer *renderer, GameApplication &app)
{
    SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
    SDL_RenderClear(renderer);
    if (!m_font || !m_font->isLoaded())
    {
        return;
    }
    std::string text = m_status.empty() ? "Loading..." : m_status;
    int x = 24;
    int y = app.windowHeight() / 2;
    m_font->drawText(renderer, text, x, y, nullptr, SDL_Color{255, 200, 200, 255});
}

void EditorScene::handleCameraInput(double deltaSeconds)
{
    if (!m_camera)
    {
        return;
    }
    const Uint8 *keys = SDL_GetKeyboardState(nullptr);
    Vec2 delta{0.0f, 0.0f};
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])
    {
        delta.x -= 1.0f;
    }
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT])
    {
        delta.x += 1.0f;
    }
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])
    {
        delta.y -= 1.0f;
    }
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])
    {
        delta.y += 1.0f;
    }
    if (delta.x != 0.0f || delta.y != 0.0f)
    {
        const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        delta = delta / std::max(len, 0.0001f);
        m_camera->position += delta * static_cast<float>(m_camera->speed * deltaSeconds);
    }
}

void EditorScene::handleEditingInput(const SDL_Event &event, GameApplication &app)
{
    if (!m_sim || !m_camera || !m_hasStageConfig)
    {
        return;
    }
    if (event.type == SDL_KEYDOWN)
    {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_r)
        {
            reload(app);
            return;
        }
        const bool shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
        const float stepPx = shift ? 4.0f : 16.0f;
        const float tileSize = static_cast<float>(std::max(1, m_sim->mapDefs.tile_size));
        const float stepTiles = stepPx / tileSize;
        auto &stage = m_stageConfig;
        if (key == SDLK_1)
        {
            m_selectedEnemyBase = std::max(0, m_selectedEnemyBase - 1);
        }
        else if (key == SDLK_2)
        {
            m_selectedEnemyBase = std::min(static_cast<int>(stage.enemyBases.size()) - 1, m_selectedEnemyBase + 1);
        }
        else if (key == SDLK_3)
        {
            m_selectedAllyBase = std::max(0, m_selectedAllyBase - 1);
        }
        else if (key == SDLK_4)
        {
            m_selectedAllyBase = std::min(static_cast<int>(stage.allyBases.size()) - 1, m_selectedAllyBase + 1);
        }
        else if (key == SDLK_EQUALS || key == SDLK_PLUS)
        {
            if (m_selectedEnemyBase >= 0 && m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
            {
                stage.enemyBases[m_selectedEnemyBase].radiusPx += stepPx;
            }
        }
        else if (key == SDLK_MINUS)
        {
            if (m_selectedEnemyBase >= 0 && m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
            {
                stage.enemyBases[m_selectedEnemyBase].radiusPx =
                    std::max(4.0f, stage.enemyBases[m_selectedEnemyBase].radiusPx - stepPx);
            }
        }
        else if (key == SDLK_5)
        {
            m_selectedHazard = std::max(0, m_selectedHazard - 1);
        }
        else if (key == SDLK_6)
        {
            m_selectedHazard = std::min(static_cast<int>(stage.hazards.size()) - 1, m_selectedHazard + 1);
        }
        else if ((key == SDLK_PLUS || key == SDLK_EQUALS) && m_selectedHazard >= 0 &&
                 m_selectedHazard < static_cast<int>(stage.hazards.size()))
        {
            stage.hazards[m_selectedHazard].radiusPx += stepPx;
        }
        else if (key == SDLK_MINUS && m_selectedHazard >= 0 && m_selectedHazard < static_cast<int>(stage.hazards.size()))
        {
            stage.hazards[m_selectedHazard].radiusPx =
                std::max(4.0f, stage.hazards[m_selectedHazard].radiusPx - stepPx);
        }
        else if (key == SDLK_h && m_selectedHazard >= 0 && m_selectedHazard < static_cast<int>(stage.hazards.size()))
        {
            stage.hazards[m_selectedHazard].enabled = !stage.hazards[m_selectedHazard].enabled;
        }
        else if (key == SDLK_LEFTBRACKET && m_selectedHazard >= 0 &&
                 m_selectedHazard < static_cast<int>(stage.hazards.size()))
        {
            stage.hazards[m_selectedHazard].weight = std::max(0.0f, stage.hazards[m_selectedHazard].weight - 0.1f);
        }
        else if (key == SDLK_RIGHTBRACKET && m_selectedHazard >= 0 &&
                 m_selectedHazard < static_cast<int>(stage.hazards.size()))
        {
            stage.hazards[m_selectedHazard].weight = stage.hazards[m_selectedHazard].weight + 0.1f;
        }
        else if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN)
        {
            Vec2 delta{0.0f, 0.0f};
            if (key == SDLK_LEFT)
            {
                delta.x -= stepTiles;
            }
            if (key == SDLK_RIGHT)
            {
                delta.x += stepTiles;
            }
            if (key == SDLK_UP)
            {
                delta.y -= stepTiles;
            }
            if (key == SDLK_DOWN)
            {
                delta.y += stepTiles;
            }
            if (m_selectedEnemyBase >= 0 && m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
            {
                stage.enemyBases[m_selectedEnemyBase].position += delta;
            }
            if (m_selectedAllyBase >= 0 && m_selectedAllyBase < static_cast<int>(stage.allyBases.size()))
            {
                stage.allyBases[m_selectedAllyBase].position += delta;
            }
            if (m_selectedHazard >= 0 && m_selectedHazard < static_cast<int>(stage.hazards.size()))
            {
                stage.hazards[m_selectedHazard].position += delta;
            }
        }
        else if (key == SDLK_COMMA && m_selectedEnemyBase >= 0 &&
                 m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
        {
            stage.enemyBases[m_selectedEnemyBase].ratePerSecond =
                std::max(0.0f, stage.enemyBases[m_selectedEnemyBase].ratePerSecond - 0.01f);
        }
        else if (key == SDLK_PERIOD && m_selectedEnemyBase >= 0 &&
                 m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
        {
            stage.enemyBases[m_selectedEnemyBase].ratePerSecond += 0.01f;
        }
        else if (key == SDLK_SEMICOLON && m_selectedEnemyBase >= 0 &&
                 m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
        {
            stage.enemyBases[m_selectedEnemyBase].rateMax =
                std::max(0.0f, stage.enemyBases[m_selectedEnemyBase].rateMax - 0.01f);
        }
        else if (key == SDLK_QUOTE && m_selectedEnemyBase >= 0 &&
                 m_selectedEnemyBase < static_cast<int>(stage.enemyBases.size()))
        {
            stage.enemyBases[m_selectedEnemyBase].rateMax += 0.01f;
        }
        else if (key == SDLK_s && ((SDL_GetModState() & KMOD_CTRL) || (SDL_GetModState() & KMOD_GUI)))
        {
            SaveFileOptions opts;
            opts.createBackup = true;
            if (m_stageConfigPath.empty())
            {
                showFeedback("No stage_config path", 2.0);
                return;
            }
            SaveFileResult result = saveStageConfig(m_stageConfig, m_stageConfigPath, opts);
            showFeedback(result.ok ? "Saved stage config" : ("Save failed: " + result.error), 3.0);
        }

        m_sim->configureStage(m_stageConfig);
        clampSelection();
    }
}

void EditorScene::reload(GameApplication &app)
{
    if (app.reloadConfig())
    {
        loadResources(app);
        showFeedback("Config reloaded", 2.0);
    }
}

void EditorScene::clampSelection()
{
    if (!m_sim)
    {
        return;
    }
    const auto &stage = m_sim->stageState();
    if (stage.enemyBases.empty())
    {
        m_selectedEnemyBase = -1;
    }
    else
    {
        m_selectedEnemyBase = std::clamp(m_selectedEnemyBase, 0, static_cast<int>(stage.enemyBases.size() - 1));
    }
    if (stage.allyBases.empty())
    {
        m_selectedAllyBase = -1;
    }
    else
    {
        m_selectedAllyBase = std::clamp(m_selectedAllyBase, 0, static_cast<int>(stage.allyBases.size() - 1));
    }
    if (stage.hazards.empty())
    {
        m_selectedHazard = -1;
    }
    else
    {
        m_selectedHazard = std::clamp(m_selectedHazard, 0, static_cast<int>(stage.hazards.size() - 1));
    }
}

void EditorScene::showFeedback(const std::string &text, double seconds)
{
    m_feedback = text;
    m_feedbackTimer = std::max(0.0, seconds);
}
