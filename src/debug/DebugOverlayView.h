#pragma once

#include "app/FramePerf.h"

#include <string>

struct SDL_Renderer;
struct SDL_Color;
struct SDL_Rect;

class TextRenderer;
struct RenderStats;

namespace debug
{

struct DisplayState;

class DebugOverlayView
{
  public:
    void render(SDL_Renderer *renderer,
                const TextRenderer &headerFont,
                const TextRenderer &bodyFont,
                const DisplayState &state,
                const FramePerf &framePerf,
                RenderStats &stats,
                int screenWidth,
                int screenHeight) const;

  private:
    void renderCategories(SDL_Renderer *renderer,
                          const TextRenderer &font,
                          const DisplayState &state,
                          RenderStats &stats,
                          int x,
                          int y,
                          int width) const;

    void renderEntries(SDL_Renderer *renderer,
                       const TextRenderer &font,
                       const DisplayState &state,
                       RenderStats &stats,
                       int x,
                       int y,
                       int width,
                       int lineHeight) const;

    void renderTelemetry(SDL_Renderer *renderer,
                         const TextRenderer &font,
                         const FramePerf &perf,
                         RenderStats &stats,
                         int panelX,
                         int panelY,
                         int panelWidth,
                         int lineHeight) const;
};

} // namespace debug
