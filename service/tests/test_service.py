"""Service tests. No hardware is touched: backends are fakes, scans are patched.

Run:  /usr/bin/python3 -I -m unittest discover -s service/tests -v
"""

import os
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))

import backend as bk  # noqa: E402
import conflicts  # noqa: E402
import kraken_service as ks  # noqa: E402
import media  # noqa: E402


class FakeOut:
    def __init__(self):
        self.frames = []

    def send(self, obj):
        self.frames.append(obj)

    def events(self, name):
        return [f["data"] for f in self.frames if f.get("event") == name]


class RecordingBackend(bk.MockBackend):
    """A 'real' (non-simulated) backend that records every hardware call."""

    simulated = False

    def __init__(self):
        super().__init__()
        self.calls = []
        self.frames = []

    def connect(self):
        self.calls.append("connect")
        return super().connect()

    def disconnect(self):
        self.calls.append("disconnect")
        super().disconnect()

    def write_lighting(self, channel, colors):
        self.calls.append(("light", channel, len(colors)))
        self.frames.append((channel, colors[0] if colors else None))
        return super().write_lighting(channel, colors)

    def write_effect(self, channel, effect, colors, brightness, speed):
        self.calls.append(("effect", channel, effect))
        return super().write_effect(channel, effect, colors, brightness, speed)

    def set_fixed(self, channel, duty):
        self.calls.append(("fixed", channel, duty))
        return super().set_fixed(channel, duty)


class OwnershipTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        ks.RUNTIME_DIR = Path(self.tmp.name)
        self.holders = []
        self._orig_scan = conflicts.scan
        conflicts.scan = lambda *a, **k: list(self.holders)
        self.be = RecordingBackend()
        self.out = FakeOut()
        self.svc = ks.Service(self.be, self.out)
        self.svc.lock = conflicts.InstanceLock(Path(self.tmp.name) / "kraken.lock")

    def tearDown(self):
        conflicts.scan = self._orig_scan
        self.svc.lock.release()
        self.tmp.cleanup()

    def spin(self, n=50):
        for _ in range(n):
            for k in self.svc.next:
                self.svc.next[k] = 0.0  # every periodic task is due, every iteration
            self.svc.tick()

    def test_never_connects_while_another_process_holds_the_device(self):
        self.holders = [conflicts.Holder(4242, "openkraken", "OpenKraken", ["/dev/hidraw12"])]
        self.spin(200)
        self.assertNotIn("connect", self.be.calls)
        self.assertEqual(self.svc.state, "conflict")

    def test_connects_once_conflict_clears(self):
        self.holders = [conflicts.Holder(1, "openrgb", "OpenRGB", ["/dev/hidraw12"])]
        self.spin(10)
        self.holders = []
        self.spin(3)
        self.assertEqual(self.be.calls.count("connect"), 1)
        self.assertEqual(self.svc.state, "ready")

    def test_releases_device_when_another_process_appears(self):
        self.spin(3)
        self.assertEqual(self.svc.state, "ready")
        self.holders = [conflicts.Holder(7, "openkraken", "OpenKraken", [])]
        self.spin(20)
        self.assertIn("disconnect", self.be.calls)
        self.assertEqual(self.be.calls.count("connect"), 1)  # and it stays away
        self.assertFalse(self.be.is_connected())

    def test_second_instance_is_locked_out(self):
        other = conflicts.InstanceLock(Path(self.tmp.name) / "kraken.lock")
        self.assertTrue(other.try_acquire())
        try:
            self.spin(10)
            self.assertNotIn("connect", self.be.calls)
            self.assertEqual(self.svc.state, "conflict")
        finally:
            other.release()

    def test_desired_state_is_reapplied_after_reconnect(self):
        self.spin(3)
        self.svc.handle({"id": 1, "cmd": "set_cooling", "args": {"channel": "pump", "mode": "fixed", "duty": 5}})
        self.assertIn(("fixed", "pump", bk.PUMP_MIN), self.be.calls)  # clamped to the pump floor
        self.be.disconnect()
        self.be.calls.clear()
        self.spin(3)
        self.assertIn(("fixed", "pump", bk.PUMP_MIN), self.be.calls)

    def test_bad_requests_get_structured_errors(self):
        self.svc.handle({"id": 9, "cmd": "nope"})
        self.svc.handle({"id": 10, "cmd": "set_cooling", "args": {"channel": "pump", "mode": "curve", "points": [[30, 50]]}})
        errs = {f["id"]: f["error"]["code"] for f in self.out.frames if f.get("ok") is False}
        self.assertEqual(errs, {9: "bad_request", 10: "bad_request"})


