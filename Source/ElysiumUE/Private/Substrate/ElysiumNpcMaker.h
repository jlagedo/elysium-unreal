#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
// Story 29d, family Precache10: the maker's slot-104 arms record `FElysiumNpc::FPrecacheOp`, the
// one recorded-request type the kernel's precaches share. Included whole rather than duplicated
// here — every translation unit that includes this header already includes `ElysiumNpc.h`, and a
// maker's precache IS an NPC-kernel slot body that happens to land on this type.
#include "Substrate/ElysiumNpc.h"

// `npc_maker`: retail admission, quotas, timed retries and child ownership.

class FElysiumNpcMaker final : public FElysiumEntity
{
public:
	FString NpcType;
	int32 RemainingTotal = 0;       // MaxNPCCount is the mutable remaining finite total
	float SpawnFrequency = 0.0f;
	int32 LiveChildren = 0;
	int32 MaxLiveChildren = 0;
	float CachedGroundZ = 0.0f;
	FString ChildTargetName;
	bool bDisabled = false;
	bool bNpcClip = false;
	bool bFade = false;
	bool bInfinite = false;
	bool bNoDrop = false;           // base CNPCMaker declares it but does not consume it
	bool bViewCone = false;
	int32 MinPcDistance = 0;        // Source units
	// `CNPCMaker` is a `CAI_BaseNPCTroika` in retail, so it carries `m_bDisableAI` and the
	// `DisableThink` input, and `MakeNPC` `0x1034b7b0` copies its own value onto every child
	// (`0x1029f2e0` -> `0x1029f300`). The maker itself never thinks as an NPC here.
	bool bDisableAi = false;
	void InputDisableThink(const FElysiumInputArgs& Args);

	enum class EAttempt : uint8
	{
		Spawned,
		LiveLimit,
		Scene,
		Visible,
		ViewCone,
		Distance,
		Occupied,
		InvalidChild,
	};
	EAttempt LastAttempt = EAttempt::InvalidChild;

	static const TCHAR* AttemptName(EAttempt Attempt);

	// `FUN_1034b430` `0x1034b430` — `return !m_bInfChild (+0x66c3) && m_iMaxNumNPCs (+0x6660) < 1`.
	// Story 29c's checklist gives the row the verdict `rule` and the best-guess target
	// `FElysiumNpc::FUN_1034b430`; it is neither an NPC's body nor unported. Two direct callers
	// (`0x1034b7b0 MakeNPC`, `0x1034bc90 DeathNotice`) plus one from outside the closure, and the
	// port already carried the predicate verbatim. Cited rather than re-implemented — story 29c-1,
	// family Species.
	bool IsDepleted() const { return !bInfinite && RemainingTotal < 1; }

	// --- Story 29d, family SpeciesLifecycle10: slot 103 `Spawn`, the three maker arms -------------
	//
	//   `CNPCMaker::Spawn`           `0x1034afe0`  — 261 bytes
	//   `CNPCMaker_Fleshpile::Spawn` `0x1034c020`  — byte-identical but for the installed think
	//   `CNPCMaker_Zombie::Spawn`    `0x1034cc60`  — 295 bytes; a spawn jitter, a NULL disabled
	//                                                think, Relink on both paths, and the five
	//                                                police thresholds slammed to 999999
	//
	// One method with three arms keyed on the maker's own classname, exactly as slots 104, 139, 617
	// and 618 already are on this class.
	//
	// **The checklist's walk has `+0x66b0` and `+0x66b8` the wrong way round.** `vtmb_fields
	// CNPCMaker` puts `m_cLiveChildren` at `+0x66b0` and `m_flGround` at `+0x66b8`, and the listing
	// agrees: `1034b065 MOV dword ptr [ESI + 0x66b0],0x0` runs BEFORE the slot-104 dispatch and
	// `1034b0bc MOV dword ptr [ESI + 0x66b8],0x0` is the last write on both paths. It also calls the
	// collision property `+0x17c`; `1034b04c LEA ECX,[ESI + 0x270]` says `m_Collision` is `+0x270`
	// and `+0x17c` is `m_flNextThink`, the other word the body writes.

