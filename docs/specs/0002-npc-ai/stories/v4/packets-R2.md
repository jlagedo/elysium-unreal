# V4r — packet R2: the event chain, the attack producers and the small bodies

Reader R2, 2026-10-04, `spec-0002/step-2`. No code, no build, no run. Every claim carries its
address. **(L)** read in the listing or the decompilation this session; **(D)** taken from
`docs/vtmb/` and cited, not re-read; **(I)** inferred, with what it rests on.

Doc sections written: `animation_events.md` § "Who calls the dispatcher, and where against the frame
advance" (new) and § "Overlay layers…" (per-layer body, who pushes a layer);
`combat-and-damage.md` § "The NPC attack producers, start to commit" (new); `npc-ai/shape.md`
§ "The activity commit" (`RunAnimation`'s pick, which finish a task reads, `AutoMovement`);
`npc-ai/senses.md` § "Cone" (the three inputs per class); `animation_and_movers.md` § the
disposition stance machine (`SetDisposition` arm by arm).

## Corrections to README §1 (the coders' briefs quote it)

- **`RunAnimation`'s gate is `m_Activity (+0xfec) == 1`, not `m_IdealActivity`**, and the pick's
  argument is `m_TranslatedActivity (+0xff4)`, not `m_Activity` (`0x1026c540`: `param_1[0x3fb] == 1`,
  `Select…(param_1[0x3fd])`) (L).
- **Slot 247 is live on every Troika NPC** (item 5). README §8 Q4 goes to the judge.
- **`SetDisposition` is not "gated on `m_bDisableAI`"**: the flag gates the immediate commit only
  (item 7).
- **Slot 15 in `0x10090c80` is the ENTITY's slot 15**, `CBaseEntity::SetAttackExtents 0x1009af40`;
  the motor's slot 15 (`0x102e2180`, R1's) is another table.
- **K3's premise is half wrong**: retail props never dispatch (extension 1).
- `docs/vtmb/player-entity.md` § "Recovered `PostThink` body" says `→ Weapon_FrameUpdate`; the call
  is slot 312 `UpdateCharacter` and the player never calls `Weapon_FrameUpdate`. Not my file: for
  its owner.

## 1. The dispatcher

- **Order** (L): `0x10098c80` calls the base `0x10091880`, then `0x10098cd0` on layers 0..3
  (`+0x734`, stride `0x30`).
- **Base writes** (L): `m_bSequenceFinished (+0x65c) = 0`; `flEnd = m_flCycle + 0.1 (0x104491b4) ×
  GetSequenceCycleRate × m_flPlaybackRate`; non-looping (`m_bSequenceLoops +0x65d == 0`) and a
  seqdesc: `flEnd >= 1 || flEnd < 0` → finished = 1, `flEnd = 1.0`, else `m_fSequencePastHalf
  (+0x568) = flEnd > 0.5`; looping: the same without the clamp, plus the start wrapped into `[0,1)`;
  `m_flLastEventCheck (+0x658) = flEnd`; events `< 5000` in `[start, flEnd)` or, with
  `seqdesc.flags & 1` and `flEnd >= 1`, `< flEnd − 1`; each to the HANDLER's slot 259 (`+0x40c`);
  `eventtime = (cycle − m_flCycle) / rate + m_flAnimTime`. The interval argument is unused.
  With no seqdesc on a non-looping sequence neither the finish nor past-half is written.
- **`OnSequenceFinished`** (L): a direct call `0x10091b9a → 0x10091c80`, not a slot; rising edge
  (flag set now, clear at entry).
- **Per layer** (L): zeroes `layer+4` and never sets it; no past-half; `layer+0x2c` = the layer's
  look-ahead end; no clamp; no "in use" or weight test; `eventtime` from the owner's `m_flAnimTime`.
- **`StudioFrameAdvance 0x1008f120`** (L) only SETS the flag (cycle left `[0,1)`), writes past-half
  from the real cycle, the two speed words, and its own rising-edge `OnSequenceFinished`.
  `ResetSequenceInfo` clears (D).
