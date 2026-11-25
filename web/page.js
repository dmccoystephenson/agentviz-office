/* page.js: polls /api/agents, drives the Office model and the Strata kit,
   and owns the HTML around the canvas: labels, HUD chips, the selection
   sheet and the legend. */
(function () {
  "use strict";
  const q = new URLSearchParams(location.search);
  const DEMO = q.get("demo") === "1";
  const FIXTURE = q.get("fixture") === "1" || DEMO;
  const POLL_MS = 2000;
  const STATE_COLOUR = {busy: "var(--busy)", waiting: "var(--note)", idle: "var(--idle)", dim: "var(--idle)",
    offline: "var(--off)", active: "var(--busy)", resting: "var(--idle)"};
  const ACTION_ICON = {
    edit: '<path d="M3 13l1-4 7-7 3 3-7 7z" fill="currentColor"/>',
    run: '<path d="M2 3h12v10H2z" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M4 6l2 2-2 2M7 10h4" stroke="currentColor" stroke-width="1.5" fill="none"/>',
    read: '<path d="M2 3c2-1 4-1 6 1 2-2 4-2 6-1v10c-2-1-4-1-6 1-2-2-4-2-6-1z" fill="currentColor"/>',
    web: '<circle cx="8" cy="8" r="6" fill="none" stroke="currentColor" stroke-width="1.5"/><path d="M2 8h12M8 2c-3 3-3 9 0 12M8 2c3 3 3 9 0 12" stroke="currentColor" fill="none"/>',
    delegate: '<circle cx="8" cy="4" r="2.5" fill="currentColor"/><path d="M8 7v3M8 10l-5 3M8 10l5 3" stroke="currentColor" stroke-width="1.5"/>',
    think: '<circle cx="9" cy="6" r="4" fill="currentColor"/><circle cx="4" cy="12" r="1.6" fill="currentColor"/><circle cx="2" cy="15" r="0.9" fill="currentColor"/>',
    other: '<circle cx="8" cy="8" r="2.5" fill="currentColor"/><path d="M8 1v3M8 12v3M1 8h3M12 8h3" stroke="currentColor" stroke-width="1.5"/>',
    none: '<path d="M4 8h8" stroke="currentColor" stroke-width="2"/>',
  };
  const icon = (a) => `<svg class="ic" viewBox="0 0 16 16" style="color:var(--a-${ACTION_ICON[a] ? a : "none"})">${ACTION_ICON[a] || ACTION_ICON.none}</svg>`;
  const esc = (s) => String(s == null ? "" : s).replace(/[&<>"']/g, (c) => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"}[c]));

  const labelsEl = document.getElementById("labels");
  const sheet = document.getElementById("sheet"), sheetBody = document.getElementById("sheetBody");
  const canvas = document.getElementById("view");
  let kit = null, office = null, mod = null, doc = null, selectedId = null, hoveredId = null, polls = 0;
  let camAnim = null, fitted = false;
  const labelEls = new Map();

  /* ------------------------------------------------------------ demo */
  // ?demo=1: the fixture, with a scripted change every poll so transitions
  // (walks to the window and the owner's door, action changes) can be seen
  // and captured. Synthetic only.
  function demoMutate(d, k) {
    const ACTS = ["edit", "run", "read", "web", "edit", "other", "read"];
    for (const s of d.sessions) {
      (s.followers || []).forEach((f, i) => {
        if (f.state !== "active") return;
        f.action = ACTS[(i * 2 + Math.floor(k / 2)) % ACTS.length];
      });
      if (s.name === "Scout") s.stalled = true;
      if (s.name === "Anvil" && Math.floor(k / 3) % 3 === 2) { s.state = "waiting"; s.action = "none"; }
      if (s.name === "Wren") s.action = Math.floor(k / 4) % 2 ? "delegate" : "read";
    }
    d.counts.waiting = d.sessions.filter((s) => s.state === "waiting").length;
    return d;
  }

  /* ------------------------------------------------------------ feed */
  async function poll() {
    try {
      const r = await fetch("/api/agents" + (FIXTURE ? "?fixture=1" : ""), {cache: "no-store"});
      if (!r.ok) throw new Error(r.status);
      let d = await r.json();
      if (DEMO) d = demoMutate(d, polls);
      polls++;
      doc = d;
      office.update(d);
      if (!fitted) { fitted = true; fit(true); }
      renderHud();
      if (selectedId) renderSheet();
    } catch (e) {
      // Keep the last snapshot; the next poll tries again.
    }
  }

  function renderHud() {
    const ss = doc.sessions || [];
    const n = (st) => ss.filter((s) => s.state === st).length;
    document.getElementById("nBusy").textContent = n("busy");
    document.getElementById("nIdle").textContent = n("idle") + n("dim");
    document.getElementById("nOff").textContent = n("offline");
    document.getElementById("nSub").textContent = ss.reduce((a, s) => a + (s.followers || []).filter((f) => f.state === "active").length, 0);
    const w = n("waiting");
    document.getElementById("nWait").textContent = w;
    document.getElementById("waitChip").hidden = w === 0;
    document.getElementById("fixtureBadge").hidden = !doc.fixture && !DEMO;
    document.title = (w ? `(${w}) ` : "") + "Agent Office";
  }

  /* ------------------------------------------------------------ camera */
  const DEFAULT_YAW = Math.PI / 2 - 0.32, DEFAULT_PITCH = 0.92;
  function fitView() {
    const f = office.bounds();
    const w = f.x1 - f.x0, d = f.zS - f.zN;
    const aspect = canvas.clientWidth / Math.max(1, canvas.clientHeight);
    const span = Math.max(w / Math.min(aspect, 1.9) * 1.05, d * 0.95);
    return {yaw: DEFAULT_YAW, pitch: DEFAULT_PITCH, dist: Math.min(140, span * 0.82 + 3),
      tx: (f.x0 + f.x1) / 2, ty: 0, tz: (f.zN + f.zS) / 2 + d * 0.04};
  }
  function fit(instant) { flyTo(fitView(), instant); }
  function flyTo(to, instant) {
    if (!kit) return;
    if (instant || kit.driven) { Object.assign(kit.cam, to); camAnim = null; return; }
    camAnim = {from: Object.assign({}, kit.cam), to, t0: performance.now(), ms: 700};
  }
  function stepCam() {
    if (!camAnim) return;
    const k = Math.min(1, (performance.now() - camAnim.t0) / camAnim.ms), e = k * k * (3 - 2 * k);
    for (const key of Object.keys(camAnim.to)) kit.cam[key] = camAnim.from[key] + (camAnim.to[key] - camAnim.from[key]) * e;
    if (k >= 1) camAnim = null;
  }
  function focusOn(a, dist) {
    const w = office.where(a);
    if (!w) return;
    flyTo({tx: w.x, ty: 0.8, tz: w.z - 1, dist: dist || Math.min(kit.cam.dist, 16), pitch: Math.max(kit.cam.pitch, 0.7)});
  }

  /* ------------------------------------------------------------ labels */
  function labelFor(key, cls) {
    let el = labelEls.get(key);
    if (!el) {
      el = document.createElement("div");
      labelsEl.appendChild(el);
      labelEls.set(key, el);
    }
    if (el.className !== "label " + cls) el.className = "label " + cls;
    return el;
  }
  function setHtml(el, html) { if (el._h !== html) { el._h = html; el.innerHTML = html; } }

  function placeLabels() {
    const items = [], live = new Set();
    for (const a of office.agents.values()) {
      if (a.appear < 0.5 || !a.data) continue;
      const w = office.where(a);
      if (!w) continue;
      const key = "a:" + a.id;
      live.add(key);
      if (a.lead) {
        const s = a.data, waiting = s.state === "waiting";
        const el = labelFor(key, "lead" + (waiting ? " waiting" : "") + (s.state === "offline" ? " offline" : ""));
        const dot = `<span class="dot" style="background:${s.stalled ? "var(--stall)" : STATE_COLOUR[s.state] || "var(--idle)"}"></span>`;
        setHtml(el, waiting ? `${dot}${esc(s.name)} needs you` : `${dot}${s.state === "busy" ? icon(s.action) : ""}${esc(s.name || s.id.slice(0, 6))}`);
        const anchor = s.state === "offline" ? {x: a.desk.x, y: 1.4, z: a.desk.z + 0.8} : {x: w.x, y: w.y + (waiting ? 1.15 : 0.45), z: w.z};
        items.push({el, q: kit.project(anchor.x, anchor.y, anchor.z), rank: a.id === selectedId || waiting ? 0 : 1});
      } else {
        const f = a.data;
        const el = labelFor(key, "sub");
        setHtml(el, `${icon(f.state === "resting" ? "none" : f.action)}${esc(f.label || f.id)}`);
        // At the monitor: the lane label sits on the satellite's screen.
        const anchor = a.loc === "seat" && !a.path ? {x: a.desk.x, y: 1.45, z: a.desk.z - 0.2} : {x: w.x, y: w.y + 0.4, z: w.z};
        items.push({el, q: kit.project(anchor.x, anchor.y, anchor.z), rank: a.id === selectedId ? 0 : 3});
      }
    }
    for (const [id, p] of office.pods) {
      if (p.appear < 0.5) continue;
      const key = "p:" + id;
      live.add(key);
      const el = labelFor(key, "place");
      setHtml(el, esc(p.info.label));
      items.push({el, q: kit.project((p.cur.x0 + p.cur.x1) / 2, 1.3, p.cur.z0), rank: 2});
    }
    for (const [key, el] of labelEls) if (!live.has(key)) { el.remove(); labelEls.delete(key); }
    StrataKit.placeLabels(items);
  }

  /* ------------------------------------------------------------ picking */
  function pick(x, y, radius) {
    let best = null, bd = radius * radius;
    for (const a of office.agents.values()) {
      if (a.appear < 0.5) continue;
      const w = office.where(a);
      if (!w) continue;
      const pts = [[w.x, w.y, w.z], [w.x, w.y - 0.45, w.z], [a.desk.x, 1.0, a.desk.z - 0.1]];
      for (const p of pts) {
        const s = kit.project(p[0], p[1], p[2]);
        if (!s) continue;
        const d = (s.x - x) ** 2 + (s.y - y) ** 2;
        if (d < bd) { bd = d; best = a; }
      }
    }
    return best;
  }

  function select(a, focus) {
    selectedId = a ? a.id : null;
    if (kit) kit.selected = a || null;
    if (a && focus) focusOn(a);
    renderSheet();
  }

  /* ------------------------------------------------------------ sheet */
  function chip(html, colour) {
    return `<span class="chip">${colour ? `<span class="dot" style="background:${colour}"></span>` : ""}${html}</span>`;
  }
  function renderSheet() {
    const a = selectedId && office.agents.get(selectedId);
    if (!a || !a.data) { sheet.hidden = true; return; }
    sheet.hidden = false;
    let html;
    if (a.lead) {
      const s = a.data;
      const act = s.state === "busy" ? s.action : "none";
      const label = s.activity && s.activity.label;
      const lanes = (s.followers || []).map((f) =>
        `<button class="chip" data-id="${esc(s.id + "/" + f.id)}">${icon(f.state === "resting" ? "none" : f.action)}${esc(f.label || f.id)}</button>`).join("");
      html = `<h2><span class="dot" style="width:12px;height:12px;border-radius:50%;background:${STATE_COLOUR[s.state] || "var(--idle)"}"></span>${esc(s.name || s.id)}</h2>
        <div class="sub">session &middot; ${esc(s.placeLabel || s.place)}</div>
        <div class="row">${chip(s.state === "waiting" ? "<b>needs you</b>" : esc(s.state), STATE_COLOUR[s.state])}
          ${s.state === "busy" ? chip(icon(act) + esc(act)) : ""}${s.stalled ? chip("<b>stalled</b>", "var(--stall)") : ""}</div>
        <dl class="kv">
          ${label ? `<dt>doing</dt><dd>${esc(label)}</dd>` : ""}
          ${s.activity && s.activity.tool ? `<dt>tool</dt><dd>${esc(s.activity.tool)}</dd>` : ""}
          <dt>branch</dt><dd>${esc(s.branch || "—")}</dd>
          <dt>subagents</dt><dd>${(s.followers || []).filter((f) => f.state === "active").length} active of ${(s.followers || []).length}</dd>
        </dl>
        ${lanes ? `<div class="lanes">${lanes}</div>` : ""}`;
    } else {
      const f = a.data, s = a.session;
      html = `<h2>${icon(f.state === "resting" ? "none" : f.action)}${esc(f.label || f.id)}</h2>
        <div class="sub">subagent of <button class="chip" data-id="${esc(s.id)}">${esc(s.name || s.id)}</button></div>
        <div class="row">${chip(esc(f.state), STATE_COLOUR[f.state])}${f.state === "active" ? chip(icon(f.action) + esc(f.action)) : ""}</div>
        <dl class="kv">
          ${f.tool ? `<dt>tool</dt><dd>${esc(f.tool)}${f.toolLabel ? " &middot; " + esc(f.toolLabel) : ""}</dd>` : ""}
          <dt>tool calls</dt><dd>${esc(f.toolCount != null ? f.toolCount : "—")}</dd>
          <dt>branch</dt><dd>${esc(s.branch || "—")}</dd>
          <dt>place</dt><dd>${esc(s.placeLabel || s.place)}</dd>
        </dl>`;
    }
    if (sheetBody._h !== html) { sheetBody._h = html; sheetBody.innerHTML = html; }
  }
  sheetBody.addEventListener("click", (e) => {
    const b = e.target.closest("[data-id]");
    if (!b) return;
    const a = office.agents.get(b.dataset.id);
    if (a) select(a, true);
  });
  document.getElementById("sheetClose").addEventListener("click", () => select(null));
  document.getElementById("waitChip").addEventListener("click", () => {
    const w = [...office.agents.values()].filter((a) => a.lead && a.data && a.data.state === "waiting");
    if (!w.length) return;
    const i = w.findIndex((a) => a.id === selectedId);
    select(w[(i + 1) % w.length], false);  // cycles; the first press takes the first
    const f = office.bounds();
    flyTo({tx: f.doorX, ty: 0.8, tz: f.oz1 + 0.5, dist: 15, pitch: 0.75, yaw: DEFAULT_YAW});
  });
  document.getElementById("homeBtn").addEventListener("click", () => fit(false));
  const legendBtn = document.getElementById("legendBtn"), legend = document.getElementById("legend");
  legendBtn.addEventListener("click", () => {
    legend.hidden = !legend.hidden;
    legendBtn.setAttribute("aria-expanded", String(!legend.hidden));
  });
  addEventListener("keydown", (e) => { if (e.key === "Escape") { select(null); legend.hidden = true; } });

  /* ------------------------------------------------------------ boot */
  function setSun() {
    // ?night=1 / ?night=0 pins it; otherwise the real sun by the local clock,
    // the longitude taken from the time zone (no location is read).
    const n = q.get("night");
    if (n === "1" || n === "0") {
      office.nightForce = Number(n);
      kit.setSun(Date.UTC(2026, 5, n === "1" ? 22 : 21, n === "1" ? 5 : 17, 0, 0) / 1000, 40, -105);  // 23:00 or 11:00 at -105
      return;
    }
    office.nightForce = -1;
    const lon = -new Date().getTimezoneOffset() / 4;
    kit.setSun(Date.now() / 1000, 40, lon);
  }

  (async () => {
    try {
      mod = await StrataLoader.load({script: "office-engine.js", factory: "createOfficeEngine",
        wasm: "office-engine.wasm", title: "the office"});
    } catch (e) { return; }
    office = new window.Office(mod);
    window.__office = office;
    try {
      kit = StrataKit.create({
        module: mod, canvas,
        camera: {yaw: DEFAULT_YAW, pitch: DEFAULT_PITCH, dist: 40, tx: 0, ty: 0, tz: 14},
        limits: {maxDist: 150, minDist: 4, maxPitch: 1.4},
        idleOrbit: false,
        frame(t) {
          stepCam();
          office.step(t);
          office.write(selectedId, hoveredId);
        },
        after: () => { if (office.floor) placeLabels(); },
        pick,
        idOf: (a) => a.id,
        onTap(a) { select(a, false); },
        onHover(a) { hoveredId = a ? a.id : null; canvas.style.cursor = a ? "pointer" : ""; },
        onSlow: () => StrataLoader.slow(),
        onFatal: (code, msg) => StrataLoader.fallback(code, msg),
        targets: () => [...office.agents.values()].filter((a) => a.appear > 0.5).map((a) => {
          const w = office.where(a);
          const s = w && kit.project(w.x, w.y - 0.45, w.z);
          const r = canvas.getBoundingClientRect();
          return s && {id: a.id, x: s.x + r.left, y: s.y + r.top};
        }).filter(Boolean),
      });
    } catch (e) {
      return StrataLoader.fallback(e.code === "no-webgl2" ? "no-webgl2" : "engine");
    }
    window.__page = {kit, office, select: (id, focus) => select(office.agents.get(id), focus), fit, flyTo,
      get doc() { return doc; }, get polls() { return polls; }};
    setSun();
    setInterval(setSun, 60000);
    await poll();
    let timer = setInterval(poll, POLL_MS);
    // For captures: stop polling and hand the page snapshots directly.
    window.__page.stopPolling = () => { clearInterval(timer); timer = null; };
    window.__page.feed = (d) => { doc = d; office.update(d); renderHud(); if (selectedId) renderSheet(); };
    window.__page.demoMutate = demoMutate;
    kit.start();
    if (kit.driven) kit.drive(0);
    StrataLoader.done();
    canvas.focus({preventScroll: true});
  })();
})();
