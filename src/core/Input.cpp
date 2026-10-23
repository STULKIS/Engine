#include "core/Input.h"

#include "core/Log.h"

#include <cstring>

namespace derecho::core {
namespace {
constexpr size_t kActionCount = static_cast<size_t>(Action::Count);
}

bool Input::Initialize() {
    m_keys.fill(false);
    m_previousKeys.fill(false);
    m_actionDown.fill(false);
    m_actionPressed.fill(false);
    m_mouseDeltaX = m_mouseDeltaY = m_scrollDelta = 0.0f;

    Bind(Action::MoveForward, SDL_SCANCODE_W);
    Bind(Action::MoveBackward, SDL_SCANCODE_S);
    Bind(Action::MoveLeft, SDL_SCANCODE_A);
    Bind(Action::MoveRight, SDL_SCANCODE_D);
    Bind(Action::Jump, SDL_SCANCODE_SPACE);
    Bind(Action::Sprint, SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT);
    Bind(Action::Walk, SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL);
    Bind(Action::Interact, SDL_SCANCODE_E);
    Bind(Action::ToggleUI, SDL_SCANCODE_F1);
    Bind(Action::ToggleWireframe, SDL_SCANCODE_F2);
    Bind(Action::ToggleShadows, SDL_SCANCODE_F3);
    Bind(Action::ToggleFoliage, SDL_SCANCODE_F4);
    Bind(Action::ToggleWater, SDL_SCANCODE_F5);
    Bind(Action::ToggleTimePause, SDL_SCANCODE_F6);
    Bind(Action::ToggleStyle, SDL_SCANCODE_F7);
    Bind(Action::ToggleCamera, SDL_SCANCODE_V);
    Bind(Action::Screenshot, SDL_SCANCODE_F12);

    DE_INFO("Input initialized (relative mouse mode off).");
    return true;
}

void Input::Shutdown() {
#ifdef _WIN32
    SDL_SetRelativeMouseMode(SDL_FALSE);
#endif
    SetRelativeMouseMode(false);
}

bool Input::ProcessEvent(const SDL_Event& event) {
    switch (event.type) {
        case SDL_KEYDOWN:
            if (event.key.repeat == 0 && event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                m_keys[event.key.keysym.scancode] = true;
            }
            return true;
        case SDL_KEYUP:
            if (event.key.keysym.scancode < SDL_NUM_SCANCODES) {
                m_keys[event.key.keysym.scancode] = false;
            }
            return true;
        case SDL_MOUSEMOTION:
            if (m_relativeMouse) {
                m_mouseDeltaX += static_cast<float>(event.motion.xrel) * m_sensitivity;
                m_mouseDeltaY += static_cast<float>(event.motion.yrel) * m_sensitivity;
            }
            return true;
        case SDL_MOUSEWHEEL:
            m_scrollDelta += static_cast<float>(event.wheel.y);
            return true;
        default:
            return false;
    }
}

void Input::NewFrame() {
    UpdateActionState();
    m_previousKeys = m_keys;
}

void Input::ConsumeMouseDelta(float& dx, float& dy) {
    dx = m_mouseDeltaX;
    dy = m_mouseDeltaY;
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
}

float Input::ConsumeScrollDelta() {
    const float value = m_scrollDelta;
    m_scrollDelta = 0.0f;
    return value;
}

void Input::UpdateActionState() {
    for (size_t i = 0; i < kActionCount; ++i) {
        const Binding& b = m_bindings[i];
        const bool down = (b.primary != SDL_SCANCODE_UNKNOWN && m_keys[b.primary]) ||
                          (b.secondary != SDL_SCANCODE_UNKNOWN && m_keys[b.secondary]);
        const bool wasDown = m_actionDown[i];
        m_actionDown[i] = down;
        m_actionPressed[i] = down && !wasDown;
    }
}

bool Input::IsKeyDown(SDL_Scancode code) const {
    return code >= 0 && code < SDL_NUM_SCANCODES && m_keys[code];
}

bool Input::WasKeyPressed(SDL_Scancode code) const {
    return IsKeyDown(code) && !(code < SDL_NUM_SCANCODES && m_previousKeys[code]);
}

bool Input::WasKeyReleased(SDL_Scancode code) const {
    return !IsKeyDown(code) && (code < SDL_NUM_SCANCODES && m_previousKeys[code]);
}

bool Input::IsActionDown(Action action) const {
    const size_t index = static_cast<size_t>(action);
    return index < kActionCount && m_actionDown[index];
}

bool Input::WasActionPressed(Action action) const {
    const size_t index = static_cast<size_t>(action);
    return index < kActionCount && m_actionPressed[index];
}

float Input::MoveAxisX() const {
    float x = 0.0f;
    if (IsActionDown(Action::MoveRight)) x += 1.0f;
    if (IsActionDown(Action::MoveLeft)) x -= 1.0f;
    return x;
}

float Input::MoveAxisY() const {
    float y = 0.0f;
    if (IsActionDown(Action::MoveForward)) y += 1.0f;
    if (IsActionDown(Action::MoveBackward)) y -= 1.0f;
    return y;
}

void Input::SetRelativeMouseMode(bool enabled) {
    if (m_relativeMouse == enabled) {
        return;
    }
    if (SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE) == 0) {
        m_relativeMouse = enabled;
        DE_DEBUG("Relative mouse mode %s", enabled ? "enabled" : "disabled");
    } else {
        DE_WARN("Could not set relative mouse mode: %s", SDL_GetError());
    }
}

void Input::InjectKey(SDL_Scancode code, bool down) {
    if (code < 0 || code >= SDL_NUM_SCANCODES) {
        return;
    }
    m_keys[code] = down;
}

void Input::Bind(Action action, SDL_Scancode primary, SDL_Scancode secondary) {
    const size_t index = static_cast<size_t>(action);
    if (index >= kActionCount) {
        return;
    }
    m_bindings[index].primary = primary;
    m_bindings[index].secondary = secondary;
}

std::string Input::DescribeBinding(Action action) const {
    const size_t index = static_cast<size_t>(action);
    if (index >= kActionCount) {
        return "unbound";
    }
    const Binding& b = m_bindings[index];
    auto keyName = [](SDL_Scancode code) {
        if (code == SDL_SCANCODE_UNKNOWN) return std::string("-");
        const char* name = SDL_GetScancodeName(code);
        return name && name[0] ? std::string(name) : std::string("?");
    };
    std::string result = keyName(b.primary);
    if (b.secondary != SDL_SCANCODE_UNKNOWN) {
        result += " / " + keyName(b.secondary);
    }
    return result;
}

} // namespace derecho::core
