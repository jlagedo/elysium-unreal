# Brief — V4c integrator: C1, C2, C3 (one commit; never push)

Final, 2026-10-04. Run only after **C1, C2 and C3** report. Read AGENTS.md first, HANDOVER.md,
the three final briefs, README's shared names/rules, Arena/README.md and S5 item 3, S10, S11,
S12, **S13 / Changes to the plan**. Read git log and messages 64895278/88649932: event shots
are landed/green; shot estimate deletion and dead-enemy reselection remain V4c; chase_melee's
swing row does not move the body yet. Owner rulings are settled: one animation-pick stream
**NpcSchedule**, registry built now, real reload in V5. No question to re-ask.

No file named `report*.md` is created. Use worker responses/scratch outside repo for reports;
landed recovery belongs in the named docs and verdict table goes in the single commit message.

## Files and ownership

C1/C2/C3 have the exact file manifests in their briefs; braces expand before comparing them.
**All three pairwise intersections are empty.** C1 owns weapon/contact and combat doc; C2 owns
main combat-character/damage/spawn/player/animation/memory and lifecycle/senses/feed docs;
C3 owns Public/ElysiumPlayer.h's team declarations, new registry/team bodies/tests and teams.md.
No file has two coder writers. Patch another lane's file only after reports, serially.

Your additional integration paths (Source/ElysiumUE prefix unless stated):

- `Public/ElysiumEntityWorld.h`, `Private/Substrate/ElysiumEntityWorld.cpp`,
  `Private/Substrate/ElysiumEntityWorldPersistence.cpp` — team ownership accessor, level hooks,
  restored registration, removed world sweep declaration.
- `Public/ElysiumSessionSubsystem.h`, `Private/Session/ElysiumSessionSubsystem.cpp` — game-system
  registry ownership/access/lifecycle.
- `Private/Map/ElysiumMapActor.cpp`, `Private/Visual/ElysiumMeleeTrail.h` — remove world sweep
  call, correct trail comment; named read-only contact observations if needed.
- `Public/ElysiumWorldServices.h`, `Public/ElysiumMapActor.h`,
  `Private/Map/ElysiumMapActorEmbodiment.cpp`, `Private/Tests/ElysiumTestServices.h` — movement
  interval accessor and exact reported pose/query/test-double lines.
- `Private/Substrate/ElysiumNpcBaseHelpers2.cpp`,
  `Private/Substrate/ElysiumAnimatingOverlaySlotBodies.cpp` — common picker forwarding if owed.
- `Private/Visual/ElysiumEntityBodies.h`,
  `Private/Visual/ElysiumEntityBodiesProps.cpp` — C2's dedicated live prop-pick adapter; existing
  capability/construction previews stay draw-free. Interface/map paths are already listed above.
- `Private/Substrate/ElysiumNpcBaseSelect.cpp` — state-7 bone−1 line only if owed.
- `research/tooling/gen_kernel_bindings.py` — C3's owning generation-source team field row.
- `research/tooling/ghidra/driver/kernel_verdicts.tsv`, kernel shape/member-map sources only
  for exact reported declarations/verdicts; generated outputs only via their generators.
- `Arena/scenarios/combat/` — named records below; probe/runner files only for exact missing
  read-only observations, discovered by name and reported before implementation.
- `docs/specs/0002-npc-ai/spec.md`, `stories/v1/divergences.md`, `stories/v1/triage.md`,
  `docs/specs/TRACKER.md` — V4c close only; V4 itself waits for V4d. Coder recovery docs are
  corrected by their owners/your serial integration, without rewriting settled packets.

Read nested AGENTS.md before touching research/pipeline-owned tooling. A necessary extra-file
line is declared by file/function/address before applying it; no broad follow-up refactor.

## The job

