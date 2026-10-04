# Packets S13 — settling before V4c (2026-10-04)

One reader. `AGENTS.md` read first. Source, `Arena/`, and existing documents were read only;
this packet is the sole repository write. An existing untracked S13 draft was present and was
audited against the listings; its team-registry lifetime claim was corrected below. No build, test, bake, arena, game, or
commit was run. The messages of `64895278` and `88649932` were read with `git show -s`.

**Verified** below means read in the retail decompilation or instruction listing in this
sitting, in `vampire.dll` unless stated otherwise. **Inferred** means a consequence of those
bodies, a prediction for a staged record, or an attribution that the available evidence does
not measure. Port observations are explicitly labelled **port, read**; they are not retail
verification. Existing packet/map/clip data is identified separately. Addresses inside a
function identify the instruction, not a separately callable function.

Recovery entry points were `vtmb_where` (ledger rows, indexed prose sections and port cites),
then `vtmb_code`, `vtmb_asm`, `vtmb_fields`, `vtmb_readers`, `vtmb_callers`, `vtmb_slot`, and
`vtmb_vtable`. The initial `uv run elysium research where` covered the brief's addresses,
`npc_maker`, `TASK_KNOCKOUT`, and `ONE_HIT_KILL`; that requested CLI emitted its normal research
logs outside the repository. Subsequent lookups used the MCP index. No query exceeded 10 s
or reached the 60 s stop. Shell searches used a 60 s subprocess timeout and MCP reads a 60 s
watchdog. Budget exception: an early Python excerpt loaded the 219,611-byte weapon-test file
before slicing it, breaching the large-file rule. Later source excerpts used streaming
`islice`; the 1.9 MB manifest was searched with bounded `rg -o`, never loaded whole in this audit.

## 1. The maker's child and spawnflag bit 9

**Settled; verified.** This is a generated child flag, not inheritance of bit 9 from the
maker's own `spawnflags`.

| maker body | child flags before `DispatchSpawn` | fade source |
|---|---|---|
| `CNPCMaker::MakeNPC`, `0x1034b7b0` | **assignment** `4`, replaced with **assignment** `0x204` when `m_bFade +0x66c2` is nonzero | `Flag_Fade`; `Spawn 0x1034afe0` forces `m_bFade = 1` when `Flag_InfChild` is set |
| fleshpile `MakeNPC`, `0x1034c2d0` | existing child flags **OR** `4`, or **OR** `0x204` | the same byte; `Spawn 0x1034c020` has the same infinite-child implication |
| zombie `MakeNPC`, `0x1034d140` | calls the ordinary `0x1034b7b0` first; does not change the child's fade flag afterwards | `Spawn 0x1034cc60` also forces fade for infinite children |

Slot 617's three maker fills were checked. Do not describe the ordinary maker's assignment
as `| 0x204`; that wording in S4 and the C2 brief is imprecise.

**Verified death chain.** `CAI_BaseNPC::Event_Killed 0x10265ad0` calls the combat-character
death body, which calls slot 301 `CreateCorpse`, before the fade test. At `0x10265d66` it asks
slot 552, `ShouldFadeOnDeath 0x1027a400`: exactly `(spawnflags >> 9) & 1`. True calls
`SUB_StartFadeOut 0x102695d0` at `0x10265d72`; false inserts a carcass sound. The test reads
the **child's** flags. It does not inspect its owner or a maker's fade byte.

`SUB_StartFadeOut` changes render mode 0 to mode 2 and alpha 255 (other render modes retain
their alpha), adds solid flag 4, zeroes local angular velocity, relinks, sets the first think
to `curtime + 10.0`, and installs `SUB_FadeOut` (thunk `0x100152b2`, body `0x10269960`). Each
fade think: alpha > 7 loses 7 and re-arms at +0.1; otherwise alpha becomes 0 and `SUB_Remove
0x101c0b10` is installed at +0.2. **Inferred clock from verified steps:** alpha 255 produces
36 decrements, the zero-alpha think at death +13.6, and removal at about death +13.8, regardless
of visibility. Cadence/float rounding can move the observed timestamp by a think.

**Without bit 9, verified:** the corpse keeps the think selected by its own corpse body.
The ordinary non-Kindred ragdoll tail of `CreateCorpse 0x1032c0e0` installs `SUB_PVSRemove
0x102696f0` at +10. That poll retains the entity and re-arms at +10 if any player passes
view-cone, PVS, and visibility tests; otherwise it calls `UTIL_Remove 0x101cd940` immediately.
This is not an unconditional +10 removal. The Kindred tail instead burns and installs
`SUB_Remove` at +10. `CNPC_VPedestrian::CreateCorpse 0x103a38c0` snapshots bounds, calls the
base, **clears** the think and makes the corpse `SOLID_NONE`. Its ordinary corpse therefore
stays. **Inferred from verified ordering:** with bit 9, the later fade install supersedes
both the Kindred +10 removal and the pedestrian's cleared think.

