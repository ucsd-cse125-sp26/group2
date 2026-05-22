/// @file ChatWidget.cpp
/// @brief Bottom-left all-chat widget.

#include "ChatWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace
{

void trimLastUtf8Codepoint(std::string& text)
{
    if (text.empty())
        return;
    std::size_t firstByte = text.size() - 1;
    while (firstByte > 0 && (static_cast<unsigned char>(text[firstByte]) & 0xc0u) == 0x80u)
        --firstByte;
    text.erase(firstByte);
}

std::string elideToWidth(HudContext& ctx, std::string_view text, float fontSize, float maxWidth)
{
    std::string out(text);
    if (ctx.measureText(out.c_str(), fontSize) <= maxWidth)
        return out;

    constexpr std::string_view ellipsis = "...";
    const float ellipsisW = ctx.measureText(ellipsis.data(), fontSize);
    while (!out.empty() && ctx.measureText(out.c_str(), fontSize) + ellipsisW > maxWidth)
        trimLastUtf8Codepoint(out);
    out += ellipsis;
    return out;
}

} // namespace

ChatWidget::ChatWidget()
{
    anchor = HudAnchor::CenterLeft;
    offsetX = 45.f;
    offsetY = 0.f;
    width = 400.f;
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
    const float panelY = y - panelH;

    const HudColor panel = chat_.open ? HudColor{0.08f, 0.075f, 0.07f, 0.72f} : HudColor{0.08f, 0.075f, 0.07f, 0.28f};
    ctx.rect(x, panelY, w, panelH, panel);
    if (chat_.open)
        ctx.rectOutline(x, panelY, w, panelH, 1.f, k_lineDim);

    const auto& messages = chat_.messages;
    int visibleMessages = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && visibleMessages < maxLines; ++it) {
        if (!chat_.open && it->ageSeconds > 7.0f)
            continue;
        ++visibleMessages;
    }

    float cursorY = panelY + pad + (static_cast<float>(maxLines - visibleMessages) * lineH);
    int drawn = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && drawn < visibleMessages; ++it) {
        if (!chat_.open && it->ageSeconds > 7.0f)
            continue;

        const float fade = chat_.open ? 1.0f : std::clamp(1.0f - (it->ageSeconds - 5.5f) / 1.5f, 0.0f, 1.0f);
        const HudColor nameColor = it->fromLocal ? withAlpha(k_cyan, fade) : withAlpha(k_amber, fade);
        const HudColor textColor = withAlpha(k_text, fade);

        const float nameW = ctx.measureText(it->senderName.c_str(), font);
        const float messageX = x + pad + nameW + 8.f * s;
        const std::string message = elideToWidth(ctx, it->message, font, x + w - pad - messageX);
        ctx.text(it->senderName.c_str(), x + pad, cursorY, font, nameColor, HudAlign::Left);
        ctx.text(":", x + pad + nameW, cursorY, font, withAlpha(k_textDim, fade), HudAlign::Left);
        ctx.text(message.c_str(), messageX, cursorY, font, textColor, HudAlign::Left);

        cursorY += lineH;
        ++drawn;
    }

    if (!speakers_.empty()) {
        float speakerY = panelY - (static_cast<float>(speakers_.size()) * 18.f + 8.f) * s;
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

    const float inputY = panelY + panelH - inputH - pad * 0.35f;
    ctx.rect(x + pad, inputY, w - pad * 2.f, inputH, HudColor{0.12f, 0.115f, 0.105f, 0.86f});
    ctx.rectOutline(x + pad, inputY, w - pad * 2.f, inputH, 1.f, k_lineDim);

    std::string draft = "> " + chat_.draft;
    if (static_cast<int>(SDL_GetTicks() / 450u) % 2 == 0)
        draft.push_back('_');
    const float draftX = x + pad * 1.6f;
    draft = elideToWidth(ctx, draft, font, x + w - pad * 1.6f - draftX);
    ctx.text(draft.c_str(), draftX, inputY + 4.f * s, font, k_textBright, HudAlign::Left);
}
