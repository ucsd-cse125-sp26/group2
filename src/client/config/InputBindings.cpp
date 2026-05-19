#include "InputBindings.hpp"

#include "SDL3/SDL_mouse.h"

Binding InputBindings::get(Action a) const
{
    return bindings[size_t(a)];
}

void InputBindings::rebind(Action a, Binding b)
{
    bindings[size_t(a)] = b;
}

bool InputBindings::pressed(Action a, const bool* keyStates, SDL_MouseButtonFlags mouseState) const
{
    const Binding& b = get(a);
    switch (b.kind) {
    case BindingKind::Keyboard:
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
    b.rebind(Action::Ability1, Binding::bindKeyboard(SDL_SCANCODE_Q));
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
