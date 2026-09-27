#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpc.h"

struct FElysiumClassDesc;

// `CNPCMaker` (primary vtable `0x1049f404`), built by the `npc_maker` factory — story 5 fold A4.
// `CNPCMaker_Fleshpile` (`ElysiumNpcMakerFleshpile.h`) and `CNPCMaker_Zombie`
// (`ElysiumNpcMakerZombie.h`) derive from it.
//
// **A retail maker IS a Troika NPC.** Its constructor chain runs `CAI_BaseNPC`'s (`0x1027c300`: the
// AI-list add on `DAT_1090fe10`, flag `0x20` at `+0x4c`, `m_pBaseNPC +0x94 = this`) and the Troika's
// (`0x1028d230`: flag `0x40`, `m_pTroika +0x98 = this`), so it is in every AI-list walk —
// `SetAIEnabled` `0x10265680`, `WakeNpcsNear` `0x1028d820` — and answers every slot a Troika does.
// What makes it inert as a body is its own slot table: `Spawn` (`0x1034afe0`) and `Activate`
// (`0x1034b140`) chain NO NPC body, so `NPCInit` and `NPCThink` never run; `SetSolid(SOLID_NONE)`,
// no `SetModel`, `ShouldTransmit` false; the four cones, `CanWitnessSupernatural` and the
// discipline veto (slot 72) all answer false. What it runs is its INSTALLED THINK (`m_pfnThink`):
// `MakerThink` `0x1034bbf0`, the fleshpile re-arm `0x1034c8b0`, the zombie think `0x1034d2d0`, the
// bare-`RET` inert think `0x101c0b60`, or none (`EMakerThink`).
//
// Twenty-one own vtable bodies (the vtable diff against `CAI_BaseNPCTroika`; `classes.md`'s 17 is
// the name-prefix count and misses 617-620): slot 5 is the deleting destructor (the C++
// destructor's); the rest are overrides below, each citing its address. Slots 617-620 are the
// maker's OWN virtuals past the Troika's 617-slot table (`signatures.md`), declared here.
class FElysiumNpcMaker : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CNPCMaker", FElysiumNpc)


	// --- `CNPCMaker`'s own words (datamap `0x10624718`) ------------------------------------------
	FString NpcType;                // +0x665c m_iszNPCClassname     NPCType
	int32 RemainingTotal = 0;       // +0x6660 m_iMaxNumNPCs         MaxNPCCount (the mutable total)
	float SpawnFrequency = 0.0f;    // +0x6664 m_flSpawnFrequency    SpawnFrequency
	int32 LiveChildren = 0;         // +0x66b0 m_cLiveChildren       (SAVE)
	int32 MaxLiveChildren = 0;      // +0x66b4 m_iMaxLiveChildren    MaxLiveChildren
	float CachedGroundZ = 0.0f;     // +0x66b8 m_flGround            (SAVE)
	FString ChildTargetName;        // +0x66bc m_ChildTargetName     NPCTargetname
	bool bDisabled = false;         // +0x66c0 m_bDisabled           Flag_StartDisabled
	bool bNpcClip = false;          // +0x66c1 m_bNPCClip            Flag_NPCClip
	bool bFade = false;             // +0x66c2 m_bFade               Flag_Fade
	bool bInfinite = false;         // +0x66c3 m_bInfChild           Flag_InfChild
	bool bNoDrop = false;           // +0x66c4 m_bNoDrop             Flag_NoDrop (read by the fleshpile only)
	bool bViewCone = false;         // +0x66c5 m_bViewCone           Flag_ViewCone
	int32 MinPcDistance = 0;        // +0x66c8 m_iMinPCDistance      MinPCDistance (Source units)
	// +0x76cc m_sRefMapDataBuffer (SAVE) — the text `ParseMapData` carves into the 4 KB `+0x66cc`
	// buffer. See `ParseMapData` for what the port carries and what `MakeNPC` replays.
	FString RefMapDataBuffer;

	// --- The own vtable slots (`CNPCMaker`, `0x1049f404`) ----------------------------------------
	// Slot 72 `0x1034aef0` — `XOR AL,AL; RET 4`: the discipline target filter `0x101e1a60` asks it,
	// so no discipline targets a maker. Inherited by both variants.
	virtual bool Slot72(int32 Discipline) override;
	// Slot 82 `0x1034ab50` — `&datamap_CNPCMaker`; this port's datamap is the class descriptor.
	virtual void* GetDataDescMap() override;
	// Slot 86 `0x1034af10` — `return false`: a maker is never transmitted, so it draws nothing.
	virtual bool ShouldTransmit(int32 Arg1, void* Edict, void* CheckBits, int32 Arg4, int32 Arg5) override;
	// Slot 103 `0x1034afe0`. Chains no base `Spawn`: no body, no model, no `NPCInit`.
	virtual void Spawn() override;
	// Slot 104 `0x1034b160`. Chains `CAI_BaseNPC::Precache` `0x1027bb50` DIRECT, not the Troika's.
	virtual void Precache() override;
	// Slot 107 `0x1034b3c0`. `MapData` is this port's `CEntityMapData`: the entity's keyvalue TEXT,
	// passed as `const FString*`.
	virtual void ParseMapData(void* MapData) override;
	// Slot 113 `0x1034b140` — an EMPTY body; it does not even chain `CBaseEntity::Activate`, so the
	// Troika `Activate` (disposition, relationship, senses, admission, `NPCInit`) never runs.
	virtual void Activate() override;
	// Slot 123 `0x1034bd30` — an empty body (the debug overlay slot; verdict dead).
	virtual void DrawDebugGeometryOverlays() override;
	// Slot 139 `0x1034bc90`. `CNPCMaker_Fleshpile` overrides it (`0x1034c8e0`) and calls this one.
	virtual void DeathNotice(FElysiumEntity* Child) override;
	// Slots 362-365 `0x1034ae70` / `0x1034ae50` / `0x1034aeb0` / `0x1034ae90` — every cone is false.
	// The variants' own copies (`0x1034bf10`.., `0x1034cb50`..) are byte-identical and inherited.
	virtual bool FInViewCone(const FVector& PointCm) override;
	virtual bool FInViewCone(FElysiumEntity* Candidate) override;
	virtual bool FInAimCone(const FVector& TargetCm) override;
	virtual bool FInAimCone(FElysiumEntity* AimTarget) override;
	// Slots 370/371 `0x1034adf0` / `0x1034ae20` — this-adjusting forwards through slots 368/369.
	virtual FVector HeadDirection2D() override;
	virtual FVector HeadDirection3D() override;
	// Slot 587 `0x1034aed0` — `return false`: a maker is never a masquerade witness.
	virtual bool CanWitnessSupernatural(int32 Level) override;

	// Slots 617-620, the maker's own virtuals.
	// Slot 617 `0x1034b7b0` `MakeNPC(bool bypass)` — returns the child (the ledger typed it void; the
	// listing's `InputSpawn` tail jump, `MakerThink`'s test and `0x10310c10`'s use all read EAX).
	virtual FElysiumNpc* MakeNPC(bool bBypass);
	// Slot 618 `0x1034b580` `CanMakeNPC(bool bypass)`.
	virtual bool CanMakeNPC(bool bBypass);
	// Slot 619 `0x1034af30` / slot 620 `0x1034af50` — both `RET 4`, and so are both variants'.
	virtual void ChildPreSpawn(FElysiumNpc* Child);
	virtual void ChildPostSpawn(FElysiumNpc* Child);

	// --- The installed think (`m_pfnThink`) -------------------------------------------------------
	/** Which body `ThinkSet` last installed, named by its retail address. The port has no think
	 *  pointer; this enum is it, saved with the record (retail saves the pointer through the
	 *  datamap's FUNCTIONTABLE rows). */
	enum class EMakerThink : uint8
	{
		None,        // `ThinkSet(NULL)`: Disable, depletion, the zombie's disabled Spawn
		Inert,       // `0x1000572c` -> `0x101c0b60`, a bare `RET`: the base/fleshpile disabled Spawn
		Base,        // `0x1000696a` -> `0x1034bbf0` `MakerThink`: Spawn, and Enable on all three
		Fleshpile,   // `0x10010dd4` -> `0x1034c8b0`: the fleshpile's enabled Spawn
		Zombie,      // `0x10015c4e` -> `0x1034d2d0`: the zombie's enabled Spawn
	};
	EMakerThink InstalledThink = EMakerThink::None;
	static const TCHAR* MakerThinkName(EMakerThink Think);

	/** The entity think: `m_pfnThink(this)`, never `NPCThink`. The decision pass, the
	 *  `m_bDisableAI` gate and the `g_AIDisabled` gate are all `NPCThink`'s and none runs here, so a
	 *  maker keeps its cadence through a feed. The variants add their own think bodies. */
	virtual void Think() override;
	/** `0x1034bbf0`, `CNPCMaker`'s think: slot 617 `MakeNPC(false)`; a child or a full live
	 *  ceiling re-arms at `m_flSpawnFrequency`, anything else at `RandomFloat(1, 2)`. */
	void MakerThink();

	// --- Inputs (datamap `0x10624718`) -----------------------------------------------------------
	// `InputSpawnNPC` `0x1034b500` — writes 0 over its argument and tail-jumps into slot 617.
	void InputSpawn(const FElysiumInputArgs& Args);
	// `InputEnable` `0x1034b520` -> `0x1034b490`.
	void InputEnable(const FElysiumInputArgs& Args);
	// `InputDisable` `0x1034b540` -> `0x1034b4d0`.
	void InputDisable(const FElysiumInputArgs& Args);
	// `InputToggle` `0x1034b560` -> `0x1034b460`.
	void InputToggle(const FElysiumInputArgs& Args);
	// The datamap inputs on the `CNPCMaker` descriptor (`ElysiumNpcClasses.cpp`).
	static void AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass);

	// `0x1034b490` Enable: refuse when depleted, else clear the latch, install the BASE think
	// (`0x1000696a`, on every maker class) and stamp `m_flNextThink = curtime`.
	void Enable();
	// `0x1034b4d0` Disable: latch set, `ThinkSet(NULL)`; `m_flNextThink` is not written.
	void Disable();

	// `0x1034b430` — `!m_bInfChild && m_iMaxNumNPCs < 1`.
	bool IsDepleted() const { return !bInfinite && RemainingTotal < 1; }

	// `CNPCMaker::ParseMapData`'s extraction verbatim: copy until `\0` or `}`, then ALWAYS write a
	// `}` — an input with no brace still ends in one and an EMPTY input yields `}`. The
	// `m_sRefMapDataBuffer` latch reads the first byte, which that `}` makes non-zero on every path.
	static FString ExtractRefMapDataBlock(const FString& MapData);

	// --- Port diagnostics -------------------------------------------------------------------------
	/** Why the last admission (slot 618) or creation refused, or `Spawned`. Retail answers a bool
	 *  and a pointer; the reason is the port's, for the debug panel and the tests. */
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

	/** SEAM for `CBaseEntity::Relink` (`0x1001514a`): this runtime has no spatial partition, so the
	 *  call is counted. The base and fleshpile arms run it inside each branch, the zombie's once
	 *  after the join. */
	int32 RelinkCalls = 0;

	/** `DAT_1070af4c`, the `developer` cvar `CNPCMaker::Precache`'s overlay arms gate on (`>= 1`).
	 *  No console variable stands for it; the value is retail's shipped 0, settable as a cvar is. */
	static int32 DeveloperCvarLevel;
	/** One `NDebugOverlay::Box` request `CNPCMaker::Precache` issues under `developer >= 1`. SEAM:
	 *  no maker debug-overlay service; the text and the bounds are recorded, nothing is drawn. */
	struct FDeveloperOverlayBox
	{
		FString Text;
		FVector Mins = FVector::ZeroVector;
		FVector Maxs = FVector::ZeroVector;
	};
	TArray<FDeveloperOverlayBox> DeveloperOverlayBoxes;

	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnOwnedEntityTerminated(FElysiumEntity& Child,
		EElysiumOwnedEntityTermination Reason) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

