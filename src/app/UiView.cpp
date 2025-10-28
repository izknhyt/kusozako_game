#include "app/UiView.h"

UiView::UiView() = default;

UiView::UiView(const Dependencies &dependencies)
    : m_dependencies(dependencies)
{
}

void UiView::setDependencies(const Dependencies &dependencies)
{
    m_dependencies = dependencies;
}

void UiView::setRenderer(SDL_Renderer *renderer)
{
    m_dependencies.renderer = renderer;
}

void UiView::setHudFont(const TextRenderer *font)
{
    m_dependencies.hudFont = font;
}

void UiView::setDebugFont(const TextRenderer *font)
{
    m_dependencies.debugFont = font;
}

void UiView::setScreenSize(int width, int height)
{
    m_dependencies.screenWidth = width;
    m_dependencies.screenHeight = height;
}

const UiView::Dependencies &UiView::dependencies() const noexcept
{
    return m_dependencies;
}

void UiView::draw(const DrawContext &context) const
{
    (void)context;
}

