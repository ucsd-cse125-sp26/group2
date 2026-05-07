/// @file HudDebugPanel.cpp
#include "HudDebugPanel.hpp"

#include "hud/Hud.hpp"
#include "hud/widgets/AmmoCounter.hpp"
#include "hud/widgets/CrosshairWidget.hpp"
#include "hud/widgets/HealthArmorBar.hpp"
#include "hud/widgets/KillFeed.hpp"
#include "hud/widgets/Minimap.hpp"
#include "hud/widgets/RoundTimer.hpp"

#include <imgui.h>

namespace
{

const char* anchorName(HudAnchor a)
{
    switch (a) {
    case HudAnchor::TopLeft:
        return "TopLeft";
    case HudAnchor::TopCenter:
        return "TopCenter";
    case HudAnchor::TopRight:
        return "TopRight";
    case HudAnchor::CenterLeft:
        return "CenterLeft";
    case HudAnchor::Center:
        return "Center";
    case HudAnchor::CenterRight:
        return "CenterRight";
    case HudAnchor::BottomLeft:
        return "BottomLeft";
    case HudAnchor::BottomCenter:
        return "BottomCenter";
    case HudAnchor::BottomRight:
        return "BottomRight";
    }
    return "?";
}

} // namespace

void HudDebugPanel::build(Hud& hud, bool* open)
{
    if (!*open)
        return;

    if (!ImGui::Begin("HUD Tweaker", open)) {
        ImGui::End();
        return;
    }

    for (auto& w : hud.widgets()) {
        ImGui::PushID(w.get());

        // Widget header: type name + visibility toggle.
        bool vis = w->visible;
        ImGui::Checkbox("##vis", &vis);
        w->visible = vis;
        ImGui::SameLine();

        if (ImGui::TreeNode("Widget")) {
            ImGui::Text("Anchor: %s", anchorName(w->anchor));
            ImGui::DragFloat("Offset X", &w->offsetX, 1.f, -2000.f, 2000.f);
            ImGui::DragFloat("Offset Y", &w->offsetY, 1.f, -2000.f, 2000.f);

            // Widget-specific controls.
            if (auto* ch = dynamic_cast<CrosshairWidget*>(w.get())) {
                ImGui::DragFloat("Gap", &ch->style.gap, 0.5f, 0.f, 50.f);
                ImGui::DragFloat("Length", &ch->style.length, 0.5f, 1.f, 50.f);
                ImGui::DragFloat("Thickness", &ch->style.thickness, 0.5f, 0.5f, 10.f);
                ImGui::ColorEdit4("Color", &ch->style.color.r);
                ImGui::Checkbox("Dot", &ch->style.dot);
            } else if (auto* hp = dynamic_cast<HealthArmorBar*>(w.get())) {
                ImGui::DragFloat("Panel Width", &hp->panelWidth, 1.f, 50.f, 500.f);
                ImGui::DragFloat("HP Bar Height", &hp->healthBarHeight, 0.5f, 4.f, 50.f);
                ImGui::DragFloat("Shield Bar Height", &hp->shieldBarHeight, 0.5f, 2.f, 24.f);
                ImGui::DragInt("Shield Segments", &hp->shieldSegments, 1, 1, 8);
                ImGui::DragFloat("Chamfer Size", &hp->chamferSize, 0.5f, 0.f, 40.f);
            } else if (auto* ac = dynamic_cast<AmmoCounter*>(w.get())) {
                ImGui::DragFloat("Clip Font", &ac->clipFontSize, 0.5f, 12.f, 64.f);
                ImGui::DragFloat("Reserve Font", &ac->reserveFontSize, 0.5f, 8.f, 48.f);
            } else if (auto* kf = dynamic_cast<KillFeed*>(w.get())) {
                ImGui::DragFloat("Lifetime", &kf->entryLifetime, 0.1f, 1.f, 15.f);
                ImGui::DragFloat("Font Size", &kf->fontSize, 0.5f, 8.f, 24.f);
                ImGui::DragInt("Max Entries", &kf->maxEntries, 1, 1, 20);
            } else if (auto* rt = dynamic_cast<RoundTimer*>(w.get())) {
                ImGui::DragFloat("Font Size", &rt->fontSize, 0.5f, 12.f, 48.f);
                ImGui::DragFloat("Low Time Threshold", &rt->lowTimeThreshold, 0.5f, 1.f, 60.f);
            } else if (auto* mm = dynamic_cast<Minimap*>(w.get())) {
                ImGui::DragFloat("Map Size", &mm->mapSize, 1.f, 50.f, 400.f);
                ImGui::DragFloat("Dot Size", &mm->dotSize, 0.5f, 1.f, 12.f);
            }

            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    ImGui::End();
}