protected:
	/** The model half every maker `Precache` opens with (`0x1034b160`, `0x1034c180`, `0x1034cde0`
	 *  share it byte for byte): slot 9 `GetModelName` empty -> `Warning(... missing modelname)` and
	 *  `UTIL_Remove(this)`; otherwise `PrecacheModel(model, 0)`. False when the maker removed itself.
	 *  `bBadModelOverlay` is the base arm's `developer` overlay, which the variants drop. */
	bool PrecacheMakerModel(bool bBadModelOverlay);
	/** The `developer >= 1` overlay box the base `Precache` issues: `"%s: BAD NPC Classname"`
	 *  (`0x10624f18`) or `"%s: BAD MODEL NAME"` (`0x10624f00`). */
	void DrawBadNameOverlay(bool bBadClassname);

	/** Slot 103's common prefix and suffix, in retail's order: `SetSolid(SOLID_NONE)`,
	 *  `m_cLiveChildren = 0`, slot 104, `m_bInfChild` => `m_bFade`. */
	void SpawnPrefix();

	/** `CanMakeNPC`'s ground cache: when `m_flGround == 0.0` (compared against 0, not a sentinel),
	 *  trace `2048` units down (mask `0x2400b`) and keep the end Z. */
	void CacheGroundZ();
	/** `UTIL_EntitiesInBox(.., 0x2080)` (`0x101cca80`) over `[origin.xy ± 34] x [FloorZ, origin.z]`:
	 *  true when the player or an `FL_NPC` body stands in it. The base passes `origin.z` (a flat box);
	 *  the fleshpile passes its cached ground unless `m_bNoDrop`. */
	bool IsSpawnBoxOccupied(float FloorZ) const;

	/** The nearest live entity of `Classname` within `RadiusUnits` of `PointCm`,
	 *  `FindEntityByClassnameNearest` `0x100f7d50`: squared distance STRICTLY below the radius
	 *  squared, first-listed on a tie. */
	static FElysiumEntity* FindNearestByClassname(FElysiumEntityWorld& InWorld, const TCHAR* Classname,
		const FVector& PointCm, float RadiusUnits);
};
