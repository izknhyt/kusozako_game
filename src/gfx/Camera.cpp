#include "gfx/Camera.h"

#include <algorithm>
#include <cmath>

Vec2 worldToScreen(const Vec2 &world, const Camera &camera)
{
    return {world.x - camera.position.x, world.y - camera.position.y};
}

Vec2 screenToWorld(int screenX, int screenY, const Camera &camera)
{
    return {static_cast<float>(screenX) + camera.position.x, static_cast<float>(screenY) + camera.position.y};
}

void drawTileLayer(SDL_Renderer *renderer, const TileMap &map, const std::vector<int> &tiles, const Camera &camera,
                   int screenW, int screenH, RenderStats &stats)
{
    if (!map.tileset.get())
    {
        return;
    }
    const int totalTiles = static_cast<int>(tiles.size());
    if (totalTiles == 0)
    {
        return;
    }
    for (int y = 0; y < map.height; ++y)
    {
        for (int x = 0; x < map.width; ++x)
        {
            const int index = y * map.width + x;
            if (index < 0 || index >= totalTiles)
            {
                continue;
            }
            const int gid = tiles[index];
            if (gid <= 0)
            {
                continue;
            }
            const int tileIndex = gid - 1;
            const int srcX = (tileIndex % map.tilesetColumns) * map.tileWidth;
            const int srcY = (tileIndex / map.tilesetColumns) * map.tileHeight;
            SDL_Rect src{srcX, srcY, map.tileWidth, map.tileHeight};
            SDL_Rect dst{
                static_cast<int>(x * map.tileWidth - camera.position.x),
                static_cast<int>(y * map.tileHeight - camera.position.y),
                map.tileWidth,
                map.tileHeight};
            if (dst.x + dst.w < 0 || dst.y + dst.h < 0 || dst.x > screenW || dst.y > screenH)
            {
                continue;
            }
            countedRenderCopy(renderer, map.tileset.getRaw(), &src, &dst, stats);
        }
    }
}
