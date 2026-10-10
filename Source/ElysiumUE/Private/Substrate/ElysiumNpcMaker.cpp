#include "Substrate/ElysiumNpcMaker.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"

// `CNPCMaker` — story 5 fold A4. Every body is the retail body at the address its comment names,
// read off the listing (`vtmb_asm`) and the decompilation (`vtmb_code`). The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md` ("The NPC makers") and `docs/vtmb/entity_io.md` (`npc_maker`).

namespace
{
	// `SetSolid(SOLID_NONE = 0)`, the one solidity write every maker `Spawn` makes.
	constexpr int32 GMakerSolidNone = 0;
	// `_DAT_1046bacc` = 2048.0 — the downward trace depth the ground cache uses.
	constexpr float GMakerGroundTraceDepthUnits = ElysiumNpcTunables::TwoThousandFortyEight;
	// `_DAT_1049ffac` = 34.0 — the spawn box half-extent (x and y).
	constexpr float GMakerSpawnBoxHalfExtentUnits = ElysiumNpcTunables::ThirtyFour;
	// `MakeNPC`'s spawnflags: `4` (`SF_NPC_FALL_TO_GROUND`), `0x204` when `m_bFade`.
	constexpr int32 GMakerChildSpawnFlags = 0x4;
	constexpr int32 GMakerChildFadeSpawnFlags = 0x204;
	// `MakerThink`'s retry band, `RandomFloat(0x3f800000, 0x40000000)` = 1.0 .. 2.0 s.
	constexpr float GMakerRetryMinSeconds = 1.0f;
	constexpr float GMakerRetryMaxSeconds = 2.0f;
}

const TCHAR* FElysiumNpcMaker::AttemptName(EAttempt Attempt)
{
	switch (Attempt)
	{
	case EAttempt::Spawned:      return TEXT("spawned");
	case EAttempt::LiveLimit:    return TEXT("live-limit");
	case EAttempt::Scene:        return TEXT("scene");
	case EAttempt::Visible:      return TEXT("visible");
	case EAttempt::ViewCone:     return TEXT("view-cone");
	case EAttempt::Distance:     return TEXT("distance");
	case EAttempt::Occupied:     return TEXT("occupied");
	case EAttempt::InvalidChild: return TEXT("invalid-child");
	}
	return TEXT("unknown");
}

const TCHAR* FElysiumNpcMaker::MakerThinkName(EMakerThink Think)
{
	switch (Think)
	{
	case EMakerThink::None:      return TEXT("none");
	case EMakerThink::Inert:     return TEXT("inert");
	case EMakerThink::Base:      return TEXT("base");
	case EMakerThink::Fleshpile: return TEXT("fleshpile");
	case EMakerThink::Zombie:    return TEXT("zombie");
	}
	return TEXT("unknown");
}

// --- Own slots ------------------------------------------------------------------------------------

// Slot 72: `0x1034aef0`.
bool FElysiumNpcMaker::Slot72(int32 Discipline)
{
	(void)Discipline;
	return false;
}

void FElysiumNpcMaker::SpawnPrefix()
{
	// 1. `1034b04c LEA ECX,[ESI+0x270] / CALL 0x1000428c` — `SetSolid(SOLID_NONE)` on `m_Collision`,
	//    under a `"CBaseEntity::SetSolid"` scope frame. The entity's `m_Collision` record.
	RetailSolidType = GMakerSolidNone;
	++RetailSolidSets;
	// 2. `1034b065` — `m_cLiveChildren = 0`, BEFORE the slot-104 dispatch.
	LiveChildren = 0;
	// 3. `1034b06f CALL [EAX+0x1a0]` — slot 104 `Precache`, VIRTUAL: each class takes its own body.
	Precache();
	// 4. `1034b075` — `if (m_bInfChild) m_bFade = 1`: infinite implies fade.
	if (bInfinite)
	{
		bFade = true;
	}
}

