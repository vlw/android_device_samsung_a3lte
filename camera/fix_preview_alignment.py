#!/usr/bin/env python3
"""Align CSG3 NV12/NV21 preview planes with the A300FU Adreno gralloc ABI.

Only four Thumb instruction immediates in mm_stream_calc_offset_preview change.
Keep the stock format switch, heights, offsets and frame-length calculation.
Fail closed on any donor other than the verified A300FUXXU1CSG3 library.
"""
import hashlib
from pathlib import Path
import struct
import sys

STOCK_SHA256 = '63b2340985195ca466ec558fb615561cde3952f4b86c669442fc884ac0cfc957'
PATCHES = (
    (0x75ea, bytes.fromhex('0f37'), bytes.fromhex('1f37')),  # Y: width + 31
    (0x75f0, bytes.fromhex('27f00f01'), bytes.fromhex('27f01f01')),  # Y: & ~31
    (0x761c, bytes.fromhex('0f36'), bytes.fromhex('1f36')),  # UV: width + 31
    (0x761e, bytes.fromhex('26f00f01'), bytes.fromhex('26f01f01')),  # UV: & ~31
)


def file_offset(data, address, size):
    if data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
        raise ValueError('expected little-endian ARM ELF32')
    table = struct.unpack_from('<I', data, 28)[0]
    entry_size, count = struct.unpack_from('<HH', data, 42)
    for index in range(count):
        kind, offset, vaddr, _, length = struct.unpack_from('<IIIII', data, table + index * entry_size)
        if kind == 1 and vaddr <= address and address + size <= vaddr + length:
            return offset + address - vaddr
    raise ValueError('patch address outside a file-backed LOAD segment')


def fix(data):
    original = bytearray(data)
    for address, before, after in PATCHES:
        offset = file_offset(data, address, len(before))
        found = data[offset:offset + len(before)]
        if found not in (before, after):
            raise ValueError('unexpected instruction at %#x' % address)
        original[offset:offset + len(before)] = before
    if hashlib.sha256(original).hexdigest() != STOCK_SHA256:
        raise ValueError('unrecognised stock camera interface; refusing to patch')
    result = bytearray(original)
    for address, before, after in PATCHES:
        offset = file_offset(original, address, len(before))
        result[offset:offset + len(after)] = after
    return bytes(result)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: fix_preview_alignment.py libmmcamera_interface.so')
    path = Path(sys.argv[1])
    data = path.read_bytes()
    result = fix(data)
    if result != data:
        path.write_bytes(result)
    print('NV21 preview alignment verified: ' + hashlib.sha256(result).hexdigest())
