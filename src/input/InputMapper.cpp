#include "input/InputMapper.h"

#include <algorithm>
#include <cctype>

namespace
{

ActionId skillActionId(std::size_t index)
{
    constexpr std::array<ActionId, 8> kSkillIds{
        ActionId::SelectSkill1,
        ActionId::SelectSkill2,
        ActionId::SelectSkill3,
        ActionId::SelectSkill4,
        ActionId::SelectSkill5,
        ActionId::SelectSkill6,
        ActionId::SelectSkill7,
        ActionId::SelectSkill8};
    if (index < kSkillIds.size())
    {
        return kSkillIds[index];
    }
    return ActionId::Count;
}

Uint8 pointerButtonFromString(const std::string &name)
{
    std::string normalized;
    normalized.reserve(name.size());
    for (char ch : name)
    {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (normalized == "mouseleft")
    {
        return SDL_BUTTON_LEFT;
    }
    if (normalized == "mouseright")
    {
        return SDL_BUTTON_RIGHT;
    }
    if (normalized == "mousemiddle" || normalized == "mousewheel")
    {
        return SDL_BUTTON_MIDDLE;
    }
    return 0;
}

} // namespace

InputMapper::InputMapper()
    : m_bufferFrames(4),
      m_bufferExpiryMs(80.0)
{
    m_skillBindings.fill(ActionId::Count);
}

void InputMapper::configure(const InputBindings &bindings)
{
    m_movePositiveX.clear();
    m_moveNegativeX.clear();
    m_movePositiveY.clear();
    m_moveNegativeY.clear();
    m_pressBindings.clear();
    m_pendingEvents.clear();
    m_skillBindings.fill(ActionId::Count);
    m_pointerActivate = {};

    m_movePositiveX = {};
    m_moveNegativeX = {};
    m_movePositiveY = {};
    m_moveNegativeY = {};
    for (const std::string &name : bindings.commanderMoveRight)
    {
        if (SDL_Scancode sc = scancodeFromName(name); sc != SDL_SCANCODE_UNKNOWN)
        {
            m_movePositiveX.push_back(sc);
        }
    }
    for (const std::string &name : bindings.commanderMoveLeft)
    {
        if (SDL_Scancode sc = scancodeFromName(name); sc != SDL_SCANCODE_UNKNOWN)
        {
            m_moveNegativeX.push_back(sc);
        }
    }
    for (const std::string &name : bindings.commanderMoveDown)
    {
        if (SDL_Scancode sc = scancodeFromName(name); sc != SDL_SCANCODE_UNKNOWN)
        {
            m_movePositiveY.push_back(sc);
        }
    }
    for (const std::string &name : bindings.commanderMoveUp)
    {
        if (SDL_Scancode sc = scancodeFromName(name); sc != SDL_SCANCODE_UNKNOWN)
        {
            m_moveNegativeY.push_back(sc);
        }
    }

    if (!bindings.focusCommander.empty())
    {
        bindKey(bindings.focusCommander, ActionId::FocusCommander);
    }
    if (!bindings.focusBase.empty())
    {
        bindKey(bindings.focusBase, ActionId::FocusBase);
    }
    if (!bindings.restart.empty())
    {
        bindKey(bindings.restart, ActionId::RestartScenario);
    }
    if (!bindings.toggleDebugHud.empty())
    {
        bindKey(bindings.toggleDebugHud, ActionId::ToggleDebugHud);
    }
    if (!bindings.quit.empty())
    {
        bindKey(bindings.quit, ActionId::QuitGame);
    }
    if (!bindings.formationPrevious.empty())
    {
        bindKey(bindings.formationPrevious, ActionId::CycleFormationPrevious);
    }
    if (!bindings.formationNext.empty())
    {
        bindKey(bindings.formationNext, ActionId::CycleFormationNext);
    }

    bindKeys(bindings.orderRushNearest, ActionId::CommanderOrderRushNearest);
    bindKeys(bindings.orderPushForward, ActionId::CommanderOrderPushForward);
    bindKeys(bindings.orderFollowLeader, ActionId::CommanderOrderFollowLeader);
    bindKeys(bindings.orderDefendBase, ActionId::CommanderOrderDefendBase);

    bindSkillHotkeys(bindings.summonMode);

    if (!bindings.skillActivate.empty())
    {
        if (Uint8 button = pointerButtonFromString(bindings.skillActivate); button != 0)
        {
            m_pointerActivate.action = ActionId::ActivateSkill;
            m_pointerActivate.button = button;
        }
    }

    const int configuredFrames = bindings.bufferFrames > 0 ? bindings.bufferFrames : 1;
    setBufferFrames(static_cast<std::size_t>(configuredFrames));
    setBufferExpiryMs(static_cast<double>(bindings.bufferExpiryMs));
}

void InputMapper::setBufferFrames(std::size_t frames)
{
    m_bufferFrames = frames == 0 ? 1 : frames;
}

void InputMapper::setBufferExpiryMs(double ms)
{
    m_bufferExpiryMs = ms < 0.0 ? 0.0 : ms;
}

void InputMapper::beginEventPump()
{
    m_pendingEvents.clear();
}

void InputMapper::handleEvent(const SDL_Event &event)
{
    switch (event.type)
    {
    case SDL_KEYDOWN:
        if (event.key.repeat == 0)
        {
            const SDL_Scancode sc = event.key.keysym.scancode;
            auto it = m_pressBindings.find(sc);
            if (it != m_pressBindings.end())
            {
                for (ActionId action : it->second)
                {
                    ActionEvent evt;
                    evt.id = action;
                    evt.value = 1.0f;
                    evt.pressed = true;
                    m_pendingEvents.push_back(evt);
                }
            }
        }
        break;
    case SDL_MOUSEMOTION:
        m_pointerState.hasPosition = true;
        m_pointerState.x = event.motion.x;
        m_pointerState.y = event.motion.y;
        m_pointerState.left = (event.motion.state & SDL_BUTTON_LMASK) != 0;
        m_pointerState.right = (event.motion.state & SDL_BUTTON_RMASK) != 0;
        m_pointerState.middle = (event.motion.state & SDL_BUTTON_MMASK) != 0;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    {
        const bool pressed = event.type == SDL_MOUSEBUTTONDOWN;
        const Uint8 button = event.button.button;
        m_pointerState.hasPosition = true;
        m_pointerState.x = event.button.x;
        m_pointerState.y = event.button.y;
        if (button == SDL_BUTTON_LEFT)
        {
            m_pointerState.left = pressed;
        }
        else if (button == SDL_BUTTON_RIGHT)
        {
            m_pointerState.right = pressed;
        }
        else if (button == SDL_BUTTON_MIDDLE)
        {
            m_pointerState.middle = pressed;
        }
        if (m_pointerActivate.valid() && button == m_pointerActivate.button)
        {
            enqueuePointerEvent(m_pointerActivate.action, pressed, !pressed, event.button.x, event.button.y);
        }
        break;
    }
    default:
        break;
    }
}

void InputMapper::sampleFrame(bool commanderEnabled,
                              double deviceTimestampMs,
                              std::uint64_t frameSequence,
                              ActionBuffer &buffer)
{
    if (buffer.capacity() != m_bufferFrames)
    {
        buffer.setCapacity(m_bufferFrames);
    }
    if (m_bufferExpiryMs > 0.0)
    {
        buffer.expireOlderThan(deviceTimestampMs - m_bufferExpiryMs);
    }

    const Uint8 *keyboardState = SDL_GetKeyboardState(nullptr);

    std::array<float, static_cast<std::size_t>(AxisId::Count)> axes{};
    if (commanderEnabled && keyboardState)
    {
        axes[static_cast<std::size_t>(AxisId::CommanderMoveX)] =
            axisValue(m_movePositiveX, m_moveNegativeX, keyboardState);
        axes[static_cast<std::size_t>(AxisId::CommanderMoveY)] =
            axisValue(m_movePositiveY, m_moveNegativeY, keyboardState);
    }

    buffer.pushFrame(frameSequence, deviceTimestampMs, axes, std::move(m_pendingEvents), m_pointerState);
    m_pendingEvents.clear();
}

SDL_Scancode InputMapper::scancodeFromName(const std::string &name)
{
    return SDL_GetScancodeFromName(name.c_str());
}

float InputMapper::axisValue(const AxisList &positive, const AxisList &negative, const Uint8 *keyboardState)
{
    if (!keyboardState)
    {
        return 0.0f;
    }

    float pos = 0.0f;
    for (SDL_Scancode code : positive)
    {
        if (keyboardState[code])
        {
            pos = 1.0f;
            break;
        }
    }

    float neg = 0.0f;
    for (SDL_Scancode code : negative)
    {
        if (keyboardState[code])
        {
            neg = 1.0f;
            break;
        }
    }

    return std::clamp(pos - neg, -1.0f, 1.0f);
}

void InputMapper::bindKeys(const std::vector<std::string> &names, ActionId action)
{
    for (const std::string &name : names)
    {
        bindKey(name, action);
    }
}

void InputMapper::bindKey(const std::string &name, ActionId action)
{
    if (name.empty())
    {
        return;
    }
    if (SDL_Scancode sc = scancodeFromName(name); sc != SDL_SCANCODE_UNKNOWN)
    {
        m_pressBindings[sc].push_back(action);
    }
}

void InputMapper::bindSkillHotkeys(const std::vector<std::string> &names)
{
    for (std::size_t i = 0; i < names.size() && i < m_skillBindings.size(); ++i)
    {
        if (ActionId id = skillActionId(i); id != ActionId::Count)
        {
            m_skillBindings[i] = id;
            bindKey(names[i], id);
        }
    }
}

void InputMapper::enqueuePointerEvent(ActionId action, bool pressed, bool released, int x, int y)
{
    ActionEvent evt;
    evt.id = action;
    evt.value = pressed ? 1.0f : 0.0f;
    evt.pressed = pressed;
    evt.released = released;
    PointerPayload payload;
    payload.x = x;
    payload.y = y;
    payload.pressed = pressed;
    payload.released = released;
    evt.pointer = payload;
    m_pendingEvents.push_back(evt);
}

