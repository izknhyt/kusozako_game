#include "scenes/CampScene.h"

#include "app/GameApplication.h"
#include "assets/AssetManager.h"
#include "config/AppConfig.h"
#include "game/CampaignState.h"
#include "game/CampShop.h"
#include "game/TrainingMath.h"
#include "game/AllyLevelingConfig.h"
#include "scenes/BattleScene.h"
#include "scenes/SceneStack.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

CampScene::CampScene(std::shared_ptr<CampaignState> campaign, BattleScene *battle)
    : m_campaign(std::move(campaign)), m_battle(battle)
{
}

void CampScene::onEnter(GameApplication &app, SceneStack &)
{
    AssetManager &assets = app.assetManager();
    constexpr const char *kFontPath = "assets/ui/NotoSansJP-Regular.ttf";
    if (!m_titleFont.isLoaded())
    {
        if (!m_titleFont.load(assets, kFontPath, 28))
        {
            std::cerr << "[camp] Failed to load title font (" << kFontPath << ")\n";
        }
    }
    if (!m_bodyFont.isLoaded())
    {
        if (!m_bodyFont.load(assets, kFontPath, 22))
        {
            std::cerr << "[camp] Failed to load body font (" << kFontPath << ")\n";
        }
    }

    autoFocusAll(app.appConfig());
}

void CampScene::onExit(GameApplication &, SceneStack &) {}

void CampScene::handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack)
{
    const AppConfig &config = app.appConfig();
    switch (event.type)
    {
    case SDL_KEYDOWN:
    {
        const SDL_Keycode key = event.key.keysym.sym;
        switch (key)
        {
        case SDLK_ESCAPE:
        case SDLK_SPACE:
            deploy(stack, app);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            handlePurchase(config);
            break;
        case SDLK_UP:
            moveSelection(-1, config);
            break;
        case SDLK_DOWN:
            moveSelection(1, config);
            break;
        case SDLK_LEFT:
            if (m_tab == Tab::Strategies)
            {
                if (const StrategyCharacter *character = strategyAt(config, currentSelection()))
                {
                    switchStrategyOption(*character, -1);
                }
            }
            break;
        case SDLK_RIGHT:
            if (m_tab == Tab::Strategies)
            {
                if (const StrategyCharacter *character = strategyAt(config, currentSelection()))
                {
                    switchStrategyOption(*character, 1);
                }
            }
            break;
        case SDLK_z:
            if (hasUndo())
            {
                performUndo(config);
            }
            break;
        case SDLK_1:
        case SDLK_2:
        case SDLK_3:
        case SDLK_4:
        {
            const int idx = static_cast<int>(key - SDLK_1);
            if (idx >= 0 && idx < 4)
            {
                setTab(static_cast<Tab>(idx), config);
            }
            break;
        }
        case SDLK_F9:
        {
            m_campaign->debugDisableYunaSpawns = !m_campaign->debugDisableYunaSpawns;
            m_feedbackTimer = 1.5;
            m_feedbackText = m_campaign->debugDisableYunaSpawns ? "Chibi spawn: OFF" : "Chibi spawn: ON";
            break;
        }
        case SDLK_F10:
        {
            m_campaign->debugDisableBaseRegenAndDef = !m_campaign->debugDisableBaseRegenAndDef;
            m_feedbackTimer = 1.5;
            m_feedbackText = m_campaign->debugDisableBaseRegenAndDef ? "Base regen/def: OFF" : "Base regen/def: ON";
            break;
        }
        default:
            break;
        }
        break;
    }
    case SDL_MOUSEMOTION:
        handleMouseMotion(event.motion);
        break;
    case SDL_MOUSEBUTTONDOWN:
        handleMouseButtonDown(event.button, config);
        break;
    case SDL_MOUSEBUTTONUP:
        handleMouseButtonUp(event.button, config, stack, app);
        break;
    case SDL_WINDOWEVENT:
        if (event.window.event == SDL_WINDOWEVENT_LEAVE)
        {
            clearPointerHover();
        }
        break;
    default:
        break;
    }
}

void CampScene::update(double deltaSeconds, GameApplication &, SceneStack &)
{
    if (m_feedbackTimer > 0.0)
    {
        m_feedbackTimer = std::max(0.0, m_feedbackTimer - deltaSeconds);
    }
    updateUndo(deltaSeconds);
}

