#pragma once

#include <string>

enum class MoraleState
{
    Stable,
    LeaderDown,
    Panic,
    Mesomeso,
    Recovering,
    Shielded,
};

inline const char *moraleStateLabel(MoraleState state)
{
    switch (state)
    {
    case MoraleState::LeaderDown: return "leader_down";
    case MoraleState::Panic: return "panic";
    case MoraleState::Mesomeso: return "mesomeso";
    case MoraleState::Recovering: return "recovering";
    case MoraleState::Shielded: return "shielded";
    case MoraleState::Stable:
    default: return "stable";
    }
}

