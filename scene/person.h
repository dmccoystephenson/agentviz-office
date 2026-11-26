// person.h: the people. A body of sphere chains posed by what the agent does,
// its props (binder, sticky note, thought dots, Z's), and the "needs you"
// beacon: a pulsing floor ring, a beam and an orange light.
#pragma once

#include "desk.h"

namespace office {

// A person's local frame: r to the right, f forward, y up, scaled by s.
struct Body {
    float px, pz, fx, fz, rx, rz, sink, s;
    void at(float r, float y, float f, float* out) const {
        out[0] = px + (rx * r + fx * f) * s;
        out[1] = y * s - sink;
        out[2] = pz + (rz * r + fz * f) * s;
    }
};

// Hands in local coordinates, the torso's lean, and which props to draw.
struct Arms {
    float lean = 0.0f, headDrop = 0.0f;
    float l[3] = {-0.24f, 0.86f, 0.0f}, r[3] = {0.24f, 0.86f, 0.0f};  // {right, up, forward}
    bool binder = false, note = false, bubble = false;
    void set(float lr, float ly, float lf, float rr, float ry, float rf) {
        l[0] = lr; l[1] = ly; l[2] = lf; r[0] = rr; r[1] = ry; r[2] = rf;
    }
};

inline Body bodyOf(const Desk& g, bool sit) {
    Body b{};
    b.s = g.lead ? 1.0f : 0.9f;
    b.sink = (1.0f - std::min(g.appear, g.personAppear)) * 2.0f;
    if (sit) {
        b.px = g.dx; b.pz = chairZ(g) + 0.05f; b.fx = 0.0f; b.fz = -1.0f;
    } else {
        b.px = g.px; b.pz = g.pz; b.fx = std::cos(g.facing); b.fz = std::sin(g.facing);
    }
    b.rx = -b.fz;
    b.rz = b.fx;
    return b;
}

inline Arms armsFor(const Desk& g, float t) {
    const float ph = g.own * 31.0f;
    Arms a;
    switch (g.pose) {
        case P_TYPE: {
            const float l = std::max(0.0f, std::sin(t * 14.0f + ph)), r = std::max(0.0f, std::sin(t * 13.0f + ph + 1.7f));
            a.lean = 0.08f; a.set(-0.15f, 0.80f + 0.025f * l, 0.55f, 0.15f, 0.80f + 0.025f * r, 0.55f);
            break;
        }
        case P_RUN:
            a.lean = 0.12f;
            a.set(-0.12f, 0.80f + 0.03f * std::max(0.0f, std::sin(t * 6.0f + ph)), 0.55f,
                  0.34f, 0.79f, 0.55f + 0.03f * std::sin(t * 2.0f + ph));
            break;
        case P_READ:
            a.lean = -0.02f; a.binder = true;
            a.set(-0.13f, 1.0f, 0.36f, 0.13f, 1.0f + 0.02f * std::sin(t * 0.8f + ph), 0.36f);
            break;
        case P_THINK: a.lean = -0.16f; a.bubble = true; a.set(-0.17f, 1.3f, -0.2f, 0.17f, 1.3f, -0.2f); break;
        case P_OTHER:  // a hand to the chin, the other on the mouse
            a.lean = 0.05f;
            a.set(-0.08f, 1.1f, 0.18f, 0.34f + 0.03f * std::cos(t * 2.5f + ph), 0.79f, 0.55f + 0.03f * std::sin(t * 2.5f + ph));
            break;
        case P_SLUMP: a.lean = 0.18f; a.headDrop = 0.12f; a.set(-0.24f, 0.5f, 0.15f, 0.24f, 0.5f, 0.15f); break;
        case P_REST: a.lean = -0.08f; a.set(-0.14f, 0.58f, 0.28f, 0.14f, 0.58f, 0.28f); break;
        case P_DELEGATE: {  // pointing out to the pod
            const float wv = std::sin(t * 3.0f + ph);
            a.set(-0.36f + 0.05f * std::sin(t * 2.0f + ph), 1.25f, 0.3f, 0.32f, 1.62f + 0.08f * wv, 0.38f + 0.1f * wv);
            break;
        }
        case P_WINDOW: a.set(-0.12f, 0.9f, -0.12f, 0.12f, 0.9f, -0.12f); break;  // hands behind the back
        case P_WAIT: a.note = true; a.set(-0.24f, 0.86f, 0.06f, 0.18f, 2.02f + 0.05f * std::sin(t * 5.0f), 0.12f); break;
        case P_WALK: {
            const float sw = std::sin(g.walk * 5.5f);
            a.set(-0.24f, 0.86f, -sw * 0.2f, 0.24f, 0.86f, sw * 0.2f);
            break;
        }
        default: break;
    }
    // Stalled at the desk: hands clutching the head, leaning in over the red
    // screen; the raised arms read from behind too.
    if (g.stalled && sitting(g.pose) && g.pose != P_SLUMP && g.pose != P_REST) {
        const float sh = 0.012f * std::sin(t * 3.0f + ph);
        a.lean = 0.14f; a.headDrop = 0.06f; a.binder = false; a.bubble = false;
        a.set(-0.2f, 1.34f + sh, 0.12f, 0.2f, 1.34f - sh, 0.12f);
    }
    return a;
}

// Legs: seated knees forward, or a walk cycle.
inline void drawLegs(Host& h, const Body& b, const Desk& g, bool sit, float baseY, Rgb pants) {
    float knee[2][3], foot[2][3], hip[2][3];
    for (int side = 0; side < 2; ++side) {
        const float sgn = side ? 1.0f : -1.0f;
        if (sit) {
            b.at(sgn * 0.11f, 0.52f, 0.36f, knee[side]);
            b.at(sgn * 0.12f, 0.06f, 0.42f, foot[side]);
        } else {
            const bool walking = g.pose == P_WALK;
            const float sw = walking ? std::sin(g.walk * 5.5f) * -sgn : 0.0f;
            const float bob = walking ? std::fabs(std::cos(g.walk * 5.5f)) * 0.04f : 0.0f;
            b.at(sgn * 0.1f, 0.44f + bob, 0.04f + sw * 0.14f, knee[side]);
            b.at(sgn * 0.1f, 0.06f + std::max(0.0f, sw) * 0.08f, sw * 0.24f, foot[side]);
        }
        b.at(sgn * 0.1f, baseY, 0.0f, hip[side]);
        limb(h, knee[side], foot[side], 0.13f * b.s, pants);
        limb(h, hip[side], knee[side], 0.15f * b.s, pants);
    }
}

struct Head { float pos[3]; };

inline Head drawUpperBody(Host& h, const Body& b, const Desk& g, const Arms& a, float baseY, Rgb shirt) {
    const float torso = 0.52f, s = b.s, drop = a.headDrop;
    const Rgb skin = hsv(0.07f, 0.35f + 0.25f * hash(g.own * 3.0f), 0.55f + 0.35f * hash(g.own * 7.0f));
    const Rgb hair = hsv(0.07f, 0.5f, 0.08f + 0.4f * hash(g.own * 11.0f));
    float hip[3], neck[3], shL[3], shR[3], handL[3], handR[3], hairPos[3];
    Head head{};
    const float headF = a.lean * 1.3f + drop * 0.6f;
    b.at(0, baseY, 0.0f, hip);
    b.at(0, baseY + torso - drop * 0.5f, a.lean, neck);
    b.at(0, baseY + torso + 0.2f - drop, headF, head.pos);
    b.at(-0.21f, baseY + torso - 0.04f - drop * 0.5f, a.lean, shL);
    b.at(0.21f, baseY + torso - 0.04f - drop * 0.5f, a.lean, shR);
    b.at(a.l[0], a.l[1], a.l[2], handL);
    b.at(a.r[0], a.r[1], a.r[2], handR);
    b.at(0, baseY + torso + 0.27f - drop, headF - 0.05f, hairPos);
    limb(h, hip, neck, 0.36f * s, shirt);
    limb(h, shL, handL, 0.1f * s, shirt);
    limb(h, shR, handR, 0.1f * s, shirt);
    ball(h, handL, 0.1f * s, skin);
    ball(h, handR, 0.1f * s, skin);
    ball(h, head.pos, 0.3f * s, skin);
    ball(h, hairPos, 0.29f * s, hair);
    if (g.lead) {  // leads wear a headset: the one who talks to the team
        float set[3];
        b.at(0.15f, baseY + torso + 0.2f - drop, headF, set);
        ball(h, set, 0.09f * s, {0.1f, 0.1f, 0.12f});
    }
    return head;
}

// The sticky note held up, saying why: "?" for a question, a padlock for a
// permission prompt. Drawn on the side the camera sees (south, +z).
inline void drawNote(Host& h, const Body& b, const Desk& g, const Arms& a, float t) {
    float hand[3];
    b.at(a.r[0], a.r[1], a.r[2], hand);
    const float s = b.s, x = hand[0], y = hand[1] + 0.22f * s, z = hand[2], face = z + 0.21f * s;
    box(h, x, y, z, 0.4f * s, 0.4f * s, 0.06f * s, kNote);
    box(h, x, y, z, 0.06f * s, 0.4f * s, 0.4f * s, kNote);
    h.point(x, y, z, 30.0f, 0.8f, kNote, 0.45f + 0.35f * wave(t, 5.0f));
    const float k = 0.12f * s;
    if (g.reason == agentviz::WaitingReason::Question) {
        h.wideLine(x - k, y + k, face, x + k, y + k, face, kInk, 4.0f, 0.03f, 1.0f);
        h.wideLine(x + k, y + k, face, x + k, y, face, kInk, 4.0f, 0.03f, 1.0f);
        h.wideLine(x + k, y, face, x, y, face, kInk, 4.0f, 0.03f, 1.0f);
        h.wideLine(x, y, face, x, y - k * 0.6f, face, kInk, 4.0f, 0.03f, 1.0f);
        h.wideLine(x, y - k * 1.1f, face, x, y - k * 1.4f, face, kInk, 4.0f, 0.03f, 1.0f);
    } else if (g.reason == agentviz::WaitingReason::Permission) {
        box(h, x, y - k * 0.5f, face, 1.6f * k, 1.2f * k, 0.01f, kInk);
        for (int i = 0; i < 8; ++i) {  // the shackle: a half ring above the body
            const float a0 = PI * i / 8, a1 = PI * (i + 1) / 8;
            h.wideLine(x + std::cos(a0) * k * 0.55f, y + k * 0.1f + std::sin(a0) * k * 0.7f, face,
                       x + std::cos(a1) * k * 0.55f, y + k * 0.1f + std::sin(a1) * k * 0.7f, face, kInk, 4.0f, 0.03f, 1.0f);
        }
    }
}

inline void drawProps(Host& h, const Body& b, const Desk& g, const Arms& a, const Head& head, float t, float night) {
    const float ph = g.own * 31.0f, s = b.s;
    const float* p = head.pos;
    if (a.binder) {  // an open binder held up
        float at[3];
        b.at(0, 1.03f, 0.39f, at);
        box(h, at[0], at[1], at[2], 0.36f * s, 0.26f * s, 0.05f * s, hsv(hash(g.own + 0.37f), 0.6f, 0.55f));
        box(h, at[0], at[1] + 0.005f, at[2] - 0.03f * s, 0.32f * s, 0.23f * s, 0.02f * s, {0.92f, 0.92f, 0.88f});
    }
    if (a.note) drawNote(h, b, g, a, t);
    if (a.bubble) {  // thought dots rising
        for (int i = 0; i < 3; ++i) {
            const float k = std::fmod(t * 0.5f + i / 3.0f + ph, 1.0f);
            h.point(p[0] + 0.1f, p[1] + 0.25f + k * 0.5f, p[2] + 0.1f, 6.0f + 10.0f * k, 0.06f + 0.12f * k,
                    {0.9f, 0.9f, 1.0f}, 0.8f * (1.0f - k));
        }
    }
    if (g.pose == P_WINDOW) {  // looking out: a blue spark of the web over the head
        const float k = std::fmod(t * 0.8f + ph, 1.0f);
        h.point(p[0], p[1] + 0.35f, p[2], 14.0f, 0.18f, {0.4f, 0.85f, 1.0f}, 0.8f);
        ring(h, p[0], p[1] + 0.35f, p[2], 0.1f + 0.25f * k, {0.4f, 0.85f, 1.0f}, 0.8f * (1 - k), 16);
    }
    if (g.pose == P_SLUMP) {  // Z's drifting up
        const Rgb c = night > 0.5f ? Rgb{0.75f, 0.85f, 1.0f} : Rgb{0.2f, 0.3f, 0.75f};
        for (int i = 0; i < 3; ++i) {
            const float k = std::fmod(t * 0.3f + i / 3.0f + ph, 1.0f), al = 0.95f * (1 - k);
            const float zx = p[0] + 0.2f + 0.25f * k, zy = p[1] + 0.35f + 0.8f * k, zz = p[2], zs = 0.07f + 0.08f * k;
            h.wideLine(zx - zs, zy + zs, zz, zx + zs, zy + zs, zz, c, 3.0f, 0.03f, al);
            h.wideLine(zx + zs, zy + zs, zz, zx - zs, zy - zs, zz, c, 3.0f, 0.03f, al);
            h.wideLine(zx - zs, zy - zs, zz, zx + zs, zy - zs, zz, c, 3.0f, 0.03f, al);
        }
    }
}

// Waiting: unmissable. Rings spreading on the floor, a beam, an orange light.
inline void drawBeacon(Host& h, float x, float z, float t) {
    const float pulse = wave(t, 5.0f);
    for (int k = 0; k < 3; ++k) {
        const float rr = 0.4f + std::fmod(t * 0.6f + k / 3.0f, 1.0f) * 0.9f;
        ring(h, x, 0.04f, z, rr, kWaitOrange, 0.9f * (1.0f - (rr - 0.4f) / 0.9f), 32);
    }
    h.wideLine(x, 0.05f, z, x, 4.2f, z, kWaitOrange, 5.0f, 0.12f, 0.25f + 0.25f * pulse);
    h.light(x, 2.4f, z, 5.5f, kWaitOrange, 1.0f + 1.5f * pulse, 10);
}

inline void drawPerson(Host& h, const Desk& g, float t, float night) {
    if (g.pose == P_ABSENT || g.personAppear <= 0.01f) return;
    const bool sit = sitting(g.pose);
    const Body b = bodyOf(g, sit);
    // Seated people breathe: a slow rise and fall, out of step with each other.
    const float baseY = sit ? 0.52f + 0.012f * std::sin(t * 1.7f + g.own * 23.0f) : 0.82f;
    const Rgb shirt = g.lead ? sessionHue(g.hue, 0.7f, 0.55f) : sessionHue(g.hue, 0.35f, 0.82f);
    const Rgb pants = g.lead ? Rgb{0.12f, 0.13f, 0.18f} : Rgb{0.3f, 0.3f, 0.34f};
    const Arms arms = armsFor(g, t);
    drawLegs(h, b, g, sit, baseY, pants);
    const Head head = drawUpperBody(h, b, g, arms, baseY, shirt);
    drawProps(h, b, g, arms, head, t, night);
    if (g.pose == P_WAIT || g.waiting > 0.5f) drawBeacon(h, b.px, b.pz, t);
    if (g.selected) {  // selection rings at the feet
        ring(h, b.px, 0.05f, b.pz, 0.55f, kWhite, 0.95f, 32, t * 0.8f);
        ring(h, b.px, 0.05f, b.pz, 0.6f, {0.6f, 0.8f, 1.0f}, 0.7f, 32, -t * 0.8f);
    }
}

}  // namespace office
