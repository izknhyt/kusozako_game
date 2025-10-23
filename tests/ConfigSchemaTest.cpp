#include "config/AppConfigLoader.h"

#include <filesystem>
#include <iostream>

#include "assets/AssetManager.h"

int main()
{
    std::filesystem::path root = std::filesystem::path(PROJECT_SOURCE_DIR);
    std::filesystem::path configRoot = root / "config";
    std::filesystem::path assetsRoot = root / "assets";

    AssetManager assets;
    assets.setAssetRoot(assetsRoot.string());

    AppConfigLoader loader(configRoot);
    auto result = loader.load(assets);
    if (!result.success)
    {
        std::cerr << "Config schema validation failed:\n";
        for (const auto &error : result.errors)
        {
            std::cerr << "  " << error.file << ": " << error.message << '\n';
        }
        return 1;
    }
    return 0;
}
