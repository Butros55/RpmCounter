#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import queue
import socket
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from flask import Flask, jsonify, redirect, render_template_string, request
import serial
from serial import SerialException
from serial.tools import list_ports


@dataclass
class SimHubState:
    reachable: bool = False
    live: bool = False
    error: str = ""
    rpm: int = 0
    speed: int = 0
    gear: int = 0
    throttle: float = 0.0
    pit_limiter: bool = False
    maxrpm: int = 0
    last_packet_ts: float = 0.0


@dataclass
class EspState:
    port: str = ""
    serial_connected: bool = False
    bridge_connected: bool = False
    usb_state: str = "USB getrennt"
    active_telemetry: str = "Keine"
    usb_host: str = ""
    wifi_suspended: bool = False
    obd_allowed: bool = True
    telemetry_preference: str = "AUTO"
    sim_transport: str = "AUTO"
    display_focus: str = "RPM"
    rpm: int = 0
    speed: int = 0
    gear: int = 0
    throttle: float = 0.0
    pit_limiter: bool = False
    maxrpm: int = 0
    error: str = ""
    raw: dict[str, Any] = field(default_factory=dict)


class UsbSimBridge:
    def __init__(self, serial_port: str | None, baudrate: int, simhub_host: str, simhub_port: int, debug: bool) -> None:
        self.serial_port_arg = serial_port
        self.baudrate = baudrate
        self.simhub_host = simhub_host
        self.simhub_port = simhub_port
        self.debug = debug
        self.hostname = socket.gethostname()
        self.web_bind_host = "0.0.0.0"
        self.web_public_host = self._detect_public_host()
        self.stop_event = threading.Event()
        self.state_lock = threading.Lock()
        self.tx_lock = threading.Lock()
        self.rpc_lock = threading.Lock()
        self.serial: serial.Serial | None = None
        self.rx_buffer = ""
        self.rpc_counter = 0
        self.rpc_waiters: dict[int, queue.Queue[dict[str, Any]]] = {}
        self.last_hello_monotonic = 0.0
        self.last_status_ok_monotonic = 0.0
        self.serial_open_monotonic = 0.0
        self.simhub = SimHubState()
        self.esp = EspState()
        self.config_cache: dict[str, Any] = {}

    def log(self, text: str) -> None:
        if self.debug:
            print(f"[USB-BRIDGE] {text}", flush=True)

    @staticmethod
    def _normalize_throttle(value: float) -> float:
        if value > 1.0:
            value /= 100.0
        return max(0.0, min(1.0, value))

    @staticmethod
    def _friendly_serial_error(exc: Exception) -> str:
        text = str(exc)
        lower = text.lower()
        if "zugriff verweigert" in lower or "access is denied" in lower:
            return "COM-Port ist belegt. PlatformIO/Serial Monitor und zweite USB-Bridge schliessen."
        if "gerät erkennt den befehl nicht" in lower or "device does not recognize the command" in lower:
            return "USB-Link wurde vom ESP getrennt. Board neu verbinden und Serial Monitor schliessen."
        return text

    def _bridge_recently_seen(self, timeout: float = 8.0) -> bool:
        latest = max(self.last_hello_monotonic, self.last_status_ok_monotonic)
        return latest > 0.0 and (time.monotonic() - latest) <= timeout

    def start(self) -> None:
        threading.Thread(target=self._serial_loop, name="usb-serial", daemon=True).start()
        threading.Thread(target=self._simhub_loop, name="simhub-http", daemon=True).start()
        threading.Thread(target=self._status_loop, name="esp-status", daemon=True).start()

    def stop(self) -> None:
        self.stop_event.set()
        self._close_serial("bridge stopping")

    def _detect_port(self) -> str | None:
        if self.serial_port_arg:
            return self.serial_port_arg
        def score(port: list_ports.ListPortInfo) -> int:
            text = " ".join(filter(None, [port.device, port.description, port.manufacturer, port.product])).lower()
            points = 0
            if port.vid == 0x303A:
                points += 50
            if "esp32" in text or "espressif" in text:
                points += 25
            if "usb jtag" in text or "serial" in text or "cdc" in text:
                points += 10
            return points
        ports = sorted(list_ports.comports(), key=score, reverse=True)
        for port in ports:
            if score(port) > 0:
                return port.device
        return None

    def _detect_public_host(self) -> str:
        override = os.environ.get("SHIFTLIGHT_USB_PUBLIC_HOST", "").strip()
        if override:
            return override
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect(("8.8.8.8", 80))
            return probe.getsockname()[0]
        except OSError:
            return self.hostname
        finally:
            probe.close()

    def _open_serial(self) -> None:
        if self.serial and self.serial.is_open:
            return
        port = self._detect_port()
        if not port:
            with self.state_lock:
                self.esp.port = ""
                self.esp.serial_connected = False
                self.esp.bridge_connected = False
            return
        try:
            ser = serial.Serial(port=port, baudrate=self.baudrate, timeout=0.05, write_timeout=0.2)
            ser.dtr = False
            ser.rts = False
            time.sleep(0.9)
            ser.reset_input_buffer()
            ser.reset_output_buffer()
            self.serial = ser
            self.serial_open_monotonic = time.monotonic()
            with self.state_lock:
                self.esp.port = port
                self.esp.serial_connected = True
                self.esp.error = ""
            self.log(f"connected to {port}")
        except SerialException as exc:
            with self.state_lock:
                self.esp.port = port
                self.esp.serial_connected = False
                self.esp.error = self._friendly_serial_error(exc)

    def _close_serial(self, reason: str) -> None:
        if self.serial:
            try:
                self.serial.close()
            except SerialException:
                pass
        self.serial = None
        with self.state_lock:
            self.esp.serial_connected = False
            self.esp.bridge_connected = False
            self.esp.usb_state = "USB getrennt"
            self.esp.error = reason
        self.last_hello_monotonic = 0.0
        self.last_status_ok_monotonic = 0.0
        self.serial_open_monotonic = 0.0

    def _send_line(self, line: str) -> None:
        if not self.serial:
            raise SerialException("serial not open")
        with self.tx_lock:
            self.serial.write((line + "\n").encode("utf-8"))

    def _process_line(self, line: str) -> None:
        if not line:
            return
        if line.startswith("USBSIM RES "):
            tail = line[len("USBSIM RES "):]
            req_text, _, payload = tail.partition(" ")
            try:
                waiter = self.rpc_waiters.pop(int(req_text), None)
                data = json.loads(payload)
            except (ValueError, json.JSONDecodeError):
                return
            if waiter:
                waiter.put(data)
            return
        if line.startswith("USBSIM HELLO ") or line.startswith("USBSIM PONG "):
            payload = line.split(" ", 2)[2] if line.count(" ") >= 2 else ""
            fields = urllib.parse.parse_qs(payload, keep_blank_values=True)
            with self.state_lock:
                self.last_hello_monotonic = time.monotonic()
                self.esp.bridge_connected = True
                self.esp.serial_connected = True
                self.esp.error = ""
                if "state" in fields and fields["state"]:
                    self.esp.usb_state = fields["state"][0]
                if "device" in fields and fields["device"]:
                    self.esp.usb_host = fields["device"][0]
            return
        if not line.startswith("USBSIM "):
            self.log(f"log: {line}")

    def _rpc(self, command: str, timeout: float = 0.8) -> dict[str, Any]:
        if not self.serial:
            raise RuntimeError("ESP not connected")
        with self.rpc_lock:
            self.rpc_counter += 1
            request_id = self.rpc_counter
            waiter: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=1)
            self.rpc_waiters[request_id] = waiter
            try:
                self._send_line(f"USBSIM RPC {request_id} {command}")
                return waiter.get(timeout=timeout)
            except queue.Empty as exc:
                raise RuntimeError("ESP RPC timeout") from exc
            except (SerialException, OSError) as exc:
                message = self._friendly_serial_error(exc)
                self._close_serial(message)
                raise RuntimeError(message) from exc
            finally:
                self.rpc_waiters.pop(request_id, None)

    def _serial_loop(self) -> None:
        next_ping = 0.0
        next_tx = 0.0
        while not self.stop_event.is_set():
            if not self.serial:
                self._open_serial()
                time.sleep(0.35)
                continue
            try:
                count = self.serial.in_waiting or 0
                if count:
                    self.rx_buffer += self.serial.read(count).decode("utf-8", errors="ignore")
                    while "\n" in self.rx_buffer:
                        line, self.rx_buffer = self.rx_buffer.split("\n", 1)
                        self._process_line(line.strip())
                now = time.monotonic()
                if now >= next_ping:
                    ping = urllib.parse.urlencode({"host": self.web_public_host, "device": self.hostname, "web": 1})
                    self._send_line(f"USBSIM PING {ping}")
                    next_ping = now + 0.35
                if now >= next_tx and not self.rpc_lock.locked():
                    simhub = self.get_simhub()
                    if simhub.live:
                        payload = urllib.parse.urlencode({
                            "rpm": simhub.rpm,
                            "speed": simhub.speed,
                            "gear": simhub.gear,
                            "throttle": f"{simhub.throttle:.3f}",
                            "pit": 1 if simhub.pit_limiter else 0,
                            "maxrpm": simhub.maxrpm,
                        })
                        self._send_line(f"USBSIM TELEMETRY {payload}")
                    next_tx = now + 0.016
                time.sleep(0.01)
            except (SerialException, OSError) as exc:
                self.log(f"serial error: {exc}")
                self._close_serial(self._friendly_serial_error(exc))
                time.sleep(0.5)

    def _fetch_text(self, path: str, timeout: float = 0.35) -> str:
        url = f"http://{self.simhub_host}:{self.simhub_port}{path}"
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.read().decode("utf-8", errors="replace")

    def _simhub_loop(self) -> None:
        while not self.stop_event.is_set():
            next_state = SimHubState()
            try:
                game_data = json.loads(self._fetch_text("/Api/GetGameData"))
                next_state.reachable = True
                if bool(game_data.get("GameRunning")) and game_data.get("NewData") not in (None, "null"):
                    simple = json.loads(self._fetch_text("/Api/GetGameDataSimple"))
                    gear_raw = simple.get("gear", 0)
                    next_state.rpm = int(simple.get("rpms", simple.get("rpm", 0)) or 0)
                    next_state.speed = int(simple.get("speed", 0) or 0)
                    next_state.gear = 0 if str(gear_raw).upper() in {"N", "R"} else int(gear_raw or 0)
                    next_state.maxrpm = int(simple.get("maxRpm", next_state.rpm) or next_state.rpm)
                    throttle = float(simple.get("throttle", simple.get("throttlePct", 0.0)) or 0.0)
                    try:
                        throttle = float(self._fetch_text("/Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle"))
                    except Exception:
                        pass
                    next_state.throttle = self._normalize_throttle(throttle)
                    for path in (
                        "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiterOn",
                        "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiter",
                        "/Api/GetProperty/DataCorePlugin.GameData.PitLimiterOn",
                        "/Api/GetProperty/DataCorePlugin.GameData.PitLimiter",
                    ):
                        try:
                            raw = self._fetch_text(path).strip().lower()
                        except Exception:
                            continue
                        if raw in {"1", "true", "yes", "on"}:
                            next_state.pit_limiter = True
                            break
                        if raw in {"0", "false", "no", "off"}:
                            next_state.pit_limiter = False
                            break
                    next_state.live = next_state.rpm > 0 or next_state.speed > 0 or next_state.gear > 0
                    next_state.last_packet_ts = time.time()
            except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
                next_state.error = str(exc)
            with self.state_lock:
                if next_state.last_packet_ts <= 0:
                    next_state.last_packet_ts = self.simhub.last_packet_ts
                self.simhub = next_state
            time.sleep(0.025)

    def _status_loop(self) -> None:
        next_config = 0.0
        while not self.stop_event.is_set():
            if not self.serial:
                time.sleep(0.2)
                continue
            try:
                now = time.monotonic()
                if self.last_hello_monotonic <= 0 and (now - self.serial_open_monotonic) < 2.2:
                    time.sleep(0.08)
                    continue

                status = self._rpc("STATUS", timeout=0.9)
                with self.state_lock:
                    self.last_status_ok_monotonic = now
                    self.esp.bridge_connected = True
                    self.esp.raw = status
                    self.esp.usb_state = str(status.get("usbState", self.esp.usb_state))
                    self.esp.active_telemetry = str(status.get("activeTelemetry", self.esp.active_telemetry))
                    self.esp.usb_host = str(status.get("usbHost", self.hostname))
                    self.esp.wifi_suspended = bool(status.get("wifiSuspended", False))
                    self.esp.obd_allowed = bool(status.get("obdAllowed", True))
                    self.esp.telemetry_preference = str(status.get("telemetryPreference", "AUTO"))
                    self.esp.sim_transport = str(status.get("simTransport", "AUTO"))
                    self.esp.display_focus = str(status.get("displayFocus", "RPM"))
                    self.esp.rpm = int(status.get("rpm", 0) or 0)
                    self.esp.speed = int(status.get("speed", 0) or 0)
                    self.esp.gear = int(status.get("gear", 0) or 0)
                    self.esp.throttle = float(status.get("throttle", 0.0) or 0.0)
                    self.esp.pit_limiter = bool(status.get("pitLimiter", False))
                    self.esp.maxrpm = int(status.get("maxRpm", 0) or 0)
                    self.esp.error = str(status.get("usbError", "")) or self.esp.error
                if now >= next_config or not self.config_cache:
                    config = self._rpc("CONFIG_GET", timeout=1.5)
                    with self.state_lock:
                        self.config_cache = config
                    next_config = now + 8.0
            except RuntimeError as exc:
                with self.state_lock:
                    self.esp.bridge_connected = self._bridge_recently_seen()
                    if self.esp.bridge_connected and self.esp.usb_state in {"USB getrennt", "USB disconnected"}:
                        self.esp.usb_state = "Warte auf Status"
                    self.esp.error = str(exc)
            time.sleep(0.25)

    def get_simhub(self) -> SimHubState:
        with self.state_lock:
            return SimHubState(**self.simhub.__dict__)

    def get_config(self) -> dict[str, Any]:
        with self.state_lock:
            return dict(self.config_cache)

    def default_config(self) -> dict[str, Any]:
        return {
            "ok": True,
            "telemetryPreference": 0,
            "simTransport": 0,
            "displayFocus": 0,
            "displayBrightness": 220,
            "nightMode": False,
            "useMph": False,
            "simHubHost": "",
            "simHubPort": self.simhub_port,
            "simHubPollMs": 75,
        }

    def save_config(self, payload: dict[str, Any]) -> dict[str, Any]:
        encoded = urllib.parse.urlencode({
            "telemetryPreference": int(payload.get("telemetryPreference", 0)),
            "simTransport": int(payload.get("simTransport", 0)),
            "displayFocus": int(payload.get("displayFocus", 0)),
            "displayBrightness": int(payload.get("displayBrightness", 220)),
            "nightMode": 1 if payload.get("nightMode") else 0,
            "useMph": 1 if payload.get("useMph") else 0,
            "simHubHost": str(payload.get("simHubHost", "")),
            "simHubPort": int(payload.get("simHubPort", self.simhub_port)),
            "simHubPollMs": int(payload.get("simHubPollMs", 75)),
        })
        if not self.serial:
            raise RuntimeError("ESP nicht per USB verbunden. Serial Monitor schliessen und Bridge neu starten.")
        response = self._rpc(f"SET {encoded}", timeout=3.0)
        with self.state_lock:
            self.config_cache = response
        return response

    def build_status(self) -> dict[str, Any]:
        with self.state_lock:
            esp = EspState(**self.esp.__dict__)
            simhub = SimHubState(**self.simhub.__dict__)
            bridge_recent = self._bridge_recently_seen()
        last_age = "-"
        if simhub.last_packet_ts > 0:
            last_age = f"{max(0.0, time.time() - simhub.last_packet_ts):.1f}s"
        active_map = {
            "USB_SIM": "USB Sim",
            "USB Sim": "USB Sim",
            "SIM_NET": "SimHub Netzwerk",
            "SimHub": "SimHub Netzwerk",
            "OBD": "OBD",
            "NONE": "Keine",
        }
        active_label = active_map.get(esp.active_telemetry, esp.active_telemetry or "Keine")
        usb_state_labels = {
            "LIVE": "USB verbunden",
            "WAITING_DATA": "USB verbunden, warte auf Daten",
            "WAITING_BRIDGE": "USB erkannt, warte auf Bridge",
            "DISABLED": "USB in ESP deaktiviert",
            "DISCONNECTED": "USB getrennt",
            "ERROR": "USB Fehler",
        }
        usb_ready = esp.serial_connected and (esp.bridge_connected or bridge_recent)
        rpm = esp.rpm if usb_ready else simhub.rpm
        speed = esp.speed if usb_ready else simhub.speed
        gear = esp.gear if usb_ready else simhub.gear
        throttle = esp.throttle if usb_ready else simhub.throttle
        pit_limiter = esp.pit_limiter if usb_ready else simhub.pit_limiter
        maxrpm = esp.maxrpm if usb_ready and esp.maxrpm > 0 else simhub.maxrpm
        shift_state = "SHIFT" if maxrpm > 0 and rpm >= max(maxrpm - 200, 1) else "Ready"
        usb_live = esp.usb_state.upper() == "LIVE" or esp.usb_state.lower() == "usb live"
        simhub_live = simhub.live
        return {
            "ok": True,
            "port": esp.port or "Kein ESP gefunden",
            "host": self.hostname,
            "usbConnected": esp.serial_connected,
            "usbBridgeConnected": usb_ready,
            "usbState": esp.usb_state,
            "usbStateLabel": usb_state_labels.get(esp.usb_state.upper(), esp.usb_state),
            "usbLive": usb_live,
            "usbHost": esp.usb_host or self.hostname,
            "activeTelemetry": esp.active_telemetry,
            "activeTelemetryLabel": active_label,
            "telemetryPreference": esp.telemetry_preference,
            "simTransport": esp.sim_transport,
            "transportLabel": "USB Telemetrie" if esp.sim_transport in {"AUTO", "USB"} else "Netzwerk SimHub",
            "rpm": rpm,
            "speed": speed,
            "gear": gear,
            "gearText": "N" if gear <= 0 else str(gear),
            "throttle": throttle,
            "pitLimiter": pit_limiter,
            "shiftState": shift_state,
            "modeSummary": "USB Sim aktiv" if esp.wifi_suspended or active_label == "USB Sim" else ("OBD aktiv" if active_label == "OBD" else "Warte auf Quelle"),
            "wifiSummary": "Unterdrueckt" if esp.wifi_suspended else "Normal aktiv",
            "obdSummary": "Deaktiviert" if not esp.obd_allowed else "Erlaubt",
            "lastError": esp.error or simhub.error,
            "simHubState": "Live" if simhub_live else ("Warte auf Daten" if simhub.reachable else "Nicht erreichbar"),
            "simHubStateLabel": "Live" if simhub_live else ("Warte auf Daten" if simhub.reachable else "Nicht erreichbar"),
            "simHubReachable": simhub.reachable,
            "simHubLive": simhub_live,
            "lastTelemetryAgeLabel": last_age,
        }

    def build_status_compat(self) -> dict[str, Any]:
        with self.state_lock:
            esp = EspState(**self.esp.__dict__)
        status = self.build_status()
        display_focus_map = {"RPM": 0, "GEAR": 1, "SPEED": 2}
        return {
            "rpm": status["rpm"],
            "maxRpm": max(status["rpm"], 0),
            "speed": status["speed"],
            "gear": 0 if status["gearText"] == "N" else status["gear"],
            "pitLimiter": status["pitLimiter"],
            "lastTx": "",
            "lastObd": "",
            "connected": False,
            "autoReconnect": False,
            "devMode": True,
            "bleText": "USB Sim Modus",
            "vehicleVin": "",
            "vehicleModel": "USB / SimHub",
            "vehicleDiag": status["simHubStateLabel"],
            "vehicleInfoRequestRunning": False,
            "vehicleInfoReady": status["simHubLive"],
            "vehicleInfoAge": 0,
            "telemetryPreference": status["telemetryPreference"],
            "simTransport": status["simTransport"],
            "activeTelemetry": status["activeTelemetryLabel"],
            "simHubState": status["simHubStateLabel"],
            "usbState": status["usbStateLabel"],
            "usbConnected": status["usbConnected"],
            "usbBridgeConnected": status["usbBridgeConnected"],
            "usbBridgeWebActive": True,
            "usbHost": status["usbHost"],
            "usbError": status["lastError"],
            "wifiSuspended": status["wifiSummary"] == "Unterdrueckt",
            "obdAllowed": status["obdSummary"] != "Deaktiviert",
            "displayFocus": display_focus_map.get((esp.display_focus or "RPM").upper(), 0),
            "simHubConfigured": True,
            "bleConnectInProgress": False,
            "bleConnectTargetAddr": "",
            "bleConnectTargetName": "",
            "bleConnectError": "",
        }

    def build_wifi_status_compat(self) -> dict[str, Any]:
        status = self.build_status()
        suspended = status["wifiSummary"] == "Unterdrueckt"
        return {
            "mode": "USB_BRIDGE",
            "apActive": False,
            "apClients": 0,
            "apIp": "",
            "staConnected": False,
            "staConnecting": False,
            "staLastError": "" if suspended else "USB-Modus aktiv",
            "currentSsid": "",
            "staIp": "",
            "ip": "127.0.0.1" if status["usbBridgeConnected"] else "",
            "scanRunning": False,
            "scanResults": [],
        }

    def build_ble_status_compat(self) -> dict[str, Any]:
        status = self.build_status()
        return {
            "connected": False,
            "targetName": "USB Sim Modus",
            "targetAddr": "",
            "autoReconnect": False,
            "manualActive": False,
            "manualFailed": False,
            "manualAttempts": 0,
            "autoAttempts": 0,
            "connectBusy": False,
            "connectManual": False,
            "lastConnectOk": False,
            "connectInProgress": False,
            "connectTargetAddr": "",
            "connectTargetName": "",
            "connectError": "" if status["obdSummary"] == "Deaktiviert" else "OBD im USB-Modus nicht aktiv",
            "scanRunning": False,
            "scanAge": -1,
            "connectAge": -1,
            "results": [],
        }


