/// @file ChatWidget.hpp
/// @brief Bottom-left all-chat log and input row.

#pragma once

#include "hud/HudWidget.hpp"

struct ChatWidget : HudWidget
{
    ChatWidget();
    void update(float dt, const HudGameState& state, HudTweenPool& tweens) override;
    void draw(HudContext& ctx, float drawX, float drawY) override;

private:
    HudChatState chat_{};
    std::span<const HudVoiceSpeaker> speakers_{};
};
