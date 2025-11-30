#include "game/CampShop.h"

#include "game/TrainingMath.h"

#include <algorithm>

CampShop::CampShop(CampaignState &state) : m_state(state) {}

CampShop::PurchaseOutcome CampShop::buyUpgrade(const CampUpgradeEntry &entry)
{
    PurchaseOutcome result;
    result.id = entry.id;
    if (entry.id.empty())
    {
        result.status = Status::Invalid;
        return result;
    }
    const int currentLevel = m_state.campLevel(entry.id);
    if (entry.maxLevel > 0 && currentLevel >= entry.maxLevel)
    {
        result.status = Status::MaxLevel;
        result.previousLevel = currentLevel;
        return result;
    }
    // 無制限に購入可能（コストは配列の末尾を維持）
    const int cost = upgradeCost(entry, currentLevel);
    if (!m_state.spendMana(cost))
    {
        result.status = Status::InsufficientMana;
        return result;
    }
    m_state.campUpgradeLevels[entry.id] = currentLevel + 1;
    result.status = Status::Success;
    result.cost = cost;
    result.previousLevel = currentLevel;
    return result;
}

CampShop::PurchaseOutcome CampShop::buyTraining(const TrainingEntry &entry)
{
    PurchaseOutcome result;
    result.id = entry.id;
    const int currentLevel = m_state.trainingStep(entry.id);
    if (trainingIsMaxed(entry, currentLevel))
    {
        result.status = Status::MaxLevel;
        return result;
    }
    const int cost = trainingCostAtLevel(entry, currentLevel);
    if (!m_state.spendMana(cost))
    {
        result.status = Status::InsufficientMana;
        return result;
    }
    m_state.trainingProgress[entry.id] = currentLevel + 1;
    result.status = Status::Success;
    result.cost = cost;
    result.previousLevel = currentLevel;
    return result;
}

CampShop::PurchaseOutcome CampShop::buyMeta(const MetaShopItem &item)
{
    PurchaseOutcome result;
    result.id = item.id;
    const bool consumable = item.type == "consumable";
    const int currentLevel = consumable ? 0 : m_state.metaLevel(item.id);
    // メタ強化も上限なし
    const int cost = metaCost(item, currentLevel);
    if (!m_state.spendMana(cost))
    {
        result.status = Status::InsufficientMana;
        return result;
    }
    result.cost = cost;
    result.previousLevel = currentLevel;
    result.previousTokens = m_state.manaGainTokens;
    result.consumable = consumable;
    if (consumable)
    {
        ++m_state.manaGainTokens;
    }
    else
    {
        m_state.metaLevels[item.id] = currentLevel + 1;
    }
    result.status = Status::Success;
    return result;
}

CampShop::Status CampShop::selectStrategy(const StrategyCharacter &character, const std::string &optionId)
{
    if (character.options.empty())
    {
        return Status::Invalid;
    }
    auto it = std::find_if(character.options.begin(), character.options.end(), [&](const StrategyOption &option) {
        return option.id == optionId;
    });
    if (it == character.options.end())
    {
        return Status::Invalid;
    }
    m_state.strategySelections[character.id] = optionId;
    return Status::Success;
}

int CampShop::upgradeCost(const CampUpgradeEntry &entry, int level)
{
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

int CampShop::metaCost(const MetaShopItem &item, int level)
{
    return item.baseCost + item.perLevelCost * std::max(level, 0);
}