	/** Which think body the last `Spawn` installed, named by its retail address. `ThinkSet` is what
	 *  chooses between the four, and the choice is the whole difference between the three arms. */
	enum class EMakerThink : uint8
	{
		None,        // `CBaseEntity::ThinkSet(NULL)` — the zombie's disabled path
		Inert,       // `0x1000572c` -> `0x101c0b60`, a bare `RET`: the base/fleshpile disabled path
		Base,        // `0x1000696a` -> `0x1034bbf0`, `CNPCMaker`'s own think
		Fleshpile,   // `0x10010dd4` -> `0x1034c8b0`
		Zombie,      // `0x10015c4e` -> `0x1034d2d0`
	};
	EMakerThink InstalledThink = EMakerThink::None;
	static const TCHAR* MakerThinkName(EMakerThink Think);

	/** SEAM for `CCollisionProperty::SetSolid(SOLID_NONE = 0)` on `m_Collision` (`+0x270`), under a
	 *  `"CBaseEntity::SetSolid"` scope-trace frame naming this maker's targetname. Family Motor10
	 *  stands the same seam on `FElysiumNpc` as `RetailSolidType`; a maker is not an `FElysiumNpc`,
	 *  so it gets its own. Seeded to `SOLID_NONE` because that is what `Spawn` writes and nothing in
	 *  this runtime writes anything else — a maker is never solid. */
	int32 RetailSolidType = 0;

	/** SEAM for `CBaseEntity::Relink` (`0x1001514a`), which re-inserts the entity into the engine's
	 *  spatial partition. This runtime has no partition to relink into, so the call is counted —
	 *  and the COUNT is the recovered fact, because the base and fleshpile arms run it on the
	 *  enabled path only reachable through their `if`, while the zombie arm runs it on both. */
	int32 RelinkCalls = 0;

	/** `m_iPLInvestigateLevel` (`+0x6348`), `m_iPLCriminalFleeLevel` (`+0x634c`),
	 *  `m_iPLCriminalAttackLevel` (`+0x6350`), `m_iPLSupernaturalFleeLevel` (`+0x6354`) and
	 *  `m_iPLSupernaturalAttackLevel` (`+0x6358`) — the five `CAI_BaseNPCTroika` law thresholds a
	 *  maker carries in retail because `CNPCMaker` IS one. `CNPCMaker_Zombie::Spawn` writes the
	 *  literal 999999 into all five on ITSELF, so the maker entity can never cross a law threshold.
	 *
	 *  **SEAM:** `ElysiumNpcClasses.cpp` registers no police-level keyfield on `npc_maker`, and this
	 *  port's maker is not a law participant at all, so nothing reads these back. They are carried
	 *  so the zombie arm's five writes are real writes at their retail names. */
	int32 PlInvestigate = 0;
	int32 PlCriminalFlee = 0;
	int32 PlCriminalAttack = 0;
	int32 PlSupernaturalFlee = 0;
	int32 PlSupernaturalAttack = 0;

	/** `1034cd58 MOV EAX,0xf423f` — **999999**, the literal all five take. */
	static constexpr int32 ZombieMakerPoliceLevel = 999999;

	/** `1034cd25 PUSH 0x3f800000` / `1034cd20 PUSH 0x40000000` — `RandomFloat(1.0, 2.0)`, the
	 *  zombie maker's first-think jitter, added to `m_flSpawnFrequency` and curtime. */
	static constexpr float ZombieSpawnJitterMin = 1.0f;
	static constexpr float ZombieSpawnJitterMax = 2.0f;

	virtual void Spawn() override;

	// --- Story 29c-1, family Lifecycle ------------------------------------------------------------

