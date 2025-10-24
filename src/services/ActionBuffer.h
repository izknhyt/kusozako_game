#pragma once

#include "core/Vec2.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

enum class ActionId : std::size_t
{
    CommanderMoveX = 0,
    CommanderMoveY,
    Count
};

struct ActionSample
{
    ActionId id;
    float value = 0.0f;
};

class ActionBuffer
{
  public:
    struct Frame
    {
        std::uint64_t sequence = 0;
        double deviceTimestampMs = 0.0;
        std::array<float, static_cast<std::size_t>(ActionId::Count)> values{};
    };

    explicit ActionBuffer(std::size_t capacity = 4);

    void setCapacity(std::size_t capacity);
    std::size_t capacity() const { return m_capacity; }

    void clear();

    void pushFrame(std::uint64_t sequence,
                   double deviceTimestampMs,
                   float moveX,
                   float moveY);

    void expireOlderThan(double minTimestampMs);

    bool empty() const { return m_frames.empty(); }
    const Frame *latest() const;

    Vec2 commanderMoveVector() const;

  private:
    std::size_t m_capacity;
    std::deque<Frame> m_frames;
};

