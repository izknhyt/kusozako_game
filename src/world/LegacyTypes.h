#pragma once

#include <string>

struct HUDState
{
    std::string telemetryText;
    float telemetryTimer = 0.0f;
    std::string resultText;
    float resultTimer = 0.0f;
};

enum class GameResult
{
    Playing,
    Victory,
    Defeat
};
