#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class RenderingPrepSystem : public ISystem
{
  public:
    RenderingPrepSystem() = default;
    void update(float, SystemContext &) override;
};

} // namespace world::systems

