#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/Vec2.h"

struct StageAllyFactoryConfig
{
    float ratePerSecond = 1.0f;
    int cap = 200;
};

struct StageBaseSealBehavior
{
    bool stopSpawn = true;
    bool visualOnly = true;
    bool removeCollision = true;
};

struct StageGlobalCaps
{
    int enemies = 40;
    int chibi = 200;
};

struct StageVictoryRequirements
{
    bool requireDragon = true;
    bool requireAllEnemyBases = true;
};

struct StageSpeedConfig
{
    std::vector<float> steps{1.0f, 2.0f, 3.0f};
    float hpBarFastInterval = 0.15f;
};

struct StageAllyBaseConfig
{
    std::string id;
    Vec2 position{};
    float hp = 200.0f;
    float auraRadiusPx = 128.0f;
    bool sealedByDefault = false;
};

struct StageEnemyBaseConfig
{
    std::string id;
    Vec2 position{};
    float hp = 300.0f;
    float ratePerSecond = 0.10f;
    float rateMax = 0.18f;
    std::unordered_map<std::string, float> weights;
    bool sealedByDefault = false;
};

struct StageHazardConfig
{
    std::string id;
    Vec2 position{};
    float radiusPx = 0.0f;
    float weight = 1.0f;
    bool enabled = true;
    std::string triggerEnemyBase;
};

struct StageConfig
{
    int schemaVersion = 1;
    StageAllyFactoryConfig allyFactory{};
    StageBaseSealBehavior sealBehavior{};
    StageGlobalCaps globalCaps{};
    StageVictoryRequirements victory{};
    StageSpeedConfig speed{};
    std::vector<StageAllyBaseConfig> allyBases;
    std::vector<StageEnemyBaseConfig> enemyBases;
    std::vector<StageHazardConfig> hazards;
};
