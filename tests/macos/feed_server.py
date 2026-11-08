from __future__ import annotations

import http.server
import threading
import time
from pathlib import Path


class FeedServer:
    """Pre-bound loopback HTTP server with a request log for release tests."""

    def __init__(self, serve_dir: Path) -> None:
        self._lock = threading.Lock()
        self._log_path = serve_dir.parent / "feed-http.log"
        self._log = open(self._log_path, "w")
        feed = self

        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self, format: str, *args: object) -> None:
                with feed._lock:
                    feed._log.write((format % args) + "\n")
                    feed._log.flush()

        self._server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0),
            lambda *args, **kwargs: Handler(
                *args, directory=str(serve_dir), **kwargs))
        self.port = self._server.server_address[1]
        self._thread = threading.Thread(target=self._server.serve_forever, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._server.shutdown()
        self._server.server_close()
        self._thread.join(timeout=10)
        self._log.close()

    def requests(self) -> str:
        with self._lock:
            self._log.flush()
            return self._log_path.read_text(encoding="utf-8", errors="ignore")

    def wait_for_feed_request(self, channel: str = "osx-arm64",
                              timeout_s: float = 60.0,
                              poll_s: float = 2.0) -> None:
        deadline = time.monotonic() + timeout_s
        needle = f"GET /releases.{channel}.json"
        while time.monotonic() < deadline:
            if needle in self.requests():
                return
            time.sleep(poll_s)
        raise AssertionError(
            f"packed app never requested {needle} from the loopback feed "
            f"within {timeout_s}s.\n--- HTTP request log ---\n{self.requests() or '(empty)'}")
