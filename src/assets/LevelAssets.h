#pragma once

#include <SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "assets/AssetManager.h"

struct TileMap
{
    int width = 0;
    int height = 0;
    int tileWidth = 0;
    int tileHeight = 0;
    int tilesetColumns = 0;
    AssetManager::TextureReference tileset;
    std::vector<int> floor;
    std::vector<int> block;
    std::vector<int> deco;
};

struct Atlas
{
    AssetManager::TextureReference texture;
    std::unordered_map<std::string, SDL_Rect> frames;

    const SDL_Rect *getFrame(const std::string &name) const
    {
        auto it = frames.find(name);
        return it != frames.end() ? &it->second : nullptr;
    }
};

bool loadAtlas(AssetManager &assets, const std::string &path, Atlas &out);
bool loadTileMap(AssetManager &assets, const std::string &path, TileMap &out);