**Port, read.** `ElysiumNpcMaker.cpp`, `FElysiumNpcMaker::MakeNPC` :460 already assigns
`bFade ? 0x204 : 4`; its `Spawn` forces fade for infinite children. The fleshpile :226 and
zombie's base call also match. `ElysiumNpcBaseLifecycle.cpp::ShouldFadeOnDeath` reads bit 9.
`ElysiumNpcBaseSpawn.cpp::Event_Killed` :133 already dispatches the fade step, but
`Spawn19StartFadeOut` :204 only increments a counter. Thus the call is present and the body
is missing; the claim that this version of `Event_Killed` has no call is stale.
`ElysiumNpc.cpp::Think` :646 treats every committed death as `SUB_PVSRemove`, overriding an
explicitly cleared or fade think. C2 must fix that dispatch as well as implementing the fade.

**Record staging, inferred acceptance.** Keep `corpse_fades`'s actual tutorial
`stealth_victim_maker` (`NPCType npc_VVampire`, `Flag_InfChild 1`), fire `Spawn`, kill the
named **child**, retain render mode 0 / alpha 255, and keep the player watching it. Assert the
child's `0x204` flag, death/corpse, no removal through death +13, and removal by death +14.5.
The Kindred burn at death is allowed; removal at +10 is forbidden. This distinguishes a real
fade from unseen-corpse polling. The existing absolute `never until 16.0` is a timing
approximation; use the now-supported relative window `after: dies, within: 13.0`.

## 2. `+0x10b0`: team symbols, writers, readers, and C3

**Settled.** `m_TeamSymbol` is the project's recovered member name (**inferred name**, recorded
in `npc-kernel/layout.md` :439); the listing verifies an **unsigned 16-bit symbol**, not an
integer relationship, squad number, or team entity pointer. `0x10323a70` reads a word;
`SameTeam 0x10323930` compares it as `ushort`, with **`0xffff` invalid**. Adjacent
`m_sTeamName +0x10ac` is the `string_t` keyfield **`team_name`**, confirmed by the datamap
field/string lookup (`datamap_CBaseCombatCharacter_builder 0x1031a600`, name string
`0x1061ea74`, key string `0x1061ea68`). The symbol itself has no keyfield or input row.

### Writers — verified, including initialization and restore

1. Constructor `0x10326de0`, instruction **`0x103272f6`**, stores word `0xffff` at +0x10b0.
2. `AddToTeam 0x103239a0`, instruction **`0x10323a21`**, stores AX at +0x10b0. It strips
   **one** leading `!` and calls the registry `0x10751140` through `0x10230880`.
3. `0x10230880` returns `0xffff` for null/empty names; otherwise `Q_strncpy(..., 0x80)` into
   the registry scratch string, lowercase, then find-or-insert `0x1024b5e0` on the symbol
   table at registry +8. Existing names reuse their symbol. A sole `!` therefore means no team.

There is **no direct team-symbol key/input writer**. The `team_name` keyfield populates the
name, which the spawn/restore calls convert to the symbol. `AddToTeam` does **not** write
`m_sTeamName` itself. All five actual callers of its thunk `0x10015bcc` were enumerated:

| caller | name passed |
|---|---|
| combat-character `Spawn 0x10323a90` | nonempty `m_sTeamName` |
| Troika `Spawn 0x10298d30` | nonempty `m_sTeamName` |
| combat-character `Restore 0x10348890` | nonempty restored `m_sTeamName` |
| player `Spawn 0x1016d260` | literal **`player`**, `0x10566574` |
| player `Restore 0x1016ebd0` | literal **`player`**, after the base restore |

`vtmb_readers` finds only the raw getter and thunk, because the constructor uses dword-array
index `0x42c` and the writer uses `field_0x10b0`. A bounded indexed search of both byte offset
and array-index forms, with the constructor's WORD store checked in assembly, found the two
writer bodies above (thunks are aliases). The field ledger's empty typed section is not proof
that no writer exists.

### Readers — verified, complete recovered call chain

Direct raw read: getter **`0x10323a70`**, thunk **`0x1000bc44`**. Its callers are:

- **`SameTeam 0x10323930`**, thunk `0x10008d7d`: self symbol valid, other nonnull, other
  symbol valid, equality → true; otherwise false. Only the low-byte bool is meaningful.
- `AddToTeam 0x103239a0`'s final getter call, whose answer is discarded.

All four real callers of `SameTeam`'s thunk:

