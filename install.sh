#!/usr/bin/env bash
# SudoRGB one-step install: detects your distribution, installs build/runtime
# dependencies, builds, installs to /usr/local and reloads udev rules.
# Re-run after `git pull` to update. Needs Qt 6.8 or newer.
set -euo pipefail
cd "$(dirname "$0")"

if [ -e /run/ostree-booted ]; then
    echo "Immutable (rpm-ostree) system detected (e.g. Bazzite, Silverblue)."
    echo "Build inside a distrobox (Fedora image), or wait for the Flatpak/AppImage (see DISTRIBUTION_SUPPORT.md)."
    exit 1
fi

. /etc/os-release
need_openrgb_hint=0
if command -v pacman >/dev/null; then          # Arch, Omarchy, EndeavourOS, Manjaro, CachyOS
    sudo pacman -S --needed cmake ninja gcc qt6-base qt6-declarative qt6-shadertools qt6-wayland \
        openrgb liquidctl python-pillow
elif command -v apt-get >/dev/null; then       # Debian, Ubuntu, Mint, Pop!_OS
    sudo apt-get update
    sudo apt-get install -y cmake ninja-build g++ qt6-base-dev qt6-declarative-dev qt6-shadertools-dev \
        qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-shapes \
        qml6-module-qtquick-dialogs qml6-module-qtquick-window qml6-module-qtquick-templates \
        qml6-module-qtqml-workerscript qml6-module-qtcore qt6-wayland python3-pil liquidctl || true
    need_openrgb_hint=1                         # OpenRGB is not in Debian/Ubuntu repositories
elif command -v dnf >/dev/null; then           # Fedora, Nobara
    sudo dnf install -y cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtdeclarative-devel \
        qt6-qtshadertools-devel qt6-qtwayland python3-pillow liquidctl openrgb
elif command -v zypper >/dev/null; then        # openSUSE
    sudo zypper install -y cmake ninja gcc-c++ qt6-base-devel qt6-declarative-devel qt6-shadertools-devel \
        qt6-wayland python3-Pillow liquidctl OpenRGB
else
    echo "Unknown package manager (${ID:-?}). Install: CMake, Ninja, a C++20 compiler, Qt >= 6.8 (base, declarative,"
    echo "shadertools, wayland), Python 3 + Pillow, liquidctl, OpenRGB. Then re-run this script."
    read -rp "Continue with the build anyway? [y/N] " a; [ "${a:-n}" = y ] || exit 1
fi

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DORKC_BUILD_TESTS=OFF
cmake --build build
sudo cmake --install build
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=hidraw
echo
echo "Done. Open SudoRGB from your app launcher."
if [ "$need_openrgb_hint" = 1 ] && ! command -v openrgb >/dev/null; then
    echo "Note: install OpenRGB (https://openrgb.org/releases.html, .deb or AppImage) for keyboard/RAM/GPU/fan lighting."
fi
