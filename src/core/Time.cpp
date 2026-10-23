#include "core/Time.h"

#include <algorithm>
#include <chrono>

namespace derecho::core {
namespace {

uint64_t NowTicks() {
    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
}

constexpr double kSecondsPerTick = 1.0e-9;

} // namespace

Clock::Clock() {
    m_start = NowTicks();
    m_last = m_start;
}

float Clock::Tick() {
    const uint64_t now = NowTicks();
    const double delta = static_cast<double>(now - m_last) * kSecondsPerTick;
    m_last = now;
    m_elapsed = static_cast<double>(now - m_start) * kSecondsPerTick;
    ++m_frames;
    // Clamp absurd deltas (debugger breakpoints, window drags) to 100 ms.
    return static_cast<float>(std::min(delta, 0.1));
}

double Clock::Elapsed() const {
    return static_cast<double>(NowTicks() - m_start) * kSecondsPerTick;
}

void Clock::Reset() {
    m_start = NowTicks();
    m_last = m_start;
    m_elapsed = 0.0;
    m_frames = 0;
}

void FrameStats::Push(float dt, double cpuMs) {
    deltaSeconds = dt;
    cpuFrameMs = cpuMs;
    const float frameMs = dt * 1000.0f;
    minFrameMs = (minFrameMs <= 0.0f) ? frameMs : std::min(minFrameMs, frameMs);
    maxFrameMs = std::max(maxFrameMs, frameMs);

    // Exponential moving average with ~0.1 s time constant.
    const float alpha = std::clamp(dt / 0.15f, 0.02f, 1.0f);
    smoothedDelta += (dt - smoothedDelta) * alpha;
    smoothedFps = smoothedDelta > 0.0001f ? 1.0f / smoothedDelta : 0.0f;
    fps = dt > 0.0001f ? 1.0f / dt : 0.0f;
}

void FrameStats::ResetWindow() {
    minFrameMs = 0.0f;
    maxFrameMs = 0.0f;
}

int FixedStep::Consume(float deltaSeconds) {
    if (deltaSeconds > 0.25f) {
        deltaSeconds = 0.25f;
    }
    m_accumulator += static_cast<double>(deltaSeconds);
    int steps = 0;
    while (m_accumulator >= m_step && steps < m_maxSteps) {
        m_accumulator -= m_step;
        ++steps;
    }
    if (steps == m_maxSteps) {
        // Drop the backlog rather than spiral.
        m_accumulator = 0.0;
    }
    return steps;
}

} // namespace derecho::core
