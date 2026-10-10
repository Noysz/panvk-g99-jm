#!/usr/bin/env python3
"""Summarise an iolog trace of kbase JOB_SUBMITs.

Usage: blobdeps.py <iolog.txt> [t_from_s t_to_s]   (times relative to the
first submit). Stride 72 = base_jd_atom (seq_nr first) + 8 vendor bytes;
stride 64 = base_jd_atom_v2 + 8 vendor bytes (what panvk writes).
"""
import re
import sys
from collections import Counter, defaultdict

SOFT = {0x201: 'dump', 0x202: 'fence_trigger', 0x203: 'fence_wait',
        0x205: 'event_wait', 0x206: 'event_set', 0x207: 'event_reset',
        0x208: 'debug_copy', 0x209: 'jit_alloc', 0x20a: 'jit_free',
        0x20b: 'res_map', 0x20c: 'res_unmap'}


def kind(req):
    if req & 0x200:
        return SOFT.get(req & 0x3ff, 'soft%x' % req)
    if req == 0:
        return 'dep'
    if req & 0x1:
        return 'frag'
    if req & 0xc:  # V or T
        return 'vtc'
    if req & 0x2:
        return 'compute'
    return 'req%x' % req


def parse(path):
    subs, events = [], []
    cur = None
    for line in open(path, errors='replace'):
        if line.startswith('S '):
            m = re.match(r'S (\d+) .*n=(\d+) stride=(\d+) ret=(-?\d+)', line)
            cur = {'t': int(m.group(1)), 'stride': int(m.group(3)),
                   'atoms': []}
            subs.append(cur)
        elif line.startswith(' A ') and cur is not None:
            m = re.search(r'n=(\d+) req=0x([0-9a-f]+) dep=(\d+):(\d+),(\d+):(\d+)'
                          r' prio=(\d+) slot=(\d+) jc=0x([0-9a-f]+)'
                          r'(?: tail=([0-9a-f]+))?', line)
            n, req = int(m.group(1)), int(m.group(2), 16)
            d = [(int(m.group(3)), int(m.group(4))),
                 (int(m.group(5)), int(m.group(6)))]
            tail = bytes.fromhex(m.group(10) or '')
            if cur['stride'] == 72 and len(tail) >= 8:
                # printed "req" = bytes 44..47 = pre_dep[0..1]; tail = 48..63
                d = [(req & 0xff, (req >> 8) & 0xff),
                     ((req >> 16) & 0xff, (req >> 24) & 0xff)]
                n = tail[0]
                req = int.from_bytes(tail[4:8], 'little')
            cur['atoms'].append({'n': n, 'req': req, 'k': kind(req),
                                 'deps': [x for x in d if x[0]]})
        elif line.startswith('E '):
            m = re.match(r'E (\d+) code=0x([0-9a-f]+) atom=(\d+)', line)
            events.append((int(m.group(1)), int(m.group(2), 16),
                           int(m.group(3))))
    return subs, events


def main():
    subs, events = parse(sys.argv[1])
    if not subs:
        print('no submits')
        return
    t0 = subs[0]['t']
    lo = float(sys.argv[2]) if len(sys.argv) > 2 else 0
    hi = float(sys.argv[3]) if len(sys.argv) > 3 else 1e9
    sel = [s for s in subs if lo <= (s['t'] - t0) / 1e9 <= hi]
    span = (sel[-1]['t'] - sel[0]['t']) / 1e9 if len(sel) > 1 else 1
    kinds = Counter(a['k'] for s in sel for a in s['atoms'])
    print(f"{len(sel)} submits in {span:.1f} s ({len(sel)/span:.1f}/s), "
          f"stride {sel[0]['stride']}, atoms per submit "
          f"{sum(len(s['atoms']) for s in sel)/len(sel):.1f}")
    print('atoms per s:', ', '.join(f"{k} {v/span:.1f}"
                                    for k, v in kinds.most_common()))

    # dependency targets by kind, resolving atom numbers to the latest atom
    # with that number submitted before (numbers are reused)
    last = {}
    dep_kinds = defaultdict(Counter)
    vtc_after_frag = vtc_total = 0
    for s in subs:
        inside = lo <= (s['t'] - t0) / 1e9 <= hi
        for a in s['atoms']:
            if inside:
                tk = tuple(sorted(f"{last[d][0] if d in last else '?'}/"
                                  f"{'data' if t == 1 else 'order'}"
                                  for d, t in a['deps']))
                dep_kinds[a['k']][tk] += 1
                if a['k'] == 'vtc':
                    vtc_total += 1
                    # direct dep on a frag, or on a dep atom that waits for one
                    hit = False
                    for d, _ in a['deps']:
                        if d in last and last[d][0] == 'frag':
                            hit = True
                        elif d in last and last[d][0] == 'dep':
                            hit |= 'frag' in last[d][1]
                    vtc_after_frag += hit
            last[a['n']] = (a['k'], [last[d][0] for d, _ in a['deps']
                                     if d in last])
    print(f"vtc atoms that wait for a fragment atom: {vtc_after_frag} of "
          f"{vtc_total}")
    for k in ('vtc', 'frag', 'dep', 'compute'):
        if k in dep_kinds:
            print(f"  {k} deps:", '; '.join(f"{'+'.join(t) or 'none'} x{c}"
                                            for t, c in
                                            dep_kinds[k].most_common(5)))


if __name__ == '__main__':
    main()
