/* office-model.js: the office floor's model. Reads agent-feed snapshots, lays
   out pods, desks and people, and eases them between snapshots. People's
   behaviour (where they go, how they walk) is office-walk.js; packing a frame
   for the scene is office-frame.js.

   Layout, north (-z) at the top of the screen:

       [ west windows ]   [ owner's glass office ]   [ east windows ]
                          [      queue mat       ]
       pod  pod  ... |  corridor  | ...  pod  pod
       (each pod: one block per session; the lead desk at the block's
        south edge, its subagents' satellite desks in rows north of it)

   Classic script: defines window.Office. Needs AgentvizFeed. */
(function () {
  "use strict";
  const {ease, seedOf, followerKey} = window.AgentvizFeed;

  // Poses and screens: scene/desk.h Pose / Screen.
  const POSE = {type: 0, run: 1, read: 2, window: 3, delegate: 4, think: 5, other: 6, slump: 7, wait: 8,
    walk: 9, rest: 10, absent: 11, stand: 12};
  const SCREEN = {off: 0, dim: 1, edit: 2, run: 3, read: 4, web: 5, delegate: 6, think: 7, other: 8,
    stalled: 9, waiting: 10, idle: 11};

  // Geometry (world units, about a metre).
  const GEOMETRY = {
    COL_W: 2.3,        // one desk column in a session block
    ROW_D: 2.6,        // one desk row
    POD_PAD: 0.8,
    BLOCK_GAP: 0.6,
    POD_GAP: 2.4,      // the aisle between pods in a column
    CORRIDOR: 3.2,
    PERIM: 2.0,        // the aisle along the window walls
    OFFICE_W: 7.0, OFFICE_D: 4.6,
    QUEUE: 3.4,        // the space before the owner's door
  };
  const G = GEOMETRY;

  function groupByPlace(sessions) {
    const byPlace = new Map();
    for (const s of sessions) {
      if (!byPlace.has(s.place)) byPlace.set(s.place, []);
      byPlace.get(s.place).push(s);
    }
    return byPlace;
  }

  class Office {
    constructor() {
      this.agents = new Map();   // id ("session" or "session/follower") -> agent
      this.pods = new Map();     // place id -> pod
      this.blocks = new Map();   // session id -> block rect
      this.slots = new Map();    // session id -> Map(follower id -> slot)
      this.floor = null;
      this.clock = 0;
      this.lastT = null;
      this.nightForce = -1;
    }

    /* ------------------------------------------------------------ layout */

    update(doc) {
      const sessions = doc.sessions || [];
      const podDefs = this._podDefs(doc, groupByPlace(sessions));
      this._placePods(podDefs);
      const waitingList = sessions.filter((s) => s.state === "waiting").map((s) => s.id).sort();
      const seen = new Set();
      for (const p of podDefs) this._layoutPod(p, waitingList, seen);
      for (const [id, a] of this.agents) a.gone = !seen.has(id);
      for (const [id, r] of this.pods) r.gone = !seen.has("pod:" + id);
      for (const [id, r] of this.blocks) r.gone = !seen.has("blk:" + id);
      for (const a of this.agents.values()) if (!a.gone) a.intent = this._intent(a);
    }

    // One pod per place, sorted by label; each holds one block per session.
    _podDefs(doc, byPlace) {
      const placeInfo = new Map((doc.places || []).map((p) => [p.id, p]));
      const labelOf = (id) => (placeInfo.get(id) || {}).label || id;
      const ids = [...byPlace.keys()].sort((a, b) => labelOf(a).localeCompare(labelOf(b)));
      return ids.map((id) => {
        const sessions = byPlace.get(id);
        const blocks = sessions.map((s) => this._blockDef(s));
        const info = placeInfo.get(id) || {};
        return {id, label: labelOf(id), seed: info.seed != null ? info.seed : seedOf(id), blocks,
          w: blocks.reduce((a, b) => a + b.w, 0) + G.BLOCK_GAP * (blocks.length - 1) + G.POD_PAD * 2,
          d: Math.max(...blocks.map((b) => b.d)) + G.POD_PAD * 2,
          waiting: sessions.some((s) => s.state === "waiting"),
          busy: sessions.filter((s) => s.state === "busy").length / sessions.length};
      });
    }

    _blockDef(s) {
      const slotMap = this._slotsFor(s);
      const nSlots = slotMap.size ? Math.max(...slotMap.values()) + 1 : 0;
      const cols = nSlots === 0 ? 1 : Math.min(3, nSlots);
      const rows = Math.ceil(nSlots / cols);
      return {s, slotMap, cols, rows, w: cols * G.COL_W, d: (rows + 1) * G.ROW_D};
    }

    // Two columns of pods either side of the corridor, filled greedily; sets the floor.
    _placePods(podDefs) {
      const top = G.OFFICE_D + G.QUEUE;
      const colZ = [top, top], maxW = [0, 0];
      for (const p of podDefs) {
        const side = colZ[0] <= colZ[1] ? 0 : 1;
        Object.assign(p, {side, z0: colZ[side], z1: colZ[side] + p.d});
        colZ[side] = p.z1 + G.POD_GAP;
        maxW[side] = Math.max(maxW[side], p.w);
        p.x0 = side === 0 ? -G.CORRIDOR / 2 - p.w : G.CORRIDOR / 2;
        p.x1 = p.x0 + p.w;
      }
      const halfW = Math.max(G.OFFICE_W / 2 + 3, G.CORRIDOR / 2 + Math.max(maxW[0], maxW[1], 6) + G.PERIM);
      const zS = Math.max(colZ[0], colZ[1], top + 6) - G.POD_GAP + 2.6;
      this.floor = {x0: -halfW, x1: halfW, zN: 0, zS, cx0: -G.CORRIDOR / 2, cx1: G.CORRIDOR / 2,
        ox0: -G.OFFICE_W / 2, ox1: G.OFFICE_W / 2, oz1: G.OFFICE_D, doorX: 0};
    }

    // Blocks are south-aligned so the pod's lead desks form one front row;
    // the first session sits nearest the corridor.
    _layoutPod(p, waitingList, seen) {
      const pod = this._ease(this.pods, p.id, {x0: p.x0, z0: p.z0, x1: p.x1, z1: p.z1});
      Object.assign(pod.info, {label: p.label, seed: p.seed, busy: p.busy, waiting: p.waiting, side: p.side,
        aisleZ: p.z1 + G.POD_GAP / 2});
      seen.add("pod:" + p.id);
      const west = p.side === 0;
      let bx = west ? p.x1 - G.POD_PAD : p.x0 + G.POD_PAD;
      for (const b of west ? [...p.blocks].reverse() : p.blocks) {
        const x0 = west ? bx - b.w : bx, x1 = x0 + b.w;
        bx = west ? x0 - G.BLOCK_GAP : x1 + G.BLOCK_GAP;
        const z1 = p.z1 - G.POD_PAD;
        const blk = this._ease(this.blocks, b.s.id, {x0, z0: z1 - b.d, x1, z1});
        blk.info.seed = b.s.seed;
        seen.add("blk:" + b.s.id);
        this._layoutBlock(b, pod, blk, waitingList, seen);
      }
    }

    _layoutBlock(b, pod, blk, waitingList, seen) {
      const s = b.s, {x0, x1, z1} = blk.target;
      const lead = this._agent(s.id, true);
      Object.assign(lead, {data: s, session: s, follower: null, pod, block: blk, hue: s.seed, own: s.seed,
        target: {x: (x0 + x1) / 2, z: z1 - G.ROW_D + 0.9}, waitSlot: waitingList.indexOf(s.id),
        waitCount: waitingList.length});
      seen.add(lead.id);
      for (const f of s.followers || []) {
        const slot = b.slotMap.get(f.id), row = Math.floor(slot / b.cols), col = slot % b.cols;
        const ag = this._agent(followerKey(s, f), false);
        Object.assign(ag, {data: f, session: s, follower: f, pod, block: blk, hue: s.seed,
          own: f.seed != null ? f.seed : seedOf(f.id),
          target: {x: x0 + (col + 0.5) * G.COL_W, z: z1 - G.ROW_D * (row + 2) + 0.9}, waitSlot: -1});
        seen.add(ag.id);
      }
    }

    // Stable desk slots per session: a subagent keeps its desk while it is listed.
    _slotsFor(s) {
      let m = this.slots.get(s.id);
      if (!m) { m = new Map(); this.slots.set(s.id, m); }
      const ids = new Set((s.followers || []).map((f) => f.id));
      for (const id of [...m.keys()]) if (!ids.has(id)) m.delete(id);
      const used = new Set(m.values());
      const fresh = [...ids].filter((id) => !m.has(id)).sort();
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
        a = {id, lead, desk: null, appear: 0, personAppear: 0, lamp: 0, push: 0, waiting: 0, loc: "seat", pos: null,
          path: null, pathS: 0, arrivedAt: -1e9, walk: 0, facing: -Math.PI / 2, fresh: true};
        this.agents.set(id, a);
      }
      return a;
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

    // Where a person is now, for labels, picking and the camera.
    where(a) {
      if (!a.desk) return null;
      const standing = [POSE.walk, POSE.stand, POSE.wait, POSE.window, POSE.delegate].includes(a.pose);
      if (!standing) return {x: a.desk.x, z: a.desk.z + 0.77 + 0.5 * a.push, y: a.lead ? 1.35 : 1.25, sitting: true};
      return {x: a.pos.x, z: a.pos.z, y: a.lead ? 1.6 : 1.5, sitting: false};
    }

    bounds() { return this.floor; }
  }

  Office.POSE = POSE;
  Office.SCREEN = SCREEN;
  Office.GEOMETRY = GEOMETRY;
  window.Office = Office;
})();
