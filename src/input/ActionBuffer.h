#pragma once

#include "core/Vec2.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

enum class AxisId : std::uint8_t
{
    CommanderMoveX = 0,
    CommanderMoveY,
    Count
};

enum class ActionId : std::uint16_t
{
    CommanderMoveX = 0,
    CommanderMoveY,
    CommanderOrderRushNearest,
    CommanderOrderPushForward,
    CommanderOrderFollowLeader,
    CommanderOrderDefendBase,
    CycleFormationPrevious,
    CycleFormationNext,
    ToggleDebugHud,
    RestartScenario,
    SelectSkill1,
    SelectSkill2,
    SelectSkill3,
    SelectSkill4,
    SelectSkill5,
    SelectSkill6,
    SelectSkill7,
    SelectSkill8,
    FocusCommander,
    FocusBase,
    ActivateSkill,
    QuitGame,
    Count
};

struct PointerPayload
{
    int x = 0;
    int y = 0;
    bool pressed = false;
    bool released = false;
};

struct ActionEvent
{
    ActionId id = ActionId::CommanderMoveX;
    float value = 0.0f;
    bool pressed = false;
    bool released = false;
    std::optional<PointerPayload> pointer;
};

struct PointerState
{
    bool hasPosition = false;
    int x = 0;
    int y = 0;
    bool left = false;
    bool right = false;
    bool middle = false;
};

class ActionBuffer
{
  public:
    struct Frame
    {
        std::uint64_t sequence = 0;
        double deviceTimestampMs = 0.0;
        std::array<float, static_cast<std::size_t>(AxisId::Count)> axes{};
        std::vector<ActionEvent> events;
        PointerState pointer;
    };

    explicit ActionBuffer(std::size_t capacity = 4);

    void setCapacity(std::size_t capacity);
    std::size_t capacity() const { return m_capacity; }

    void clear();

    void pushFrame(std::uint64_t sequence,
                   double deviceTimestampMs,
                   const std::array<float, static_cast<std::size_t>(AxisId::Count)> &axes,
                   std::vector<ActionEvent> events,
                   const PointerState &pointer);

    void expireOlderThan(double minTimestampMs);

    bool empty() const { return m_frames.empty(); }
    const Frame *latest() const;

    Vec2 commanderMoveVector() const;

  private:
    std::size_t m_capacity;
    std::deque<Frame> m_frames;
};

