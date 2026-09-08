# 0005 park-stealth — cross the tutorial park past the patrolling enemy unseen and reach Jack inside the building

## Witness
The `sp_tutorial_1` park beat: the patrol NPC runs its authored patrol schedule unattended; the
player's stealth target surface (light level / `Sneaking`) and `trigger_stealth_mod` overlap
volumes publish the balanced scalars 13.5's senses consume; the NPC's sense pass (sight, sound,
comfort) escalates awareness exactly as retail's `GatherConditions` sweeps and `ShouldInvestigate`
do; and the player reaches Jack inside the building either unseen or having survived detection.
Proven headless today by `uv run elysium test Elysium.Substrate.<...>` (schedule/condition/flag
suites, see Tasks); the played beat itself is open and is proven by a Play-tier beat script over
`sp_tutorial_1` (11.10). This spec is also the awareness/senses seam 0006 (first kill) and 0008
(sound distraction) consume.

## Scope
- Roadmap rows absorbed:
  - **13.1 Stealth** — target surface, modifier volumes, and the senses consumer.
  - **13.5 Combat AI**, senses/conditions/awareness half only — the sound-event bus, senses and
    stimulus memory, the conditions layer, and the ideal-state pass that reads them.
- Out of scope (belongs to another spec or is parked):
  - 13.5's enemy transaction, alert/combat schedule families, loadout and `aiscripted_schedule`,
    and the flinch/reaction action family (LIFE5) — **spec 0006** (enemy attack transaction).
  - The footstep hearing producer (AUD2's; this spec is only its consumer).
  - `COND_HIT_BY_DOOR` / the door-obstruction retreat and the `Prone` state — parked in 10.7.
  - Followers (`m_hFollowerBoss`), squads, the `TaskFail` failure virtual, and the Troika
    reduced-think cadence — parked in docs/specs/README.md, mapped by another spec.
  - The feed/mesmerize trance itself (`SCHED_TROIKA_MESMERIZED`, `TASK_MAKE_OBLIVIOUS`'s
    `SetEnemy(NULL)`/squad-disconnect arm) — mapped by another spec; only its **effect on
    sensing** (oblivious NPCs skip the whole sense pass and are backstab-able from any angle) is a
    requirement here because 0005's stealth-kill transaction consumes it.
  - 13.2 Disciplines, 13.3 Firearms & melee, 13.4 Terminals — separate specs.

## Requirements
1. **Stealth target surface.** Light-level and `Sneaking` state publish a target-surface scalar;
   `trigger_stealth_mod` volumes contribute a balanced overlap modifier. Landed headless.
   → `docs/vtmb/stealth.md`.
2. **Stealth-kill transaction — open.** `vdata/system/StealthKillRules.txt`, the deaf arc /
   approach depth test, and grapple mode 3 on a qualified target (recovered under RE50,
   unimplemented). → `docs/vtmb/stealth.md`.
3. **8.9's committed observer snapshot — open.** Named dependency of 13.1; content not given
   beyond the name in the plan.
4. **The three `GatherConditions` sweeps** (retail addresses in parens), each a `ShouldInvestigate`
   caller:
   - See-unknown `0x102b15c0`: `m_hBestSeeUnknown` (+0x6088, slot 586, player-only), 1.5 s grace
     (+0x6084), `m_vecLastSeeUnknownPos` (+0x6090); one-shot `MADE_INITIAL_RESPONSE` roll over
     `m_iSeeUnknownRepeatSightings` (+0x60a4) and `full_investigate` (+0x6340) setting
     `ATTACK_UNKNOWN`/`IGNORE_UNKNOWN`; 2-D closing speed vs `20.0f` →
     `UNKNOWN_ADVANCING/HOLDING/RETREATING` (0x05–0x07 — reproduce the dead `HOLDING`);
     `INVESTIGATE_SIGHT` 0x26; producer slot 472 `0x102b3e00` sets `SEE_UNKNOWN` and fires
     `OnUnknownVisionPlayer`.
   - Sound `0x102b1cd0`: six records — `m_LastSoundWorld` (+0x61e4), `PhysicsDanger` (+0x6134),
     `Danger` (+0x6108), `Player` (+0x61b8), `BulletImpact` (+0x618c), `Combat` (+0x6160); gate
     `m_flNextInvestigateSoundTime` (+0x623c); arm = `HasCondition(HEAR_X) && (schedule interrupts
     on HEAR_X || ShouldInvestigate)` → `INVESTIGATE_SOUND` 0x25; `HEAR_DANGER` skips the
     predicate; last-wins priority combat > bullet > player > danger > physics > world;
     `HEAR_FLANK_SOUND` 0x33; `SEE_SOUND_SOURCE` 0x2d tail rate-limited by +0x6418.
   - Comfort `0x102b1a20`: idle-only, 0.2–0.4 s, global `AddToComfortList`
     (`0x10323630`)/`Remove` (`0x10323770`), nearest ≤1024 u, ≤3 per target via +0xE94,
     `COMFORT` 0x27.
   - Blocked on decoding the 32-program `INVESTIGAT` schedule family (byte-scan +
     `FUN_102b9810`) so the conditions have something to select into.
   → `docs/vtmb/npc-ai-reverse-engineering.md` ("The three `GatherConditions` sweeps").
5. **`ShouldInvestigate(candidate, bCombat)`** `0x102b3270`: reject on
   `DONT_INVESTIGATE|IN_FLEE_SCHED` (0x4000080), `stay_entrenched` (+0x6435), null, my own
   `m_hFollowerBoss`; the committed enemy → true; else `investigate_mode` (+0x6338) or, under
   `bCombat`, `investigate_mode_combat` (+0x633c): 0 never … 6 anything, shipped default 4.
   Landed headless.
6. **The condition enum** relevant to sensing/awareness: `SeeFear 0x44, SeeEnemy 0x46,
   HearDanger 0x6a, HearCombat 0x6d, HearWorld 0x6e, HearPlayer 0x6f, InvestigateLevel 0x1e,
   Comfort 0x27, SquadSeeEnemy 0x31, HearFlinch 0x72, NpcFreeze 0x75`. Landed.
7. **The NPC flag word**, insofar as it gates sensing/awareness: `m_bfAINPCFlags` (+0x14b8) /
   `m_bfAINPCFlags2` (+0x14bc), 62 names parsed from `NPCFlag:<name>` (`0x1030cbd0`).
   `DONT_INVESTIGATE` (bit 26) gates the interest predicate only. Nothing in schedule data clears
   these bits; `OnScheduleChange` (slot 435, `0x102a0940`) is the sole clearer, gated on
   `PRESERVE_PATH`. Landed headless.
8. **Obliviousness gates the whole sense pass.** `PerformSensing` (`0x1026e4f0`) skips sensing
   entirely while `m_iIsOblivious` (+0x5bb4) is nonzero; `CStealthKillRules::FindVictim`
   (`0x101be1f0`) allows backstab from any angle on an oblivious target. Sense-pass gate landed
   headless (`IsOblivious()`); the stealth-kill consumer is requirement 2 (open).
9. **Patrol.** The NPC follows `FollowPatrolPath` over `info_node_patrol_point` routes; patrol is
   one of `ThinkAutonomous`'s executors (patrol / ambient / stance) and one of
   `SelectIdleSchedule`'s case-1 steps (busy/choreo → follower → **patrol/ambient** → alert
   lookaround → door obstruction → return-to-initial → disposition idle). Landed headless; the
   played beat (this NPC's authored route on `sp_tutorial_1`) is open.

## Design
`FElysiumNpc::Think()` runs `RunConditionPass` (senses → `ElysiumNpcEnemy::GatherConditions` →
`UpdateIdealState`) ahead of `ThinkAutonomous`, which ticks the patrol/ambient/stance executors
when no schedule/scripted order/combat program pre-empts them (`ThinkSchedulePolicy`). Senses live
in `Cognition.Conditions` (session-only, gathered per pass) and `Senses.Memory` (`Enemy`,
`bPlayerLos`, `ClosestPlayer`). The flag word is `NpcFlags` (`FElysiumNpcFlags`), read through
`IsOblivious()` / `IsBusyWithDiscipline()` / `HasDialogSuppressFlag()`. Condition identifiers live
in `ElysiumNpcConditions.h`; `EElysiumInvestigateMode` and the keyfield readers live in
`ElysiumNpcConditions.{h,cpp}`. The stealth target surface and `trigger_stealth_mod` are a
separate producer that 13.5's senses consume as a published scalar (join point, not yet named in
the plan beyond "the senses consumer").

