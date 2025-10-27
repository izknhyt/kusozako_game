#pragma once

#include "core/Vec2.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace world
{

class SpatialGrid
{
  public:
    SpatialGrid() = default;

    void configure(const Vec2 &min, const Vec2 &max, float cellSize)
    {
        const float clampedSize = std::max(cellSize, 1.0f);
        Vec2 minBounds = min;
        Vec2 maxBounds = max;
        if (maxBounds.x <= minBounds.x)
        {
            maxBounds.x = minBounds.x + clampedSize;
        }
        if (maxBounds.y <= minBounds.y)
        {
            maxBounds.y = minBounds.y + clampedSize;
        }
        const float width = std::max(maxBounds.x - minBounds.x, clampedSize);
        const float height = std::max(maxBounds.y - minBounds.y, clampedSize);
        const std::size_t cols = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(width / clampedSize)));
        const std::size_t rows = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(height / clampedSize)));
        const bool sizeChanged = cols != m_cols || rows != m_rows || clampedSize != m_cellSize;

        m_min = minBounds;
        m_max = maxBounds;
        m_cellSize = clampedSize;
        m_cols = cols;
        m_rows = rows;

        if (sizeChanged || m_cells.size() != m_cols * m_rows)
        {
            m_cells.clear();
            m_cells.resize(m_cols * m_rows);
        }
    }

    void clear()
    {
        for (Cell &cell : m_cells)
        {
            cell.units.clear();
            cell.enemies.clear();
            cell.walls.clear();
        }
    }

    struct Cell
    {
        std::vector<std::size_t> units;
        std::vector<std::size_t> enemies;
        std::vector<std::size_t> walls;
    };

    void insertUnit(std::size_t index, const Vec2 &pos, float radius)
    {
        insertImpl<&Cell::units>(index, pos, radius);
    }

    void insertEnemy(std::size_t index, const Vec2 &pos, float radius)
    {
        insertImpl<&Cell::enemies>(index, pos, radius);
    }

    void insertWall(std::size_t index, const Vec2 &pos, float radius)
    {
        insertImpl<&Cell::walls>(index, pos, radius);
    }

    void queryCells(const Vec2 &pos, float radius, std::vector<std::size_t> &outCells) const
    {
        outCells.clear();
        if (m_cells.empty())
        {
            return;
        }
        const Range range = computeRange(pos, radius);
        for (std::size_t y = range.minY; y <= range.maxY; ++y)
        {
            const std::size_t rowOffset = y * m_cols;
            for (std::size_t x = range.minX; x <= range.maxX; ++x)
            {
                outCells.push_back(rowOffset + x);
            }
        }
    }

    const Cell &cell(std::size_t index) const
    {
        return m_cells[index];
    }

    float cellSize() const
    {
        return m_cellSize;
    }

  private:
    struct Range
    {
        std::size_t minX = 0;
        std::size_t maxX = 0;
        std::size_t minY = 0;
        std::size_t maxY = 0;
    };

    Vec2 m_min{0.0f, 0.0f};
    Vec2 m_max{0.0f, 0.0f};
    float m_cellSize = 1.0f;
    std::size_t m_cols = 0;
    std::size_t m_rows = 0;
    std::vector<Cell> m_cells;

    Range computeRange(const Vec2 &pos, float radius) const
    {
        Range range{};
        if (m_cols == 0 || m_rows == 0)
        {
            return range;
        }
        const float queryRadius = std::max(radius, 0.0f);
        const float minX = pos.x - queryRadius;
        const float maxX = pos.x + queryRadius;
        const float minY = pos.y - queryRadius;
        const float maxY = pos.y + queryRadius;

        int startX = static_cast<int>(std::floor((minX - m_min.x) / m_cellSize));
        int endX = static_cast<int>(std::floor((maxX - m_min.x) / m_cellSize));
        int startY = static_cast<int>(std::floor((minY - m_min.y) / m_cellSize));
        int endY = static_cast<int>(std::floor((maxY - m_min.y) / m_cellSize));

        const int maxCol = static_cast<int>(m_cols) - 1;
        const int maxRow = static_cast<int>(m_rows) - 1;

        startX = std::clamp(startX, 0, maxCol);
        endX = std::clamp(endX, 0, maxCol);
        startY = std::clamp(startY, 0, maxRow);
        endY = std::clamp(endY, 0, maxRow);

        if (endX < startX)
        {
            endX = startX;
        }
        if (endY < startY)
        {
            endY = startY;
        }

        range.minX = static_cast<std::size_t>(startX);
        range.maxX = static_cast<std::size_t>(endX);
        range.minY = static_cast<std::size_t>(startY);
        range.maxY = static_cast<std::size_t>(endY);
        return range;
    }

    template <std::vector<std::size_t> Cell::*Member>
    void insertImpl(std::size_t index, const Vec2 &pos, float radius)
    {
        if (m_cells.empty())
        {
            return;
        }
        const Range range = computeRange(pos, radius);
        for (std::size_t y = range.minY; y <= range.maxY; ++y)
        {
            const std::size_t rowOffset = y * m_cols;
            for (std::size_t x = range.minX; x <= range.maxX; ++x)
            {
                (m_cells[rowOffset + x].*Member).push_back(index);
            }
        }
    }
};

} // namespace world

