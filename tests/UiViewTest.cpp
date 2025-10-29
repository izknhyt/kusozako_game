#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using Uint8 = unsigned char;

enum SDL_BlendMode
{
    SDL_BLENDMODE_NONE = 0,
    SDL_BLENDMODE_BLEND = 1,
};

struct SDL_Color
{
    Uint8 r = 0;
    Uint8 g = 0;
    Uint8 b = 0;
    Uint8 a = 0;
};

struct SDL_Rect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct SDL_Renderer
{
};

struct SDL_Texture
{
};

struct SDL_FRect
{
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct RenderStats;

struct DrawCall
{
    std::string text;
    int x = 0;
    int y = 0;
    SDL_Color color{255, 255, 255, 255};
};

struct RecordingRenderer
{
    std::vector<DrawCall> draws;
};

struct FramePerf
{
    float fps = 0.0f;
    float msUpdate = 0.0f;
    float msRender = 0.0f;
    float msInput = 0.0f;
    float msHud = 0.0f;
    int drawCalls = 0;
    int entities = 0;
    bool budgetExceeded = false;
    std::string budgetStage;
    float budgetSampleMs = 0.0f;
    float budgetTargetMs = 0.0f;
};

class TextRenderer
{
  public:
    explicit TextRenderer(int lineHeight = 18)
        : m_lineHeight(lineHeight)
    {
    }

    void setLoaded(bool loaded) { m_loaded = loaded; }

    bool isLoaded() const { return m_loaded; }

    void setLineHeight(int lineHeight) { m_lineHeight = lineHeight; }

    int getLineHeight() const { return m_lineHeight; }

    void setDefaultCharWidth(int width) { m_charWidth = width; }

    void setMeasure(const std::string &text, int width) { m_measures[text] = width; }

    int measureText(const std::string &text) const
    {
        auto it = m_measures.find(text);
        if (it != m_measures.end())
        {
            return it->second;
        }
        return static_cast<int>(text.size()) * m_charWidth;
    }

    void drawText(SDL_Renderer *renderer, const std::string &text, int x, int y, RenderStats * /*stats*/,
                  SDL_Color color = SDL_Color{255, 255, 255, 255}) const
    {
        if (!renderer)
        {
            return;
        }
        auto *recording = reinterpret_cast<RecordingRenderer *>(renderer);
        recording->draws.push_back(DrawCall{text, x, y, color});
    }

  private:
    bool m_loaded = true;
    int m_lineHeight = 18;
    int m_charWidth = 8;
    std::unordered_map<std::string, int> m_measures;
};

int FakeSDL_SetRenderDrawBlendMode(SDL_Renderer *, SDL_BlendMode);
int FakeSDL_SetRenderDrawColor(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8);
int FakeSDL_RenderFillRect(SDL_Renderer *, const SDL_Rect *);
int FakeSDL_RenderDrawRect(SDL_Renderer *, const SDL_Rect *);
int FakeSDL_RenderDrawPoint(SDL_Renderer *, int, int);
int FakeSDL_RenderClear(SDL_Renderer *);
int FakeSDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);
int FakeSDL_RenderFillRectF(SDL_Renderer *, const SDL_FRect *);

namespace world
{
inline std::string normalizeTelemetry(const std::string &text)
{
    return text;
}
} // namespace world

#define SDL_h_

#define SDL_SetRenderDrawBlendMode FakeSDL_SetRenderDrawBlendMode
#define SDL_SetRenderDrawColor FakeSDL_SetRenderDrawColor
#define SDL_RenderFillRect FakeSDL_RenderFillRect
#define SDL_RenderDrawRect FakeSDL_RenderDrawRect
#define SDL_RenderDrawPoint FakeSDL_RenderDrawPoint
#define SDL_RenderClear FakeSDL_RenderClear
#define SDL_RenderCopy FakeSDL_RenderCopy
#define SDL_RenderFillRectF FakeSDL_RenderFillRectF

#include "app/UiView.cpp"

