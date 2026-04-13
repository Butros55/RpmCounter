# RpmCounter / ShiftLight

ESP32-S3 firmware for a ShiftLight / RPM display with BLE OBD-II, LVGL UI, WiFi web UI, NeoPixel LED bar, and a new desktop LVGL simulator.

## Features

- ESP32-S3 firmware via PlatformIO + Arduino
- BLE OBD-II integration with NimBLE/Core-BLE compatibility
- Waveshare AMOLED LVGL UI with touch support
- ST7789 fallback display path for non-S3 hardware
- WiFi AP/STA web configuration UI
- 30-LED NeoPixel shift light
- ESP32 SimHub telemetry over WiFi with automatic source selection
- ESP32 USB Sim bridge for direct PC-connected SimHub telemetry
- Desktop simulator with LVGL + SDL2, mouse input, and debug hotkeys

## Structure

```text
src/
├── bluetooth/              # BLE OBD runtime
├── core/                   # config, state, wifi, logging, helpers
├── hardware/               # ESP32 display, LEDs, logo animation
├── platform/esp32/         # ESP32-only bridge into shared UI state
├── telemetry/              # ESP32 source arbitration + SimHub client
├── ui/                     # shared LVGL UI + UI runtime types
├── web/                    # Web UI and HTTP handlers
└── main.cpp                # ESP32 firmware entry point

tools/
├── usb_sim_bridge.py       # local USB bridge + localhost web UI
├── usb_sim_bridge_index.html
└── run_usb_sim_bridge.ps1  # Windows helper to start the bridge

simulator/
├── main.cpp                # Desktop simulator entry point
├── sdl_lvgl_window.cpp     # SDL2/LVGL display + mouse backend
├── rpm_simulator.cpp       # deterministic fallback/mock telemetry
├── simulator_app.cpp       # simulator shell + UI state controller
├── run_simulator.ps1       # helper to configure/build/run on Windows
└── tests/                  # simulator-side tests

telemetry/
├── simhub_http_listener.cpp # non-blocking SimHub local API listener
├── simhub_udp_listener.cpp # non-blocking UDP JSON listener
└── telemetry_service.cpp   # source switching + stale/fallback handling
```

## ESP32 Build

From the repository root:

```powershell
pio run -e esp32s3
```

The repo now uses a shorter PlatformIO build directory (`.pio/b`) because the GFX library object paths can exceed Windows tooling limits with the default path layout.

## ESP32 SimHub Telemetry

The ESP32 firmware can now consume live SimHub telemetry over WiFi and arbitrate it against BLE OBD data:

- `Auto`: prefer SimHub when it is live, otherwise fall back to fresh OBD
- `OBD`: only use BLE OBD telemetry
- `SimHub`: only use SimHub telemetry

The source can be changed in two places:

- On-device LVGL Settings screen: tap the telemetry source button to cycle `Auto`, `OBD`, `SimHub`
- Web UI `/settings`: set `SimHub Host / PC-IP`, `SimHub Port`, `Poll interval`, and the preferred source

The ESP32 path uses SimHub's local HTTP API on the PC by default. This is the pragmatic path for ACC because SimHub already consumes ACC's UDP broadcast and exposes normalized telemetry over the network. No SimHub Arduino device is required for this mode.

Typical hardware setup:

1. Flash the firmware and boot the ESP32.
2. Connect the ESP32 to the same network as the SimHub PC, or connect the PC to the ESP32 AP.
3. Open the ESP32 Web UI and set `SimHub Host / PC-IP` to the PC address.
4. Leave `SimHub Port` at `8888` unless you changed SimHub's API port.
5. Set the preferred source to `Auto` or `SimHub`.

In `Auto`, the firmware prefers live SimHub data and falls back to OBD when SimHub is unavailable or stale.

## ESP32 USB Sim Bridge

The new direct PC-connected mode uses the ESP32 USB serial connection instead of WiFi for live SimHub telemetry.

This mode is intentionally built as:

- ESP32 firmware with a lightweight USB serial protocol
- a local PC bridge that reads SimHub on the same machine
- a local browser UI on `http://127.0.0.1:8765`

Why this shape:

- the current Arduino ESP32 core in this project already exposes USB CDC cleanly
- it avoids keeping WiFi and BLE active while the board is sitting on the PC
- it keeps the transport deterministic and easy to debug

Behavior:

- `Telemetry = Auto` and `Sim Link = Auto`:
  - if the USB bridge is connected, the ESP32 switches to USB sim telemetry
  - BLE / OBD is suppressed
  - WiFi is suspended
- `Telemetry = OBD`:
  - the ESP32 stays in real-car BLE mode
- `Telemetry = Sim / PC`:
  - the ESP32 only accepts SimHub telemetry
  - with `Sim Link = USB`, the bridge is mandatory
  - with `Sim Link = Network`, the old WiFi SimHub path stays available

### Flash the ESP32

```powershell
pio run -e esp32s3 -t upload --upload-port COM3
```

Replace `COM3` with the actual ESP port on your machine.

