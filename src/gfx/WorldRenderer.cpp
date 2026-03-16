#include "gfx/WorldRenderer.h"
#include "world/FormationUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

extern bool g_showChibiLabels;

using namespace world;

void renderWorld(SDL_Renderer *renderer, const LegacySimulation &sim, const FormationHudStatus *formationHud,
                 const MoraleHudStatus *moraleHud, const JobHudStatus *jobHud, const Camera &camera,
                 const TextRenderer &font, const TextRenderer &debugFont, const TileMap &map,
                 const Atlas &atlas, int screenW, int screenH, RenderStats &stats)
{
    (void)formationHud;
    (void)jobHud;

    const LegacySimulation::RenderQueue &queue = sim.renderQueue;
    const bool skipActors = queue.skipActors;
    const int lineHeight = std::max(font.getLineHeight(), 18);
    const int debugLineHeight = std::max(debugFont.isLoaded() ? debugFont.getLineHeight() : lineHeight, 14);
    const bool minimalHud = false;
    const bool showWorldLabels = true;
    const bool showMoraleMarkers = true;

    auto enemyColor = [](EnemyArchetype type) -> SDL_Color {
        switch (type)
        {
        case EnemyArchetype::Boss: return SDL_Color{230, 40, 40, 255};
        case EnemyArchetype::Golem: return SDL_Color{245, 210, 50, 255};
        case EnemyArchetype::Wallbreaker: return SDL_Color{200, 140, 90, 255};
        case EnemyArchetype::Goblin: return SDL_Color{90, 210, 90, 255};
        case EnemyArchetype::Magician: return SDL_Color{170, 130, 230, 255};
        case EnemyArchetype::Bat: return SDL_Color{90, 190, 210, 255};
        case EnemyArchetype::Toritori: return SDL_Color{230, 150, 80, 255};
        case EnemyArchetype::Slime:
        default: return SDL_Color{90, 130, 230, 255};
        }
    };
    auto enemyLabel = [](EnemyArchetype type) -> const char * {
        switch (type)
        {
        case EnemyArchetype::Boss: return "DRG";
        case EnemyArchetype::Golem: return "GLM";
        case EnemyArchetype::Wallbreaker: return "WB";
        case EnemyArchetype::Goblin: return "GB";
        case EnemyArchetype::Magician: return "MG";
        case EnemyArchetype::Bat: return "BT";
        case EnemyArchetype::Toritori: return "TT";
        case EnemyArchetype::Slime:
        default: return "SL";
        }
    };

    auto measureWorldText = [](const TextRenderer &renderer, const std::string &text, int approxHeight) {
        const int measured = renderer.measureText(text);
        if (measured > 0)
        {
            return measured;
        }
        const int approxWidth = std::max(approxHeight / 2, 8);
        return static_cast<int>(text.size()) * approxWidth;
    };

    auto moraleIconColor = [](MoraleState state) -> SDL_Color {
        switch (state)
        {
        case MoraleState::LeaderDown: return SDL_Color{255, 160, 60, 220};
        case MoraleState::Panic: return SDL_Color{235, 70, 85, 230};
        case MoraleState::Mesomeso: return SDL_Color{130, 120, 255, 230};
        case MoraleState::Recovering: return SDL_Color{110, 200, 255, 220};
        case MoraleState::Shielded: return SDL_Color{80, 220, 180, 230};
        case MoraleState::Stable:
        default: return SDL_Color{255, 255, 255, 0};
        }
    };

    auto unitRingColor = [](UnitJob job) -> SDL_Color {
        switch (job)
        {
        case UnitJob::Warrior: return SDL_Color{220, 80, 80, 255};
        case UnitJob::Archer: return SDL_Color{80, 200, 120, 255};
        case UnitJob::Shield: return SDL_Color{70, 130, 230, 255};
        }
        return SDL_Color{200, 200, 200, 255};
    };
    auto drawNameLabel = [&](const LegacySimulation::RenderQueue::AllySprite &ally, const Vec2 &screenPos) {
        if (!ally.named || ally.name.empty())
        {
            return;
        }
        const TextRenderer &nameFont = debugFont.isLoaded() ? debugFont : font;
        const int textW = nameFont.measureText(ally.name);
        const int padX = 4;
        const int padY = 2;
        SDL_Rect bg{
            static_cast<int>(std::round(screenPos.x)) - textW / 2 - padX,
            static_cast<int>(std::round(screenPos.y - ally.radius)) - (nameFont.getLineHeight() + padY * 2) - 4,
            textW + padX * 2,
            nameFont.getLineHeight() + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 20, 20, 32, 200);
        countedRenderFillRect(renderer, &bg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        nameFont.drawText(renderer, ally.name, bg.x + padX, bg.y + padY, &stats, SDL_Color{255, 255, 255, 255});
    };

    auto drawMoraleIcon = [&](const Vec2 &worldPos, float radius, MoraleState state) {
        if (!showMoraleMarkers)
        {
            return;
        }
        if (state == MoraleState::Stable)
        {
            return;
        }
        SDL_Color color = moraleIconColor(state);
        Vec2 screen = worldToScreen(worldPos, camera);
        const float iconRadius = std::max(4.0f, radius * 0.5f);
        Vec2 iconCenter{screen.x, screen.y - radius - iconRadius - 4.0f};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        drawFilledCircle(renderer, iconCenter, iconRadius, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    };

    auto braveryLabel = [](int axis) -> const char * {
        static const char *labels[] = {"びびり", "ひかえめ", "ふつう", "ゆうかん", "ちょとつ"};
        int idx = std::clamp(axis + 2, 0, 4);
        return labels[idx];
    };
    auto wisdomLabel = [](int axis) -> const char * {
        static const char *labels[] = {"おばか", "ぽやん", "ふつう", "なやみがち", "かんがえるこ"};
        int idx = std::clamp(axis + 2, 0, 4);
        return labels[idx];
    };
    auto temperamentColorForAxes = [](int bravery, int wisdom, bool panic) -> SDL_Color {
        if (panic)
        {
            return SDL_Color{235, 70, 85, 255};
        }
        const std::uint8_t base = 150;
        std::uint8_t r = static_cast<std::uint8_t>(std::clamp(base + bravery * 20, 60, 255));
        std::uint8_t b = static_cast<std::uint8_t>(std::clamp(base + wisdom * 20, 60, 255));
        return SDL_Color{r, 180, b, 255};
    };

    auto drawPanicBadge = [&](const LegacySimulation::RenderQueue::AllySprite &ally, float spriteTopY, float centerX) -> int {
        if (!debugFont.isLoaded())
        {
            return 0;
        }
        const char *badge = nullptr;
        SDL_Color color{235, 70, 85, 230};
        if (ally.panicTokkou)
        {
            badge = "とっこう";
            color = SDL_Color{255, 150, 90, 235};
        }
        else if (ally.panicCling)
        {
            badge = "すがる";
            color = SDL_Color{120, 200, 255, 230};
        }
        else if (ally.panicKaiten)
        {
            badge = "かいてん";
            color = SDL_Color{130, 200, 150, 230};
        }
        else if (ally.panicRunAround)
        {
            badge = "にげまどう";
            color = SDL_Color{200, 140, 255, 230};
        }
        else if (ally.panicActive)
        {
            badge = "Panic";
        }
        if (!badge)
        {
            return 0;
        }
        const int padX = 4;
        const int padY = 2;
        const int textWidth = measureWorldText(debugFont, badge, debugLineHeight);
        SDL_Rect badgeBg{
            static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
            static_cast<int>(std::round(spriteTopY)) - (debugLineHeight + padY * 2) - 10,
            textWidth + padX * 2,
            debugLineHeight + padY * 2};
        if (badgeBg.x < 4) badgeBg.x = 4;
        if (badgeBg.x + badgeBg.w > screenW - 4) badgeBg.x = screenW - badgeBg.w - 4;
        if (badgeBg.y < 4) badgeBg.y = 4;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        countedRenderFillRect(renderer, &badgeBg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        debugFont.drawText(renderer, badge, badgeBg.x + padX, badgeBg.y + padY, &stats, SDL_Color{255, 255, 255, 255});
        return badgeBg.h + 4;
    };

    auto drawTemperamentLabel = [&](const LegacySimulation::RenderQueue::AllySprite &ally, float spriteTopY, float centerX) {
        if (ally.named)
        {
            return;
        }
        if (!debugFont.isLoaded())
        {
            return;
        }
        std::string label = std::string("ゆ:") + braveryLabel(ally.braveryAxis) + " ち:" + wisdomLabel(ally.wisdomAxis);
        const char *branchLabel = nullptr;
        if (ally.panicTokkou)
        {
            branchLabel = "とっこう";
        }
        else if (ally.panicCling)
        {
            branchLabel = "すがる";
        }
        else if (ally.panicKaiten)
        {
            branchLabel = "かいてん";
        }
        else if (ally.panicRunAround)
        {
            branchLabel = "にげまどう";
        }
        if (!branchLabel && ally.panicTimer > 0.0f)
        {
            char timeBuf[16];
            std::snprintf(timeBuf, sizeof(timeBuf), " 残%.1fs", ally.panicTimer);
            label += timeBuf;
        }
        label += " 行動:" + std::string(chibiActionLabel(ally.action));
        if (branchLabel)
        {
            label += " [";
            label += branchLabel;
            if (ally.panicBranchTimer > 0.0f)
            {
                char timeBuf[16];
                std::snprintf(timeBuf, sizeof(timeBuf), " 残%.1fs", ally.panicBranchTimer);
                label += timeBuf;
            }
            label += "]";
        }
        else if (ally.panicActive)
        {
            label += ally.forcePanic ? " [強Panic]" : " [Panic]";
        }

        const int textWidth = measureWorldText(debugFont, label, debugLineHeight);
        const int padX = 4;
        const int padY = 2;
        const int panicOffset = drawPanicBadge(ally, spriteTopY, centerX);
        SDL_Rect bg{
            static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
            static_cast<int>(std::round(spriteTopY)) - panicOffset - (debugLineHeight + padY * 2) - 6,
            textWidth + padX * 2,
            debugLineHeight + padY * 2
        };
        if (bg.x < 4) bg.x = 4;
        if (bg.x + bg.w > screenW - 4) bg.x = screenW - bg.w - 4;
        if (bg.y < 4) bg.y = 4;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        countedRenderFillRect(renderer, &bg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        SDL_Color color = temperamentColorForAxes(ally.braveryAxis, ally.wisdomAxis, ally.panicActive);
        debugFont.drawText(renderer, label, bg.x + padX, bg.y + padY, &stats, color);
    };

    auto drawEnemyHpBar = [&](const LegacySimulation::RenderQueue::EnemySprite &enemy,
                              const Vec2 &screenPos,
                              const SDL_Rect *spriteRect) {
        if (enemy.maxHp <= 0.0f || enemy.hp <= 0.0f)
        {
            return;
        }
        const float ratio = std::clamp(enemy.hp / enemy.maxHp, 0.0f, 1.0f);
        const float baseWidth = spriteRect ? static_cast<float>(spriteRect->w) : enemy.radius * 2.0f;
        const float barWidthF = std::clamp(baseWidth * 0.9f, 18.0f, 56.0f);
        const int barWidth = std::max(1, static_cast<int>(std::round(barWidthF)));
        const int barHeight = 3;
        const float centerX = spriteRect ? (static_cast<float>(spriteRect->x) + spriteRect->w * 0.5f) : screenPos.x;
        const float baseY = spriteRect ? (static_cast<float>(spriteRect->y + spriteRect->h) + 2.0f)
                                       : (screenPos.y + enemy.radius + 2.0f);
        const int barX = static_cast<int>(std::round(centerX - barWidthF * 0.5f));
        const int barY = static_cast<int>(std::round(baseY));
        SDL_Rect bg{barX, barY, barWidth, barHeight};
        SDL_Color fill = enemyColor(enemy.type);
        const int fillWidth = std::max(1, static_cast<int>(std::round(barWidthF * ratio)));
        SDL_Rect fg{barX, barY, fillWidth, barHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
        countedRenderFillRect(renderer, &bg, stats);
        SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, 180);
        countedRenderFillRect(renderer, &fg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    };

    auto drawWorldPulse = [&](const Vec2 &worldPos, float radius, SDL_Color color, float phase, float alphaScale) {
        Vec2 screenPos = worldToScreen(worldPos, camera);
        const float pulse = 0.75f + 0.25f * static_cast<float>((std::sin(sim.simTime * phase) + 1.0f) * 0.5f);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                               static_cast<Uint8>(std::clamp(alphaScale * 0.55f, 0.0f, 255.0f)));
        drawFilledCircle(renderer, screenPos, radius * 1.35f * pulse, stats);
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b,
                               static_cast<Uint8>(std::clamp(alphaScale, 0.0f, 255.0f)));
        drawFilledCircle(renderer, screenPos, radius * 0.82f * pulse, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    };

    auto drawMinimap = [&]() -> SDL_Rect {
        SDL_Rect empty{0, 0, 0, 0};
        if (map.width <= 0 || map.height <= 0 || map.tileWidth <= 0 || map.tileHeight <= 0)
        {
            return empty;
        }
        const int panelSize = std::clamp(screenW / 8, 120, 200);
        const int margin = 12;
        const SDL_Rect panel{screenW - panelSize - margin, margin, panelSize, panelSize};
        const float worldW = static_cast<float>(map.width * map.tileWidth);
        const float worldH = static_cast<float>(map.height * map.tileHeight);
        if (worldW <= 0.0f || worldH <= 0.0f)
        {
            return empty;
        }
        const float scale = std::min(panelSize / worldW, panelSize / worldH);
        const float offsetX = static_cast<float>(panel.x) + (panelSize - worldW * scale) * 0.5f;
        const float offsetY = static_cast<float>(panel.y) + (panelSize - worldH * scale) * 0.5f;

        auto toMini = [&](const Vec2 &world) -> SDL_Point {
            SDL_Point out;
            out.x = static_cast<int>(std::round(offsetX + world.x * scale));
            out.y = static_cast<int>(std::round(offsetY + world.y * scale));
            return out;
        };

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 8, 10, 18, 210);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_Rect titleBand{panel.x, panel.y, panel.w, 22};
        SDL_SetRenderDrawColor(renderer, 24, 42, 64, 220);
        countedRenderFillRect(renderer, &titleBand, stats);
        SDL_Rect inner{panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2};
        SDL_SetRenderDrawColor(renderer, 20, 26, 38, 160);
        countedRenderDrawRect(renderer, &inner, stats);
        SDL_SetRenderDrawColor(renderer, 120, 130, 170, 210);
        SDL_RenderDrawRect(renderer, &panel);

        const StageRuntimeState &stageState = sim.stageState();

        if (stageState.enabled)
        {
            for (const auto &base : stageState.enemyBases)
            {
                SDL_Point p = toMini(base.pos);
                SDL_Color col = base.sealed ? SDL_Color{120, 120, 130, 210} : SDL_Color{230, 90, 90, 230};
                SDL_Rect dot{p.x - 2, p.y - 2, 5, 5};
                SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
                countedRenderFillRect(renderer, &dot, stats);
                SDL_SetRenderDrawColor(renderer, 10, 10, 18, 200);
                SDL_RenderDrawRect(renderer, &dot);
            }
        }

        SDL_Color allyColor{120, 220, 255, 200};
        SDL_Color commanderColor{245, 245, 210, 255};
        SDL_Color bossOutline{250, 220, 120, 230};

        // Enemies (non-boss)
        for (const auto &enemy : queue.enemies)
        {
            if (enemy.type == EnemyArchetype::Boss)
            {
                continue;
            }
            SDL_Point p = toMini(enemy.position);
            SDL_Color col = enemyColor(enemy.type);
            SDL_Rect dot{p.x - 1, p.y - 1, 3, 3};
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 200);
            countedRenderFillRect(renderer, &dot, stats);
        }

        // Allies (non-commander)
        for (const auto &ally : queue.allies)
        {
            if (ally.commander)
            {
                continue;
            }
            SDL_Point p = toMini(ally.position);
            SDL_Rect dot{p.x - 1, p.y - 1, 3, 3};
            SDL_SetRenderDrawColor(renderer, allyColor.r, allyColor.g, allyColor.b, allyColor.a);
            countedRenderFillRect(renderer, &dot, stats);
        }

        // Boss
        for (const auto &enemy : queue.enemies)
        {
            if (enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            SDL_Point p = toMini(enemy.position);
            SDL_Color col = enemyColor(enemy.type);
            SDL_Rect dot{p.x - 3, p.y - 3, 7, 7};
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, 230);
            countedRenderFillRect(renderer, &dot, stats);
            SDL_SetRenderDrawColor(renderer, bossOutline.r, bossOutline.g, bossOutline.b, bossOutline.a);
            SDL_RenderDrawRect(renderer, &dot);
        }

        // Commander
        for (const auto &ally : queue.allies)
        {
            if (!ally.commander)
            {
                continue;
            }
            SDL_Point p = toMini(ally.position);
            SDL_Rect dot{p.x - 2, p.y - 2, 5, 5};
            SDL_SetRenderDrawColor(renderer, commanderColor.r, commanderColor.g, commanderColor.b, commanderColor.a);
            countedRenderFillRect(renderer, &dot, stats);
            SDL_Rect ring{p.x - 4, p.y - 4, 9, 9};
            SDL_RenderDrawRect(renderer, &ring);
            break;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, "TACTICAL MAP", panel.x + 10, panel.y + 3, &stats, SDL_Color{235, 241, 255, 255});
        return panel;
    };

    auto drawEnemyBasePanel = [&](const SDL_Rect &minimapRect) {
        const StageRuntimeState &stageState = sim.stageState();
        if (!stageState.enabled || stageState.enemyBases.empty())
        {
            return;
        }
        std::vector<const StageEnemyBaseState *> bases;
        bases.reserve(stageState.enemyBases.size());
        for (const auto &base : stageState.enemyBases)
        {
            bases.push_back(&base);
        }
        std::sort(bases.begin(), bases.end(),
                  [](const StageEnemyBaseState *a, const StageEnemyBaseState *b) { return a->pos.y < b->pos.y; });

        auto shortLabel = [&](const StageEnemyBaseState &base, std::size_t index) -> std::string {
            std::string id = base.id;
            for (char &c : id)
            {
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (id.find("north") != std::string::npos) return "N";
            if (id.find("center") != std::string::npos || id.find("middle") != std::string::npos) return "C";
            if (id.find("south") != std::string::npos) return "S";
            if (bases.size() == 3)
            {
                return index == 0 ? "N" : (index == 1 ? "C" : "S");
            }
            return std::to_string(static_cast<int>(index + 1));
        };

        const int panelWidth = 92;
        const int rowHeight = 44;
        const int padX = 10;
        const int padY = 10;
        const int margin = 12;
        const int panelHeight = padY * 2 + 18 + static_cast<int>(bases.size()) * rowHeight;
        const int panelX = screenW - panelWidth - margin;
        int panelY = minimapRect.w > 0 ? minimapRect.y + minimapRect.h + 10 : margin;
        if (panelY + panelHeight > screenH - margin)
        {
            panelY = std::max(margin, screenH - margin - panelHeight);
        }
        SDL_Rect panel{panelX, panelY, panelWidth, panelHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 8, 10, 18, 210);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_Rect titleBand{panel.x, panel.y, panel.w, 20};
        SDL_SetRenderDrawColor(renderer, 68, 28, 28, 220);
        countedRenderFillRect(renderer, &titleBand, stats);
        SDL_Rect panelInner{panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2};
        SDL_SetRenderDrawColor(renderer, 20, 26, 38, 160);
        countedRenderDrawRect(renderer, &panelInner, stats);
        SDL_SetRenderDrawColor(renderer, 120, 130, 170, 210);
        SDL_RenderDrawRect(renderer, &panel);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, "BASES", panel.x + 10, panel.y + 2, &stats, SDL_Color{255, 232, 220, 255});

        for (std::size_t i = 0; i < bases.size(); ++i)
        {
            const StageEnemyBaseState &base = *bases[i];
            const int rowY = panel.y + padY + 18 + static_cast<int>(i) * rowHeight;
            const std::string label = shortLabel(base, i);
            font.drawText(renderer, label, panel.x + padX, rowY, &stats, SDL_Color{220, 230, 245, 255});

            if (base.sealed)
            {
                const std::string seal = "封";
                const int sealPadX = 4;
                const int sealPadY = 2;
                const int sealW = font.measureText(seal);
                SDL_Rect badge{panel.x + padX + 14, rowY, sealW + sealPadX * 2, lineHeight + sealPadY * 2};
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 120, 30, 30, 210);
                countedRenderFillRect(renderer, &badge, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                font.drawText(renderer, seal, badge.x + sealPadX, badge.y + sealPadY, &stats, SDL_Color{255, 230, 230, 255});
            }

            SDL_Color baseColor = base.sealed ? SDL_Color{90, 90, 100, 200} : SDL_Color{230, 90, 90, 220};
            const int iconSize = 10;
            SDL_Rect icon{panel.x + padX, rowY + lineHeight + 6, iconSize, iconSize};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, baseColor.r, baseColor.g, baseColor.b, baseColor.a);
            countedRenderFillRect(renderer, &icon, stats);
            SDL_SetRenderDrawColor(renderer, 30, 30, 40, 200);
            countedRenderDrawRect(renderer, &icon, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

            const int barWidth = 6;
            const int barHeight = rowHeight - 12;
            const int barX = panel.x + panel.w - padX - barWidth;
            const int barY = rowY + (rowHeight - barHeight) / 2;
            SDL_Rect barBg{barX, barY, barWidth, barHeight};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 18, 22, 30, 230);
            countedRenderFillRect(renderer, &barBg, stats);
            SDL_SetRenderDrawColor(renderer, 60, 70, 90, 200);
            countedRenderDrawRect(renderer, &barBg, stats);
            const float ratio = (!base.sealed && base.maxHp > 0.0f) ? std::clamp(base.hp / base.maxHp, 0.0f, 1.0f) : 0.0f;
            const int fillHeight = std::max(1, static_cast<int>(std::round((barHeight - 2) * ratio)));
            const float flash = std::clamp(base.hpFlashTimer / StageEnemyBaseState::kHpFlashDuration, 0.0f, 1.0f);
            const int flashBoost = static_cast<int>(std::round(60.0f * flash));
            SDL_Color fillColor{
                static_cast<Uint8>(std::min(baseColor.r + flashBoost, 255)),
                static_cast<Uint8>(std::min(baseColor.g + flashBoost, 255)),
                static_cast<Uint8>(std::min(baseColor.b + flashBoost, 255)),
                static_cast<Uint8>(std::min(baseColor.a + 25, 255))
            };
            SDL_Rect barFill{barX + 1, barY + (barHeight - 1) - fillHeight, barWidth - 2, fillHeight};
            SDL_SetRenderDrawColor(renderer, fillColor.r, fillColor.g, fillColor.b, fillColor.a);
            countedRenderFillRect(renderer, &barFill, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
    };

    auto drawCommanderConsole = [&]() {
        const int margin = 14;
        const int panelW = 288;
        const int panelH = 124;
        SDL_Rect panel{margin, screenH - panelH - margin, panelW, panelH};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 12, 16, 26, 220);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_Rect titleBand{panel.x, panel.y, panel.w, 22};
        SDL_SetRenderDrawColor(renderer, 24, 44, 74, 220);
        countedRenderFillRect(renderer, &titleBand, stats);
        SDL_Rect panelInner{panel.x + 1, panel.y + 1, panel.w - 2, panel.h - 2};
        SDL_SetRenderDrawColor(renderer, 24, 30, 44, 160);
        countedRenderDrawRect(renderer, &panelInner, stats);
        SDL_SetRenderDrawColor(renderer, 140, 150, 190, 210);
        SDL_RenderDrawRect(renderer, &panel);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, "YUUNA COMMAND", panel.x + 10, panel.y + 3, &stats, SDL_Color{245, 240, 220, 255});

        const SDL_Rect *portraitFrame = atlas.getFrame("yuna_front_0");
        const int portraitSize = 48;
        SDL_Rect portraitRect{panel.x + 12, panel.y + 34, portraitSize, portraitSize};
        if (portraitFrame && atlas.texture.get())
        {
            countedRenderCopy(renderer, atlas.texture.getRaw(), portraitFrame, &portraitRect, stats);
        }
        else
        {
            SDL_SetRenderDrawColor(renderer, 40, 60, 80, 255);
            countedRenderFillRect(renderer, &portraitRect, stats);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 20, 24, 32, 200);
        countedRenderDrawRect(renderer, &portraitRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        const float hpMax = std::max(sim.commanderStats.hp, 1.0f);
        const float hp = sim.commander.alive ? sim.commander.hp : 0.0f;
        const float hpRatio = std::clamp(hp / hpMax, 0.0f, 1.0f);
        const float hpLagRatio = std::clamp(sim.commanderHudHpLag / hpMax, 0.0f, 1.0f);
        const float mpMax = std::max(sim.commander.mpMax, 1.0f);
        const float mp = sim.commander.alive ? sim.commander.mp : 0.0f;
        const float mpRatio = std::clamp(mp / mpMax, 0.0f, 1.0f);

        const float pulse = (hpRatio < 0.3f && sim.commander.alive)
                                ? static_cast<float>((std::sin(sim.simTime * 6.0f) + 1.0f) * 0.5f)
                                : 0.0f;
        const int pulseBoost = static_cast<int>(std::round(35.0f * pulse));

        const int barX = portraitRect.x + portraitRect.w + 12;
        const int barW = panel.x + panel.w - barX - 12;
        const int barH = 10;
        const int hpBarY = panel.y + 38;
        const int mpBarY = hpBarY + barH + 18;

        SDL_Rect hpBg{barX, hpBarY, barW, barH};
        SDL_Rect mpBg{barX, mpBarY, barW, barH};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 18, 24, 34, 230);
        countedRenderFillRect(renderer, &hpBg, stats);
        countedRenderFillRect(renderer, &mpBg, stats);
        SDL_SetRenderDrawColor(renderer, 60, 70, 90, 200);
        countedRenderDrawRect(renderer, &hpBg, stats);
        countedRenderDrawRect(renderer, &mpBg, stats);

        const int hpLagW = static_cast<int>(std::round((barW - 2) * hpLagRatio));
        SDL_Rect hpLag{hpBg.x + 1, hpBg.y + 1, hpLagW, hpBg.h - 2};
        SDL_SetRenderDrawColor(renderer, 190, 90, 60, 200);
        countedRenderFillRect(renderer, &hpLag, stats);

        const int hpW = static_cast<int>(std::round((barW - 2) * hpRatio));
        SDL_Color hpColor{
            static_cast<Uint8>(std::min(220 + pulseBoost, 255)),
            static_cast<Uint8>(std::min(90 + pulseBoost, 255)),
            static_cast<Uint8>(std::min(90 + pulseBoost, 255)),
            230
        };
        if (sim.commanderHudHealFlashTimer > 0.0f)
        {
            hpColor.g = static_cast<Uint8>(std::min(200, hpColor.g + 60));
            hpColor.b = static_cast<Uint8>(std::min(200, hpColor.b + 60));
        }
        SDL_Rect hpFill{hpBg.x + 1, hpBg.y + 1, hpW, hpBg.h - 2};
        SDL_SetRenderDrawColor(renderer, hpColor.r, hpColor.g, hpColor.b, hpColor.a);
        countedRenderFillRect(renderer, &hpFill, stats);

        const int mpW = static_cast<int>(std::round((barW - 2) * mpRatio));
        SDL_Rect mpFill{mpBg.x + 1, mpBg.y + 1, mpW, mpBg.h - 2};
        SDL_SetRenderDrawColor(renderer, 90, 170, 255, 220);
        countedRenderFillRect(renderer, &mpFill, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

        std::ostringstream hpText;
        hpText << "HP " << static_cast<int>(std::round(hp)) << "/" << static_cast<int>(std::round(hpMax));
        font.drawText(renderer, hpText.str(), barX, hpBarY - lineHeight, &stats, SDL_Color{230, 240, 255, 255});
        std::ostringstream mpText;
        mpText << "MP " << static_cast<int>(std::round(mp)) << "/" << static_cast<int>(std::round(mpMax));
        font.drawText(renderer, mpText.str(), barX, mpBarY - lineHeight, &stats, SDL_Color{200, 220, 255, 255});

        std::ostringstream stanceText;
        stanceText << "Order " << stanceLabel(sim.currentOrder());
        std::ostringstream formationText;
        formationText << "Formation " << formationLabel(sim.formation);
        std::ostringstream guardText;
        guardText << "Guard " << (sim.commander.guardActive ? "READY" : "OPEN");
        std::ostringstream supportText;
        supportText << "Allies " << sim.yunas.size() << "  Panic " << sim.moraleSummary.panicCount;

        const int infoY = mpBg.y + mpBg.h + 12;
        font.drawText(renderer, stanceText.str(), panel.x + 12, infoY, &stats, SDL_Color{255, 224, 170, 255});
        font.drawText(renderer, formationText.str(), panel.x + 12, infoY + lineHeight, &stats, SDL_Color{205, 230, 255, 255});
        font.drawText(renderer, guardText.str(), panel.x + 144, infoY, &stats,
                      sim.commander.guardActive ? SDL_Color{120, 255, 220, 255} : SDL_Color{255, 210, 150, 255});
        font.drawText(renderer, supportText.str(), panel.x + 144, infoY + lineHeight, &stats,
                      sim.moraleSummary.panicCount > 0 ? SDL_Color{255, 170, 170, 255} : SDL_Color{215, 235, 225, 255});
    };

    MoraleState commanderMorale = sim.moraleSummary.commanderState;
    std::vector<MoraleState> moraleStates(sim.yunas.size(), MoraleState::Stable);
    for (const LegacySimulation::RenderQueue::AllySprite &ally : queue.allies)
    {
        if (ally.commander)
        {
            commanderMorale = ally.morale;
        }
        else if (ally.hasUnitIndex && ally.unitIndex < moraleStates.size())
        {
            moraleStates[ally.unitIndex] = ally.morale;
        }
    }
    if (moraleHud)
    {
        for (const MoraleHudIcon &icon : moraleHud->icons)
        {
            if (icon.commander)
            {
                commanderMorale = icon.state;
            }
            else if (icon.unitIndex < moraleStates.size())
            {
                moraleStates[icon.unitIndex] = icon.state;
            }
        }
    }

    SDL_SetRenderDrawColor(renderer, 26, 32, 38, 255);
    countedRenderClear(renderer, stats);

    drawTileLayer(renderer, map, map.floor, camera, screenW, screenH, stats);
    if (map.tileset.get())
    {
        SDL_SetTextureColorMod(map.tileset.getRaw(), 190, 190, 200);
        drawTileLayer(renderer, map, map.block, camera, screenW, screenH, stats);
        SDL_SetTextureColorMod(map.tileset.getRaw(), 255, 255, 255);
    }
    drawTileLayer(renderer, map, map.deco, camera, screenW, screenH, stats);

    const StageRuntimeState &stageState = sim.stageState();

    drawWorldPulse(sim.basePos, 42.0f, SDL_Color{60, 110, 180, 52}, 1.6f, 42.0f);
    for (const StageEnemyBaseState &base : stageState.enemyBases)
    {
        if (base.sealed)
        {
            drawWorldPulse(base.pos, std::max(base.radiusPx, 24.0f), SDL_Color{90, 90, 100, 28}, 1.3f, 20.0f);
        }
        else
        {
            drawWorldPulse(base.pos, std::max(base.radiusPx, 24.0f), SDL_Color{180, 58, 52, 50}, 2.2f, 46.0f);
        }
    }

    // Draw base
    const Vec2 baseScreen = worldToScreen(sim.basePos, camera);

    if (atlas.texture.get())
    {
        if (const SDL_Rect *baseFrame = atlas.getFrame("base_box"))
        {
            SDL_Rect dest{
                static_cast<int>(baseScreen.x - baseFrame->w * 0.5f),
                static_cast<int>(baseScreen.y - baseFrame->h * 0.5f),
                baseFrame->w,
                baseFrame->h};
            countedRenderCopy(renderer, atlas.texture.getRaw(), baseFrame, &dest, stats);
        }
        else
        {
            SDL_FRect baseRect{baseScreen.x - sim.config.base_aabb.x * 0.5f, baseScreen.y - sim.config.base_aabb.y * 0.5f, sim.config.base_aabb.x, sim.config.base_aabb.y};
            SDL_SetRenderDrawColor(renderer, 130, 90, 50, 255);
            countedRenderFillRectF(renderer, &baseRect, stats);
        }
    }
    else
    {
        SDL_FRect baseRect{baseScreen.x - sim.config.base_aabb.x * 0.5f, baseScreen.y - sim.config.base_aabb.y * 0.5f, sim.config.base_aabb.x, sim.config.base_aabb.y};
        SDL_SetRenderDrawColor(renderer, 130, 90, 50, 255);
        countedRenderFillRectF(renderer, &baseRect, stats);
    }

    if (sim.missionMode == MissionMode::Capture)
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const auto &zone : sim.captureZones)
        {
            Vec2 screenPos = worldToScreen(zone.worldPos, camera);
            const int radius = static_cast<int>(zone.config.radius_px);
            SDL_Rect outline{static_cast<int>(screenPos.x) - radius, static_cast<int>(screenPos.y) - radius,
                             radius * 2, radius * 2};
            SDL_SetRenderDrawColor(renderer, 40, 160, 255, 90);
            countedRenderDrawRect(renderer, &outline, stats);
            SDL_Rect fill = outline;
            fill.h = static_cast<int>(outline.h * zone.progress);
            fill.y = outline.y + (outline.h - fill.h);
            SDL_SetRenderDrawColor(renderer, 80, 210, 255, 100);
            countedRenderFillRect(renderer, &fill, stats);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    if (stageState.enabled && !stageState.allyBases.empty())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const StageAllyBaseState &base : stageState.allyBases)
        {
            if (base.destroyed)
            {
                continue;
            }
            const float auraRadius = base.auraRadiusPx > 0.0f ? base.auraRadiusPx : 128.0f;
            Vec2 screenPos = worldToScreen(base.pos, camera);
            SDL_SetRenderDrawColor(renderer, 70, 180, 255, 80);
            drawFilledCircle(renderer, screenPos, auraRadius, stats);
            if (showWorldLabels)
            {
                const TextRenderer &labelFont = debugFont.isLoaded() ? debugFont : font;
                std::ostringstream oss;
                oss << "ALLY " << (base.id.empty() ? "Camp" : base.id) << ' '
                    << static_cast<int>(std::round(std::max(base.hp, 0.0f))) << '/'
                    << static_cast<int>(std::round(std::max(base.maxHp, 0.0f)));
                const std::string label = oss.str();
                const int labelWidth = measureWorldText(labelFont, label, debugLineHeight);
                const int pad = 4;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - labelWidth / 2 - pad,
                    static_cast<int>(std::round(screenPos.y - auraRadius)) - (debugLineHeight + pad * 2) - 4,
                    labelWidth + pad * 2,
                    debugLineHeight + pad * 2};
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawColor(renderer, 10, 30, 50, 160);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_Color textColor{210, 240, 255, 255};
                labelFont.drawText(renderer, label, labelBg.x + pad, labelBg.y + pad, &stats, textColor);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    // Gate rendering removed (legacy)

    if (stageState.enabled && !stageState.enemyBases.empty())
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const StageEnemyBaseState &base : stageState.enemyBases)
        {
            Vec2 screenPos = worldToScreen(base.pos, camera);
            const float drawRadius = std::max(base.radiusPx, 32.0f);
            const SDL_Color baseColor = base.sealed ? SDL_Color{90, 90, 90, 130} : SDL_Color{180, 60, 50, 150};
            SDL_SetRenderDrawColor(renderer, baseColor.r, baseColor.g, baseColor.b, baseColor.a);
            drawFilledCircle(renderer, screenPos, drawRadius, stats);
            if (!base.sealed && base.maxHp > 0.0f)
            {
                const float ratio = std::clamp(base.hp / base.maxHp, 0.0f, 1.0f);
                if (ratio > 0.0f)
                {
                    const float innerRadius = std::max(4.0f, drawRadius * ratio);
                    SDL_SetRenderDrawColor(renderer, 230, 110, 90, 180);
                    drawFilledCircle(renderer, screenPos, innerRadius, stats);
                }
            }
            else if (base.sealed)
            {
                SDL_SetRenderDrawColor(renderer, 40, 40, 40, 180);
                drawFilledCircle(renderer, screenPos, std::max(4.0f, drawRadius * 0.35f), stats);
            }

            if (showWorldLabels)
            {
                const TextRenderer &labelFont = debugFont.isLoaded() ? debugFont : font;
                std::ostringstream oss;
                oss << "Base " << (base.id.empty() ? "???": base.id) << ' ';
                if (base.sealed)
                {
                    oss << "(Sealed)";
                }
                else if (base.maxHp > 0.0f)
                {
                    oss << static_cast<int>(std::round(std::max(base.hp, 0.0f))) << '/' << static_cast<int>(std::round(base.maxHp));
                }
                else
                {
                    oss << "Active";
                }
                const std::string label = oss.str();
                const int labelWidth = measureWorldText(labelFont, label, debugLineHeight);
                const int pad = 4;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - labelWidth / 2 - pad,
                    static_cast<int>(std::round(screenPos.y - drawRadius)) - (debugLineHeight + pad * 2) - 6,
                    labelWidth + pad * 2,
                    debugLineHeight + pad * 2};
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawColor(renderer, 28, 12, 12, 160);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_Color textColor = base.sealed ? SDL_Color{200, 200, 200, 255} : SDL_Color{255, 210, 210, 255};
                labelFont.drawText(renderer, label, labelBg.x + pad, labelBg.y + pad, &stats, textColor);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    const SDL_Rect *commanderFrame = nullptr;
    auto fetchFrame = [&](const std::string &prefix) -> const SDL_Rect * {
        if (prefix.empty())
        {
            return nullptr;
        }
        return atlas.getFrame(prefix + "_0");
    };
    commanderFrame = fetchFrame(sim.commanderStats.spritePrefix);
    auto animatedFrameForPrefix = [&](const std::string &prefix, std::uint64_t divisorMs) -> const SDL_Rect * {
        if (prefix.empty())
        {
            return nullptr;
        }
        std::vector<const SDL_Rect *> frames;
        for (int i = 0; i < 8; ++i)
        {
            const SDL_Rect *frame = atlas.getFrame(prefix + "_" + std::to_string(i));
            if (!frame)
            {
                break;
            }
            frames.push_back(frame);
        }
        if (frames.empty())
        {
            return nullptr;
        }
        if (frames.size() == 1)
        {
            return frames.front();
        }
        const std::uint64_t ticks = SDL_GetTicks64();
        return frames[(ticks / divisorMs) % frames.size()];
    };
    auto frameForEnemy = [&](EnemyArchetype type) -> const SDL_Rect * {
        switch (type)
        {
        case EnemyArchetype::Goblin:
        {
            const SDL_Rect *goblin = animatedFrameForPrefix(sim.goblinStats.spritePrefix, 120);
            const SDL_Rect *slime = animatedFrameForPrefix(sim.slimeStats.spritePrefix, 180);
            return goblin ? goblin : slime;
        }
        case EnemyArchetype::Magician: return animatedFrameForPrefix(sim.magicianStats.spritePrefix, 180);
        case EnemyArchetype::Bat: return animatedFrameForPrefix(sim.batStats.spritePrefix, 140);
        case EnemyArchetype::Toritori: return animatedFrameForPrefix(sim.toritoriStats.spritePrefix, 180);
        case EnemyArchetype::Golem: return animatedFrameForPrefix(sim.golemStats.spritePrefix, 220);
        case EnemyArchetype::Wallbreaker: return animatedFrameForPrefix(sim.wallbreakerStats.spritePrefix, 180);
        case EnemyArchetype::Boss:
            return nullptr;
        case EnemyArchetype::Slime:
        default: return animatedFrameForPrefix(sim.slimeStats.spritePrefix, 180);
        }
    };
    const SDL_Rect *friendRing = atlas.getFrame("ring_friend");

    auto chibiFrameForAlly = [&](const LegacySimulation::RenderQueue::AllySprite &ally)
        -> std::pair<const SDL_Rect *, SDL_RendererFlip> {
        constexpr int kFrames = 6;
        std::uint64_t idx = 0;
        std::string base;
        if (ally.attacking && ally.attackDuration > 0.0f)
        {
            const float progress =
                std::clamp(ally.attackTimer / ally.attackDuration, 0.0f, 0.999f);
            idx = static_cast<std::uint64_t>(progress * kFrames);
            base = "chibi_attack";
        }
        else
        {
            const bool moving = ally.moving;
            base = moving ? "chibi_walk" : "chibi_idle";
            // animate by real time to avoid reliance on simulation frameCounter
            const std::uint64_t ticks = SDL_GetTicks64();
            const std::uint64_t divisorMs = moving ? 100 : 200; // ~10fps walk, ~5fps idle
            idx = ((ticks / divisorMs) % kFrames);
        }
        const std::string key = base + "_" + std::to_string(idx);
        const SDL_Rect *rect = atlas.getFrame(key);
        const SDL_RendererFlip flip = ally.facingX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        return {rect, flip};
    };
    auto commanderFrameForAlly = [&](const LegacySimulation::RenderQueue::AllySprite &ally)
        -> std::pair<const SDL_Rect *, SDL_RendererFlip> {
        constexpr int kFrames = 6;
        std::uint64_t idx = 0;
        std::string base;
        if (ally.attacking && ally.attackDuration > 0.0f)
        {
            const float progress = std::clamp(ally.attackTimer / ally.attackDuration, 0.0f, 0.999f);
            idx = static_cast<std::uint64_t>(progress * kFrames);
            base = "yuna_attack";
        }
        else
        {
            const bool moving = ally.moving;
            base = moving ? "yuna_walk" : "yuna_idle";
            const std::uint64_t ticks = SDL_GetTicks64();
            const std::uint64_t divisorMs = moving ? 100 : 200;
            idx = ((ticks / divisorMs) % kFrames);
        }
        const std::string key = base + "_" + std::to_string(idx);
        const SDL_Rect *rect = atlas.getFrame(key);
        if (!rect)
        {
            rect = commanderFrame;
        }
        const SDL_RendererFlip flip = ally.facingX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
        return {rect, flip};
    };

    if (atlas.texture.get())
    {
        for (const LegacySimulation::RenderQueue::AllySprite &ally : queue.allies)
        {
            if (skipActors && !ally.commander)
            {
                continue;
            }

            Vec2 screenPos = worldToScreen(ally.position, camera);
            if (ally.commander)
            {
                const float hpMax = std::max(sim.commanderStats.hp, 1.0f);
                const float hpRatio = std::clamp(sim.commander.hp / hpMax, 0.0f, 1.0f);
                SDL_Color ringColor{
                    static_cast<Uint8>(std::min(255.0f, 200.0f + (1.0f - hpRatio) * 55.0f)),
                    static_cast<Uint8>(std::min(255.0f, 180.0f * hpRatio + 40.0f)),
                    static_cast<Uint8>(std::min(255.0f, 200.0f * hpRatio + 40.0f)),
                    60
                };
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, ringColor.r, ringColor.g, ringColor.b, ringColor.a);
                drawFilledCircle(renderer, screenPos, ally.radius + 8.0f, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                const auto [resolvedCommanderFrame, commanderFlip] = commanderFrameForAlly(ally);
                if (resolvedCommanderFrame)
                {
                    SDL_Rect dest{
                        static_cast<int>(screenPos.x - resolvedCommanderFrame->w * 0.5f),
                        static_cast<int>(screenPos.y - resolvedCommanderFrame->h * 0.5f),
                        resolvedCommanderFrame->w,
                        resolvedCommanderFrame->h};
                    countedRenderCopyFlip(
                        renderer, atlas.texture.getRaw(), resolvedCommanderFrame, &dest, commanderFlip, stats);
                    if (friendRing)
                    {
                        SDL_Rect ringDest{
                            dest.x + (dest.w - friendRing->w) / 2,
                            dest.y + dest.h - friendRing->h,
                            friendRing->w,
                            friendRing->h};
                        countedRenderCopy(renderer, atlas.texture.getRaw(), friendRing, &ringDest, stats);
                    }
                }
                else
                {
                    SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
                    drawFilledCircle(renderer, screenPos, ally.radius, stats);
                }
                drawMoraleIcon(ally.position, ally.radius, commanderMorale);
                continue;
            }

            const auto [chibiFrame, flip] = chibiFrameForAlly(ally);
            const bool drawAsSquare = ally.named;
            if (drawAsSquare)
            {
                const int size = static_cast<int>(std::round(ally.radius * 2.2f));
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - size * 0.5f),
                    static_cast<int>(screenPos.y - size * 0.5f),
                    size,
                    size};
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_Color unitColor = unitRingColor(ally.job);
                SDL_SetRenderDrawColor(renderer, unitColor.r, unitColor.g, unitColor.b, ally.alpha);
                countedRenderFillRect(renderer, &dest, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        if (g_showChibiLabels)
        {
            drawTemperamentLabel(ally, static_cast<float>(dest.y), static_cast<float>(dest.x + dest.w * 0.5f));
        }
                drawNameLabel(ally, screenPos);
            }
            else if (chibiFrame)
            {
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), ally.alpha);
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - chibiFrame->w * 0.5f),
                    static_cast<int>(screenPos.y - chibiFrame->h * 0.5f),
                    chibiFrame->w,
                    chibiFrame->h};
                countedRenderCopyFlip(renderer, atlas.texture.getRaw(), chibiFrame, &dest, flip, stats);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
                if (friendRing)
                {
                    SDL_Color ringColor = unitRingColor(ally.job);
                    SDL_SetTextureColorMod(atlas.texture.getRaw(), ringColor.r, ringColor.g, ringColor.b);
                    SDL_Rect ringDest{
                        dest.x + (dest.w - friendRing->w) / 2,
                        dest.y + dest.h - friendRing->h,
                        friendRing->w,
                        friendRing->h};
                    countedRenderCopy(renderer, atlas.texture.getRaw(), friendRing, &ringDest, stats);
                    SDL_SetTextureColorMod(atlas.texture.getRaw(), 255, 255, 255);
                }
                if (!ally.named)
                {
                    if (g_showChibiLabels)
                    {
                        drawTemperamentLabel(
                            ally, static_cast<float>(dest.y), static_cast<float>(dest.x + dest.w * 0.5f));
                    }
                }
                drawNameLabel(ally, screenPos);
            }
            else
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_Color unitColor = unitRingColor(ally.job);
                SDL_SetRenderDrawColor(renderer, unitColor.r, unitColor.g, unitColor.b, ally.alpha);
                drawFilledCircle(renderer, screenPos, ally.radius, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                if (!ally.named)
                {
                    if (g_showChibiLabels)
                    {
                        drawTemperamentLabel(ally, screenPos.y - ally.radius, screenPos.x);
                    }
                }
                drawNameLabel(ally, screenPos);
            }

            MoraleState state = (ally.hasUnitIndex && ally.unitIndex < moraleStates.size()) ? moraleStates[ally.unitIndex]
                                                                                           : ally.morale;
            drawMoraleIcon(ally.position, ally.radius, state);
        }
        if (!queue.deathFx.empty())
        {
            for (const auto &fx : queue.deathFx)
            {
                const float duration = std::max(fx.duration, 0.0001f);
                const float progress = std::clamp(1.0f - (fx.timer / duration), 0.0f, 1.0f);
                const int frameIdx = std::min(3, static_cast<int>(progress * 4.0f));
                const std::string key = std::string(fx.commander ? "yuna_death_" : "chibi_death_") +
                                        std::to_string(frameIdx);
                const SDL_Rect *frame = atlas.getFrame(key);
                if (!frame)
                {
                    continue;
                }
                const std::uint8_t alpha = static_cast<std::uint8_t>(std::clamp(fx.timer / duration, 0.0f, 1.0f) * 255);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), alpha);
                Vec2 screenPos = worldToScreen(fx.position, camera);
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - frame->w * 0.5f),
                    static_cast<int>(screenPos.y - frame->h * 0.5f),
                    frame->w,
                    frame->h};
                const SDL_RendererFlip flip = fx.facingX < 0.0f ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
                countedRenderCopyFlip(renderer, atlas.texture.getRaw(), frame, &dest, flip, stats);
                SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
            }
        }
        SDL_SetTextureAlphaMod(atlas.texture.getRaw(), 255);
    }
    else
    {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        for (const LegacySimulation::RenderQueue::AllySprite &ally : queue.allies)
        {
            if (skipActors && !ally.commander)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(ally.position, camera);
            if (ally.commander)
            {
                SDL_SetRenderDrawColor(renderer, 200, 220, 255, 255);
                drawFilledCircle(renderer, screenPos, ally.radius, stats);
                drawMoraleIcon(ally.position, ally.radius, commanderMorale);
                continue;
            }

            SDL_Color unitColor = unitRingColor(ally.job);
            SDL_SetRenderDrawColor(renderer, unitColor.r, unitColor.g, unitColor.b, ally.alpha);
            drawFilledCircle(renderer, screenPos, ally.radius, stats);
            if (g_showChibiLabels)
            {
                drawTemperamentLabel(ally, screenPos.y - ally.radius, screenPos.x);
            }
            MoraleState state = (ally.hasUnitIndex && ally.unitIndex < moraleStates.size()) ? moraleStates[ally.unitIndex]
                                                                                            : ally.morale;
            drawMoraleIcon(ally.position, ally.radius, state);
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    SDL_SetRenderDrawColor(renderer, 120, 150, 200, 255);
    for (const LegacySimulation::RenderQueue::WallSprite &wall : queue.walls)
    {
        if (skipActors)
        {
            continue;
        }
        Vec2 screenPos = worldToScreen(wall.position, camera);
        drawFilledCircle(renderer, screenPos, wall.radius, stats);
    }

    if (atlas.texture.get())
    {
        // Ephemeral effects (rings / cones)
        for (const auto &fx : queue.effects)
        {
            Vec2 screenPos = worldToScreen(fx.position, camera);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, fx.r, fx.g, fx.b, fx.a);
            if (fx.cone && fx.length > 0.0f)
            {
                Vec2 dir = fx.dir;
                const float dirLen = std::sqrt(lengthSq(dir));
                if (dirLen > 0.0001f)
                {
                    dir = dir / dirLen;
                }
                else
                {
                    dir = {1.0f, 0.0f};
                }
                Vec2 perp{-dir.y, dir.x};
                const float nearHalf = fx.nearWidth * 0.5f;
                const float farHalf = fx.farWidth * 0.5f;
                Vec2 p0 = screenPos + perp * nearHalf;
                Vec2 p1 = screenPos - perp * nearHalf;
                Vec2 tip = screenPos + dir * fx.length;
                Vec2 p2 = tip + perp * farHalf;
                Vec2 p3 = tip - perp * farHalf;
                SDL_Vertex verts[4];
                verts[0].position = {p0.x, p0.y};
                verts[1].position = {p1.x, p1.y};
                verts[2].position = {p2.x, p2.y};
                verts[3].position = {p3.x, p3.y};
                for (auto &v : verts)
                {
                    v.color = SDL_Color{fx.r, fx.g, fx.b, fx.a};
                }
                int indices[6] = {0, 1, 2, 1, 3, 2};
                SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
            }
            else
            {
                drawFilledCircle(renderer, screenPos, fx.radius, stats);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        // Projectiles (simple circles)
        for (const auto &proj : queue.projectiles)
        {
            if (skipActors)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(proj.position, camera);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 240, 120, 60, 220);
            drawFilledCircle(renderer, screenPos, proj.radius, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        for (const LegacySimulation::RenderQueue::EnemySprite &enemy : queue.enemies)
        {
            if (skipActors && enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            const SDL_Rect *frame = frameForEnemy(enemy.type);
            Vec2 screenPos = worldToScreen(enemy.position, camera);
            const SDL_Color halo = enemyColor(enemy.type);
            SDL_Rect spriteRect{};
            bool hasSpriteRect = false;
            if (frame)
            {
                SDL_Rect dest{
                    static_cast<int>(screenPos.x - frame->w * 0.5f),
                    static_cast<int>(screenPos.y - frame->h * 0.5f),
                    frame->w,
                    frame->h};
                countedRenderCopy(renderer, atlas.texture.getRaw(), frame, &dest, stats);
                spriteRect = dest;
                hasSpriteRect = true;
            }
            else
            {
                SDL_Color color = enemyColor(enemy.type);
                SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
                drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            }

            if (!minimalHud && enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
            {
                const std::string bossText = "BOSS";
                const int textWidth = measureWorldText(debugFont, bossText, debugLineHeight);
                const int padX = 6;
                const int padY = 3;
                const float spriteTop = hasSpriteRect ? static_cast<float>(spriteRect.y) : screenPos.y - enemy.radius;
                const float centerX = hasSpriteRect ? static_cast<float>(spriteRect.x + spriteRect.w * 0.5f) : screenPos.x;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(centerX)) - textWidth / 2 - padX,
                    static_cast<int>(std::round(spriteTop)) - (debugLineHeight + padY * 2) - 8,
                    textWidth + padX * 2,
                    debugLineHeight + padY * 2
                };
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 70, 0, 80, 200);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                debugFont.drawText(renderer, bossText, labelBg.x + padX, labelBg.y + padY, &stats,
                                   SDL_Color{255, 180, 255, 255});
            }
            if (!minimalHud)
            {
                // Archetype abbreviation label (non-bossも表示)
                const TextRenderer &labelFont = debugFont.isLoaded() ? debugFont : font;
                const std::string abbrev = enemyLabel(enemy.type);
                const int abbrevWidth = labelFont.measureText(abbrev);
                const int padX = 4;
                const int padY = 2;
                const float labelAnchorY = hasSpriteRect ? static_cast<float>(spriteRect.y) : screenPos.y - enemy.radius;
                SDL_Rect tagBg{
                    static_cast<int>(std::round(screenPos.x)) - abbrevWidth / 2 - padX,
                    static_cast<int>(std::round(labelAnchorY)) - (labelFont.getLineHeight() + padY * 2) - 2,
                    abbrevWidth + padX * 2,
                    labelFont.getLineHeight() + padY * 2};
                SDL_Color labelBgColor{static_cast<Uint8>(std::min(halo.r + 30, 255)),
                                       static_cast<Uint8>(std::min(halo.g + 30, 255)),
                                       static_cast<Uint8>(std::min(halo.b + 30, 255)),
                                       200};
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, labelBgColor.r, labelBgColor.g, labelBgColor.b, labelBgColor.a);
                countedRenderFillRect(renderer, &tagBg, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                labelFont.drawText(renderer, abbrev, tagBg.x + padX, tagBg.y + padY, &stats, SDL_Color{0, 0, 0, 255});
            }

            drawEnemyHpBar(enemy, screenPos, hasSpriteRect ? &spriteRect : nullptr);
        }
    }
    else
    {
        for (const auto &proj : queue.projectiles)
        {
            if (skipActors)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(proj.position, camera);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 240, 120, 60, 220);
            drawFilledCircle(renderer, screenPos, proj.radius, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        for (const LegacySimulation::RenderQueue::EnemySprite &enemy : queue.enemies)
        {
            if (skipActors && enemy.type != EnemyArchetype::Boss)
            {
                continue;
            }
            Vec2 screenPos = worldToScreen(enemy.position, camera);
            const SDL_Color halo = enemyColor(enemy.type);
            SDL_SetRenderDrawColor(renderer, halo.r, halo.g, halo.b, 255);
            drawFilledCircle(renderer, screenPos, enemy.radius, stats);
            if (!minimalHud)
            {
                const TextRenderer &labelFont = debugFont.isLoaded() ? debugFont : font;
                const std::string abbrev = enemyLabel(enemy.type);
                const int abbrevWidth = labelFont.measureText(abbrev);
                const int padX = 4;
                const int padY = 2;
                const float labelAnchorY = screenPos.y - enemy.radius;
                SDL_Rect tagBg{
                    static_cast<int>(std::round(screenPos.x)) - abbrevWidth / 2 - padX,
                    static_cast<int>(std::round(labelAnchorY)) - (labelFont.getLineHeight() + padY * 2) - 2,
                    abbrevWidth + padX * 2,
                    labelFont.getLineHeight() + padY * 2};
                SDL_Color labelBgColor{static_cast<Uint8>(std::min(halo.r + 30, 255)),
                                       static_cast<Uint8>(std::min(halo.g + 30, 255)),
                                       static_cast<Uint8>(std::min(halo.b + 30, 255)),
                                       200};
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, labelBgColor.r, labelBgColor.g, labelBgColor.b, labelBgColor.a);
                countedRenderFillRect(renderer, &tagBg, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                labelFont.drawText(renderer, abbrev, tagBg.x + padX, tagBg.y + padY, &stats, SDL_Color{0, 0, 0, 255});
            }
            if (!minimalHud && enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
            {
                const std::string bossText = "BOSS";
                const int textWidth = measureWorldText(debugFont, bossText, debugLineHeight);
                const int padX = 6;
                const int padY = 3;
                SDL_Rect labelBg{
                    static_cast<int>(std::round(screenPos.x)) - textWidth / 2 - padX,
                    static_cast<int>(std::round(screenPos.y - enemy.radius)) - (debugLineHeight + padY * 2) - 8,
                    textWidth + padX * 2,
                    debugLineHeight + padY * 2
                };
                if (labelBg.x < 4) labelBg.x = 4;
                if (labelBg.x + labelBg.w > screenW - 4) labelBg.x = screenW - labelBg.w - 4;
                if (labelBg.y < 4) labelBg.y = 4;
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, 70, 0, 80, 200);
                countedRenderFillRect(renderer, &labelBg, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                debugFont.drawText(renderer, bossText, labelBg.x + padX, labelBg.y + padY, &stats,
                                   SDL_Color{255, 180, 255, 255});
            }
            drawEnemyHpBar(enemy, screenPos, nullptr);
        }
    }

    // Ambient vignette overlay for dungeon mood
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 8, 24, 140);
    SDL_Rect overlay{0, 0, screenW, screenH};
    countedRenderFillRect(renderer, &overlay, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Minimap (top-right)
    const SDL_Rect minimapRect = drawMinimap();
    drawEnemyBasePanel(minimapRect);

    // Commander console (bottom-left)
    drawCommanderConsole();

    // Enemy legend (always on)
    if (!minimalHud)
    {
        struct LegendEntry
        {
            const char *label;
            SDL_Color color;
        };
        const LegendEntry entries[] = {
            {"DRG (Boss)", SDL_Color{230, 40, 40, 255}},
            {"GLM (Golem)", SDL_Color{245, 210, 50, 255}},
            {"WB (Wallbreaker)", SDL_Color{200, 140, 90, 255}},
            {"GB (Goblin)", SDL_Color{90, 210, 90, 255}},
            {"MG (Magician)", SDL_Color{170, 130, 230, 255}},
            {"BT (Bat)", SDL_Color{90, 190, 210, 255}},
            {"TT (Toritori)", SDL_Color{230, 150, 80, 255}},
            {"SL (Slime)", SDL_Color{90, 130, 230, 255}},
        };
        const int padX = 8;
        const int padY = 6;
        const int boxSize = 14;
        const TextRenderer &legendFont = font.isLoaded() ? font : debugFont;
        const int lineH = legendFont.isLoaded() ? legendFont.getLineHeight() : 18;
        int y = screenH - static_cast<int>(std::size(entries)) * (lineH + padY) - 12;
        const int x = screenW - 240;
        SDL_Rect panel{x - padX, y - padY, 220 + padX * 2, static_cast<int>(std::size(entries)) * (lineH + padY) + padY};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 12, 12, 24, 200);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        for (const auto &entry : entries)
        {
            SDL_Rect swatch{x, y + (lineH - boxSize) / 2, boxSize, boxSize};
            SDL_SetRenderDrawColor(renderer, entry.color.r, entry.color.g, entry.color.b, 255);
            countedRenderFillRect(renderer, &swatch, stats);
            legendFont.drawText(renderer, entry.label, x + boxSize + 8, y, nullptr, SDL_Color{230, 230, 240, 255});
            y += lineH + padY;
        }
    }
}

class BattleScene;
