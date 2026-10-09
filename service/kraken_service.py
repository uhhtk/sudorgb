#!/usr/bin/python3 -I
"""ORKC Kraken hardware service.

A headless process that is the *only* owner of the NZXT Kraken. The C++
application launches it with the system interpreter in isolated mode
(``python3 -I``: no user site-packages, no PYTHONPATH), so a mise/pyenv/venv
Python on PATH can never shadow the distro's liquidctl/Pillow/pyusb.

IPC: newline-delimited JSON on stdin/stdout.

  request   {"id": 7, "cmd": "set_lcd", "args": {...}}
  response  {"id": 7, "ok": true, "result": {...}}
            {"id": 7, "ok": false, "error": {"code": "media_error", "message": "..."}}
  event     {"event": "status", "data": {...}}      (telemetry, every poll)
            {"event": "state",  "data": {...}}      (lifecycle / conflicts)
            {"event": "lcd",    "data": {...}}      (what the panel shows now)
            {"event": "hello",  "data": {...}}      (once, at start)

stdout is reserved for protocol frames: ``sys.stdout`` is redirected to stderr
so that no stray library print can corrupt the stream, and logging goes to
stderr, which the C++ side captures.

Threads: a reader thread turns stdin lines into queue items, and a media
thread runs the resource-limited media subprocess. ALL hardware access happens
on the main worker thread, strictly sequentially.
"""

from __future__ import annotations

import ctypes
import json
import logging
import os
import queue
import resource
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))  # -I drops the script dir from sys.path

import backend as bk  # noqa: E402
import conflicts  # noqa: E402

PROTOCOL = 1

#: Minimum spacing between lighting frames per channel. The USB write takes
#: ~0.4 ms, but the Kraken's firmware only *displays* frames at a limited rate;
#: OpenKraken soak-tested 5 FPS as reliable on the 2024 Elite. Faster streams get
#: dropped or queued inside the cooler, which is what made colour changes lag.
LIGHT_FRAME_INTERVAL = float(os.environ.get("ORKC_LIGHT_INTERVAL", "0.2"))
#: After the last change, the final frame is written once more after this delay,
#: in case the firmware dropped it for arriving too soon after the previous one.
LIGHT_SETTLE_DELAY = 1.0
log = logging.getLogger("orkc.service")


def _xdg(var: str, default: str) -> Path:
    v = os.environ.get(var)
    return Path(v) if v and os.path.isabs(v) else Path.home() / default


RUNTIME_DIR = Path(os.environ.get("XDG_RUNTIME_DIR") or f"/tmp/orkc-{os.getuid()}") / "orkc"
CACHE_DIR = _xdg("XDG_CACHE_HOME", ".cache") / "orkc" / "lcd"


class Out:
    """Thread-safe writer of protocol frames to the *real* stdout fd."""

    def __init__(self) -> None:
        self._fd = os.dup(1)
        os.dup2(2, 1)  # anything printing to fd 1 now lands on stderr
        sys.stdout = sys.stderr
        self._lock = threading.Lock()

    def send(self, obj: dict) -> None:
        data = (json.dumps(obj, separators=(",", ":"), default=str) + "\n").encode()
        with self._lock:
            view = memoryview(data)
            while view:
                try:
                    n = os.write(self._fd, view)
                except BrokenPipeError:
                    os._exit(0)  # parent is gone
                view = view[n:]


def _rgb(c) -> tuple[int, int, int]:
    if isinstance(c, str):
        s = c.lstrip("#")
        if len(s) == 8:  # #AARRGGBB from Qt
            s = s[2:]
        if len(s) != 6:
            raise bk.BackendError("bad_request", f"Bad colour {c!r}")
        return int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16)
    r, g, b = (int(x) for x in c[:3])
    return max(0, min(255, r)), max(0, min(255, g)), max(0, min(255, b))


