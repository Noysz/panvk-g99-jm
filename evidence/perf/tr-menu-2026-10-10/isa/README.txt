ISA shader comparison: system (proprietary 'blob') driver vs panvk.
Two pandecode dumps of the SAME menu but DIFFERENT frames, so draws are
not 1:1 and shader sets only partly overlap. All counts measured, not invented.
FILES: shaders_system.txt / shaders_panvk.txt = one row per unique GPU VA
(stage, registers, draws, instr, LD_PKA/LD_ATTR/LD_VAR/TEX/IADD_IMM/MOV/
FMA+FADD+FMUL/STORE/BRANCH counts, code bytes), sorted by draws.
pair_NN_<stage>.txt = full disassembly of each matched pair + per-class counts.
METHOD: for pairing, VAs with the same mnemonic sequence are collapsed to one
archetype (panvk re-uploads binaries; each driver bakes constants per variant).
Pairs share a stage; the score uses compiler-stable features -- TEX count and
varying widths (fragment), attribute widths and FMA count (vertex); raw instr
and LD_PKA are weighted low (panvk scalar matrix loads vs blob wide loads) and
DISCARD/ATEST ignored (panvk always emits it); greedy one-to-one match. High/
medium confidence require the primary feature (TEX / attribute widths) equal.
System: 90 VAs, 849 bindings, 68 archetypes.  Panvk: 252 VAs, 2334 bindings, 103.
PAIRS (system->panvk):
 01 frag high     sim 0.831 instr 29->68 pka 0->9 reg 32->32 dr 1/28 tex 2->2
 02 frag low      sim 0.821 instr 258->384 pka 22->52 reg 32->64 dr 33/3 tex 4->5
 03 frag high     sim 0.819 instr 67->135 pka 0->13 reg 32->32 dr 1/3 tex 4->4
 04 frag medium   sim 0.777 instr 89->160 pka 0->18 reg 32->64 dr 7/49 tex 5->5
 05 frag medium   sim 0.761 instr 29->60 pka 0->9 reg 32->32 dr 1/48 tex 1->1
 06 vert high     sim 0.857 instr 134->188 pka 9->32 reg 32->64 dr 9/9 attr 4->4
 07 vert high     sim 0.856 instr 162->250 pka 21->65 reg 32->32 dr 44/43 attr 3->3
 08 vert high     sim 0.848 instr 110->155 pka 10->33 reg 32->32 dr 34/31 attr 5->5
 09 vert high     sim 0.843 instr 49->69 pka 6->16 reg 32->32 dr 175/673 attr 1->1
 10 vert high     sim 0.838 instr 77->114 pka 9->29 reg 32->32 dr 6/6 attr 2->2
AGGREGATE: instr 1004 vs 1583 (panvk +579); LD_PKA 77 vs 276 (panvk +199, scalar matrix loads); regs sys 10x32, panvk 7x32+3x64.
UNCERTAIN (same stage/shape, primary feature or arithmetic differs; maybe different source): pair_02.
