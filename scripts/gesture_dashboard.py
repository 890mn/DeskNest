"""Tiny local serial dashboard for DeskNest. Python stdlib + pyserial only."""

from __future__ import annotations

import argparse
import json
import re
import threading
import time
import webbrowser
from collections import deque
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


CHANNEL_RE = re.compile(r"^\[D\]\[([A-Z_]+)\]")
NUMBER_FIELDS = {"ax", "ay", "az", "mag", "base", "motion", "shake", "ret", "settle"}
INTEGER_FIELDS = {"t", "sample", "win", "cd", "inv"}


def classify_line(line: str) -> str:
    match = CHANNEL_RE.match(line.strip())
    if not match:
        return "other"
    return {
        "STATE": "state",
        "INPUT": "input",
        "GESTURE": "gesture",
        "SENS": "sensor",
        "TELEM": "telemetry",
        "HELP": "help",
        "SHOW": "show",
    }.get(match.group(1), "other")


def parse_telemetry(line: str) -> dict:
    if classify_line(line) != "telemetry":
        return {}
    result = {}
    for key, value in re.findall(r"(\w+)=([^\s]+)", line):
        try:
            if key in NUMBER_FIELDS:
                result[key] = float(value)
            elif key in INTEGER_FIELDS:
                result[key] = int(value)
            else:
                result[key] = value
        except ValueError:
            result[key] = value
    return result


class RepeatedLog:
    def __init__(self, limit: int = 80):
        self.limit = limit
        self.items = deque(maxlen=limit)

    def add(self, text: str) -> None:
        if self.items and self.items[-1]["text"] == text:
            self.items[-1]["count"] += 1
            self.items[-1]["time"] = time.strftime("%H:%M:%S")
            return
        self.items.append({"text": text, "count": 1, "time": time.strftime("%H:%M:%S")})


class DashboardState:
    def __init__(self):
        self.lock = threading.Lock()
        self.logs = {name: RepeatedLog() for name in ("state", "input", "gesture", "sensor", "other")}
        self.telemetry = {}
        self.help_lines = []
        self.show_lines = []
        self.last_phase = None
        self.connected = False
        self.serial_port = ""
        self.version = 0

    def add_line(self, line: str) -> None:
        line = line.strip()
        if not line:
            return
        channel = classify_line(line)
        with self.lock:
            if channel == "telemetry":
                parsed = parse_telemetry(line)
                if parsed:
                    phase = parsed.get("phase")
                    if self.last_phase and phase and phase != self.last_phase:
                        transition = f"PHASE {self.last_phase} -> {phase}"
                        if self.last_phase == "OUTBOUND" and phase == "IDLE":
                            transition += " OUTBOUND_TIMEOUT"
                        self.logs["gesture"].add(transition)
                    if phase:
                        self.last_phase = phase
                    self.telemetry = parsed
            elif channel == "help":
                self.help_lines = self._tagged_lines(self.help_lines, line, "[D][HELP]")
            elif channel == "show":
                self.show_lines = self._tagged_lines(self.show_lines, line, "[D][SHOW]")
            else:
                self.logs[channel if channel in self.logs else "other"].add(line)
            self.version += 1

    @staticmethod
    def _tagged_lines(current, line, tag):
        text = line[len(tag):].strip()
        if text == "BEGIN":
            return []
        if text == "END":
            return current
        return (current + [text])[-80:]

    def snapshot(self) -> dict:
        with self.lock:
            return {
                "connected": self.connected,
                "serial_port": self.serial_port,
                "telemetry": dict(self.telemetry),
                "help": list(self.help_lines),
                "show": list(self.show_lines),
                "logs": {key: list(value.items) for key, value in self.logs.items()},
                "version": self.version,
            }


