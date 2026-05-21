#include "client/config/UserSettings.hpp"
#include "config/InputBindings.hpp"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_scancode.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>

namespace
{
bool expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << message << "\n";
        return false;
    }
    return true;
}

bool testMouseButtonsAndAltCrouchDefaults()
{
    const InputBindings bindings = InputBindings::defaults();

    bool ok = true;
    ok &= expect(bindings.get(Action::Shoot).kind == BindingKind::MouseButton, "shoot should default to mouse button");
    ok &= expect(bindings.get(Action::Shoot).mouseButton == MouseButton::Left, "shoot should default to left mouse");
    ok &= expect(bindings.get(Action::Scope).kind == BindingKind::MouseButton, "scope should default to mouse button");
    ok &= expect(bindings.get(Action::Scope).mouseButton == MouseButton::Right, "scope should default to right mouse");
    ok &= expect(bindings.get(Action::Crouch).key == SDL_SCANCODE_LCTRL, "crouch primary should default to left ctrl");
    ok &= expect(bindings.get(Action::Crouch, BindingDevice::KeyboardMouse, 1).key == SDL_SCANCODE_C,
                 "crouch alternate should default to C");

    std::array<bool, SDL_SCANCODE_COUNT> keys{};
    keys[SDL_SCANCODE_C] = true;
    ok &= expect(bindings.pressed(Action::Crouch, keys.data(), 0), "crouch should respond to alternate C binding");

    keys[SDL_SCANCODE_C] = false;
    const SDL_MouseButtonFlags mouse = SDL_BUTTON_LMASK;
    ok &= expect(bindings.pressed(Action::Shoot, keys.data(), mouse), "shoot should respond to left mouse binding");

    return ok;
}

bool testControllerDefaults()
{
    const InputBindings bindings = InputBindings::defaults();

    bool ok = true;
    ok &= expect(bindings.get(Action::Jump, BindingDevice::Controller).kind == BindingKind::GamepadButton,
                 "controller jump should default to a gamepad button");
    ok &= expect(bindings.get(Action::Jump, BindingDevice::Controller).gamepadButton == SDL_GAMEPAD_BUTTON_SOUTH,
                 "controller jump should default to south face button");
    ok &= expect(bindings.get(Action::Shoot, BindingDevice::Controller).kind == BindingKind::GamepadAxis,
                 "controller shoot should default to a trigger axis");
    ok &= expect(bindings.get(Action::Shoot, BindingDevice::Controller).gamepadAxis == GamepadAxisBinding::RightTrigger,
                 "controller shoot should default to right trigger");
    ok &= expect(bindings.get(Action::SwitchToPrimary, BindingDevice::Controller, 1).gamepadButton ==
                     SDL_GAMEPAD_BUTTON_DPAD_UP,
                 "controller primary weapon alternate should default to d-pad up");
    return ok;
}

bool testEventMatching()
{
    const InputBindings bindings = InputBindings::defaults();

    bool pressed = false;
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.y = -1.0f;
    bool ok =
        expect(bindings.eventMatches(Action::NextWeapon, event, pressed), "mouse wheel down should match next weapon");
    ok &= expect(pressed, "mouse wheel next weapon event should be a press");

    event = {};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    ok &= expect(bindings.eventMatches(Action::SwitchToPrimary, event, pressed),
                 "gamepad d-pad up should match switch primary alternate");
    ok &= expect(pressed, "gamepad button down should report pressed");

    event = {};
    event.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.button = SDL_GAMEPAD_BUTTON_DPAD_UP;
    ok &= expect(bindings.eventMatches(Action::SwitchToPrimary, event, pressed),
                 "gamepad d-pad up release should match switch primary alternate");
    ok &= expect(!pressed, "gamepad button up should report released");
    return ok;
}

bool testSettingsPersistence()
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "group2-input-bindings-test.toml";
    std::filesystem::remove(path);

    UserSettings settings;
    settings.mouseSensitivity = 0.0015f;
    settings.horizontalFovDegrees = 103.0f;
    settings.showControllerBindings = true;
    settings.inputBindings.rebind(
        Action::Ability2, Binding::bindKeyboard(SDL_SCANCODE_Q), BindingDevice::KeyboardMouse, 1);
    settings.inputBindings.rebind(
        Action::Scope, Binding::bindGamepadButton(SDL_GAMEPAD_BUTTON_RIGHT_STICK), BindingDevice::Controller, 1);

    bool ok = expect(user_settings::save(path.string(), settings), "settings save should succeed");
    const UserSettings loaded = user_settings::load(path.string());
    ok &= expect(loaded.mouseSensitivity == settings.mouseSensitivity, "mouse sensitivity should round-trip");
    ok &= expect(loaded.horizontalFovDegrees == settings.horizontalFovDegrees, "horizontal fov should round-trip");
    ok &= expect(loaded.showControllerBindings, "controller binding view flag should round-trip");
    ok &= expect(loaded.inputBindings.get(Action::Ability2, BindingDevice::KeyboardMouse, 1).key == SDL_SCANCODE_Q,
                 "keyboard alternate binding should round-trip");
    ok &= expect(loaded.inputBindings.get(Action::Scope, BindingDevice::Controller, 1).gamepadButton ==
                     SDL_GAMEPAD_BUTTON_RIGHT_STICK,
                 "controller alternate binding should round-trip");

    std::filesystem::remove(path);
    return ok;
}
} // namespace

int main()
{
    SDL_Init(0);

    bool ok = true;
    ok &= testMouseButtonsAndAltCrouchDefaults();
    ok &= testControllerDefaults();
    ok &= testEventMatching();
    ok &= testSettingsPersistence();

    SDL_Quit();
    return ok ? 0 : 1;
}
