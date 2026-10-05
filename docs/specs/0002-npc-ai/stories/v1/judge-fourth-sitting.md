# Judge's fourth sitting — the flamethrower and real reload

2026-10-05. Judgment against main checkout `9e29f419` and its current source. Read main and coordination AGENTS.md, the third sitting, the V5b commit message and reload brief. `research kernel --check` matched all seven checks; used `research where`, `research section`, direct vtmb-corpus code/asm/callers/vtables, and read-only installed-file inspection. All code addresses below are **vampire.dll**, image base `0x10000000`. Unindexed TASK_RANGE_ATTACK1 run bytes were read directly from `Vampire/dlls/vampire.dll` with Capstone. No game execution, build, automation tests, bake or source implementation was performed. The research CLI emitted its normal work-root reports/logs; this ruling is the only authored file.

| Item | Ruling | Instruction |
|---|---|---|
| V5b's missing `ranged_real_reload` | **do it now**, bounded verification follow-up | Run a real flamethrower-owning NPC through naturally selected real reload, starting from an explicitly staged empty magazine and positive reserve. Finish admission, staging and observation support now. Preserve the retail humanoid's silent flamethrower attack. Do not invent a firing caller, event, timer or clip debit to make this record possible. Arm proof alone does not close the owner's live-run requirement. |

## Findings — questions 1–5

### 1. NPC firing entry: verified chain; negative reachability is an inference

**Verified.** `CWeaponRanged_FlameThrower` vtable `0x104d2d74` fills **weapon slot 372** with `Attack 0x103e2f30`; slot 373 is inherited generic `Shot 0x102387b0`. Weapon slots 322/323 are reload functions, not the unrelated NPC slots of those numbers. Slot 326 is flame `PrimaryAttack 0x103e2ec0`, slot 319 `ItemPostFrame 0x103e31c0`, slot 320 `ItemBusyFrame 0x103e3200`.

`PrimaryAttack` activates the flame latch/sound and calls base `PrimaryAttack 0x102382f0`, which sets primary mode and calls `ModeDispatch 0x102383b0` with **false**: mode types 1/2 call slot 372. Its two frame overrides call their base frame first, then slot 372 whenever flame-active `+0x1518` is set, then simulation `0x103e32b0`. Thus the player's press/held-fire frame path reaches Attack without an animation shot event. `CBasePlayer::ItemPostFrame 0x10174ce0` supplies those weapon-frame calls. The sole corpus direct caller `0x10011919` is Attack's import-style jump thunk, not an NPC producer.

**Verified for the shipped human carrier.** `CNPC_VHumanCombatant` uses human StartTask `0x103847f0` and RunTask `0x10384ab0`; both delegate ordinary ranged tasks to Troika. Troika TASK_RANGE_ATTACK1 start `0x102a4505..0x102a4573` only initializes `m_iBurstFireCount`. Its run arm `0x102ab0a9..0x102ab1e0` moves/aims, checks the weapon next-primary stamp, decrements burst count, and calls `0x102aaa60` to restart the attack activity; it does not call weapon PrimaryAttack or Attack. Helper `0x102aaa60` chooses ordinary/cover activities, with condition 99 refusal; no weapon firing virtual occurs there either.

NPC animation forwarding to ranged `HandleAnimEvent 0x10238160` takes 3030..3044 through `0x10238320` into **ModeDispatch(true)**, which calls slot **373**, not 372. Generic Shot's NPC path does not spend its clip. NPC PostRun `0x1026c7c0` calls `Weapon_FrameUpdate 0x1032aa40`, which dispatches weapon slot **369** `0x1024efa0`: held-model sequence advance/event dispatch, not ItemPostFrame/ItemBusyFrame. The supposed NPC weapon frame loop is therefore the wrong interface.

The corpus's other slot-372 candidate calls were checked for receiver identity. Troika `0x102a46ac` is a cast to **CWeaponMelee** followed by RequestActivity, behind the melee capability bit; it is not a flame caller. `0x1029c4a0`, `0x103e8c50`, `0x103e8c90`, `0x102a0870` and `0x10344f80` use other receivers at colliding slot numbers. Bach's custom task `0x14d` in `0x103645a0` can call an active weapon's PrimaryAttack, but neither the shipped human Hunter nor the patch vampire flunkies is Bach. A corpus POSSIBLE edge is not proof of a flamethrower NPC firing wire.

