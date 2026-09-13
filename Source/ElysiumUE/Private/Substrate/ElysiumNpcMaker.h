#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"

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

	/** `CNPCMaker_Fleshpile::MakeNPC` `0x1034c2d0`, slot 617. */
	EAttempt FUN_1034c2d0(bool bBypass);

	/** `CNPCMaker_Fleshpile::DeathNotice` `0x1034c8e0`, slot 139. */
	void FUN_1034c8e0(FElysiumEntity* Child);
};
