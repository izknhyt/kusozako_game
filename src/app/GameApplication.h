#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <SDL.h>

#include "assets/AssetManager.h"
#include "config/AppConfigLoader.h"
#include "input/InputMapper.h"
#include "scenes/SceneStack.h"

class EventBus;
class TelemetrySink;

class GameApplication
{
  public:
    explicit GameApplication(std::shared_ptr<AppConfigLoader> configLoader);
    ~GameApplication();

    int run();

    SceneStack &sceneStack();
    AssetManager &assetManager();
    const AssetManager &assetManager() const;

    const AppConfig &appConfig() const;
    const AppConfigLoadResult &appConfigResult() const;

    InputMapper &inputMapper() { return m_inputMapper; }
    const InputMapper &inputMapper() const { return m_inputMapper; }

    SDL_Window *window() const;
    SDL_Renderer *renderer() const;

    int windowWidth() const;
    int windowHeight() const;

    bool isRendererReady() const;

    void requestQuit();

    void setTelemetryOutputDirectory(const std::filesystem::path &path);

    bool reloadConfig();

  private:
    bool initialize();
    void shutdown();
    void registerCoreServices();
    void unregisterCoreServices();
    void applyTelemetrySettings();

    SDL_Window *m_window = nullptr;
    SDL_Renderer *m_renderer = nullptr;

    bool m_running = false;
    bool m_quitRequested = false;
    bool m_initialized = false;

    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    std::string m_windowTitle = "Kusozako MVP";

    SceneStack m_sceneStack;
    AssetManager m_assetManager;
    std::shared_ptr<AppConfigLoader> m_configLoader;
    AppConfigLoadResult m_appConfigResult;
    std::shared_ptr<TelemetrySink> m_telemetrySink;
    std::shared_ptr<EventBus> m_eventBus;
    std::shared_ptr<AssetManager> m_assetManagerHandle;
    InputMapper m_inputMapper;
    std::filesystem::path m_defaultTelemetryDir{std::filesystem::path("build") / "debug_dumps"};
    std::optional<std::filesystem::path> m_telemetryDirOverride;
    std::optional<std::uintmax_t> m_telemetryRotationOverride;
    std::optional<std::size_t> m_telemetryRetentionOverride;
};

