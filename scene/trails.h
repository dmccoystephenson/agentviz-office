// trails.h: lines on and over the floor, from the wire's links: the orange
// trail from a waiting lead's desk to the owner's door, and the notes a
// delegating lead sends to its subagents' desks.
#pragma once

#include "draw_util.h"
#include "../vendor/agentviz-kit/scene/agentviz.h"

namespace office {

// Link kinds (web/office-model.js).
constexpr int LINK_WAITING = 0;

// Orange dashes marching towards the owner's door; phase = distance along the route.
inline void drawWaitingTrail(Host& h, const agentviz::Link& l, float len, float t) {
    const float dx = l.x1 - l.x0, dz = l.z1 - l.z0, dash = 0.5f, step = 1.0f;
    const float phase = std::fmod(l.phase - t * 1.2f, step);
    for (float s = -phase; s < len; s += step) {
        const float s0 = std::max(0.0f, s), s1 = std::min(len, s + dash);
        if (s1 <= s0) continue;
        h.wideLine(l.x0 + dx * s0 / len, 0.035f, l.z0 + dz * s0 / len, l.x0 + dx * s1 / len, 0.035f,
                   l.z0 + dz * s1 / len, kWaitOrange, 3.0f, 0.12f, 0.85f * l.alpha);
    }
}

// A note flying along a faint arc from the lead to a lane, in the session's colour.
inline void drawDelegation(Host& h, const agentviz::Link& l, float t) {
    const Rgb c = sessionHue(l.seed, 0.6f, 1.0f);
    const float dx = l.x1 - l.x0, dz = l.z1 - l.z0;
    const auto height = [](float k) { return 1.4f + std::sin(k * PI) * 0.8f; };
    const float k = std::fmod(t * 0.6f + l.phase, 1.0f);
    h.point(l.x0 + dx * k, height(k), l.z0 + dz * k, 16.0f, 0.22f, c, l.alpha);
    for (int s = 0; s < 10; ++s) {
        const float k0 = s / 10.0f, k1 = (s + 1) / 10.0f;
        h.line(l.x0 + dx * k0, height(k0), l.z0 + dz * k0, l.x0 + dx * k1, height(k1), l.z0 + dz * k1, c, 0.6f * l.alpha);
    }
}

inline void drawLinks(Host& h, const std::vector<agentviz::Link>& links, float t) {
    for (const auto& l : links) {
        const float len = std::hypot(l.x1 - l.x0, l.z1 - l.z0);
        if (len < 0.01f || l.alpha <= 0.01f) continue;
        if (l.kind == LINK_WAITING) drawWaitingTrail(h, l, len, t);
        else drawDelegation(h, l, t);
    }
}

}  // namespace office