| reader | meaning of the test |
|---|---|
| `MeleeSwingStep 0x10343020`, **`0x103439a0`** | with an NPC attacker, a distinct combat-character victim and `debug_allow_melee_ff == 0`, require relation hate/fear, then **reject same team**; self, non-character, player-attacker and debug-bypass arms pass this particular filter |
| `OnTakeDamage 0x1032ef60` | reject a teammate's damage packet before discipline notification and life-state dispatch, **except self-damage** |
| `UpdatePresenceEffect 0x10322b40` | same team **or** disposition `D_LI` classifies Presence as friendly benefit; otherwise enemy effect |
| **`TeamFilter 0x103426b0`** | obtain candidate's combat-character self-cast at +0x9c (null for no candidate) and return the same-team predicate through the wrapper |

The last reader is a filter wrapper, not another team owner. Assembly at
`0x1034394d..0x103439a7` verified the melee relation test precedes the team rejection.
The packet-level inference in S5 D1 is now a verified symbol predicate.

### Witness defaults and minimal lane

**Inferred defaults from verified writer closure plus map data:** the deployed manifest
contains **zero `team_name` keys** (bounded lookup and streaming scan). Thus every placed NPC
and maker child on `sm_hub_1` / `sp_tutorial_1` starts with `0xffff`, and spawn has no nonempty
name to register. This covers the reach classes `CNPCMaker`, `CNPC_VHumanCombatant`,
`CNPC_VPedestrian`, `CNPC_VRat`, `CNPC_VVampire`, plus the hub's `CNPC_VCop`, `CNPC_VHuman`,
`CNPC_VHunter`, `CNPC_VTaxiDriver`, and dialog pedestrian line. The ordinary maker does not
copy a team symbol into its child. Equal class, equal squad, or neutral relationship does not
give two bodies a team. The **player** is the exception: it joins the named `player` team.
This is map-start/default evidence, not a claim about arbitrary future keyvalue injection.

**Port, read.** `ElysiumNpcBaseSpawn.cpp::Spawn19TeamName` :278 always returns empty;
`Spawn19AddToTeam` :283 is counted only. `ElysiumNpcKernelBindings.cpp` :552 leaves
`team_name` unbound with a stale "no reader" comment. `ElysiumWeaponClasses.cpp` :2757
`ElysiumSwingSameTeam` returns false. `ElysiumCombatCharacter.cpp` :1195
`CombatTeamSymbolOf` always returns `0xffff`; its packet rejection therefore never detects
teammates. The current neutral-ally record passes by the preceding **relationship** filter
and proves nothing about team identity.

**C3 design, inferred implementation contract from the verified bodies:** carry `TeamName`
and a `uint16 TeamSymbol = 0xffff` on **the combat character**, one find-or-insert registry
owned by the game system but **cleared at each level boundary**, and one common `IsSameTeam(other)` accessor. Bind
`team_name`; replace the spawn counted seam with the real `AddToTeam`; rebuild symbols from
names on restore; explicitly join the player to `player`. Registry constructor
`0x10230750` initializes the global symbol table and destructor `0x102307b0` destroys it.
**Verified lifecycle correction:** `CTeamManager` vtable `0x1048e6dc`, slot 2
`LevelInitPreEntity 0x10230820` and slot 5 `LevelShutdownPostEntity 0x10230860`, both call
`0x1024b880(table +8)`: clear the tree, free string pools, reset the live count. Slot 3
`LevelInitPostEntity 0x102308f0` calls `0x1024b940`, a diagnostic enumeration, not a reset.
The old draft's session-long membership claim was wrong. Re-register entities after the
pre-entity reset; do not retain numeric symbols across map transitions. Match the one-`!`, 127-byte payload and
lowercase normalization; do not equate empty/invalid names. No member lists, faction matrix,
team actors or squad coupling are needed.

Port the bodies represented by `0x103239a0`, `0x10230880` (including its table operation),
`0x10323a70`, `0x10323930`, `0x103426b0`, and the level-init/shutdown clear hooks
`0x10230820` / `0x10230860`. Route the **two live consumers**, contact and
damage, through that same accessor. The Presence reader's team test is recovered; its wider
discipline body remains its own story, not a substitute constant or a reason to build it in
C3. For generated bindings, edit their owning generation source and regenerate in integration;
do not hand-edit the generated binding/table file. No new `SetTeam` input is justified by this
listing. Player/team restore joins and cross-lane consumer lines must be explicit integration
patches where another lane owns the file.

## 3. Dead enemy: selection, memory, and the exact port divergence

**Settled; verified retail chain.** Death is observable while the entity handle still resolves.
`IsAlive` slot 158, `0x100b4dc0`, reads `m_lifeState == LIFE_ALIVE`; being present, visible,
not hidden, or not pending removal is insufficient.

