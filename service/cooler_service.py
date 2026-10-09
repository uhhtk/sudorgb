#!/usr/bin/python3 -I
"""SudoRGB cooler service: every AIO and fan controller liquidctl drives (except
the Kraken models kraken_service.py owns), plus host-streamed LCD panels.

Same IPC as kraken_service.py (JSON lines on stdin/stdout).

  requests  snapshot                                        -> {"devices": [...]}
            set_speed {device, channel, mode: fixed|curve, duty | points}
            set_lcd   {device, mode: off|media, path, fit, rotation, brightness}
  events    hello (once), devices (every poll: the full device list)

Ownership is the same rule as the Kraken service: a device is opened only when
a scan made right before the open finds no other process holding its nodes, and
it is released as soon as another holder shows up. Two programs on one HID
reply stream garble each other's commands.

Safety: pumps never go below PUMP_MIN; a software curve with no temperature
reading runs at the curve's highest duty; nothing writes firmware.
"""

from __future__ import annotations

import json
import logging
import os
import queue
import re
import resource
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import conflicts  # noqa: E402
import corsair_lcd  # noqa: E402
import media  # noqa: E402
from kraken_service import CACHE_DIR, Out, _die_with_parent, _xdg  # noqa: E402

log = logging.getLogger("orkc.coolers")
PROTOCOL = 1
STATE_FILE = _xdg("XDG_STATE_HOME", ".local/state") / "orkc" / "coolers.json"
POLL_S = 2.0
SCAN_S = 10.0
PUMP_MIN = 20
#: liquidctl driver modules that drive pumps/fans. Lighting-only, RAM, PSU and
#: GPU drivers are left to OpenRGB.
COOLING_MODULES = {
    "kraken2", "kraken3", "asetek", "asetek_pro", "hydro_platinum", "commander_core", "commander_pro",
    "aquacomputer", "asus_ryujin", "asus_ryuo", "msi", "smart_device", "lianli_uni", "coolit",
    "control_hub", "ga2_lcd",
}
#: liquidctl devices whose screen takes still images (stored in a device slot).
#: Clearer names where liquidctl names the controller, not the cooler.
NAMES = {(0x1B1C, 0x0C1C): "Corsair iCUE ELITE CAPELLIX (Commander Core)",
         (0x1B1C, 0x0C32): "Corsair iCUE H100i/H150i ELITE (Commander ST)"}
LIQUIDCTL_SCREENS = {(0x0DB0, 0xB130): {"media": ["image"], "size": [240, 320], "brightness": False}}


def channel_of(label: str) -> str | None:
    low = label.lower()
    if "pump" in low:
        return "pump"
    if "fan" in low:
        m = re.search(r"fan\D*?(\d+)", low)
        return f"fan{m.group(1)}" if m else "fan"
    return None


def channel_label(ch: str) -> str:
    return "Pump" if ch == "pump" else "Fans" if ch == "fan" else f"Fan {ch[3:]}"


def interpolate(points, temp: float) -> int:
    pts = sorted(points)
    if temp <= pts[0][0]:
        return int(pts[0][1])
    for (t0, d0), (t1, d1) in zip(pts, pts[1:]):
        if temp <= t1:
            return round(d0 + (d1 - d0) * (temp - t0) / max(t1 - t0, 1e-9))
    return int(pts[-1][1])


def parse_status(status) -> tuple[dict, float | None]:
    """liquidctl status rows -> ({channel: {rpm, duty}}, liquid temperature)."""
    channels: dict[str, dict] = {}
    liquid = first_temp = None
    for label, value, unit in status:
        if unit in ("rpm", "%"):
            ch = channel_of(label)
            if ch and isinstance(value, (int, float)):
                channels.setdefault(ch, {})["rpm" if unit == "rpm" else "duty"] = value
        elif unit == "°C" and isinstance(value, (int, float)):
            first_temp = value if first_temp is None else first_temp
            if liquid is None and re.search(r"liquid|water|coolant", label, re.I):
                liquid = value
    return channels, liquid if liquid is not None else first_temp


