// desk.h: one desk per agent: the monitor (what it is doing), keyboard,
// binder rack, the lead's lamp and mug, the chair, and the selection outline.
#pragma once

#include "draw_util.h"
#include "../vendor/agentviz-kit/scene/agentviz.h"

namespace office {

// Poses and screens: web/office-model.js POSE / SCREEN.
enum Pose {
    P_TYPE = 0, P_RUN = 1, P_READ = 2, P_WINDOW = 3, P_DELEGATE = 4, P_THINK = 5, P_OTHER = 6,
    P_SLUMP = 7, P_WAIT = 8, P_WALK = 9, P_REST = 10, P_ABSENT = 11, P_STAND = 12
};
enum Screen {
    S_OFF = 0, S_DIM = 1, S_EDIT = 2, S_RUN = 3, S_READ = 4, S_WEB = 5, S_DELEGATE = 6, S_THINK = 7,
    S_OTHER = 8, S_STALLED = 9, S_WAITING = 10, S_IDLE = 11
};

// The office's reading of one wire agent: the common fields plus its extras
// (web/office-model.js, agentExtras).
struct Desk {
    float dx, dz;           // the desk
    bool lead;
    float hue, own;         // the session's seed, the agent's own
    float appear, px, pz, facing;  // where the person is (walking, standing)
    int pose;
    float walk, lamp;
    int screen;
    float push;
    bool jacket;
    int selected;           // 0 none, 1 selected, 2 hovered
    float waiting, personAppear;
    bool stalled, binderOut;
    agentviz::WaitingReason reason;
};

inline Desk deskOf(const agentviz::Agent& a) {
    const float* e = a.extra;
    Desk d{};
    d.dx = e[0]; d.dz = e[1]; d.lead = a.lead(); d.hue = a.parentSeed; d.own = a.seed;
    d.appear = agentviz::clamp01(a.appear); d.px = a.x; d.pz = a.z; d.facing = a.facing;
    d.pose = agentviz::toInt(e[2]); d.walk = e[3]; d.lamp = agentviz::clamp01(e[4]);
    d.screen = agentviz::toInt(e[5]); d.push = agentviz::clamp01(e[6]); d.jacket = e[7] > 0.5f;
    d.selected = static_cast<int>(a.highlight); d.waiting = agentviz::clamp01(e[9]);
    d.personAppear = agentviz::clamp01(e[8]); d.stalled = a.stalled; d.binderOut = e[10] > 0.5f;
    d.reason = a.waitingReason;
    return d;
}

inline bool sitting(int pose) {
    return pose == P_TYPE || pose == P_RUN || pose == P_READ || pose == P_THINK || pose == P_OTHER ||
           pose == P_SLUMP || pose == P_REST;
}

inline float chairZ(const Desk& g) { return g.dz + 0.72f + 0.5f * g.push; }

// ---------------------------------------------------------------- the screen

struct ScreenStyle { Rgb base, ink; float glow; };

inline ScreenStyle screenStyle(int screen, float t) {
    switch (screen) {
        case S_DIM: return {{0.05f, 0.07f, 0.1f}, {0.8f, 0.9f, 1.0f}, 0.0f};
        case S_IDLE: return {{0.02f, 0.03f, 0.08f}, {0.8f, 0.9f, 1.0f}, 0.0f};
        case S_EDIT: return {{0.10f, 0.16f, 0.32f}, {0.75f, 0.88f, 1.0f}, 0.9f};
        case S_RUN: return {{0.02f, 0.06f, 0.03f}, {0.3f, 1.0f, 0.45f}, 0.7f};
        case S_READ: return {{0.85f, 0.84f, 0.78f}, {0.15f, 0.15f, 0.2f}, 0.6f};
        case S_WEB: return {{0.08f, 0.28f, 0.42f}, {0.6f, 0.95f, 1.0f}, 0.7f};
        case S_DELEGATE: return {{0.12f, 0.10f, 0.22f}, {0.85f, 0.7f, 1.0f}, 0.7f};
        case S_THINK: return {{0.10f, 0.08f, 0.18f}, {0.7f, 0.6f, 1.0f}, 0.5f};
        case S_OTHER: return {{0.20f, 0.14f, 0.05f}, {1.0f, 0.8f, 0.4f}, 0.6f};
        case S_STALLED: return {{0.35f + 0.45f * wave(t, 6.0f), 0.02f, 0.02f}, {1.0f, 0.85f, 0.85f}, 1.0f};
        case S_WAITING: return {{0.25f, 0.13f, 0.02f}, kNote, 0.7f};
        default: return {{0.03f, 0.03f, 0.04f}, {0.8f, 0.9f, 1.0f}, 0.0f};
    }
}

// The screen's drawing area: left, right, top, centre, and its z.
struct ScreenArea { float l, r, top, cx, cy, hh, z, ph; };

inline void screenEdit(Host& h, const ScreenArea& s, Rgb ink, float t) {  // code lines growing as it types
    for (int i = 0; i < 6; ++i) {
        const float y = s.top - i * 0.045f;
        const float indent = static_cast<int>(hash(s.ph + i) * 3) * 0.05f;
        float len = 0.15f + hash(s.ph + i * 1.7f) * 0.32f;
        if (i == 5) len *= std::fmod(t * 0.7f + s.ph, 1.0f);
        h.line(s.l + indent, y, s.z, std::min(s.r, s.l + indent + len), y, s.z, ink, 0.95f);
    }
    if (std::fmod(t * 2.0f, 1.0f) < 0.5f) h.line(s.l, s.top - 0.27f, s.z, s.l + 0.03f, s.top - 0.27f, s.z, ink, 1.0f);
}

inline void screenRun(Host& h, const ScreenArea& s, Rgb ink, float t) {  // terminal rows scrolling, flickering
    const float scroll = std::fmod(t * 5.0f + s.ph, 1.0f), flick = 0.6f + 0.4f * std::sin(t * 23.0f + s.ph);
    for (int i = 0; i < 6; ++i) {
        const float y = s.top - (i + scroll) * 0.045f;
        if (y < s.cy - s.hh * 0.5f + 0.03f) continue;
        const float len = 0.1f + hash(std::floor(t * 5.0f + s.ph) + i * 3.1f) * 0.4f;
        h.line(s.l, y, s.z, s.l + len, y, s.z, ink, flick);
    }
}

inline void screenRead(Host& h, const ScreenArea& s, Rgb ink, float t) {  // a page, a highlight bar sweeping down
    for (int i = 0; i < 6; ++i) {
        const float y = s.top - i * 0.045f;
        h.line(s.l + 0.02f, y, s.z, s.r - 0.02f - hash(s.ph + i) * 0.1f, y, s.z, ink, 0.8f);
    }
    const float sy = s.top - std::fmod(t * 0.12f + s.ph, 1.0f) * 0.27f;
    h.line(s.l, sy, s.z, s.r, sy, s.z, {0.2f, 0.5f, 1.0f}, 0.8f);
}

inline void screenWeb(Host& h, const ScreenArea& s, Rgb ink, float t) {  // a spinning globe
    const float r = 0.12f, spin = std::cos(t * 1.5f + s.ph);
    for (int k = 0; k < 20; ++k) {
        const float a0 = k * 2 * PI / 20, a1 = (k + 1) * 2 * PI / 20;
        const float y0 = s.cy + std::sin(a0) * r, y1 = s.cy + std::sin(a1) * r;
        h.line(s.cx + std::cos(a0) * r, y0, s.z, s.cx + std::cos(a1) * r, y1, s.z, ink, 0.95f);
        h.line(s.cx + spin * std::cos(a0) * r, y0, s.z, s.cx + spin * std::cos(a1) * r, y1, s.z, ink, 0.7f);
    }
    h.line(s.cx - r, s.cy, s.z, s.cx + r, s.cy, s.z, ink, 0.7f);
}

inline void screenDelegate(Host& h, const ScreenArea& s, Rgb ink, float t) {  // a board of lanes, one lit in turn
    const int lit = static_cast<int>(t * 3.0f + s.ph) % 6;
    for (int i = 0; i < 6; ++i) {
        const float tx = s.l + 0.03f + (i % 3) * 0.18f, ty = s.top - 0.02f - (i / 3) * 0.13f;
        const Rgb c = i == lit ? kWhite : ink;
        h.line(tx, ty, s.z, tx + 0.14f, ty, s.z, c, 0.9f);
        h.line(tx, ty - 0.09f, s.z, tx + 0.14f, ty - 0.09f, s.z, c, 0.9f);
        h.line(tx, ty, s.z, tx, ty - 0.09f, s.z, c, 0.9f);
        h.line(tx + 0.14f, ty, s.z, tx + 0.14f, ty - 0.09f, s.z, c, 0.9f);
    }
}

inline void screenThink(Host& h, const ScreenArea& s, Rgb ink, float t) {  // a slow wave
    for (int k = 0; k < 16; ++k) {
        const float x0 = s.l + (s.r - s.l) * k / 16.0f, x1 = s.l + (s.r - s.l) * (k + 1) / 16.0f;
        h.line(x0, s.cy + 0.06f * std::sin(x0 * 30.0f + t * 1.2f), s.z, x1,
               s.cy + 0.06f * std::sin(x1 * 30.0f + t * 1.2f), s.z, ink, 0.85f);
    }
}

inline void screenOther(Host& h, const ScreenArea& s, Rgb ink, float t) {  // bars rising and falling
    const float y0 = s.cy - s.hh * 0.5f + 0.03f;
    for (int i = 0; i < 6; ++i) {
        const float x = s.l + 0.05f + i * 0.09f, bh = 0.05f + 0.2f * wave(t * 3.0f + i + s.ph, 1.0f);
        h.wideLine(x, y0, s.z, x, y0 + bh, s.z, ink, 3.0f, 0.04f, 0.9f);
    }
}

inline void screenStalled(Host& h, const ScreenArea& s) {  // a warning triangle with a bang
    const float k = 0.12f, x = s.cx, y = s.cy;
    const Rgb c{1.0f, 0.9f, 0.2f};
    h.line(x, y + k, s.z, x - k, y - k * 0.8f, s.z, c, 1.0f);
    h.line(x - k, y - k * 0.8f, s.z, x + k, y - k * 0.8f, s.z, c, 1.0f);
    h.line(x + k, y - k * 0.8f, s.z, x, y + k, s.z, c, 1.0f);
    h.line(x, y + k * 0.5f, s.z, x, y - k * 0.25f, s.z, c, 1.0f);
    h.line(x, y - k * 0.5f, s.z, x, y - k * 0.6f, s.z, c, 1.0f);
}

inline void screenContent(Host& h, int screen, const ScreenArea& s, Rgb ink, float t) {
    switch (screen) {
        case S_EDIT: screenEdit(h, s, ink, t); break;
        case S_RUN: screenRun(h, s, ink, t); break;
        case S_READ: screenRead(h, s, ink, t); break;
        case S_WEB: screenWeb(h, s, ink, t); break;
        case S_DELEGATE: screenDelegate(h, s, ink, t); break;
        case S_THINK: screenThink(h, s, ink, t); break;
        case S_OTHER: screenOther(h, s, ink, t); break;
        case S_STALLED: screenStalled(h, s); break;
        case S_WAITING: box(h, s.cx + 0.12f, s.cy + 0.04f, s.z + 0.004f, 0.16f, 0.16f, 0.008f, kNote); break;
        default: break;
    }
}

// The monitor's face. Lines are unlit, so screens glow by night.
inline void drawScreen(Host& h, const Desk& g, float sink, float t) {
    const float w = 0.6f, hh = 0.32f, cy = 1.07f - sink;
    const ScreenStyle style = screenStyle(g.screen, t);
    box(h, g.dx, cy, g.dz - 0.17f, w + 0.04f, hh + 0.04f, 0.012f, style.base);
    if (sink > 0.3f) return;
    const ScreenArea area{g.dx - w * 0.5f + 0.04f, g.dx + w * 0.5f - 0.04f, cy + hh * 0.5f - 0.05f, g.dx, cy, hh,
                          g.dz - 0.17f + 0.008f, g.own * 17.0f};
    screenContent(h, g.screen, area, style.ink, t);
    if (style.glow > 0.0f) {
        const Rgb gc = g.screen == S_READ ? Rgb{0.9f, 0.9f, 0.85f} : mix(style.base, style.ink, 0.4f);
        h.point(g.dx, cy, g.dz - 0.05f, 40.0f, 0.9f, gc, 0.10f * style.glow);
    }
}

// ---------------------------------------------------------------- furniture

inline void drawJacket(Host& h, float x, float cz, float sink) {  // offline: a coat over the chair back
    const Rgb j{0.62f, 0.44f, 0.24f}, jd{0.5f, 0.34f, 0.18f};
    box(h, x, 0.92f - sink, cz + 0.3f, 0.6f, 0.62f, 0.12f, j);
    box(h, x, 0.72f - sink, cz + 0.17f, 0.56f, 0.5f, 0.08f, jd);
    box(h, x, 1.26f - sink, cz + 0.27f, 0.62f, 0.08f, 0.22f, j);       // collar over the top
    box(h, x - 0.3f, 0.66f - sink, cz + 0.05f, 0.1f, 0.5f, 0.12f, jd);  // a sleeve over the arm
    box(h, x + 0.3f, 0.7f - sink, cz + 0.25f, 0.1f, 0.5f, 0.12f, jd);
}

inline void drawChair(Host& h, const Desk& g, float sink) {
    const float cz = chairZ(g), x = g.dx;
    const Rgb seat = mix({0.12f, 0.12f, 0.14f}, sessionHue(g.hue, 0.6f, 0.45f), g.lead ? 0.75f : 0.45f);
    const float lean = g.pose == P_THINK ? 0.12f : (g.pose == P_REST ? 0.08f : 0.0f);
    h.add(STRATA_PRIMITIVE_CYLINDER, x, 0.03f - sink, cz, 0.55f, 0.02f, 0.55f, {0.1f, 0.1f, 0.11f});
    h.add(STRATA_PRIMITIVE_CYLINDER, x, 0.24f - sink, cz, 0.07f, 0.2f, 0.07f, {0.15f, 0.15f, 0.16f});
    box(h, x, 0.45f - sink, cz, 0.52f, 0.08f, 0.5f, seat);
    box(h, x, (g.lead ? 0.88f : 0.8f) - sink - lean * 0.3f, cz + 0.26f + lean, 0.5f, g.lead ? 0.75f : 0.6f, 0.07f, seat);
    if (g.jacket) drawJacket(h, x, cz, sink);
}

// Lead desks: a lamp (a Strata point light, on while busy) and a mug.
inline void drawLamp(Host& h, const Desk& g, float w, Rgb top, float sink, float night) {
    const float lx = g.dx - w * 0.5f + 0.22f, lz = g.dz - 0.18f;
    const Rgb metal{0.2f, 0.2f, 0.22f}, bulbOff{0.3f, 0.3f, 0.28f}, bulbOn{1.0f, 0.92f, 0.6f};
    h.add(STRATA_PRIMITIVE_CYLINDER, lx, 0.765f - sink, lz, 0.18f, 0.015f, 0.18f, {0.15f, 0.15f, 0.16f});
    box(h, lx, 1.0f - sink, lz, 0.03f, 0.46f, 0.03f, metal);
    box(h, lx + 0.08f, 1.23f - sink, lz, 0.18f, 0.03f, 0.03f, metal);
    h.add(STRATA_PRIMITIVE_CYLINDER, lx + 0.18f, 1.18f - sink, lz, 0.2f, 0.06f, 0.2f, sessionHue(g.hue, 0.5f, 0.55f));
    ball(h, lx + 0.18f, 1.11f - sink, lz, 0.09f, mix(bulbOff, bulbOn, g.lamp));
    if (g.lamp > 0.02f && sink < 0.2f) {
        h.point(lx + 0.18f, 1.08f - sink, lz, 18.0f, 0.35f, bulbOn, 0.35f * g.lamp);
        box(h, lx + 0.25f, 0.748f, lz + 0.12f, 0.5f, 0.004f, 0.4f, mix(top, {1.0f, 0.9f, 0.6f}, 0.45f * g.lamp));  // warm pool
        h.light(lx + 0.18f, 1.0f - sink, lz + 0.1f, 3.5f, {1.0f, 0.82f, 0.5f}, g.lamp * (0.5f + 1.0f * night),
                g.selected ? 6 : 2);
    }
    h.add(STRATA_PRIMITIVE_CYLINDER, g.dx + 0.55f, 0.8f - sink, g.dz + 0.2f, 0.08f, 0.05f, 0.08f,
          sessionHue(g.hue, 0.5f, 0.8f));
}

inline void drawDeskFrame(Host& h, const Desk& g, float w, float d, Rgb top, float sink) {
    const float x = g.dx, z = g.dz;
    const Rgb leg{0.25f, 0.25f, 0.27f}, dark{0.1f, 0.1f, 0.11f}, keys{0.2f, 0.2f, 0.22f};
    box(h, x, 0.72f - sink, z, w, 0.05f, d, top);
    box(h, x, 0.38f - sink, z - d * 0.5f + 0.04f, w - 0.1f, 0.64f, 0.04f, leg);
    box(h, x - w * 0.5f + 0.04f, 0.36f - sink, z, 0.05f, 0.7f, d - 0.06f, leg);
    box(h, x + w * 0.5f - 0.04f, 0.36f - sink, z, 0.05f, 0.7f, d - 0.06f, leg);
    box(h, x, 0.82f - sink, z - 0.22f, 0.07f, 0.16f, 0.07f, dark);  // monitor stand and bezel
    box(h, x, 0.76f - sink, z - 0.22f, 0.26f, 0.02f, 0.16f, dark);
    box(h, x, 1.07f - sink, z - 0.2f, 0.72f, 0.44f, 0.05f, {0.07f, 0.07f, 0.08f});
    box(h, x, 0.755f - sink, z + 0.12f, 0.46f, 0.02f, 0.15f, keys);  // keyboard and mouse
    box(h, x + 0.34f, 0.755f - sink, z + 0.14f, 0.07f, 0.025f, 0.1f, keys);
    const float bx = x + w * 0.5f - 0.24f;  // binder rack: three binders, one out while reading
    box(h, bx, 0.76f - sink, z - 0.1f, 0.34f, 0.02f, 0.32f, leg);
    for (int i = 0; i < 3; ++i) {
        if (i == 1 && g.binderOut) continue;
        box(h, bx - 0.1f + i * 0.1f, 0.93f - sink, z - 0.1f, 0.07f, 0.32f, 0.26f, hsv(hash(g.own + i * 0.37f), 0.6f, 0.55f));
    }
}

inline void drawStalledGlow(Host& h, const Desk& g, float t) {
    const float f = wave(t, 6.0f);
    h.light(g.dx, 1.1f, g.dz + 0.1f, 3.0f, {1.0f, 0.1f, 0.05f}, 0.6f + 1.2f * f, 7);
    h.point(g.dx, 1.07f, g.dz - 0.1f, 60.0f, 1.2f, {1.0f, 0.15f, 0.1f}, 0.25f + 0.2f * f);
}

inline void drawDesk(Host& h, const Desk& g, float t, float night) {
    if (g.appear <= 0.01f) return;
    const float sink = (1.0f - g.appear) * 1.4f, w = g.lead ? 1.9f : 1.4f, d = 0.8f;
    const Rgb top = g.lead ? Rgb{0.50f, 0.36f, 0.22f} : Rgb{0.68f, 0.66f, 0.62f};
    drawDeskFrame(h, g, w, d, top, sink);
    drawScreen(h, g, sink, t);
    if (g.lead) drawLamp(h, g, w, top, sink, night);
    if (g.stalled && sink < 0.2f) drawStalledGlow(h, g, t);
    drawChair(h, g, sink);
    if (g.selected) {  // a bright outline round the desk and chair
        const Rgb c = g.selected == 2 ? kWhite : Rgb{0.7f, 0.85f, 1.0f};
        floorOutline(h, g.dx - w * 0.5f - 0.15f, g.dz - d * 0.5f - 0.15f, g.dx + w * 0.5f + 0.15f, chairZ(g) + 0.45f,
                     0.04f, c, 3.0f, 0.05f, 0.95f);
    }
}

}  // namespace office
