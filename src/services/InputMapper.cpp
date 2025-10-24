#include "services/InputMapper.h"

#include <algorithm>
#include <string>

namespace
{

float axisValue(const std::vector<SDL_Scancode> &positive,
                const std::vector<SDL_Scancode> &negative,
                const Uint8 *state)
{
    if (!state)
    {
        return 0.0f;
    }

    float pos = 0.0f;
    for (SDL_Scancode code : positive)
    {
        if (state[code])
        {
            pos = 1.0f;
            break;
        }
    }

    float neg = 0.0f;
    for (SDL_Scancode code : negative)
    {
        if (state[code])
        {
            neg = 1.0f;
            break;
        }
    }

    return std::clamp(pos - neg, -1.0f, 1.0f);
}

void uniqueAppend(std::vector<SDL_Scancode> &target, SDL_Scancode code)
{
    if (code == SDL_SCANCODE_UNKNOWN)
    {
        return;
    }
    if (std::find(target.begin(), target.end(), code) == target.end())
    {
        target.push_back(code);
    }
}

void fillBindings(const std::vector<std::string> &names, std::vector<SDL_Scancode> &out)
{
    out.clear();
    for (const std::string &name : names)
    {
        uniqueAppend(out, SDL_GetScancodeFromName(name.c_str()));
    }
}

} // namespace

InputMapper::InputMapper()
    : m_bufferFrames(4),
      m_bufferExpiryMs(80.0)
{
}

void InputMapper::configure(const InputBindings &bindings)
{
    fillBindings(bindings.commanderMoveRight, m_movePositiveX);
    fillBindings(bindings.commanderMoveLeft, m_moveNegativeX);
    fillBindings(bindings.commanderMoveDown, m_movePositiveY);
    fillBindings(bindings.commanderMoveUp, m_moveNegativeY);

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

void InputMapper::captureKeyboardFrame(const Uint8 *keyboardState,
                                       bool commanderEnabled,
                                       double deviceTimestampMs,
                                       std::uint64_t frameSequence,
                                       ActionBuffer &buffer) const
{
    if (buffer.capacity() != m_bufferFrames)
    {
        buffer.setCapacity(m_bufferFrames);
    }
    if (m_bufferExpiryMs > 0.0)
    {
        buffer.expireOlderThan(deviceTimestampMs - m_bufferExpiryMs);
    }

    float moveX = 0.0f;
    float moveY = 0.0f;
    if (commanderEnabled)
    {
        moveX = axisValue(m_movePositiveX, m_moveNegativeX, keyboardState);
        moveY = axisValue(m_movePositiveY, m_moveNegativeY, keyboardState);
    }

    buffer.pushFrame(frameSequence, deviceTimestampMs, moveX, moveY);
}

