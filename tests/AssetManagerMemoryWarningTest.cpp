#include "assets/AssetManager.h"
#include "services/ServiceLocator.h"
#include "telemetry/TelemetrySink.h"
#include "app/UiPresenter.h"

#include <SDL.h>

#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace
{

struct FakeTexture
{
    Uint32 format = SDL_PIXELFORMAT_RGBA8888;
    int access = SDL_TEXTUREACCESS_STATIC;
    int width = 0;
    int height = 0;
};

class MockTelemetrySink : public TelemetrySink
{
  public:
    void recordEvent(std::string_view eventName, const Payload &payload) override
    {
        events.push_back(std::string(eventName));
        payloads.push_back(payload);
    }

    std::vector<std::string> events;
    std::vector<Payload> payloads;
};

bool assertTrue(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}

AssetManager::TexturePtr makeFakeTexture(const FakeTexture &info)
{
    auto *texture = new FakeTexture(info);
    return AssetManager::TexturePtr(reinterpret_cast<SDL_Texture *>(texture), [](SDL_Texture *ptr) {
        delete reinterpret_cast<FakeTexture *>(ptr);
    });
}

} // namespace

int main()
{
    bool success = true;

    ServiceLocator &locator = ServiceLocator::instance();
    locator.clear();

    auto telemetry = std::make_shared<MockTelemetrySink>();
    locator.registerService<TelemetrySink>(telemetry);

    auto uiPresenter = std::make_shared<UiPresenter>();
    locator.registerService<UiPresenter>(uiPresenter);

    AssetManager manager;
    manager.setRenderer(reinterpret_cast<SDL_Renderer *>(0x1));
    manager.setTextureMemoryWarningThreshold(5ull * 1024ull * 1024ull);

    std::unordered_map<std::string, FakeTexture> textures{
        {"tex_a", {SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 1024, 1024}},
        {"tex_b", {SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 1024, 512}},
        {"tex_c", {SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STATIC, 512, 512}},
    };

    manager.setTextureLoadCallback([&textures](SDL_Renderer *, const std::string &path) -> AssetManager::TexturePtr {
        auto it = textures.find(path);
        if (it == textures.end())
        {
            return {};
        }
        return makeFakeTexture(it->second);
    });

    manager.setTextureQueryCallback([](SDL_Texture *texture, Uint32 *format, int *access, int *w, int *h) {
        const auto *fake = reinterpret_cast<FakeTexture *>(texture);
        if (!fake)
        {
            return -1;
        }
        if (format)
        {
            *format = fake->format;
        }
        if (access)
        {
            *access = fake->access;
        }
        if (w)
        {
            *w = fake->width;
        }
        if (h)
        {
            *h = fake->height;
        }
        return 0;
    });

    const std::uintmax_t texABytes = 1024u * 1024u * 4u;
    const std::uintmax_t texBBytes = 1024u * 512u * 4u;
    const std::uintmax_t texCBytes = 512u * 512u * 4u;

    {
        auto texA = manager.acquireTexture("tex_a");
        success &= assertTrue(static_cast<bool>(texA), "tex_a should load successfully");
        success &= assertTrue(manager.totalTextureBytes() == texABytes,
                              "Total bytes should match tex_a after first load");
        success &= assertTrue(telemetry->events.empty(), "No warning should be emitted below the threshold");

        auto texB = manager.acquireTexture("tex_b");
        success &= assertTrue(static_cast<bool>(texB), "tex_b should load successfully");
        const std::uintmax_t expectedAfterB = texABytes + texBBytes;
        success &= assertTrue(manager.totalTextureBytes() == expectedAfterB,
                              "Total bytes should include tex_a and tex_b");
        success &= assertTrue(telemetry->events.size() == 1,
                              "Warning should trigger exactly once when crossing the threshold");
        success &= assertTrue(!uiPresenter->lastWarningMessage().empty(),
                              "HUD warning message should be populated after crossing the threshold");

        auto texC = manager.acquireTexture("tex_c");
        (void)texC;
        success &= assertTrue(telemetry->events.size() == 1,
                              "Additional loads above the threshold should not emit extra warnings");

        texA.reset();
        const std::uintmax_t expectedAfterRelease = texBBytes + texCBytes;
        success &= assertTrue(manager.totalTextureBytes() == expectedAfterRelease,
                              "Releasing tex_a should reduce the total byte count");

        texA = manager.acquireTexture("tex_a");
        success &= assertTrue(telemetry->events.size() == 2,
                              "Crossing the threshold again after dropping below should emit a new warning");
    }

    locator.clear();

    return success ? 0 : 1;
}
