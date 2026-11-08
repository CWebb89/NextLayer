#!/usr/bin/env python3
"""A stand-in Moonraker: answer /server/info and accept an upload.

The point is to exercise the engine's own PrintHostMoonraker without a printer
on the network, and to prove the bytes that arrive are the G-code we sent.
"""

import json
import sys
import threading
from http.server import BaseHTTPRequestHandler, HTTPServer

RECEIVED = []


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *args):
        pass

    def _json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path.startswith("/server/info"):
            self._json({"result": {"klippy_state": "ready", "moonraker_version": "v0.8.0",
                                   "components": ["klippy_apis"]}})
            return
        if self.path.startswith("/printer/info"):
            self._json({"result": {"state": "ready"}})
            return
        self._json({"error": "not found"}, 404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        if self.path.startswith("/server/files/upload"):
            RECEIVED.append(body)
            self._json({"result": {"item": {"path": "upload.gcode"}, "action": "create"}},
                       status=201)
            return
        if self.path.startswith("/printer/print/start"):
            self._json({"result": "ok"})
            return
        self._json({"error": "not found"}, 404)


def main():
    port = int(sys.argv[1])
    server = HTTPServer(("127.0.0.1", port), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print("ready", flush=True)

    import os
    import subprocess

    binary, gcode = sys.argv[2], sys.argv[3]
    # The preset home comes from the caller's environment: this runs the same
    # binary the app does, and a test must neither read nor write the connection
    # stored beside a person's presets.
    result = subprocess.run([binary, "--print-host-test", f"http://127.0.0.1:{port}/,,,{gcode}"],
                            capture_output=True, text=True, env=os.environ.copy())
    server.shutdown()

    for line in result.stdout.splitlines():
        if line.startswith("print host: "):
            print(line)
    if result.returncode != 0:
        print(result.stderr[-400:])
        return 1

    if not RECEIVED:
        print("print host: the server received no upload")
        return 1
    body = RECEIVED[0]
    marker = b"; next-layer calibration marker"
    if marker not in body:
        print(f"print host: the upload is {len(body)} bytes but does not contain the G-code "
              f"marker")
        return 1
    print(f"print host: uploaded {len(body)} bytes, marker present")
    return 0


if __name__ == "__main__":
    sys.exit(main())
