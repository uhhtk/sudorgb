"""LCD media pipeline: turn arbitrary images/GIFs into device-ready files.

Why this exists
---------------
liquidctl's ``KrakenZ3._prepare_gif_file`` stretches every frame to the LCD
resolution (ignoring aspect ratio) and re-encodes the result with Pillow's
defaults. Pillow decodes GIF frames after the first as RGB, so every frame is
quantized against its *own* palette with dithering. That destroys inter-frame
compression: a 661 KB, 54-frame 540x301 GIF becomes a 16.8 MB upload (5.7 s of
CPU, ~160 MB RSS), about 70% of the cooler's 24 MB bucket memory. The bulk
transfer then holds the device for seconds, while anything else on the HID node
(OpenRGB, a second tool) desyncs the reply stream. That combination is what
made GIF mode fall over.

This module instead:

* checks the source before decoding (byte size, pixel count, frame count);
* fits frames to the round panel by aspect ratio (cover or contain);
* builds ONE global palette from sampled frames and quantizes every frame
  against it without dithering, then marks pixels unchanged from the previous
  frame as transparent so each frame stores only what moved (the gifsicle -O3
  trick, done in pure Pillow);
* drops frames rather than slowing animation when the GIF exceeds the frame or
  size budget, and enforces a minimum frame duration;
* writes atomically into a content-addressed cache, so re-applying a profile
  never re-processes.

It runs as a separate, resource-limited, low-priority process
(``python -I media.py ...``). A Pillow crash or runaway allocation kills only
that child and never the hardware service holding the device.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import math
import os
import sys
import tempfile
from pathlib import Path

LCD_DEFAULT = 640
MAX_SOURCE_BYTES = 64 * 1024 * 1024
MAX_SOURCE_PIXELS = 4096 * 4096
MAX_FRAMES = 240
MIN_FRAME_MS = 30
DEFAULT_FRAME_MS = 100
#: Upload budget. The device has ~24.9 MB of bucket memory shared with the
#: sensor-frame ring; staying far below it keeps uploads short (< 2 s) and leaves
#: room so the driver never has to wipe every bucket mid-transfer.
DEFAULT_BUDGET = 8 * 1024 * 1024
#: Panels that the host streams JPEG frames to (Corsair iCUE ELITE LCD).
JPEG_QUALITY = 85
PIPELINE_VERSION = 3  # bump to invalidate cached outputs


class MediaError(Exception):
    """A user-facing problem with the media file (never a device problem)."""


def _pil():
    from PIL import Image, ImageSequence  # noqa: F401  (import check)

    Image.MAX_IMAGE_PIXELS = MAX_SOURCE_PIXELS
    return Image


def cache_key(src: Path, kind: str, fit: str, rotation: int, size: int) -> str:
    h = hashlib.sha256()
    with open(src, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    h.update(f"|{kind}|{fit}|{rotation}|{size}|v{PIPELINE_VERSION}".encode())
    return h.hexdigest()[:24]


def _fit(frame, size: int, fit: str):
    """Scale an RGB frame onto a size x size black canvas."""
    Image = _pil()
    w, h = frame.size
    if fit == "stretch":
        return frame.resize((size, size), Image.Resampling.LANCZOS)
    scale = (max if fit == "cover" else min)(size / w, size / h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    scaled = frame.resize((nw, nh), Image.Resampling.LANCZOS)
    if fit == "cover":
        left, top = (nw - size) // 2, (nh - size) // 2
        return scaled.crop((left, top, left + size, top + size))
    canvas = Image.new("RGB", (size, size), (0, 0, 0))
    canvas.paste(scaled, ((size - nw) // 2, (size - nh) // 2))
    return canvas


def _flatten(frame):
    """RGBA/P/LA -> RGB composited over black (the panel's background)."""
    Image = _pil()
    rgba = frame.convert("RGBA")
    bg = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
    return Image.alpha_composite(bg, rgba).convert("RGB")


def _durations(img) -> list[int]:
    out = []
    for i in range(img.n_frames):
        img.seek(i)
        d = img.info.get("duration") or DEFAULT_FRAME_MS
        out.append(int(d) if d >= 10 else DEFAULT_FRAME_MS)  # 0/1 ms -> browser default
    img.seek(0)
    return out


def _check_source(src: Path):
    if not src.is_file():
        raise MediaError(f"File not found: {src}")
    nbytes = src.stat().st_size
    if nbytes == 0:
        raise MediaError("File is empty")
    if nbytes > MAX_SOURCE_BYTES:
        raise MediaError(f"File is {nbytes / 1e6:.1f} MB; the limit is {MAX_SOURCE_BYTES // (1024 * 1024)} MB")
    Image = _pil()
    try:
        img = Image.open(src)
        img.verify()  # cheap structural check, no full decode
        img = Image.open(src)
    except Image.DecompressionBombError as exc:
        raise MediaError(f"Image dimensions too large: {exc}") from None
    except Exception as exc:  # UnidentifiedImageError, truncated headers, ...
        raise MediaError(f"Not a readable image: {exc}") from None
    w, h = img.size
    if w * h > MAX_SOURCE_PIXELS:
        raise MediaError(f"Image is {w}x{h}; the limit is 4096x4096")
    return img


def process_static(src: Path, dst: Path, fit: str, size: int) -> dict:
    img = _check_source(src)
    img.seek(0)
    frame = _fit(_flatten(img), size, fit)
    _atomic_save(dst, lambda p: frame.save(p, format="PNG", optimize=True))
    return {"path": str(dst), "frames": 1, "bytes": dst.stat().st_size, "size": size}


def process_gif(src: Path, dst: Path, fit: str, rotation: int, size: int, budget: int) -> dict:
    Image = _pil()
    img = _check_source(src)
    n = getattr(img, "n_frames", 1)
    durs = _durations(img) if n > 1 else [DEFAULT_FRAME_MS]

    # Decimation factor: enough to respect both the frame cap and the minimum
    # per-frame duration (average). Dropping frames keeps playback speed intact.
    avg = sum(durs) / len(durs)
    base_k = max(1, math.ceil(n / MAX_FRAMES), math.ceil(MIN_FRAME_MS / max(avg, 1)))

    # One global palette from up to 16 evenly spaced, downscaled frames.
    sample_idx = sorted({round(i * (n - 1) / 15) for i in range(16)}) if n > 1 else [0]
    tile = 160
    cols = math.ceil(math.sqrt(len(sample_idx)))
    montage = Image.new("RGB", (tile * cols, tile * math.ceil(len(sample_idx) / cols)))
    for j, i in enumerate(sample_idx):
        img.seek(i)
        thumb = _fit(_flatten(img), tile, fit)
        montage.paste(thumb, ((j % cols) * tile, (j // cols) * tile))

    attempts = [(255, base_k), (192, base_k), (128, base_k), (128, base_k * 2), (64, base_k * 2), (64, base_k * 4)]
    last_size = None
    for colors, k in attempts:
        if n > 1 and n // k < 2:
            continue
        frames, out_durs = _encode_frames(img, montage, n, k, durs, colors, size, fit, rotation)
        tmp = dst.with_suffix(".partial")
        frames[0].save(
            tmp,
            format="GIF",
            save_all=True,
            append_images=frames[1:],
            duration=out_durs,
            loop=0,
            disposal=1,  # keep previous frame; transparent pixels show it through
            transparency=colors,
            optimize=False,
        )
        last_size = tmp.stat().st_size
        if last_size <= budget:
            os.replace(tmp, dst)
            _validate_gif(dst, size)
            return {
                "path": str(dst),
                "frames": len(frames),
                "source_frames": n,
                "duration_ms": sum(out_durs),
                "colors": colors,
                "bytes": last_size,
                "size": size,
            }
        tmp.unlink(missing_ok=True)
    raise MediaError(
        f"GIF is still {last_size / 1e6:.1f} MB after optimisation (budget {budget / 1e6:.0f} MB). "
        "Try a shorter clip or one with fewer moving areas."
    )


def process_frames(src: Path, dst: Path, fit: str, rotation: int, size: int) -> dict:
    """Any still or animated image -> concatenated JPEG frames plus an index.

    For panels without on-device GIF playback: the host streams one JPEG per
    frame. The index holds [offset, length, duration_ms] per frame (duration 0
    for a still). Frames are dropped (not slowed) to respect the frame cap and
    the minimum frame time, like process_gif.
    """
    img = _check_source(src)
    n = getattr(img, "n_frames", 1)
    durs = _durations(img) if n > 1 else [0]
    k = max(1, math.ceil(n / MAX_FRAMES), math.ceil(MIN_FRAME_MS / max(sum(durs) / len(durs), 1))) if n > 1 else 1
    index: list[list[int]] = []

    def write(path):
        index.clear()
        with open(path, "wb") as f:
            for start in range(0, n, k):
                img.seek(start)
                rgb = _fit(_flatten(img), size, fit)
                if rotation:
                    rgb = rgb.rotate(-rotation)
                buf = io.BytesIO()
                rgb.save(buf, format="JPEG", quality=JPEG_QUALITY)
                data = buf.getvalue()
                ms = max(MIN_FRAME_MS, sum(durs[start : start + k])) if n > 1 else 0
                index.append([f.tell(), len(data), ms])
                f.write(data)

    _atomic_save(dst, write)
    return {"path": str(dst), "frames": len(index), "source_frames": n, "index": index,
            "duration_ms": sum(i[2] for i in index), "bytes": dst.stat().st_size, "size": size}


def read_frames(meta: dict) -> list[tuple[bytes, int]]:
    """Load a process_frames() result as [(jpeg, duration_ms), ...]."""
    data = Path(meta["path"]).read_bytes()
    return [(data[o : o + n], ms) for o, n, ms in meta["index"]]


def _encode_frames(img, montage, n, k, durs, colors, size, fit, rotation):
    """Quantize frames against one palette; unchanged pixels become transparent.

    Index ``colors`` (one past the palette) is reserved as the transparent index.
    With ``disposal=1`` a transparent pixel shows the previous frame, so each
    frame only stores what changed, as long runs of one index that LZW
    collapses. Verified lossless: decoded frames are pixel-identical to the
    directly quantized frames.
    """
    from PIL import ImageChops

    Image = _pil()
    palette = montage.quantize(colors=colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal_bytes = palette.getpalette()[: 3 * colors] + [0, 0, 0] * (256 - colors)
    frames, out_durs, prev = [], [], None
    for start in range(0, n, k):
        img.seek(start)
        rgb = _fit(_flatten(img), size, fit)
        if rotation:
            rgb = rgb.rotate(-rotation)
        q = rgb.quantize(palette=palette, dither=Image.Dither.NONE)
        out = q
        if prev is not None:
            cur_l = Image.frombytes("L", q.size, q.tobytes())
            prev_l = Image.frombytes("L", q.size, prev.tobytes())
            unchanged = ImageChops.difference(cur_l, prev_l).point(lambda v: 255 if v == 0 else 0)
            out = q.copy()
            out.paste(colors, mask=unchanged)
        out.putpalette(pal_bytes)
        frames.append(out)
        prev = q
        out_durs.append(max(MIN_FRAME_MS, sum(durs[start : start + k])))
    return frames, out_durs


def _validate_gif(path: Path, size: int) -> None:
    Image = _pil()
    with Image.open(path) as chk:
        if chk.size != (size, size) or chk.format != "GIF":
            raise MediaError("Internal error: processed GIF failed validation")


def _atomic_save(dst: Path, writer) -> None:
    fd, tmp = tempfile.mkstemp(dir=dst.parent, prefix=".tmp-", suffix=dst.suffix)
    os.close(fd)
    try:
        writer(tmp)
        os.replace(tmp, dst)
    finally:
        if os.path.exists(tmp):
            os.unlink(tmp)


def run(src: str, kind: str, fit: str, rotation: int, size: int, cache_dir: str, budget: int) -> dict:
    if fit not in ("cover", "contain", "stretch"):
        raise MediaError(f"Unknown fit mode {fit!r}")
    if rotation not in (0, 90, 180, 270):
        raise MediaError(f"Invalid rotation {rotation}")
    srcp = Path(src).expanduser()
    cache = Path(cache_dir)
    cache.mkdir(parents=True, exist_ok=True)
    key = cache_key(srcp, kind, fit, rotation, size) if srcp.is_file() else ""
    ext = {"gif": ".gif", "frames": ".mjpg"}.get(kind, ".png")
    dst = cache / f"{kind}-{key}{ext}"
    meta = dst.with_suffix(dst.suffix + ".json")
    if key and dst.is_file():
        try:
            res = json.loads(meta.read_text())
        except (OSError, ValueError):
            res = {"path": str(dst), "bytes": dst.stat().st_size, "size": size}
        res["cached"] = True
        return res
    if kind == "gif":
        res = process_gif(srcp, dst, fit, rotation, size, budget)
    elif kind == "frames":
        res = process_frames(srcp, dst, fit, rotation, size)
    else:
        res = process_static(srcp, dst, fit, size)
    _atomic_save(meta, lambda p: Path(p).write_text(json.dumps(res)))
    res["cached"] = False
    return res


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="Prepare media for a cooler LCD")
    ap.add_argument("--src", required=True)
    ap.add_argument("--kind", choices=("gif", "image", "frames"), required=True)
    ap.add_argument("--fit", default="cover")
    ap.add_argument("--rotation", type=int, default=0)
    ap.add_argument("--size", type=int, default=LCD_DEFAULT)
    ap.add_argument("--cache-dir", required=True)
    ap.add_argument("--budget", type=int, default=DEFAULT_BUDGET)
    a = ap.parse_args(argv)
    try:
        res = run(a.src, a.kind, a.fit, a.rotation, a.size, a.cache_dir, a.budget)
        print(json.dumps({"ok": True, "result": res}))
        return 0
    except MediaError as exc:
        print(json.dumps({"ok": False, "error": str(exc)}))
        return 2
    except MemoryError:
        print(json.dumps({"ok": False, "error": "Out of memory while decoding the image"}))
        return 2


if __name__ == "__main__":
    sys.exit(main())
