# VtMB core-mechanics research case

This offline case joins character ratings, consumer-specific checks, player verbs, combat
damage and health commit in the pinned retail server module. It does not run or modify the
game and it does not implement the Unreal combat system. Generated decompilation and copied
Ghidra projects remain below `ELYSIUM_WORK_ROOT`.

## Questions

- Which consumers compare a feat rating directly, which roll, and which use opposed or hybrid
  checks?
- How does `CVDmg_t` parse authored `Dmg`, and which words carry damage family, damage values,
  trait references, Source flags, soak and resolver policy?
- Where do ranged and melee paths roll defense, and how do their results reach damage?
- How are damage successes, automatic successes, soak and NPC template filters ordered?
- How do blood shield, unkillable, aggravated tracking and Source health projection commit?
- Which command/usercmd/action/effect layers own attack, block, reload, use, feed and discipline
  verbs?
- Which remaining joins prevent an implementation-grade melee and combat-state specification?

The established facts are consolidated in `docs/vtmb/skills-and-checks.md`,
`docs/vtmb/combat-and-damage.md` and `docs/vtmb/gameplay-verbs.md`. The open joins remain in
those documents and RE40.

## Evidence and procedure

1. Use the patch-first exported `vdata/system/` and `vdata/items/` corpus. Record provenance;
   do not copy those game-derived files into Git.
2. Copy the completed analyzed Ghidra project to a workstream-private directory. Never open or
   mutate the shared project directly.
3. Run the server specification against the hash-pinned `vampire.dll`.
4. Join `FeatValue`, dice construction/rolling and each consumer instead of assuming every
   feat use rolls.
5. Follow `CVDmg_t` from parser and callback registration through ranged/melee attack paths,
   common apply/soak and `OnTakeDamage_Alive`.
6. Treat data-file comments as leads. A field's numerical role is confirmed only when joined to
   native reads/writes.
7. Use live retail diagnostics only for the remaining timing and formula validation; preserve
   captures below `ELYSIUM_WORK_ROOT`.

The driver verifies the binary hash and writes derived output below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/core-mechanics-server/`:

```powershell
uv run elysium research core-mechanics research/cases/core-mechanics/specs/core_mechanics_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_core_mechanics_<task>" --kinds funcs,asm,xrefs,fields,grep
```

For a focused rerun, append `--address <address>` and narrow `--kinds`. Use `--dry-run` to
validate the specification and print the planned Ghidra calls without executing them.

## Consumers

- Check policy: `docs/vtmb/skills-and-checks.md`
- Damage pipeline: `docs/vtmb/combat-and-damage.md`
- Verb/action routing: `docs/vtmb/gameplay-verbs.md`
- Dice algorithm: `docs/recovered/dice-system.md`
- Project/research status: `docs/project/roadmap.md`
