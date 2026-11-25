// Shared by the browser tests: starts the fixture server on port 8941
// (8841 + 100), returns {base, stop}; the child is killed by its own handle.
const {spawn} = require("child_process");
const path = require("path");
const http = require("http");

const PORT = Number(process.env.OFFICE_TEST_PORT || 8941);
const LAUNCH = {args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist"]};

function up(url) {
  return new Promise((resolve) => {
    http.get(url, (r) => { r.resume(); resolve(r.statusCode === 200); }).on("error", () => resolve(false));
  });
}

async function startServer() {
  const repo = path.resolve(__dirname, "..");
  const child = spawn("python3", ["-m", "vendor.agentfeed", "serve", "--web", "web", "--port", String(PORT), "--fixture"],
    {cwd: repo, stdio: "ignore"});
  const base = `http://127.0.0.1:${PORT}`;
  for (let i = 0; i < 100; i++) {
    if (await up(base + "/api/agents")) break;
    await new Promise((r) => setTimeout(r, 100));
  }
  return {base, stop: () => child.kill()};
}

// Waits until the page has its first snapshot and the kit has drawn.
async function ready(page) {
  await page.waitForFunction(() => window.__page && window.__page.polls > 0 && window.__page.kit.stats &&
    window.__office.agents.size > 0, null, {timeout: 400000});
}

module.exports = {startServer, ready, LAUNCH, PORT};
