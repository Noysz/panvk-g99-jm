#!/usr/bin/env python3
"""isapairs.py: per-shader ISA stats and cross-driver shader pairing.

Reads two pandecode dumps (a "system"/proprietary-blob dump and a panvk dump),
builds a per-unique-binary table of instruction-class counts, finds likely
matching shader pairs between the two dumps (same source shader compiled by
each driver), and writes:

  shaders_system.txt / shaders_panvk.txt : per-GPU-VA tables (sorted by draws)
  pair_NN_<stage>.txt                     : full disassembly of each matched pair
  README.txt                              : plain-English explanation

Parsing reuses the approach of shaderstats.py (same repo):
  * a disassembly block starts at 'Shader <ptr> (GPU VA <hex>) sz <n>';
  * instruction lines are 8 hex bytes then the mnemonic+operands;
  * blank lines separate basic blocks;
  * a block ends at the first line that is neither an instruction nor blank;
  * the same binary may be printed many times -> one copy per GPU VA is kept.
Shader descriptors carry 'Stage:', 'Register allocation:' and 'Binary: 0x<va>'.

Tables list one row per unique GPU VA (as required). Matching instead works on
structural archetypes: VAs whose full mnemonic sequence is identical are
collapsed (the panvk driver re-uploads the same binary to many addresses, and
each driver emits several constant-only variants of one source shader), so a
pair represents distinct shaders rather than re-upload copies.

Usage:
  isapairs.py SYSTEM_DUMP PANVK_DUMP OUT_DIR [--pairs=N]

The tool only reads the input dumps and writes into OUT_DIR.
"""
import os
import re
import sys
from collections import Counter, defaultdict

HDR = re.compile(r'^Shader \S+ \(GPU VA ([0-9a-f]+)\) sz (\d+)')
INS = re.compile(r'^((?:[0-9a-f]{2} ){8}) +(\S.*?)\s*$')
STAGE = re.compile(r'^\s+Stage: (\w+)')
REGS = re.compile(r'^\s+Register allocation: (\d+)')
BIN = re.compile(r'^\s+Binary: 0x([0-9a-f]+)')
HEX = re.compile(r'0x([0-9a-fA-F]+)')
IDX = re.compile(r'index:0x([0-9a-fA-F]+)')
VW = re.compile(r'\.v(\d)\b')


# -------- parsing -------------------------------------------------------------

def parse_dump(path):
    """Return (code, uses).

    code: va -> dict(mnem=[...], text=[...raw instr/blank lines...], size=int)
    uses: list of (stage, regs, va) in descriptor order (one per draw binding)
    """
    lines = open(path, errors='replace').read().split('\n')
    code = {}
    cur = None
    for ln in lines:
        m = HDR.match(ln)
        if m:
            cur = int(m.group(1), 16)
            code[cur] = {'mnem': [], 'text': [], 'size': int(m.group(2))}
            continue
        if cur is None:
            continue
        m = INS.match(ln)
        if m:
            body = m.group(2)
            code[cur]['mnem'].append(body.split()[0])
            code[cur]['text'].append(ln.rstrip())
        elif ln.strip() == '':
            code[cur]['text'].append('')
        else:
            t = code[cur]['text']
            while t and t[-1] == '':
                t.pop()
            cur = None

    uses = []
    stage = regs = None
    for ln in lines:
        m = STAGE.match(ln)
        if m:
            stage, regs = m.group(1), None
            continue
        m = REGS.match(ln)
        if m:
            regs = int(m.group(1))
            continue
        m = BIN.match(ln)
        if m and stage:
            uses.append((stage, regs, int(m.group(1), 16)))
    return code, uses


# -------- per-shader classification ------------------------------------------

