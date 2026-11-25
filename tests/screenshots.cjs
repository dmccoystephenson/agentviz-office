// Regenerates every README image in docs/media/ from the FIXTURE feed:
//   NODE_PATH=/root/arcade-watch/node_modules node tests/screenshots.cjs [name ...]
// Stills are drawn with ?drive=1 (the kit draws one frame on demand), so they
// repeat. The GIF steps scene time frame by frame through the ?demo=1 script
// (the fixture with scripted changes) and is encoded with Pillow.
const {chromium} = require("playwright");
const fs = require("fs");
const os = require("os");
const path = require("path");
const {execFileSync} = require("child_process");
const {startServer, ready, LAUNCH} = require("./serve.cjs");

const MEDIA = path.resolve(__dirname, "../docs/media");
const STILL = "{const s=document.createElement('style');s.textContent='*{animation:none!important;transition:none!important}';document.head.appendChild(s)}";
const only = new Set(process.argv.slice(2));
const want = (n) => only.size === 0 || only.has(n);

// Camera helpers run in the page: frame an agent, a pod or the door.
const HELPERS = `
window.__shots = {
  settle(seconds) { const o = window.__office; for (let i = 0; i < seconds * 10; i++) o.step((o.lastT || 0) + 0.1); },
  at(id) { const o = window.__office, a = o.agents.get(id); return o.where(a); },
  cam(c) { Object.assign(window.__page.kit.cam, c); },
  agent(id, c) { const w = this.at(id); this.cam(Object.assign({tx: w.x, ty: 0.8, tz: w.z - 1.2}, c)); },
  pod(id, c) { const p = window.__office.pods.get(id).cur;
    this.cam(Object.assign({tx: (p.x0 + p.x1) / 2, ty: 0.5, tz: (p.z0 + p.z1) / 2 + 0.5}, c)); },
  draw() { const k = window.__page.kit, t = (window.__office.lastT || 0); k.drive(t); },
};`;

async function openPage(browser, base, query, ctxOpts) {
  const ctx = await browser.newContext(ctxOpts);
  const page = await ctx.newPage();
  const errors = [];
  page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
  page.on("pageerror", (e) => errors.push(String(e)));
  await page.goto(base + "/" + query, {waitUntil: "commit"});
  await ready(page);
  await page.evaluate(STILL);
  await page.evaluate(() => window.__page.stopPolling());
  await page.evaluate(HELPERS);
  await page.evaluate(() => window.__shots.settle(8));
  page.errors = errors;
  return {ctx, page};
}

async function shoot(page, name, setup) {
  if (!want(name)) return;
  await page.evaluate(setup);
  // Twice: labels that newly fit are shown on the next placement 200 ms on.
  await page.evaluate(() => window.__shots.draw());
  await page.waitForTimeout(400);
  await page.evaluate(() => window.__shots.draw());
  await page.waitForTimeout(400);
  await page.evaluate(() => window.__shots.draw());
  await page.waitForTimeout(200);
  await page.screenshot({path: path.join(MEDIA, name + ".png"), timeout: 400000});
  console.log("saved", name + ".png");
}

