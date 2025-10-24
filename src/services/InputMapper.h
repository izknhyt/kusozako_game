#pragma once

#include "config/AppConfig.h"
#include "services/ActionBuffer.h"

#include <SDL.h>

#include <cstdint>
#include <vector>

class InputMapper
{
  public:
    InputMapper();

    void configure(const InputBindings &bindings);

    void setBufferFrames(std::size_t frames);
    void setBufferExpiryMs(double ms);

    void captureKeyboardFrame(const Uint8 *keyboardState,
                              bool commanderEnabled,
                              double deviceTimestampMs,
                              std::uint64_t frameSequence,
                              ActionBuffer &buffer) const;

  private:
    std::vector<SDL_Scancode> m_movePositiveX;
    std::vector<SDL_Scancode> m_moveNegativeX;
    std::vector<SDL_Scancode> m_movePositiveY;
    std::vector<SDL_Scancode> m_moveNegativeY;
    std::size_t m_bufferFrames;
    double m_bufferExpiryMs;
};

