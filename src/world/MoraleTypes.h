#pragma once

#include <cstdint>
#include <string_view>

enum class MoraleState : std::uint8_t
{
    Stable = 0,
    LeaderDown,
    Panic,
    Mesomeso,
    Recovering,
    Shielded
};

struct MoraleModifiers
{
    float speed = 1.0f;
    float accuracy = 1.0f;
    float defense = 1.0f;
};

inline std::string_view moraleStateLabel(MoraleState state)
{
    switch (state)
    {
    case MoraleState::Stable:
        return "stable";
    case MoraleState::LeaderDown:
        return "leader_down";
    case MoraleState::Panic:
        return "panic";
    case MoraleState::Mesomeso:
        return "mesomeso";
    case MoraleState::Recovering:
        return "recovering";
    case MoraleState::Shielded:
        return "shielded";
    }
    return "unknown";
}

