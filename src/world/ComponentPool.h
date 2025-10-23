#pragma once

#include "Entity.h"

#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace world
{

template <typename T>
class ComponentPool
{
  public:
    ComponentPool() = default;

    std::size_t size() const { return m_components.size(); }

    bool empty() const { return m_components.empty(); }

    void reserve(std::size_t count)
    {
        m_components.reserve(count);
        m_entities.reserve(count);
    }

    bool has(const EntityRegistry &registry, EntityId entity) const
    {
        if (!registry.isAlive(entity))
        {
            return false;
        }
        const std::uint32_t idx = entity.index;
        if (idx >= m_sparse.size())
        {
            return false;
        }
        const std::uint32_t denseIndex = m_sparse[idx];
        return denseIndex != Invalid && denseIndex < m_components.size() &&
               m_entities[denseIndex].generation == entity.generation;
    }

    T &get(const EntityRegistry &registry, EntityId entity)
    {
        return m_components.at(indexOf(registry, entity));
    }

    const T &get(const EntityRegistry &registry, EntityId entity) const
    {
        return m_components.at(indexOf(registry, entity));
    }

    EntityId entityAt(std::size_t denseIndex) const
    {
        return m_entities.at(denseIndex);
    }

    T &operator[](std::size_t denseIndex)
    {
        return m_components[denseIndex];
    }

    const T &operator[](std::size_t denseIndex) const
    {
        return m_components[denseIndex];
    }

    template <typename... Args>
    std::pair<EntityId, T &> create(EntityRegistry &registry, Args &&...args)
    {
        EntityId id = registry.create();
        ensureSparse(registry.capacity());
        const std::size_t denseIndex = m_components.size();
        m_components.emplace_back(std::forward<Args>(args)...);
        m_entities.push_back(id);
        m_sparse[id.index] = static_cast<std::uint32_t>(denseIndex);
        return {id, m_components.back()};
    }

    void attach(EntityRegistry &registry, EntityId entity, const T &component)
    {
        ensureSparse(registry.capacity());
        if (has(registry, entity))
        {
            m_components[m_sparse[entity.index]] = component;
            return;
        }
        const std::size_t denseIndex = m_components.size();
        m_components.push_back(component);
        m_entities.push_back(entity);
        m_sparse[entity.index] = static_cast<std::uint32_t>(denseIndex);
    }

    void remove(EntityRegistry &registry, EntityId entity)
    {
        if (!has(registry, entity))
        {
            return;
        }
        const std::uint32_t denseIndex = m_sparse[entity.index];
        const std::uint32_t lastIndex = static_cast<std::uint32_t>(m_components.size() - 1);
        if (denseIndex != lastIndex)
        {
            m_components[denseIndex] = std::move(m_components[lastIndex]);
            m_entities[denseIndex] = m_entities[lastIndex];
            m_sparse[m_entities[denseIndex].index] = denseIndex;
        }
        m_components.pop_back();
        m_entities.pop_back();
        m_sparse[entity.index] = Invalid;
        registry.destroy(entity);
    }

    void removeAt(EntityRegistry &registry, std::size_t denseIndex)
    {
        if (denseIndex >= m_components.size())
        {
            return;
        }
        const EntityId entity = m_entities[denseIndex];
        remove(registry, entity);
    }

    void clear(EntityRegistry &registry)
    {
        for (EntityId entity : m_entities)
        {
            registry.destroy(entity);
        }
        m_components.clear();
        m_entities.clear();
        m_sparse.clear();
    }

    template <typename Fn>
    void forEach(Fn &&fn)
    {
        for (std::size_t i = 0; i < m_components.size(); ++i)
        {
            fn(m_entities[i], m_components[i]);
        }
    }

    template <typename Fn>
    void forEach(Fn &&fn) const
    {
        for (std::size_t i = 0; i < m_components.size(); ++i)
        {
            fn(m_entities[i], m_components[i]);
        }
    }

  private:
    static constexpr std::uint32_t Invalid = std::numeric_limits<std::uint32_t>::max();

    std::vector<T> m_components;
    std::vector<EntityId> m_entities;
    std::vector<std::uint32_t> m_sparse;

    void ensureSparse(std::size_t capacity)
    {
        if (m_sparse.size() < capacity)
        {
            m_sparse.resize(capacity, Invalid);
        }
    }

    std::size_t indexOf(const EntityRegistry &registry, EntityId entity) const
    {
        if (!has(registry, entity))
        {
            throw std::out_of_range("ComponentPool::indexOf invalid entity");
        }
        return m_sparse[entity.index];
    }
};

} // namespace world

