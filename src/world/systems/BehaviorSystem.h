#pragma once

#include "world/systems/SystemContext.h"

namespace world::systems
{

class BehaviorSystem : public ISystem
{
  public:
    BehaviorSystem() = default;

    void update(float dt, SystemContext &context) override;

  private:
    float m_tickAccumulator = 0.0f;
    float m_tokkouWarningTimer = 0.0f;
};

} // namespace world::systems
