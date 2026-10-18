# SPDX-License-Identifier: Apache-2.0
# Optional oracle regeneration: python stock_oracle.py STOCK.so Tfa9895.cnt OUT.json
# Requires pyelftools, capstone and unicorn; never accesses physical hardware.
import sys, struct, json
from capstone import Cs, CS_ARCH_ARM, CS_MODE_THUMB
from unicorn import *
import re
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile


class Oracle:
    def __init__(self, path, container):
        self.u = Uc(UC_ARCH_ARM, UC_MODE_ARM)
        u = self.u
        self.base = 0x100000
        u.mem_map(self.base, 0x80000)
        self.heap = 0x200000
        u.mem_map(self.heap, 0x100000)
        u.mem_map(0x400000, 0x100000)
        u.mem_map(0x800000, 0x10000)
        u.mem_map(0x900000, 0x1000)
        e = ELFFile(open(path, "rb"))
        sy = e.get_section_by_name(".dynsym")
        self.symbols = {
            s.name: s["st_value"] + self.base
            for s in sy.iter_symbols()
            if s["st_shndx"] != "SHN_UNDEF"
        }
        for p in e.iter_segments():
            if p["p_type"] == "PT_LOAD":
                u.mem_write(self.base + p["p_vaddr"], p.data())
        imports = {}
        self.hooks = {}
        for s in sy.iter_symbols():
            if s.name and s["st_shndx"] == "SHN_UNDEF":
                a = 0x800000 + len(imports) * 16
                imports[s.name] = a
                self.hooks[a] = s.name
        for sec in e.iter_sections():
            if sec["sh_type"] != "SHT_REL":
                continue
            for r in sec.iter_relocations():
                a = self.base + r["r_offset"]
                t = r["r_info_type"]
                s = sy.get_symbol(r["r_info_sym"])
                old = struct.unpack("<I", u.mem_read(a, 4))[0]
                if t == 23:
                    v = self.base + old
                elif t in (21, 22, 2):
                    v = self.symbols.get(s.name, imports.get(s.name, 0)) + (
                        old if t == 2 else 0
                    )
                else:
                    raise ValueError(t)
                self.w32(a, v)
        overrides = [
            "Tfa98xx_DspWritePreset",
            "Tfa98xx_DspWriteDrc",
            "Tfa98xx_DspSetParam",
            "tfa98xx_dsp_set_param",
            "Tfa98xx_SetVolumeLevel",
            "Tfa98xx_Powerdown",
            "tfaRunMute",
            "tfaRunUnmute",
            "Tfa98xx_SetMute",
            "tfaRunCfPowerup",
            "tfaRunIsPwdn",
        ]
        for n in overrides:
            self.hooks[self.symbols[n] & ~1] = n
        u.reg_write(UC_ARM_REG_C1_C0_2, 0xF << 20)
        u.reg_write(UC_ARM_REG_FPEXC, 0x40000000)
        u.hook_add(UC_HOOK_CODE, self.hook)
        b = open(container, "rb").read()
        self.cnt = self.heap
        u.mem_write(self.cnt, b)
        self.heap += 0x10000

        def addresses(name):
            sym = sy.get_symbol_by_name(name)[0]
            addr = sym["st_value"] & ~1
            raw = bytes(u.mem_read(self.base + addr, sym["st_size"]))
            constants = {}
            found = []
            for ins in Cs(CS_ARCH_ARM, CS_MODE_THUMB).disasm(raw, addr):
                m = re.match(r"(\w+), \[pc, #(0x[0-9a-f]+|\d+)\]", ins.op_str)
                if ins.mnemonic.startswith("ldr") and m:
                    loc = ((ins.address + 4) & ~3) + int(m[2], 0)
                    constants[m[1]] = struct.unpack(
                        "<I", u.mem_read(self.base + loc, 4)
                    )[0]
                m = re.match(r"(\w+), pc$", ins.op_str)
                if ins.mnemonic == "add" and m and m[1] in constants:
                    target = (constants[m[1]] + ins.address + 4) & 0xFFFFFFFF
                    if target >= e.get_section_by_name(".data")["sh_addr"]:
                        found.append(self.base + target)
            return found

        count, counts, ptrs = addresses("tfaContProfile")
        self.w32(count, 1)
        self.w32(counts, 11)
        self.w32(addresses("tfa98xx_get_cnt")[0], self.cnt)
        self.w32(addresses("tfaContSetCurrentProfile")[0], 0xFFFFFFFF)
        dev = struct.unpack_from("<I", b, 44)[0] & 0xFFFFFF
        profiles = []
        for i in range(b[dev]):
            item = struct.unpack_from("<I", b, dev + 12 + 4 * i)[0]
            if item >> 24 == 1:
                profiles.append(item & 0xFFFFFF)
        for i, p in enumerate(profiles):
            self.w32(ptrs + 4 * i, self.cnt + p)
        self.trace = []
        self.pwdn = 1

    def w32(self, a, v):
        self.u.mem_write(a, struct.pack("<I", v & 0xFFFFFFFF))

    def hook(self, u, a, size, data):
        n = self.hooks.get(a)
        if not n:
            return
        r = [
            u.reg_read(x)
            for x in [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3]
        ]
        ret = 0
        if n in ["Tfa98xx_DspWritePreset", "Tfa98xx_DspWriteDrc"]:
            self.trace.append([n, bytes(u.mem_read(r[2], r[1])).hex()])
        elif n in ["Tfa98xx_DspSetParam", "tfa98xx_dsp_set_param"]:
            p = struct.unpack("<I", u.mem_read(u.reg_read(UC_ARM_REG_SP), 4))[0]
            self.trace.append(["param", r[1], r[2], bytes(u.mem_read(p, r[3])).hex()])
        elif n == "Tfa98xx_SetVolumeLevel":
            self.trace.append(["volume", r[1]])
        elif n == "Tfa98xx_Powerdown":
            self.pwdn = r[1]
            self.trace.append(["powerdown", r[1]])
        elif n == "tfaRunIsPwdn":
            ret = self.pwdn
        elif n == "tfaRunCfPowerup":
            self.pwdn = 0
            self.trace.append(["cf-powerup"])
        elif n in ["tfaRunMute", "tfaRunUnmute"]:
            self.trace.append([n])
        elif n == "Tfa98xx_SetMute":
            self.trace.append(["mute", r[1]])
        elif n == "usleep":
            pass
        elif n == "__android_log_print":
            pass
        else:
            raise RuntimeError(("unhandled", n, r))
        u.reg_write(UC_ARM_REG_R0, ret)
        u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def call(self, n, *args):
        u = self.u
        u.reg_write(UC_ARM_REG_SP, 0x4FF000)
        u.reg_write(UC_ARM_REG_LR, 0x900000)
        for reg, val in zip(
            [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], args
        ):
            u.reg_write(reg, val)
        u.emu_start(self.symbols[n], 0x900000, count=1000000)
        if u.reg_read(UC_ARM_REG_PC) != 0x900000:
            raise RuntimeError("did not return")
        return u.reg_read(UC_ARM_REG_R0)


if __name__ == "__main__":
    o = Oracle(sys.argv[1], sys.argv[2])
    result = []
    for profile in range(11):
        for step in range(15 if profile == 0 else 6):
            o.call("tfaContSetCurrentVstep", step, 0)
            o.trace = []
            rc = o.call("tfaContWriteProfile", 0, profile, step)
            if rc:
                raise RuntimeError((profile, step, rc))
            result.append({"profile": profile, "step": step, "trace": o.trace})
    json.dump(result, open(sys.argv[3], "w"), indent=2)
    print("oracle cases", len(result))
