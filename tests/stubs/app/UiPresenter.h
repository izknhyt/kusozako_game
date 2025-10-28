#pragma once

#include "world/FormationUtils.h"
#include "world/LegacyTypes.h"
#include "world/MoraleTypes.h"

#include <string>
#include <vector>

struct MoraleHudIcon
{
    MoraleState state = MoraleState::Stable;
};

struct FormationHudStatus
{
    Formation formation = Formation::Swarm;
    FormationAlignmentState state = FormationAlignmentState::Idle;
    float progress = 0.0f;
    float secondsRemaining = 0.0f;
    std::size_t followers = 0;
    std::string label;
    struct AlignmentCountdown
    {
        bool active = false;
        std::string label;
        float secondsRemaining = 0.0f;
        float progress = 0.0f;
        std::size_t followers = 0;
    } countdown;
};

struct MoraleHudStatus
{
    std::vector<MoraleHudIcon> icons;
    std::size_t panicCount = 0;
    std::size_t mesomesoCount = 0;
    struct Summary
    {
        MoraleState commanderState = MoraleState::Stable;
        float leaderDownTimer = 0.0f;
        float commanderBarrierTimer = 0.0f;
        std::size_t panicCount = 0;
        std::size_t mesomesoCount = 0;
    } summary;
};

struct JobHudEntryStatus
{
    UnitJob job = UnitJob::Warrior;
    std::size_t total = 0;
    std::size_t ready = 0;
    float maxCooldown = 0.0f;
    float maxEndlag = 0.0f;
    bool specialActive = false;
    float specialTimer = 0.0f;
};

struct JobHudSkillStatus
{
    std::string id;
    std::string label;
    float cooldownRemaining = 0.0f;
    float activeTimer = 0.0f;
    bool toggled = false;
};

struct JobHudStatus
{
    std::vector<JobHudEntryStatus> jobs;
    std::vector<JobHudSkillStatus> skills;
};

class UiPresenter
{
  public:
    UiPresenter() = default;
};

