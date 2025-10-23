#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class CombatSystem : public ISystem
{
  public:
    CombatSystem() = default;

    void update(float dt, SystemContext &context) override;
};

} // namespace world::systems

