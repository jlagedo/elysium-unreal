// Story 29d, family **SpeciesMisc10** — the declarations of this family's layer 10–18 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here: the
// generator already declares that virtual and this family only defines it.
//
// The definitions are in `Substrate/ElysiumNpcKernelSpeciesMisc10.cpp` (the species words, the
// Newscaster, the Chang brothers, the ghoul croucher, the guard and the ManBat) and
// `Substrate/ElysiumNpcKernelSpeciesMisc10_2.cpp` (the Sabbat leader, the Tzimisce pair, the vampire
// boss, the Werewolf and the Bullseye/Pedestrian spawn-side bodies); the tests are
// `Tests/ElysiumNpcKernelSpeciesMisc10Tests.cpp`. The walked prose is
// `docs/vtmb/npc-ai/social.md`, `shape.md` and `lifecycle.md`
// § "Story 29d, family SpeciesMisc10 — …".
//
// --- What this family is --------------------------------------------------------------------------
//
// **The one-off species bodies that are not slot overrides.** Forty-one rows: the Newscaster's story
// loader and its player, the Sabbat leader's blood splash and player-damage record, the ManBat's
// screech cone and its slowed-entity release, the Tzimisce runner's two form words and the head
// claw's slow/HUD chain, the Werewolf's stuck and path helpers, the Chang brothers' teleport/united
// gates and their ledge pick, the vampire boss's health record and body emitters, the transformation
// wait, and eight registry rows that are census data rather than code.
//
// FIVE STANDING FACTS OF THIS FAMILY, stated once here rather than at thirty call sites.
//
//   * **`GetCurrHealthPercent` (`0x103c6830`, family Combat10) RISES with damage.** It is
//     `wounds(stat 0x0f) / cap(stat 0x11)` and not a remaining-health fraction, so
//     `HealthPercentLostSinceRecord` (`0x103c6a20`) answers **positive** for a body that has lost
//     health. The checklist's walk of `0x103c6a20` says "a body that has lost health answers
//     negative"; it is the other way round, and every threshold that reads it
//     (`_DAT_104ad9f8` = 0.1 for the Chang brothers, `_DAT_104c3cc4` = 0.0666667 for the Sabbat
//     leader) is a *fraction of the health bar lost since the mark*. **CORRECTED.**
//   * **`+0x9c` is the entity's `CBaseCombatCharacter` self-downcast cache and `+0xa8` its
//     `CBasePlayer` one**, so `victim->+0xa8 != 0` reads "is this the player" and `victim->+0x9c`
//     is the victim itself. Family Senses10 states the first half; the second is what makes the
//     ManBat screech cone, the head claw's slot 332 and the ghoul croucher's burn
//     **player-only** bodies.
//   * **Every emitter word in this family is an `EHANDLE` to an entity this substrate does not
//     create.** Family Damage's `CreateNamedEmitter`/`StartNamedEmitter`/`KillNamedEmitter`/
//     `RemoveNamedEntity` seam is the one this family goes through, so the DECISION (which name,
//     which attach mode, which bone, in what order) is recorded and assertable while the handles
//     stay dead — which is retail's own behaviour for a dead handle.
//   * **`_DAT_1044fab0` is a DOUBLE and reads 0.0** (`103c1db6` is `FCOMP double ptr`), read out of
//     the pinned `vampire.dll` at file offset `0x44fab0`. It is the "no slow running" sentinel both
//     the ManBat and the head claw compare their expiry against.
//   * **`CNPC_VWerewolf`'s two hint words are the other way round from the checklist's walk.** The
//     datamap reads `+0x66b0 m_pTeleportHint` and `+0x66bc m_pMoveHint` (plus `+0x66b4
//     m_pLastUsedTeleportHint`, `+0x66c0 m_pLastUsedMoveHint`, `+0x66c4 m_pBreakHint`); the walk of
//     `0x103ce750` has `+0x66b0` and `+0x66bc` swapped. Family **Hints** already bound them
//     correctly (`TeleportHintNode`/`MoveHintNode`) and this family reads those. **CORRECTED.**

// --- `CAI_BaseNPC::LeaveGrappleState` `0x1026ce30` -----------------------------------------------

/** `CAI_BaseNPC::LeaveGrappleState` (`0x1026ce30`), 100 bytes — a DISTINCT retail function beside
 *  the Troika override `0x102b5d90` that owns slot 380, so it takes its own name and is not a slot
 *  body. Four UNCONDITIONAL steps with no grapple-type gate anywhere: fire `m_OnGrappleEnd` with the
 *  grapple partner as activator (null when the handle is stale), `CBaseCombatCharacter::
 *  LeaveGrappleState`, slot 416 `SetForceFrequentThink(false)`, then `0x10007ea0` — the saturating
 *  decrement of `m_iIsOblivious` (`+0x5bb4`) clamped at 0, and the squad reconnect `0x10009601`.
 *
 *  `FElysiumNpc::LeaveGrappleState` (`ElysiumNpc.cpp`) calls this and then slot 614. */
void BaseLeaveGrappleState();

// --- `CNPC_Bullseye::Spawn` `0x103567e0` (slot 103) ----------------------------------------------

