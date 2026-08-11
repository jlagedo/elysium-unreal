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
- What use lifecycle and output surface do `prop_switch`, `prop_button`, `prop_doorknob`,
  `prop_sign`, and `prop_hacking` expose?
- When do VCD `firetrigger`, scene completion, start and cancel outputs enter the ordinary queue?

## Reproduction

Use a workstream-private copy of the analyzed Ghidra project; never mutate the shared project.
Generated decompilation and game-derived evidence remain below `ELYSIUM_WORK_ROOT`.

```powershell
uv run elysium research tutorial-event-resolution research/cases/tutorial-event-resolution/specs/tutorial_event_resolution.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_tutorial_events_<task>" --kinds funcs,xrefs,fields,vtables,grep
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
