# Story 8 — family Misc19, walked (lane L11)

Walked prose for the `rule` bodies of family Misc19 over 64 bytes (spec 0019 story 8, pass I). Pass C
folds these sections into `schedule-kernel.md` / `conditions-and-states.md` / `lifecycle.md`. Source
of truth: `research/npc-kernel-checklist/families-19-29/Misc19-READING.md` and the listings it cites.
Port bodies: `ElysiumNpcEnemy.cpp`, `ElysiumNpcBaseMisc19.cpp`, `ElysiumNpcMisc19.cpp`,
`ElysiumNpcMisc19Species.cpp`, `ElysiumLaw.cpp`.

## `SetEnemy` `0x10279a50`

Reads `m_hEnemy` (`+0x5ce0`) and resolves it; `-1` (`0x10279a63`) or a stale serial (`0x10279a7d`)
is a null old enemy.

1. When the resolved old enemy differs from the argument (`0x10279a8b`): only a LIVE old handle
   (`0x10279a96`, `0x10279ab2`, `0x10279ab7`) reaches `0x10279b70`, which writes the old enemy's
   handle into `m_hLastEnemy` (`+0x1a94`); then slot 560 `ClearAttackConditions` runs on EVERY
   change, a change from null included (`0x10279aed`).
2. A non-null argument writes its handle into `m_hEnemy` (`0x10279b01`) and runs the discipline
   manager's break-on-notice sweep `0x101e3d70(&DAT_10739a4c, this)` (`0x10279b0c`) — on every
   non-null write, an unchanged enemy included. A null argument writes `-1` (`0x10279b16`).

`0x10279b70` is `m_hLastEnemy := arg ? arg->GetRefEHandle() : -1`.

**Unrecovered:** the break flag the sweep tests (record `+0x32`); the port counts the sweep.

## `CAI_BaseNPC::ChooseEnemy` `0x10279dd0`

Reads slot 167 `GetEnemy`, `m_afMemory` (`+0x5d8c`), `m_pSchedule` (`+0x5c38`), slot 541's
eluded flag and slot 158 `IsAlive`.

1. `bHadEnemy = (m_afMemory & 0x18000) != 0` (`0x10279e6b`). With it set and no current enemy the
   enemy WENT NULL (`0x10279e8b`) and counts as lost; otherwise a current enemy that `IsEluded`
   (`0x10279efa`) counts as lost. `bDead` is a current enemy whose slot 158 answers false.
2. With no running schedule the three interrupt answers (NEW_ENEMY 0x54, LOST_ENEMY 0x47,
   ENEMY_DEAD 0x58) are forced true (`0x10279eb9`); otherwise each is
   `ConditionInterruptsCurrentSchedule` `0x10269c70` (mask only).
3. The gate, unless the enemy is dead and ENEMY_DEAD interrupts (`0x10279f18`/`f1c`): the went-null
   case only `DevMsg(2, "WARNING: AI enemy went NULL but schedule (%s) is not interested\n")` when
   neither NEW_ENEMY nor LOST_ENEMY interrupts a running program, and FALLS THROUGH
   (`0x10279fd7`); every other case RETURNS `current != null` when NEW_ENEMY does not interrupt and
   either LOST_ENEMY does not or nothing was lost (`0x10279f30`-`0x10279f44`).
4. Slot 480 `ShouldChooseNewEnemy` false keeps the current enemy; true asks slot 478 `BestEnemy` and
   passes the answer through `0x102707d0` (a summoned body — its Troika answering `Classify() == 3`
   — hands the hate to its owner, slot 97). The change work runs when the candidate differs, OR when
   the enemy went null (the saved byte `[ESP+0x13]`, `0x1027a00d`-`0x1027a013`) — the one case
   where old and new are both null.
