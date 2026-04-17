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

from flask import Flask, Response, jsonify, redirect, render_template_string, request
import serial
from serial import SerialException
from serial.tools import list_ports

SERIAL_PING_INTERVAL_S = 0.20
SERIAL_TX_INTERVAL_S = 0.006
SERIAL_LOOP_SLEEP_S = 0.002
SIMHUB_SIMPLE_POLL_INTERVAL_S = 0.006
SIMHUB_META_REFRESH_INTERVAL_S = 0.15
SIMHUB_OPTIONAL_REFRESH_INTERVAL_S = 0.05
SIMHUB_HTTP_TIMEOUT_S = 0.18
STATUS_RPC_INTERVAL_S = 0.50
CONFIG_RPC_REFRESH_INTERVAL_S = 8.0
ESP_RPC_TIMEOUT_S = 0.8
ESP_CONFIG_RPC_TIMEOUT_S = 1.5
SIMHUB_GAME_DATA_PATH = "/Api/GetGameData"
SIMHUB_GAME_DATA_SIMPLE_PATH = "/Api/GetGameDataSimple"
SIMHUB_THROTTLE_PATH = "/Api/GetProperty/DataCorePlugin.GameData.NewData.Throttle"
SIMHUB_PIT_LIMITER_PATHS = (
    "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiterOn",
    "/Api/GetProperty/DataCorePlugin.GameData.NewData.PitLimiter",
    "/Api/GetProperty/DataCorePlugin.GameData.PitLimiterOn",
    "/Api/GetProperty/DataCorePlugin.GameData.PitLimiter",
)


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
    sim_transport_mode: str = "AUTO"
    simhub_state: str = "Deaktiviert"
    telemetry_fallback: bool = False
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
        self.rpc_commands: dict[int, str] = {}
        self.last_hello_monotonic = 0.0
        self.last_status_ok_monotonic = 0.0
        self.serial_open_monotonic = 0.0
        self.simhub = SimHubState()
        self.esp = EspState()
        self.config_cache: dict[str, Any] = {}
        self.last_esp_web_base_url = ""
        self.telemetry_seq = 0
        self.telemetry_tx_count = 0
        self.telemetry_tx_error_count = 0
        self.last_telemetry_tx_monotonic = 0.0
        self.last_telemetry_gap_ms = 0.0
        self.max_telemetry_gap_ms = 0.0
        self.status_rpc_count = 0
        self.status_rpc_error_count = 0

    @staticmethod
    def _timestamp() -> str:
        now = time.time()
        return time.strftime("%H:%M:%S", time.localtime(now)) + f".{int((now % 1.0) * 1000):03d}"

    def log(self, text: str) -> None:
        if self.debug:
            print(f"[USB-BRIDGE {self._timestamp()}] {text}", flush=True)

    @staticmethod
    def _normalize_throttle(value: float) -> float:
        if value > 1.0:
            value /= 100.0
        return max(0.0, min(1.0, value))

    @staticmethod
    def _parse_boolish(value: Any) -> bool | None:
        if isinstance(value, bool):
            return value
        if isinstance(value, (int, float)):
            return bool(value)
        text = str(value or "").strip().lower()
        if text in {"1", "true", "yes", "on"}:
            return True
        if text in {"0", "false", "no", "off"}:
            return False
        return None

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

    def _log_esp_status_change(self, previous: EspState, current: EspState) -> None:
        changes: list[str] = []
        if previous.serial_connected != current.serial_connected:
            changes.append(f"serial={current.serial_connected}")
        if previous.bridge_connected != current.bridge_connected:
            changes.append(f"bridge={current.bridge_connected}")
        if previous.usb_state != current.usb_state:
            changes.append(f"usb={current.usb_state}")
        if previous.active_telemetry != current.active_telemetry:
            changes.append(f"source={current.active_telemetry}")
        if previous.sim_transport_mode != current.sim_transport_mode:
            changes.append(f"transport={current.sim_transport_mode}")
        if previous.telemetry_fallback != current.telemetry_fallback:
            changes.append(f"fallback={current.telemetry_fallback}")
        if previous.error != current.error and current.error:
            changes.append(f"error={current.error}")
        if changes:
            self.log("status change: " + ", ".join(changes))

    def _log_simhub_state_change(self, previous: SimHubState, current: SimHubState) -> None:
        changes: list[str] = []
        if previous.reachable != current.reachable:
            changes.append(f"reachable={current.reachable}")
        if previous.live != current.live:
            changes.append(f"live={current.live}")
        if previous.error != current.error and current.error:
            changes.append(f"error={current.error}")
        if changes:
            self.log("simhub change: " + ", ".join(changes))

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
                self.esp.usb_state = "USB getrennt"
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
            self.log(f"serial open failed on {port}: {self._friendly_serial_error(exc)}")

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
        self.log(f"serial closed: {reason}")

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
                request_id = int(req_text)
                waiter = self.rpc_waiters.pop(request_id, None)
                data = json.loads(payload)
            except (ValueError, json.JSONDecodeError):
                return
            command = self.rpc_commands.pop(request_id, "")
            if waiter:
                waiter.put(data)
            if command == "STATUS":
                self.log(f"rx STATUS id={req_text}")
            return
        if line.startswith("USBSIM HELLO ") or line.startswith("USBSIM PONG "):
            payload = line.split(" ", 2)[2] if line.count(" ") >= 2 else ""
            fields = urllib.parse.parse_qs(payload, keep_blank_values=True)
            command = "HELLO" if line.startswith("USBSIM HELLO ") else "PONG"
            with self.state_lock:
                self.last_hello_monotonic = time.monotonic()
                self.esp.bridge_connected = True
                self.esp.serial_connected = True
                self.esp.error = ""
                if "state" in fields and fields["state"]:
                    self.esp.usb_state = fields["state"][0]
                if "device" in fields and fields["device"]:
                    self.esp.usb_host = fields["device"][0]
            self.log(f"rx {command} payload={payload}")
            return
        if line.startswith("USBSIM TELEMETRY "):
            self.log(f"rx TELEMETRY payload={line[len('USBSIM TELEMETRY '):]}")
            return
        if not line.startswith("USBSIM "):
            self.log(f"log: {line}")

    def _rpc(self, command: str, timeout: float = ESP_RPC_TIMEOUT_S) -> dict[str, Any]:
        if not self.serial:
            raise RuntimeError("ESP not connected")
        with self.rpc_lock:
            self.rpc_counter += 1
            request_id = self.rpc_counter
            waiter: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=1)
            self.rpc_waiters[request_id] = waiter
            self.rpc_commands[request_id] = command.split(" ", 1)[0]
            try:
                self.log(f"tx RPC id={request_id} cmd={command}")
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
                self.rpc_commands.pop(request_id, None)

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
                    self.log(f"tx PING payload={ping}")
                    next_ping = now + SERIAL_PING_INTERVAL_S
                if now >= next_tx:
                    simhub = self.get_simhub()
                    if simhub.live:
                        if self.last_telemetry_tx_monotonic > 0.0:
                            gap_ms = max(0.0, (now - self.last_telemetry_tx_monotonic) * 1000.0)
                            self.last_telemetry_gap_ms = gap_ms
                            self.max_telemetry_gap_ms = max(self.max_telemetry_gap_ms, gap_ms)
                        self.last_telemetry_tx_monotonic = now
                        self.telemetry_seq += 1
                        payload = urllib.parse.urlencode({
                            "seq": self.telemetry_seq,
                            "rpm": simhub.rpm,
                            "speed": simhub.speed,
                            "gear": simhub.gear,
                            "throttle": f"{simhub.throttle:.3f}",
                            "pit": 1 if simhub.pit_limiter else 0,
                            "maxrpm": simhub.maxrpm,
                        })
                        self._send_line(f"USBSIM TELEMETRY {payload}")
                        self.telemetry_tx_count += 1
                        self.log(f"tx TELEMETRY payload={payload}")
                    next_tx = now + SERIAL_TX_INTERVAL_S
                time.sleep(SERIAL_LOOP_SLEEP_S)
            except (SerialException, OSError) as exc:
                self.telemetry_tx_error_count += 1
                self.log(f"serial error: {exc}")
                self._close_serial(self._friendly_serial_error(exc))
                time.sleep(0.5)

    def _fetch_text(self, path: str, timeout: float = SIMHUB_HTTP_TIMEOUT_S) -> str:
        url = f"http://{self.simhub_host}:{self.simhub_port}{path}"
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.read().decode("utf-8", errors="replace")

    def _fetch_json(self, path: str, timeout: float = SIMHUB_HTTP_TIMEOUT_S) -> dict[str, Any]:
        return json.loads(self._fetch_text(path, timeout=timeout))

    def _fetch_optional_bool(self, paths: tuple[str, ...]) -> bool | None:
        for path in paths:
            try:
                parsed = self._parse_boolish(self._fetch_text(path).strip())
            except Exception:
                continue
            if parsed is not None:
                return parsed
        return None

    def _simhub_loop(self) -> None:
        last_meta_refresh = 0.0
        last_optional_refresh = 0.0
        next_poll = 0.0
        game_running = False
        has_new_data = False
        cached_throttle = 0.0
        cached_pit_limiter = False
        while not self.stop_event.is_set():
            now = time.monotonic()
            if now < next_poll:
                time.sleep(min(SERIAL_LOOP_SLEEP_S, next_poll - now))
                continue
            next_poll = now + SIMHUB_SIMPLE_POLL_INTERVAL_S

            next_state = SimHubState()
            try:
                if last_meta_refresh <= 0.0 or (now - last_meta_refresh) >= SIMHUB_META_REFRESH_INTERVAL_S:
                    game_data = self._fetch_json(SIMHUB_GAME_DATA_PATH)
                    last_meta_refresh = now
                    game_running = bool(game_data.get("GameRunning"))
                    has_new_data = game_data.get("NewData") not in (None, "null")
                next_state.reachable = True

                if game_running and has_new_data:
                    simple = self._fetch_json(SIMHUB_GAME_DATA_SIMPLE_PATH)
                    gear_raw = simple.get("gear", 0)
                    next_state.rpm = int(simple.get("rpms", simple.get("rpm", 0)) or 0)
                    next_state.speed = int(simple.get("speed", 0) or 0)
                    next_state.gear = 0 if str(gear_raw).upper() in {"N", "R"} else int(gear_raw or 0)
                    next_state.maxrpm = int(simple.get("maxRpm", next_state.rpm) or next_state.rpm)
                    throttle_value = simple.get("throttle", simple.get("throttlePct", None))
                    if throttle_value is not None:
                        cached_throttle = self._normalize_throttle(float(throttle_value or 0.0))

                    pit_simple = None
                    for key in ("pitLimiter", "pitLimiterOn", "PitLimiter", "PitLimiterOn"):
                        pit_simple = self._parse_boolish(simple.get(key))
                        if pit_simple is not None:
                            break
                    if pit_simple is not None:
                        cached_pit_limiter = pit_simple

                    if last_optional_refresh <= 0.0 or (now - last_optional_refresh) >= SIMHUB_OPTIONAL_REFRESH_INTERVAL_S:
                        last_optional_refresh = now
                        try:
                            cached_throttle = self._normalize_throttle(float(self._fetch_text(SIMHUB_THROTTLE_PATH).strip()))
                        except Exception:
                            pass
                        pit_value = self._fetch_optional_bool(SIMHUB_PIT_LIMITER_PATHS)
                        if pit_value is not None:
                            cached_pit_limiter = pit_value

                    next_state.throttle = cached_throttle
                    next_state.pit_limiter = cached_pit_limiter
                    # Previously this gated "live" on rpm/speed/gear being > 0,
                    # which flipped false whenever the car was stationary in
                    # the pits (rpm=idle=maybe 0 for some games, speed=0,
                    # gear=0). That made the ESP see the stream as "stale" and
                    # flicker between Live and Error. If SimHub says the game
                    # is running and NewData exists, trust it — the actual
                    # values (0 or otherwise) are still forwarded faithfully.
                    next_state.live = True
                    next_state.last_packet_ts = time.time()
                    self.log(
                        "rx TELEMETRY "
                        f"rpm={next_state.rpm} speed={next_state.speed} gear={next_state.gear} throttle={next_state.throttle:.3f}"
                    )
            except (urllib.error.URLError, TimeoutError, ValueError, json.JSONDecodeError) as exc:
                next_state.error = str(exc)
            with self.state_lock:
                previous = SimHubState(**self.simhub.__dict__)
                if next_state.last_packet_ts <= 0:
                    next_state.last_packet_ts = self.simhub.last_packet_ts
                self.simhub = next_state
            self._log_simhub_state_change(previous, next_state)
            time.sleep(SERIAL_LOOP_SLEEP_S)

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

                status = self._rpc("STATUS", timeout=ESP_RPC_TIMEOUT_S)
                self.status_rpc_count += 1
                with self.state_lock:
                    previous = EspState(**self.esp.__dict__)
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
                    self.esp.sim_transport_mode = str(status.get("simTransportMode", self.esp.sim_transport_mode or "AUTO"))
                    self.esp.simhub_state = str(status.get("simHubState", self.esp.simhub_state or "Deaktiviert"))
                    self.esp.telemetry_fallback = bool(status.get("telemetryFallback", False))
                    self.esp.display_focus = str(status.get("displayFocus", "RPM"))
                    self.esp.rpm = int(status.get("rpm", 0) or 0)
                    self.esp.speed = int(status.get("speed", 0) or 0)
                    self.esp.gear = int(status.get("gear", 0) or 0)
                    self.esp.throttle = float(status.get("throttle", 0.0) or 0.0)
                    self.esp.pit_limiter = bool(status.get("pitLimiter", False))
                    self.esp.maxrpm = int(status.get("maxRpm", 0) or 0)
                    self.esp.error = str(status.get("usbError", "")) or self.esp.error
                    current = EspState(**self.esp.__dict__)
                self.log(
                    "rx STATUS "
                    f"usb={current.usb_state} source={current.active_telemetry} "
                    f"transport={current.sim_transport_mode} fallback={current.telemetry_fallback}"
                )
                wifi_ip = str(status.get("wifiIp", "") or "").strip()
                if wifi_ip and wifi_ip not in {"0.0.0.0", "-", "Nicht verbunden"}:
                    self._remember_esp_web_base_url(f"http://{wifi_ip}")
                self._log_esp_status_change(previous, current)
                if now >= next_config or not self.config_cache:
                    config = self._rpc("CONFIG_GET", timeout=ESP_CONFIG_RPC_TIMEOUT_S)
                    with self.state_lock:
                        self.config_cache = config
                    next_config = now + CONFIG_RPC_REFRESH_INTERVAL_S
            except RuntimeError as exc:
                self.status_rpc_error_count += 1
                with self.state_lock:
                    previous = EspState(**self.esp.__dict__)
                    self.esp.bridge_connected = self._bridge_recently_seen()
                    if self.esp.bridge_connected and self.esp.usb_state in {"USB getrennt", "USB disconnected"}:
                        self.esp.usb_state = "Warte auf Status"
                    self.esp.error = str(exc)
                    current = EspState(**self.esp.__dict__)
                self.log(f"status loop error: {exc}")
                self._log_esp_status_change(previous, current)
            time.sleep(STATUS_RPC_INTERVAL_S)

    def get_simhub(self) -> SimHubState:
        with self.state_lock:
            return SimHubState(**self.simhub.__dict__)

    def get_config(self) -> dict[str, Any]:
        with self.state_lock:
            return dict(self.config_cache)

    def _remember_esp_web_base_url(self, base_url: str) -> None:
        with self.state_lock:
            self.last_esp_web_base_url = base_url

    def get_esp_web_base_urls(self) -> list[str]:
        with self.state_lock:
            raw = dict(self.esp.raw)
            cached = self.last_esp_web_base_url

        wifi_ip = str(raw.get("wifiIp", "") or "").strip()
        candidates: list[str] = []
        if wifi_ip and wifi_ip not in {"0.0.0.0", "-", "Nicht verbunden"}:
            candidates.append(f"http://{wifi_ip}")
        if cached:
            candidates.append(cached)

        unique: list[str] = []
        for candidate in candidates:
            if candidate not in unique:
                unique.append(candidate)
        return unique

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
        transport_mode = (esp.sim_transport_mode or "AUTO").upper()
        transport_labels = {
            "AUTO": "Auto",
            "USB_ONLY": "USB only",
            "NETWORK_ONLY": "Network only",
            "DISABLED": "Aus",
        }
        simhub_state_map = {
            "LIVE": "Live",
            "WAITING_DATA": "Warte auf Daten",
            "WAITING_HOST": "Host fehlt",
            "WAITING_NETWORK": "WLAN fehlt",
            "ERROR": "Fehler",
            "DISABLED": "Deaktiviert",
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
        usb_state_label = usb_state_labels.get(esp.usb_state.upper(), esp.usb_state)
        simhub_state_label = simhub_state_map.get((esp.simhub_state or "").upper(), esp.simhub_state or ("Live" if simhub.live else ("Warte auf Daten" if simhub.reachable else "Nicht erreichbar")))
        simhub_live = simhub.live or simhub_state_label == "Live"
        if transport_mode == "USB_ONLY":
            mode_summary = f"USB only: {usb_state_label or 'warte auf Bridge'}"
        elif transport_mode == "NETWORK_ONLY":
            mode_summary = f"Network only: {simhub_state_label or 'warte auf Netzwerk'}"
        elif active_label == "USB Sim":
            mode_summary = "Auto: USB aktiv"
        elif active_label == "SimHub Netzwerk":
            mode_summary = "Auto: Netzwerk Fallback" if esp.telemetry_fallback else "Auto: Netzwerk aktiv"
        elif active_label == "OBD":
            mode_summary = "Auto: OBD Fallback" if esp.telemetry_fallback else "OBD aktiv"
        else:
            mode_summary = "Auto: warte auf Quelle"
        if transport_mode == "USB_ONLY":
            wifi_summary = "WLAN aktiv (USB-only Telemetrie)"
        else:
            wifi_summary = "WLAN aktiv" if not esp.wifi_suspended else "WLAN pausiert"
        if not esp.obd_allowed:
            obd_summary = "Deaktiviert"
        elif active_label == "OBD":
            obd_summary = "Aktiv"
        else:
            obd_summary = "Bereit"
        return {
            "ok": True,
            "port": esp.port or "Kein ESP gefunden",
            "host": self.hostname,
            "usbConnected": esp.serial_connected,
            "usbBridgeConnected": usb_ready,
            "usbState": esp.usb_state,
            "usbStateLabel": usb_state_label,
            "usbLive": usb_live,
            "usbHost": esp.usb_host or self.hostname,
            "activeTelemetry": esp.active_telemetry,
            "activeTelemetryLabel": active_label,
            "telemetryPreference": esp.telemetry_preference,
            "simTransport": esp.sim_transport,
            "simTransportMode": transport_mode,
            "telemetryFallback": esp.telemetry_fallback,
            "transportLabel": transport_labels.get(transport_mode, transport_mode),
            "rpm": rpm,
            "speed": speed,
            "gear": gear,
            "gearText": "N" if gear <= 0 else str(gear),
            "throttle": throttle,
            "pitLimiter": pit_limiter,
            "shiftState": shift_state,
            "modeSummary": mode_summary,
            "wifiSummary": wifi_summary,
            "obdSummary": obd_summary,
            "lastError": esp.error or simhub.error,
            "simHubState": simhub_state_label,
            "simHubStateLabel": simhub_state_label,
            "simHubReachable": simhub.reachable or simhub_state_label in {"Live", "Warte auf Daten"},
            "simHubLive": simhub_live,
            "lastTelemetryAgeLabel": last_age,
            "bridgeTelemetrySeq": self.telemetry_seq,
            "bridgeTelemetryTxCount": self.telemetry_tx_count,
            "bridgeTelemetryTxErrors": self.telemetry_tx_error_count,
            "bridgeTelemetryTargetIntervalMs": round(SERIAL_TX_INTERVAL_S * 1000.0, 1),
            "bridgeSimHubPollIntervalMs": round(SIMHUB_SIMPLE_POLL_INTERVAL_S * 1000.0, 1),
            "bridgeSimHubMetaRefreshMs": round(SIMHUB_META_REFRESH_INTERVAL_S * 1000.0, 1),
            "bridgeLastTelemetryGapMs": round(self.last_telemetry_gap_ms, 1),
            "bridgeMaxTelemetryGapMs": round(self.max_telemetry_gap_ms, 1),
            "bridgeLastTelemetryTxAgeMs": round(max(0.0, (time.monotonic() - self.last_telemetry_tx_monotonic) * 1000.0), 1) if self.last_telemetry_tx_monotonic > 0.0 else 0.0,
            "bridgeStatusRpcCount": self.status_rpc_count,
            "bridgeStatusRpcErrors": self.status_rpc_error_count,
            "espUsbTelemetryAgeMs": int(esp.raw.get("usbTelemetryAgeMs", 0) or 0),
            "espUsbTelemetryFrames": int(esp.raw.get("usbTelemetryFrames", 0) or 0),
            "espUsbTelemetryParseErrors": int(esp.raw.get("usbTelemetryParseErrors", 0) or 0),
            "espUsbTelemetryGlitchRejects": int(esp.raw.get("usbTelemetryGlitchRejects", 0) or 0),
            "espUsbTelemetryGlitchRejectUps": int(esp.raw.get("usbTelemetryGlitchRejectUps", 0) or 0),
            "espUsbTelemetryGlitchRejectDowns": int(esp.raw.get("usbTelemetryGlitchRejectDowns", 0) or 0),
            "espUsbTelemetryGlitchWindowMs": int(esp.raw.get("usbTelemetryGlitchWindowMs", 0) or 0),
            "espUsbTelemetryGlitchDeltaRpm": int(esp.raw.get("usbTelemetryGlitchDeltaRpm", 0) or 0),
            "espUsbTelemetryGapEvents": int(esp.raw.get("usbTelemetryGapEvents", 0) or 0),
            "espUsbTelemetryLastGapMs": int(esp.raw.get("usbTelemetryLastGapMs", 0) or 0),
            "espUsbTelemetryMaxGapMs": int(esp.raw.get("usbTelemetryMaxGapMs", 0) or 0),
            "espUsbTelemetrySeq": int(esp.raw.get("usbTelemetrySeq", 0) or 0),
            "espUsbTelemetrySeqGapEvents": int(esp.raw.get("usbTelemetrySeqGapEvents", 0) or 0),
            "espUsbTelemetrySeqGapFrames": int(esp.raw.get("usbTelemetrySeqGapFrames", 0) or 0),
            "espUsbTelemetrySeqDuplicates": int(esp.raw.get("usbTelemetrySeqDuplicates", 0) or 0),
            "espUsbTelemetryLineOverflows": int(esp.raw.get("usbTelemetryLineOverflows", 0) or 0),
            "espUsbTelemetryLastRejectedRpm": int(esp.raw.get("usbTelemetryLastRejectedRpm", 0) or 0),
            "espLedRawRpm": int(esp.raw.get("ledRawRpm", 0) or 0),
            "espLedFilteredRpm": int(esp.raw.get("ledFilteredRpm", 0) or 0),
            "espLedStartRpm": int(esp.raw.get("ledStartRpm", 0) or 0),
            "espLedDisplayedLeds": int(esp.raw.get("ledDisplayedLeds", 0) or 0),
            "espLedDesiredLevel": int(esp.raw.get("ledDesiredLevel", 0) or 0),
            "espLedDisplayedLevel": int(esp.raw.get("ledDisplayedLevel", 0) or 0),
            "espLedLevelCount": int(esp.raw.get("ledLevelCount", 0) or 0),
            "espLedFilterAdjusts": int(esp.raw.get("ledFilterAdjusts", 0) or 0),
            "espLedRenderCalls": int(esp.raw.get("ledRenderCalls", 0) or 0),
            "espLedFrameShows": int(esp.raw.get("ledFrameShows", 0) or 0),
            "espLedFrameSkips": int(esp.raw.get("ledFrameSkips", 0) or 0),
            "espLedBrightnessUpdates": int(esp.raw.get("ledBrightnessUpdates", 0) or 0),
            "espLedShiftBlink": bool(esp.raw.get("ledShiftBlink", False)),
            "espLedPitLimiterOnly": bool(esp.raw.get("ledPitLimiterOnly", False)),
            "espLedFastResponseActive": bool(esp.raw.get("ledFastResponseActive", False)),
            "espRedColorFallbackActive": bool(esp.raw.get("redColorFallbackActive", False)),
            "espLedDiagnosticMode": str(esp.raw.get("ledDiagnosticMode", "") or ""),
            "espLedRenderMode": str(esp.raw.get("ledRenderMode", "") or ""),
            "espLedLastWriter": str(esp.raw.get("ledLastWriter", "") or ""),
            "espLedLastShowAgeMs": int(esp.raw.get("ledLastShowAgeMs", 0) or 0),
            "espLedLastFrameHash": int(esp.raw.get("ledLastFrameHash", 0) or 0),
            "espLedExternalWriteAttempts": int(esp.raw.get("ledExternalWriteAttempts", 0) or 0),
            "espLedSnapshotChangedDuringRender": int(esp.raw.get("ledSnapshotChangedDuringRender", 0) or 0),
            "espLedDeterministicSweepActive": bool(esp.raw.get("ledDeterministicSweepActive", False)),
            "espLedSessionEffectsEnabled": bool(esp.raw.get("ledSessionEffectsEnabled", False)),
            "espLedActiveEffect": str(esp.raw.get("ledActiveEffect", "") or ""),
            "espLedQueuedEffect": str(esp.raw.get("ledQueuedEffect", "") or ""),
            "espLedLastQueuedEffect": str(esp.raw.get("ledLastQueuedEffect", "") or ""),
            "espLedSessionEffectRequests": int(esp.raw.get("ledSessionEffectRequests", 0) or 0),
            "espLedSessionEffectSuppressions": int(esp.raw.get("ledSessionEffectSuppressions", 0) or 0),
            "espTelemetrySnapshotVersion": int(esp.raw.get("telemetrySnapshotVersion", 0) or 0),
            "espTelemetrySnapshotAgeMs": int(esp.raw.get("telemetrySnapshotAgeMs", 0) or 0),
            "espTelemetrySnapshotPublishAgeMs": int(esp.raw.get("telemetrySnapshotPublishAgeMs", 0) or 0),
            "espTelemetrySnapshotPublishCount": int(esp.raw.get("telemetrySnapshotPublishCount", 0) or 0),
            "espTelemetrySnapshotSource": str(esp.raw.get("telemetrySnapshotSource", "") or ""),
            "espTelemetrySnapshotFresh": bool(esp.raw.get("telemetrySnapshotFresh", False)),
            "espTelemetryTaskRunning": bool(esp.raw.get("telemetryTaskRunning", False)),
            "espTelemetryTaskIntervalMs": int(esp.raw.get("telemetryTaskIntervalMs", 0) or 0),
            "espSimSessionState": str(esp.raw.get("simSessionState", "") or ""),
            "espTelemetrySourceTransitionCount": int(esp.raw.get("telemetrySourceTransitionCount", 0) or 0),
            "espTelemetryLastSourceTransition": str(esp.raw.get("telemetryLastSourceTransition", "") or ""),
            "espSimSessionTransitionCount": int(esp.raw.get("simSessionTransitionCount", 0) or 0),
            "espSimSessionSuppressedCount": int(esp.raw.get("simSessionSuppressedCount", 0) or 0),
            "espSimSessionLastTransition": str(esp.raw.get("simSessionLastTransition", "") or ""),
            "gearSource": str(esp.raw.get("gearSource", "") or ""),
            "espWifiIp": str(esp.raw.get("wifiIp", "") or ""),
            "espSimHubPollOk": int(esp.raw.get("simHubPollOk", 0) or 0),
            "espSimHubPollErr": int(esp.raw.get("simHubPollErr", 0) or 0),
            "espSimHubSuppressedWhileUsb": int(esp.raw.get("simHubSuppressedWhileUsb", 0) or 0),
            "espSimHubLastOkAgeMs": int(esp.raw.get("simHubLastOkAgeMs", 0) or 0),
            "espSimHubLastErrAgeMs": int(esp.raw.get("simHubLastErrAgeMs", 0) or 0),
            "espSimHubLastError": str(esp.raw.get("simHubLastError", "") or ""),
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
            "simTransportMode": status["simTransportMode"],
            "activeTelemetry": status["activeTelemetryLabel"],
            "telemetryFallback": status["telemetryFallback"],
            "simHubState": status["simHubStateLabel"],
            "usbState": status["usbStateLabel"],
            "usbConnected": status["usbConnected"],
            "usbBridgeConnected": status["usbBridgeConnected"],
            "usbBridgeWebActive": True,
            "usbHost": status["usbHost"],
            "usbError": status["lastError"],
            "wifiSuspended": status["simTransportMode"] == "USB_ONLY",
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
        suspended = status["simTransportMode"] == "USB_ONLY"
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

    def bridge_template_response(status_code: int = 200):
        return Response(render_template_string(template), status=status_code, mimetype="text/html")

    def esp_unavailable_response() -> Response:
        body = """<!doctype html>
<html lang="de">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ShiftLight ESP UI nicht erreichbar</title>
  <style>
    :root{--bg:#070a0f;--panel:#11161e;--line:#243041;--text:#edf2f7;--muted:#9aa8ba;--accent:#56c8ea;--radius:18px}
    *{box-sizing:border-box}body{margin:0;min-height:100vh;display:grid;place-items:center;padding:20px;background:linear-gradient(180deg,#06080c 0%,#0a1017 100%);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
    .panel{width:min(720px,100%);padding:24px;border-radius:var(--radius);background:rgba(17,22,30,.96);border:1px solid var(--line)}
    h1{margin:0 0 10px;font-size:1.8rem}p{margin:0 0 12px;color:var(--muted);line-height:1.5}.row{display:flex;gap:10px;flex-wrap:wrap;margin-top:18px}
    a{display:inline-flex;align-items:center;justify-content:center;min-height:46px;padding:0 16px;border-radius:14px;text-decoration:none}
    .primary{background:var(--accent);color:#04111a}.secondary{border:1px solid var(--line);color:var(--text)}
  </style>
</head>
<body>
  <section class="panel">
    <h1>ESP-Dashboard derzeit nicht erreichbar</h1>
    <p>Diese URL bleibt jetzt fest fuer die ESP-Weboberflaeche reserviert und faellt nicht mehr automatisch auf die Bridge-Seite zurueck.</p>
    <p>Die Bridge-Diagnose bleibt separat unter <strong>/bridge/</strong> verfuegbar. Sobald das Board im WLAN wieder erreichbar ist, funktioniert das Dashboard hier wieder normal.</p>
    <div class="row">
      <a class="primary" href="/bridge/">Bridge oeffnen</a>
      <a class="secondary" href="/">Dashboard erneut pruefen</a>
    </div>
  </section>
</body>
</html>"""
        return Response(body, status=503, mimetype="text/html")

    def rewrite_location(location: str, base_url: str) -> str:
        if not location:
            return location
        parsed = urllib.parse.urlsplit(location)
        base = urllib.parse.urlsplit(base_url)
        if not parsed.netloc:
            return location
        if parsed.scheme == base.scheme and parsed.netloc == base.netloc:
            rewritten = parsed.path or "/"
            if parsed.query:
                rewritten += "?" + parsed.query
            if parsed.fragment:
                rewritten += "#" + parsed.fragment
            return rewritten
        return location

    def proxy_esp_request(path: str) -> Response | None:
        base_urls = bridge.get_esp_web_base_urls()
        if not base_urls:
            return None

        headers: dict[str, str] = {}
        for name in ("Content-Type", "Accept"):
            value = request.headers.get(name)
            if value:
                headers[name] = value
        headers["X-ShiftLight-Bridge-Proxy"] = "1"

        data = request.get_data() if request.method in {"POST", "PUT", "PATCH"} else None
        query = request.query_string.decode("utf-8", errors="ignore")

        for base_url in base_urls:
            target = base_url + path
            if query:
                target += "?" + query
            upstream = urllib.request.Request(target, data=data if data else None, headers=headers, method=request.method)
            try:
                with urllib.request.urlopen(upstream, timeout=1.5) as response:
                    body = response.read()
                    response_headers: dict[str, str] = {}
                    content_type = response.headers.get("Content-Type")
                    if content_type:
                        response_headers["Content-Type"] = content_type
                    location = response.headers.get("Location")
                    if location:
                        response_headers["Location"] = rewrite_location(location, base_url)
                    bridge._remember_esp_web_base_url(base_url)
                    return Response(body, status=response.status, headers=response_headers)
            except urllib.error.HTTPError as exc:
                body = exc.read()
                response_headers = {}
                content_type = exc.headers.get("Content-Type")
                if content_type:
                    response_headers["Content-Type"] = content_type
                location = exc.headers.get("Location")
                if location:
                    response_headers["Location"] = rewrite_location(location, base_url)
                bridge._remember_esp_web_base_url(base_url)
                return Response(body, status=exc.code, headers=response_headers)
            except Exception as exc:
                bridge.log(f"esp proxy failed base={base_url} path={path}: {exc}")

        return None

    def fallback_compat_route(path: str):
        if request.method == "GET" and path in {"/", "/settings", "/settings/"}:
            return esp_unavailable_response()
        if request.method == "GET" and path == "/status":
            return jsonify(bridge.build_status_compat())
        if request.method == "GET" and path == "/wifi/status":
            return jsonify(bridge.build_wifi_status_compat())
        if request.method == "GET" and path == "/ble/status":
            return jsonify(bridge.build_ble_status_compat())
        if request.method == "POST" and path == "/wifi/scan":
            return jsonify({"status": "busy", "reason": "usb-mode"})
        if request.method == "POST" and path == "/wifi/disconnect":
            return jsonify({"status": "ok"})
        if request.method == "POST" and path == "/ble/scan":
            return jsonify({"status": "busy", "reason": "usb-mode"})
        if request.method == "POST" and path in {"/save", "/settings", "/settings/"}:
            payload = request_payload()
            try:
                bridge.save_config(payload)
                if request.is_json:
                    return jsonify({"ok": True})
                return redirect("/bridge/?saved=1", code=303)
            except Exception as exc:
                if request.is_json:
                    return jsonify({"ok": False, "error": str(exc)}), 503
                return bridge_template_response(status_code=503)
        if request.method == "GET" and path == "/favicon.ico":
            return Response(status=404)
        return Response("ESP Web UI ist aktuell nicht ueber WLAN erreichbar. Bridge-Diagnose bleibt unter /bridge/ verfuegbar.", status=503, mimetype="text/plain")

    @app.get("/bridge")
    def bridge_index_redirect():
        return redirect("/bridge/", code=303)

    @app.get("/bridge/")
    @app.get("/bridge/settings")
    @app.get("/bridge/settings/")
    def bridge_index() -> Response:
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

    @app.route("/", defaults={"path": ""}, methods=["GET", "POST"])
    @app.route("/<path:path>", methods=["GET", "POST"])
    def esp_ui_or_bridge_fallback(path: str):
        normalized = "/" + path
        if normalized.startswith("/api/") or normalized.startswith("/bridge"):
            return Response("Not found", status=404, mimetype="text/plain")

        proxied = proxy_esp_request(normalized)
        if proxied is not None:
            return proxied
        return fallback_compat_route(normalized)

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
