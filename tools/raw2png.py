#!/usr/bin/env python3
"""Turn -shot framebuffer dumps into PNGs.

    python3 tools/raw2png.py <dir-or-file> [more...]

A dump is an 8-byte header (width, height as little-endian int32) followed by
width*height*3 bytes of RGB. glReadPixels hands back rows bottom-up, so they
get flipped on the way out. Each <name>.raw becomes <name>.png beside it.

Written against the stdlib only: the machine that reads these is often the Mac,
which cross-compiles but has no business growing a Pillow install for one tool.
"""
import pathlib
import struct
import sys
import zlib


def chunk(tag, body):
    payload = tag + body
    return struct.pack(">I", len(body)) + payload + struct.pack(">I", zlib.crc32(payload))


def convert(src):
    data = src.read_bytes()
    if len(data) < 8:
        raise ValueError(f"{src}: too short to hold a header")
    w, h = struct.unpack_from("<ii", data, 0)
    want = 8 + w * h * 3
    if w <= 0 or h <= 0 or len(data) < want:
        raise ValueError(f"{src}: header says {w}x{h}, needs {want} bytes, has {len(data)}")

    px = data[8:want]
    stride = w * 3
    # b"\x00" is the per-row PNG filter byte (None).
    rows = b"".join(b"\x00" + px[(h - 1 - y) * stride:(h - y) * stride] for y in range(h))

    dst = src.with_suffix(".png")
    dst.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(rows, 6))
        + chunk(b"IEND", b"")
    )
    print(f"{dst}  {w}x{h}")


def main(argv):
    if not argv:
        print(__doc__.strip(), file=sys.stderr)
        return 2

    targets = []
    for arg in argv:
        path = pathlib.Path(arg)
        if path.is_dir():
            targets.extend(sorted(path.glob("*.raw")))
        else:
            targets.append(path)

    if not targets:
        print("no .raw dumps found", file=sys.stderr)
        return 1

    failed = 0
    for src in targets:
        try:
            convert(src)
        except (OSError, ValueError) as exc:
            print(exc, file=sys.stderr)
            failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