#undef SDL_SetRenderDrawBlendMode
#undef SDL_SetRenderDrawColor
#undef SDL_RenderFillRect
#undef SDL_RenderDrawRect
#undef SDL_RenderDrawPoint
#undef SDL_RenderClear
#undef SDL_RenderCopy
#undef SDL_RenderFillRectF

int FakeSDL_SetRenderDrawBlendMode(SDL_Renderer *, SDL_BlendMode)
{
    return 0;
}

int FakeSDL_SetRenderDrawColor(SDL_Renderer *, Uint8, Uint8, Uint8, Uint8)
{
    return 0;
}

int FakeSDL_RenderFillRect(SDL_Renderer *, const SDL_Rect *)
{
    return 0;
}

int FakeSDL_RenderDrawRect(SDL_Renderer *, const SDL_Rect *)
{
    return 0;
}

int FakeSDL_RenderDrawPoint(SDL_Renderer *, int, int)
{
    return 0;
}

int FakeSDL_RenderClear(SDL_Renderer *)
{
    return 0;
}

int FakeSDL_RenderCopy(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *)
{
    return 0;
}

int FakeSDL_RenderFillRectF(SDL_Renderer *, const SDL_FRect *)
{
    return 0;
}

namespace
{

bool assertTrue(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

const DrawCall *findDraw(const RecordingRenderer &renderer, const std::string &text)
{
    auto it = std::find_if(renderer.draws.begin(), renderer.draws.end(), [&](const DrawCall &call) {
        return call.text == text;
    });
    if (it == renderer.draws.end())
    {
        return nullptr;
    }
    return &*it;
}

bool assertDrawPresent(const RecordingRenderer &renderer, const std::string &text, const char *message)
{
    const DrawCall *call = findDraw(renderer, text);
    if (!call)
    {
        std::cerr << "Available draws:\n";
        for (const auto &entry : renderer.draws)
        {
            std::cerr << "  " << entry.text << "\n";
        }
    }
    return assertTrue(call != nullptr, message);
}

bool assertDrawNotPresent(const RecordingRenderer &renderer, const std::string &text, const char *message)
{
    const DrawCall *call = findDraw(renderer, text);
    if (call)
    {
        std::cerr << "Unexpected draw present: " << text << "\n";
        std::cerr << "Available draws:\n";
        for (const auto &entry : renderer.draws)
        {
            std::cerr << "  " << entry.text << "\n";
        }
    }
    return assertTrue(call == nullptr, message);
}

bool assertDrawColor(const RecordingRenderer &renderer, const std::string &text, const SDL_Color &expected,
                     const char *message)
{
    const DrawCall *call = findDraw(renderer, text);
    if (!assertTrue(call != nullptr, message))
    {
        return false;
    }
    if (!assertTrue(call->color.r == expected.r && call->color.g == expected.g && call->color.b == expected.b
                         && call->color.a == expected.a,
                     message))
    {
        return false;
    }
    return true;
}

RecordingRenderer renderView(UiView &view, UiView::DrawContext &context)
{
    RecordingRenderer rendererMemory;
    view.setRenderer(reinterpret_cast<SDL_Renderer *>(&rendererMemory));
    view.render(context);
    return rendererMemory;
}

UiView makeView(TextRenderer &hudFont, TextRenderer &debugFont, int width = 1280, int height = 720)
{
    UiView::Dependencies deps;
    deps.renderer = nullptr;
    deps.hudFont = &hudFont;
    deps.debugFont = &debugFont;
    deps.screenWidth = width;
    deps.screenHeight = height;
    return UiView(deps);
}

} // namespace

bool testFormationCountdownBanner()
{
    bool success = true;

    TextRenderer hudFont(20);
    TextRenderer debugFont(16);
    UiView view = makeView(hudFont, debugFont);

    world::LegacySimulation sim{};
    sim.config.base_hp = 100;
    sim.baseHp = 80.0f;

    RenderStats stats{};
    UiView::DrawContext context{};
    context.simulation = &sim;
    context.renderStats = &stats;

    FormationHudStatus formation{};
    formation.label = "Alignment";
    formation.countdown.active = true;
    formation.countdown.label = "Stack";
    formation.countdown.secondsRemaining = 3.36f;
    formation.countdown.progress = 0.5f;
    formation.countdown.followers = 12;
    context.formationHud = &formation;

    RecordingRenderer renderer = renderView(view, context);

    success &= assertDrawPresent(renderer, "Stack 3.4s • 12 followers",
                                 "Formation countdown banner should include label, time, and followers");

    return success;
}

bool testMoraleSummaryBullets()
{
    bool success = true;

    TextRenderer hudFont(20);
    TextRenderer debugFont(16);
    UiView view = makeView(hudFont, debugFont);

    world::LegacySimulation sim{};
    sim.config.base_hp = 100;
    sim.baseHp = 80.0f;

    RenderStats stats{};
    UiView::DrawContext context{};
    context.simulation = &sim;
    context.renderStats = &stats;

    MoraleHudStatus morale{};
    morale.summary.commanderState = MoraleState::Panic;
    morale.summary.leaderDownTimer = 1.25f;
    morale.summary.commanderBarrierTimer = 2.65f;
    morale.summary.panicCount = 3;
    morale.summary.mesomesoCount = 4;
    context.moraleHud = &morale;

    RecordingRenderer renderer = renderView(view, context);

    success &= assertDrawPresent(renderer, "Commander: Panic",
                                 "Commander morale summary should capitalize state name");
    success &= assertDrawPresent(renderer, "Leader down: 1.2s",
                                 "Leader down timer should display with one decimal place");
    success &= assertDrawPresent(renderer, "Barrier: 2.7s",
                                 "Barrier timer should render with formatted seconds");
    success &= assertDrawPresent(renderer, "Panic: 3", "Panic counter should be shown");
    success &= assertDrawPresent(renderer, "Mesomeso: 4", "Mesomeso counter should be shown");

    return success;
}

bool testJobListFormatting()
{
    bool success = true;

    TextRenderer hudFont(20);
    TextRenderer debugFont(16);
    UiView view = makeView(hudFont, debugFont);

    world::LegacySimulation sim{};
    sim.config.base_hp = 100;
    sim.baseHp = 80.0f;

    RenderStats stats{};
    UiView::DrawContext context{};
    context.simulation = &sim;
    context.renderStats = &stats;

    JobHudStatus jobs{};
    JobHudEntryStatus warrior{};
    warrior.job = UnitJob::Warrior;
    warrior.total = 5;
    warrior.ready = 2;
    warrior.maxCooldown = 1.5f;
    warrior.maxEndlag = 0.75f;
    warrior.specialActive = true;
    warrior.specialTimer = 2.35f;
    jobs.jobs.push_back(warrior);

    JobHudEntryStatus archer{};
    archer.job = UnitJob::Archer;
    archer.total = 4;
    archer.ready = 1;
    archer.maxCooldown = 0.0f;
    archer.maxEndlag = 0.0f;
    archer.specialActive = false;
    jobs.jobs.push_back(archer);

    JobHudSkillStatus skill{};
    skill.label = "Fervor";
    skill.cooldownRemaining = 4.65f;
    skill.activeTimer = 0.85f;
    skill.toggled = true;
    jobs.skills.push_back(skill);

    context.jobHud = &jobs;

    RecordingRenderer renderer = renderView(view, context);

    success &= assertDrawPresent(renderer, "Warrior: 2/5 CD 1.5s EL 0.8s Stumble 2.3s",
                                 "Warrior entry should include counts, cooldown, endlag, and special timer");
    success &= assertDrawPresent(renderer, "Archer: 1/4",
                                 "Archer entry should include ready and total counts");
    success &= assertDrawPresent(renderer, "Fervor [ON] CD 4.7s Active 0.9s",
                                 "Skill entry should include toggle and timers");

    return success;
}

bool testWarningAndResultOverlays()
{
    bool success = true;

    TextRenderer hudFont(20);
    TextRenderer debugFont(16);
    UiView view = makeView(hudFont, debugFont);

    world::LegacySimulation sim{};
    sim.config.base_hp = 100;
    sim.baseHp = 80.0f;
    sim.renderQueue.performanceWarningText = "Events lost 12";
    sim.renderQueue.performanceWarningTimer = 1.0f;
    sim.renderQueue.spawnWarningText = "Spawn backlog";
    sim.renderQueue.spawnWarningTimer = 1.0f;
    sim.renderQueue.telemetryText = "Elite incoming";
    sim.renderQueue.telemetryTimer = 1.0f;
    sim.hud.resultText = "Mission Complete";
    sim.hud.resultTimer = 1.0f;
    sim.restartCooldown = 0.5f;

    RenderStats stats{};
    UiView::DrawContext context{};
    context.simulation = &sim;
    context.renderStats = &stats;

    RecordingRenderer renderer = renderView(view, context);

    success &= assertDrawColor(renderer, "Events lost 12", SDL_Color{255, 220, 220, 255},
                               "Performance warning should render with light red text");
    success &= assertDrawColor(renderer, "Spawn backlog", SDL_Color{255, 240, 210, 255},
                               "Spawn warning should render with warm highlight color");
    success &= assertDrawPresent(renderer, "Elite incoming", "Telemetry text should be drawn when timer is active");
    success &= assertDrawPresent(renderer, "Mission Complete", "Result overlay should appear while timer is active");
    success &= assertDrawNotPresent(renderer, "Rキーで再挑戦",
                                    "Restart hint should stay hidden while cooldown is active");

    sim.restartCooldown = 0.0f;

    RecordingRenderer cooldownReadyRenderer = renderView(view, context);
    success &= assertDrawPresent(cooldownReadyRenderer, "Rキーで再挑戦",
                                 "Restart hint should appear once restart cooldown finishes");

    return success;
}

bool testInputDiagnosticsPanel()
{
    bool success = true;

    TextRenderer hudFont(20);
    TextRenderer debugFont(16);
    UiView view = makeView(hudFont, debugFont);

    world::LegacySimulation sim{};
    sim.config.base_hp = 100;
    sim.baseHp = 80.0f;

    RenderStats stats{};
    UiView::DrawContext context{};
    context.simulation = &sim;
    context.renderStats = &stats;
    context.showDebugHud = true;

    UiView::DrawContext::InputDiagnosticsState diagnostics{};
    diagnostics.bufferedFrames = 3;
    diagnostics.bufferCapacity = 4;
    diagnostics.configuredBufferFrames = 5;
    diagnostics.bufferExpiryMs = 120.0;
    diagnostics.hasLatestFrame = true;
    diagnostics.latestSequence = 42;
    diagnostics.latestDeviceTimestampMs = 1234.5;
    diagnostics.hasPointerState = true;
    diagnostics.pointerState.hasPosition = true;
    diagnostics.pointerState.x = 640;
    diagnostics.pointerState.y = 360;
    diagnostics.pointerState.left = true;
    UiView::DrawContext::InputDiagnosticsState::Event event{};
    event.id = ActionId::ToggleDebugHud;
    event.pressed = true;
    event.hasPointer = true;
    event.pointerX = 640;
    event.pointerY = 360;
    event.pointerPressed = true;
    diagnostics.latestEvents.push_back(event);
    context.inputDiagnostics = &diagnostics;

    RecordingRenderer renderer = renderView(view, context);

    success &= assertDrawPresent(renderer, "Input buffer 3/4 (cfg 5, exp 120.0ms)",
                                 "Input diagnostics should show buffer usage and expiry");
    success &= assertDrawPresent(renderer, "Latest frame #42 @1234.5ms",
                                 "Latest frame info should include sequence and timestamp");
    success &= assertDrawPresent(renderer, "Pointer 640,360 L1 R0 M0",
                                 "Pointer state should list position and button flags");
    success &= assertDrawPresent(renderer, "- ToggleDebugHud pressed ptr 640,360 down",
                                 "Latest event should include action name and pointer details");

    return success;
}

int main()
{
    bool success = true;

    success &= testFormationCountdownBanner();
    success &= testMoraleSummaryBullets();
    success &= testJobListFormatting();
    success &= testWarningAndResultOverlays();
    success &= testInputDiagnosticsPanel();

    return success ? 0 : 1;
}