/** `CNPC_Bullseye::Spawn` (`0x103567e0`), slot 103's species body — the aim-target dummy's whole
 *  spawn, in retail's order. `CNPC_Bullseye` carries NO entity classname in the census, so this arm
 *  is unreachable at runtime today and is driven in the suite through `SetRetailClassForTests`. */
void BullseyeSpawn();

/** What `BullseyeSpawn` wrote, so a headless case can read a body this substrate has no collision
 *  or think surface for. Every field is retail's, named by the offset it came from. */
struct FBullseyeSpawnRecord
{
	bool bRan = false;
	FVector HullMinsUnits = FVector::ZeroVector;   // `0x101cf390 SetSize(-16,-16,-16 .. 16,16,16)`
	FVector HullMaxsUnits = FVector::ZeroVector;
	int32 BloodColorFirst = 0;    // the first `SetBloodColor`, always 0xf7
	int32 BloodColorSecond = 0;   // the second, 0xf7 under spawnflag 0x80000 and -1 otherwise
	int32 Effects = 0;            // `m_fEffects` (+0x17c): 0, then `|= 0x40` at the tail
	float FieldOfView = 0.f;      // `m_flFieldOfView` (+0x19c) = 0.5
	float Gravity = 0.f;          // `m_flGravity` (+0x1fc) = 0
	int32 Flags = 0;              // `AddFlag(0x2000)`
	int32 Flags2 = 0;             // `AddFlag2(0x10)`
	double NextThink = 0.0;       // `curtime + _DAT_104493d0` (0.1, a DOUBLE in `.rdata`)
	int32 Solid = 0;              // `SetSolid(2)`
	int32 SolidFlags = 0;         // `|= 0x10`, and `|= 4` under spawnflag 0x10000
	int32 TakeDamage = 0;         // 0 under spawnflag 0x20000, else 2
	bool bRelinked = false;
	bool bPhysicsCheckedWater = false;
};
FBullseyeSpawnRecord BullseyeSpawn_Record;

// --- `CNPC_VBach::GatherAttackConditions` `0x10363db0` (slot 561) --------------------------------

/** `CNPC_VBach::GatherAttackConditions` (`0x10363db0`), slot 561's species arm. It ADDS the shield,
 *  teleport and weapon-switch block IN FRONT of the base and changes nothing the base gathers, so
 *  `FElysiumNpc::GatherAttackConditions` runs this first and then the base body unmodified. */
void BachGatherAttackConditions(float DistanceUnits);

/** `CNPC_VBach`'s shield block. `m_iBachTeleportState` indexes `DAT_1062d200`, whose four cells read
 *  **768.0, 768.0, 384.0, 384.0** out of the pinned image at `0x62d200`. */
static constexpr float BachTeleportDistanceUnits[4] = { 768.f, 768.f, 384.f, 384.f };
int32 BachTeleportState = 0;             // `m_iBachTeleportState`
double BachNextHolyLightTime = 0.0;      // `m_flNextHolyLightTime`, an absolute curtime stamp
double BachShieldTime = 0.0;             // +0x6684 `m_flShieldTime`
double BachNextShieldTime = 0.0;         // +0x6688 `m_flNextShieldTime`
double BachNextWeaponSwitchTime = 0.0;   // +0x668c `m_flNextWeaponSwitchTime`
bool bBachShieldActive = false;          // +0x66a5 `m_bShieldActive`
bool bBachShieldFlagB = false;           // +0x66a6, cleared on BOTH arms; retail name unrecovered

// --- `CNPC_VChangBros` — `0x1036cab0`, `0x1036cbd0`, `0x1036cfa0` -------------------------------

/** `CNPC_VChangBros::CheckForTeleport` (`0x1036cab0`), three arms in retail's order: `m_ChangType`
 *  (`+0x66b8`, family Squad's `ChangType`) equal to 1 answers false outright; else a health loss of
 *  `_DAT_104ad9f8` = **0.1** or more since the mark answers true; else, only for type 0 and only
 *  with a live other brother, `curtime - m_fFacingTime` strictly greater than
 *  `GetFacingTimeToTeleport()` answers true. */
bool CheckForTeleport();

/** `CNPC_VChangBros::CheckForUnited` (`0x1036cbd0`): false with no other brother, and false while
 *  `curtime` has not passed `m_fLastUnitedAttackTime + _DAT_104ada48` (**30.0** s). Past both it is
 *  true UNLESS BOTH brothers' `GetCurrHealthPercent` are below `_DAT_104ada4c` (**0.5**) — one
 *  healthy brother is enough. Note the sense: the percent RISES with damage, so "below 0.5" is the
 *  HEALTHY half, and two healthy brothers refuse the united attack. */
bool CheckForUnited();
double ChangLastUnitedAttackTime = 0.0;   // +0x66ec `m_fLastUnitedAttackTime`

