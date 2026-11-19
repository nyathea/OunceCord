// SPDX-License-Identifier: MIT
//
// iOS-style scrolling physics for one scrollable area: a drag follows the
// finger (with rubber-band resistance past either end), a flick keeps
// gliding and slows down, and anything past an end springs back. The
// scroll offset itself stays owned by the caller; this keeps the motion.
// Platform-free.
#pragma once

#include <cstdint>
#include <deque>

class Scroller {
public:
    // Each frame, where the area is laid out: lo..hi are the offsets that
    // show content, view is the visible length (for the rubber band), dt in
    // seconds. Glides, springs back, or just clamps when at rest.
    void update(float &pos, float lo, float hi, float view, float dt);
    // Finger down: stops any glide and starts following.
    void grab();
    // Finger moved by delta (in offset units). With rubber off (mouse wheel)
    // the offset is clamped instead of stretching.
    void drag(float &pos, float delta, bool rubber = true);
    // Finger up, moving at velocity (offset units per second).
    void release(float velocity);
    // Stops all motion (when the controller moves the view).
    void stop();
    // Gliding or springing back.
    bool moving() const { return !m_held && (m_vel != 0 || m_settling); }
    bool held() const { return m_held; }

    // Tuning, close to UIScrollView's.
    static constexpr float kDecay = 2.0f;          // 1/s: velocity *= e^(-kDecay t)
    static constexpr float kStiffness = 230.0f;    // spring back (critically damped)
    static constexpr float kRubber = 0.55f;        // resistance coefficient
    static constexpr float kMinVelocity = 8.0f;    // px/s below which a glide stops
    static constexpr float kMaxVelocity = 6000.0f;
    // Speed allowed outwards once past an end: a glide arriving faster is
    // cut down to this, so the bounce stays small (about 25 px) however
    // hard the flick, and letting go of a stretched list doesn't throw it
    // further out.
    static constexpr float kMaxBounceVelocity = 1000.0f;

private:
    float m_vel = 0;
    float m_lo = 0, m_hi = 0, m_view = 600;
    bool m_held = false;
    bool m_settling = false;
};

// Recent pointer positions, to tell how fast a finger was moving when it
// let go.
class VelocityTracker {
public:
    void reset() { m_samples.clear(); }
    void add(uint64_t ms, float y);
    // Pixels per second over the last ~100 ms; 0 if the finger had stopped.
    float velocity(uint64_t now_ms) const;

private:
    struct Sample {
        uint64_t ms;
        float y;
    };
    std::deque<Sample> m_samples;
};
