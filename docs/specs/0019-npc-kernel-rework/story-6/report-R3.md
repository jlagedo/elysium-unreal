# Report R3 · residue sweep and missing seams

(Saved by the orchestrator from the lane's final answer. Nothing built or run; corpus server down.)

**1. Yaw ladders (done).** Slot 516 is back as data on Base, Troika, Dog, MingXiao, Tzimisce and Werewolf, with the trimmed ladder tests. `0x102e1cf0` now stores `MaxYawSpeed()` in `+0x38`, including at the tail of `SetActivityAndSequence`. `UpdateYaw` hands the mover `int(speed)×10` degrees per second: the ×10 and the int truncation come from the SDK and from Rules.txt's "degrees / tenth of a second", not from this binary (**unrecovered**). MingXiao reads Rules `TurnSpeedNormal` / `TurnSpeedAttack` (record +0x44 / +0x48); the defaults 6 and 2 are the file's commented originals.

**2. Slot-584 one-shot (done).** `FElysiumEntityWorld::RunNetworkManagerFirstThink` runs slot 584 on every NPC once per build stamp (retail: the network manager's one-shot think `0x102f6a50` at build stamp + 0.8 s). Test `Think19.Gate.Slot584OneShot`.

**3–4. Seams (done)**, each with a live body, recording-motor support and tests: `Face(yaw, YawSpeedDegPerS = 0)` and `FElysiumNpcMoveRequest::YawSpeedDegPerS` carry `m_YawSpeed +0x38`; `SetHullSize(minsCm, maxsCm)` carries `UTIL_SetSize`; `SetFacingTarget(TOptional<FVector>)` carries `m_facingQueue +0x54` (a motor call, not a move-request field, because `NPCThink` refills the queue while a route runs); `FElysiumNpcRouteQuery::StartCm` carries `0x102ee380(start, end)`; `RegisterMoveIgnores` asks slot 69 of every entity before each kernel move and clears the set when the move ends.

**5. Prop filter arms (done).** `EElysiumRetailTraceFilter { Simple, FVisible }` and `NpcTransparentMaskBit`; props read `blocks_traces` and `npc_transparent` at spawn. **Behaviour change:** catalogue props now carry the prop mask bits; they block only traces that include MONSTER unless `blocks_traces` is set (→ reviewer and the live stealth check).

**6. Owner edits (done)** except the regeneration items. **7–8. Sweeps (done):** ~245 stale cites cleared across 77 addresses; one orphan declaration and eight dead members removed; no stale includes.

**Regeneration needed (orchestrator, done at the gate):** slot 516 declaration re-emitted on `FElysiumNpc`, the generated base slot-516 stub dropped, slot 579 then `Slot579` deleted, slot 106 as a default (the empty `PostConstructor` body deleted by the orchestrator), Bach's `Precache` row in the override census.

**Cells:** `DebugSlowIdleYawSpeed` (20), `DebugSlowWalkYawSpeed` (25) — function-local static ConVars, the checker extended for their shape; Andrei's 10.0. **Re-verdict:** `0x1027bf50` OnRestore live through `0x102998c0` → `rule` (done). **Targets flag:** `0x1027bb20` → `UEComponent` (done).

**Unrecovered:** the retail `UpdateYaw` scaling; which point the facing queue's entity entry faces; the MingXiao loader immediates. `targets-R3.tsv`: 23 rows; `cells-R3.tsv`: 3 cells.