/** `CNPC_VChangBros::SelectLedgeNode` (`0x1036cfa0`) — type `0x4653` again, but the FARTHEST
 *  reachable ledge rather than the nearest: the incumbent is seeded `-FLT_MAX` and replaced on a
 *  STRICTLY GREATER distance, the opposite comparison from `CNPC_VSheriffMan::SelectLedgeNode`
 *  (`0x103b0ab0`) that family Positions' `SelectLedgeNodeRule` carries. `CheckJumpPathToHintNode`
 *  gates every candidate and the distance is the full 3-D one from the NPC's own origin.
 *
 *  Split the way family Positions split `SelectLedgeNodeRule`/`SelectLedgeNode`: the PURE rule (the
 *  type filter and the farthest-wins comparison) is a static over an already-accepted list, and the
 *  member applies the jump-path gate while it gathers. Retail interleaves the two, and the answer is
 *  identical because a tie keeps the incumbent either way — but the gate is a SEAM here
 *  (`JumpPathSector` answers 4, which closes it for every node, retail's own refusal for a brother
 *  in sector 4), so the comparison has to be assertable without it. */
static int32 ChangBrosSelectLedgeNodeRule(TArrayView<const FHintWords> Nodes,
	const FVector& MeasureFromCm);
int32 ChangBrosSelectLedgeNode() const;

// --- `CNPC_VCop`'s pursuit latch — `0x10372cc0` (slot 597) --------------------------------------

/** `CNPC_VCop::vfunc597` (`0x10372cc0`), slot 597's species arm: ADDS the `m_hPursuitPlayer` latch
 *  and the `"Player D_HT 10"` relationship write in front of the Troika base `0x102b4fb0`, which
 *  then runs unchanged. Called from `FElysiumNpc::Slot597`.
 *
 *  CORRECTION to the walk: `argument+0xa8` is `m_pPlayer`, the player self-downcast cache, not a
 *  "troika sub-object" — so the latch stores the PLAYER's own handle. */
void CopSlot597Prologue(FElysiumEntity* Other);

/** `+0x6664 CNPC_VCop::m_hPursuitPlayer`. Family **Debug10** declared the READER (`CopPursuitPlayer`)
 *  as a seam answering null; this is the word its writer needs, so that seam now resolves it. */
FElysiumEntityHandle CopPursuitHandle;

// --- `CNPC_VGhoulCroucher` — `0x1037be80` (slot 24) and `0x1037c090` ----------------------------

/** `CNPC_VGhoulCroucher::BurnPlayer` (`0x1037c090`). With a non-null target: walk every hitbox of
 *  its model calling `CBaseCombatCharacter::BurnHitbox(target, index, 5.0, 0.5)`, then build a
 *  `CTakeDamageInfo(inflictor = my active weapon, attacker = this, damage, bits = 8)` and
 *  `TakeDamage` it. The caller (`0x1037be80`) passes **10.0**. */
void BurnPlayer(FElysiumEntity* Target, float Damage);

/** One `BurnHitbox` request. **SEAM**: this substrate exposes no hitbox table to the kernel
 *  (family Damage's `HitboxSetCount` answers 0), so the loop makes no passes and the record is what
 *  says the body ran and took retail's own zero-hitbox arm. */
struct FBurnHitboxCall
{
	FElysiumEntityHandle Target;
	int32 HitboxIndex = INDEX_NONE;
	float Seconds = 0.f;      // `5.0`
	float Interval = 0.f;     // `0.5` (`0x3f000000`)
};
TArray<FBurnHitboxCall> BurnHitboxCalls;

// --- `CNPC_VGuard1` — `0x1037e2d0` --------------------------------------------------------------

/** `CNPC_VGuard1`'s hate latch (`0x1037e2d0`), reached from `OnStateChange` (`0x1037d020`) and six
 *  times from `vfunc461` (`0x1037d290`): set `+0x6660` and call `InputSetRelationship` with the
 *  literal `"player D_HT 10"` at priority 0 — lower case `player`, unlike the cop's `"Player"`. */
void Guard1HatePlayer();
bool bGuard1HatesPlayer = false;   // +0x6660 CNPC_VGuard1 (walked; the retail name is unrecovered)

// --- `CNPC_VManBat` — `0x1038e9c0` and `0x1038f020` ---------------------------------------------

/** `CNPC_VManBat::StartScreechCone` (`0x1038e9c0`), 1283 bytes, no vtable slot; one direct caller,
 *  `StartTask` (`0x1038c390`), once with the resolved enemy and once with null. Retail name
 *  unrecovered; named for what it does. */
void ManBatStartScreechCone(FElysiumEntity* Target);

/** `CNPC_VManBat::ReleaseSlowedEntity` (`0x1038f020`), the slowed-grab teardown: `RunAI`
 *  (`0x1038e990`) calls it with `force = 0` and `Event_Killed` (`0x1038e8c0`) with `force = 1`. */
void ManBatReleaseSlowedEntity(bool bForce);

FElysiumEntityHandle ManBatSlowedEntity;     // +0x6680 `m_hSlowedEntity`
FElysiumEntityHandle ManBatPlayerEmitter;    // +0x669c `Manbat_player_emitter`
FElysiumEntityHandle ManBatHudEmitter;       // +0x66a0 `HUD_Manbat_emitter`
FElysiumEntityHandle ManBatScreechCone;      // +0x66a4 `Manbat_screechcone_emitter`
FElysiumEntityHandle ManBatBlastEmitter;     // +0x66a8 `Manbat_blast_player`

/** SEAM for `CBaseCombatCharacter::BeginSlowEntity` / `EndSlowEntity` — retail's movement-slow on
 *  another character, always with the argument **500.0** on both the ManBat (`1038eb5e`) and the
 *  Tzimisce head claw (`103c1dca`). No port system slows a character from the kernel tier, so the
 *  calls are recorded rather than applied and the record is what a case reads. */