void CampScene::render(SDL_Renderer *renderer, GameApplication &app)
{
    if (!renderer || (!m_titleFont.isLoaded() && !m_bodyFont.isLoaded()))
    {
        return;
    }
    SDL_SetRenderDrawColor(renderer, 8, 6, 18, 255);
    SDL_RenderClear(renderer);

    const AppConfig &config = app.appConfig();
    const int screenW = app.windowWidth();
    const int screenH = app.windowHeight();

    const TextRenderer &titleFont = m_titleFont.isLoaded() ? m_titleFont : m_bodyFont;
    const TextRenderer &bodyFont = m_bodyFont.isLoaded() ? m_bodyFont : m_titleFont;
    const int headerY = 24;
    titleFont.drawText(renderer, "Camp", 32, headerY, nullptr, SDL_Color{255, 230, 180, 255});
    if (m_campaign->debugDisableYunaSpawns)
    {
        bodyFont.drawText(renderer, "[Debug] Chibi spawn OFF (F9)", 220, headerY,
                          nullptr, SDL_Color{255, 120, 120, 230});
    }
    if (m_campaign->debugDisableBaseRegenAndDef)
    {
        bodyFont.drawText(renderer, "[Debug] Base regen/def OFF (F10)", 220, headerY + 26,
                          nullptr, SDL_Color{255, 150, 120, 230});
    }

    std::ostringstream walletText;
    walletText << "Wallet: " << (m_campaign ? m_campaign->availableMana() : 0) << " mana";
    bodyFont.drawText(renderer, walletText.str(), 32, headerY + titleFont.getLineHeight() + 10, nullptr,
                      SDL_Color{210, 220, 255, 255});

    if (m_campaign)
    {
        std::ostringstream tokenText;
        tokenText << "Mana gain tokens: " << m_campaign->manaGainTokens;
        bodyFont.drawText(renderer,
                          tokenText.str(),
                          32,
                          headerY + titleFont.getLineHeight() + bodyFont.getLineHeight() + 18,
                          nullptr,
                          SDL_Color{200, 210, 255, 255});
    }

    beginHitTestFrame();
    renderTabs(renderer, config, headerY + titleFont.getLineHeight() + bodyFont.getLineHeight() + 40);
    renderContent(renderer, config, screenW, screenH);
    renderButtons(renderer, bodyFont, screenW, headerY);
    renderUndoPrompt(renderer, bodyFont, screenW, screenH);

    if (m_feedbackTimer > 0.0 && !m_feedbackText.empty())
    {
        const int textWidth = bodyFont.measureText(m_feedbackText);
        const int x = (screenW - textWidth) / 2;
        const int y = screenH - bodyFont.getLineHeight() - 24;
        bodyFont.drawText(renderer, m_feedbackText, x, y, nullptr, SDL_Color{255, 210, 160, 255});
    }
    refreshHoverTargets();
}

int CampScene::tabIndex(Tab tab) const { return static_cast<int>(tab); }

int CampScene::currentSelection() const
{
    return m_selection[tabIndex(m_tab)];
}

void CampScene::setTab(Tab tab, const AppConfig &config)
{
    if (m_tab == tab)
    {
        return;
    }
    m_tab = tab;
    clampSelection(config);
}

void CampScene::clampSelection(const AppConfig &config)
{
    const int idx = tabIndex(m_tab);
    const int count = entryCountForTab(m_tab, config);
    if (count <= 0)
    {
        m_selection[idx] = 0;
    }
    else if (m_selection[idx] >= count)
    {
        m_selection[idx] = count - 1;
    }
    else if (m_selection[idx] < 0)
    {
        m_selection[idx] = 0;
    }
}

void CampScene::moveSelection(int delta, const AppConfig &config)
{
    if (delta == 0)
    {
        return;
    }
    const int idx = tabIndex(m_tab);
    const int count = entryCountForTab(m_tab, config);
    if (count <= 0)
    {
        m_selection[idx] = 0;
        return;
    }
    int next = m_selection[idx] + delta;
    next = std::clamp(next, 0, std::max(0, count - 1));
    m_selection[idx] = next;
}

void CampScene::handleMouseMotion(const SDL_MouseMotionEvent &motion)
{
    updatePointerPosition(motion.x, motion.y);
}

void CampScene::handleMouseButtonDown(const SDL_MouseButtonEvent &button, const AppConfig &config)
{
    if (button.button != SDL_BUTTON_LEFT)
    {
        return;
    }
    updatePointerPosition(button.x, button.y);
    const SDL_Point point{button.x, button.y};
    int index = -1;
    m_pressTarget.kind = determineTargetAtPoint(point, index);
    m_pressTarget.index = index;
    if (m_pressTarget.kind == PointerTargetKind::Row && index >= 0)
    {
        const int tabIdx = tabIndex(m_tab);
        const int count = entryCountForTab(m_tab, config);
        if (index >= 0 && index < count)
        {
            m_selection[tabIdx] = index;
        }
    }
}

void CampScene::handleMouseButtonUp(const SDL_MouseButtonEvent &button,
                                     const AppConfig &config,
                                     SceneStack &stack,
                                     GameApplication &app)
{
    if (button.button != SDL_BUTTON_LEFT)
    {
        return;
    }
    updatePointerPosition(button.x, button.y);
    const SDL_Point point{button.x, button.y};
    switch (m_pressTarget.kind)
    {
    case PointerTargetKind::Tab:
        if (tabIndexAt(point.x, point.y) == m_pressTarget.index && m_pressTarget.index >= 0 &&
            m_pressTarget.index < static_cast<int>(m_tabRects.size()))
        {
            setTab(static_cast<Tab>(m_pressTarget.index), config);
        }
        break;
    case PointerTargetKind::Row:
        if (rowIndexAt(point.x, point.y) == m_pressTarget.index)
        {
            if (const RowHitbox *hit = rowHitboxForIndex(m_pressTarget.index))
            {
                if (!hit->disabled && hit->affordable)
                {
                    handlePurchase(config);
                }
            }
        }
        break;
    case PointerTargetKind::PurchaseButton:
        if (buttonAt(point.x, point.y) == PointerTargetKind::PurchaseButton)
        {
            handlePurchase(config);
        }
        break;
    case PointerTargetKind::DeployButton:
        if (buttonAt(point.x, point.y) == PointerTargetKind::DeployButton)
        {
            deploy(stack, app);
        }
        break;
    case PointerTargetKind::None:
    default:
        break;
    }
    m_pressTarget = {};
}

