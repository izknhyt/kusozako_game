#pragma once

#include "config/AppConfig.h"

#include <cstddef>
#include <string>
#include <vector>

struct JobHudSummaryEntry
{
    UnitJob job = UnitJob::Warrior;
    std::size_t total = 0;
    std::size_t ready = 0;
    float maxCooldown = 0.0f;
    float maxEndlag = 0.0f;
    bool specialActive = false;
    float specialTimer = 0.0f;
};

struct JobHudSkillEntry
{
    std::string id;
    std::string label;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
    bool toggled = false;
};

struct JobHudSummaryEvent
{
    std::vector<JobHudSummaryEntry> jobs;
    std::vector<JobHudSkillEntry> skills;
};

inline constexpr char JobHudSummaryEventName[] = "jobs.summary";