struct FSlowEntityCall
{
	FElysiumEntityHandle Target;
	float Magnitude = 0.f;
	bool bBegin = false;
};
TArray<FSlowEntityCall> SlowEntityCalls;
void BeginSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude);
void EndSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude);

/** SEAM for `UTIL_ScreenShake` (`0x101cdba0`) — the ManBat's cone shakes the screen with
 *  `(2.5, 0.2, 3.0, 0.0, 0, 0)` around **its own** slot-220 origin (`vt+0x370`), not the target's.
 *  Nothing in this substrate shakes the view from the kernel; the request is recorded. */
struct FScreenShakeCall
{
	FVector CentreUnits = FVector::ZeroVector;
	float Amplitude = 0.f;
	float Frequency = 0.f;
	float Duration = 0.f;
	float Radius = 0.f;
};
TArray<FScreenShakeCall> ScreenShakeCalls;

/** SEAM for `0x10344f80(combatCharacter, delta, 2, 0)` — the physics push the cone applies, with
 *  `delta = target->GetAbsOrigin() - my GetAbsOrigin()` (both slot 217). Recorded. */
struct FPushEntityCall
{
	FElysiumEntityHandle Target;
	FVector DeltaUnits = FVector::ZeroVector;
	int32 Mode = 0;
};
TArray<FPushEntityCall> PushEntityCalls;

/** SEAM for `player->+0x2454` bit 0 — the word the ManBat cone ORs on and its teardown clears. The
 *  retail field has no name in the corpus and no port counterpart; it is carried here so both
 *  bodies' writes are observable and paired. */
bool bPlayerScreechConeBit = false;

/** SEAM for `0x1015d680(player, 0)` — the entity in the player's inventory slot 0 (the `+0x2308`
 *  handle array), which both the ManBat's HUD emitter and the head claw's gate on. This runtime's
 *  inventory is `FElysiumInventory`; this answers its active weapon entity, which IS retail's slot 0
 *  for a character carrying one, and null otherwise — retail's own refusal. */
FElysiumEntity* PlayerInventorySlot0() const;

// --- `CNPC_VNewscaster` — `0x103a0670` and `0x103a0ab0` -----------------------------------------

/** `CNPC_VNewscaster::PlayNextNewscasterStory` (`0x103a0670`), 302 bytes, no slot.
 *
 *  **CLASS ATTRIBUTION CORRECTED** — its batch filed it under the Ming Xiao family; it is
 *  `CNPC_VNewscaster`, whose loader `0x103a0ab0` and teardown `0x103a0d50` touch the same six words.
 *
 *  Order: with `m_bStoriesLoaded` (`+0x6690`) clear and `UTIL_PlayerByIndex(1)` (`0x101cd9e0`)
 *  answering an entity, load, then seed both cursors with `RandomInt(0, count - 1)`; a missing
 *  player returns WITHOUT loading. Then, unless `IsInDialog` (`0x102c1170`), and only when
 *  `count0 + count1` is non-zero, roll `RandomInt(0, count0 + count1)` — an INCLUSIVE upper bound,
 *  so the roll can equal the sum — and write 1 to `m_bPlayMainStory` (`+0x668c`) when it lands below
 *  `count0` and 0 otherwise. A zero `+0x668c` or an empty main queue advances the SIDE cursor
 *  (returning early on an empty side queue), anything else the MAIN cursor, each modulo its count;
 *  the advanced row's SELECTED VERSION filename is then played through `0x102c0520`
 *  (`OnDialogFilePlayed`) when it is non-null. */
void PlayNextNewscasterStory();

/** `CNPC_VNewscaster::LoadNewscasterStories` (`0x103a0ab0`), 525 bytes.
 *
 *  **PATH CORRECTED**: `0x1064aadc` and `0x1064aac0` are the FORMAT strings `"%sNewscaster_Main.txt"`
 *  and `"%sNewscaster_Side.txt"`, handed to `UTIL_VarArgs` (`0x101d3730`) with `"vdata\system\"`
 *  (`0x105a0f80`) — so the files are `vdata/system/Newscaster_Main.txt` and `…_Side.txt`, not the
 *  `\s…` the walk read out of the `%s`.
 *
 *  Order: tear both queues down through `0x103a0d50` FIRST; open the main file as KeyValues and, for
 *  each child key, zero a ten-word `0x28` scratch row, fill it with `0x103a07f0` and append it only
 *  when that answers true; release the KeyValues; repeat verbatim for the side file; set
 *  `m_bStoriesLoaded`. */
void LoadNewscasterStories();

