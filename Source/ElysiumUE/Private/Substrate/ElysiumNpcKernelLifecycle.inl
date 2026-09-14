// Story 29c-1, family **Lifecycle** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelLifecycle.cpp` and the tests in
// `Tests/ElysiumNpcKernelLifecycleTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// --- What this family is --------------------------------------------------------------------------
//
// Spawn, init, precache, save/restore, dormancy and the per-think clocks. 59 rows.
//
// TWELVE of them fill Troika-line slots and are defined (not declared) here: slots 0, 3, 4, 91,
// 116, 137, 152, 158, 423, 471, 552 and 559. **Slot 158 `IsAlive`** is the one other landed
// families were waiting on — family Bosses' slot-482 `CanPlaySequenceSpecies` and family Sounds'
// `PlaySentence` both dispatch it and took the dead arm while its stub answered false.
//
// The rest are SPECIES or VARIANT bodies of slots whose Troika-line body belongs to a later story
// (29d/29e) and whose generated stub is therefore still standing: slot 77/78 `ScriptHide`/
// `ScriptUnhide`, 103 `Spawn`, 104 `Precache`, 110 `KeyValue`, 113 `Activate`, 117 `ObjectCaps`,
// 119 `Kill`, 127 `Restore`, 130 `OnRestore`, 175 `Touch`, 180 `UpdateOnRemove`, 412–414 the think
// stamps, 434 `PrescheduleThink`, 512 `GetExpresser` and 585 `ProcessTweakParam`. Each lands under
// a NAMED method here that the slot's own body routes to when its story lands — the shape family
// **Hints** used for its slot-566/567 species halves, and for the same reason: defining the slot
// twice is a link error, and guessing at the Troika-line body is not this story's licence.
//
// Rows whose target is another struct landed on that struct: `FElysiumEntity::ObjectCaps` and
// `FElysiumEntity::ScriptHide`/`ScriptUnhide` (`ElysiumEntity.h`), `FElysiumInterestingPlace::Spawn`
// /`OnRestore`/`Reset` (`ElysiumInterestingPlace.h`), `FElysiumNpcMaker::ParseMapData`
// (`ElysiumNpcMaker.h`), `FElysiumNpcSenses::Tick` (`ElysiumNpcSenses.h`).

// --- The words this family's bodies read that 29b did not declare ---------------------------------
//
// 29b declared every word of the flattened `CAI_BaseNPCTroika` layout. What is missing is (a) base
// `CBaseEntity` words BELOW `+0x1a40`, which the NPC word table does not cover, and (b) the species
// words above `+0x665c`, where one offset means a different thing per retail class — the same
// situation families Hints and Squad declared theirs in. Each carries its offset and the retail
// class that owns it.

/** `+0x0368 m_CollisionGroup`, a `CBaseEntity` word below the NPC table. The ONE input of slot 91
 *  `ShouldCollide` (`0x100b4de0`). Nothing in this runtime writes it yet: this substrate's collision
 *  is Unreal's channel set on the body, so the retail group number has no producer. Declared so the
 *  rule has the word it reads rather than a guess. Retail's `COLLISION_GROUP_DEBRIS` is 1. */
int32 CollisionGroup = 0;

/** `+0x0500 m_flDelay`, the `CBaseDelay` trigger delay slot 152 `GetDelay` (`0x1004fc10`) answers.
 *  A `CBaseEntity` word below the NPC table; this runtime carries an output's delay on the wire
 *  (`FElysiumOutputDef`) rather than on the entity, so nothing writes this. */
float EntityDelay = 0.f;

/** `+0x1ddc`, read by `FUN_10160680` — a float scaled by the compiled constant `DAT_10725c9c`.
 *  **Unrecovered**: the body has one direct caller, no vtable slot, and neither the retail field
 *  name nor the class that owns the offset is settled. Declared by offset, as 29b declares an
 *  unsettled word. */
float Field_0x1ddc = 0.f;

/** `+0x2830` and `+0x30e9`, the two words `FUN_100e58b0` tests. **Unrecovered**, same situation:
 *  a free function with one direct caller and no oracle or port citation. */
int32 Field_0x2830 = 0;
bool bField_0x30e9 = false;

