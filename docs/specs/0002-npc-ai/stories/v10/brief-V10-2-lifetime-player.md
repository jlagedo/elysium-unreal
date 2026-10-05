# Brief V10-2 — cleanup and the reserved type-4 player record

**Start fence:** V10 runs after committed V5b, V6 and V7. Its lanes share
`ElysiumNpcSenses.*`, `ElysiumEntityWorld.cpp`, `ElysiumPlayer.h` and arena
scenario files with V6/V7. Relocate functions by name on the committed code of
all preceding waves; never replace a landed file with the planner snapshot.
Fresh stages use V6's **1.0 before Load/entity initialization**; revisits use the
map's frozen clock and explicit load its saved clock. Preserve seed/reset order,
shared draws, state-ban rules and original behavioral bounds. Re-measure old
zero-tuned records alone and after another; change only proved epoch/staging
assumptions with per-record evidence, never replay RNG draws or widen windows.


Read AGENTS.md, README and packets-V10 P1/P3. V10 lifetime lands before V12's
producer correction. Coordinator names worktree; prerequisite waves through V7 are landed
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

B∩A=B∩C=∅: A4+B9+C32=45 unique paths. Earlier-wave overlap is serialized. EntityWorld wrapper/activation/
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
   safely; no process-global clocks. World dispatches it only as the soundent's
   due callback in RunThinks at its mapped entity-list position (job10 and
   integrator job7), never an independently privileged pre-Listen timer.
   Job8 replaces 128/oldest eviction with the pulled-forward 64-slot allocator.
   Pressure must preserve reserved rows. Keep existing non-player producer
   duration defaults named; do not assign new authored durations to R1 producers.

3. **Reserved record**, `ElysiumGameSound.h/.cpp::Refresh/Retire` and event
   declaration, **0x101baf80 / 0x1016b480 / 0x101bab50**: distinguish stable
   allocation identity from changing revision serial. Refresh rewrites owner,
   origin, type4, integer volume, occlusion and time in place, expiry=-1, stable
   allocation order. Initialize/reserve the configured client count at soundent
   startup, consuming the same 64-slot pool (P5); bind the single-player client
   index including silent volume0. Rebind saved reservations without reallocating.
   Missing-slot path must be
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
   and the now-owned P5 allocator/refusal/free-list contract without claiming
   other R1 producers. Report allocator oracle lines to lane1, which owns senses.md.

8. **V10.2 pull-forward allocator**, `ElysiumGameSound.h/.cpp::Reset/Emit/
   Refresh/Evict/Retire` (relocate actual allocator/free functions),
   **0x101b9880 / 0x101baf80 / 0x101bab50 / 0x101ba9d0 / 0x101bac90**:
   implement exactly 64 total embedded slots including client reservations.
   Initialize free chain 0->1->...->63->-1, active=-1; reserve clients via the
   same allocator with expiry=-1. Allocate free head and prepend to active;
   exhaustion returns no stimulus and changes no surviving row/order/identity.
   Unlink the actual removed index through its predecessor (or active head), then
   push it onto free head. No oldest eviction/replacement and no reserved bypass
   pool. Separate slot/allocation identity from observation revisions; reuse must
   not alias an old allocation in traces. Refresh in place does not relocate it.
   Add real bus arm tests. **Proof:** lane3 `sound_pool_pressure` (single-client:
   1 reserved+63 finite, 65th request refused), `sound_pool_reuse` (head/interior/
   tail removals and LIFO reuse), `sound_pool_reserved_survival` (pressure, silent
   restamp and finite cleanup, same sentinel identity). Other R1 producers remain
   named `R1SoundProducer/VSound` seams under 0002/R1, not new production work.

9. **V10.3 save handoff API**, `ElysiumGameSound.h/.cpp` pool capture/apply/rebind
   helpers and `ElysiumPlayerEntity.cpp::Hydrate/Dehydrate/UpdatePlayerSound`
   with `ElysiumPlayer.h` declarations/state, **0x101baf80 / 0x1016b480** plus
   P6 raw datamaps: expose complete pool/list state, saved player target volume
   INT (+0x2130), current integer volume in CSound, client binding and callback
   state to the common applier. Owner EHANDLE fixup must use V6's shared identity
   map; times go through its audited TIME policy, preserving -1 expiry semantics.
   Do not synthesize fresh reservations during apply. Diagnostic observers and
   revision views are host instrumentation, not retail SAVE fields. The integrator
   owns codec/snapshot plumbing outside this manifest. **Proof:** `sound_save_finite`
   and `sound_save_reserved` show pool heads/links, owner rebind, stable allocation,
   volumes, expiry policy and first real Listen at the apply fence; add local
   capture/apply tests without replacing production save/load evidence.

10. **V10.4 callback interface**, `ElysiumGameSound.h/.cpp::Activate/PruneDue`
    (or named equivalents), **0x101ba6f0 / 0x101ba890 / 0x1003bdd0 /
    0x100f7060 / 0x100f9fc0 / 0x1023c020 / 0x101a2e40**: provide the due
    cleanup callback for integrator's stable soundent identity and native-list
    placement. Initial now+1; recurrence now+0.3; restored due state uses the
    audited callback contract, not an activation restart. **Proof:** lane3
    `sound_lifetime_prune` pins both explicit callback orders; real-world
    `sound_cleanup_coincident` traces dispatch indices, cleanup/Listen order and
    finite expiry+4 equality. P7's fresh/restore-position mapping gates a
    boundary parity claim; do not hardcode first/last priority.

## Integrator owed lines and data contract

- `ElysiumEntityWorld.cpp::Activate` (relocate actual activation method),
  **0x101ba6f0 / 0x1023c020**: initialize once at the recovered soundent
  creation/spawn fence; activation must not restart restored state. `::RunThinks`,
  **0x101ba890 / 0x1003bdd0 / 0x100f7060**, dispatches cleanup in native-list
  sequence. P7 fresh/restore mapping gates parity; no clock catch-up loop.
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
  exact replacement is reported, not edited. Integrator extends landed V6
  codec/common applier for new pool/list/player-volume state (job9).
  Full landing publication: `PlayerLayer0LandingState` -> 0015 player layer0.
  Missing game-over/locked/observer producers: named
  `PlayerPostThinkGameOver/Locked/Observer` accessors -> player story.
  Missing locomotion: `PlayerSoundLocomotion` -> player embodiment.
  Keep `sound_player_landing_live`, `sound_player_locked_live`,
  `sound_player_observer_live` and `sound_player_game_over_live` absent;
  fixtures prove consumer arms only. Integrator lists seams/owners at close.
  No speculative content import or re-bake.

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