void CampScene::beginHitTestFrame()
{
    m_rowHits.clear();
    for (SDL_Rect &rect : m_tabRects)
    {
        rect = SDL_Rect{0, 0, 0, 0};
    }
    m_buttonPurchase = SDL_Rect{0, 0, 0, 0};
    m_buttonDeploy = SDL_Rect{0, 0, 0, 0};
}

void CampScene::updatePointerPosition(int x, int y)
{
    m_pointerPos.x = x;
    m_pointerPos.y = y;
    m_pointerActive = true;
    refreshHoverTargets();
}

void CampScene::refreshHoverTargets()
{
    if (!m_pointerActive)
    {
        m_hoverTab = -1;
        m_hoverRow = -1;
        m_hoverButton = PointerTargetKind::None;
        return;
    }
    m_hoverTab = tabIndexAt(m_pointerPos.x, m_pointerPos.y);
    m_hoverRow = rowIndexAt(m_pointerPos.x, m_pointerPos.y);
    PointerTargetKind buttonTarget = buttonAt(m_pointerPos.x, m_pointerPos.y);
    if (buttonTarget == PointerTargetKind::PurchaseButton || buttonTarget == PointerTargetKind::DeployButton)
    {
        m_hoverButton = buttonTarget;
    }
    else
    {
        m_hoverButton = PointerTargetKind::None;
    }
}

void CampScene::clearPointerHover()
{
    m_pointerActive = false;
    refreshHoverTargets();
}

CampScene::PointerTargetKind CampScene::determineTargetAtPoint(const SDL_Point &point, int &indexOut) const
{
    if (PointerTargetKind buttonTarget = buttonAt(point.x, point.y); buttonTarget != PointerTargetKind::None)
    {
        indexOut = -1;
        return buttonTarget;
    }
    const int row = rowIndexAt(point.x, point.y);
    if (row >= 0)
    {
        indexOut = row;
        return PointerTargetKind::Row;
    }
    const int tabHit = tabIndexAt(point.x, point.y);
    if (tabHit >= 0)
    {
        indexOut = tabHit;
        return PointerTargetKind::Tab;
    }
    indexOut = -1;
    return PointerTargetKind::None;
}

void CampScene::handlePurchase(const AppConfig &config)
{
    if (!m_campaign)
    {
        pushFeedback("No campaign data");
        return;
    }
    switch (m_tab)
    {
    case Tab::Upgrades:
        if (const CampUpgradeEntry *entry = upgradeAt(config, currentSelection()))
        {
            if (purchaseUpgrade(*entry))
            {
                autoFocusTab(m_tab, config);
            }
        }
        break;
    case Tab::Training:
        if (const TrainingEntry *entry = trainingAt(config, currentSelection()))
        {
            if (purchaseTraining(*entry))
            {
                autoFocusTab(m_tab, config);
            }
        }
        break;
    case Tab::Shop:
        if (const MetaShopItem *item = metaAt(config, currentSelection()))
        {
            if (purchaseMeta(*item))
            {
                autoFocusTab(m_tab, config);
            }
        }
        break;
    case Tab::Strategies:
        if (const StrategyCharacter *character = strategyAt(config, currentSelection()))
        {
            switchStrategyOption(*character, 1);
        }
        break;
    }
}

bool CampScene::purchaseUpgrade(const CampUpgradeEntry &entry)
{
    if (!m_campaign)
    {
        return false;
    }
    CampShop shop(*m_campaign);
    CampShop::PurchaseOutcome outcome = shop.buyUpgrade(entry);
    switch (outcome.status)
    {
    case CampShop::Status::Success:
    {
        UndoRecord record;
        record.kind = UndoRecord::Kind::CampUpgrade;
        record.id = entry.id;
        record.previousLevel = outcome.previousLevel;
        record.cost = outcome.cost;
        beginUndo(record);
        pushFeedback(entry.label + " を強化しました");
        m_campaign->saveToDisk();
        return true;
    }
    case CampShop::Status::MaxLevel:
        pushFeedback(entry.label + " は最大です");
        break;
    case CampShop::Status::InsufficientMana:
        pushFeedback("マナが足りません");
        break;
    case CampShop::Status::Invalid:
    default:
        pushFeedback("購入できません");
        break;
    }
    return false;
}

bool CampScene::purchaseTraining(const TrainingEntry &entry)
{
    if (!m_campaign)
    {
        return false;
    }
    CampShop shop(*m_campaign);
    CampShop::PurchaseOutcome outcome = shop.buyTraining(entry);
    switch (outcome.status)
    {
    case CampShop::Status::Success:
    {
        UndoRecord record;
        record.kind = UndoRecord::Kind::Training;
        record.id = entry.id;
        record.previousLevel = outcome.previousLevel;
        record.cost = outcome.cost;
        beginUndo(record);
        pushFeedback(entry.label + " を伸ばしました");
        m_campaign->saveToDisk();
        return true;
    }
    case CampShop::Status::MaxLevel:
        pushFeedback(entry.label + " は完了済み");
        break;
    case CampShop::Status::InsufficientMana:
        pushFeedback("マナが足りません");
        break;
    case CampShop::Status::Invalid:
    default:
        pushFeedback("購入できません");
        break;
    }
    return false;
}

