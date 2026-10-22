#!/usr/bin/env python3
"""Align Samsung NV12/NV21 preview planes with the Adreno gralloc ABI (32 px).

mm_stream_calc_offset_preview switches on the camera HAL format. Its shared
NV12/NV21 case rounds the Y and UV plane strides to 16 pixels with
`adds Rn, #15` followed by `bic.w Rd, Rn, #15`; the current allocator needs 32,
so only those two immediates per plane change to 31. The function is found
through its exported symbol, the case through the switch's TBH jump table and
the pairs by their encoding, so any build with this code shape is handled. A
library whose NV12/NV21 case does not have exactly two such pairs is refused.
"""
from pathlib import Path
import struct
import sys

SYMBOL = b'mm_stream_calc_offset_preview'
PAIR_WINDOW = 12  # bytes searched after the adds for its bic.w
PLANES = 2        # Y and UV
FORMAT_NV12, FORMAT_NV21 = 1, 2  # cam_format_t, camera HAL interface ABI


def elf_sections(data):
    if data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
        raise ValueError('expected little-endian ARM ELF32')
    shoff = struct.unpack_from('<I', data, 32)[0]
    shentsize, shnum = struct.unpack_from('<HH', data, 46)
    return [struct.unpack_from('<IIIIIIIIII', data, shoff + i * shentsize) for i in range(shnum)]


def function_range(data):
    """File offsets [start, end) of SYMBOL, from .dynsym and the LOAD segments."""
    sections = elf_sections(data)
    for _, kind, _, _, offset, size, link, _, _, entsize in sections:
        if kind != 11:  # SHT_DYNSYM
            continue
        strtab = sections[link][4]
        for pos in range(offset, offset + size, entsize):
            name, value, length = struct.unpack_from('<III', data, pos)
            end = data.index(b'\0', strtab + name)
            if data[strtab + name:end] == SYMBOL:
                address = value & ~1  # Thumb bit
                return file_offset(data, address, length), length
    raise ValueError('%s not exported; unknown camera interface' % SYMBOL.decode())


def file_offset(data, address, size):
    table = struct.unpack_from('<I', data, 28)[0]
    entry_size, count = struct.unpack_from('<HH', data, 42)
    for index in range(count):
        kind, offset, vaddr, _, length = struct.unpack_from('<IIIII', data, table + index * entry_size)
        if kind == 1 and vaddr <= address and address + size <= vaddr + length:
            return offset + address - vaddr
    raise ValueError('function outside a file-backed LOAD segment')


def rounding_pairs(data, start, length):
    """(adds offset, bic.w offset, immediate) for every `adds Rn,#imm; bic.w Rd,Rn,#imm`."""
    pairs = []
    for pos in range(start, start + length - 1, 2):
        imm, op = data[pos], data[pos + 1]
        if op & 0xf8 != 0x30 or imm not in (15, 31):  # T1 ADDS Rn, #imm8
            continue
        rn = op & 7
        for bic in range(pos + 2, min(pos + 2 + PAIR_WINDOW, start + length - 3), 2):
            hw1 = struct.unpack_from('<H', data, bic)[0]
            hw2 = struct.unpack_from('<H', data, bic + 2)[0]
            # T1 BIC (immediate): 11110 i 0 0001 S Rn | 0 imm3 Rd imm8, plain imm8.
            if hw1 == 0xf020 | rn and hw2 & 0x70ff == imm:
                pairs.append((pos, bic + 2, imm))
                break
    return pairs


def nv21_case(data, start, length):
    """File range [case, next case) of the shared NV12/NV21 switch case."""
    for tbh in range(start, start + length - 3, 2):
        hw1, hw2 = struct.unpack_from('<HH', data, tbh)
        if hw1 != 0xe8df or hw2 & 0xfff0 != 0xf010:  # TBH [pc, Rm, lsl #1]
            continue
        rm = hw2 & 0xf
        cmp = next((p for p in range(tbh - 2, max(start, tbh - 12) - 1, -2)
                    if data[p + 1] == 0x28 | rm), None)  # T1 CMP Rm, #imm8
        if cmp is None:
            break
        entries = data[cmp] + 1
        bias = 0
        for p in range(cmp - 2, start - 2, -2):
            hw = struct.unpack_from('<H', data, p)[0]
            if hw & 0xfe00 == 0x1e00 and hw & 7 == rm:  # T1 SUBS Rm, Rn, #imm3
                bias = (hw >> 6) & 7
                break
        table = tbh + 4
        targets = [table + 2 * struct.unpack_from('<H', data, table + 2 * i)[0] for i in range(entries)]
        if not all(0 <= f - bias < entries for f in (FORMAT_NV12, FORMAT_NV21)):
            break
        case = targets[FORMAT_NV21 - bias]
        if targets[FORMAT_NV12 - bias] != case:
            raise ValueError('NV12 and NV21 no longer share one preview case; refusing to patch')
        following = [t for t in targets if t > case]
        return case, min(following) if following else start + length
    raise ValueError('no format switch found in %s; refusing to patch' % SYMBOL.decode())


def fix(data):
    start, length = function_range(data)
    case, end = nv21_case(data, start, length)
    pairs = rounding_pairs(data, case, end - case)
    if len(pairs) != PLANES:
        raise ValueError('expected %d stride roundings in the NV12/NV21 case, found %d; '
                         'refusing to patch' % (PLANES, len(pairs)))
    result = bytearray(data)
    for adds, bic, _ in pairs:
        result[adds] = 31
        result[bic] = 31
    return bytes(result), all(imm == 31 for _, _, imm in pairs)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('usage: fix_preview_alignment.py libmmcamera_interface.so')
    path = Path(sys.argv[1])
    data = path.read_bytes()
    result, already = fix(data)
    if result != data:
        path.write_bytes(result)
    print('NV21 preview alignment: %s' % ('already 32 px' if already else 'patched to 32 px'))