/** `+0x66a4` / `+0x66d4` / `+0x66d8` / `+0x66ec` — `CNPC_VWerewolf`'s morph-timer block, which its
 *  own `ScriptUnhide` (`0x103d4a20`) zeroes and stamps. The first three are timers, the fourth an
 *  absolute curtime stamp (`DAT_1070b228+0xc`), carried as `double` like every other stamp here.
 *  NOTE `+0x66a4` is `CNPC_VMingXiao::m_flProxyReadyTimer` on a MingXiao — one offset, two species,
 *  which is exactly why these are declared by retail class rather than by offset alone. */
float WerewolfMorphTimerA = 0.f;    // +0x66a4 CNPC_VWerewolf (walked)
float WerewolfMorphTimerB = 0.f;    // +0x66d4 CNPC_VWerewolf (walked)
float WerewolfMorphTimerC = 0.f;    // +0x66d8 CNPC_VWerewolf (walked)
double WerewolfUnhideStamp = 0.0;   // +0x66ec CNPC_VWerewolf (walked)

/** `+0x6670 CNPC_VGhoulCroucher::m_hBurningParticle` — the particle entity its `ScriptUnhide`
 *  (`0x1037c2f0`) kills on the way back up. */
FElysiumEntityHandle BurningParticle;

/** `+0x6668 CNPC_VGhoulCroucher::m_nUnawareType`, the index `UnawareTableA`/`UnawareTableB`
 *  (`0x1037b870` / `0x1037b890`) read the two static tables with. */
int32 UnawareType = 0;

/** `CNPC_VMingXiao`'s proxy block, read by `ProxyReadyTimer` (`0x10397b40`): `+0x66a4` the ready
 *  stamp (family **Bosses**' `MingXiaoProxyReadyTimer`), `+0x668c` the six-slot handle array (family
 *  **Squad**'s `Proxies`) and `+0x6684` the six-byte registered flags — the only one of the three
 *  that had no owner, so it lands here. SIX is the loop bound in the body (`iVar6 < 6`), and it is
 *  the same six as those two arrays'. */
static constexpr int32 MingXiaoProxySlots = 6;
bool bProxyRegistered[MingXiaoProxySlots] = { false, false, false, false, false, false };

/** SEAM for `thunk_FUN_101618a0(player)` — the `!playercontroller` half of slot 559
 *  `FindNamedEntity` (`0x10279090`). Retail resolves the player's own scene stand-in from the
 *  player; this runtime stands that as the `npc_VPlayerController` leaf, which a map places by name
 *  and which the player holds no pointer to. Answers the player, and names what would settle it. */
FElysiumEntity* PlayerControllerOf(FElysiumEntity* Player) const;

// --- Slots 412/413/414: the three think stamps ----------------------------------------------------
//
// `CAI_BaseNPC::GetLastUpdateThink` / `GetLastNormalThink` / `GetLastMoveThink` (`0x101a6440`,
// `0x101a6460`, `0x101a6480`) each forward to `CBaseEntity::GetLastThink` for one channel of the
// four-clock bookkeeping the schedule/condition kernel times its interrupts off. This runtime
// already CARRIES those four words — `FElysiumNpcScheduleHost::LastUpdate` / `LastNormal` /
// `LastMove` / `LastAI` — so the bodies are that read and nothing else. Named rather than defined on
// the slots, because slots 412–414's own Troika-line bodies (`0x101aa6d0` / `f0` / `710`) are a
// later story's and their generated stubs still stand.

float LastUpdateThink() const;   // 0x101a6440
float LastNormalThink() const;   // 0x101a6460
float LastMoveThink() const;     // 0x101a6480

// --- Slot 77/78/119: dormancy, by species ---------------------------------------------------------

/** `CAI_Hint::ScriptHide` (`0x102d0860`) — the base `CBaseEntity::ScriptHide` and then the hint's
 *  own `m_iDisabled` (`+0x05e8`) := 1. Pure over family **Hints**' `FHintWords`, because there is no
 *  `CAI_Hint` ENTITY in this substrate to run the base half on; the caller owns that half. */
static void HintScriptHide(FHintWords& Hint);

/** `CAI_Hint::ScriptUnhide` (`0x102d0890`) — the exact inverse, `m_iDisabled := 0`. */
static void HintScriptUnhide(FHintWords& Hint);

