#!/usr/bin/env bash
# SudoRGB one-step install for Arch / Omarchy. Re-run after `git pull` to update.
set -euo pipefail
cd "$(dirname "$0")"
sudo pacman -S --needed cmake ninja qt6-base qt6-declarative qt6-shadertools qt6-wayland openrgb liquidctl python-pillow
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DORKC_BUILD_TESTS=OFF
cmake --build build
sudo cmake --install build
sudo udevadm control --reload
sudo udevadm trigger --subsystem-match=hidraw
echo "Done. Open SudoRGB from your app launcher."
