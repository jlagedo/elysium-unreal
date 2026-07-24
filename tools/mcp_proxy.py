#!/usr/bin/env python3
"""Reconnecting stdio<->HTTP bridge for the in-game Elysium MCP server.

Claude Code talks to the Elysium MCP server (the `elysium_*` tools) over HTTP at
`http://127.0.0.1:8000/mcp`. That server lives *inside* the running UE process, so its
lifetime equals the game's: it is down until the game boots, and every rebuild -> relaunch
kills and recreates it. Claude Code auto-reconnects a dropped HTTP MCP server, but only 5
times over ~31s (hardcoded backoff) before marking it `failed` and waiting for a manual
`/mcp` reconnect. A UE C++ rebuild + relaunch takes minutes, so that window always expires
-> the agent hand-reconnects after every build.

This proxy removes the manual step. Claude Code launches it once as a `stdio` server and
keeps it alive for the whole session (stdio children are never torn down for downstream
failures). The proxy forwards MCP traffic to the in-game HTTP endpoint, connecting *lazily*
and reconnecting on its own, so it does not care that the game died and came back three
minutes later:

  - game up   -> transparent passthrough; every `elysium_*` tool works
  - game down -> `initialize` still succeeds (Claude Code sees "connected"); `tools/list`
                 is served from an on-disk cache so the tools stay visible; `tools/call`
                 returns a clean "game not running" result instead of a dead session
  - game back -> the next call just works; a background poller emits
                 `notifications/tools/list_changed` so a changed tool set repopulates live

Pure stdlib (no `mcp` package): the proxy speaks newline-delimited JSON-RPC to Claude Code
over stdio and streamable-HTTP JSON-RPC to the game over `http.client` (the game answers a
tool call with an open, Content-Length-less SSE stream that `urllib` mis-reads as empty).

Config (all optional): `--url` / `$ELYSIUM_MCP_URL` (default http://127.0.0.1:8000/mcp),
`--poll` / `$ELYSIUM_MCP_POLL` seconds (default 2.0). Diagnostics go to stderr; stdout
carries only protocol messages.
"""
import argparse
import http.client
import json
import os
import socket
import sys
import threading
import time
from urllib.parse import urlsplit

# The protocol version we advertise to Claude Code when the real server is unreachable at
# `initialize` time. When the game is up we echo the client's requested version instead.
DEFAULT_PROTOCOL_VERSION = "2025-06-18"
PROXY_VERSION = "1.0"

# A loopback accept is sub-millisecond when the game is up, so this only ever elapses when the
# game is down — and on Windows a closed loopback port *times out* rather than refusing, so this
# short gate (not urllib's long read timeout) is what makes down-detection fast.
PORT_CHECK_TIMEOUT = 0.5

GAME_DOWN_TEXT = (
    "The Elysium game is not running, so its MCP tools are unavailable right now. "
    "Launch it (play.bat / editor.bat) and retry — this proxy reconnects on its own, "
    "with no /mcp reconnect needed."
)


def log(msg):
    sys.stderr.write("[elysium-mcp-proxy] %s\n" % msg)
    sys.stderr.flush()


class GameUnavailable(Exception):
    """The in-game server could not be reached (refused / timed out)."""


class _Reconnect(Exception):
    """The server answered but rejected the request (stale session) — reinitialize."""


