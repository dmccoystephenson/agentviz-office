"""Serves the fixture feed next to web/ and checks the page, its assets and
the feed. Stdlib only: python3 -m unittest discover -s tests"""
import json
import os
import socket
import subprocess
import sys
import time
import unittest
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PORT = int(os.environ.get("OFFICE_TEST_PORT", "8941"))
BASE = "http://127.0.0.1:%d" % PORT


def get(path):
    with urllib.request.urlopen(BASE + path, timeout=10) as r:
        return r.status, r.headers.get("Content-Type", ""), r.read()


class ServeFixtureTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proc = subprocess.Popen(
            [sys.executable, "-m", "vendor.agentfeed", "serve", "--web", "web", "--port", str(PORT), "--fixture"],
            cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                socket.create_connection(("127.0.0.1", PORT), timeout=0.5).close()
                return
            except OSError:
                time.sleep(0.1)
        cls.proc.kill()
        raise RuntimeError("server did not start")

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=10)

    def test_page_and_assets(self):
        status, ctype, body = get("/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ctype)
        html = body.decode()
        for asset in ("strata-loader.js", "strata-kit.js", "office.js", "page.js"):
            self.assertIn(asset, html)
        for asset in ("strata-loader.js", "strata-kit.js", "office.js", "page.js", "office-engine.js"):
            status, ctype, body = get("/" + asset)
            self.assertEqual(status, 200, asset)
            self.assertGreater(len(body), 1000, asset)
        status, ctype, body = get("/office-engine.wasm")
        self.assertEqual(status, 200)
        self.assertEqual(ctype, "application/wasm")
        self.assertEqual(body[:4], b"\0asm")

    def test_engine_exports_the_scene_setter(self):
        _, _, body = get("/page.js")
        self.assertIn("createOfficeEngine", body.decode())
        _, _, body = get("/office.js")
        self.assertIn("_of_frame", body.decode())

    def test_feed_parses(self):
        status, ctype, body = get("/api/agents")
        self.assertEqual(status, 200)
        doc = json.loads(body)
        self.assertEqual(doc["version"], 1)
        self.assertTrue(doc["fixture"])
        names = {s["name"]: s for s in doc["sessions"]}
        self.assertEqual(names["Thistle"]["state"], "waiting")
        self.assertEqual(len(names["Wren"]["followers"]), 5)
        self.assertEqual(len(names["Otto"]["followers"]), 6)
        for s in doc["sessions"]:
            self.assertIn(s["state"], ("offline", "dim", "idle", "busy", "waiting"))
            self.assertIn(s["action"], ("edit", "run", "read", "web", "delegate", "think", "other", "none"))
            for f in s["followers"]:
                self.assertIn(f["state"], ("active", "resting"))

    def test_no_escape_from_web(self):
        with self.assertRaises(urllib.error.HTTPError) as e:
            get("/../vendor/agentfeed/feed.py")
        self.assertEqual(e.exception.code, 404)


if __name__ == "__main__":
    unittest.main()