// Slot 103: `0x1034afe0`, in retail's order. No base `Spawn`: no body, no model, no `NPCInit`.
void FElysiumNpcMaker::Spawn()
{
	SpawnPrefix();
	// 5. `1034b086` — the enabled/disabled split, each arm with its own `Relink`.
	if (!bDisabled)
	{
		// `1034b096 PUSH 0x1000696a` -> `MakerThink` `0x1034bbf0`; `m_flNextThink = freq + curtime`.
		InstalledThink = EMakerThink::Base;
		ArmThinkAt((World != nullptr ? World->NowSeconds() : 0.0) + static_cast<double>(SpawnFrequency));
	}
	else
	{
		// `1034b0c8 PUSH 0x1000572c` -> the inert think `0x101c0b60` (a bare `RET`). `m_flNextThink` is
		// NOT written: the entity keeps whatever it had, and the body that would run does nothing.
		InstalledThink = EMakerThink::Inert;
	}
	// 6. `CBaseEntity::Relink` (`0x1001514a`) — inside either arm, once.
	++RelinkCalls;
	// 7. `1034b0bc` / `1034b0d9` — `m_flGround = 0`, the last write on both arms.
	CachedGroundZ = 0.0f;
}

// Slot 104: `0x1034b160`.
void FElysiumNpcMaker::Precache()
{
	// The model half, with the base arm's `developer` overlay on its failure path.
	if (!PrecacheMakerModel(/*bBadModelOverlay=*/true))
	{
		return;
	}
	// Retail calls `CAI_BaseNPC::Precache` DIRECT here; that body is asset loading
	// (0019 story 6: `Bake`), so the port carries no call.
	// `m_iszNPCClassname` (`+0x665c`): ONLY the base arm checks it for emptiness.
	if (NpcType.IsEmpty())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s at %.0f %.0f %.0f missing NPCClassname"),
			*DebugString(), Origin.X, Origin.Y, Origin.Z);
		Kill();   // `UTIL_Remove(this)`
		DrawBadNameOverlay(/*bBadClassname=*/true);
		return;
	}
	// `UTIL_PrecacheOther(m_iszNPCClassname)`.
	NpcKernelPrecache10Shared::Precache10Other(*this, NpcType);
}

bool FElysiumNpcMaker::PrecacheMakerModel(bool bBadModelOverlay)
{
	// Slot 9 `GetModelName` read three times for "unset or empty"; the keyfield is `Model`.
	if (Model.IsEmpty())
	{
		// `Warning("%s at %.0f %.0f %0.f missing modelname\n")` (`0x105470e4`, retail's `%0.f` typo
		// prints the same as `%.0f`), then `UTIL_Remove(this)`.
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s at %.0f %.0f %.0f missing modelname"),
			*DebugString(), Origin.X, Origin.Y, Origin.Z);
		Kill();
		if (bBadModelOverlay)
		{
			DrawBadNameOverlay(/*bBadClassname=*/false);
		}
		return false;
	}
	// `PrecacheModel(model, 0)` through the engine (`(*DAT_1070b22c)+0x34`).
	NpcKernelPrecache10Shared::Precache10Model(*this, *Model, 0);
	return true;
}

int32 FElysiumNpcMaker::DeveloperCvarLevel = 0;

void FElysiumNpcMaker::DrawBadNameOverlay(bool bBadClassname)
{
	// `1034b21f` / `1034b2bf`: `if (cvar->IsCommand()) return; if (cvar->m_nValue <= 0) return;`.
	if (DeveloperCvarLevel < 1)
	{
		return;
	}
	// `0x100067f3` formats the text with `GetDebugName()`; `thunk_FUN_101cf390` draws the box at
	// `m_Collision`'s OBB. SEAM: no OBB and no overlay service — the bounds are zero, nothing draws.
	FDeveloperOverlayBox Box;
	Box.Text = bBadClassname
		? FString::Printf(TEXT("%s: BAD NPC Classname"), *DebugString())
		: FString::Printf(TEXT("%s: BAD MODEL NAME"), *DebugString());
	DeveloperOverlayBoxes.Add(Box);
}

FString FElysiumNpcMaker::ExtractRefMapDataBlock(const FString& MapData)
{
	// `while (c != '\0' && c != '}') { *out++ = c; c = *++in; } *out = '}';`
	int32 Brace = INDEX_NONE;
	const FString Body = MapData.FindChar(TEXT('}'), Brace) ? MapData.Left(Brace) : MapData;
	return Body + TEXT("}");
}