class Downstream:
    """A lazily-connected streamable-HTTP MCP client to the in-game server.

    One MCP session at a time, guarded by a lock so the request path and the background
    poller never race on (re)connect. Any call transparently (re)initializes; a connection
    refusal surfaces as GameUnavailable, a mid-session HTTP rejection triggers one clean
    reinitialize-and-retry.
    """

    def __init__(self, url, timeout=30.0):
        self.url = url
        self.timeout = timeout
        parts = urlsplit(url)
        self.scheme = parts.scheme or "http"
        self.host = parts.hostname or "127.0.0.1"
        self.port = parts.port or (443 if self.scheme == "https" else 80)
        self.path = parts.path or "/"
        self._lock = threading.RLock()
        self._session_id = None
        self._ready = False
        self._client_protocol = DEFAULT_PROTOCOL_VERSION
        self.server_protocol = None  # the version the game negotiated (from its InitializeResult)

    def set_client_protocol(self, version):
        if version:
            self._client_protocol = version

    # -- public API ----------------------------------------------------------------

    def ensure(self):
        """Force a connection and return the game's negotiated protocol version.

        Raises GameUnavailable if the game cannot be reached. Used at `initialize` time so we
        report the game's real version to Claude Code (it may down-negotiate) rather than
        blindly echoing the client's request.
        """
        with self._lock:
            try:
                self._ensure_connected()
            except _Reconnect:
                self._reset()
                self._ensure_connected()
            return self.server_protocol

    def request(self, message):
        """Send a JSON-RPC request and return the parsed response dict.

        Raises GameUnavailable if the game cannot be reached (after one reconnect try).
        """
        with self._lock:
            try:
                return self._attempt(message)
            except _Reconnect:
                self._reset()
                try:
                    return self._attempt(message)
                except _Reconnect as exc:
                    raise GameUnavailable("server rejected request after reconnect: %s" % exc)

    def notify(self, message):
        """Forward a JSON-RPC notification best-effort (no response expected)."""
        with self._lock:
            try:
                self._ensure_connected()
                self._post(message, expect_response=False)
            except _Reconnect:
                self._reset()

    def fetch_tools(self):
        """Return the server's current tools/list result, or raise GameUnavailable."""
        resp = self.request({"jsonrpc": "2.0", "id": "proxy-tools", "method": "tools/list",
                             "params": {}})
        result = (resp or {}).get("result")
        if result is None:
            raise GameUnavailable("tools/list returned no result")
        return result

    def probe(self):
        """Cheap liveness check: can we open the TCP port? Drops a stale session if not."""
        if self._port_open(PORT_CHECK_TIMEOUT):
            return True
        self._reset()
        return False

    # -- internals -----------------------------------------------------------------

    def _reset(self):
        self._ready = False
        self._session_id = None

    def _attempt(self, message):
        self._ensure_connected()
        return self._post(message, expect_response=True)

    def _port_open(self, timeout):
        try:
            with socket.create_connection((self.host, self.port), timeout=timeout):
                return True
        except OSError:
            return False

    def _ensure_connected(self):
        # Always gate on a fast socket check: cheap when the game is up (a loopback accept is
        # instant) and bounded when it is down, and it drops a stale session the instant the
        # port closes so a dead epoch never costs urllib's long connect timeout.
        if not self._port_open(PORT_CHECK_TIMEOUT):
            self._reset()
            raise GameUnavailable("port %d not open" % self.port)
        if self._ready:
            return
        # A fresh server epoch: initialize, capture any session id, then confirm.
        init = {
            "jsonrpc": "2.0",
            "id": "proxy-init",
            "method": "initialize",
            "params": {
                "protocolVersion": self._client_protocol,
                "capabilities": {},
                "clientInfo": {"name": "elysium-mcp-proxy", "version": PROXY_VERSION},
            },
        }
        resp = self._post(init, expect_response=True)
        if isinstance(resp, dict):
            self.server_protocol = (resp.get("result") or {}).get("protocolVersion")
        self._post({"jsonrpc": "2.0", "method": "notifications/initialized"},
                   expect_response=False)
        self._ready = True

    def _open(self):
        if self.scheme == "https":
            return http.client.HTTPSConnection(self.host, self.port, timeout=self.timeout)
        return http.client.HTTPConnection(self.host, self.port, timeout=self.timeout)

    def _post(self, message, expect_response):
        # http.client, not urllib: the game answers a tool call with a `text/event-stream`
        # body that has no Content-Length and is left open, and urllib mis-reads that as an
        # empty body. http.client streams it correctly.
        body = json.dumps(message).encode("utf-8")
        headers = {
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
        }
        if self._session_id:
            headers["Mcp-Session-Id"] = self._session_id
        try:
            conn = self._open()
            conn.request("POST", self.path, body=body, headers=headers)
            resp = conn.getresponse()
        except (OSError, http.client.HTTPException) as exc:
            raise GameUnavailable(str(exc))
        try:
            sid = resp.getheader("Mcp-Session-Id")
            if sid:
                self._session_id = sid
            if resp.status >= 400:
                # Up but rejected us (commonly a stale session after a relaunch) — drop the
                # session and let request() reinitialize once.
                raise _Reconnect("HTTP %d" % resp.status)
            if not expect_response:
                return None
            ctype = resp.getheader("Content-Type", "") or ""
            if "text/event-stream" in ctype:
                return self._read_sse_response(resp, message.get("id"))
            data = resp.read()
            return json.loads(data.decode("utf-8", "replace")) if data else None
        except (socket.timeout, TimeoutError, OSError, http.client.HTTPException) as exc:
            raise GameUnavailable(str(exc))
        finally:
            conn.close()

    def _read_sse_response(self, resp, want_id):
        """Return the JSON-RPC message with id == want_id from an open SSE response, or None.

        The tool-call stream carries the response as one `data:` event terminated by a blank
        line and then stays open — so read line by line and stop the instant the matching id
        arrives, rather than draining a stream the server never closes.
        """
        deadline = time.monotonic() + self.timeout
        data_parts = []
        while time.monotonic() < deadline:
            raw = resp.readline()
            if not raw:
                break  # server closed the stream
            line = raw.rstrip(b"\r\n")
            if line == b"":  # blank line terminates an SSE event
                msg = _parse_sse_data(data_parts)
                data_parts = []
                if msg is not None and msg.get("id") == want_id:
                    return msg
            elif line.startswith(b"data:"):
                data_parts.append(line[5:].lstrip())
            # 'event:', 'id:', and ':' comment lines are ignored
        msg = _parse_sse_data(data_parts)  # a trailing event with no closing blank line
        return msg if msg is not None and msg.get("id") == want_id else None


