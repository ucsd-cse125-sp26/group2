/// @file ChatWidget.cpp
/// @brief Bottom-left all-chat widget.

#include "ChatWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>

ChatWidget::ChatWidget()
{
    anchor = HudAnchor::BottomLeft;
    offsetX = 24.f;
    offsetY = -196.f;
    width = 520.f;
    height = 172.f;
}

void ChatWidget::update(float /*dt*/, const HudGameState& state, HudTweenPool& /*tweens*/)
{
    chat_ = state.chat;
    speakers_ = state.voiceSpeakers;
    visible = chat_.open || !chat_.messages.empty() || !speakers_.empty();
}

void ChatWidget::draw(HudContext& ctx, float x, float y)
{
    using namespace voidfall;

    if (!visible)
        return;

    const float s = uiScale_;
    const float w = width * s;
    const float lineH = 18.f * s;
    const float font = 12.f * s;
    const float pad = 10.f * s;
    const int maxLines = chat_.open ? 8 : 4;
    const float inputH = chat_.open ? 26.f * s : 0.f;
    const float panelH = pad * 2.f + lineH * static_cast<float>(maxLines) + inputH;

    const HudColor panel = chat_.open ? HudColor{0.08f, 0.075f, 0.07f, 0.72f} : HudColor{0.08f, 0.075f, 0.07f, 0.28f};
    ctx.rect(x, y, w, panelH, panel);
    if (chat_.open)
        ctx.rectOutline(x, y, w, panelH, 1.f, k_lineDim);

    const auto& messages = chat_.messages;
    int visibleMessages = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && visibleMessages < maxLines; ++it) {
        if (!chat_.open && it->ageSeconds > 7.0f)
            continue;
        ++visibleMessages;
    }

    float cursorY = y + pad + (static_cast<float>(maxLines - visibleMessages) * lineH);
    int drawn = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && drawn < visibleMessages; ++it) {
        if (!chat_.open && it->ageSeconds > 7.0f)
            continue;

        const float fade = chat_.open ? 1.0f : std::clamp(1.0f - (it->ageSeconds - 5.5f) / 1.5f, 0.0f, 1.0f);
        const HudColor nameColor = it->fromLocal ? withAlpha(k_cyan, fade) : withAlpha(k_amber, fade);
        const HudColor textColor = withAlpha(k_text, fade);

        ctx.pushClipRect(x + pad, cursorY - 1.f * s, w - pad * 2.f, lineH + 2.f * s);
        const float nameW = ctx.measureText(it->senderName.c_str(), font);
        ctx.text(it->senderName.c_str(), x + pad, cursorY, font, nameColor, HudAlign::Left);
        ctx.text(":", x + pad + nameW, cursorY, font, withAlpha(k_textDim, fade), HudAlign::Left);
        ctx.text(it->message.c_str(), x + pad + nameW + 8.f * s, cursorY, font, textColor, HudAlign::Left);
        ctx.popClipRect();

        cursorY += lineH;
        ++drawn;
    }

    if (!speakers_.empty()) {
        float speakerY = y - (static_cast<float>(speakers_.size()) * 18.f + 8.f) * s;
        int count = 0;
        for (const auto& speaker : speakers_) {
            if (count++ >= 3)
                break;
            ctx.text("VOICE", x + pad, speakerY, 10.f * s, withAlpha(k_cyan, 0.85f), HudAlign::Left);
            ctx.text(speaker.senderName.c_str(),
                     x + pad + 46.f * s,
                     speakerY,
                     11.f * s,
                     withAlpha(k_textBright, 0.9f),
                     HudAlign::Left);
            speakerY += 18.f * s;
        }
    }

    if (!chat_.open)
        return;

    const float inputY = y + panelH - inputH - pad * 0.35f;
    ctx.rect(x + pad, inputY, w - pad * 2.f, inputH, HudColor{0.12f, 0.115f, 0.105f, 0.86f});
    ctx.rectOutline(x + pad, inputY, w - pad * 2.f, inputH, 1.f, k_lineDim);

    std::string draft = "> " + chat_.draft;
    if (static_cast<int>(SDL_GetTicks() / 450u) % 2 == 0)
        draft.push_back('_');
    ctx.pushClipRect(x + pad * 1.6f, inputY + 4.f * s, w - pad * 3.2f, inputH - 8.f * s);
    ctx.text(draft.c_str(), x + pad * 1.6f, inputY + 4.f * s, font, k_textBright, HudAlign::Left);
    ctx.popClipRect();
}
