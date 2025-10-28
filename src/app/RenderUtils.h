#pragma once

#include <SDL.h>

#include "core/Vec2.h"

struct RenderStats
{
    int drawCalls = 0;
};

inline void countedRenderClear(SDL_Renderer *renderer, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderClear(renderer);
}

inline void countedRenderCopy(SDL_Renderer *renderer, SDL_Texture *texture, const SDL_Rect *src, const SDL_Rect *dst,
                              RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderCopy(renderer, texture, src, dst);
}

inline void countedRenderFillRect(SDL_Renderer *renderer, const SDL_Rect *rect, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderFillRect(renderer, rect);
}

inline void countedRenderFillRectF(SDL_Renderer *renderer, const SDL_FRect *rect, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderFillRectF(renderer, rect);
}

inline void countedRenderDrawRect(SDL_Renderer *renderer, const SDL_Rect *rect, RenderStats &stats)
{
    ++stats.drawCalls;
    SDL_RenderDrawRect(renderer, rect);
}

inline void drawFilledCircle(SDL_Renderer *renderer, const Vec2 &pos, float radius, RenderStats &stats)
{
    ++stats.drawCalls;
    const int r = static_cast<int>(radius);
    const int cx = static_cast<int>(pos.x);
    const int cy = static_cast<int>(pos.y);
    for (int dy = -r; dy <= r; ++dy)
    {
        for (int dx = -r; dx <= r; ++dx)
        {
            if (dx * dx + dy * dy <= r * r)
            {
                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    }
}

