# VtMB player law-admission research case

This offline case recovers the upstream transaction that turns a player action into a witnessed
criminal or supernatural incident. It joins action producers, NPC perception and zone/context
policy to the player incident consumers already recovered by the player-entity case. It does not
run or modify the game. Generated decompilation and Ghidra projects remain below
`ELYSIUM_WORK_ROOT`.

## Questions

- Which native functions can submit criminal and supernatural incidents?
- What state does one NPC retain while testing the player's activity levels?
- Which visibility, hearing, AI-state, relationship, zone or context guards qualify a witness?
- How are repeated observations ignored, refreshed, escalated or converted to flee/attack state?
- Which player actions enqueue supernatural scares directly rather than waiting for an NPC
  schedule pass?
- Where do Discipline `Overt`, zone legality and witness perception join, and which parts remain
  independent?

The player-wide queue and response ordering belongs in `docs/vtmb/player-entity.md`. Discipline
admission belongs in `docs/vtmb/disciplines.md`; NPC witness state belongs in
`docs/vtmb/npc-ai-reverse-engineering.md`; zone semantics belong in `docs/vtmb/game_runtime.md`.

## Procedure

1. Use a workstream-private copy of the analyzed Ghidra project.
2. Enumerate xrefs to the sole criminal- and supernatural-incident thunks.
3. Recover the NPC schedule branches, retained witnessed-level fields, offender handles,
   locations, ignore/witness timers and processed flags.
4. Recover the player supernatural-scare queue and every writer before classifying its contents.
5. Trace physics and Discipline producers separately; a shared incident consumer does not imply a
   shared admission rule.
6. Join only guards proven by static call/field evidence. Reserve engine visibility ordering and
   presentation for controlled retail acceptance.

```powershell
uv run elysium research player-law-admission research/cases/player-law-admission/specs/player_law_admission_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_player_law_admission_<task>" --kinds funcs,asm,xrefs,fields,grep
```

Use `--dry-run` to validate the specification. Generated evidence is written below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/player-law-admission/`.

## Initial boundary

- `PlayerCriminalIncident` has five native call sites through one thunk, all in three NPC schedule
  bodies; two schedule bodies contain two calls each.
- `PlayerSupernaturalIncident` has three native call sites: the player rule queue and two NPC
  schedule paths.
- The remaining direct criminal sites are NPC subclass schedule paths. Physics drag/lift and throw
  producers raise the player activity channels but do not call either terminal incident thunk.
- Incident submission, Masquerade throttling and police response are distinct stages.

## Recovered joins

- A successful targeted Discipline commit maps target-record `SupernaturalLvl` to the player's
  supernatural activity channel and maps `Overt` to criminal activity level 3. The commit has no
  zone query and `TriggerAISound` remains independent.
- Each NPC compares the player's criminal and supernatural act counts with its own processed
  counts, applies four authored `pl_*` thresholds, and retains severity, origin and offender before
  setting conditions 31 through 34.
- World-law records must be in the NPC view cone, within `m_flSeekDistInspection`, and reached by
  an unobstructed trace. The strongest accepted criminal and supernatural records are selected
  independently.
- Schedule selection/translation is the terminal boundary: it verifies the retained offender is
  still the player, submits the incident and advances the processed count.
- Supernatural flee-only observations enter a 16-byte player queue keyed by NPC identity; repeats
  refresh time and retain maximum severity. Seeing `Player_Nosferatu` can enqueue severity 2
  without a new cast.
- `m_flCriminalWitnessedTimer` and `m_flSupernaturalWitnessedTimer` drive debug markers in
  `NPCThinkDebug`; they are not the observation-memory lifetime.

The static transaction is closed through incident admission. Controlled retail work remains for
frame-exact threshold, occlusion, ignore-window and zone-authority acceptance.