def classify(mnem):
    c = Counter()
    for m in mnem:
        if m.startswith('LD_PKA'):
            c['ld_pka'] += 1
        if m.startswith('LD_ATTR'):
            c['ld_attr'] += 1
        if m.startswith('LD_VAR'):
            c['ld_var'] += 1
        if m.startswith(('TEX', 'VAR_TEX')):
            c['tex'] += 1
        if m.startswith('IADD_IMM'):
            c['iadd_imm'] += 1
        if m.startswith('MOV'):
            c['mov'] += 1
        if m.startswith(('FMA', 'FADD', 'FMUL')):
            c['fma'] += 1
        if m.startswith('STORE'):
            c['store'] += 1
        if m.startswith('BRANCH'):
            c['branch'] += 1
    return c


def signatures(text):
    """Structural signatures used for cross-driver matching."""
    imm = Counter()
    attr_w = Counter()
    var_w = Counter()
    attr_idx = set()
    var_idx = set()
    for ln in text:
        if not ln:
            continue
        body = ln[24:] if len(ln) > 24 else ln  # drop the 8 hex bytes
        toks = body.split()
        mnem = toks[0] if toks else ''
        for h in HEX.findall(body):
            v = int(h, 16)
            if v != 0:
                imm[v] += 1
        wm = VW.search(mnem)
        if mnem.startswith('LD_ATTR'):
            if wm:
                attr_w[int(wm.group(1))] += 1
            im = IDX.search(body)
            if im:
                attr_idx.add(int(im.group(1), 16))
        if mnem.startswith('LD_VAR'):
            if wm:
                var_w[int(wm.group(1))] += 1
            im = IDX.search(body)
            if im:
                var_idx.add(int(im.group(1), 16))
    return {'imm': imm, 'attr_w': attr_w, 'var_w': var_w,
            'attr_idx': attr_idx, 'var_idx': var_idx}


def build(code, uses):
    """One record per unique GPU VA that is used by at least one draw."""
    draws = Counter()
    stages = defaultdict(Counter)
    regset = defaultdict(Counter)
    for st, rg, va in uses:
        if va not in code:
            continue
        draws[va] += 1
        stages[va][st] += 1
        regset[va][rg] += 1
    recs = []
    for va, n in draws.items():
        mnem = code[va]['mnem']
        recs.append({
            'va': va,
            'stage': stages[va].most_common(1)[0][0],
            'regs': regset[va].most_common(1)[0][0],
            'regs_mixed': len(regset[va]) > 1,
            'draws': n,
            'nva': 1,
            'vas': [va],
            'instr': len(mnem),
            'size': code[va]['size'],
            'mnem': list(mnem),
            'cls': classify(mnem),
            'sig': signatures(code[va]['text']),
            'text': code[va]['text'],
            'has': {k: any(k in t for t in code[va]['text'])
                    for k in ('DISCARD', 'BLEND', 'ATEST')},
        })
    recs.sort(key=lambda r: (-r['draws'], -r['instr']))
    return recs


def canonicalize(recs):
    """Collapse VAs with an identical mnemonic sequence into one archetype.

    recs is pre-sorted by draws desc, so the first VA seen for a structure is
    its highest-draw representative; its disassembly/immediates are kept.
    """
    groups = {}
    order = []
    for r in recs:
        key = (r['stage'], tuple(r['mnem']))
        if key in groups:
            g = groups[key]
            g['draws'] += r['draws']
            g['nva'] += 1
            g['vas'].append(r['va'])
        else:
            g = dict(r)
            g['vas'] = list(r['vas'])
            groups[key] = g
            order.append(key)
    out = [groups[k] for k in order]
    out.sort(key=lambda r: (-r['draws'], -r['instr']))
    return out


# -------- matching ------------------------------------------------------------

def closeness(a, b):
    if a == 0 and b == 0:
        return 1.0
    return 1.0 - abs(a - b) / float(a + b)


def ms_sim(ca, cb):
    """Multiset Jaccard over Counters."""
    if not ca and not cb:
        return 1.0
    if not ca or not cb:
        return 0.0
    inter = sum((ca & cb).values())
    union = sum((ca | cb).values())
    return inter / float(union) if union else 1.0