5. The change work, in order: `m_afMemory &= 0xfffe7fff` (`0x1027a027`); the OLD enemy non-null and
   dead SETS ENEMY_DEAD (`0x1027a04c`; nothing here clears 0x58); NEW_ENEMY set for a non-null
   choice, cleared otherwise (`0x1027a06f` / `0x1027a059`); `SetEnemy(choice)` (`0x1027a077`); with
   `bHadEnemy`, `0x1028ae60` (vacate the squad slot) and `m_afMemory &= 0xfffdffff`
   (`0x1027a086`/`a08b`); a null choice, when lost, sets LOST_ENEMY and slot 493 `LostEnemySound`
   (`0x1027a0b0`/`a0b9`), then fires `m_OnLostPlayer` (`+0x5ecc`) when the ENTRY word had
   `0x10000`, else `m_OnLostEnemy` (`+0x5e84`), with `this` as activator and caller; a non-null
   choice ORs `0x10000` for the player (its `+0xa8` `m_pPlayer`), else `0x8000` (`0x1027a0ff`).
6. Answers whether an enemy is held.

**Unrecovered:** slot 1 of `DAT_10924a6c` before each `SetCondition` is the `ent_trace_conditions`
ConVar touch, with no observable.

`ShouldChooseNewEnemy` (`0x10279d00`, slot 480) tests the enemy with slot 158 `IsAlive`
(`0x10279d33`), `m_lifeState`; the port's old `IsInert` (dead-or-hidden) test was corrected in the
L11 integration. Its first arm, `m_bfAINPCFlags2`-area bit `+0x14bc & 0x10000` answering FALSE
(`0x10279d13`), still has no port word.

## `CAI_BaseNPC::EnterGrappleState` `0x1026cdc0`

Three unconditional statements: `0x1026d130` (`SetEnemy(NULL)`, `DisconnectFromSquad` `0x1026d050`,
`++m_iIsOblivious` `+0x5bb4`), `m_OnGrappleBegin` (`+0x5bd8`) with the partner as activator
(`0x1026cdd7`), then `CBaseCombatCharacter::EnterGrappleState` `0x10329760` with every argument; its
`AL` is the answer.

`DisconnectFromSquad` (`0x1026d050`) only leaves the squad when not already disconnected
(`0x1026d05b`) and increments `m_iSquadDisconnected` (`+0x5bb0`); it writes no flag. The port's
extra `D_DISCONNECT_SQUAD` write was removed in the L11 integration (bit 23 is set by the task at
`0x102a536e`, not here).

**Unrecovered:** `0x102dfc10(DAT_109203f0, …)` inside `DisconnectFromSquad` (a global pending-record
flush).

## `CAI_BaseNPC` slot 354 `0x1026cec0`

The victim's feed-begin callback (`CBaseCombatCharacter::FeedBegin` `0x10339d90` dispatches it on
the victim). `0x1026d130` first. Then, reading `m_GrapplePartner` (`+0x1538`), `m_GrappleRole`
(`+0x153c`) and `m_GrappleType` (`+0x1540`): a LIVE partner with a role and type 8
(`BeFedOnByZombie`) returns having fired nothing (`0x1026cf0a`); a live partner with no role, or no
role at all, fires `m_OnFedUponBegin` (`+0x5c08`) with a null activator; otherwise the partner handle
is re-resolved and fired as activator (null when stale).

**Unrecovered:** none.

The feed wires both callbacks: `FeedBegin` (`0x10339d90`) dispatches the victim's slot 354 (the
body above), and the feed end (`FeedInterrupt` `0x1033a9e0` -> slot 355 `0x1026cf90`) fires
`OnFedUponEnd` and then runs `0x1026d160` unconditionally, releasing the obliviousness slot 354
took. `CBaseCombatCharacter`'s slot 354 (`0x1014f8d0`) is a lone `RET`.

## `CAI_BaseNPCTroika` slot 595 `0x102b4cc0`

