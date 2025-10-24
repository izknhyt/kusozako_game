#pragma once

#include "config/AppConfig.h"

#include <cstddef>
#include <string>

enum class FormationAlignmentState
{
    Idle,
    Aligning,
    Locked
};

inline constexpr char FormationChangedEventName[] = "formation.changed";
inline constexpr char FormationProgressEventName[] = "formation.progress";

struct FormationChangedEvent
{
    Formation formation = Formation::Swarm;
    std::string label;
};

struct FormationProgressEvent
{
    Formation formation = Formation::Swarm;
    FormationAlignmentState state = FormationAlignmentState::Idle;
    float progress = 0.0f;
    std::size_t followers = 0;
};

