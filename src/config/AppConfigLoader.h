#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/AppConfig.h"

namespace json
{
struct JsonValue;
}

class AssetManager;

struct AppConfigLoadError
{
    std::string file;
    std::string message;
};

struct AppConfigLoadResult
{
    AppConfig config;
    bool success = false;
    std::vector<AppConfigLoadError> errors;
};

class AppConfigLoader
{
  public:
    explicit AppConfigLoader(std::filesystem::path configRoot);

    const std::filesystem::path &configRoot() const { return m_configRoot; }

    AppConfigLoadResult load(AssetManager &assets);

    std::vector<std::string> detectChangedFiles();

  private:
    struct TrackedFile
    {
        std::filesystem::path path;
        std::filesystem::file_time_type timestamp{};
    };

    AppConfig loadFallback() const;

    void trackFile(const std::string &logicalName, const std::filesystem::path &path);

    std::filesystem::path m_configRoot;
    std::unordered_map<std::string, TrackedFile> m_trackedFiles;
};

