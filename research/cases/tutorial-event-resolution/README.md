# VtMB tutorial event-resolution research case

This case recovers the event layer exercised by patch-first `sp_tutorial_1`: output parsing and
ordering, queue recursion, Python scheduling, trigger/refire gates, teleport placement, specialized
prop use, NPC-maker child outputs, and choreographed-scene trigger production. It deliberately does
not recover animation blending, audio playback, or presentation internals.

## Questions

- Which output fields are consumed, in what row order, and how do `times` counters expire?
- How are equal deadlines ordered, when do recursively produced events run, and can a cycle starve
  the world frame?
- In what order do named I/O, field-5 Python, direct handles, synchronous reflected Python inputs,
  `logic_pythoncheck`, and `ScheduleTask` resolve?
- Which trigger admission, wait, one-shot, relay-refire, hidden/disabled, and changelevel guards
  swallow work rather than defer it?
- What transform and safety checks does `point_teleport` perform, and where does touch
  reconciliation occur?
- How does `CAI_BaseNPCTroika::TeleportToEntity` convert its destination, write the NPC transform,
  schedule AI work, and expose the discontinuity to clients?
- Do maker-authored NPC lifecycle rows fire on the maker or on each child?
- Which quota, enable, obstruction, visibility, distance, and hull guards admit explicit and timed
  maker spawns, and how do finite/infinite totals differ from the live-child ceiling?
- What use lifecycle and output surface do `prop_switch`, `prop_button`, `prop_doorknob`,
  `prop_sign`, and `prop_hacking` expose?
- When do VCD `firetrigger`, scene completion, start and cancel outputs enter the ordinary queue?
- During the `wait == -1` window between `SetTouch(NULL)` and `SUB_Remove`, does a new contact still
  produce `OnStartTouch`, and does anything else — `EFL_KILLME`, solidity, touch-link state — gate it?
- Which link state pairs an `EndTouch` to its begin, and does a self-removing entity receive its own?
- Does anything on the synchronous reflected-input path — `AcceptInput`, the PyMethodDef body, or the
  embedded interpreter — bound script→input→script recursion?

## Reproduction

Use a workstream-private copy of the analyzed Ghidra project; never mutate the shared project.
Generated decompilation and game-derived evidence remain below `ELYSIUM_WORK_ROOT`.

```powershell
uv run elysium research tutorial-event-resolution --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_tutorial_events_<task>" --kinds funcs,xrefs,fields,vtables,datamaps,consts,grep
uv run elysium research ent_survey --patch --map sp_tutorial_1
```

Join the derived native evidence to the hash-pinned `.ents`, `tutorial.py`, `vamputil.py`, Jack
dialogue, and the three VCDs listed in `docs/vtmb/sp_tutorial_1-event-surface.md`. Spatial thresholds
use Source feet plus the recovered 32×32×72 standing player hull.

## Consumers and remaining boundary

- Generic entity/trigger/teleport facts: `docs/vtmb/entity_io.md`
- Queue/frame facts: `docs/vtmb/game_runtime.md`
- Python boundary: `docs/vtmb/python_bridge.md`
- VCD boundary: `docs/vtmb/choreographed_scenes.md`
- Map-specific join: `docs/vtmb/sp_tutorial_1-event-surface.md`
- Status: `docs/project/roadmap.md` RE43

