#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/AppConfig.h"
#include "json/JsonUtils.h"
#include "game/AllyLevelingConfig.h"
#include "game/TrainingMath.h"
#include "world/LegacySimulation.h"

// Tracks persistent upgrades and wallet state between runs.
struct CampaignState
{
    int bankedMana = 0;
    int manaGainTokens = 0;
    std::unordered_map<std::string, int> campUpgradeLevels;
    std::unordered_map<std::string, int> trainingProgress;
    std::unordered_map<std::string, std::string> strategySelections;
    std::unordered_map<std::string, int> metaLevels;
    bool debugDisableYunaSpawns = false;
    bool debugDisableBaseRegenAndDef = false;

    int availableMana() const { return bankedMana; }

    void depositMana(int amount)
    {
        if (amount > 0)
        {
            bankedMana += amount;
        }
    }

    bool spendMana(int cost)
    {
        if (cost <= 0)
        {
            return true;
        }
        if (bankedMana < cost)
        {
            return false;
        }
        bankedMana -= cost;
        return true;
    }

    int campLevel(const std::string &id) const
    {
        if (auto it = campUpgradeLevels.find(id); it != campUpgradeLevels.end())
        {
            return it->second;
        }
        return 0;
    }

    int metaLevel(const std::string &id) const
    {
        if (auto it = metaLevels.find(id); it != metaLevels.end())
        {
            return it->second;
        }
        return 0;
    }

    int trainingStep(const std::string &id) const
    {
        if (auto it = trainingProgress.find(id); it != trainingProgress.end())
        {
            return it->second;
        }
        return 0;
    }

    std::string strategyOption(const std::string &id, const std::string &fallback) const
    {
        if (auto it = strategySelections.find(id); it != strategySelections.end())
        {
            return it->second;
        }
        return fallback;
    }

