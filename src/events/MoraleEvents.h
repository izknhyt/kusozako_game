#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "world/MoraleTypes.h"

struct MoraleHudIcon
{
    bool commander = false;
    std::size_t unitIndex = 0;
    MoraleState state = MoraleState::Stable;
};

struct MoraleUpdateEvent
{
    std::vector<MoraleHudIcon> icons;
    std::string telemetry;
    std::size_t panicCount = 0;
    std::size_t mesomesoCount = 0;
};

inline constexpr const char *MoraleUpdateEventName = "morale.update";

