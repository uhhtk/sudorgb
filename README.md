# SudoRGB — RGB & NZXT Kraken control for Linux

One desktop app for OpenRGB lighting and NZXT Kraken cooling/LCD on Omarchy (Arch + Hyprland).
C++20 / Qt 6 Quick UI, native OpenRGB SDK client, and an isolated Python service that is the
*only* thing allowed to touch the Kraken.

## Build & install

```bash
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-shadertools qt6-wayland
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
sudo cmake --install build          # /usr/local/bin/orkc + service + .desktop
```

Glorious Model O 2 / O 2 Mini / I 2 (wireless receiver) lighting is driven natively; after
installing, reload udev once so your user may access it:

```bash
sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw
```

The launcher entry is **SudoRGB**; the command is `orkc`.

Runtime deps: `openrgb` (driver server for non-Kraken RGB devices), `liquidctl ≥ 1.15`,
`python-pillow`, system `/usr/bin/python3`. The OpenKraken application is **not** needed: its
low-level Kraken driver is bundled in `service/kraken_driver/` (MIT, see `LICENSE.OpenKraken`).

## First run: hand the Kraken over

Only one program may drive the cooler. If OpenKraken (or OpenRGB with its Kraken detector
enabled) has it open, SudoRGB shows who holds it and waits instead of fighting.

1. Quit OpenKraken and turn off its autostart / "run in background".
2. Close OpenRGB, then in ORKC → Settings → OpenRGB press **Disable Kraken in OpenRGB…**
   (edits `~/.config/OpenRGB/OpenRGB.json`, writes a timestamped backup first). Or untick
   *NZXT Kraken 2024 ELITE Series RGB* in OpenRGB → Settings → Supported Devices.
3. Start OpenRGB with its SDK server (`openrgb --server --server-host 127.0.0.1`, or the
   SDK Server tab). ORKC can auto-start it, bound to loopback only.

ORKC takes the Kraken over automatically as soon as it is free.

Autostart on Hyprland (`~/.config/hypr/autostart.conf`): `exec-once = uwsm app -- orkc`
or, headless (restore lighting/LCD/cooling then exit): `exec-once = orkc --restore`.

## Why OpenKraken's GIF mode fell over

Measured on a `540×301`, 54-frame, 661 KB GIF:

| | liquidctl `_prepare_gif_file` | ORKC media pipeline |
|---|---|---|
| Upload size | **16.8 MB** (70% of the 24 MB LCD memory) | **2.2 MB** (fill) / 1.5 MB (fit) |
| CPU time | 5.7 s, 159 MB RSS | 0.8 s, isolated child process |
| Geometry | stretched to 640×640 | aspect-correct cover/contain |

liquidctl resizes each frame and re-encodes; Pillow decodes frames after the first as RGB, so
each frame gets its *own* dithered palette and inter-frame compression is lost. The long bulk
transfer then holds the device while OpenRGB's traffic on the same HID node desyncs replies
(“missing messages”), and the upload fails or reconnect-storms.

ORKC instead: validates first (size, pixels, frames), fits by aspect ratio, builds **one**
palette from sampled frames, stores unchanged pixels as transparency (lossless — verified
pixel-identical), drops frames rather than slowing playback to stay under 8 MB, and runs in a
`RLIMIT_AS`/`RLIMIT_CPU`-limited, `nice`d subprocess with a timeout. Results are cached by content
hash. The pre-processed bytes are handed to the device verbatim (liquidctl's re-encode is bypassed
only for our own cache files).

## Architecture

```
Qt Quick UI (qml/) ──► RgbService ─► OpenRgbClient ──TCP──► OpenRGB SDK server
                   ├─► KrakenService ─stdin/stdout JSON─► kraken_service.py (python3 -I)
                   ├─► ProfileManager (presets + ~/.config/orkc/profiles/*.json)          │
                   └─► SystemMonitor (hwmon / procfs)            bundled kraken_driver (KrakenDevice) → liquidctl → USB
```

* **OpenRGB** (`src/openrgb/`): async SDK client; negotiates protocol ≤ 4 (server 1.0rc3 speaks 5);
  sync on connect and on `DEVICE_LIST_UPDATED`; reply FIFO with timeouts; exponential reconnect;
  LED writes coalesced at ≤ 30 Hz. Modes, zones, speed/brightness ranges and colour modes come from
  each device — nothing hard-coded. A Kraken exposed by OpenRGB is never written to.
* **Kraken service** (`service/`): one worker thread owns the device. Per-user `flock`, plus a
  `/proc/*/fd` scan for other holders of the Kraken's hidraw/usb nodes and for OpenKraken, re-run
  immediately before every open and every 5 s while connected (releases the device if someone
  else appears). Reconnects with backoff and re-applies the desired state, including the LCD.
  Host-side ring/fan effects (the 2023/2024 firmware rejects hardware effects; ~1 frame/s).
* **Supervisor** (`KrakenService`): system interpreter in isolated mode with a scrubbed environment
  (your mise Python 3.14.8 on PATH lacks liquidctl/Pillow), request ids + timeouts, heartbeat,
  restart backoff, crash-loop cap.
* **Persistence**: XDG dirs, `QSaveFile` atomic writes; corrupt files are moved aside, never
  overwritten. Settings `~/.config/orkc/settings.json`, profiles `~/.config/orkc/profiles/`,
  state `~/.local/state/orkc/` (Kraken desired state, last RGB session, deleted profiles),
  media cache `~/.cache/orkc/lcd/`.

## Development

```bash
ORKC_KRAKEN_MOCK=1 ./build/orkc        # simulated Kraken (UI shows "sim")
ORKC_KRAKEN_DEBUG=1 ./build/orkc       # verbose service log (Settings → Diagnostics)
# Live SDK test against a throw-away OpenRGB with only debug devices:
ORKC_TEST_OPENRGB_PORT=6743 ctest --test-dir build
```

## Limits

* Kraken effects are streamed by the host at ~1 Hz (firmware limitation), so they stop if the app
  quits; cooling curves and LCD media persist in the cooler.
* Protocol v5/v6 extras (per-zone modes, alternative LED names) are not used.
* GPU temperature is read for AMD (`amdgpu` hwmon) only; others show "unavailable".