class Device:
    def __init__(self, dev_id: str, name: str, kind: str, ids: set, nodes: set, open_fn) -> None:
        self.id, self.name, self.kind, self.ids, self.nodes = dev_id, name, kind, ids, nodes
        self.open_fn = open_fn  # () -> handle (liquidctl driver, or EliteLcd)
        self.handle = None
        self.state, self.message, self.holders = "connecting", "", []
        self.status: list = []
        self.channels: dict[str, dict] = {}
        self.liquid: float | None = None
        self.curve_kind: dict[str, str] = {}
        self.soft: dict[str, list] = {}
        self.soft_last: dict[str, tuple[int, float]] = {}
        self.lcd_caps: dict | None = None
        self.lcd_shown: dict = {"mode": "firmware"}

    def to_json(self, desired: dict) -> dict:
        want = desired.get(self.id, {})
        chans = []
        for ch in sorted(set(self.channels) | set(want.get("speed", {})), key=lambda c: (c != "pump", c)):
            info = self.channels.get(ch, {})
            chans.append({"id": ch, "label": channel_label(ch), "rpm": info.get("rpm"), "duty": info.get("duty"),
                          "curve": self.curve_kind.get(ch), "floor": PUMP_MIN if ch == "pump" else 0})
        return {"id": self.id, "name": self.name, "kind": self.kind, "state": self.state, "message": self.message,
                "holders": self.holders, "liquid_temp": self.liquid, "channels": chans,
                "status": [[str(a), b if isinstance(b, (int, float, str, type(None))) else str(b), str(c)] for a, b, c in self.status],
                "speed": want.get("speed", {}), "lcd": None if not self.lcd_caps else
                {"caps": self.lcd_caps, "desired": want.get("lcd"), "shown": self.lcd_shown}}


def discover_liquidctl() -> list[Device]:
    try:
        from liquidctl import find_liquidctl_devices
    except ImportError:
        return []
    out, seen = [], {}
    for drv in find_liquidctl_devices():
        module = type(drv).__module__.rsplit(".", 1)[-1]
        vid, pid = drv.vendor_id, drv.product_id
        if module not in COOLING_MODULES or (vid, pid) in conflicts.KRAKEN_IDS:
            continue
        base = f"{vid:04x}:{pid:04x}"
        seen[base] = seen.get(base, 0) + 1
        dev_id = base if seen[base] == 1 else f"{base}#{seen[base]}"
        name = NAMES.get((vid, pid)) or drv.description.replace(" (broken)", "")
        dev = Device(dev_id, name, "cooler", {(vid, pid)}, conflicts.usb_nodes({(vid, pid)}), lambda d=drv: _open_liquidctl(d))
        dev.lcd_caps = LIQUIDCTL_SCREENS.get((vid, pid))
        out.append(dev)
    return out


def _open_liquidctl(drv):
    drv.connect()
    try:
        drv.initialize()
    except Exception as exc:  # status may still work; report, don't fail
        log.warning("%s: initialize failed: %s", drv.description, exc)
    return drv


def discover_lcds() -> list[Device]:
    out = []
    for i, info in enumerate(corsair_lcd.find()):
        base = f"{info['vid']:04x}:{info['pid']:04x}"
        dev = Device(base if i == 0 else f"{base}#{i + 1}", info["name"], "lcd", {(info["vid"], info["pid"])},
                     {info["node"]}, lambda n=info["node"], p=info["pid"]: corsair_lcd.EliteLcd(n, p))
        dev.lcd_caps = {"media": ["image", "animation"], "size": [corsair_lcd.SIZE, corsair_lcd.SIZE], "brightness": True}
        out.append(dev)
    return out


def run_media(cfg: dict, size: int) -> dict:
    cmd = [sys.executable, "-I", str(HERE / "media.py"), "--src", cfg["path"], "--kind", "frames",
           "--fit", cfg.get("fit", "cover"), "--rotation", str(cfg.get("rotation", 0)),
           "--size", str(size), "--cache-dir", str(CACHE_DIR)]

    def limits():
        os.nice(10)
        resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
        resource.setrlimit(resource.RLIMIT_CPU, (120, 125))

    try:
        proc = subprocess.run(cmd, capture_output=True, timeout=150, preexec_fn=limits, text=True)
        lines = [ln for ln in proc.stdout.splitlines() if ln.startswith("{")]
        if lines:
            return json.loads(lines[-1])
        return {"ok": False, "error": f"Media processor failed: {proc.stderr.strip()[-300:]}"}
    except subprocess.TimeoutExpired:
        return {"ok": False, "error": "Media processing timed out"}