### Start the USB bridge on Windows

```powershell
.\tools\run_usb_sim_bridge.ps1
```

Optional overrides:

```powershell
$env:SHIFTLIGHT_USB_PORT = "COM3"
$env:SIMHUB_HOST = "127.0.0.1"
$env:SIMHUB_PORT = "8888"
$env:SHIFTLIGHT_USB_WEB_PORT = "8765"
.\tools\run_usb_sim_bridge.ps1 -Debug
```

Once the bridge is running:

- open `http://127.0.0.1:8765`
- the page shows USB status, live telemetry, and the ESP config fields
- saving writes directly to the ESP over USB

### SimHub / ACC setup

No SimHub Arduino device is required for this USB mode.

Use this flow:

1. In SimHub, choose `Assetto Corsa Competizione`.
2. In `Game config -> Telemetry`, keep ACC UDP enabled.
3. Make sure ACC itself is in a running session, not just the menu.
4. Start `run_usb_sim_bridge.ps1`.
5. Open `http://127.0.0.1:8765`.
6. On the ESP or in the bridge UI, use:
   - `Bevorzugte Quelle = Automatisch` or `Nur Sim / PC`
   - `Sim Link = Automatisch` or `USB Serial Bridge`

Expected result:

- ESP display shows USB status instead of WiFi / BLE
- BLE reconnect attempts stop while USB sim is active
- WiFi is suspended while the USB bridge is driving telemetry
- the local bridge UI becomes the control surface instead of `192.168.4.1`

## Desktop Simulator

### Quick start on Windows

```powershell
.\simulator\run_simulator.ps1
```

### Manual build

```powershell
cmake -S . -B build/simulator -G "Visual Studio 17 2022" -A x64
cmake --build build/simulator --config Debug --target rpmcounter_simulator
.\build\simulator\Debug\rpmcounter_simulator.exe
```

The simulator reuses the shared LVGL UI from `src/ui/ui_s3_main.cpp`, opens an SDL2 window, accepts mouse clicks/drags, and feeds the UI with deterministic mock state.

### SimHub telemetry

`run_simulator.ps1` now supports two telemetry modes:

- `SIM_MODE=true`: internal simulator telemetry
- `SIM_MODE=false`: use real SimHub telemetry

With `SIM_MODE=false`, the simulator defaults to the local SimHub HTTP API on `http://127.0.0.1:8888`. This is the recommended mode for Assetto Corsa Competizione because SimHub already consumes ACC's UDP broadcast and exposes normalized telemetry through its local API.

Optional source selection:

- `SIMHUB_SOURCE=http`: poll SimHub's local API on `localhost:8888` (default, recommended for ACC)
- `SIMHUB_SOURCE=udpjson`: listen for manually forwarded JSON UDP on `localhost:20888`

Examples:

```powershell
$env:SIM_MODE = "false"
$env:SIMHUB_SOURCE = "http"
$env:SIMHUB_HTTP_PORT = "8888"
$env:SIM_DEBUG_TELEMETRY = "true"
.\simulator\run_simulator.ps1
```

If no live SimHub data is available yet, the desktop UI stays in a visible `SimHub waiting` state with zeroed telemetry instead of switching to animated demo RPM. Once live data has been received, the simulator keeps the last known values for up to 2 seconds and then marks them as stale. The old fallback simulator can still be enabled explicitly with `SIM_ALLOW_FALLBACK_SIMULATOR=true`.

Optional test hooks:

```powershell
$env:SIM_CAPTURE_FRAME_PATH = "build/simulator/captures/live.bmp"
$env:SIM_UI_ACTIONS = "right,right,right,right,open"
$env:SIM_EXIT_ON_CAPTURE = "true"
.\simulator\run_simulator.ps1
```

This exports the rendered LVGL framebuffer as a `.bmp` once a live telemetry frame is visible. It is intended for reproducible UI validation and future automated smoke tests.

On Windows, the first live-mode launch may trigger a one-time firewall prompt for local listeners. Allowing it once is sufficient for subsequent runs.

### Simulator controls

- Mouse drag / click: normal LVGL interaction
- Left / Right: previous or next card
- Enter: open selected card
- Esc: return to home
- `L`: show logo overlay
- `B`: cycle BLE state
- `W`: cycle WiFi state
- Up / Down: raise or lower fallback simulator RPM
- `S`: toggle shift indicator
- Space: pause or resume fallback simulator RPM animation
- `R`: reset simulator state

## Simulator Tests

```powershell
cmake --build build/simulator --config Debug --target rpmcounter_simulator_tests
ctest --test-dir build/simulator -C Debug --output-on-failure
```

## Notes

- The ESP32 firmware path remains unchanged in `src/main.cpp` and still uses the existing non-blocking setup/loop orchestration.
- The shared UI now consumes a platform-neutral runtime snapshot from `src/ui/ui_runtime.h`.
- ESP32 hardware stays in the existing `src/hardware/*` modules; the simulator lives separately under `simulator/`.
