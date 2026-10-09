# Changelog

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
