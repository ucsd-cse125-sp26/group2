/// @file HudDebugPanel.cpp
#include "HudDebugPanel.hpp"

#include "hud/Hud.hpp"
#include "hud/widgets/AbilitySelectionWidget.hpp"
#include "hud/widgets/AmmoCounter.hpp"
#include "hud/widgets/BuyMenu.hpp"
#include "hud/widgets/ChatWidget.hpp"
#include "hud/widgets/CrosshairWidget.hpp"
#include "hud/widgets/DamageAccumWidget.hpp"
#include "hud/widgets/DamageIndicator.hpp"
#include "hud/widgets/DamageNumberWidget.hpp"
#include "hud/widgets/EnemyWorldHealthBar.hpp"
#include "hud/widgets/EquipmentSlots.hpp"
#include "hud/widgets/GravityIndicator.hpp"
#include "hud/widgets/GrenadeSlotsWidget.hpp"
#include "hud/widgets/HealthArmorBar.hpp"
#include "hud/widgets/HitMarkerWidget.hpp"
#include "hud/widgets/KdaCounter.hpp"
#include "hud/widgets/KillFeed.hpp"
#include "hud/widgets/Minimap.hpp"
#include "hud/widgets/PickupNotification.hpp"
#include "hud/widgets/PickupPrompt.hpp"
#include "hud/widgets/RailgunScopeWidget.hpp"
#include "hud/widgets/RoundTimer.hpp"
#include "hud/widgets/Scoreboard.hpp"
#include "hud/widgets/TeamStatusBar.hpp"
#include "hud/widgets/VignetteWidget.hpp"

#include <array>
#include <fstream>
#include <imgui.h>
#include <ostream>
#include <string>
#include <string_view>

