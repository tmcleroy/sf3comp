#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tommy McLeroy. Part of cps3recomp (sf3comp); from satrecomp: https://github.com/tmcleroy/satrecomp
"""cps3recomp code generator: turns a block database into C++.

Input:  a block database (address + code bytes per line, written by the game with --record-blocks)
        machine/sh2_ops.h (the instruction set)
Output: <out>/blocks_NNN.cpp   one function per block (and per function of several blocks joined by branches)
        <out>/blocks_index.cpp the table of all generated files

Nothing here is specific to a game. Generated files are derived from the game's code: never commit them.
"""

import argparse
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sh2emit  # noqa: E402
from sh2dis import disasm  # noqa: E402

UNCOND_DELAYED = {"BRA", "BRAF", "BSR", "BSRF", "JMP", "JSR", "RTS", "RTE"}
COND_DELAYED = {"BTS", "BFS"}
ENDS_BLOCK = {"Illegal", "SLEEP", "TRAPA"}
MAX_BLOCK_INSTRS = 48


def own_tables(ops_h):
    """The decode table from machine/sh2_ops.h: the names of the kinds of instruction, the kind of every opcode (as
    itself and as a delay slot instruction) and its nominal cycles (machine/sh2.cpp's table)."""
    text = open(ops_h).read()
    m = re.search(r"enum class Op\s*:\s*\w+\s*\{(.*?)\};", text, re.S)
    names = [n.strip() for n in m.group(1).replace("\n", " ").split(",") if n.strip()]
    patterns = [(names.index(n), int(mask, 16), int(match, 16))
                for n, mask, match in re.findall(r"\{Op::(\w+), 0x([0-9A-F]+), 0x([0-9A-F]+)\}", text)]
    illegal = names.index("Illegal")
    no_slot = set(names.index(n) for n in ("BRA", "BRAF", "BSR", "BSRF", "JMP", "JSR", "RTS", "RTE", "BT", "BF", "BTS",
                                           "BFS", "TRAPA", "Illegal"))
    slot_names = ["Delay_" + n for n in names] + ["IllegalSlot"]
    plain, delay, nominal = [], [], bytearray()
    two = set(names.index(n) for n in ("BRA", "BRAF", "BSR", "BSRF", "JMP", "JSR", "RTS", "BT", "BF", "BTS", "BFS", "MUL",
                                       "DMULS", "DMULU"))
    three = set(names.index(n) for n in ("MACW", "MACL", "AND_M", "OR_M", "XOR_M", "TST_M", "LDC_GBR_M", "LDC_SR_M",
                                         "LDC_VBR_M", "STC_GBR_M", "STC_SR_M", "STC_VBR_M"))
    four = set(names.index(n) for n in ("TAS", "RTE"))
    for op in range(0x10000):
        kind = illegal
        for index, mask, match in patterns:
            if op & mask == match:
                kind = index
                break
        plain.append(kind)
        delay.append(len(names) + len(slot_names) - 1 if kind in no_slot else len(names) + kind)
        nominal.append(2 if kind in two else 3 if kind in three else 4 if kind in four else 1)
    return names + slot_names, plain + delay, bytes(nominal)


def load_blocks(path, regions):
    blocks = {}
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        addr, code = line.split()
        pc = int(addr, 16)
        if not any(lo <= pc < hi for lo, hi in regions):
            continue
        blocks[(pc, code)] = bytes.fromhex(code)
    return [(pc, code) for (pc, _), code in sorted(blocks.items())]