class SerialBridge(threading.Thread):
    def __init__(self, state: DashboardState, port_name: str = ""):
        super().__init__(daemon=True)
        self.state = state
        self.port_name = port_name
        self.serial = None
        self.pending = deque()
        self.stop_event = threading.Event()

    def send(self, command: str) -> None:
        self.pending.append(command.rstrip("\r\n"))

    def _find_port(self):
        from serial.tools import list_ports
        ports = list(list_ports.comports())
        if self.port_name:
            return self.port_name
        preferred = [p.device for p in ports if "303A:1001" in (p.hwid or "").upper()]
        return preferred[0] if preferred else (ports[0].device if ports else "")

    def run(self):
        import serial
        while not self.stop_event.is_set():
            port = self._find_port()
            if not port:
                time.sleep(1)
                continue
            try:
                self.serial = serial.Serial(port, 115200, timeout=0.1)
                with self.state.lock:
                    self.state.connected = True
                    self.state.serial_port = port
                    self.state.version += 1
                started = time.monotonic()
                initialized = False
                while not self.stop_event.is_set() and self.serial.is_open:
                    raw = self.serial.readline()
                    if raw:
                        line = raw.decode("utf-8", errors="replace").strip()
                        self.state.add_line(line)
                        if "[D][BOOT] done" in line:
                            started = 0
                    if not initialized and (started == 0 or time.monotonic() - started > 13):
                        for cmd in ("help", "show", "record"):
                            self.pending.append(cmd)
                        initialized = True
                    while self.pending:
                        self.serial.write((self.pending.popleft() + "\n").encode("utf-8"))
            except (OSError, serial.SerialException):
                time.sleep(1)
            finally:
                if self.serial:
                    try:
                        self.serial.close()
                    except Exception:
                        pass
                with self.state.lock:
                    self.state.connected = False
                    self.state.version += 1


def make_handler(state: DashboardState, bridge: SerialBridge | None, html_path: Path):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *_args):
            return

        def _json(self, payload, status=HTTPStatus.OK):
            body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = urlparse(self.path).path
            if path == "/":
                body = html_path.read_bytes()
                self.send_response(HTTPStatus.OK)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            elif path == "/api/snapshot":
                self._json(state.snapshot())
            else:
                self.send_error(HTTPStatus.NOT_FOUND)

        def do_POST(self):
            if urlparse(self.path).path != "/api/command":
                self.send_error(HTTPStatus.NOT_FOUND)
                return
            length = int(self.headers.get("Content-Length", "0"))
            try:
                payload = json.loads(self.rfile.read(length) or b"{}")
                command = str(payload.get("command", "")).strip()
            except (ValueError, UnicodeDecodeError):
                self._json({"ok": False, "error": "invalid json"}, HTTPStatus.BAD_REQUEST)
                return
            if not command or len(command) > 95 or "\n" in command or "\r" in command:
                self._json({"ok": False, "error": "invalid command"}, HTTPStatus.BAD_REQUEST)
                return
            if bridge is None:
                self._json({"ok": False, "error": "serial disabled"}, HTTPStatus.SERVICE_UNAVAILABLE)
                return
            bridge.send(command)
            self._json({"ok": True})

    return Handler


def main():
    parser = argparse.ArgumentParser(description="DeskNest local gesture dashboard")
    parser.add_argument("--serial", default="", help="serial port, auto-detected by default")
    parser.add_argument("--port", type=int, default=8765, help="local HTTP port")
    parser.add_argument("--no-serial", action="store_true", help="UI preview without a device")
    parser.add_argument("--open", action="store_true", help="open the browser")
    args = parser.parse_args()

    state = DashboardState()
    bridge = None if args.no_serial else SerialBridge(state, args.serial)
    if bridge:
        bridge.start()
    html_path = Path(__file__).with_name("gesture_dashboard.html")
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(state, bridge, html_path))
    url = f"http://127.0.0.1:{args.port}"
    print(f"DeskNest dashboard: {url}")
    if args.open:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if bridge:
            bridge.stop_event.set()
        server.server_close()


if __name__ == "__main__":
    main()
