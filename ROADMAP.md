# SudoRGB roadmap

Priorities follow: users who benefit × protocol availability × reliability × reuse across device
families. Hardware the maintainers own gets no special priority.

## Stage 0: foundation (done, 1.0)
* Qt 6 / QML app, OpenRGB SDK client, Kraken service (LCD, cooling, firmware effects), profiles,
  atomic XDG persistence, native Glorious Model O 2 driver.

## Stage 1: know what's connected (done, 1.1)
* Runtime compatibility registry from installed backends' udev rules + usb.ids + sysfs.
* **Devices** page: per-device driver/backend, permission problems, unrecognized hardware.
* Diagnostics export with HID report descriptors for unrecognized devices.
* Multi-distribution `install.sh`; explicit Qt ≥ 6.8 requirement.
* Docs: architecture, device support, driver development, distribution support, changelog.

## Stage 2: driver architecture (next)
1. `IDriver` interface (probe/open/capabilities/apply/close) + registry; move Glorious into it.
2. Per-driver worker thread with watchdog; per-device error state shown on the Devices page.
3. udev netlink hot-plug (libudev, LGPL-2.1) replacing the 4 s sysfs poll and the native 5 s rescan.
4. Unit tests with mock HID transport for every native driver.

## Stage 3: more hardware through existing Linux projects
1. **OpenRazer bridge** (D-Bus): Razer keyboards/mice/headsets/mats, the biggest gap vs SignalRGB.
2. **ratbagd bridge** (D-Bus): Logitech/SteelSeries/Glorious mice DPI + LEDs where OpenRGB lacks them.
3. **liquidctl general devices** in the Python service: Corsair Commander/Hydro, NZXT Smart Device/
   RGB & Fan controllers, ASUS Ryujin, cooling + lighting where OpenRGB doesn't already cover them.
4. Contribute missing devices upstream to OpenRGB where possible; it benefits every Linux user.

## Stage 4: lighting engine
* Layout canvas (device placement, per-LED coordinates) → synchronised cross-device effects.
* Effects library: wave, gradient, ripple/reactive (key events via libinput), custom user effects.
* Audio-reactive (PipeWire capture, FFT) and screen sync (xdg-desktop-portal ScreenCast + PipeWire),
  both working on Wayland and X11.
* Frame scheduler that respects per-device limits (e.g. Kraken firmware: no streaming; wireless mice:
  ≥ 1 s between commands).

## Stage 5: distribution
* AppImage (linuxdeploy-qt), Flatpak (needs host udev rules: first-run polkit helper), AUR PKGBUILD,
  .deb/.rpm via CPack, CI builds on Arch, Fedora, Ubuntu 25.04, Debian 13, openSUSE Tumbleweed.
* Minimal polkit helper limited to installing SudoRGB's own udev rules (never run the GUI as root).

## Stage 6: automation
* Per-application profile switching (Hyprland/KWin/GNOME IPC where available), schedules, idle/lock.

## Stage 7: product readiness
* Owner decides the license (see ARCHITECTURE.md → Licenses). Plugin SDK for third-party drivers,
  opt-in crash reports, signed releases, localisation.

## Known open issues
* Kraken 2024 Elite: ~1 s firmware latency for static colours (test firmware "Fixed" effect).
* Glorious wireless: Static effect stalls cursor reports (capture Glorious CORE traffic to compare).
* OpenRGB 1.0rc3 protocol v5 features (zone flags) not used yet; SudoRGB negotiates v4.
