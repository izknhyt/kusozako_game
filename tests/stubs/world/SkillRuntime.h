#pragma once

#include <string>

enum class SkillType
{
    Generic,
    SpawnRate,
};

struct SkillDef
{
    int hotkey = 0;
    std::string displayName;
    SkillType type = SkillType::Generic;
};

struct RuntimeSkill
{
    SkillDef def;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
};

