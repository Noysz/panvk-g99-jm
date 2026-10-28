# Test9 readiness - 2026-10-28

Test9 is [published as a prerelease asset](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9)
and its extracted-package smoke checks pass at the scope recorded below.
The ZIP contains the unchanged Android candidate
and the dependencies used in the earlier launcher checks. See the
[package evidence](../evidence/validation/test9-package-2026-10-28/README.md)
and [combined-build evidence](../evidence/validation/api-2026-10-26/README.md).

The two package steps are complete at the recorded scope:

1. The ZIP, metadata, dependencies, licenses, README and checksums are checked.
   Fresh extraction reproduces all ten members. The external README matches
   the internal `README.txt` byte for byte; old packages remain unchanged.
2. Six launcher/wrapper/profile checks and one enumeration control give their
   expected results. DXVK render/readback passes; the wrong-reference control
   fails as expected. Mapped libraries come from the extracted package.
   The release instructions match the candidate defaults and validation limits.

The current ZIP is a README-only revision of the smoke-tested package.
All nine other members are identical; archive extraction and checksums were
checked again. Runtime results retain their original package identity.

Release files: `FourFectVK-G57-v0.1.0-test9.zip`,
`README-v0.1.0-test9.txt` and `SHA256SUMS.txt`. The dedicated test9 release
contains the concise README revision. Its downloaded assets match the
prepared hashes. Test1-test8 remain on their grouped release page.
See [publication-concise.json](../evidence/validation/test9-package-2026-10-28/publication-concise.json).

The [testing README](test9/README-v0.1.0-test9.txt) gives R0-R3 sessions
for any game, test9-specific controls and brief report instructions. The [build/package order](test9/build-order.md)
records the source stack and package gates. A test9 GitHub issue form is also
prepared in `.github/ISSUE_TEMPLATE/test9-report.yml`.

Completed feature repeats, tracked regression and application runs do not
need to be repeated merely to rename a package containing identical bytes.
The package checks use the reconstructed native Wine/Termux:X11 stack, not
an installation inside the Winlator app. Tester R0 checks are still needed
on the installed container/device. A changed library or dependency requires
appropriate regression. The source/docs commit and asset publication are
recorded separately; uploading this package did not rerun runtime tests.

Test9 keeps depth bounds opt-in and overlap, incremental tiler backing, late
vectorization and deferred occlusion off. UBO push defaults to 32 words in
this candidate; the testing README corrects the older off-by-default guidance.
Fourteen pre-existing tracked CTS
failures are documented; no tracked Pass was lost against the unchanged
test8 driver. This does not meet the RC/1.0 zero-failure gate.

The [release roadmap](release-roadmap.md) still governs RC/1.0 qualification:
three runs with zero tracked failures on every RC, the longer game matrix and
the regression observation period. Broader T06-T08 work remains open. This
test-build checklist does not mark T10 complete or claim a gameplay speedup.