def _parse_sse_data(data_parts):
    """Join an SSE event's `data:` byte lines and parse them as a JSON object, or None."""
    if not data_parts:
        return None
    try:
        val = json.loads(b"".join(data_parts).decode("utf-8", "replace"))
    except json.JSONDecodeError:
        return None
    return val if isinstance(val, dict) else None


class ToolsCache:
    """On-disk cache of the last good tools/list result, so the tools stay visible in
    Claude Code even when the game is down (including a cold start with no game running)."""

    def __init__(self, path):
        self.path = path

    def load(self):
        try:
            with open(self.path, "r", encoding="utf-8") as fh:
                return json.load(fh)
        except (OSError, json.JSONDecodeError):
            return None

    def save(self, result):
        try:
            os.makedirs(os.path.dirname(self.path), exist_ok=True)
            with open(self.path, "w", encoding="utf-8") as fh:
                json.dump(result, fh)
        except OSError as exc:
            log("tools cache write failed: %s" % exc)


class Proxy:
    def __init__(self, downstream, cache, poll_interval):
        self.down = downstream
        self.cache = cache
        self.poll_interval = poll_interval
        self._out_lock = threading.Lock()
        self._stop = threading.Event()
        self._client_ready = False          # seen the client's initialize yet?
        self._last_tools_sig = None         # tool-name signature we last surfaced

    # -- stdout (one JSON message per line, no embedded newlines, utf-8) -------------

    def _send(self, message):
        line = (json.dumps(message) + "\n").encode("utf-8")
        with self._out_lock:
            sys.stdout.buffer.write(line)
            sys.stdout.buffer.flush()

    def _send_result(self, mid, result):
        self._send({"jsonrpc": "2.0", "id": mid, "result": result})

    def _send_error(self, mid, code, text):
        self._send({"jsonrpc": "2.0", "id": mid, "error": {"code": code, "message": text}})

    @staticmethod
    def _tools_sig(result):
        return tuple(sorted(t.get("name", "") for t in (result or {}).get("tools", [])))

    # -- dispatch -------------------------------------------------------------------

    def handle(self, message):
        method = message.get("method")
        mid = message.get("id")
        is_request = method is not None and mid is not None

        if method == "initialize":
            self._handle_initialize(message)
        elif method == "notifications/initialized":
            pass  # consumed; our own downstream handshake is separate
        elif method == "ping" and is_request:
            self._send_result(mid, {})
        elif method == "tools/list" and is_request:
            self._handle_tools_list(mid)
        elif method == "tools/call" and is_request:
            self._handle_tools_call(message)
        elif is_request:
            self._handle_generic(message)
        elif method:
            # An unknown notification from the client — forward best-effort.
            try:
                self.down.notify(message)
            except GameUnavailable:
                pass
        # else: a response/unknown with no method — nothing for us to do.

    def _handle_initialize(self, message):
        mid = message.get("id")
        params = message.get("params", {}) or {}
        client_proto = params.get("protocolVersion", DEFAULT_PROTOCOL_VERSION)
        self.down.set_client_protocol(client_proto)
        # Report the game's negotiated version when it is reachable now; otherwise echo the
        # client's request and let the real negotiation happen transparently on first use.
        try:
            proto = self.down.ensure() or client_proto
        except GameUnavailable:
            proto = client_proto
        self._send_result(mid, {
            "protocolVersion": proto,
            "capabilities": {"tools": {"listChanged": True}},
            "serverInfo": {"name": "elysium (reconnecting proxy)", "version": PROXY_VERSION},
        })
        self._client_ready = True

    def _handle_tools_list(self, mid):
        try:
            result = self.down.fetch_tools()
            self.cache.save(result)
            self._last_tools_sig = self._tools_sig(result)
            self._send_result(mid, result)
        except GameUnavailable:
            self._send_result(mid, self.cache.load() or {"tools": []})

    def _handle_tools_call(self, message):
        mid = message.get("id")
        try:
            resp = self.down.request(message)  # forwarded verbatim; id already matches
            if resp is None:
                self._send_error(mid, -32603, "empty response from game server")
            else:
                self._send(resp)
        except GameUnavailable:
            self._send_result(mid, {
                "isError": True,
                "content": [{"type": "text", "text": GAME_DOWN_TEXT}],
            })

    def _handle_generic(self, message):
        mid = message.get("id")
        try:
            resp = self.down.request(message)
            if resp is None:
                self._send_error(mid, -32603, "empty response from game server")
            else:
                self._send(resp)
        except GameUnavailable:
            self._send_error(mid, -32000, GAME_DOWN_TEXT)

    # -- background reconnect / list_changed ---------------------------------------

    def poll_loop(self):
        was_up = False
        while not self._stop.wait(self.poll_interval):
            up = self.down.probe()
            if up and not was_up and self._client_ready:
                # down -> up transition: refresh tools and, if they changed, tell Claude
                # Code so a new/removed elysium_* tool repopulates without a reconnect.
                try:
                    result = self.down.fetch_tools()
                    self.cache.save(result)
                    sig = self._tools_sig(result)
                    if sig != self._last_tools_sig:
                        self._last_tools_sig = sig
                        self._send({"jsonrpc": "2.0",
                                    "method": "notifications/tools/list_changed"})
                        log("game up — %d tools (list_changed sent)" % len(sig))
                    else:
                        log("game up — %d tools (unchanged)" % len(sig))
                except GameUnavailable:
                    up = False  # port opened but MCP not ready yet; retry next tick
            elif not up and was_up:
                log("game down — will reconnect on demand")
            was_up = up

    def run(self):
        poller = threading.Thread(target=self.poll_loop, name="poller", daemon=True)
        poller.start()
        log("ready — bridging stdio <-> %s" % self.down.url)
        try:
            for raw in sys.stdin.buffer:  # one JSON-RPC message per line
                line = raw.decode("utf-8", "replace").strip()
                if not line:
                    continue
                try:
                    message = json.loads(line)
                except json.JSONDecodeError as exc:
                    log("dropping malformed line: %s" % exc)
                    continue
                try:
                    self.handle(message)
                except Exception as exc:  # never let one message kill the bridge
                    log("handler error: %s" % exc)
                    mid = message.get("id")
                    if mid is not None:
                        self._send_error(mid, -32603, "proxy error: %s" % exc)
        finally:
            self._stop.set()
        log("stdin closed — exiting")


def main():
    parser = argparse.ArgumentParser(description="Reconnecting stdio<->HTTP MCP proxy for "
                                                 "the in-game Elysium server.")
    parser.add_argument("--url", default=os.environ.get("ELYSIUM_MCP_URL",
                                                        "http://127.0.0.1:8000/mcp"),
                        help="the in-game MCP endpoint (default http://127.0.0.1:8000/mcp)")
    parser.add_argument("--poll", type=float,
                        default=float(os.environ.get("ELYSIUM_MCP_POLL", "2.0")),
                        help="seconds between game liveness checks (default 2.0)")
    here = os.path.dirname(os.path.abspath(__file__))
    parser.add_argument("--cache",
                        default=os.environ.get("ELYSIUM_MCP_CACHE",
                                               os.path.join(here, "out", "_mcp",
                                                            "tools_cache.json")),
                        help="tool-list cache path (default tools/out/_mcp/tools_cache.json); "
                             "set ELYSIUM_MCP_CACHE to relocate, e.g. so tests don't touch it")
    args = parser.parse_args()

    cache_path = args.cache

    downstream = Downstream(args.url)
    proxy = Proxy(downstream, ToolsCache(cache_path), args.poll)
    proxy.run()


if __name__ == "__main__":
    main()