- **Think order** (D, `schedule-kernel.md` :4825-4904, `0x10292de0` / `0x1026ca80`): `RunAI` →
  `PostRun` → `PerformMovement`, then (Troika) slot 312 `UpdateCharacter` when due.
- **(a) What slot 251 reads**: `IsActivityFinished 0x10272900` = `m_bSequenceFinished && m_nSequence
  == m_nIdealSequence` (L). In a `RunTask` arm the flag is **the dispatcher's look-ahead value from
  the previous think's `PostRun`** — true 0.1 s of clip time before the pose ends — unless a
  `ResetSequenceInfo` cleared it since. An arm that calls `AutoMovement` first advances the clock
  itself and can add a true, never remove one. Within `PostRun`, `RunAnimation`'s own slot-251 test
  reads `StudioFrameAdvance`'s value OR'd onto the previous look-ahead.

## 2. `Weapon_FrameUpdate` `0x1032aa40` (L)

One caller, `PostRun`. Active weapon's slot 369 (`+0x5c4`) with the wielder; every weapon class
holds `CBaseCombatWeapon 0x1024efa0` there (checked on `CWeaponMelee`, `CWeaponRanged`): weapon
`StudioFrameAdvance(0)`; if `finished && loops`, `SelectWeightedSequence(m_Activity +0x8a8)` →
`m_nSequence`, `ResetSequenceInfo`; weapon slot 258 `(interval, wielder)`. **(b)** For an NPC it is
the world-model weapon's animation clock, with its events delivered to the NPC's `HandleAnimEvent`.
No fire, no sweep. The player never calls it.

## 3. The NPC attack producers

**(a) Ranged** (L unless marked). Start `0x102a4505`: burst count only (D). Run `0x102ab0a9` (D) →
`0x102aaa60` (L): `m_flLastAttackTime = curtime`, `RestartIdealActivity(0x19)` (hint types 100 /
0x65 / 0x27d8: `0x1114` / `0x110f` / `0x1120|0x111f`). Event 3030..3044 → `CWeaponRanged
0x10238160` → `0x10238320` → `ModeDispatch(1) 0x102383b0` → slot 373 `Shot 0x102387b0`.
`ModeDispatch`'s only other entries are `PrimaryAttack 0x102382f0` / `SecondaryAttack 0x10238350`
(mode 0 → slot 372 `Attack`, the start). NPC code calling a weapon's slot 326: the Troika melee arm,
`CNPC_VBach::StartTask 0x103645a0`, `CNPC_VManBat::RunTask 0x1038d130`. **No retail path fires a
Troika human's shot without the event.** The port's `ContactEventCycle` estimate
(`ElysiumWeaponClasses.h:303`, `.cpp:1263-1317, 2166`) has no retail counterpart on the NPC path:
remove it. One more producer C1 must know: the move-and-shoot overlay `0x102e8560` fires from a
LAYER's 3031 (item 4). *Not read:* the species weapon bodies (`0x103ed200`, flamethrower, thrown).

**(b) Melee** (L). Start arm `0x102a45c6` (D for the arm, L for the `+0x518` call in
`0x102a1910`): weapon `+0x5a0 & 0x18000` → `m_flLastAttackTime`, weapon slot 326 `PrimaryAttack`,
`AutoMovement`. `CWeaponMelee::PrimaryAttack 0x103eaca0`: owner in a grapple → return; no player
owner → slot 372 `RequestActivity(0x4b, 1, 1)` `0x103e9e00` → the swing sequence on the owner via
slot 311 (`+0x4dc`), playback rate, `m_flNextAttack`. Contact: `UpdateCharacter 0x103246d0` →
`MeleeSwingUpdate 0x10346cd0` (D), reached from the Troika think's tail `0x1029365b` — after
`PostRun` and `PerformMovement`, on the update clock, not every think. The port sweeps from the
world interaction tick (`ElysiumEntityWorldInteraction.cpp:299-326`): the retail site is the NPC's
own slot 312.

