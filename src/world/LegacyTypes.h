#pragma once

#include <string>
#include <vector>
#include <array>

#include "config/AppConfig.h"
#include "world/MoraleTypes.h"

struct HUDState
{
    std::string telemetryText;
    float telemetryTimer = 0.0f;
    std::string resultText;
    float resultTimer = 0.0f;
    std::size_t unconsumedEvents = 0;

    struct AlignmentCountdown
    {
        bool active = false;
        std::string label;
        float secondsRemaining = 0.0f;
        float progress = 0.0f;
        std::size_t followers = 0;
    } alignment;

    struct MoraleStatus
    {
        MoraleState commanderState = MoraleState::Stable;
        float leaderDownTimer = 0.0f;
        float commanderBarrierTimer = 0.0f;
        std::size_t panicCount = 0;
        std::size_t mesomesoCount = 0;
    } morale;

    struct JobSummary
    {
        struct Entry
        {
            UnitJob job = UnitJob::Warrior;
            std::size_t total = 0;
            std::size_t ready = 0;
            float maxCooldown = 0.0f;
            float maxEndlag = 0.0f;
            bool specialActive = false;
            float specialTimer = 0.0f;
        };

        std::array<Entry, UnitJobCount> entries{};

        struct Skill
        {
            std::string id;
            std::string label;
            float cooldownRemaining = 0.0f;
            float activeTimer = 0.0f;
            bool toggled = false;
        };

        std::vector<Skill> skills;
    } jobs;
};

enum class GameResult
{
    Playing,
    Victory,
    Defeat
};
