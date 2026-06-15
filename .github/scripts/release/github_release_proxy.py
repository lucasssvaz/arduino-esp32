#!/usr/bin/env python3
"""Serve package JSON locally and stream draft release assets via the GitHub API."""

from __future__ import annotations

import argparse
import http.server
import json
import mimetypes
import os
import socketserver
import sys
import urllib.error
import urllib.request


class ReleaseProxyHandler(http.server.BaseHTTPRequestHandler):
    static_dir: str
    asset_ids: dict[str, int]
    repo: str
    token: str

    def log_message(self, fmt: str, *args) -> None:
        pass

    def do_GET(self) -> None:
        name = self.path.split("?", 1)[0].lstrip("/")
        if not name or ".." in name or name.startswith("/"):
            self.send_error(400, "bad path")
            return

        local_path = os.path.join(self.static_dir, name)
        if os.path.isfile(local_path):
            self._serve_file(local_path)
            return

        asset_id = self.asset_ids.get(name)
        if asset_id is not None:
            self._proxy_github_asset(asset_id, name)
            return

        self.send_error(404, f"not found: {name}")

    def _serve_file(self, path: str) -> None:
        with open(path, "rb") as fh:
            data = fh.read()
        content_type = mimetypes.guess_type(path)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def _proxy_github_asset(self, asset_id: int, name: str) -> None:
        url = f"https://api.github.com/repos/{self.repo}/releases/assets/{asset_id}"
        req = urllib.request.Request(
            url,
            headers={
                "Authorization": f"Bearer {self.token}",
                "Accept": "application/octet-stream",
                "User-Agent": "arduino-esp32-release-test-proxy",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=600) as resp:
                self.send_response(resp.status)
                for header in ("Content-Type", "Content-Length"):
                    value = resp.headers.get(header)
                    if value:
                        self.send_header(header, value)
                self.end_headers()
                while True:
                    chunk = resp.read(1024 * 64)
                    if not chunk:
                        break
                    self.wfile.write(chunk)
        except urllib.error.HTTPError as exc:
            body = exc.read(256).decode("utf-8", errors="replace")
            sys.stderr.write(f"[release-proxy] GitHub API {exc.code} for {name}: {body}\n")
            self.send_error(exc.code, f"github asset proxy failed for {name}")


def load_asset_ids(path: str) -> dict[str, int]:
    with open(path, encoding="utf-8") as fh:
        data = json.load(fh)
    mapping: dict[str, int] = {}
    for name, entry in (data.get("assets") or {}).items():
        if isinstance(entry, dict) and entry.get("id") is not None:
            mapping[name] = int(entry["id"])
    if not mapping:
        raise SystemExit(f"ERROR: no asset ids in {path}")
    return mapping


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dir", required=True, help="Directory with package JSON files")
    parser.add_argument("--assets", required=True, help="draft-assets.json path")
    parser.add_argument("--repo", required=True, help="owner/repo")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--pid-file", required=True, help="Write bound port number here")
    args = parser.parse_args()

    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        raise SystemExit("ERROR: GITHUB_TOKEN required")

    static_dir = os.path.abspath(args.dir)
    asset_ids = load_asset_ids(args.assets)

    handler = ReleaseProxyHandler
    handler.static_dir = static_dir
    handler.asset_ids = asset_ids
    handler.repo = args.repo
    handler.token = token

    with socketserver.TCPServer(("127.0.0.1", args.port), handler) as httpd:
        port = httpd.server_address[1]
        with open(args.pid_file, "w", encoding="utf-8") as fh:
            fh.write(str(port))
        sys.stderr.write(f"[release-proxy] listening on http://127.0.0.1:{port}\n")
        httpd.serve_forever()


if __name__ == "__main__":
    main()
