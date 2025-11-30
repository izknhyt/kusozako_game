#include "assets/LevelAssets.h"

#include <SDL.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "assets/AssetManager.h"
#include "json/JsonUtils.h"

using json::JsonValue;

namespace
{

std::optional<JsonValue> loadJsonDocument(AssetManager &assets, const std::string &path,
                                          AssetManager::AssetLoadStatus *outStatus = nullptr)
{
    auto document = assets.acquireJson(path);
    AssetManager::AssetLoadStatus status = document.status();
    if (outStatus)
    {
        *outStatus = status;
    }
    if (!document.get())
    {
        return std::nullopt;
    }
    JsonValue value = *document.get();
    return value;
}

std::string trimCopy(const std::string &text)
{
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])))
    {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])))
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

struct TagResult
{
    std::string tag;
    std::size_t start = 0;
    std::size_t end = 0;
};

std::optional<TagResult> findTag(const std::string &xml, const std::string &name, std::size_t begin = 0)
{
    const std::string needle = "<" + name;
    const std::size_t pos = xml.find(needle, begin);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    const std::size_t end = xml.find('>', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    TagResult result;
    result.tag = xml.substr(pos, end - pos + 1);
    result.start = pos;
    result.end = end;
    return result;
}

std::optional<int> parseXmlIntAttribute(const std::string &tag, const std::string &attr)
{
    const std::string needle = attr + "=\"";
    std::size_t pos = tag.find(needle);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    pos += needle.size();
    const std::size_t end = tag.find('"', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    try
    {
        return std::stoi(tag.substr(pos, end - pos));
    }
    catch (...)
    {
        return std::nullopt;
    }
}

std::optional<std::string> parseXmlStringAttribute(const std::string &tag, const std::string &attr)
{
    const std::string needle = attr + "=\"";
    std::size_t pos = tag.find(needle);
    if (pos == std::string::npos)
    {
        return std::nullopt;
    }
    pos += needle.size();
    const std::size_t end = tag.find('"', pos);
    if (end == std::string::npos)
    {
        return std::nullopt;
    }
    return tag.substr(pos, end - pos);
}

bool parseCsvLayer(const std::string &csv, int expectedCount, std::vector<int> &out)
{
    out.clear();
    if (expectedCount > 0)
    {
        out.reserve(static_cast<std::size_t>(expectedCount));
    }
    std::stringstream ss(csv);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        const std::string trimmed = trimCopy(token);
        if (trimmed.empty())
        {
            continue;
        }
        try
        {
            out.push_back(std::stoi(trimmed));
        }
        catch (...)
        {
            return false;
        }
    }
    if (expectedCount > 0)
    {
        if (static_cast<int>(out.size()) < expectedCount)
        {
            out.resize(static_cast<std::size_t>(expectedCount), 0);
        }
        else if (static_cast<int>(out.size()) > expectedCount)
        {
            out.resize(static_cast<std::size_t>(expectedCount));
        }
    }
    return true;
}

} // namespace

bool loadAtlas(AssetManager &assets, const std::string &path, Atlas &out)
{
    out = Atlas{};

    AssetManager::AssetLoadStatus status{};
    auto document = loadJsonDocument(assets, path, &status);
    if (!document)
    {
        return false;
    }

    const JsonValue &root = *document;
    std::string imagePathStr;
    if (const JsonValue *meta = json::getObjectField(root, "meta"))
    {
        imagePathStr = json::getString(*meta, "image", "");
    }
    std::filesystem::path atlasDir = std::filesystem::path(path).parent_path();
    std::filesystem::path imagePath;
    if (imagePathStr.empty())
    {
        imagePath = std::filesystem::path(path).replace_extension(".png");
    }
    else
    {
        std::filesystem::path candidate(imagePathStr);
        imagePath = candidate.is_absolute() ? candidate : (atlasDir / candidate);
    }
    out.texture = assets.acquireTexture(imagePath.lexically_normal().string());
    if (!out.texture.get())
    {
        return false;
    }

    if (const JsonValue *frames = json::getObjectField(root, "frames"))
    {
        if (frames->type == JsonValue::Type::Object)
        {
            for (const auto &entry : frames->object)
            {
                const JsonValue *frame = &entry.second;
                const JsonValue *xywh = json::getObjectField(*frame, "xywh");
                if (!xywh || xywh->type != JsonValue::Type::Array || xywh->array.size() < 4)
                {
                    continue;
                }
                SDL_Rect rect{};
                rect.x = static_cast<int>(xywh->array[0].number);
                rect.y = static_cast<int>(xywh->array[1].number);
                rect.w = static_cast<int>(xywh->array[2].number);
                rect.h = static_cast<int>(xywh->array[3].number);
                out.frames[entry.first] = rect;
            }
        }
    }

    return true;
}