/** `CAI_Hint::Kill` (`0x102d08c0`), whose whole body is `JMP [[this]+0x134]` — vtable `+0x134` is
 *  slot 77, so a hint node's **Kill is its ScriptHide**, not the entity teardown every other class
 *  runs at slot 119. Verbatim: this forwards. */
static void HintKill(FHintWords& Hint);

/** `CAI_Hint::vfunc5` (`0x102d2f00`), the hint node's deleting destructor — `thunk_FUN_102d3040`
 *  then MSVC's `if (flags & 1) operator delete(this)`. What `0x102d3040` does that the KERNEL can
 *  observe is done to the **owning NPC**, so it lands as a method on the owner and not as a static
 *  over `FHintWords`:
 *
 *      if (m_hOwner resolves && owner->m_pNPC (+0x98) != NULL) {
 *          (**(DAT_10924a6c + 4))();           // the memory-alloc hook; no NPC state
 *          SetCondition(owner_npc, 0x29);      // 0x10269a20
 *          CAI_BaseNPCTroika::ClearHintNode(owner_npc, 0.0);
 *      }
 *
 *  The ORDER is retail's and is the point: the condition is raised BEFORE the hint reference is
 *  dropped, so a selector that runs on the same pass sees both the bit and the cleared node.
 *  `ClearHintNode(0.0)` is `ClearScheduleHint(0.f)` here — a ZERO reuse delay, unlike the 5.0 s
 *  `TaskFail` and the state-change path pass, because the node is going away and there is nothing
 *  left to cool down.
 *
 *  Condition `0x29` has no recovered name in the port's registrar, so it is spelled as the raw
 *  number, the convention family Conditions set for the Werewolf's `0x7a`.
 *
 *  **SEAM, stated:** the rest of `0x102d3040` unlinks the node from the engine's global hint list
 *  (`DAT_10925450` head, `+0x5d8` next, `DAT_10925454`/`DAT_1092545c` cursors, `DAT_10925458`
 *  count) and frees its `CUtlVector`s. This substrate stands hint nodes as rows and not as engine
 *  objects with a list, so there is no list to unlink from; nothing the kernel reads changes. */
void HintDeletingDestructor();

/** What `CAI_BaseNPCTroika::ScriptUnhide` (`0x102c1ec0`) hands the `scripted_sequence` that hid this
 *  body — the six words it writes into the cine entity at `+0x5f78`..`+0x5f8c`. Returned rather than
 *  written, because this runtime's beat carries no such block: see the definition. */
struct FCineUnhideRecord
{
	/** False when `m_bCineScriptHidden` (`+0x5d78`) was clear or `m_hCine` (`+0x5d74`) did not
	 *  resolve — the arm that writes nothing to a cine and only clears the latch. */
	bool bWroteToCine = false;
	int32 MoveType = 0;     // +0x5f78 <- slot 94 GetMoveType
	int32 MoveCollide = 0;  // +0x5f7c <- slot 95 GetMoveCollide
	int32 Solid = 0;        // +0x5f80 <- slot 92 GetSolid
	int32 SolidFlags = 0;   // +0x5f84 <- slot 211 GetSolidFlags
	int32 Effects = 0;      // +0x5f88 <- m_fEffects
	uint32 NpcFlagWord = 0; // +0x5f8c <- m_bfAINPCFlags
};

/** `CAI_BaseNPCTroika::ScriptUnhide` (`0x102c1ec0`), slot 78 for 62 classes — the whole tail that
 *  runs AFTER `CBaseEntity::ScriptUnhide`. Three things, in retail's order: dispatch slot 614
 *  `ResetThinkTimers`; unhide the active weapon (slot 78 on it, vtable `+0x138`); record this body's
 *  four physics words plus `m_fEffects` and `m_bfAINPCFlags` onto the cine that hid it, then clear
 *  `m_bCineScriptHidden`.
 *
 *  The slot-614 half is ALREADY CARRIED by `FElysiumNpc::OnDormancyChanged`, which cites this exact
 *  address; this body does not repeat it. */
FCineUnhideRecord TroikaScriptUnhideTail();

