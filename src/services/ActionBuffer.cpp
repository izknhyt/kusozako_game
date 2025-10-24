#include "services/ActionBuffer.h"

#include <algorithm>

namespace
{

constexpr std::size_t clampCapacity(std::size_t capacity)
{
    return capacity == 0 ? 1 : capacity;
}

std::size_t actionIndex(ActionId id)
{
    return static_cast<std::size_t>(id);
}

} // namespace

ActionBuffer::ActionBuffer(std::size_t capacity)
    : m_capacity(clampCapacity(capacity))
{
}

void ActionBuffer::setCapacity(std::size_t capacity)
{
    m_capacity = clampCapacity(capacity);
    while (m_frames.size() > m_capacity)
    {
        m_frames.pop_front();
    }
}

void ActionBuffer::clear()
{
    m_frames.clear();
}

void ActionBuffer::pushFrame(std::uint64_t sequence, double deviceTimestampMs, float moveX, float moveY)
{
    Frame frame;
    frame.sequence = sequence;
    frame.deviceTimestampMs = deviceTimestampMs;
    frame.values.fill(0.0f);
    frame.values[actionIndex(ActionId::CommanderMoveX)] = std::clamp(moveX, -1.0f, 1.0f);
    frame.values[actionIndex(ActionId::CommanderMoveY)] = std::clamp(moveY, -1.0f, 1.0f);

    if (!m_frames.empty() && m_frames.back().sequence == sequence)
    {
        m_frames.back() = frame;
    }
    else
    {
        m_frames.push_back(frame);
    }

    while (m_frames.size() > m_capacity)
    {
        m_frames.pop_front();
    }
}

void ActionBuffer::expireOlderThan(double minTimestampMs)
{
    while (!m_frames.empty() && m_frames.front().deviceTimestampMs < minTimestampMs)
    {
        m_frames.pop_front();
    }
}

const ActionBuffer::Frame *ActionBuffer::latest() const
{
    if (m_frames.empty())
    {
        return nullptr;
    }
    return &m_frames.back();
}

Vec2 ActionBuffer::commanderMoveVector() const
{
    if (const Frame *frame = latest())
    {
        Vec2 vec;
        vec.x = frame->values[actionIndex(ActionId::CommanderMoveX)];
        vec.y = frame->values[actionIndex(ActionId::CommanderMoveY)];
        return vec;
    }
    return {0.0f, 0.0f};
}