class FakeClock:
    def __init__(self):
        self.t = 1000.0

    def monotonic(self):
        return self.t


class LightingResponsivenessTests(unittest.TestCase):
    """Colour drags must never queue up inside the cooler (the old lag)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        ks.RUNTIME_DIR = Path(self.tmp.name)
        self._orig_scan, self._orig_time = conflicts.scan, ks.time
        conflicts.scan = lambda *a, **k: []
        self.clock = FakeClock()
        ks.time = self.clock
        self.be = RecordingBackend()
        self.svc = ks.Service(self.be, FakeOut())
        self.svc.lock = conflicts.InstanceLock(Path(self.tmp.name) / "kraken.lock")
        self.svc.tick()  # connect
        self.be.frames.clear()

    def tearDown(self):
        conflicts.scan, ks.time = self._orig_scan, self._orig_time
        self.svc.lock.release()
        self.tmp.cleanup()

    def set(self, rgb, ch="ring"):
        self.svc.handle({"id": 1, "cmd": "set_lighting", "args": {"channel": ch, "mode": "fixed", "colors": [rgb]}})

    def advance(self, dt):
        self.clock.t += dt
        if self.clock.t >= self.svc.next["light"]:
            self.svc.tick()

    def test_first_change_is_written_immediately(self):
        self.clock.t += 5
        self.set("#ff0000")
        self.svc.tick()
        self.assertEqual(self.be.frames, [("ring", (255, 0, 0))])

    def test_burst_collapses_to_latest_value(self):
        for i in range(100):  # 100 updates arrive before the worker wakes
            self.set("#%02x0000" % i)
        self.svc.tick()
        self.assertEqual(self.be.frames, [("ring", (99, 0, 0))])

    def test_drag_is_paced_and_final_colour_lands(self):
        self.clock.t += 5
        for i in range(60):  # 60 fps drag for one second
            self.set("#00%02x00" % i)
            self.advance(1 / 60)
        writes = [f for f in self.be.frames if f[0] == "ring"]
        self.assertLessEqual(len(writes), 1 / ks.LIGHT_FRAME_INTERVAL + 2)
        for _ in range(30):
            self.advance(0.05)
        self.assertEqual(self.be.frames[-1], ("ring", (0, 59, 0)))  # final value always arrives

    def test_settled_colour_is_resent_once(self):
        self.clock.t += 5
        self.set("#0000ff")
        self.svc.tick()
        for _ in range(60):
            self.advance(0.05)  # 3 s
        self.assertEqual(self.be.frames, [("ring", (0, 0, 255)), ("ring", (0, 0, 255))])

    def test_channels_are_independent(self):
        self.clock.t += 5
        self.set("#ff0000", "ring")
        self.set("#00ff00", "fans")
        self.svc.tick()
        self.assertEqual(sorted(self.be.frames), [("fans", (0, 255, 0)), ("ring", (255, 0, 0))])


class HardwareEffectTests(LightingResponsivenessTests):
    def test_rainbow_is_one_firmware_effect_not_a_stream(self):
        self.clock.t += 5
        self.be.calls.clear()
        self.svc.handle({"id": 1, "cmd": "set_lighting", "args": {"channel": "all", "mode": "spectrum", "colors": []}})
        for _ in range(60):
            self.advance(0.05)  # 3 s
        self.assertEqual(sorted(c for c in self.be.calls if c[0] == "effect"),
                         [("effect", "fans", "spectrum"), ("effect", "ring", "spectrum")])
        self.assertEqual(self.be.frames, [])  # nothing streamed

    def test_static_still_uses_direct_frames(self):
        self.clock.t += 5
        self.set("#ff0000")
        self.svc.tick()
        self.assertEqual(self.be.frames, [("ring", (255, 0, 0))])

    def test_effect_packet_layout(self):
        p = bk.effect_packet(0x01, "breathing", [(255, 10, 0)], 100, "normal")
        self.assertEqual(len(p), 64)
        self.assertEqual(p[:7], [0x2A, 0x04, 0x01, 0x01, 0x07, 0x14, 0x00])
        self.assertEqual(p[7:10], [10, 255, 0])  # GRB
        self.assertEqual(p[0x38:0x3C], [1, 0x08, 0x08, 0x03])


class PureTests(unittest.TestCase):
    def test_curve_is_monotone_and_forced_to_full_at_60(self):
        pts = bk.normalize_curve([[50, 40], [20, 60], [40, 30]])
        self.assertEqual(pts[-1], (60.0, 100))
        duties = [d for _t, d in pts]
        self.assertEqual(duties, sorted(duties))

    def test_effect_frames_have_led_count_and_respect_brightness(self):
        for mode in bk.EFFECTS:
            f = bk.effect_frame(mode, [(255, 0, 0), (0, 0, 255)], 50, 24, 1.3, "normal")
            self.assertEqual(len(f), 24, mode)
            self.assertTrue(all(c <= 128 for px in f for c in px), mode)

    def test_colour_parsing(self):
        self.assertEqual(ks._rgb("#ff8000"), (255, 128, 0))
        self.assertEqual(ks._rgb("#80ff8000"), (255, 128, 0))
        with self.assertRaises(bk.BackendError):
            ks._rgb("#12")


class MediaTests(unittest.TestCase):
    def setUp(self):
        from PIL import Image, ImageDraw

        self.tmp = tempfile.TemporaryDirectory()
        self.src = os.path.join(self.tmp.name, "wide.gif")
        frames = []
        for i in range(40):  # 480x200 moving bar on a static background
            im = Image.new("RGB", (480, 200), (20, 20, 40))
            ImageDraw.Draw(im).rectangle((i * 10, 50, i * 10 + 60, 150), fill=(255, 80, 0))
            frames.append(im)
        frames[0].save(self.src, save_all=True, append_images=frames[1:], duration=10, loop=0)

    def tearDown(self):
        self.tmp.cleanup()

    def test_gif_is_square_small_and_keeps_speed(self):
        res = media.run(self.src, "gif", "contain", 0, 640, self.tmp.name, media.DEFAULT_BUDGET)
        from PIL import Image

        with Image.open(res["path"]) as out:
            self.assertEqual(out.size, (640, 640))
        self.assertLess(res["bytes"], 1_000_000)
        # 10 ms frames are below the minimum: frames are dropped, total time kept.
        # (The last, partial group is clamped up to the minimum, hence "within 10%".)
        self.assertAlmostEqual(res["duration_ms"], 400, delta=40)
        self.assertLessEqual(res["frames"], 40 // 3 + 1)

    def test_cache_hit(self):
        a = media.run(self.src, "gif", "cover", 0, 320, self.tmp.name, media.DEFAULT_BUDGET)
        b = media.run(self.src, "gif", "cover", 0, 320, self.tmp.name, media.DEFAULT_BUDGET)
        self.assertFalse(a["cached"])
        self.assertTrue(b["cached"])

    def test_rejects_garbage(self):
        bad = os.path.join(self.tmp.name, "bad.gif")
        Path(bad).write_bytes(b"GIF89a" + os.urandom(64))
        with self.assertRaises(media.MediaError):
            media.run(bad, "gif", "cover", 0, 640, self.tmp.name, media.DEFAULT_BUDGET)


if __name__ == "__main__":
    unittest.main()