class CoolerService:
    def __init__(self, out, discover=None, scan=None, media_fn=run_media) -> None:
        self.out = out
        self.discover = discover or (lambda: discover_liquidctl() + discover_lcds())
        self.scan = scan or (lambda nodes: conflicts.scan(nodes=nodes))
        self.media_fn = media_fn
        self.q: queue.Queue = queue.Queue()
        self.running = True
        self.devices: dict[str, Device] = {}
        self.desired: dict = self._load()
        self.next_poll = self.next_scan = 0.0

    # -------------------------------------------------------------- plumbing
    def _load(self) -> dict:
        try:
            data = json.loads(STATE_FILE.read_text())
            return data if isinstance(data, dict) else {}
        except (OSError, ValueError):
            return {}

    def _save(self) -> None:
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        tmp = STATE_FILE.with_suffix(".tmp")
        tmp.write_text(json.dumps(self.desired, indent=1))
        os.replace(tmp, STATE_FILE)

    def reply(self, rid, result=None, error=None) -> None:
        if rid is None:
            return
        if error:
            self.out.send({"id": rid, "ok": False, "error": {"code": error[0], "message": error[1]}})
        else:
            self.out.send({"id": rid, "ok": True, "result": result or {}})

    def emit_devices(self) -> None:
        self.out.send({"event": "devices", "data": {"devices": self.snapshot()}})

    def snapshot(self) -> list:
        return [d.to_json(self.desired) for d in self.devices.values()]

    def reader(self) -> None:
        for line in sys.stdin.buffer:
            try:
                msg = json.loads(line)
                if isinstance(msg, dict):
                    self.q.put(("cmd", msg))
            except ValueError:
                continue
        self.q.put(("eof", None))

    def run(self) -> int:
        self.out.send({"event": "hello", "data": {"protocol": PROTOCOL, "pid": os.getpid(), "python": sys.version.split()[0]}})
        threading.Thread(target=self.reader, name="stdin", daemon=True).start()
        self.tick()
        while self.running:
            try:
                kind, payload = self.q.get(timeout=max(0.05, min(self.next_poll, self.next_scan) - time.monotonic()))
                if kind == "cmd":
                    self.handle(payload)
                elif kind == "media_done":
                    self.finish_media(*payload)
                elif kind == "eof":
                    self.running = False
            except queue.Empty:
                pass
            except Exception:
                log.exception("worker loop error")
            self.tick()
        for dev in self.devices.values():
            self.release(dev, "stopped")
        return 0

    # -------------------------------------------------------------- lifecycle
    def tick(self, now: float | None = None) -> None:
        now = time.monotonic() if now is None else now
        changed = False
        if now >= self.next_scan:
            self.next_scan = now + SCAN_S
            self.rescan()
            changed = True
        if now >= self.next_poll:
            self.next_poll = now + POLL_S
            for dev in self.devices.values():
                if dev.state == "ready":
                    self.poll(dev, now)
            changed = True
        if changed:
            self.emit_devices()

    def rescan(self) -> None:
        found = {d.id: d for d in self.discover()}
        for gone in set(self.devices) - set(found):
            self.release(self.devices.pop(gone), "unplugged")
        for dev_id, dev in found.items():
            self.devices.setdefault(dev_id, dev)
        nodes = set().union(*(d.nodes for d in self.devices.values())) if self.devices else set()
        holders = self.scan(nodes) if nodes else []
        for dev in self.devices.values():
            mine = [h for h in holders if set(h.nodes) & dev.nodes]
            dev.holders = [h.tool for h in mine]
            if mine:
                if dev.handle:
                    self.release(dev, "busy")
                dev.state = "busy"
                dev.message = (f"{', '.join(dev.holders)} has this device open. Two programs on one controller "
                               "garble each other's commands, so SudoRGB stays off it.")
            elif dev.handle is None:
                self.open(dev)

    def open(self, dev: Device) -> None:
        unwritable = [n for n in dev.nodes if n.startswith("/dev/hidraw") and not os.access(n, os.W_OK)]
        if unwritable:
            dev.state, dev.message = "permission", f"No write access to {', '.join(sorted(unwritable))}: install the udev rules (install.sh) and replug."
            return
        try:
            dev.handle = dev.open_fn()
        except Exception as exc:
            dev.state, dev.message = "error", f"Could not open: {exc}"
            log.warning("%s: open failed: %s", dev.name, exc)
            return
        dev.state, dev.message = "ready", ""
        want = self.desired.get(dev.id, {})
        for ch, cfg in want.get("speed", {}).items():
            try:
                self.apply_speed(dev, ch, cfg)
            except Exception as exc:
                dev.message = f"Restoring {channel_label(ch)} failed: {exc}"
        if want.get("lcd"):
            self.start_lcd(dev, want["lcd"], None)

    def release(self, dev: Device, why: str) -> None:
        h, dev.handle = dev.handle, None
        dev.state = why
        dev.soft.clear()
        if h is None:
            return
        try:
            if isinstance(h, corsair_lcd.EliteLcd):
                h.close()
            else:
                h.disconnect()
        except Exception as exc:
            log.warning("%s: release failed: %s", dev.name, exc)

    # -------------------------------------------------------------- cooling
    def poll(self, dev: Device, now: float) -> None:
        if isinstance(dev.handle, corsair_lcd.EliteLcd):
            if dev.handle.error:
                self.release(dev, "error")
                dev.message = f"LCD write failed: {dev.handle.error}"
            return
        try:
            dev.status = list(dev.handle.get_status())
        except Exception as exc:
            self.release(dev, "error")
            dev.message = f"Status read failed: {exc}"
            return
        dev.channels, dev.liquid = parse_status(dev.status)
        for ch, pts in list(dev.soft.items()):
            duty = interpolate(pts, dev.liquid) if dev.liquid is not None else max(d for _t, d in pts)
            last = dev.soft_last.get(ch)
            if last is None or abs(last[0] - duty) >= 2 or now - last[1] > 60:
                try:
                    dev.handle.set_fixed_speed(ch, duty)
                    dev.soft_last[ch] = (duty, now)
                except Exception as exc:
                    dev.message = f"{channel_label(ch)}: {exc}"

    def apply_speed(self, dev: Device, ch: str, cfg: dict) -> None:
        floor = PUMP_MIN if ch == "pump" else 0
        dev.soft.pop(ch, None)
        dev.soft_last.pop(ch, None)
        if cfg["mode"] == "fixed":
            dev.handle.set_fixed_speed(ch, max(floor, int(cfg["duty"])))
            dev.curve_kind[ch] = "fixed"
            return
        pts = [(int(t), max(floor, min(100, int(d)))) for t, d in cfg["points"]]
        try:
            from liquidctl.error import NotSupportedByDevice, NotSupportedByDriver
            unsupported: tuple = (NotSupportedByDevice, NotSupportedByDriver, NotImplementedError)
        except ImportError:
            unsupported = (NotImplementedError,)
        try:
            dev.handle.set_speed_profile(ch, pts)
            dev.curve_kind[ch] = "hardware"
        except unsupported:
            dev.soft[ch] = pts  # evaluated against liquid temperature on every poll
            dev.curve_kind[ch] = "software"
            self.poll(dev, time.monotonic())

    # -------------------------------------------------------------- LCD
    def start_lcd(self, dev: Device, cfg: dict, rid) -> None:
        h = dev.handle
        if cfg.get("mode") == "off":
            if isinstance(h, corsair_lcd.EliteLcd):
                h.blank()
                h.release()
            dev.lcd_shown = {"mode": "firmware"}
            self.reply(rid, {"mode": "off"})
            return
        if isinstance(h, corsair_lcd.EliteLcd):
            if "brightness" in cfg:
                try:
                    h.set_brightness(int(cfg["brightness"]))
                except OSError as exc:  # never block the picture on the backlight
                    log.warning("%s: brightness failed: %s", dev.name, exc)
            dev.lcd_shown = {"mode": "processing", "source": cfg["path"]}
            size = dev.lcd_caps["size"][0]
            threading.Thread(target=lambda: self.q.put(("media_done", (dev.id, cfg, self.media_fn(cfg, size), rid))),
                             name="media", daemon=True).start()
            return
        # liquidctl screen (MSI): still image into user slot 0, then show it.
        h.set_screen("lcd", "image", f"1;0;{cfg['path']}")
        h.set_screen("lcd", "image", "1;0")
        dev.lcd_shown = {"mode": "image", "source": cfg["path"]}
        self.reply(rid, {"mode": "image"})

    def finish_media(self, dev_id: str, cfg: dict, res: dict, rid) -> None:
        dev = self.devices.get(dev_id)
        if self.desired.get(dev_id, {}).get("lcd") is not cfg:
            self.reply(rid, error=("superseded", "Replaced by a newer LCD request"))
            return
        if not res.get("ok"):
            if dev:
                dev.lcd_shown = {"mode": "error", "message": res.get("error")}
            self.reply(rid, error=("media_error", res.get("error", "unknown media error")))
            return
        if not dev or not isinstance(dev.handle, corsair_lcd.EliteLcd):
            self.reply(rid, {"deferred": True})
            return
        meta = res["result"]
        dev.handle.show(media.read_frames(meta))
        dev.lcd_shown = {"mode": "animation" if meta["frames"] > 1 else "image", "source": cfg["path"],
                         "frames": meta["frames"], "bytes": meta["bytes"]}
        self.reply(rid, {"mode": dev.lcd_shown["mode"], "frames": meta["frames"]})
        self.emit_devices()

    # -------------------------------------------------------------- commands
    def handle(self, msg: dict) -> None:
        rid, cmd, a = msg.get("id"), msg.get("cmd"), msg.get("args") or {}
        try:
            if cmd == "ping":
                return self.reply(rid, {})
            if cmd == "snapshot":
                return self.reply(rid, {"devices": self.snapshot()})
            dev = self.devices.get(a.get("device"))
            if dev is None:
                return self.reply(rid, error=("no_device", f"Unknown device {a.get('device')!r}"))
            if cmd == "set_speed":
                ch, mode = str(a.get("channel")), a.get("mode")
                if mode == "fixed":
                    cfg = {"mode": "fixed", "duty": max(0, min(100, int(a["duty"])))}
                elif mode == "curve":
                    pts = [[int(t), int(d)] for t, d in a["points"]]
                    if not 2 <= len(pts) <= 7:
                        raise ValueError("A curve needs 2 to 7 points")
                    cfg = {"mode": "curve", "points": pts}
                else:
                    raise ValueError(f"Unknown mode {mode!r}")
                self.desired.setdefault(dev.id, {}).setdefault("speed", {})[ch] = cfg
                self._save()
                if dev.state == "ready":
                    self.apply_speed(dev, ch, cfg)
                self.reply(rid, {"applied": dev.state == "ready", "curve": dev.curve_kind.get(ch)})
            elif cmd == "set_lcd":
                if not dev.lcd_caps:
                    return self.reply(rid, error=("no_lcd", f"{dev.name} has no supported screen"))
                if a.get("mode") not in ("off", "media") or (a.get("mode") == "media" and not a.get("path")):
                    raise ValueError("mode must be 'off' or 'media' with a path")
                cfg = {k: a[k] for k in ("mode", "path", "fit", "rotation", "brightness") if k in a}
                self.desired.setdefault(dev.id, {})["lcd"] = cfg
                self._save()
                if dev.state != "ready":
                    return self.reply(rid, {"deferred": True})
                self.start_lcd(dev, cfg, rid)
            else:
                return self.reply(rid, error=("bad_request", f"Unknown command {cmd!r}"))
        except (KeyError, TypeError, ValueError) as exc:
            self.reply(rid, error=("bad_request", str(exc)))
        except Exception as exc:
            log.exception("command %s failed", cmd)
            self.reply(rid, error=("device_error", str(exc)))
        self.emit_devices()