1. **Integrate owed lines first, before a build.** Read every lane report and verify each exact
   patch against its owner. C1: remove MapActor's AdvanceMeleeSwings call/declaration/comment;
   slot315 body plus C2 PostThink tail must both land (player sweep after slot258,
   `0x1016c316`). Add interval movement API beside GetBodySequenceMovement in the interface,
   map declaration/implementation/test double over SampleDelta(from,to); default zero/false.
   Check capsule propagation at SetRuntimeOrigin and apply reported Teleport only if required.
   Resolve D9's pose-at-cycle accessor or retain named input seam and file judge work explicitly.
   C2: forwarding of weighted/heaviest callers, shared helper on NpcSchedule, C1-owned
   BuildActivityClipRequest patch (check C1 already applied it), any corpse virtual/slot301
   dispatch, death-fork bone−1/declarations and selected-memory owner/squad-hook lines.
   SelectHeaviestSequence currently calls the weighted selector: replace that forwarding with
   the strict heaviest rule, no draw. Implement C2's PickAnimatedPropRestClip interface/map/
   entity-bodies adapter over raw baked Clips/RestCandidates via the common picker; actual
   StandRestPose/PlayRandomAnimation calls use it, capability/model construction do not draw.
   AnimatedPropRestClip→SelectRest's hash/floored weights remain only a draw-free preview,
   never the actual retail spawn/random-animate pick (`0x1018df70` / `0x10190850`).
   C3: one game-system registry, World access, pre-clear before Load creates/spawns entities
   (`0x10230820`), post-clear after Teardown removes entities (`0x10230860`). Restore joins
   names after ApplyEntityRecord fields before post-restore consumers (`0x10348890`); player
   Spawn/Hydrate joins literal player (`0x1016d260/0x1016ebd0`). Do not clear mid-level between
   restored entities or persist numeric symbols. Verify C1 contact and C2 damage use the same
   accessor; no no-team constants survive as live predicates. Maker flags already match S13:
   **no maker patch** or re-bake is owed. Any code divergence newly found is reported/placed,
   not silently adopted.
2. **Generation before compile.** Retarget kernel_verdicts.tsv rows `10346cd0` and `10090c80`
   to `hand:FElysiumCombatCharacter::MeleeSwingUpdate` and
   `hand:FElysiumAnimating::SetAttackExtentsForSequence` (shape of existing hand row 103338c0).
   Add/retarget any reported team/pedestrian verdict rows, including verified team addresses
   and level hooks, based on lookup rather than guessed slot fills. Hand bodies stay in matching
   **SlotBodies.cpp**, generated Slots.cpp are never hand-edited; leaving both gives double
   definitions. In gen_kernel_bindings.py CHAIN_MEMBER_MAPS add CBaseCombatCharacter 0x10ac →
   FElysiumCombatCharacter::TeamName, remove CHAIN_UNBOUND's stale reason, preserve replay flags.
   Run `uv run elysium research gen_kernel_bindings` and `uv run elysium research gen_kernel_shape`
   once after inputs are final, then `uv run elysium research kernel --check`. Time these query
   paths with 60 s hard timeouts; >10 s is logged; stop/optimize at timeout. No asset import.
3. **Read every diff before build 1.** Inspect all implementation **and test** diffs for shadowed
   locals (C4458/C4459 are errors), missing includes, wrong declarations/signatures, incomplete
   types, double definitions and generated-stub/hand-body collisions. Include Public/ElysiumPlayer.h
   and the new picker/registry bodies. HANDOVER's required uncommitted Codex review uses the
   codex-cli skill/method; read it before that review and request these same checks. Apply all
   review fixes before the first build. Verify the three file manifests' intersections and
   scope, and that no pipeline/body-data/bbox hunk or report*.md is present.
4. **One `uv run elysium build --arm`; maximum two builds total.** Build with arm tests included
   so the later arm run does not trigger a hidden compilation. A second `build --arm` is only
   for integration breaks, after another complete diff review. A third build means stop and
   report placement. Wait on completion, no polling. Do not run the full arena until the last
   build is complete; final acceptance below uses that binary.
