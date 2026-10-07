#!/usr/bin/env python3
"""Convierte las capturas PPM del simulador a PNG (solo biblioteca estandar)."""
import pathlib
import struct
import sys
import zlib


def ppm_to_png(src: pathlib.Path, dst: pathlib.Path) -> None:
    data = src.read_bytes()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pixels = parts[3]
    raw = b"".join(b"\x00" + pixels[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    dst.write_bytes(png)


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        src = pathlib.Path(arg)
        ppm_to_png(src, src.with_suffix(".png"))
        src.unlink()
