#include "assets/AssetManager.h"

#include <SDL_image.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "json/JsonUtils.h"

namespace
{
struct TextureDeleter
{
    void operator()(SDL_Texture *texture) const
    {
        if (texture)
        {
            SDL_DestroyTexture(texture);
        }
    }
};

struct FontDeleter
{
    void operator()(TTF_Font *font) const
    {
        if (font)
        {
            TTF_CloseFont(font);
        }
    }
};

AssetManager::TexturePtr makeTexturePtr(SDL_Texture *texture)
{
    return AssetManager::TexturePtr(texture, TextureDeleter{});
}

AssetManager::FontPtr makeFontPtr(TTF_Font *font)
{
    return AssetManager::FontPtr(font, FontDeleter{});
}

} // namespace

AssetManager::AssetManager() = default;

AssetManager::~AssetManager()
{
    clear();
}

void AssetManager::setRenderer(SDL_Renderer *renderer)
{
    m_renderer = renderer;
}

void AssetManager::setAssetRoot(const std::string &rootPath)
{
    namespace fs = std::filesystem;
    fs::path root(rootPath);
    if (!root.empty())
    {
        m_assetRoot = root.lexically_normal().string();
    }
    else
    {
        m_assetRoot.clear();
    }
}

std::string AssetManager::resolvePath(const std::string &path) const
{
    namespace fs = std::filesystem;
    fs::path input(path);
    if (input.is_absolute())
    {
        return input.lexically_normal().string();
    }
    if (m_assetRoot.empty())
    {
        return input.lexically_normal().string();
    }
    fs::path root(m_assetRoot);
    if (!input.empty())
    {
        auto first = *input.begin();
        if (!root.empty() && first == root.filename())
        {
            fs::path trimmed;
            auto it = input.begin();
            ++it;
            for (; it != input.end(); ++it)
            {
                trimmed /= *it;
            }
            return (root / trimmed).lexically_normal().string();
        }
    }
    return (root / input).lexically_normal().string();
}

void AssetManager::setFallbackTexture(TexturePtr texture)
{
    m_fallbackTexture = std::move(texture);
}

void AssetManager::setFallbackFont(FontPtr font)
{
    m_fallbackFont = std::move(font);
}

void AssetManager::setFallbackJson(JsonPtr json)
{
    m_fallbackJson = std::move(json);
}

AssetManager::AssetLoadStatus AssetManager::requestLoadTexture(const std::string &path)
{
    return requestLoad({AssetType::Texture, path, 0});
}

AssetManager::AssetLoadStatus AssetManager::requestLoadFont(const std::string &path, int pointSize)
{
    return requestLoad({AssetType::Font, path, pointSize});
}

AssetManager::AssetLoadStatus AssetManager::requestLoadJson(const std::string &path)
{
    return requestLoad({AssetType::Json, path, 0});
}

AssetManager::TextureReference AssetManager::acquireTexture(const std::string &path)
{
    return acquireTextureInternal({AssetType::Texture, path, 0});
}

AssetManager::FontReference AssetManager::acquireFont(const std::string &path, int pointSize)
{
    return acquireFontInternal({AssetType::Font, path, pointSize});
}

AssetManager::JsonReference AssetManager::acquireJson(const std::string &path)
{
    return acquireJsonInternal({AssetType::Json, path, 0});
}

void AssetManager::release(const AssetHandle &handle)
{
    if (!handle.valid())
    {
        return;
    }
    auto it = m_assets.find(handle.key);
    if (it == m_assets.end())
    {
        return;
    }
    AssetRecord &record = it->second;
    if (record.refCount > 0)
    {
        --record.refCount;
    }
    if (record.refCount <= 0)
    {
        m_assets.erase(it);
    }
}

void AssetManager::clear()
{
    m_assets.clear();
    m_fallbackTexture.reset();
    m_fallbackFont.reset();
    m_fallbackJson.reset();
    m_renderer = nullptr;
    m_assetRoot.clear();
}

AssetManager::AssetLoadStatus AssetManager::requestLoad(const AssetRequest &request)
{
    AssetLoadStatus status;
    std::string key;
    loadOrGet(request, status, key);
    return status;
}

AssetManager::TextureReference AssetManager::acquireTextureInternal(const AssetRequest &request)
{
    AssetLoadStatus status;
    std::string key;
    AssetRecord *record = loadOrGet(request, status, key);
    TexturePtr resource;
    AssetHandle handle{request.type, key};
    if (record && std::holds_alternative<TexturePtr>(record->resource))
    {
        resource = std::get<TexturePtr>(record->resource);
        ++record->refCount;
    }
    else
    {
        status.usedFallback = static_cast<bool>(m_fallbackTexture);
        status.ok = false;
        if (!m_fallbackTexture)
        {
            status.message = status.message.empty() ? "Texture load failed" : status.message;
        }
        resource = m_fallbackTexture;
        handle.key.clear();
    }
    return TextureReference(this, std::move(handle), std::move(resource), status);
}

