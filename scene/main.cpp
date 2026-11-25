// agentviz-office: Claude Code sessions as an open-plan office floor.
//
// The page (web/office.js) owns the data and the layout: it reads the agent
// feed, places pods, desks and people, eases them between snapshots and walks
// people along aisles. Every frame it writes one float buffer and calls
// of_frame(); this file only draws what the buffer says.
//
// Buffer layout (floats):
//   [FLOOR_STRIDE]           the room: walls, corridor, the owner's office
//   [nRects  * RECT_STRIDE]  carpets: pods (one per place) and session blocks
//   [nAgents * AGENT_STRIDE] one desk + one person per session or subagent
//   [nTrails * TRAIL_STRIDE] floor trails: waiting (desk -> owner's door), delegation
#define STRATA_KIT_MAIN
#include "strata_kit.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

using strata_kit::Host;
using strata_kit::Rgb;

constexpr int FLOOR_STRIDE = 16;
constexpr int RECT_STRIDE = 8;
constexpr int AGENT_STRIDE = 24;
constexpr int TRAIL_STRIDE = 8;
constexpr float PI = 3.14159265f;

// Poses (web/office.js POSE).
enum Pose {
    P_TYPE = 0, P_RUN = 1, P_READ = 2, P_WINDOW = 3, P_DELEGATE = 4, P_THINK = 5, P_OTHER = 6,
    P_SLUMP = 7, P_WAIT = 8, P_WALK = 9, P_REST = 10, P_ABSENT = 11, P_STAND = 12
};
// Screens (web/office.js SCREEN).
enum Screen {
    S_OFF = 0, S_DIM = 1, S_EDIT = 2, S_RUN = 3, S_READ = 4, S_WEB = 5, S_DELEGATE = 6, S_THINK = 7,
    S_OTHER = 8, S_STALLED = 9, S_WAITING = 10, S_IDLE = 11
};

std::vector<float> buf;
int nRects = 0, nAgents = 0, nTrails = 0;

// ---------------------------------------------------------------- helpers

Rgb hsv(float h, float s, float v) {
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
Rgb mix(Rgb a, Rgb b, float k) { return {a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k}; }
Rgb scale(Rgb a, float k) { return {a.r * k, a.g * k, a.b * k}; }
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float hash(float x) { return x * 12.9898f - std::floor(x * 12.9898f); }

// Session hue: the seed spread over a palette that avoids the orange/red/yellow
// band kept for waiting and warnings.
Rgb sessionHue(float seed, float s = 0.55f, float v = 0.62f) {
    const float h = 0.30f + seed * 0.62f;  // green .. blue .. violet .. pink
    return hsv(h, s, v);
}

void box(Host& h, float x, float y, float z, float sx, float sy, float sz, Rgb c) {
    h.add(STRATA_PRIMITIVE_CUBE, x, y, z, sx, sy, sz, c);
}
void ball(Host& h, float x, float y, float z, float d, Rgb c) { h.add(STRATA_PRIMITIVE_SPHERE, x, y, z, d, d, d, c); }

// A chain of spheres from a to b: limbs and torsos that read the same from
// any facing (primitives are axis-aligned, spheres do not care).
void limb(Host& h, const float* a, const float* b, float d, Rgb c) {
    const float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int n = std::max(2, static_cast<int>(std::ceil(len / (d * 0.42f))) + 1);
    for (int i = 0; i < n; ++i) {
        const float k = static_cast<float>(i) / (n - 1);
        ball(h, a[0] + dx * k, a[1] + dy * k, a[2] + dz * k, d, c);
    }
}

void ring(Host& h, float x, float y, float z, float r, Rgb c, float a, int segs = 28, float spin = 0.0f) {
    for (int k = 0; k < segs; ++k) {
        const float b0 = k * 2 * PI / segs + spin, b1 = (k + 1) * 2 * PI / segs + spin;
        h.line(x + std::cos(b0) * r, y, z + std::sin(b0) * r, x + std::cos(b1) * r, y, z + std::sin(b1) * r, c, a);
    }
}

void wide(Host& h, float x0, float y0, float z0, float x1, float y1, float z1, Rgb c, float px, float world,
          float a) {
#if STRATA_ABI_MINOR >= 13
    h.wideLine(x0, y0, z0, x1, y1, z1, c, px, world, a);
#else
    (void)px; (void)world;
    h.line(x0, y0, z0, x1, y1, z1, c, a);
#endif
}

// ---------------------------------------------------------------- the room

const Rgb kFloor{0.46f, 0.47f, 0.50f};
const Rgb kWall{0.80f, 0.78f, 0.74f};
const Rgb kGlass{0.55f, 0.75f, 0.85f};
const Rgb kFrame{0.22f, 0.23f, 0.26f};
const Rgb kWaitOrange{1.0f, 0.55f, 0.08f};
const Rgb kNote{1.0f, 0.88f, 0.15f};

struct Floor {
    float x0, zN, x1, zS, cx0, cx1, ox0, ox1, oz1, doorX, waiting, nightForce;
};

Floor readFloor() {
    const float* f = buf.data();
    return {f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]};
}