**Inferred, strongly supported by those verified bodies and the raw content below:** the ordinary shipped human/vampire carrier does not fire a flamethrower through its ranged program. A shot event is necessary for its generic NPC Shot path, and these flame clips have none; even inserting 3031 would reach generic Shot rather than specialized Attack. Attack's unconditional owner debit proves what happens *if called*, not that the ordinary NPC calls it. No live retail run was observed; no claim is made about every hypothetical species equipped through a debug fixture.

### 2. Retail event tables: verified, not a bake defect

Read both stock `Vampire/pack*.vpk` and patch-first loose MDLs, not only pipeline output. The same counts occur in both sources. Studio header NumLocalSeq/LocalSeqIndex are at 272/276; descriptor stride 764; `numevents`/descriptor-relative `eventindex` at +20/+24, event stride 76.

| Bank | Sequence | Local index | Descriptor file offset | Event count | Relative event index |
|---|---|---:|---:|---:|---:|
| male | flamet_attack | 170 | 0x529e28 | 0 | 338800 |
| male | flamet_reload | 172 | 0x52a420 | 0 | 337288 |
| male | flamet_attack_layer | 173 | 0x52a71c | 0 | 336524 |
| female | flamet_attack | 197 | 0x485384 | 0 | 290116 |
| female | flamet_reload | 199 | 0x48597c | 0 | 288604 |
| female | flamet_attack_layer | 200 | 0x485c78 | 0 | 287840 |

Paths: `models/character/shared/{male,female}/move_and_ranged.mdl`. Existing decoded dumps were located at `E:/elysium-work/import/characters/character/shared/{male,female}/move_and_ranged.clips.json`. Each named clip's `timelines.events[label]` is `[]`, and `counts.events` is zero. Male standing/layer attacks have `cycleSeconds=1`; female dumps report 0.1, so the V5b prose's blanket 1-second assertion is not supported for female clips. Both reload clips report `65/30 = 2.1666666667` seconds. The witness uses the admitted male bank.

The stock and patch Elite_Hunter body each declare one local sequence and no local flame attack; the weapon bank is the relevant donor. `pipeline/src/elysium_pipeline/formats/mdl_skel.py::read_events` reads the same count/index pair. **The pipeline must continue emitting empty flame attack event arrays.** No synthetic 3031, full-corpus bake or pipeline item is authorized. Whether another presentation property needs future correction is outside this event-count finding.

### 3. Attack's NPC-owner arms: verified body; port comparison verified

These are conditional NPC-owner semantics, not a claim of ordinary NPC reachability:

1. `0x103e2f38` calls base Attack `0x10238580`; `0x103e2f41` clears jam `+0x89a` **before** owner resolution. Base first writes soonest-primary `+0x910 = now + mode rate`; its empty-clip arm tails to reload slot325 (`0x102385c8..0x102385d2`). Its remaining animation/action/bullet-set/next-attack catch-up work is player-gated (`0x102385d8..0x102385f2` and following asm). Do not invent an NPC `now + rate` write to +0x730 from this player block.
2. `0x103e2f47..0x103e2f50`: resolve owner `0x102521f0`; null returns with the preceding base/jam effects intact.
3. `0x103e2f56..0x103e2f87`: when held `+0x1519` is false, active `+0x1518` true, and stop time `+0x151c < now`, clear active and return **without debit**. Equality does not stop it. PrimaryAttack's activation window uses the PE float at `0x104629b8`, independently read as **0.75**.
4. `0x103e2f88..0x103e2f91`: decrement magazine0 `+0x74c` unconditionally for a non-null admitted owner. No player/NPC gate and no zero clamp. Result <=0 (`0x103e2f97..0x103e2fa7`) clears active and returns. Thus 1 -> 0 emits no new flame particle; directly calling this body at 0 can make -1.
5. `0x103e2fa8..0x103e302f`: positive remainder activates flame, gets owner slot369 direction, obtains cached weapon muzzle through `0x103e3d80`, gets owner slot199 velocity and adds direction times flame speed (`0x103e1680`). Initializes emission record at +0x14d4 (state1), +0x14d8 (0), position +0x14dc and velocity +0x14e8.
6. `0x103e3035..0x103e3091`: enabled +0x1550 branch. On no linked tail (+0x1510 == -1), resolve old flame_cluster through `0x103e3130`, mark it finished if live, create a new `flame_cluster` through `0x103e1d90`, and store its handle at +0x1530 or invalid on failure. Append/link a 60-byte record into the 50-row flame pool through `0x103e1c80`; preserve failure/return semantics of that allocator when ported. Disabled emission still retains the debit and emission-state writes.
7. `0x103e3097..0x103e30a5`: store engine frame-count result at +0x1514, also when emission is disabled. Attack does **not** itself deliver victim damage or play a shot sound. Its calling base Attack has no NPC next-primary catch-up block.

