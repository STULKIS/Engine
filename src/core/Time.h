// Frontier Engine - clocks and frame timing.
#pragma once

#include <cstdint>

namespace frontier::core {

// Monotonic high resolution clock. All deltas are in seconds.
class Clock {
public:
    Clock();

    // Seconds since the last call.
    float Tick();
    // Seconds since construction / Reset.
    double Elapsed() const;
    void Reset();

    uint64_t FrameCount() const { return m_frames; }
    double TotalTime() const { return m_elapsed; }

private:
    uint64_t m_start = 0;
    uint64_t m_last = 0;
    double m_elapsed = 0.0;
    uint64_t m_frames = 0;
};

// Smoothed frame statistics used by the debug overlay.
struct FrameStats {
    float deltaSeconds = 0.016f;
    float smoothedDelta = 0.016f;
    float fps = 60.0f;
    float smoothedFps = 60.0f;
    float minFrameMs = 0.0f;
    float maxFrameMs = 0.0f;
    double cpuFrameMs = 0.0;

    void Push(float dt, double cpuMs);
    void ResetWindow();
};

// A fixed-step accumulator for deterministic sub-systems (currently the
// time-of-day simulation and camera smoothing).
class FixedStep {
public:
    FixedStep(double step, int maxSteps = 8) : m_step(step), m_maxSteps(maxSteps) {}

    // Returns how many fixed steps to run this frame.
    int Consume(float deltaSeconds);

private:
    double m_step;
    double m_accumulator = 0.0;
    int m_maxSteps = 8;
};

} // namespace frontier::core