1. Full `GatherConditions 0x1026ec30` performs sensing, memory refresh (`0x1026ee15`), then
   `ChooseEnemy 0x10279dd0` (`0x1026ee1c`). If a committed enemy remains, it calls
   `GatherEnemyConditions 0x10270b20` (slot 481).
2. `ChooseEnemy` tests the current enemy's `IsAlive`. A dead current enemy can trigger a new
   choice when the running schedule listens for `ENEMY_DEAD`; otherwise its usual
   `NEW_ENEMY`/`LOST_ENEMY` interest gate still applies. On change, it sets `ENEMY_DEAD`
   for the **old dead enemy**, sets/clears `NEW_ENEMY` according to the new pointer, then
   calls `SetEnemy`. It handles enemy-memory bits, squad-slot release, sight-memory bit and
   lost-enemy/player output in the existing listing order. It does not blindly clear 0x58.
3. If the dead enemy remains committed, `GatherEnemyConditions`, **`0x10270e5a..0x10270e89`**,
   asks its `IsAlive`, sets **`ENEMY_DEAD 0x58`**, clears `SEE_ENEMY 0x46` and
   `ENEMY_OCCLUDED 0x48`, and returns before gathering attacks. It does **not** clear the handle.
4. `BestEnemy 0x102743c0`, **`0x10274475`**, independently rejects every candidate whose
   `IsAlive` is false, even if the memory list deliberately retains its entry.
5. `SetEnemy 0x10279a50`: a pointer change calls `SetLastEnemy(old)` when old resolves and
   clears attack conditions (slot 560). Null stores `0xffffffff` in +0x5ce0. Nonnull stores
   its handle and notifies the discipline manager. Clearing the handle is not the same
   operation as removing its memory record.

**Verified memory detail that the existing prose abbreviates too far.**
`RefreshMemories 0x102df320` unconditionally removes an unresolvable handle. A resolvable
entity is a dead-entry removal candidate only if its **NPC self-cast +0x94** exists and slot
464 says **state 7**; removal then requires permission from owner slot **54**, or the squad's
AND-of-members predicate `0x103167f0`. Troika slot 54 `0x102b50b0` refuses removal of the
current enemy while a schedule exists that does **not** list `LOST_ENEMY 0x47`. Base slot 54
`0x10026910` returns false. A vetoed dead entry can remain indefinitely. A player corpse is
not an NPC-state-7 entry; its presence still does not make it a valid `BestEnemy` candidate.

Kept entries refresh tracked position +0x00 only while `now < lastSeen + freeKnowledge`;
there is no age expiry and no refresh of LKP +0x0c. Removed entries are unlinked before
owner slot **56** (or squad fanout `0x103169a0`) receives target, LKP +0x0c, vector +0x18 and
the function tag; Troika `0x102b5120` clears `m_hLastEnemy` only when the removed target is
nonalive and equals it. There is also a verified traversal quirk: after removal,
**`0x102df50e..0x102df518`** takes the successor's **next** link, skipping the immediate
successor for this pass. Preserve that order if replacing the refresh, including multiple
dead entries; do not substitute an eager `RemoveAll(!IsAlive)`.

**Verified schedules/state exit.** Base `SelectSchedule 0x1028a380` tests `NEW_ENEMY` first
(→ `WAKE_ANGRY 5`), then `ENEMY_DEAD`: `SetEnemy(NULL)`, `ChooseEnemy`; a live replacement
clears 0x58 and re-enters the selector; no replacement sets state **3 ALERT** and re-enters
it. Its alert branch returns `ALERT_SCAN 8` if 0x58 stands and activity 0x61 has a sequence,
otherwise the ordinary alert ladder.

Troika `SelectSchedule 0x102af660`, **`0x102afc24..0x102afca7`**, does the same clear/rechoose
transaction at the head of combat, but chooses **IDLE 1 when `m_bNoAlertState +0x65f6` is
true, otherwise ALERT 3**. `PreSelectSchedule 0x102ae920` also exits combat on a null enemy
before its `NEW_ENEMY → START_COMBAT 0xea` arm. Under quiet normal staging, the Troika
reselect is `SCHED_TROIKA_ALERT_WAIT 0x4b`; no-alert staging takes
`SCHED_TROIKA_IDLE_DISPOSITION 0x6b`. Other incident/sound/damage predicates can win their
earlier arms; there is no universal fixed "enemy died" program. `SelectIdealState
0x1026f660` case 2 changes combat-with-null-enemy to ALERT, not on the dead bit alone;
Troika `0x102ad660` falls through to it for combat. Its alert→idle path subsequently depends
on slot 462. Do not add an unconditional ENEMY_DEAD→idle state shortcut.

