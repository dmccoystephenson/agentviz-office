# CLAUDE.md

agentviz-office: Claude Code sessions drawn as an open-plan office floor
(Strata web kit). Part of the agentviz family: https://github.com/dmccoystephenson/agentviz

- Data comes only from `/api/agents` (vendor/agentfeed; schema in the agentviz repo's docs/FEED.md).
- Shared pieces come from the agentviz kit (docs/KIT.md there): `vendor/agentviz-kit/` and the
  `web/agentviz-*` copies. Never edit `vendor/` or `web/agentviz-*`; re-vendor from an agentviz
  checkout with `tools/vendor-kit.sh <this repo>`. `tests/test_serve.py` checks the PINs.
- Page: `web/office-model.js` lays out pods and desks and eases them; `web/office-walk.js` decides
  poses and walks people; `web/office-frame.js` packs each frame in the agentviz wire format;
  `web/page.js` boots the feed client, HUD, labels and camera; `web/index.html` holds the legend.
- Scene: `scene/main.cpp` decodes the wire (`agentviz.h`) and calls `room.h`, `desk.h`, `person.h`,
  `trails.h`. Build: `EMSDK=/path/to/emsdk vendor/agentviz-kit/scene/build-scene.sh scene/kit.conf`
  (emsdk 6.0.11, Strata 0.10.0). The built `web/office-engine.*` are committed.
- Run: `python3 -m vendor.agentfeed serve --web web --port 8841 [--fixture]`.
- Tests: `python3 -m unittest discover -s tests`; browser tests after `npm ci` and
  `npx playwright install chromium`: `npm run smoke`, `npm run screenshots` (Pillow from
  requirements-dev.txt for the GIF). Tests use port 8911 (`OFFICE_TEST_PORT` overrides).
- Screenshots come from the FIXTURE feed only, never live sessions.
