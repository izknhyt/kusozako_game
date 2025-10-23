#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class MoraleSystem : public ISystem
{
  public:
    MoraleSystem() = default;

    void update(float dt, SystemContext &context) override;
};

} // namespace world::systems

