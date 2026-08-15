# VtMB stealth-kill research case

This offline case recovers the transaction that selects a stealth-kill victim, computes the
attacker's deaf arc and minimum approach depth, publishes eligibility, accepts Use or melee input,
and enters the paired kill action. It composes the ordinary observer recovered by
`research/cases/stealth-detection/` without treating detection and stealth-kill eligibility as one
state. It does not run or modify the game. Generated Ghidra projects and evidence remain below
`ELYSIUM_WORK_ROOT`.

## Questions

- How does `StealthKillRules.txt` extend the `DeafZoneArc` table and clamp Sneaking and hearing?
- Which player, weapon, victim, relationship, state, trace, arc and distance guards qualify one
  cached victim?
- How do victim hearing/readiness and attacker Sneaking produce the minimum rear approach depth?
- Which server fields publish the stealth-kill icon, and which Use or Attack path owns commitment?
- How does grapple mode 3 select paired activities, reserve both actors, resolve success/failure,
  apply death, sound and camera policy, and release state?
- Which output or script state completes the tutorial lesson?

The completed contract belongs in `docs/vtmb/stealth.md`. Generic paired-action mechanics remain
in `docs/vtmb/feeding.md` and `docs/vtmb/animation_and_movers.md`; damage and death remain in
`docs/vtmb/combat-and-damage.md`; tutorial wiring remains in
`docs/vtmb/sp_tutorial_1-event-surface.md`.

## Reproduction

Use a workstream-private copy of the hash-pinned server project:

```powershell
uv run elysium research stealth-kill research/cases/stealth-kill/specs/stealth_kill_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_stealth_kill_<task>" --kinds funcs,asm,xrefs,fields,consts,grep
```

Use `--dry-run` to validate the specification or append `--address <address>` and narrow `--kinds`
for a focused rerun. Generated evidence is written below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/stealth-kill-server/`.