	// +0x66cc `field_0x66cc` / +0x76cc `m_sRefMapDataBuffer` — the sub-block `CNPCMaker::ParseMapData`
	// (`0x1034b3c0`, slot 107) carves out of its own map data before forwarding to
	// `CBaseEntity::ParseMapData`. It is the child NPC's keyvalue block, which `MakeNPC` replays onto
	// each spawned child. Nothing in this runtime consumes it yet: the port's maker builds its child
	// from the registered classname alone.
	FString RefMapDataBuffer;

	// `CNPCMaker::ParseMapData` (`0x1034b3c0`), the extraction verbatim. Retail copies characters
	// until a `\0` or a literal `}` and then ALWAYS writes a `}` at the cursor — so an input with no
	// brace still ends in one, and an EMPTY input yields the single character `}`. It then sets
	// `m_sRefMapDataBuffer` from the buffer's first byte, which the `}` it just wrote makes non-zero
	// on every path: **the "empty means null" arm is unreachable**, and that is a retail fact, not a
	// simplification.
	static FString ExtractRefMapDataBlock(const FString& MapData);

	// The slot-107 body: extract, latch, and then the base's own `ParseMapData`.
	void ParseMapData(const FString& MapData);

	EAttempt CanMakeNpc(bool bBypass) const;

	EAttempt TrySpawn(bool bBypass = false);

	void InputSpawn(const FElysiumInputArgs&) { TrySpawn(/*bBypass=*/false); }

	void InputEnable(const FElysiumInputArgs&);
	void InputDisable(const FElysiumInputArgs&);
	void InputToggle(const FElysiumInputArgs& Args);

	virtual void Think() override;

	virtual void OnOwnedEntityTerminated(FElysiumEntity& Child,
		EElysiumOwnedEntityTermination Reason) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	// --- Story 29c-1, family Species: `CNPCMaker_Fleshpile`'s two overrides ------------------------
	//
	// `npc_maker_fleshpile` shares this leaf with `npc_maker` (`Substrate/ElysiumNpcClasses.cpp`
	// registers both against it), so the fleshpile's slot 617 `MakeNPC` and slot 139 `DeathNotice`
	// are species arms on this class and not a subclass — the same rule family Species applies to
	// `FElysiumNpc`. The retail class of the maker a map stood is read off its own classname, which
	// is the only discriminator either body needs.
	//
	// Both bodies talk to the ONE `npc_VAndreiBlood` in the level through the file-static
	// `DAT_10938040`, which `MakeNPC` fills lazily by classname search and dynamic cast. That
	// singleton's `m_iActiveRunnerCount` (`+0x66b8`) and `m_iKillCount` (`+0x66bc`) are family
	// Species' members on `FElysiumNpc`; both bodies reach them through the world by classname,
	// exactly as retail reaches them through the cached pointer.

	/** Is this maker the fleshpile variant? `npc_maker_fleshpile` in this runtime,
	 *  `CNPCMaker_Fleshpile` in the census. */
	bool IsFleshpileMaker() const;

	/** The one `npc_VAndreiBlood` in the level — retail's `DAT_10938040`, which `0x1034c2d0` fills
	 *  with `FindEntityByClassname(NULL, "npc_VAndreiBlood")` plus a dynamic cast the first time it
	 *  is asked and never clears. Null when the level stands none, which is the arm BOTH bodies
	 *  refuse on. */
	class FElysiumNpc* FleshpileOwner() const;

	/** `CNPCMaker_Fleshpile::OnRestore` `0x1034c260`, slot 130: bind `DAT_10938040` to the live
	 *  `npc_VAndreiBlood`, then chain Troika `OnRestore`. The chain is a seam — this leaf is not
	 *  `FElysiumNpc`. */
	void OnRestore(bool bFromLoad);
	int32 MakerOnRestoreTroikaChains = 0;

	/** `CNPCMaker_Fleshpile::MakeNPC` `0x1034c2d0`, slot 617. */
	EAttempt FUN_1034c2d0(bool bBypass);

