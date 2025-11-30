#pragma once

#include <SDL.h>

#include "app/RenderUtils.h"
#include "app/TextRenderer.h"
#include "assets/LevelAssets.h"
#include "gfx/Camera.h"
#include "app/UiPresenter.h"
#include "world/LegacySimulation.h"

void renderWorld(SDL_Renderer *renderer,
                 const world::LegacySimulation &sim,
                 const FormationHudStatus *formationHud,
                 const MoraleHudStatus *moraleHud,
                 const JobHudStatus *jobHud,
                 const Camera &camera,
                 const TextRenderer &font,
                 const TextRenderer &debugFont,
                 const TileMap &map,
                 const Atlas &atlas,
                 int screenW,
                 int screenH,
                 RenderStats &stats);
