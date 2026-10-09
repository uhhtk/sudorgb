"""Hardware backends for the Kraken service.

``OpenKrakenBackend`` uses the bundled ``kraken_driver.device.KrakenDevice``
(from OpenKraken, MIT; see kraken_driver/LICENSE.OpenKraken). That class is the battle-tested
liquidctl wrapper for the Kraken 2023/2024 (Elite) family: bucket-ring LCD
streaming, HID desync recovery, bulk-interface release and native HUE2 LED
frames (liquidctl has no colour channels for these models). Importing it never
touches hardware and never imports PyQt.

``LiquidctlBackend`` is the fallback if the bundled driver fails to load. It
covers telemetry, cooling and LCD through plain liquidctl, without ring
lighting.

``MockBackend`` simulates a device for UI development. It is selected only
explicitly (``--mock``) and every payload it produces is flagged
``simulated: true``.

All backends are used from a single worker thread; none are thread-safe by
themselves beyond what KrakenDevice provides.
"""

from __future__ import annotations

import logging
import math
import os
import sys
import threading
import time
from pathlib import Path

log = logging.getLogger("orkc.backend")


PUMP_MIN = 20


class BackendError(Exception):
    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code


# --------------------------------------------------------------------------- #
# Effects (host-side; the 2023/2024 firmware rejects hardware effect modes)
# --------------------------------------------------------------------------- #
EFFECTS = {
    # key: (label, min_colors, max_colors, animated)
    "off": ("Off", 0, 0, False),
    "fixed": ("Static", 1, 1, False),
    "gradient": ("Gradient", 2, 4, False),
    "breathing": ("Breathing", 1, 4, True),
    "cycle": ("Color cycle", 2, 8, True),
    "spectrum": ("Spectrum", 0, 0, True),
}
SPEED_PERIODS = {"slow": 12.0, "normal": 6.0, "fast": 3.0}


def _hsv(h: float, s: float, v: float) -> tuple[int, int, int]:
    i = int(h * 6) % 6
    f = h * 6 - int(h * 6)
    p, q, t = v * (1 - s), v * (1 - f * s), v * (1 - (1 - f) * s)
    r, g, b = [(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)][i]
    return int(r * 255), int(g * 255), int(b * 255)


def _lerp(a, b, f):
    return tuple(int(a[i] + (b[i] - a[i]) * f) for i in range(3))


# Firmware-run effects (HUE2 "2023" effect packet 2A 04, as OpenRGB's
# SendEffect2023). On the Kraken 2024 Elite (fw 1.2.0) the firmware only shows a
# streamed Direct frame after ~1 s without new frames, so streamed animations
# freeze; these effects run inside the cooler instead. Hardware-verified.
# our effect -> (HUE2 mode, speed table [slowest..fastest] as (lo, hi), modifier)
HW_EFFECTS = {
    "spectrum": (0x0B, [(0x5E, 0x01), (0x2C, 0x01), (0xFA, 0x00), (0x96, 0x00), (0x50, 0x00)], 0x00),  # Rainbow Flow
    "breathing": (0x07, [(0x28, 0x00), (0x1E, 0x00), (0x14, 0x00), (0x0A, 0x00), (0x04, 0x00)], 0x08),
    "cycle": (0x01, [(0x50, 0x00), (0x3C, 0x00), (0x28, 0x00), (0x14, 0x00), (0x0A, 0x00)], 0x08),  # Fading
}
_SPEED_INDEX = {"slow": 1, "normal": 2, "fast": 3}


def effect_packet(mask: int, effect: str, colors: list, brightness: int, speed: str) -> list[int]:
    """64-byte 2A 04 packet. Colours are GRB, brightness applied host-side."""
    mode, speeds, modifier = HW_EFFECTS[effect]
    lo, hi = speeds[_SPEED_INDEX.get(speed, 2)]
    k = max(0, min(100, int(brightness))) / 100.0
    cols = [tuple(int(c * k) for c in rgb) for rgb in colors[:16]]
    pkt = [0x2A, 0x04, mask, mask, mode, lo, hi] + [0] * 57
    for i, (r, g, b) in enumerate(cols):
        pkt[7 + i * 3 : 10 + i * 3] = [g, r, b]
    pkt[0x37] = 0x00  # direction
    pkt[0x38] = len(cols)
    pkt[0x39] = modifier
    pkt[0x3A] = 0x08
    pkt[0x3B] = 0x03
    return pkt


