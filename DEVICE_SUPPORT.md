# Device support

SudoRGB never claims support it has not verified. Status levels:

| Level | Meaning |
|---|---|
| ✅ **Hardware-tested** | Run on the real device by a maintainer; features listed were seen working |
| 🧪 **Implemented, awaiting verification** | Code path exists and is unit-tested, not yet run on that exact device |
| 🟨 **Partial** | Works with known limitations (listed) |
| 🔌 **Via backend** | Driven through OpenRGB; quality depends on OpenRGB's driver for it |
| 🗓 **Planned** | On the roadmap, not implemented |
| ⛔ **Unsupported / blocked** | No usable protocol, or a hardware/firmware limit |

Your own machine: open **Devices** in SudoRGB. It shows, for each connected device, which installed
driver can control it, and lets you export a diagnostics file for anything unrecognized.

## Hardware-tested

| Device | USB id | Path | Tested features | Limits |
|---|---|---|---|---|
| NZXT Kraken 2024 Elite RGB | 1e71:3012 | SudoRGB Kraken service (liquidctl + OpenKraken driver) | Liquid temp, pump/fan RPM & duty, curves and fixed duty, LCD image / GIF / liquid / sensor screens, ring + fan static colours. Firmware effects (Spectrum/Rainbow Flow/Super Rainbow) seen working via OpenRGB with the same packet; SudoRGB's own effect path is unit/IPC-tested, visual confirmation pending | Firmware (1.2.0) shows a new static colour ~1 s after the last frame; streamed animations freeze, so animations use firmware effects |
| Glorious Model O 2 Mini Wireless (receiver) | 093a:826d | SudoRGB native | All 9 effects, master brightness | 🟨 On wireless, the firmware's *Static* effect causes cursor report stalls (measured 13–52 per 15 s vs 1 with lights off); Glorious CORE on Windows does not, so its packet differs. Under investigation. Speed byte has no effect on this firmware |
| Glorious Model O 2 Mini (wired) | 093a:826a | SudoRGB native | All effects | — |
| Corsair Vengeance RGB DDR5 | SMBus | 🔌 OpenRGB | Direct colours, hardware Rainbow | — |
| SteelSeries Apex Pro TKL Gen 3 | 1038:1642 | 🔌 OpenRGB | Direct colours | OpenRGB exposes only Direct; SudoRGB software rainbow/breathing tested against OpenRGB debug devices, not yet seen on this keyboard |
| ASUS ROG STRIX B650-A Aura | 0b05:19af | 🔌 OpenRGB | Direct colours, hardware Rainbow/Spectrum | — |
| Corsair iCUE Commander Core | 1b1c:0c1c | 🔌 OpenRGB | Direct colours | Same as above: software effects pending visual confirmation |

## Implemented, awaiting verification

| Device | USB id | Path |
|---|---|---|
| Glorious Model O 2 Wireless (receiver / wired) | 093a:822d / 093a:822a | SudoRGB native |
| Glorious Model O 2 (Bluetooth) | 093a:822b | SudoRGB native (may not expose the vendor interface over BT) |
| Glorious Model I 2 Wireless | 093a:821d | SudoRGB native |
| NZXT Kraken Z53 / Z63 / Z73 | 1e71:3008 | Kraken service (LCD 320×320) |
| NZXT Kraken 2023 / 2023 Elite | 1e71:300e / 1e71:300c | Kraken service (300c: liquidctl marks it "broken", bulk LCD may be unavailable) |
| NZXT Kraken 2024 Plus | 1e71:3014 | Kraken service |

## Via OpenRGB

Everything OpenRGB supports appears in SudoRGB automatically. On a system with OpenRGB 1.0rc3 that is
**1,338 USB devices from 76 vendors**, plus SMBus RAM, motherboards and GPUs, and network lights
(Philips Hue, Nanoleaf, configured inside OpenRGB). See https://openrgb.org/devices.html.
SudoRGB adds software rainbow/breathing for devices whose OpenRGB driver only offers Direct mode.

## Coverage target

SignalRGB's public catalogue (October 2026) lists ~1,470 product families: keyboards 342 (+23 QMK),
GPUs 211, mice 192, fans 132, AIOs 94, lighting controllers 69, Wi-Fi lights 67, RAM 37, mainboards
61, headphones 46, mousepads 30, LED strips 30, cases 25, monitors 19, speakers 14, LCDs 11,
microphones 7, laptops 9, misc 53. Use it as a target list only: its drivers are Windows plugins and
proprietary.

## Planned

| Area | Approach |
|---|---|
| Razer (most models) | Bridge to OpenRazer daemon over D-Bus |
| Logitech mice (G-series) | Bridge to ratbagd (libratbag) over D-Bus; OpenRGB already covers many |
| liquidctl-only devices (Corsair/NZXT/ASUS coolers & hubs) | Extend the Python service beyond Kraken |
| Kraken static-colour latency | Test firmware "Fixed" effect (2A 04 mode 0) as a faster static path |
| Glorious wireless static stall | Capture Glorious CORE traffic (USBPcap) and match it |

## Unsupported / blocked

* Devices with no public protocol and no Linux driver anywhere (report them via **Devices → Export
  diagnostics**; the file contains HID report descriptors that help driver authors).
* Lighting that only works through a vendor cloud account.
* Writes to motherboard SMBus/EC, RAM SPD, or firmware outside OpenRGB's vetted drivers: SudoRGB will
  not do this itself, because mistakes can brick hardware.
