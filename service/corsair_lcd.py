"""Corsair LCD panels over hidraw: iCUE ELITE LCD (Elite Capellix cap), NAUTILUS
LCD cap, XC7 ELITE LCD block, iCUE LINK AIO / XD5 LCD.

Protocol facts, taken from OpenLinkHub (GPL-3.0, github.com/jurkovic-nikola/
OpenLinkHub: src/devices/cc, lsh, nautilusLcd, xc7); no code was copied:

* HID interface 0, 480x480 panel, one shared frame protocol (table below).
* The host streams baseline JPEG frames; the panel has no stored animation.
  A frame goes out as 1024-byte output reports:
  ``02 05 <kind> <last> <chunk#> 00 <len lo> <len hi>`` + up to 1016 JPEG bytes,
  where ``<last>`` is 1 on the final chunk (that is what makes it render) and
  ``<kind>`` is 01 for iCUE LINK panels, 00 otherwise.
* Feature report ``03 0b <brightness> 01`` sets the backlight (default 0x40).
* On shutdown OpenLinkHub sends ``03 1e 01 01`` then ``03 1d 00 01``, handing
  the panel back to its firmware screen.
* A still image is re-sent continuously (OpenLinkHub: every 100 ms).

Nothing here writes firmware or persistent settings.
"""

from __future__ import annotations

import fcntl
import os
import threading
import time
from pathlib import Path

VENDOR = 0x1B1C
#: pid -> (name, header byte 2)
PRODUCTS = {
    0x0C33: ("Corsair iCUE ELITE LCD", 0x00),
    0x0C39: ("Corsair iCUE ELITE LCD", 0x00),
    0x0C55: ("Corsair NAUTILUS LCD", 0x00),
    0x0C57: ("Corsair NAUTILUS LCD", 0x00),
    0x0C42: ("Corsair XC7 ELITE LCD", 0x00),
    0x0C4E: ("Corsair iCUE LINK AIO LCD", 0x01),
    0x0C43: ("Corsair iCUE LINK XD5 LCD", 0x01),
}
SIZE = 480
REPORT = 1024
HEADER = 8
CHUNK = REPORT - HEADER
#: Re-send interval for a still image. ponytail: copied from OpenLinkHub's 100 ms;
#: the firmware's real timeout is unknown, raise this if the panel tolerates it.
STILL_RESEND_S = 0.1
RELEASE_REPORTS = (bytes([0x03, 0x1E, 0x01, 0x01]), bytes([0x03, 0x1D, 0x00, 0x01]))


def packets(jpeg: bytes, kind: int = 0) -> list[bytes]:
    """Split one JPEG into output reports. The final chunk is always flagged,
    including when the JPEG length is an exact multiple of the chunk size."""
    if not jpeg:
        raise ValueError("empty frame")
    count = (len(jpeg) + CHUNK - 1) // CHUNK
    if count > 255:
        raise ValueError("frame too large")  # chunk index is one byte
    out = []
    for i in range(count):
        part = jpeg[i * CHUNK : (i + 1) * CHUNK]
        head = bytes([0x02, 0x05, kind, 1 if i == count - 1 else 0, i, 0x00, len(part) & 0xFF, len(part) >> 8])
        out.append((head + part).ljust(REPORT, b"\0"))
    return out


def brightness_report(value: int) -> bytes:
    return bytes([0x03, 0x0B, max(0, min(100, int(value))), 0x01])


def _hidiocsfeature(length: int) -> int:
    return (3 << 30) | (length << 16) | (ord("H") << 8) | 0x06


def find() -> list[dict]:
    """Interface-0 hidraw nodes of connected ELITE LCD panels."""
    found = []
    for hid in sorted(Path("/sys/class/hidraw").glob("hidraw*")):
        try:
            uevent = (hid / "device" / "uevent").read_text()
            ids = next(ln[7:] for ln in uevent.splitlines() if ln.startswith("HID_ID="))
            _bus, vid, pid = (int(x, 16) for x in ids.split(":"))
            if vid != VENDOR or pid not in PRODUCTS:
                continue
            iface = int((hid / "device" / ".." / "bInterfaceNumber").read_text(), 16)
        except (OSError, StopIteration, ValueError):
            continue
        if iface == 0:
            found.append({"node": f"/dev/{hid.name}", "vid": vid, "pid": pid, "name": PRODUCTS[pid][0]})
    return found


class EliteLcd:
    """One panel. A background thread plays the current frame list; every
    device write goes through ``self._io`` so frames and reports never interleave."""

    def __init__(self, node: str, pid: int) -> None:
        self.node = node
        self.kind = PRODUCTS[pid][1]
        self.fd = os.open(node, os.O_RDWR | os.O_CLOEXEC)
        self._io = threading.Lock()
        self._frames: list[tuple[bytes, int]] = []
        self._wake = threading.Event()
        self._idle = threading.Event()  # set while the player has nothing to show
        self._stop = False
        self.error: str | None = None
        self._thread = threading.Thread(target=self._play, name=f"lcd-{os.path.basename(node)}", daemon=True)
        self._thread.start()

    def _write(self, data: bytes) -> None:
        with self._io:
            os.write(self.fd, data)

    def _feature(self, data: bytes) -> None:
        buf = bytearray(data)
        with self._io:
            fcntl.ioctl(self.fd, _hidiocsfeature(len(buf)), buf)

    def set_brightness(self, value: int) -> None:
        self._feature(brightness_report(value))

    def show(self, frames: list[tuple[bytes, int]]) -> None:
        """Play frames ([(jpeg, ms)]; one frame = still) until replaced."""
        for jpeg, _ms in frames:
            packets(jpeg, self.kind)  # validate before the player sees it
        self._frames = list(frames)
        self._wake.set()

    def blank(self) -> None:
        """Stop streaming; returns once the frame in flight has been sent."""
        self._idle.clear()
        self._frames = []
        self._wake.set()
        self._idle.wait(timeout=2)

    def _play(self) -> None:
        while not self._stop:
            frames = self._frames
            if not frames:
                self._idle.set()
                self._wake.wait()
                self._wake.clear()
                continue
            for jpeg, ms in frames:
                t = time.monotonic()
                try:
                    for pkt in packets(jpeg, self.kind):
                        self._write(pkt)
                except OSError as exc:
                    self.error = os.strerror(exc.errno or 0) or str(exc)
                    self._stop = True
                    return
                wait = (ms / 1000 if ms else STILL_RESEND_S) - (time.monotonic() - t)
                if self._wake.wait(max(0.0, wait)):
                    self._wake.clear()
                    break  # new content: start it immediately
                if self._stop:
                    return

    def release(self) -> None:
        """Hand the panel back to its firmware screen (call after blank())."""
        try:
            for rep in RELEASE_REPORTS:
                self._feature(rep)
        except OSError:
            pass

    def close(self) -> None:
        """Stop playback, hand the panel back to its firmware screen, close."""
        self._stop = True
        self._wake.set()
        self._thread.join(timeout=2)
        self.release()
        os.close(self.fd)


if __name__ == "__main__":  # self-check, no hardware
    assert len(packets(b"x")) == 1 and packets(b"x")[0][:8] == bytes([2, 5, 0, 1, 0, 0, 1, 0])
    p = packets(bytes(CHUNK * 2))
    assert len(p) == 2 and p[0][3] == 0 and p[1][3] == 1 and p[1][4] == 1 and p[1][6:8] == bytes([0xF8, 0x03])
    assert all(len(x) == REPORT for x in p)
    assert packets(b"x", 1)[0][2] == 1
    assert brightness_report(250) == bytes([3, 0x0B, 100, 1])
    print("ok")
