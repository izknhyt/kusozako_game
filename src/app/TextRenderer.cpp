#include "app/TextRenderer.h"

#include "app/RenderUtils.h"

#include <SDL_ttf.h>

#include <utility>

TextRenderer::TextRenderer() = default;

TextRenderer::~TextRenderer()
{
    unload();
}

TextRenderer::TextRenderer(TextRenderer &&other) noexcept
{
    *this = std::move(other);
}

TextRenderer &TextRenderer::operator=(TextRenderer &&other) noexcept
{
    if (this != &other)
    {
        unload();
        m_assetManager = other.m_assetManager;
        m_font = std::move(other.m_font);
        m_pointSize = other.m_pointSize;
        m_lineHeight = other.m_lineHeight;
        other.m_assetManager = nullptr;
        other.m_pointSize = 0;
        other.m_lineHeight = 0;
    }
    return *this;
}

bool TextRenderer::load(AssetManager &assets, const std::string &path, int pointSize)
{
    unload();
    m_assetManager = &assets;
    m_pointSize = pointSize;
    m_font = assets.acquireFont(path, pointSize);
    if (!m_font.get())
    {
        m_assetManager = nullptr;
        m_pointSize = 0;
        return false;
    }

    if (TTF_Font *font = m_font.getRaw())
    {
        m_lineHeight = TTF_FontLineSkip(font);
        if (m_lineHeight <= 0)
        {
            m_lineHeight = TTF_FontHeight(font);
        }
    }
    else
    {
        m_lineHeight = 0;
    }

    return true;
}

void TextRenderer::unload()
{
    m_font.reset();
    m_assetManager = nullptr;
    m_pointSize = 0;
    m_lineHeight = 0;
}

bool TextRenderer::isLoaded() const
{
    return static_cast<bool>(m_font.get());
}

int TextRenderer::getLineHeight() const
{
    if (m_lineHeight > 0)
    {
        return m_lineHeight;
    }
    return m_pointSize > 0 ? m_pointSize : 0;
}

int TextRenderer::measureText(const std::string &text) const
{
    if (!isLoaded() || text.empty())
    {
        return 0;
    }
    int width = 0;
    int height = 0;
    if (TTF_SizeUTF8(m_font.getRaw(), text.c_str(), &width, &height) == 0)
    {
        return width;
    }
    return 0;
}

void TextRenderer::drawText(SDL_Renderer *renderer,
                            const std::string &text,
                            int x,
                            int y,
                            RenderStats *stats,
                            SDL_Color color) const
{
    if (!renderer || !isLoaded() || text.empty())
    {
        return;
    }

    TTF_Font *font = m_font.getRaw();
    if (!font)
    {
        return;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    if (!surface)
    {
        return;
    }
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (!texture)
    {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect dest{x, y, surface->w, surface->h};
    if (stats)
    {
        countedRenderCopy(renderer, texture, nullptr, &dest, *stats);
    }
    else
    {
        SDL_RenderCopy(renderer, texture, nullptr, &dest);
    }

    SDL_DestroyTexture(texture);
    SDL_FreeSurface(surface);
}
