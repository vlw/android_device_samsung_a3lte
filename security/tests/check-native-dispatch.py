#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Offline-only execution of the pinned BRH1 TA's unsupported-command branch.

Usage: python3 check-native-dispatch.py /path/to/native/keymaste.b02
Requires unicorn. Does not open USB, QSEE, block devices or the live TA.
"""
import hashlib
import json
import struct
import sys
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
from unicorn.arm_const import (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_SP, UC_ARM_REG_LR)

code = Path(sys.argv[1]).read_bytes()
digest = hashlib.sha256(code).hexdigest()
if digest != "db37715aa93e9cc5e82c70e990fbb3a22f2c96a6f9427a8e13f1a7d4ae4ff05d":
    raise SystemExit("Unknown TA: handler address is not qualified for this image")
results = []
for command in (0x101, 0x108, 0x200, 0x207, 0x1001, 0x1002, 0x1003, 0x1004):
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.mem_map(0, 0x50000)
    cpu.mem_write(0, code)
    cpu.mem_write(0x20000, struct.pack("<I", command) + bytes(20))
    canary = bytes([0xA5]) * 0x650
    cpu.mem_write(0x21000, canary)
    for register, value in ((UC_ARM_REG_R0, 0x20000), (UC_ARM_REG_R1, 24),
                            (UC_ARM_REG_R2, 0x21000), (UC_ARM_REG_R3, 0x650),
                            (UC_ARM_REG_SP, 0x31000), (UC_ARM_REG_LR, 0x40001)):
        cpu.reg_write(register, value)
    trace = []

    def instruction(uc, address, _size, _data):
        trace.append(address)
        if address == 0x40000:
            uc.emu_stop()
        elif not 0x324A <= address <= 0x327C:
            raise RuntimeError(f"Unexpected handler path: {address:#x}")

    cpu.hook_add(UC_HOOK_CODE, instruction)
    cpu.emu_start(0x324B, 0x40000, count=100)
    returned = cpu.reg_read(UC_ARM_REG_R0)
    unchanged = bytes(cpu.mem_read(0x21000, len(canary))) == canary
    if returned != 1 or not unchanged:
        raise RuntimeError(f"Unexpected result for {command:#x}")
    results.append(dict(command=hex(command), returned=returned,
                        response_unchanged=unchanged, instructions=trace))
print(json.dumps(dict(offline_only=True, code_sha256=digest,
                     handler="0x324b", results=results), indent=2))