**Port, read; exact fix.** `ElysiumNpcEnemy.cpp::ChooseEnemy` and `SetEnemy`,
`ElysiumNpcBaseConditions2.cpp::GatherEnemyConditions`, and the two selector death arms
already reproduce the relevant control flow. **`ElysiumNpcBaseSenses10.cpp`,
`FElysiumNpcBase::BestEnemy`, the candidate gate under the `0x10274475` comment (:173), uses
`Candidate->IsInert()` instead of `!Candidate->IsAlive()`**. A killed body has a nonalive
`LifeState` but `bDead == false` until removal; `IsInert` only reads pending removal/hidden.
It can therefore be chosen again, setting `NEW_ENEMY`, and the preselector chooses
`START_COMBAT` before the combat selector gets to clear it. **Replace this one gate with
`if (!Candidate->IsAlive()) { continue; }`**; do not suppress START_COMBAT or fake a clear
condition. The rejection at `0x10274475` is a single proved divergence in an otherwise
already-ported selection transaction.

The memory companion also diverges: `ElysiumNpcEnemyMemory.cpp::Refresh` uses `IsInert`
for position refresh/removal, has no owner/squad veto or removal notification, and eagerly
removes all matching entries. C2 must recover its owner context from the store's actual
owner and call the already-ported slots 54/56 in `ElysiumNpcTroikaHelpers.cpp`.
`GatherConditions`'s refresh call must reach slot 541's selected store; `BestEnemy` currently
walks the member `EnemyMemory` directly, so preserve the connected/disconnected/shared-store
seam of `GetEnemies 0x10273e10` as well. These are separate fidelity work, not prerequisites
for claiming that the one candidate-liveness line is wrong.

## 4. Knockout, cower, and the unmeasured fifth hit

**The lethal rule is settled, verified; the historical attribution is not.** A count of five
hits is not a retail knockout criterion, and the test comment's assertion that knockout is
the sole writer is false.

`TASK_KNOCKOUT 0xdd` starts at **`0x102a3339`** inside Troika `StartTask 0x102a1910`:
`m_bfAINPCFlags +0x14b8 |= 0x440a0000` at `0x102a3344`, then
`RestartIdealActivity(0x1050)` at `0x102a3352`. Those bits are SLEEPING, NO_DIALOG,
DONT_INVESTIGATE and **ONE_HIT_KILL `0x40000000`**. `TASK_UNKNOCKOUT 0xde`,
`0x102a3364`, merely restarts 0x1052; it does not clear them. Schedule-change masking clears
the transient bits unless its preserve-path arm keeps them.

`SCHED_TROIKA_KNOCKOUT 0xfa` stages `MAKE_OBLIVIOUS TRUE; STOP_MOVING; KNOCKOUT;
SLEEP_BOUNDING_BOX 1; SET_ACTIVITY ACT_SLEEP_IDLE; WAIT 60; WAIT_RANDOM 120; ... UNKNOCKOUT`,
with no authored interrupts. Retail input **`Faint 0x1029f250`** resets think timers and
installs it. Pedestrian `SelectSchedule 0x103a29f0` returns it only when `PASS_OUT 0x24`
stands and `IsBusyWithDiscipline` is false (after its first-think arm). Its gather
`0x103a2c30` clears PASS_OUT before the Troika gather. None of this counts gunshots to five.

**Exact independent-of-health predicate, verified:** Troika slot 390
`OnTakeDamage_Alive 0x102beda0` caches the packet and calls the base
`CAI_BaseNPC::OnTakeDamage_Alive 0x10265ed0`; if the base result is nonzero, the packet's
descriptor `GetDmg()` (or scalar +0x30) is **strictly > 0**, and flags1 has **`0x40000000`**,
it calls victim slot 144 `Event_Killed(info)` at **`0x102beea4`** and returns before the
ordinary tail. There is no health, hit-count, damage-type, unconsciousness, or activity test
in this arm. A zero-damage packet does not take it. The outer damage gates still have to
admit the packet; notably no damage mode or another teammate's packet returns before here.

The other immediate health-independent kill arm in this same body is a positive hit while
occupying an interesting place with a death activity, with `m_iInterestingDeathActivity == -1`
and the activity-name lookup returning -1: **`0x102bef13`** kills immediately. A valid name
instead installs `SCHED_TROIKA_INTEREST_DEATH 0x104`; an already-resolved name returns without
reinstalling it. The ordinary `CBaseCombatCharacter::OnTakeDamage 0x1032ef60` kill compares
sheet damage stat **0x0f** with sheet maximum **0x11**; it is not a five-hit rule.

