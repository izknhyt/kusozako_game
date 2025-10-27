#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include <SDL.h>
#include <SDL_ttf.h>

namespace json
{
struct JsonValue;
}

class AssetManager
{
  public:
    enum class AssetType
    {
        Texture,
        Font,
        Json
    };

    struct AssetHandle
    {
        AssetType type = AssetType::Texture;
        std::string key;

        bool valid() const { return !key.empty(); }
    };

    struct AssetLoadStatus
    {
        bool ok = false;
        bool usedFallback = false;
        std::string message;
    };

    using TexturePtr = std::shared_ptr<SDL_Texture>;
    using FontPtr = std::shared_ptr<TTF_Font>;
    using JsonPtr = std::shared_ptr<json::JsonValue>;

    template <typename Ptr>
    class AssetReference
    {
      public:
        AssetReference() = default;
        AssetReference(const AssetReference &) = delete;
        AssetReference &operator=(const AssetReference &) = delete;

        AssetReference(AssetReference &&other) noexcept
        {
            *this = std::move(other);
        }

        AssetReference &operator=(AssetReference &&other) noexcept
        {
            if (this != &other)
            {
                reset();
                m_manager = other.m_manager;
                m_handle = std::move(other.m_handle);
                m_resource = std::move(other.m_resource);
                m_status = std::move(other.m_status);
                other.m_manager = nullptr;
                other.m_handle = {};
                other.m_status = {};
            }
            return *this;
        }

        ~AssetReference()
        {
            reset();
        }

        explicit operator bool() const { return static_cast<bool>(m_resource); }

        Ptr get() const { return m_resource; }

        auto getRaw() const -> typename Ptr::element_type *
        {
            return m_resource ? m_resource.get() : nullptr;
        }

        const AssetLoadStatus &status() const { return m_status; }

        const AssetHandle &handle() const { return m_handle; }

        void reset()
        {
            if (m_resource)
            {
                m_resource.reset();
            }
            if (m_manager && m_handle.valid())
            {
                m_manager->release(m_handle);
            }
            m_manager = nullptr;
            m_handle = {};
            m_status = {};
        }

      private:
        friend class AssetManager;

        AssetReference(AssetManager *manager, AssetHandle handle, Ptr resource, AssetLoadStatus status)
            : m_manager(manager), m_handle(std::move(handle)), m_resource(std::move(resource)),
              m_status(std::move(status))
        {
        }

        AssetManager *m_manager = nullptr;
        AssetHandle m_handle{};
        Ptr m_resource{};
        AssetLoadStatus m_status{};
    };

    using TextureReference = AssetReference<TexturePtr>;
    using FontReference = AssetReference<FontPtr>;
    using JsonReference = AssetReference<JsonPtr>;

    AssetManager();
    ~AssetManager();

    void setRenderer(SDL_Renderer *renderer);
    void setAssetRoot(const std::string &rootPath);
    std::string resolvePath(const std::string &path) const;

    void setFallbackTexture(TexturePtr texture);
    void setFallbackFont(FontPtr font);
    void setFallbackJson(JsonPtr json);

    AssetLoadStatus requestLoadTexture(const std::string &path);
    AssetLoadStatus requestLoadFont(const std::string &path, int pointSize);
    AssetLoadStatus requestLoadJson(const std::string &path);

    TextureReference acquireTexture(const std::string &path);
    FontReference acquireFont(const std::string &path, int pointSize);
    JsonReference acquireJson(const std::string &path);

    void release(const AssetHandle &handle);
    void clear();

    using TextureLoadFunc = std::function<TexturePtr(SDL_Renderer *, const std::string &)>;
    using TextureQueryFunc = std::function<int(SDL_Texture *, Uint32 *, int *, int *, int *)>;

    void setTextureLoadCallback(TextureLoadFunc loader);
    void setTextureQueryCallback(TextureQueryFunc query);

    void setTextureMemoryWarningThreshold(std::uintmax_t bytes);
    std::uintmax_t textureMemoryWarningThreshold() const { return m_textureWarningThresholdBytes; }
    std::uintmax_t totalTextureBytes() const { return m_totalTextureBytes; }

  private:
    struct AssetRequest
    {
        AssetType type;
        std::string path;
        int variant = 0;
    };

    struct AssetRecord
    {
        AssetType type = AssetType::Texture;
        std::string resolvedPath;
        std::variant<std::monostate, TexturePtr, FontPtr, JsonPtr> resource;
        int refCount = 0;
        int width = 0;
        int height = 0;
        std::uintmax_t byteSize = 0;
    };

    AssetLoadStatus requestLoad(const AssetRequest &request);

    TextureReference acquireTextureInternal(const AssetRequest &request);
    FontReference acquireFontInternal(const AssetRequest &request);
    JsonReference acquireJsonInternal(const AssetRequest &request);

    AssetRecord *loadOrGet(const AssetRequest &request, AssetLoadStatus &status, std::string &outKey);

    static std::string typePrefix(AssetType type);
    std::string makeKey(const AssetRequest &request, std::string &resolvedPath) const;

    SDL_Renderer *m_renderer = nullptr;
    std::string m_assetRoot;
    std::unordered_map<std::string, AssetRecord> m_assets;

    TexturePtr m_fallbackTexture;
    FontPtr m_fallbackFont;
    JsonPtr m_fallbackJson;

    TextureLoadFunc m_textureLoader;
    TextureQueryFunc m_textureQuery;
    std::uintmax_t m_textureWarningThresholdBytes = 150ull * 1024ull * 1024ull;
    std::uintmax_t m_totalTextureBytes = 0;
    bool m_textureWarningActive = false;

    void evaluateTextureMemoryWarning();
    void emitTextureMemoryWarning();
};

