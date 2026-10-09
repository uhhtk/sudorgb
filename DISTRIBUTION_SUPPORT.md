# Distribution support

SudoRGB's code is distribution-independent: Qt 6.8+, sysfs, udev `uaccess` rules, XDG directories,
Wayland or X11 (Qt picks the platform; the Wayland app id is `orkc`). The GUI never runs as root.

| Distribution | Qt ≥ 6.8 in repos | OpenRGB in repos | `install.sh` | Status |
|---|---|---|---|---|
| Arch Linux, Omarchy | ✅ | ✅ | pacman | ✅ Tested (maintainer machine) |
| EndeavourOS, Manjaro, CachyOS | ✅ | ✅ | pacman | 🧪 Expected to work, untested |
| Fedora 41+, Nobara | ✅ | ✅ | dnf | 🧪 Untested |
| openSUSE Tumbleweed | ✅ | ✅ | zypper | 🧪 Untested |
| Debian 13 (trixie) | ✅ 6.8 | ❌ (openrgb.org .deb) | apt | 🧪 Untested |
| Ubuntu 25.04+ | ✅ | ❌ (openrgb.org .deb) | apt | 🧪 Untested |
| Ubuntu 24.04, Linux Mint 22, Pop!_OS 24.04 | ❌ Qt 6.4 | ❌ | — | ⛔ Needs AppImage/Flatpak (ROADMAP stage 5) |
| Bazzite, Fedora Silverblue/Kinoite | ✅ in a distrobox | — | refuses (ostree) | 🗓 Flatpak planned; distrobox works for building |

## Permissions without root

* Each backend ships udev rules with `TAG+="uaccess"`: systemd-logind grants the active local user
  access to matching device nodes. SudoRGB installs `70-orkc-glorious.rules` for its native driver.
* After installing rules: `sudo udevadm control --reload && sudo udevadm trigger --subsystem-match=hidraw`
  (install.sh does this), or replug the device.
* `uaccess` follows the *active seat*: SSH-only sessions don't get access.
* The **Devices** page flags "needs permission" whenever a driver exists but the node isn't writable.

## Packaging notes

* **AppImage**: bundle Qt; udev rules still need one privileged install step (planned polkit helper).
* **Flatpak**: needs `--device=all` for hidraw; udev rules can't be installed from inside the sandbox,
  so a host-side helper or documentation step is required. OpenRGB stays a separate host app reachable
  on `127.0.0.1:6742`.
* **AUR / .deb / .rpm**: install rules to the distro's udev directory (`/usr/lib/udev/rules.d`); the
  current CMake install uses `/usr/local/lib/udev/rules.d`, which systemd-udevd also reads.

## Runtime dependencies

| Need | Package (Arch name) | Optional? |
|---|---|---|
| Qt ≥ 6.8 base, declarative, wayland | qt6-base, qt6-declarative, qt6-wayland | Required |
| OpenRGB (most RGB devices) | openrgb | Strongly recommended |
| NZXT Kraken coolers | python, python-pillow, liquidctl | Only for Kraken |
| hwdata `usb.ids` (device names) | hwdata | Recommended |