	/** `CNPCMaker_Fleshpile::DeathNotice` `0x1034c8e0`, slot 139. */
	void FUN_1034c8e0(FElysiumEntity* Child);

	// --- Story 29d, family Precache10: slot 104, the three maker arms -----------------------------
	//
	// `classes.md` stands `CNPCMaker` as a `CAI_BaseNPCTroika` with 621 slots, so slot 104 IS the
	// NPC `Precache` virtual for a maker — but no maker is an `FElysiumNpc` in this port
	// (`FElysiumNpcMaker final : public FElysiumEntity`, registered for `npc_maker`,
	// `npc_maker_fleshpile` and `npc_maker_zombie` in `ElysiumNpcClasses.cpp`), so an
	// `FElysiumNpc` arm would never run. The three bodies are therefore ONE method with three arms,
	// keyed on the maker's own classname exactly as `IsFleshpileMaker` already keys slots 617 and
	// 139 — the same shape the overlay uses for the three maker slot-103 bodies on `Spawn`.
	//
	//   `CNPCMaker::Precache`           `0x1034b160`  — both keyfields, both developer overlays
	//   `CNPCMaker_Fleshpile::Precache` `0x1034c180`  — the model keyfield only, no overlay
	//   `CNPCMaker_Zombie::Precache`    `0x1034cde0`  — the fleshpile shape, plus two zeroed
	//                                                   equipment words and `item_w_zombie_fists`
	//
	// NOTHING IN THIS RUNTIME CALLS `Precache()` YET, for the reason `FElysiumNpc::Precache` states:
	// this substrate acquires assets for the whole map epoch before an entity stands, so wiring a
	// per-entity precache into `Spawn` would add an event retail's order does not have here.

	/** Is this maker the zombie variant? `npc_maker_zombie` here, `CNPCMaker_Zombie` in the census.
	 *
	 *  **GAP, named rather than patched:** `ElysiumNpcClasses.cpp` registers only `npc_maker` and
	 *  `npc_maker_fleshpile` against this leaf, so no map in this runtime can stand a
	 *  `CNPCMaker_Zombie` and this predicate can never answer true at runtime — even though the
	 *  census claims the classname (`CNPCMaker_Zombie_Classnames`, `npc-kernel/classes.md`) and
	 *  `docs/vtmb/wielded_weapons.md` records that the shipped maps place them. That is a
	 *  spawn-registration gap, not a fact about retail, and registering a new spawnable classname
	 *  is a change to the port's spawn surface rather than a body of this story. The arm is ported
	 *  whole and is exercised through the test latch below. */
	bool IsZombieMaker() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only: stand this maker as `npc_maker_zombie`, so the arm above can be driven although
	 *  no classname resolves to it. The same instrument `FElysiumNpc::SetRetailClassForTests` is,
	 *  and for the same reason — a body whose carrier no fixture can spawn is still a body.
	 *  Nothing in the shipping build calls this. */
	void SetZombieMakerForTests() { bZombieMakerForTests = true; }
	bool bZombieMakerForTests = false;
#endif

	/** `DAT_1070af4c`, the `developer` cvar both `CNPCMaker::Precache` overlay arms gate on:
	 *  `!cvar->IsCommand() && cvar->GetInt() >= 1`. This runtime stands no console variable for it,
	 *  so the value is retail's own shipped **0** and the overlay arms do not run. Settable because
	 *  it IS a cvar — a debug panel or a test raises it and the arm runs, which is what retail does
	 *  with `developer 1`. */
	static int32 DeveloperCvarLevel;