(async () => {
  fs.mkdirSync(MEDIA, {recursive: true});
  const srv = await startServer();
  const browser = await chromium.launch(LAUNCH);
  let failed = false;
  try {
    // Desktop stills.
    {
      const {ctx, page} = await openPage(browser, srv.base, "?fixture=1&drive=1&night=0", {viewport: {width: 1600, height: 900}});
      await shoot(page, "hero", "window.__page.fit(true)");
      await shoot(page, "angle-west", "window.__page.fit(true); window.__shots.cam({yaw: Math.PI / 2 + 0.75, pitch: 0.62, dist: window.__page.kit.cam.dist * 0.92})");
      await shoot(page, "angle-overhead", "window.__page.fit(true); window.__shots.cam({yaw: Math.PI / 2, pitch: 1.32, dist: window.__page.kit.cam.dist * 1.2})");
      await shoot(page, "lead-pod", "window.__shots.pod('plugin-site', {yaw: Math.PI / 2 - 0.25, pitch: 0.8, dist: 14, tz: window.__office.pods.get('plugin-site').cur.z0 + 3.5})");
      await shoot(page, "delegate", "window.__shots.pod('site-one', {yaw: Math.PI / 2 + 0.35, pitch: 0.7, dist: 14})");
      await shoot(page, "waiting", "window.__shots.agent('demo-waiting', {yaw: Math.PI / 2 - 0.45, pitch: 0.55, dist: 11})");
      await shoot(page, "idle-offline", "window.__shots.pod('~', {yaw: Math.PI / 2 - 0.2, pitch: 0.72, dist: 9})");
      await shoot(page, "stalled", "window.__shots.agent('demo-thinker', {yaw: Math.PI / 2 - 0.3, pitch: 0.55, dist: 6.5})");
      await shoot(page, "resting", "window.__shots.agent('demo-lead-a/demo-a5', {yaw: Math.PI / 2 + 0.3, pitch: 0.62, dist: 7.5})");
      await shoot(page, "panel-desktop", "window.__page.fit(true); window.__page.select('demo-lead-b', false)");
      await page.evaluate(() => window.__page.select(null));
      if (page.errors.length) throw new Error("console errors:\n" + page.errors.join("\n"));
      await ctx.close();
    }
    // Night, by the clock pinned to night.
    if (want("night")) {
      const {ctx, page} = await openPage(browser, srv.base, "?fixture=1&drive=1&night=1", {viewport: {width: 1600, height: 900}});
      await shoot(page, "night", "window.__page.fit(true); window.__shots.cam({yaw: Math.PI / 2 - 0.7, pitch: 0.72})");
      await ctx.close();
    }
    // Phone portrait with the bottom sheet open.
    if (want("phone")) {
      const {ctx, page} = await openPage(browser, srv.base, "?fixture=1&drive=1&night=0",
        {viewport: {width: 390, height: 844}, deviceScaleFactor: 3, isMobile: true, hasTouch: true});
      await shoot(page, "phone", "window.__page.fit(true); window.__shots.cam({tz: window.__page.kit.cam.tz + 5}); window.__page.select('demo-lead-a', false)");
      await ctx.close();
    }
    // The GIF: the demo script, stepped at 10 frames a second.
    if (want("demo")) {
      const {ctx, page} = await openPage(browser, srv.base, "?fixture=1&drive=1&night=0", {viewport: {width: 880, height: 500}});
      const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "office-gif-"));
      await page.evaluate(async () => {
        const r = await fetch("/api/agents?fixture=1");
        window.__base = await r.json();
        window.__feedDemo = (k) => window.__page.feed(window.__page.demoMutate(JSON.parse(JSON.stringify(window.__base)), k));
        window.__feedDemo(0);
        window.__shots.settle(6);
        window.__shots.cam({tx: -3, ty: 0.5, tz: 15, yaw: Math.PI / 2 - 0.3, pitch: 0.82, dist: 27});
      });
      const FPS = 10, SECONDS = 6;
      for (let i = 0; i < FPS * SECONDS; i++) {
        await page.evaluate(([i, FPS]) => {
          const t = i / FPS;
          if (i % 10 === 0) window.__feedDemo(4 + i / 10);  // a new snapshot every second
          const o = window.__office;
          o.step((o.lastT || 0) + 1 / FPS);
          window.__shots.draw();
        }, [i, FPS]);
        await page.screenshot({path: path.join(tmp, String(i).padStart(3, "0") + ".png"), timeout: 400000});
      }
      execFileSync("python3", [path.join(__dirname, "make_gif.py"), tmp, path.join(MEDIA, "demo.gif"), String(FPS)], {stdio: "inherit"});
      fs.rmSync(tmp, {recursive: true, force: true});
      console.log("saved demo.gif");
      await ctx.close();
    }
  } catch (e) {
    console.error("screenshots: FAIL", e.message);
    failed = true;
  } finally {
    await browser.close();
    srv.stop();
  }
  process.exit(failed ? 1 : 0);
})();
