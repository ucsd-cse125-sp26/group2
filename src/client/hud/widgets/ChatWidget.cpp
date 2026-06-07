/// @file ChatWidget.cpp
/// @brief Bottom-left all-chat widget.

#include "ChatWidget.hpp"

#include "hud/HudContext.hpp"
#include "hud/VoidfallStyle.hpp"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

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
    width = 346.f;
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
    const float lineH = 22.f * s;
    const float font = 16.f * s;
    const float pad = 10.f * s;
    const int maxLines = chat_.open ? 8 : 4;
    const float inputH = chat_.open ? 32.f * s : 0.f;
    const float panelH = pad * 2.f + lineH * static_cast<float>(maxLines) + inputH;
    const float panelY = y - panelH;

    const HudColor panel = chat_.open ? withAlpha(k_quaternary, 0.72f) : withAlpha(k_quaternary, 0.28f);
    ctx.rect(x, panelY, w, panelH, panel);
    if (chat_.open)
        ctx.rectOutline(x, panelY, w, panelH, 1.f, k_lineDim);

    const auto& messages = chat_.messages;
    std::vector<const HudChatMessage*> visibleMessages;
    visibleMessages.reserve(static_cast<std::size_t>(maxLines));
    for (const auto& message : messages) {
        if (!chat_.open && message.ageSeconds > 7.0f)
            continue;
        visibleMessages.push_back(&message);
        if (static_cast<int>(visibleMessages.size()) > maxLines)
            visibleMessages.erase(visibleMessages.begin());
    }

    const int visibleCount = static_cast<int>(visibleMessages.size());
    float cursorY = panelY + pad + (static_cast<float>(maxLines - visibleCount) * lineH);
    for (const HudChatMessage* messageEntry : visibleMessages) {
        const HudChatMessage& messageData = *messageEntry;
        const float fade =
            chat_.open ? 1.0f : std::clamp(1.0f - (messageData.ageSeconds - 5.5f) / 1.5f, 0.0f, 1.0f);
        const HudColor nameColor = messageData.fromLocal ? withAlpha(k_cyan, fade) : withAlpha(k_amber, fade);
        const HudColor textColor = withAlpha(k_text, fade);

        const float nameW = ctx.measureText(messageData.senderName.c_str(), font);
        const float messageX = x + pad + nameW + 8.f * s;
        const std::string message = elideToWidth(ctx, messageData.message, font, x + w - pad - messageX);
        ctx.text(messageData.senderName.c_str(), x + pad, cursorY, font, nameColor, HudAlign::Left);
        ctx.text(":", x + pad + nameW, cursorY, font, withAlpha(k_textDim, fade), HudAlign::Left);
        ctx.text(message.c_str(), messageX, cursorY, font, textColor, HudAlign::Left);

        cursorY += lineH;
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
    ctx.rect(x + pad, inputY, w - pad * 2.f, inputH, withAlpha(k_quaternary, 0.86f));
    ctx.rectOutline(x + pad, inputY, w - pad * 2.f, inputH, 1.f, k_lineDim);

    std::string draft = "> " + chat_.draft;
    if (static_cast<int>(SDL_GetTicks() / 450u) % 2 == 0)
        draft.push_back('_');
    const float draftX = x + pad * 1.6f;
    draft = elideToWidth(ctx, draft, font, x + w - pad * 1.6f - draftX);
    ctx.text(draft.c_str(), draftX, inputY + 4.f * s, font, k_textBright, HudAlign::Left);
}