**(c) `melee_swing`'s `hit_event` is a record error.** The male `baseball` bank authors events on
four stealth-kill clips only (4050/4051, 5118); no attack clip has any (the staged
`character/shared/male/baseball.clips.json`). Retail produces contact with no anim event. The
record should expect a `damage` on the player after `task_melee_attack1` (it already probes
`health < 100`) and drop the `animevent`. Its `about` is also wrong ("the clip's hit event reaches
HandleAnimEvent"). `ranged_open_fire`'s `shot_event` matches `""`; it should match `3031`.

## 4. Overlay layers on NPCs (L)

Pushers: `AddGesture 0x100991b0` ← `0x10099250` (TASK 0xe4 `0x102a5046`; the discipline applier
`0x101de660`), `RestartGesture` slot 273 `0x10099570`, **`0x102e8560`**, `0x10397930` (MingXiao);
`AddGestureSequence` ← the scene entity `0x10081510`. `AddFlinchGesture 0x10099690` writes
`m_Flinch`, not a layer (D).
**(d) On step-2 paths: the move-and-shoot overlay, and only it.** `RunTaskOverlay 0x10289c90` →
`0x102e8560` on a continuous-move task when slot 575 passes (enemy, flags2 `0x400`, weapon
`& 0x6000`, `CAP_MOVE_SHOOT`): `AddGesture(TranslateActivity(0x1a))`, `Weapon_SetActivity(0x19)`,
plus the `0x47`/`0x48` gestures. A gunman running to cover or establishing line of fire with
`COND 0x4f` can reach it (`cover`, `range_bands`); the port counts the call
(`ElysiumNpcBaseMaintain.cpp:105`) and pushes nothing. The standing `ranged_open_fire` and the
patrols push none. A1's "no layer" seam is correct only while that overlay stays a counter.
*Unrecovered:* slot 273's 23 callers were not walked.

## 5. Slot 247 `0x10090c80` (L)

Slot 15 (`+0x3c`) = `CBaseEntity::SetAttackExtents 0x1009af40` (collision `0x100dc220` + `+0x50`).
`GetFlags2` reads `m_fFlags2`; **`CAI_BaseNPCTroika::Spawn 0x10298d30` calls `AddFlag2(4)`**.
`RemoveFlag2(4)`: `CPayphone`, `CNPC_VHengeyokai`, `CNPC_VMingXiaoTentacle`, `CNPC_VTzimisce`,
`…HeadClaw`, `…Runner` (Spawn and vfunc130), `CNPC_VWerewolf`. **(e) Live on every Troika human**:
each `ResetSequenceInfo` re-derives the attack extents from the sequence bbox. Whether a step-2
record observes it is the judge's question (Q4).

## 6. Slot 363's inputs (L)

Slot 192 = `0x10027160` on NPC and player: the collision box centre in world space. Slot 29: Troika
`0x101aa630` → `m_flStealthVisionCone +0x63c8`, no writer in the image → 0 (I); player `0x1034f390`
→ `+0x1c74`. **(f)** `CBasePlayer::Spawn 0x1016d260` writes `m_flFieldOfView = 0.5`; slot 363 on
`CHL2_Player` is the base `0x10326750`. Port home for A3: the player's spawn.

## 7. `SetDisposition 0x102c0f70` (L)

`old = +0x64d4`; lookup `0x100ec530`, miss → `("Neutral", 1)` with `old = -1`; tuning writes always
(`+0x64d8`, `+0x6584/8`, `+0x5b94`, `+0xe3c`, `+0x10b4`, `+0x64d0`, `+0x10b8`); if the index
changed: `seq = old == -1 ? slot 611 (0x102c12a0) : GetTransitionAnim 0x100ed150`; if `seq >= 0`:
`m_IdealActivity = 0xf1`, `m_nIdealSequence = seq`; and if `!m_bDisableAI`: `m_nSequence = seq`,
`m_flCycle = 0`, `m_Activity = 0xf1`, `m_flAnimTime = curtime`, `ResetSequenceInfo`.
`GetTransitionAnim`'s fallbacks stay (D). `RunAnimation`'s pick: `shape.md`.

## 8. `AutoMovement`'s `StudioFrameAdvance` (L)

Retail: `0x10280a50` calls slot 250 first, unconditionally. The port's call is right. A second
advance in one tick is inert (`dt = 0` fails `dt > *0x1044f020`), so the first caller in the think
owns the advance. *Not read:* the constant's value and the early-out's return.

## Extension 1 — anim events off the NPC chain (K3)

**(g)** Call sites of slot 258 — the whole image (L): `PostRun`; `CBasePlayer::PostThink
0x1016be10` (twice); weapon slot 369 `0x1024efa0`; `CCameraAnimated`'s think `0x10071840`.

- **Player**: `PostThink`: slot 250 `(0)` → `0x101600a0` → slot 258 `(interval, this)` → slot 312
  `UpdateCharacter`. Handler `CBasePlayer::HandleAnimEvent 0x10178a10` (L): gate `!IsObserver &&
  source == this`; 4050, 4051, 2060 itself; 2050..2053 swallowed; the rest →
  `CBaseCombatCharacter::HandleAnimEvent 0x1032e330` (weapon band, 4006/4007, 4020, 4100..4102).
  Slot 258 is the overlay body, so the player's layers dispatch too.
- **Props: none.** `CDynamicProp`'s think `0x10190850` advances and never dispatches; no other prop
  think does. Retail delivers no server anim event from any prop.
- **Camera**: `0x10071840`, 10 Hz, advance then dispatch then the finish test — so the camera ends
  on the look-ahead finish. Base dispatcher, `CBaseAnimating::HandleAnimEvent 0x10091da0` (2070,
  2071, 4005, else `DevWarning`).
- **NPC-observable events from a non-NPC**: yes, both the player's — 3031 (the shot: damage and
  sound conditions) and 4006/4007 (the feed transaction on the paired victim).