## Seams
- Consumes:
  - 8.9's committed observer snapshot (spec unknown).
  - 11.10 (Play-tier beat script infrastructure the witness runs on; spec unknown).
  - Decode of the 32-program `INVESTIGAT` schedule family (unrecovered; blocks requirement 4's
    last bullet).
  - The follower/squad build (parked in docs/specs/README.md, another spec) for the interest
    predicate's "my `m_hFollowerBoss`" reject and `IRelationType`'s composed relation, which the
    sense sweeps and `ShouldInvestigate` read but do not build.
- Provides:
  - The awareness/senses seam — `Cognition.Conditions`, `Senses.Memory`, `IsOblivious()`,
    `ShouldInvestigate` — consumed by **0006** (first kill: enemy transaction reads committed
    enemy / oblivious state) and **0008** (sound distraction: `HearWorld`/`HearPlayer`/sound-sweep
    conditions).
  - The stealth target surface and modifier-volume scalars for any later spec that needs
    detection state.

## Tasks
- [~] **13.1 Stealth**
  - [x] Light/Sneaking target surface; `trigger_stealth_mod` balanced overlap contributions;
    13.5's senses consumption of the published scalars.
  - [ ] Stealth-kill transaction: `vdata/system/StealthKillRules.txt`, deaf arc / approach depth,
    grapple mode 3 on qualified targets (RE50, unimplemented).
  - [ ] 8.9's committed observer snapshot.
  - [ ] The tutorial's ordinary-stealth and stealth-kill lessons complete as retail, from real
    input (acceptance).
