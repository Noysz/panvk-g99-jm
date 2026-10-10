#!/usr/bin/env python3
"""shaderstats.py <pandecode dump> : per stage, draw-weighted shader cost.

For every shader descriptor in the dump (Stage, Register allocation,
Binary), counts the instructions of the binary it points to (pandecode's
disassembly, "Shader ... (GPU VA x)" blocks) and some instruction classes.
"""
import re
import sys
from collections import Counter, defaultdict

INS = re.compile(r'^(?:[0-9a-f]{2} ){8}\s+(\S+)')


def main():
    lines = open(sys.argv[1], errors='replace').read().split('\n')
    code = {}            # va -> list of mnemonics
    cur = None
    for ln in lines:
        m = re.match(r'^Shader \S+ \(GPU VA ([0-9a-f]+)\)', ln)
        if m:
            cur = int(m.group(1), 16)
            # pandecode prints a binary once per use: keep the last copy
            code[cur] = []
            continue
        m = INS.match(ln)
        if m and cur is not None:
            code[cur].append(m.group(1))
        elif ln.strip() == '':
            continue            # blank lines separate basic blocks
        else:
            cur = None

    uses = []            # (stage, regs, va)
    stage = regs = None
    for ln in lines:
        m = re.match(r'^\s+Stage: (\w+)', ln)
        if m:
            stage, regs = m.group(1), None
        m = re.match(r'^\s+Register allocation: (\d+)', ln)
        if m:
            regs = int(m.group(1))
        m = re.match(r'^\s+Binary: 0x([0-9a-f]+)', ln)
        if m and stage:
            uses.append((stage, regs, int(m.group(1), 16)))

    per = defaultdict(lambda: Counter())
    hist = defaultdict(Counter)
    uniq = defaultdict(set)
    for st, rg, va in uses:
        ops = code.get(va)
        if ops is None:
            continue
        c = per[st]
        c['uses'] += 1
        c['instr'] += len(ops)
        c['regs64'] += rg == 64
        c['tex'] += sum(o.startswith(('TEX', 'VAR_TEX')) for o in ops)
        c['ldvar'] += sum(o.startswith('LD_VAR') for o in ops)
        c['ldst_tl'] += sum(o.startswith(('LOAD.', 'STORE.')) and '.tl' in o
                            for o in ops)
        c['mem'] += sum(o.startswith(('LOAD', 'STORE', 'LD_BUFFER', 'ST_'))
                        for o in ops)
        c['fma'] += sum(o.startswith(('FMA', 'FADD', 'FMUL', 'V2F32', 'FMA_RSCALE'))
                        for o in ops)
        uniq[st].add(va)
        for o in ops:
            hist[st][o.split('.')[0]] += 1
    for st, c in sorted(per.items()):
        n = c['uses']
        print(f"{st:9s} draws {n:4d} unique {len(uniq[st]):3d} | per draw: "
              f"instr {c['instr']/n:6.1f}, tex {c['tex']/n:4.1f}, "
              f"ld_var {c['ldvar']/n:4.1f}, mem {c['mem']/n:4.1f}, "
              f"spill(tl) {c['ldst_tl']/n:4.1f}, fp {c['fma']/n:5.1f} | "
              f"64 regs {100*c['regs64']/n:.0f}%")
        if len(sys.argv) > 2:
            top = hist[st].most_common(int(sys.argv[2]))
            print('   ', ' '.join(f"{k}:{v/n:.1f}" for k, v in top))


if __name__ == '__main__':
    main()
