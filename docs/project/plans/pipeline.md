# Pipeline plan — open-task specifications

Specifications for **open** PL backlog rows. Status lives solely in `docs/project/roadmap.md`;
a row that lands is deleted here. No status marks in this file. Pipeline boundaries and
formats: `pipeline/CLAUDE.md`.

### PL6 Texlight merge in exporter

The exporter-side half of 3.4: bin type-0 patches by `(intensity, normal)` and merge
deterministically in `UE_bsp_to_scene.py`.

### PL11 Remove the dead Lumen-card path

`export_all.py`'s `bake_cards`/`--no-cards` calls a `cards.bat` that no longer exists and
prints a "skipped" line every run; `ElysiumCardGen.cpp` (`ELYSIUM_WITH_CARDGEN`,
`elysium.cards.probe`) still builds into editor targets. Nothing depends on either.

### PL12 Particle mirror + weather height maps

The merged install's `particles/*.txt` (1,698) + `particles/*.tga` (318) mirror verbatim and
patch-first into `$ELYSIUM_EXPORT_ROOT/particles/`, wired into export orchestration; the
`sm_hub_1` closure is compiled for 7.9. Per-map 2048² R16 top-down height maps use corrected
exported geometry bounds; `sm_hub_1` is accepted, but grid-wide height-map
generation/acceptance has not run. Format: `docs/vtmb/weather.md`. Needed by 7.9.

The audio catalog and its typed sidecars are the audio programme's AUD0 (`plans/audio.md`), even
though the work is the exporter's.
