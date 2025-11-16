#pragma once

struct AllyLevelingParams
{
    int maxLevel = 9999;
    float hpPerLevel = 0.5f;
    float dpsPerLevel = 0.0007f;
    float cooldownFracPerLevel = 0.00018f;
    float minCooldownRatio = 0.6f;
    float speedPerLevel = 0.0001f;
    float critPerLevel = 0.000009f;
};

inline constexpr AllyLevelingParams kCommanderLevelingParams{};
