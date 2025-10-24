#pragma once

#include "config/AppConfig.h"

inline const char *formationLabel(Formation formation)
{
    switch (formation)
    {
    case Formation::Swarm: return "Swarm";
    case Formation::Wedge: return "Wedge";
    case Formation::Line: return "Line";
    case Formation::Ring: return "Ring";
    }
    return "Unknown";
}