**The missing alternative writer, verified from retail schedule data and the task listing:**
`SCHED_TROIKA_COWER_SIMPLE 0x109`, `_HINT 0x10a`, and `_NOSEE 0x10b` all execute
`TASK_SET_NPC_FLAG NPCFlag:ONE_HIT_KILL` **before** `PLAY_COWER` / `SET_COWER`.
The task arm **`0x102a585d..0x102a5874`** applies a positive raw mask to flags1 via
`0x102a97a0`; no knockout task is needed. `SCHED_TROIKA_DO_SLEEP_ACTIVITY` sets the same
flag too. The three cower blobs are respectively **`0x105e5788`, `0x105e5590`, `0x105e5398`**;
their deployed `.sch` programs were read.

There is a reachable pedestrian chain, not just a mask: `CNPC_VPedestrian::SelectIdealState
0x103a2e30` sees an admitted LIGHT/HEAVY/REPEATED_DAMAGE interrupt, records the attacker and
sets ideal state **8 FLEE** (`0x103a34a3`). Troika selector case 8 (`0x102b0250`) can choose
`COWER 0x77` when no enemy/fear/damage/law condition remains, or `FLEE_AND_COWER 0x73`.
`0x77` installs/fails to `0x109`; `0x73` has fail schedule `0x74 FLEE_AND_COWER_STALL`,
whose path failure goes to **`0x10b COWER_SIMPLE_NOSEE`**. Those actual schedule programs
reach the flag write. This supplies a retail explanation for a high-health pedestrian dying
after it has had time to flee/cower, including a headless world's failed navigation.

**Port, read.** `ElysiumNpcStartTask.cpp::StartTask`'s knockout arm :2033 and
`ElysiumNpcDamage3.cpp::OnTakeDamage_Alive` :347 match the verified flag and lethal gate.
`ElysiumNpcStartTask_2.cpp::StartTask19SecondHalf`, TASK_SET_NPC_FLAG :1038, applies the
cower program's same flag. The weapon fixture's `MakeReachTestDefs` creates `far` as
`npc_VPedestrian`, template **Thug**, and `SeedHealth` changes its sheet ceiling to 100000.
It does not stage Faint, PASS_OUT, or a knockout schedule. The comment at
`ElysiumWeaponTests.cpp` :4010 attributes a past death to knockout without a flag/task trace;
its new `SetDisableAi(true)` prevents both cower and knockout execution and therefore cannot
identify which was responsible before the change.

**Verdict, inferred and bounded:** five hits of 18 against a *still-effective* sheet cap
100000 cannot kill by arithmetic (90 < 100000). They **can** kill in retail if the fifth
positive packet arrives after either cower or knockout has set ONE_HIT_KILL. Cower is a
reachable explanation for this fixture; knockout is not established. No flag/schedule
snapshot from that historical failure was supplied, so **the historical death is not
verified as either retail cower/knockout or a port damage bug**. If ONE_HIT_KILL is clear,
the interesting-death arm absent, and the actual current sheet ceiling remains 100000, an
early death is a port defect and needs its actual death caller recovered. Do not patch
damage math or call the fifth hit retail merely from the amount and the test comment.

## 5. The feed victim's first think: an idle sequence is allowed

**Settled ordering, verified; exact idle variant inferred.** S10 correctly says the leave
itself writes no animation words, but its claim that the next sequence is always the trance
task's omitted the maintenance between tasks.

1. `FeedInterrupt 0x1033a9e0` installs MESMERIZED 0xfb on a surviving NPC victim that does
   not hate the feeder, **while the pair still exists**. `SetGrappleActivity 0x1032a100`
   commits `m_IdealActivity = phase base` and `m_Activity = translated role/size cell`.
   For normal feed release those are **0xf88 ACT_FEEDING_FEED_RELEASE** and, for the staged
   tall male feeder/front victim, **0xf8c**. Do not confuse 0xf88 with the later generic
   `ACT_FEEDING_RELEASE 0xf91` family.
2. `RunAlternateAI 0x1028fd80` with live partner / victim role 1 skips RunAI; release bases
   including 0xf88 permit AutoMovement. The activity/base mismatch remains while paired.
3. The three `LeaveGrappleState` bodies `0x10329a70`, `0x1026ce30`, `0x102b5d90` leave
   activity, ideal activity, sequence, cycle, NPC state and schedule untouched. The Troika
   decompile was damaged: its full assembly verifies base call then tail jump to slot 614
   at **`0x102b5d9d`**, resetting think timers.
4. On the first released AI pass, the installed 0xfb program's **first task** is
   `TASK_MAKE_OBLIVIOUS TRUE`. The start arm **`0x102a72e3..0x102a7315`** sets flags2
   0x80001000, calls `MakeOblivious 0x1026d130` (enemy clear, squad disconnect, nesting
   increment), fires OnIncapacitatedStart and completes. **No activity word is changed.**