/** `CNPC_VWerewolf::ScriptUnhide` (`0x103d4a20`) — the base, then `+0x66ec := curtime` and
 *  `+0x66d8`/`+0x66d4`/`+0x66a4 := 0`, in that write order. */
void WerewolfScriptUnhideTail(double Now);

/** `CNPC_VGhoulCroucher::ScriptUnhide` (`0x1037c2f0`) — the base, then resolve
 *  `m_hBurningParticle` and, when it resolves to a live entity, dispatch its vtable `+0x138`
 *  (slot 78, the particle's own `ScriptUnhide`). Retail does NOT clear the handle. */
void GhoulCroucherScriptUnhideTail();

/** The species dispatcher over the three tails above: the Troika tail always, then the one row this
 *  NPC's retail class carries. Returns the cine record the Troika tail produced. */
FCineUnhideRecord ScriptUnhideSpecies(double Now);

// --- Slot 103 `Spawn`: the species bodies ---------------------------------------------------------

/** `CCineNPC::Spawn` (`0x101a6f10`), shared with `CCineAI` — the cine actor's own spawn. Pure: it
 *  reads the spawnflags and the name and answers the whole state it would have written, because the
 *  cine entity is `FElysiumScriptedSequence` here and not an NPC at all. */
struct FCineSpawnState
{
	int32 Solid = 0;                  // SetSolid(0) — SOLID_NONE
	int32 AddedSolidFlags = 4;        // AddSolidFlags(m_Collision.flags | 4)
	bool bTargetable = false;         // m_bIsBCCTargetable = 0
	bool bAlive = false;              // m_bIsAlive = 0
	bool bAutoRemoveThink = false;    // an unnamed cine, or spawnflag 0x10
	double NextThink = 0.0;           // curtime + _DAT_10449280 when the think is armed
	bool bHasStartTime = false;       // only a NAMED cine that armed the think gets one
	double StartTime = 0.0;           // curtime + _DAT_10449e10
	bool bInterruptable = true;       // spawnflag 0x20 CLEARS it
	int32 SequenceStarted = 0;
	bool bNextCineSet = false;        // m_hNextCine = -1
	int32 AddedFlags2 = 0x10;         // AddFlag2(0x10)
};
static FCineSpawnState CineSpawn(int32 SpawnFlags, bool bNamed, double Now);

/** `CAI_StandoffGoal::Spawn` (`0x102cd2d0`) — `ThinkSet(&LAB_10006672, 0, null)` then
 *  `m_flNextThink = curtime + _DAT_1044e658`. Answers the next-think it would have armed. */
static double StandoffGoalSpawnNextThink(double Now);

/** `CAI_Hint::Spawn` (`0x102d0b60`) — a hint node's own spawn: fill the unset per-hint-type
 *  defaults, turn the angle range into its dot-product test, and fold the group id into a bit.
 *  Pure over family **Hints**' `FHintWords`, which is the hint's datamap. */
static void HintSpawn(FHintWords& Hint);

/** `CAI_InterestingPlaceConverstation::Spawn` (`0x102dbc80`), whose whole body is its own field
 *  init followed by `JMP [[this]+0x1a0]` — vtable `+0x1a0` is slot 104, so **the spawn precaches at
 *  its END rather than at its start**. True means "and now run Precache", which is the recovered
 *  fact this answers. */
static bool ConversationPlaceSpawnPrecachesLast();

// --- Slot 104 `Precache`: the species bodies ------------------------------------------------------

/** One precache request a species `Precache` body issues, in retail's own order. `bModel` picks the
 *  model precacher (`*DAT_1070b22c+0x34`) over the sound one (`*DAT_1070b248`). */
struct FPrecacheRequest
{
	FString Name;
	bool bModel = false;
	/** Retail warned about this name instead of precaching it (an empty/invalid sound). */
	bool bWarnedInvalid = false;
};

/** `CAI_InterestingPlaceConverstation::Precache` (`0x102dbcb0`) — `m_iszSoundLoop` (warned, and NOT
 *  precached, when the name is empty) then `m_iszSoundOnce` (never warned). The asymmetry is a
 *  retail fact and is reproduced. */
static void ConversationPlacePrecache(const FString& SoundLoop, const FString& SoundOnce,
	TArray<FPrecacheRequest>& Out);

