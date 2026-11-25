/* office.js: the office floor's model. Reads agent-feed snapshots, lays out
   pods, desks and people, eases between snapshots and walks people along the
   aisles; writes one float buffer per frame for scene/main.cpp (of_frame).

   Layout, north (-z) at the top of the screen:

       [ west windows ]   [ owner's glass office ]   [ east windows ]
                          [      queue mat       ]
       pod  pod  ... |  corridor  | ...  pod  pod
       (each pod: one block per session; the lead desk at the block's
        south edge, its subagents' satellite desks in rows north of it)

   Classic script: defines window.Office. No dependencies. */
(function () {
  "use strict";

  const FLOOR_STRIDE = 16, RECT_STRIDE = 8, AGENT_STRIDE = 24, TRAIL_STRIDE = 8;
  const POSE = {type: 0, run: 1, read: 2, window: 3, delegate: 4, think: 5, other: 6, slump: 7, wait: 8,
    walk: 9, rest: 10, absent: 11, stand: 12};
  const SCREEN = {off: 0, dim: 1, edit: 2, run: 3, read: 4, web: 5, delegate: 6, think: 7, other: 8,
    stalled: 9, waiting: 10, idle: 11};
  const ACTION_POSE = {edit: POSE.type, run: POSE.run, read: POSE.read, web: POSE.window,
    delegate: POSE.delegate, think: POSE.think, other: POSE.other, none: POSE.think};
  const ACTION_SCREEN = {edit: SCREEN.edit, run: SCREEN.run, read: SCREEN.read, web: SCREEN.web,
    delegate: SCREEN.delegate, think: SCREEN.think, other: SCREEN.other, none: SCREEN.think};

  // Geometry (world units, about a metre).
  const COL_W = 2.3;        // one desk column in a session block
  const ROW_D = 2.6;        // one desk row
  const POD_PAD = 0.8;
  const BLOCK_GAP = 0.6;
  const POD_GAP = 2.4;      // the aisle between pods in a column
  const CORRIDOR = 3.2;
  const PERIM = 2.0;        // the aisle along the window walls
  const OFFICE_W = 7.0, OFFICE_D = 4.6;
  const QUEUE = 3.4;        // the space before the owner's door
  const WALK_SPEED = 2.4;   // units per second
  const DWELL = 4.0;        // seconds at a place before a non-urgent move

  function hashSeed(s) {
    let h = 2166136261;
    for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
    return ((h >>> 0) % 10000) / 10000;
  }
  const ease = (cur, target, dt, rate) => cur + (target - cur) * (1 - Math.exp(-rate * dt));

  class Office {
    constructor(mod) {
      this.mod = mod;
      this.agents = new Map();   // id -> agent (sessions and subagents)
      this.pods = new Map();     // place id -> pod
      this.blocks = new Map();   // session id -> block rect
      this.slots = new Map();    // session id -> Map(follower id -> slot)
      this.floor = null;
      this.doc = null;
      this.clock = 0;
      this.lastT = null;
      this.nightForce = -1;
      this.selectedId = null;
      this.hoveredId = null;
      this._ptr = 0; this._cap = 0;
    }

    /* ------------------------------------------------------------ data */

    update(doc) {
      this.doc = doc;
      const sessions = doc.sessions || [];
      const byPlace = new Map();
      for (const s of sessions) {
        if (!byPlace.has(s.place)) byPlace.set(s.place, []);
        byPlace.get(s.place).push(s);
      }
      const placeInfo = new Map((doc.places || []).map((p) => [p.id, p]));
      const placeIds = [...byPlace.keys()].sort((a, b) => {
        const la = (placeInfo.get(a) || {}).label || a, lb = (placeInfo.get(b) || {}).label || b;
        return la.localeCompare(lb);
      });

      // Session blocks: lead desk at the south edge, satellites in rows north.
      const seen = new Set();
      const podDefs = [];
      for (const pid of placeIds) {
        const blocks = [];
        for (const s of byPlace.get(pid)) {
          const slotMap = this._slotsFor(s);
          const nSlots = slotMap.size ? Math.max(...slotMap.values()) + 1 : 0;
          const cols = nSlots === 0 ? 1 : Math.min(3, nSlots);
          const rows = Math.ceil(nSlots / cols);
          blocks.push({s, slotMap, cols, rows, w: cols * COL_W, d: (rows + 1) * ROW_D});
        }
        const w = blocks.reduce((a, b) => a + b.w, 0) + BLOCK_GAP * (blocks.length - 1) + POD_PAD * 2;
        const d = Math.max(...blocks.map((b) => b.d)) + POD_PAD * 2;
        const info = placeInfo.get(pid) || {id: pid, label: pid, seed: hashSeed(pid)};
        podDefs.push({id: pid, label: info.label || pid, seed: info.seed != null ? info.seed : hashSeed(pid),
          blocks, w, d, waiting: byPlace.get(pid).some((s) => s.state === "waiting"),
          busy: byPlace.get(pid).filter((s) => s.state === "busy").length / byPlace.get(pid).length});
      }

      // Two columns of pods either side of the corridor, filled greedily.
      const top = OFFICE_D + QUEUE;
      const colZ = [top, top];
      let maxW = [0, 0];
      for (const p of podDefs) {
        const side = colZ[0] <= colZ[1] ? 0 : 1;
        p.side = side;
        p.z0 = colZ[side];
        p.z1 = p.z0 + p.d;
        colZ[side] = p.z1 + POD_GAP;
        maxW[side] = Math.max(maxW[side], p.w);
        if (side === 0) { p.x1 = -CORRIDOR / 2; p.x0 = p.x1 - p.w; }
        else { p.x0 = CORRIDOR / 2; p.x1 = p.x0 + p.w; }
      }
      const halfW = Math.max(OFFICE_W / 2 + 3, CORRIDOR / 2 + Math.max(maxW[0], maxW[1], 6) + PERIM);
      const zS = Math.max(colZ[0], colZ[1], top + 6) - POD_GAP + 2.6;
      this.floor = {x0: -halfW, x1: halfW, zN: 0, zS, cx0: -CORRIDOR / 2, cx1: CORRIDOR / 2,
        ox0: -OFFICE_W / 2, ox1: OFFICE_W / 2, oz1: OFFICE_D, doorX: 0};

      // Desks.
      const waitingList = sessions.filter((s) => s.state === "waiting").map((s) => s.id).sort();
      for (const p of podDefs) {
        const pod = this._ease(this.pods, p.id, {x0: p.x0, z0: p.z0, x1: p.x1, z1: p.z1});
        Object.assign(pod.info, {label: p.label, seed: p.seed, busy: p.busy, waiting: p.waiting, side: p.side,
          aisleZ: p.z1 + POD_GAP / 2});
        seen.add("pod:" + p.id);
        // Blocks are south-aligned; the pod's lead desks form one front row.
        let bx = p.side === 0 ? p.x1 - POD_PAD : p.x0 + POD_PAD;
        const ordered = p.side === 0 ? [...p.blocks].reverse() : p.blocks;  // keep the first session nearest the corridor
        for (const b of ordered) {
          const x0 = p.side === 0 ? bx - b.w : bx, x1 = x0 + b.w;
          bx = p.side === 0 ? x0 - BLOCK_GAP : x1 + BLOCK_GAP;
          const z1 = p.z1 - POD_PAD, z0 = z1 - b.d;
          const blk = this._ease(this.blocks, b.s.id, {x0, z0, x1, z1});
          blk.info.seed = b.s.seed;
          seen.add("blk:" + b.s.id);
          const s = b.s;
          const lead = this._agent(s.id, true);
          lead.data = s; lead.session = s; lead.pod = pod; lead.block = blk; lead.hue = s.seed; lead.own = s.seed;
          lead.target = {x: (x0 + x1) / 2, z: z1 - ROW_D + 0.9};
          lead.waitSlot = waitingList.indexOf(s.id);
          lead.waitCount = waitingList.length;
          seen.add(lead.id);
          for (const f of s.followers || []) {
            const slot = b.slotMap.get(f.id);
            const row = Math.floor(slot / b.cols), col = slot % b.cols;
            const ag = this._agent(s.id + "/" + f.id, false);
            ag.data = f; ag.session = s; ag.pod = pod; ag.block = blk; ag.hue = s.seed;
            ag.own = f.seed != null ? f.seed : hashSeed(f.id);
            ag.target = {x: x0 + (col + 0.5) * COL_W, z: z1 - ROW_D * (row + 2) + 0.9};
            ag.waitSlot = -1;
            seen.add(ag.id);
          }
        }
      }
      // Gone: shrink into the floor, then drop.
      for (const [id, a] of this.agents) if (!seen.has(id)) a.gone = true; else a.gone = false;
      for (const [id, r] of this.pods) r.gone = !seen.has("pod:" + id);
      for (const [id, r] of this.blocks) r.gone = !seen.has("blk:" + id);
      for (const a of this.agents.values()) if (!a.gone) this._retarget(a);
    }

    // Stable desk slots per session: a subagent keeps its desk while it is listed.
    _slotsFor(s) {
      let m = this.slots.get(s.id);
      if (!m) { m = new Map(); this.slots.set(s.id, m); }
      const ids = new Set((s.followers || []).map((f) => f.id));
      for (const id of [...m.keys()]) if (!ids.has(id)) m.delete(id);
      const used = new Set(m.values());
      const fresh = (s.followers || []).map((f) => f.id).filter((id) => !m.has(id)).sort();
      let k = 0;
      for (const id of fresh) { while (used.has(k)) k++; m.set(id, k); used.add(k); }
      // Compact: if slots run past the count, renumber in slot order.
      if (m.size && Math.max(...m.values()) >= m.size + 2) {
        [...m.entries()].sort((a, b) => a[1] - b[1]).forEach(([id], i) => m.set(id, i));
      }
      return m;
    }

    _ease(map, id, rect) {
      let r = map.get(id);
      if (!r) { r = {cur: Object.assign({}, rect), info: {}, appear: 0}; map.set(id, r); }
      r.target = rect;
      return r;
    }

    _agent(id, lead) {
      let a = this.agents.get(id);
      if (!a) {
        a = {id, lead, desk: null, appear: 0, personAppear: 0, lamp: 0, push: 0, waiting: 0,
          loc: "seat", pos: null, path: null, pathS: 0, arrivedAt: -1e9, pending: null, walk: 0, facing: -Math.PI / 2,
          fresh: true};
        this.agents.set(id, a);
      }
      return a;
    }

    /* ------------------------------------------------------------ behaviour */

    // What the agent should be doing now, from its data.
    _intent(a) {
      const d = a.data;
      if (a.lead) {
        const st = d.state;
        if (st === "offline") return {loc: "seat", pose: POSE.absent, screen: SCREEN.off, lamp: 0, push: 0, jacket: true};
        if (st === "idle" || st === "dim") return {loc: "seat", pose: POSE.slump, screen: SCREEN.idle, lamp: 0, push: 0.15};
        if (st === "waiting") return {loc: "door", pose: POSE.wait, screen: SCREEN.waiting, lamp: 0.6, push: 0.3};
        const act = d.action || "none";
        return {loc: act === "web" ? "window" : "seat", pose: ACTION_POSE[act] != null ? ACTION_POSE[act] : POSE.other,
          screen: d.stalled ? SCREEN.stalled : (ACTION_SCREEN[act] != null ? ACTION_SCREEN[act] : SCREEN.other),
          lamp: 1, push: act === "delegate" ? 0.35 : 0};
      }
      if (d.state === "resting") return {loc: "seat", pose: POSE.rest, screen: SCREEN.dim, lamp: 0, push: 1};
      const act = d.action && d.action !== "none" ? d.action : "other";
      return {loc: act === "web" ? "window" : "seat", pose: ACTION_POSE[act], screen: ACTION_SCREEN[act], lamp: 1, push: 0};
    }

    _home(a) { return {x: a.desk.x, z: a.desk.z + 1.45}; }

    _spot(a, loc) {
      const f = this.floor, home = this._home(a);
      if (loc === "window") {
        const west = a.pod.info.side === 0;
        return {x: west ? f.x0 + 0.75 : f.x1 - 0.75, z: Math.min(Math.max(home.z, f.zN + 6), f.zS - 1),
          facing: west ? Math.PI : 0};
      }
      if (loc === "door") {
        const n = Math.max(1, a.waitCount), k = Math.max(0, a.waitSlot);
        const perRow = 4, row = Math.floor(k / perRow), inRow = Math.min(perRow, n - row * perRow);
        return {x: f.doorX + (k % perRow - (inRow - 1) / 2) * 0.85, z: f.oz1 + 0.85 + row * 0.85, facing: -Math.PI / 2};
      }
      return {x: home.x, z: home.z, facing: -Math.PI / 2};
    }

    // Aisle route between an agent's desk and a spot away from it.
    _route(a, loc) {
      const home = Object.assign({}, this._home(a)), spot = this._spot(a, loc), az = a.pod.info.aisleZ;
      // Subagents first step out sideways between the desk rows, to the
      // block's edge, so they do not walk through the desks south of them.
      const out = [home];
      if (!a.lead && a.block) {
        const towardWall = loc === "window";
        const west = (a.pod.info.side === 0) === towardWall;
        const ex = west ? a.block.cur.x0 - BLOCK_GAP / 2 : a.block.cur.x1 + BLOCK_GAP / 2;
        out.push({x: ex, z: home.z});
        home.x = ex;
      }
      if (loc === "window") {
        return out.concat([{x: home.x, z: az}, {x: spot.x, z: az}, spot]);
      }
      if (loc === "door") {
        const lane = a.pod.info.side === 0 ? -0.7 : 0.7;
        return out.concat([{x: home.x, z: az}, {x: lane, z: az}, {x: lane, z: spot.z + 0.9}, spot]);
      }
      return [home];
    }

    _retarget(a) {
      a.intent = this._intent(a);
    }

    _startWalk(a, to) {
      // Retrace to the desk (if away), then out to the new spot.
      let pts = [];
      const cur = a.pos ? {x: a.pos.x, z: a.pos.z} : this._home(a);
      if (a.loc !== "seat" || a.path) {
        const back = (a.routeOut || this._route(a, a.loc)).slice().reverse();
        // Drop the nodes already behind us: start from the nearest.
        let best = 0, bd = Infinity;
        back.forEach((p, i) => { const d = (p.x - cur.x) ** 2 + (p.z - cur.z) ** 2; if (d < bd) { bd = d; best = i; } });
        pts = [cur, ...back.slice(best)];
      } else {
        pts = [cur];
      }
      if (to !== "seat") {
        const out = this._route(a, to);
        a.routeOut = out;
        pts = pts.concat(out);
      } else {
        a.routeOut = null;
      }
      // Remove zero-length segments.
      const clean = [pts[0]];
      for (const p of pts.slice(1)) {
        const q = clean[clean.length - 1];
        if ((p.x - q.x) ** 2 + (p.z - q.z) ** 2 > 1e-4) clean.push(p);
      }
      let len = 0;
      for (let i = 1; i < clean.length; i++) len += Math.hypot(clean[i].x - clean[i - 1].x, clean[i].z - clean[i - 1].z);
      a.path = clean;
      a.pathLen = len;
      a.pathS = 0;
      a.dest = to;
      if (len < 0.05) this._arrive(a);
    }

    _arrive(a) {
      a.loc = a.dest;
      a.path = null;
      a.arrivedAt = this.clock;
      if (a.loc === "seat") a.routeOut = null;
    }

    _stepAgent(a, dt) {
      const it = a.intent || {loc: "seat", pose: POSE.think, screen: SCREEN.off, lamp: 0, push: 0};
      // Desk position eases to its layout slot.
      if (!a.desk) a.desk = {x: a.target.x, z: a.target.z};
      a.desk.x = ease(a.desk.x, a.target.x, dt, 3);
      a.desk.z = ease(a.desk.z, a.target.z, dt, 3);
      a.appear = ease(a.appear, a.gone ? 0 : 1, dt, a.gone ? 4 : 2.5);
      const present = it.pose !== POSE.absent && !a.gone;
      a.personAppear = ease(a.personAppear, present ? 1 : 0, dt, 3);
      a.lamp = ease(a.lamp, it.lamp, dt, 4);
      a.push = ease(a.push, it.push, dt, 3);
      a.waiting = ease(a.waiting, it.pose === POSE.wait ? 1 : 0, dt, 4);

      // First sight: start where the data says, without a walk.
      if (a.fresh) {
        a.fresh = false;
        a.loc = it.loc;
        a.routeOut = it.loc === "seat" ? null : this._route(a, it.loc);
        const sp = this._spot(a, it.loc);
        a.pos = {x: sp.x, z: sp.z};
        a.facing = sp.facing;
        a.arrivedAt = -1e9;
      }
      // Decide on a move.
      const want = present ? it.loc : "seat";
      const heading = a.path ? a.dest : a.loc;
      if (want !== heading) {
        const urgent = want === "door" || heading === "door";
        if (urgent || this.clock - a.arrivedAt > DWELL || a.path) this._startWalk(a, want);
      }
      // Walk.
      if (a.path) {
        a.pathS += WALK_SPEED * dt;
        a.walk += WALK_SPEED * dt;
        if (a.pathS >= a.pathLen) {
          const end = a.path[a.path.length - 1];
          a.pos = {x: end.x, z: end.z};
          this._arrive(a);
        } else {
          let s = a.pathS;
          for (let i = 1; i < a.path.length; i++) {
            const p0 = a.path[i - 1], p1 = a.path[i], l = Math.hypot(p1.x - p0.x, p1.z - p0.z);
            if (s <= l) {
              const k = l > 0 ? s / l : 1;
              a.pos = {x: p0.x + (p1.x - p0.x) * k, z: p0.z + (p1.z - p0.z) * k};
              a.facing = Math.atan2(p1.z - p0.z, p1.x - p0.x);
              break;
            }
            s -= l;
          }
        }
      } else {
        // Standing spots follow their slot (the door queue reshuffles, desks slide).
        const sp = this._spot(a, a.loc);
        a.pos.x = ease(a.pos.x, sp.x, dt, 3);
        a.pos.z = ease(a.pos.z, sp.z, dt, 3);
        a.facing = sp.facing;
      }
      // The pose: walking, or what the data says once there.
      if (a.path) a.pose = POSE.walk;
      else if (a.loc === "seat") {
        a.pose = it.loc === "seat" ? it.pose : POSE.stand;
        if (!present) a.pose = POSE.absent;
      } else if (a.loc === it.loc) a.pose = it.pose;
      else a.pose = POSE.stand;
      a.screen = it.screen;
      a.jacket = !!it.jacket;
    }

    /* ------------------------------------------------------------ frame */

    step(t) {
      const dt = this.lastT == null ? 0 : Math.min(Math.max(t - this.lastT, 0), 0.25);
      this.lastT = t;
      this.clock += dt;
      if (!this.floor) return;
      for (const map of [this.pods, this.blocks]) {
        for (const [id, r] of map) {
          for (const k of ["x0", "z0", "x1", "z1"]) r.cur[k] = ease(r.cur[k], r.target[k], dt, 3);
          r.appear = ease(r.appear, r.gone ? 0 : 1, dt, 3);
          if (r.gone && r.appear < 0.02) map.delete(id);
        }
      }
      for (const [id, a] of this.agents) {
        this._stepAgent(a, dt);
        if (a.gone && a.appear < 0.02) this.agents.delete(id);
      }
    }

    // Where a thing is now, for labels, picking and the camera.
    where(a) {
      const sitting = a.pose !== POSE.walk && a.pose !== POSE.stand && a.pose !== POSE.wait &&
        a.pose !== POSE.window && a.pose !== POSE.delegate;
      if (!a.desk) return null;
      if (sitting) return {x: a.desk.x, z: a.desk.z + 0.77 + 0.5 * a.push, y: a.lead ? 1.35 : 1.25, sitting};
      return {x: a.pos.x, z: a.pos.z, y: a.lead ? 1.6 : 1.5, sitting};
    }

    write(selectedId, hoveredId) {
      const mod = this.mod, f = this.floor;
      if (!f) return;
      const agents = [...this.agents.values()].filter((a) => a.desk);
      const rects = [...this.pods.entries()].map(([, r]) => ({r, kind: 0}))
        .concat([...this.blocks.entries()].map(([, r]) => ({r, kind: 1})));
      const trails = [];
      for (const a of agents) {
        if (!a.lead || a.appear < 0.3) continue;
        if (a.waiting > 0.05 || (a.path && a.dest === "door")) {
          const route = a.routeOut || this._route(a, "door");
          let off = 0;
          for (let i = 1; i < route.length; i++) {
            const p0 = route[i - 1], p1 = route[i];
            trails.push([p0.x, p0.z, p1.x, p1.z, 0, off, a.hue, Math.max(a.waiting, 0.4)]);
            off += Math.hypot(p1.x - p0.x, p1.z - p0.z);
          }
        }
        if (a.pose === POSE.delegate) {
          let i = 0;
          for (const b of agents) {
            if (b.lead || b.session.id !== a.session.id || b.data.state !== "active") continue;
            trails.push([a.desk.x, a.desk.z + 0.6, b.desk.x, b.desk.z, 1, (i++) * 0.37, a.hue, 1]);
          }
        }
      }
      const n = FLOOR_STRIDE + rects.length * RECT_STRIDE + agents.length * AGENT_STRIDE + trails.length * TRAIL_STRIDE;
      if (n > this._cap) {
        if (this._ptr) mod._free(this._ptr);
        this._cap = n + 64 * AGENT_STRIDE;
        this._ptr = mod._malloc(this._cap * 4);
      }
      const heap = mod.HEAPF32;
      let o = this._ptr >> 2;
      const anyWaiting = agents.reduce((m, a) => Math.max(m, a.lead ? a.waiting : 0), 0);
      heap.set([f.x0, f.zN, f.x1, f.zS, f.cx0, f.cx1, f.ox0, f.ox1, f.oz1, f.doorX, anyWaiting, this.nightForce,
        0, 0, 0, 0], o);
      o += FLOOR_STRIDE;
      for (const {r, kind} of rects) {
        // A rect shrinks towards its centre as it disappears.
        const k = r.appear, cx = (r.cur.x0 + r.cur.x1) / 2, cz = (r.cur.z0 + r.cur.z1) / 2;
        heap.set([cx + (r.cur.x0 - cx) * k, cz + (r.cur.z0 - cz) * k, cx + (r.cur.x1 - cx) * k, cz + (r.cur.z1 - cz) * k,
          kind, r.info.seed || 0, r.info.busy || 0, r.info.waiting ? 1 : 0], o);
        o += RECT_STRIDE;
      }
      for (const a of agents) {
        const sel = a.id === selectedId ? 1 : (a.id === hoveredId ? 2 : 0);
        heap.set([a.desk.x, a.desk.z, a.lead ? 1 : 0, a.hue || 0, a.own || 0, a.appear,
          a.pos ? a.pos.x : a.desk.x, a.pos ? a.pos.z : a.desk.z, a.facing, a.pose, a.walk, a.lamp, a.screen, a.push,
          a.jacket ? 1 : 0, sel, a.waiting, a.personAppear, 0, a.lead && a.data.stalled ? 1 : 0,
          a.pose === POSE.read ? 1 : 0, 0, 0, 0], o);
        o += AGENT_STRIDE;
      }
      for (const tr of trails) { heap.set(tr, o); o += TRAIL_STRIDE; }
      mod._of_frame(this._ptr, rects.length, agents.length, trails.length);
    }

    bounds() {
      return this.floor;
    }
  }

  window.Office = Office;
  window.Office.POSE = POSE;
})();