// Slot 107: `0x1034b3c0`.
void FElysiumNpcMaker::ParseMapData(void* MapData)
{
	// This port's `CEntityMapData` is `FElysiumEntityMapData` (`ElysiumEntityDefs.h`, L0-r017): the
	// parsed pairs the base walks, and the raw text a class that stashes it reads -- null for a baked
	// row, which arrives parsed.
	const FElysiumEntityMapData* Map = static_cast<const FElysiumEntityMapData*>(MapData);
	const FString* Text = Map != nullptr ? Map->Text : nullptr;
	// Copy into `+0x66cc` up to `}`, always write `}`, then latch `m_sRefMapDataBuffer` (`+0x76cc`)
	// from the first byte — never null, because the `}` was just written.
	RefMapDataBuffer = ExtractRefMapDataBlock(Text != nullptr ? *Text : FString());
	// Then `CBaseEntity::ParseMapData` `0x1009e280` (thunk 0x1000915b): one virtual `KeyValue` per pair.
	// SEAM: the world never hands a maker its raw text (maps arrive as parsed defs), so `+0x76cc` holds
	// `}` alone in play; `MakeNPC`'s replay reads the parsed form of the same block (`Def->Keys`).
	FElysiumEntity::ParseMapData(MapData);
}

// Slot 113: `0x1034b140` — `RET`.
void FElysiumNpcMaker::Activate()
{
}

// Slot 139: `0x1034bc90`.
void FElysiumNpcMaker::DeathNotice(FElysiumEntity* Child)
{
	if (Child == nullptr)
	{
		return;
	}
	// `if (!child->m_bHasCalledMakerDeathNotice (+0x44c)) { ... = 1; ...}`. The port's latch is
	// `FElysiumEntity::bOwnerTerminationNotified`, which `NotifyOwnerOfTermination` sets immediately
	// before it dispatches this slot — the same byte, set at the same point of the same chain.
	//
	// `child->IsAlive()` (slot 158): a child removed while alive REFUNDS the total — UNCONDITIONALLY
	// (`1034bcb7 INC [ESI+0x6660]`, no `m_bInfChild` test); a dead one fires `m_OnNPCDied` with the
	// child as activator and the maker as caller.
	if (Child->IsAlive())
	{
		++RemainingTotal;
	}
	else
	{
		static const FName OnNpcDied(TEXT("OnNPCDied"));
		FireOutput(OnNpcDied, Child->Handle);
	}
	// `0x1034b430` depleted -> `m_OnLastNPCDied`.
	if (IsDepleted())
	{
		static const FName OnLastNpcDied(TEXT("OnLastNPCDied"));
		FireOutput(OnLastNpcDied, Child->Handle);
	}
	// `m_cLiveChildren = max(m_cLiveChildren - 1, 0)` (`uVar2 & ((int)uVar2 < 0) - 1`).
	LiveChildren = FMath::Max(0, LiveChildren - 1);
}

void FElysiumNpcMaker::OnOwnedEntityTerminated(FElysiumEntity& Child, EElysiumOwnedEntityTermination Reason)
{
	// A child's `Event_Killed` / `UpdateOnRemove` dispatches its owner's slot 139 — VIRTUAL, so the
	// fleshpile's `0x1034c8e0` runs on a fleshpile maker. `IsAlive` tells the two arms apart.
	(void)Reason;
	DeathNotice(&Child);
}

// Slots 362-365: `0x1034ae70`, `0x1034ae50`, `0x1034aeb0`, `0x1034ae90`.
bool FElysiumNpcMaker::FInViewCone(const FVector& PointCm)
{
	(void)PointCm;
	return false;
}

bool FElysiumNpcMaker::FInViewCone(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return false;
}

bool FElysiumNpcMaker::FInAimCone(const FVector& TargetCm)
{
	(void)TargetCm;
	return false;
}

bool FElysiumNpcMaker::FInAimCone(FElysiumEntity* AimTarget)
{
	(void)AimTarget;
	return false;
}

// Slot 587: `0x1034aed0`.
bool FElysiumNpcMaker::CanWitnessSupernatural(int32 Level)
{
	(void)Level;
	return false;
}

// --- 617-620 ----------------------------------------------------------------------------------------

void FElysiumNpcMaker::CacheGroundZ()
{
	// `if (m_flGround == _DAT_104454c4 (0.0))` — so a maker over ground at exactly Z 0 re-traces on
	// every attempt, as retail's does. `TraceRay(from origin, to origin - 2048, mask 0x2400b)`.
	if (CachedGroundZ != 0.0f)
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	CachedGroundZ = Embodiment != nullptr
		? Embodiment->ResolveNpcMakerGroundZ(Origin, GMakerGroundTraceDepthUnits * ElysiumMove::U)
		: static_cast<float>(Origin.Z);
}