def score(a, b):
    """Similarity using compiler-stable features; returns a breakdown dict.

    Stable across the two compilers: stage, texture-op count, attribute and
    varying vector-width multisets, relative float-arithmetic magnitude.
    Deliberately low-weighted: raw instruction count and LD_PKA count (panvk
    emits scalar matrix loads, the blob emits wide loads, so these differ for
    the same source). DISCARD/ATEST presence is ignored (panvk always emits it).
    """
    if a['stage'] != b['stage']:
        return None
    ca, cb = a['cls'], b['cls']
    if a['stage'] == 'Fragment':
        feats = [
            ('tex', 3.0, closeness(ca['tex'], cb['tex'])),
            ('ld_var', 2.0, closeness(ca['ld_var'], cb['ld_var'])),
            ('var_w', 2.0, ms_sim(a['sig']['var_w'], b['sig']['var_w'])),
            ('fma', 1.5, closeness(ca['fma'], cb['fma'])),
            ('instr', 0.5, closeness(a['instr'], b['instr'])),
        ]
    else:
        feats = [
            ('attr_w', 3.0, ms_sim(a['sig']['attr_w'], b['sig']['attr_w'])),
            ('ld_attr', 2.0, closeness(ca['ld_attr'], cb['ld_attr'])),
            ('fma', 2.0, closeness(ca['fma'], cb['fma'])),
            ('store', 1.0, closeness(ca['store'], cb['store'])),
            ('instr', 0.5, closeness(a['instr'], b['instr'])),
        ]
    wsum = sum(w for _, w, _ in feats)
    base = sum(w * v for _, w, v in feats) / wsum
    imm = ms_sim(a['sig']['imm'], b['sig']['imm'])
    final = 0.85 * base + 0.15 * imm
    return {'final': final, 'base': base, 'imm': imm,
            'feats': {n: v for n, _, v in feats}}


def confidence(a, b, s):
    if a['stage'] == 'Fragment':
        prim = a['cls']['tex'] == b['cls']['tex']
    else:
        prim = a['sig']['attr_w'] == b['sig']['attr_w']
    if s['base'] >= 0.90 and prim:
        return 'high'
    if s['base'] >= 0.80 and prim:
        return 'medium'
    if s['base'] >= 0.75:
        return 'low'
    return 'uncertain'


