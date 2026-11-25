/* office-walk.js: what each person in the office does and where they go.
   From an agent's feed data it picks a pose, a monitor screen and a place
   (seat, window, the owner's door), then walks the person there along the
   aisles. Mixed into Office.prototype (office-model.js). */
(function () {
  "use strict";
  const {ease} = window.AgentvizFeed;
  const {POSE, SCREEN, GEOMETRY: G} = window.Office;
  const WALK_SPEED = 2.4;   // units per second
  const DWELL = 4.0;        // seconds at a place before a non-urgent move

  const ACTION_POSE = {edit: POSE.type, run: POSE.run, read: POSE.read, web: POSE.window,
    delegate: POSE.delegate, think: POSE.think, other: POSE.other, none: POSE.think};
  const ACTION_SCREEN = {edit: SCREEN.edit, run: SCREEN.run, read: SCREEN.read, web: SCREEN.web,
    delegate: SCREEN.delegate, think: SCREEN.think, other: SCREEN.other, none: SCREEN.think};
  const sq = (p, q) => (p.x - q.x) ** 2 + (p.z - q.z) ** 2;

  function leadIntent(s) {
    if (s.state === "offline") return {loc: "seat", pose: POSE.absent, screen: SCREEN.off, lamp: 0, push: 0, jacket: true};
    if (s.state === "idle" || s.state === "dim") return {loc: "seat", pose: POSE.slump, screen: SCREEN.idle, lamp: 0, push: 0.15};
    if (s.state === "waiting") return {loc: "door", pose: POSE.wait, screen: SCREEN.waiting, lamp: 0.6, push: 0.3};
    const act = s.action || "none";
    return {loc: act === "web" ? "window" : "seat", pose: ACTION_POSE[act] != null ? ACTION_POSE[act] : POSE.other,
      screen: s.stalled ? SCREEN.stalled : (ACTION_SCREEN[act] != null ? ACTION_SCREEN[act] : SCREEN.other),
      lamp: 1, push: act === "delegate" ? 0.35 : 0};
  }

  // A stalled subagent (quiet for minutes, not finished) gets the red monitor.
  function followerIntent(f) {
    const stalledScreen = f.stalled ? SCREEN.stalled : null;
    if (f.state === "resting") return {loc: "seat", pose: POSE.rest, screen: stalledScreen || SCREEN.dim, lamp: 0, push: 1};
    const act = f.action && f.action !== "none" ? f.action : "other";
    return {loc: act === "web" ? "window" : "seat", pose: ACTION_POSE[act], screen: stalledScreen || ACTION_SCREEN[act],
      lamp: 1, push: 0};
  }

  const Walk = {
    _intent(a) { return a.lead ? leadIntent(a.data) : followerIntent(a.data); },

    _home(a) { return {x: a.desk.x, z: a.desk.z + 1.45}; },

    _spot(a, loc) {
      const f = this.floor, home = this._home(a);
      if (loc === "window") {
        const west = a.pod.info.side === 0;
        return {x: west ? f.x0 + 0.75 : f.x1 - 0.75, z: Math.min(Math.max(home.z, f.zN + 6), f.zS - 1),
          facing: west ? Math.PI : 0};
      }
      if (loc === "door") {  // the queue before the owner's door, four to a row
        const n = Math.max(1, a.waitCount), k = Math.max(0, a.waitSlot);
        const perRow = 4, row = Math.floor(k / perRow), inRow = Math.min(perRow, n - row * perRow);
        return {x: f.doorX + (k % perRow - (inRow - 1) / 2) * 0.85, z: f.oz1 + 0.85 + row * 0.85, facing: -Math.PI / 2};
      }
      return {x: home.x, z: home.z, facing: -Math.PI / 2};
    },

    // Aisle route from an agent's desk to a spot away from it. Subagents first
    // step out sideways between the desk rows, to the block's edge, so they do
    // not walk through the desks south of them.
    _route(a, loc) {
      const home = Object.assign({}, this._home(a)), spot = this._spot(a, loc), az = a.pod.info.aisleZ;
      const out = [home];
      if (!a.lead && a.block) {
        const west = (a.pod.info.side === 0) === (loc === "window");
        const ex = west ? a.block.cur.x0 - G.BLOCK_GAP / 2 : a.block.cur.x1 + G.BLOCK_GAP / 2;
        out.push({x: ex, z: home.z});
        home.x = ex;
      }
      if (loc === "window") return out.concat([{x: home.x, z: az}, {x: spot.x, z: az}, spot]);
      if (loc === "door") {
        const lane = a.pod.info.side === 0 ? -0.7 : 0.7;
        return out.concat([{x: home.x, z: az}, {x: lane, z: az}, {x: lane, z: spot.z + 0.9}, spot]);
      }
      return [home];
    },

    // Retrace to the desk (if away), then out to the new spot.
    _startWalk(a, to) {
      const cur = a.pos ? {x: a.pos.x, z: a.pos.z} : this._home(a);
      let pts = [cur];
      if (a.loc !== "seat" || a.path) {
        const back = (a.routeOut || this._route(a, a.loc)).slice().reverse();
        let nearest = 0;
        back.forEach((p, i) => { if (sq(p, cur) < sq(back[nearest], cur)) nearest = i; });
        pts = pts.concat(back.slice(nearest));
      }
      a.routeOut = to !== "seat" ? this._route(a, to) : null;
      if (a.routeOut) pts = pts.concat(a.routeOut);
      const clean = pts.filter((p, i) => i === 0 || sq(p, pts[i - 1]) > 1e-4);
      let len = 0;
      for (let i = 1; i < clean.length; i++) len += Math.sqrt(sq(clean[i], clean[i - 1]));
      Object.assign(a, {path: clean, pathLen: len, pathS: 0, dest: to});
      if (len < 0.05) this._arrive(a);
    },

    _arrive(a) {
      a.loc = a.dest;
      a.path = null;
      a.arrivedAt = this.clock;
      if (a.loc === "seat") a.routeOut = null;
    },

    _easeAgent(a, it, dt) {
      if (!a.desk) a.desk = {x: a.target.x, z: a.target.z};
      a.desk.x = ease(a.desk.x, a.target.x, dt, 3);
      a.desk.z = ease(a.desk.z, a.target.z, dt, 3);
      a.appear = ease(a.appear, a.gone ? 0 : 1, dt, a.gone ? 4 : 2.5);
      a.personAppear = ease(a.personAppear, it.pose !== POSE.absent && !a.gone ? 1 : 0, dt, 3);
      a.lamp = ease(a.lamp, it.lamp, dt, 4);
      a.push = ease(a.push, it.push, dt, 3);
      a.waiting = ease(a.waiting, it.pose === POSE.wait ? 1 : 0, dt, 4);
    },

    // First sight: start where the data says, without a walk.
    _place(a, it) {
      a.fresh = false;
      a.loc = it.loc;
      a.routeOut = it.loc === "seat" ? null : this._route(a, it.loc);
      const sp = this._spot(a, it.loc);
      a.pos = {x: sp.x, z: sp.z};
      a.facing = sp.facing;
      a.arrivedAt = -1e9;
    },

    _walkAlong(a, dt) {
      a.pathS += WALK_SPEED * dt;
      a.walk += WALK_SPEED * dt;
      if (a.pathS >= a.pathLen) {
        const end = a.path[a.path.length - 1];
        a.pos = {x: end.x, z: end.z};
        this._arrive(a);
        return;
      }
      let s = a.pathS;
      for (let i = 1; i < a.path.length; i++) {
        const p0 = a.path[i - 1], p1 = a.path[i], l = Math.sqrt(sq(p0, p1));
        if (s <= l) {
          const k = l > 0 ? s / l : 1;
          a.pos = {x: p0.x + (p1.x - p0.x) * k, z: p0.z + (p1.z - p0.z) * k};
          a.facing = Math.atan2(p1.z - p0.z, p1.x - p0.x);
          return;
        }
        s -= l;
      }
    },

    // Standing spots follow their slot (the door queue reshuffles, desks slide).
    _holdSpot(a, dt) {
      const sp = this._spot(a, a.loc);
      a.pos.x = ease(a.pos.x, sp.x, dt, 3);
      a.pos.z = ease(a.pos.z, sp.z, dt, 3);
      a.facing = sp.facing;
    },

    _poseOf(a, it, present) {
      if (a.path) return POSE.walk;
      if (a.loc === "seat") return !present ? POSE.absent : (it.loc === "seat" ? it.pose : POSE.stand);
      return a.loc === it.loc ? it.pose : POSE.stand;
    },

    _stepAgent(a, dt) {
      const it = a.intent || {loc: "seat", pose: POSE.think, screen: SCREEN.off, lamp: 0, push: 0};
      const present = it.pose !== POSE.absent && !a.gone;
      this._easeAgent(a, it, dt);
      if (a.fresh) this._place(a, it);
      const want = present ? it.loc : "seat", heading = a.path ? a.dest : a.loc;
      if (want !== heading) {
        const urgent = want === "door" || heading === "door";
        if (urgent || this.clock - a.arrivedAt > DWELL || a.path) this._startWalk(a, want);
      }
      if (a.path) this._walkAlong(a, dt);
      else this._holdSpot(a, dt);
      a.pose = this._poseOf(a, it, present);
      a.screen = it.screen;
      a.jacket = !!it.jacket;
    },
  };

  Object.assign(window.Office.prototype, Walk);
})();
