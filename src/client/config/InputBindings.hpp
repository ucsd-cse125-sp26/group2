#pragma once

#include "SDL3/SDL_events.h"
#include "SDL3/SDL_gamepad.h"
#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_scancode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/// @brief Player-configurable gameplay and HUD actions sampled by the client.
enum class Action : uint8_t
{
    Forward,           ///< Move forward.
    Back,              ///< Move backward.
    Left,              ///< Strafe left.
    Right,             ///< Strafe right.
    Jump,              ///< Jump and skip respawn while dead.
    Crouch,            ///< Crouch and slide.
    AbilityMenu,       ///< Open the ability selection radial menu.
    Ability1,          ///< Activate the first ability slot.
    Ability2,          ///< Activate the second ability slot.
    Shoot,             ///< Fire the equipped weapon.
    Scope,             ///< Aim/scope the equipped weapon.
    Reload,            ///< Reload the equipped weapon.
    Pickup,            ///< Pick up an interactable weapon or item.
    SwitchToPrimary,   ///< Switch to the primary weapon slot.
    SwitchToSecondary, ///< Switch to the secondary weapon slot.
    PreviousWeapon,    ///< Cycle to the previous weapon slot.
    NextWeapon,        ///< Cycle to the next weapon slot.
    CycleGrenade,      ///< Cycle to the next grenade type that has ammo.
    ThrowGrenade,      ///< Throw the currently selected grenade.
    KillSelf,          ///< Request self-elimination.
    Scoreboard,        ///< Show the scoreboard while held.
    Emote,             ///< Hold to open the emote wheel; release to play the selected emote.
    PushToTalk,        ///< Hold to transmit voice chat.
    Count              ///< Keep this last, used for iteration and array sizing.
};

enum class BindingDevice : uint8_t
{
    KeyboardMouse,
    Controller
};

/// @brief Physical input source type used by a binding.
enum class BindingKind : uint8_t
{
    Unbound,       ///< No physical input is assigned.
    Keyboard,      ///< SDL keyboard scancode binding.
    MouseButton,   ///< SDL mouse button binding.
    MouseWheel,    ///< SDL mouse wheel direction binding.
    GamepadButton, ///< SDL gamepad button binding.
    GamepadAxis    ///< SDL gamepad trigger-axis binding.
};

/// @brief Mouse buttons supported by the rebind UI.
enum class MouseButton : uint8_t
{
    None = 0,                   ///< No mouse button.
    Left = SDL_BUTTON_LEFT,     ///< Left mouse button.
    Middle = SDL_BUTTON_MIDDLE, ///< Middle mouse button.
    Right = SDL_BUTTON_RIGHT,   ///< Right mouse button.
    X1 = SDL_BUTTON_X1,         ///< First extra mouse button.
    X2 = SDL_BUTTON_X2,         ///< Second extra mouse button.
};

enum class MouseWheelDirection : uint8_t
{
    None,
    Up,
    Down
};

enum class GamepadAxisBinding : uint8_t
{
    None,
    LeftTrigger,
    RightTrigger
};

/// @brief One configurable action binding.
struct Binding
{
    BindingKind kind{BindingKind::Unbound};                      ///< Which physical input source is active.
    SDL_Scancode key{SDL_SCANCODE_UNKNOWN};                      ///< Keyboard scancode when kind is Keyboard.
    MouseButton mouseButton{MouseButton::None};                  ///< Mouse button when kind is MouseButton.
    MouseWheelDirection mouseWheel{MouseWheelDirection::None};   ///< Wheel direction when kind is MouseWheel.
    SDL_GamepadButton gamepadButton{SDL_GAMEPAD_BUTTON_INVALID}; ///< Gamepad button when kind is GamepadButton.
    GamepadAxisBinding gamepadAxis{GamepadAxisBinding::None};    ///< Gamepad trigger when kind is GamepadAxis.

    /// @brief Create a keyboard binding for an SDL scancode.
    static Binding bindKeyboard(SDL_Scancode key) { return Binding{.kind = BindingKind::Keyboard, .key = key}; };