bool CampScene::purchaseMeta(const MetaShopItem &item)
{
    if (!m_campaign)
    {
        return false;
    }
    CampShop shop(*m_campaign);
    CampShop::PurchaseOutcome outcome = shop.buyMeta(item);
    switch (outcome.status)
    {
    case CampShop::Status::Success:
    {
        UndoRecord record;
        record.cost = outcome.cost;
        record.previousLevel = outcome.previousLevel;
        record.previousTokens = outcome.previousTokens;
        if (outcome.consumable)
        {
            record.kind = UndoRecord::Kind::Token;
            pushFeedback(item.label + " を補充しました");
        }
        else
        {
            record.kind = UndoRecord::Kind::Meta;
            record.id = item.id;
            pushFeedback(item.label + " を強化しました");
        }
        beginUndo(record);
        m_campaign->saveToDisk();
        return true;
    }
    case CampShop::Status::MaxLevel:
        pushFeedback(item.label + " は最大です");
        break;
    case CampShop::Status::InsufficientMana:
        pushFeedback("マナが足りません");
        break;
    case CampShop::Status::Invalid:
    default:
        pushFeedback("購入できません");
        break;
    }
    return false;
}

void CampScene::switchStrategyOption(const StrategyCharacter &character, int direction)
{
    if (!m_campaign || character.options.empty() || direction == 0)
    {
        return;
    }
    const std::string currentId = m_campaign->strategyOption(character.id, character.defaultOption);
    int currentIndex = 0;
    for (std::size_t i = 0; i < character.options.size(); ++i)
    {
        if (character.options[i].id == currentId)
        {
            currentIndex = static_cast<int>(i);
            break;
        }
    }
    const int optionCount = static_cast<int>(character.options.size());
    currentIndex = (currentIndex + optionCount + direction) % optionCount;
    const StrategyOption &next = character.options[static_cast<std::size_t>(currentIndex)];
    CampShop shop(*m_campaign);
    if (shop.selectStrategy(character, next.id) == CampShop::Status::Success)
    {
        pushFeedback(character.label + " strategy set to " + next.label);
        m_campaign->saveToDisk();
    }
    else
    {
        pushFeedback("作戦を変更できません");
    }
}

void CampScene::deploy(SceneStack &stack, GameApplication &app)
{
    if (m_battle)
    {
        m_battle->startRunFromCamp(app);
    }
    stack.pop();
}

bool CampScene::hasUndo() const { return m_undo.kind != UndoRecord::Kind::None; }

void CampScene::pushFeedback(const std::string &text)
{
    m_feedbackText = text;
    m_feedbackTimer = 2.0;
}

int CampScene::entryCountForTab(Tab tab, const AppConfig &config) const
{
    switch (tab)
    {
    case Tab::Upgrades: return static_cast<int>(config.campUpgrades.size());
    case Tab::Training: return static_cast<int>(config.trainingEntries.size());
    case Tab::Strategies: return static_cast<int>(config.strategyCharacters.size());
    case Tab::Shop: return static_cast<int>(config.metaShopItems.size());
    }
    return 0;
}

const CampUpgradeEntry *CampScene::upgradeAt(const AppConfig &config, int index) const
{
    if (index < 0 || index >= static_cast<int>(config.campUpgrades.size()))
    {
        return nullptr;
    }
    return &config.campUpgrades[static_cast<std::size_t>(index)];
}

const TrainingEntry *CampScene::trainingAt(const AppConfig &config, int index) const
{
    if (index < 0 || index >= static_cast<int>(config.trainingEntries.size()))
    {
        return nullptr;
    }
    return &config.trainingEntries[static_cast<std::size_t>(index)];
}

const StrategyCharacter *CampScene::strategyAt(const AppConfig &config, int index) const
{
    if (index < 0 || index >= static_cast<int>(config.strategyCharacters.size()))
    {
        return nullptr;
    }
    return &config.strategyCharacters[static_cast<std::size_t>(index)];
}

const MetaShopItem *CampScene::metaAt(const AppConfig &config, int index) const
{
    if (index < 0 || index >= static_cast<int>(config.metaShopItems.size()))
    {
        return nullptr;
    }
    return &config.metaShopItems[static_cast<std::size_t>(index)];
}

void CampScene::autoFocusAll(const AppConfig &config)
{
    autoFocusTab(Tab::Upgrades, config);
    autoFocusTab(Tab::Training, config);
    autoFocusTab(Tab::Shop, config);
    autoFocusTab(Tab::Strategies, config);
    clampSelection(config);
}

void CampScene::autoFocusTab(Tab tab, const AppConfig &config)
{
    const int idx = tabIndex(tab);
    const int count = entryCountForTab(tab, config);
    if (count <= 0)
    {
        m_selection[idx] = 0;
        return;
    }
    const int selection = findInitialSelection(tab, config);
    m_selection[idx] = std::clamp(selection, 0, count - 1);
    if (tab == m_tab)
    {
        clampSelection(config);
    }
}

