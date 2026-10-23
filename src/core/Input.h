// Derecho Engine - input state, action mapping and mouse capture.
#pragma once

#include <SDL.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace derecho::core {

// Logical actions decouple gameplay from physical keys.
enum class Action {
    MoveForward,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Sprint,
    Walk,
    Interact,
    ToggleUI,
    ToggleWireframe,
    ToggleShadows,
    ToggleFoliage,
    ToggleWater,
    ToggleTimePause,
    ToggleStyle,
    ToggleCamera,
    Screenshot,
    Count
};

class Input {
public:
    bool Initialize();
    void Shutdown();

    // Feeds one SDL event. Returns true when the event was consumed by input
    // handling (used to stop ImGui from double-handling).
    bool ProcessEvent(const SDL_Event& event);

    // Call once per frame, after event processing.
    void NewFrame();

    bool IsKeyDown(SDL_Scancode code) const;
    bool WasKeyPressed(SDL_Scancode code) const;
    bool WasKeyReleased(SDL_Scancode code) const;

    bool IsActionDown(Action action) const;
    bool WasActionPressed(Action action) const;

    float MouseDeltaX() const { return m_mouseDeltaX; }
    float MouseDeltaY() const { return m_mouseDeltaY; }
    float ScrollDelta() const { return m_scrollDelta; }

    // Returns accumulated look deltas (already scaled by sensitivity) and clears them.
    void ConsumeMouseDelta(float& dx, float& dy);
    float ConsumeScrollDelta();
    float MoveAxisX() const; // +1 right
    float MoveAxisY() const; // +1 forward

    void SetRelativeMouseMode(bool enabled);
    bool IsRelativeMouseMode() const { return m_relativeMouse; }
    void SetMouseSensitivity(float sensitivity) { m_sensitivity = sensitivity; }
    float MouseSensitivity() const { return m_sensitivity; }

    // Injects a synthetic key press for automation / smoke tests.
    void InjectKey(SDL_Scancode code, bool down);

    void Bind(Action action, SDL_Scancode primary, SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN);
    std::string DescribeBinding(Action action) const;

private:
    struct Binding {
        SDL_Scancode primary = SDL_SCANCODE_UNKNOWN;
        SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN;
    };

    std::array<bool, SDL_NUM_SCANCODES> m_keys{};
    std::array<bool, SDL_NUM_SCANCODES> m_previousKeys{};
    std::array<bool, static_cast<size_t>(Action::Count)> m_actionDown{};
    std::array<bool, static_cast<size_t>(Action::Count)> m_actionPressed{};
    std::array<Binding, static_cast<size_t>(Action::Count)> m_bindings{};

    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    float m_scrollDelta = 0.0f;
    float m_sensitivity = 0.11f;
    bool m_relativeMouse = false;

    void UpdateActionState();
};

} // namespace derecho::core