/** `CGenericNPC::Precache` (`0x1034aa40`) — the three weapon sounds of the
 *  `PTR_s_weapons_ar2_ar2_fire1_wav` table (`uVar3 < 0xc`, stride 4: three entries), then this
 *  entity's own model. */
static void GenericNpcPrecache(const FString& Model, TArray<FPrecacheRequest>& Out);

/** The three sound names `CGenericNPC::Precache` walks, in table order. */
static const TCHAR* const* GenericNpcWeaponSounds(int32& OutCount);

/** `CNPC_VCamera::Precache` (`0x103689c0`), shared with `CNPC_VCameraSecurity` — fall the model key
 *  back to `models/null.mdl` when it is unset or empty, precache it, then run the link-table
 *  integrity check. Answers the model that was precached; `OutLinkWarning` is retail's
 *  "spawned after links have been..." arm. */
static FString CameraPrecacheModel(const FString& AuthoredModel);

// --- Slot 110 `KeyValue`: the AI-helper-entity cascade ---------------------------------------------

/** What `CBaseEntity::KeyValue(const char*, const char*)` (`0x1009e430`) did with one key — the
 *  cascade slot 110 runs for `CAISound`, `CAI_Hint`, `CAI_InterestingPlace`,
 *  `CAI_InterestingPlaceConverstation` and `CAI_StandoffGoal`. */
enum class EKeyValueArm : uint8
{
	RenderColor,     // rendercolor / rendercolor32 -> m_clrRender RGB
	RenderAmt,       // renderamt -> m_clrRender alpha, atoi
	DisableShadows,  // disableshadows, nonzero -> m_fEffects |= 0x20
	DisableReceiveShadows,  // disablereceiveshadows, nonzero -> m_fEffects |= 0x80
	Mins,            // mins -> SetCollisionBounds(value, current maxs)
	Maxs,            // maxs -> SetCollisionBounds(current mins, value)
	Angle,           // angle -> rewritten as angles and re-dispatched
	Angles,          // angles -> vtable +0x368 SetAbsAngles
	Origin,          // origin -> vtable +0x360 SetAbsOrigin
	DataMap,         // no literal matched: walk the datamap chain (vtable +0x148)
};

/** The recovered classification of one key, with the `#` truncation retail performs FIRST applied to
 *  the name. `OutKey` is the truncated key — a `#` in a key name ends it, so `"origin#2"` is
 *  `"origin"`. */
static EKeyValueArm ClassifyKeyValue(const FString& Key, FString& OutKey);

/** The `angle` arm's rewrite (`0x1009e430` @ `1009e6f6`): a NEGATIVE value takes a literal, and any
 *  other value becomes `"<current pitch> <value> <current roll>"` — the yaw only. Retail then
 *  re-enters the cascade as `angles`. */
static FString RewriteAngleKey(float AngleValue, const FVector& CurrentAngles);

// --- Slot 113 `Activate` ---------------------------------------------------------------------------

/** `CAI_BaseNPCTroika::Activate` (`0x1028e310`) — after `CBaseEntity::Activate`, an NPC whose
 *  `Classify()` (slot 138) is non-zero applies `m_sDefaultDisposition` (`+0x6558`, or the empty
 *  string when unset) through `SetDisposition(name, 1)`.
 *
 *  `FElysiumNpc::Activate` never ran this. It does now — this is that arm, split out so a fixture can
 *  state it without standing a whole world. */
void ApplyDefaultDispositionOnActivate();

/** SEAM for slot 138 `Classify()`, the gate above. Retail's `CAI_BaseNPCTroika` body answers a
 *  non-zero class for every living NPC and 0 for the inert helper entities that share the vtable
 *  (`CAI_Hint`, `CAI_TestHull`, `CScriptedTarget`); this runtime stands only living NPCs on this
 *  leaf, so it answers non-zero. Named rather than inlined so the day slot 138 lands the gate moves
 *  with it. */
bool ClassifyIsNonZero() const;

/** `CAI_InterestingPlaceConverstation::Activate` (`0x102dbde0`) — walk `m_iszInterestingPlaces` by
 *  name, admit each hit that is a conversation interesting place whose `field[0x161]` is 1, warn
 *  about the rest, then re-arm or silence the think by `m_bEnabled`. Returns the admitted list.
 *  `OutRejected` collects the names retail `DevWarning`s. */
