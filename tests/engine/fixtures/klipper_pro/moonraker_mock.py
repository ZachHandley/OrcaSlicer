#!/usr/bin/env python3
# Phase 6.5 — Moonraker HTTP mock used by tests/engine/test_klipper_pro_e2e.cpp.
#
# Stdlib only (no aiohttp / flask deps) so the test runs on any CI worker that
# already builds OrcaSlicer (Python 3 is a deps/ requirement). Binds to
# 127.0.0.1:0, prints `PORT=<n>\n` on its first line of stdout, then flushes
# so the parent can capture the port. Any number of subsequent log lines may
# follow on stderr for debugging; stdout stays single-line.
#
# Endpoints mirrored from engine/plugins/klipper-pro/src/moonraker.rs:
#   GET  /printer/info
#   GET  /printer/objects/query?<list>
#   POST /printer/gcode/script              body {script}
#   POST /server/files/upload?root=gcodes   multipart/form-data
#   POST /printer/print/start?filename=<n>
#   POST /printer/print/cancel
#
# Test introspection (NOT real Moonraker):
#   GET  /__test/requests   -> JSON array of every request the mock observed,
#                              with method/path/header subset/body length /
#                              parsed body (for upload + gcode/script). The
#                              test reads this to assert the sequence.
#   POST /__test/shutdown   -> gracefully exits the process. The test calls
#                              this in its teardown.

from __future__ import annotations

import json
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# Shared mutable state. Tests run one mock per test case (parent spawns +
# joins around each), so a module-level list keeps the wiring trivial.
_request_log: list[dict] = []
_log_lock = threading.Lock()


def _record(method: str, path: str, headers, body: bytes) -> None:
    """Append a sanitized record of one HTTP request to the test log."""
    parsed_body: object = None
    text: str | None = None
    try:
        if body:
            text = body.decode("utf-8", errors="replace")
    except Exception:
        text = None

    # Multipart upload — pull out the filename + payload size, drop the raw
    # binary so /__test/requests stays human-readable.
    content_type = headers.get("Content-Type", "")
    if "multipart/form-data" in content_type and body:
        filename = None
        try:
            head = body.split(b"\r\n\r\n", 1)[0].decode("utf-8", errors="replace")
            for line in head.splitlines():
                if "filename=" in line:
                    filename = line.split('filename="', 1)[1].split('"', 1)[0]
                    break
        except Exception:
            pass
        parsed_body = {
            "multipart": True,
            "filename":  filename,
            "size":      len(body),
        }
    elif text and content_type.startswith("application/json"):
        try:
            parsed_body = json.loads(text)
        except Exception:
            parsed_body = {"raw": text}
    elif text:
        parsed_body = {"raw": text}

    with _log_lock:
        _request_log.append({
            "method":  method,
            "path":    path,
            "body":    parsed_body,
            "body_len": len(body) if body else 0,
            "headers": {
                "X-Api-Key":     headers.get("X-Api-Key"),
                "Content-Type":  headers.get("Content-Type"),
            },
        })


class MockHandler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args) -> None:  # noqa: N802 — stdlib signature
        # Redirect access log to stderr so stdout stays the PORT= channel.
        sys.stderr.write("[moonraker_mock] " + (fmt % args) + "\n")

    # ----- shared response helpers --------------------------------------

    def _send_json(self, status: int, payload: object) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_body(self) -> bytes:
        length = int(self.headers.get("Content-Length", "0") or "0")
        return self.rfile.read(length) if length > 0 else b""

    # ----- handlers ------------------------------------------------------

    def do_GET(self) -> None:  # noqa: N802 — stdlib signature
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        _record("GET", self.path, self.headers, b"")

        if path == "/__test/requests":
            with _log_lock:
                self._send_json(200, _request_log)
            return

        if path == "/printer/info":
            self._send_json(200, {"result": {
                "state":             "ready",
                "state_message":     "Klipper mock ready",
                "hostname":          "moonraker-mock",
                "software_version":  "v0.test",
            }})
            return

        if path == "/printer/objects/query":
            self._send_json(200, {"result": {"status": {
                "heater_bed":  {"temperature": 25.0, "target": 0.0},
                "extruder":    {"temperature": 210.0, "target": 210.0},
                "print_stats": {
                    "state":          "printing",
                    "filename":       "mock-job.gcode",
                    "print_duration": 12.0,
                    "total_duration": 15.0,
                    "filament_used":  0.5,
                },
            }}})
            return

        self._send_json(404, {"error": "not found", "path": path})

    def do_POST(self) -> None:  # noqa: N802 — stdlib signature
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        body = self._read_body()
        _record("POST", self.path, self.headers, body)

        if path == "/__test/shutdown":
            self._send_json(200, {"result": "ok"})
            # Schedule a clean exit after the response flushes.
            threading.Thread(
                target=lambda: self.server.shutdown(),
                daemon=True,
            ).start()
            return

        if path == "/printer/gcode/script":
            self._send_json(200, {"result": "ok"})
            return

        if path == "/server/files/upload":
            # Echo back the filename we plucked out of the multipart body.
            filename = "unknown.gcode"
            try:
                head = body.split(b"\r\n\r\n", 1)[0].decode("utf-8", errors="replace")
                for line in head.splitlines():
                    if "filename=" in line:
                        filename = line.split('filename="', 1)[1].split('"', 1)[0]
                        break
            except Exception:
                pass
            self._send_json(200, {"result": {"item": {"path": filename}}})
            return

        if path == "/printer/print/start":
            self._send_json(200, {"result": "ok"})
            return

        if path == "/printer/print/cancel":
            self._send_json(200, {"result": "ok"})
            return

        self._send_json(404, {"error": "not found", "path": path})


def main() -> int:
    server = ThreadingHTTPServer(("127.0.0.1", 0), MockHandler)
    port = server.server_address[1]

    # First line of stdout is the port; flush so the parent reads immediately.
    sys.stdout.write(f"PORT={port}\n")
    sys.stdout.flush()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
