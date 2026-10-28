# Test9 package checks

The test9 ZIP packages the Android candidate from source stack
`7f28e47db6e50215fbe2935cecaf048f23d6dbfb` without changing its library bytes.
The dependencies match the October 26 launcher staging, including its
`libdrm` and `libz` versions. Their hashes differ from those in the test8 ZIP.

`manifest.json` records the package used for the runtime checks.
`readme-revision.json` records the first published ZIP, whose README focuses on
T06-T10 and the required baseline/logging settings. Only `README.txt`
changed; the other nine members, including all libraries and metadata, have
identical hashes. The original archive and runtime evidence are preserved.

The current archive has ten root files, passes CRC checks and extracts to
identical input bytes. External `README-v0.1.0-test9.txt` and internal
`README.txt` match. The old release files and 540 historical preservation
pins are unchanged. `environment-source-index.json` records source references
for the previous, longer testing guide.

`smoke.json` records checks from the extracted directory:

- Adrenotools and the launcher library enumerate the device with the test9
  metadata suffix. All four package libraries appear in their mappings.
- Wrapper device creation and `dxvk_probe` pass with depth bounds off/on.
  All four probed D3D11 profiles remain usable; the Sarek depth-bounds
  degradation indicator follows the selected feature state.
- The enumeration opt-in control rejects the device as expected.
- DXVK 2.3.1-stripped renders 180 frames with six successful pixel checks.
  The separate wrong-reference control detects six mismatches and exits 2.
  The package libraries are mapped in both render processes.

There is one package repetition per check on the original extracted package.
The README-only revision was checked by archive/member comparison; these
runtime checks were not repeated for unchanged library/metadata bytes.
The unchanged driver already has
the feature repeats and four-version DXVK matrix recorded in
[the combined validation](../api-2026-10-26/README.md); they were not rerun.
The smoke profile uses UBO push 32 words, timestamps selected and Mesa/DXVK
disk/state caching disabled. It is not the README's shipped-default R0 or
the controlled UBO-off R1/R2 game comparison. Full controls and input hashes
are recorded in the manifest and smoke results.

`selected-logs.txt` retains result, profile and mapping lines. Paths are
normalized; original-log SHA-256 values identify the private raw files.
Test prefixes/processes and the temporary X11 display are stopped after
the check. The extracted inputs and archive keep their original hashes.

This checks packaging and loading on the reconstructed native
Wine/Termux:X11 stack. It is not an actual Winlator app installation,
multi-device qualification, new gameplay benchmark or RC acceptance.
Fourteen old tracked CTS failures remain.

## Initial publication (2026-10-28)

The first ZIP, external README and `SHA256SUMS-v0.1.0-test9.txt` were
initially published on the shared test-build prerelease.
All three downloaded assets match their prepared SHA-256 values; the ZIP
passes CRC checks and its internal README matches the external file.
`publication.json` records asset IDs, URLs and the verification results.
The 17 historical assets, original checksum file and shared tag remain
unchanged. The versioned checksum filename avoids replacing the test1-test8
checksum asset; its contents equal the local test9 `SHA256SUMS.txt`.

Earlier manifests retain their preparation-time publication status.
This upload changes neither source/library bytes nor the runtime scope
described above. It does not close the RC/1.0 gates.

## Concise README revision

The [dedicated test9 release](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9)
contains a 52-line guide: short changelog, four sessions with any game,
test9-specific controls and reporting. `concise-readme-revision.json` records
its hashes. Only `README.txt` changes; all nine other members are identical.
`publication-concise.json` records the download checks. Earlier packages
and their evidence are preserved; no runtime tests were repeated.

The duplicate initial test9 attachments were later removed from the grouped
test1-test8 release after preserving and verifying their bytes. Test9 now
has one download location: the dedicated release linked above. Earlier
publication manifests retain their original asset IDs, URLs and hashes as
historical records; those retired asset URLs no longer serve downloads.

Testing and issue-report instructions:
[README-v0.1.0-test9.txt](../../../docs/test9/README-v0.1.0-test9.txt).