def effect_frame(mode: str, colors: list, brightness: int, count: int, t: float, speed: str) -> list:
    """One frame of ``count`` RGB triplets with brightness applied host-side.

    Animated modes are slow and smooth on purpose: the Kraken only accepts
    Direct frames reliably at about 1 Hz (OpenRGB #4828).
    """
    period = SPEED_PERIODS.get(speed, 6.0)
    cols = [tuple(c) for c in colors] or [(255, 122, 41)]
    if mode == "off" or count <= 0:
        out = [(0, 0, 0)] * max(count, 0)
    elif mode == "fixed":
        out = [cols[0]] * count
    elif mode == "gradient":
        out = []
        for i in range(count):
            pos = i / max(count - 1, 1) * (len(cols) - 1)
            j = min(int(pos), len(cols) - 2) if len(cols) > 1 else 0
            out.append(_lerp(cols[j], cols[min(j + 1, len(cols) - 1)], pos - j))
    elif mode == "breathing":
        phase = (t / period) % len(cols)
        base = cols[int(phase)]
        level = 0.15 + 0.85 * (0.5 - 0.5 * math.cos(2 * math.pi * (phase % 1)))
        out = [tuple(int(c * level) for c in base)] * count
    elif mode == "cycle":
        phase = (t / period) % len(cols)
        j = int(phase)
        out = [_lerp(cols[j], cols[(j + 1) % len(cols)], phase - j)] * count
    elif mode == "spectrum":
        shift = (t / period) % 1.0
        out = [_hsv((i / max(count, 1) + shift) % 1.0, 1.0, 1.0) for i in range(count)]
    else:
        raise BackendError("bad_request", f"Unknown lighting mode {mode!r}")
    k = max(0, min(100, int(brightness))) / 100.0
    return [tuple(max(0, min(255, int(c * k))) for c in px) for px in out]


# --------------------------------------------------------------------------- #
# Common helpers
# --------------------------------------------------------------------------- #
def normalize_curve(points: list) -> list[tuple[float, int]]:
    """Validate a duty curve: sorted by temperature, monotone duty, 100% at 60 °C.

    Liquid above ~60 °C is outside safe operation for these AIOs, so the curve is
    always forced to full duty there regardless of what the profile says.
    """
    pts = []
    for p in points:
        temp, duty = float(p[0]), int(round(float(p[1])))
        if not (0 <= temp <= 100 and 0 <= duty <= 100):
            raise BackendError("bad_request", f"Curve point out of range: {p}")
        pts.append((temp, duty))
    if len(pts) < 2:
        raise BackendError("bad_request", "A curve needs at least two points")
    pts.sort()
    mono, top = [], 0
    for temp, duty in pts:
        top = max(top, duty)
        mono.append((temp, top))
    mono = [p for p in mono if p[0] < 60] + [(60.0, 100)]
    return mono


class _Sensors:
    """Small hwmon reader for the LCD sensor screens (CPU/GPU temperature)."""

    def __init__(self) -> None:
        self.cpu = self._find(("k10temp", "zenpower", "coretemp"), ("Tctl", "Tdie", "Package id 0"))
        self.gpu = self._find(("amdgpu",), ("edge", "junction"))

    @staticmethod
    def _find(names, labels):
        for hw in sorted(Path("/sys/class/hwmon").glob("hwmon*")):
            try:
                if (hw / "name").read_text().strip() not in names:
                    continue
            except OSError:
                continue
            inputs = sorted(hw.glob("temp*_input"))
            for lab in labels:
                for inp in inputs:
                    try:
                        if (hw / inp.name.replace("_input", "_label")).read_text().strip() == lab:
                            return inp
                    except OSError:
                        pass
            if inputs:
                return inputs[0]
        return None

    @staticmethod
    def _read(path):
        try:
            return int(path.read_text()) / 1000.0 if path else None
        except (OSError, ValueError):
            return None

    def read(self) -> dict:
        return {"cpu_temp": self._read(self.cpu), "gpu_temp": self._read(self.gpu)}


