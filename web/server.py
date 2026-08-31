#!/usr/bin/env python3
"""Local web front end for the flightnet CLI.

    python web/server.py            # then open http://localhost:8000

The browser talks to this server; this server runs the real C++ binary with
--json and passes its output straight through. There is no second
implementation of the algorithms in JavaScript, so what the page shows is
exactly what the compiled engine computed.

Standard library only - no pip install required.
"""

import json
import os
import re
import subprocess
import sys
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEB_DIR = os.path.join(ROOT, "web")

# The binary, whichever suffix this platform produced.
BINARY = None
for candidate in ("build/flightnet.exe", "build/flightnet"):
    path = os.path.join(ROOT, candidate)
    if os.path.isfile(path):
        BINARY = path
        break

# Only these subcommands may be invoked from the browser.
ALLOWED_COMMANDS = {"info", "route", "flow", "resilience"}

# Every accepted query parameter, with a validator. Anything not listed here is
# dropped rather than forwarded, so a crafted URL cannot smuggle arguments into
# the command line. Values are passed via a list to subprocess (never a shell),
# so there is no quoting or injection surface either way.
CODE_RE = re.compile(r"^[A-Za-z0-9_#-]{1,12}$")
NUM_RE = re.compile(r"^-?\d+(\.\d+)?$")

# Datasets the browser is allowed to name, mapped to their directory. Keeping
# this a whitelist means the `--data` path can never be steered at an arbitrary
# location on disk.
DATASETS = {
    "sample": None,                 # the binary's built-in network
    "data": "data",
    "synthetic-file": "data/synthetic",
}


VALID = {
    "from":       lambda v: bool(CODE_RE.match(v)),
    "to":         lambda v: bool(CODE_RE.match(v)),
    "mode":       lambda v: v in ("stops", "cost", "time", "balanced", "all"),
    "algo":       lambda v: v in ("ff", "ek", "dinic", "all"),
    "alternates": lambda v: bool(NUM_RE.match(v)),
    "top":        lambda v: bool(NUM_RE.match(v)),
    "airports":   lambda v: bool(NUM_RE.match(v)),
    "hubs":       lambda v: bool(NUM_RE.match(v)),
    "seed":       lambda v: bool(NUM_RE.match(v)),
    "w-cost":     lambda v: bool(NUM_RE.match(v)),
    "w-time":     lambda v: bool(NUM_RE.match(v)),
    "w-leg":      lambda v: bool(NUM_RE.match(v)),
    "mesh":       lambda v: bool(NUM_RE.match(v)),
    "spoke":      lambda v: bool(NUM_RE.match(v)),
}

FLAG_PARAMS = {"no-airport-caps", "synthetic"}


def build_argv(command, params):
    """Turn validated query parameters into an argv list for the binary."""
    argv = [BINARY, command, "--json"]

    dataset = params.get("dataset", ["sample"])[0]
    if dataset not in DATASETS:
        raise ValueError("unknown dataset %r" % dataset)

    if dataset == "sample":
        pass                                    # binary default
    elif dataset.startswith("synthetic"):
        argv.append("--synthetic")
    else:
        directory = DATASETS[dataset]
        full = os.path.join(ROOT, directory)
        if not os.path.isdir(full):
            raise ValueError("dataset directory not found: %s" % directory)
        argv += ["--data", full]

    if params.get("generate", ["0"])[0] == "1":
        argv.append("--synthetic")

    for key, values in params.items():
        if key in ("dataset", "generate"):
            continue
        raw = values[0]
        if key in FLAG_PARAMS:
            if raw in ("1", "true", "on"):
                argv.append("--" + key)
            continue
        if key not in VALID:
            continue                            # unknown key: ignore entirely
        # A known parameter with a bad value is rejected rather than dropped.
        # Dropping it would silently answer a different question than the one
        # asked -- e.g. a mangled "from" would fall back to the first airport.
        if not VALID[key](raw):
            raise ValueError("invalid value for %s: %r" % (key, raw))
        argv += ["--" + key, raw]

    return argv


def run_engine(command, params):
    argv = build_argv(command, params)
    try:
        proc = subprocess.run(
            argv,
            capture_output=True,
            text=True,
            timeout=30,
            cwd=ROOT,
        )
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": "engine timed out after 30s"}, argv

    out = proc.stdout.strip()
    if not out:
        detail = proc.stderr.strip() or ("exit code %d" % proc.returncode)
        return {"ok": False, "error": "engine produced no output (%s)" % detail}, argv

    try:
        return json.loads(out), argv
    except json.JSONDecodeError as exc:
        # Surface the raw text; it is far more useful than "parse failed".
        return {"ok": False, "error": "engine output was not JSON: %s" % exc,
                "raw": out[:2000]}, argv


class Handler(BaseHTTPRequestHandler):
    # Quieter than the default, which logs every asset fetch.
    def log_message(self, fmt, *args):
        if "/api/" in (self.path or ""):
            sys.stderr.write("  %s\n" % (fmt % args))

    def _send(self, code, body, content_type):
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _send_json(self, code, payload):
        self._send(code, json.dumps(payload), "application/json; charset=utf-8")

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/":
            path = "/index.html"

        if path.startswith("/api/"):
            return self._handle_api(path[len("/api/"):], parse_qs(parsed.query))

        # Static files, restricted to web/ so nothing outside it can be read.
        name = os.path.normpath(path.lstrip("/")).replace("\\", "/")
        if name.startswith("..") or os.path.isabs(name):
            return self._send(403, "forbidden", "text/plain")

        full = os.path.join(WEB_DIR, name)
        if not os.path.isfile(full):
            return self._send(404, "not found: %s" % name, "text/plain")

        types = {".html": "text/html; charset=utf-8",
                 ".css": "text/css; charset=utf-8",
                 ".js": "application/javascript; charset=utf-8",
                 ".svg": "image/svg+xml"}
        ctype = types.get(os.path.splitext(full)[1], "application/octet-stream")
        with open(full, "rb") as fh:
            return self._send(200, fh.read(), ctype)

    def _handle_api(self, command, params):
        if BINARY is None:
            return self._send_json(500, {
                "ok": False,
                "error": "flightnet binary not found - run 'make' first",
            })
        if command not in ALLOWED_COMMANDS:
            return self._send_json(404, {"ok": False,
                                         "error": "unknown command %r" % command})
        try:
            payload, argv = run_engine(command, params)
        except ValueError as exc:
            return self._send_json(400, {"ok": False, "error": str(exc)})

        # Echo the exact command line so the page can show what it ran. This is
        # the whole point of wrapping the CLI rather than reimplementing it.
        # Paths are shown relative to the project root to keep it readable.
        shown = [os.path.basename(argv[0])]
        for a in argv[1:]:
            if os.path.isabs(a) and a.startswith(ROOT):
                a = os.path.relpath(a, ROOT).replace("\\", "/")
            shown.append(a)
        payload["_command"] = " ".join(shown)
        code = 200 if payload.get("ok") else 400
        return self._send_json(code, payload)


def main():
    if BINARY is None:
        print("error: build/flightnet(.exe) not found.")
        print("build it first:  make        (or: mingw32-make)")
        return 1

    port = int(os.environ.get("PORT", "8000"))
    if len(sys.argv) > 1:
        port = int(sys.argv[1])

    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    url = "http://localhost:%d" % port
    print("flightnet web UI")
    print("  engine : %s" % os.path.relpath(BINARY, ROOT))
    print("  serving: %s" % url)
    print("  stop   : Ctrl+C")

    if os.environ.get("NO_BROWSER") != "1":
        threading.Timer(0.5, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
