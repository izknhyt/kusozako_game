#pragma once

#include <deque>
#include <string>
#include <vector>
#include <array>

#include "config/AppConfig.h"
#include "world/MoraleTypes.h"

enum class GameResult
{
    Playing,
    Victory,
    Defeat
};

struct HUDState
{
    static constexpr std::size_t kHudActionCount = 7;
    std::string telemetryText;
    float telemetryTimer = 0.0f;
    std::string resultText;
    float resultTimer = 0.0f;
    struct ResultStats
    {
        bool available = false;
        GameResult result = GameResult::Playing;
        float durationSeconds = 0.0f;
        int chibiDeaths = 0;
        int chibiSurvivors = 0;
        int enemyKills = 0;
        int manaEarned = 0;
        int manaCap = 0;
        int manaBonusPercent = 0;
        int basesSealed = 0;
        int basesTotal = 0;
    } resultStats;
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

    struct PerformanceWarning
    {
        bool active = false;
        std::string message;
        float timer = 0.0f;
    } performance;

    struct SpawnBudgetWarning
    {
        bool active = false;
        std::string message;
        float timer = 0.0f;
        std::size_t lastDeferred = 0;
        std::size_t totalDeferred = 0;
    } spawnBudget;

    struct StageBanner
    {
        std::string text;
        float timer = 0.0f;
    } stageBanner;

    struct TipMessage
    {
        std::string text;
        float timer = 0.0f;
    } tip;

    struct EconomyHud
    {
        int currency = 0;
        int cap = 0;
        int tokens = 0;
        std::string recommended;
    } economy;

    struct AiDebugInfo
    {
        struct PanicEntry
        {
            int unitIndex = -1;
            std::string state;
            float timer = 0.0f;
        };

        bool available = false;
        float panicRatio = 0.0f;
        float avgRadius = 0.0f;
        float exceedRatio = 0.0f;
        float clingRatio = 0.0f;
        float kaitenRatio = 0.0f;
        float runRatio = 0.0f;
        float hazardAverage = 0.0f;
        float aoeRatio = 0.0f;
        float aoeMagnitude = 0.0f;
        std::array<float, kHudActionCount> actionRatios{};
        std::vector<std::string> history;
        std::deque<std::array<float, kHudActionCount>> ratioHistory;
        std::vector<PanicEntry> panicEntries;
    } aiDebug;
};