Damage is downstream of `0x103e32b0`: simulation/update `0x103e1bb0`, contact states and cluster update `0x103e2470`, target admission `0x103e3490`, damage `0x103e3570`, and periodic spatial overlap scan `0x103e3930`. The admission rejects null, missing `0x20002080` flags, entity flag0x40, unselectable and recent-hit targets. The damage body constructs burn damage, applies template scaling, dispatches TakeDamage, appends victim/time suppression and optionally BurnHitbox. Recent-hit entries expire only after the stored time plus 0.5; simulation still updates after emission stops. Debug draw `0x103e3c40` is not damage. These are neither generic bullets nor a damage-on-decrement transaction.

Sounds belong to separate control entries: PrimaryAttack asm `0x103e2ed7..0x103e2ede` plays Activate.wav once on a fresh positive-clip press; release slot265 `0x103e2e70` can play Deactivate and clears held; FireOnEmpty asm `0x103e3240..0x103e3288` conditionally plays Deactivate/Empty, clears +0x1520/+0x1519 and sends activity0xc4. No NPC sound event is supplied by the empty flame attack timelines.

**Current port:** registration/catalogue, range predicate, body activity translation, wield geometry, existing generic Shot, typed damage substrate and real reload are present. `FElysiumWeapon` has no specialized flame PrimaryAttack/Attack/frame/simulation/cluster implementation; generic queued player attacks currently cannot be credited as this body. `WeaponFrameUpdate`, `ShotFromAnimEvent` and the NPC no-timer guard in `ElysiumWeaponClasses.cpp` correctly distinguish generic NPC event commits. Merely adding a debit there would be a divergence. The full flame controller is future **0008-ranged-bottles, named flamethrower-controller follow-up**, with presentation joins to 0013/0014 and typed sound to 0011. No existing numbered 0008 story is falsely credited as owning that entire controller. This missing controller is not an input to the already landed reload body.

### 4. Shipped carriers: verified installed BSP and deployed script census

Read entity lump0 (offset/size at BSP byte8) in **209 loose BSPs** across both installed map directories, checking all entity blocks containing the classname. Results:

- **Stock and patch `hw_asphole_1`: `Hunter`, `npc_VHumanCombatant`**, `additionalequipment=item_w_flamethrower`, `stattemplate=HunterSafeArea2`, `models/character/npc/unique/Society_of_Leopold/elite_hunter/Elite_Hunter.mdl`, origin `-112 152 0`, spawnflags8196. This is a stock-game NPC carrier, not evidence that its AI can fire the weapon.
- **Patch-only `hw_chateau_1`: `flunky6` and `flunky7`, `npc_VVampire`**, same additional equipment, `BloodHuntPresence`, Copper/Serial_Killer models respectively. Keep patch additions distinct from the stock witness.
- `la_bradbury_2` in both directories and patch `la_bradbury_1` contain loose flamethrower items; those are not NPC carriers.

Deployed `Content/ElysiumCorpus/scripts` search found player weapon checks in `vamputil.py:1455`, a commented player/container restoration at 4504/4509 and player tooling catalogue entries in `zvtool/zvtool_pc.py:180,208`; no explicit classname-based NPC grant. This negative is limited to that deployed tree and literal classname search. It does not prove the absence of dynamically constructed script grants or additional package-only maps. One verified stock NPC already settles existence.

### 5. Empty clip and TASK_RELOAD: verified conditional chain; natural depletion unproved

There is **no proved natural Attack -> empty sequence for the Hunter**. Inventory_Insert `0x10334e70` equips an NPC with `max(Default_Size,1)`; the deployed flame item declares Default_Size0, Size250 and ammo type MediumRound. Its ordinary initial clip therefore is **1**, not 0 or 250. A silent ranged program cannot consume that 1.