int CampScene::findInitialSelection(Tab tab, const AppConfig &config) const
{
    const int wallet = m_campaign ? m_campaign->availableMana() : 0;
    const int count = entryCountForTab(tab, config);
    if (count <= 0)
    {
        return 0;
    }

    auto pickFromList = [&](auto begin, auto end, auto &&extractCost, auto &&isAvailable) {
        int cheapestIdx = 0;
        int cheapestCost = std::numeric_limits<int>::max();
        bool hasCheapest = false;
        for (auto it = begin; it != end; ++it)
        {
            const int idx = static_cast<int>(std::distance(begin, it));
            if (!isAvailable(*it, idx))
            {
                continue;
            }
            const int cost = extractCost(*it, idx);
            if (wallet >= cost)
            {
                return idx;
            }
            if (!hasCheapest || cost < cheapestCost)
            {
                cheapestCost = cost;
                cheapestIdx = idx;
                hasCheapest = true;
            }
        }
        return hasCheapest ? cheapestIdx : 0;
    };

    switch (tab)
    {
    case Tab::Upgrades:
        return pickFromList(config.campUpgrades.begin(),
                            config.campUpgrades.end(),
                            [this](const CampUpgradeEntry &entry, int) {
                                return upgradeCost(entry, m_campaign ? m_campaign->campLevel(entry.id) : 0);
                            },
                            [this](const CampUpgradeEntry &entry, int) {
                                const int currentLevel = m_campaign ? m_campaign->campLevel(entry.id) : 0;
                                const int maxLevel =
                                    entry.maxLevel > 0 ? entry.maxLevel : static_cast<int>(entry.costs.size());
                                return !(maxLevel > 0 && currentLevel >= maxLevel);
                            });
    case Tab::Training:
        return pickFromList(config.trainingEntries.begin(),
                            config.trainingEntries.end(),
                            [this](const TrainingEntry &entry, int) {
                                return trainingCost(entry, m_campaign ? m_campaign->trainingStep(entry.id) : 0);
                            },
                            [this](const TrainingEntry &entry, int) {
                                const int currentLevel = m_campaign ? m_campaign->trainingStep(entry.id) : 0;
                                return !trainingIsMaxed(entry, currentLevel);
                            });
    case Tab::Shop:
        return pickFromList(config.metaShopItems.begin(),
                            config.metaShopItems.end(),
                            [this](const MetaShopItem &item, int) {
                                const bool consumable = item.type == "consumable";
                                const int level = consumable ? 0 : (m_campaign ? m_campaign->metaLevel(item.id) : 0);
                                return metaCost(item, level);
                            },
                            [this](const MetaShopItem &item, int) {
                                if (item.type == "consumable")
                                {
                                    return true;
                                }
                                const int currentLevel = m_campaign ? m_campaign->metaLevel(item.id) : 0;
                                return !(item.maxLevel > 0 && currentLevel >= item.maxLevel);
                            });
    case Tab::Strategies:
    default:
        return std::clamp(m_selection[tabIndex(Tab::Strategies)], 0, count - 1);
    }
}

int CampScene::upgradeCost(const CampUpgradeEntry &entry, int level) const
{
    if (entry.maxLevel > 0 && level >= entry.maxLevel)
    {
        return 0;
    }
    if (!entry.costs.empty())
    {
        if (level < static_cast<int>(entry.costs.size()))
        {
            return entry.costs[static_cast<std::size_t>(level)];
        }
        return entry.costs.back();
    }
    return 0;
}

int CampScene::trainingCost(const TrainingEntry &entry, int level) const
{
    return trainingCostAtLevel(entry, level);
}

int CampScene::metaCost(const MetaShopItem &item, int level) const
{
    return item.baseCost + item.perLevelCost * std::max(level, 0);
}

std::string CampScene::formatDelta(const CampUpgradeEntry &entry) const
{
    std::ostringstream oss;
    if (entry.type == "percent")
    {
        oss << '+' << std::round(entry.delta * 100.0f) << '%';
    }
    else
    {
        oss << (entry.delta >= 0.0f ? "+" : "") << entry.delta;
    }
    return oss.str();
}

std::string CampScene::formatTrainingDelta(const TrainingEntry &entry, int level) const
{
    std::ostringstream oss;
    const float delta = trainingDeltaAtLevel(entry, level);
    if (entry.id == "mean_level")
    {
        oss << "次 Lv+" << delta;
    }
    else if (entry.id == "elite_rate")
    {
        oss << "次 +" << static_cast<int>(std::round(delta * 100.0f)) << "%";
    }
    else if (entry.id == "yuna_level")
    {
        const AllyLevelingParams params = kCommanderLevelingParams;
        const int nextLevel = trainingAbsoluteLevel(entry, level) + 1;
        auto formatFlat = [](float value) {
            std::ostringstream tmp;
            tmp << std::fixed << std::setprecision(value < 1.0f ? 3 : 2) << value;
            return tmp.str();
        };
        oss << "次 Lv" << nextLevel << "：HP+" << formatFlat(params.hpPerLevel);
        oss << " / DPS+" << formatFlat(params.dpsPerLevel);
        oss << " / SPD+" << formatFlat(params.speedPerLevel);
    }
    else
    {
        oss << "Next +" << delta;
    }
    return oss.str();
}