5. `MaintainSchedule 0x102817c0`, **`0x10281eee`**, calls `MaintainActivity` immediately
   after that task, before advancing to the later SET_ACTIVITY. Slot 466
   `ShouldMaintainActivity 0x102bf510` refuses schedule 0xb9, not 0xfb; normal non-scripted
   staging therefore maintains. `MaintainActivity 0x102727d0` sees the release-cell/base
   mismatch, calls `ResolveActivityToSequence 0x10272130` on the retained **base 0xf88**,
   then `AdvanceToIdealActivity 0x102726a0`.
6. **Inferred for the witness body from verified resolver plus clip data:** the forced-feed
   banks (male 101888 bytes, female 102639 bytes, queried by activity) author release
   **cells**, including victim 0xf8c, and **no bare ACT_FEEDING_FEED_RELEASE** clip. The
   released human translation leaves that base unchanged: human early translation
   `0x103854f0` delegates to Troika `0x10295590`, then combat-character early translation
   **`0x10328030`** recognizes a grapple base through `0x104126a0` and calls
   **`TranslateBaseGrappleActivity 0x10328380`**. With implicit role -1 and the partner/role
   already cleared by the leave, that body returns the **unchanged base**, not a victim
   cell. Human late translation `0x103858b0` / Troika `0x10295710` also leaves this base
   unchanged. When the base pick misses,
   `0x10272130` retries **ACT_DISPOSITION 0xf1**, and Troika resolver `0x10295a80` gets a
   stance sequence from slot **611**. `SetActivityAndSequence 0x10272490` commits that
   idle **sequence**, retaining the requested **activity 0xf88**, with translated activity
   0xf1. Thus an idle-labelled row here is not a write of ACT_IDLE 1.
7. The program's intervening flag tasks change no activity. Its TASK_SET_ACTIVITY
   `0x102a1c0f` then calls `SetIdealActivity(ACT_DISPOSITION_MESMERIZED 0x104e)`; the next
   maintenance commits the mesmerized sequence. The intermediate idle row can exist
   within the same think, with no separately displayed idle frame.

**Verified exclusion of the suggested alternative:** `RunAnimation 0x1026c540` runs in
PostRun after the AI/tasks, and its idle re-pick requires **`m_Activity == 1`**, state not
4/7, and activity finished. Before trance the requested activity is still 0xf88; afterwards
it is 0x104e. Neither is 1. It cannot be the source of this intermediate idle row.

**Port, read.** The same calls stand in `ElysiumSchedule.cpp` :453,
`ElysiumNpcBaseAnim10.cpp::BaseMaintainActivity` :207,
`ElysiumNpcBaseAnim.cpp::ResolveActivityToSequence` :599,
`ElysiumNpcStartTask_2.cpp` :1783, and `ElysiumNpcStartTask.cpp` :929.
`ElysiumFeed.cpp::EndFeedVictimRole` :464 does no NPC animation reset.
The current name-based sequence bridge can substitute a disposition row already at its
activity lookup; C2's planned direct weighted-table lookup must instead report the missing
base and let the **retail resolver** perform the disposition retry. That can change the pick
site/RNG draw, but does not justify forbidding the idle row.

**Verdict:** the intermediate disposition idle sequence is a retail-permitted consequence
of the first maintenance, **not a demonstrated port defect**. The particular
`Stance_Neutral_Idle_3` variant in commit `88649932` is not guaranteed by the listing: it
depends on slot 611's stance state/draws and available sequences. No fix to the leave,
RunAnimation, or maintenance ordering is warranted. A blanket no-idle assertion after
release would prohibit retail's fallback.

## Changes to the plan

These are instructions for the later lanes, not edits made by this reader. Record outcomes
are **inferred acceptance criteria**, not results: no record was run here.