AssetManager::FontReference AssetManager::acquireFontInternal(const AssetRequest &request)
{
    AssetLoadStatus status;
    std::string key;
    AssetRecord *record = loadOrGet(request, status, key);
    FontPtr resource;
    AssetHandle handle{request.type, key};
    if (record && std::holds_alternative<FontPtr>(record->resource))
    {
        resource = std::get<FontPtr>(record->resource);
        ++record->refCount;
    }
    else
    {
        status.usedFallback = static_cast<bool>(m_fallbackFont);
        status.ok = false;
        if (!m_fallbackFont)
        {
            status.message = status.message.empty() ? "Font load failed" : status.message;
        }
        resource = m_fallbackFont;
        handle.key.clear();
    }
    return FontReference(this, std::move(handle), std::move(resource), status);
}

AssetManager::JsonReference AssetManager::acquireJsonInternal(const AssetRequest &request)
{
    AssetLoadStatus status;
    std::string key;
    AssetRecord *record = loadOrGet(request, status, key);
    JsonPtr resource;
    AssetHandle handle{request.type, key};
    if (record && std::holds_alternative<JsonPtr>(record->resource))
    {
        resource = std::get<JsonPtr>(record->resource);
        ++record->refCount;
    }
    else
    {
        status.usedFallback = static_cast<bool>(m_fallbackJson);
        status.ok = false;
        if (!m_fallbackJson)
        {
            status.message = status.message.empty() ? "JSON load failed" : status.message;
        }
        resource = m_fallbackJson;
        handle.key.clear();
    }
    return JsonReference(this, std::move(handle), std::move(resource), status);
}

AssetManager::AssetRecord *AssetManager::loadOrGet(const AssetRequest &request, AssetLoadStatus &status, std::string &outKey)
{
    std::string resolvedPath;
    outKey = makeKey(request, resolvedPath);
    auto existing = m_assets.find(outKey);
    if (existing != m_assets.end())
    {
        status.ok = true;
        status.usedFallback = false;
        return &existing->second;
    }

    AssetRecord record;
    record.type = request.type;
    record.resolvedPath = resolvedPath;

    switch (request.type)
    {
    case AssetType::Texture:
    {
        if (!m_renderer)
        {
            status.message = "Renderer not available";
            break;
        }
        SDL_Texture *texture = IMG_LoadTexture(m_renderer, record.resolvedPath.c_str());
        if (!texture)
        {
            status.message = std::string("Failed to load texture: ") + record.resolvedPath + " -> " + IMG_GetError();
            break;
        }
        record.resource = makeTexturePtr(texture);
        status.ok = true;
        break;
    }
    case AssetType::Font:
    {
        TTF_Font *font = TTF_OpenFont(record.resolvedPath.c_str(), request.variant);
        if (!font)
        {
            status.message = std::string("Failed to load font: ") + record.resolvedPath + " -> " + TTF_GetError();
            break;
        }
        record.resource = makeFontPtr(font);
        status.ok = true;
        break;
    }
    case AssetType::Json:
    {
        std::ifstream file(record.resolvedPath);
        if (!file.is_open())
        {
            status.message = std::string("Failed to open JSON: ") + record.resolvedPath;
            break;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        auto parsed = json::parseJson(buffer.str());
        if (!parsed)
        {
            status.message = std::string("Failed to parse JSON: ") + record.resolvedPath;
            break;
        }
        record.resource = std::make_shared<json::JsonValue>(std::move(*parsed));
        status.ok = true;
        break;
    }
    }

    if (!status.ok)
    {
        return nullptr;
    }

    auto inserted = m_assets.emplace(outKey, std::move(record));
    if (inserted.second)
    {
        return &inserted.first->second;
    }
    return nullptr;
}

std::string AssetManager::typePrefix(AssetType type)
{
    switch (type)
    {
    case AssetType::Texture: return "texture";
    case AssetType::Font: return "font";
    case AssetType::Json: return "json";
    }
    return "unknown";
}

std::string AssetManager::makeKey(const AssetRequest &request, std::string &resolvedPath) const
{
    resolvedPath = resolvePath(request.path);
    std::string key = typePrefix(request.type);
    key.append("|");
    key.append(resolvedPath);
    if (request.type == AssetType::Font)
    {
        key.append("#");
        key.append(std::to_string(request.variant));
    }
    return key;
}

