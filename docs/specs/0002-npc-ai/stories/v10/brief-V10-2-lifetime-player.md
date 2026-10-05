# Brief V10-2 — cleanup and the reserved type-4 player record

Read AGENTS.md, README and packets-V10 P1/P3. V10 lifetime lands before V12's
producer correction. Coordinator names worktree; prerequisites V4d/V5b are landed
before this wave. No missing-footfall producer is to be invented.

## Only these nine files

- `Source/ElysiumUE/Private/Substrate/ElysiumGameSound.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumGameSound.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumPlayerEntity.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumFootsteps.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumFootsteps.h`
- `Source/ElysiumUE/Public/ElysiumPlayer.h`
- `Source/ElysiumUE/Private/Tests/ElysiumGameSoundTests.cpp`
- `Source/ElysiumUE/Private/Tests/ElysiumPlayerFootstepTests.cpp`
- `docs/vtmb/footsteps.md`

B∩A=B∩C=∅. No V4d/V5b coder-manifest overlap. EntityWorld wrapper/activation/
tick lines are integrator-owned; header ownership does not authorize World edits.

## Numbered jobs

1. **Observation-only**, `ElysiumGameSound.h/.cpp::FElysiumGameSoundBus::Emit/
   Refresh/Evict/Retire`, **0x101bac90 / 0x101ba890 / 0x1016b480**: supply an
   optional non-shipping callback describing actual insert, refresh, removal,
   identity/revision, Time, ExpireTime, duration, cleanup time/reason. Observer
   changes no semantics; callback cannot modify bus, RNG or world clocks. Lane3
   installs/restores it with runner lifetime and captures listener stamp at
   insert. Keep this separately applicable before correction items2/3. Trace
   legacy insertion-age, expiry and overflow removals distinctly in build1.