namespace
{

constexpr const char* k_anchorNames[] = {
    "Top Left",
    "Top Center",
    "Top Right",
    "Center Left",
    "Center",
    "Center Right",
    "Bottom Left",
    "Bottom Center",
    "Bottom Right",
};

const char* anchorName(HudAnchor anchor)
{
    const int index = static_cast<int>(anchor);
    if (index < 0 || index >= static_cast<int>(IM_ARRAYSIZE(k_anchorNames)))
        return "Unknown";
    return k_anchorNames[index];
}

void editFloat(const char* label, float& value, float speed, float minValue, float maxValue)
{
    ImGui::DragFloat(label, &value, speed, minValue, maxValue, "%.2f");
}

void editColor(const char* label, HudColor& color)
{
    ImGui::ColorEdit4(label, &color.r, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float);
}

const char* widgetName(const HudWidget* widget)
{
    if (dynamic_cast<const AbilitySelectionWidget*>(widget))
        return "Ability Selection";
    if (dynamic_cast<const AmmoCounter*>(widget))
        return "Ammo Counter";
    if (dynamic_cast<const BuyMenu*>(widget))
        return "Buy Menu";
    if (dynamic_cast<const ChatWidget*>(widget))
        return "Chat";
    if (dynamic_cast<const CrosshairWidget*>(widget))
        return "Crosshair";
    if (dynamic_cast<const DamageAccumWidget*>(widget))
        return "Damage Accumulator";
    if (dynamic_cast<const DamageIndicator*>(widget))
        return "Damage Indicator";
    if (dynamic_cast<const DamageNumberWidget*>(widget))
        return "Damage Numbers";
    if (dynamic_cast<const EnemyWorldHealthBar*>(widget))
        return "Enemy World Health Bar";
    if (dynamic_cast<const EquipmentSlots*>(widget))
        return "Ability Slots";
    if (dynamic_cast<const GravityIndicator*>(widget))
        return "Gravity Indicator";
    if (dynamic_cast<const GrenadeSlotsWidget*>(widget))
        return "Grenade Slots";
    if (dynamic_cast<const HealthArmorBar*>(widget))
        return "Health Armor Bar";
    if (dynamic_cast<const HitMarkerWidget*>(widget))
        return "Hit Marker";
    if (dynamic_cast<const KdaCounter*>(widget))
        return "KDA Counter";
    if (dynamic_cast<const KillFeed*>(widget))
        return "Kill Feed";
    if (dynamic_cast<const Minimap*>(widget))
        return "Minimap";
    if (dynamic_cast<const PickupNotification*>(widget))
        return "Pickup Notification";
    if (dynamic_cast<const PickupPrompt*>(widget))
        return "Pickup Prompt";
    if (dynamic_cast<const RailgunScopeWidget*>(widget))
        return "Railgun Scope";
    if (dynamic_cast<const RoundTimer*>(widget))
        return "Round Timer";
    if (dynamic_cast<const Scoreboard*>(widget))
        return "Scoreboard";
    if (dynamic_cast<const TeamStatusBar*>(widget))
        return "Team Status Bar";
    if (dynamic_cast<const VignetteWidget*>(widget))
        return "Vignette";
    return "Hud Widget";
}

void writeJsonString(std::ostream& out, std::string_view value)
{
    out << '"';
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    out << '"';
}

void writeParamPrefix(std::ostream& out, bool& first, std::string_view name)
{
    if (!first)
        out << ",\n";
    first = false;
    out << "      ";
    writeJsonString(out, name);
    out << ": ";
}

void writeFloatParam(std::ostream& out, bool& first, std::string_view name, float value)
{
    writeParamPrefix(out, first, name);
    out << value;
}

void writeIntParam(std::ostream& out, bool& first, std::string_view name, int value)
{
    writeParamPrefix(out, first, name);
    out << value;
}

void writeBoolParam(std::ostream& out, bool& first, std::string_view name, bool value)
{
    writeParamPrefix(out, first, name);
    out << (value ? "true" : "false");
}

void writeColorValue(std::ostream& out, HudColor color)
{
    out << "{ \"r\": " << color.r << ", \"g\": " << color.g << ", \"b\": " << color.b << ", \"a\": " << color.a << " }";
}

void writeColorParam(std::ostream& out, bool& first, std::string_view name, HudColor color)
{
    writeParamPrefix(out, first, name);
    writeColorValue(out, color);
}

void writeWidgetParamsJson(std::ostream& out, const HudWidget& widget)
{
    bool first = true;

    if (const auto* ability = dynamic_cast<const AbilitySelectionWidget*>(&widget)) {
        writeFloatParam(out, first, "panelWidth", ability->panelWidth);
        writeFloatParam(out, first, "choiceWidth", ability->choiceWidth);
        writeFloatParam(out, first, "choiceHeight", ability->choiceHeight);
        writeFloatParam(out, first, "choiceGap", ability->choiceGap);
        writeFloatParam(out, first, "headerFontSize", ability->headerFontSize);
        writeFloatParam(out, first, "nameFontSize", ability->nameFontSize);
        writeFloatParam(out, first, "bodyFontSize", ability->bodyFontSize);
        writeFloatParam(out, first, "keyFontSize", ability->keyFontSize);
    } else if (const auto* ammo = dynamic_cast<const AmmoCounter*>(&widget)) {
        writeFloatParam(out, first, "panelWidth", ammo->panelWidth);
        writeFloatParam(out, first, "panelHeight", ammo->panelHeight);
        writeFloatParam(out, first, "clipFontSize", ammo->clipFontSize);
        writeFloatParam(out, first, "reserveFontSize", ammo->reserveFontSize);
        writeFloatParam(out, first, "edgePadding", ammo->edgePadding);
    } else if (const auto* buy = dynamic_cast<const BuyMenu*>(&widget)) {
        writeFloatParam(out, first, "panelWidth", buy->panelWidth);
        writeFloatParam(out, first, "panelHeight", buy->panelHeight);
        writeFloatParam(out, first, "fontSize", buy->fontSize);
        writeFloatParam(out, first, "itemHeight", buy->itemHeight);
    } else if (const auto* crosshair = dynamic_cast<const CrosshairWidget*>(&widget)) {
        writeFloatParam(out, first, "gap", crosshair->style.gap);
        writeFloatParam(out, first, "length", crosshair->style.length);
        writeFloatParam(out, first, "thickness", crosshair->style.thickness);
        writeColorParam(out, first, "color", crosshair->style.color);
        writeBoolParam(out, first, "dot", crosshair->style.dot);
    } else if (const auto* damage = dynamic_cast<const DamageIndicator*>(&widget)) {
        writeFloatParam(out, first, "arcDistance", damage->arcDistance);
        writeFloatParam(out, first, "arcLength", damage->arcLength);
        writeFloatParam(out, first, "arcThickness", damage->arcThickness);
        writeFloatParam(out, first, "fadeTime", damage->fadeTime);
    } else if (const auto* enemy = dynamic_cast<const EnemyWorldHealthBar*>(&widget)) {
        writeFloatParam(out, first, "barWidth", enemy->barWidth);
        writeFloatParam(out, first, "shieldHeight", enemy->shieldHeight);
        writeFloatParam(out, first, "healthHeight", enemy->healthHeight);
        writeFloatParam(out, first, "fontSize", enemy->fontSize);
        writeFloatParam(out, first, "yOffsetPx", enemy->yOffsetPx);
        writeFloatParam(out, first, "showAfterDamageSecs", enemy->showAfterDamageSecs);
        writeFloatParam(out, first, "fadeOutSecs", enemy->fadeOutSecs);
    } else if (const auto* equipment = dynamic_cast<const EquipmentSlots*>(&widget)) {
        writeFloatParam(out, first, "slotSize", equipment->slotSize);
        writeFloatParam(out, first, "slotGap", equipment->slotGap);
        writeFloatParam(out, first, "iconSize", equipment->iconSize);
        writeFloatParam(out, first, "keyFontSize", equipment->keyFontSize);
        writeFloatParam(out, first, "keyPadX", equipment->keyPadX);
        writeFloatParam(out, first, "keyPadY", equipment->keyPadY);
    } else if (const auto* gravity = dynamic_cast<const GravityIndicator*>(&widget)) {
        writeFloatParam(out, first, "diskSize", gravity->diskSize);
    } else if (const auto* grenades = dynamic_cast<const GrenadeSlotsWidget*>(&widget)) {
        writeFloatParam(out, first, "slotSize", grenades->slotSize);
        writeFloatParam(out, first, "slotGap", grenades->slotGap);
        writeFloatParam(out, first, "iconSize", grenades->iconSize);
        writeFloatParam(out, first, "countFontSize", grenades->countFontSize);
        writeFloatParam(out, first, "countPadX", grenades->countPadX);
        writeFloatParam(out, first, "countPadY", grenades->countPadY);
        writeFloatParam(out, first, "countCharacterGap", grenades->countCharacterGap);
        writeFloatParam(out, first, "iconPadRight", grenades->iconPadRight);
        writeFloatParam(out, first, "cornerCut", grenades->cornerCut);
        writeFloatParam(out, first, "borderThickness", grenades->borderThickness);
    } else if (const auto* health = dynamic_cast<const HealthArmorBar*>(&widget)) {
        writeFloatParam(out, first, "panelWidth", health->panelWidth);
        writeFloatParam(out, first, "barHeight", health->barHeight);
        writeFloatParam(out, first, "chamferSize", health->chamferSize);
        writeFloatParam(out, first, "cornerCutSize", health->cornerCutSize);
        writeFloatParam(out, first, "outlineThickness", health->outlineThickness);
    } else if (const auto* hit = dynamic_cast<const HitMarkerWidget*>(&widget)) {
        writeFloatParam(out, first, "armLength", hit->armLength);
        writeFloatParam(out, first, "armThickness", hit->armThickness);
        writeFloatParam(out, first, "armGap", hit->armGap);
        writeFloatParam(out, first, "fadeDuration", hit->fadeDuration);
        writeFloatParam(out, first, "killFadeDuration", hit->killFadeDuration);
        writeFloatParam(out, first, "killRingRadius", hit->killRingRadius);
        writeFloatParam(out, first, "headshotTriangleSize", hit->headshotTriangleSize);
    } else if (const auto* kda = dynamic_cast<const KdaCounter*>(&widget)) {
        writeFloatParam(out, first, "kFontSize", kda->kFontSize);
        writeFloatParam(out, first, "adFontSize", kda->adFontSize);
        writeFloatParam(out, first, "labelFontSize", kda->labelFontSize);
        writeFloatParam(out, first, "panelPadX", kda->panelPadX);
        writeFloatParam(out, first, "panelPadY", kda->panelPadY);
        writeFloatParam(out, first, "colGap", kda->colGap);
    } else if (const auto* feed = dynamic_cast<const KillFeed*>(&widget)) {
        writeFloatParam(out, first, "entryHeight", feed->entryHeight);
        writeFloatParam(out, first, "entryPadding", feed->entryPadding);
        writeFloatParam(out, first, "entryLifetime", feed->entryLifetime);
        writeFloatParam(out, first, "fontSize", feed->fontSize);
        writeFloatParam(out, first, "fadeOutDuration", feed->fadeOutDuration);
        writeIntParam(out, first, "maxEntries", feed->maxEntries);
    } else if (const auto* minimap = dynamic_cast<const Minimap*>(&widget)) {
        writeFloatParam(out, first, "mapSize", minimap->mapSize);
        writeFloatParam(out, first, "dotSize", minimap->dotSize);
        writeFloatParam(out, first, "borderThickness", minimap->borderThickness);
        writeFloatParam(out, first, "levelRingThickness", minimap->levelRingThickness);
        writeFloatParam(out, first, "levelRingGap", minimap->levelRingGap);
        writeFloatParam(out, first, "levelRingDrainSeconds", minimap->levelRingDrainSeconds);
    } else if (const auto* pickup = dynamic_cast<const PickupNotification*>(&widget)) {
        writeFloatParam(out, first, "entryHeight", pickup->entryHeight);
        writeFloatParam(out, first, "entryGap", pickup->entryGap);
        writeFloatParam(out, first, "entryFontSize", pickup->entryFontSize);
        writeFloatParam(out, first, "entryLifetime", pickup->entryLifetime);
        writeFloatParam(out, first, "fadeOut", pickup->fadeOut);
    } else if (const auto* prompt = dynamic_cast<const PickupPrompt*>(&widget)) {
        writeFloatParam(out, first, "fontSize", prompt->fontSize);
        writeFloatParam(out, first, "keyFontSize", prompt->keyFontSize);
        writeFloatParam(out, first, "keyBoxPadding", prompt->keyBoxPadding);
        writeFloatParam(out, first, "keyBoxRadius", prompt->keyBoxRadius);
        writeFloatParam(out, first, "spacing", prompt->spacing);
    } else if (const auto* roundTimer = dynamic_cast<const RoundTimer*>(&widget)) {
        writeFloatParam(out, first, "fontSize", roundTimer->fontSize);
        writeFloatParam(out, first, "lowTimeThreshold", roundTimer->lowTimeThreshold);
    } else if (const auto* scoreboard = dynamic_cast<const Scoreboard*>(&widget)) {
        writeFloatParam(out, first, "panelWidth", scoreboard->panelWidth);
        writeFloatParam(out, first, "panelHeight", scoreboard->panelHeight);
        writeFloatParam(out, first, "headerFontSize", scoreboard->headerFontSize);
        writeFloatParam(out, first, "rowFontSize", scoreboard->rowFontSize);
        writeFloatParam(out, first, "rowHeight", scoreboard->rowHeight);
    } else if (const auto* team = dynamic_cast<const TeamStatusBar*>(&widget)) {
        writeFloatParam(out, first, "indicatorSize", team->indicatorSize);
        writeFloatParam(out, first, "indicatorSpacing", team->indicatorSpacing);
        writeFloatParam(out, first, "scoreFontSize", team->scoreFontSize);
    }

    if (!first)
        out << '\n';
}

bool saveHudTweaks(Hud& hud, std::string_view path, std::string& status)
{
    std::ofstream out(std::string{path});
    if (!out) {
        status = "Failed to open output file.";
        return false;
    }

    out << "{\n";
    out << "  \"schema\": \"group2.hudTweaks.v1\",\n";
    out << "  \"widgets\": [\n";

    const auto& widgets = hud.widgets();
    for (std::size_t i = 0; i < widgets.size(); ++i) {
        const HudWidget& widget = *widgets[i];

        out << "    {\n";
        out << "      \"index\": " << i << ",\n";
        out << "      \"type\": ";
        writeJsonString(out, widgetName(&widget));
        out << ",\n";
        out << "      \"visible\": " << (widget.visible ? "true" : "false") << ",\n";
        out << "      \"anchor\": ";
        writeJsonString(out, anchorName(widget.anchor));
        out << ",\n";
        out << "      \"offsetX\": " << widget.offsetX << ",\n";
        out << "      \"offsetY\": " << widget.offsetY << ",\n";
        out << "      \"width\": " << widget.width << ",\n";
        out << "      \"height\": " << widget.height << ",\n";
        out << "      \"tint\": ";
        writeColorValue(out, widget.tint);
        out << ",\n";
        out << "      \"params\": {\n";
        writeWidgetParamsJson(out, widget);
        out << "      }\n";
        out << "    }";
        if (i + 1 < widgets.size())
            out << ',';
        out << '\n';
    }

    out << "  ]\n";
    out << "}\n";

    if (!out) {
        status = "Failed while writing output file.";
        return false;
    }

    status = "Saved HUD tweaks.";
    return true;
}

void editCommon(HudWidget& widget)
{
    ImGui::SeparatorText("Widget");

    int anchor = static_cast<int>(widget.anchor);
    if (ImGui::Combo("Anchor", &anchor, k_anchorNames, IM_ARRAYSIZE(k_anchorNames)))
        widget.anchor = static_cast<HudAnchor>(anchor);

    editFloat("Offset X", widget.offsetX, 1.0f, -4000.0f, 4000.0f);
    editFloat("Offset Y", widget.offsetY, 1.0f, -4000.0f, 4000.0f);
    editFloat("Width", widget.width, 1.0f, 0.0f, 4000.0f);
    editFloat("Height", widget.height, 1.0f, 0.0f, 4000.0f);
    editColor("Widget Tint", widget.tint);
    ImGui::TextDisabled("Resolved UI scale: %.3f", static_cast<double>(widget.uiScale_));
}

void editWidgetSpecific(HudWidget& widget)
{
    ImGui::SeparatorText("Parameters");

    if (auto* ability = dynamic_cast<AbilitySelectionWidget*>(&widget)) {
        editFloat("Panel Width", ability->panelWidth, 1.0f, 100.0f, 1200.0f);
        editFloat("Choice Width", ability->choiceWidth, 1.0f, 20.0f, 800.0f);
        editFloat("Choice Height", ability->choiceHeight, 1.0f, 20.0f, 400.0f);
        editFloat("Choice Gap", ability->choiceGap, 0.5f, 0.0f, 200.0f);
        editFloat("Header Font Size", ability->headerFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Name Font Size", ability->nameFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Body Font Size", ability->bodyFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Key Font Size", ability->keyFontSize, 0.5f, 4.0f, 96.0f);
    } else if (auto* ammo = dynamic_cast<AmmoCounter*>(&widget)) {
        editFloat("Panel Width", ammo->panelWidth, 1.0f, 80.0f, 1200.0f);
        editFloat("Panel Height", ammo->panelHeight, 1.0f, 40.0f, 800.0f);
        editFloat("Clip Font Size", ammo->clipFontSize, 0.5f, 6.0f, 160.0f);
        editFloat("Reserve Font Size", ammo->reserveFontSize, 0.5f, 6.0f, 120.0f);
        editFloat("Edge Padding", ammo->edgePadding, 0.5f, 0.0f, 200.0f);
    } else if (auto* buy = dynamic_cast<BuyMenu*>(&widget)) {
        editFloat("Panel Width", buy->panelWidth, 1.0f, 80.0f, 1400.0f);
        editFloat("Panel Height", buy->panelHeight, 1.0f, 80.0f, 1000.0f);
        editFloat("Font Size", buy->fontSize, 0.5f, 6.0f, 120.0f);
        editFloat("Item Height", buy->itemHeight, 0.5f, 8.0f, 160.0f);
    } else if (auto* crosshair = dynamic_cast<CrosshairWidget*>(&widget)) {
        editFloat("Gap", crosshair->style.gap, 0.25f, 0.0f, 200.0f);
        editFloat("Arm Length", crosshair->style.length, 0.25f, 0.0f, 200.0f);
        editFloat("Thickness", crosshair->style.thickness, 0.25f, 0.1f, 60.0f);
        editColor("Crosshair Color", crosshair->style.color);
        ImGui::Checkbox("Center Dot", &crosshair->style.dot);
    } else if (auto* damage = dynamic_cast<DamageIndicator*>(&widget)) {
        editFloat("Arc Distance", damage->arcDistance, 1.0f, 0.0f, 600.0f);
        editFloat("Arc Length", damage->arcLength, 0.5f, 0.0f, 300.0f);
        editFloat("Arc Thickness", damage->arcThickness, 0.25f, 0.0f, 80.0f);
        editFloat("Fade Time", damage->fadeTime, 0.05f, 0.01f, 10.0f);
    } else if (auto* enemy = dynamic_cast<EnemyWorldHealthBar*>(&widget)) {
        editFloat("Bar Width", enemy->barWidth, 1.0f, 10.0f, 600.0f);
        editFloat("Shield Height", enemy->shieldHeight, 0.25f, 0.0f, 80.0f);
        editFloat("Health Height", enemy->healthHeight, 0.25f, 0.0f, 80.0f);
        editFloat("Font Size", enemy->fontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Y Offset Px", enemy->yOffsetPx, 0.5f, -200.0f, 200.0f);
        editFloat("Show After Damage Secs", enemy->showAfterDamageSecs, 0.1f, 0.0f, 30.0f);
        editFloat("Fade Out Secs", enemy->fadeOutSecs, 0.05f, 0.0f, 10.0f);
    } else if (auto* equipment = dynamic_cast<EquipmentSlots*>(&widget)) {
        editFloat("Slot Size", equipment->slotSize, 1.0f, 8.0f, 240.0f);
        editFloat("Slot Gap", equipment->slotGap, 0.5f, 0.0f, 80.0f);
        editFloat("Icon Size", equipment->iconSize, 0.5f, 4.0f, 160.0f);
        editFloat("Key Font Size", equipment->keyFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Key Pad X", equipment->keyPadX, 0.25f, 0.0f, 40.0f);
        editFloat("Key Pad Y", equipment->keyPadY, 0.25f, 0.0f, 40.0f);
    } else if (auto* gravity = dynamic_cast<GravityIndicator*>(&widget)) {
        editFloat("Disk Size", gravity->diskSize, 1.0f, 4.0f, 240.0f);
    } else if (auto* grenades = dynamic_cast<GrenadeSlotsWidget*>(&widget)) {
        editFloat("Slot Size", grenades->slotSize, 1.0f, 8.0f, 160.0f);
        editFloat("Slot Gap", grenades->slotGap, 0.5f, 0.0f, 80.0f);
        editFloat("Icon Size", grenades->iconSize, 0.5f, 4.0f, 120.0f);
        editFloat("Count Font Size", grenades->countFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Count Pad X", grenades->countPadX, 0.25f, 0.0f, 40.0f);
        editFloat("Count Pad Y", grenades->countPadY, 0.25f, 0.0f, 40.0f);
        editFloat("Count Character Gap", grenades->countCharacterGap, 0.25f, 0.0f, 20.0f);
        editFloat("Icon Pad Right", grenades->iconPadRight, 0.25f, 0.0f, 40.0f);
        editFloat("Corner Cut", grenades->cornerCut, 0.25f, 0.0f, 40.0f);
        editFloat("Border Thickness", grenades->borderThickness, 0.25f, 0.0f, 20.0f);
    } else if (auto* health = dynamic_cast<HealthArmorBar*>(&widget)) {
        editFloat("Panel Width", health->panelWidth, 1.0f, 80.0f, 1200.0f);
        editFloat("Bar Height", health->barHeight, 0.5f, 4.0f, 200.0f);
        editFloat("Chamfer Size", health->chamferSize, 0.5f, 0.0f, 120.0f);
        editFloat("Corner Cut Size", health->cornerCutSize, 0.25f, 0.0f, 80.0f);
        editFloat("Outline Thickness", health->outlineThickness, 0.25f, 0.0f, 40.0f);
    } else if (auto* hit = dynamic_cast<HitMarkerWidget*>(&widget)) {
        editFloat("Arm Length", hit->armLength, 0.25f, 0.0f, 120.0f);
        editFloat("Arm Thickness", hit->armThickness, 0.1f, 0.0f, 40.0f);
        editFloat("Arm Gap", hit->armGap, 0.25f, 0.0f, 120.0f);
        editFloat("Fade Duration", hit->fadeDuration, 0.05f, 0.01f, 10.0f);
        editFloat("Kill Fade Duration", hit->killFadeDuration, 0.05f, 0.01f, 10.0f);
        editFloat("Kill Ring Radius", hit->killRingRadius, 0.25f, 0.0f, 120.0f);
        editFloat("Headshot Triangle Size", hit->headshotTriangleSize, 0.25f, 0.0f, 120.0f);
    } else if (auto* kda = dynamic_cast<KdaCounter*>(&widget)) {
        editFloat("K Font Size", kda->kFontSize, 0.5f, 4.0f, 120.0f);
        editFloat("A/D Font Size", kda->adFontSize, 0.5f, 4.0f, 120.0f);
        editFloat("Label Font Size", kda->labelFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Panel Pad X", kda->panelPadX, 0.5f, 0.0f, 120.0f);
        editFloat("Panel Pad Y", kda->panelPadY, 0.5f, 0.0f, 120.0f);
        editFloat("Column Gap", kda->colGap, 0.5f, 0.0f, 120.0f);
    } else if (auto* feed = dynamic_cast<KillFeed*>(&widget)) {
        editFloat("Entry Height", feed->entryHeight, 0.5f, 4.0f, 120.0f);
        editFloat("Entry Padding", feed->entryPadding, 0.5f, 0.0f, 80.0f);
        editFloat("Entry Lifetime", feed->entryLifetime, 0.1f, 0.1f, 60.0f);
        editFloat("Font Size", feed->fontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Fade Out Duration", feed->fadeOutDuration, 0.05f, 0.0f, 20.0f);
        ImGui::DragInt("Max Entries", &feed->maxEntries, 0.1f, 1, 40);
    } else if (auto* minimap = dynamic_cast<Minimap*>(&widget)) {
        editFloat("Map Size", minimap->mapSize, 1.0f, 20.0f, 800.0f);
        editFloat("Dot Size", minimap->dotSize, 0.25f, 0.0f, 80.0f);
        editFloat("Border Thickness", minimap->borderThickness, 0.25f, 0.0f, 40.0f);
        editFloat("Level Ring Thickness", minimap->levelRingThickness, 0.25f, 0.0f, 40.0f);
        editFloat("Level Ring Gap", minimap->levelRingGap, 0.25f, 0.0f, 80.0f);
        editFloat("Level Ring Drain Seconds", minimap->levelRingDrainSeconds, 0.05f, 0.01f, 10.0f);
    } else if (auto* pickup = dynamic_cast<PickupNotification*>(&widget)) {
        editFloat("Entry Height", pickup->entryHeight, 0.5f, 4.0f, 120.0f);
        editFloat("Entry Gap", pickup->entryGap, 0.25f, 0.0f, 80.0f);
        editFloat("Entry Font Size", pickup->entryFontSize, 0.5f, 4.0f, 96.0f);
        editFloat("Entry Lifetime", pickup->entryLifetime, 0.1f, 0.1f, 60.0f);
        editFloat("Fade Out", pickup->fadeOut, 0.05f, 0.0f, 20.0f);
    } else if (auto* prompt = dynamic_cast<PickupPrompt*>(&widget)) {
        editFloat("Font Size", prompt->fontSize, 0.5f, 4.0f, 120.0f);
        editFloat("Key Font Size", prompt->keyFontSize, 0.5f, 4.0f, 120.0f);
        editFloat("Key Box Padding", prompt->keyBoxPadding, 0.25f, 0.0f, 80.0f);
        editFloat("Key Box Radius", prompt->keyBoxRadius, 0.25f, 0.0f, 80.0f);
        editFloat("Spacing", prompt->spacing, 0.5f, 0.0f, 120.0f);
    } else if (auto* roundTimer = dynamic_cast<RoundTimer*>(&widget)) {
        editFloat("Font Size", roundTimer->fontSize, 0.5f, 4.0f, 120.0f);
        editFloat("Low Time Threshold", roundTimer->lowTimeThreshold, 0.5f, 0.0f, 300.0f);
    } else if (auto* scoreboard = dynamic_cast<Scoreboard*>(&widget)) {
        editFloat("Panel Width", scoreboard->panelWidth, 1.0f, 80.0f, 1600.0f);
        editFloat("Panel Height", scoreboard->panelHeight, 1.0f, 80.0f, 1200.0f);
        editFloat("Header Font Size", scoreboard->headerFontSize, 0.5f, 4.0f, 120.0f);
        editFloat("Row Font Size", scoreboard->rowFontSize, 0.5f, 4.0f, 120.0f);
        editFloat("Row Height", scoreboard->rowHeight, 0.5f, 4.0f, 160.0f);
    } else if (auto* team = dynamic_cast<TeamStatusBar*>(&widget)) {
        editFloat("Indicator Size", team->indicatorSize, 0.5f, 2.0f, 120.0f);
        editFloat("Indicator Spacing", team->indicatorSpacing, 0.5f, 0.0f, 80.0f);
        editFloat("Score Font Size", team->scoreFontSize, 0.5f, 4.0f, 120.0f);
    } else {
        ImGui::TextDisabled("No public widget-specific parameters.");
    }
}

} // namespace

void HudDebugPanel::build(Hud& hud, bool* open)
{
    if (!open || !*open)
        return;

    ImGui::SetNextWindowSize({460.0f, 720.0f}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HUD Tweaker", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Live edits affect the current HUD instance only.");
    ImGui::Checkbox("Render Inactive Widgets", &hud.debugRenderInactiveWidgets());
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Draws inactive HUD widgets for layout debugging. Railgun Scope still uses normal visibility.");
    }

    static std::array<char, 260> savePath{"hud_tweaks.json"};
    static std::string saveStatus;
    static bool saveFailed = false;

    ImGui::InputText("Save Path", savePath.data(), savePath.size());
    if (ImGui::Button("Save JSON")) {
        saveFailed = !saveHudTweaks(hud, savePath.data(), saveStatus);
    }
    if (!saveStatus.empty()) {
        const ImVec4 color = saveFailed ? ImVec4{1.0f, 0.35f, 0.25f, 1.0f} : ImVec4{0.35f, 0.95f, 0.55f, 1.0f};
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", saveStatus.c_str());
    }
    ImGui::Separator();

    for (auto& widget : hud.widgets()) {
        ImGui::PushID(widget.get());

        ImGui::Checkbox("##visible", &widget->visible);
        ImGui::SameLine();

        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (ImGui::TreeNodeEx(widgetName(widget.get()), flags)) {
            editCommon(*widget);
            editWidgetSpecific(*widget);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    ImGui::End();
}