	/** One `NDebugOverlay::Box` request `CNPCMaker::Precache` issues under `developer >= 1`:
	 *  `0x100067f3` formats the text, `0x100092c3` binds it to this entity's origin and angles, and
	 *  `thunk_FUN_101cf390` draws it at the collision `OBBMins`/`OBBMaxs`.
	 *
	 *  SEAM: this substrate has no maker debug-overlay service and the box is developer-only and
	 *  visual, so the request is RECORDED and nothing is drawn. The text and the bounds are the
	 *  recovered half. */
	struct FDeveloperOverlayBox
	{
		FString Text;
		FVector Mins = FVector::ZeroVector;
		FVector Maxs = FVector::ZeroVector;
	};
	TArray<FDeveloperOverlayBox> DeveloperOverlayBoxes;

	/** What slot 104 precached, in retail's order. See `FElysiumNpc::IssuePrecache` for why a
	 *  precache is recorded rather than performed in this substrate. */
	TArray<FElysiumNpc::FPrecacheOp> PrecacheLog;

	/** `m_altEquipment` (`+0x1a98`) and `m_spawnEquipment` (`+0x5dec`) — the two `CAI_BaseNPCTroika`
	 *  words a maker carries in retail because `CNPCMaker` IS one, and the two
	 *  `CNPCMaker_Zombie::Precache` zeroes BEFORE it chains `CAI_BaseNPC::Precache`, discarding any
	 *  authored equipment keyfield.
	 *
	 *  SEAM, and it answers nothing: `ElysiumNpcClasses.cpp` registers `additionalequipment` and
	 *  `alternateequipment` on the NPC class only, so nothing on this port's maker ever writes
	 *  them and the base chain's `UTIL_PrecacheOther(m_spawnEquipment)` arm is never taken. They
	 *  are carried so the zombie arm's two writes are real writes rather than a dropped line. */
	FString AlternateEquipment;    // +0x1a98
	FString AdditionalEquipment;   // +0x5dec

	/** Slot 104 for all three maker classnames. */
	void Precache();

	/** The tail only the base `CNPCMaker` arm runs: the `developer >= 1` gate and the overlay box
	 *  request. The bool IS the whole state — retail's two texts are `"%s: BAD NPC Classname"`
	 *  (`0x10624f18`) and `"%s: BAD MODEL NAME"` (`0x10624f00`) and there is no third. */
	void DrawBadNameOverlay(bool bBadClassname);

	// --- Story 29d, family Lifecycle10: `MakeNPC`'s child inheritance, and the zombie maker -------
	//
	// `CNPCMaker::MakeNPC` `0x1034b7b0` copies eleven of its OWN `CAI_BaseNPCTroika` words onto each
	// child — retail's maker IS an NPC, so a mapper authoring `npc_perception` or a script-state key
	// on the maker has it inherited by everything it spawns. This port's maker is an
	// `FElysiumEntity`, so those words have no home on it; the block below is the seam that gives
	// them one.
	//
	// **It answers nothing today and names why:** `ElysiumNpcClasses.cpp` registers none of these
	// keyfields on `npc_maker`, so every field stays at its default and the copy moves the default
	// onto the child — which is what a retail maker that authors none of them does. The day a
	// keyfield lands on the maker the copy is already here.

