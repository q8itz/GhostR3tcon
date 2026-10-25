#!/usr/bin/env python3
"""Copy the two executables codegen needs from your own GRAW disc image.

    scripts/extract-game-files.py "Ghost Recon Advanced Warfighter.iso"

Writes assets/default.xex and assets/Yeti_Final_Xenon.exe. Both are checked
against the USA/Europe release (disc US-2005), the one the hints were made for.
Keep FILES in sync with kDiscFiles in src/graw_disc.h.
The assets folder is git-ignored and never leaves your machine.
"""
import hashlib
import pathlib
import struct
import sys

FILES = {
    "default.xex": "09619b6967b086f9eb7b0d48dffdb14142ae4faa",
    "Yeti_Final_Xenon.exe": "5243428702092fa5a99a9ee5e1542ad71b003245",
}
MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048
# Game partition offsets for XGD1, XGD2 and XGD3 discs.
PARTITION_OFFSETS = (0, 0xFD90000, 0x2080000, 0x18300000)


def find_partition(f):
    for offset in PARTITION_OFFSETS:
        f.seek(offset + 32 * SECTOR)
        if f.read(len(MAGIC)) == MAGIC:
            root_sector, root_size = struct.unpack("<II", f.read(8))
            return offset, root_sector, root_size
    sys.exit("not an Xbox 360 disc image (no XDVDFS volume found)")


def read_root(f, base, sector, size):
    """Returns {lower-case name: (start sector, size)} for the root directory."""
    f.seek(base + sector * SECTOR)
    data = f.read(size)
    entries = {}
    pos = 0
    while pos + 14 <= len(data):
        left, right, start, length, attributes, name_length = struct.unpack_from(
            "<HHIIBB", data, pos)
        if left == 0xFFFF and right == 0xFFFF:
            # Padding to the end of the sector.
            pos = (pos // SECTOR + 1) * SECTOR
            continue
        name = data[pos + 14:pos + 14 + name_length].decode("latin1")
        entries[name.lower()] = (start, length)
        pos = (pos + 14 + name_length + 3) & ~3
    return entries


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    out_dir = pathlib.Path(__file__).resolve().parent.parent / "assets"
    out_dir.mkdir(exist_ok=True)
    with open(sys.argv[1], "rb") as f:
        base, root_sector, root_size = find_partition(f)
        root = read_root(f, base, root_sector, root_size)
        for name, expected_sha1 in FILES.items():
            if name.lower() not in root:
                sys.exit(f"{name} is not on this disc")
            start, length = root[name.lower()]
            f.seek(base + start * SECTOR)
            data = f.read(length)
            sha1 = hashlib.sha1(data).hexdigest()
            if sha1 != expected_sha1:
                sys.exit(f"{name} doesn't match the supported release (SHA-1 {sha1})")
            (out_dir / name).write_bytes(data)
            print(f"wrote {out_dir / name}")


if __name__ == "__main__":
    main()
