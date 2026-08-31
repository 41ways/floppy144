#!/usr/bin/env python3
"""Cut a window out of a -shot dump and blow it up, for looking at one thing.

    python3 tools/crop.py <file.raw> <x> <y> <w> <h> [zoom] [out.png]

A whole 1280x720 capture of a monster nine metres away is forty pixels of
monster, which is not enough to judge a shape by. Coordinates are top-left
origin, matching what you read off the PNG. Stdlib only, like raw2png.
"""
import pathlib
import struct
import sys
import zlib


def chunk(tag, body):
    payload = tag + body
    return struct.pack(">I", len(body)) + payload + struct.pack(">I", zlib.crc32(payload))


def crop(src, x0, y0, cw, ch, zoom, dst):
    data = pathlib.Path(src).read_bytes()
    w, h = struct.unpack_from("<ii", data, 0)
    px = data[8:]
    if not (0 <= x0 < w and 0 <= y0 < h):
        raise ValueError(f"{src}: {x0},{y0} is outside {w}x{h}")
    cw = min(cw, w - x0)
    ch = min(ch, h - y0)
    stride = w * 3

    rows = []
    for y in range(y0, y0 + ch):
        base = (h - 1 - y) * stride          # dumps are bottom-up
        line = px[base + x0 * 3: base + (x0 + cw) * 3]
        wide = b"".join(line[i * 3:i * 3 + 3] * zoom for i in range(cw))
        rows.extend([b"\x00" + wide] * zoom)

    pathlib.Path(dst).write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", cw * zoom, ch * zoom, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(b"".join(rows), 6))
        + chunk(b"IEND", b"")
    )
    print(f"{dst}  {cw * zoom}x{ch * zoom}  from {src} at {x0},{y0}")


def main(argv):
    if len(argv) < 5:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    src = argv[0]
    x0, y0, cw, ch = (int(v) for v in argv[1:5])
    zoom = int(argv[5]) if len(argv) > 5 else 2
    dst = argv[6] if len(argv) > 6 else str(pathlib.Path(src).with_suffix(".crop.png"))
    try:
        crop(src, x0, y0, cw, ch, zoom, dst)
    except (OSError, ValueError) as exc:
        print(exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
