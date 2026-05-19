#pragma once

#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_scancode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/// @brief Player-configurable gameplay actions sampled by the client.
enum class Action : uint8_t
{
    Forward,           ///< Move forward.
    Back,              ///< Move backward.
    Left,              ///< Strafe left.
    Right,             ///< Strafe right.
    Jump,              ///< Jump and skip respawn while dead.
    Crouch,            ///< Crouch.
    Ability1,          ///< Activate the first ability slot.
    Ability2,          ///< Activate the second ability slot.
    Shoot,             ///< Fire the equipped weapon.
    Reload,            ///< Reload the equipped weapon.
    Pickup,            ///< Pick up an interactable weapon or item.
    SwitchToPrimary,   ///< Switch to the primary weapon slot.
    SwitchToSecondary, ///< Switch to the secondary weapon slot.
    CycleGrenade,      ///< Quick-throw or open the grenade radial menu.
    KillSelf,          ///< Request self-elimination.
    Count              ///< Keep this last, used for iteration and array sizing.
};

/// @brief Physical input source type used by a binding.
enum class BindingKind : uint8_t
{
    Unbound,    ///< No physical input is assigned.
    Keyboard,   ///< SDL keyboard scancode binding.
    MouseButton ///< SDL mouse button binding.
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

/// @brief One configurable action binding.
struct Binding
{
    BindingKind kind{BindingKind::Unbound};     ///< Which physical input source is active.
    SDL_Scancode key{SDL_SCANCODE_UNKNOWN};     ///< Keyboard scancode when kind is Keyboard.
    MouseButton mouseButton{MouseButton::None}; ///< Mouse button when kind is MouseButton.

    /// @brief Create a keyboard binding for an SDL scancode.
    static Binding bindKeyboard(SDL_Scancode key)
    {
        return Binding{.kind = BindingKind::Keyboard, .key = key, .mouseButton = MouseButton::None};
    };

    /// @brief Create a mouse-button binding.
    static Binding bindMouse(MouseButton button)
    {
        return Binding{.kind = BindingKind::MouseButton, .key = SDL_SCANCODE_UNKNOWN, .mouseButton = button};
    };

    /// @brief Create an unbound action binding.
    static Binding unbound()
    {
        return Binding{.kind = BindingKind::Unbound, .key = SDL_SCANCODE_UNKNOWN, .mouseButton = MouseButton::None};
    };
};

// TODO: Support key chords (e.g. Alt+LMB for ability select) so the system doesn't need to know about Alt at all.
/// @brief Stores action-to-input bindings and helper conversions for UI and persistence.
class InputBindings
{
public:
    /// @brief Return bindings matching the game's historical default controls.
    static InputBindings defaults();

    /// @brief Return the binding assigned to an action.
    [[nodiscard]] Binding get(Action a) const;

    /// @brief Assign a binding to an action, swapping with any action already using that binding.
    void rebind(Action a, Binding b);

    /// @brief Test whether an action is currently pressed using SDL keyboard and mouse state.
    bool pressed(Action a, const bool* keyStates, SDL_MouseButtonFlags mouseState) const;

    /// @brief Return every configurable action in stable UI/config order.
    static constexpr std::array<Action, size_t(Action::Count)> actions()
    {
        return {Action::Forward,
                Action::Back,
                Action::Left,
                Action::Right,
                Action::Jump,
                Action::Crouch,
                Action::Ability1,
                Action::Ability2,
                Action::Shoot,
                Action::Reload,
                Action::Pickup,
                Action::SwitchToPrimary,
                Action::SwitchToSecondary,
                Action::CycleGrenade,
                Action::KillSelf};
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
    std::array<Binding, size_t(Action::Count)> bindings; ///< Binding table indexed by Action.
};