**The port's poll and its consumers**: `FElysiumEntityWorld::AdvanceAnimEvents`
(`ElysiumEntityWorld.cpp:2077-2100`, called `:1938`, every entity with a skeletal body);
`FElysiumAnimating::AdvanceAnimEvents` (`ElysiumAnimatingImpl.cpp:310-396`; channels Base and
UpperBody `:41-42`). Handlers reached: `FElysiumPlayer` (`ElysiumPlayerEntity.cpp:593`),
`FElysiumCombatCharacter` (`ElysiumCombatCharacter.cpp:2103`), the NPC chain
(`ElysiumNpcBaseMisc2.cpp:204`, `ElysiumNpcMisc2.cpp:385`, `ElysiumNpcMisc2Species.cpp:192-496`,
`ElysiumNpcTzimisceRunner.cpp:243`), `FElysiumNpcCamera` (`ElysiumNpcCamera.cpp:390`), props through
the `FElysiumEntity` default (`ElysiumEntity.h:422`). Dependents: the camera think's comment
(`ElysiumCameraAnimated.cpp:251-253`), the feed timeline (`ElysiumFeed.cpp:1158-1164`), the weapon's
clip precondition (`ElysiumWeaponClasses.cpp:1259-1282`), `ElysiumEntityBodies.cpp:141`.
What each needs: NPCs — A1. Props — nothing: delete, retail has no dispatch. Camera — the dispatch
inside `FElysiumCameraAnimated::Think` and its finish from the look-ahead (today a time compare,
`:256`). Player — a `PostThink` site and a sequence clock; **the port has no `FElysiumPlayer::
PostThink` and no kernel sequence words for the player** (its clock is the pose layer's phase,
`TickStealthKill` and the step clock run from the world tick `:1921, :1940`).

## Extension 2 — the weighted pick outside the kernel (K4)

Retail (L): every pick is `SelectWeightedSequence 0x1008dc40` → `0x10427fc0`, `RandomInt(0,
total−1)` on the one shared engine stream (`*0x1070b244` slot 2); all-zero → uniform on the same
stream. 54 direct callers. **(h)** `PickWeighted` has one production call,
`TryActivity` (`ElysiumAnimationResolve.cpp:187`); what differs is who sets `Variant`:

