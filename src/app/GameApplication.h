#pragma once

#include <memory>
#include <string>

#include <SDL.h>

#include "scenes/SceneStack.h"

class GameApplication
{
  public:
    GameApplication();
    ~GameApplication();

    int run();

    SceneStack &sceneStack();

    SDL_Window *window() const;
    SDL_Renderer *renderer() const;

    int windowWidth() const;
    int windowHeight() const;

    bool isRendererReady() const;

    void requestQuit();

  private:
    bool initialize();
    void shutdown();

    SDL_Window *m_window = nullptr;
    SDL_Renderer *m_renderer = nullptr;

    bool m_running = false;
    bool m_quitRequested = false;
    bool m_initialized = false;

    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    std::string m_windowTitle = "Kusozako MVP";

    SceneStack m_sceneStack;
};

