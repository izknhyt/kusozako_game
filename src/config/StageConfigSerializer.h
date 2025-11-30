#pragma once

#include <filesystem>

#include "assets/FileIO.h"
#include "config/AppConfig.h"
#include "json/JsonUtils.h"

json::JsonValue stageConfigToJson(const StageConfig &config);
SaveFileResult saveStageConfig(const StageConfig &config, const std::filesystem::path &path,
                               const SaveFileOptions &options = {});
