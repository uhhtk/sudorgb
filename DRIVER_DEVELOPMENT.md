# Adding device support

## 1. Check whether it already works

Open **Devices**. If a backend lists the device as supported, there is nothing to write. If it's
**known** (e.g. only liquidctl or OpenRazer knows it), the fix is a bridge (ROADMAP stage 3), not a new
driver. If OpenRGB lacks it, consider contributing upstream to OpenRGB first: every Linux user
benefits, and SudoRGB picks it up automatically.

## 2. Native driver (when no Linux driver exists)

Reference implementation: `src/devices/NativeDevices.{h,cpp}` (Glorious Model O 2 family).

1. **Protocol facts.** Use documented protocols, your own USB captures (USBPcap/Wireshark on Windows,
   `usbmon` on Linux), or *facts* from open projects. Never copy code from projects whose license is
   incompatible, and never use proprietary SDKs or SignalRGB plugins.
2. **Identify the control interface.** For HID devices, find the hidraw node whose report descriptor
   contains the vendor usage page and report id you write to (see `NativeDevices::rescan`). Match
   product ids explicitly: many vendors share a VID (e.g. PixArt `093a`).
3. **Pure payload builder.** One function that turns (effect, brightness, speed, colours) into packets,
   with no I/O, plus a unit test in `tests/tst_orkc.cpp` that pins every byte.
4. **Capabilities as `orgb::Mode`s.** Only set flags (`HasSpeed`, `HasBrightness`, colours) for
   controls you verified the hardware honours. A dead slider is a bug.
5. **Pacing.** Coalesce writes (latest value wins) and respect the device: wireless devices often store
   settings in flash and stall while doing so (Glorious: 120 ms between fragments, ≥ 1 s between
   commands). Never write in a tight loop.
6. **Permissions.** Add the ids to a `packaging/70-orkc-*.rules` file (`TAG+="uaccess"`, scoped to the
   exact product ids). The compatibility registry then marks the device "SudoRGB" automatically.
7. **Docs.** Add the device to `DEVICE_SUPPORT.md` as *implemented, awaiting verification* until
   someone has run it on the real hardware.

## 3. Python service drivers (coolers)

Devices that need liquidctl (cooling curves, LCDs) go in `service/`. Rules:
* The service is the only process that opens the device; check `conflicts.scan()` before every open.
* Requests are JSON lines with ids; replies within the C++ timeout (8 s, LCD 180 s).
* Media/heavy work runs in a resource-limited subprocess (`media.py`).
* Tests in `service/tests/` use fake backends and a fake clock; never touch real hardware in tests.

## 4. Safety rules (non-negotiable)

* No writes to motherboard SMBus/EC, RAM SPD, GPU I²C or firmware from SudoRGB code. Those go through
  OpenRGB's vetted drivers only.
* No firmware updates, no flash writes outside the device's normal settings command.
* Anything that could damage hardware needs an explicit, per-action user confirmation in the UI.
* Hardware tests are run by a person watching the device; automated tests use mocks.

## 5. Verification levels

Update `DEVICE_SUPPORT.md` honestly: *hardware-tested* means a person ran that exact model and saw each
listed feature work.