TArray<FElysiumEntityHandle> ConversationPlaceActivate(const FString& PlacesName, bool bEnabled,
	TArray<FString>* OutRejected = nullptr) const;

// --- Slot 127/130: save and restore ----------------------------------------------------------------

/** `CAI_BaseNPC::Restore` (`0x1027c160`), slot 127 — read the extended save header
 *  (`AIExtendedSaveHeader_t`) into `+0x19b4`, chain `CBaseCombatCharacter::Restore`, re-derive four
 *  extended-block timers, then re-link the motor and the move-and-shoot overlay. */
void RestoreExtendedHeader(float SaveTimeDelta);

/** The re-derivation `Restore` applies to its timer blocks (`thunk_FUN_101cf2f0`, twice — four
 *  floats from `m_flExtendedBlockedByFriendTimer` and three from `m_flWaitFinished`): a saved
 *  ABSOLUTE stamp is re-based onto the restored clock. Answers the re-based stamp; a stamp that was
 *  never set (0) stays 0, which is retail's own answer for an unset `FIELD_TIME`. */
static double RebaseRestoredStamp(double SavedStamp, double SaveTimeDelta);

/** `CAISound::OnRestore` (`0x100aa5a0`), slot 130 — the asm is
 *  `MOV [ESP+4], 0 / JMP [[this]+0x18]`: it overwrites its own argument with 0 and tail-jumps to
 *  slot 6 `SetCheckUntouch`, so **a restore always lands as `SetCheckUntouch(false)` whatever the
 *  caller passed**. Answers the value forwarded, which is always false. */
static bool OnRestoreForwardsCheckUntouch(bool bCallerValue);

// --- Slot 175 `Touch`, slot 180 `UpdateOnRemove` ---------------------------------------------------

/** `CCineNPC::Touch` (`0x101a75a0`), shared by `CCineAI` and `CCineAISchedule` — the body is
 *  `return;` with the parameter ignored. Nothing happens when something touches a cine actor.
 *  Answers whether the touch was consumed, which is always true (retail does not chain). */
static bool CineTouch(FElysiumEntity* Other);

/** `CAI_BaseNPC::UpdateOnRemove` (`0x1027ca30`), slot 180 for the NON-Troika branch — the squad
 *  unlink (`+0x5da4`), the hint release (`+0x5ddc`, a 0.0-delay release), the own vtable `+0x7fc`
 *  dispatch, then `CBaseCombatCharacter::UpdateOnRemove`. In that order. */
void BaseNpcUpdateOnRemove();

// --- Slot 434 `PrescheduleThink`: the species bodies ----------------------------------------------

/** `CNPC_VCamera::PrescheduleThink` (`0x10369100`), shared with `CNPC_VCameraSecurity` — an EMPTY
 *  body. A camera does no preschedule work at all, which is a fact and not a gap.
 *  `CNPC_VSabbatLeader::PrescheduleThink` (`0x103a7650`) is the retail scope-trace wrapper and an
 *  unconditional forward to `CNPC_VAndreiBlood`'s (`0x10385a30`, family Bosses').
 *
 *  Answers what THIS NPC's species does with slot 434: `Empty` for the two camera classes, `Forward`
 *  for `CNPC_VSabbatLeader`, `Base` for everybody else. */
enum class EPrescheduleSpecies : uint8 { Base, Empty, ForwardToAndreiBlood };
EPrescheduleSpecies PrescheduleSpecies() const;

// --- Slot 512 `GetExpresser`, slot 585 `ProcessTweakParam` -----------------------------------------

/** `CAI_ExpressiveNPC::GetExpresser` (`0x10260da0`) — the per-instance expresser pointer at
 *  `+0x5f48`. SEAM: there is no expression substrate here and the offset is unbound in the shape
 *  map, so this answers null, which is exactly what the base body (`0x101a6b20`) answers. */
void* ExpressiveNpcExpresser() const;

/** `CAI_BaseHumanoid::vfunc585` (`0x1025f1c0`) — the body that occupies `ProcessTweakParam`'s
 *  physical slot in the base branch is **`CAI_BaseActor::PickLookTarget`**, not a tweak-param
 *  handler. Its recovered shape is below; the answer is the entity the gaze should be aimed at plus
 *  the importance/duration randomised for it. */
