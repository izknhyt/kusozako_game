#pragma once

#include "assets/AssetManager.h"

#include <SDL.h>

#include <string>

struct RenderStats;

class TextRenderer
{
  public:
    TextRenderer();
    ~TextRenderer();

    TextRenderer(const TextRenderer &) = delete;
    TextRenderer &operator=(const TextRenderer &) = delete;
    TextRenderer(TextRenderer &&other) noexcept;
    TextRenderer &operator=(TextRenderer &&other) noexcept;

    bool load(AssetManager &assets, const std::string &path, int pointSize);
    void unload();

    bool isLoaded() const;
    int getLineHeight() const;

    int measureText(const std::string &text) const;

    void drawText(SDL_Renderer *renderer,
                  const std::string &text,
                  int x,
                  int y,
                  RenderStats *stats,
                  SDL_Color color = SDL_Color{255, 255, 255, 255}) const;

  private:
    AssetManager *m_assetManager = nullptr;
    AssetManager::FontReference m_font;
    int m_pointSize = 0;
    int m_lineHeight = 0;
};