void windowWall(Host& h, float ax, float az, float bx, float bz, float t, bool alongX) {
    // A wall from a to b with a ribbon window from 0.9 to 2.1 and mullions.
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

void drawRoom(Host& h, const Floor& fl, float t, float night) {
    const float W = fl.x1 - fl.x0, D = fl.zS - fl.zN;
    // Floor slab, top at y = 0.
    box(h, (fl.x0 + fl.x1) * 0.5f, -0.25f, (fl.zN + fl.zS) * 0.5f, W + 0.6f, 0.5f, D + 0.6f, kFloor);
    // The corridor runner: a wood strip leading to the owner's office.
    box(h, (fl.cx0 + fl.cx1) * 0.5f, 0.006f, (fl.oz1 + fl.zS) * 0.5f, fl.cx1 - fl.cx0, 0.012f, fl.zS - fl.oz1,
        {0.50f, 0.38f, 0.25f});
    // Walls: north (with the owner's office in the middle), west and east. The
    // south side is cut away so the camera sees in.
    const float th = 0.18f;
    windowWall(h, fl.x0, fl.zN, fl.ox0, fl.zN, th, true);
    windowWall(h, fl.ox1, fl.zN, fl.x1, fl.zN, th, true);
    box(h, (fl.ox0 + fl.ox1) * 0.5f, 1.3f, fl.zN, fl.ox1 - fl.ox0, 2.6f, th, kWall);
    windowWall(h, fl.x0, fl.zN, fl.x0, fl.zS, th, false);
    windowWall(h, fl.x1, fl.zN, fl.x1, fl.zS, th, false);
    // Baseboard along the cut-away south edge.
    box(h, (fl.x0 + fl.x1) * 0.5f, 0.05f, fl.zS, W, 0.1f, 0.08f, kFrame);
    // Plants in the corners and along the corridor mouth.
    const float plants[][2] = {{fl.x0 + 0.7f, fl.zN + 0.7f}, {fl.x1 - 0.7f, fl.zN + 0.7f},
                               {fl.x0 + 0.7f, fl.zS - 0.7f}, {fl.x1 - 0.7f, fl.zS - 0.7f},
                               {fl.cx0 - 0.45f, fl.zS - 0.6f}, {fl.cx1 + 0.45f, fl.zS - 0.6f}};
    for (const auto& p : plants) {
        h.add(STRATA_PRIMITIVE_CYLINDER, p[0], 0.25f, p[1], 0.45f, 0.25f, 0.45f, {0.55f, 0.30f, 0.20f});
        ball(h, p[0], 0.8f, p[1], 0.75f, {0.18f, 0.42f, 0.16f});
        ball(h, p[0] + 0.12f, 1.1f, p[1] - 0.1f, 0.5f, {0.22f, 0.50f, 0.18f});
    }
    // Ceiling lights by night: soft panels over the floor (fills, so the
    // lowest tier keeps the level as ambient).
    if (night > 0.05f) {
        StrataPointLight l{};
        l.position[0] = (fl.x0 + fl.x1) * 0.5f;
        l.position[1] = 7.0f;
        l.position[2] = (fl.zN + fl.zS) * 0.5f;
        l.range = std::max(W, D) * 1.1f;
        l.color[0] = 0.85f; l.color[1] = 0.88f; l.color[2] = 1.0f;
        l.intensity = 0.6f * night;
        h.fill(l, {0.05f * night, 0.05f * night, 0.07f * night}, 3);
    }
    (void)t;
}

void drawOwnerOffice(Host& h, const Floor& fl, float t) {
    const float x0 = fl.ox0, x1 = fl.ox1, z0 = fl.zN, z1 = fl.oz1, cx = (x0 + x1) * 0.5f;
    const float waiting = clamp01(fl.waiting);
    const float pulse = 0.5f + 0.5f * std::sin(t * 4.0f);
    // Rug and furniture.
    box(h, cx, 0.008f, (z0 + z1) * 0.5f, x1 - x0 - 0.6f, 0.016f, z1 - z0 - 0.5f, {0.42f, 0.10f, 0.12f});
    box(h, cx, 0.74f, z0 + 1.3f, 2.4f, 0.07f, 1.0f, {0.30f, 0.18f, 0.10f});
    box(h, cx, 0.37f, z0 + 0.86f, 2.3f, 0.7f, 0.06f, {0.25f, 0.15f, 0.08f});
    box(h, cx - 1.1f, 0.37f, z0 + 1.3f, 0.08f, 0.72f, 0.9f, {0.25f, 0.15f, 0.08f});
    box(h, cx + 1.1f, 0.37f, z0 + 1.3f, 0.08f, 0.72f, 0.9f, {0.25f, 0.15f, 0.08f});
    box(h, cx, 0.55f, z0 + 0.55f, 0.7f, 0.1f, 0.6f, {0.12f, 0.12f, 0.14f});   // big chair, empty: the owner is you
    box(h, cx, 1.05f, z0 + 0.3f, 0.7f, 0.9f, 0.1f, {0.12f, 0.12f, 0.14f});
    box(h, cx + 0.6f, 1.0f, z0 + 1.25f, 0.6f, 0.38f, 0.04f, {0.08f, 0.08f, 0.1f});
    box(h, cx + 0.6f, 1.0f, z0 + 1.27f, 0.54f, 0.32f, 0.01f, {0.25f, 0.4f, 0.55f});
    // Glass walls: frame posts and rails, panes as faint lines.
    const float posts[][2] = {{x0, z1}, {x1, z1}, {x0, z0}, {x1, z0}};
    const float doorW = 1.4f, d0 = fl.doorX - doorW * 0.5f, d1 = fl.doorX + doorW * 0.5f;
    Rgb frame = mix(kFrame, kWaitOrange, waiting * (0.6f + 0.4f * pulse));
    for (const auto& p : posts) box(h, p[0], 1.3f, p[1], 0.12f, 2.6f, 0.12f, frame);
    box(h, d0, 1.3f, z1, 0.12f, 2.6f, 0.12f, frame);
    box(h, d1, 1.3f, z1, 0.12f, 2.6f, 0.12f, frame);
    box(h, cx, 2.6f, z1, x1 - x0, 0.1f, 0.12f, frame);
    box(h, x0, 2.6f, (z0 + z1) * 0.5f, 0.12f, 0.1f, z1 - z0, frame);
    box(h, x1, 2.6f, (z0 + z1) * 0.5f, 0.12f, 0.1f, z1 - z0, frame);
    box(h, cx, 0.05f, z1, x1 - x0, 0.1f, 0.1f, frame);
    // Low glass sills so the box reads as a room.
    for (float y = 0.4f; y < 2.6f; y += 0.55f) {
        h.line(x0, y, z1, d0, y, z1, kGlass, 0.5f);
        h.line(d1, y, z1, x1, y, z1, kGlass, 0.5f);
        h.line(x0, y, z0, x0, y, z1, kGlass, 0.5f);
        h.line(x1, y, z0, x1, y, z1, kGlass, 0.5f);
    }
    // The door, swung open inward, and the queue mat before it.
    box(h, d1 - 0.05f, 1.15f, z1 - 0.65f, 0.06f, 2.2f, 1.2f, {0.55f, 0.66f, 0.72f});
    const float mz = z1 + 1.2f;
    box(h, fl.doorX, 0.01f, mz, 3.2f, 0.02f, 1.8f, {0.12f, 0.12f, 0.12f});
    for (int i = 0; i < 8; ++i)
        box(h, fl.doorX - 1.4f + i * 0.4f, 0.025f, mz, 0.18f, 0.02f, 1.6f, mix({0.45f, 0.40f, 0.1f}, kNote, waiting));
    // Sign over the door.
    box(h, fl.doorX, 2.95f, z1, 1.6f, 0.45f, 0.08f, mix({0.15f, 0.15f, 0.18f}, kWaitOrange, waiting * pulse));
#if STRATA_ABI_MINOR >= 14
    h.label(fl.doorX, 3.35f, z1, "OWNER", 14.0f, mix({0.9f, 0.9f, 0.9f}, kNote, waiting));
#endif
    // Desk lamp in the owner's office; the door burns orange while anyone waits.
    h.light(cx - 0.8f, 1.6f, z0 + 1.3f, 5.0f, {1.0f, 0.85f, 0.6f}, 0.5f, 1);
    if (waiting > 0.01f) {
        h.light(fl.doorX, 2.2f, z1 + 0.6f, 7.0f, kWaitOrange, waiting * (0.8f + 1.2f * pulse), 9);
        for (int i = 0; i < 12; ++i) {
            const float a = t * 1.5f + i * 2 * PI / 12;
            h.point(fl.doorX + std::cos(a) * 0.9f, 2.95f + std::sin(a * 2) * 0.1f, z1 + 0.15f, 9.0f, 0.12f, kNote,
                    0.6f * waiting);
        }
    }
}

// ---------------------------------------------------------------- carpets

void drawRects(Host& h, const float* r, int n, float t) {
    for (int i = 0; i < n; ++i, r += RECT_STRIDE) {
        const float x0 = r[0], z0 = r[1], x1 = r[2], z1 = r[3], kind = r[4], seed = r[5], busy = r[6],
                    waiting = r[7];
        if (kind < 0.5f) {  // pod carpet: place hue, muted
            const Rgb c = hsv(0.08f + seed * 0.8f, 0.18f, 0.42f);
            box(h, (x0 + x1) * 0.5f, 0.01f, (z0 + z1) * 0.5f, x1 - x0, 0.02f, z1 - z0, c);
            // Low partition along the pod's north edge with a name plate post.
            box(h, (x0 + x1) * 0.5f, 0.55f, z0, x1 - x0, 1.1f, 0.08f, mix(c, kWall, 0.5f));
            box(h, (x0 + x1) * 0.5f, 1.12f, z0, x1 - x0 + 0.04f, 0.04f, 0.12f, kFrame);
            // Activity strip on the partition top: brighter when the pod is busy,
            // orange when someone in it waits on the owner.
            const float pulse = 0.5f + 0.5f * std::sin(t * 4.0f);
            Rgb strip = mix({0.25f, 0.27f, 0.3f}, {0.35f, 0.85f, 0.55f}, busy);
            if (waiting > 0.5f) strip = mix(strip, kWaitOrange, 0.5f + 0.5f * pulse);
            h.line(x0, 1.15f, z0 + 0.07f, x1, 1.15f, z0 + 0.07f, strip, 0.9f);
        } else {  // session block: a border in the session's colour
            const Rgb c = sessionHue(seed, 0.65f, 0.85f);
            const float y = 0.03f;
            box(h, (x0 + x1) * 0.5f, 0.018f, (z0 + z1) * 0.5f, x1 - x0, 0.012f, z1 - z0,
                mix(hsv(0.08f + seed, 0.1f, 0.45f), sessionHue(seed, 0.35f, 0.5f), 0.55f));
            h.line(x0, y, z0, x1, y, z0, c, 0.8f);
            h.line(x1, y, z0, x1, y, z1, c, 0.8f);
            h.line(x1, y, z1, x0, y, z1, c, 0.8f);
            h.line(x0, y, z1, x0, y, z0, c, 0.8f);
        }
    }
}

// ---------------------------------------------------------------- desks

struct Agent {
    float dx, dz;
    bool lead;
    float hue, own, appear, px, pz, facing;
    int pose;
    float walk, lamp;
    int screen;
    float push;
    bool jacket;
    int selected;
    float waiting, personAppear;
    bool stalled, binderOut;
};

Agent readAgent(const float* a) {
    Agent g{};
    g.dx = a[0]; g.dz = a[1]; g.lead = a[2] > 0.5f; g.hue = a[3]; g.own = a[4]; g.appear = clamp01(a[5]);
    g.px = a[6]; g.pz = a[7]; g.facing = a[8]; g.pose = static_cast<int>(a[9] + 0.5f); g.walk = a[10];
    g.lamp = clamp01(a[11]); g.screen = static_cast<int>(a[12] + 0.5f); g.push = clamp01(a[13]);
    g.jacket = a[14] > 0.5f; g.selected = static_cast<int>(a[15] + 0.5f); g.waiting = clamp01(a[16]);
    g.personAppear = clamp01(a[17]); g.stalled = a[19] > 0.5f; g.binderOut = a[20] > 0.5f;
    return g;
}

bool sitting(int pose) {
    return pose == P_TYPE || pose == P_RUN || pose == P_READ || pose == P_THINK || pose == P_OTHER ||
           pose == P_SLUMP || pose == P_REST;
}

float chairZ(const Agent& g) { return g.dz + 0.72f + 0.5f * g.push; }

// The monitor's face, and the lines drawn on it per screen mode. Lines are
// unlit, so screens glow by night.
void drawScreen(Host& h, const Agent& g, float sink, float t) {
    const float cx = g.dx, cy = 1.07f - sink, z = g.dz - 0.17f + 0.008f;
    const float w = 0.6f, hh = 0.32f;
    const float ph = g.own * 17.0f;
    Rgb base{0.03f, 0.03f, 0.04f};
    Rgb ink{0.8f, 0.9f, 1.0f};
    float glow = 0.0f;
    switch (g.screen) {
        case S_DIM: base = {0.05f, 0.07f, 0.1f}; break;
        case S_IDLE: base = {0.02f, 0.03f, 0.08f}; break;
        case S_EDIT: base = {0.10f, 0.16f, 0.32f}; ink = {0.75f, 0.88f, 1.0f}; glow = 0.9f; break;
        case S_RUN: base = {0.02f, 0.06f, 0.03f}; ink = {0.3f, 1.0f, 0.45f}; glow = 0.7f; break;
        case S_READ: base = {0.85f, 0.84f, 0.78f}; ink = {0.15f, 0.15f, 0.2f}; glow = 0.6f; break;
        case S_WEB: base = {0.08f, 0.28f, 0.42f}; ink = {0.6f, 0.95f, 1.0f}; glow = 0.7f; break;
        case S_DELEGATE: base = {0.12f, 0.10f, 0.22f}; ink = {0.85f, 0.7f, 1.0f}; glow = 0.7f; break;
        case S_THINK: base = {0.10f, 0.08f, 0.18f}; ink = {0.7f, 0.6f, 1.0f}; glow = 0.5f; break;
        case S_OTHER: base = {0.20f, 0.14f, 0.05f}; ink = {1.0f, 0.8f, 0.4f}; glow = 0.6f; break;
        case S_STALLED: {
            const float f = 0.5f + 0.5f * std::sin(t * 6.0f);
            base = {0.35f + 0.45f * f, 0.02f, 0.02f};
            ink = {1.0f, 0.85f, 0.85f};
            glow = 1.0f;
            break;
        }
        case S_WAITING: base = {0.25f, 0.13f, 0.02f}; ink = kNote; glow = 0.7f; break;
        default: break;
    }
    box(h, cx, cy, g.dz - 0.17f, w + 0.04f, hh + 0.04f, 0.012f, base);
    if (sink > 0.3f) return;
    const float l = cx - w * 0.5f + 0.04f, r = cx + w * 0.5f - 0.04f, top = cy + hh * 0.5f - 0.05f;
    switch (g.screen) {
        case S_EDIT: {  // code lines growing as the agent types
            for (int i = 0; i < 6; ++i) {
                const float y = top - i * 0.045f;
                const float indent = (static_cast<int>(hash(ph + i) * 3)) * 0.05f;
                float len = 0.15f + hash(ph + i * 1.7f) * 0.32f;
                if (i == 5) len *= std::fmod(t * 0.7f + ph, 1.0f);
                h.line(l + indent, y, z, std::min(r, l + indent + len), y, z, ink, 0.95f);
            }
            if (std::fmod(t * 2.0f, 1.0f) < 0.5f) h.line(l, top - 0.27f, z, l + 0.03f, top - 0.27f, z, ink, 1.0f);
            break;
        }
        case S_RUN: {  // terminal: scrolling green rows with a flicker
            const float scroll = std::fmod(t * 5.0f + ph, 1.0f);
            const float flick = 0.6f + 0.4f * std::sin(t * 23.0f + ph);
            for (int i = 0; i < 6; ++i) {
                const float y = top - (i + scroll) * 0.045f;
                if (y < cy - hh * 0.5f + 0.03f) continue;
                const float len = 0.1f + hash(std::floor(t * 5.0f + ph) + i * 3.1f) * 0.4f;
                h.line(l, y, z, l + len, y, z, ink, flick);
            }
            break;
        }
        case S_READ: {  // a page: dark text rows, a highlight bar sweeping down
            for (int i = 0; i < 6; ++i) {
                const float y = top - i * 0.045f;
                h.line(l + 0.02f, y, z, r - 0.02f - hash(ph + i) * 0.1f, y, z, ink, 0.8f);
            }
            const float sy = top - std::fmod(t * 0.12f + ph, 1.0f) * 0.27f;
            h.line(l, sy, z, r, sy, z, {0.2f, 0.5f, 1.0f}, 0.8f);
            break;
        }
        case S_WEB: {  // a spinning globe
            const float gx = cx, gy = cy;
            for (int k = 0; k < 20; ++k) {
                const float a0 = k * 2 * PI / 20, a1 = (k + 1) * 2 * PI / 20;
                h.line(gx + std::cos(a0) * 0.12f, gy + std::sin(a0) * 0.12f, z, gx + std::cos(a1) * 0.12f,
                       gy + std::sin(a1) * 0.12f, z, ink, 0.95f);
                const float s = std::cos(t * 1.5f + ph);
                h.line(gx + s * std::cos(a0) * 0.12f, gy + std::sin(a0) * 0.12f, z, gx + s * std::cos(a1) * 0.12f,
                       gy + std::sin(a1) * 0.12f, z, ink, 0.7f);
            }
            h.line(gx - 0.12f, gy, z, gx + 0.12f, gy, z, ink, 0.7f);
            break;
        }
        case S_DELEGATE: {  // tiles, one lit in turn: a board of lanes
            const int lit = static_cast<int>(t * 3.0f + ph) % 6;
            for (int i = 0; i < 6; ++i) {
                const float tx = l + 0.03f + (i % 3) * 0.18f, ty = top - 0.02f - (i / 3) * 0.13f;
                const Rgb c = i == lit ? Rgb{1.0f, 1.0f, 1.0f} : ink;
                h.line(tx, ty, z, tx + 0.14f, ty, z, c, 0.9f);
                h.line(tx, ty - 0.09f, z, tx + 0.14f, ty - 0.09f, z, c, 0.9f);
                h.line(tx, ty, z, tx, ty - 0.09f, z, c, 0.9f);
                h.line(tx + 0.14f, ty, z, tx + 0.14f, ty - 0.09f, z, c, 0.9f);
            }
            break;
        }
        case S_THINK: {  // a slow wave
            for (int k = 0; k < 16; ++k) {
                const float x0 = l + (r - l) * k / 16.0f, x1 = l + (r - l) * (k + 1) / 16.0f;
                const float y0 = cy + 0.06f * std::sin(x0 * 30.0f + t * 1.2f), y1 = cy + 0.06f * std::sin(x1 * 30.0f + t * 1.2f);
                h.line(x0, y0, z, x1, y1, z, ink, 0.85f);
            }
            break;
        }
        case S_OTHER: {  // bars rising and falling
            for (int i = 0; i < 6; ++i) {
                const float x = l + 0.05f + i * 0.09f;
                const float bh = 0.05f + 0.2f * (0.5f + 0.5f * std::sin(t * 3.0f + i + ph));
                wide(h, x, cy - hh * 0.5f + 0.03f, z, x, cy - hh * 0.5f + 0.03f + bh, z, ink, 3.0f, 0.04f, 0.9f);
            }
            break;
        }
        case S_STALLED: {  // a warning triangle with a bang
            const float s = 0.12f;
            const Rgb c{1.0f, 0.9f, 0.2f};
            h.line(cx, cy + s, z, cx - s, cy - s * 0.8f, z, c, 1.0f);
            h.line(cx - s, cy - s * 0.8f, z, cx + s, cy - s * 0.8f, z, c, 1.0f);
            h.line(cx + s, cy - s * 0.8f, z, cx, cy + s, z, c, 1.0f);
            h.line(cx, cy + s * 0.5f, z, cx, cy - s * 0.25f, z, c, 1.0f);
            h.line(cx, cy - s * 0.5f, z, cx, cy - s * 0.6f, z, c, 1.0f);
            break;
        }
        case S_WAITING: {  // a sticky note on the screen
            box(h, cx + 0.12f, cy + 0.04f, z + 0.004f, 0.16f, 0.16f, 0.008f, kNote);
            break;
        }
        default: break;
    }
    if (glow > 0.0f) {
        const Rgb gc = g.screen == S_READ ? Rgb{0.9f, 0.9f, 0.85f} : mix(base, ink, 0.4f);
        h.point(cx, cy, g.dz - 0.05f, 40.0f, 0.9f, gc, 0.10f * glow);
    }
}

void drawChair(Host& h, const Agent& g, float sink) {
    const float cz = chairZ(g), x = g.dx;
    const Rgb seat = mix({0.12f, 0.12f, 0.14f}, sessionHue(g.hue, 0.6f, 0.45f), g.lead ? 0.75f : 0.45f);
    const float lean = (g.pose == P_THINK) ? 0.12f : (g.pose == P_REST ? 0.08f : 0.0f);
    h.add(STRATA_PRIMITIVE_CYLINDER, x, 0.03f - sink, cz, 0.55f, 0.02f, 0.55f, {0.1f, 0.1f, 0.11f});
    h.add(STRATA_PRIMITIVE_CYLINDER, x, 0.24f - sink, cz, 0.07f, 0.2f, 0.07f, {0.15f, 0.15f, 0.16f});
    box(h, x, 0.45f - sink, cz, 0.52f, 0.08f, 0.5f, seat);
    box(h, x, (g.lead ? 0.88f : 0.8f) - sink - lean * 0.3f, cz + 0.26f + lean, 0.5f, g.lead ? 0.75f : 0.6f, 0.07f,
        seat);
    if (g.jacket) {  // offline: a coat left over the chair back
        const Rgb j{0.62f, 0.44f, 0.24f};
        const Rgb jd{0.5f, 0.34f, 0.18f};
        box(h, x, 0.92f - sink, cz + 0.3f, 0.6f, 0.62f, 0.12f, j);
        box(h, x, 0.72f - sink, cz + 0.17f, 0.56f, 0.5f, 0.08f, jd);
        box(h, x, 1.26f - sink, cz + 0.27f, 0.62f, 0.08f, 0.22f, j);  // collar over the top
        box(h, x - 0.3f, 0.66f - sink, cz + 0.05f, 0.1f, 0.5f, 0.12f, jd);  // a sleeve over the arm
        box(h, x + 0.3f, 0.7f - sink, cz + 0.25f, 0.1f, 0.5f, 0.12f, jd);
    }
}

void drawDesk(Host& h, const Agent& g, float t, float night) {
    const float sink = (1.0f - g.appear) * 1.4f;
    if (g.appear <= 0.01f) return;
    const float w = g.lead ? 1.9f : 1.4f, d = 0.8f, x = g.dx, z = g.dz;
    const Rgb top = g.lead ? Rgb{0.50f, 0.36f, 0.22f} : Rgb{0.68f, 0.66f, 0.62f};
    const Rgb leg{0.25f, 0.25f, 0.27f};
    box(h, x, 0.72f - sink, z, w, 0.05f, d, top);
    box(h, x, 0.38f - sink, z - d * 0.5f + 0.04f, w - 0.1f, 0.64f, 0.04f, leg);
    box(h, x - w * 0.5f + 0.04f, 0.36f - sink, z, 0.05f, 0.7f, d - 0.06f, leg);
    box(h, x + w * 0.5f - 0.04f, 0.36f - sink, z, 0.05f, 0.7f, d - 0.06f, leg);
    // Monitor.
    box(h, x, 0.82f - sink, z - 0.22f, 0.07f, 0.16f, 0.07f, {0.1f, 0.1f, 0.11f});
    box(h, x, 0.76f - sink, z - 0.22f, 0.26f, 0.02f, 0.16f, {0.1f, 0.1f, 0.11f});
    box(h, x, 1.07f - sink, z - 0.2f, 0.72f, 0.44f, 0.05f, {0.07f, 0.07f, 0.08f});
    drawScreen(h, g, sink, t);
    // Keyboard and mouse.
    box(h, x, 0.755f - sink, z + 0.12f, 0.46f, 0.02f, 0.15f, {0.2f, 0.2f, 0.22f});
    box(h, x + 0.34f, 0.755f - sink, z + 0.14f, 0.07f, 0.025f, 0.1f, {0.2f, 0.2f, 0.22f});
    // Binder rack on the right end: three binders, one out while reading.
    const float bx = x + w * 0.5f - 0.24f;
    box(h, bx, 0.76f - sink, z - 0.1f, 0.34f, 0.02f, 0.32f, leg);
    for (int i = 0; i < 3; ++i) {
        if (i == 1 && g.binderOut) continue;
        const Rgb bc = hsv(hash(g.own + i * 0.37f), 0.6f, 0.55f);
        box(h, bx - 0.1f + i * 0.1f, 0.93f - sink, z - 0.1f, 0.07f, 0.32f, 0.26f, bc);
    }
    // Lead desks: a lamp (a Strata point light, on while busy) and a mug.
    if (g.lead) {
        const float lx = x - w * 0.5f + 0.22f, lz = z - 0.18f;
        h.add(STRATA_PRIMITIVE_CYLINDER, lx, 0.765f - sink, lz, 0.18f, 0.015f, 0.18f, {0.15f, 0.15f, 0.16f});
        box(h, lx, 1.0f - sink, lz, 0.03f, 0.46f, 0.03f, {0.2f, 0.2f, 0.22f});
        box(h, lx + 0.08f, 1.23f - sink, lz, 0.18f, 0.03f, 0.03f, {0.2f, 0.2f, 0.22f});
        const Rgb shade = sessionHue(g.hue, 0.5f, 0.55f);
        h.add(STRATA_PRIMITIVE_CYLINDER, lx + 0.18f, 1.18f - sink, lz, 0.2f, 0.06f, 0.2f, shade);
        const Rgb bulbOff{0.3f, 0.3f, 0.28f}, bulbOn{1.0f, 0.92f, 0.6f};
        ball(h, lx + 0.18f, 1.11f - sink, lz, 0.09f, mix(bulbOff, bulbOn, g.lamp));
        if (g.lamp > 0.02f && sink < 0.2f) {
            h.point(lx + 0.18f, 1.08f - sink, lz, 18.0f, 0.35f, bulbOn, 0.35f * g.lamp);
            // A warm pool on the desk.
            box(h, lx + 0.25f, 0.748f, lz + 0.12f, 0.5f, 0.004f, 0.4f, mix(top, {1.0f, 0.9f, 0.6f}, 0.45f * g.lamp));
            h.light(lx + 0.18f, 1.0f - sink, lz + 0.1f, 3.5f, {1.0f, 0.82f, 0.5f}, g.lamp * (0.5f + 1.0f * night),
                    g.selected ? 6 : 2);
        }
        h.add(STRATA_PRIMITIVE_CYLINDER, x + 0.55f, 0.8f - sink, z + 0.2f, 0.08f, 0.05f, 0.08f,
              sessionHue(g.hue, 0.5f, 0.8f));
    }
    if (g.stalled && sink < 0.2f) {
        const float f = 0.5f + 0.5f * std::sin(t * 6.0f);
        h.light(x, 1.1f, z + 0.1f, 3.0f, {1.0f, 0.1f, 0.05f}, 0.6f + 1.2f * f, 7);
        h.point(x, 1.07f, z - 0.1f, 60.0f, 1.2f, {1.0f, 0.15f, 0.1f}, 0.25f + 0.2f * f);
    }
    drawChair(h, g, sink);
    // Selection: a bright outline round the desk and chair.
    if (g.selected) {
        const Rgb c = g.selected == 2 ? Rgb{1, 1, 1} : Rgb{0.7f, 0.85f, 1.0f};
        const float x0 = x - w * 0.5f - 0.15f, x1 = x + w * 0.5f + 0.15f, z0 = z - d * 0.5f - 0.15f,
                    z1 = chairZ(g) + 0.45f, y = 0.04f;
        wide(h, x0, y, z0, x1, y, z0, c, 3.0f, 0.05f, 0.95f);
        wide(h, x1, y, z0, x1, y, z1, c, 3.0f, 0.05f, 0.95f);
        wide(h, x1, y, z1, x0, y, z1, c, 3.0f, 0.05f, 0.95f);
        wide(h, x0, y, z1, x0, y, z0, c, 3.0f, 0.05f, 0.95f);
    }
}

// ---------------------------------------------------------------- people

struct Frame3 {
    float px, pz, fx, fz, rx, rz, sink, s;
    void at(float r, float y, float f, float* out) const {
        out[0] = px + (rx * r + fx * f) * s;
        out[1] = y * s - sink;
        out[2] = pz + (rz * r + fz * f) * s;
    }
};

void drawPerson(Host& h, const Agent& g, float t, float night) {
    if (g.pose == P_ABSENT || g.personAppear <= 0.01f) return;
    const bool sit = sitting(g.pose);
    const float sizeK = g.lead ? 1.0f : 0.9f;
    Frame3 F{};
    F.s = sizeK;
    F.sink = (1.0f - std::min(g.appear, g.personAppear)) * 2.0f;
    if (sit) {
        F.px = g.dx;
        F.pz = chairZ(g) + 0.05f;
        F.fx = 0.0f;
        F.fz = -1.0f;
    } else {
        F.px = g.px;
        F.pz = g.pz;
        F.fx = std::cos(g.facing);
        F.fz = std::sin(g.facing);
    }
    F.rx = -F.fz;
    F.rz = F.fx;
    const float ph = g.own * 31.0f;
    // Colours: shirts in the session's hue (leads deeper, subagents lighter).
    const Rgb shirt = g.lead ? sessionHue(g.hue, 0.7f, 0.55f) : sessionHue(g.hue, 0.35f, 0.82f);
    const Rgb pants = g.lead ? Rgb{0.12f, 0.13f, 0.18f} : Rgb{0.3f, 0.3f, 0.34f};
    const Rgb skin = hsv(0.07f, 0.35f + 0.25f * hash(g.own * 3.0f), 0.55f + 0.35f * hash(g.own * 7.0f));
    const Rgb hair = hsv(0.07f, 0.5f, 0.08f + 0.4f * hash(g.own * 11.0f));
    float hip[3], neck[3], head[3], kneeL[3], kneeR[3], footL[3], footR[3], handL[3], handR[3], shL[3], shR[3];
    float headDrop = 0.0f, lean = 0.0f;
    // Legs.
    if (sit) {
        F.at(0, 0.52f, 0.0f, hip);
        F.at(-0.11f, 0.52f, 0.36f, kneeL);
        F.at(0.11f, 0.52f, 0.36f, kneeR);
        F.at(-0.12f, 0.06f, 0.42f, footL);
        F.at(0.12f, 0.06f, 0.42f, footR);
    } else {
        const bool walking = g.pose == P_WALK;
        const float sw = walking ? std::sin(g.walk * 5.5f) : 0.0f;
        const float bob = walking ? std::fabs(std::cos(g.walk * 5.5f)) * 0.04f : 0.0f;
        F.at(0, 0.82f + bob, 0.0f, hip);
        F.at(-0.1f, 0.44f + bob, 0.04f + sw * 0.14f, kneeL);
        F.at(0.1f, 0.44f + bob, 0.04f - sw * 0.14f, kneeR);
        F.at(-0.1f, 0.06f + std::max(0.0f, sw) * 0.08f, sw * 0.24f, footL);
        F.at(0.1f, 0.06f + std::max(0.0f, -sw) * 0.08f, -sw * 0.24f, footR);
    }
    // Upper body by pose.
    const float baseY = sit ? 0.52f : 0.82f + 0.0f;
    float lhr = -0.2f, lhy = baseY + 0.05f, lhf = 0.08f;  // left hand (local)
    float rhr = 0.2f, rhy = baseY + 0.05f, rhf = 0.08f;
    bool binder = false, note = false, bubble = false;
    switch (g.pose) {
        case P_TYPE: {
            lean = 0.08f;
            const float a = std::sin(t * 14.0f + ph), b = std::sin(t * 13.0f + ph + 1.7f);
            lhr = -0.15f; lhy = 0.80f + 0.025f * std::max(0.0f, a); lhf = 0.55f;
            rhr = 0.15f;  rhy = 0.80f + 0.025f * std::max(0.0f, b); rhf = 0.55f;
            break;
        }
        case P_RUN: {
            lean = 0.12f;
            const float a = std::sin(t * 6.0f + ph);
            lhr = -0.12f; lhy = 0.80f + 0.03f * std::max(0.0f, a); lhf = 0.55f;
            rhr = 0.34f; rhy = 0.79f; rhf = 0.55f + 0.03f * std::sin(t * 2.0f + ph);
            break;
        }
        case P_READ:
            lean = -0.02f; binder = true;
            lhr = -0.13f; lhy = 1.0f; lhf = 0.36f;
            rhr = 0.13f; rhy = 1.0f + 0.02f * std::sin(t * 0.8f + ph); rhf = 0.36f;
            break;
        case P_THINK:
            lean = -0.16f; bubble = true;
            lhr = -0.17f; lhy = 1.3f; lhf = -0.2f;
            rhr = 0.17f; rhy = 1.3f; rhf = -0.2f;
            break;
        case P_OTHER: {
            lean = 0.05f;
            rhr = 0.34f + 0.03f * std::cos(t * 2.5f + ph); rhy = 0.79f; rhf = 0.55f + 0.03f * std::sin(t * 2.5f + ph);
            lhr = -0.08f; lhy = 1.1f; lhf = 0.18f;  // hand to chin
            break;
        }
        case P_SLUMP:
            lean = 0.18f; headDrop = 0.12f;
            lhr = -0.24f; lhy = 0.5f; lhf = 0.15f;
            rhr = 0.24f; rhy = 0.5f; rhf = 0.15f;
            break;
        case P_REST:
            lean = -0.08f;
            lhr = -0.14f; lhy = 0.58f; lhf = 0.28f;
            rhr = 0.14f; rhy = 0.58f; rhf = 0.28f;
            break;
        case P_DELEGATE: {
            const float wv = std::sin(t * 3.0f + ph);
            rhr = 0.32f; rhy = 1.62f + 0.08f * wv; rhf = 0.38f + 0.1f * wv;  // pointing out to the pod
            lhr = -0.36f + 0.05f * std::sin(t * 2.0f + ph); lhy = 1.25f; lhf = 0.3f;
            break;
        }
        case P_WINDOW:
            lhr = -0.12f; lhy = 0.9f; lhf = -0.12f;  // hands behind the back, looking out
            rhr = 0.12f; rhy = 0.9f; rhf = -0.12f;
            break;
        case P_WAIT: {
            const float up = 0.05f * std::sin(t * 5.0f);
            rhr = 0.18f; rhy = 2.02f + up; rhf = 0.12f;  // a raised hand with a sticky note
            lhr = -0.24f; lhy = 0.86f; lhf = 0.06f;
            note = true;
            break;
        }
        case P_WALK: {
            const float sw = std::sin(g.walk * 5.5f);
            lhr = -0.24f; lhy = 0.86f; lhf = -sw * 0.2f;
            rhr = 0.24f; rhy = 0.86f; rhf = sw * 0.2f;
            break;
        }
        default:
            lhr = -0.24f; lhy = 0.86f; lhf = 0.0f;
            rhr = 0.24f; rhy = 0.86f; rhf = 0.0f;
            break;
    }
    const float torsoLen = 0.52f;
    F.at(0, baseY + torsoLen - headDrop * 0.5f, lean, neck);
    F.at(0, baseY + torsoLen + 0.2f - headDrop, lean * 1.3f + headDrop * 0.6f, head);
    F.at(-0.21f, baseY + torsoLen - 0.04f - headDrop * 0.5f, lean, shL);
    F.at(0.21f, baseY + torsoLen - 0.04f - headDrop * 0.5f, lean, shR);
    F.at(lhr, lhy, lhf, handL);
    F.at(rhr, rhy, rhf, handR);
    const float s = F.s;
    limb(h, kneeL, footL, 0.13f * s, pants);
    limb(h, kneeR, footR, 0.13f * s, pants);
    float hipL[3], hipR[3];
    F.at(-0.1f, baseY, 0.0f, hipL);
    F.at(0.1f, baseY, 0.0f, hipR);
    limb(h, hipL, kneeL, 0.15f * s, pants);
    limb(h, hipR, kneeR, 0.15f * s, pants);
    limb(h, hip, neck, 0.36f * s, shirt);
    limb(h, shL, handL, 0.1f * s, shirt);
    limb(h, shR, handR, 0.1f * s, shirt);
    ball(h, handL[0], handL[1], handL[2], 0.1f * s, skin);
    ball(h, handR[0], handR[1], handR[2], 0.1f * s, skin);
    ball(h, head[0], head[1], head[2], 0.3f * s, skin);
    float hb[3];
    F.at(0, baseY + torsoLen + 0.27f - headDrop, lean * 1.3f + headDrop * 0.6f - 0.05f, hb);
    ball(h, hb[0], hb[1], hb[2], 0.29f * s, hair);
    if (g.lead) {  // leads wear a headset: the one who talks to the team
        float hs[3];
        F.at(0.15f, baseY + torsoLen + 0.2f - headDrop, lean * 1.3f + headDrop * 0.6f, hs);
        ball(h, hs[0], hs[1], hs[2], 0.09f * s, {0.1f, 0.1f, 0.12f});
    }
    if (binder) {  // an open binder held up
        float b[3];
        F.at(0, 1.03f, 0.39f, b);
        const Rgb bc = hsv(hash(g.own + 0.37f), 0.6f, 0.55f);
        box(h, b[0], b[1], b[2], 0.36f * s, 0.26f * s, 0.05f * s, bc);
        box(h, b[0], b[1] + 0.005f, b[2] - 0.03f * s, 0.32f * s, 0.23f * s, 0.02f * s, {0.92f, 0.92f, 0.88f});
    }
    if (note) {  // the sticky note in the raised hand, and the halo that says "you"
        const float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
        box(h, handR[0], handR[1] + 0.22f * s, handR[2], 0.4f * s, 0.4f * s, 0.06f * s, kNote);
        box(h, handR[0], handR[1] + 0.22f * s, handR[2], 0.06f * s, 0.4f * s, 0.4f * s, kNote);
        h.point(handR[0], handR[1] + 0.22f * s, handR[2], 30.0f, 0.8f, kNote, 0.45f + 0.35f * pulse);
    }
    if (bubble) {  // thought dots rising
        for (int i = 0; i < 3; ++i) {
            const float k = std::fmod(t * 0.5f + i / 3.0f + ph, 1.0f);
            h.point(head[0] + 0.1f, head[1] + 0.25f + k * 0.5f, head[2] + 0.1f, 6.0f + 10.0f * k, 0.06f + 0.12f * k,
                    {0.9f, 0.9f, 1.0f}, 0.8f * (1.0f - k));
        }
    }
    if (g.pose == P_WINDOW) {  // looking out: a blue spark of the web over the head
        const float k = std::fmod(t * 0.8f + ph, 1.0f);
        h.point(head[0], head[1] + 0.35f, head[2], 14.0f, 0.18f, {0.4f, 0.85f, 1.0f}, 0.8f);
        ring(h, head[0], head[1] + 0.35f, head[2], 0.1f + 0.25f * k, {0.4f, 0.85f, 1.0f}, 0.8f * (1 - k), 16);
    }
    if (g.pose == P_SLUMP) {  // Z's drifting up
        for (int i = 0; i < 3; ++i) {
            const float k = std::fmod(t * 0.3f + i / 3.0f + ph, 1.0f);
            const float zx = head[0] + 0.2f + 0.25f * k, zy = head[1] + 0.35f + 0.8f * k, zz = head[2];
            const float zs = 0.07f + 0.08f * k;
            const Rgb c = night > 0.5f ? Rgb{0.75f, 0.85f, 1.0f} : Rgb{0.2f, 0.3f, 0.75f};
            const float al = 0.95f * (1 - k);
            wide(h, zx - zs, zy + zs, zz, zx + zs, zy + zs, zz, c, 3.0f, 0.03f, al);
            wide(h, zx + zs, zy + zs, zz, zx - zs, zy - zs, zz, c, 3.0f, 0.03f, al);
            wide(h, zx - zs, zy - zs, zz, zx + zs, zy - zs, zz, c, 3.0f, 0.03f, al);
        }
    }
    // Waiting: unmissable. A pulsing floor ring, a beam, and an orange light.
    if (g.pose == P_WAIT || g.waiting > 0.5f) {
        const float pulse = 0.5f + 0.5f * std::sin(t * 5.0f);
        const float bx = sit ? F.px : g.px, bz = sit ? F.pz : g.pz;
        for (int k = 0; k < 3; ++k) {
            const float rr = 0.4f + std::fmod(t * 0.6f + k / 3.0f, 1.0f) * 0.9f;
            ring(h, bx, 0.04f, bz, rr, kWaitOrange, 0.9f * (1.0f - (rr - 0.4f) / 0.9f), 32);
        }
        wide(h, bx, 0.05f, bz, bx, 4.2f, bz, kWaitOrange, 5.0f, 0.12f, 0.25f + 0.25f * pulse);
        h.light(bx, 2.4f, bz, 5.5f, kWaitOrange, 1.0f + 1.5f * pulse, 10);
    }
    // Selection ring at the feet.
    if (g.selected) {
        const float bx = sit ? F.px : g.px, bz = sit ? F.pz : g.pz;
        ring(h, bx, 0.05f, bz, 0.55f, {1.0f, 1.0f, 1.0f}, 0.95f, 32, t * 0.8f);
        ring(h, bx, 0.05f, bz, 0.6f, {0.6f, 0.8f, 1.0f}, 0.7f, 32, -t * 0.8f);
    }
}

// ---------------------------------------------------------------- trails

void drawTrails(Host& h, const float* r, int n, float t) {
    for (int i = 0; i < n; ++i, r += TRAIL_STRIDE) {
        const float x0 = r[0], z0 = r[1], x1 = r[2], z1 = r[3], kind = r[4], off = r[5], seed = r[6], a = r[7];
        const float dx = x1 - x0, dz = z1 - z0, len = std::sqrt(dx * dx + dz * dz);
        if (len < 0.01f || a <= 0.01f) continue;
        if (kind < 0.5f) {  // waiting: orange dashes marching towards the owner's door
            const float dash = 0.5f, step = 1.0f;
            const float phase = std::fmod(off - t * 1.2f, step);
            for (float s = -phase; s < len; s += step) {
                const float s0 = std::max(0.0f, s), s1 = std::min(len, s + dash);
                if (s1 <= s0) continue;
                wide(h, x0 + dx * s0 / len, 0.035f, z0 + dz * s0 / len, x0 + dx * s1 / len, 0.035f,
                     z0 + dz * s1 / len, kWaitOrange, 3.0f, 0.12f, 0.85f * a);
            }
        } else {  // delegation: little notes flying from the lead to a lane
            const Rgb c = sessionHue(seed, 0.6f, 1.0f);
            const float k = std::fmod(t * 0.6f + off, 1.0f);
            const float y = 1.4f + std::sin(k * PI) * 0.8f;
            h.point(x0 + dx * k, y, z0 + dz * k, 16.0f, 0.22f, c, 1.0f * a);
            for (int s = 0; s < 10; ++s) {  // a faint arc
                const float k0 = s / 10.0f, k1 = (s + 1) / 10.0f;
                h.line(x0 + dx * k0, 1.4f + std::sin(k0 * PI) * 0.8f, z0 + dz * k0, x0 + dx * k1,
                       1.4f + std::sin(k1 * PI) * 0.8f, z0 + dz * k1, c, 0.6f * a);
            }
        }
    }
}

}  // namespace