# --------------------------------------------------------------------------- #
# Backends
# --------------------------------------------------------------------------- #
class Backend:
    name = "abstract"
    simulated = False
    hw_effects = False  # True: animated effects run in the cooler (write_effect)
    capabilities: dict = {}

    def write_effect(self, channel: str, effect: str, colors: list, brightness: int, speed: str) -> bool:
        return False

    def connect(self) -> bool: ...
    def disconnect(self) -> None: ...
    def is_connected(self) -> bool: ...
    def info(self) -> dict: ...
    def status(self) -> dict: ...
    def led_count(self, channel: str) -> int: ...
    def write_lighting(self, channel: str, colors: list) -> bool: ...
    def set_fixed(self, channel: str, duty: int) -> bool: ...
    def set_curve(self, channel: str, points: list) -> bool: ...
    def lcd_brightness(self, value: int) -> bool: ...
    def lcd_orientation(self, deg: int) -> bool: ...
    def lcd_liquid(self) -> bool: ...
    def lcd_static(self, path: str) -> bool: ...
    def lcd_gif(self, path: str) -> bool: ...
    def lcd_sensor_frame(self, path: str) -> bool: ...
    def lcd_clear(self) -> bool: ...
    def lcd_resolution(self) -> tuple[int, int]: ...
    def lcd_bulk_unavailable(self) -> bool:
        return False

    def render_sensor_screen(self, style: str, status: dict, ring_color, path: str) -> str | None:
        return None


