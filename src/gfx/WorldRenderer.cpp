#include "gfx/WorldRenderer.h"

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
            if (debugFont.isLoaded())
            {
                std::ostringstream oss;
                oss << "拠点オーラ (r=" << static_cast<int>(std::round(auraRadius)) << ")";
                const std::string label = oss.str();
                const int labelWidth = measureWorldText(debugFont, label, debugLineHeight);
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
                debugFont.drawText(renderer, label, labelBg.x + pad, labelBg.y + pad, &stats, textColor);
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

            if (debugFont.isLoaded())
            {
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
                const int labelWidth = measureWorldText(debugFont, label, debugLineHeight);
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
                debugFont.drawText(renderer, label, labelBg.x + pad, labelBg.y + pad, &stats, textColor);
            }
        }
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    }

    const SDL_Rect *commanderFrame = nullptr;
    const SDL_Rect *slimeFrame = nullptr;
    const SDL_Rect *goblinFrame = nullptr;
    const SDL_Rect *magicianFrame = nullptr;
    const SDL_Rect *batFrame = nullptr;
    const SDL_Rect *toritoriFrame = nullptr;
    const SDL_Rect *golemFrame = nullptr;
    const SDL_Rect *wallbreakerFrame = nullptr;
    auto fetchFrame = [&](const std::string &prefix) -> const SDL_Rect * {
        if (prefix.empty())
        {
            return nullptr;
        }
        return atlas.getFrame(prefix + "_0");
    };
    commanderFrame = fetchFrame(sim.commanderStats.spritePrefix);
    slimeFrame = fetchFrame(sim.slimeStats.spritePrefix);
    goblinFrame = fetchFrame(sim.goblinStats.spritePrefix);
    magicianFrame = fetchFrame(sim.magicianStats.spritePrefix);
    batFrame = fetchFrame(sim.batStats.spritePrefix);
    toritoriFrame = fetchFrame(sim.toritoriStats.spritePrefix);
    golemFrame = fetchFrame(sim.golemStats.spritePrefix);
    wallbreakerFrame = fetchFrame(sim.wallbreakerStats.spritePrefix);
    auto frameForEnemy = [&](EnemyArchetype type) -> const SDL_Rect * {
        switch (type)
        {
        case EnemyArchetype::Goblin: return goblinFrame ? goblinFrame : slimeFrame;
        case EnemyArchetype::Magician: return magicianFrame ? magicianFrame : slimeFrame;
        case EnemyArchetype::Bat: return batFrame ? batFrame : slimeFrame;
        case EnemyArchetype::Toritori: return toritoriFrame ? toritoriFrame : slimeFrame;
        case EnemyArchetype::Golem: return golemFrame ? golemFrame : slimeFrame;
        case EnemyArchetype::Wallbreaker: return wallbreakerFrame ? wallbreakerFrame : slimeFrame;
        case EnemyArchetype::Boss:
            return nullptr;
        case EnemyArchetype::Slime:
        default: return slimeFrame;
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
                if (commanderFrame)
                {
                    SDL_Rect dest{
                        static_cast<int>(screenPos.x - commanderFrame->w * 0.5f),
                        static_cast<int>(screenPos.y - commanderFrame->h * 0.5f),
                        commanderFrame->w,
                        commanderFrame->h};
                    countedRenderCopy(renderer, atlas.texture.getRaw(), commanderFrame, &dest, stats);
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
                const std::string key = "chibi_death_" + std::to_string(frameIdx);
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

            if (enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
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
            if (enemy.type == EnemyArchetype::Boss && debugFont.isLoaded())
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
        }
    }

    // Ambient vignette overlay for dungeon mood
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 8, 24, 140);
    SDL_Rect overlay{0, 0, screenW, screenH};
    countedRenderFillRect(renderer, &overlay, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);

    // Commander HP/MP HUD
    if (sim.commander.alive)
    {
        std::ostringstream hpmp;
        hpmp << "HP " << static_cast<int>(std::round(sim.commander.hp)) << "/" << static_cast<int>(std::round(sim.commanderStats.hp))
             << "  MP " << static_cast<int>(std::round(sim.commander.mp)) << "/" << static_cast<int>(std::round(sim.commander.mpMax));
        font.drawText(renderer, hpmp.str(), 16, 16, nullptr, SDL_Color{240, 240, 255, 255});
    }

    // Enemy legend (always on)
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