class Generator:
    def __init__(self, names, decode, nominal):
        self.names = names
        self.plain = decode[:0x10000]
        self.delay = decode[0x10000:]
        self.nominal = nominal

    def successors(self, pc, code):
        """Addresses a block can go on to by a branch with a fixed target, by running off its end, or when the
        call it ends with comes back: the transfers that can stay inside one function."""
        ops = struct.unpack(">%dH" % (len(code) // 2), code)
        names = [self.names[self.plain[op]] for op in ops]
        out = []
        n = len(ops)
        for i in range(n):
            name = names[i]
            here = pc + i * 2
            if name in ("BT", "BF", "BTS", "BFS"):
                out.append(here + 4 + sh2emit.s8(ops[i] & 0xFF) * 2)
            elif name in UNCOND_DELAYED:
                if name == "BRA":
                    out.append(here + 4 + sh2emit.s12(ops[i] & 0xFFF) * 2)
                elif name in ("JSR", "BSR", "BSRF") and i + 2 == n:
                    out.append(pc + n * 2)  # where the call comes back to
                return out
        out.append(pc + n * 2)
        return out

    def native(self, pc, code, members, base, label, index):
        """The lines of a block in C++ (tools/sh2emit.py), or None. members: address -> label of the blocks in the
        same function; a branch to one of them is a goto. base: address of the function's first block. label: this
        block's label. index: address -> number of the block in the function."""
        ops = struct.unpack(">%dH" % (len(code) // 2), code)
        n = len(ops)
        lines = []
        cycles = [0]
        for op in ops:
            cycles.append(cycles[-1] + self.nominal[op])
        names = [self.names[self.plain[op]] for op in ops]

        def leave(index, cyc):
            return "{ SN_LEAVE(%d, %d) }" % (index * 2, cyc)

        def go(dest, expr, cyc, idle=False):
            if dest in members:
                if idle:
                    return "SN_GOTO_IDLE(%s, %d, %d, %d)" % (label, dest - base, cyc, index[dest])
                return "SN_GOTO(%s, %d, %d, %d, %d)" % (members[dest], dest - base, cyc, 1 if dest <= pc else 0, index[dest])
            return "SN_JUMP(%s, %d)" % (expr, cyc)

        def call(expr, cyc):
            end = pc + n * 2
            if end in members:
                return "SN_CALL(%s, %d, %d, %d, goto %s;)" % (expr, cyc, end - base, index[end], members[end])
            return "SN_CALL(%s, %d, %d, -1, )" % (expr, cyc, end - base)

        def instr(i, pcx, in_slot):
            on_wait = leave(i, cycles[i + 1])
            ins = sh2emit.emit(names[i], ops[i], pcx, on_wait, in_slot, None)
            if ins is None:
                return None, None
            # (each instruction's fetch first: the bus's timing charges it, block_native.h)
            body = "{ SN_FETCH(%d) SN_I(%d, %d) %s }" % (i * 2, i * 2, cycles[i], " ".join(ins.lines)) if ins.lines else \
                "{ SN_FETCH(%d) }" % (i * 2)
            return ins, body

        def fixed_target(i):
            if names[i] in ("BT", "BF", "BTS", "BFS"):
                return pc + i * 2 + 4 + sh2emit.s8(ops[i] & 0xFF) * 2
            if names[i] in ("BRA", "BSR"):
                return pc + i * 2 + 4 + sh2emit.s12(ops[i] & 0xFFF) * 2
            return None

        # A countdown at the start of the block (dt Rn; bf to itself): entered from elsewhere the count is skipped
        # ahead first; the loop itself branches to the label after that.
        entry_label = label
        if n >= 2 and names[0] == "DT" and names[1] == "BF" and fixed_target(1) == pc and pc in members:
            entry_label = label + "_in"
            lines.append("%s: SN_COUNTDOWN(%d, %d)" % (entry_label, (ops[0] >> 8) & 15, cycles[2]))
        lines.append("SN_BLOCK(%s, %d)" % (label, pc - base))

        i = 0
        allow_off = False
        first_branch = True
        while i < n:
            name = names[i]
            if name in ENDS_BLOCK:
                return None
            text = "// %08X  %s" % (pc + i * 2, disasm(ops[i], pc + i * 2))
            if allow_off:
                lines.append("c.m_intrFlags.allow = true;")
                allow_off = False
            if name in ("BT", "BF"):
                _, expr = sh2emit.branch_target(name, ops[i], "pc + %du" % (i * 2))
                cond = "c.SR.T" if name == "BT" else "!c.SR.T"
                dest = fixed_target(i)
                idle = dest == pc and first_branch and sh2emit.only_waits(names[:i], ops[:i])
                lines.append("SN_FETCH(%d) if (%s) { %s } %s" % (i * 2, cond, go(dest, expr, cycles[i + 1], idle), text))
                first_branch = False
                i += 1
                continue
            if name in UNCOND_DELAYED or name in COND_DELAYED:
                first_branch = False
                if i + 1 >= n:
                    return None
                slot_text = "// %08X  %s" % (pc + i * 2 + 2, disasm(ops[i + 1], pc + i * 2 + 2))
                pre, expr = sh2emit.branch_target(name, ops[i], "pc + %du" % (i * 2))
                slot, slot_body = instr(i + 1, "(dest - 2u)", True)
                if slot is None:
                    return None
                dest = fixed_target(i) if name != "BSR" else None
                taken = ["{ SN_I(%d, %d) %s" % (i * 2, cycles[i], " ".join(pre)),
                         "  const uint32 dest = %s;" % expr,
                         "  SN_SLOT_BEGIN(dest) %s" % text,
                         "  %s %s" % (slot_body, slot_text)]
                if slot.clears_allow:
                    taken.append("  c.m_intrFlags.allow = false;")
                if name in ("JSR", "BSR", "BSRF") and i + 2 == n:
                    leave_by = call("dest", cycles[i + 2])
                else:
                    leave_by = go(dest, "dest", cycles[i + 2])
                taken.append("  SN_SLOT_END() %s }" % leave_by)
                lines.append("SN_FETCH(%d)" % (i * 2))
                if name in UNCOND_DELAYED:
                    if i + 2 != n:
                        return None
                    lines.extend(taken)
                    return lines, entry_label
                cond = "c.SR.T" if name == "BTS" else "!c.SR.T"
                lines.append("if (%s)" % cond)
                lines.extend(taken)
                i += 1  # not taken: the next instruction is an ordinary one
                continue
            ins, body = instr(i, "pc + %du" % (i * 2), False)
            if ins is None:
                return None
            lines.append("%s %s" % (body, text))
            if ins.clears_allow:
                lines.append("c.m_intrFlags.allow = false;")
                allow_off = True
            if ins.mem or ins.sr:
                lines.append("if (c.m_intrFlags.pending) [[unlikely]] %s" % leave(i + 1, cycles[i + 1]))
            i += 1
        end = pc + n * 2
        if end in members:
            lines.append("{ %s }" % go(end, None, cycles[n]))
        else:
            lines.append(leave(n, cycles[n]))
        return lines, entry_label

    def function(self, blocks):
        """Function body for a list of (pc, code) blocks, the first being its entry; None if any of them cannot be
        translated."""
        base = blocks[0][0]
        labels = {pc: "B%d" % k for k, (pc, _) in enumerate(blocks)}
        index = {pc: k for k, (pc, _) in enumerate(blocks)}
        bodies = []
        # (a block that starts with a countdown is entered through its second label)
        for _ in range(2):
            bodies = []
            entries = {}
            for k, (pc, code) in enumerate(blocks):
                r = self.native(pc, code, labels, base, "B%d" % k, index)
                if r is None:
                    return None
                bodies.append(r[0])
                entries[pc] = r[1]
            changed = False
            for pc in labels:
                if entries[pc] != labels[pc] and not labels[pc].endswith("_in"):
                    labels[pc] = entries[pc]
                    changed = True
            if not changed:
                break
        out = []
        if len(blocks) > 1:
            out.append("static const int32_t kBlockAt[] = {%s};" % ", ".join(str(bpc - base) for bpc, _ in blocks))
            out.append("SN_BEGIN(c.PC - kBlockAt[start])")
            out.append("switch (start) {")
            for k, (bpc, _) in enumerate(blocks[1:], 1):
                out.append("case %d: goto %s;" % (k, labels[bpc]))
            out.append("default: break;")
            out.append("}")
        else:
            out.append("SN_BEGIN(c.PC)")
            out.append("(void)start;")
        for body in bodies:
            out.extend(body)
        return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--blocks", required=True, help="block database")
    ap.add_argument("--ops", required=True, help="machine/sh2_ops.h: the instruction list")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--per-file", type=int, default=400, help="blocks per generated file")
    ap.add_argument("--max-function-blocks", type=int, default=64, help="blocks joined into one function")
    ap.add_argument("--prefix", default="",
                    help="name the tables after this (another game's code linked beside the main game's: a stage board's); "
                         "the tables register themselves by this name (blocks.h, RegisterBlockTables)")
    ap.add_argument("--index-base", type=int, default=0,
                    help="first BlockSet number (different for each game linked into one program)")
    ap.add_argument("--region", action="append", default=[],
                    help="LO-HI (hex): only generate blocks in these address ranges (default: the ROMs and main RAM)")
    args = ap.parse_args()

    names, decode, nominal = own_tables(args.ops)
    regions = []
    for r in args.region:
        lo, hi = r.split("-")
        regions.append((int(lo, 16), int(hi, 16)))
    if not regions:
        regions = [(0x00000000, 0x00080000), (0x02000000, 0x02080000), (0x06000000, 0x07000000)]

    gen = Generator(names, decode, nominal)
    blocks = load_blocks(args.blocks, regions)
    os.makedirs(args.out, exist_ok=True)

    # Functions: blocks joined by branches with fixed targets. Only addresses with a single version of code, and
    # only in the ROMs, take part (main RAM may hold anything: there each block stays on its own).
    count = {}
    for pc, code in blocks:
        count[pc] = count.get(pc, 0) + 1
    nodes = {pc: code for pc, code in blocks if count[pc] == 1 and not (0x02000000 <= pc < 0x02080000)}
    succ = {pc: [t for t in gen.successors(pc, code) if t in nodes] for pc, code in nodes.items()}
    has_pred = set(t for pc in succ for t in succ[pc] if t != pc)
    functions = {}
    assigned = set()
    order = [pc for pc in sorted(nodes) if pc not in has_pred] + [pc for pc in sorted(nodes) if pc in has_pred]
    for head in order:
        if head in assigned:
            continue
        members = [head]
        assigned.add(head)
        stack = [head]
        while stack and len(members) < args.max_function_blocks:
            for t in succ[stack.pop()]:
                if t not in assigned and len(members) < args.max_function_blocks:
                    assigned.add(t)
                    members.append(t)
                    stack.append(t)
        if len(members) > 1:
            functions[head] = [(head, nodes[head])] + sorted((m, nodes[m]) for m in members[1:])

    files = []
    natives = 0
    skipped = 0
    instrs = 0
    total_functions = 0
    pre = (args.prefix + "_") if args.prefix else ""
    for start in range(0, len(blocks), args.per_file):
        index = len(files)
        setno = args.index_base + index
        out = ["// Generated by cps3recomp/tools/recomp.py from the game's code. Do not edit, do not commit.", "",
               '#include "block_native.h"', "", "namespace cps3recomp {", "", "template <>",
               "struct BlockSet<%d> {" % setno]
        descs = []
        func_descs = []
        for pc, code in blocks[start:start + args.per_file]:
            native = gen.function([(pc, code)])
            if native is None:
                skipped += 1
                continue
            instrs += len(code) // 2
            ident = "b%08X_%d" % (pc, len(descs))
            natives += 1
            out.append("    static void n%s(SH2 &c, uint64_t target, unsigned start) {" % ident[1:])
            out.extend("        " + line for line in native)
            out.append("    }")
            members = functions.get(pc)
            if members and members[0][1] == code:
                body = gen.function(members)
                if body is not None:
                    out.append("    static void f%s(SH2 &c, uint64_t target, unsigned start) {" % ident[1:])
                    out.extend("        " + line for line in body)
                    out.append("    }")
                    func_descs.append((ident, members))
            descs.append((pc, code, ident))
        out.append("};")
        out.append("")
        for pc, code, ident in descs:
            out.append("static const uint8_t code_%s[] = {%s};" % (ident, ",".join("0x%02X" % b for b in code)))
        out.append("")
        for ident, members in func_descs:
            for k, (mpc, mcode) in enumerate(members[1:], 1):
                out.append("static const uint8_t fcode_%s_%d[] = {%s};" % (ident, k, ",".join("0x%02X" % b for b in mcode)))
            out.append("static const FunctionBlock fblocks_%s[] = {" % ident)
            for k, (mpc, mcode) in enumerate(members):
                out.append("    {%d, %d, %s}," % (mpc - members[0][0], len(mcode),
                                                  "code_%s" % ident if k == 0 else "fcode_%s_%d" % (ident, k)))
            out.append("};")
        out.append("extern const FunctionDesc g_functions_%s%d[];" % (pre, index))
        out.append("const FunctionDesc g_functions_%s%d[] = {" % (pre, index))
        for ident, members in func_descs:
            out.append("    {0x%08X, &BlockSet<%d>::f%s, fblocks_%s, %d}," % (members[0][0], setno, ident[1:], ident, len(members)))
        if not func_descs:
            out.append("    {0, nullptr, nullptr, 0},")
        out.append("};")
        out.append("extern const size_t g_functions_%s%d_count;" % (pre, index))
        out.append("const size_t g_functions_%s%d_count = %d;" % (pre, index, len(func_descs)))
        total_functions += len(func_descs)
        out.append("")
        out.append("extern const BlockDesc g_blocks_%s%d[];" % (pre, index))
        out.append("const BlockDesc g_blocks_%s%d[] = {" % (pre, index))
        for pc, code, ident in descs:
            out.append("    {0x%08X, %d, code_%s, &BlockSet<%d>::n%s}," % (pc, len(code) // 2, ident, setno, ident[1:]))
        if not descs:
            out.append("    {0, 0, nullptr, nullptr},")
        out.append("};")
        out.append("extern const size_t g_blocks_%s%d_count;" % (pre, index))
        out.append("const size_t g_blocks_%s%d_count = %d;" % (pre, index, len(descs)))
        out.append("")
        out.append("} // namespace cps3recomp")
        files.append(("blocks_%03d.cpp" % index, "\n".join(out) + "\n"))

    idx = ["// Generated by cps3recomp/tools/recomp.py. Do not edit, do not commit.", '#include "blocks.h"', "",
           "namespace cps3recomp {", ""]
    for i in range(len(files)):
        idx.append("extern const BlockDesc g_blocks_%s%d[];" % (pre, i))
        idx.append("extern const size_t g_blocks_%s%d_count;" % (pre, i))
        idx.append("extern const FunctionDesc g_functions_%s%d[];" % (pre, i))
        idx.append("extern const size_t g_functions_%s%d_count;" % (pre, i))
    idx.append("")
    table = "g_blockTables_" + args.prefix if args.prefix else "g_blockTables"
    idx.append("const BlockTable %s[] = {" % table)
    for i in range(len(files)):
        idx.append("    {g_blocks_%s%d, g_blocks_%s%d_count, g_functions_%s%d, g_functions_%s%d_count}," % (pre, i, pre, i, pre, i, pre, i))
    if not files:
        idx.append("    {nullptr, 0, nullptr, 0},")
    idx.append("};")
    if args.prefix:
        # (another game's: found by name, and by its code matching a board's program: runtime/recomp.cpp, AttachSecond)
        idx.append("namespace {")
        idx.append("const bool kRegistered = RegisterBlockTables(\"%s\", %s, %d);" % (args.prefix, table, len(files)))
        idx.append("} // namespace")
    else:
        idx.append("const size_t g_blockTableCount = %d;" % len(files))
    idx.append("")
    idx.append("} // namespace cps3recomp")
    files.append(("blocks_index.cpp", "\n".join(idx) + "\n"))

    # Only touch files whose content changed, so unchanged ones aren't recompiled
    wanted = set()
    written = 0
    for name, content in files:
        path = os.path.join(args.out, name)
        wanted.add(name)
        if os.path.exists(path) and open(path).read() == content:
            continue
        with open(path, "w") as f:
            f.write(content)
        written += 1
    for name in os.listdir(args.out):
        if name.startswith("blocks_") and name.endswith(".cpp") and name not in wanted:
            os.remove(os.path.join(args.out, name))

    print("recomp: %d blocks translated, %d functions of several blocks, %d instructions, %d files (%d written), "
          "%d blocks skipped" % (natives, total_functions, instrs, len(files) - 1, written, skipped))


if __name__ == "__main__":
    main()