5. **Write/correct the records below before their named run.** Each about/notes cites addresses
   and distinguishes packet acceptance predictions from measured outcomes. Follow current
   Arena format; use relative never windows (`after`, `within`) where specified. Remove
   known_red only when the expected outcome is proved; never loosen retail expectations to
   obtain green. Records and read-only probes are your work, never a coder's Arena edit.

   | record | staging | expect / never and citations |
   |---|---|---|
   | **chase_melee** (correct existing) | existing brawler/player chase; retain retail melee schedules | kernel sequence names swing at nonzero rate; end distance <200 cm and player damage. No direct Base-channel NPC swing/empty root movement. `0x103e9e00`→slot331 `0x10347180`→slot311 `0x10272400`; AutoMovement `0x10280a50`→`0x10094b70`→`0x102e0bd0`; V11 commit 88649932's measured red |
   | **melee_enemy_blocked** (new) | melee_ally_in_the_way cast/seed/seat/keys; front `[-775,0,0]`, rear `[-525,0,0]`, player on centre line; duration 10 | rear START_COMBAT by3, then ENEMY_BLOCKED 0x3a within2, then local schedule 0xce within1. Never 0xe1/0xcb while front blocks line (start until1.5, pin from first trace to its move goal), death on either, or damage from front on rear. Slot331 line trace `0x1034727b`, PUSH `0x103472f2` / call `0x103472f6`, exit `0x10347d97`; failure ladder `0x102b6fe0`, human selector `0x10385e40` before `0x102a11d0`. Correct ally record's stale raise cite 0x103472e8 to 0x103472f2; otherwise keep its 50 cm off-line relation-control staging |
   | **corpse_fades** (correct existing) | tutorial stealth_victim_maker, Spawn, kill named Kindred child; mode0/alpha255, player watching | child flags0x204, death/corpse, removal by death+14.5; never removal within13 after dies. Burn allowed, +10 removal forbidden. Ordinary maker assigns `0x1034b7b0` with infinite-child Spawn `0x1034afe0`; post-corpse `0x10265d72`→StartFade `0x102695d0`→Fade `0x10269960`; S13 §1 |
   | **corpse_pedestrian_stays** (correct existing) | hub pedestrian from_map, bit9 clear, player turned away | corpse remains through death+25; never automatic removal. Snapshot/base/ThinkSet(NULL)/SOLID_NONE `0x103a38c0`; explicit-clear dispatch wins. S13 §1; no substitute unseen-removal think |
   | **melee_same_team** (new) | hated bystander, priority below player, no squad, FF off; attacker !Arena_Melee, bystander arena_melee; overlap swing off slot331 centre ray; different-team control otherwise identical | equal valid symbols and admitted swing; control takes positive damage. Never matching bystander damage, contact impact/knockback or Swing.RecordHits insertion. Read-only hit-list observation required; outer damage gate alone is insufficient. S13 §2; `0x1034394d..0x103439a7` / SameTeam `0x10323930`, impact `0x102579f0` |
   | **team_damage_gate** (new) | named gunman's real weapon packet, attacker handle intact, explicitly hated teammate; identical different-team control | teammate no damage, control positive damage; self-damage predicate admitted. Never attackerless scalar input offered as proof. S13 §2; OnTakeDamage `0x1032ef60` before discipline/life dispatch |
   | **ranged_enemy_dead** (new) | gunman hates exactly one NPC; player neutral, quiet, no hints/incidents/sounds, NoAlertState false; victim AI off, kill after acquisition; retain nonhidden visible/resolvable corpse | committed enemy→none, ALERT/0x4b TROIKA_ALERT_WAIT; never subsequent START_COMBAT or ranged hit on corpse. S13 §3; BestEnemy `0x10274475` / IsAlive `0x100b4dc0`, ChooseEnemy `0x10279dd0`, GatherEnemyConditions `0x10270e5a..0x10270e89`, Troika `0x102afc24..0x102afca7` / PreSelect `0x102ae920` |
   | **ranged_enemy_dead_retarget** (new) | same death staging, remember second live hostile (and a dead control if needed); player neutral | live replacement becomes enemy; allow one fresh START_COMBAT. Never choose a dead body. Same S13 §3 selection addresses; no eager memory purge. Include arm memory check where schedule vetoes LOST_ENEMY: entry retained indefinitely but unselectable (`0x102df320`, slot54 `0x102b50b0`) |
   | **damage_knockout_one_hit** (new) | high-health neutral victim; retail Faint, wait for TASK_KNOCKOUT flag write; zero packet, then positive18 | zero never kills; positive kills with wounds below cap. Faint `0x1029f250`, task `0x102a3339/0x102a3344`, kill `0x102beea4`; S13 §4 |
   | **damage_cower_one_hit** (new) | damage-responsive pedestrian, no usable flee nodes; wait for COWER_SIMPLE(_NOSEE)'s ONE_HIT_KILL task, then positive18 | death below cap; never require knockout as sole cause. FLEE `0x103a2e30/0x103a34a3`, selector `0x102b0250`, flag task `0x102a585d..0x102a5874`, cower blobs `0x105e5788/0x105e5590/0x105e5398`; S13 §4 |
   | **damage_high_health_control** (new) | effective cap100000, no interest-death place, AI off, flag clear; five separately recorded positive18 commits | 90 wounds, never death; audit current cap/flags/death caller if false. `0x1032ef60` sheet0x0f/0x11, `0x102beda0` / alternative `0x102bef13`; S13 §4. No retrospective claim about historical fifth hit |
   | **verbs_feed_victim_dispatch** (correct existing) | neutral surviving front-feed victim, retained release 0xf88/0xf8c | MESMERIZED install before OnGrappleEnd; first released MAKE_OBLIVIOUS, disposition fallback row before SET_ACTIVITY, eventual0x104e. Never idle while paired or death; **allow released first-pass idle**, no variant3 pin. Keep existing until5.6 boundary excluding released pass. S13 §5: `0x1033a9e0`, `0x10281eee`, `0x102727d0`, `0x10272130`, `0x10295a80`, `0x102a1c0f` |

   Keep **corpse_removed_unseen** (ordinary mortal, bit9 clear, player away: +10 PVS removal),
   **corpse_kept_seen** (same kind, watching: retained/rearm), **corpse_kindred_burns** (watched
   non-fade Kindred: unconditional +10), citing `0x1032c0e0` / `0x102696f0`; do not stage a fade
   maker child as Kindred's clear-bit control. Keep verbs_feed_trance's no-idle window paired
   only. Damage_lethal_death / verbs_stealth_kill have death/OnDeath/corpse once on kill tick,
   no NPC schedule/task after ordinary death; corpse_on_floor remains V4d's known_red.