class OpenKrakenBackend(Backend):
    """Reuses OpenKraken's KrakenDevice (no GUI, no PyQt)."""

    name = "kraken-driver"

    def __init__(self) -> None:
        from kraken_driver import device as okdev

        self._okdev = okdev
        self._version = "0.4.1"
        self._kd = okdev.KrakenDevice()
        self._render = None
        try:
            from kraken_driver import lcd_render

            self._render = lcd_render
        except Exception:  # Pillow font issues etc. -- sensors screen just becomes unavailable
            log.warning("OpenKraken lcd_render unavailable; sensor screens disabled", exc_info=True)
        self._sensors = _Sensors()
        self.capabilities = {
            "lighting": ["ring", "fans"],
            "cooling": ["pump", "fan"],
            "lcd": True,
            "lcd_modes": ["liquid", "image", "gif", "off"] + (["sensors"] if self._render else []),
            "sensor_styles": ["liquid_ring", "cpu_gpu", "triple"] if self._render else [],
            "effects": {k: {"label": v[0], "min_colors": v[1], "max_colors": v[2], "animated": v[3]} for k, v in EFFECTS.items()},
            "pump_duty_min": PUMP_MIN,
        }

    @property
    def backend_label(self) -> str:
        return f"Bundled Kraken driver (from OpenKraken {self._version}, MIT) + liquidctl"

    def connect(self) -> bool:
        ok = self._kd.connect()
        if ok:
            self._install_gif_passthrough()
        return ok

    def _install_gif_passthrough(self) -> None:
        """Make liquidctl upload our pre-processed GIF bytes verbatim.

        liquidctl's ``_prepare_gif_file`` re-decodes and re-encodes the GIF (the
        16 MB blow-up described in media.py). Files produced by our media
        pipeline are already at LCD resolution and orientation, so for those we
        hand the bytes over untouched; anything else goes through the original.
        """
        dev = getattr(self._kd, "_dev", None)
        if dev is None or getattr(dev, "_orkc_passthrough", False):
            return
        original = dev._prepare_gif_file
        cache_root = os.environ.get("ORKC_MEDIA_CACHE", "")

        def prepare(path, rotation):
            p = str(path)
            if cache_root and p.startswith(cache_root) and os.path.basename(p).startswith("gif-"):
                with open(p, "rb") as f:
                    return f.read()
            return original(path, rotation)

        dev._prepare_gif_file = prepare
        dev._orkc_passthrough = True

    def disconnect(self) -> None:
        self._kd.disconnect()

    def is_connected(self) -> bool:
        return self._kd.is_connected

    def lcd_resolution(self):
        dev = getattr(self._kd, "_dev", None)
        res = getattr(dev, "lcd_resolution", None) if dev else None
        return tuple(res) if res else (640, 640)

    def info(self) -> dict:
        li = self._kd.lighting_info
        return {
            "description": self._kd.description,
            "firmware": self._kd.firmware_version,
            "lcd_resolution": list(self.lcd_resolution()),
            "lcd_brightness": self._kd.lcd_brightness,
            "lcd_orientation": self._kd.lcd_orientation,
            "led_counts": {c: self._kd.led_count_for(c) for c in ("ring", "fans")},
            "accessories": ({k: [f"0x{a:02X}" for a in v] for k, v in li.accessories.items()} if li else {}),
        }

    def status(self) -> dict:
        s = self._kd.get_status()
        return {
            "connected": s.connected,
            "liquid_temp": s.liquid_temp,
            "pump_rpm": s.pump_rpm,
            "pump_duty": s.pump_duty,
            "fan_rpm": s.fan_rpm,
            "fan_duty": s.fan_duty,
        }

    def led_count(self, channel):
        return self._kd.led_count_for(channel)

    hw_effects = True

    def write_lighting(self, channel, colors):
        return self._kd.write_lighting_frame(channel, colors)

    def write_effect(self, channel, effect, colors, brightness, speed):
        mask = self._okdev.LIGHTING_CHANNELS.get(channel)
        if mask is None:
            return False
        pkt = effect_packet(mask, effect, colors, brightness, speed)
        with self._kd._lock:
            if not self._kd.is_connected:
                return False
            try:
                self._kd._lighting_write(pkt)
                return True
            except Exception:
                log.exception("effect write failed; dropping device")
                self._kd._mark_disconnected()
                return False

    def set_fixed(self, channel, duty):
        return self._kd.set_fixed_speed(channel, duty)

    def set_curve(self, channel, points):
        return self._kd.set_speed_profile(channel, points)

    def lcd_brightness(self, value):
        return self._kd.set_lcd_brightness(value)

    def lcd_orientation(self, deg):
        return self._kd.set_lcd_orientation(deg)

    def lcd_liquid(self):
        return self._kd.set_lcd_liquid_mode()

    def lcd_static(self, path):
        return self._kd.set_lcd_static(path)

    def lcd_gif(self, path):
        return self._kd.set_lcd_gif(path)

    def lcd_sensor_frame(self, path):
        return self._kd.set_lcd_sensor_frame(path)

    def lcd_clear(self):
        return self._kd.clear_lcd_media()

    def lcd_bulk_unavailable(self):
        return self._kd.lcd_bulk_unavailable

    def render_sensor_screen(self, style, status, ring_color, path):
        if not self._render:
            return None
        sys_ = self._sensors.read()
        data = self._render.LcdData(
            liquid_temp=status.get("liquid_temp"),
            cpu_temp=sys_["cpu_temp"],
            cpu_load=None,
            gpu_temp=sys_["gpu_temp"],
            gpu_load=None,
            pump_rpm=status.get("pump_rpm"),
            fan_rpm=status.get("fan_rpm"),
            ring_color=tuple(ring_color),
        )
        img = self._render.render(style, data)
        w, h = self.lcd_resolution()
        if img.size != (w, h):
            img = img.resize((w, h))
        tmp = path + ".tmp.png"
        img.save(tmp, format="PNG")
        os.replace(tmp, path)
        return path


