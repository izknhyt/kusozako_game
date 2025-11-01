#include "app/UiView.h"

#include "app/RenderUtils.h"
#ifndef KUSOZAKO_UIVIEW_STUB_TEXT_RENDERER
#include "app/TextRenderer.h"
#endif
#include "world/LegacySimulation.h"
#include "world/LegacyTypes.h"
#include "world/MoraleTypes.h"
#include "world/FormationUtils.h"
#include "world/SkillRuntime.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

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

namespace
{
const TextRenderer &resolveDebugFont(const UiView::Dependencies &deps)
{
    if (deps.debugFont)
    {
        return *deps.debugFont;
    }
    return *deps.hudFont;
}
}

const char *UiView::actionDisplayName(ActionId id)
{
    switch (id)
    {
    case ActionId::CommanderMoveX:
        return "CommanderMoveX";
    case ActionId::CommanderMoveY:
        return "CommanderMoveY";
    case ActionId::CommanderOrderRushNearest:
        return "OrderRushNearest";
    case ActionId::CommanderOrderPushForward:
        return "OrderPushForward";
    case ActionId::CommanderOrderFollowLeader:
        return "OrderFollowLeader";
    case ActionId::CommanderOrderDefendBase:
        return "OrderDefendBase";
    case ActionId::CycleFormationPrevious:
        return "FormationPrev";
    case ActionId::CycleFormationNext:
        return "FormationNext";
    case ActionId::ToggleDebugHud:
        return "ToggleDebugHud";
    case ActionId::ToggleDebugOverlay:
        return "ToggleDebugOverlay";
    case ActionId::ReloadConfig:
        return "ReloadConfig";
    case ActionId::DumpSpawnHistory:
        return "DumpSpawnHistory";
    case ActionId::RestartScenario:
        return "RestartScenario";
    case ActionId::SelectSkill1:
        return "SelectSkill1";
    case ActionId::SelectSkill2:
        return "SelectSkill2";
    case ActionId::SelectSkill3:
        return "SelectSkill3";
    case ActionId::SelectSkill4:
        return "SelectSkill4";
    case ActionId::SelectSkill5:
        return "SelectSkill5";
    case ActionId::SelectSkill6:
        return "SelectSkill6";
    case ActionId::SelectSkill7:
        return "SelectSkill7";
    case ActionId::SelectSkill8:
        return "SelectSkill8";
    case ActionId::FocusCommander:
        return "FocusCommander";
    case ActionId::FocusBase:
        return "FocusBase";
    case ActionId::ActivateSkill:
        return "ActivateSkill";
    case ActionId::QuitGame:
        return "QuitGame";
    case ActionId::Count:
    default:
        return "Unknown";
    }
}

char UiView::indicatorFromBool(bool value)
{
    return value ? '1' : '0';
}

std::string UiView::formatMilliseconds(double ms, int precision)
{
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << ms;
    oss << "ms";
    return oss.str();
}