	/** The twelve words `0x1034b7b0` copies from the maker onto the child, by retail offset and in
	 *  the listing's copy order.
	 *
	 *  **CORRECTED against the checklist's walk.** The walk calls `+0x6420`..`+0x6436` "the whole
	 *  script-state block (saved move collide, hidden, solid flags, effects, transparent, blocks
	 *  traces, occludes sound, sound override ent, the three fake-silence bytes)", which is Ghidra's
	 *  mislabelled `this_00[0x17].<field>` struct view of the child pointer. The listing copies plain
	 *  offsets (`1034ba0f MOV ECX,[ESI+0x6420] / MOV [EDI+0x6420],ECX` and so on) and the datamap
	 *  (`ElysiumNpcKernelShape.cpp`) names them: five `m_iPercentOccluded*` thresholds and three
	 *  authored policy bytes. Nine words, not eleven, and nothing about script state. */
	struct FChildInheritance
	{
		/** `+0x1584 m_RelationshipString` (`CBaseCombatCharacter`'s `string_t`). **SEAM:** no port
		 *  member carries it — `ElysiumNpcClasses.cpp` registers no `relationship` keyfield and
		 *  `ElysiumNpcKernelShapeMap.cpp` has no row for the offset — so it is copied into
		 *  `LastChildRelationshipString` and applied to nothing. */
		FString RelationshipString;
		int32 AuthoredPerception = 0;       // +0x63b0 m_iPerception
		float AuthoredVision = -1.f;        // +0x63b4 m_flVision
		float AuthoredHearing = -1.f;       // +0x63bc m_flHearing
		bool bUseInteresting = false;       // +0x63d9 m_bUseInteresting
		int32 PercentOccludedWait = 0;      // +0x6420 m_iPercentOccludedWait
		int32 PercentOccludedCover = 0;     // +0x6424 m_iPercentOccludedCover
		int32 PercentOccludedWalk = 0;      // +0x6428 m_iPercentOccludedWalk
		int32 PercentOccludedFlank = 0;     // +0x642c m_iPercentOccludedFlank
		int32 PercentOccludedChase = 0;     // +0x6430 m_iPercentOccludedChase
		bool bAllowAlertLookaround = false; // +0x6434 m_bAllowAlertLookaround
		bool bStayEntrenched = false;       // +0x6435 m_bStayEntrenched
		bool bAllowKickHintUse = false;     // +0x6436 m_bAllowKickHintUse
	};
	FChildInheritance ChildWords;

	/** The `+0x1584` copy's destination, for the seam above. */
	FString LastChildRelationshipString;

	/** Apply `ChildWords` to a freshly created child, in the listing's order, and run the two
	 *  perception derivations retail runs — **on the MAKER**, which is retail's own oddity and is
	 *  reproduced: `1034b9ee MOV ECX,ESI` puts `this` (the maker), not `EDI` (the child), in the
	 *  `this` register for `thunk_FUN_1028fb70` (`InitPerceptionDistances`) and `thunk_FUN_1028fc90`.
	 *  So the child inherits the three authored perception words and nothing derives them. */
	void ApplyChildInheritance(class FElysiumNpc& Child);

	/** SEAM for `InitPerceptionDistances` (`0x1028fb70`) and `0x1028fc90` run on the MAKER. Counted,
	 *  because a maker on this leaf carries no perception words to derive from; naming the oddity is
	 *  the recovered half. */
	int32 MakerPerceptionDerivations = 0;

	/** SEAM for the child's `ParseMapData` (`+0x1ac`) replay: `MakeNPC` copies
	 *  `m_sRefMapDataBuffer` (`+0x76cc`) byte by byte into its inline buffer at `+0x66cc` and hands
	 *  the child a `CEntityMapData` over it, then `Precache` (`+0x1bc`) and `SetClassname`
	 *  (`+0x1e8`). This runtime builds a child from the registered classname and a keyvalue map the
	 *  world applies at `Construct`, which has already happened by the time `TrySpawn` has a child —
	 *  so the replayed block is RECORDED and applied to nothing. */
	FString LastChildMapDataReplay;

	/** Slots 619 / 620, `ChildPreSpawn` and `ChildPostSpawn`. `CNPCMaker`'s own bodies
	 *  (`0x1034af30`, `0x1034af50`) are EMPTY — a `return;` apiece — so the base maker's hooks are a
	 *  fact and not a gap; only the fleshpile and zombie variants fill them, and those are other
	 *  rows. Counted so the call ORDER around `DispatchSpawn` stays assertable. */
	int32 ChildPreSpawnCalls = 0;
	int32 ChildPostSpawnCalls = 0;

	/** `+0x76d8`, the MAXIMUM player distance `CNPCMaker_Zombie::CanMakeNPC` refuses beyond, in
	 *  Source units. **SEAM:** `npc_maker_zombie` is not a registered spawn leaf here, so no keyfield
	 *  writes it and it holds the authored **0** a maker with no such key would carry. */
	int32 ZombieMaxPcDistance = 0;