bool FElysiumNpcMaker::IsSpawnBoxOccupied(float FloorZ) const
{
	const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	// The box `[origin.xy ± 34] x [FloorZ, origin.z]` (`1034b686`..`1034b72b`), enumerated for
	// `FL_CLIENT | FL_NPC` (`1034b715 PUSH 0x2080`).
	return Embodiment != nullptr && Embodiment->IsNpcMakerSpawnAreaOccupied(
		FVector(Origin), GMakerSpawnBoxHalfExtentUnits * ElysiumMove::U, FloorZ);
}

// Slot 618: `0x1034b580`.
bool FElysiumNpcMaker::CanMakeNPC(bool bBypass)
{
    auto TraceAttempt = [this](bool bAccepted = false) -> bool
    {
#if !UE_BUILD_SHIPPING
        if (World && World->HasAiTraceSink())
        {
            const float HalfExtentCm = GMakerSpawnBoxHalfExtentUnits * ElysiumMove::U;
            FString Candidates;
            const IElysiumEmbodiment* Services = World->Embodiment();
            if (!Services || !Services->DescribeNpcMakerSpawnArea(Origin, HalfExtentCm, static_cast<float>(Origin.Z), Candidates))
                Candidates = TEXT("candidates=unavailable:no maker geometry reader");
            const FVector BoxMin(Origin.X - HalfExtentCm, Origin.Y - HalfExtentCm, Origin.Z);
            const FVector BoxMax(Origin.X + HalfExtentCm, Origin.Y + HalfExtentCm, Origin.Z);
            World->EmitAiTrace(*this, FName(TEXT("makerattempt")), FString::Printf(
                TEXT("gate=%s live=%d max_live=%d global=%d box_min=%s box_max=%s %s"),
                AttemptName(LastAttempt), LiveChildren, MaxLiveChildren, World->IsNpcMakerSceneBlocked() ? 1 : 0,
                *BoxMin.ToString(), *BoxMax.ToString(), *Candidates)); // synchronous 0x1034b580 admission/refusal
        }
#endif
        return bAccepted;
    };
	// 1. A bypass answers yes before anything else.
	if (bBypass)
	{
		LastAttempt = EAttempt::Spawned;
		return TraceAttempt(true);
	}
	// 2. `0 < m_iMaxLiveChildren && m_iMaxLiveChildren <= m_cLiveChildren`.
	if (MaxLiveChildren > 0 && LiveChildren >= MaxLiveChildren)
	{
		LastAttempt = EAttempt::LiveLimit;
		return TraceAttempt();
	}
	// 3. `DAT_106c8a41`, the global no-spawn byte (a scene holds the stage).
	if (World != nullptr && World->IsNpcMakerSceneBlocked())
	{
		LastAttempt = EAttempt::Scene;
		return TraceAttempt();
	}
	// 4. With a player (`0x101cda50`): npcclip visibility (`0x101d1a90`), the player's view cone
	//    (the player's slot 363 asked of this maker), and the Euclidean minimum distance, truncated.
	const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Player != nullptr)
	{
		if (bNpcClip && Embodiment != nullptr && Embodiment->IsNpcMakerVisibleFromPlayer(Origin))
		{
			LastAttempt = EAttempt::Visible;
			return TraceAttempt();
		}
		if (bViewCone && Embodiment != nullptr && Embodiment->IsNpcMakerInPlayerViewCone(Origin))
		{
			LastAttempt = EAttempt::ViewCone;
			return TraceAttempt();
		}
		if (MinPcDistance > 0)
		{
			const int32 DistanceUnits = FMath::TruncToInt(FVector::Dist(Player->Origin, Origin) / ElysiumMove::U);
			if (DistanceUnits < MinPcDistance)
			{
				LastAttempt = EAttempt::Distance;
				return TraceAttempt();
			}
		}
	}
	// 5. `UTIL_EntitiesInBox(list, 2, mins, maxs, 0x2080)` (`0x101cca80`) over a box FLAT at the
	//    maker's own Z: `1034b6a5` / `1034b6e3` copy `GetAbsOrigin().z` into both corners and
	//    `1034b70c` re-reads it into `mins.z` — the cached ground plays no part (corrected 2026-09-27,
	//    the port used to float the box at `m_flGround`). Occupied when the count is non-zero.
	if (IsSpawnBoxOccupied(static_cast<float>(Origin.Z)))
	{
		LastAttempt = EAttempt::Occupied;
		return TraceAttempt();
	}
	LastAttempt = EAttempt::Spawned;
	return TraceAttempt(true);
}

