#pragma once

#include "config/AppConfig.h"
#include "world/LegacyTypes.h"

namespace world
{

struct LegacySimulation
{
    GameConfig config{};
    HUDState hud{};
};

} // namespace world