/** `0x103a07f0`, the per-`Story` parser, whose tail is the fact the play body depends on. It is a
 *  file static in the `.cpp` rather than a member, because its one argument is a `KeyValues*` and
 *  this header is included inside `class FElysiumNpc`.
 *
 *  **CORRECTED**: `+0x24` is NOT a version count. `103a098f`–`103a0a0b` walks the parsed versions in
 *  order and stores the index of the FIRST whose `dependency` is absent, empty, or evaluates
 *  non-zero through `0x1000134d` — `PyRun_String(src, Py_eval_input, __main__, __main__)` — and
 *  answers **true**. A record whose every dependency is false frees its strings and answers
 *  **false**, and is never appended. So a `Story` row carries FOUR `(dependency, filename)` pairs at
 *  `+0x04`/`+0x08`, `+0x0c`/`+0x10`, `+0x14`/`+0x18`, `+0x1c`/`+0x20` (a fifth is refused with
 *  `"Newscaster: too many versions in %s! skipping %s"`), the chosen index at `+0x24`, and the story
 *  name at `+0x00` — `GetString("Name", "STORY")`, so an unnamed story is literally `STORY`. A key
 *  whose name does not contain `"Story"` warns `"Newscaster: invalid key! (%s)"` and is skipped.
 *
 *  The dependency evaluation goes through `FElysiumEntityWorld::EvalCondition`, this runtime's
 *  `PyRun_String`, so the same expressions the retail files carry (`G.Story_State < 10`,
 *  `not IsPCMalk()`) are answered by the same interpreter every dlg condition uses. */
bool EvalNewscasterDependency(const FString& Source) const;

/** SEAM for `0x101cd9e0(1)` — `UTIL_PlayerByIndex(1)`, the gate that stops the loader running before
 *  there is a player. Answers the world's player entity. */
bool NewscasterPlayerPresent() const;

/** Each `0x102c0520(this, filename, 0, 0)` the play body reached — `OnDialogFilePlayed`, family
 *  Dialogue's row. Recorded here because nothing in this substrate plays a VCD from the kernel and
 *  the CHOICE (which queue, which cursor, which version) is the whole of what this body decides. */
TArray<FString> NewscasterPlayedFiles;

// --- `CNPC_VPedestrian::CreateCorpse` `0x103a38c0` (slot 301) -----------------------------------

/** `CNPC_VPedestrian::CreateCorpse` (`0x103a38c0`), slot 301's species body. BEFORE the base it
 *  snapshots the collision OBB — `m_Collision` vtable `+4` into `m_vecPreDeathMins` (`+0x6660`) and
 *  `+8` into `m_vecPreDeathMaxs` (`+0x666c`) — because the base resizes the hull; then
 *  `CBaseCombatCharacter::CreateCorpse`, `ThinkSet(NULL, 0.0, NULL)` and `SetSolid(SOLID_NONE)`.
 *
 *  **NOT WIRED TO SLOT 301.** Slot 301's Troika-line body (`0x1032c0e0`,
 *  `CBaseCombatCharacter::CreateCorpse`, layer 14) carries no verdict yet, so `gen_kernel_shape`
 *  still emits its stub and there is no port method to hang the species case on. The body lands
 *  under its recovered name and the gap is named in the story's answer. */
void PedestrianCreateCorpse();
FVector PedestrianPreDeathMinsUnits = FVector::ZeroVector;   // +0x6660 `m_vecPreDeathMins`
FVector PedestrianPreDeathMaxsUnits = FVector::ZeroVector;   // +0x666c `m_vecPreDeathMaxs`
/** SEAM for `CBaseCombatCharacter::CreateCorpse` (`0x1032c0e0`, slot 301's Troika-line body): this
 *  substrate stands no corpse entity at the kernel tier and that row carries no verdict, so the
 *  chain call is counted. `ThinkSet(NULL, 0.0, NULL)` and `SetSolid(SOLID_NONE)` are recorded beside
 *  it for the same reason — neither has a port word, and the ORDER around the base is the load-bearing
 *  half of this body. */
int32 PedestrianCreateCorpseCalls = 0;
bool bPedestrianCorpseThinkStopped = false;
int32 PedestrianCorpseSolid = -1;

// --- `CNPC_VSabbatLeader` — `0x103a9d90`, `0x103aa960`, `0x103aaa80`, `0x103aabc0` --------------

/** `CNPC_VSabbatLeader::CheckForJumpCondition` (`0x103a9d90`), three arms in retail's order:
 *  `0x103c67f0(this, DAT_104c3cc8)` — `curtime - m_flLastAttackTime (+0x5d9c)` STRICTLY greater than
 *  **8.0** — then a health loss since the mark of `_DAT_104c3cc4` = **0.0666667** or more, then
 *  `PlayerDamagedEnoughThisRound`. */
bool CheckForJumpCondition();

/** `0x103c67f0` — `curtime - m_flLastAttackTime > Seconds`, the boss line's shared idle test. */
bool AttackIdleLongerThan(float Seconds) const;

/** `CNPC_VSabbatLeader::UpdateBloodSplash` (`0x103aa960`): nothing at all while `m_bDiving`
 *  (`+0x66d5`) is set — not even the level copy — otherwise, with a water level above 0 and either a
 *  previous level of 0 or `m_fLastSplashTime + _DAT_104c3cdc` (**0.25** s) already past, spawn
 *  `bloodsplash_emitter` and, ONLY on the dry-to-wet edge, `bloodbigsplash_emitter` too, then stamp
 *  the splash time. The previous level is copied on every non-diving pass, so a dive freezes it and
 *  leaving one re-fires the big splash. */
