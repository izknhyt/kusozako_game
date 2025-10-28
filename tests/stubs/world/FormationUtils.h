#pragma once

#include <string>

enum class Formation
{
    Swarm,
    Line,
};

enum class ArmyStance
{
    RushNearest,
    PushForward,
    FollowLeader,
    DefendBase,
};

enum class FormationAlignmentState
{
    Idle,
    Aligning,
};

inline const char *formationLabel(Formation formation)
{
    switch (formation)
    {
    case Formation::Line: return "Line";
    case Formation::Swarm:
    default: return "Swarm";
    }
}

