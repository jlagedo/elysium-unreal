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

Static `vampire.dll` inspection reaches `PhysicsTouchTriggers`, but the exact ordering of old-contact
ends and new-contact begins is below the `engine.dll` collision-property interface. The
`trigger_autosave` save transaction is also not closed by this server-DLL pass. Keep both explicit
rather than inferring them from Source SDK or the Unreal implementation.

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