For an actually empty weapon, weapon slot365 `0x1024f670..0x1024f67f` returns `COND_NO_PRIMARY_AMMO 0x40` before distance/facing gates, consumed by GatherAttackConditions `0x1026de87`. **CheckAmmo slot565 `0x101a6ca0` is a literal no-op**, including on the human carrier; it is not the producer. Existing ranged schedule texts permit interrupt0x40.

Selection pre-pass `0x102b8620` first offers fake reload when enabled and fake count<1; only then loaded clip returns0. At `0x102b86e5`, clip<=0 plus weapon slot280 `0x10253ab0` admission and owner GetAmmoCount `0x103346c0` >=1 selects **0xc2** with SEE_ENEMY or NEW_ENEMY, otherwise **0xc3**. Do not skip fake-reload priority. `SCHED_TROIKA_HIDE_AND_RELOAD1` (0xc2) authors find cover/move/remember/SET_SCHEDULE0xc3 and fail schedule **RELOAD**; 0xc3 authors FACE_ENEMY/TASK_RELOAD. Generic `RELOAD` authors STOP_MOVING/TASK_RELOAD. Troika TranslateSchedule `0x102b12f0` and base `0x102cc080` do not remap generic RELOAD to0xc3: cover failure can run generic RELOAD directly.

TASK_RELOAD start `0x102842cf..0x102842d3` restarts ideal ACT_RELOAD. Run `0x102890f3..0x102891b9` moves/turns and waits for activity finished; then writes weapon bInReload (`0x1028918d`), calls **weapon slot322** (`0x1028919d`), clears0x40/0x41 and completes in that order. Bulk slot322 `0x102551e2..0x1025523e` requires live owner combat pointer, bInReload and owner NextAttack<=now; admitted dispatch calls **weapon slot323** at `0x10255219`, then stamps both weapon attack times=now. Equality is admitted; greater or unordered is refused. Slot323 `0x102552c0` adds min(capacity-clip,reserve) per represented clipped magazine, debits reserve only for a player, and clears reload/jam/interrupt flags. NPC single-round slot322 returns at the player-pointer gate; the flamethrower is the bulk witness.

## Ruling and complete bounded lane brief

**Do it now.** The item is real TASK_RELOAD getting a real NPC run. An explicit empty-magazine initial condition is a controlled same-arm witness, not a claim that retail humanoid firing emptied it. It keeps the owner-required flamethrower, real body/weapon, actual condition producer, actual schedule selection, real animation clock and actual reload functions. Nothing in the owner's quoted requirement makes an invented NPC firing loop lawful. This sitting decisively rejects V5b's classification of empty events as a pipeline/content blocker and rejects arm-only acceptance. The record's title/about must disclose its staged empty initial condition. Do not call it an attack/exhaustion proof.

No new gameplay function needs porting for this witness: V5b already ports the required predicate/pre-pass/start/run/slots280/322/323. This lane delivers their live verification and any **demonstrated single divergence** in that recovered chain; no speculative rewrite. A future flame-controller port must implement its complete callable retail arms above, not a debit fragment. It is not pulled forward as a dependency of this record.

### Exact file manifest

