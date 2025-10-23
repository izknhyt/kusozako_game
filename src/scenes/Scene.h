#pragma once

#include <SDL.h>

class GameApplication;
class SceneStack;

class Scene
{
  public:
    virtual ~Scene() = default;

    virtual void onEnter(GameApplication &, SceneStack &) {}
    virtual void onExit(GameApplication &, SceneStack &) {}
    virtual void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) = 0;
    virtual void update(double deltaSeconds, GameApplication &app, SceneStack &stack) = 0;
    virtual void render(SDL_Renderer *renderer, GameApplication &app) = 0;
};

