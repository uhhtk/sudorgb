# Changelog

## 1.2.0 (2026-10-09)

### Added
* **All liquidctl AIOs and fan controllers** (not just the Kraken): Corsair Hydro / Platinum / Pro XT /
  iCUE ELITE CAPELLIX (Commander Core / ST), NZXT Kraken X / M22 / Smart Device / Control Hub, EVGA CLC,
  ASUS Ryujin II / Ryuo, MSI MPG Coreliquid, Lian Li Galahad II LCD / Uni hubs, Aquacomputer.
  Live pump/fan readouts plus per-channel curve or fixed duty on the Cooling page. Curves run in
  the cooler's firmware where supported; otherwise SudoRGB applies them every 2 s.
  Pumps never go below 20%.
* **LCD support is per device, not Kraken-only.** The LCD page has a screen picker. New screens:
  * Corsair iCUE ELITE LCD (the Elite Capellix LCD cap), NAUTILUS LCD, XC7 ELITE LCD, and iCUE LINK
    AIO / XD5 LCD: stills plus GIF, animated WebP and APNG, streamed frame by frame.
  * MSI Coreliquid K360: still images.
* Media pipeline: JPEG frame-sequence output (any still or animated format → 480×480 frames + timing).
* Cooler service (`service/cooler_service.py`): an isolated process with the same ownership rule as
  the Kraken: it never opens a device another program holds.
* udev rule `70-orkc-corsair-lcd.rules`.

### Changed
* The Kraken's conflict scan now covers only the Kraken models, so an NZXT hub held by another program
  no longer blocks the Kraken.
* The Devices page counts liquidctl coolers as supported (power supplies excluded).

## 1.1.0 (2026-10-09)

### Added
* **Devices** page: every connected USB device with the driver/backend that can control it, status
  (supported / needs permission / known to another backend / unrecognized), hidraw nodes, hot-plug
  rescans while visible.
* Runtime compatibility registry built from installed backends' udev rules (OpenRGB, liquidctl,
  OpenRazer, SudoRGB) and hwdata `usb.ids`.
* Diagnostics export (`~/sudorgb-diagnostics-*.txt`) with system info, backends and HID report
  descriptors of unrecognized devices.
* `install.sh` for pacman, apt, dnf and zypper distributions; refuses ostree systems with guidance.
* Docs: ARCHITECTURE, DEVICE_SUPPORT, DRIVER_DEVELOPMENT, DISTRIBUTION_SUPPORT, ROADMAP.

### Changed
* Qt 6.8 is now an explicit minimum (it was already needed in practice).

## 1.0.x (2026-10-07 → 10-09)

### Added
* SudoRGB name and RGB Tux logo; "Graphite" UI with ember accent.
* Native Glorious Model O 2 / O 2 Mini / I 2 lighting driver (9 effects, brightness) with udev rule.
* Kraken firmware lighting effects (Rainbow Flow, Breathing, Fading) instead of streamed frames.
* Software rainbow/breathing for OpenRGB devices that only expose Direct mode.

### Fixed
* Kraken GIF crash: media pipeline re-encodes with one palette + transparency deltas (16.8 MB → 2.2 MB).
* Kraken ownership: never opens the cooler while another program holds it.
* OpenRGB client heap corruption on exit.
* Kraken colour drags no longer queue frames; disk save debounced.
* "All devices → Rainbow" no longer skips the Kraken while it is reconnecting.
* Glorious wireless: fragments spaced 120 ms, commands ≥ 1 s apart; brightness written to the byte
  the firmware honours.
* A fresh install no longer auto-applies a profile the user never chose.