class LiquidctlBackend(Backend):
    """Plain liquidctl fallback: telemetry, cooling and LCD; no ring lighting."""

    name = "liquidctl"

    def __init__(self) -> None:
        import liquidctl

        self._ver = getattr(liquidctl, "__version__", "?")
        self._dev = None
        self._lock = threading.RLock()
        self._brightness = 50
        self._orientation = 0
        self.capabilities = {
            "lighting": [],
            "cooling": ["pump", "fan"],
            "lcd": True,
            "lcd_modes": ["liquid", "image", "gif", "off"],
            "sensor_styles": [],
            "effects": {},
            "pump_duty_min": PUMP_MIN,
        }

    @property
    def backend_label(self) -> str:
        return f"liquidctl {self._ver} (bundled driver unavailable: no ring lighting)"

    def connect(self):
        from liquidctl import find_liquidctl_devices
        from liquidctl.driver.kraken3 import KrakenZ3

        with self._lock:
            if self._dev:
                return True
            for d in find_liquidctl_devices():
                if isinstance(d, KrakenZ3) and getattr(d, "vendor_id", 0) == 0x1E71:
                    try:
                        d.connect()
                        d.initialize()
                        self._dev = d
                        return True
                    except Exception:
                        log.exception("liquidctl connect failed")
                        self._release(d)
            return False

    @staticmethod
    def _release(d):
        try:
            d.disconnect()
        except Exception:
            pass
        bulk = getattr(d, "bulk_device", None)
        if bulk is not None and hasattr(bulk, "release"):
            try:
                bulk.release()
            except Exception:
                pass

    def disconnect(self):
        with self._lock:
            if self._dev:
                self._release(self._dev)
            self._dev = None

    def is_connected(self):
        return self._dev is not None

    def _call(self, fn, *a):
        with self._lock:
            if not self._dev:
                return False
            try:
                fn(*a)
                return True
            except (AssertionError, ValueError, FileNotFoundError) as exc:
                raise BackendError("device_rejected", str(exc)) from None
            except Exception:
                log.exception("liquidctl call failed; dropping device")
                self.disconnect()
                return False

    def lcd_resolution(self):
        return tuple(getattr(self._dev, "lcd_resolution", (640, 640)))

    def info(self):
        return {
            "description": getattr(self._dev, "description", ""),
            "firmware": "",
            "lcd_resolution": list(self.lcd_resolution()),
            "lcd_brightness": self._brightness,
            "lcd_orientation": self._orientation,
            "led_counts": {},
            "accessories": {},
        }

    def status(self):
        with self._lock:
            if not self._dev:
                return {"connected": False}
            try:
                raw = {k.lower(): v for k, v, _u in self._dev.get_status()}
            except Exception:
                log.exception("status failed")
                self.disconnect()
                return {"connected": False}

        def pick(frag, cast):
            for k, v in raw.items():
                if frag in k:
                    try:
                        return cast(v)
                    except (TypeError, ValueError):
                        return None
            return None

        return {
            "connected": True,
            "liquid_temp": pick("liquid temperature", float),
            "pump_rpm": pick("pump speed", int),
            "pump_duty": pick("pump duty", int),
            "fan_rpm": pick("fan speed", int),
            "fan_duty": pick("fan duty", int),
        }

    def led_count(self, channel):
        return 0

    def write_lighting(self, channel, colors):
        raise BackendError("unsupported", "Ring lighting needs OpenKraken's backend")

    def set_fixed(self, channel, duty):
        return self._call(self._dev.set_fixed_speed, channel, duty)

    def set_curve(self, channel, points):
        return self._call(self._dev.set_speed_profile, channel, points)

    def lcd_brightness(self, value):
        ok = self._call(self._dev.set_screen, "lcd", "brightness", value)
        self._brightness = value if ok else self._brightness
        return ok

    def lcd_orientation(self, deg):
        ok = self._call(self._dev.set_screen, "lcd", "orientation", deg)
        self._orientation = deg if ok else self._orientation
        return ok

    def lcd_liquid(self):
        return self._call(self._dev.set_screen, "lcd", "liquid", None)

    def lcd_static(self, path):
        return self._call(self._dev.set_screen, "lcd", "static", path)

    def lcd_gif(self, path):
        return self._call(self._dev.set_screen, "lcd", "gif", path)

    def lcd_sensor_frame(self, path):
        return False

    def lcd_clear(self):
        return self._call(self._dev._delete_all_buckets)