def match(sys_recs, pvk_recs, want):
    """Greedy one-to-one over structural archetypes, balanced across stages."""
    cand = []
    for i, a in enumerate(sys_recs):
        for j, b in enumerate(pvk_recs):
            s = score(a, b)
            if s is None:
                continue
            cand.append((s['final'], i, j, s))
    cand.sort(key=lambda x: -x[0])
    used_i, used_j = set(), set()
    picked = []
    for fin, i, j, s in cand:
        if i in used_i or j in used_j:
            continue
        used_i.add(i)
        used_j.add(j)
        picked.append((sys_recs[i], pvk_recs[j], s))
    # balance stages: take the best half from each where possible
    per = max(1, want // 2)
    frag = [p for p in picked if p[0]['stage'] == 'Fragment'][:per + 2]
    vert = [p for p in picked if p[0]['stage'] == 'Vertex'][:per + 2]
    chosen = sorted(frag[:per] + vert[:per],
                    key=lambda p: -p[2]['final'])
    extra = sorted([p for p in frag[per:] + vert[per:]],
                   key=lambda p: -p[2]['final'])
    while len(chosen) < want and extra:
        chosen.append(extra.pop(0))
    chosen.sort(key=lambda p: (p[0]['stage'], -p[2]['final']))
    return chosen[:want]


# -------- output --------------------------------------------------------------

TBL_HDR = (f"{'GPU_VA':>12} {'stage':9} {'reg':>4} {'draws':>5} "
           f"{'instr':>5} {'LD_PKA':>6} {'LD_ATTR':>7} {'LD_VAR':>6} "
           f"{'TEX':>4} {'IADD_I':>6} {'MOV':>5} {'FMA/AD':>6} "
           f"{'STORE':>5} {'BRNCH':>5} {'bytes':>5}")


def row(r):
    c = r['cls']
    reg = ('%d*' % r['regs']) if r['regs_mixed'] else ('%d' % (r['regs'] or 0))
    return (f"{r['va']:012x} {r['stage']:9} {reg:>4} {r['draws']:5d} "
            f"{r['instr']:5d} {c['ld_pka']:6d} {c['ld_attr']:7d} "
            f"{c['ld_var']:6d} {c['tex']:4d} {c['iadd_imm']:6d} {c['mov']:5d} "
            f"{c['fma']:6d} {c['store']:5d} {c['branch']:5d} {r['size']:5d}")


def write_table(path, title, recs):
    with open(path, 'w') as f:
        f.write(title + '\n')
        f.write('=' * len(title) + '\n\n')
        f.write('One row per unique GPU VA used by at least one draw, sorted '
                'by draws (descending).\n')
        f.write('FMA/AD = FMA+FADD+FMUL (float arithmetic). reg* = register '
                'allocation varied across draws.\n')
        f.write('Note: the panvk driver re-uploads some identical binaries to '
                'several GPU VAs, so\nseveral rows can be byte-identical.\n\n')
        by_stage = defaultdict(list)
        for r in recs:
            by_stage[r['stage']].append(r)
        for st in sorted(by_stage):
            rs = by_stage[st]
            f.write(f"---- Stage: {st}  ({len(rs)} unique VAs, "
                    f"{sum(x['draws'] for x in rs)} draw-bindings) ----\n")
            f.write(TBL_HDR + '\n')
            for r in rs:
                f.write(row(r) + '\n')
            f.write('\n')
        f.write(f"Totals: {len(recs)} unique VAs, "
                f"{sum(r['draws'] for r in recs)} draw-bindings.\n")


def pair_header(tag, r):
    c = r['cls']
    extra = ''
    if r['nva'] > 1:
        extra = (f" ({r['nva']} GPU VAs share this instruction sequence, "
                 f"constants may differ; draws summed)")
    return (f"[{tag}] GPU VA {r['va']:012x}  stage={r['stage']}  "
            f"regs={r['regs']}{'(mixed)' if r['regs_mixed'] else ''}  "
            f"draws={r['draws']}  instr={r['instr']}  bytes={r['size']}{extra}\n"
            f"      LD_PKA={c['ld_pka']} LD_ATTR={c['ld_attr']} "
            f"LD_VAR={c['ld_var']} TEX={c['tex']} IADD_IMM={c['iadd_imm']} "
            f"MOV={c['mov']} FMA/FADD/FMUL={c['fma']} STORE={c['store']} "
            f"BRANCH={c['branch']}")


def write_pair(path, idx, a, b, s):
    conf = confidence(a, b, s)
    with open(path, 'w') as f:
        f.write(f"Pair {idx:02d} - stage {a['stage']}\n")
        f.write('=' * 64 + '\n')
        f.write(f"confidence: {conf}   similarity={s['final']:.3f} "
                f"(structural base={s['base']:.3f}, immediate-overlap="
                f"{s['imm']:.3f})\n")
        f.write("feature agreement: "
                + ', '.join(f"{k}={v:.2f}" for k, v in s['feats'].items())
                + '\n\n')
        f.write(pair_header('SYSTEM', a) + '\n\n')
        f.write(pair_header('PANVK ', b) + '\n\n')
        f.write("delta (system -> panvk): "
                f"instr {a['instr']}->{b['instr']} "
                f"({b['instr']-a['instr']:+d}); "
                f"LD_PKA {a['cls']['ld_pka']}->{b['cls']['ld_pka']} "
                f"({b['cls']['ld_pka']-a['cls']['ld_pka']:+d}); "
                f"FMA/AD {a['cls']['fma']}->{b['cls']['fma']} "
                f"({b['cls']['fma']-a['cls']['fma']:+d}); "
                f"TEX {a['cls']['tex']}->{b['cls']['tex']}; "
                f"regs {a['regs']}->{b['regs']}\n")
        f.write('\n' + '-' * 64 + '\n')
        f.write(f"SYSTEM disassembly  (GPU VA {a['va']:012x}, "
                f"{a['instr']} instr)\n")
        f.write('-' * 64 + '\n')
        f.write('\n'.join(a['text']) + '\n')
        f.write('\n' + '-' * 64 + '\n')
        f.write(f"PANVK disassembly   (GPU VA {b['va']:012x}, "
                f"{b['instr']} instr)\n")
        f.write('-' * 64 + '\n')
        f.write('\n'.join(b['text']) + '\n')


def write_readme(path, sys_recs, pvk_recs, sys_can, pvk_can, pairs):
    L = []
    L.append("ISA shader comparison: system (proprietary 'blob') driver vs panvk.")
    L.append("Two pandecode dumps of the SAME menu but DIFFERENT frames, so draws are")
    L.append("not 1:1 and shader sets only partly overlap. All counts measured, not invented.")
    L.append("FILES: shaders_system.txt / shaders_panvk.txt = one row per unique GPU VA")
    L.append("(stage, registers, draws, instr, LD_PKA/LD_ATTR/LD_VAR/TEX/IADD_IMM/MOV/")
    L.append("FMA+FADD+FMUL/STORE/BRANCH counts, code bytes), sorted by draws.")
    L.append("pair_NN_<stage>.txt = full disassembly of each matched pair + per-class counts.")
    L.append("METHOD: for pairing, VAs with the same mnemonic sequence are collapsed to one")
    L.append("archetype (panvk re-uploads binaries; each driver bakes constants per variant).")
    L.append("Pairs share a stage; the score uses compiler-stable features -- TEX count and")
    L.append("varying widths (fragment), attribute widths and FMA count (vertex); raw instr")
    L.append("and LD_PKA are weighted low (panvk scalar matrix loads vs blob wide loads) and")
    L.append("DISCARD/ATEST ignored (panvk always emits it); greedy one-to-one match. High/")
    L.append("medium confidence require the primary feature (TEX / attribute widths) equal.")
    L.append(f"System: {len(sys_recs)} VAs, {sum(r['draws'] for r in sys_recs)} "
             f"bindings, {len(sys_can)} archetypes.  Panvk: {len(pvk_recs)} VAs, "
             f"{sum(r['draws'] for r in pvk_recs)} bindings, {len(pvk_can)}.")
    L.append("PAIRS (system->panvk):")
    for i, (a, b, s) in enumerate(pairs, 1):
        prim = (f"tex {a['cls']['tex']}->{b['cls']['tex']}"
                if a['stage'] == 'Fragment'
                else f"attr {a['cls']['ld_attr']}->{b['cls']['ld_attr']}")
        L.append(f" {i:02d} {a['stage'][:4].lower()} {confidence(a,b,s):8} "
                 f"sim {s['final']:.3f} instr {a['instr']}->{b['instr']} "
                 f"pka {a['cls']['ld_pka']}->{b['cls']['ld_pka']} "
                 f"reg {a['regs']}->{b['regs']} dr {a['draws']}/{b['draws']} "
                 f"{prim}")
    si = sum(a['instr'] for a, b, _ in pairs)
    pi = sum(b['instr'] for a, b, _ in pairs)
    sp = sum(a['cls']['ld_pka'] for a, b, _ in pairs)
    pp = sum(b['cls']['ld_pka'] for a, b, _ in pairs)
    srg = Counter(a['regs'] for a, b, _ in pairs)
    prg = Counter(b['regs'] for a, b, _ in pairs)
    rg = lambda d: '+'.join(f"{v}x{k}" for k, v in sorted(d.items()))
    L.append(f"AGGREGATE: instr {si} vs {pi} (panvk {pi-si:+d}); LD_PKA {sp} vs "
             f"{pp} (panvk {pp-sp:+d}, scalar matrix loads); regs sys {rg(srg)}, "
             f"panvk {rg(prg)}.")
    unc = [f"pair_{i:02d}" for i, (a, b, s) in enumerate(pairs, 1)
           if confidence(a, b, s) in ('low', 'uncertain')]
    if unc:
        L.append("UNCERTAIN (same stage/shape, primary feature or arithmetic "
                 "differs; maybe different source): " + ', '.join(unc) + ".")
    else:
        L.append("UNCERTAIN: none; every chosen pair is medium or high.")
    with open(path, 'w') as f:
        f.write('\n'.join(L) + '\n')


# -------- main ----------------------------------------------------------------

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    want = 10
    for a in sys.argv[1:]:
        if a.startswith('--pairs'):
            want = int(a.split('=')[1])
    if len(args) < 3:
        sys.exit("usage: isapairs.py SYSTEM_DUMP PANVK_DUMP OUT_DIR [--pairs=N]")
    sys_dump, pvk_dump, out = args[0], args[1], args[2]
    os.makedirs(out, exist_ok=True)
    for fn in os.listdir(out):          # drop stale pair files from prior runs
        if fn.startswith('pair_') and fn.endswith('.txt'):
            os.remove(os.path.join(out, fn))

    sys_code, sys_uses = parse_dump(sys_dump)
    pvk_code, pvk_uses = parse_dump(pvk_dump)
    sys_recs = build(sys_code, sys_uses)
    pvk_recs = build(pvk_code, pvk_uses)

    write_table(os.path.join(out, 'shaders_system.txt'),
                'System (blob) driver - per-shader ISA table', sys_recs)
    write_table(os.path.join(out, 'shaders_panvk.txt'),
                'panvk driver - per-shader ISA table', pvk_recs)

    sys_can = canonicalize([r for r in sys_recs if r['stage'] != 'Compute'])
    pvk_can = canonicalize([r for r in pvk_recs if r['stage'] != 'Compute'])
    pairs = match(sys_can, pvk_can, want)
    for i, (a, b, s) in enumerate(pairs, 1):
        fn = f"pair_{i:02d}_{a['stage'].lower()}.txt"
        write_pair(os.path.join(out, fn), i, a, b, s)

    write_readme(os.path.join(out, 'README.txt'),
                 sys_recs, pvk_recs, sys_can, pvk_can, pairs)

    print(f"system: {len(sys_recs)} VAs ({len(sys_can)} archetypes), "
          f"panvk: {len(pvk_recs)} VAs ({len(pvk_can)} archetypes)")
    print(f"{len(pairs)} pairs:")
    for i, (a, b, s) in enumerate(pairs, 1):
        print(f" pair_{i:02d} {a['stage']:8} {confidence(a,b,s):9} "
              f"sim={s['final']:.3f} base={s['base']:.3f} imm={s['imm']:.2f} | "
              f"sys {a['va']:012x} i={a['instr']} pka={a['cls']['ld_pka']} "
              f"tex={a['cls']['tex']} var={a['cls']['ld_var']} "
              f"attr={a['cls']['ld_attr']} fma={a['cls']['fma']} "
              f"reg={a['regs']} dr={a['draws']} -> "
              f"pvk {b['va']:012x} i={b['instr']} pka={b['cls']['ld_pka']} "
              f"tex={b['cls']['tex']} var={b['cls']['ld_var']} "
              f"attr={b['cls']['ld_attr']} fma={b['cls']['fma']} "
              f"reg={b['regs']} dr={b['draws']}")


if __name__ == '__main__':
    main()