// Slot 619: `0x1034af30` — `RET 4`.
void FElysiumNpcMaker::ChildPreSpawn(FElysiumNpc* Child)
{
	(void)Child;
}

// Slot 620: `0x1034af50` — `RET 4`.
void FElysiumNpcMaker::ChildPostSpawn(FElysiumNpc* Child)
{
	(void)Child;
}

// Slot 617: `0x1034b7b0`, in the listing's order.
FElysiumNpc* FElysiumNpcMaker::MakeNPC(bool bBypass)
{
	if (World == nullptr)
	{
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	// 1. The ground cache, BEFORE admission and on the bypass path too.
	CacheGroundZ();
	// 2. Slot 618 `CanMakeNPC(bypass)`, VIRTUAL (`CALL [EAX+0x9a8]`): the zombie's body on a zombie.
	if (!CanMakeNPC(bBypass))
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Spawn rejected: %s (live %d/%d)"),
			*DebugString(), AttemptName(LastAttempt), LiveChildren, MaxLiveChildren);
		return nullptr;
	}
	// 3. `CreateEntityByName(m_iszNPCClassname)`, with the maker's own map data replayed onto it
	//    (`1034b928`..`1034b982`: `+0x76cc` copied into `+0x66cc`, handed to the child's slot 107
	//    `ParseMapData`) and `SetClassname(m_iszNPCClassname)` (slot 122). The port builds the child
	//    from a def, so the replay IS the def: every key of the maker's own block — its targetname
	//    included — with `classname` superseded by `m_iszNPCClassname` exactly as `SetClassname`
	//    supersedes it. A key the child's datamap does not name is ignored by it, as retail's
	//    `KeyValue` ignores it.
	//
	//    One key is not replayed: `StartHidden` (`+0xe0 m_bStartHidden`). Retail consumes it only in
	//    `CBaseEntity::PostSpawn` `0x100aaf30`, which the map loader calls and `MakeNPC`'s
	//    `DispatchSpawn` does not, so a child of a hidden maker stands visible; this port consumes it
	//    at `Construct`. **Named divergence:** the child's `m_bStartHidden` word stays 0 rather than
	//    carrying the maker's value; nothing reads it after spawn.
	FElysiumEntityDef ChildDef;
	ChildDef.Classname = NpcType;
	ChildDef.TargetName = Def != nullptr ? Def->TargetName : TargetName;
	ChildDef.Origin = Origin;
	if (Def != nullptr)
	{
		for (const TPair<FString, FString>& KV : Def->Keys)
		{
			if (KV.Key.Equals(TEXT("classname"), ESearchCase::IgnoreCase)
				|| KV.Key.Equals(TEXT("StartHidden"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			ChildDef.Keys.Add(KV.Key, KV.Value);
		}
		// The maker's own output rows ride the replayed block too (an output IS a keyvalue in the
		// map data); the child keeps the ones its datamap names.
		ChildDef.Outputs = Def->Outputs;
	}
	// The live angles over the authored text: identical unless the maker was turned after load.
	ChildDef.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%g %g %g"), Angles.X, Angles.Y, Angles.Z));
	const FElysiumEntityHandle ChildHandle = World->CreateRuntimeEntityNoSpawn(MoveTemp(ChildDef));
	FElysiumEntity* ChildEntity = World->Resolve(ChildHandle);
	if (ChildEntity == nullptr || ChildEntity->IsRecordOnly())
	{
		// `Warning("NULL Ent in NPCMaker!\n")` (`0x10624f8c`).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: NULL Ent in NPCMaker! ('%s')"), *DebugString(), *NpcType);
		if (ChildEntity != nullptr)
		{
			ChildEntity->Kill();
		}
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	FElysiumNpc* Child = ChildEntity->AsNpc();
	if (Child == nullptr)
	{
		// `if (!child->m_pTroika (+0x98)) Warning("Non Troika Ent in NPCMaker!\n")` (`0x10624f68`).
		// Retail returns and leaves the entity standing unspawned; the port removes the orphan
		// (named divergence: no leaked half-built entity).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: Non Troika Ent in NPCMaker! ('%s')"), *DebugString(), *NpcType);
		ChildEntity->Kill();
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	// 4. `CALL [child+0x1bc]` — slot 111 (`MemberSync`), dispatched. The walk read the offset as
	//    `Precache` (slot 104 is `+0x1a0`); corrected here.
	Child->MemberSync();
	// 5. `child+0x1584 = this+0x1584` — `m_RelationshipString`. SEAM: the port parses the authored
	//    line into `Relationships` through the `SetRelationship` input and carries no string member
	//    (`gen_kernel_bindings.py` `CHAIN_UNBOUND`), so there is nothing to copy; the child's own
	//    replayed keys carry whatever the maker authored.
	// 6. `m_OnSpawnNPC.FireOutput(this, this)` — BEFORE the child spawns.
	static const FName OnSpawnNpc(TEXT("OnSpawnNPC"));
	FireOutput(OnSpawnNpc, Handle);
	// 7. `child->m_spawnflags = 4`, `0x204` when `m_bFade`.
	Child->SpawnFlags = bFade ? GMakerChildFadeSpawnFlags : GMakerChildSpawnFlags;
	// 8. `SetDisableAI(child, GetDisableAI(this))` — `0x1029f2e0` -> `0x1029f300`.
	Child->SetDisableAi(IsAiDisabled());
	// 9. The maker's OWN Troika words onto the child (`1034b9d0`..`1034ba69`): the perception triple,
	//    then `InitPerceptionDistances` (`0x1028fb70`) and `0x1028fc90` on the MAKER (`MOV ECX,ESI`),
	//    not the child — retail's own oddity — then the rest.
	Child->AuthoredPerception = AuthoredPerception;       // +0x63b0
	Child->AuthoredVision = AuthoredVision;               // +0x63b4
	Child->AuthoredHearing = AuthoredHearing;             // +0x63bc
	RecomputePerceptionDistances();
	Child->bUseInteresting = bUseInteresting;             // +0x63d9
	Child->PercentOccludedWait = PercentOccludedWait;     // +0x6420
	Child->PercentOccludedCover = PercentOccludedCover;   // +0x6424
	Child->PercentOccludedWalk = PercentOccludedWalk;     // +0x6428
	Child->PercentOccludedFlank = PercentOccludedFlank;   // +0x642c
	Child->PercentOccludedChase = PercentOccludedChase;   // +0x6430
	Child->bAllowAlertLookaround = bAllowAlertLookaround; // +0x6434
	Child->bStayEntrenched = bStayEntrenched;             // +0x6435
	Child->ScheduleHost.bAllowKickHintUse = ScheduleHost.bAllowKickHintUse;   // +0x6436
	// 10. Slot 619 `ChildPreSpawn(child)`, VIRTUAL.
	ChildPreSpawn(Child);
	// 11. `DispatchSpawn(child)`.
	World->CallEntitySpawn(*Child);
	if (Child->IsDead())
	{
		// **Named divergence** (story 5 fold A4). Retail has no such test: `1034ba7a DispatchSpawn` is
		// followed unconditionally by the owner, the name, slot 620, the counters, `+0x65f4` and
		// `EAX = child`, because a `UTIL_Remove` during the child's own spawn is DEFERRED — the child
		// still stands, is owned and counted, and its end-of-frame `UpdateOnRemove` then dispatches
		// this maker's slot 139, whose `IsAlive` arm refunds `m_iMaxNumNPCs`. The port's `Kill` is
		// immediate and latches the owner notice (`bOwnerTerminationNotified`) before any owner is
		// set, so that refund can never arrive; owning and counting the corpse would leak a live-child
		// slot for good. So the port returns null instead: no owner, no `m_cLiveChildren` /
		// `m_iMaxNumNPCs` change, no depletion `ThinkSet(NULL)`, and `MakerThink` re-arms at
		// `RandomFloat(1, 2)` rather than `+freq`. Unreachable in shipped content (the only removal
		// arm in an NPC's own spawn is `Precache`'s slot-452 `LoadedSchedules` refusal, whose gate
		// the port does not carry).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' removed itself during Spawn"),
			*DebugString(), *NpcType);
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	// 12. `child->SetOwnerEntity(this)` (child slot 202, `+0x328`).
	Child->SetOwnerEntity(Handle);
	// 13. `if (m_ChildTargetName) child->SetName(m_ChildTargetName)`. An absent or empty key is a
	//     NULL `string_t` (`0x1042bff0` stores 0 for ""), so the child KEEPS the targetname the
	//     replayed block gave it — the maker's own.
	if (!ChildTargetName.IsEmpty())
	{
		World->RenameEntity(*Child, ChildTargetName);
	}
	// 14. Slot 620 `ChildPostSpawn(child)`, VIRTUAL.
	ChildPostSpawn(Child);
	// 15. The counters: `++m_cLiveChildren`; finite -> `--m_iMaxNumNPCs`, and on depletion
	//     `ThinkSet(NULL)` (the listing then zeroes `+0x1f0`, `m_pfnUse` by the datamap layout; the
	//     port carries no use-function pointer on a maker).
	++LiveChildren;
	if (!bInfinite)
	{
		--RemainingTotal;
		if (IsDepleted())
		{
			InstalledThink = EMakerThink::None;
		}
	}
	// 16. `child->m_bCameFromSpawner (+0x65f4) = 1`, the last write.
	Child->bCameFromSpawner = true;
	LastAttempt = EAttempt::Spawned;
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s (live %d/%d, remaining %d%s)"),
		*DebugString(), *World->DescribeHandle(ChildHandle), LiveChildren, MaxLiveChildren,
		RemainingTotal, bInfinite ? TEXT(" infinite") : TEXT(""));
	return Child;
}

// --- The installed think ----------------------------------------------------------------------------

void FElysiumNpcMaker::Think()
{
	// `m_pfnThink(this)`. Source clears `m_flNextThink` before the call (so does `RunThinks`), so a
	// body that does not re-arm stops thinking. The inert body and NULL do nothing: a slot-614 reset
	// (`SetAIEnabled`, `WakeNpcsNear`, an unhide) on a disabled or depleted maker spawns nothing.
	switch (InstalledThink)
	{
	case EMakerThink::Base:
		MakerThink();
		break;
	case EMakerThink::Inert:
	case EMakerThink::None:
		break;
	default:
		// `0x1034c8b0` / `0x1034d2d0` are the variants' own bodies (their `Think` overrides); on any
		// other class the FUNCTIONTABLE has no such row, so nothing is installed that could run.
		break;
	}
}

// `0x1034bbf0`.
void FElysiumNpcMaker::MakerThink()
{
	// `CALL [EAX+0x9a4](0)` — slot 617 `MakeNPC(false)`, VIRTUAL.
	FElysiumNpc* Child = MakeNPC(/*bBypass=*/false);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Child != nullptr || (MaxLiveChildren > 0 && MaxLiveChildren <= LiveChildren))
	{
		// A child, or a full live ceiling: `m_flNextThink = m_flSpawnFrequency + curtime`.
		ArmThinkAt(Now + static_cast<double>(SpawnFrequency));
		return;
	}
	// Anything else: `RandomFloat(1.0, 2.0) + curtime`.
	ArmThinkAt(Now + static_cast<double>(ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(
		GMakerRetryMinSeconds, GMakerRetryMaxSeconds)));
}

// --- Inputs -------------------------------------------------------------------------------------------

void FElysiumNpcMaker::InputSpawn(const FElysiumInputArgs& Args)
{
	// `0x1034b500`: `MOV [ESP+4],0` then `JMP [EAX+0x9a4]` — slot 617 with bypass false.
	(void)Args;
	MakeNPC(/*bBypass=*/false);
}

void FElysiumNpcMaker::InputEnable(const FElysiumInputArgs& Args)
{
	(void)Args;
	Enable();
}

void FElysiumNpcMaker::InputDisable(const FElysiumInputArgs& Args)
{
	(void)Args;
	Disable();
}

void FElysiumNpcMaker::InputToggle(const FElysiumInputArgs& Args)
{
	// `0x1034b460`: `m_bDisabled` ? Enable : Disable.
	(void)Args;
	if (bDisabled)
	{
		Enable();
	}
	else
	{
		Disable();
	}
}

void FElysiumNpcMaker::Enable()
{
	// `0x1034b490`: `CALL 0x10001afa` (`IsDepleted`) — refuse when depleted.
	if (IsDepleted())
	{
		return;
	}
	bDisabled = false;
	// `PUSH 0x1000696a; CALL ThinkSet` — the BASE `MakerThink`, on every maker class: an enabled
	// fleshpile or zombie maker runs `0x1034bbf0`, whose slot-617 call is still its own `MakeNPC`.
	InstalledThink = EMakerThink::Base;
	// `m_flNextThink = curtime`.
	ArmThinkAt(World != nullptr ? World->NowSeconds() : 0.0);
}

void FElysiumNpcMaker::Disable()
{
	// `0x1034b4d0`: `m_bDisabled = 1; ThinkSet(NULL)`. `m_flNextThink` is left as it was.
	bDisabled = true;
	InstalledThink = EMakerThink::None;
}

void FElysiumNpcMaker::AddInputs(FElysiumClassDesc& D, const TCHAR* RetailClass)
{
	if (FCString::Strcmp(RetailClass, RetailClassName) != 0)
	{
		return;
	}
	D.Input(TEXT("Spawn"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputSpawn(Args); });
	D.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputEnable(Args); });
	D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputDisable(Args); });
	D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{ static_cast<FElysiumNpcMaker&>(E).InputToggle(Args); });
}