    /// @brief Create a mouse-button binding.
    static Binding bindMouse(MouseButton button)
    {
        return Binding{.kind = BindingKind::MouseButton, .mouseButton = button};
    };

    /// @brief Create a mouse-wheel binding.
    static Binding bindMouseWheel(MouseWheelDirection direction)
    {
        return Binding{.kind = BindingKind::MouseWheel, .mouseWheel = direction};
    };

    /// @brief Create a gamepad-button binding.
    static Binding bindGamepadButton(SDL_GamepadButton button)
    {
        return Binding{.kind = BindingKind::GamepadButton, .gamepadButton = button};
    };

    /// @brief Create a gamepad trigger-axis binding.
    static Binding bindGamepadAxis(GamepadAxisBinding axis)
    {
        return Binding{.kind = BindingKind::GamepadAxis, .gamepadAxis = axis};
    };

    /// @brief Create an unbound action binding.
    static Binding unbound() { return Binding{}; };
};

/// @brief Stores action-to-input bindings and helper conversions for UI and persistence.
class InputBindings
{
public:
    static constexpr std::size_t kBindingSlots = 2;

    /// @brief Return bindings matching the game's historical default controls.
    static InputBindings defaults();

    /// @brief Return the binding assigned to an action/device/slot.
    [[nodiscard]] Binding
    get(Action a, BindingDevice device = BindingDevice::KeyboardMouse, std::size_t slot = 0) const;

    /// @brief Assign a binding to an action/device/slot, clearing any duplicate action use on that device.
    void rebind(Action a, Binding b, BindingDevice device = BindingDevice::KeyboardMouse, std::size_t slot = 0);

    /// @brief Test whether an action is currently pressed using SDL keyboard and mouse state.
    bool pressed(Action a, const bool* keyStates, SDL_MouseButtonFlags mouseState) const;

    /// @brief Test whether an action is currently pressed on a controller.
    bool controllerPressed(Action a, SDL_Gamepad* gamepad) const;

    /// @brief Match a key/button/wheel/axis SDL event against any binding for an action.
    bool eventMatches(Action a, const SDL_Event& event, bool& down) const;

    /// @brief Return every configurable action in stable UI/config order.
    static constexpr std::array<Action, size_t(Action::Count)> actions()
    {
        return {Action::Forward,
                Action::Back,
                Action::Left,
                Action::Right,
                Action::Jump,
                Action::Crouch,
                Action::AbilityMenu,
                Action::Ability1,
                Action::Ability2,
                Action::Shoot,
                Action::Scope,
                Action::Reload,
                Action::Pickup,
                Action::SwitchToPrimary,
                Action::SwitchToSecondary,
                Action::PreviousWeapon,
                Action::NextWeapon,
                Action::CycleGrenade,
                Action::ThrowGrenade,
                Action::KillSelf,
                Action::Scoreboard,
                Action::Emote,
                Action::PushToTalk};
    }

    /// @brief Human-readable action label shown in settings UI.
    static std::string_view actionLabel(Action a);

    /// @brief Stable TOML key for an action.
    static std::string_view configKey(Action a);

    /// @brief Parse a TOML action key.
    static bool actionFromConfigKey(std::string_view key, Action& out);

    /// @brief Human-readable binding label shown in settings UI.
    static std::string bindingLabel(Binding b);

    /// @brief Stable TOML value for a binding.
    static std::string bindingConfigValue(Binding b);

    /// @brief Parse a TOML binding value.
    static bool bindingFromConfigValue(std::string_view value, Binding& out);

private:
    using BindingRow = std::array<Binding, kBindingSlots>;
    using BindingTable = std::array<BindingRow, size_t(Action::Count)>;

    [[nodiscard]] BindingTable& tableFor(BindingDevice device);
    [[nodiscard]] const BindingTable& tableFor(BindingDevice device) const;

    BindingTable keyboardMouseBindings{}; ///< Keyboard/mouse binding table indexed by Action and slot.
    BindingTable controllerBindings{};    ///< Controller binding table indexed by Action and slot.
};