2. **Finite cleanup**, `ElysiumGameSound.h/.cpp::Emit/Evict/Reset` and proposed
   `Activate/PruneDue`, **0x101ba6f0 / 0x101ba890 / 0x101bac90**: replace finite
   expiry/insertion-age deletion with a due cleanup pass. Initial activation
   nextCleanup=now+1.0, each due pass rearms currentTime+0.3; walk the entire list,
   remove finite expiry+4.0 <= now, exempt exact -1. Keep expiry=insert+duration
   (grace is cleanup's, never smuggled into ExpireTime). A threshold crossed
   between cleanup passes retains the record. Remove age-four cutoff and prefix
   assumption; long record before short must not block cleanup. Emission does
   not force an early cleanup. Make Reset clear cleanup state and observers
   safely; no process-global clocks. World invokes actual due pass once per tick
   at its sound-entity position, integrator owed item below. Current128-capacity
   overflow policy is inherited R1 scope, **not** replaced or sold as retail here;
   never let pressure cleanup retire the permanently reserved player slot.
   Stay below capacity in all wave fixtures. Keep existing non-player producer
   duration defaults named; do not assign new authored durations to R1 producers.

3. **Reserved record**, `ElysiumGameSound.h/.cpp::Refresh/Retire` and event
   declaration, **0x101baf80 / 0x1016b480 / 0x101bab50**: distinguish stable
   allocation identity from changing revision serial. Refresh rewrites owner,
   origin, type4, integer volume, occlusion and time in place, expiry=-1, stable
   allocation order. Reserve once per player/world session (allocation timing
   equivalent to startup), including silent volume0; missing-slot path must be
   diagnosable. Retire is explicit teardown, never normal silent refresh.
   Do not use RadiusCm<=0 to re-resolve a quiet record to normal table radius.
   Use an explicit resolved-radius field/mode so zero really means zero.
   Keep `EventsSince` behavior for other event consumers: refreshed slots notify
   via revision, and their view must remain ordered by revision rather than
   accidentally depending on allocation-array order. Prefer a separate small
   revision view/cache or separate persistent slot, with active view used by
   lane1; no alias-invalidated mutable view from a const call. Changing the
   reserved identity to the newest serial is not acceptable. Other consumers
   keep finite-event notification semantics. Give lane1/lane3 exact API names.

4. **Frame placement/input**, `ElysiumPlayerEntity.cpp::Think/UpdatePlayerSound/
   PostThinkAnimation`, `ElysiumPlayer.h` declarations, **0x1016be10 /
   0x1016b480 / 0x1016b4c9**: remove the ordinary pre-move Think call; call the
   producer after the existing per-frame PostThink animation/UpdateCharacter
   tail and its alive/observer/etc admission. A headless body with no skeletal
   clip must still reach ordinary sound sampling: don't nest sound production
   in the mesh-only animation block. Use the frame delta supplied by the World
   post-move call (integrator owed declaration/call) rather than time since the
   last 0.1 s player think. First-frame delta is the world frame delta, not zero.
   Read **World->GetPlayerButtons() & EElysiumButton::Jump** (real retained raw
   input published by PlayerController), not JumpHoldRemaining. Keep the separate
   law/stealth/feed heartbeat untouched. No duplicate producer from TickStepClock
   or HandleAnimEvent. Preserve named missing game-over/locked/observer sources.

5. **Integer producer arms**, `ElysiumPlayerEntity.cpp::UpdatePlayerSound`,
   `ElysiumFootsteps.cpp/.h::HearingCategory/DecayHearingRadiusUnits`,
   `ElysiumPlayer.h::PlayerSound...`, **0x1016b4b8..0x1016b655**: preserve jump
   ->ground->landing->3-D movement->duck->strict run band priority. Raw table
   volume is integer; rise immediately, decrease `int(old - dt*250)` toward zero,
   clamp at integer target. Keep authored180/240 and threshold128 lookup; do not
   replace it with a constant-only table. FL_NOTARGET zeroes existing volume
   and returns with old owner/type/time intact; m_fNoPlayerSound zeroes after
   decay but still does ordinary writes/restamp. Separate those inputs instead
   of the current merged bNoPlayerSound shorthand. Use real Entity flags if
   FL_NOTARGET is exposed; otherwise a named field/accessor for retail flag
   0x8000 answering false is an exact declared seam, not “implemented notarget”.
   No body/sample source: quiet reserved record with a named locomotion seam,
   not free/recreate. Pending landing categories retain their named one-pass
   stand-in; their true player SetAnimation publisher is outside this wave.

6. `ElysiumGameSoundTests.cpp::bus automation` and
   `ElysiumPlayerFootstepTests.cpp::PlayerHearing/PlayerSwallows`,
   **0x101ba890 / 0x101baf80 / 0x1016b480 / 0x10178a10**: lifecycle before/
   equality/after, long-before-short whole scan, initial1/rearm0.3, no emission
   needed, sentinel/stable allocation/revision notifications, silent slot0
   volume, reset isolation. Drive real player producer with local recording
   services and actual post-move call: once per frame after animation, jump bit
   wins regardless of push window, integer dt1/60 decay240->235->230, target
   clamp and immediate rise, separate no-target/no-sound writes, air/stationary,
   landing and strict128/129/3-D bands. Existing event swallowing stays silent.
   Report assertion migrations individually. Do not edit shared TestServices.h.
   README lists the arena counterparts; isolated arm calls alone don't prove
   the real tutorial hearing wire.

7. `docs/vtmb/footsteps.md::2.5/2.8`, **0x1016b480 / 0x1016be10 /
   0x101baf80 / 0x10274e30 / 0x10178a10**: correct persistent duration/cadence/
   integer semantics and distinguish two silence gates. Preserve retail
   no-AI-footfall fact. Name landing/gate seams, raw Jump's now-real source,
   and inherited allocator scope without claiming a new modernization.

## Integrator owed lines and data contract

- `ElysiumEntityWorld.cpp::Activate` (relocate actual activation method),
  **0x101ba6f0**: arm bus cleanup and player reservation once on activation, not
  every Emit or fixture record refresh. `::Tick`, **0x101ba890**, invokes due
  sound cleanup as a think with its actual ordering measured/documented; no
  clock catch-up loop. Packet's final note does not verify coincident thinker
  priority; follow integrator's recovery gate rather than force pre-Listen.
  Preserve player post-move ordering.
- `ElysiumEntityWorld.cpp::RefreshGameSound` and public declaration,
  **0x1016b480**: silent type4 refresh is zero-volume rewrite, no Retire; remove
  finite0.2 argument and preserve explicit zero radius; route distinct no-target
  volume-only arm. Keep wrapper signatures usable by all current callers;
  report exact call/declaration changes, no speculative blanket replacement.
- `ElysiumEntityWorld.cpp::Tick` and player declaration/call,
  **0x1016be10**: supply actual frame delta at existing PostThinkAnimation call
  or a narrow post-think wrapper agreed by name. World input already carries
  Jump; no PlayerController rewrite is owed. Player sound origin is the sampled
  post-move Origin, before NPC RunThinks.
- Any stale `FootstepSenseTests.cpp` / `FootstepSeamTests.cpp` assertion needing
  exact replacement is reported, not edited. PlayerSound fields/save behavior
  beyond this transient producer belong to V6. No content import or re-bake.

## Coder rules

Write only the nine listed files in the worktree the coordinator names, **by
absolute path**. Read files and run read-only tools from
`E:\dev\elysium-unreal`. Never build, run editor/game/tests/arena, bake or commit;
never push. No git clean/reset/stash/checkout/worktree/delete. Address at every
changed runtime line and every numbered job; report a new divergence, do not adopt
it. Never hand-edit generated `*Slots.cpp`; hand bodies belong in matching
`*SlotBodies.cpp`, integrator owns `kernel_verdicts.tsv` and regeneration.
Shadowed locals/members/globals are compile errors **C4458 / C4459**. Report
**under 350 words**: changed paths/functions/addresses, observation vs correction
hunks, exact API agreement and assertions changed, plus exact lines owed by
files outside the lane (or explicitly none). No file named `report*.md`.