class _MockDriver:
    """--mock: a Capellix-shaped cooler for UI work without hardware."""

    def __init__(self):
        self.duty = {"pump": 70, "fan1": 40, "fan2": 40, "fan3": 40}

    def get_status(self):
        t = 31 + 3 * abs((time.time() / 20) % 2 - 1)
        return ([("Pump speed", 24 * self.duty["pump"], "rpm")] +
                [(f"Fan speed {i}", 18 * self.duty[f"fan{i}"], "rpm") for i in (1, 2, 3)] +
                [("Water temperature", round(t, 1), "°C")])

    def set_fixed_speed(self, ch, duty):
        self.duty[ch] = duty

    def set_speed_profile(self, ch, pts):
        raise NotImplementedError

    def disconnect(self):
        pass


def discover_mock() -> list[Device]:
    drv = _MockDriver()
    cooler = Device("mock:cooler", "Corsair iCUE H150i ELITE CAPELLIX (mock)", "cooler", set(), set(), lambda: drv)
    lcd = Device("mock:lcd", "Corsair iCUE ELITE LCD (mock)", "lcd", set(), set(),
                 lambda: corsair_lcd.EliteLcd("/dev/null", 0x0C33))
    lcd.lcd_caps = {"media": ["image", "animation"], "size": [480, 480], "brightness": True}
    return [cooler, lcd]


_MOCK_DEVICES: list = []


def main() -> int:
    logging.basicConfig(stream=sys.stderr, level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    _die_with_parent()
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    discover = None
    if "--mock" in sys.argv:
        global STATE_FILE
        STATE_FILE = STATE_FILE.with_name("coolers-mock.json")
        _MOCK_DEVICES.extend(discover_mock())
        discover = lambda: list(_MOCK_DEVICES)  # noqa: E731  (same objects every rescan)
    svc = CoolerService(Out(), discover=discover)

    def on_term(*_):
        svc.running = False
        svc.q.put(("eof", None))

    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    return svc.run()


if __name__ == "__main__":
    sys.exit(main())
