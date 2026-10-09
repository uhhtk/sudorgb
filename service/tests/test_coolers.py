"""Cooler service tests. No hardware: fake liquidctl drivers, LCD streams into a temp file.

Run:  /usr/bin/python3 -I -m unittest discover -s service/tests -v
"""

import io
import sys
import tempfile
import time
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))

import conflicts  # noqa: E402
import cooler_service as cs  # noqa: E402
import corsair_lcd  # noqa: E402
import media  # noqa: E402
from test_service import FakeOut  # noqa: E402

try:
    from liquidctl.error import NotSupportedByDriver
except ImportError:  # liquidctl missing: the service treats NotImplementedError the same way
    NotSupportedByDriver = NotImplementedError


class FakeDriver:
    """Commander Core-shaped status; no hardware speed profiles."""

    def __init__(self, temp=30.0):
        self.temp = temp
        self.calls = []

    def get_status(self):
        return [("Pump speed", 2400, "rpm"), ("Fan speed 1", 900, "rpm"), ("Fan speed 2", 0, "rpm"),
                ("Water temperature", self.temp, "°C")]

    def set_fixed_speed(self, ch, duty):
        self.calls.append(("fixed", ch, duty))

    def set_speed_profile(self, ch, pts):
        raise NotSupportedByDriver()

    def disconnect(self):
        self.calls.append(("disconnect",))


def jpeg(color=(255, 0, 0)):
    from PIL import Image
    buf = io.BytesIO()
    Image.new("RGB", (480, 480), color).save(buf, format="JPEG")
    return buf.getvalue()


class CoolerServiceTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        cs.STATE_FILE = Path(self.tmp.name) / "coolers.json"
        self.drv = FakeDriver()
        self.holders = []
        self.out = FakeOut()
        dev = lambda: cs.Device("1b1c:0c1c", "Corsair Commander Core", "cooler", {(0x1B1C, 0x0C1C)},
                                {"/nonexistent/node"}, lambda: self.drv)
        self.svc = cs.CoolerService(self.out, discover=lambda: [dev()], scan=lambda nodes: list(self.holders))

    def tearDown(self):
        self.tmp.cleanup()

    def dev(self):
        return self.svc.snapshot()[0]

    def test_status_parsing(self):
        chans, liquid = cs.parse_status(self.drv.get_status())
        self.assertEqual(liquid, 30.0)
        self.assertEqual(set(chans), {"pump", "fan1", "fan2"})
        self.assertEqual(cs.channel_of("Fan 3 duty"), "fan3")
        self.assertEqual(cs.channel_of("Fan speed"), "fan")

    def test_software_curve_and_pump_floor(self):
        self.svc.tick(0)
        self.assertEqual(self.dev()["state"], "ready")
        self.svc.handle({"id": 1, "cmd": "set_speed", "args": {"device": "1b1c:0c1c", "channel": "pump",
                                                             "mode": "curve", "points": [[20, 0], [40, 100]]}})
        # 30 °C halfway -> 50 %; the 0 % point was raised to the pump floor first
        self.assertIn(("fixed", "pump", 60), self.drv.calls)
        self.assertEqual(self.dev()["channels"][0]["curve"], "software")
        self.svc.handle({"id": 2, "cmd": "set_speed", "args": {"device": "1b1c:0c1c", "channel": "pump", "mode": "fixed", "duty": 0}})
        self.assertEqual(self.drv.calls[-1], ("fixed", "pump", cs.PUMP_MIN))

    def test_no_temperature_runs_curve_at_max(self):
        self.drv.temp = None
        self.svc.tick(0)
        self.svc.handle({"id": 1, "cmd": "set_speed", "args": {"device": "1b1c:0c1c", "channel": "fan1",
                                                             "mode": "curve", "points": [[20, 30], [40, 80]]}})
        self.assertEqual(self.drv.calls[-1], ("fixed", "fan1", 80))

    def test_other_holder_blocks_and_releases(self):
        self.holders = [conflicts.Holder(42, "openrgb", "OpenRGB", ["/nonexistent/node"])]
        self.svc.tick(0)
        self.assertEqual(self.dev()["state"], "busy")
        self.assertEqual(self.drv.calls, [])  # never opened
        self.holders = []
        self.svc.tick(cs.SCAN_S)
        self.assertEqual(self.dev()["state"], "ready")
        self.holders = [conflicts.Holder(42, "openrgb", "OpenRGB", ["/nonexistent/node"])]
        self.svc.tick(2 * cs.SCAN_S)
        self.assertEqual(self.dev()["state"], "busy")
        self.assertIn(("disconnect",), self.drv.calls)

    def test_desired_speed_restored_on_reopen(self):
        self.svc.tick(0)
        self.svc.handle({"id": 1, "cmd": "set_speed", "args": {"device": "1b1c:0c1c", "channel": "fan1", "mode": "fixed", "duty": 55}})
        fresh = cs.CoolerService(FakeOut(), discover=self.svc.discover, scan=lambda n: [])
        self.drv.calls.clear()
        fresh.tick(0)
        self.assertIn(("fixed", "fan1", 55), self.drv.calls)


class LcdTest(unittest.TestCase):
    def test_frames_from_animated_gif(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "a.gif"
            frames = [Image.new("RGB", (300, 200), c) for c in ((255, 0, 0), (0, 255, 0), (0, 0, 255))]
            frames[0].save(src, save_all=True, append_images=frames[1:], duration=80, loop=0)
            res = media.run(str(src), "frames", "cover", 90, 480, d, 0)
            got = media.read_frames(res)
            self.assertEqual([ms for _j, ms in got], [80, 80, 80])
            for j, _ms in got:
                self.assertEqual(Image.open(io.BytesIO(j)).size, (480, 480))

    def test_stream_to_panel(self):
        with tempfile.NamedTemporaryFile() as f:
            panel = corsair_lcd.EliteLcd(f.name, 0x0C33)
            a, b = jpeg(), jpeg((0, 0, 255))
            panel.show([(a, 40), (b, 40)])
            time.sleep(0.15)
            panel.blank()
            panel.close()
            data = Path(f.name).read_bytes()
        reports = [data[i:i + 1024] for i in range(0, len(data), 1024)]
        self.assertTrue(reports and all(r[:2] == b"\x02\x05" for r in reports))
        # reassemble the first frame from its chunks
        first, i = b"", 0
        while True:
            r = reports[i]
            first += r[8:8 + int.from_bytes(r[6:8], "little")]
            i += 1
            if r[3] == 1:
                break
        self.assertEqual(first, a)


if __name__ == "__main__":
    unittest.main()
