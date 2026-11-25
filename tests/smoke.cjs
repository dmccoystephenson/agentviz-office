// Headless smoke test on the demo feed (vendor/agentviz-kit/tests/harness.cjs):
// every fixture agent is in the scene; the "needs you" chip cycles the waiting
// sessions; a tap on a waiting session at the owner's door opens its panel,
// which says why it waits; a stalled subagent has the red monitor; feed
// strings never become HTML; the empty state and the unreachable banner
// show; the frame is not flat; no console errors.
//   npm ci && npx playwright install chromium && npm run smoke
const path = require("path");
const h = require("../vendor/agentviz-kit/tests/harness.cjs");

const REPO = path.resolve(__dirname, "..");
const PORT = Number(process.env.OFFICE_TEST_PORT || 8911);
const READY = "window.__page && window.__page.polls > 0 && window.__office.agents.size > 0";

function check(cond, message) { if (!cond) throw new Error(message); }

async function checkScene(page) {
  const info = await page.evaluate(() => {
    const o = window.__office, doc = window.__page.doc, agents = [...o.agents.values()];
    return {agents: agents.length, leads: agents.filter((a) => a.lead).length, pods: o.pods.size,
      want: {agents: doc.sessions.length + doc.sessions.reduce((n, s) => n + s.followers.length, 0),
        leads: doc.sessions.length, pods: new Set(doc.sessions.map((s) => s.place)).size},
      waiting: doc.sessions.filter((s) => s.state === "waiting").map((s) => s.name)};
  });
  console.log("scene:", JSON.stringify(info));
  check(info.agents === info.want.agents && info.leads === info.want.leads && info.pods === info.want.pods,
    "the scene does not hold every fixture agent");
  check(info.waiting.length === 2, "the fixture should have two sessions waiting");
  return info;
}

const panelTitle = (page) => page.evaluate(() => {
  const panel = document.querySelector(".av-panel");
  return panel.hidden ? null : panel.querySelector("h2").textContent.trim();
});

async function checkWaitingChip(page, names) {
  const chip = page.locator(".av-wait");
  check(await chip.isVisible(), "the needs-you chip is hidden");
  check((await chip.locator("b").textContent()) === "2", "the needs-you chip does not count 2");
  const seen = [];
  for (let i = 0; i < 2; i++) { await chip.click(); seen.push(await panelTitle(page)); }
  console.log("needs-you chip cycles:", seen.join(" -> "));
  check(seen.slice().sort().join() === names.slice().sort().join(), "the chip did not cycle both waiting sessions");
  check(await page.locator(".av-panel .av-needs").isVisible(), "the panel does not say why it waits");
}

async function checkTapAtDoor(page) {
  await page.evaluate(() => { window.__page.select(null); const o = window.__office; for (let i = 0; i < 80; i++) o.step(o.lastT + 0.1); });
  const hit = await page.evaluate(() => {
    const k = window.__page.kit, o = window.__office, a = o.agents.get("demo-waiting");
    const w = o.where(a), q = k.project(w.x, w.y - 0.45, w.z), r = k.canvas.getBoundingClientRect();
    return q && {x: q.x + r.left, y: q.y + r.top, loc: a.loc};
  });
  check(hit && hit.loc === "door", "the waiting session is not at the owner's door");
  await page.mouse.click(hit.x, hit.y);
  await page.waitForTimeout(300);
  const title = await panelTitle(page);
  const reason = await page.evaluate(() => document.querySelector(".av-panel .av-needs").textContent);
  console.log("tap at the door:", title, "/", reason);
  check(title === "Thistle" && reason === "needs permission", "the tap did not open Thistle's panel with its reason");
}

async function checkFollowers(page) {
  const screens = await page.evaluate(() => {
    const o = window.__office, S = window.Office.SCREEN;
    return {stalled: o.agents.get("demo-lead-b/demo-b7").screen === S.stalled,
      fine: o.agents.get("demo-lead-b/demo-b6").screen !== S.stalled};
  });
  check(screens.stalled && screens.fine, "a stalled subagent should have the red monitor, others not");
  await page.evaluate(() => window.__page.select("demo-lead-b/demo-b6", false));
  const branch = await page.evaluate(() => [...document.querySelectorAll(".av-panel dd")].map((d) => d.textContent));
  check(branch.includes("fix/endpoint-hardening"), "the subagent panel does not show its own branch");
}

async function checkTextOnly(page) {
  const result = await page.evaluate(() => {
    const doc = JSON.parse(JSON.stringify(window.__page.doc));
    doc.sessions.find((s) => s.id === "demo-lead-b").followers[0].label = "<img src=x id=pwned>";
    window.__page.feed(doc);
    window.__page.select("demo-lead-b", false);
    return {img: !!document.getElementById("pwned"),
      text: [...document.querySelectorAll(".av-panel .av-lanes button")].some((b) => b.textContent === "<img src=x id=pwned>")};
  });
  check(!result.img && result.text, "a feed label was parsed as HTML");
}

async function checkEmptyState(page) {
  const shown = await page.evaluate(() => {
    const doc = JSON.parse(JSON.stringify(window.__page.doc));
    const sessions = doc.sessions;
    window.__page.feed(Object.assign(doc, {sessions: []}));
    const visible = !document.querySelector(".av-empty").hidden;
    window.__page.feed(Object.assign(doc, {sessions}));
    return visible && document.querySelector(".av-empty").hidden;
  });
  check(shown, "no empty-state hint with zero sessions");
}

async function checkUnreachable(page) {
  await page.route("**/api/agents*", (route) => route.fulfill({status: 503, body: "down"}));
  await page.evaluate(async () => { await window.__page.pollOnce(); await window.__page.pollOnce(); });
  const banner = await page.evaluate(() => !document.querySelector(".av-banner").hidden && document.querySelector(".av-banner").textContent);
  console.log("banner:", banner);
  check(banner && banner.includes("HTTP 503"), "no unreachable banner after failed polls");
}

h.run("smoke", {repo: REPO, port: PORT}, async ({server, browser}) => {
  const {page, errors} = await h.open(browser, server.base + "/?fixture=1&night=0&quality=1", {ready: READY});
  await h.stillAnimations(page);
  await page.waitForFunction(() => window.__page.kit.stats().frames > 3, null, {timeout: 400000});
  await page.evaluate(() => window.__page.stopPolling());
  const info = await checkScene(page);
  await checkWaitingChip(page, info.waiting);
  await checkTapAtDoor(page);
  await checkFollowers(page);
  await checkTextOnly(page);
  await checkEmptyState(page);
  await page.evaluate(() => window.__page.select(null));
  await page.mouse.move(2, 400);  // no hover outline in the saved frame
  await page.waitForTimeout(300);
  const png = await h.shot(page, path.join(REPO, "docs/media/screenshot.png"));
  console.log("distinct colours:", await h.assertNotFlat(page, png));
  h.assertNoErrors(errors);
  await checkUnreachable(page);  // last: the failed fetches log console errors of their own
});
