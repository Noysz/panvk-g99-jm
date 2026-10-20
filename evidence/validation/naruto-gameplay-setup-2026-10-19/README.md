# Naruto Storm 2 gameplay setup

The interactive pilot reaches the initial village in Ultimate Adventure after
the intro, dialogue and tutorials. Character orientation changes in response
to the recorded right-movement input. Selected images show the village,
characters and minimap. Combat has not been tested in this follow-up.

The stack is the preserved compiler/cache Android candidate, Proton 10.0-4
arm64ec with Hangover 11.16/FEX, DXVK 2.3.1-gplasync and the Winlator wrapper.
`PANVK_V9_CLIP_KILL_FPK=0`, UBO push 32, deferred occlusion off; both caches
are disabled. `manifest.json` records hashes and launch controls. The shader
and pipeline counts in the HUD are diagnostic; GPL support in the log alone
does not prove which submitted draws benefit from K2.

This is setup evidence, not a performance result. Source verification and
repository inspection ran during the pilot. The copied config requests
640x360; DXVK initially creates that size, then recreates the swapchain at
640x480. A stable resolution, frozen state and an affected draw-path capture
are required before K2 comparisons. HUD FPS is not used as a benchmark.

`manual-actions.json` records keyboard inputs. Inputs before the last action
used a 0.25 second hold; the final movement used two seconds. `pilot.py` is
the launched script; `input.py` includes the later optional hold-duration
argument. These are interactive setup helpers, not an automated benchmark.
The before-input image is taken during the final autosave notification.
The after-input image shows the character turned to the right.

The pilot uses a copied prefix and game directory, with large game
assets linked from the installation. Thirty original files retain their
hashes. The full original-state check and the new village save are retained
privately in `~/panvk-refs/naruto-2026-10-19/`; no save or registry contents
are included here. The pilot was stopped deliberately after the input check.

Runtime logs also contain existing Fontconfig and ALSA configuration warnings.
Audio was not validated. Selected rendering images do not establish complete
game compatibility or a K2 speedup.
