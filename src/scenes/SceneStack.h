#pragma once

#include <memory>
#include <vector>

#include <SDL.h>

class GameApplication;
class Scene;

class SceneStack
{
  public:
    explicit SceneStack(GameApplication &app);
    ~SceneStack();

    void push(std::unique_ptr<Scene> scene);
    void pop();
    void clear();

    void handleEvent(const SDL_Event &event);
    void update(double deltaSeconds);
    void render(SDL_Renderer *renderer);

    bool empty() const;

    GameApplication &app();

    void onRendererReady();

  private:
    void activatePendingScenes();

    GameApplication &m_app;
    std::vector<std::unique_ptr<Scene>> m_scenes;
    std::vector<std::unique_ptr<Scene>> m_pendingScenes;
};

