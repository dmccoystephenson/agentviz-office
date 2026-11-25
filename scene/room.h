// room.h: the floor, the window walls, the owner's glass office and the
// carpets (pods and session blocks), from the wire's scene floats and zones.
#pragma once

#include "draw_util.h"
#include "../vendor/agentviz-kit/scene/agentviz.h"

namespace office {

// Wire scene floats (web/office.js, writeFrame).
struct Floor {
    float x0, zN, x1, zS, cx0, cx1, ox0, ox1, oz1, doorX, waiting, nightForce;
};

inline Floor floorOf(const agentviz::Frame& f) {
    const float* s = f.scene;
    return {s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10], s[11]};
}

// Zone kinds and extras (web/office.js).
constexpr int ZONE_POD = 0;
constexpr int ZONE_EXTRA_BUSY = 0, ZONE_EXTRA_WAITING = 1;

// A wall from a to b with a ribbon window from 0.9 to 2.1 and mullions.
inline void windowWall(Host& h, float ax, float az, float bx, float bz, float t, bool alongX) {
    const float len = alongX ? (bx - ax) : (bz - az);
    const float mx = (ax + bx) * 0.5f, mz = (az + bz) * 0.5f;
    const float sx = alongX ? len : t, sz = alongX ? t : len;
    box(h, mx, 0.45f, mz, sx, 0.9f, sz, kWall);
    box(h, mx, 2.35f, mz, sx, 0.5f, sz, kWall);
    box(h, mx, 1.5f, mz, alongX ? len : t * 0.3f, 1.2f, alongX ? t * 0.3f : len, kGlass);
    const int n = std::max(1, static_cast<int>(len / 2.0f));
    for (int i = 0; i <= n; ++i) {
        const float k = static_cast<float>(i) / n;
        const float x = alongX ? ax + len * k : ax, z = alongX ? az : az + len * k;
        box(h, x, 1.5f, z, alongX ? 0.08f : t * 1.1f, 1.2f, alongX ? t * 1.1f : 0.08f, kFrame);
    }
    box(h, mx, 0.92f, mz, alongX ? len : t * 1.6f, 0.05f, alongX ? t * 1.6f : len, kFrame);  // sill
}

inline void drawPlants(Host& h, const Floor& fl) {
    const float plants[][2] = {{fl.x0 + 0.7f, fl.zN + 0.7f}, {fl.x1 - 0.7f, fl.zN + 0.7f},
                               {fl.x0 + 0.7f, fl.zS - 0.7f}, {fl.x1 - 0.7f, fl.zS - 0.7f},
                               {fl.cx0 - 0.45f, fl.zS - 0.6f}, {fl.cx1 + 0.45f, fl.zS - 0.6f}};
    for (const auto& p : plants) {
        h.add(STRATA_PRIMITIVE_CYLINDER, p[0], 0.25f, p[1], 0.45f, 0.25f, 0.45f, {0.55f, 0.30f, 0.20f});
        ball(h, p[0], 0.8f, p[1], 0.75f, {0.18f, 0.42f, 0.16f});
        ball(h, p[0] + 0.12f, 1.1f, p[1] - 0.1f, 0.5f, {0.22f, 0.50f, 0.18f});
    }
}

// Ceiling lights by night: one soft fill over the floor (the lowest tier
// keeps its level as ambient).
inline void drawCeilingLight(Host& h, const Floor& fl, float night) {
    if (night <= 0.05f) return;
    StrataPointLight l{};
    l.position[0] = (fl.x0 + fl.x1) * 0.5f;
    l.position[1] = 7.0f;
    l.position[2] = (fl.zN + fl.zS) * 0.5f;
    l.range = std::max(fl.x1 - fl.x0, fl.zS - fl.zN) * 1.1f;
    l.color[0] = 0.85f; l.color[1] = 0.88f; l.color[2] = 1.0f;
    l.intensity = 0.6f * night;
    h.fill(l, {0.05f * night, 0.05f * night, 0.07f * night}, 3);
}

