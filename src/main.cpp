#include "app/AppLaunch.h"
#include "app/GameApplication.h"
#include "config/AppConfigLoader.h"
#include "game/CampaignState.h"
#include "scenes/BattleScene.h"
#include "scenes/CampScene.h"
#include "scenes/EditorScene.h"
#include "scenes/SceneStack.h"
#include "scenes/TitleScene.h"

#include <memory>

#ifndef KUSOZAKO_SKIP_APP_MAIN
int main(int argc, char **argv)
{
    AppLaunchOptions options = parseAppLaunchOptions(argc, argv);

    auto configLoader = std::make_shared<AppConfigLoader>(options.configRoot);
    GameApplication app(std::move(configLoader));
    if (options.telemetryDir)
    {
        app.setTelemetryOutputDirectory(*options.telemetryDir);
    }
    auto campaign = loadCampaignState(options.savePath);
    if (options.editorMode)
    {
        app.sceneStack().push(std::make_unique<EditorScene>());
    }
    else
    {
        app.sceneStack().push(std::make_unique<TitleScene>(std::move(campaign)));
    }
    return app.run();
}
#endif
