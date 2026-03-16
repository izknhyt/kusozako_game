#pragma once

#include <SDL.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "app/TextRenderer.h"
#include "scenes/Scene.h"

struct AppConfig;
struct CampUpgradeEntry;
struct MetaShopItem;
struct StrategyCharacter;
struct StrategyOption;
struct TrainingEntry;

class BattleScene;
struct CampaignState;
class GameApplication;
class SceneStack;

class CampScene : public Scene
{
  public:
    enum class Tab : int
    {
        Upgrades = 0,
        Training = 1,
        Strategies = 2,
        Shop = 3
    };

    struct RowDisplay
    {
        std::string title;
        std::string subtitle;
        std::string cost;
        std::string hint;
        int index = -1;
        bool selected = false;
        bool disabled = false;
        bool affordable = false;
    };

    struct RowHitbox
    {
        SDL_Rect rect{0, 0, 0, 0};
        int index = -1;
        bool disabled = false;
        bool affordable = false;
    };

    enum class PointerTargetKind
    {
        None,
        Tab,
        Row,
        PurchaseButton,
        DeployButton
    };

    struct PointerLatch
    {
        PointerTargetKind kind = PointerTargetKind::None;
        int index = -1;
    };

    struct UndoRecord
    {
        enum class Kind
        {
            None,
            CampUpgrade,
            Training,
            Meta,
            Token
        };

        Kind kind = Kind::None;
        std::string id;
        int cost = 0;
        int previousLevel = 0;
        int previousTokens = 0;
        double timer = 0.0;
    };

    CampScene(std::shared_ptr<CampaignState> campaign, BattleScene *battle);

    void onEnter(GameApplication &app, SceneStack &) override;
    void onExit(GameApplication &, SceneStack &) override;
    void handleEvent(const SDL_Event &event, GameApplication &app, SceneStack &stack) override;
    void update(double deltaSeconds, GameApplication &, SceneStack &) override;
    void render(SDL_Renderer *renderer, GameApplication &app) override;

  private:
    std::shared_ptr<CampaignState> m_campaign;
    BattleScene *m_battle = nullptr;
    Tab m_tab = Tab::Upgrades;
    std::array<int, 4> m_selection{{0, 0, 0, 0}};
    TextRenderer m_titleFont;
    TextRenderer m_bodyFont;
    double m_feedbackTimer = 0.0;
    std::string m_feedbackText;
    UndoRecord m_undo;
    static constexpr double UndoWindowSeconds = 3.0;
    std::array<SDL_Rect, 4> m_tabRects{};
    std::vector<RowHitbox> m_rowHits;
    SDL_Rect m_buttonPurchase{0, 0, 0, 0};
    SDL_Rect m_buttonDeploy{0, 0, 0, 0};
    PointerLatch m_pressTarget;
    SDL_Point m_pointerPos{0, 0};
    bool m_pointerActive = false;
    int m_hoverTab = -1;
    int m_hoverRow = -1;
    PointerTargetKind m_hoverButton = PointerTargetKind::None;

    int tabIndex(Tab tab) const;
    int currentSelection() const;
    void setTab(Tab tab, const AppConfig &config);
    void clampSelection(const AppConfig &config);
    void moveSelection(int delta, const AppConfig &config);
    void handleMouseMotion(const SDL_MouseMotionEvent &motion);
    void handleMouseButtonDown(const SDL_MouseButtonEvent &button, const AppConfig &config);
    void handleMouseButtonUp(const SDL_MouseButtonEvent &button,
                             const AppConfig &config,
                             SceneStack &stack,
                             GameApplication &app);
    void beginHitTestFrame();
    void updatePointerPosition(int x, int y);
    void refreshHoverTargets();
    void clearPointerHover();
    PointerTargetKind determineTargetAtPoint(const SDL_Point &point, int &indexOut) const;
    void handlePurchase(const AppConfig &config);
    bool purchaseUpgrade(const CampUpgradeEntry &entry);
    bool purchaseTraining(const TrainingEntry &entry);
    bool purchaseMeta(const MetaShopItem &item);
    void switchStrategyOption(const StrategyCharacter &character, int direction);
    void deploy(SceneStack &stack, GameApplication &app);
    bool hasUndo() const;
    void pushFeedback(const std::string &text);
    int entryCountForTab(Tab tab, const AppConfig &config) const;
    const CampUpgradeEntry *upgradeAt(const AppConfig &config, int index) const;
    const TrainingEntry *trainingAt(const AppConfig &config, int index) const;
    const StrategyCharacter *strategyAt(const AppConfig &config, int index) const;
    const MetaShopItem *metaAt(const AppConfig &config, int index) const;
    void autoFocusAll(const AppConfig &config);
    void autoFocusTab(Tab tab, const AppConfig &config);
    int findInitialSelection(Tab tab, const AppConfig &config) const;
    int upgradeCost(const CampUpgradeEntry &entry, int level) const;
    int trainingCost(const TrainingEntry &entry, int level) const;
    int metaCost(const MetaShopItem &item, int level) const;
    std::string formatDelta(const CampUpgradeEntry &entry) const;
    std::string formatTrainingDelta(const TrainingEntry &entry, int level) const;
    void renderTabs(SDL_Renderer *renderer, const AppConfig &, int startY);
    void renderContent(SDL_Renderer *renderer, const AppConfig &config, int screenW, int screenH);
    void renderButtons(SDL_Renderer *renderer, const TextRenderer &font, int screenW, int headerY);
    void renderRows(SDL_Renderer *renderer,
                    const std::vector<RowDisplay> &rows,
                    int x,
                    int y,
                    int width);
    void drawButton(SDL_Renderer *renderer,
                    const TextRenderer &font,
                    const SDL_Rect &rect,
                    const std::string &label,
                    bool hovered,
                    bool accent);
    void renderUndoPrompt(SDL_Renderer *renderer, const TextRenderer &font, int screenW, int screenH) const;
    void beginUndo(const UndoRecord &record);
    void cancelUndo();
    void updateUndo(double deltaSeconds);
    void performUndo(const AppConfig &config);
    void setMappedLevel(std::unordered_map<std::string, int> &map, const std::string &id, int level);
    int tabIndexAt(int x, int y) const;
    int rowIndexAt(int x, int y) const;
    PointerTargetKind buttonAt(int x, int y) const;
    const RowHitbox *rowHitboxForIndex(int index) const;
    static bool rectValid(const SDL_Rect &rect);
    static bool pointInRect(const SDL_Rect &rect, int x, int y);
};