void SabbatLeaderUpdateBloodSplash();
int32 SabbatLastWaterLevel = 0;      // +0x66c4 `m_nLastWaterLevel`
double SabbatLastSplashTime = 0.0;   // +0x66c8 `m_fLastSplashTime`
bool bSabbatDiving = false;          // +0x66d5 `m_bDiving`

/** `CNPC_VSabbatLeader::RecordPlayerHealth` (`0x103aaa80`) — snapshot `m_hClosestPlayer`'s type-0
 *  stat `0x0f` into `m_LastPlayerHealth` (`+0x66cc`). Stat `0x0f` is the accumulated WOUND counter,
 *  not current health, so this records the player's damage TOTAL at the start of a round. */
void RecordPlayerHealth();
int32 SabbatLastPlayerHealth = 0;    // +0x66cc `m_LastPlayerHealth`

/** `CNPC_VSabbatLeader::PlayerDamagedEnoughThisRound` (`0x103aabc0`) — false with no live
 *  `m_hClosestPlayer`; otherwise true only when `_DAT_104c3ce0` (**2.0**, read at file offset
 *  `0x4c3ce0` of the pinned image) `<= (float)(stat0x0f - m_LastPlayerHealth)`. */
bool PlayerDamagedEnoughThisRound() const;

/** SEAM: another character's type-0 `CVStatList_t` value. Family **Combat10** built
 *  `TypedStatValue` for THIS NPC's sheet; these two bodies read the PLAYER's, so the walk
 *  (`+0x13bc` count, `+0x13c0` table, tag `+0x10 == 0`, else the lazily built global
 *  `DAT_109f0b40`) is applied to another entity here. A candidate with no sheet answers 0, which is
 *  the empty global's own answer. */
static int32 TypedStatValueOf(const FElysiumEntity* Candidate, int32 StatId);

// --- `CNPC_VTzimisceHeadClaw` — `0x103c1d80` (slot 332) and `0x103c2230` ------------------------
//
// **OFFSETS CORRECTED.** The listing reads `m_flSlowedExpire` at `+0x6678` (`103c1db0`),
// `m_hSlowedEntity` at `+0x6674` (`103c1e02`), the player emitter at `+0x667c` (`103c1dfc`) and the
// HUD emitter at `+0x6680` (`103c1ff2`) — the checklist's walk has the first two swapped with each
// other and the last two shifted. `0x103c2230` reads the same four, which is the cross-check.

/** `CNPC_VTzimisceHeadClaw::vfunc332` (`0x103c1d80`), 956 bytes; slot 332's base (`0x1014f890`) is
 *  `return;`. Guarded on a target that is the PLAYER (`+0xa8`) and has a combat view (`+0x9c`).
 *  Seven steps, in order: `BeginSlowEntity(victim, 500.0)` only when the expiry still equals the
 *  0.0 sentinel; `m_flSlowedExpire = curtime + RandomFloat(5.0, 8.0)` (`103c1dd5`: `PUSH 8.0` then
 *  `PUSH 5.0`); latch the victim's handle; spawn `Tzim2_player_emitter` at the victim's origin and
 *  attach it at `Bip01 Spine` mode 1 when the handle is dead; emit
 *  `Character/Monster/TC_FatGuy/Sluge_Hit.wav` on channel **4** and `…/Sluge_Affected.wav` on
 *  channel **3**, both volume 1.0 attenuation 0.8 pitch 100, from the victim's slot-222 origin;
 *  spawn `HUD_Tzim2_emitter` on the player's inventory slot 0 at mode `0xe` with an empty bone;
 *  finally raise condition `0x35` when the 2-D distance reaches `_DAT_104cd108`, a **DOUBLE**
 *  reading **240.0** (`103c20dc` is `FCOMP double ptr`). */
void HeadClawSlot332(FElysiumEntity* Target);

/** `CNPC_VTzimisceHeadClaw::EndSlow` (`0x103c2230`). Gate `0x103c24a0`: act only while
 *  `m_flSlowedExpire` is above 0.0, then only when the caller forces it or the timer has reached
 *  curtime. Zero the expiry; and ONLY when the slowed entity still resolves with a combat view,
 *  `EndSlowEntity(victim, 500.0)`, clear the handle and emit `…/Sluge_Affected.wav` on channel 3
 *  from the VICTIM's slot-222 origin at attenuation 0.8. Then `UTIL_Remove` the two owned effects at
 *  `+0x667c` and `+0x6680`, each only while its handle resolves, setting both to -1. */
void TzimisceHeadClawEndSlow(bool bForce);
bool HeadClawSlowRunning() const;   // `0x103c24a0`

FElysiumEntityHandle HeadClawSlowedEntity;    // +0x6674 `m_hSlowedEntity`
FElysiumEntityHandle HeadClawPlayerEmitter;   // +0x667c `Tzim2_player_emitter`
FElysiumEntityHandle HeadClawHudEmitter;      // +0x6680 `HUD_Tzim2_emitter`

/** SEAM for `IEngineSound::EmitSound(edict, channel, wav, volume, attenuation, 0, pitch, …)` on a
 *  NAMED wav — the path the Bach shield and both Slug sounds take, which is NOT the VSound concept
 *  table family Sounds10 seams. Forwarded to `IElysiumAudio::PlayBodySound` where there is one, and
 *  recorded either way because retail's channel/volume/attenuation triple is the recovered half. */