- `Arena/scenarios/combat/ranged_real_reload.json` — new record below.
- `Arena/scenarios/combat/ranged_flamethrower_silent.json` — the separate loaded-clip negative control in job5; same donor/stage/seed, clip1/reserve250/fake count8, requires ordinary ranged-task completion and unchanged clip/reserve, forbids shot events and real reload.
- `Arena/README.md` — explicit initial-state fixture and observation schema, separate from retail keyvalues.
- `docs/specs/0002-npc-ai/stories/wave2/seam.md` — new read-only reload observation kind/text.
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.{h,cpp}` — strict parsing of `initial_weapon_state` and `reload` kind.
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.{h,cpp}` — apply initial state once, post-equip/activation and before first think, fail admission explicitly, restore no global tunable because none is changed.
- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.cpp` — guarded read-only observations at existing FinishReload/FinishReloadBulk boundaries; retain bodies/ordering.
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseRunTask.cpp` — observation surrounding the existing write/slot322 call, after-call condition-clear observation if needed.
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`, `ElysiumNpcKernelRunTaskTests.cpp`, `ElysiumNpcKernelCombat10Tests.cpp` — existing relevant arm tests and the composed regression controls below.
- `docs/vtmb/combat-and-damage.md` — retail recovery: separate hypothetical NPC-owner Attack semantics from ordinary human reachability; correct the content-gate conclusion and female duration assertion. No implementation narrative in the oracle. V5b acceptance/result notes belong in `docs/specs/0002-npc-ai/stories/v5/README.md`.

All unqualified C++ test filenames above live under `Source/ElysiumUE/Private/Tests/`. No pipeline, item data, sequence data or assets are changed. A measured divergence needing a file outside this manifest is not authority to silently expand it: identify its already recovered body and bound the additional work before depending on it.

### Numbered jobs

1. **Admit the existing male donor.** `regular_cop`, npc_VHumanCombatant, male move_and_ranged bank, its existing A_flamet_reload (65/30s), attack/layer empty timelines and w_m_flamethrower wield SK/DA. Report the actual model/asset/native sequence mapping, not a content-name guess. Missing required existing payload is stage failure. Use the shared admission barrier and third-sitting fresh-map epoch1.0; do not create a second clock policy here or compensate the seed. Measure final deadlines at that epoch. No new bake is presently evidenced.

2. **Explicit initial-state fixture, not map keyvalues.** Add arena-only root `initial_weapon_state` entries with `who`, `weapon`, `magazine`, `reserve`, `fake_reload_count`. Resolve the already equipped active FElysiumWeapon for the named NPC; require matching classname, ammo type/record, bulk clipped magazine, unique entry and nonnegative integer values. `reserve` is assigned through the real Inventory reserve store for the record's ammo type. `fake_reload_count=8` is an explicit fixture ensuring `0x102b8620`'s first-priority arm does not hide the real reload arm; no debug_allow_fake_reload change. Apply after ordinary equip's max(Default,1) write and before the first NPC think, at the ready boundary, with trace sink already installed. These writes establish the test's initial state once; after that only ordinary runtime writers act. Never set conditions, enemy, schedules, activity-finished or bInReload to drive success. A missing actor/item/body, late application, wrong active item or absent reserve store fails staging instead of fabricating it. Record both actual post-equip clip1 and fixture clip0 so provenance cannot be hidden.

3. **Observe the landed retail bodies.** Add trace kind `reload`, emitted for the NPC owner, guarded by HasAiTraceSink. Emit deterministic text at initial admission, slot322 entry/admission/refusal, slot323 entry/exit and post-322 stamp point. Include item classname/handle, magazine before/after, reserve before/after, live owner NextAttack, now, both weapon stamps and three flags. Observers only append; no queues, callbacks, RNG draws or forced state. Test text can use compact phase tokens below, but numeric flags/stamps must come from the actual objects. `bulk_commit` occurs after slot323; `finish_stamps` after +0x730/+0x734 assignment; taskdone remains the existing real taskdone tap. Do not report a generic BeginReload/CommitQueuedReload as slot322/323.

4. **Run selection through animation and commit.** Stage the record below with fake count8, clip0, reserve250, no squad/compatible cover hint group. SEE_ENEMY/NEW_ENEMY should choose0xc2 via `0x102b86e5`; with no compatible cover route its authored fail schedule is generic RELOAD. If the actual existing arena geometry supplies a valid cover route, expect0xc3 and prove its move/remember handoff instead; the supplied record admits either exact retail continuation by asserting TASK_RELOAD and real transaction observations after0xc2. Do not alter selectors or navigation to force a branch. Require NO_PRIMARY_AMMO from slot365, male flamet_reload sequence, activity-finished, slot322, slot323, stamp writes, condition clear and task completion. Starting an animation or incrementing WeaponFinishReloadCalls alone is insufficient.

5. **Prove what this record excludes.** After completion the NPC may resume a silent flamet_attack; no 3030..3044 event, generic shot damage or flame emission should appear. Post-run clip250/reserve250/flags0 must hold through ordinary thinks. The record never asserts an attack debit. Retain the existing pistol sustained-fire/fake-reload controls and add a separate loaded-flamethrower control with clip1/reserve250/fake count8: normal ranged tasks finish, empty event timelines stay empty, clip stays1, no real reload. This is the negative reachability control, not a same-arm replacement for the reload record.

6. **Close on measurements.** Run relevant weapon/reload/run-task/combat10 arms and the new record; then the required story-close arm tier. Prove the new record alone, after control_sequence and after ranged_fake_reload in one boot, and verify those existing controls retain their assertions. Publish real trace/provenance/results and update V5b acceptance only after the live record passes. The by/within bounds below are initial budgets, not measurements from this sitting: revise only a demonstrated staging/time assumption with the retail predicates unchanged, never widen to cover an unexplained failure. A green arm suite cannot waive a red/missing live record.

### `ranged_real_reload` record

The following is the complete record contract, including the explicitly authorized schema extension. `reload` texts are observations from job3, with checked invariants encoded in the text by reading actual values; predicates must not synthesize a successful trace when an invariant is false. The phase strings use `item=...`, `clip=...`, `reserve=...`, `flags=...`; equality tokens such as `stamps=now` may be emitted only after comparing actual stamps to the actual callback time. `clip_before=0 clip_after=250 reserve_before=250 reserve_after=250` is the exact bulk min arm, not an estimated animation result.

```json
{
  "name": "ranged_real_reload",
  "about": "A live flamethrower NPC starts with an explicitly staged empty clip and reserve250. Weapon slot365 0x1024f670 raises0x40; pre-pass0x102b86e5 selects real reload; TASK_RELOAD0x102842cf/0x1028918d..b9 reaches weapon slots322/323 0x10255050/0x102552c0. This proves live reload, not natural flame attack depletion.",
  "stage": "arena",
  "seed": 1,
  "duration": 30.0,
  "player": { "at": "cover_seat" },
  "cast": [
    {
      "name": "arena_flame_reloader",
      "classname": "npc_VHumanCombatant",
      "at": "far_ne",
      "face": "player",
      "body": "regular_cop",
      "keys": {
        "stattemplate": "HunterSafeArea2",
        "npc_perception": "3",
        "spawnflags": "4",
        "StartHidden": "0",
        "additionalequipment": "item_w_flamethrower",
        "player_reaction": "D_HT 5",
        "hint_groups": "32",
        "stay_entrenched": "0",
        "allow_kick_hint_use": "0",
        "squadname": ""
      }
    }
  ],
  "initial_weapon_state": [
    {
      "who": "arena_flame_reloader",
      "weapon": "item_w_flamethrower",
      "magazine": 0,
      "reserve": 250,
      "fake_reload_count": 8
    }
  ],
  "script": [],
  "expect": [
    { "label": "initial", "who": "arena_flame_reloader", "kind": "reload", "match": "initial item=item_w_flamethrower clip=0 reserve=250 fake=8", "by": 0.1 },
    { "label": "empty", "who": "arena_flame_reloader", "kind": "cond+", "match": "NO_PRIMARY_AMMO (0x40)", "within": 6.0 },
    { "label": "real_selection", "who": "arena_flame_reloader", "kind": "schedule", "match": "\\(0xc2\\)$", "regex": true, "within": 6.0 },
    { "label": "reload_task", "who": "arena_flame_reloader", "kind": "task", "match": "task_reload (", "within": 15.0 },
    { "label": "reload_body", "who": "arena_flame_reloader", "kind": "sequence", "match": "flamet_reload", "within": 2.0 },
    { "label": "finish_enter", "who": "arena_flame_reloader", "kind": "reload", "match": "finish_enter item=item_w_flamethrower clip=0 reserve=250 in_reload=1", "within": 4.0 },
    { "label": "bulk", "who": "arena_flame_reloader", "kind": "reload", "match": "bulk_commit item=item_w_flamethrower clip_before=0 clip_after=250 reserve_before=250 reserve_after=250 flags=0", "within": 0.2 },
    { "label": "stamps", "who": "arena_flame_reloader", "kind": "reload", "match": "finish_stamps item=item_w_flamethrower stamps=now", "within": 0.2 },
    { "label": "cleared", "who": "arena_flame_reloader", "kind": "cond-", "match": "NO_PRIMARY_AMMO (0x40)", "within": 0.2 },
    { "label": "done", "who": "arena_flame_reloader", "kind": "taskdone", "match": "^task_reload$", "regex": true, "within": 0.2 },
    { "label": "final", "who": "arena_flame_reloader", "kind": "reload", "match": "final item=item_w_flamethrower clip=250 reserve=250 flags=0", "by": 30.0 }
  ],
  "never": [
    { "who": "arena_flame_reloader", "kind": "schedule", "match": "\\(0xc[456]\\)$", "regex": true },
    { "who": "arena_flame_reloader", "kind": "animevent", "match": "^30(3[0-9]|4[0-4])( |$)", "regex": true },
    { "who": "arena_flame_reloader", "kind": "reload", "match": "finish_refused" },
    { "who": "arena_flame_reloader", "kind": "reload", "match": "bulk_commit", "at_most": 1 },
    { "who": "arena_flame_reloader", "kind": "cond+", "match": "NO_PRIMARY_AMMO (0x40)", "after": "done" },
    { "who": "arena_flame_reloader", "kind": "death" },
    { "who": "player", "kind": "damage" },
    { "who": "player", "kind": "death" }
  ],
  "probes": [
    { "at": "end", "who": "arena_flame_reloader", "probe": "alive", "equals": true }
  ],
  "notes": "Fixture applied once after equip before first think. Default NPC equip was clip1 (0x10334e70); empty0 and reserve250 are controlled initial state. Male reload65/30s is the real sequence clock. c2 continues via successful cover c3 or its authored generic RELOAD fail schedule. No event/Attack/timer is fabricated. Final observation is emitted before end evaluation at duration30. Actual live trace establishes timing; this sitting performed no run."
}
```

Job3/runner must additionally emit `final` before evaluation finishes and fail if the entity/weapon no longer resolves. The ordered reload-body expectation requires that the existing `sequence` tap actually carries the resolved label; add label provenance to that read-only tap in `ElysiumNpcBaseRunTask.cpp` if absent, without making an extra sequence change. A task start/sequence switch sharing one timestamp still requires the real insertion order. If condition0x40 first appears in the same think as selection, keep that actual order; don't defer a tap to make the JSON match.

Citations for expect/never: initial fixture is explicitly non-authored state, justified by stock equip `0x10334e70`; empty condition `0x1024f670`; real vs fake selection `0x102b8620` with `0x102b86e5`; schedule continuations are the deployed `.sch` texts named in finding5; animation/start `0x102842cf`; finish entry/clear/taskdone `0x1028918d..0x102891b9`; bulk/stamps `0x102552c0`, `0x10255219..0x1025523a`; silent-event prohibition is the raw MDL table and `0x10238160`/`0x102383b0` entry distinction. The death/damage exclusions keep the donor/target available throughout; they do not prove the future flame-damage service.

### Arm tests and admission controls

Retain V5b tests; add only missing composed coverage and meaningful observer/staging controls:

1. Weapon slot365: clip0/-1 gives0x40 even with deliberately failed range/facing; positive clip follows later gates (`0x1024f670`). CheckAmmo remains no-op (`0x101a6ca0`).
2. Pre-pass priority: fake count0/enabled defeats real-empty selection; fake count8/clip0/reserve>0 admits0xc2 with either SEE or NEW,0xc3 with neither; reserve0 declines real reload (`0x102b8620`, slot280 `0x10253ab0`). Do not assert that empty always forces real reload.
3. Actual RunTask composition: unfinished real sequence makes no reload call/writes; finished bulk sequence sets bInReload before slot322, fills clip by slot323, retains NPC reserve, stamps both attack words, clears0x40/0x41 and then completes (`0x102842cf`, `0x102890f3..0x102891b9`). Test real methods, not an override that counts calls and simulates the outputs.
4. Slot322 live owner deadline: less/equal admission and future/NaN refusal; no owner/noncombat/no bInReload refusal; NPC single-round keeps bInReload (`0x10255050` asm). Confirm a refused finish can still be followed by the task's own clears/completion; observers must not imply a bulk commit there.
5. Slot323 min arithmetic: reserve smaller/equal/larger than missing capacity, unclamped negative missing capacity, null-owner no flag clear, valid NPC reserve retention and all three flags cleared (`0x102552c0`). Magazine1 remains the explicitly named absent no-clip input, not borrowed magazine0 state.
6. Initial fixture refusal: actor/item mismatch, apply-after-first-think, missing ammo type, duplicate fixture, noninteger/negative values, wrong host. Successful initial state is visible in real readers and applied only once. Observations report actual refusal and mismatched values rather than printing nominal success strings.

**No modernization is named or authorized.** The deliberate initial test state is disclosed staging, not a gameplay change. The unanswered question of a live retail flame firing demonstration remains explicitly unmeasured; no downstream implementation or acceptance depends on pretending it was measured. The owner's reload live run is delivered by this bounded same-arm witness, and remains outstanding until that run actually passes.
