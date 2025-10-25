#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class MovementSystem : public ISystem
{
  public:
    MovementSystem() = default;

    void update(float dt, SystemContext &context) override;
};

} // namespace world::systems

