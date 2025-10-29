#pragma once

#include "app/UiPresenter.h"
#include "input/ActionBuffer.h"

#include <cstddef>
#include <vector>

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
        struct InputDiagnosticsState
        {
            struct Event
            {
                ActionId id = ActionId::CommanderMoveX;
                float value = 0.0f;
                bool pressed = false;
                bool released = false;
                bool hasPointer = false;
                int pointerX = 0;
                int pointerY = 0;
                bool pointerPressed = false;
                bool pointerReleased = false;
            };

            struct Pointer
            {
                bool hasPosition = false;
                int x = 0;
                int y = 0;
                bool left = false;
                bool right = false;
                bool middle = false;
            };

            std::size_t bufferedFrames = 0;
            std::size_t bufferCapacity = 0;
            std::size_t configuredBufferFrames = 0;
            double bufferExpiryMs = 0.0;
            bool hasLatestFrame = false;
            std::uint64_t latestSequence = 0;
            double latestDeviceTimestampMs = 0.0;
            bool hasPointerState = false;
            Pointer pointerState;
            std::vector<Event> latestEvents;
        };

        const InputDiagnosticsState *inputDiagnostics = nullptr;
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
    static const char *actionDisplayName(ActionId id);
    static char indicatorFromBool(bool value);
    static std::string formatMilliseconds(double ms, int precision = 1);

    Dependencies m_dependencies{};
};

