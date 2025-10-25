#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class CommanderInputSystem : public ISystem
{
  public:
    CommanderInputSystem() = default;

    void update(float dt, SystemContext &context) override;
};

} // namespace world::systems