`UTIL_EntitiesInBox` (`0x101ccc80`) over its slot-217 origin ± (1024, 1024, 128) with mask `0x40`
against `m_edtDerivedType` (`+0x4c`, a bit the Troika constructor `0x1028d230` sets), at most 32.
Per candidate, in order: its `+0x9c` combat character, `GetFlags() & 0x8000` (`FL_NOTARGET`) clear,
`m_bIsBCCTargetable` (`+0x1480`), slot 158 `IsAlive`, `0x100b5190` (`m_bScriptHidden` `+0xf4`)
clear, not itself, slot 404 `IRelationType` exactly `D_HT`. The strictly nearest (squared distance,
from FLT_MAX) goes to slot 596 (`0x102b4ea8`). No NPC word written.

**Unrecovered:** the partition's enumeration order; retail tests bounds overlap, the port origins.

## `CAI_BaseNPCTroika` slot 598 `0x102b4fe0`

The forget-this-entity route: `AddEntityRelationship(entity, D_NU 4, 0)` FIRST; `SetEnemy(NULL)`
when slot 167 is that entity; `0x10279b70(NULL)` when `m_hLastEnemy` resolves to it;
`CAI_Enemies::ClearMemory` (`0x102dfaa0`) on slot 541 with a `"%s(%d) :"` debug reason.
`AddEntityRelationship` (`0x10332ca0`) overwrites the entity's row at any priority (a lower
priority is not refused), so the forget always lands. `ClearMemory` unlinks the first record whose
handle resolves to the entity (a null entity removes nothing).

**Unrecovered:** `ClearMemory`'s notify target (`+0xe0` / `0x103169a0`).

## `CAI_BaseNPCTroika::EnterGrappleState` `0x102b5c00`

A non-empty queued-burn list (`+0x65a8`, count `+0x65b4`, 0x4c-byte records) is written in place
(`+0x2c` inflictor and `+0x28` attacker := this), each record gets hitbox `RandomInt(0,1) ? 4 : 5`
(`0x101c2a10`) and is handed to the PARTNER's `TakeDamage`; the grapple is then REFUSED (FALSE) and
the list left standing. Otherwise `CAI_BaseNPC::EnterGrappleState`; on TRUE: `IsInDialog`
(`0x102c1170`) → `0x102c0bb0`; a live `m_hCine` → `CancelScript` (`0x101a8c30`) and, when
`m_NPCState != m_IdealNPCState`, `SetState(ideal)`; `ClearSchedule` (`0x10280d30`); TRUE.

