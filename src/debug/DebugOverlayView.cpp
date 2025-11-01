#include "debug/DebugOverlayView.h"

#include "app/RenderUtils.h"
#include "app/TextRenderer.h"
#include "debug/DebugController.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace debug
{

namespace
{

SDL_Color panelBackground()
{
    return SDL_Color{12, 24, 36, 210};
}

SDL_Color headerColor(bool active)
{
    return active ? SDL_Color{255, 220, 120, 255} : SDL_Color{180, 190, 210, 255};
}

SDL_Color entryColor(bool selected, bool command)
{
    if (selected)
    {
        return command ? SDL_Color{120, 255, 180, 255} : SDL_Color{255, 255, 255, 255};
    }
    return command ? SDL_Color{160, 200, 160, 255} : SDL_Color{200, 210, 220, 255};
}

SDL_Color toastColor()
{
    return SDL_Color{255, 210, 120, 255};
}

} // namespace

void DebugOverlayView::render(SDL_Renderer *renderer,
                              const TextRenderer &headerFont,
                              const TextRenderer &bodyFont,
                              const DisplayState &state,
                              const FramePerf &framePerf,
                              RenderStats &stats,
                              int screenWidth,
                              int screenHeight) const
{
    if (!renderer || !state.active)
    {
        return;
    }

    const int margin = 20;
    const int panelWidth = std::min(420, screenWidth - margin * 2);
    const int panelHeight = screenHeight - margin * 2;
    const int panelX = margin;
    const int panelY = margin;

    SDL_Rect panel{panelX, panelY, panelWidth, panelHeight};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Color bg = panelBackground();
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    countedRenderFillRect(renderer, &panel, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    int cursorY = panelY + 16;
    renderCategories(renderer, headerFont, state, stats, panelX + 16, cursorY, panelWidth - 32);

    const int lineHeight = std::max(bodyFont.getLineHeight(), 18);
    cursorY += lineHeight + 8;
    renderEntries(renderer, bodyFont, state, stats, panelX + 16, cursorY, panelWidth - 32, lineHeight);

    cursorY = panelY + panelHeight - (lineHeight * 3) - 24;
    if (state.showTelemetry)
    {
        renderTelemetry(renderer, bodyFont, framePerf, stats, panelX + 16, cursorY, panelWidth - 32, lineHeight);
    }

    if (!state.footer.empty())
    {
        cursorY = panelY + panelHeight - (lineHeight * 2) - 12;
        bodyFont.drawText(renderer, state.footer, panelX + 16, cursorY, &stats, SDL_Color{180, 200, 220, 255});
    }
    if (!state.help.empty())
    {
        cursorY = panelY + panelHeight - lineHeight - 8;
        bodyFont.drawText(renderer, state.help, panelX + 16, cursorY, &stats, SDL_Color{150, 170, 200, 255});
    }

    if (!state.toast.empty())
    {
        const int toastWidth = panelWidth - 32;
        SDL_Rect toastRect{panelX + 16, panelY + panelHeight - (lineHeight * 4) - 32, toastWidth, lineHeight + 12};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 30, 30, 30, 200);
        countedRenderFillRect(renderer, &toastRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        bodyFont.drawText(renderer,
                          state.toast,
                          toastRect.x + 8,
                          toastRect.y + 6,
                          &stats,
                          toastColor());
    }
}

void DebugOverlayView::renderCategories(SDL_Renderer *renderer,
                                        const TextRenderer &font,
                                        const DisplayState &state,
                                        RenderStats &stats,
                                        int x,
                                        int y,
                                        int width) const
{
    int cursorX = x;
    for (const DisplayCategory &category : state.categories)
    {
        const SDL_Color color = headerColor(category.active);
        font.drawText(renderer, category.name, cursorX, y, &stats, color);
        cursorX += font.measureText(category.name) + 16;
        if (cursorX > x + width)
        {
            break;
        }
    }
}

void DebugOverlayView::renderEntries(SDL_Renderer *renderer,
                                     const TextRenderer &font,
                                     const DisplayState &state,
                                     RenderStats &stats,
                                     int x,
                                     int y,
                                     int width,
                                     int lineHeight) const
{
    (void)width;
    int cursorY = y;
    for (const DisplayEntry &entry : state.entries)
    {
        const SDL_Color color = entryColor(entry.selected, entry.command);
        std::string line = entry.label;
        if (!entry.value.empty())
        {
            line += "  ";
            line += entry.value;
        }
        font.drawText(renderer, line, x, cursorY, &stats, color);
        cursorY += lineHeight + 4;
        if (cursorY > y + (lineHeight + 4) * 12)
        {
            break;
        }
    }
}

void DebugOverlayView::renderTelemetry(SDL_Renderer *renderer,
                                       const TextRenderer &font,
                                       const FramePerf &perf,
                                       RenderStats &stats,
                                       int panelX,
                                       int panelY,
                                       int panelWidth,
                                       int lineHeight) const
{
    (void)panelWidth;
    std::string line1 = "FPS: ";
    line1 += std::to_string(static_cast<int>(std::round(perf.fps)));
    font.drawText(renderer, line1, panelX, panelY, &stats, SDL_Color{180, 200, 255, 255});

    std::ostringstream oss;
    oss.setf(std::ios::fixed, std::ios::floatfield);
    oss.precision(2);
    oss << "Update " << perf.msUpdate << "ms  Render " << perf.msRender << "ms  HUD " << perf.msHud << "ms";
    font.drawText(renderer, oss.str(), panelX, panelY + lineHeight + 2, &stats, SDL_Color{160, 180, 230, 255});

    std::string line3 = "Entities: ";
    line3 += std::to_string(perf.entities);
    if (perf.budgetExceeded)
    {
        line3 += "  OVER " + perf.budgetStage + " (" + std::to_string(static_cast<int>(std::round(perf.budgetSampleMs))) + "ms)";
    }
    font.drawText(renderer, line3, panelX, panelY + (lineHeight + 2) * 2, &stats, SDL_Color{160, 200, 200, 255});
}

} // namespace debug
