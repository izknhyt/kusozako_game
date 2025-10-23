#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class JobAbilitySystem : public ISystem
{
  public:
    JobAbilitySystem() = default;

    void update(float dt, SystemContext &context) override;
};

} // namespace world::systems