bool loadTileMap(AssetManager &assets, const std::string &path, TileMap &out)
{
    out = TileMap{};

    const std::string resolvedPath = assets.resolvePath(path);
    std::ifstream file(resolvedPath);
    if (!file)
    {
        return false;
    }
    const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto mapTag = findTag(xml, "map");
    if (!mapTag)
    {
        return false;
    }
    out.width = parseXmlIntAttribute(mapTag->tag, "width").value_or(0);
    out.height = parseXmlIntAttribute(mapTag->tag, "height").value_or(0);
    out.tileWidth = parseXmlIntAttribute(mapTag->tag, "tilewidth").value_or(0);
    out.tileHeight = parseXmlIntAttribute(mapTag->tag, "tileheight").value_or(0);

    auto tilesetTag = findTag(xml, "tileset", mapTag->end);
    if (!tilesetTag)
    {
        return false;
    }
    out.tilesetColumns = parseXmlIntAttribute(tilesetTag->tag, "columns").value_or(0);

    const std::size_t tilesetClose = xml.find("</tileset", tilesetTag->end);
    auto imageTag = findTag(xml, "image", tilesetTag->end);
    std::string imageSource;
    if (imageTag && (tilesetClose == std::string::npos || imageTag->start < tilesetClose))
    {
        imageSource = parseXmlStringAttribute(imageTag->tag, "source").value_or("");
    }
    std::filesystem::path mapDir = std::filesystem::path(path).parent_path();
    std::filesystem::path imagePath;
    if (imageSource.empty())
    {
        imagePath = mapDir / "tileset.png";
    }
    else
    {
        std::filesystem::path candidate(imageSource);
        imagePath = candidate.is_absolute() ? candidate : (mapDir / candidate);
    }
    out.tileset = assets.acquireTexture(imagePath.lexically_normal().string());

    if (out.tilesetColumns <= 0 && out.tileset.get() && out.tileWidth > 0)
    {
        int texW = 0;
        if (SDL_QueryTexture(out.tileset.getRaw(), nullptr, nullptr, &texW, nullptr) == 0 && texW > 0)
        {
            out.tilesetColumns = std::max(1, texW / out.tileWidth);
        }
    }
    if (out.tilesetColumns <= 0)
    {
        out.tilesetColumns = 1;
    }

    const int expectedTiles = (out.width > 0 && out.height > 0) ? out.width * out.height : 0;
    std::size_t searchPos = tilesetTag->end;
    while (searchPos < xml.size())
    {
        auto layerTag = findTag(xml, "layer", searchPos);
        if (!layerTag)
        {
            break;
        }
        searchPos = layerTag->end + 1;

        const std::size_t layerClose = xml.find("</layer", layerTag->end);
        if (layerClose == std::string::npos)
        {
            break;
        }

        auto dataTag = findTag(xml, "data", layerTag->end);
        if (!dataTag || dataTag->start > layerClose)
        {
            searchPos = layerClose;
            continue;
        }
        const std::size_t dataClose = xml.find("</data>", dataTag->end);
        if (dataClose == std::string::npos || dataClose > layerClose)
        {
            searchPos = layerClose;
            continue;
        }
        const std::string dataContent = xml.substr(dataTag->end + 1, dataClose - dataTag->end - 1);
        std::vector<int> tiles;
        if (!parseCsvLayer(dataContent, expectedTiles, tiles))
        {
            searchPos = layerClose;
            continue;
        }

        auto layerName = parseXmlStringAttribute(layerTag->tag, "name");
        if (layerName)
        {
            std::string lower = *layerName;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (lower == "floor")
            {
                out.floor = std::move(tiles);
            }
            else if (lower == "block")
            {
                out.block = std::move(tiles);
            }
            else if (lower == "deco" || lower == "decor" || lower == "decoration")
            {
                out.deco = std::move(tiles);
            }
        }

        searchPos = layerClose;
    }

    const bool dimensionsValid = out.width > 0 && out.height > 0 && out.tileWidth > 0 && out.tileHeight > 0;
    return dimensionsValid && out.tileset.get();
}
