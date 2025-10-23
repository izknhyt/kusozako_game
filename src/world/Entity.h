#pragma once

#include <cstdint>
#include <vector>

namespace world
{

struct EntityId
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    bool operator==(const EntityId &other) const
    {
        return index == other.index && generation == other.generation;
    }

    bool operator!=(const EntityId &other) const
    {
        return !(*this == other);
    }
};

class EntityRegistry
{
  public:
    EntityRegistry() = default;

    EntityId create()
    {
        if (!m_freeList.empty())
        {
            const std::uint32_t slot = m_freeList.back();
            m_freeList.pop_back();
            return EntityId{slot, m_generations[slot]};
        }

        const std::uint32_t slot = static_cast<std::uint32_t>(m_generations.size());
        m_generations.push_back(1);
        return EntityId{slot, 1};
    }

    void destroy(EntityId id)
    {
        if (!isAlive(id))
        {
            return;
        }
        ++m_generations[id.index];
        m_freeList.push_back(id.index);
    }

    bool isAlive(EntityId id) const
    {
        return id.index < m_generations.size() && m_generations[id.index] == id.generation;
    }

    std::uint32_t capacity() const
    {
        return static_cast<std::uint32_t>(m_generations.size());
    }

  private:
    std::vector<std::uint32_t> m_generations;
    std::vector<std::uint32_t> m_freeList;
};

} // namespace world