| port site | variant | retail body |
|---|---|---|
| `FElysiumNpc::PlayActivity` `ElysiumNpc.cpp:1124` | `ScheduleActivityCycle++` | `ResolveActivityToSequence 0x10272130` |
| `FElysiumNpc::SequenceForActivity` `ElysiumNpcAnim.cpp:376` | 0 | the same, and `RunAnimation 0x1026c540` |
| `FElysiumNpc::StartWalkingAnimation` `ElysiumNpc.cpp:529` | `Handle.Index` | the navigator's movement activity → `0x10272130`; the motor's own draw `0x10264680` |
| `FElysiumCombatCharacter::PlayReactionActivity` `ElysiumCombatCharacter.cpp:2025` | the `Reaction` stream | `AddFlinchGesture 0x10099690`, `PlayerKnockbackReaction 0x101606e0` |
| `FElysiumWeapon::BuildActivityClipRequest` `ElysiumWeaponClasses.cpp:1077` | `Handle.Index` | player `0x101644f0` / layer `0x1015fbb0`; NPC `0x10272130`; the weapon model `0x10253390`, `0x1024efa0` |
| the locomotion intent `ElysiumAnimationIntent.cpp:538` | the caller's | player: Heaviest (`0x101644f0`), no draw; cast: `0x10272130` |
| prop random animator (port `ElysiumProp.cpp:136`) | — | `0x10190850` `SelectWeightedSequence(ACT_IDLE)`; `CBaseProp::Spawn 0x1018df70` |

Pass-throughs, not picks: `ElysiumAnimationDriver.cpp:498` (a cache key), `ElysiumAnimSubsystem.cpp:
826`, `ElysiumAnimationResolve.cpp:801, 851`. `ElysiumGrapple.cpp:40` is Heaviest. **No pick
without a retail counterpart was found**; the hash seed itself and the driver's variant-keyed cache
are the port-only parts. *Not walked:* each of the 54 retail callers against a port site.

## (i) The arena records

- **`anim_prop_event`: leave it out, or state it as a `never`.** Retail delivers no prop event, and
  the staged clip tables agree there is nothing to deliver: scenery 284, gibs 19, editor 10,
  worldcraft 2 and the cinematic lane author **zero** events. The one non-character, non-viewmodel
  carrier is `models/items/walkie_talkie/walkie_talkie.mdl` (4100 @ 0.225 on
  `Crooked_Cop_Walkie_Talkie_Into`, 4100 @ 0.0523 on `…_Loop`, 4101 @ 0.0 on `…_Outof`) — a
  character ornament bank, in no map sidecar under `exports_v2/_sidecars` (I: the sidecars may not
  list every entity row). No retail map row exists to copy.
- **`anim_player_footsteps`**: the walk clips carry 2050 / 2051 (run: 2052 / 2053) (D); retail
  dispatches them to the player and the handler swallows them — the sound is the step clock's.
- **`anim_player_weapon_event`**: firearm **3031**, on the `*_attack_layer` clip in an overlay
  layer, through `0x10098cd0` → `0x10178a10` → `0x1032e330` → `0x10238160` → `Shot`, before the
  damage. Melee: **no event** — the sweep in slot 312 right after the dispatch; the record should
  expect none. 4050/4051 are the stealth-kill clips' camera pair, not a weapon event.

## (j) For the judge, and unrecovered

- Slot 247 is live (item 5): Q4, the bbox.
- The player on the kernel dispatcher needs a player `PostThink` and sequence clock (the player
  entity / 0015's layer 0). The melee sweep's retail site (slot 312 in the think tail) and the
  move-and-shoot overlay's gesture need the overlay stack on NPCs (0015).
- Unrecovered: `0x1044f020`'s value; `StudioFrameAdvance`'s early-out return; slot 273's callers;
  species weapon fire bodies; `0x1018e330`'s class (`CActivityCopyProp` by its body, I); the walk
  clip's event cycles on the PLAYER's model (cited from the cast's banks).
- Queries over 10 s: none observed.
