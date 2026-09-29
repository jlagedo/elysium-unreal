# Review · wave 3

## Findings

1. `ElysiumNpcBaseMotor.cpp:958-966`: `PrepareMoveRequest` asks `MaxYawSpeed()` again for every leg. Retail's move turn reads the stored `m_YawSpeed +0x38`, which is `UpdateYaw(-1)`'s word. A task-set speed (`BaseRunTask.cpp:167`, Andrei's 10.0) is lost on the next leg, and an activity change mid-leg does not reach the rate. Fix: hand the rate from `MotorYawSpeedWord`.
2. `ElysiumNpcBaseFacing.cpp:36-51`: the facing queue is collapsed to the last point. Retail keeps a weighted, expiring average (slot 15 `0x102e2180`, `shape.md:2860`), and the entity form copies `Entity->Origin` once. `:250-262`: `ClearFacingTarget` drops every entry, but retail slot 7 `0x102e11f0` cancels only the current one (`shape.md:444`). The motor is the right owner (`motor+0x54`); the shape is not. Observable: an NPC running for cover with two facing targets, or one target that moves.
3. `ElysiumNpcBaseRunTask.cpp:184-188`: the ×10 and the int truncation follow the SDK's `AI_ClampYaw(yawSpeed*10)` and Rules.txt's "tenth of a second". `0x102e1e20` is not read. `shape.md:1347` still says "degrees per second", so the doc contradicts the code. Fix: record the unit in `shape.md` as unrecovered. Observable: how long retail takes to complete a `FacingIdeal` task through 180° (45 gives 0.4 s here, 4 s unscaled).
4. `ElysiumNpcBaseMotor.cpp:969-989`: slot 69 is snapshotted when the leg is issued. Retail asks at each contact. That misses the kick-prop handle (`0x1029b180`), flag-bit 0x16 changes, and entities spawned mid-leg. `IsInert` also skips hidden solids. `Motor->Stop()` sites other than `ElysiumNpc.cpp:2089` leave the set registered until the next sample. Observable: a Gargoyle crossing a `func_door_rotating` that opens mid-leg.
5. `ElysiumAnimatingOverlaySlots.cpp:120`: `Flinch[]` (`OverlaySlotBodies.inl:33-49`) has lost its only writer. `Slot266` and the pawn copy (`EntityWorld.cpp:883`) now move constants. Report F's deletions are supported by the ledger (`seam-list.md:151-214`, targets 0010/0015). They still drop retail quirks the rows said to keep: the `m_bNoFlinch` gate, three-slot earliest-expiry reuse, and `ProcessSceneEvents`' count re-read. Fix: note them under 0010/0015. Observable: a walking NPC keeps walking through a flinch.

## Departures judged sound

- `ElysiumProp.cpp:645-653`, the catalogue-prop mask: supported by `StandardFilterRules 0x101d3080` (`101d30f2`) and `0x10107630`. Observable: the live stealth check through the `npc_transparent` `prop_dynamic` rows.

## Checked and clean

- Verdicts on the deleted Facing rows 519/520/526 (`delete-list.md:164-169`).
- Surviving symbols: only comments name deleted code.
- The slot-584 one-shot.
- Mask-bit plumbing (`QueryIgnoreMask`).
