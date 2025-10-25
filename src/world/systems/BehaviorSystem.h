#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class BehaviorSystem : public ISystem
{
  public:
    BehaviorSystem() = default;

    void update(float dt, SystemContext &context) override;
};

} // namespace world::systems

