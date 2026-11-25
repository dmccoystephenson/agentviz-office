// Headless smoke test: loads /?fixture=1, waits for the kit to draw, checks
// the scene holds every fixture agent, that the frame is not one flat colour,
// that a tap on the waiting session selects it and opens the sheet, and that
// there are no console errors. Saves docs/media/screenshot.png.
//   NODE_PATH=/root/arcade-watch/node_modules node tests/smoke.cjs
// (any node_modules with playwright 1.61 works; Chromium runs SwiftShader).
const {chromium} = require("playwright");
const path = require("path");
const {startServer, ready, LAUNCH} = require("./serve.cjs");

// CSS animations (the pulsing "needs you" chip) keep headless SwiftShader
// compositing forever, which stalls screenshots; real browsers are fine.
const STILL = "{const s=document.createElement('style');s.textContent='*{animation:none!important;transition:none!important}';document.head.appendChild(s)}";

(async () => {
  const srv = await startServer();
  const browser = await chromium.launch(LAUNCH);
  const errors = [];
  let ok = false;
  try {
    const page = await browser.newPage({viewport: {width: 1280, height: 760}});
    page.on("console", (m) => { if (m.type() === "error") errors.push(m.text()); });
    page.on("pageerror", (e) => errors.push(String(e)));
    await page.goto(srv.base + "/?fixture=1&night=0&quality=1", {waitUntil: "commit"});
    await ready(page);
    await page.evaluate(STILL);
    await page.waitForFunction(() => window.__page.kit.stats().frames > 3, null, {timeout: 400000});
    await page.evaluate(() => window.__page.stopPolling());
    const info = await page.evaluate(() => {
      const o = window.__office;
      const agents = [...o.agents.values()];
      return {agents: agents.length, leads: agents.filter((a) => a.lead).length, pods: o.pods.size,
        waitChip: document.getElementById("nWait").textContent, hidden: document.getElementById("waitChip").hidden,
        stats: window.__page.kit.stats()};
    });
    console.log("scene:", JSON.stringify(info));
    if (info.agents !== 19 || info.leads !== 8 || info.pods !== 5) throw new Error("unexpected scene " + JSON.stringify(info));
    if (info.waitChip !== "1" || info.hidden) throw new Error("waiting chip not shown");
    // Let the waiting session reach the owner's door, then tap it through a real click.
    await page.evaluate(() => { const o = window.__office; for (let i = 0; i < 80; i++) o.step(o.lastT + 0.1); });
    const hit = await page.evaluate(() => {
      const k = window.__page.kit, o = window.__office, a = o.agents.get("demo-waiting");
      const w = o.where(a), q = k.project(w.x, w.y - 0.45, w.z), r = k.canvas.getBoundingClientRect();
      return q && {x: q.x + r.left, y: q.y + r.top, pose: a.pose, loc: a.loc};
    });
    console.log("waiting agent:", JSON.stringify(hit));
    if (!hit || hit.loc !== "door") throw new Error("waiting session is not at the owner's door");
    const picked = await page.evaluate(([x, y]) => { const a = window.__page.kit.pickAt(x, y); return a && a.id; }, [hit.x, hit.y]);
    if (picked !== "demo-waiting") throw new Error("pick at the waiting session returned " + picked);
    await page.mouse.click(hit.x, hit.y);
    await page.waitForTimeout(300);
    const sheet = await page.evaluate(() => !document.getElementById("sheet").hidden && document.querySelector("#sheet h2").textContent.trim());
    console.log("sheet:", sheet);
    if (sheet !== "Thistle") throw new Error("tap did not open Thistle's sheet: " + sheet);
    await page.evaluate(() => window.__page.select(null));
    await page.mouse.move(2, 400);  // no hover outline in the saved frame
    await page.waitForTimeout(300);
    const shot = await page.screenshot({path: path.resolve(__dirname, "../docs/media/screenshot.png"), timeout: 400000});
    const colours = await page.evaluate(async (b64) => {
      const img = new Image(); img.src = "data:image/png;base64," + b64; await img.decode();
      const c = document.createElement("canvas"); c.width = img.width; c.height = img.height;
      const g = c.getContext("2d"); g.drawImage(img, 0, 0);
      const d = g.getImageData(0, 0, c.width, c.height).data, seen = new Set();
      for (let i = 0; i < d.length; i += 4 * 97) seen.add((d[i] >> 4) * 256 + (d[i + 1] >> 4) * 16 + (d[i + 2] >> 4));
      return seen.size;
    }, shot.toString("base64"));
    console.log("distinct colours:", colours);
    if (colours < 30) throw new Error("frame looks flat");
    if (errors.length) throw new Error("console errors:\n" + errors.join("\n"));
    ok = true;
    console.log("smoke: PASS");
  } catch (e) {
    console.error("smoke: FAIL", e.message);
  } finally {
    await browser.close();
    srv.stop();
  }
  process.exit(ok ? 0 : 1);
})();
