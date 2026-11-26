// Regenerates every README image in docs/media/ from the DEMO feed (never live):
//   npm run screenshots [-- name ...]
// Stills are drawn with ?drive=1 (the kit draws one frame on demand), so they
// repeat. The GIF steps scene time frame by frame through the ?demo=1 script
// (the fixture with scripted changes) and is encoded with Pillow.
const fs = require("fs");
const os = require("os");
const path = require("path");
const h = require("../vendor/agentviz-kit/tests/harness.cjs");
const screens = require("../vendor/agentviz-kit/tests/screens.cjs");

const REPO = path.resolve(__dirname, "..");
const MEDIA = path.join(REPO, "docs/media");
const PORT = Number(process.env.OFFICE_TEST_PORT || 8911);
const READY = "window.__page && window.__page.polls > 0 && window.__office.agents.size > 0";
const only = new Set(process.argv.slice(2));
const want = (name) => only.size === 0 || only.has(name);
const DESKTOP = {viewport: {width: 1600, height: 900}};
const PHONE = {viewport: {width: 390, height: 844}, dpr: 3, mobile: true};

// Camera helpers run in the page: frame an agent, a pod or the door.
const HELPERS = `
window.__shots = {
  settle(seconds) { const o = window.__office; for (let i = 0; i < seconds * 10; i++) o.step((o.lastT || 0) + 0.1); },
  cam(c) { Object.assign(window.__page.kit.cam, c); },
  agent(id, c) { const o = window.__office, w = o.where(o.agents.get(id)); this.cam(Object.assign({tx: w.x, ty: 0.8, tz: w.z - 1.2}, c)); },
  pod(id, c) { const p = window.__office.pods.get(id).cur;
    this.cam(Object.assign({tx: (p.x0 + p.x1) / 2, ty: 0.5, tz: (p.z0 + p.z1) / 2 + 0.5}, c)); },
  draw() { window.__page.kit.drive(window.__office.lastT || 0); },
  // A real tap on an agent (the kit's tap path), as a finger would.
  tap(id) { this.draw(); const k = window.__page.kit, o = window.__office, w = o.where(o.agents.get(id));
    const q = k.project(w.x, w.y - 0.45, w.z), r = k.canvas.getBoundingClientRect(); k.tapAt(q.x + r.left, q.y + r.top); },
};`;

async function openPage(browser, server, night, options) {
  const opened = await h.open(browser, `${server.base}/?fixture=1&drive=1&night=${night}`, Object.assign({ready: READY}, options));
  await h.stillAnimations(opened.page);
  await opened.page.evaluate(() => window.__page.stopPolling());
  await opened.page.evaluate(HELPERS);
  await opened.page.evaluate(() => window.__shots.settle(8));
  return opened;
}

async function shoot(page, name, setup) {
  if (!want(name)) return;
  await page.evaluate(setup);
  await screens.still(page, path.join(MEDIA, name + ".png"), () => window.__shots.draw());
}

const DESKTOP_SHOTS = [
  ["hero", "window.__page.fit(true)"],
  ["angle-west", "window.__page.fit(true); window.__shots.cam({yaw: Math.PI / 2 + 0.75, pitch: 0.62, dist: window.__page.kit.cam.dist * 0.92})"],
  ["angle-overhead", "window.__page.fit(true); window.__shots.cam({yaw: Math.PI / 2, pitch: 1.32, dist: window.__page.kit.cam.dist * 1.2})"],
  ["lead-pod", "window.__shots.pod('plugin-site', {yaw: Math.PI / 2 - 0.25, pitch: 0.8, dist: 14, tz: window.__office.pods.get('plugin-site').cur.z0 + 3.5})"],
  ["delegate", "window.__shots.pod('site-one', {yaw: Math.PI / 2 + 0.35, pitch: 0.7, dist: 14})"],
  ["waiting", "window.__shots.agent('demo-waiting', {yaw: Math.PI / 2 - 0.45, pitch: 0.55, dist: 11})"],
  ["question", "window.__shots.agent('demo-asker', {ty: 1.6, yaw: Math.PI / 2 + 0.25, pitch: 0.4, dist: 7})"],
  ["idle-offline", "window.__shots.pod('~', {yaw: Math.PI / 2 - 0.2, pitch: 0.72, dist: 9})"],
  ["stalled", "window.__shots.agent('demo-stalled', {yaw: Math.PI / 2 - 0.3, pitch: 0.55, dist: 6.5})"],
  ["resting", "window.__shots.agent('demo-lead-a/demo-a5', {yaw: Math.PI / 2 + 0.3, pitch: 0.62, dist: 7.5})"],
  ["panel-desktop", "window.__page.fit(true); window.__page.select('demo-lead-b', false)"],
];

async function desktopStills(browser, server) {
  const {ctx, page, errors} = await openPage(browser, server, 0, DESKTOP);
  for (const [name, setup] of DESKTOP_SHOTS) await shoot(page, name, setup);
  h.assertNoErrors(errors);
  await ctx.close();
}

async function single(browser, server, name, night, options, setup) {
  if (!want(name)) return;
  const {ctx, page, errors} = await openPage(browser, server, night, options);
  await shoot(page, name, setup);
  h.assertNoErrors(errors);
  await ctx.close();
}

// The GIF: the demo script, a new snapshot every second, stepped at 10 frames a second.
async function demoGif(browser, server) {
  if (!want("demo")) return;
  const {ctx, page, errors} = await openPage(browser, server, 0, {viewport: {width: 880, height: 500}});
  await page.evaluate(async () => {
    const base = await (await fetch("/api/agents?fixture=1")).json();
    window.__feedDemo = (k) => window.__page.feed(window.__page.demoMutate(JSON.parse(JSON.stringify(base)), k));
    window.__feedDemo(0);
    window.__shots.settle(6);
    window.__shots.cam({tx: -3, ty: 0.5, tz: 15, yaw: Math.PI / 2 - 0.3, pitch: 0.82, dist: 27});
  });
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "office-gif-"));
  const FPS = 10;
  await screens.frames(page, tmp, FPS * 6, (i) => {
    if (i % 10 === 0) window.__feedDemo(4 + i / 10);
    const o = window.__office;
    o.step((o.lastT || 0) + 0.1);
    window.__shots.draw();
  });
  screens.gif(tmp, path.join(MEDIA, "demo.gif"), FPS);
  fs.rmSync(tmp, {recursive: true, force: true});
  h.assertNoErrors(errors);
  await ctx.close();
}

h.run("screenshots", {repo: REPO, port: PORT}, async ({server, browser}) => {
  fs.mkdirSync(MEDIA, {recursive: true});
  await desktopStills(browser, server);
  await single(browser, server, "night", 1, DESKTOP, "window.__page.fit(true); window.__shots.cam({yaw: Math.PI / 2 - 0.7, pitch: 0.72})");
  await single(browser, server, "phone", 0, PHONE,
    "window.__page.fit(true); window.__shots.tap('demo-lead-a')");
  await demoGif(browser, server);
});
