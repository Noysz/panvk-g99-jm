# Publication checks, 2026-10-20

These checks cover the candidate patches, tools and selected evidence being
published. They do not add hardware runs or change the recorded CTS outcomes.

- All 28 Python tool tests pass: patch-stack comparison, gameplay analysis and
  query-trace parsing. See `unit-tests.log`.
- The published patch stack reconstructs the same source tree as the preserved
  final replay: 12,928 files, zero differences, six support-file exclusions and
  one explicitly allowed root-document pointer mode difference. See
  `patch-chain.json` for the base, patch hashes and exclusions.
- Five donor patch subject lines were normalized. Their authors, dates,
  message bodies and source diffs are unchanged. The candidate provenance
  file distinguishes imported upstream hashes from published file hashes.
- All eight Naruto timing analyses reproduce from the published CSVs. Primary
  pairs remain 2 and 3; pairs 1 and 4 remain excluded.
- Published evidence hashes, donor provenance and local documentation links
  were checked. `checks.json` records counts and results.

Older evidence retains its test-time hashes and statements about unpublished
work. The reports identify those historical records; the current publication
replay is `patch-chain.json` here. No driver/game binaries, game saves or raw
GPU memory are part of this publication.
