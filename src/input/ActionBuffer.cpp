#include "input/ActionBuffer.h"

#include <algorithm>

namespace
{

constexpr std::size_t clampCapacity(std::size_t capacity)
{
    return capacity == 0 ? 1 : capacity;
}

std::size_t axisIndex(AxisId axis)
{
    return static_cast<std::size_t>(axis);
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

void ActionBuffer::pushFrame(std::uint64_t sequence,
                             double deviceTimestampMs,
                             const std::array<float, static_cast<std::size_t>(AxisId::Count)> &axes,
                             std::vector<ActionEvent> events,
                             const PointerState &pointer)
{
    Frame frame;
    frame.sequence = sequence;
    frame.deviceTimestampMs = deviceTimestampMs;
    frame.axes = axes;
    frame.events = std::move(events);
    frame.pointer = pointer;

    if (!m_frames.empty() && m_frames.back().sequence == sequence)
    {
        m_frames.back() = std::move(frame);
    }
    else
    {
        m_frames.push_back(std::move(frame));
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
        vec.x = std::clamp(frame->axes[axisIndex(AxisId::CommanderMoveX)], -1.0f, 1.0f);
        vec.y = std::clamp(frame->axes[axisIndex(AxisId::CommanderMoveY)], -1.0f, 1.0f);
        return vec;
    }
    return {0.0f, 0.0f};
}

