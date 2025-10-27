#pragma once

#include "world/SpatialGrid.h"
#include "world/systems/SystemContext.h"

#include <cstdint>
#include <vector>

namespace world::systems
{

class CombatSystem : public ISystem
{
  public:
    CombatSystem() = default;

    void update(float dt, SystemContext &context) override;

  private:
    SpatialGrid m_grid;
    std::vector<std::uint32_t> m_enemyVisit;
    std::vector<std::uint32_t> m_wallVisit;
    std::uint32_t m_enemyStamp = 1;
    std::uint32_t m_wallStamp = 1;
    std::vector<std::size_t> m_cellScratch;
    std::vector<std::size_t> m_enemyScratch;
    std::vector<std::size_t> m_wallScratch;
};

} // namespace world::systems

