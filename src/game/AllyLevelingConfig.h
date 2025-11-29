#pragma once

struct AllyLevelingParams
{
    int maxLevel = 9999;
    // Flat additions per level (not percentage multipliers)
    float hpPerLevel = 1.0f;       // +1 HP / level
    float dpsPerLevel = 0.05f;     // +0.05 DPS / level
    float cooldownFracPerLevel = 0.00018f;
    float minCooldownRatio = 0.6f;
    float speedPerLevel = 0.002f;  // +0.002 u/s / level
    float critPerLevel = 0.000009f;
};

inline constexpr AllyLevelingParams kCommanderLevelingParams{};
