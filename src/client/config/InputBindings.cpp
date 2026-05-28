#include "InputBindings.hpp"

#include "SDL3/SDL_keyboard.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
constexpr float k_gamepadTriggerThreshold = 0.5f;

bool assigned(Binding b)
{
    switch (b.kind) {
    case BindingKind::Keyboard:
        return b.key != SDL_SCANCODE_UNKNOWN;
    case BindingKind::MouseButton:
        return b.mouseButton != MouseButton::None;
    case BindingKind::MouseWheel:
        return b.mouseWheel != MouseWheelDirection::None;
    case BindingKind::GamepadButton:
        return b.gamepadButton != SDL_GAMEPAD_BUTTON_INVALID;
    case BindingKind::GamepadAxis:
        return b.gamepadAxis != GamepadAxisBinding::None;
    case BindingKind::Unbound:
        return false;
    }
    return false;
}

bool sameBinding(Binding a, Binding b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case BindingKind::Keyboard:
        return a.key == b.key && a.key != SDL_SCANCODE_UNKNOWN;
    case BindingKind::MouseButton:
        return a.mouseButton == b.mouseButton && a.mouseButton != MouseButton::None;
    case BindingKind::MouseWheel:
        return a.mouseWheel == b.mouseWheel && a.mouseWheel != MouseWheelDirection::None;
    case BindingKind::GamepadButton:
        return a.gamepadButton == b.gamepadButton && a.gamepadButton != SDL_GAMEPAD_BUTTON_INVALID;
    case BindingKind::GamepadAxis:
        return a.gamepadAxis == b.gamepadAxis && a.gamepadAxis != GamepadAxisBinding::None;
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

std::string_view mouseWheelName(MouseWheelDirection direction)
{
    switch (direction) {
    case MouseWheelDirection::Up:
        return "Mouse Wheel Up";
    case MouseWheelDirection::Down:
        return "Mouse Wheel Down";
    case MouseWheelDirection::None:
        return "Unbound";
    }
    return "Unbound";
}

constexpr std::array<std::pair<std::string_view, SDL_GamepadButton>, 15> k_gamepadButtons = {
    {{"Gamepad South", SDL_GAMEPAD_BUTTON_SOUTH},
     {"Gamepad East", SDL_GAMEPAD_BUTTON_EAST},
     {"Gamepad West", SDL_GAMEPAD_BUTTON_WEST},
     {"Gamepad North", SDL_GAMEPAD_BUTTON_NORTH},
     {"Gamepad Back", SDL_GAMEPAD_BUTTON_BACK},
     {"Gamepad Start", SDL_GAMEPAD_BUTTON_START},
     {"Gamepad Left Stick", SDL_GAMEPAD_BUTTON_LEFT_STICK},
     {"Gamepad Right Stick", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
     {"Gamepad Left Shoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
     {"Gamepad Right Shoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
     {"Gamepad D-Pad Up", SDL_GAMEPAD_BUTTON_DPAD_UP},
     {"Gamepad D-Pad Down", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
     {"Gamepad D-Pad Left", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
     {"Gamepad D-Pad Right", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
     {"Gamepad Touchpad", SDL_GAMEPAD_BUTTON_TOUCHPAD}}};

std::string_view gamepadButtonName(SDL_GamepadButton button)
{
    for (const auto& [name, value] : k_gamepadButtons) {
        if (button == value)
            return name;
    }
    return "Unbound";
}

std::string_view gamepadAxisName(GamepadAxisBinding axis)
{
    switch (axis) {
    case GamepadAxisBinding::LeftTrigger:
        return "Gamepad Left Trigger";
    case GamepadAxisBinding::RightTrigger:
        return "Gamepad Right Trigger";
    case GamepadAxisBinding::None:
        return "Unbound";
    }
    return "Unbound";
}

SDL_GamepadAxis sdlAxis(GamepadAxisBinding axis)
{
    switch (axis) {
    case GamepadAxisBinding::LeftTrigger:
        return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    case GamepadAxisBinding::RightTrigger:
        return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    case GamepadAxisBinding::None:
        return SDL_GAMEPAD_AXIS_INVALID;
    }
    return SDL_GAMEPAD_AXIS_INVALID;
}

bool keyboardMouseBindingPressed(Binding binding, const bool* keyStates, SDL_MouseButtonFlags mouseState)
{
    switch (binding.kind) {
    case BindingKind::Keyboard:
        return binding.key != SDL_SCANCODE_UNKNOWN && keyStates[binding.key];
    case BindingKind::MouseButton:
        return binding.mouseButton != MouseButton::None &&
               (mouseState & SDL_BUTTON_MASK(static_cast<uint8_t>(binding.mouseButton))) != 0;
    default:
        return false;
    }
}

bool controllerBindingPressed(Binding binding, SDL_Gamepad* gamepad)
{
    if (!gamepad || !SDL_GamepadConnected(gamepad))
        return false;

    switch (binding.kind) {
    case BindingKind::GamepadButton:
        return binding.gamepadButton != SDL_GAMEPAD_BUTTON_INVALID &&
               SDL_GetGamepadButton(gamepad, binding.gamepadButton);
    case BindingKind::GamepadAxis: {
        const SDL_GamepadAxis axis = sdlAxis(binding.gamepadAxis);
        if (axis == SDL_GAMEPAD_AXIS_INVALID)
            return false;
        const float value = std::clamp(static_cast<float>(SDL_GetGamepadAxis(gamepad, axis)) / 32767.0f, 0.0f, 1.0f);
        return value >= k_gamepadTriggerThreshold;
    }
    default:
        return false;
    }
}

bool bindingMatchesEvent(Binding binding, const SDL_Event& event, bool& down)
{
    switch (binding.kind) {
    case BindingKind::Keyboard:
        if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) && event.key.scancode == binding.key) {
            down = event.type == SDL_EVENT_KEY_DOWN;
            return true;
        }
        break;
    case BindingKind::MouseButton:
        if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
            event.button.button == static_cast<uint8_t>(binding.mouseButton))
        {
            down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            return true;
        }
        break;
    case BindingKind::MouseWheel:
        if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            const bool wheelUp = event.wheel.y > 0.0f;
            const bool wheelDown = event.wheel.y < 0.0f;
            if ((binding.mouseWheel == MouseWheelDirection::Up && wheelUp) ||
                (binding.mouseWheel == MouseWheelDirection::Down && wheelDown))
            {
                down = true;
                return true;
            }
        }
        break;
    case BindingKind::GamepadButton:
        if ((event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || event.type == SDL_EVENT_GAMEPAD_BUTTON_UP) &&
            event.gbutton.button == static_cast<uint8_t>(binding.gamepadButton))
        {
            down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            return true;
        }
        break;
    case BindingKind::GamepadAxis:
        if (event.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
            event.gaxis.axis == static_cast<uint8_t>(sdlAxis(binding.gamepadAxis)))
        {
            const float value = std::clamp(static_cast<float>(event.gaxis.value) / 32767.0f, 0.0f, 1.0f);
            down = value >= k_gamepadTriggerThreshold;
            return true;
        }
        break;
    case BindingKind::Unbound:
        break;
    }
    return false;
}
} // namespace

InputBindings::BindingTable& InputBindings::tableFor(BindingDevice device)
{
    return device == BindingDevice::Controller ? controllerBindings : keyboardMouseBindings;
}

const InputBindings::BindingTable& InputBindings::tableFor(BindingDevice device) const
{
    return device == BindingDevice::Controller ? controllerBindings : keyboardMouseBindings;
}

Binding InputBindings::get(Action a, BindingDevice device, std::size_t slot) const
{
    if (slot >= kBindingSlots)
        return Binding::unbound();
    return tableFor(device)[size_t(a)][slot];
}

void InputBindings::rebind(Action a, Binding b, BindingDevice device, std::size_t slot)
{
    if (slot >= kBindingSlots)
        return;

    BindingTable& table = tableFor(device);
    if (assigned(b)) {
        for (Action other : actions()) {
            for (std::size_t otherSlot = 0; otherSlot < kBindingSlots; ++otherSlot) {
                if (other == a && otherSlot == slot)
                    continue;
                if (sameBinding(table[size_t(other)][otherSlot], b))
                    table[size_t(other)][otherSlot] = Binding::unbound();
            }
        }
    }

    table[size_t(a)][slot] = b;
}

bool InputBindings::pressed(Action a, const bool* keyStates, SDL_MouseButtonFlags mouseState) const
{
    if (!keyStates)
        return false;
    for (Binding binding : keyboardMouseBindings[size_t(a)]) {
        if (keyboardMouseBindingPressed(binding, keyStates, mouseState))
            return true;
    }
    return false;
}

bool InputBindings::controllerPressed(Action a, SDL_Gamepad* gamepad) const
{
    for (Binding binding : controllerBindings[size_t(a)]) {
        if (controllerBindingPressed(binding, gamepad))
            return true;
    }
    return false;
}

bool InputBindings::eventMatches(Action a, const SDL_Event& event, bool& down) const
{
    for (Binding binding : keyboardMouseBindings[size_t(a)]) {
        if (bindingMatchesEvent(binding, event, down))
            return true;
    }
    for (Binding binding : controllerBindings[size_t(a)]) {
        if (bindingMatchesEvent(binding, event, down))
            return true;
    }
    return false;
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
    b.rebind(Action::Crouch, Binding::bindKeyboard(SDL_SCANCODE_C), BindingDevice::KeyboardMouse, 1);
    b.rebind(Action::AbilityMenu, Binding::bindKeyboard(SDL_SCANCODE_LALT));
    b.rebind(Action::Ability1, Binding::bindKeyboard(SDL_SCANCODE_LSHIFT));
    b.rebind(Action::Ability2, Binding::bindKeyboard(SDL_SCANCODE_E));
    b.rebind(Action::Shoot, Binding::bindMouse(MouseButton::Left));
    b.rebind(Action::Scope, Binding::bindMouse(MouseButton::Right));
    b.rebind(Action::Reload, Binding::bindKeyboard(SDL_SCANCODE_R));
    b.rebind(Action::Pickup, Binding::bindKeyboard(SDL_SCANCODE_F));
    b.rebind(Action::SwitchToPrimary, Binding::bindKeyboard(SDL_SCANCODE_1));
    b.rebind(Action::SwitchToSecondary, Binding::bindKeyboard(SDL_SCANCODE_2));
    b.rebind(Action::PreviousWeapon, Binding::bindMouseWheel(MouseWheelDirection::Up));
    b.rebind(Action::NextWeapon, Binding::bindMouseWheel(MouseWheelDirection::Down));
    b.rebind(Action::CycleGrenade, Binding::bindKeyboard(SDL_SCANCODE_G));
    b.rebind(Action::KillSelf, Binding::bindKeyboard(SDL_SCANCODE_K));
    b.rebind(Action::Scoreboard, Binding::bindKeyboard(SDL_SCANCODE_TAB));
    b.rebind(Action::BuyMenu, Binding::bindKeyboard(SDL_SCANCODE_B));
    b.rebind(Action::PushToTalk, Binding::bindKeyboard(SDL_SCANCODE_V));

    b.rebind(Action::Jump, Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_SOUTH), BindingDevice::Controller);
    b.rebind(Action::Crouch, Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_EAST), BindingDevice::Controller);
    b.rebind(Action::Ability1, Binding::bindGamepadAxis(GamepadAxisBinding::LeftTrigger), BindingDevice::Controller);
    b.rebind(Action::Shoot, Binding::bindGamepadAxis(GamepadAxisBinding::RightTrigger), BindingDevice::Controller);
    b.rebind(Action::Reload, Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_WEST), BindingDevice::Controller);
    b.rebind(Action::Pickup, Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_NORTH), BindingDevice::Controller);
    b.rebind(Action::SwitchToPrimary,
             Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER),
             BindingDevice::Controller);
    b.rebind(
        Action::SwitchToPrimary, Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_UP), BindingDevice::Controller, 1);
    b.rebind(Action::SwitchToSecondary,
             Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
             BindingDevice::Controller);
    b.rebind(Action::SwitchToSecondary,
             Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
             BindingDevice::Controller,
             1);

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
        return "Crouch / Slide";
    case Action::AbilityMenu:
        return "Ability Menu";
    case Action::Ability1:
        return "Ability 1";
    case Action::Ability2:
        return "Ability 2";
    case Action::Shoot:
        return "Shoot";
    case Action::Scope:
        return "Aim / Scope";
    case Action::Reload:
        return "Reload";
    case Action::Pickup:
        return "Pickup";
    case Action::SwitchToPrimary:
        return "Switch Primary";
    case Action::SwitchToSecondary:
        return "Switch Secondary";
    case Action::PreviousWeapon:
        return "Previous Weapon";
    case Action::NextWeapon:
        return "Next Weapon";
    case Action::CycleGrenade:
        return "Cycle Grenade";
    case Action::KillSelf:
        return "Kill Self";
    case Action::Scoreboard:
        return "Scoreboard";
    case Action::BuyMenu:
        return "Buy Menu";
    case Action::PushToTalk:
        return "Push to Talk";
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
    case Action::AbilityMenu:
        return "ability-menu";
    case Action::Ability1:
        return "ability-1";
    case Action::Ability2:
        return "ability-2";
    case Action::Shoot:
        return "shoot";
    case Action::Scope:
        return "scope";
    case Action::Reload:
        return "reload";
    case Action::Pickup:
        return "pickup";
    case Action::SwitchToPrimary:
        return "switch-primary";
    case Action::SwitchToSecondary:
        return "switch-secondary";
    case Action::PreviousWeapon:
        return "previous-weapon";
    case Action::NextWeapon:
        return "next-weapon";
    case Action::CycleGrenade:
        return "cycle-grenade";
    case Action::KillSelf:
        return "kill-self";
    case Action::Scoreboard:
        return "scoreboard";
    case Action::BuyMenu:
        return "buy-menu";
    case Action::PushToTalk:
        return "push-to-talk";
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
    case BindingKind::MouseWheel:
        return std::string(mouseWheelName(b.mouseWheel));
    case BindingKind::GamepadButton:
        return std::string(gamepadButtonName(b.gamepadButton));
    case BindingKind::GamepadAxis:
        return std::string(gamepadAxisName(b.gamepadAxis));
    case BindingKind::Unbound:
        return "Unbound";
    }
    return "Unbound";
}

std::string InputBindings::bindingConfigValue(Binding b)
{
    return assigned(b) ? bindingLabel(b) : std::string();
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

    constexpr std::array<std::pair<std::string_view, MouseWheelDirection>, 2> k_mouseWheelBindings = {
        {{"Mouse Wheel Up", MouseWheelDirection::Up}, {"Mouse Wheel Down", MouseWheelDirection::Down}}};
    for (const auto& [name, direction] : k_mouseWheelBindings) {
        if (value == name) {
            out = Binding::bindMouseWheel(direction);
            return true;
        }
    }

    for (const auto& [name, button] : k_gamepadButtons) {
        if (value == name) {
            out = Binding::bindGamepadButton(button);
            return true;
        }
    }

    constexpr std::array<std::pair<std::string_view, GamepadAxisBinding>, 2> k_gamepadAxisBindings = {
        {{"Gamepad Left Trigger", GamepadAxisBinding::LeftTrigger},
         {"Gamepad Right Trigger", GamepadAxisBinding::RightTrigger}}};
    for (const auto& [name, axis] : k_gamepadAxisBindings) {
        if (value == name) {
            out = Binding::bindGamepadAxis(axis);
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
