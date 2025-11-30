#include "app/AppLaunch.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "game/CampaignState.h"

namespace
{

bool consumeArg(int &index, int argc, char **argv, std::string &out)
{
    if (index + 1 >= argc)
    {
        return false;
    }
    out = argv[index + 1];
    index += 1;
    return true;
}

} // namespace

AppLaunchOptions parseAppLaunchOptions(int argc, char **argv)
{
    AppLaunchOptions options;
    options.configRoot = std::filesystem::absolute("config");
    options.savePath = std::filesystem::absolute(std::filesystem::path("save") / "campaign_state.json");

    constexpr std::string_view kTelemetryPrefix{"--telemetry-dir="};
    constexpr std::string_view kConfigPrefix{"--config="};
    constexpr std::string_view kSavePrefix{"--save="};

    for (int i = 1; i < argc; ++i)
    {
        std::string_view arg(argv[i]);
        if (arg == "--telemetry-dir")
        {
            std::string pathStr;
            if (consumeArg(i, argc, argv, pathStr))
            {
                options.telemetryDir = std::filesystem::path(pathStr);
            }
        }
        else if (arg.substr(0, kTelemetryPrefix.size()) == kTelemetryPrefix)
        {
            options.telemetryDir = std::filesystem::path(std::string(arg.substr(kTelemetryPrefix.size())));
        }
        else if (arg == "--editor")
        {
            options.editorMode = true;
        }
        else if (arg == "--config")
        {
            std::string pathStr;
            if (consumeArg(i, argc, argv, pathStr))
            {
                options.configRoot = std::filesystem::absolute(pathStr);
            }
        }
        else if (arg.substr(0, kConfigPrefix.size()) == kConfigPrefix)
        {
            options.configRoot = std::filesystem::absolute(std::string(arg.substr(kConfigPrefix.size())));
        }
        else if (arg == "--save")
        {
            std::string pathStr;
            if (consumeArg(i, argc, argv, pathStr))
            {
                options.savePath = std::filesystem::absolute(pathStr);
            }
        }
        else if (arg.substr(0, kSavePrefix.size()) == kSavePrefix)
        {
            options.savePath = std::filesystem::absolute(std::string(arg.substr(kSavePrefix.size())));
        }
    }

    return options;
}

std::shared_ptr<CampaignState> loadCampaignState(const std::filesystem::path &savePath)
{
    auto campaign = std::make_shared<CampaignState>();
    campaign->setSavePath(savePath);
    if (!campaign->loadFromDisk())
    {
        std::cerr << "[save] failed to load campaign state from " << savePath << '\n';
    }
    return campaign;
}
