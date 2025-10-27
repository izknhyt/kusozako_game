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
    const GameConfig &game = result.config.game;
    if (game.jobs_path.empty() || game.morale_path.empty() || game.formations_path.empty())
    {
        std::cerr << "Config missing referenced asset paths in game config.\n";
        return 1;
    }
    if (game.spawn_weights_path.empty() || game.jobSpawn.weightsAssetPath.empty())
    {
        std::cerr << "Spawn weighting configuration not resolved.\n";
        return 1;
    }
    if (game.performance.updateMs <= 0.0f || game.performance.renderMs <= 0.0f ||
        game.performance.inputMs <= 0.0f || game.performance.hudMs <= 0.0f)
    {
        std::cerr << "Performance budgets must be positive.\n";
        return 1;
    }
    return 0;
}
