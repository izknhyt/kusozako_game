#include "json/JsonUtils.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

bool assertTrue(bool condition, const std::string &message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

std::optional<json::JsonValue> loadJsonFile(const std::filesystem::path &path)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to open " << path << '\n';
        return std::nullopt;
    }
    std::string contents((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return json::parseJson(contents);
}

bool roundTripFile(const std::filesystem::path &path, bool pretty)
{
    auto original = loadJsonFile(path);
    if (!original)
    {
        std::cerr << "Parse failed for " << path << '\n';
        return false;
    }

    std::string serialized = json::serializeJson(*original, pretty);
    auto reparsed = json::parseJson(serialized);
    if (!reparsed)
    {
        std::cerr << "Re-parse failed for " << path << '\n';
        return false;
    }
    if (!json::jsonEquals(*original, *reparsed))
    {
        std::cerr << "Round-trip mismatch for " << path << '\n';
        return false;
    }
    return true;
}

} // namespace

int main()
{
    bool success = true;
    const std::filesystem::path root = std::filesystem::path(PROJECT_SOURCE_DIR);
    const std::vector<std::filesystem::path> targets{
        root / "config" / "app.json",
        root / "assets" / "game.json",
        root / "assets" / "stage1_config.json",
        root / "assets" / "entities.json",
    };

    for (const auto &path : targets)
    {
        success &= assertTrue(roundTripFile(path, true), "Pretty round-trip failed");
        success &= assertTrue(roundTripFile(path, false), "Compact round-trip failed");
    }
    return success ? 0 : 1;
}