struct FNamedWavEmit
{
	FString Wav;
	int32 Channel = 0;
	float Volume = 0.f;
	float Attenuation = 0.f;
	int32 Pitch = 0;
	FElysiumEntityHandle Emitter;   // whose `+0x2e0` edict retail passes
};
TArray<FNamedWavEmit> NamedWavEmits;
void EmitNamedWav(const FElysiumEntity* Emitter, int32 Channel, const TCHAR* Wav, float Volume,
	float Attenuation, int32 Pitch);

// --- `CNPC_VTzimisceRunner` — `0x103c3cd0` (slot 335) and `0x103c3d10` (slot 336) ---------------
//
// **NOT WIRED TO SLOTS 335/336.** Both Troika-line bodies (`0x103415f0`, `0x10341680`) are layer-0
// rows of band 0–9 with no verdict, so `gen_kernel_shape` emits their stubs and `merge_verdicts
// --band 10-18` refuses a row for an address outside this band. Each body lands under its recovered
// species name, is driven by the suite, and the gap is named in the story's answer.

/** `CNPC_VTzimisceRunner::NotifyChangeSizeSmall` (`0x103c3cd0`), four writes in order:
 *  `SetHullSizeSmall(force = 1)`; the form byte `+0x6672` (family Anim10's `bTzimisceRunnerForm`);
 *  `m_bWantsLargeHull` (`+0x5f2c`) = 0; and `+0x6674` = the engine token `DAT_1070b22c`
 *  vtable `+0x1dc` answers for 0. The base body is a pure no-op, so this arm IS the behaviour. */
void TzimisceRunnerNotifyChangeSizeSmall();

/** `CNPC_VTzimisceRunner::NotifyChangeSizeNormal` (`0x103c3d10`), the inverse and its guard: re-read
 *  the engine token and act ONLY when it differs from the one slot 335 cached, so an unchanged token
 *  leaves the runner small and the form byte set. Edge-triggered on the engine value, not on a
 *  request. */
void TzimisceRunnerNotifyChangeSizeNormal();
float RunnerHullToken = 0.f;   // +0x6674 CNPC_VTzimisceRunner (walked)

/** SEAM for `DAT_1070b22c` vtable `+0x1dc` called with 0 — the engine-interface float the runner
 *  latches and compares. `Tick()` is the only thing in this substrate whose value changes per frame
 *  in the same way; this answers the world's current time so the token is stable within a frame and
 *  differs across frames, which is the ONE property both bodies read. Named because the retail
 *  quantity is unrecovered. */
float RunnerHullEngineToken() const;

// --- `CNPC_VVampireBoss` — `0x103c63c0`, `0x103c6a00`, `0x103c6a20`, `0x103c6f40` ---------------

/** `CNPC_VVampireBoss::WaitForTransformation` (`0x103c63c0`), one gate then three writes: nothing
 *  until `curtime` passes `m_flProteanTransformStartTime + _DAT_104ce8bc` (**2.0** s); then
 *  `CVStatList_t::Set(stat 0x0f Health, 0)` on the type-0 list — a full heal, because `0x0f` is the
 *  WOUND counter; then fire the transform partner's `+0x6664` output with this as both activator and
 *  caller; then `TaskComplete(false)`. */
void WaitForTransformation();
double ProteanTransformStartTime = 0.0;      // `m_flProteanTransformStartTime`
FElysiumEntityHandle TransformPartner;       // `m_hTransformPartner`

/** `0x103c6a00` — `m_HealthPercentRecord (+0x6698) = GetCurrHealthPercent()`, the boss line's
 *  snapshot taken before a phase. Five direct callers across the boss species. */
void RecordHealthPercent();

/** `0x103c6a20` — `GetCurrHealthPercent() - m_HealthPercentRecord`, IN THAT ORDER. The percent rises
 *  with damage, so this answers POSITIVE for a body that has lost health (standing fact one). */
float HealthPercentLostSinceRecord() const;
float BossHealthPercentRecord = 0.f;   // +0x6698 `m_HealthPercentRecord` (walked)

/** `CNPC_VVampireBoss::SpawnBodyEmitters` (`0x103c6f40`): `KillBodyEmitters` FIRST, then four fixed
 *  iterations of `SpawnBodyEmitter(i, name)` over the pointer table at
 *  `PTR_s_Bip01_L_Hand_1065e6d0`, storing each spawned entity's handle into `m_hParticleEmitters[i]`
 *  (`+0x66a0`). A spawn that answers null leaves that slot's PREVIOUS handle standing rather than
 *  clearing it, which is why the kill has to run first.
 *
 *  The four attachment names read out of the pinned image at `0x65e6d0`: `Bip01 L Hand`,
 *  `Bip01 R Hand`, `Bip01 Spine`, `Bip01 Spine` — the last two are the same string. */
void VampireBossSpawnBodyEmitters();
static const TCHAR* VampireBossEmitterAttachment(int32 Region);
/** The ATTACHMENT name each `SpawnBodyEmitter` call was handed. Family Damage's
 *  `SpawnBodyEmitter(region, attachTo)` takes the attach ENTITY (retail's second argument is a
 *  `CBaseEntity*`), so the bone this table supplies is recorded beside the call rather than folded
 *  into it — it is the half of the decision the seam does not carry. */