// --- Helpers, persistence, debug ---------------------------------------------------------------

FElysiumEntity* FElysiumNpcMaker::FindNearestByClassname(FElysiumEntityWorld& InWorld,
	const TCHAR* Classname, const FVector& PointCm, float RadiusUnits)
{
	// `0x100f7d50`: radius squared (0 -> 3.2212255e9), `dist² < best` keeps the first-listed on a
	// tie. Its `+0x2e0 != 0` edict test has no port word (A3's seam); a live entity stands for it.
	const double Radius = static_cast<double>(RadiusUnits) * ElysiumMove::U;
	double Best = RadiusUnits == 0.0f ? 3.2212255e9 * ElysiumMove::U * ElysiumMove::U : Radius * Radius;
	FElysiumEntity* Found = nullptr;
	for (const TUniquePtr<FElysiumEntity>& Candidate : InWorld.Entities())
	{
		if (!Candidate.IsValid() || Candidate->IsDead() || Candidate->Def == nullptr
			|| !Candidate->Def->Classname.Equals(Classname, ESearchCase::IgnoreCase))
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Candidate->Origin, PointCm);
		if (DistSq < Best)
		{
			Best = DistSq;
			Found = Candidate.Get();
		}
	}
	return Found;
}

void FElysiumNpcMaker::Serialize(FElysiumSaveArchive& Ar)
{
	// The Troika record (retail's Save/Restore chain), then the installed think: retail saves
	// `m_pfnThink` through the datamap's FUNCTIONTABLE rows (`CNPCMakerMakerThink`, ...), and the
	// port's enum is that pointer. The maker's own words ride the registry's SAVE walk
	// (`AddNpcMakerSaveFields`: `m_cLiveChildren`, `m_flGround`, `m_sRefMapDataBuffer`).
	FElysiumNpc::Serialize(Ar);
	uint8 Think = static_cast<uint8>(InstalledThink);
	Ar << Think;
	if (Ar.IsLoading())
	{
		InstalledThink = Think <= static_cast<uint8>(EMakerThink::Zombie)
			? static_cast<EMakerThink>(Think) : EMakerThink::None;
	}
}

void FElysiumNpcMaker::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
	Out.Emplace(TEXT("Installed think"), MakerThinkName(InstalledThink));
	Out.Emplace(TEXT("NPCType"), NpcType.IsEmpty() ? TEXT("(none)") : NpcType);
	Out.Emplace(TEXT("NPCTargetname"), ChildTargetName.IsEmpty() ? TEXT("(none)") : ChildTargetName);
	Out.Emplace(TEXT("Live children"), FString::Printf(TEXT("%d / %d"), LiveChildren, MaxLiveChildren));
	Out.Emplace(TEXT("Remaining total"), bInfinite ? TEXT("infinite") : FString::FromInt(RemainingTotal));
	Out.Emplace(TEXT("Spawn frequency"), FString::Printf(TEXT("%.3f s"), SpawnFrequency));
	Out.Emplace(TEXT("Cached ground Z"), FString::SanitizeFloat(CachedGroundZ));
	Out.Emplace(TEXT("Last attempt"), AttemptName(LastAttempt));
}
