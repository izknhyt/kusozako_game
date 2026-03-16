#include "scenes/TitleScene.h"

#include "app/GameApplication.h"
#include "assets/AssetManager.h"
#include "game/CampaignState.h"
#include "scenes/BattleScene.h"
#include "scenes/CampScene.h"
#include "scenes/SceneStack.h"

#include <algorithm>
#include <iostream>

TitleScene::TitleScene(std::shared_ptr<CampaignState> campaign) : m_campaign(std::move(campaign)) {}

void TitleScene::onEnter(GameApplication &app, SceneStack &)
{
    AssetManager &assets = app.assetManager();
    constexpr const char *kFontPath = "assets/ui/NotoSansJP-Regular.ttf";
    if (!m_titleFont.isLoaded())
    {
        m_titleFont.load(assets, kFontPath, 48);
    }
    if (!m_bodyFont.isLoaded())
    {
        m_bodyFont.load(assets, kFontPath, 24);
    }
    if (m_campaign && !m_loadedOnce)
    {
        m_campaign->loadFromDisk();
        m_loadedOnce = true;
    }
}

void TitleScene::onExit(GameApplication &, SceneStack &) {}

void TitleScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    if (m_started)
    {
        return;
    }
    if (event.type == SDL_KEYDOWN)
    {
        const SDL_Keycode key = event.key.keysym.sym;
        if (key == SDLK_ESCAPE)
        {
            app.requestQuit();
            return;
        }
        if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_SPACE)
        {
            start(stack);
        }
    }
    else if (event.type == SDL_MOUSEBUTTONDOWN)
    {
        if (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
        {
            start(stack);
        }
    }
}

void TitleScene::update(double, GameApplication &, SceneStack &) {}

void TitleScene::render(SDL_Renderer *renderer, GameApplication &app)
{
    if (m_started)
    {
        return;
    }
    if (!renderer)
    {
        return;
    }
    SDL_SetRenderDrawColor(renderer, 12, 16, 24, 255);
    SDL_RenderClear(renderer);

    const int screenW = app.windowWidth();
    const int screenH = app.windowHeight();
    const char *title = "Kusozako Trial";
    const char *prompt = "Press Enter/Space to start";
    const char *subtitle = "Save will load into Camp, then battle";
    const int titleSize = 48;
    const int bodySize = 24;
    const int titleW = m_titleFont.isLoaded() ? measureText(m_titleFont, title, titleSize) : 0;
    const int promptW = m_bodyFont.isLoaded() ? measureText(m_bodyFont, prompt, bodySize) : 0;
    const int subtitleW = m_bodyFont.isLoaded() ? measureText(m_bodyFont, subtitle, bodySize - 2) : 0;
    const int centerX = screenW / 2;
    const int centerY = screenH / 2;
    if (m_titleFont.isLoaded())
    {
        m_titleFont.drawText(renderer,
                             title,
                             centerX - titleW / 2,
                             centerY - titleSize - 20,
                             nullptr,
                             SDL_Color{255, 230, 200, 255});
    }
    if (m_bodyFont.isLoaded())
    {
        m_bodyFont.drawText(renderer,
                            prompt,
                            centerX - promptW / 2,
                            centerY + 6,
                            nullptr,
                            SDL_Color{200, 220, 255, 255});
        m_bodyFont.drawText(renderer,
                            subtitle,
                            centerX - subtitleW / 2,
                            centerY + bodySize + 14,
                            nullptr,
                            SDL_Color{180, 200, 230, 220});
    }
}

int TitleScene::measureText(const TextRenderer &renderer, const std::string &text, int approxHeight) const
{
    const int measured = renderer.measureText(text);
    if (measured > 0)
    {
        return measured;
    }
    const int approxWidth = std::max(approxHeight / 2, 8);
    return static_cast<int>(text.size()) * approxWidth;
}

void TitleScene::start(SceneStack &stack)
{
    if (m_started)
    {
        return;
    }
    m_started = true;
    auto battle = std::make_unique<BattleScene>(m_campaign, true);
    BattleScene *battlePtr = battle.get();
    stack.push(std::move(battle));
    stack.push(std::make_unique<CampScene>(m_campaign, battlePtr));
}