FString BodyEmitterAttachments[4];

// --- `CNPC_VWerewolf` — `0x103ce750` (slot 448), `0x103d0db0`, `0x103d5130` (slot 76), `0x103d9f90`

/** `CNPC_VWerewolf::TaskFail` (`0x103ce750`), slot 448's species body. The DIAGNOSTIC half runs only
 *  when the running schedule (`+0x5c38`) is one of `0x160`, `0x161` or `0x162`; the rest is
 *  unconditional: `SetHullSizeSmall(1)`, `ClearMoveHint`, `ClearTeleportHint`, `m_pBreakHint` = 0,
 *  `+0x66a4` = 0, `+0x66a1` = 1, the Troika base `0x1029adb0`, `CheckStuck(NULL)`, then the zone word
 *  `+0x66e8` = 0 and `+0x6708` / `+0x670c` = -1.
 *
 *  The BREAK line prints the TELEPORT hint — a retail copy-paste bug, reproduced. */
void WerewolfTaskFail(int32 Reason);
int32 WerewolfBreakHintNode = INDEX_NONE;       // +0x66c4 `m_pBreakHint`
int32 WerewolfLastUsedTeleportHint = INDEX_NONE; // +0x66b4 `m_pLastUsedTeleportHint`
int32 WerewolfLastUsedMoveHint = INDEX_NONE;     // +0x66c0 `m_pLastUsedMoveHint`
bool bWerewolfTaskFailed = false;                // +0x66a1 (walked; retail name unrecovered)
int32 WerewolfHintNodeCacheA = INDEX_NONE;       // +0x6708 (walked; retail name unrecovered)

/** `CNPC_VWerewolf::HasPath` (`0x103d0db0`). 120 of its 138 bytes are the scope-trace frame; the
 *  body is one call, `0x102ee380(m_pNavigator, start, end)`, which stamps the navigator cache twice
 *  — `+8` from the NPC's own `+0x156c` hull and `+0xc` from the global frame word, then the same
 *  pair on the path object `0x102ecc00` — before forwarding both Vectors to `0x102fdcc0`.
 *
 *  CORRECTION: the stamping is inside `0x102ee380`, not in this body, and the body itself neither
 *  reads nor writes anything of its own. `0x102fdcc0` is the seam: this substrate stands no path
 *  object, so it answers **false** — retail's own answer for a navigator with no path. */
bool WerewolfHasPath(const FVector& StartUnits, const FVector& EndUnits) const;

/** Each `WerewolfHasPath` ask, with both endpoints, so a case can read that the forward happened
 *  while the seam refuses. */
struct FHasPathQuery
{
	FVector StartUnits = FVector::ZeroVector;
	FVector EndUnits = FVector::ZeroVector;
};
mutable TArray<FHasPathQuery> HasPathQueries;

/** `CNPC_VWerewolf::DrawDebugStatOverlays` (`0x103d5130`), slot 76's species arm. PREPENDS
 *  `Not Seen Time` and `Player Distance`, CHAINS `CNPC_VMingXiao`'s arm (`0x10366290`), then APPENDS
 *  the zone word bit by bit, five conditions, the door state, a cvar-selected hint dump and the last
 *  five rows of the schedule stack.
 *
 *  `Not Seen Time` is `max(curtime - +0x66ec, 0.0)` — the clamp at `103d51aa` is against
 *  `_DAT_104454c4` = 0.0 and the decompiler drops it — and `Player Distance` is `+0x6264`
 *  (`m_flPlayerDist`), NOT a computed range. */
void WerewolfDrawDebugStatOverlays(TArray<FString>& OutLines) const;

/** `+0x668c` (the schedule-stack array) and `+0x6698` (its count) — the last five rows the overlay
 *  prints, a null row printing `INVALID SCHEDULE`. Carried as the names the overlay reads because
 *  that is every observable the body produces. */
TArray<FString> WerewolfScheduleStack;

/** `CNPC_VWerewolf::SnapToAnimationPoint` (`0x103d9f90`), six writes and two calls in order:
 *  `MatchOriginAnglesToAnimation("Bip01", 1, 1)` snaps origin AND angles onto the animation's root
 *  bone; `SetHullSizeSmall(force = 0)`, which therefore does nothing when the body is already small;
 *  the cached fake-hull point `+0x66dc`/`+0x66e0`/`+0x66e4` cleared from `vec3_origin`; `+0x66a8` =
 *  0; and both hint-node handles `+0x6708` / `+0x670c` = -1, so the snap also drops whatever hint the
 *  Werewolf was heading for. */
void SnapToAnimationPoint();
int32 WerewolfSnapWordA = 0;   // +0x66a8 (walked; retail name unrecovered)

/** SEAM for `CBaseAnimating::MatchOriginAnglesToAnimation(bone, bOrigin, bAngles)` — the snap onto
 *  the animation's root bone. This substrate has no bone-space sampler at the kernel tier, so the
 *  request is recorded and the transform is left alone; every other write of the body still lands. */
struct FMatchOriginAnglesCall
{
	FString Bone;
	bool bOrigin = false;
	bool bAngles = false;
};
TArray<FMatchOriginAnglesCall> MatchOriginAnglesCalls;