	/** The child `TrySpawn` last created, so `EquipZombieFists` can reach it — retail's `MakeNPC`
	 *  RETURNS the child and this port's `TrySpawn` returns an admission verdict instead. */
	FElysiumEntityHandle LastSpawnedChild;

	/** `CNPCMaker_Zombie::CanMakeNPC` `0x1034d0a0`, slot 618. */
	EAttempt CanMakeNpcZombie(bool bBypass) const;

	/** `CNPCMaker_Zombie::MakeNPC` `0x1034d140`, slot 617 — `MakeNPC` and then the zombie's fists.
	 *  Answers the spawned child, or null when either the spawn or the item lookup refused. */
	class FElysiumNpc* EquipZombieFists(bool bBypass);

	/** SEAM for `CBaseCombatCharacter::Weapon_OwnsThisType(this, "item_w_zombie_fists", 0)` — note
	 *  retail asks the MAKER, not the spawned zombie (`1034d16a MOV ECX,EDI`, and `EDI` is `this`).
	 *  A maker on this leaf owns no weapons, so it answers **false**, which is the arm that goes on
	 *  to look the item up. See `EquipZombieFists` for why the other arm is a retail crash. */
	bool ZombieMakerOwnsFists() const;

	/** SEAM for `thunk_FUN_10136580("item_w_zombie_fists")`, retail's entity factory. This runtime's
	 *  factory is the class registry, and the item catalogue registers one class per `vdata/items`
	 *  definition only when `ElysiumItems::Install` runs — which a headless world does not do. So
	 *  the honest answer in a bare fixture is **false**, and that is retail's own "the item
	 *  definition is missing" arm: release the spawned zombie and answer null.
	 *
	 *  **GAP, named rather than patched:** the SUCCESS arm is therefore unreachable without an
	 *  installed catalogue, exactly as `IsZombieMaker`'s spawn-leaf gap is. The latch below is the
	 *  same instrument `SetZombieMakerForTests` is, for the same reason — a body whose carrier no
	 *  fixture can stand is still a body. Nothing in the shipping build sets it. */
	bool ZombieFistsItemExists() const;

#if WITH_DEV_AUTOMATION_TESTS
	/** Force the catalogue answer, in BOTH directions.
	 *
	 *  The fallback reads `FElysiumClassRegistry`, which is a **process-wide singleton**: once any
	 *  earlier suite in the same process has installed the item catalogue, `item_w_zombie_fists` is
	 *  registered and the missing-item arm stops being reachable. A fixture that asserted the
	 *  absence therefore passed alone and failed in a full run, which is an order dependency rather
	 *  than a defect in either body. Unset, the registry still answers. */
	void SetZombieFistsItemForTests(bool bExists) { ZombieFistsItemForTests = bExists; }
	void ClearZombieFistsItemForTests() { ZombieFistsItemForTests.Reset(); }
	TOptional<bool> ZombieFistsItemForTests;
#endif

	/** `"Zombies_spawning_emitter"` (`0x1062565c`), created at the maker's origin and angles and
	 *  given a 15.0-second life (`1034d249 PUSH 0x41700000`). SEAM: this runtime stands no Source
	 *  particle emitters, so the request is recorded. */
	struct FZombieSpawnEmitter
	{
		FVector Origin = FVector::ZeroVector;
		FVector Angles = FVector::ZeroVector;
		float LifetimeSeconds = 0.f;
	};
	TArray<FZombieSpawnEmitter> ZombieSpawnEmitters;

	/** What `EquipZombieFists` handed the child, in order, so a case can state the whole arm without
	 *  an item substrate: the item classname it created, whether the item's own `+0x1d0` check
	 *  refused (which is what makes retail hand it over), and the `0x40000000` flag it OR'd into the
	 *  item's flag word at `+0x204`. */
	FString LastZombieFistsItem;
	int32 ZombieFistsEquips = 0;
};