inline void drawRoom(Host& h, const Floor& fl, float night) {
    const float W = fl.x1 - fl.x0, D = fl.zS - fl.zN;
    box(h, (fl.x0 + fl.x1) * 0.5f, -0.25f, (fl.zN + fl.zS) * 0.5f, W + 0.6f, 0.5f, D + 0.6f, kFloor);  // slab, top at 0
    // The corridor runner: a wood strip leading to the owner's office.
    box(h, (fl.cx0 + fl.cx1) * 0.5f, 0.006f, (fl.oz1 + fl.zS) * 0.5f, fl.cx1 - fl.cx0, 0.012f, fl.zS - fl.oz1,
        {0.50f, 0.38f, 0.25f});
    // Walls north (the owner's office in the middle), west and east; the south
    // side is cut away so the camera sees in.
    const float th = 0.18f;
    windowWall(h, fl.x0, fl.zN, fl.ox0, fl.zN, th, true);
    windowWall(h, fl.ox1, fl.zN, fl.x1, fl.zN, th, true);
    box(h, (fl.ox0 + fl.ox1) * 0.5f, 1.3f, fl.zN, fl.ox1 - fl.ox0, 2.6f, th, kWall);
    windowWall(h, fl.x0, fl.zN, fl.x0, fl.zS, th, false);
    windowWall(h, fl.x1, fl.zN, fl.x1, fl.zS, th, false);
    box(h, (fl.x0 + fl.x1) * 0.5f, 0.05f, fl.zS, W, 0.1f, 0.08f, kFrame);  // baseboard on the open side
    drawPlants(h, fl);
    drawCeilingLight(h, fl, night);
}

inline void drawOwnerFurniture(Host& h, float cx, float z0, float z1, float x0, float x1) {
    const Rgb wood{0.25f, 0.15f, 0.08f}, chair{0.12f, 0.12f, 0.14f};
    box(h, cx, 0.008f, (z0 + z1) * 0.5f, x1 - x0 - 0.6f, 0.016f, z1 - z0 - 0.5f, {0.42f, 0.10f, 0.12f});  // rug
    box(h, cx, 0.74f, z0 + 1.3f, 2.4f, 0.07f, 1.0f, {0.30f, 0.18f, 0.10f});
    box(h, cx, 0.37f, z0 + 0.86f, 2.3f, 0.7f, 0.06f, wood);
    box(h, cx - 1.1f, 0.37f, z0 + 1.3f, 0.08f, 0.72f, 0.9f, wood);
    box(h, cx + 1.1f, 0.37f, z0 + 1.3f, 0.08f, 0.72f, 0.9f, wood);
    box(h, cx, 0.55f, z0 + 0.55f, 0.7f, 0.1f, 0.6f, chair);  // the big chair, empty: the owner is you
    box(h, cx, 1.05f, z0 + 0.3f, 0.7f, 0.9f, 0.1f, chair);
    box(h, cx + 0.6f, 1.0f, z0 + 1.25f, 0.6f, 0.38f, 0.04f, {0.08f, 0.08f, 0.1f});
    box(h, cx + 0.6f, 1.0f, z0 + 1.27f, 0.54f, 0.32f, 0.01f, {0.25f, 0.4f, 0.55f});
}

// Glass walls: frame posts and rails (orange while anyone waits), panes as faint lines.
inline void drawGlassWalls(Host& h, const Floor& fl, float d0, float d1, Rgb frame) {
    const float x0 = fl.ox0, x1 = fl.ox1, z0 = fl.zN, z1 = fl.oz1, cx = (x0 + x1) * 0.5f;
    const float posts[][2] = {{x0, z1}, {x1, z1}, {x0, z0}, {x1, z0}, {d0, z1}, {d1, z1}};
    for (const auto& p : posts) box(h, p[0], 1.3f, p[1], 0.12f, 2.6f, 0.12f, frame);
    box(h, cx, 2.6f, z1, x1 - x0, 0.1f, 0.12f, frame);
    box(h, x0, 2.6f, (z0 + z1) * 0.5f, 0.12f, 0.1f, z1 - z0, frame);
    box(h, x1, 2.6f, (z0 + z1) * 0.5f, 0.12f, 0.1f, z1 - z0, frame);
    box(h, cx, 0.05f, z1, x1 - x0, 0.1f, 0.1f, frame);
    for (float y = 0.4f; y < 2.6f; y += 0.55f) {
        h.line(x0, y, z1, d0, y, z1, kGlass, 0.5f);
        h.line(d1, y, z1, x1, y, z1, kGlass, 0.5f);
        h.line(x0, y, z0, x0, y, z1, kGlass, 0.5f);
        h.line(x1, y, z0, x1, y, z1, kGlass, 0.5f);
    }
}