| item | lane and exact instruction | arena record: staging; expect / never |
|---|---|---|
| 1 — fade child | **C2**: keep the existing maker assignment; replace `Spawn19StartFadeOut`'s counter with `0x102695d0` and implement `0x10269960`; route both Event_Killed and TASK_DIE's existing StartFadeOut through that body. Dispatch the installed think, with explicit clear distinguished from a missing restored name. Later fade wins over burn/removal/clear. Correct assignment-vs-OR and stale no-call wording in the lane's recovery. **C1/C3: none.** | **`corpse_fades`**, existing: tutorial stealth maker → named Kindred child, alpha 255, watching player; expect 0x204, death/corpse and removal by death +14.5; never removal within 13 s after death. Keep `corpse_removed_unseen`, `corpse_kindred_burns`, and `corpse_pedestrian_stays` as the clear-bit controls described in item 1. |
| 2 — team identity | **New C3**: combat-character TeamName/uint16 symbol, level-cleared registry (0x10230820 / 0x10230860), real spawn/restore registration including player, getter/SameTeam/filter, generated key binding. Replace both no-team constants with the shared accessor. C3 reports the exact lines in C1-owned `ElysiumWeaponClasses.cpp` and C2-owned spawn/player files for integration, avoiding concurrent edits. Record the four-reader closure; leave wider Presence to its discipline owner. **C1** applies the shared contact predicate at its existing filter; **C2** supplies the registration hook patches if it owns those files. | **New `melee_same_team`**: attacker hates the bystander (D_HT, below the player's priority), both use nonempty `!Arena_Melee` / `arena_melee`, no squad, FF off; bystander overlaps the swing but is off slot 331's centre ray. Expect an admitted swing and positive damage to an otherwise-identical **different-team control**; never damage, contact-side impact/knockback, or a `Swing.RecordHits` entry for the matching-team bystander. Expose that hit-once observation if the arena lacks it: damage alone could pass through the separately correct outer damage refusal while the contact filter remained wrong. Assert symbols equal and not 0xffff. Existing **`melee_ally_in_the_way`** stays the neutral/no-team relation control. Add **`team_damage_gate`**: a named gunman's live weapon packet, with its attacker handle intact and an explicitly hated teammate as target → no damage; the different-team control → damage. Self-damage remains an admitted predicate check. Do not use an attackerless scalar TakeDamage input to claim this gate was exercised. |
| 3 — dead enemy | **C2**: replace `BestEnemy`'s candidate `IsInert` gate with `!IsAlive` in `ElysiumNpcBaseSenses10.cpp`; preserve ChooseEnemy/SetEnemy/selector order. Port `EnemyMemory::Refresh`'s state-7 owner/squad permission, notify, tracked-position and cursor arms with slot 541's actual store. Do not eagerly purge all nonalive entries or suppress START_COMBAT. **C1/C3: none.** | **New `ranged_enemy_dead`**: gunman hates exactly one NPC, player neutral, no hints/incidents/sounds, normal alert state; victim AI off, kill it after acquisition, keep the nonhidden corpse resolvable/visible. Expect enemy → none and ALERT/`SCHED_TROIKA_ALERT_WAIT (0x4b)`; never subsequent START_COMBAT or a ranged hit on the corpse. Companion **`ranged_enemy_dead_retarget`** remembers a second live hostile: expect that live enemy and allow one fresh START_COMBAT; never choose either dead body. Include a death-during-a-schedule-that-vetoes-LOST_ENEMY memory check: retained dead memory must still be unselectable. |
| 4 — fifth-hit attribution | **C1**: replace the weapon-test comment's unmeasured knockout assertion with the verified flag predicate and cower alternative. Keep isolated shot testing's disabled victim AI. No damage-math fix is justified yet; any work relying on the historical cause must first capture the schedule/flags, current sheet cap/wounds, packet magnitude, and death caller. **C2/C3: none.** | **New `damage_knockout_one_hit`**: high-health neutral victim, send retail Faint, wait for TASK_KNOCKOUT before a positive 18 packet; expect death with wounds below cap. Zero packet before that positive hit must never kill. **New `damage_cower_one_hit`**: damage-responsive pedestrian with no usable flee nodes; wait for COWER_SIMPLE(_NOSEE)'s ONE_HIT_KILL task, then positive 18 → death; never require TASK_KNOCKOUT. **New `damage_high_health_control`**: effective sheet cap 100000, no interest-death place, AI disabled/flag clear, five separately recorded 18 commits → 90 wounds; never death. These prove the rule and distinguish a port bug; they do not retrospectively measure the old fifth hit. |
| 5 — released victim row | **C2**: preserve first-task maintenance, release base/cell mismatch, true missing-activity answers, resolver's 0xf1 retry, and eventual 0x104e commit. Remove any proposed blanket no-idle gate spanning the released first pass; do not reset activity on leave or zero cycle at RunAnimation's independent idle re-pick. Correct S10's "next sequence is the trance task's" claim in the matching later recovery. **C1/C3: none.** | **`verbs_feed_victim_dispatch`**, existing stage: neutral surviving victim, paired front feed/release. Expect MESMERIZED install before OnGrappleEnd, MAKE_OBLIVIOUS on first released think, then a disposition fallback row before SET_ACTIVITY/MESMERIZED, final activity 0x104e; never idle **while paired**, never death. The current `until 5.6` window intentionally excludes the first released pass; keep that distinction. **`verbs_feed_trance`**: keep the grapple-only no-idle window, do not extend it across released maintenance. Do not pin idle variant 3. |

Order remains V4o → V4c → V4d. V4c now has C1, C2, **C3**, and its integrator; stage shared
file patches in lane reports and apply them serially. The historical fifth-hit cause is the
one attribution left unverified; the lethal predicate and the reachable cower program are
settled before any dependent change.
