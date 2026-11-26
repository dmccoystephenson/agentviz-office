/* page.js: boots the office. The feed client, HUD, labels and wire come from
   the agentviz kit (agentviz-*.js); this file wires them to the Office model
   and the Strata kit, and owns the camera. */
(function () {
  "use strict";
  const query = new URLSearchParams(location.search);
  const DEMO = query.get("demo") === "1";
  const DEFAULT_YAW = Math.PI / 2 - 0.32, DEFAULT_PITCH = 0.92, PORTRAIT_PITCH = 1.12, PORTRAIT_YAW = Math.PI / 2 - 0.14;
  const REASON = {question: "has a question", permission: "needs permission"};
  const canvas = document.getElementById("view");
  const office = new window.Office();
  const wire = AgentvizWire.create();
  const labels = AgentvizScreen.labels(document.getElementById("labels"));
  let kit = null, mod = null, feed = null, selectedId = null, hoveredId = null, camAnim = null, fitted = false;

  /* ------------------------------------------------------------ demo */
  // ?demo=1: the fixture with a scripted change every poll, so transitions
  // (walks to the window and the owner's door, action changes) can be seen
  // and captured. Synthetic only.
  function demoMutate(d, k) {
    const ACTS = ["edit", "run", "read", "web", "edit", "other", "read"];
    for (const s of d.sessions) {
      (s.followers || []).forEach((f, i) => {
        if (f.state === "active") f.action = ACTS[(i * 2 + Math.floor(k / 2)) % ACTS.length];
      });
      if (s.name === "Scout") s.stalled = true;
      if (s.name === "Anvil" && Math.floor(k / 3) % 3 === 2) Object.assign(s, {state: "waiting", action: "none", waitingReason: "question"});
      if (s.name === "Wren") s.action = Math.floor(k / 4) % 2 ? "delegate" : "read";
    }
    return d;
  }

  /* ------------------------------------------------------------ camera */
  // The engine's camera maths (Strata kit sk_frame) comes from the agentviz
  // kit, so a view can be fitted before it is drawn.
  const {projectWith, aim, fitPoints, sameView} = AgentvizScreen;

  // The part of the canvas nothing covers: below the top bar and, with
  // `panel`, above the bottom sheet (phone) or right of the card (desktop).
  function freeRect(panel) {
    const W = canvas.clientWidth, H = canvas.clientHeight;
    const bar = document.querySelector(".av-bar");
    const r = {x0: 8, y0: (bar ? bar.getBoundingClientRect().bottom : 0) + 8, x1: W - 8, y1: H - 8, W, H};
    const p = panel && hud.panel;
    if (p && !p.hidden) {
      if (p.offsetWidth >= W - 40) r.y1 = Math.min(r.y1, H - p.offsetHeight - 8);
      else if (p.offsetTop < H * 0.6) r.x0 = Math.max(r.x0, p.offsetLeft + p.offsetWidth + 8);
      else r.y1 = Math.min(r.y1, p.offsetTop - 8);
    }
    return r;
  }

  // The whole floor, walls and the OWNER sign included, as large as it fits
  // in the free part of the screen, on phones and desktops alike.
  function fitView() {
    const f = office.bounds(), fr = freeRect(false);
    const pts = [];
    for (const x of [f.x0, f.x1]) for (const z of [f.zN, f.zS]) pts.push([x, 0, z]);
    for (const x of [f.x0, f.x1]) pts.push([x, 2.6, f.zN]);
    pts.push([f.doorX, 3.6, f.oz1]);
    // Portrait screens look down more steeply, so the floor's depth fills the height.
    // and turn less, so the floor's rectangle wastes less of a narrow screen.
    const portrait = fr.W / fr.H < 0.8, pitch = portrait ? PORTRAIT_PITCH : DEFAULT_PITCH;
    // fitPoints keeps the yaw and pitch and centres the box by sliding the target.
    const c = {yaw: portrait ? PORTRAIT_YAW : DEFAULT_YAW, pitch, dist: 60, tx: (f.x0 + f.x1) / 2, ty: 0, tz: (f.zN + f.zS) / 2};
    return fitPoints(c, pts, fr, {minDist: 8, maxDist: 140});
  }
  let lastFit = null;
  function fit(instant) { if (office.floor) { lastFit = fitView(); flyTo(lastFit, instant); } }
  // A turned phone or a resized window refits, unless the view was moved since.
  function refitOnResize() {
    const c = kit && kit.cam;
    if (!c || !lastFit || camAnim) return;
    if (sameView(c, lastFit)) fit(true);
  }
  function flyTo(to, instant) {
    if (!kit) return;
    if (instant || kit.driven) { Object.assign(kit.cam, to); camAnim = null; return; }
    camAnim = {from: Object.assign({}, kit.cam), to, t0: performance.now(), ms: 700};
  }
  function stepCam() {
    if (!camAnim) return;
    const k = Math.min(1, (performance.now() - camAnim.t0) / camAnim.ms), e = AgentvizFeed.smoothstep(k);
    for (const key of Object.keys(camAnim.to)) kit.cam[key] = AgentvizFeed.lerp(camAnim.from[key], camAnim.to[key], e);
    if (k >= 1) camAnim = null;
  }
  // Brings an agent into view, clear of the card or bottom sheet. With
  // `zoom` it also moves in close; otherwise the camera only pans, and only
  // when the agent is covered or off screen.
  function reveal(a, zoom) {
    const w = office.where(a);
    if (!w) return;
    const fr = freeRect(true), base = camAnim ? camAnim.to : kit.cam;
    const to = AgentvizScreen.reveal(base, [w.x, w.y - 0.45, w.z], fr,
      zoom ? {set: {dist: Math.min(base.dist, 16), pitch: Math.max(base.pitch, 0.7)}, force: true} : {});
    if (to) flyTo(to);
  }
  function flyToDoor() {
    const f = office.bounds(), fr = freeRect(true);
    const c = {yaw: lastFit ? lastFit.yaw : DEFAULT_YAW, pitch: 0.75, dist: fr.W < 500 ? 19 : 15, tx: f.doorX, ty: 0.8, tz: f.oz1 + 0.5};
    flyTo(aim(c, f.doorX, 1.2, f.oz1 + 0.9, (fr.x0 + fr.x1) / 2, (fr.y0 + fr.y1) / 2 + 20, fr.W, fr.H));
  }

  /* ------------------------------------------------------------ labels */
  function leadLabel(a, w) {
    const s = a.data, waiting = s.state === "waiting", offline = s.state === "offline";
    const idle = s.state === "idle" || s.state === "dim", stalled = !!s.stalled && !waiting;
    const cls = "lead" + (waiting ? " waiting" : "") + (offline ? " offline" : "") + (idle ? " idle" : "") +
      (stalled ? " stalled" : "");
    const name = s.name || s.id.slice(0, 6);
    const text = waiting ? name + " " + (REASON[s.waitingReason] || "needs you") : name + (stalled ? " · stalled" : "");
    const el = labels.set("a:" + a.id, {cls, state: stalled ? "stalled" : s.state, action: s.state === "busy" ? s.action : null, text});
    const anchor = offline ? {x: a.desk.x, y: 1.4, z: a.desk.z + 0.8} : {x: w.x, y: w.y + (waiting ? 1.15 : 0.45), z: w.z};
    return {el, q: kit.project(anchor.x, anchor.y, anchor.z), rank: a.id === selectedId || waiting ? 0 : 1};
  }
  // At the monitor, a subagent's lane label sits on its screen. A stalled
  // lane says so in red and outranks its quiet neighbours.
  function followerLabel(a, w) {
    const f = a.data, stalled = !!f.stalled;
    const el = labels.set("a:" + a.id, {cls: "sub" + (stalled ? " stalled" : ""), state: stalled ? "stalled" : null,
      action: f.state === "resting" ? "none" : f.action, text: (f.label || f.id) + (stalled ? " · stalled" : "")});
    const anchor = a.loc === "seat" && !a.path ? {x: a.desk.x, y: 1.45, z: a.desk.z - 0.2} : {x: w.x, y: w.y + 0.4, z: w.z};
    return {el, q: kit.project(anchor.x, anchor.y, anchor.z), rank: a.id === selectedId ? 0 : (stalled ? 1 : 3)};
  }
  function placeLabels() {
    const items = [];
    labels.begin();
    for (const a of office.agents.values()) {
      const w = a.appear >= 0.5 && a.data && office.where(a);
      if (w) items.push(a.lead ? leadLabel(a, w) : followerLabel(a, w));
    }
    for (const [id, p] of office.pods) {
      if (p.appear < 0.5) continue;
      const el = labels.set("p:" + id, {cls: "place", text: p.info.label});
      items.push({el, q: kit.project((p.cur.x0 + p.cur.x1) / 2, 1.3, p.cur.z0), rank: 2});
    }
    labels.end();
    // On screen and clear of the bar, panel and legend; a label that must
    // show (waiting, selected) drops below the bar instead of hiding.
    hud.placeLabels(items, {canvas});
  }

  /* ------------------------------------------------------------ picking + selection */
  function pick(x, y, radius) {
    const candidates = [];
    for (const a of office.agents.values()) {
      const w = a.appear >= 0.5 && office.where(a);
      if (w) candidates.push({item: a, points: [[w.x, w.y, w.z], [w.x, w.y - 0.45, w.z], [a.desk.x, 1.0, a.desk.z - 0.1]]});
    }
    return AgentvizScreen.pick(kit.project, candidates, x, y, radius);
  }
  const targetOf = (a) => (a ? {session: a.session, follower: a.follower} : null);
  const idOf = (target) => (target ? (target.follower ? AgentvizFeed.followerKey(target.session, target.follower) : target.session.id) : null);

  function onSelect(target, how) {
    selectedId = idOf(target);
    const a = selectedId && office.agents.get(selectedId);
    if (kit) kit.selected = a || null;
    if (how === "waiting") flyToDoor();
    else if (a && how === "list") reveal(a, true);
    else if (a && how === "tap") reveal(a, false);
  }

  const hud = AgentvizHud.create({
    title: "Agent Office",
    oneRow: true,
    legend: document.getElementById("legend"),
    buttons: [{icon: "home", label: "Recentre the view", onClick: () => fit(false)}],
    onSelect,
  });

  function accept(doc) {
    office.update(doc);
    hud.update(doc);
    if (!fitted && office.floor) { fitted = true; fit(true); }
  }

  /* ------------------------------------------------------------ the sun */
  // ?night=1 / ?night=0 pins it; otherwise the real sun by the local clock,
  // the longitude taken from the time zone (no location is read).
  function setSun() {
    const n = query.get("night");
    if (n === "1" || n === "0") {
      office.nightForce = Number(n);
      kit.setSun(Date.UTC(2026, 5, n === "1" ? 22 : 21, n === "1" ? 5 : 17, 0, 0) / 1000, 40, -105);  // 23:00 or 11:00 at -105
      return;
    }
    office.nightForce = -1;
    kit.setSun(Date.now() / 1000, 40, -new Date().getTimezoneOffset() / 4);
  }

  /* ------------------------------------------------------------ boot */
  function frame(t) {
    stepCam();
    office.step(t);
    if (!office.floor) return;
    const highlight = (id) => (id === selectedId ? 1 : id === hoveredId ? 2 : 0);
    window.OfficeFrame.write(wire, office, feed.doc, highlight, feed.pulse).send(mod);
  }

  function screenTargets() {
    const r = canvas.getBoundingClientRect();
    return [...office.agents.values()].filter((a) => a.appear > 0.5).map((a) => {
      const w = office.where(a), s = w && kit.project(w.x, w.y - 0.45, w.z);
      return s && {id: a.id, x: s.x + r.left, y: s.y + r.top};
    }).filter(Boolean);
  }

  function createKit() {
    return StrataKit.create({
      module: mod, canvas,
      camera: {yaw: DEFAULT_YAW, pitch: DEFAULT_PITCH, dist: 40, tx: 0, ty: 0, tz: 14},
      limits: {maxDist: 150, minDist: 4, maxPitch: 1.4},
      idleOrbit: false,
      frame,
      after: () => { if (office.floor) placeLabels(); },
      pick,
      idOf: (a) => a.id,
      onTap: (a) => hud.select(targetOf(a), "tap"),
      onHover(a) { hoveredId = a ? a.id : null; canvas.style.cursor = a ? "pointer" : ""; },
      onSlow: () => StrataLoader.slow(),
      onFatal: (code, msg) => StrataLoader.fallback(code, msg),
      targets: screenTargets,
    });
  }

  async function boot() {
    try {
      mod = await StrataLoader.load({script: "office-engine.js", factory: "createOfficeEngine",
        wasm: "office-engine.wasm", title: "the office"});
    } catch (error) {
      console.error("office: the engine did not load", error);
      return;  // StrataLoader has already shown its fallback
    }
    try {
      kit = createKit();
    } catch (error) {
      StrataLoader.fallback(error.code === "no-webgl2" ? "no-webgl2" : "engine");
      return;
    }
    feed = AgentvizFeed.create({onUpdate: accept, onStatus: hud.setStatus, transform: DEMO ? demoMutate : null});
    window.__office = office;
    window.__page = {kit, office, hud, fit, flyTo, projectWith,
      select: (id, focus) => hud.select(targetOf(office.agents.get(id)), focus ? "list" : "api"),
      get doc() { return feed.doc; }, get polls() { return feed.polls; },
      stopPolling: () => feed.stop(), pollOnce: () => feed.pollOnce(), feed: (d) => feed.push(d), demoMutate};
    setSun();
    setInterval(setSun, 60000);
    addEventListener("resize", () => requestAnimationFrame(refitOnResize));
    feed.start();
    kit.start();
    if (kit.driven) kit.drive(0);
    StrataLoader.done();
    canvas.focus({preventScroll: true});
  }

  boot();
})();
