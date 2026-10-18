#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare supplied CSG3 firmware for the sensor-free ExifManager oracle.

Requires pyelftools. Creates diagnostic inputs, not a production library.
StockExifOracle.cpp calls only ExifManager/MakerNote; it does not run obsolete
camera/UI dependencies or ELF initializers. The firmware hashes are pinned.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from elftools.elf.elffile import ELFFile


class Elf:
    def __init__(self, path):
        self.file = open(path, "rb")
        self.elf = ELFFile(self.file)
        self.symbols = self.elf.get_section_by_name(".dynsym")
        self.segments = [p for p in self.elf.iter_segments() if p["p_type"] == "PT_LOAD"]

    def read(self, address, size):
        for segment in self.segments:
            start = segment["p_vaddr"]
            if start <= address and address + size <= start + segment["p_filesz"]:
                return segment.data()[address - start:address - start + size]
        return bytes(size)

    def relocations(self):
        for section in self.elf.iter_sections():
            if section["sh_type"] == "SHT_REL":
                for relocation in section.iter_relocations():
                    yield relocation["r_offset"], relocation["r_info"], None
            elif section.name == ".rel.dyn" and section.data()[:4] == b"APS2":
                # CSG3 uses Android packed REL: signed LEB128 delta groups.
                data = section.data()
                position = 4

                def sleb():
                    nonlocal position
                    value = shift = 0
                    while True:
                        byte = data[position]
                        position += 1
                        value |= (byte & 127) << shift
                        shift += 7
                        if not byte & 128:
                            return value - (1 << shift) if byte & 64 else value

                count, offset = sleb(), sleb()
                done = 0
                while done < count:
                    size, flags = sleb(), sleb()
                    if flags & ~3:
                        raise ValueError("Expected packed REL without explicit addends")
                    delta = sleb() if flags & 2 else 0
                    info = sleb() if flags & 1 else 0
                    for _ in range(size):
                        offset += delta if flags & 2 else sleb()
                        if not flags & 1:
                            info = sleb()
                        yield offset, info, None
                    done += size
                if done != count:
                    raise ValueError("Invalid packed relocation count")

    def constants(self):
        result = {}
        for symbol in self.symbols.iter_symbols():
            if ("CameraParameters" in symbol.name
                    and symbol["st_info"]["type"] == "STT_OBJECT"
                    and symbol["st_shndx"] != "SHN_UNDEF"):
                result[symbol.name] = self.read(
                    symbol["st_value"], symbol["st_size"]).split(b"\0")[0].decode("ascii")
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("stock", type=Path, help="Directory with CSG3 camera libraries")
    parser.add_argument("output", type=Path, help="Output file prefix")
    args = parser.parse_args()
    expected = {
        "libseccameracore.so": "148978b5cd593907a48396b1e8196d2b96172df0c849af24650bcc29b4e4dfc1",
        "libcamera_client.so": "c6a78f91211cb82c2920035b303bf1106d01721c944054f33f33edeb9c91c149",
    }
    for name, digest in expected.items():
        if hashlib.sha256((args.stock / name).read_bytes()).hexdigest() != digest:
            raise SystemExit("Unsupported firmware: " + name)
    core = Elf(args.stock / "libseccameracore.so")
    size = (max(p["p_vaddr"] + p["p_memsz"] for p in core.segments) + 4095) & ~4095
    image = bytearray(size)
    for segment in core.segments:
        start = segment["p_vaddr"]
        image[start:start + segment["p_filesz"]] = segment.data()
    relocations = []
    for offset, info, addend in core.relocations():
        symbol = core.symbols.get_symbol(info >> 8)
        value = symbol["st_value"] if symbol["st_shndx"] != "SHN_UNDEF" else 0
        if addend is not None:
            struct.pack_into("<I", image, offset, addend)
        relocations.append(f"{offset} {info & 255} {value} {symbol.name or '-'}")

    def output(suffix, value):
        path = Path(str(args.output) + suffix)
        path.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(value, str):
            path.write_text(value)
        else:
            path.write_bytes(value)

    output(".image", image)
    output(".relocs", "\n".join(relocations))
    constants = Elf(args.stock / "libcamera_client.so").constants()
    output(".constants", "\n".join(k + " " + v.encode().hex() for k, v in constants.items() if v))
    output(".symbols", "\n".join(str(s["st_value"]) + " " + s.name
                                for s in core.symbols.iter_symbols()
                                if s["st_shndx"] != "SHN_UNDEF"))
    output(".manifest.json", json.dumps({"firmware": "A300FUXXU1CSG3", "sha256": expected},
                                        indent=2) + "\n")
    print("Prepared diagnostic oracle:", args.output)


if __name__ == "__main__":
    main()
