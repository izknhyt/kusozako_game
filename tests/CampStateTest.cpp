#include "game/CampaignState.h"
#include "game/CampShop.h"

#include "config/AppConfig.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

AppConfig buildTestConfig()
{
    AppConfig config;
    CampUpgradeEntry upgrade;
    upgrade.id = "base_hp";
    upgrade.label = "Base HP";
    upgrade.maxLevel = 2;
    upgrade.costs = {20, 30};
    config.campUpgrades.push_back(upgrade);

    TrainingEntry training;
    training.id = "mean_level";
    training.label = "Mean Level";
    training.steps = {{0.5f, 15}, {1.0f, 25}};
    config.trainingEntries.push_back(training);

    TrainingEntry yuna;
    yuna.id = "yuna_level";
    yuna.label = "Yuna Level";
    yuna.repeatable.enabled = true;
    yuna.repeatable.delta = 1.0f;
    yuna.repeatable.maxLevel = 5;
    yuna.repeatable.baseCost = 10.0f;
    yuna.repeatable.costGrowth = 1.5f;
    config.trainingEntries.push_back(yuna);

    MetaShopItem meta;
    meta.id = "mana_cap";
    meta.label = "Mana Cap";
    meta.type = "cap";
    meta.maxLevel = 2;
    meta.baseCost = 40;
    meta.perLevelCost = 10;
    meta.delta = 50.0f;
    config.metaShopItems.push_back(meta);

    MetaShopItem token;
    token.id = "mana_gain_token";
    token.label = "Mana Token";
    token.type = "consumable";
    token.baseCost = 5;
    token.gainBonus = 0.10f;
    config.metaShopItems.push_back(token);

    StrategyCharacter strat;
    strat.id = "milly";
    strat.label = "Milly";
    strat.defaultOption = "rear_safe";
    strat.options = {{"rear_safe", "Rear"}, {"focus_fire", "Focus"}};
    config.strategyCharacters.push_back(strat);
    return config;
}

bool testCampaignStateSpend()
{
    CampaignState state;
    state.depositMana(50);
    if (!state.spendMana(20) || state.availableMana() != 30)
    {
        std::cerr << "Spend did not reduce wallet" << '\n';
        return false;
    }
    if (state.spendMana(40))
    {
        std::cerr << "Spend allowed overdraft" << '\n';
        return false;
    }
    if (state.availableMana() != 30)
    {
        std::cerr << "Wallet changed after failed spend" << '\n';
        return false;
    }
    return true;
}