**Unrecovered:** none (the hit group has no field on the port's damage packet).

## `CNPC_VCop` slot 598 `0x10372dd0`

Runs only when the argument IS the entity `m_hClosestPlayer` (`+0x628c`) resolves to (a stale handle
and a null argument match): `AddEntityRelationship(player, D_NU, 0)`, `m_eOldPlayerRelationType`
(`+0x6668`) := 0, `SetEnemy(NULL)` / `0x10279b70(NULL)` when the enemy / last enemy is the player,
`ClearMemory`, then `InputSetRelationship("Player D_NU 10", 0)`. Anything else goes to the Troika
body `0x102b4fe0` with none of that.

**Unrecovered:** none.

## `CNPC_VCop` slot 596 `0x10372c50` / `CAI_BaseNPCTroika` slot 596 `0x102b4f60`

The Troika body: a null argument does nothing; otherwise `0x102707d0`, `SetEnemy`, then slot 544
`UpdateEnemyMemory(entity, slot 217 origin, &entity->+0x3d4)` — the enemy set before the memory row
is written. The cop runs the same three steps and then calls the Troika body DIRECT with the
resolved entity, which repeats them.

**Unrecovered:** why retail pushes `&entity->+0x3d4` as slot 544's third argument.

## `CNPC_VPedestrian` slot 27 `0x103a3850`

The Troika slot 27 `0x1029f8f0` direct, slot 596 with the same entity, the ideal-state trace (line
`0x405`), `m_IdealNPCState := 8` by DIRECT write (no `SetState`, no state-change virtual), and
`0x102ae7f0(0x15a)` — `m_iForcedSchedule` (`+0x65c8`) := 0x15a.

## `CNPC_VAndreiBlood::Activate` `0x1035dc30`

The Troika `Activate` `0x1028e310` direct, then `m_NPCState` and `m_IdealNPCState` := 2 by DIRECT
write (no `m_flLastStateChangeTime` stamp, no slot 463), the schedule trace (line `0x1cc`), and
`0x102ae750(0x6b, false)`.

## `CNPC_VGhoulCroucher::EnterGrappleState` `0x1037b500`

With `m_bSpawnBurning` (`+0x6665`) set and a grappler whose `+0xa8` `m_pPlayer` is non-null:
`BurnPlayer(player, 10.0)` and FALSE — the grapple is refused. Otherwise the Troika body direct.

## `PlayerSupernaturalIncident` `0x1017f4a0`

0. `debug_show_cs_acts` (`DAT_107258dc`, shipped "0") enabled: the witness's
   `m_flSupernaturalWitnessedTimer` (`+0x63a8`) := curtime + 20.0 and
   `DevMsg("CSActs:    %6.1f - Supernatural act witnessed by %s at %f\n")` (`**UNKNOWN**` for a null
   witness). The stamp is inside the debug guard: dead on a shipped build.
1. `0x1023bd00()` non-null with `+0x49c` (area type) non-zero, else return.
2. `m_flMasqueradeTimerNext` (`+0x1dbc`) `<=` curtime: `ChangeMasqueradeLevel(+1)` (`0x1022dea0`) and
   `+0x1dbc := curtime + debug_masquerade_timer` (`DAT_10723984`).
3. Independently, `debug_supernatural_cop_spawn` (`DAT_10725804`) enabled: `0x1017ed00` (the police
   response).

**Unrecovered:** the `debug_masquerade_timer` default (`DAT_10588678`).

## `CAI_BaseNPC::HandleAnimEvent` `0x10274e30`

Slot 259's base body, a two-band switch; every id it names returns at `0x102754d1` without the
base, guard failures included. `0x3fc..0x3fe`: `DevMsg("Bodygroup!\n")`. `1000`
(`SCRIPT_EVENT_DEAD`) and `0x3f2` (`NOT_DEAD`), only in `m_NPCState` 4: `m_lifeState := 1`,
`m_iHealth := 0` / `m_lifeState := 0`, `m_iHealth := m_iMaxHealth`. `0x3e9`/`0x3ea`: a live
`m_hCine` gets `AllowInterrupt(0/1)` (`0x101a8890`). `0x3eb`: the Troika's interesting place
(`+0x98`→`+0x62ec`) is read first; `atoi(options)` goes to the live cine's `OnScriptEvent0<n>`
(`0x101a7230`), else to `m_pHintNode`'s `OnAnimEvent<n>` (`0x102d09b0`, `+0x4e4+0x18n`), else to the
place's `OnAnimEvent<n>` (`0x102db3e0`, `(3n+0x8d)*8`); each fires only for n in 1..8, the NPC as
activator of the hint and place outputs; with no target the event is still claimed. `0x7e6` and
`0x7fa..0x7fc` test the options pointer only, so an empty option reaches `atoi("") = 0`. `0x3ec`/`0x3f0`: `EmitSound(options)` (`0x101b0c10`).
`0x3f1`: `RandomInt(0,2)`, zero returns, non-zero falls into `0x3ed`: `SENTENCEG_PlayRndSz(edict,
options, 1.0, 80, 0, 100)`. `0x7d1`/`0x7d2` (2001/2002), only with `FL_ONGROUND`:
`AI_BaseNPC.BodyDrop_Light` / `_Heavy` (2001 is LIGHT). `0x7da`: `AI_BaseNPC.SwishSound`. `0x7e4`:
`SetIdealActivity(ACT_IDLE)`, `m_afMemory &= ~0x2000`, `SetBoneController(0, GetAngles().y)`,
`m_fEffects |= EF_NOINTERP`. `0x7e6`: `SetIdealYawAndUpdate(GetAbsAngles().y + atoi, -1)`.
`0x7f8` (pickup): the nearest entity named by the option within 256 units (a miss is "stolen", no
fallback), else `m_hTargetEnt`; its weapon (`+0xa0`); an owned weapon fails "Weapon in use by
someone else" (line 0x1f06), `Weapon_CanUse` false "Can't use this weapon type" (0x1f0e), no weapon
"Weapon stolen by someone else" (0x1f19); success: weapon slot 341 `OnPickedUp`, `Weapon_Equip`,
`TaskComplete`. `0x7f9`: the named lookup and its `WorldSpaceCenter` are discarded;
`Weapon_Drop(active, NULL, false)`. `0x7fa`/`0x7fb`/`0x7fc`: weapon-model sequence / activity.
`0x802..0x805`: `0x1026d460(this, 0 | 1)`. Default: an id in `3000..0xfa2` (or from another owner)
goes to `Weapon_HandleAnimEvent`, everything else to `CBaseCombatCharacter::HandleAnimEvent`.

**Unrecovered:** the NPC's `m_lifeState` word, soundscript emission, the weapon model's
sequences, `OnPickedUp`, `FL_ONGROUND` on NPCs.

## `CAI_BaseNPCTroika::HandleAnimEvent` `0x1029b290`

`GetCurTask` (`0x1028a150`) first. `0x80c`: `sscanf("%s %f %f %f")` (defaults 1.0/0.2/0.2), fewer
than two fields or a total `<= 0` return; fades exceeding the total are rescaled; hold = total -
fades (floored at 0), at x87 width against the image's 0.0 cell `0x104454c4`;
`SetExpression(name, 0, in, hold, out, 1.0)`. `0x7d5`: `EmitSound` with the
option as the sample, `CHAN_AUTO`, 1.0, soundlevel 0x42, pitch 100. `0x7d6`: an inlined
`CBaseCombatCharacter::Die` without its life-state guard — a `CVDmg_t` sourced on this NPC, a
`CTakeDamageInfo` whose attacker is `m_hClosestPlayer`, the sheet's wounds := max health,
`Event_Killed` then `Event_Dying`. `0x7e5`: `m_afMemory &= ~0x2000`. `0x7f8`: `m_hTargetEnt` as a
`CBaseCombatWeapon`, owned -> "in use", `Weapon_CanUse` false -> "can't use", none -> "stolen";
else `Weapon_Equip` and `TaskComplete`. `0x80d`: `SetExpression(option, 0, 0, SequenceDuration *
m_flCycle + 0.1, 0, 1.0)` (the time ELAPSED; 0.1 is the double `0x104493d0`). `0x1036/0x1037/0x103a/0x103b`, only in
`TASK_DO_INTEREST_ACTIVITY` (0xb4): `Interesting_places/<male|female>/<opt>.wav` when that file
exists, else `Interesting_places/<opt>.wav`, channel 4 (0x1036/0x103a) or 2. `0x1038/0x1039`, same
gate: STOP channel 4 / 2. Everything else: the base body.

**Unrecovered:** `SetExpression`'s partner-scale constants; `CTakeDamageInfo +0x48`.

## Species slot 259 bodies

- **Dog `0x10374280`:** `0xbb9` with an active weapon runs the bite `0x10374f40` and
  `Msg("Gots a doggie bite!\n")`; without one the event is swallowed. Everything else: Troika.
- **Gargoyle `0x103786c0`:** `0x802/0x803` shake (2.0, 0.2, 0.2, 1024) and a stomp (slot 617);
  2 the roar; 1 swallowed; the rest to the Troika.
- **Hengeyokai `0x1037fb60`:** `0xbbd` releases the carried body when carrying; `0x802/0x803` only
  in shark form: shake and stomp; `0x7f8` attaches the pickup animlink unless carrying; failed
  guards are swallowed; `0x804/0x805` go to the Troika.
- **ManBat `0x1038e000`:** 1 wingflap, 2 screech, 3 heavy exert.
- **MingXiao `0x10392a70`:** `0xbbd` throws in mode 4 (else a DevMsg); `0xbd7` the vomit emitter
  then the Troika; `0x835` shake (15, 1, 1.5, 1024); `0x7f8` the tentacle grab in mode 2 (else a
  DevMsg); `0x802/0x803` swallowed; `0x834` shake (5, 0.6, 0.2, 512).
- **SabbatLeader `0x103a7000`:** `0x802/0x803` slot 620 `FootstepSound`.
- **Tzimisce `0x103ba410`:** `0xbbd` release when carrying; `0xbbb` swish (channel 1); `0x802/0x803`
  shake and footstep; `0x7f8` pickup unless carrying; ids 2..9 the voice slots (2 and 3 only in
  state 1), with expression 2 after 6, 7 and 9: `0x103b9f90(2, t)` is
  `SetExpression("scream", 0, 0.15, max(t - 0.3, 0), 0.15, 1.0)` (names `0x10653120` {normal,
  angry, scream, dead}; 0.3 the double `0x1047b868`).
- **TzimisceHeadClaw `0x103c1540`:** `0x802` shake (1.3) and slot 619(1) (Foot_Step3/4); `0x803`
  shake and slot 619(0) (Foot_Step1/2).
- **Werewolf `0x103d88e0`:** `0x835` the activity voice `0x103d8df0`; `0x836..0x83d` swallowed;
  `0x834` air shake (16, 2, 2, 1500) at `Bip01`; `0x3eb` fires the held teleport / move / break
  hint's `OnAnimEvent<n>` (`0x102d09b0`); `0x802..0x805` the footstep `0x103d8c10`, whose sound and
  shake (centred on slot 192 `WorldSpaceCenter`) are behind `werewolf_footstep_sounds` / `_shakes`,
  both shipped "0".

**Unrecovered:** the dog bite and Ming Xiao grab bodies; the `Bip01` bone position (the port's
`RetailBonePosition` seam answers false, so the origin stands in).

## `0x10365a90` — Bach's camper pass

The target is `GetEnemy` or the local player; ten `FVisible` asks. Any visible answer: nothing
occluded returns; otherwise `m_iWasOccluded := 0` and, with the camper flag set, schedule `0x15f`
(line 0x57b). Ten failures: the first occluded pass stamps the enter time and compares
`|dx| * |dy|` against 20000 (below: `++m_iReusedOccludeCount`, at 2 the flag); a later pass after
MORE than 4.0 s compares `max(|dx|, |dy|)` against 200 (below: the flag); both refresh the saved
origin. Grenade zones 10 and 7 force the flag AND the sound edge every pass. With the flag: zones
5/6/7/9/10 throw from `grenade_spawn_N` (300 / 80 / -26 / 85 / -26) in the starting position, zone
8 (`grenade_spawn_8`, 15) out of it with no teleport state. On the edge: `bach_grenade.wav` when a
spawn arm was entered, else `bach_camp_warn.wav`, CHAN_VOICE, 1.0 / 0.8 / 100.

## `CNPC_VWerewolf::CheckAllMoveHints` `0x103cfc50`

Running one of the four move-hint programs (0x15f, 0x15b, 0x15a, 0x160 through `GetScheduleOfType`)
answers TRUE; `ShouldPursueEnemy` refusing FALSE; a held hint whose target groundpoint has a path
to the enemy point TRUE. Then the search (timer, `+0x66d4` frame, `+0x66b8 := 0`, `+0x66d8 :=
curtime`) over the global hint list with `werewolf_pursuit_distance` as both limits: an imperative
hint is taken at once; a valid one not yet seen must be within limit B of us, its target
groundpoint within limit A of the enemy point, with both paths; every non-taken iteration appends
the local vector (an invalid hint appends the STALE one). Exhaustion: `DevWarning("CNPC_VWerewolf::
CheckAllMoveHints FAILED.\n")`, FALSE.

**Unrecovered:** `IsImperativeMoveHint` (lane L12); the uninitialised first stale vector.
