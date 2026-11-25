/* office-frame.js: packs the office for the scene, once a frame, in the
   agentviz wire format (vendor/agentviz-kit, docs/KIT.md). scene/room.h,
   desk.h, person.h and trails.h read what is written here.

     scene floats  x0, zN, x1, zS, corridor x0/x1, owner's office x0/x1/z1, doorX,
                   anyone waiting (eased), night override (-1: the real sun)
     zones         kind 0 pod, 1 session block; extras: busy share, waiting
     agents        extras: desk x, desk z, pose, walk phase, lamp, screen, chair push,
                   jacket, person appear, waiting (eased), binder out
     links         kind 0 waiting trail (phase = distance along it), 1 delegation

   Classic script: window.OfficeFrame. */
(function () {
  "use strict";
  const {POSE} = window.Office;
  const ZONE = {pod: 0, block: 1};
  const LINK = {waiting: 0, delegation: 1};

  function sceneFloats(office) {
    const f = office.floor;
    let anyWaiting = 0;
    for (const a of office.agents.values()) if (a.lead && a.desk) anyWaiting = Math.max(anyWaiting, a.waiting);
    return [f.x0, f.zN, f.x1, f.zS, f.cx0, f.cx1, f.ox0, f.ox1, f.oz1, f.doorX, anyWaiting, office.nightForce];
  }

  // A rect shrinks towards its centre as it disappears.
  function zone(wire, id, r, kind) {
    const k = r.appear, cx = (r.cur.x0 + r.cur.x1) / 2, cz = (r.cur.z0 + r.cur.z1) / 2;
    wire.zone({id, kind, seed: r.info.seed || 0, appear: k,
      x0: cx + (r.cur.x0 - cx) * k, z0: cz + (r.cur.z0 - cz) * k, x1: cx + (r.cur.x1 - cx) * k, z1: cz + (r.cur.z1 - cz) * k,
      extra: [r.info.busy || 0, r.info.waiting ? 1 : 0]});
  }

  function agent(wire, a, highlight, pulse) {
    const pos = a.pos || a.desk;
    wire.agent({session: a.session, follower: a.follower, x: pos.x, y: 0, z: pos.z, facing: a.facing,
      appear: a.appear, highlight, pulse,
      extra: [a.desk.x, a.desk.z, a.pose, a.walk, a.lamp, a.screen, a.push, a.jacket ? 1 : 0, a.personAppear,
        a.waiting, a.pose === POSE.read ? 1 : 0]});
  }

  // The orange trail from a waiting lead's desk to the owner's door.
  function waitingTrail(wire, office, a) {
    const route = a.routeOut || office._route(a, "door");
    let along = 0;
    for (let i = 1; i < route.length; i++) {
      const p0 = route[i - 1], p1 = route[i];
      wire.link({kind: LINK.waiting, from: a.id, x0: p0.x, z0: p0.z, x1: p1.x, z1: p1.z, phase: along,
        seed: a.hue, alpha: Math.max(a.waiting, 0.4)});
      along += Math.hypot(p1.x - p0.x, p1.z - p0.z);
    }
  }

  // Notes flying from a delegating lead to each of its active subagents.
  function delegation(wire, agents, a) {
    let i = 0;
    for (const b of agents) {
      if (b.lead || b.session.id !== a.session.id || b.data.state !== "active") continue;
      wire.link({kind: LINK.delegation, from: a.id, to: b.id, x0: a.desk.x, z0: a.desk.z + 0.6, x1: b.desk.x,
        z1: b.desk.z, phase: (i++) * 0.37, seed: a.hue, alpha: 1});
    }
  }

  // highlightOf(id) -> 0 none, 1 selected, 2 hovered; pulseOf(id) -> 0..1.
  function write(wire, office, doc, highlightOf, pulseOf) {
    wire.begin({doc, scene: sceneFloats(office)});
    for (const [id, r] of office.pods) zone(wire, "pod:" + id, r, ZONE.pod);
    for (const [id, r] of office.blocks) zone(wire, "block:" + id, r, ZONE.block);
    const agents = [...office.agents.values()].filter((a) => a.desk);
    for (const a of agents) agent(wire, a, highlightOf(a.id), pulseOf(a.id));
    for (const a of agents) {
      if (!a.lead || a.appear < 0.3) continue;
      if (a.waiting > 0.05 || (a.path && a.dest === "door")) waitingTrail(wire, office, a);
      if (a.pose === POSE.delegate) delegation(wire, agents, a);
    }
    return wire;
  }

  window.OfficeFrame = {write, ZONE, LINK};
})();