class MockBackend(Backend):
    """Simulated Kraken for UI development. Never chosen implicitly."""

    name = "mock"
    simulated = True
    backend_label = "Simulated device (--mock)"

    def __init__(self) -> None:
        self._connected = False
        self._t0 = time.monotonic()
        self._fixed = {"pump": 60, "fan": 40}
        self._bright = 70
        self._orient = 0
        self.capabilities = {
            "lighting": ["ring", "fans"],
            "cooling": ["pump", "fan"],
            "lcd": True,
            "lcd_modes": ["liquid", "image", "gif", "sensors", "off"],
            "sensor_styles": ["liquid_ring", "cpu_gpu", "triple"],
            "effects": {k: {"label": v[0], "min_colors": v[1], "max_colors": v[2], "animated": v[3]} for k, v in EFFECTS.items()},
            "pump_duty_min": PUMP_MIN,
        }

    def connect(self):
        self._connected = True
        return True

    def disconnect(self):
        self._connected = False

    def is_connected(self):
        return self._connected

    def lcd_resolution(self):
        return (640, 640)

    def info(self):
        return {
            "description": "NZXT Kraken 2024 Elite RGB (simulated)",
            "firmware": "2.0.0-sim",
            "lcd_resolution": [640, 640],
            "lcd_brightness": self._bright,
            "lcd_orientation": self._orient,
            "led_counts": {"ring": 24, "fans": 24},
            "accessories": {"ring": ["0x1E"], "fans": ["0x1B"]},
        }

    def status(self):
        t = time.monotonic() - self._t0
        liquid = 31.0 + 2.5 * math.sin(t / 40)
        return {
            "connected": self._connected,
            "liquid_temp": round(liquid, 1),
            "pump_rpm": int(1800 + self._fixed["pump"] * 10 + 20 * math.sin(t)),
            "pump_duty": self._fixed["pump"],
            "fan_rpm": int(400 + self._fixed["fan"] * 12 + 15 * math.sin(t / 3)),
            "fan_duty": self._fixed["fan"],
        }

    def led_count(self, channel):
        return 24

    hw_effects = True

    def write_lighting(self, channel, colors):
        return self._connected

    def write_effect(self, channel, effect, colors, brightness, speed):
        return self._connected

    def set_fixed(self, channel, duty):
        self._fixed[channel] = duty
        return True

    def set_curve(self, channel, points):
        self._fixed[channel] = points[len(points) // 2][1]
        return True

    def lcd_brightness(self, value):
        self._bright = value
        return True

    def lcd_orientation(self, deg):
        self._orient = deg
        return True

    def lcd_liquid(self):
        time.sleep(0.05)
        return True

    def lcd_static(self, path):
        time.sleep(0.2)
        return True

    def lcd_gif(self, path):
        time.sleep(0.4 + os.path.getsize(path) / 4e6)  # ~4 MB/s bulk
        return True

    def lcd_sensor_frame(self, path):
        return True

    def lcd_clear(self):
        return True

    def render_sensor_screen(self, style, status, ring_color, path):
        try:
            from PIL import Image, ImageDraw
        except ImportError:
            return None
        img = Image.new("RGB", (640, 640), (13, 14, 18))
        d = ImageDraw.Draw(img)
        d.ellipse((40, 40, 600, 600), outline=tuple(ring_color), width=28)
        d.text((280, 300), f"{status.get('liquid_temp') or 0:.1f} C", fill=(236, 237, 241))
        img.save(path, format="PNG")
        return path


def create(mock: bool) -> Backend:
    if mock:
        return MockBackend()
    try:
        return OpenKrakenBackend()
    except Exception:
        log.exception("bundled Kraken driver failed to load; falling back to plain liquidctl")
    try:
        return LiquidctlBackend()
    except ImportError as exc:
        raise BackendError("missing_dependency", f"liquidctl is not importable: {exc}") from None
