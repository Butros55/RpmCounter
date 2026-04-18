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
.\run_simulator.cmd
```

Alternativ direkt:

```powershell
.\simulator\run_simulator.cmd
.\simulator\run_simulator.ps1
```

This now starts the desktop UI in the new `800x400` landscape format in `Auto` mode by default: the simulator prefers live SimHub data and falls back to the internal telemetry until SimHub becomes live.

The launcher now prefers the repo-local LLVM-MinGW toolchain under `.tools\llvm-mingw`, so Visual Studio is no longer required for the default Windows path. It also reuses the existing CMake cache, which makes repeat starts much faster after the first build. On a normal local start it also opens the dashboard root page in the default browser automatically.

The simulator now runs as a local stand-alone desktop stack:

- shared LVGL DDU UI
- native local ShiftLight dashboard on `http://127.0.0.1:8765`
- virtual external LED bar preview above the `800x400` display
- optional live SimHub telemetry via HTTP or UDP JSON

Useful variants:

```powershell
# 800x400 default, Auto mode: prefer SimHub and fall back to local mock telemetry
.\run_simulator.cmd

# Auto is the default, but you can force either side when needed
.\run_simulator.cmd -Telemetry Auto
.\run_simulator.cmd -Telemetry SimHub
.\run_simulator.cmd -Telemetry Simulator

# Change the local dashboard port
.\run_simulator.cmd -WebPort 8770

# Larger preview window on the desktop
.\run_simulator.cmd -Scale 2

# Disable the automatic browser open, the local dashboard, or the LED-bar preview if needed
.\run_simulator.cmd -NoBrowser
.\run_simulator.cmd -NoWeb
.\run_simulator.cmd -NoLedBar

# Force a full reconfigure if you changed generator/toolchain details
.\run_simulator.cmd -Fresh

# Capture one frame and exit automatically
.\run_simulator.cmd -CaptureFramePath "build/simulator/captures/ui.bmp" -ExitOnCapture

# Open and navigate via startup actions before capturing
.\run_simulator.cmd -UiActions "right,open" -CaptureFramePath "build/simulator/captures/source.bmp" -ExitOnCapture
```

### Manual build

```powershell
cmake -S . -B build/simulator-local -G "MinGW Makefiles" `
  -D CMAKE_BUILD_TYPE=Debug `
  -D CMAKE_C_COMPILER=.tools\llvm-mingw\llvm-mingw-20260407-ucrt-x86_64\bin\x86_64-w64-mingw32-gcc.exe `
  -D CMAKE_CXX_COMPILER=.tools\llvm-mingw\llvm-mingw-20260407-ucrt-x86_64\bin\x86_64-w64-mingw32-g++.exe `
  -D CMAKE_MAKE_PROGRAM=.tools\llvm-mingw\llvm-mingw-20260407-ucrt-x86_64\bin\mingw32-make.exe
cmake --build build/simulator-local --target rpmcounter_simulator --parallel
.\build\simulator-local\rpmcounter_simulator.exe
```

The simulator reuses the shared LVGL UI from `src/ui/ui_s3_main.cpp`, opens an SDL2 window, accepts mouse clicks/drags, and feeds the UI with deterministic mock state.

### SimHub telemetry

`run_simulator.ps1` now supports three launcher modes:

- `-Telemetry Auto`: prefer real SimHub telemetry and keep the UI alive with simulator fallback until live data arrives
- `-Telemetry SimHub` / `SIM_MODE=false`: use real SimHub telemetry without fallback
- `-Telemetry Simulator` / `SIM_MODE=true`: internal simulator telemetry

`Auto` and `SimHub` both use the local SimHub HTTP API on `http://127.0.0.1:8888` by default. This is the recommended mode for Assetto Corsa Competizione because SimHub already consumes ACC's UDP broadcast and exposes normalized telemetry through its local API.

The desktop window size can also be overridden through:

- `SIM_WINDOW_WIDTH`
- `SIM_WINDOW_HEIGHT`
- `SIM_WINDOW_SCALE`

Optional source selection:

- `SIMHUB_SOURCE=http`: poll SimHub's local API on `localhost:8888` (default, recommended for ACC)
- `SIMHUB_SOURCE=udpjson`: listen for manually forwarded JSON UDP on `localhost:20888`

Examples:

```powershell
$env:SIM_MODE = "false"
$env:SIMHUB_SOURCE = "http"
$env:SIMHUB_HTTP_PORT = "8888"
$env:SIM_DEBUG_TELEMETRY = "true"
$env:SIM_WINDOW_WIDTH = "800"
$env:SIM_WINDOW_HEIGHT = "400"
$env:SIM_WINDOW_SCALE = "1"
.\simulator\run_simulator.ps1
```

In the default `Auto` launch mode, the desktop UI starts with simulator fallback so the dashboard is immediately usable, then switches over to live SimHub data as soon as it arrives. In explicit `SimHub` mode without fallback, the UI stays in a visible `SimHub waiting` state with zeroed telemetry until data is available. Once live data has been received, the simulator keeps the last known values for up to 2 seconds and then marks them as stale.

### Local desktop dashboard

When the simulator is running, open:

```text
http://127.0.0.1:8765
```

This now opens the ShiftLight dashboard root page, not a separate simulator-only site.

The local dashboard can:

- show live RPM / speed / gear / source state
- expose dashboard and `/settings` style navigation
- switch the LED-bar behavior between `Casual`, `F1-Style`, `Aggressiv`, and `GT3 / Endurance`
- mirror the broader setup surface from the device web UI, including Auto-Max-RPM, thresholds, colors, labels, auto-brightness, display focus, WiFi names, and effect toggles
- change the virtual LED-bar count and thresholds
- adjust display brightness
- switch between internal simulator telemetry and SimHub
- send UI navigation and test commands to the running simulator

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
