#include "scenes/SceneStack.h"

#include "app/GameApplication.h"
#include "scenes/Scene.h"

SceneStack::SceneStack(GameApplication &app) : m_app(app) {}

SceneStack::~SceneStack()
{
    clear();
}

void SceneStack::push(std::unique_ptr<Scene> scene)
{
    if (!scene)
    {
        return;
    }
    m_pendingScenes.emplace_back(std::move(scene));
    if (m_app.isRendererReady())
    {
        activatePendingScenes();
    }
}

void SceneStack::pop()
{
    if (m_scenes.empty())
    {
        return;
    }
    auto scene = std::move(m_scenes.back());
    m_scenes.pop_back();
    if (scene)
    {
        scene->onExit(m_app, *this);
    }
}

void SceneStack::clear()
{
    for (auto it = m_scenes.rbegin(); it != m_scenes.rend(); ++it)
    {
        if (*it)
        {
            (*it)->onExit(m_app, *this);
        }
    }
    m_scenes.clear();
    m_pendingScenes.clear();
}

void SceneStack::handleEvent(const SDL_Event &event)
{
    activatePendingScenes();
    for (auto &scene : m_scenes)
    {
        if (scene)
        {
            scene->handleEvent(event, m_app, *this);
        }
    }
}

void SceneStack::update(double deltaSeconds)
{
    activatePendingScenes();
    for (auto &scene : m_scenes)
    {
        if (scene)
        {
            scene->update(deltaSeconds, m_app, *this);
        }
    }
}

void SceneStack::render(SDL_Renderer *renderer)
{
    activatePendingScenes();
    for (auto &scene : m_scenes)
    {
        if (scene)
        {
            scene->render(renderer, m_app);
        }
    }
}

bool SceneStack::empty() const
{
    return m_scenes.empty() && m_pendingScenes.empty();
}

GameApplication &SceneStack::app()
{
    return m_app;
}

void SceneStack::onRendererReady()
{
    activatePendingScenes();
}

void SceneStack::activatePendingScenes()
{
    if (!m_app.isRendererReady())
    {
        return;
    }

    for (auto &scene : m_pendingScenes)
    {
        if (scene)
        {
            scene->onEnter(m_app, *this);
            m_scenes.emplace_back(std::move(scene));
        }
    }
    m_pendingScenes.clear();
}

