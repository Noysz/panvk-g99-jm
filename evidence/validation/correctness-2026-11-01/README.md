# November correctness validation

Target: Infinix Note 40 4G (X6853), Mali-G57 MC2, kbase r54p1.
The [five-patch series](../../../patches-candidates/2026-11-01-correctness/README.md)
applies after Test9. It is development source, not a released driver.

The historical Host and Android binaries each ran 5,658 tracked cases:
**3,768 Pass / 1,890 NotSupported / 0 Fail**. Against Test9, 14 TES
transform-feedback failures became Pass; no Pass was lost.

[Build identities](build.json) record incremental builds of source
`ea696dfb5359bcf9055536b782cfa9cc812f476e`. These results do not establish a
clean rebuild or qualify the later combined November stack.

[Results](results.json) retain the test counts, negative controls and hashes
of the original records. Thirty TES/XFB cases passed on Host and Android
in three repeats; disabling capture made all thirty fail. Forced indirect
draws and cache populate/reload controls passed. Indirect compute had
32 Pass / 12 NotSupported.

Not covered: XFB overflow clamping, isolines/point-mode capture, repeated
tessellated draws and counter resume across command buffers, full Vulkan CTS,
Winlator installations and games. No FPS improvement is claimed.
