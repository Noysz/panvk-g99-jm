#!/usr/bin/env python3
"""hwcnt.py <hwcnt.bin> [t_from_s t_to_s] [--all]: decode hwcnt.c output.

Counter names come from $MALI_LAYOUT_XML, by default Mesa's copy of Arm's libGPUCounters layout for
Mali-G77 (src/panfrost/perf/database/hardwarelayout/Mali-G77.xml). The G57
is the same Valhall generation; there is no G57 file in that database, so
treat names as the G77 layout until a counter is cross-checked.
Shader core blocks are summed over cores. Values are printed per second of
sample time, and per GPU cycle where that helps (GPU_ACTIVE of the front-end).
"""
import os
import struct
import sys
import xml.etree.ElementTree as ET
from collections import defaultdict

XML = os.environ.get(
    'MALI_LAYOUT_XML',
    '/data/data/com.termux/files/home/panvk-g57/mesa-up/src/panfrost/perf/'
    'database/hardwarelayout/Mali-G77.xml')
KIND = {0: 'GPU Front-end', 1: 'Tiler', 2: 'Memory System', 3: 'Shader Core'}
SHORT = {0: 'FE', 1: 'TI', 2: 'L2', 3: 'SC'}

KEY = ['GPU_ACTIVE', 'JS0_ACTIVE', 'JS1_ACTIVE', 'JS2_ACTIVE', 'JS0_JOBS',
       'JS1_JOBS', 'JS0_TASKS', 'JS1_TASKS', 'TILER_ACTIVE', 'PRIMITIVES',
       'PRIM_CULLED', 'PRIM_CLIPPED', 'PRIM_SAT_CULLED', 'IDVS_POS_SHAD_REQ',
       'IDVS_VAR_SHAD_REQ', 'FRAG_ACTIVE', 'FRAG_PRIMITIVES_OUT',
       'FRAG_QUADS_RAST', 'FRAG_QUADS_EZS_TEST', 'FRAG_QUADS_EZS_UPDATE',
       'FRAG_QUADS_EZS_KILL', 'FRAG_LZS_TEST', 'FRAG_LZS_KILL',
       'FRAG_PTILES', 'FRAG_TRANS_ELIM', 'QUAD_FPK_KILLER', 'FULL_QUAD_WARPS',
       'COMPUTE_ACTIVE', 'COMPUTE_TASKS', 'EXEC_CORE_ACTIVE',
       'EXEC_INSTR_FMA', 'EXEC_INSTR_CVT', 'EXEC_INSTR_SFU', 'EXEC_INSTR_MSG',
       'EXEC_INSTR_DIVERGED', 'EXEC_ICACHE_MISS', 'EXEC_STARVE_ARITH',
       'TEX_MSGI_NUM_FLITS', 'TEX_TFCH_NUM_OPERATIONS', 'TEX_FILT_NUM_OPERATIONS',
       'TEX_TFCH_STARVED_PENDING_DATA_FETCH', 'LS_MEM_READ_FULL',
       'LS_MEM_READ_SHORT', 'LS_MEM_WRITE_FULL', 'LS_MEM_WRITE_SHORT',
       'LS_MEM_ATOMIC', 'VARY_SLOT_32', 'VARY_SLOT_16', 'ATTR_INSTR',
       'BEATS_RD_FTC', 'BEATS_RD_FTC_EXT', 'BEATS_RD_LSC', 'BEATS_RD_LSC_EXT',
       'BEATS_RD_TEX', 'BEATS_RD_TEX_EXT', 'BEATS_RD_OTHER', 'BEATS_WR_LSC',
       'BEATS_WR_TIB', 'L2_RD_MSG_IN', 'L2_WR_MSG_IN', 'L2_SNP_MSG_IN',
       'L2_EXT_READ', 'L2_EXT_READ_BEATS', 'L2_EXT_WRITE',
       'L2_EXT_WRITE_BEATS', 'L2_EXT_AR_STALL', 'L2_EXT_W_STALL',
       'L2_EXT_RD_BUF_FULL', 'MMU_REQUESTS', 'MMU_TABLE_READS_L3',
       'MMU_TABLE_READS_L2', 'MMU_HIT_L3', 'MMU_HIT_L2', 'L2_ANY_LOOKUP']


def names():
    root = ET.parse(XML).getroot()
    out = {}
    for blk in root.iter('CounterBlock'):
        kind = [k for k, v in KIND.items() if v == blk.get('type')]
        if not kind:
            continue
        for c in blk.iter('Counter'):
            out[(kind[0], int(c.get('index')))] = c.get('name')
    return out


def read(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'HWC2', 'not a hwcnt.c v2 file'
    nk = struct.unpack_from('<I', d, 4)[0]
    off = 8 + 8 * nk
    samples = []
    while off + 56 <= len(d):
        ts0, ts1, flags, nb = struct.unpack_from('<QQII', d, off)
        cyc = struct.unpack_from('<4Q', d, off + 24)
        off += 56
        blocks = []
        for _ in range(nb):
            t, idx, nval, state = struct.unpack_from('<BBHI', d, off)
            off += 8
            vals = struct.unpack_from('<%dQ' % nval, d, off)
            off += 8 * nval
            blocks.append((t, idx, state, vals))
        samples.append((ts0, ts1, flags, cyc, blocks))
    return samples


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show_all = '--all' in sys.argv
    samples = read(args[0])
    if not samples:
        print('no samples')
        return
    t0 = samples[0][0]
    lo = float(args[1]) if len(args) > 1 else 0
    hi = float(args[2]) if len(args) > 2 else 1e9
    sel = [s for s in samples if lo <= (s[0] - t0) / 1e9 <= hi]
    dur = sum(s[1] - s[0] for s in sel) / 1e9
    nm = names()
    tot = defaultdict(int)
    cycles = sum(s[3][0] for s in sel)
    for s in sel:
        for t, idx, state, vals in s[4]:
            for i, v in enumerate(vals):
                if i >= 4:          # 0-3 are block header fields
                    tot[(t, i)] += v
    gpu_active = tot.get((0, 6), 0)
    print(f'{len(sel)} samples, {dur:.2f} s, clock-domain-0 cycles '
          f'{cycles/dur/1e6:.0f} M/s, GPU_ACTIVE {gpu_active/dur/1e6:.0f} M/s')
    rows = []
    for (t, i), v in sorted(tot.items()):
        n = nm.get((t, i), f'{SHORT[t]}_{i}')
        if not v or (not show_all and n not in KEY):
            continue
        rows.append((t, n, v))
    for t, n, v in rows:
        per_cyc = f'{v/gpu_active:8.3f}/gpu-cycle' if gpu_active else ''
        print(f'{SHORT[t]} {n:38s} {v/dur/1e6:10.3f} M/s  {per_cyc}')


if __name__ == '__main__':
    main()
