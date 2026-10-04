# Brief A3 — V4a: slot 363 on the combat character (coder; no build)

Read `README.md` here (§1 "Slot 363 on the enemy"; §2 M7), `packets.md` § R2 item 6,
`docs/vtmb/npc-ai/senses.md` :44-80, the records `perception/sense_enemy_facing_me.json` and
`combat/ranged_open_fire.json`. After the seam commit. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterSlots.cpp` (slot 363's stub ~:841-846
  only)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterSlotBodies.cpp` (beside slot 362's body)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.{h,cpp}` (`IsInViewCone`'s threshold)
- the player's spawn file where R2 item 6 places the `m_flFieldOfView` write (expected
  `ElysiumPlayerEntity.cpp`; if R2 names another, that one — say so in your report)
- `Source/ElysiumUE/Private/Tests/ElysiumCombatCharacterConeTests.cpp` (new)

## The job

1. **`FElysiumCombatCharacter::FInViewCone(FElysiumEntity* Candidate)`** as `0x10326750` (decompile
   in README §1): the candidate's slot 192 point (R2: which accessor), this character's
   `FieldOfView` (the seam's word), the candidate's slot 29 cone scalar (R2: what an NPC answers;
   the player's `m_flStealthVisionCone` is already ported for the NPC-side sight test — reuse its
   accessor), then the 3-D body `0x103264d0` (the 2-D body only when `debug_view_cone_2d3d` reads 2;
   it ships 3 — keep the existing ConVar read). A null candidate: what retail does (it dereferences;
   the port answers false and says so at the line). Remove the counting stub.
2. **The cone body's threshold**: `FElysiumNpcSenses::IsInViewCone(const FElysiumEntity&, point,
   scalar)` compares against the **observer's** `FieldOfView` (`0x1032669c`: `cos × scalar` vs the
   FOV), not `ElysiumNpcSense::DefaultViewConeDot`. Every existing caller is an NPC observer whose word
   reads 0.2, so their answers do not move; check each caller by Grep and list them in your report.
3. **The player's FOV**: written where and as retail writes it (R2 item 6; `senses.md` says 0.5).
4. **Tests** (`Elysium.Arm.CombatCharacter.FInViewCone`): the player at 0.5 facing an NPC 300 units
   ahead → true; the NPC 100° off the player's forward → false; behind (`dot < 0`) → false; the NPC
   observer's answers unchanged at 0.2. Delete a slot-363 stub-count assertion if one exists (Grep).

## Not yours

`GatherEnemyConditions` (its call site is already retail), the Troika override `0x102b4540`, the
dispatcher, the clock.

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words.
