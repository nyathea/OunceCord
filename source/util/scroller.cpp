// SPDX-License-Identifier: MIT
#include "util/scroller.h"

#include <algorithm>
#include <cmath>

void Scroller::grab()
{
    m_held = true;
    m_vel = 0;
    m_settling = false;
}

void Scroller::stop()
{
    m_vel = 0;
    m_settling = false;
}

void Scroller::release(float velocity)
{
    m_held = false;
    m_vel = std::clamp(velocity, -kMaxVelocity, kMaxVelocity);
    if (std::fabs(m_vel) < kMinVelocity * 4)
        m_vel = 0;
    m_settling = true;  // let update() spring back if past an end
}

void Scroller::drag(float &pos, float delta, bool rubber)
{
    if (!rubber) {
        pos = std::clamp(pos + delta, m_lo, std::max(m_lo, m_hi));
        m_vel = 0;
        return;
    }
    const float hi = std::max(m_lo, m_hi);
    const float view = std::max(1.0f, m_view);
    // Inside: the finger moves the content 1:1, up to the end.
    if (pos >= m_lo && pos <= hi) {
        float to = pos + delta;
        if (to >= m_lo && to <= hi) {
            pos = to;
            return;
        }
        float edge = to < m_lo ? m_lo : hi;
        delta -= edge - pos;
        pos = edge;
    }
    // Past an end: less movement the further out (UIScrollView's
    // d * (1 - 1 / (x * c / d + 1)), applied step by step).
    // How far past the nearer end (0 when sitting exactly on it).
    float over = pos < m_lo ? m_lo - pos : pos > hi ? pos - hi : 0;
    float factor = kRubber * std::pow(std::max(0.0f, 1 - over / view), 2.0f);
    pos += delta * factor;
}

void Scroller::update(float &pos, float lo, float hi, float view, float dt)
{
    m_lo = lo;
    m_hi = std::max(lo, hi);
    m_view = view;
    if (m_held)
        return;
    dt = std::clamp(dt, 0.0f, 0.1f);
    bool outside = pos < m_lo || pos > m_hi;
    if (m_vel == 0 && !outside) {
        m_settling = false;
        return;  // at rest
    }
    // Small fixed steps keep the spring stable at any frame rate.
    const float h = 1.0f / 240.0f;
    const float damping = 2 * std::sqrt(kStiffness);
    for (float t = 0; t < dt; t += h) {
        float step = std::min(h, dt - t);
        if (pos < m_lo || pos > m_hi) {
            float edge = pos < m_lo ? m_lo : m_hi;
            if (edge == m_lo)
                m_vel = std::max(m_vel, -kMaxBounceVelocity);
            else
                m_vel = std::min(m_vel, kMaxBounceVelocity);
            float a = -kStiffness * (pos - edge) - damping * m_vel;
            m_vel += a * step;
            pos += m_vel * step;
            // Close enough: rest exactly at the end.
            if (std::fabs(pos - edge) < 0.5f && std::fabs(m_vel) < 20) {
                pos = edge;
                m_vel = 0;
                break;
            }
            // Coming back in: arrive without bouncing inward.
            if ((edge == m_lo && pos >= m_lo) || (edge == m_hi && pos <= m_hi)) {
                pos = edge;
                m_vel = 0;
                break;
            }
        } else {
            m_vel *= std::exp(-kDecay * step);
            pos += m_vel * step;
            if (std::fabs(m_vel) < kMinVelocity) {
                m_vel = 0;
                if (pos >= m_lo && pos <= m_hi)
                    break;
            }
        }
    }
    if (m_vel == 0 && pos >= m_lo && pos <= m_hi)
        m_settling = false;
}

void VelocityTracker::add(uint64_t ms, float y)
{
    m_samples.push_back({ms, y});
    while (m_samples.size() > 2 && ms - m_samples.front().ms > 150)
        m_samples.pop_front();
}

float VelocityTracker::velocity(uint64_t now_ms) const
{
    if (m_samples.size() < 2)
        return 0;
    const Sample &last = m_samples.back();
    if (now_ms - last.ms > 60)
        return 0;  // held still before letting go
    // The oldest sample within 100 ms of the last.
    const Sample *first = &m_samples.front();
    for (const Sample &s : m_samples)
        if (last.ms - s.ms <= 100) {
            first = &s;
            break;
        }
    if (last.ms <= first->ms)
        return 0;
    return (last.y - first->y) * 1000.0f / (float)(last.ms - first->ms);
}