class Service:
    def __init__(self, backend: bk.Backend, out: Out) -> None:
        self.be = backend
        self.out = out
        self.q: queue.Queue = queue.Queue()
        self.running = True
        self.state = ""
        self.lock = conflicts.InstanceLock(RUNTIME_DIR / "kraken.lock")
        self.poll_interval = 1.5
        self.desired: dict = {"lighting": {}, "cooling": {}, "lcd": None}
        self.last_status: dict = {}
        self.lcd_shown: dict = {"mode": "unknown"}
        self.light_t0: dict[str, float] = {}
        self.light_written: dict[str, bool] = {}
        self.light_dirty: set[str] = set()          # desired changed, frame not yet written
        self.light_last: dict[str, float] = {}      # monotonic time of last write per channel
        self.light_settle: dict[str, float] = {}    # when to re-send the final static frame
        self.reassert_until = 0.0
        self.next = {"poll": 0.0, "conflict": 0.0, "reconnect": 0.0, "light": 0.0, "sensor": 0.0}
        self.reconnect_delay = 1.0
        self.media_busy = False
        self.sensor_frame = str(RUNTIME_DIR / "sensor-frame.png")

    # ------------------------------------------------------------- plumbing
    def emit(self, event: str, data: dict) -> None:
        if self.be.simulated:
            data = dict(data, simulated=True)
        self.out.send({"event": event, "data": data})

    def set_state(self, state: str, **extra) -> None:
        if state != self.state or extra.get("force"):
            extra.pop("force", None)
            self.state = state
            log.info("state -> %s %s", state, extra or "")
            self.emit("state", {"state": state, **extra})

    def reply(self, rid, result=None, error: tuple[str, str] | None = None) -> None:
        if rid is None:
            return
        if error:
            self.out.send({"id": rid, "ok": False, "error": {"code": error[0], "message": error[1]}})
        else:
            self.out.send({"id": rid, "ok": True, "result": result or {}})

    # ------------------------------------------------------------- lifecycle
    def reader(self) -> None:
        for line in sys.stdin.buffer:
            line = line.strip()
            if not line:
                continue
            try:
                msg = json.loads(line)
                if not isinstance(msg, dict):
                    raise ValueError("not an object")
            except ValueError as exc:
                self.out.send({"id": None, "ok": False, "error": {"code": "bad_json", "message": str(exc)}})
                continue
            self.q.put(("cmd", msg))
        self.q.put(("eof", None))

    def run(self) -> int:
        RUNTIME_DIR.mkdir(parents=True, exist_ok=True, mode=0o700)
        CACHE_DIR.mkdir(parents=True, exist_ok=True)
        os.environ["ORKC_MEDIA_CACHE"] = str(CACHE_DIR)
        self.emit(
            "hello",
            {
                "protocol": PROTOCOL,
                "pid": os.getpid(),
                "python": sys.version.split()[0],
                "executable": sys.executable,
                "backend": self.be.name,
                "backend_label": getattr(self.be, "backend_label", self.be.name),
                "capabilities": self.be.capabilities,
            },
        )
        threading.Thread(target=self.reader, name="stdin", daemon=True).start()
        self.set_state("starting")
        while self.running:
            now = time.monotonic()
            due = min(self.next.values())
            try:
                kind, payload = self.q.get(timeout=max(0.0, min(due - now, 1.0)))
            except queue.Empty:
                kind, payload = None, None
            try:
                # Handle everything that is already queued before touching
                # hardware, so a burst of colour updates collapses into one frame.
                for _ in range(256):
                    if kind == "cmd":
                        self.handle(payload)
                    elif kind == "media_done":
                        self.finish_media(*payload)
                    elif kind == "eof":
                        log.info("stdin closed; shutting down")
                        self.running = False
                    try:
                        kind, payload = self.q.get_nowait()
                    except queue.Empty:
                        break
                self.tick()
            except Exception:
                log.exception("unexpected error in worker loop")
        self.shutdown()
        return 0

    def shutdown(self) -> None:
        try:
            self.be.disconnect()
        finally:
            self.lock.release()

    # ------------------------------------------------------------- periodic work
    def tick(self) -> None:
        now = time.monotonic()
        if not self.be.is_connected():
            # Ownership is re-verified with a fresh scan immediately before EVERY
            # open attempt. Never open the device on the strength of an older scan.
            if now >= self.next["reconnect"]:
                if self.check_ownership():
                    self.try_connect()
                else:
                    self.next["reconnect"] = now + 2.0
            return
        if now >= self.next["conflict"]:
            self.next["conflict"] = now + 5.0
            if not self.check_ownership():
                return
        if now >= self.next["poll"]:
            self.next["poll"] = now + self.poll_interval
            self.poll()
        if now >= self.next["light"]:
            self.next["light"] = self.lighting_tick(now)
        if now >= self.next["sensor"]:
            self.next["sensor"] = now + 2.0
            self.sensor_tick()

    def check_ownership(self) -> bool:
        """Hold the device only while nobody else does. Returns True if we may proceed."""
        if not self.lock.try_acquire():
            self.next["reconnect"] = time.monotonic() + 3
            self.set_state(
                "conflict",
                holders=[{"pid": self.lock.holder_pid(), "name": "orkc kraken service", "tool": "ORKC", "nodes": []}],
                message="Another ORKC Kraken service already owns the cooler.",
            )
            return False
        if self.be.simulated:
            return True
        holders = conflicts.scan()
        if holders:
            if self.be.is_connected():
                log.warning("another process grabbed the Kraken; releasing it: %s", holders)
                self.be.disconnect()
            tools = sorted({h.tool for h in holders})
            names = " and ".join(tools)
            verb, it = ("are", "them") if len(tools) > 1 else ("is", "it")
            self.set_state(
                "conflict",
                holders=[h.to_json() for h in holders],
                message=f"{names} {verb} using the Kraken. Close {it} (or disable Kraken support there) to let ORKC take over.",
                force=self.state == "conflict" and getattr(self, "_last_holders", None) != [h.pid for h in holders],
            )
            self._last_holders = [h.pid for h in holders]
            return False
        self._last_holders = []
        return True

    def try_connect(self) -> None:
        self.set_state("searching")
        try:
            ok = self.be.connect()
        except Exception:
            log.exception("connect raised")
            ok = False
        if not ok:
            self.next["reconnect"] = time.monotonic() + self.reconnect_delay
            self.reconnect_delay = min(self.reconnect_delay * 2, 15.0)
            return
        self.reconnect_delay = 1.0
        info = self.be.info()
        self.set_state("ready", device=info, backend=self.be.name, force=True)
        self.next["poll"] = 0.0
        self.restore_desired()

    def poll(self) -> None:
        st = self.be.status()
        if not st.get("connected"):
            self.lcd_shown = {"mode": "unknown"}
            self.set_state("searching", message="Kraken disconnected; reconnecting")
            self.emit("status", {"connected": False})
            self.next["reconnect"] = time.monotonic() + 1.0
            return
        self.last_status = st
        st = dict(st, lcd_bulk_unavailable=self.be.lcd_bulk_unavailable())
        self.emit("status", st)

    # ------------------------------------------------------------- lighting
    def lighting_tick(self, now: float) -> float:
        """Write due frames; return when the next lighting work is due.

        Per channel: a pending change is written as soon as the channel's frame
        interval allows (the first change after a pause goes out immediately);
        animated effects advance at the same rate; a static colour is re-sent
        once after it settles; LCD transfers trigger a short repaint window.
        """
        nxt = now + 1.0
        for ch, cfg in self.desired["lighting"].items():
            hw = self._hw_effect(cfg)
            animated = bk.EFFECTS.get(cfg["mode"], ("", 0, 0, False))[3] and not hw  # host-streamed
            ready_at = self.light_last.get(ch, 0.0) + LIGHT_FRAME_INTERVAL
            # A firmware effect is written once; re-sending it would restart the animation.
            wants = ch in self.light_dirty or animated or not self.light_written.get(ch) or (now < self.reassert_until and not hw)
            settle = self.light_settle.get(ch)
            if wants or (settle is not None and now >= settle):
                if now >= ready_at:
                    self.write_light(ch, cfg, now)
                    self.light_dirty.discard(ch)
                    if settle is not None and now >= settle:
                        self.light_settle.pop(ch, None)
                    elif not animated and not hw:
                        self.light_settle[ch] = now + LIGHT_SETTLE_DELAY
                    ready_at = now + LIGHT_FRAME_INTERVAL
                if ch in self.light_dirty or animated or now < self.reassert_until:
                    nxt = min(nxt, ready_at)
            if ch in self.light_settle:
                nxt = min(nxt, max(self.light_settle[ch], ready_at))
        return nxt

    def _hw_effect(self, cfg: dict) -> bool:
        return self.be.hw_effects and cfg["mode"] in bk.HW_EFFECTS

    def write_light(self, ch: str, cfg: dict, now: float) -> bool:
        if self._hw_effect(cfg):
            ok = self.be.write_effect(ch, cfg["mode"], cfg["colors"], cfg["brightness"], cfg["speed"])
            log.debug("light effect %s %s ok=%s", ch, cfg["mode"], ok)
            self.light_written[ch] = bool(ok)
            self.light_last[ch] = now
            return ok
        count = self.be.led_count(ch)
        frame = bk.effect_frame(cfg["mode"], cfg["colors"], cfg["brightness"], count, now - self.light_t0.get(ch, now), cfg["speed"])
        ok = self.be.write_lighting(ch, frame)
        log.debug("light frame %s %s ok=%s", ch, frame[0] if frame else None, ok)
        self.light_written[ch] = bool(ok)
        self.light_last[ch] = now
        return ok

    # ------------------------------------------------------------- LCD sensor screen
    def sensor_tick(self) -> None:
        lcd = self.desired["lcd"]
        if not lcd or lcd["mode"] != "sensors" or self.media_busy or self.be.lcd_bulk_unavailable():
            return
        path = self.be.render_sensor_screen(lcd.get("sensor_style", "liquid_ring"), self.last_status, _rgb(lcd.get("ring_color", "#ff7a29")), self.sensor_frame)
        if path and self.be.lcd_sensor_frame(path):
            if self.lcd_shown.get("mode") != "sensors":
                self.lcd_shown = {"mode": "sensors", "preview": path}
                self.emit("lcd", self.lcd_shown)

    # ------------------------------------------------------------- commands
    def handle(self, msg: dict) -> None:
        rid, cmd, args = msg.get("id"), msg.get("cmd"), msg.get("args") or {}
        fn = getattr(self, f"cmd_{cmd}", None) if isinstance(cmd, str) else None
        if fn is None:
            self.reply(rid, error=("bad_request", f"Unknown command {cmd!r}"))
            return
        try:
            result = fn(args, rid)
            if result is not _DEFERRED:
                self.reply(rid, result)
        except bk.BackendError as exc:
            self.reply(rid, error=(exc.code, str(exc)))
        except (KeyError, TypeError, ValueError) as exc:
            self.reply(rid, error=("bad_request", f"{type(exc).__name__}: {exc}"))
        except Exception as exc:
            log.exception("command %s failed", cmd)
            self.reply(rid, error=("internal", str(exc)))

    def _require_ready(self) -> None:
        if not self.be.is_connected():
            raise bk.BackendError("not_ready", f"Kraken not available ({self.state})")

    def cmd_ping(self, a, rid):
        return {"pong": True, "state": self.state}

    def cmd_get_info(self, a, rid):
        return {
            "state": self.state,
            "backend": self.be.name,
            "capabilities": self.be.capabilities,
            "device": self.be.info() if self.be.is_connected() else None,
            "lcd": self.lcd_shown,
        }

    def cmd_set_poll_interval(self, a, rid):
        self.poll_interval = max(0.5, min(10.0, float(a["seconds"])))
        return {"seconds": self.poll_interval}

    def cmd_shutdown(self, a, rid):
        self.running = False
        return {}

    def cmd_apply(self, a, rid):
        """Store a full desired state (profile restore) and apply what we can now."""
        for ch, cfg in (a.get("lighting") or {}).items():
            self._store_lighting(ch, cfg)
        for ch, cfg in (a.get("cooling") or {}).items():
            self._store_cooling(ch, cfg)
        if a.get("lcd"):
            self.desired["lcd"] = self._lcd_cfg(a["lcd"])
        if not self.be.is_connected():
            return {"deferred": True}
        errors = self.restore_desired(rid=rid, include_lcd=bool(a.get("lcd")))
        return _DEFERRED if a.get("lcd") else {"errors": errors}

    def restore_desired(self, rid=None, include_lcd=True) -> list[str]:
        """Re-apply everything we were asked for (after reconnect / profile apply)."""
        errors = []
        for ch, cfg in self.desired["cooling"].items():
            if not self._apply_cooling(ch, cfg):
                errors.append(f"cooling {ch}")
        now = time.monotonic()
        for ch, cfg in self.desired["lighting"].items():
            self.light_written[ch] = False
            self.light_t0.setdefault(ch, now)
            if not self.write_light(ch, cfg, now):
                errors.append(f"lighting {ch}")
        if include_lcd and self.desired["lcd"]:
            self.apply_lcd(self.desired["lcd"], rid)
        elif rid is not None and include_lcd:
            self.reply(rid, {"errors": errors})
        return errors

    def _store_lighting(self, ch, cfg):
        if ch not in self.be.capabilities.get("lighting", []):
            raise bk.BackendError("unsupported", f"Lighting channel {ch!r} not supported by this backend")
        mode = cfg.get("mode", "fixed")
        if mode not in bk.EFFECTS:
            raise bk.BackendError("bad_request", f"Unknown lighting mode {mode!r}")
        prev = self.desired["lighting"].get(ch)
        self.desired["lighting"][ch] = {
            "mode": mode,
            "colors": [_rgb(c) for c in cfg.get("colors", [])][:8],
            "brightness": max(0, min(100, int(cfg.get("brightness", 100)))),
            "speed": cfg.get("speed", "normal") if cfg.get("speed") in bk.SPEED_PERIODS else "normal",
        }
        if not prev or prev["mode"] != mode:
            self.light_t0[ch] = time.monotonic()  # restart the effect only when it changes
        self.light_dirty.add(ch)
        self.light_settle.pop(ch, None)

    def cmd_set_lighting(self, a, rid):
        ch = a["channel"]
        channels = list(self.be.capabilities.get("lighting", [])) if ch == "all" else [ch]
        for c in channels:
            self._store_lighting(c, a)
        # Don't write here: the scheduler sends the newest colour right away if
        # the channel is ready, or at its next frame slot (latest value wins).
        self.next["light"] = 0.0
        return {"applied": self.be.is_connected()}

    def _store_cooling(self, ch, cfg):
        if ch not in ("pump", "fan"):
            raise bk.BackendError("bad_request", f"Unknown cooling channel {ch!r}")
        floor = bk.PUMP_MIN if ch == "pump" else 0
        if cfg.get("mode") == "fixed":
            self.desired["cooling"][ch] = {"mode": "fixed", "duty": max(floor, min(100, int(cfg["duty"])))}
        elif cfg.get("mode") == "curve":
            pts = bk.normalize_curve(cfg["points"])
            self.desired["cooling"][ch] = {"mode": "curve", "points": [(t, max(floor, d)) for t, d in pts]}
        else:
            raise bk.BackendError("bad_request", "cooling mode must be 'fixed' or 'curve'")

    def _apply_cooling(self, ch, cfg) -> bool:
        if cfg["mode"] == "fixed":
            return self.be.set_fixed(ch, cfg["duty"])
        return self.be.set_curve(ch, cfg["points"])

    def cmd_set_cooling(self, a, rid):
        ch = a["channel"]
        self._store_cooling(ch, a)
        if self.be.is_connected() and not self._apply_cooling(ch, self.desired["cooling"][ch]):
            raise bk.BackendError("device_error", f"Setting {ch} speed failed")
        return {"applied": self.be.is_connected(), "config": self.desired["cooling"][ch]}

    # ------------------------------------------------------------- LCD
    def _lcd_cfg(self, a: dict) -> dict:
        caps = self.be.capabilities
        mode = a.get("mode", "liquid")
        if mode not in caps.get("lcd_modes", []):
            raise bk.BackendError("unsupported", f"LCD mode {mode!r} not supported")
        if mode in ("image", "gif") and not a.get("path"):
            raise bk.BackendError("bad_request", f"LCD mode {mode!r} needs a file")
        orient = int(a.get("orientation", 0))
        if orient not in (0, 90, 180, 270):
            raise bk.BackendError("bad_request", "orientation must be 0/90/180/270")
        return {
            "mode": mode,
            "path": os.path.expanduser(a.get("path") or ""),
            "fit": a.get("fit", "cover"),
            "brightness": max(0, min(100, int(a.get("brightness", 70)))),
            "orientation": orient,
            "sensor_style": a.get("sensor_style", "liquid_ring"),
            "ring_color": a.get("ring_color", "#ff7a29"),
        }

    def cmd_set_lcd(self, a, rid):
        cfg = self._lcd_cfg(a)
        self.desired["lcd"] = cfg
        if not self.be.is_connected():
            return {"deferred": True}
        self.apply_lcd(cfg, rid)
        return _DEFERRED

    def cmd_lcd_brightness(self, a, rid):
        self._require_ready()
        v = max(0, min(100, int(a["value"])))
        if self.desired["lcd"]:
            self.desired["lcd"]["brightness"] = v
        if not self.be.lcd_brightness(v):
            raise bk.BackendError("device_error", "Setting LCD brightness failed")
        return {"brightness": v}

    def cmd_lcd_clear_media(self, a, rid):
        self._require_ready()
        if self.media_busy:
            raise bk.BackendError("busy", "An LCD upload is in progress")
        if not self.be.lcd_clear():
            raise bk.BackendError("device_error", "Clearing LCD memory failed")
        self.lcd_shown = {"mode": "liquid"}
        self.emit("lcd", self.lcd_shown)
        if self.desired["lcd"]:
            self.apply_lcd(self.desired["lcd"], None)
        return {}

    def apply_lcd(self, cfg: dict, rid) -> None:
        """Brightness/orientation now; media is processed off-thread, then uploaded."""
        if self.media_busy:
            self.reply(rid, error=("busy", "An LCD upload is already in progress"))
            return
        bright = 0 if cfg["mode"] == "off" else cfg["brightness"]
        self.be.lcd_brightness(bright)
        info = self.be.info()
        if info.get("lcd_orientation") != cfg["orientation"]:
            self.be.lcd_orientation(cfg["orientation"])
        mode = cfg["mode"]
        if mode in ("liquid", "off"):
            ok = self.be.lcd_liquid()
            self._after_lcd_write()
            self.lcd_shown = {"mode": mode}
            self.emit("lcd", self.lcd_shown)
            self.reply(rid, {"mode": mode}) if ok else self.reply(rid, error=("device_error", "LCD mode change failed"))
        elif mode == "sensors":
            self.lcd_shown = {"mode": "pending"}
            self.next["sensor"] = 0.0
            self.reply(rid, {"mode": mode})
        else:
            if self.be.lcd_bulk_unavailable():
                self.reply(rid, error=("device_busy", "The LCD's USB bulk interface is held by another driver"))
                return
            self.media_busy = True
            w, _h = self.be.lcd_resolution()
            self.emit("lcd", {"mode": "processing", "source": cfg["path"]})
            threading.Thread(target=self._media_worker, args=(cfg, w, rid), name="media", daemon=True).start()

    def _media_worker(self, cfg: dict, size: int, rid) -> None:
        kind = "gif" if cfg["mode"] == "gif" else "image"
        # liquidctl rotates static images itself; our GIF bytes bypass it, so GIFs are pre-rotated.
        rotation = cfg["orientation"] if kind == "gif" else 0
        cmd = [
            sys.executable, "-I", str(HERE / "media.py"),
            "--src", cfg["path"], "--kind", kind, "--fit", cfg["fit"],
            "--rotation", str(rotation), "--size", str(size), "--cache-dir", str(CACHE_DIR),
        ]

        def limits():
            os.nice(10)
            resource.setrlimit(resource.RLIMIT_AS, (2 << 30, 2 << 30))
            resource.setrlimit(resource.RLIMIT_CPU, (120, 125))

        try:
            proc = subprocess.run(cmd, capture_output=True, timeout=150, preexec_fn=limits, text=True)
            lines = [ln for ln in proc.stdout.splitlines() if ln.startswith("{")]
            if lines:
                res = json.loads(lines[-1])
            elif proc.returncode < 0:
                res = {"ok": False, "error": f"Media processor was killed by signal {-proc.returncode} (likely memory/CPU limit)"}
            else:
                res = {"ok": False, "error": f"Media processor failed: {proc.stderr.strip()[-300:]}"}
        except subprocess.TimeoutExpired:
            res = {"ok": False, "error": "Media processing timed out"}
        except Exception as exc:
            res = {"ok": False, "error": f"Media processing failed: {exc}"}
        self.q.put(("media_done", (cfg, res, rid)))

    def finish_media(self, cfg: dict, res: dict, rid) -> None:
        self.media_busy = False
        if self.desired["lcd"] is not cfg:
            # Superseded by a newer request while processing; that one will run next.
            self.reply(rid, error=("superseded", "Replaced by a newer LCD request"))
            if self.desired["lcd"] and self.be.is_connected():
                self.apply_lcd(self.desired["lcd"], None)
            return
        if not res.get("ok"):
            self.emit("lcd", {"mode": "error", "message": res.get("error")})
            self.reply(rid, error=("media_error", res.get("error", "unknown media error")))
            return
        if not self.be.is_connected():
            self.reply(rid, {"deferred": True, "media": res["result"]})
            return
        path = res["result"]["path"]
        t = time.monotonic()
        ok = self.be.lcd_gif(path) if cfg["mode"] == "gif" else self.be.lcd_static(path)
        took = time.monotonic() - t
        self._after_lcd_write()
        if ok:
            self.lcd_shown = {"mode": cfg["mode"], "preview": path, "source": cfg["path"], "upload_s": round(took, 2), **{k: res["result"].get(k) for k in ("frames", "bytes")}}
            self.emit("lcd", self.lcd_shown)
            self.reply(rid, {"mode": cfg["mode"], "media": res["result"], "upload_s": round(took, 2)})
        else:
            reason = "the LCD's USB bulk interface is busy" if self.be.lcd_bulk_unavailable() else "the device rejected the upload"
            self.emit("lcd", {"mode": "error", "message": f"Upload failed: {reason}"})
            self.reply(rid, error=("device_error", f"LCD upload failed: {reason}"))

    def _after_lcd_write(self) -> None:
        # LCD transfers disturb the HUE2 controller on the same HID interface; a
        # few ring LEDs fall back to firmware green. Repaint for a few seconds.
        self.reassert_until = time.monotonic() + 3.0
        for ch, cfg in self.desired["lighting"].items():
            if self._hw_effect(cfg):
                self.light_written[ch] = False  # re-send the effect once, not for 3 s
        self.next["light"] = 0.0


_DEFERRED = object()  # handler will reply asynchronously


def _die_with_parent() -> None:
    try:
        libc = ctypes.CDLL("libc.so.6", use_errno=True)
        libc.prctl(1, signal.SIGTERM)  # PR_SET_PDEATHSIG
    except Exception:
        pass


def main() -> int:
    mock = "--mock" in sys.argv
    level = logging.DEBUG if "--debug" in sys.argv else logging.INFO
    logging.basicConfig(stream=sys.stderr, level=level, format="%(levelname)s %(name)s: %(message)s")
    _die_with_parent()
    out = Out()
    try:
        be = bk.create(mock)
    except bk.BackendError as exc:
        out.send({"event": "state", "data": {"state": "error", "code": exc.code, "message": str(exc)}})
        return 3
    svc = Service(be, out)

    def on_term(*_):
        svc.running = False
        svc.q.put(("eof", None))

    signal.signal(signal.SIGTERM, on_term)
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    return svc.run()


if __name__ == "__main__":
    sys.exit(main())
