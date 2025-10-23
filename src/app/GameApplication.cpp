#include "app/GameApplication.h"

#include <SDL_image.h>
#include <SDL_ttf.h>

#include <filesystem>
#include <iostream>
#include <memory>

#include "json/JsonUtils.h"

GameApplication::GameApplication(std::shared_ptr<AppConfigLoader> configLoader)
    : m_sceneStack(*this), m_configLoader(std::move(configLoader))
{
}

GameApplication::~GameApplication()
{
    shutdown();
}

SceneStack &GameApplication::sceneStack()
{
    return m_sceneStack;
}

AssetManager &GameApplication::assetManager()
{
    return m_assetManager;
}

const AssetManager &GameApplication::assetManager() const
{
    return m_assetManager;
}

const AppConfig &GameApplication::appConfig() const
{
    return m_appConfigResult.config;
}

const AppConfigLoadResult &GameApplication::appConfigResult() const
{
    return m_appConfigResult;
}

SDL_Window *GameApplication::window() const
{
    return m_window;
}

SDL_Renderer *GameApplication::renderer() const
{
    return m_renderer;
}

int GameApplication::windowWidth() const
{
    return m_windowWidth;
}

int GameApplication::windowHeight() const
{
    return m_windowHeight;
}

bool GameApplication::isRendererReady() const
{
    return m_renderer != nullptr;
}

void GameApplication::requestQuit()
{
    m_quitRequested = true;
}

int GameApplication::run()
{
    if (!initialize())
    {
        m_sceneStack.clear();
        return 1;
    }

    const double frequency = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 prevCounter = SDL_GetPerformanceCounter();

    while (m_running && !m_quitRequested)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                m_quitRequested = true;
            }
            m_sceneStack.handleEvent(event);
        }

        if (m_quitRequested)
        {
            break;
        }

        const Uint64 nowCounter = SDL_GetPerformanceCounter();
        const double deltaSeconds = (nowCounter - prevCounter) / frequency;
        prevCounter = nowCounter;

        m_sceneStack.update(deltaSeconds);

        if (m_quitRequested)
        {
            break;
        }

        m_sceneStack.render(m_renderer);
        SDL_RenderPresent(m_renderer);

        if (m_sceneStack.empty())
        {
            m_quitRequested = true;
        }
    }

    shutdown();
    return 0;
}

bool GameApplication::initialize()
{
    if (m_initialized)
    {
        return true;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return false;
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0)
    {
        std::cerr << "IMG_Init failed: " << IMG_GetError() << '\n';
        SDL_Quit();
        return false;
    }

    if (TTF_Init() != 0)
    {
        std::cerr << "TTF_Init failed: " << TTF_GetError() << '\n';
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    m_window = SDL_CreateWindow(m_windowTitle.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, m_windowWidth,
                                m_windowHeight, SDL_WINDOW_SHOWN);
    if (!m_window)
    {
        std::cerr << "Failed to create window: " << SDL_GetError() << '\n';
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!m_renderer)
    {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        TTF_Quit();
        IMG_Quit();
        SDL_Quit();
        return false;
    }

    m_assetManager.setRenderer(m_renderer);
    const std::filesystem::path rootPath = std::filesystem::absolute("assets");
    m_assetManager.setAssetRoot(rootPath.string());

    SDL_Texture *fallbackTexture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    if (fallbackTexture)
    {
        const Uint32 transparent = 0u;
        SDL_UpdateTexture(fallbackTexture, nullptr, &transparent, sizeof(transparent));
        SDL_SetTextureBlendMode(fallbackTexture, SDL_BLENDMODE_BLEND);
        m_assetManager.setFallbackTexture(AssetManager::TexturePtr(fallbackTexture, [](SDL_Texture *tex) {
            if (tex) SDL_DestroyTexture(tex);
        }));
    }
    else
    {
        std::cerr << "Failed to create fallback texture: " << SDL_GetError() << '\n';
        m_assetManager.setFallbackTexture(nullptr);
    }

    const std::string fallbackFontPath = m_assetManager.resolvePath("ui/NotoSansJP-Regular.ttf");
    TTF_Font *fallbackFont = TTF_OpenFont(fallbackFontPath.c_str(), 20);
    if (fallbackFont)
    {
        m_assetManager.setFallbackFont(AssetManager::FontPtr(fallbackFont, [](TTF_Font *font) {
            if (font) TTF_CloseFont(font);
        }));
    }
    else
    {
        std::cerr << "Failed to load fallback font: " << fallbackFontPath << " -> " << TTF_GetError() << '\n';
        m_assetManager.setFallbackFont(nullptr);
    }

    auto fallbackJson = std::make_shared<json::JsonValue>();
    fallbackJson->type = json::JsonValue::Type::Object;
    m_assetManager.setFallbackJson(std::move(fallbackJson));

    if (m_configLoader)
    {
        m_appConfigResult = m_configLoader->load(m_assetManager);
        for (const auto &error : m_appConfigResult.errors)
        {
            std::cerr << "[config] " << error.file << ": " << error.message << '\n';
        }
    }
    else
    {
        m_appConfigResult = {};
        m_appConfigResult.config = AppConfig{};
        m_appConfigResult.config.skills = buildDefaultSkills();
        m_appConfigResult.success = false;
    }

    m_running = true;
    m_quitRequested = false;
    m_initialized = true;

    m_sceneStack.onRendererReady();

    return true;
}

void GameApplication::shutdown()
{
    if (!m_initialized)
    {
        return;
    }

    m_sceneStack.clear();
    m_assetManager.clear();

    if (m_renderer)
    {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();

    m_running = false;
    m_quitRequested = false;
    m_initialized = false;
}

