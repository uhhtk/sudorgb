"""Exclusive-ownership guards for the Kraken hardware.

Two layers make sure only one process ever drives the cooler:

1. :class:`InstanceLock` takes ``flock`` on a per-user lock file, so two copies
   of this service can never run at once, even from different front-ends.
2. :func:`scan` finds *other* programs that already hold the Kraken's device
   nodes (``/dev/hidrawN`` for HID and ``/dev/bus/usb/BBB/DDD`` for the LCD bulk
   interface) by walking ``/proc/*/fd``. It also flags a running OpenKraken
   even before that program has opened the device.

The HID reply stream is shared by every opener of a hidraw node. A second
reader (OpenKraken, or OpenRGB with its "NZXT Kraken" detector enabled)
injects replies into our queue and produces liquidctl's "missing messages"
desync. So any holder counts as a conflict, and the service releases the
device instead of fighting over it.
"""

from __future__ import annotations

import fcntl
import os
from dataclasses import dataclass, field
from pathlib import Path

NZXT_VENDOR = "1e71"
_KNOWN_TOOLS = {
    "openkraken": "OpenKraken",
    "openrgb": "OpenRGB",
    "liquidctl": "liquidctl",
    "coolercontrol": "CoolerControl",
    "coolercontrold": "CoolerControl",
}


@dataclass
class Holder:
    pid: int
    name: str
    tool: str
    nodes: list[str] = field(default_factory=list)

    def to_json(self) -> dict:
        return {"pid": self.pid, "name": self.name, "tool": self.tool, "nodes": self.nodes}


class InstanceLock:
    """Non-blocking per-user ``flock``; released automatically if we die."""

    def __init__(self, path: Path) -> None:
        self.path = path
        self._fd: int | None = None

    def try_acquire(self) -> bool:
        if self._fd is not None:
            return True
        self.path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        fd = os.open(self.path, os.O_RDWR | os.O_CREAT | os.O_CLOEXEC, 0o600)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            os.close(fd)
            return False
        os.ftruncate(fd, 0)
        os.write(fd, f"{os.getpid()}\n".encode())
        self._fd = fd
        return True

    def holder_pid(self) -> int | None:
        try:
            return int(self.path.read_text().strip() or 0) or None
        except (OSError, ValueError):
            return None

    def release(self) -> None:
        if self._fd is not None:
            os.close(self._fd)
            self._fd = None


def kraken_nodes() -> set[str]:
    """Device nodes belonging to any NZXT (vendor 1e71) device."""
    nodes: set[str] = set()
    for hid in Path("/sys/class/hidraw").glob("hidraw*"):
        try:
            uevent = (hid / "device" / "uevent").read_text()
        except OSError:
            continue
        # HID_ID=0003:00001E71:00003012
        for line in uevent.splitlines():
            if line.startswith("HID_ID=") and f":0000{NZXT_VENDOR.upper()}:" in line.upper():
                nodes.add(f"/dev/{hid.name}")
    for usb in Path("/sys/bus/usb/devices").iterdir():
        try:
            if (usb / "idVendor").read_text().strip().lower() != NZXT_VENDOR:
                continue
            bus = int((usb / "busnum").read_text())
            dev = int((usb / "devnum").read_text())
        except (OSError, ValueError):
            continue
        nodes.add(f"/dev/bus/usb/{bus:03d}/{dev:03d}")
    return nodes


def _proc_name(pid: str) -> str:
    try:
        raw = Path(f"/proc/{pid}/cmdline").read_bytes()
    except OSError:
        return ""
    parts = [p.decode(errors="replace") for p in raw.split(b"\0") if p]
    if not parts:
        return ""
    # "python /usr/bin/openkraken" -> "openkraken"
    exe = os.path.basename(parts[0])
    if exe.startswith("python") and len(parts) > 1:
        for arg in parts[1:]:
            if not arg.startswith("-"):
                return os.path.basename(arg)
            if arg == "-m":
                continue
    return exe


def _tool_for(name: str) -> str:
    low = name.lower()
    for key, label in _KNOWN_TOOLS.items():
        if key in low:
            return label
    return name


def scan(exclude_pids: set[int] | None = None) -> list[Holder]:
    """Return other processes that hold Kraken nodes or are known Kraken tools."""
    exclude = {os.getpid()} | (exclude_pids or set())
    nodes = kraken_nodes()
    holders: dict[int, Holder] = {}
    for entry in os.scandir("/proc"):
        if not entry.name.isdigit():
            continue
        pid = int(entry.name)
        if pid in exclude:
            continue
        name = None
        if nodes:
            try:
                fds = os.listdir(f"/proc/{pid}/fd")
            except OSError:
                fds = []  # other users' processes; nothing we can do
            for fd in fds:
                try:
                    target = os.readlink(f"/proc/{pid}/fd/{fd}")
                except OSError:
                    continue
                if target in nodes:
                    name = name or _proc_name(entry.name)
                    h = holders.setdefault(pid, Holder(pid, name, _tool_for(name)))
                    if target not in h.nodes:
                        h.nodes.append(target)
        # OpenKraken grabs the device on its own schedule (and reconnects in a
        # loop), so its mere presence is a conflict even before it opens a node.
        if pid not in holders:
            pname = _proc_name(entry.name)
            if pname == "openkraken" or pname.startswith("openkraken"):
                holders[pid] = Holder(pid, pname, "OpenKraken")
    return sorted(holders.values(), key=lambda h: h.pid)