6. **Validation after the last build, in this order.** Run the named records once on that
   binary, then default tier, arm tier, then **one full arena run**:

   ```text
   uv run elysium arena chase_melee melee_enemy_blocked melee_same_team team_damage_gate ranged_enemy_dead ranged_enemy_dead_retarget damage_knockout_one_hit damage_cower_one_hit damage_high_health_control corpse_fades corpse_pedestrian_stays corpse_removed_unseen corpse_kept_seen corpse_kindred_burns melee_swing melee_ally_in_the_way ranged_open_fire ranged_sustained_fire cover cover_move_shoot range_bands damage_lethal_death verbs_stealth_kill verbs_feed_victim_dispatch verbs_feed_trance control_sequence anim_player_weapon_event_melee anim_player_weapon_event_firearm
   uv run elysium test
   uv run elysium test arm
   uv run elysium arena
   ```

   No extra arm-family boot is necessary after complete default/arm tiers. A moved verdict is
   triaged by address, never loosened. If validation forces build2, repeat required final
   checks on its binary and reserve the sole full arena for after it; no third build/full run.
   Keep V4o ranged/overlay records green and PostThink dead-player tests green. Record actual
   totals and times rather than HANDOVER's baseline; distinguish remaining N4 intermittents,
   H11 rollcall red and V4d physics from failures of this wave.
7. **Recovery/close.** Verify C1 combat-and-damage, C2 lifecycle/senses/feeding, C3 teams.md
   record the settled behavior and addresses. Correct memory shorthand in C1's doc by C2's
   owed line. Close divergences row4 with commit; tick V4c in spec/tracker and place remaining
   work in triage. Do not close V4 until V4d. Slot247 is arm-tested on SequenceBounds's false
   seam, writes nothing live; no arena extents proof/import. Real reload (slots322/323) stays
   V5 with arm tests and a flamethrower record. Wider Presence, held-model attachments and
   player weapon clock remain their owners. Bbox pipeline/re-bake waits on acquire cone.
   D9 missing pose query and pose-parameter blend seam remain named; no new divergence adopted.
8. **Silent-class inventory, bounded lookup.** All operator/species event bodies and type-6
   launch are settled; no stale reading-owed list. From indexed/staged event tables record
   every NPC ranged activity lacking3030..3044, plus warnings by model/sequence; S12's
   flamet_attack_layer has no3031, a fact. ChangBros/FrenzyShadow/Bach/ManBat task fire is
   already placed on demand (absent both witness maps), not new V5 work. Report Tzimisce
   3045/3046 only if the named seam remains. Time one bank lookup before planning an inventory;
   query timeout means stop/optimize, no broad scan/retry. Historical fifth-hit attribution is
   still unverified: any cause-dependent work first reads S13 §4's task/damage addresses and
   captures flag/schedule/effective-cap/packet/death-caller evidence before changing math.
9. **Commit once, by explicit path; never push.** Review git diff --check/status, stage an
   explicit list of the actual approved lane/integration/record/recovery/generated files
   (`git add -- <path> ...`), never git add . or -A, never unrelated owner files. One commit
   describes final V4c behavior including teams/dead-enemy memory. Message includes a verdict
   table **record | before | after | remaining owner** for every written/corrected record,
   default/arm totals, full arena totals, build count/times and kernel check. No report*.md
   path or second commit. Final response ≤300 words gives those results and exact remaining
   owner work. Query rules: 10 s warns/log, every query 60 s hard stop, optimize before retry,
   no whole read over~200 KB; wait by completion notification, never polling.