def create_app(bridge: UsbSimBridge) -> Flask:
    app = Flask(__name__)
    template = Path(__file__).with_name("usb_sim_bridge_index.html").read_text(encoding="utf-8")

    def request_payload() -> dict[str, Any]:
        if request.is_json:
            return request.get_json(silent=True) or {}

        form = request.form
        return {
            "telemetryPreference": int(form.get("telemetryPreference", form.get("telemetryPreference".lower(), 0)) or 0),
            "simTransport": int(form.get("simTransport", 0) or 0),
            "displayFocus": int(form.get("displayFocus", 0) or 0),
            "displayBrightness": int(form.get("displayBrightness", form.get("brightness", 220)) or 220),
            "nightMode": form.get("nightMode", "") in {"1", "on", "true", "True"},
            "useMph": form.get("useMph", "") in {"1", "on", "true", "True"},
            "simHubHost": str(form.get("simHubHost", "") or ""),
            "simHubPort": int(form.get("simHubPort", bridge.simhub_port) or bridge.simhub_port),
            "simHubPollMs": int(form.get("simHubPollMs", 75) or 75),
        }

    @app.get("/")
    def index() -> str:
        return render_template_string(template)

    @app.get("/settings")
    @app.get("/settings/")
    def settings_page() -> str:
        return render_template_string(template)

    @app.get("/api/status")
    def api_status():
        return jsonify(bridge.build_status())

    @app.get("/status")
    def compat_status():
        return jsonify(bridge.build_status_compat())

    @app.get("/api/config")
    def api_config():
        try:
            config = bridge.get_config() or bridge._rpc("CONFIG_GET")
            return jsonify(config)
        except Exception as exc:
            fallback = bridge.default_config()
            fallback["bridgeError"] = str(exc)
            return jsonify(fallback)

    @app.get("/wifi/status")
    def compat_wifi_status():
        return jsonify(bridge.build_wifi_status_compat())

    @app.post("/wifi/scan")
    def compat_wifi_scan():
        return jsonify({"status": "busy", "reason": "usb-mode"})

    @app.post("/wifi/disconnect")
    def compat_wifi_disconnect():
        return jsonify({"status": "ok"})

    @app.get("/ble/status")
    def compat_ble_status():
        return jsonify(bridge.build_ble_status_compat())

    @app.post("/ble/scan")
    def compat_ble_scan():
        return jsonify({"status": "busy", "reason": "usb-mode"})

    @app.post("/api/config")
    def api_config_save():
        payload = request_payload()
        try:
            return jsonify(bridge.save_config(payload))
        except Exception as exc:
            return jsonify({"ok": False, "error": str(exc)}), 503

    @app.post("/save")
    @app.post("/settings")
    @app.post("/settings/")
    def compat_save():
        payload = request_payload()
        try:
            bridge.save_config(payload)
            if request.is_json:
                return jsonify({"ok": True})
            return redirect("/settings?saved=1", code=303)
        except Exception as exc:
            if request.is_json:
                return jsonify({"ok": False, "error": str(exc)}), 503
            return render_template_string(template), 503

    return app


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local USB bridge between SimHub and the ShiftLight ESP32.")
    parser.add_argument("--serial-port", help="COM port override, e.g. COM5")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--simhub-host", default="127.0.0.1")
    parser.add_argument("--simhub-port", type=int, default=8888)
    parser.add_argument("--web-port", type=int, default=8765)
    parser.add_argument("--debug", action="store_true")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    bridge = UsbSimBridge(
        serial_port=args.serial_port,
        baudrate=args.baudrate,
        simhub_host=args.simhub_host,
        simhub_port=args.simhub_port,
        debug=args.debug,
    )
    bridge.start()
    app = create_app(bridge)
    print(f"ShiftLight USB bridge ready: http://127.0.0.1:{args.web_port}", flush=True)
    print(f"ShiftLight USB bridge LAN URL: http://{bridge.web_public_host}:{args.web_port}", flush=True)
    try:
        app.run(host=bridge.web_bind_host, port=args.web_port, debug=False, use_reloader=False, threaded=True)
    finally:
        bridge.stop()


if __name__ == "__main__":
    main()