void CampScene::renderTabs(SDL_Renderer *renderer, const AppConfig &, int startY)
{
    if (!m_bodyFont.isLoaded())
    {
        return;
    }
    const std::array<std::string, 4> tabs{"拠点強化", "訓練", "作戦", "ルーの店"};
    const int padding = 20;
    int x = 32;
    const int height = m_bodyFont.getLineHeight() + 12;
    for (std::size_t i = 0; i < tabs.size(); ++i)
    {
        const std::string &label = tabs[i];
        const int width = m_bodyFont.measureText(label) + padding * 2;
        SDL_Rect rect{x, startY, width, height};
        m_tabRects[i] = rect;
        const bool selected = static_cast<int>(i) == tabIndex(m_tab);
        const bool hovered = static_cast<int>(i) == m_hoverTab;
        if (selected)
        {
            SDL_SetRenderDrawColor(renderer, 60, 40, 100, 220);
        }
        else if (hovered)
        {
            SDL_SetRenderDrawColor(renderer, 50, 50, 90, 200);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 30, 30, 60, 160);
        }
        SDL_RenderFillRect(renderer, &rect);
        SDL_SetRenderDrawColor(renderer, 90, 80, 140, 255);
        SDL_RenderDrawRect(renderer, &rect);
        m_bodyFont.drawText(renderer,
                            label,
                            x + padding,
                            startY + (height - m_bodyFont.getLineHeight()) / 2,
                            nullptr,
                            SDL_Color{255, 255, 255, 255});
        x += width + 8;
    }
}

void CampScene::renderContent(SDL_Renderer *renderer, const AppConfig &config, int screenW, int screenH)
{
    if (!m_bodyFont.isLoaded())
    {
        return;
    }
    const int panelX = 32;
    const int panelY = 200;
    const int panelWidth = screenW - panelX * 2;
    SDL_Rect panel{panelX, panelY, panelWidth, screenH - panelY - 80};
    SDL_SetRenderDrawColor(renderer, 15, 12, 28, 200);
    SDL_RenderFillRect(renderer, &panel);

    std::vector<RowDisplay> rows;
    rows.reserve(8);
    const int wallet = m_campaign ? m_campaign->availableMana() : 0;
    const int selection = currentSelection();

    switch (m_tab)
    {
    case Tab::Upgrades:
        for (std::size_t i = 0; i < config.campUpgrades.size(); ++i)
        {
            const CampUpgradeEntry &entry = config.campUpgrades[i];
            RowDisplay row;
            row.index = static_cast<int>(i);
            const int level = m_campaign ? m_campaign->campLevel(entry.id) : 0;
            const int maxLevel = entry.maxLevel > 0 ? entry.maxLevel : static_cast<int>(entry.costs.size());
            const bool maxed = maxLevel > 0 && level >= maxLevel;
            const int cost = upgradeCost(entry, level);
            row.title = entry.label + "  [" + std::to_string(level) + "/" +
                        (maxLevel > 0 ? std::to_string(maxLevel) : std::string("-")) + "]";
            row.subtitle = formatDelta(entry) + " per level";
            row.cost = maxed ? "MAX" : (cost > 0 ? (std::to_string(cost) + " mana") : "Free");
            row.selected = static_cast<int>(i) == selection;
            row.disabled = maxed;
            row.affordable = wallet >= cost;
            if (!row.disabled && cost > 0 && wallet < cost)
            {
                row.hint = "あと" + std::to_string(cost - wallet) + "マナ必要";
            }
            rows.push_back(row);
        }
        break;
    case Tab::Training:
        for (std::size_t i = 0; i < config.trainingEntries.size(); ++i)
        {
            const TrainingEntry &entry = config.trainingEntries[i];
            RowDisplay row;
            row.index = static_cast<int>(i);
            const int level = m_campaign ? m_campaign->trainingStep(entry.id) : 0;
            const bool maxed = trainingIsMaxed(entry, level);
            const int displayLevel = trainingIsRepeatable(entry) ? trainingAbsoluteLevel(entry, level) : level;
            const int maxLevel = trainingMaxDisplayLevel(entry);
            const int cost = trainingCost(entry, level);
            std::string maxLabel = maxLevel > 0 ? std::to_string(maxLevel) : std::string("-");
            row.title = entry.label + "  [" + std::to_string(displayLevel) + "/" + maxLabel + "]";
            row.subtitle = maxed ? "Complete" : formatTrainingDelta(entry, level);
            row.cost = std::to_string(cost) + " mana";
            row.selected = static_cast<int>(i) == selection;
            row.disabled = false;
            row.affordable = wallet >= cost;
            if (!row.disabled && cost > 0 && wallet < cost)
            {
                row.hint = "あと" + std::to_string(cost - wallet) + "マナ必要";
            }
            rows.push_back(row);
        }
        break;
    case Tab::Strategies:
        for (std::size_t i = 0; i < config.strategyCharacters.size(); ++i)
        {
            const StrategyCharacter &character = config.strategyCharacters[i];
            const std::string current = m_campaign ? m_campaign->strategyOption(character.id, character.defaultOption)
                                                   : character.defaultOption;
            std::string label = character.label + ": ";
            for (const StrategyOption &option : character.options)
            {
                if (option.id == current)
                {
                    label += option.label;
                    break;
                }
            }
            RowDisplay row;
            row.index = static_cast<int>(i);
            row.title = label;
            row.subtitle = "Use Left/Right to cycle";
            row.cost.clear();
            row.selected = static_cast<int>(i) == selection;
            row.disabled = false;
            row.affordable = true;
            rows.push_back(row);
        }
        break;
    case Tab::Shop:
        for (std::size_t i = 0; i < config.metaShopItems.size(); ++i)
        {
            const MetaShopItem &item = config.metaShopItems[i];
            RowDisplay row;
            row.index = static_cast<int>(i);
            const bool consumable = item.type == "consumable";
            const int level = consumable ? 0 : (m_campaign ? m_campaign->metaLevel(item.id) : 0);
            const bool maxed = false; // upper limit removed
            const int cost = metaCost(item, level);
            row.title = item.label + (consumable ? "" : ("  [" + std::to_string(level) + "/-]"));
            if (consumable)
            {
                const int stock = m_campaign ? m_campaign->manaGainTokens : 0;
                row.subtitle = "Stocked: " + std::to_string(stock);
            }
            else
            {
                row.subtitle.clear();
            }
            row.cost = maxed ? "MAX" : (std::to_string(cost) + " mana");
            row.selected = static_cast<int>(i) == selection;
            row.disabled = maxed;
            row.affordable = wallet >= cost;
            if (!row.disabled && cost > 0 && wallet < cost)
            {
                row.hint = "あと" + std::to_string(cost - wallet) + "マナ必要";
            }
            rows.push_back(row);
        }
        break;
    }

    renderRows(renderer, rows, panelX + 16, panelY + 16, panelWidth - 32);
}

