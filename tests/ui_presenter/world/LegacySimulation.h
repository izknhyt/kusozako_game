#pragma once

#include "config/AppConfig.h"
#include "world/LegacyTypes.h"
#include "world/StageConfig.h"

namespace world
{

struct LegacySimulation
{
    GameConfig config{};
    HUDState hud{};
    struct StubAllyBase
    {
        std::string id;
        float hp = 0.0f;
        float maxHp = 0.0f;
        bool destroyed = false;
        float auraRadiusPx = 0.0f;
    };
    struct StubEnemyBase
    {
        std::string id;
        float hp = 0.0f;
        float maxHp = 0.0f;
        bool sealed = false;
    };
    struct StageRuntimeState
    {
        bool enabled = false;
        bool dragonDefeated = false;
        StageVictoryRequirements victory{};
        std::vector<StubAllyBase> allyBases;
        std::vector<StubEnemyBase> enemyBases;
    } stage{};

    StageRuntimeState &stageState() { return stage; }
    const StageRuntimeState &stageState() const { return stage; }
    std::size_t sealedEnemyBases() const
    {
        if (!stage.enabled)
        {
            return 0;
        }
        std::size_t count = 0;
        for (const auto &base : stage.enemyBases)
        {
            if (base.sealed)
            {
                ++count;
            }
        }
        return count;
    }
};

} // namespace world
