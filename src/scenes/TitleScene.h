#pragma once

#include <SDL.h>

#include <memory>
#include <string>

#include "app/TextRenderer.h"
#include "scenes/Scene.h"

struct CampaignState;
class GameApplication;
class SceneStack;

class TitleScene : public Scene
{
  public:
    explicit TitleScene(std::shared_ptr<CampaignState> campaign);

    void onEnter(GameApplication &app, SceneStack &) override;
    void onExit(GameApplication &, SceneStack &) override;
    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override;
    void update(double, GameApplication &, SceneStack &) override;
    void render(SDL_Renderer *renderer, GameApplication &app) override;

  private:
    int measureText(const TextRenderer &renderer, const std::string &text, int approxHeight) const;
    void start(SceneStack &stack);

    std::shared_ptr<CampaignState> m_campaign;
    TextRenderer m_titleFont;
    TextRenderer m_bodyFont;
    bool m_loadedOnce = false;
    bool m_started = false;
};
