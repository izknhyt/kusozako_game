#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct CampaignState;

struct AppLaunchOptions
{
    bool editorMode = false;
    std::optional<std::filesystem::path> telemetryDir;
    std::filesystem::path configRoot;
    std::filesystem::path savePath;
};

AppLaunchOptions parseAppLaunchOptions(int argc, char **argv);

std::shared_ptr<CampaignState> loadCampaignState(const std::filesystem::path &savePath);