// The door swung open, the queue mat, the sign, and the beacon while anyone waits.
inline void drawOwnerDoor(Host& h, const Floor& fl, float d1, float waiting, float pulse, float t) {
    const float z1 = fl.oz1, mz = z1 + 1.2f;
    box(h, d1 - 0.05f, 1.15f, z1 - 0.65f, 0.06f, 2.2f, 1.2f, {0.55f, 0.66f, 0.72f});
    box(h, fl.doorX, 0.01f, mz, 3.2f, 0.02f, 1.8f, {0.12f, 0.12f, 0.12f});
    for (int i = 0; i < 8; ++i)
        box(h, fl.doorX - 1.4f + i * 0.4f, 0.025f, mz, 0.18f, 0.02f, 1.6f, mix({0.45f, 0.40f, 0.1f}, kNote, waiting));
    box(h, fl.doorX, 2.95f, z1, 1.6f, 0.45f, 0.08f, mix({0.15f, 0.15f, 0.18f}, kWaitOrange, waiting * pulse));
    h.label(fl.doorX, 3.35f, z1, "OWNER", 14.0f, mix({0.9f, 0.9f, 0.9f}, kNote, waiting));
    if (waiting <= 0.01f) return;
    h.light(fl.doorX, 2.2f, z1 + 0.6f, 7.0f, kWaitOrange, waiting * (0.8f + 1.2f * pulse), 9);
    for (int i = 0; i < 12; ++i) {
        const float a = t * 1.5f + i * 2 * PI / 12;
        h.point(fl.doorX + std::cos(a) * 0.9f, 2.95f + std::sin(a * 2) * 0.1f, z1 + 0.15f, 9.0f, 0.12f, kNote,
                0.6f * waiting);
    }
}

inline void drawOwnerOffice(Host& h, const Floor& fl, float t) {
    const float cx = (fl.ox0 + fl.ox1) * 0.5f;
    const float waiting = agentviz::clamp01(fl.waiting), pulse = wave(t, 4.0f);
    const float doorW = 1.4f, d0 = fl.doorX - doorW * 0.5f, d1 = fl.doorX + doorW * 0.5f;
    drawOwnerFurniture(h, cx, fl.zN, fl.oz1, fl.ox0, fl.ox1);
    drawGlassWalls(h, fl, d0, d1, mix(kFrame, kWaitOrange, waiting * (0.6f + 0.4f * pulse)));
    drawOwnerDoor(h, fl, d1, waiting, pulse, t);
    h.light(cx - 0.8f, 1.6f, fl.zN + 1.3f, 5.0f, {1.0f, 0.85f, 0.6f}, 0.5f, 1);  // the owner's desk lamp
}

// A pod carpet in the place's hue, a low partition along its north edge with
// an activity strip: brighter when busy, orange while someone in it waits.
inline void drawPod(Host& h, const agentviz::Zone& z, float t) {
    const Rgb c = hsv(0.08f + z.seed * 0.8f, 0.18f, 0.42f);
    box(h, (z.x0 + z.x1) * 0.5f, 0.01f, (z.z0 + z.z1) * 0.5f, z.x1 - z.x0, 0.02f, z.z1 - z.z0, c);
    box(h, (z.x0 + z.x1) * 0.5f, 0.55f, z.z0, z.x1 - z.x0, 1.1f, 0.08f, mix(c, kWall, 0.5f));
    box(h, (z.x0 + z.x1) * 0.5f, 1.12f, z.z0, z.x1 - z.x0 + 0.04f, 0.04f, 0.12f, kFrame);
    Rgb strip = mix({0.25f, 0.27f, 0.3f}, {0.35f, 0.85f, 0.55f}, z.extra[ZONE_EXTRA_BUSY]);
    if (z.extra[ZONE_EXTRA_WAITING] > 0.5f) strip = mix(strip, kWaitOrange, wave(t, 4.0f));
    h.line(z.x0, 1.15f, z.z0 + 0.07f, z.x1, 1.15f, z.z0 + 0.07f, strip, 0.9f);
}

// A session block: a carpet with a border in the session's colour.
inline void drawBlock(Host& h, const agentviz::Zone& z) {
    const Rgb c = sessionHue(z.seed, 0.65f, 0.85f);
    box(h, (z.x0 + z.x1) * 0.5f, 0.018f, (z.z0 + z.z1) * 0.5f, z.x1 - z.x0, 0.012f, z.z1 - z.z0,
        mix(hsv(0.08f + z.seed, 0.1f, 0.45f), sessionHue(z.seed, 0.35f, 0.5f), 0.55f));
    const float y = 0.03f;
    h.line(z.x0, y, z.z0, z.x1, y, z.z0, c, 0.8f);
    h.line(z.x1, y, z.z0, z.x1, y, z.z1, c, 0.8f);
    h.line(z.x1, y, z.z1, z.x0, y, z.z1, c, 0.8f);
    h.line(z.x0, y, z.z1, z.x0, y, z.z0, c, 0.8f);
}

inline void drawZones(Host& h, const std::vector<agentviz::Zone>& zones, float t) {
    for (const auto& z : zones) {
        if (z.kind == ZONE_POD) drawPod(h, z, t);
        else drawBlock(h, z);
    }
}

}  // namespace office
