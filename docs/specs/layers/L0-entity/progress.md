# L0 progress — the orchestrator's log

One row per worker run (`runs.md`). Pipeline per run: Haiku CLI reader walks the retail functions
(`walks/<run>.md`) → a Sonnet confirmer verifies the walk in place → one worker ports the run's briefs
(one coding lane per checkout, `method.md`) → the coordinator gates (build, default tier, the run's
records) and commits the run alone.

| run | walk | confirmed | worker | gate | commit |
|---|---|---|---|---|---|
| L0-r001 | — | — | done | build ok, default tier ok, 4 selftest records | `27ddd056` |
| L0-r002 | — | — | done | pytest 61, kernel --check ok, build ok, 169/0 | `6475c1b7` |
| L0-r003 | haiku 86 turns $0.51 | sonnet: constants recovered (1/36, 20, 40), 4 corrected | running | — | — |
| L0-r004 | haiku 71 turns $0.46 | sonnet: 4 corrected (strtol/atoi, callers of GetInt/GetFloat) | — | — | — |
| L0-r005 | haiku 82 turns $0.61 | sonnet: 3 corrected (preset block, SetMoveType, UTIL_Remove path) | — | — | — |
| L0-r006 | haiku 126 turns $0.77 | sonnet: fadein 2560 not 512; slot 113 is Activate (once, ServerActivate); preset table decoded | — | — | — |
| L0-r007 | haiku 79 turns $0.44 | sonnet: lazy getters -2 sentinel, SndScheme_Char registry, RTTI CBaseTerminal/CPropSwitch | — | — | — |
| L0-r008 | haiku 70 turns $0.43 | sonnet: RandomInt(0,0) draws nothing; rec+0x48 = missing-wave flag; case-insensitive interning |
| L0-r009 | haiku 79 turns $0.35 | sonnet: eviction key = quietest retiring; retire rate +0x48/duration; 0x1044fab0 double |
| L0-r010 | haiku done | sonnet running |
| L0-r011 | haiku running | — | — | — | — | — | — | — |

## Notes for the owner (raised by the readers)

- r004: `FUN_102482f0` (hash-keyed file-text cache, pathID 0) needs nothing above L0; `hooks.tsv:325`
  lists it as an L4 hook and `audit.tsv` calls it a string pool — classification looks wrong.
- r004: the typed setters `FUN_10249160/102491a0/102491e0` write the node type onto the container, not
  the child (retail bug; reproduce).
- r003: `0x101ac570` exact multiples of 36 depend on x87 precision (R=36 → 40 or 39); records must avoid them.
- r006: `docs/vtmb/npc-ai/senses.md:442,453` say "owner origin" for the AI-sound insert; the asm says the source entity's origin.
- r007: the sound-scheme seam's layer: `hooks.tsv` says L0, `audit.tsv` says L2 — settle before the layer closes.
- r008: `hooks.tsv:365` hook needs removing or re-pointing; `docs/vtmb/` has no section for 0x1012f700, 0x101b3240, 0x101b33f0, 0x101b2f60.
- r009: `docs/vtmb/audio_pipeline.md:250-254` (`0 is instant`) contradicts the code; Combat/Alert `Filename` default inherits the Music name.
- r005: `health`/`radius`/`message` of ambient_generic are code-built datamap keys the ledger missed;
  `docs/vtmb/audio_pipeline.md:354-355` should read `20/x` for fadein/fadeout.