  private:
    static const CampUpgradeEntry *findCampUpgrade(const AppConfig &config, const std::string &id)
    {
        for (const CampUpgradeEntry &entry : config.campUpgrades)
        {
            if (entry.id == id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    static const MetaShopItem *findMetaItem(const AppConfig &config, const std::string &id)
    {
        for (const MetaShopItem &item : config.metaShopItems)
        {
            if (item.id == id)
            {
                return &item;
            }
        }
        return nullptr;
    }

  public:
    void applyPersistentUpgrades(const AppConfig &config, world::LegacySimulation &sim)
    {
        sim.baseDamageReduction = 0.0f;
        sim.baseAuraRegenPerSecond = 0.0f;
        sim.spawnRateBaseMultiplier = 1.0f;
        sim.config.base_hp = config.game.base_hp;
        sim.economyConfig = config.economy;
        sim.meanChibiLevel = 1.0f;

        CommanderStats commander = config.entityCatalog.commander;
        const AllyLevelingParams commanderLeveling = kCommanderLevelingParams;
        const int commanderLevel = std::max(trainingStep("yuna_level") + 1, 1);
        const float commanderLevelDelta = static_cast<float>(commanderLevel - 1);
        commander.hp = commander.hp + commanderLeveling.hpPerLevel * commanderLevelDelta;
        commander.dps = commander.dps + commanderLeveling.dpsPerLevel * commanderLevelDelta;
        commander.speed_u_s = commander.speed_u_s + commanderLeveling.speedPerLevel * commanderLevelDelta;
        commander.dps = std::max(commander.dps, 0.0f);
        commander.speed_u_s = std::max(commander.speed_u_s, 0.0f);
        sim.commanderStats = commander;

        // 平均ちびレベル（無名ちびの初期レベル帯）
        auto findTraining = [&](const std::string &id) -> const TrainingEntry * {
            for (const TrainingEntry &entry : config.trainingEntries)
            {
                if (entry.id == id)
                {
                    return &entry;
                }
            }
            return nullptr;
        };
        auto trainingValue = [&](const TrainingEntry &entry, int progress) -> float {
            if (trainingIsRepeatable(entry))
            {
                return std::max(progress, 0) * entry.repeatable.delta;
            }
            float total = 0.0f;
            const int steps = std::max(progress, 0);
            for (int i = 0; i < steps && i < static_cast<int>(entry.steps.size()); ++i)
            {
                total += entry.steps[static_cast<std::size_t>(i)].delta;
            }
            return total;
        };
        if (const TrainingEntry *meanEntry = findTraining("mean_level"))
        {
            const int progress = trainingStep("mean_level");
            const float value = trainingValue(*meanEntry, progress);
            sim.meanChibiLevel = std::max(1.0f, value);
        }

        auto resolveCampBonus = [&](const std::string &id) -> float {
            const CampUpgradeEntry *entry = findCampUpgrade(config, id);
            if (!entry)
            {
                return 0.0f;
            }
            const int level = std::max(campLevel(id), 0);
            if (level <= 0 || entry->delta == 0.0f)
            {
                return 0.0f;
            }
            return entry->delta * static_cast<float>(level);
        };

        const float baseHpBonus = resolveCampBonus("base_hp");
        if (sim.stage.enabled && !sim.stage.allyBases.empty())
        {
            for (world::StageAllyBaseState &base : sim.stage.allyBases)
            {
                const float baseline = base.baseMaxHp > 0.0f ? base.baseMaxHp : base.maxHp;
                base.maxHp = baseline + baseHpBonus;
                if (base.destroyedDefault)
                {
                    base.hp = 0.0f;
                }
                else
                {
                    base.hp = base.maxHp;
                }
            }
        }
        sim.config.base_hp = static_cast<int>(std::round(config.game.base_hp + baseHpBonus));

        const float defenseBonus = resolveCampBonus("base_def");
        sim.baseDamageReduction = debugDisableBaseRegenAndDef ? 0.0f : std::clamp(defenseBonus, 0.0f, 0.95f);

        const float spawnBonus = resolveCampBonus("spawn_rate");
        sim.spawnRateBaseMultiplier = std::max(0.1f, 1.0f + spawnBonus);

        sim.baseAuraRegenPerSecond =
            debugDisableBaseRegenAndDef ? 0.0f : std::max(0.0f, resolveCampBonus("aura_regen"));

        const MetaShopItem *capItem = findMetaItem(config, "mana_cap_up_s");
        if (!capItem)
        {
            // Backward compatibility with older save IDs.
            capItem = findMetaItem(config, "mana_cap_up");
        }
        int capBonus = 0;
        if (capItem)
        {
            const int level = std::max(metaLevel(capItem->id), 0);
            if (level > 0 && capItem->delta != 0.0f)
            {
                capBonus = static_cast<int>(std::round(capItem->delta * static_cast<float>(level)));
            }
        }
        sim.economyConfig.baseCap = config.economy.baseCap + capBonus;
        // Mana gain +X% (stacking) from meta shop
        float gainBonus = 0.0f;
        if (const MetaShopItem *gainItem = findMetaItem(config, "mana_gain_up_s"))
        {
            const int level = std::max(metaLevel(gainItem->id), 0);
            if (level > 0 && gainItem->delta != 0.0f)
            {
                gainBonus = gainItem->delta * static_cast<float>(level);
            }
        }
        sim.economyConfig.gainBonus = gainBonus;
        // Mana gain token effect: 100% per token
        sim.economyConfig.tokenBonus = 1.0f;

        // Namedユニットのレベル（上限なし）
        auto buildNamedLevel = [&](const std::string &id) -> int {
            return std::max(trainingStep(id) + 1, 1);
        };
        sim.namedLevels.clear();
        sim.namedLevels["Milly"] = buildNamedLevel("milly_level");
        sim.namedLevels["Mary"] = buildNamedLevel("mary_level");
        sim.namedLevels["Coco"] = buildNamedLevel("coco_level");
    }

    void applyRunStart(const AppConfig &, world::LegacySimulation &sim)
    {
        sim.spawnRateMultiplier = std::max(sim.spawnRateBaseMultiplier, 0.1f);
        sim.disableYunaSpawns = debugDisableYunaSpawns;
        if (debugDisableBaseRegenAndDef)
        {
            sim.baseDamageReduction = 0.0f;
            sim.baseAuraRegenPerSecond = 0.0f;
        }
        if (manaGainTokens > 0)
        {
            sim.economy.tokens = 1;
            --manaGainTokens;
        }
        else
        {
            sim.economy.tokens = 0;
        }
        sim.updateEconomyHud();
    }

    void recordRunOutcome(const world::LegacySimulation &sim)
    {
        depositMana(sim.economy.mana);
    }

    void setSavePath(std::filesystem::path path) { m_savePath = std::move(path); }

    const std::filesystem::path &savePath() const { return m_savePath; }

    bool loadFromDisk()
    {
        if (m_savePath.empty())
        {
            return false;
        }
        std::ifstream in(m_savePath);
        if (!in)
        {
            return false;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (text.empty())
        {
            return false;
        }
        auto parsed = json::parseJson(text);
        if (!parsed || parsed->type != json::JsonValue::Type::Object)
        {
            return false;
        }
        const json::JsonValue &root = *parsed;
        bankedMana = static_cast<int>(std::round(json::getNumber(root, "wallet", static_cast<float>(bankedMana))));
        manaGainTokens = static_cast<int>(
            std::round(json::getNumber(root, "mana_gain_tokens", static_cast<float>(manaGainTokens))));
        debugDisableYunaSpawns = json::getBool(root, "debug_disable_yuna_spawns", debugDisableYunaSpawns);
        debugDisableBaseRegenAndDef =
            json::getBool(root, "debug_disable_base_regen_def", debugDisableBaseRegenAndDef);

        auto loadIntMap = [](const json::JsonValue *obj, std::unordered_map<std::string, int> &dst) {
            if (!obj || obj->type != json::JsonValue::Type::Object)
            {
                dst.clear();
                return;
            }
            dst.clear();
            for (const auto &kv : obj->object)
            {
                if (kv.second.type == json::JsonValue::Type::Number)
                {
                    dst[kv.first] = static_cast<int>(std::round(kv.second.number));
                }
            }
        };

        loadIntMap(json::getObjectField(root, "camp_upgrades"), campUpgradeLevels);
        loadIntMap(json::getObjectField(root, "training"), trainingProgress);
        loadIntMap(json::getObjectField(root, "meta_levels"), metaLevels);

        strategySelections.clear();
        if (const json::JsonValue *obj = json::getObjectField(root, "strategies"))
        {
            if (obj->type == json::JsonValue::Type::Object)
            {
                for (const auto &kv : obj->object)
                {
                    if (kv.second.type == json::JsonValue::Type::String)
                    {
                        strategySelections[kv.first] = kv.second.string;
                    }
                }
            }
        }
        return true;
    }

    bool saveToDisk() const
    {
        if (m_savePath.empty())
        {
            return false;
        }
        std::error_code ec;
        auto dir = m_savePath.parent_path();
        if (!dir.empty())
        {
            std::filesystem::create_directories(dir, ec);
        }
        std::ofstream out(m_savePath, std::ios::trunc);
        if (!out)
        {
            return false;
        }

        auto writeIntMap = [](std::ostream &os, const std::string &key, const std::unordered_map<std::string, int> &map) {
            os << "  \"" << key << "\": {";
            if (!map.empty())
            {
                std::vector<std::string> keys;
                keys.reserve(map.size());
                for (const auto &kv : map)
                {
                    keys.push_back(kv.first);
                }
                std::sort(keys.begin(), keys.end());
                os << '\n';
                for (std::size_t i = 0; i < keys.size(); ++i)
                {
                    const std::string &id = keys[i];
                    auto it = map.find(id);
                    const int value = it != map.end() ? it->second : 0;
                    os << "    \"" << id << "\": " << value;
                    if (i + 1 < keys.size())
                    {
                        os << ',';
                    }
                    os << '\n';
                }
                os << "  }";
            }
            else
            {
                os << "}";
            }
            os << ",\n";
        };

        out << "{\n";
        out << "  \"wallet\": " << bankedMana << ",\n";
        out << "  \"mana_gain_tokens\": " << manaGainTokens << ",\n";
        out << "  \"debug_disable_yuna_spawns\": " << (debugDisableYunaSpawns ? "true" : "false") << ",\n";
        out << "  \"debug_disable_base_regen_def\": " << (debugDisableBaseRegenAndDef ? "true" : "false") << ",\n";
        writeIntMap(out, "camp_upgrades", campUpgradeLevels);
        writeIntMap(out, "training", trainingProgress);
        writeIntMap(out, "meta_levels", metaLevels);

        out << "  \"strategies\": {";
        if (!strategySelections.empty())
        {
            std::vector<std::string> keys;
            keys.reserve(strategySelections.size());
            for (const auto &kv : strategySelections)
            {
                keys.push_back(kv.first);
            }
            std::sort(keys.begin(), keys.end());
            out << '\n';
            for (std::size_t i = 0; i < keys.size(); ++i)
            {
                const std::string &id = keys[i];
                auto it = strategySelections.find(id);
                const std::string &value = it != strategySelections.end() ? it->second : std::string();
                out << "    \"" << id << "\": \"" << value << '"';
                if (i + 1 < keys.size())
                {
                    out << ',';
                }
                out << '\n';
            }
            out << "  }\n";
        }
        else
        {
            out << "}\n";
        }
        out << "}\n";
        return true;
    }

  private:
    std::filesystem::path m_savePath;
};
