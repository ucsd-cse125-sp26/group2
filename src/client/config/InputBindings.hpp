#pragma once

#include "SDL3/SDL_mouse.h"
#include "SDL3/SDL_scancode.h"

#include <array>
#include <cstddef>
#include <cstdint>

enum class Action : uint8_t
{
    Forward,
    Back,
    Left,
    Right,
    Jump,
    Crouch,
    Ability1,
    Ability2,
    Shoot,
    Reload,
    Pickup,
    SwitchToPrimary,
    SwitchToSecondary,
    CycleGrenade,
    KillSelf,
    Count // Keep this last, used for iteration and array sizing.
};

enum class BindingKind : uint8_t
{
    Unbound,
    Keyboard,
    MouseButton
};

enum class MouseButton : uint8_t
{
    None = 0,
    Left = SDL_BUTTON_LEFT,
    Middle = SDL_BUTTON_MIDDLE,
    Right = SDL_BUTTON_RIGHT,
    X1 = SDL_BUTTON_X1,
    X2 = SDL_BUTTON_X2,
};

struct Binding
{
    BindingKind kind{BindingKind::Unbound};
    SDL_Scancode key{SDL_SCANCODE_UNKNOWN};
    MouseButton mouseButton{MouseButton::None};

    static Binding bindKeyboard(SDL_Scancode key)
    {
        return Binding{.kind = BindingKind::Keyboard, .key = key, .mouseButton = MouseButton::None};
    };

    static Binding bindMouse(MouseButton button)
    {
        return Binding{.kind = BindingKind::MouseButton, .key = SDL_SCANCODE_UNKNOWN, .mouseButton = button};
    };

    static Binding unbound()
    {
        return Binding{.kind = BindingKind::Unbound, .key = SDL_SCANCODE_UNKNOWN, .mouseButton = MouseButton::None};
    };
};

// TODO: Support key chords (e.g. Alt+LMB for ability select) so the system doesn't need to know about Alt at all.
class InputBindings
{
public:
    static InputBindings defaults();

    [[nodiscard]] Binding get(Action a) const;
    void rebind(Action a, Binding b);
    bool pressed(Action a, const bool* keyStates, SDL_MouseButtonFlags mouseState) const;

private:
    std::array<Binding, size_t(Action::Count)> bindings;
};
