# SudoRGB architecture

## Goals that shape the design

1. **Broad hardware coverage without re-implementing everything.** Linux already has mature,
   community-maintained drivers (OpenRGB, liquidctl, OpenRazer, libratbag). SudoRGB integrates them
   and adds native drivers only where none exists.
2. **No root GUI.** Device access comes from udev `uaccess` rules (ACLs for the logged-in seat).
3. **One misbehaving device never freezes the app.** Every backend runs asynchronously or in its own
   process with timeouts.
4. **License hygiene.** GPL backends stay in separate processes behind documented protocols.

## Components (current)

```
                    ┌──────────────────────── Qt Quick UI (qml/) ────────────────────────┐
                    │ Overview · Lighting · LCD · Cooling · Profiles · Devices · Settings │
                    └───────────────┬───────────────┬──────────────┬─────────────────────┘
                                    │               │              │
  ┌──────────── C++ core (src/) ────┼───────────────┼──────────────┼───────────────────────────┐
  │ RgbService ── combined device list ──┐   KrakenService     HardwareCatalog                 │
  │   ├─ OpenRgbClient (SDK, TCP)        │   (process supervisor) (devices/DeviceCatalog)      │
  │   ├─ NativeDevices (hidraw drivers)  │        │                reads udev rules + usb.ids  │
  │   └─ software effects (30 Hz)        │        │                + sysfs, no root            │
  │ ProfileManager · Settings · JsonStore (atomic XDG files) · SystemMonitor (hwmon)           │
  └────────┬──────────────────────────────────────┬────────────────────────────────────────────┘
           │ TCP 127.0.0.1:6742                   │ stdin/stdout JSON (newline-delimited)
   ┌───────▼────────┐                     ┌───────▼──────────────────────────────┐
   │ OpenRGB server │ (separate process,  │ kraken_service.py (python3 -I)        │
   │  GPL-2.0+      │  started on demand) │  kraken_driver (from OpenKraken, MIT) │
   └───────┬────────┘                     │  + liquidctl (GPL-3.0+) → USB/HID     │
           │ HID / SMBus / USB            └──────────────────────────────────────┘
```

| Component | Language | Responsibility |
|---|---|---|
| `OpenRgbClient` | C++ | Async SDK client (protocol ≤ 4), reply FIFO with timeouts, reconnect backoff, LED-write coalescing ≤ 30 Hz |
| `NativeDevices` | C++ | Drivers SudoRGB owns (Glorious Model O 2 family). Presented as `orgb::Controller` so the UI treats them like OpenRGB devices |
| `RgbService` | C++ | One device list (OpenRGB + native), capability-aware colour/mode/brightness, software effects for per-LED devices without hardware effects, profile capture/apply |
| `KrakenService` | C++ | Launches/monitors the Kraken service, JSON IPC with timeouts, crash-loop protection, persisted desired state |
| `kraken_service.py` | Python | Sole owner of NZXT Kraken coolers: telemetry, cooling, LCD media pipeline, firmware lighting effects, conflict detection |
| `HardwareCatalog` | C++ | Compatibility registry: which backend can drive each connected device; diagnostics export |

## Device detection flow

1. `HardwareCatalog` parses the udev rules installed by each backend (`60-openrgb.rules`,
   `71-liquidctl.rules`, `*razer*.rules`, `70-orkc-*.rules`) into `(backend, vid, pid)` rules. Support
   grows automatically when users update a backend; no driver data is copied into SudoRGB.
2. sysfs (`/sys/bus/usb/devices`) lists connected devices and their hidraw nodes; `access(2)` tells
   whether this user can open them.
3. Each device gets one honest status: **supported**, **needs permission**, **known** (a backend knows it
   but SudoRGB does not integrate that backend yet), **unrecognized**, or other USB.
4. OpenRGB additionally discovers SMBus devices (RAM, motherboards, GPUs) that never appear in USB lists.

The Devices page rescans every 4 s only while visible (sysfs reads, no USB traffic).

## Threading and fault isolation

* GUI thread: Qt event loop only; all device I/O is asynchronous (sockets, `QProcess`, timers).
* OpenRGB runs in its own process; a hung server costs one 5 s timeout, then reconnect.
* Kraken service runs in its own process; heartbeat + restart with backoff; refuses to touch the cooler
  while another program holds it.
* Native HID writes are short `HIDIOCSFEATURE` ioctls on a non-blocking fd, paced by timers
  (wireless devices: 120 ms between fragments, ≥ 1 s between commands).

## Lighting engine (current)

* Hardware effects whenever a device has them (OpenRGB modes, Glorious effects, Kraken firmware effects).
* Software effects (rainbow, breathing) streamed at 30 Hz for per-LED devices without hardware effects;
  they stop as soon as the user sets anything else on that device.
* Kraken 2024 Elite firmware shows a Direct frame only after ~1 s without newer frames, so animations
  there must be firmware effects (see `DEVICE_SUPPORT.md`).

## Target architecture (see ROADMAP.md)

* **Driver interface** (`IDriver`: probe → open → capabilities → apply(mode/colours) → close) that both
  native drivers and backend bridges implement; one worker thread per driver.
* **Bridges**: OpenRazer (D-Bus), ratbagd (D-Bus), liquidctl non-Kraken devices (Kraken service).
* **Layout canvas**: per-LED positions for cross-device effects, audio-reactive and screen-sync sources.
* **udev netlink** hot-plug instead of polling.
* **Packaging**: AppImage/Flatpak plus a small polkit helper that installs udev rules on first run.

## Licenses and third parties

| Dependency | License | How SudoRGB uses it | Obligation |
|---|---|---|---|
| Qt 6 | LGPL-3.0 (or commercial) | Dynamically linked | Ship LGPL notice; users must be able to swap Qt libraries (dynamic linking satisfies this) |
| OpenRGB | GPL-2.0-or-later | Separate process, documented SDK network protocol; client written independently; its udev rules are *read* at runtime, not redistributed | None for SudoRGB's code as long as no OpenRGB code is copied or linked |
| liquidctl | GPL-3.0-or-later | Imported **in-process** by `service/kraken_service.py` | The Python service is effectively GPL-3.0 when distributed. Keep it a separate program (JSON IPC) so the C++ core's license stays independent |
| OpenKraken `kraken_driver/` | MIT | Vendored, notice kept (`LICENSE.OpenKraken`) | Keep the notice |
| Pillow | MIT-CMU | Kraken media pipeline | Notice |
| hwdata `usb.ids` | GPL-2.0 / BSD-3 | Read at runtime from the system | None |
| Glorious protocol facts | gloriousctl-linux (EUPL-1.2), OpenMouse-Project (AGPL-3.0) | Packet *facts* only; implementation written independently | None for facts; do not copy their code |
| OpenRazer, libratbag (planned) | GPL-2.0 / MIT | Via D-Bus (separate daemons) | None for SudoRGB's code |
| SignalRGB | Proprietary | Device catalogue used only as a coverage target | No code or data copied |
| Logo (RGB Tux) | Derived from Tux by Larry Ewing | App icon | Credit "Larry Ewing and The GIMP" |

**Open decision for the owner:** SudoRGB itself has no `LICENSE` file yet. Choose before public release.
A proprietary or open-core model is compatible with the boundaries above if GPL components stay in
separate processes (and the Python Kraken service is published under GPL-3.0).
