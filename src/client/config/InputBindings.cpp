#include "InputBindings.hpp"

#include "SDL3/SDL_mouse.h"

#include <array>

namespace
{
bool sameBinding(Binding a, Binding b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case BindingKind::Keyboard:
        return a.key == b.key && a.key != SDL_SCANCODE_UNKNOWN;
    case BindingKind::MouseButton:
        return a.mouseButton == b.mouseButton && a.mouseButton != MouseButton::None;
    case BindingKind::Unbound:
        return false;
    }
    return false;
}

std::string_view mouseButtonName(MouseButton button)
{
    switch (button) {
    case MouseButton::Left:
        return "Mouse Left";
    case MouseButton::Middle:
        return "Mouse Middle";
    case MouseButton::Right:
        return "Mouse Right";
    case MouseButton::X1:
        return "Mouse X1";
    case MouseButton::X2:
        return "Mouse X2";
    case MouseButton::None:
        return "Unbound";
    }
    return "Unbound";
}
} // namespace

Binding InputBindings::get(Action a) const
{
    return bindings[size_t(a)];
}

void InputBindings::rebind(Action a, Binding b)
{
    const Binding previous = get(a);
    if (b.kind != BindingKind::Unbound) {
        for (Action other : actions()) {
            if (other == a)
                continue;
            if (sameBinding(get(other), b)) {
                bindings[size_t(other)] = previous;
                break;
            }
        }
    }
    bindings[size_t(a)] = b;
}

bool InputBindings::pressed(Action a, const bool* keyStates, SDL_MouseButtonFlags mouseState) const
{
    const Binding& b = get(a);
    switch (b.kind) {
    case BindingKind::Keyboard:
        if (b.key == SDL_SCANCODE_UNKNOWN)
            return false;
        return keyStates[b.key];
    case BindingKind::MouseButton:
        return (mouseState & SDL_BUTTON_MASK(static_cast<uint8_t>(b.mouseButton))) != 0;
    case BindingKind::Unbound:
        return false;
    }
}

InputBindings InputBindings::defaults()
{
    InputBindings b;

    b.rebind(Action::Forward, Binding::bindKeyboard(SDL_SCANCODE_W));
    b.rebind(Action::Back, Binding::bindKeyboard(SDL_SCANCODE_S));
    b.rebind(Action::Left, Binding::bindKeyboard(SDL_SCANCODE_A));
    b.rebind(Action::Right, Binding::bindKeyboard(SDL_SCANCODE_D));
    b.rebind(Action::Jump, Binding::bindKeyboard(SDL_SCANCODE_SPACE));
    b.rebind(Action::Crouch, Binding::bindKeyboard(SDL_SCANCODE_LCTRL));
    b.rebind(Action::Ability1, Binding::bindKeyboard(SDL_SCANCODE_LSHIFT));
    b.rebind(Action::Ability2, Binding::bindKeyboard(SDL_SCANCODE_E));
    b.rebind(Action::Shoot, Binding::bindMouse(MouseButton::Left));
    b.rebind(Action::Reload, Binding::bindKeyboard(SDL_SCANCODE_R));
    b.rebind(Action::Pickup, Binding::bindKeyboard(SDL_SCANCODE_F));
    b.rebind(Action::SwitchToPrimary, Binding::bindKeyboard(SDL_SCANCODE_1));
    b.rebind(Action::SwitchToSecondary, Binding::bindKeyboard(SDL_SCANCODE_2));
    b.rebind(Action::CycleGrenade, Binding::bindKeyboard(SDL_SCANCODE_G));
    b.rebind(Action::KillSelf, Binding::bindKeyboard(SDL_SCANCODE_K));

    return b;
}

std::string_view InputBindings::actionLabel(Action a)
{
    switch (a) {
    case Action::Forward:
        return "Forward";
    case Action::Back:
        return "Back";
    case Action::Left:
        return "Left";
    case Action::Right:
        return "Right";
    case Action::Jump:
        return "Jump";
    case Action::Crouch:
        return "Crouch";
    case Action::Ability1:
        return "Ability 1";
    case Action::Ability2:
        return "Ability 2";
    case Action::Shoot:
        return "Shoot";
    case Action::Reload:
        return "Reload";
    case Action::Pickup:
        return "Pickup";
    case Action::SwitchToPrimary:
        return "Switch Primary";
    case Action::SwitchToSecondary:
        return "Switch Secondary";
    case Action::CycleGrenade:
        return "Cycle Grenade";
    case Action::KillSelf:
        return "Kill Self";
    case Action::Count:
        return "";
    }
    return "";
}

std::string_view InputBindings::configKey(Action a)
{
    switch (a) {
    case Action::Forward:
        return "forward";
    case Action::Back:
        return "back";
    case Action::Left:
        return "left";
    case Action::Right:
        return "right";
    case Action::Jump:
        return "jump";
    case Action::Crouch:
        return "crouch";
    case Action::Ability1:
        return "ability-1";
    case Action::Ability2:
        return "ability-2";
    case Action::Shoot:
        return "shoot";
    case Action::Reload:
        return "reload";
    case Action::Pickup:
        return "pickup";
    case Action::SwitchToPrimary:
        return "switch-primary";
    case Action::SwitchToSecondary:
        return "switch-secondary";
    case Action::CycleGrenade:
        return "cycle-grenade";
    case Action::KillSelf:
        return "kill-self";
    case Action::Count:
        return "";
    }
    return "";
}

bool InputBindings::actionFromConfigKey(std::string_view key, Action& out)
{
    for (Action action : actions()) {
        if (configKey(action) == key) {
            out = action;
            return true;
        }
    }
    return false;
}

std::string InputBindings::bindingLabel(Binding b)
{
    switch (b.kind) {
    case BindingKind::Keyboard: {
        const char* name = SDL_GetScancodeName(b.key);
        return name && name[0] != '\0' ? std::string(name) : std::string("Unbound");
    }
    case BindingKind::MouseButton:
        return std::string(mouseButtonName(b.mouseButton));
    case BindingKind::Unbound:
        return "Unbound";
    }
    return "Unbound";
}

std::string InputBindings::bindingConfigValue(Binding b)
{
    return bindingLabel(b);
}

bool InputBindings::bindingFromConfigValue(std::string_view value, Binding& out)
{
    if (value == "Unbound" || value.empty()) {
        out = Binding::unbound();
        return true;
    }

    constexpr std::array<std::pair<std::string_view, MouseButton>, 5> k_mouseBindings = {
        {{"Mouse Left", MouseButton::Left},
         {"Mouse Middle", MouseButton::Middle},
         {"Mouse Right", MouseButton::Right},
         {"Mouse X1", MouseButton::X1},
         {"Mouse X2", MouseButton::X2}}};
    for (const auto& [name, button] : k_mouseBindings) {
        if (value == name) {
            out = Binding::bindMouse(button);
            return true;
        }
    }

    std::string valueString(value);
    const SDL_Scancode scancode = SDL_GetScancodeFromName(valueString.c_str());
    if (scancode != SDL_SCANCODE_UNKNOWN) {
        out = Binding::bindKeyboard(scancode);
        return true;
    }

    return false;
}
