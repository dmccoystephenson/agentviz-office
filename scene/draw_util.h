// draw_util.h: colours and primitive shapes shared by the office's drawing.
#pragma once

#include <algorithm>
#include <cmath>

#include "strata_kit.h"

namespace office {

using strata_kit::Host;
using strata_kit::Rgb;

constexpr float PI = 3.14159265f;

const Rgb kFloor{0.46f, 0.47f, 0.50f};
const Rgb kWall{0.80f, 0.78f, 0.74f};
const Rgb kGlass{0.55f, 0.75f, 0.85f};
const Rgb kFrame{0.22f, 0.23f, 0.26f};
const Rgb kWaitOrange{1.0f, 0.55f, 0.08f};
const Rgb kNote{1.0f, 0.88f, 0.15f};
const Rgb kInk{0.12f, 0.10f, 0.08f};
const Rgb kWhite{1.0f, 1.0f, 1.0f};

inline Rgb hsv(float h, float s, float v) {
    h = h - std::floor(h);
    const float i = std::floor(h * 6.0f), f = h * 6.0f - i;
    const float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
    switch (static_cast<int>(i) % 6) {
        case 0: return {v, t, p};
        case 1: return {q, v, p};
        case 2: return {p, v, t};
        case 3: return {p, q, v};
        case 4: return {t, p, v};
        default: return {v, p, q};
    }
}
inline Rgb mix(Rgb a, Rgb b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k}; }
inline float hash(float x) { return x * 12.9898f - std::floor(x * 12.9898f); }
// 0..1..0 at `rate` radians a second.
inline float wave(float t, float rate) { return 0.5f + 0.5f * std::sin(t * rate); }

// Session hue: the seed spread over a palette that avoids the orange/red/yellow
// band kept for waiting and warnings.
inline Rgb sessionHue(float seed, float s = 0.55f, float v = 0.62f) { return hsv(0.30f + seed * 0.62f, s, v); }

inline void box(Host& h, float x, float y, float z, float sx, float sy, float sz, Rgb c) {
    h.add(STRATA_PRIMITIVE_CUBE, x, y, z, sx, sy, sz, c);
}
inline void ball(Host& h, float x, float y, float z, float d, Rgb c) { h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, d, d, d, c); }
inline void ball(Host& h, const float* p, float d, Rgb c) { ball(h, p[0], p[1], p[2], d, c); }

// A chain of spheres from a to b: limbs and torsos that read the same from
// any facing (primitives are axis-aligned, spheres do not care).
inline void limb(Host& h, const float* a, const float* b, float d, Rgb c) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int n = std::max(2, static_cast<int>(std::ceil(len / (d * 0.42f))) + 1);
    for (int i = 0; i < n; ++i) {
        const float k = static_cast<float>(i) / (n - 1);
        ball(h, a[0] + dx * k, a[1] + dy * k, a[2] + dz * k, d, c);
    }
}

inline void ring(Host& h, float x, float y, float z, float r, Rgb c, float a, int segs = 28, float spin = 0.0f) {
    for (int k = 0; k < segs; ++k) {
        const float b0 = k * 2 * PI / segs + spin, b1 = (k + 1) * 2 * PI / segs + spin;
        h.line(x + std::cos(b0) * r, y, z + std::sin(b0) * r, x + std::cos(b1) * r, y, z + std::sin(b1) * r, c, a);
    }
}

// A rectangle outline on the floor, in wide lines.
inline void floorOutline(Host& h, float x0, float z0, float x1, float z1, float y, Rgb c, float px, float world,
                         float a) {
    h.wideLine(x0, y, z0, x1, y, z0, c, px, world, a);
    h.wideLine(x1, y, z0, x1, y, z1, c, px, world, a);
    h.wideLine(x1, y, z1, x0, y, z1, c, px, world, a);
    h.wideLine(x0, y, z1, x0, y, z0, c, px, world, a);
}

}  // namespace office
