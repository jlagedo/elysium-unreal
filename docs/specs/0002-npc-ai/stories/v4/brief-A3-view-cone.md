# Brief A3 — V4a: slot 363 on the combat character (coder; no build)

**Final (amended after V4r, 2026-10-04).** Read `README.md` here (§1 "Slot 363 on the enemy" with
its amendment; §2 M7), `packets-R2.md` item 6, `docs/vtmb/npc-ai/senses.md` § "Cone" (the three
inputs per class), the records `perception/sense_enemy_facing_me.json` and
`combat/ranged_open_fire.json`. After the seam commit. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterSlots.cpp` (slot 363's stub ~:841-846
  only)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacterSlotBodies.cpp` (beside slot 362's body)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSenses.{h,cpp}` (`IsInViewCone`'s threshold)
- `Source/ElysiumUE/Private/Tests/ElysiumCombatCharacterConeTests.cpp` (new)

**Not** the player's spawn file: `FElysiumPlayer::Spawn` lives in `ElysiumPlayerEntity.cpp`, which
is lane A4's this wave, so **A4 writes the player's `FieldOfView = 0.5`** (item 3 below is its
line, not yours). `Public/ElysiumPlayer.h` is A4's too: slot 363 is already declared with the
other combat-character slots; if you need any other declaration there, report the exact line.

## The job

1. **`FElysiumCombatCharacter::FInViewCone(FElysiumEntity* Candidate)`** as `0x10326750` (decompile
   in README §1): the candidate's slot 192 point — `0x10027160` on NPC and player alike, **the
   collision box centre in world space** (R2 item 6; use the port's accessor for it, Grep the
   address) —, this character's `FieldOfView` (the seam's word), the candidate's slot 29 cone
   scalar — a Troika NPC answers `0x101aa630` → `m_flStealthVisionCone +0x63c8`, which has no
   writer in the image, so **0** (inferred: keep the word and its read, do not hard-code 0); the
   player answers `0x1034f390` → `+0x1c74`, already ported for the NPC-side sight test: reuse its
   accessor —, then the 3-D body `0x103264d0` (the 2-D body only when `debug_view_cone_2d3d` reads 2;
   it ships 3 — keep the existing ConVar read). A null candidate: what retail does (it dereferences;
   the port answers false and says so at the line). Remove the counting stub.
2. **The cone body's threshold**: `FElysiumNpcSenses::IsInViewCone(const FElysiumEntity&, point,
   scalar)` compares against the **observer's** `FieldOfView` (`0x1032669c`: `cos × scalar` vs the
   FOV), not `ElysiumNpcSense::DefaultViewConeDot`. Every existing caller is an NPC observer whose word
   reads 0.2, so their answers do not move; check each caller by Grep and list them in your report.
3. **The player's FOV** — lane A4's line, stated here so you know the value your cone will meet:
   `CBasePlayer::Spawn 0x1016d260` writes `m_flFieldOfView = 0.5`; slot 363 on `CHL2_Player` is
   the base `0x10326750` (no override to port). Your tests set `FieldOfView = 0.5` on the fixture
   player themselves; they must not depend on A4's write.
4. **Tests** (`Elysium.Arm.CombatCharacter.FInViewCone`): the player at 0.5 facing an NPC 300 units
   ahead → true; the NPC 100° off the player's forward → false; behind (`dot < 0`) → false; the NPC
   observer's answers unchanged at 0.2. Delete a slot-363 stub-count assertion if one exists (Grep).

## Not yours

`GatherEnemyConditions` (its call site is already retail), the Troika override `0x102b4540`, the
dispatcher (A1), the clock (A2), the player's file and its FOV write (A4).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words.
