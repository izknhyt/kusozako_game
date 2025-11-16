#pragma once

#include "config/AppConfig.h"
#include "game/CampaignState.h"

#include <string>

class CampShop
{
  public:
    enum class Status
    {
        Success,
        MaxLevel,
        InsufficientMana,
        Invalid
    };

    struct PurchaseOutcome
    {
        Status status = Status::Invalid;
        std::string id;
        int cost = 0;
        int previousLevel = 0;
        int previousTokens = 0;
        bool consumable = false;
    };

    explicit CampShop(CampaignState &state);

    CampaignState &state() { return m_state; }
    const CampaignState &state() const { return m_state; }

    PurchaseOutcome buyUpgrade(const CampUpgradeEntry &entry);
    PurchaseOutcome buyTraining(const TrainingEntry &entry);
    PurchaseOutcome buyMeta(const MetaShopItem &item);
    Status selectStrategy(const StrategyCharacter &character, const std::string &optionId);

  private:
    CampaignState &m_state;

    static int upgradeCost(const CampUpgradeEntry &entry, int level);
    static int metaCost(const MetaShopItem &item, int level);
};