void CampScene::renderButtons(SDL_Renderer *renderer, const TextRenderer &font, int screenW, int headerY)
{
    if (!renderer || !font.isLoaded())
    {
        m_buttonPurchase = SDL_Rect{0, 0, 0, 0};
        m_buttonDeploy = SDL_Rect{0, 0, 0, 0};
        return;
    }
    const int buttonHeight = font.getLineHeight() + 12;
    const int purchaseWidth = std::max(140, font.measureText("Purchase") + 32);
    const int deployWidth = std::max(120, font.measureText("Deploy") + 32);
    const int spacing = 12;
    const int padding = 32;
    const int totalWidth = purchaseWidth + deployWidth + spacing;
    const int x = screenW - totalWidth - padding;
    const int y = headerY;
    m_buttonPurchase = SDL_Rect{x, y, purchaseWidth, buttonHeight};
    m_buttonDeploy = SDL_Rect{x + purchaseWidth + spacing, y, deployWidth, buttonHeight};

    drawButton(renderer,
               font,
               m_buttonPurchase,
               "Purchase",
               m_hoverButton == PointerTargetKind::PurchaseButton,
               false);
    drawButton(renderer,
               font,
               m_buttonDeploy,
               "Deploy",
               m_hoverButton == PointerTargetKind::DeployButton,
               true);
}

void CampScene::renderRows(SDL_Renderer *renderer,
                            const std::vector<RowDisplay> &rows,
                            int x,
                            int y,
                            int width)
{
    if (!m_bodyFont.isLoaded())
    {
        return;
    }
    const int lineHeight = std::max(m_bodyFont.getLineHeight(), 20);
    for (const RowDisplay &row : rows)
    {
        SDL_Rect rowRect{x, y, width, lineHeight + 12};
        int advance = lineHeight + 18;
        if (!row.hint.empty())
        {
            advance += 12;
        }
        SDL_Rect rowRectFull = rowRect;
        rowRectFull.h = advance;
        if (row.selected)
        {
            SDL_SetRenderDrawColor(renderer, 70, 50, 110, 200);
            SDL_RenderFillRect(renderer, &rowRectFull);
        }
        else if (row.index >= 0 && row.index == m_hoverRow)
        {
            SDL_SetRenderDrawColor(renderer, row.disabled ? 40 : 50, 45, 80, row.disabled ? 120 : 170);
            SDL_RenderFillRect(renderer, &rowRectFull);
        }
        SDL_Color titleColor = row.disabled ? SDL_Color{140, 140, 140, 255} : SDL_Color{255, 255, 255, 255};
        SDL_Color subColor = row.disabled ? SDL_Color{120, 120, 120, 255} : SDL_Color{190, 200, 230, 255};
        m_bodyFont.drawText(renderer, row.title, x + 12, y + 4, nullptr, titleColor);
        if (!row.subtitle.empty())
        {
            m_bodyFont.drawText(renderer, row.subtitle, x + 12, y + lineHeight - 4, nullptr, subColor);
        }
        if (!row.cost.empty())
        {
            SDL_Color costColor =
                row.disabled ? SDL_Color{120, 120, 120, 255}
                             : (row.affordable ? SDL_Color{180, 255, 200, 255} : SDL_Color{255, 180, 150, 255});
            const int costWidth = m_bodyFont.measureText(row.cost);
            m_bodyFont.drawText(renderer,
                                row.cost,
                                x + width - costWidth - 16,
                                y + 4,
                                nullptr,
                                costColor);
        }
        if (!row.hint.empty())
        {
            SDL_Color hintColor = SDL_Color{255, 130, 130, 255};
            m_bodyFont.drawText(renderer, row.hint, x + 12, y + lineHeight + 6, nullptr, hintColor);
        }
        if (row.index >= 0)
        {
            RowHitbox hit;
            hit.rect = rowRectFull;
            hit.index = row.index;
            hit.disabled = row.disabled;
            hit.affordable = row.affordable;
            m_rowHits.push_back(hit);
        }
        y += advance;
    }
}