void strata_kit_configure(strata_kit::Options& o) {
    o.name = "agentviz-office";
    o.farPlane = 500.0f;
}

void strata_kit_engine_ready(strata_kit::Host&) {}

void strata_kit_build(strata_kit::Host& h, double td) {
    const float t = static_cast<float>(td);
    if (buf.size() < static_cast<size_t>(FLOOR_STRIDE)) return;
    const Floor fl = readFloor();
    const float night = fl.nightForce >= 0.0f ? clamp01(fl.nightForce) : h.night;
    // Indoors stays readable at night: a cool ambient lift, the lamps do the rest.
    h.ambientLift = {0.09f * night, 0.10f * night, 0.14f * night};
    drawRoom(h, fl, t, night);
    drawOwnerOffice(h, fl, t);
    const float* p = buf.data() + FLOOR_STRIDE;
    drawRects(h, p, nRects, t);
    p += nRects * RECT_STRIDE;
    for (int i = 0; i < nAgents; ++i) {
        const Agent g = readAgent(p + i * AGENT_STRIDE);
        drawDesk(h, g, t, night);
        drawPerson(h, g, t, night);
    }
    p += nAgents * AGENT_STRIDE;
    drawTrails(h, p, nTrails, t);
}

// The page's one setter: the whole scene for this frame (layout above).
STRATA_KIT_EXPORT void of_frame(const float* data, int rects, int agents, int trails) {
    const size_t n = FLOOR_STRIDE + static_cast<size_t>(rects) * RECT_STRIDE +
                     static_cast<size_t>(agents) * AGENT_STRIDE + static_cast<size_t>(trails) * TRAIL_STRIDE;
    buf.assign(data, data + n);
    nRects = rects;
    nAgents = agents;
    nTrails = trails;
}
