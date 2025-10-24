#pragma once

#include "config/AppConfig.h"
#include "input/ActionBuffer.h"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class InputMapper
{
  public:
    InputMapper();

    void configure(const InputBindings &bindings);

    void setBufferFrames(std::size_t frames);
    void setBufferExpiryMs(double ms);

    void beginEventPump();
    void handleEvent(const SDL_Event &event);

    void sampleFrame(bool commanderEnabled,
                     double deviceTimestampMs,
                     std::uint64_t frameSequence,
                     ActionBuffer &buffer);

  private:
    using AxisList = std::vector<SDL_Scancode>;

    struct PointerBinding
    {
        ActionId action = ActionId::Count;
        Uint8 button = 0;

        bool valid() const { return button != 0 && action != ActionId::Count; }
    };

    AxisList m_movePositiveX;
    AxisList m_moveNegativeX;
    AxisList m_movePositiveY;
    AxisList m_moveNegativeY;

    std::unordered_map<SDL_Scancode, std::vector<ActionId>> m_pressBindings;
    std::array<ActionId, 8> m_skillBindings{};

    PointerBinding m_pointerActivate;

    std::vector<ActionEvent> m_pendingEvents;
    PointerState m_pointerState;

    std::size_t m_bufferFrames;
    double m_bufferExpiryMs;

    static SDL_Scancode scancodeFromName(const std::string &name);
    static float axisValue(const AxisList &positive, const AxisList &negative, const Uint8 *keyboardState);

    void bindKeys(const std::vector<std::string> &names, ActionId action);
    void bindKey(const std::string &name, ActionId action);
    void bindSkillHotkeys(const std::vector<std::string> &names);
    void enqueuePointerEvent(ActionId action, bool pressed, bool released, int x, int y);
};