void CampScene::drawButton(SDL_Renderer *renderer,
                            const TextRenderer &font,
                            const SDL_Rect &rect,
                            const std::string &label,
                            bool hovered,
                            bool accent)
{
    if (!renderer || !font.isLoaded())
    {
        return;
    }
    const SDL_Color baseColor = accent ? SDL_Color{40, 90, 95, 210} : SDL_Color{40, 35, 80, 210};
    const SDL_Color hoverColor = accent ? SDL_Color{70, 130, 140, 235} : SDL_Color{80, 65, 140, 230};
    const SDL_Color fill = hovered ? hoverColor : baseColor;
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 120, 110, 170, 255);
    SDL_RenderDrawRect(renderer, &rect);
    const int textWidth = font.measureText(label);
    const int textX = rect.x + (rect.w - textWidth) / 2;
    const int textY = rect.y + (rect.h - font.getLineHeight()) / 2;
    font.drawText(renderer, label, textX, textY, nullptr, SDL_Color{255, 255, 255, 255});
}

void CampScene::renderUndoPrompt(SDL_Renderer *renderer, const TextRenderer &font, int screenW, int screenH) const
{
    if (!renderer || !font.isLoaded() || !hasUndo())
    {
        return;
    }
    std::ostringstream oss;
    oss << "[Z] 取り消し (残り" << std::fixed << std::setprecision(1) << std::max(m_undo.timer, 0.0) << "s)";
    const std::string text = oss.str();
    const int textWidth = font.measureText(text);
    const int x = screenW - textWidth - 32;
    const int y = screenH - font.getLineHeight() - 24;
    font.drawText(renderer, text, x, y, nullptr, SDL_Color{255, 230, 180, 255});
}

void CampScene::beginUndo(const UndoRecord &record)
{
    m_undo = record;
    m_undo.timer = UndoWindowSeconds;
}

void CampScene::cancelUndo()
{
    m_undo = {};
}

void CampScene::updateUndo(double deltaSeconds)
{
    if (!hasUndo())
    {
        return;
    }
    m_undo.timer = std::max(0.0, m_undo.timer - deltaSeconds);
    if (m_undo.timer <= 0.0)
    {
        cancelUndo();
    }
}

void CampScene::performUndo(const AppConfig &config)
{
    if (!hasUndo() || !m_campaign)
    {
        return;
    }
    switch (m_undo.kind)
    {
    case UndoRecord::Kind::CampUpgrade:
        setMappedLevel(m_campaign->campUpgradeLevels, m_undo.id, m_undo.previousLevel);
        break;
    case UndoRecord::Kind::Training:
        setMappedLevel(m_campaign->trainingProgress, m_undo.id, m_undo.previousLevel);
        break;
    case UndoRecord::Kind::Meta:
        setMappedLevel(m_campaign->metaLevels, m_undo.id, m_undo.previousLevel);
        break;
    case UndoRecord::Kind::Token:
        m_campaign->manaGainTokens = std::max(0, m_undo.previousTokens);
        break;
    case UndoRecord::Kind::None:
        break;
    }
    if (m_undo.cost > 0)
    {
        m_campaign->depositMana(m_undo.cost);
    }
    cancelUndo();
    pushFeedback("購入を取り消しました");
    autoFocusTab(m_tab, config);
    if (m_campaign)
    {
        m_campaign->saveToDisk();
    }
}

void CampScene::setMappedLevel(std::unordered_map<std::string, int> &map, const std::string &id, int level)
{
    if (level <= 0)
    {
        map.erase(id);
    }
    else
    {
        map[id] = level;
    }
}

int CampScene::tabIndexAt(int x, int y) const
{
    for (std::size_t i = 0; i < m_tabRects.size(); ++i)
    {
        if (rectValid(m_tabRects[i]) && pointInRect(m_tabRects[i], x, y))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int CampScene::rowIndexAt(int x, int y) const
{
    for (const RowHitbox &hit : m_rowHits)
    {
        if (pointInRect(hit.rect, x, y))
        {
            return hit.index;
        }
    }
    return -1;
}

CampScene::PointerTargetKind CampScene::buttonAt(int x, int y) const
{
    if (rectValid(m_buttonPurchase) && pointInRect(m_buttonPurchase, x, y))
    {
        return PointerTargetKind::PurchaseButton;
    }
    if (rectValid(m_buttonDeploy) && pointInRect(m_buttonDeploy, x, y))
    {
        return PointerTargetKind::DeployButton;
    }
    return PointerTargetKind::None;
}

const CampScene::RowHitbox *CampScene::rowHitboxForIndex(int index) const
{
    for (const RowHitbox &hit : m_rowHits)
    {
        if (hit.index == index)
        {
            return &hit;
        }
    }
    return nullptr;
}

bool CampScene::rectValid(const SDL_Rect &rect)
{
    return rect.w > 0 && rect.h > 0;
}

bool CampScene::pointInRect(const SDL_Rect &rect, int x, int y)
{
    return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}
