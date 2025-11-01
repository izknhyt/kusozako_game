#pragma once

#include <cstdint>

using Uint8 = std::uint8_t;
using Uint16 = std::uint16_t;
using Uint32 = std::uint32_t;

struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;
struct SDL_FRect;
struct SDL_Color;
struct SDL_Surface;
struct SDL_version;
struct SDL_RWops;

using SDL_bool = int;

#ifndef SDL_RenderClear
inline int SDL_RenderClear(SDL_Renderer *)
{
    return 0;
}
#endif

#ifndef SDL_RenderCopy
inline int SDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *)
{
    return 0;
}
#endif

#ifndef SDL_RenderFillRect
inline int SDL_RenderFillRect(SDL_Renderer *, const SDL_Rect *)
{
    return 0;
}
#endif

#ifndef SDL_RenderFillRectF
inline int SDL_RenderFillRectF(SDL_Renderer *, const SDL_FRect *)
{
    return 0;
}
#endif

#ifndef SDL_RenderDrawRect
inline int SDL_RenderDrawRect(SDL_Renderer *, const SDL_Rect *)
{
    return 0;
}
#endif

#ifndef SDL_RenderDrawPoint
inline int SDL_RenderDrawPoint(SDL_Renderer *, int, int)
{
    return 0;
}
#endif

#ifndef SDL_SetRenderDrawBlendMode
inline int SDL_SetRenderDrawBlendMode(SDL_Renderer *, int)
{
    return 0;
}
#endif

#ifndef SDL_SetRenderDrawColor
inline int SDL_SetRenderDrawColor(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8)
{
    return 0;
}
#endif
