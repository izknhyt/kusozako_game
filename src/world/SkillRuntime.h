#pragma once

#include "config/AppConfig.h"

struct RuntimeSkill
{
    SkillDef def;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
};