struct FLookTargetPick
{
	FElysiumEntityHandle Target;
	float Importance = 0.f;
	float MinDuration = 0.f;
	float MaxDuration = 0.f;
	/** The arm that produced the pick, so a test can state WHICH one fired. */
	enum class EArm : uint8 { None, Enemy, NavigationGoal, Scan } Arm = EArm::None;
};

/** `PickLookTarget(bExcludePlayers, minTime, maxTime)`. `MaintainEyeDirection` dispatches it with
 *  `(false, 1.5, 2.5)`, the SDK-2013 defaults. */
FLookTargetPick PickLookTarget(bool bExcludePlayers, float MinTime, float MaxTime) const;

/** SEAM for `ValidHeadTarget` (`thunk_FUN_10325da0`, dispatched at vtable `+0x930`), the predicate
 *  every arm of `PickLookTarget` applies to a candidate's eye point before accepting it. This
 *  runtime carries no head-aim cone, so it answers true — the arm that ACCEPTS, which keeps retail's
 *  first-match order observable rather than emptying every arm. */
bool ValidHeadTarget(const FVector& EyePointCm) const;

// --- The free functions and the unnamed tables ------------------------------------------------------

/** `FUN_10160680` — `*(float*)(this+0x1ddc) * DAT_10725c9c`. **Unrecovered**: one direct caller, no
 *  slot, and neither the field's retail name nor the constant's value is pinned. The constant is
 *  named at the definition and the scaling is the whole body. */
float ScaleField_0x1ddc() const;

/** `FUN_100e58b0` — `*(char*)(this+0x30e9) != 0 || *(int*)(this+0x2830) == 0`. Two threshold checks
 *  over unmapped offsets; no port or oracle citation. Verbatim, arm order preserved. */
bool TestField_0x2830() const;

/** `CAISound::FUN_10026e70`, slot 153 for the five AI-helper classes — is `m_vecVelocity`
 *  (`+0x03d4`) different from the static default vector `DAT_1070d1b0/b4/b8`? That vector is the
 *  always-zero one `GetGroundVelocityToApply` (slot 210) answers, so this is "am I moving at all". */
bool HasNonDefaultVelocity() const;

/** `CNPC_VGhoulCroucher`'s two static tables, indexed by `m_nUnawareType` (`+0x6668`):
 *  `DAT_1063abcc` (`0x1037b870`) and `DAT_1063abdc` (`0x1037b890`). **Unrecovered**: the tables'
 *  purpose — message, sound or activity selection — is not settled and neither table's contents are
 *  read anywhere the corpus pins. The INDEXING is the whole recovered body and is what lands. */
int32 UnawareTableA() const;
int32 UnawareTableB() const;

/** SEAM for the two tables above. Both answer 0 and name their retail global; the day either is
 *  decoded, the row lands here and both readers come right. */
static int32 UnawareTableEntry(const TCHAR* RetailTable, int32 Index);

/** `CNPC_VMingXiao`'s `FUN_10397b40` — may `Proxy` take a proxy slot right now? Retail, arm by arm:
 *  a null argument answers false; `curtime < m_flProxyReadyTimer` answers false; the argument's own
 *  `+0x6660` slot index must resolve back to the argument through `+0x66a8`; an already-registered
 *  slot answers TRUE at once; otherwise count the six slots that are either live handles or
 *  registered, and only a count of ZERO registers this slot and answers true. */
bool ProxyReadyTimer(const FElysiumEntity* Proxy, double Now);

/** SEAM for `proxy->+0x6660`, the proxy's own slot index. `CNPC_VMingXiao`'s blood-proxy subsystem
 *  has no producer in this substrate, so this answers `INDEX_NONE` and every call to
 *  `ProxyReadyTimer` takes the "the slot did not resolve" arm. */
int32 ProxySlotIndexOf(const FElysiumEntity* Proxy) const;

