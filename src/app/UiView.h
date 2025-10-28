#pragma once

#include "app/UiPresenter.h"

#include <cstddef>

struct SDL_Renderer;

class TextRenderer;
struct FramePerf;
struct RenderStats;

namespace world
{
class LegacySimulation;
}

class UiView
{
  public:
    struct Dependencies
    {
        SDL_Renderer *renderer = nullptr;
        const TextRenderer *hudFont = nullptr;
        const TextRenderer *debugFont = nullptr;
        int screenWidth = 0;
        int screenHeight = 0;
    };

    struct DrawContext
    {
        const world::LegacySimulation *simulation = nullptr;
        const FormationHudStatus *formationHud = nullptr;
        const MoraleHudStatus *moraleHud = nullptr;
        const JobHudStatus *jobHud = nullptr;
        FramePerf *framePerf = nullptr;
        RenderStats *renderStats = nullptr;
        bool showDebugHud = false;
        double performanceFrequency = 0.0;
        double *hudTimeMs = nullptr;
    };

    UiView();
    explicit UiView(const Dependencies &dependencies);

    void setDependencies(const Dependencies &dependencies);
    void setRenderer(SDL_Renderer *renderer);
    void setHudFont(const TextRenderer *font);
    void setDebugFont(const TextRenderer *font);
    void setScreenSize(int width, int height);

    const Dependencies &dependencies() const noexcept;

    void render(const DrawContext &context) const;

  private:
    static int measureWithFallback(const TextRenderer &renderer, const std::string &text, int approxHeight);
    static SDL_Color moraleColorForState(MoraleState state);
    static std::string moraleDisplayName(MoraleState state);
    static SDL_Color jobRingColor(UnitJob job);
    static const char *jobDisplayName(UnitJob job);
    static const char *jobSpecialLabel(UnitJob job);
    static std::string formatSecondsShort(float seconds);
    static std::string formatTimer(float seconds);
    static const char *stanceDisplayName(ArmyStance stance);

    Dependencies m_dependencies{};
};

