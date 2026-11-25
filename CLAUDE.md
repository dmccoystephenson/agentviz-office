# CLAUDE.md

agentviz-office: Claude Code sessions drawn as an open-plan office floor
(Strata web kit). Part of the agentviz family: https://github.com/dmccoystephenson/agentviz

- Data comes only from `/api/agents` (vendor/agentfeed, schema in the agentviz repo's docs/FEED.md).
  Never edit `vendor/agentfeed/`; re-vendor with `/root/agentviz/tools/vendor-feed.sh .`.
- `scene/main.cpp` draws; `web/office.js` lays out, eases and walks people; `web/index.html` is the page.
- Build: `scene/build.sh` (emsdk 6.0.11, Strata 0.10.0). Built `web/office-engine.*` are committed.
- Run: `python3 -m vendor.agentfeed serve --web web --port 8841 [--fixture]`.
- Tests: `python3 -m unittest discover -s tests`; smoke + screenshots:
  `NODE_PATH=/root/arcade-watch/node_modules node tests/smoke.cjs` / `tests/screenshots.cjs`.
- Screenshots come from the FIXTURE feed only. Port 8841 (tests use 8941).
- PRIVATE repo; hosted Actions are billing-blocked, run localci on PRs.