bool testCampaignStateSaveLoad()
{
    auto path = std::filesystem::temp_directory_path() / "kusozako_campaign_state.json";
    CampaignState state;
    state.setSavePath(path);
    state.depositMana(42);
    state.campUpgradeLevels["base_hp"] = 3;
    state.trainingProgress["mean_level"] = 2;
    state.metaLevels["mana_cap"] = 1;
    state.strategySelections["milly"] = "focus_fire";
    state.manaGainTokens = 4;
    if (!state.saveToDisk())
    {
        std::cerr << "Failed to save campaign state" << '\n';
        return false;
    }
    CampaignState loaded;
    loaded.setSavePath(path);
    if (!loaded.loadFromDisk())
    {
        std::cerr << "Failed to load campaign state" << '\n';
        return false;
    }
    if (loaded.availableMana() != 42 || loaded.manaGainTokens != 4)
    {
        std::cerr << "Wallet or tokens mismatch after load" << '\n';
        return false;
    }
    if (loaded.campLevel("base_hp") != 3 || loaded.trainingStep("mean_level") != 2 ||
        loaded.metaLevel("mana_cap") != 1)
    {
        std::cerr << "Upgrade/training/meta levels mismatch" << '\n';
        return false;
    }
    if (loaded.strategyOption("milly", "rear_safe") != "focus_fire")
    {
        std::cerr << "Strategy selection mismatch" << '\n';
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

bool testCampShopPurchases()
{
    AppConfig config = buildTestConfig();
    CampaignState state;
    state.depositMana(200);
    CampShop shop(state);

    auto upgrade = shop.buyUpgrade(config.campUpgrades.front());
    if (upgrade.status != CampShop::Status::Success || state.campLevel("base_hp") != 1)
    {
        std::cerr << "Upgrade purchase failed" << '\n';
        return false;
    }
    auto maxed = shop.buyUpgrade(config.campUpgrades.front());
    if (maxed.status != CampShop::Status::Success || state.campLevel("base_hp") != 2)
    {
        std::cerr << "Second upgrade purchase failed" << '\n';
        return false;
    }
    auto beyond = shop.buyUpgrade(config.campUpgrades.front());
    if (beyond.status != CampShop::Status::MaxLevel)
    {
        std::cerr << "Exceeded max level without error" << '\n';
        return false;
    }

    auto training = shop.buyTraining(config.trainingEntries.front());
    if (training.status != CampShop::Status::Success || state.trainingStep("mean_level") != 1)
    {
        std::cerr << "Training purchase failed" << '\n';
        return false;
    }
    const TrainingEntry &repeatTraining = config.trainingEntries.back();
    auto yunaTrain = shop.buyTraining(repeatTraining);
    if (yunaTrain.status != CampShop::Status::Success || yunaTrain.cost != 10 ||
        state.trainingStep("yuna_level") != 1)
    {
        std::cerr << "Repeatable training first purchase failed" << '\n';
        return false;
    }
    state.depositMana(1000);
    for (int i = 0; i < 3; ++i)
    {
        if (shop.buyTraining(repeatTraining).status != CampShop::Status::Success)
        {
            std::cerr << "Repeatable training follow-up purchase failed" << '\n';
            return false;
        }
    }
    auto cappedTraining = shop.buyTraining(repeatTraining);
    if (cappedTraining.status != CampShop::Status::MaxLevel)
    {
        std::cerr << "Repeatable training exceeded max without error" << '\n';
        return false;
    }

    const MetaShopItem &meta = config.metaShopItems.front();
    auto metaResult = shop.buyMeta(meta);
    if (metaResult.status != CampShop::Status::Success || state.metaLevel("mana_cap") != 1)
    {
        std::cerr << "Meta upgrade failed" << '\n';
        return false;
    }

    const MetaShopItem &token = config.metaShopItems.back();
    int tokensBefore = state.manaGainTokens;
    auto tokenResult = shop.buyMeta(token);
    if (tokenResult.status != CampShop::Status::Success || state.manaGainTokens != tokensBefore + 1)
    {
        std::cerr << "Consumable token purchase failed" << '\n';
        return false;
    }

    auto strategyStatus = shop.selectStrategy(config.strategyCharacters.front(), "focus_fire");
    if (strategyStatus != CampShop::Status::Success ||
        state.strategyOption("milly", "rear_safe") != "focus_fire")
    {
        std::cerr << "Strategy selection failed" << '\n';
        return false;
    }

    return true;
}

bool testAutoLoopSimulation()
{
    AppConfig config = buildTestConfig();
    CampaignState state;
    CampShop shop(state);

    for (int run = 0; run < 5; ++run)
    {
        state.depositMana(50);
        if (shop.buyUpgrade(config.campUpgrades.front()).status != CampShop::Status::Success)
        {
            // Attempt training if upgrade maxed
            shop.buyTraining(config.trainingEntries.front());
        }
        shop.buyMeta(config.metaShopItems.back());
    }
    if (state.campLevel("base_hp") == 0)
    {
        std::cerr << "Auto loop did not improve base HP" << '\n';
        return false;
    }
    if (state.manaGainTokens == 0)
    {
        std::cerr << "Auto loop did not accumulate tokens" << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool success = true;
    success &= testCampaignStateSpend();
    success &= testCampaignStateSaveLoad();
    success &= testCampShopPurchases();
    success &= testAutoLoopSimulation();
    if (!success)
    {
        return 1;
    }
    return 0;
}
