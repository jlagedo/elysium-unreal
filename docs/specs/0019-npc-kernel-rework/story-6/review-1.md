# Review · wave 1

No behaviour drift and no deleted `rule`/`present` body found. The findings are residue.

## Findings

1. `Public/ElysiumAnimatingSlotBodies.inl:9-15,44,76,80,84,86,100,106`: these survive with no body:
   - `FMoveRebound`
   - `MoveReboundBlend`, `MoveReboundState`
   - `SetLocal(Angular)Velocity`
   - `FormatKeyValueVector`/`Float`
   - the ledger's `MoveDone` counter

   Their rows are closed (0x101c10d0, 0x1009eca0, 0x1009ebb0, 0x101c1720). It compiles only because nothing calls them. R1 said these were clean, and R1c took the file for generated; it is hand-written. Fix: delete them.
2. `Public/ElysiumCombatCharacterSlotBodies.inl:215-218`: `PythonInteropObject` is still declared, but its body is gone (0x1014f8b0). Fix: delete the declaration.
3. `BaseRunTask.inl:30`: `MotorYawSpeedWord` is now write-only (`BaseRunTask.cpp:167`). Its only reader, Debug's `MotorYawSpeed()`, is deleted, and the comment still names it. Fix: reword the comment, or send the word for re-verdict.
4. `Combat10.cpp`, in `PreSelectIdealStateRetail`: `Bound = 2` is a retail constant written inline. Fix: report the cell to story 4's tunables overlay.
5. Stale comments naming deleted code: `BaseThink.cpp:122`, `BasePrecache10.inl:12-17`, `Maker.cpp:135,523`, `NpcBase.h:70`, `Squad.inl:43-50`, `Cop.cpp:220`, `Select.cpp:353`, `Damage3.inl:22`, `BaseSchedule.inl:26`. Fix: reword them.
6. `targets-A.tsv` and `targets-C.tsv` both close `0x1027ede0`. Harmless.

## Checked and clean

- **Verdicts:** every closed address is `dead` with target `-`. The three `GetUsedHullBits` rows went back to `rule`, and their bodies are kept.
- **Rule bodies:** lines removed from them are prints or stamps only; the branch structure is intact.
- **Pass-throughs:** `SelectTrace` and the search timer were pass-throughs.
- **Moved helpers:** byte-identical apart from comments.
- **Observers:** no surviving symbol references outside findings 1–2, and no Debug includes remain.
- **Standing rule:** nothing new built.
- **Tests:** deleted tests covered only deleted bodies; rewritten tests assert the same facts.
- **Ledger:** consistent.