- [~] **13.5 Combat AI**, senses/conditions/awareness half
  - [x] Sound-event bus, senses and stimulus memory, the conditions layer, the ideal-state pass —
    landed headless.
  - [x] Flag words, refcount, 62-name table, `OnScheduleChange` masks, `ParseName`, save
    (`Substrate/ElysiumNpcFlags.{h,cpp}`).
  - [x] Sense pass gated on `IsOblivious()`; `GatherSight` gated the same way, not on
    `DONT_INVESTIGATE` (`RunConditionPass`; `ElysiumNpcConditions.cpp`).
  - [x] `ShouldInvestigate` + `EElysiumInvestigateMode`; `InvestigateMode(Combat)` keyfields read
    (`ElysiumNpcConditions.{h,cpp}`).
  - [x] Condition enum entries `SeeFear, SeeEnemy, HearDanger, HearCombat, HearWorld, HearPlayer,
    InvestigateLevel, Comfort, SquadSeeEnemy, HearFlinch, NpcFreeze` (`ElysiumNpcConditions.h`).
  - [x] `IsBusyWithDiscipline` off the bit; `HasDialogSuppressFlag` = `NO_DIALOG ||
    NO_DIALOG_PERSISTENT` (`ElysiumNpc.h`).
  - [ ] The three `GatherConditions` sweeps (see-unknown, sound, comfort) — not yet built; blocked
    on decoding the 32-program `INVESTIGAT` family.
  - [ ] The played beats (Play-tier) for the tutorial's hostile beats — this spec's share is the
    patrol/detection escalation half, not the enemy transaction.
  - [ ] Patrol played beat on the real `sp_tutorial_1` NPC route (patrol executor and
    `SelectIdleSchedule`'s patrol/ambient step are landed headless; the played beat is open).

## Open questions
- What 8.9's "committed observer snapshot" concretely is — the plan names it as a 13.1 dependency
  without detail.
- Whether the follower/squad build (another spec) lands before or after this spec's played-beat
  acceptance; `ShouldInvestigate`'s boss reject and `IRelationType`'s composed relation are read
  here but built there.
- Unspecified: which spec number owns 11.10 and the Play-tier beat-script infrastructure this
  spec's witness runs on.
