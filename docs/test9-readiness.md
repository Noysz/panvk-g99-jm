# Test9 readiness - 2026-10-26

Test9 has not been packaged or published. The host and Android candidate
libraries are already built; their identities and validation are in the
[combined-build evidence](../evidence/validation/api-2026-10-26/README.md).

Two steps remain before a test9 package is ready to publish:

1. Freeze the published source stack and package the exact tested Android
   library in a new test9 ZIP with metadata, dependencies, known limitations
   and checksums. Verify the archive and extract it to a fresh directory.
2. Check that the extracted driver and dependencies match the tested hashes,
   then run the package launcher/probe and render/control smoke checks. Review
   the release notes against the evidence. A changed library or dependency
   requires regression testing of that changed configuration.

Completed feature repeats, tracked regression and application runs do not
need to be repeated merely to rename a package containing identical bytes.
The final package smoke check tests packaging and loading, which the current
loose-library results cannot establish. Publishing the GitHub prerelease is
a separate action after these steps.

Test9 keeps depth bounds opt-in and overlap, incremental tiler backing, late
vectorization and deferred occlusion off. Fourteen pre-existing tracked CTS
failures are documented; no tracked Pass was lost against the unchanged
test8 driver. This does not meet the RC/1.0 zero-failure gate.

The [release roadmap](release-roadmap.md) still governs RC/1.0 qualification:
three runs with zero tracked failures on every RC, the longer game matrix and
the regression observation period. Broader T06-T08 work remains open. This
test-build checklist does not mark T10 complete or claim a gameplay speedup.