void UiView::render(const DrawContext &context) const
{
    if (!m_dependencies.renderer || !m_dependencies.hudFont || !context.simulation || !context.renderStats)
    {
        return;
    }

    SDL_Renderer *renderer = m_dependencies.renderer;
    const TextRenderer &font = *m_dependencies.hudFont;
    const TextRenderer &debugFontRef = resolveDebugFont(m_dependencies);
    const bool debugFontLoaded = m_dependencies.debugFont ? m_dependencies.debugFont->isLoaded() : font.isLoaded();
    const world::LegacySimulation &sim = *context.simulation;
    const auto &queue = sim.renderQueue;
    const int screenW = m_dependencies.screenWidth;
    const int screenH = m_dependencies.screenHeight;
    RenderStats &stats = *context.renderStats;

    const int lineHeight = std::max(font.getLineHeight(), 18);
    const int debugLineHeight = std::max(debugFontLoaded ? debugFontRef.getLineHeight() : lineHeight, 14);

    const int topUiAnchorBase = 20;
    int topUiAnchor = topUiAnchorBase;

    if (sim.missionMode != MissionMode::None && sim.missionUI.showGoalText && !sim.missionUI.goalText.empty())
    {
        const int padX = 18;
        const int padY = 8;
        const int textWidth = measureWithFallback(font, sim.missionUI.goalText, lineHeight);
        SDL_Rect goalRect{screenW / 2 - (textWidth + padX * 2) / 2, topUiAnchor, textWidth + padX * 2, lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        countedRenderFillRect(renderer, &goalRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, sim.missionUI.goalText, goalRect.x + padX, goalRect.y + padY, &stats,
                      SDL_Color{230, 240, 255, 255});
        topUiAnchor = goalRect.y + goalRect.h + 10;
    }

    if (sim.missionMode != MissionMode::None && sim.missionUI.showTimer)
    {
        float timerValue = sim.missionMode == MissionMode::Survival && sim.survival.duration > 0.0f
                               ? std::max(sim.survival.duration - sim.survival.elapsed, 0.0f)
                               : sim.missionTimer;
        const std::string timerText = std::string("Time ") + formatTimer(timerValue);
        const int padX = 14;
        const int padY = 6;
        const int textWidth = measureWithFallback(font, timerText, lineHeight);
        SDL_Rect timerRect{screenW / 2 - (textWidth + padX * 2) / 2, topUiAnchor, textWidth + padX * 2, lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        countedRenderFillRect(renderer, &timerRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, timerText, timerRect.x + padX, timerRect.y + padY, &stats);
        topUiAnchor = timerRect.y + timerRect.h + 10;
    }

    if (sim.isOrderActive())
    {
        std::ostringstream orderBanner;
        orderBanner << "[号令:" << stanceDisplayName(sim.currentOrder()) << " 残り"
                    << static_cast<int>(std::ceil(sim.orderTimeRemaining())) << "s]";
        const std::string bannerText = orderBanner.str();
        const int padX = 18;
        const int padY = 8;
        const int textWidth = measureWithFallback(font, bannerText, lineHeight);
        SDL_Rect bannerRect{screenW / 2 - (textWidth + padX * 2) / 2, 12, textWidth + padX * 2, lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 190);
        countedRenderFillRect(renderer, &bannerRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, bannerText, bannerRect.x + padX, bannerRect.y + padY, &stats,
                      SDL_Color{255, 220, 120, 255});
        topUiAnchor = bannerRect.y + bannerRect.h + 12;
    }

    bool alignmentActive = false;
    std::string alignmentBanner;
    float alignmentProgress = 0.0f;
    std::size_t alignmentFollowers = 0;

    if (context.formationHud && context.formationHud->countdown.active)
    {
        alignmentActive = true;
        alignmentProgress = std::clamp(context.formationHud->countdown.progress, 0.0f, 1.0f);
        alignmentFollowers = context.formationHud->countdown.followers;
        const float secondsRemaining = std::max(context.formationHud->countdown.secondsRemaining, 0.0f);
        std::ostringstream banner;
        const std::string &label = !context.formationHud->countdown.label.empty()
                                       ? context.formationHud->countdown.label
                                       : context.formationHud->label;
        if (!label.empty())
        {
            banner << label << ' ';
        }
        banner << formatSecondsShort(secondsRemaining) << "s";
        if (alignmentFollowers > 0)
        {
            banner << " • " << alignmentFollowers << " followers";
        }
        alignmentBanner = banner.str();
    }
    else if (queue.alignment.active && queue.alignment.secondsRemaining > 0.0f)
    {
        alignmentActive = true;
        alignmentProgress = std::clamp(queue.alignment.progress, 0.0f, 1.0f);
        alignmentFollowers = queue.alignment.followers;
        std::ostringstream banner;
        if (!queue.alignment.label.empty())
        {
            banner << queue.alignment.label << ' ';
        }
        banner << formatSecondsShort(std::max(queue.alignment.secondsRemaining, 0.0f)) << "s";
        if (alignmentFollowers > 0)
        {
            banner << " • " << alignmentFollowers << " followers";
        }
        alignmentBanner = banner.str();
    }

    if (alignmentActive && !alignmentBanner.empty())
    {
        const int padX = 16;
        const int padY = 6;
        const int textWidth = measureWithFallback(font, alignmentBanner, lineHeight);
        const int extraHeight = alignmentProgress > 0.0f ? 6 : 0;
        SDL_Rect alignRect{screenW / 2 - (textWidth + padX * 2) / 2, topUiAnchor, textWidth + padX * 2,
                           lineHeight + padY * 2 + extraHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        countedRenderFillRect(renderer, &alignRect, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, alignmentBanner, alignRect.x + padX, alignRect.y + padY, &stats,
                      SDL_Color{255, 208, 144, 255});
        if (alignmentProgress > 0.0f)
        {
            const int barMargin = 10;
            SDL_Rect barBg{alignRect.x + barMargin, alignRect.y + alignRect.h - 8, alignRect.w - barMargin * 2, 4};
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 60, 40, 20, 220);
            countedRenderFillRect(renderer, &barBg, stats);
            SDL_Rect barFill{barBg.x, barBg.y,
                             static_cast<int>(std::round(barBg.w * std::clamp(alignmentProgress, 0.0f, 1.0f))), barBg.h};
            SDL_SetRenderDrawColor(renderer, 255, 208, 144, 230);
            countedRenderFillRect(renderer, &barFill, stats);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }
        topUiAnchor = alignRect.y + alignRect.h + 10;
    }

    const int baseHpInt = static_cast<int>(std::round(std::max(sim.baseHp, 0.0f)));
    const float hpRatio = sim.config.base_hp > 0 ? std::clamp(baseHpInt / static_cast<float>(sim.config.base_hp), 0.0f, 1.0f)
                                                 : 0.0f;
    SDL_Rect barBg{screenW / 2 - 160, topUiAnchor, 320, 20};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 28, 22, 40, 200);
    countedRenderFillRect(renderer, &barBg, stats);
    SDL_Rect barFill{barBg.x + 4, barBg.y + 4, static_cast<int>((barBg.w - 8) * hpRatio), barBg.h - 8};
    SDL_SetRenderDrawColor(renderer, 255, 166, 64, 230);
    countedRenderFillRect(renderer, &barFill, stats);
    SDL_SetRenderDrawColor(renderer, 90, 70, 120, 230);
    countedRenderDrawRect(renderer, &barBg, stats);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    font.drawText(renderer, "Base HP", barBg.x, barBg.y - lineHeight, &stats);
    font.drawText(renderer, std::to_string(baseHpInt), barBg.x + barBg.w + 12, barBg.y - 2, &stats);

    int infoPanelAnchor = barBg.y + barBg.h + 20;
    if (sim.missionMode == MissionMode::Boss && sim.missionUI.showBossHpBar && sim.boss.maxHp > 0.0f)
    {
        const float ratio = std::clamp(sim.boss.maxHp > 0.0f ? sim.boss.hp / sim.boss.maxHp : 0.0f, 0.0f, 1.0f);
        SDL_Rect bossBg{screenW / 2 - 200, barBg.y + barBg.h + 12, 400, 18};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 30, 10, 60, 200);
        countedRenderFillRect(renderer, &bossBg, stats);
        SDL_Rect bossFill{bossBg.x + 4, bossBg.y + 4, static_cast<int>((bossBg.w - 8) * ratio), bossBg.h - 8};
        SDL_SetRenderDrawColor(renderer, 180, 70, 200, 230);
        countedRenderFillRect(renderer, &bossFill, stats);
        SDL_SetRenderDrawColor(renderer, 110, 60, 150, 230);
        countedRenderDrawRect(renderer, &bossBg, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, "Boss HP", bossBg.x, bossBg.y - lineHeight, &stats);
        infoPanelAnchor = bossBg.y + bossBg.h + 20;
    }

    int hudLeftAnchor = infoPanelAnchor;
    if (context.moraleHud)
    {
        const auto &summary = context.moraleHud->summary;
        struct TextEntry
        {
            std::string text;
            SDL_Color color;
            bool bullet;
        };
        std::vector<TextEntry> moraleLines;
        moraleLines.push_back({std::string("Commander: ") + moraleDisplayName(summary.commanderState),
                               moraleColorForState(summary.commanderState), true});
        moraleLines.push_back({"Leader down: " + formatSecondsShort(summary.leaderDownTimer) + "s",
                               moraleColorForState(MoraleState::LeaderDown), true});
        moraleLines.push_back({"Barrier: " + formatSecondsShort(summary.commanderBarrierTimer) + "s",
                               moraleColorForState(MoraleState::Shielded), true});
        moraleLines.push_back({"Panic: " + std::to_string(summary.panicCount),
                               moraleColorForState(MoraleState::Panic), true});
        moraleLines.push_back({"Mesomeso: " + std::to_string(summary.mesomesoCount),
                               moraleColorForState(MoraleState::Mesomeso), true});

        int moraleWidth = 0;
        for (const auto &entry : moraleLines)
        {
            int width = measureWithFallback(font, entry.text, lineHeight);
            if (entry.bullet && entry.color.a > 0 && !entry.text.empty())
            {
                width += 12;
            }
            moraleWidth = std::max(moraleWidth, width);
        }
        const int padX = 12;
        const int padY = 8;
        SDL_Rect panel{12, hudLeftAnchor, moraleWidth + padX * 2,
                       static_cast<int>(moraleLines.size()) * lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        int lineY = panel.y + padY;
        for (const auto &entry : moraleLines)
        {
            int textX = panel.x + padX;
            if (entry.bullet && entry.color.a > 0 && !entry.text.empty())
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, entry.color.r, entry.color.g, entry.color.b, entry.color.a);
                Vec2 bulletCenter{static_cast<float>(textX + 6), static_cast<float>(lineY + lineHeight / 2)};
                drawFilledCircle(renderer, bulletCenter, 4.0f, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                textX += 12;
            }
            font.drawText(renderer, entry.text, textX, lineY, &stats);
            lineY += lineHeight;
        }
        hudLeftAnchor = panel.y + panel.h + 12;
    }

    if (context.jobHud && (!context.jobHud->jobs.empty() || !context.jobHud->skills.empty()))
    {
        struct TextEntry
        {
            std::string text;
            SDL_Color color;
            bool bullet;
        };
        std::vector<TextEntry> jobLines;
        for (const JobHudEntryStatus &entry : context.jobHud->jobs)
        {
            std::ostringstream line;
            line << jobDisplayName(entry.job) << ": " << entry.ready << '/' << entry.total;
            if (entry.maxCooldown > 0.05f)
            {
                line << " CD " << formatSecondsShort(entry.maxCooldown) << 's';
            }
            if (entry.maxEndlag > 0.05f)
            {
                line << " EL " << formatSecondsShort(entry.maxEndlag) << 's';
            }
            if (entry.specialActive)
            {
                line << ' ' << jobSpecialLabel(entry.job);
                if (entry.specialTimer > 0.05f)
                {
                    line << ' ' << formatSecondsShort(entry.specialTimer) << 's';
                }
            }
            jobLines.push_back({line.str(), jobRingColor(entry.job), true});
        }
        if (!context.jobHud->skills.empty())
        {
            jobLines.push_back({std::string(), SDL_Color{0, 0, 0, 0}, false});
            for (const JobHudSkillStatus &skill : context.jobHud->skills)
            {
                std::ostringstream line;
                line << skill.label;
                if (skill.toggled)
                {
                    line << " [ON]";
                }
                if (skill.cooldownRemaining > 0.05f)
                {
                    line << " CD " << formatSecondsShort(skill.cooldownRemaining) << 's';
                }
                if (skill.activeTimer > 0.05f)
                {
                    line << " Active " << formatSecondsShort(skill.activeTimer) << 's';
                }
                jobLines.push_back({line.str(), SDL_Color{210, 220, 255, 255}, false});
            }
        }

        int jobWidth = 0;
        for (const auto &entry : jobLines)
        {
            jobWidth = std::max(jobWidth, measureWithFallback(font, entry.text, lineHeight));
        }
        const int padX = 12;
        const int padY = 8;
        SDL_Rect panel{12, hudLeftAnchor, jobWidth + padX * 2,
                       static_cast<int>(jobLines.size()) * lineHeight + padY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
        countedRenderFillRect(renderer, &panel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        int lineY = panel.y + padY;
        for (const auto &entry : jobLines)
        {
            if (entry.text.empty())
            {
                lineY += lineHeight;
                continue;
            }
            int textX = panel.x + padX;
            if (entry.bullet && entry.color.a > 0)
            {
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                SDL_SetRenderDrawColor(renderer, entry.color.r, entry.color.g, entry.color.b, entry.color.a);
                Vec2 bulletCenter{static_cast<float>(textX + 6), static_cast<float>(lineY + lineHeight / 2)};
                drawFilledCircle(renderer, bulletCenter, 4.0f, stats);
                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
                textX += 12;
            }
            font.drawText(renderer, entry.text, textX, lineY, &stats);
            lineY += lineHeight;
        }
        hudLeftAnchor = panel.y + panel.h + 12;
    }

    infoPanelAnchor = std::max(infoPanelAnchor, hudLeftAnchor);

    const int commanderHpInt = static_cast<int>(std::round(std::max(sim.commander.hp, 0.0f)));
    std::vector<std::string> infoLines;
    infoLines.push_back("Allies: " + std::to_string(static_cast<int>(sim.yunas.size())));
    if (sim.commander.alive)
    {
        infoLines.push_back("Commander HP: " + std::to_string(commanderHpInt));
    }
    else
    {
        infoLines.push_back("Commander: Down");
    }
    infoLines.push_back("Enemies: " + std::to_string(static_cast<int>(sim.enemies.size())));
    if (sim.missionMode == MissionMode::Boss && sim.boss.maxHp > 0.0f)
    {
        std::ostringstream bossLine;
        bossLine << "Boss HP: " << static_cast<int>(std::round(std::max(sim.boss.hp, 0.0f))) << " / "
                 << static_cast<int>(std::round(sim.boss.maxHp));
        infoLines.push_back(bossLine.str());
    }
    if (sim.missionMode == MissionMode::Capture)
    {
        const int goal = sim.captureGoal > 0 ? sim.captureGoal : static_cast<int>(sim.captureZones.size());
        std::ostringstream captureLine;
        captureLine << "Capture: " << sim.capturedZones << '/' << goal;
        infoLines.push_back(captureLine.str());
    }
    if (sim.missionMode == MissionMode::Survival)
    {
        std::ostringstream survivalLine;
        survivalLine << std::fixed << std::setprecision(2) << "Pace x" << std::max(sim.survival.spawnMultiplier, 1.0f);
        infoLines.push_back(survivalLine.str());
    }
    if (!sim.commander.alive)
    {
        std::ostringstream respawnText;
        respawnText << "Commander respawn in " << std::fixed << std::setprecision(1) << sim.commanderRespawnTimer << "s";
        infoLines.push_back(respawnText.str());
    }
    infoLines.push_back("");
    std::ostringstream orderLine;
    orderLine << "Order (F1-F4): ";
    if (sim.isOrderActive())
    {
        orderLine << stanceDisplayName(sim.currentOrder()) << " [" << std::fixed << std::setprecision(1) << sim.orderTimeRemaining()
                  << "s]";
    }
    else
    {
        orderLine << "None (default " << stanceDisplayName(sim.defaultStance) << ")";
    }
    infoLines.push_back(orderLine.str());
    infoLines.push_back(std::string("Formation (Z/X): ") + formationLabel(sim.formation));
    infoLines.push_back("");
    infoLines.push_back("Skills (Right Click):");
    for (std::size_t i = 0; i < sim.skills.size(); ++i)
    {
        const RuntimeSkill &skill = sim.skills[i];
        std::ostringstream skillLabel;
        skillLabel << (static_cast<int>(i) == sim.selectedSkill ? "> " : "  ");
        skillLabel << skill.def.hotkey << ": " << skill.def.displayName;
        if (skill.cooldownRemaining > 0.0f)
        {
            skillLabel << " [" << std::fixed << std::setprecision(1) << skill.cooldownRemaining << "s]";
        }
        else if (skill.def.type == SkillType::SpawnRate && skill.activeTimer > 0.0f)
        {
            skillLabel << " (active " << std::fixed << std::setprecision(1) << skill.activeTimer << "s)";
        }
        infoLines.push_back(skillLabel.str());
    }

    if (!infoLines.empty())
    {
        int infoPanelWidth = 0;
        for (const std::string &line : infoLines)
        {
            infoPanelWidth = std::max(infoPanelWidth, measureWithFallback(font, line, lineHeight));
        }
        const int infoPadding = 8;
        const int infoPanelHeight = static_cast<int>(infoLines.size()) * lineHeight + infoPadding * 2;
        SDL_Rect infoPanel{12, infoPanelAnchor, infoPanelWidth + infoPadding * 2, infoPanelHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 160);
        countedRenderFillRect(renderer, &infoPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        int infoY = infoPanel.y + infoPadding;
        for (const std::string &line : infoLines)
        {
            if (!line.empty())
            {
                font.drawText(renderer, line, infoPanel.x + infoPadding, infoY, &stats);
            }
            infoY += lineHeight;
        }
    }

    int topRightAnchorY = sim.isOrderActive() ? topUiAnchor : 12;
    if (context.showDebugHud && context.framePerf)
    {
        const FramePerf &perf = *context.framePerf;
        std::vector<std::string> perfLines;
        std::ostringstream line1;
        line1 << std::fixed << std::setprecision(1) << "FPS " << perf.fps << "  Ents " << perf.entities;
        perfLines.push_back(line1.str());
        std::ostringstream line2;
        line2 << std::fixed << std::setprecision(2) << "Upd " << perf.msUpdate << "ms  Ren " << perf.msRender << "ms";
        perfLines.push_back(line2.str());
        std::ostringstream line3;
        line3 << std::fixed << std::setprecision(2) << "In " << perf.msInput << "ms  Hud " << perf.msHud << "ms";
        perfLines.push_back(line3.str());
        std::ostringstream line4;
        line4 << "Draw " << perf.drawCalls;
        perfLines.push_back(line4.str());
        std::ostringstream line5;
        line5 << "Events lost " << sim.hud.unconsumedEvents;
        perfLines.push_back(line5.str());
        if (perf.budgetExceeded)
        {
            std::ostringstream warn;
            warn << "Budget " << perf.budgetStage << ' ' << std::fixed << std::setprecision(2) << perf.budgetSampleMs
                 << "ms > " << perf.budgetTargetMs << "ms";
            perfLines.push_back(warn.str());
        }

        int debugWidth = 0;
        for (const std::string &line : perfLines)
        {
            debugWidth = std::max(debugWidth, measureWithFallback(debugFontRef, line, debugLineHeight));
        }
        const int debugPadX = 10;
        const int debugPadY = 8;
        SDL_Rect debugPanel{screenW - (debugWidth + debugPadX * 2) - 12, topRightAnchorY,
                            debugWidth + debugPadX * 2, static_cast<int>(perfLines.size()) * debugLineHeight + debugPadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        countedRenderFillRect(renderer, &debugPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        int debugY = debugPanel.y + debugPadY;
        for (const std::string &line : perfLines)
        {
            debugFontRef.drawText(renderer, line, debugPanel.x + debugPadX, debugY, &stats);
            debugY += debugLineHeight;
        }
        topRightAnchorY += debugPanel.h + 12;
    }

    if (context.showDebugHud && context.inputDiagnostics)
    {
        const DrawContext::InputDiagnosticsState &diag = *context.inputDiagnostics;
        std::vector<std::string> diagLines;
        {
            std::ostringstream line;
            line << "Input buffer " << diag.bufferedFrames << '/' << diag.bufferCapacity;
            if (diag.configuredBufferFrames > 0)
            {
                line << " (cfg " << diag.configuredBufferFrames;
                if (diag.bufferExpiryMs > 0.0)
                {
                    line << ", exp " << formatMilliseconds(diag.bufferExpiryMs);
                }
                line << ')';
            }
            diagLines.push_back(line.str());
        }
        if (diag.hasLatestFrame)
        {
            std::ostringstream frameLine;
            frameLine << "Latest frame #" << diag.latestSequence << " @"
                      << formatMilliseconds(diag.latestDeviceTimestampMs);
            diagLines.push_back(frameLine.str());
        }
        if (diag.hasPointerState)
        {
            std::ostringstream ptrLine;
            ptrLine << "Pointer ";
            if (diag.pointerState.hasPosition)
            {
                ptrLine << diag.pointerState.x << ',' << diag.pointerState.y;
            }
            else
            {
                ptrLine << "(none)";
            }
            ptrLine << " L" << indicatorFromBool(diag.pointerState.left);
            ptrLine << " R" << indicatorFromBool(diag.pointerState.right);
            ptrLine << " M" << indicatorFromBool(diag.pointerState.middle);
            diagLines.push_back(ptrLine.str());
        }
        for (const auto &evt : diag.latestEvents)
        {
            std::ostringstream evtLine;
            evtLine << "- " << actionDisplayName(evt.id);
            if (std::fabs(evt.value) > 0.001f)
            {
                evtLine << " value " << std::fixed << std::setprecision(2) << evt.value;
            }
            if (evt.pressed)
            {
                evtLine << " pressed";
            }
            if (evt.released)
            {
                evtLine << " released";
            }
            if (evt.hasPointer)
            {
                evtLine << " ptr " << evt.pointerX << ',' << evt.pointerY;
                if (evt.pointerPressed)
                {
                    evtLine << " down";
                }
                if (evt.pointerReleased)
                {
                    evtLine << " up";
                }
            }
            diagLines.push_back(evtLine.str());
        }

        int diagWidth = 0;
        for (const std::string &line : diagLines)
        {
            diagWidth = std::max(diagWidth, measureWithFallback(debugFontRef, line, debugLineHeight));
        }
        const int diagPadX = 10;
        const int diagPadY = 8;
        SDL_Rect diagPanel{screenW - (diagWidth + diagPadX * 2) - 12, topRightAnchorY,
                           diagWidth + diagPadX * 2,
                           static_cast<int>(diagLines.size()) * debugLineHeight + diagPadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 170);
        countedRenderFillRect(renderer, &diagPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        int diagY = diagPanel.y + diagPadY;
        for (const std::string &line : diagLines)
        {
            debugFontRef.drawText(renderer, line, diagPanel.x + diagPadX, diagY, &stats);
            diagY += debugLineHeight;
        }
        topRightAnchorY += diagPanel.h + 12;
    }

    if (!queue.performanceWarningText.empty() && queue.performanceWarningTimer > 0.0f)
    {
        const int warnPadX = 12;
        const int warnPadY = 6;
        const int textWidth = measureWithFallback(font, queue.performanceWarningText, lineHeight);
        SDL_Rect warnPanel{screenW - (textWidth + warnPadX * 2) - 12, topRightAnchorY,
                           textWidth + warnPadX * 2, lineHeight + warnPadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 140, 30, 30, 210);
        countedRenderFillRect(renderer, &warnPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        const SDL_Color warnColor{255, 220, 220, 255};
        font.drawText(renderer, queue.performanceWarningText, warnPanel.x + warnPadX, warnPanel.y + warnPadY, &stats, warnColor);
        topRightAnchorY += warnPanel.h + 12;
    }
    if (!queue.spawnWarningText.empty() && queue.spawnWarningTimer > 0.0f)
    {
        const int warnPadX = 12;
        const int warnPadY = 6;
        const int textWidth = measureWithFallback(font, queue.spawnWarningText, lineHeight);
        SDL_Rect warnPanel{screenW - (textWidth + warnPadX * 2) - 12, topRightAnchorY,
                           textWidth + warnPadX * 2, lineHeight + warnPadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 200, 120, 30, 210);
        countedRenderFillRect(renderer, &warnPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        const SDL_Color warnColor{255, 240, 210, 255};
        font.drawText(renderer, queue.spawnWarningText, warnPanel.x + warnPadX, warnPanel.y + warnPadY, &stats, warnColor);
        topRightAnchorY += warnPanel.h + 12;
    }
    if (!queue.telemetryText.empty() && queue.telemetryTimer > 0.0f)
    {
        const int telePadX = 12;
        const int telePadY = 6;
        const int textWidth = measureWithFallback(font, queue.telemetryText, lineHeight);
        SDL_Rect telePanel{screenW - (textWidth + telePadX * 2) - 12, topRightAnchorY,
                           textWidth + telePadX * 2, lineHeight + telePadY * 2};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
        countedRenderFillRect(renderer, &telePanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, queue.telemetryText, telePanel.x + telePadX, telePanel.y + telePadY, &stats);
        topRightAnchorY += telePanel.h + 12;
    }

    if (!sim.hud.resultText.empty() && sim.hud.resultTimer > 0.0f)
    {
        const bool showRestartHint = sim.restartCooldown <= 0.0f;
        const std::string restartHintText = "Rキーで再挑戦";
        const int resultPadX = 24;
        const int resultPadY = 12;
        const int hintSpacing = 6;
        int contentWidth = measureWithFallback(font, sim.hud.resultText, lineHeight);
        if (showRestartHint)
        {
            contentWidth = std::max(contentWidth, measureWithFallback(font, restartHintText, lineHeight));
        }
        int panelHeight = lineHeight + resultPadY * 2;
        if (showRestartHint)
        {
            panelHeight += hintSpacing + lineHeight;
        }
        SDL_Rect resultPanel{screenW / 2 - (contentWidth + resultPadX * 2) / 2,
                             screenH / 2 - lineHeight - resultPadY,
                             contentWidth + resultPadX * 2,
                             panelHeight};
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
        countedRenderFillRect(renderer, &resultPanel, stats);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        font.drawText(renderer, sim.hud.resultText, resultPanel.x + resultPadX, resultPanel.y + resultPadY, &stats);
        if (showRestartHint)
        {
            const int hintY = resultPanel.y + resultPadY + lineHeight + hintSpacing;
            const SDL_Color hintColor{235, 235, 235, 255};
            font.drawText(renderer, restartHintText, resultPanel.x + resultPadX, hintY, &stats, hintColor);
        }
    }
}

int UiView::measureWithFallback(const TextRenderer &renderer, const std::string &text, int approxHeight)
{
    const int measured = renderer.measureText(text);
    if (measured > 0)
    {
        return measured;
    }
    const int approxWidth = std::max(approxHeight / 2, 8);
    return static_cast<int>(text.size()) * approxWidth;
}

SDL_Color UiView::moraleColorForState(MoraleState state)
{
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
}

std::string UiView::moraleDisplayName(MoraleState state)
{
    std::string name{moraleStateLabel(state)};
    if (!name.empty())
    {
        name[0] = static_cast<char>(std::toupper(name[0]));
        for (std::size_t i = 1; i < name.size(); ++i)
        {
            if (name[i] == '_')
            {
                name[i] = ' ';
            }
        }
    }
    return name;
}

SDL_Color UiView::jobRingColor(UnitJob job)
{
    switch (job)
    {
    case UnitJob::Warrior: return SDL_Color{220, 80, 80, 255};
    case UnitJob::Archer: return SDL_Color{80, 200, 120, 255};
    case UnitJob::Shield: return SDL_Color{70, 130, 230, 255};
    }
    return SDL_Color{200, 200, 200, 255};
}

const char *UiView::jobDisplayName(UnitJob job)
{
    switch (job)
    {
    case UnitJob::Warrior: return "Warrior";
    case UnitJob::Archer: return "Archer";
    case UnitJob::Shield: return "Shield";
    }
    return "Job";
}

const char *UiView::jobSpecialLabel(UnitJob job)
{
    switch (job)
    {
    case UnitJob::Warrior: return "Stumble";
    case UnitJob::Archer: return "Focus";
    case UnitJob::Shield: return "Guard";
    }
    return "Special";
}

std::string UiView::formatSecondsShort(float seconds)
{
    std::ostringstream oss;
    seconds = std::max(seconds, 0.0f);
    if (seconds >= 10.0f)
    {
        oss << static_cast<int>(std::round(seconds));
    }
    else
    {
        oss << std::fixed << std::setprecision(1) << seconds;
    }
    return oss.str();
}

std::string UiView::formatTimer(float seconds)
{
    seconds = std::max(seconds, 0.0f);
    int total = static_cast<int>(seconds + 0.5f);
    int minutes = total / 60;
    int secs = total % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << minutes << ':' << std::setw(2) << secs;
    return oss.str();
}

const char *UiView::stanceDisplayName(ArmyStance stance)
{
    switch (stance)
    {
    case ArmyStance::RushNearest: return "Rush Nearest";
    case ArmyStance::PushForward: return "Push Forward";
    case ArmyStance::FollowLeader: return "Follow Leader";
    case ArmyStance::DefendBase: return "Defend Base";
    }
    return "Unknown";
}
