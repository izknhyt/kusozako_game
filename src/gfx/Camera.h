#pragma once

#include <SDL.h>

#include "app/RenderUtils.h"
#include "assets/LevelAssets.h"
#include "core/Vec2.h"

struct Camera
{
    Vec2 position{0.0f, 0.0f};
    float speed = 320.0f;
};

Vec2 worldToScreen(const Vec2 &world, const Camera &camera);
Vec2 screenToWorld(int screenX, int screenY, const Camera &camera);

void drawTileLayer(SDL_Renderer *renderer, const TileMap &map, const std::vector<int> &tiles, const Camera &camera,
                   int screenW, int screenH, RenderStats &stats);