The ordering of old-contact ends and new-contact begins is now closed across both `vampire.dll` and
`engine.dll`: a new `StartTouch` fires synchronously inside the engine's relink (`engine.dll
FUN_20111f30`), while a stale `EndTouch` is deferred to `GameFrame`'s post-entity-think step, so
**new begins precede old ends within a frame** (`docs/vtmb/entity_io.md` → "New begins precede old
ends within a frame"). What stays open is only the partition's *intra-leaf* element order — the
enumeration delegates to a caller-supplied comparator two decompile layers past what this pass
reached, not a literal BSP walk — and needs a live capture (three overlapping `trigger_multiple`
volumes in different `.ents` orders) to settle. `trigger_autosave`'s full chain — `CTriggerSave`,
the engine `ServerCommand`/`Host_AutoSave_f` deferral, the save-blocked latch and `SaveGameSlot` —
is also closed (`docs/vtmb/entity_io.md` → "`trigger_autosave` (`CTriggerSave`)"). Both remaining
items narrowed further under static analysis alone: `gpGlobals+0x18` is confirmed **not** a raw
entity/client-count copy — it is `1 - (int)(-1.0 / x)` over a separate, still-unnamed global
(`engine.dll 0x20b42af4`), reducing the guard to "this session's underlying count is exactly one";
naming that global's ConVar/registration site needs a debugger read. The save-blocked reason codes
are now datamap-cross-referenced and callee-decompiled rather than shape-guessed: codes 6/7 are
confirmed to be the same `+0x1db0` relationship split by state (`==3` vs. any other live state) plus
the datamap-named camera pair `m_hCameraViewEntity`/`m_hCameraTargetEntity`; code 1's global
predicate resolves to the world-entity singleton. Codes 2/3/4 and code 1's `+0x1EB8` test are now a
**terminal static result**: the full base chain from `CBasePlayer` to the root `CBaseEntity`
(`CBasePlayer` → `CBaseCombatCharacter` → `CBaseFlex` → `CBaseAnimatingOverlay` → `CBaseAnimating` →
`CBaseToggle` → `CBaseEntity`, every one of the seven datamaps walked) carries no `typedescription_t`
at any of those four offsets — they are ordinary runtime `EHANDLE`/int fields the function reads
directly, never exposed to Hammer or save/restore. Only a live logged walk of
`GetSaveBlockedReason`'s return across player states (idle, dialogue, terminal, feeding, climbing,
cutscene, dead) can name them further, same as the final `vtbl+0x278` check (`IsAlive()` HYPOTHESIS).

The player's touch-relink cadence is closed: `CBasePlayer::PostThink` (`0x1016be10`, called from
`CPlayerMove::RunCommand` → `RunPostThink` once per **processed usercmd**, not once per server
frame) unconditionally reaches `CBasePlayer::SimulatePlayerSimulatedEntities` regardless of which
branch its live-body update takes (`docs/vtmb/player-entity.md` → "Recovered `PostThink` body";
`docs/vtmb/game_runtime.md` → "The server stage in order").

The `CNPCMaker` boundary needed by the tutorial is closed. Public `Spawn` and timed spawning share
the same admission path; start-disabled suppresses only the timer; infinite mode bypasses finite
exhaustion but not the live-child ceiling; and rejected attempts neither allocate nor fire outputs.
The exact guard, retry, accepted-child and death/removal order is recorded in
`docs/vtmb/entity_io.md`, with the `blueblood_maker` join in the map-specific consumer.

The server side of that boundary is closed: `CServerGameEnts::MarkEntitiesAsTouching`
(`FUN_1011be20`, reachable only through the interface vtable slot at `0x1001017c`) is where
`engine.dll` hands a pair over, and every gate after it — link dedup, the begin-flag bit,
`EFL_KILLME`, the `StartTouch`/`Touch`/`EndTouch` slots — is recovered. What stays open is only
*which* pairs the partition enumerates per frame and in what relative order.

The recursion question is closed server-side and open on the C stack. The embedded interpreter is
CPython 2.1.2 (`Bin/vampire_python21.dll`, SHA-256
`2ce854ddd5191721f38ccbe6c19988655632c5c9a79fdfef03a4cd8f49b04645`, image base `0x1e100000`); its
`recursion_limit` global at `0x1e1808d8` initializes to 1000 and `vampire.dll` imports neither
`Py_SetRecursionLimit` nor `Py_GetRecursionLimit`. Whether 1000 nested levels fit in
`Vampire.exe`'s 1 MB main-thread stack reserve is not decidable from the images and stays open.

The NPC-teleport boundary is closed for every concrete receiver family in the current corpus.
`TeleportToEntity` is a
`CAI_BaseNPCTroika` `FIELD_EHANDLE` input; `AcceptInput` converts the authored string through the
global first-match entity-name lookup, and the native body copies absolute origin and all angles.
Jack's `CNPC_VVampire`, `CNPC_VHumanCombatant` vtable `0x104b7ff4`, and `CNPC_VHunter` vtable
`0x104b9784` all resolve virtual slot `+0x998` through thunk `0x10010f0f` to `FUN_102c23f0`, which
schedules the general plus four Troika think lanes for `curtime` before the handler forces one
second of transmission. It performs no safe-placement, velocity, navigation, schedule or touch
reset.