/** `CNPC_VWerewolf::StartSearchTimer` (`0x103d1ca0`) and `ReportSearchTimer` (`0x103d1d60`) — a
 *  profiling pair over `rdtsc`, stamped into the STATIC pair `DAT_1093d638`/`DAT_1093d63c` and so
 *  **shared across every instance** rather than kept per NPC. The report subtracts and leaves the
 *  elapsed cycles in the same pair, and passes its second argument through unchanged.
 *
 *  Ported as file statics for exactly that reason. `ReportSearchTimer` answers its passthrough. */
static void StartSearchTimer();
static bool ReportSearchTimer(bool bPassThrough);

/** The elapsed cycle count `ReportSearchTimer` left behind — the read side of the static pair, for
 *  a test and for a debug panel. */
static uint64 SearchTimerElapsedCycles();

/** `CAI_Motor::FUN_102e1110` (`0x102e1110`) — the motor reset to default: push the navigator's move
 *  type, reset the motor state, zero the facing/move vector and set gravity back to 1.0.
 *  **The target `FElysiumNpcMotor::ResetToDefault` names a struct this runtime does not stand**, so
 *  it lands here, on the leaf that owns the motor. */
void MotorResetToDefault();

// --- `CAI_StandoffBehavior::vfunc13` (`0x102c7600`) -------------------------------------------------
//
// A standoff-behaviour selector. **There is no `CAI_StandoffBehavior` in this runtime and no
// behaviour object under the NPC at all**, so this lands the way family Hints landed its hint rules:
// the behaviour's own datamap as a typed view, and the selector as a PURE function over it, so every
// threshold and every arm is exercised without inventing a behaviour store.

/** One `CAI_StandoffBehavior`'s own words, by offset. `this+4` is the NPC it is attached to; every
 *  other offset below is the behaviour's. */
struct FStandoffWords
{
	int32 Posture = 0;             // +0x001c  the posture the selector writes (0, 2, 3)
	bool bCoverDirty = false;      // +0x0024  read only by the COND_ENEMY_DEAD arm's answer
	int32 ReactionChanceMin = 0;   // +0x0030
	int32 ReactionChanceMax = 0;   // +0x0034
	int32 ChanceThreshold = 0;     // +0x0038  the RandomInt(0,99) roll is compared against this
	double NextReactionAt = 0.0;   // +0x003c  an absolute curtime stamp
	float ReactionDelayMin = 0.f;  // +0x0040
	float ReactionDelayMax = 0.f;  // +0x0044  == 0.0f means "no range: use the min alone"
	int32 ReactionsLeft = 0;       // +0x0048  the counter the selector decrements and re-rolls
	bool bSawNewEnemy = false;     // +0x004c  latched, and cleared by the first pass that reads it
	double BlockedSince = 0.0;     // +0x0064  an absolute curtime stamp
	bool bBlockedLongEnough = false;  // +0x0054 the byte the +0x64 test sets
};

/** The conditions `0x102c7600` reads, by retail condition number, handed in so the rule is pure. */
struct FStandoffConditions
{
	bool bCond0x40 = false;  // the first gate; true takes the 0x29/0x28 answer
	bool bCond0x3f = false;  // the second gate; same answer
	bool bCond0x4c = false;  // the chance-rolled hint-timer arm
	bool bCond0x48 = false;  // the posture-2 promotion and the blocked stamp
	bool bCond0x4f = false;
	bool bCond0x51 = false;
	bool bCond0x60 = false;  // the 0x21 answer, gated on a 0x31 roll when 0x48 also stands
};

/** `CAI_StandoffBehavior::vfunc13` (`0x102c7600`) — the selector. Returns the retail schedule /
 *  task-continue code (`0x17`, `0x25`, `0x21`, `0x29`, `0x28`) or `INDEX_NONE` for "fall through to
 *  `CAI_Behavior::vfunc13`", which is the base this runtime does not carry. `bHasEnemy` is slot 156
 *  `GetEnemy() != NULL` (vtable `+0x29c`); `Hint` is the claimed hint node's words, or null.
 *
 *  The state 2 gate at the top (`m_NPCState == 2`, `+0x5cc0`) is the caller's: a standoff selector
 *  that is not in COMBAT falls straight through. */
static int32 StandoffSelect(FStandoffWords& Words, const FStandoffConditions& Conditions,
	bool bInCombatState, bool bHasEnemy, FHintWords* Hint, double Now);
