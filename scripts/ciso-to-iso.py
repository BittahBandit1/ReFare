#!/usr/bin/env python3
"""Expand a GameCube block-map CISO to a sparse ISO without changing the source.

Format reference: Dolphin Source/Core/DiscIO/CISOBlob.{h,cpp}.
This supports GameCube CISO, not PSP CSO or Wii disc images.
"""
import argparse
from pathlib import Path
import struct


def convert(source: Path, destination: Path) -> None:
    disc_size = 1_459_978_240
    with source.open('rb') as src:
        header = src.read(0x8000)
        if len(header) != 0x8000 or header[:4] != b'CISO':
            raise ValueError('Expected a GameCube block-map CISO header')
        block_size = struct.unpack_from('<I', header, 4)[0]
        blocks = header[8:]
        if not 0 < block_size <= 32 * 1024 * 1024 or set(blocks) - {0, 1}:
            raise ValueError('Invalid CISO block size or map')
        expected = 0x8000 + sum(blocks) * block_size
        if source.stat().st_size < expected:
            raise ValueError('Truncated CISO payload')
        if not blocks[0]:
            raise ValueError('Missing disc header block')
        first = src.read(min(block_size, 0x440))
        if len(first) < 0x20 or first[0x1c:0x20] != bytes.fromhex('c2339f3d'):
            raise ValueError('Expected a GameCube disc')
        src.seek(0x8000)
        # Exclusive creation protects existing images. Absent blocks stay sparse.
        with destination.open('xb') as out:
            for index, present in enumerate(blocks):
                if not present:
                    continue
                offset = index * block_size
                if offset >= disc_size:
                    raise ValueError('CISO contains data beyond a GameCube disc')
                data = src.read(block_size)
                if len(data) != block_size:
                    raise ValueError('Truncated CISO block')
                out.seek(offset)
                out.write(data[:min(block_size, disc_size - offset)])
            out.truncate(disc_size)
    print(f'Created {destination} ({disc_size} logical bytes)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    try:
        convert(args.source, args.destination)
    except (OSError, ValueError) as error:
        parser.exit(1, f'error: {error}\n')
