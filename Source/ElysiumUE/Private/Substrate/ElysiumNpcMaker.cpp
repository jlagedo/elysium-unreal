#include "Substrate/ElysiumNpcMaker.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumClassRegistry.h"   // story 29d: the zombie fists' classname lookup
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"

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

FString FElysiumNpcMaker::ExtractRefMapDataBlock(const FString& MapData)
{
	// 0x1034b3c0, character for character:
	//
	//     while (c != '\0' && c != '}') { *out++ = c; c = *++in; }
	//     *out = '}';
	//
	// so the terminator is written unconditionally and is NOT followed by a NUL — the buffer keeps
	// whatever stood past it. What this reproduces is the extracted TEXT, which is the only part any
	// consumer reads.
	int32 Brace = INDEX_NONE;
	const FString Body = MapData.FindChar(TEXT('}'), Brace) ? MapData.Left(Brace) : MapData;
	return Body + TEXT("}");
}

void FElysiumNpcMaker::ParseMapData(const FString& MapData)
{
	// Slot 107. The extraction, the latch, then the base.
	RefMapDataBuffer = ExtractRefMapDataBlock(MapData);
	// `m_sRefMapDataBuffer = -(uint)(buf[0] != '\0') & (uint)buf` — and `buf[0]` is the `}` that was
	// just written on every path, so the latch is always taken. See the header.
	//
	// `CBaseEntity::ParseMapData` is this runtime's `FElysiumEntity::Construct` keyvalue walk, which
	// the world already ran before `Spawn`; there is nothing to forward to from here.
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

void FElysiumNpcMaker::Spawn()
{
	// Story 29d, family **SpeciesLifecycle10** — slot 103 for all three maker classnames:
	// `CNPCMaker::Spawn` `0x1034afe0`, `CNPCMaker_Fleshpile::Spawn` `0x1034c020` and
	// `CNPCMaker_Zombie::Spawn` `0x1034cc60`, in the listing's order. The header carries the
	// correction this reading made to the checklist's walk (`+0x66b0`/`+0x66b8` swapped, and
	// `m_Collision` at `+0x270` rather than `+0x17c`).
	const bool bZombie = IsZombieMaker();
	const bool bFleshpile = IsFleshpileMaker();

	// 1. `1034b04c LEA ECX,[ESI + 0x270] / CALL 0x1000428c` — `SetSolid(SOLID_NONE)` on
	//    `m_Collision`, under a `"CBaseEntity::SetSolid"` scope frame naming this maker's
	//    targetname. SEAM; see the header.
	RetailSolidType = 0;

	// 2. `1034b065 MOV dword ptr [ESI + 0x66b0],0x0` — `m_cLiveChildren`, and it is written BEFORE
	//    the slot-104 dispatch, not after.
	LiveChildren = 0;

	// 3. `1034b06f CALL dword ptr [EAX + 0x1a0]` — slot **104** `Precache`, dispatched virtually, so
	//    each maker class takes its own arm. This runtime's `Precache` RECORDS rather than acquires
	//    (see the header's note on why), so calling it here adds no event retail's order lacks.
	Precache();

	// 4. `1034b075` — `if (m_bInfChild (+0x66c3)) m_bFade (+0x66c2) = 1`. Infinite implies fade.
	if (bInfinite)
	{
		bFade = true;
	}

	// 5. `1034b086 MOV AL,byte ptr [ESI + 0x66c0]` — the enabled/disabled split. The think body
	//    installed here is the ONLY difference between the base and fleshpile arms.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!bDisabled)
	{
		InstalledThink = bZombie ? EMakerThink::Zombie
			: (bFleshpile ? EMakerThink::Fleshpile : EMakerThink::Base);
		// `1034b0a6 FLD float ptr [ESI + 0x6664] / FADD float ptr [ECX + 0xc]` — spawn frequency
		// plus curtime, and for the zombie a `RandomFloat(1.0, 2.0)` on top
		// (`1034cd2a CALL dword ptr [0x109f385c]`, added BEFORE the frequency and the clock).
		double Delay = static_cast<double>(SpawnFrequency);
		if (bZombie)
		{
			Delay += static_cast<double>(
				ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(
					ZombieSpawnJitterMin, ZombieSpawnJitterMax));
		}
		NextThink = static_cast<float>(Now + Delay);
	}
	else
	{
		// `1034b0c8 PUSH 0x1000572c` — the inert think, which resolves to `0x101c0b60`, a bare `RET`.
		// The ZOMBIE arm instead pushes a NULL think (`1034cd4a PUSH 0x0`), and neither path stamps
		// `m_flNextThink` at all: a disabled maker keeps whatever next-think it already had.
		//
		// This runtime has no think-function pointer, so "installed a body that does nothing" and
		// "installed no body" are both expressed by never being due. `ELYSIUM_NEVER_THINK` is that,
		// and it is the port of leaving `m_flNextThink` at its spawn-time value with a think body
		// that cannot act — the distinction retail keeps is recorded in `InstalledThink`.
		InstalledThink = bZombie ? EMakerThink::None : EMakerThink::Inert;
		NextThink = ELYSIUM_NEVER_THINK;
	}

	// 6. `1034b0b7 CALL 0x1001514a` — `CBaseEntity::Relink`. On the base and fleshpile arms it is
	//    inside each branch; on the zombie arm the two branches join first (`1034cd48 JMP
	//    0x1034cd51`) so it runs once either way. The observable difference is nil and the count is
	//    what records it.
	++RelinkCalls;

	// 7. `1034b0bc MOV dword ptr [ESI + 0x66b8],0x0` — `m_flGround`, the LAST write of the base and
	//    fleshpile arms.
	CachedGroundZ = 0.0f;

	// 8. The zombie's second difference: the five police-level thresholds, written after `m_flGround`
	//    (`1034cd5d` then `1034cd67`..`1034cd7f`) and in offset order.
	if (bZombie)
	{
		PlInvestigate = ZombieMakerPoliceLevel;           // +0x6348
		PlCriminalFlee = ZombieMakerPoliceLevel;          // +0x634c
		PlCriminalAttack = ZombieMakerPoliceLevel;        // +0x6350
		PlSupernaturalFlee = ZombieMakerPoliceLevel;      // +0x6354
		PlSupernaturalAttack = ZombieMakerPoliceLevel;    // +0x6358
	}
}

FElysiumNpcMaker::EAttempt FElysiumNpcMaker::CanMakeNpc(bool bBypass) const
{
	if (bBypass)
	{
		return EAttempt::Spawned;
	}
	if (MaxLiveChildren > 0 && LiveChildren >= MaxLiveChildren)
	{
		return EAttempt::LiveLimit;
	}
	if (World && World->IsNpcMakerSceneBlocked())
	{
		return EAttempt::Scene;
	}
	const FElysiumPlayer* Player = World ? World->FindPlayer() : nullptr;
	const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Player)
	{
		if (bNpcClip && Embodiment && Embodiment->IsNpcMakerVisibleFromPlayer(Origin))
		{
			return EAttempt::Visible;
		}
		if (bViewCone && Embodiment && Embodiment->IsNpcMakerInPlayerViewCone(Origin))
		{
			return EAttempt::ViewCone;
		}
		if (MinPcDistance > 0)
		{
			const int32 DistanceUnits = FMath::TruncToInt(
				FVector::Dist(Player->Origin, Origin) / ElysiumMove::U);
			if (DistanceUnits < MinPcDistance)
			{
				return EAttempt::Distance;
			}
		}
	}
	if (Embodiment && Embodiment->IsNpcMakerSpawnAreaOccupied(
		FVector(Origin.X, Origin.Y, CachedGroundZ), 34.0f * ElysiumMove::U))
	{
		return EAttempt::Occupied;
	}
	return EAttempt::Spawned;
}

FElysiumNpcMaker::EAttempt FElysiumNpcMaker::TrySpawn(bool bBypass)
{
	if (!World || !Def)
	{
		return LastAttempt = EAttempt::InvalidChild;
	}
	if (CachedGroundZ == 0.0f)
	{
		CachedGroundZ = World->Embodiment()
			? World->Embodiment()->ResolveNpcMakerGroundZ(Origin, 2048.0f * ElysiumMove::U)
			: Origin.Z;
	}
	const EAttempt Admission = CanMakeNpc(bBypass);
	if (Admission != EAttempt::Spawned)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Spawn rejected: %s (live %d/%d)"),
			*DebugString(), AttemptName(Admission), LiveChildren, MaxLiveChildren);
		return LastAttempt = Admission;
	}
	if (NpcType.IsEmpty())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: no NPCType"), *DebugString());
		return LastAttempt = EAttempt::InvalidChild;
	}

	static const TSet<FName> MakerOnlyKeys = {
		FName(TEXT("classname")), FName(TEXT("targetname")), FName(TEXT("origin")),
		FName(TEXT("angles")), FName(TEXT("spawnflags")), FName(TEXT("NPCType")),
		FName(TEXT("MaxNPCCount")), FName(TEXT("SpawnFrequency")),
		FName(TEXT("MaxLiveChildren")), FName(TEXT("NPCTargetname")),
		FName(TEXT("Flag_StartDisabled")), FName(TEXT("Flag_NPCClip")),
		FName(TEXT("Flag_Fade")), FName(TEXT("Flag_InfChild")),
		FName(TEXT("Flag_NoDrop")), FName(TEXT("Flag_ViewCone")),
		FName(TEXT("MinPCDistance"))
	};
	FElysiumEntityDef Child;
	Child.Classname = NpcType;
	Child.Origin = Origin;
	for (const TPair<FString, FString>& KV : Def->Keys)
	{
		if (!MakerOnlyKeys.Contains(FName(*KV.Key)))
		{
			Child.Keys.Add(KV.Key, KV.Value);
		}
	}
	Child.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%g %g %g"), Angles.X, Angles.Y, Angles.Z));
	Child.Outputs = Def->Outputs; // each Construct seeds fresh per-row times counters

	const FElysiumEntityHandle ChildHandle = World->CreateRuntimeEntityNoSpawn(MoveTemp(Child));
	FElysiumEntity* ChildEntity = World->Resolve(ChildHandle);
	if (!ChildEntity || ChildEntity->IsRecordOnly() || !ChildEntity->AsCombatCharacter())
	{
		if (ChildEntity)
		{
			ChildEntity->Kill();
		}
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' is not a live NPC class"),
			*DebugString(), *NpcType);
		return LastAttempt = EAttempt::InvalidChild;
	}

	// Story 29d, family Lifecycle10 — `MakeNPC` `0x1034b7b0`, the steps between the child's creation
	// and its spawn, in the listing's order.
	//
	// `1034b928`..`1034b982`: `m_sRefMapDataBuffer` (`+0x76cc`) is copied byte by byte into the
	// maker's inline buffer at `+0x66cc`, handed to the child's `ParseMapData` (vtable `+0x1ac`) as a
	// two-word `CEntityMapData`, then the child's `Precache` (`+0x1bc`) runs and `SetClassname`
	// (`+0x1e8`) is given `m_iszNPCClassname` (`+0x665c`).
	//
	// SEAM, all three: this runtime builds a child from the registered classname and the keyvalue map
	// above, which the world applies at `Construct` — so the block is RECORDED and applied to
	// nothing; the classname is already the child's; and `Precache()` is not called here for the
	// reason `FElysiumNpc::Precache` states (residency is the map epoch's, and a per-entity precache
	// would add an event retail's order does not have here).
	LastChildMapDataReplay = RefMapDataBuffer;

	static const FName OnSpawnNpc(TEXT("OnSpawnNPC"));
	FElysiumNpc* ChildNpc = ChildEntity->AsNpc();
	if (ChildNpc != nullptr)
	{
		// `1034b988 MOV EAX,[ESI+0x1584] / 1034b998 MOV [EDI+0x1584],EAX` — the relationship string
		// is copied BEFORE the output fires, which is the one thing in the block that is not part of
		// `ApplyChildInheritance`'s run (retail copies it here and the rest after the spawnflags).
		LastChildRelationshipString = ChildWords.RelationshipString;
	}
	FireOutput(OnSpawnNpc, Handle);
	ChildEntity->SpawnFlags = bFade ? 0x204 : 4;
	// `MakeNPC` `0x1034b7b0`, right after the spawnflags: `child->SetDisableAI(this->GetDisableAI())`.
	if (ChildNpc != nullptr)
	{
		ChildNpc->SetDisableAi(bDisableAi);
		// `1034b9d0`..`1034ba69` — the nine inherited words and the two perception derivations.
		ApplyChildInheritance(*ChildNpc);
	}
	// `1034ba73 CALL [this->vtable + 0x9ac]` — slot 619 `ChildPreSpawn(child)`. `CNPCMaker`'s own
	// body (`0x1034af30`) is `return;`, so the base maker's hook does nothing; the fleshpile's and
	// the zombie's are other rows. Counted, so the order around `DispatchSpawn` stays assertable.
	++ChildPreSpawnCalls;
	World->CallEntitySpawn(*ChildEntity);
	if (ChildEntity->IsDead())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' removed itself during Spawn"),
			*DebugString(), *NpcType);
		return LastAttempt = EAttempt::InvalidChild;
	}
	ChildEntity->SetOwnerEntity(Handle);
	World->RenameEntity(*ChildEntity, ChildTargetName);
	// `1034baa4 CALL [this->vtable + 0x9b0]` — slot 620 `ChildPostSpawn(child)`, AFTER the rename and
	// before the counters. `CNPCMaker`'s own body (`0x1034af50`) is empty too.
	++ChildPostSpawnCalls;
	LastSpawnedChild = ChildHandle;
	++LiveChildren;
	if (!bInfinite)
	{
		--RemainingTotal;
		if (IsDepleted())
		{
			NextThink = ELYSIUM_NEVER_THINK;
		}
	}
	// `1034baee MOV byte ptr [EDI + 0x65f4],0x1` — `m_bCameFromSpawner` on the CHILD, the very last
	// write before the return. The checklist's walk calls it "the child's `+0x2c4` latch", which is
	// Ghidra's `this_00[0x17].field_0x2c4` struct view of the same byte; the listing's offset is
	// `+0x65f4` and the datamap names it.
	if (ChildNpc != nullptr)
	{
		ChildNpc->bCameFromSpawner = true;
	}
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s (live %d/%d, remaining %d%s)"),
		*DebugString(), *World->DescribeHandle(ChildHandle), LiveChildren, MaxLiveChildren,
		RemainingTotal, bInfinite ? TEXT(" infinite") : TEXT(""));
	return LastAttempt = EAttempt::Spawned;
}

void FElysiumNpcMaker::InputEnable(const FElysiumInputArgs&)
{
	if (IsDepleted())
	{
		return;
	}
	bDisabled = false;
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0);
}

void FElysiumNpcMaker::InputDisable(const FElysiumInputArgs&)
{
	bDisabled = true;
	NextThink = ELYSIUM_NEVER_THINK;
}

void FElysiumNpcMaker::InputToggle(const FElysiumInputArgs& Args)
{
	if (bDisabled) { InputEnable(Args); }
	else { InputDisable(Args); }
}

void FElysiumNpcMaker::InputDisableThink(const FElysiumInputArgs& Args)
{
	// `InputDisableThink` `0x1029f2a0` on the maker itself: a bool variant is stored, anything
	// else stores false. The value reaches the children at `MakeNPC`.
	bDisableAi = Args.Param.Type == EElysiumVariantType::Bool && Args.Param.AsBool;
}

void FElysiumNpcMaker::Think()
{
	const EAttempt Result = TrySpawn(/*bBypass=*/false);
	if (Result == EAttempt::Spawned && IsDepleted())
	{
		return;
	}
	const double Now = World ? World->NowSeconds() : 0.0;
	if (Result == EAttempt::Spawned || Result == EAttempt::LiveLimit)
	{
		NextThink = static_cast<float>(Now + SpawnFrequency);
	}
	else
	{
		NextThink = static_cast<float>(Now
			+ ElysiumRng::Stream(EElysiumRngStream::NpcMaker).FRandRange(1.0f, 2.0f));
	}
}

void FElysiumNpcMaker::OnOwnedEntityTerminated(FElysiumEntity& Child,
	EElysiumOwnedEntityTermination Reason)
{
	if (Reason == EElysiumOwnedEntityTermination::RemovedAlive)
	{
		if (!bInfinite)
		{
			++RemainingTotal;
		}
	}
	else
	{
		static const FName OnNpcDied(TEXT("OnNPCDied"));
		FireOutput(OnNpcDied, Child.Handle);
	}
	if (IsDepleted())
	{
		static const FName OnLastNpcDied(TEXT("OnLastNPCDied"));
		FireOutput(OnLastNpcDied, Child.Handle);
	}
	LiveChildren = FMath::Max(0, LiveChildren - 1);
}

// -------------------------------------------------------------------------------------------------
// Story 29c-1, family **Species** — `CNPCMaker_Fleshpile`'s slot 617 and slot 139.
//
// Both are `CNPCMaker_Fleshpile` bodies, so they land on the maker rather than on `FElysiumNpc`;
// story 29c's checklist targets them at `FElysiumNpcMaker::FUN_1034c2d0` and `FElysiumNpc::vfunc139`
// and the second of those is the NPC leaf by mistake — the report says so. The walked prose is
// `docs/vtmb/npc-ai/lifecycle.md`.
//
// Every `.rdata` constant below was READ out of the pinned retail `vampire.dll` and the value is
// quoted beside its cell address.
// -------------------------------------------------------------------------------------------------

namespace
{
	// `_DAT_10452dc4` = **2.0f** — Andrei's live-runner cap, the number `0x1034c2d0` refuses at and
	// `CNPC_VAndreiBlood`'s own `0x1035e920` (family Species' `FUN_1035e920`) tests below.
	constexpr int32 FleshpileMaxActiveRunners = 2;
	// `_DAT_104454c0` = **1.0f** — the step both counters move by.
	constexpr int32 FleshpileRunnerCountStep = 1;
	// `_DAT_1046bacc` = **2048.0f** — the downward trace depth `MakeNPC` caches `m_flGround` with.
	constexpr float FleshpileGroundTraceDepthUnits = 2048.0f;
	// `_DAT_1049ffac` = **34.0f** — the half-extent of the spawn box it then tests for occupancy.
	constexpr float FleshpileSpawnBoxHalfExtentUnits = 34.0f;
	// The classname the singleton search uses, verbatim from `.rdata`.
	constexpr const TCHAR* FleshpileOwnerClassname = TEXT("npc_VAndreiBlood");
	// The classname this runtime registers for `CNPCMaker_Fleshpile`.
	constexpr const TCHAR* FleshpileMakerClassname = TEXT("npc_maker_fleshpile");
}

bool FElysiumNpcMaker::IsFleshpileMaker() const
{
	return Def != nullptr && Def->Classname.Equals(FleshpileMakerClassname, ESearchCase::IgnoreCase);
}

void FElysiumNpcMaker::OnRestore(bool /*bFromLoad*/)
{
	// `0x1034c260`: FindEntityByClassname(NULL, "npc_VAndreiBlood") then dynamic_cast into
	// `DAT_10938040`, then `CAI_BaseNPCTroika::OnRestore`.
	if (World != nullptr)
	{
		for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
		{
			if (!Candidate.IsValid() || Candidate->Def == nullptr)
			{
				continue;
			}
			if (Candidate->Def->Classname.Equals(TEXT("npc_VAndreiBlood"), ESearchCase::IgnoreCase))
			{
				FElysiumNpc::FleshpileAndreiSingleton() = Candidate->Handle;
				break;
			}
		}
	}
	++MakerOnRestoreTroikaChains;
}

FElysiumNpc* FElysiumNpcMaker::FleshpileOwner() const
{
	// Retail's `DAT_10938040`: `FindEntityByClassname(NULL, "npc_VAndreiBlood")` plus
	// `dynamic_cast<CNPC_VAndreiBlood*>`, cached in a file static on first success and never
	// cleared. The cache is NOT reproduced — a stale pointer to a removed entity is a fault, not a
	// behaviour — and the search is run each time instead, which answers the same entity while one
	// stands and null once it does not. **Named modernization**, stated here and in the report.
	if (World == nullptr)
	{
		return nullptr;
	}
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate.IsValid() || Candidate->Def == nullptr)
		{
			continue;
		}
		if (Candidate->Def->Classname.Equals(FleshpileOwnerClassname, ESearchCase::IgnoreCase))
		{
			return Candidate->AsNpc();
		}
	}
	return nullptr;
}

FElysiumNpcMaker::EAttempt FElysiumNpcMaker::FUN_1034c2d0(bool bBypass)
{
	// `0x1034c2d0`, `CNPCMaker_Fleshpile`'s slot 617 `MakeNPC`. Arm by arm:
	//
	//     if (!DAT_10938040) DAT_10938040 = cast(FindEntityByClassname(NULL, "npc_VAndreiBlood"));
	//     if (!param_1) {                                    // param_1 is the BYPASS flag
	//         if (!DAT_10938040) return NULL;
	//         if (2.0 <= DAT_10938040->m_iActiveRunnerCount) return NULL;     // _DAT_10452dc4
	//         if (m_flGround (+0x66b8) == 0.0) {                              // _DAT_104454c4
	//             from = GetAbsOrigin();  to = from;  to.z -= 2048.0;         // _DAT_1046bacc
	//             ray = Ray(GetAbsOrigin(), &to);  filter = TraceFilter(this, 0);
	//             TraceRay(&ray, MASK 0x2400b, &filter, &tr);
	//             if (debugoverlay) DrawTrace(&tr);
	//             m_flGround = tr.endpos.z;
	//         }
	//         mins = GetAbsOrigin() - (34, 34, 0);  maxs = GetAbsOrigin() + (34, 34, 0);
	//         maxs.z = GetAbsOrigin().z;
	//         if (!m_bNoDrop (+0x66c4)) mins.z = m_flGround;
	//         if (EntityInBox(&tr, 2, &mins, &maxs, 0x2080, 0)) return NULL;  // thunk 0x101cca80
	//     }
	//     child = CreateEntityByName(m_iszNPCClassname (+0x665c));
	//     if (!child) { Warning("NULL Ent in NPCMaker!\n"); return NULL; }
	//     if (!child->m_pTroika (+0x98)) { Warning("Non Troika Ent in NPCMaker!\n"); return NULL; }
	//     ... ~twenty field copies onto the child, then Spawn, Activate, name, notify ...
	//     DAT_10938040->m_iActiveRunnerCount += 1.0;                          // _DAT_104454c0
	//     return child;
	//
	// **The bypass flag skips the WHOLE admission**, blood budget included — a fleshpile summoned by
	// a script is made no matter how many runners are already up. That is the same shape the base
	// `CNPCMaker::MakeNPC` has and is why `CanMakeNpc`'s first line here is the same short circuit.
	//
	// **The three admission terms the base does NOT have** are what makes this a species body: the
	// Andrei singleton must exist, its live-runner count must be under two, and the box test uses
	// `m_bNoDrop` to decide whether the box reaches down to the cached ground or starts at the
	// maker's own Z. The base's live-children, scene, visibility, view-cone and distance terms are
	// **absent** — a fleshpile maker ignores all five.
	//
	// **The ground cache is compared against 0.0 and not against a sentinel**, so a maker standing
	// at exactly Z=0 over ground at exactly Z=0 re-traces on every attempt. Retail's, and kept.
	//
	// `TrySpawn` already carries the whole child-construction half — the classname lookup, the
	// twenty field copies, `Spawn`, the rename and the ownership notice — as the base body's port.
	// It is CALLED rather than restated, so the two makers cannot drift; what this body adds is the
	// species admission in front of it and the counter increment behind it.
	if (!bBypass)
	{
		FElysiumNpc* Owner = FleshpileOwner();
		if (Owner == nullptr)
		{
			return LastAttempt = EAttempt::InvalidChild;
		}
		if (Owner->ActiveRunnerCount >= FleshpileMaxActiveRunners)
		{
			return LastAttempt = EAttempt::LiveLimit;
		}
		if (CachedGroundZ == 0.0f)
		{
			CachedGroundZ = World != nullptr && World->Embodiment() != nullptr
				? World->Embodiment()->ResolveNpcMakerGroundZ(
					Origin, FleshpileGroundTraceDepthUnits * ElysiumMove::U)
				: Origin.Z;
		}
		// `mins.z = m_bNoDrop ? GetAbsOrigin().z : m_flGround` — the one place `Flag_NoDrop` is
		// consumed anywhere in the binary. The base maker declares the key and never reads it
		// (`ElysiumNpcMaker.h` says so); the fleshpile is its reader.
		const float BoxFloorZ = bNoDrop ? static_cast<float>(Origin.Z) : CachedGroundZ;
		if (World != nullptr && World->Embodiment() != nullptr
			&& World->Embodiment()->IsNpcMakerSpawnAreaOccupied(
				FVector(Origin.X, Origin.Y, BoxFloorZ),
				FleshpileSpawnBoxHalfExtentUnits * ElysiumMove::U))
		{
			return LastAttempt = EAttempt::Occupied;
		}
	}
	const EAttempt Result = TrySpawn(/*bBypass=*/true);
	if (Result == EAttempt::Spawned)
	{
		if (FElysiumNpc* Owner = FleshpileOwner())
		{
			Owner->ActiveRunnerCount += FleshpileRunnerCountStep;
		}
	}
	return LastAttempt = Result;
}

void FElysiumNpcMaker::FUN_1034c8e0(FElysiumEntity* Child)
{
	// `0x1034c8e0`, `CNPCMaker_Fleshpile`'s slot 139 `DeathNotice`:
	//
	//     if (DAT_10938040 != 0) {
	//         runner = dynamic_cast<CNPC_VTzimisceRunner*>(param_1);          // 0x10625270
	//         if (runner && !runner->m_bDeathNoticeProcessed (+0x6671)) {
	//             DAT_10938040->m_iActiveRunnerCount (+0x66b8) -= 1.0;        // _DAT_104454c0
	//             DAT_10938040->m_iKillCount        (+0x66bc) += 1.0;
	//             runner->m_bDeathNoticeProcessed = 1;
	//         }
	//     }
	//     CNPCMaker::DeathNotice(this, param_1);                              // thunk 0x1034bc90
	//
	// **29c's walk reads this as "shifts the global bounds ... grows/shrinks a global volume".** It
	// is not a volume: `vtmb_fields` names `+0x66b8` `CNPC_VAndreiBlood::m_iActiveRunnerCount` and
	// `+0x66bc` `m_iKillCount`, and the flag the cast checks is
	// `CNPC_VTzimisceRunner::m_bDeathNoticeProcessed`. So the pair is a BUDGET: `MakeNPC` above
	// increments the live count, this decrements it and counts the kill, and the once-only flag is
	// what stops a runner that dies twice — a corpse re-notified — being counted twice.
	//
	// The base is called on EVERY path, cast or no cast, so the maker's own live-children
	// bookkeeping (`OnOwnedEntityTerminated`) still runs.
	if (FElysiumNpc* Owner = FleshpileOwner())
	{
		FElysiumNpc* Runner = Child != nullptr ? Child->AsNpc() : nullptr;
		// The RTTI cast to `CNPC_VTzimisceRunner`, read through the census: one leaf stands every
		// classname here, so "is this a runner" is the chain walk and never a name compare.
		if (Runner != nullptr && Runner->IsRetailClass(TEXT("CNPC_VTzimisceRunner"))
			&& !Runner->bRunnerDeathNoticeProcessed)
		{
			Owner->ActiveRunnerCount -= FleshpileRunnerCountStep;
			Owner->AndreiKillCount += FleshpileRunnerCountStep;
			Runner->bRunnerDeathNoticeProcessed = true;
		}
	}
	// `CNPCMaker::DeathNotice` `0x1034bc90` — this runtime reaches the same bookkeeping through the
	// ownership notice, which the world drives; nothing is forwarded by hand here.
}

void FElysiumNpcMaker::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Enabled"), bDisabled ? TEXT("no") : TEXT("yes"));
	Out.Emplace(TEXT("NPCType"), NpcType.IsEmpty() ? TEXT("(none)") : NpcType);
	Out.Emplace(TEXT("NPCTargetname"), ChildTargetName.IsEmpty() ? TEXT("(none)") : ChildTargetName);
	Out.Emplace(TEXT("Live children"), FString::Printf(TEXT("%d / %d"), LiveChildren, MaxLiveChildren));
	Out.Emplace(TEXT("Remaining total"), bInfinite ? TEXT("infinite") : FString::FromInt(RemainingTotal));
	Out.Emplace(TEXT("Spawn frequency"), FString::Printf(TEXT("%.3f s"), SpawnFrequency));
	Out.Emplace(TEXT("Cached ground Z"), FString::SanitizeFloat(CachedGroundZ));
	Out.Emplace(TEXT("Last attempt"), AttemptName(LastAttempt));
}

// =================================================================================================
// Story 29d, family **Precache10** — slot 104, the three `CNPCMaker*` bodies.
// `0x1034b160` `CNPCMaker::Precache`, `0x1034c180` `CNPCMaker_Fleshpile::Precache`,
// `0x1034cde0` `CNPCMaker_Zombie::Precache`. One method, three arms, keyed on the classname.
// The walked prose is `docs/vtmb/npc-ai/lifecycle.md`.
// =================================================================================================

int32 FElysiumNpcMaker::DeveloperCvarLevel = 0;

namespace
{
	// The classname `ElysiumNpcClasses.cpp` registers for `CNPCMaker_Zombie`.
	constexpr const TCHAR* ZombieMakerClassname = TEXT("npc_maker_zombie");
	// `s_item_w_zombie_fists_10625644`, the one extra `UTIL_PrecacheOther` the zombie arm adds.
	// The same `.rdata` cell `CNPC_VZombie::Precache` (`0x103df120`) reads.
	constexpr const TCHAR* ZombieFistsItem = TEXT("item_w_zombie_fists");
	// The four `.rdata` format strings this body reaches are spelled at their call sites rather than
	// named here, because `FString::Printf` and `UE_LOG` both require a literal format. For the
	// record they are, verbatim (the retail strings end in `\n`, which `UE_LOG` supplies):
	//   `0x105470e4`  "%s at %.0f %.0f %0.f missing modelname\n"
	//   `0x10624f34`  "%s at %.0f %.0f %0.f missing NPCClassname\n"
	//   `0x10624f00`  "%s: BAD MODEL NAME"
	//   `0x10624f18`  "%s: BAD NPC Classname"
	// The `%0.f` on the THIRD float is retail's own typo — `printf` reads it as `%0.f`, a precision
	// of zero, which is the same output `%.0f` gives, so the two spellings agree and the port uses
	// `%.0f` for all three.
}

bool FElysiumNpcMaker::IsZombieMaker() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (bZombieMakerForTests)
	{
		return true;
	}
#endif
	// Unreachable at runtime today: `ElysiumNpcClasses.cpp` registers no `npc_maker_zombie`. See
	// the header.
	return Def != nullptr && Def->Classname.Equals(ZombieMakerClassname, ESearchCase::IgnoreCase);
}

void FElysiumNpcMaker::Precache()
{
	// Which arm. `IsFleshpileMaker` and `IsZombieMaker` read the maker's own classname, exactly as
	// slots 617 and 139 do; the base `CNPCMaker` arm is everything else.
	const bool bFleshpile = IsFleshpileMaker();
	const bool bZombie = IsZombieMaker();
	const bool bBaseMaker = !bFleshpile && !bZombie;

	// All three bodies open with the SAME question — slot 9 `GetModelName` (vtable `+0x24`), read
	// three times to decide "unset or empty". This runtime carries the keyfield as
	// `FElysiumEntity::Model`.
	if (Model.IsEmpty())
	{
		// The missing-model arm, identical in all three: `Warning(fmt, GetDebugName(), origin.x,
		// origin.y, origin.z)` — the three floats read off slot 220 `GetOrigin` (vtable `+0x370`),
		// pushed x, y, z — then `UTIL_Remove(this)` (`thunk_FUN_101cd940`).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s at %.0f %.0f %.0f missing modelname"),
			*DebugString(), Origin.X, Origin.Y, Origin.Z);
		Kill();
		// And only the BASE `CNPCMaker` arm goes on to the developer overlay; the fleshpile and the
		// zombie drop it entirely, which is the whole difference between their error arms.
		if (bBaseMaker)
		{
			DrawBadNameOverlay(/*bBadClassname=*/false);
		}
		return;
	}

	// `PrecacheModel(model, 0)` through the engine (`(*DAT_1070b22c)+0x34`).
	FElysiumNpc::FPrecacheOp ModelOp;
	ModelOp.Channel = FElysiumNpc::EPrecacheChannel::Model;
	ModelOp.Name = Model;
	PrecacheLog.Add(ModelOp);

	if (bZombie)
	{
		// `CNPCMaker_Zombie`'s two writes, BEFORE the chain and in retail's order: `m_altEquipment`
		// (`+0x1a98`) then `m_spawnEquipment` (`+0x5dec`), both zeroed — so an authored equipment
		// keyfield on a zombie maker is discarded and the base chain's `UTIL_PrecacheOther` arm can
		// never fire. See the header for why neither word has a writer in this port.
		AlternateEquipment.Reset();
		AdditionalEquipment.Reset();
	}

	// `CAI_BaseNPC::thunk_FUN_1027bb50(this)` — the base NPC precache, run on the MAKER, which is a
	// `CAI_BaseNPCTroika` in retail. Its three steps on a maker:
	//   1. `UTIL_PrecacheOther(m_spawnEquipment)` when the word is non-null and not the sentinel
	//      `"0"`. Nothing in this port writes it (header), so the arm is not taken.
	//   2. slot 452 `LoadedSchedules`, which `FElysiumNpc::LoadedSchedules` answers true for every
	//      class by design — so the reject arm (`"ERROR: Rejecting spawn of %s as error in NPC's
	//      schedules."` plus `UTIL_Remove`) is unreachable here, exactly as it is on an NPC.
	//   3. `CBaseCombatCharacter::Precache`, the once-per-map global emitter block that is
	//      `CBaseCombatCharacter`'s story and not the NPC kernel's.
	// All three are no-ops on this type, and the chain is named rather than dropped.
	if (!AdditionalEquipment.IsEmpty() && AdditionalEquipment != TEXT("0"))
	{
		FElysiumNpc::FPrecacheOp EquipOp;
		EquipOp.Channel = FElysiumNpc::EPrecacheChannel::Other;
		EquipOp.Name = AdditionalEquipment;
		PrecacheLog.Add(EquipOp);
	}

	// `m_iszNPCClassname` (`+0x665c`, this runtime's `NpcType`), read as the empty string when null.
	if (bBaseMaker && NpcType.IsEmpty())
	{
		// ONLY the base `CNPCMaker` arm checks the classname for emptiness. The fleshpile and the
		// zombie `UTIL_PrecacheOther` it unconditionally — an empty classname there reaches
		// `UTIL_PrecacheOther("")`, whose own `Warning("NULL Ent in UTIL_PrecacheOther: %s")` is the
		// diagnostic, and neither removes the maker.
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s at %.0f %.0f %.0f missing NPCClassname"),
			*DebugString(), Origin.X, Origin.Y, Origin.Z);
		Kill();
		DrawBadNameOverlay(/*bBadClassname=*/true);
		return;
	}

	FElysiumNpc::FPrecacheOp ChildOp;
	ChildOp.Channel = FElysiumNpc::EPrecacheChannel::Other;
	ChildOp.Name = NpcType;
	PrecacheLog.Add(ChildOp);

	if (bZombie)
	{
		// And the zombie arm's one extra weapon, AFTER the classname.
		FElysiumNpc::FPrecacheOp FistsOp;
		FistsOp.Channel = FElysiumNpc::EPrecacheChannel::Other;
		FistsOp.Name = ZombieFistsItem;
		PrecacheLog.Add(FistsOp);
	}
}

void FElysiumNpcMaker::DrawBadNameOverlay(bool bBadClassname)
{
	// The developer gate both `CNPCMaker::Precache` overlay arms run, from the listing at
	// `1034b21f`/`1034b2bf`: `if (cvar->vtable[1]()) return;` — `IsCommand`, a command is not a
	// value — `if (cvar->m_nValue (+0x2c) <= 0) return;`. So the overlay needs `developer >= 1`,
	// and retail's shipped default is 0.
	if (DeveloperCvarLevel < 1)
	{
		return;
	}
	// `0x100067f3` formats `"%s: BAD …"` with `GetDebugName()`, `0x100092c3` binds the text to this
	// entity's angles (slot 219 `GetAbsAngles`, vtable `+0x36c`) and origin (slot 220, `+0x370`),
	// and `thunk_FUN_101cf390` draws the box at `m_Collision`'s `OBBMaxs` (`+0x8`) and `OBBMins`
	// (`+0x4`) — pushed maxs first, mins second.
	//
	// SEAM: this substrate stands no collision OBB on an entity and no maker debug-overlay service,
	// so the bounds are zero and nothing is drawn. The request is recorded whole.
	FDeveloperOverlayBox Box;
	Box.Text = bBadClassname
		? FString::Printf(TEXT("%s: BAD NPC Classname"), *DebugString())
		: FString::Printf(TEXT("%s: BAD MODEL NAME"), *DebugString());
	DeveloperOverlayBoxes.Add(Box);
}

// -------------------------------------------------------------------------------------------------
// Story 29d, family **Lifecycle10** — `MakeNPC`'s child inheritance and `CNPCMaker_Zombie`'s two
// slots. `ElysiumNpcMaker.h` carries this half's reading notes.
// -------------------------------------------------------------------------------------------------

namespace
{
	// `s_item_w_zombie_fists_10625644`, the classname `0x1034d140` looks up.
	const TCHAR* const GZombieFistsItem = TEXT("item_w_zombie_fists");
	// `s_Zombies_spawning_emitter_1062565c` and its `1034d249 PUSH 0x41700000` lifetime.
	const TCHAR* const GZombieSpawnEmitterName = TEXT("Zombies_spawning_emitter");
	constexpr float GZombieSpawnEmitterLifetimeSeconds = 15.0f;
	// `_DAT_10450a9c` — **0.9**, read out of the pinned `vampire.dll` at file offset `0x450a9c`
	// (base `0x10000000`). The scale `CNPCMaker_Zombie::CanMakeNPC` multiplies its MANHATTAN
	// distance by.
	constexpr float GZombieMakerDistanceScale = 0.9f;
}

void FElysiumNpcMaker::ApplyChildInheritance(FElysiumNpc& Child)
{
	// `0x1034b7b0`, the copy block, in the listing's order. See the header for the correction this
	// reading made to the checklist's walk.
	LastChildRelationshipString = ChildWords.RelationshipString;   // +0x1584, seam
	Child.AuthoredPerception = ChildWords.AuthoredPerception;      // +0x63b0
	Child.AuthoredVision = ChildWords.AuthoredVision;              // +0x63b4
	Child.AuthoredHearing = ChildWords.AuthoredHearing;            // +0x63bc

	// `1034b9ee MOV ECX,ESI` — `InitPerceptionDistances` (`0x1028fb70`) and `0x1028fc90` run on the
	// MAKER, not on the child whose three words were just written. That is retail's own oddity: a
	// spawned NPC inherits the authored perception triple and NOTHING derives the resolved pair from
	// it here; the child's own sense pass is what does. Reproduced, and counted.
	++MakerPerceptionDerivations;

	Child.bUseInteresting = ChildWords.bUseInteresting;            // +0x63d9
	Child.PercentOccludedWait = ChildWords.PercentOccludedWait;    // +0x6420
	Child.PercentOccludedCover = ChildWords.PercentOccludedCover;  // +0x6424
	Child.PercentOccludedWalk = ChildWords.PercentOccludedWalk;    // +0x6428
	Child.PercentOccludedFlank = ChildWords.PercentOccludedFlank;  // +0x642c
	Child.PercentOccludedChase = ChildWords.PercentOccludedChase;  // +0x6430
	Child.bAllowAlertLookaround = ChildWords.bAllowAlertLookaround;        // +0x6434
	Child.bStayEntrenched = ChildWords.bStayEntrenched;                    // +0x6435
	Child.ScheduleHost.bAllowKickHintUse = ChildWords.bAllowKickHintUse;   // +0x6436
}

FElysiumNpcMaker::EAttempt FElysiumNpcMaker::CanMakeNpcZombie(bool bBypass) const
{
	// `CNPCMaker_Zombie::CanMakeNPC` `0x1034d0a0`, slot 618, arm by arm.
	//
	//   1. `if ((char)param_1 != '\0') return true;` — a non-zero bypass answers YES before anything
	//      else, exactly as the base body's own first arm does.
	if (bBypass)
	{
		return EAttempt::Spawned;
	}
	//   2. With a resolvable player (`thunk_FUN_101cda50`), measure the MANHATTAN distance between
	//      the maker's and the player's `GetAbsOrigin` (slot 217, vtable `+0x364`), scale it by
	//      `_DAT_10450a9c` (0.9) and REFUSE when the product EXCEEDS `+0x76d8`. A zombie maker
	//      refuses when the player is too FAR — the opposite sense of the base's Euclidean
	//      minimum-distance arm at `+0x66c8`, which refuses when the player is too CLOSE.
	//
	//      SEAM: `+0x76d8` has no port keyfield, because `npc_maker_zombie` is not a registered
	//      spawn leaf here (see `IsZombieMaker`). `ZombieMaxPcDistance` stands for it and is the
	//      authored **0** a maker with no such key would carry, which makes this arm refuse for any
	//      player at a non-zero distance — retail's own answer for an unauthored maximum.
	const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr)
	{
		const FVector Delta = Player->Origin - Origin;
		const double ManhattanUnits =
			(FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) + FMath::Abs(Delta.Z)) / ElysiumMove::U;
		if (ManhattanUnits * GZombieMakerDistanceScale > static_cast<double>(ZombieMaxPcDistance))
		{
			return EAttempt::Distance;
		}
	}
	//   3. `thunk_FUN_1034b580(this, '\0')` — the base with a HARD-CODED false bypass, so the live
	//      limit, the global no-spawn byte, npcclip, the view cone, the Euclidean minimum distance
	//      and the hull-occupancy trace all still run.
	return CanMakeNpc(/*bBypass=*/false);
}

bool FElysiumNpcMaker::ZombieMakerOwnsFists() const
{
	// SEAM for `CBaseCombatCharacter::Weapon_OwnsThisType(this, "item_w_zombie_fists", 0)` — asked of
	// the MAKER (`1034d16a MOV ECX,EDI`, and `EDI` is `this`), not of the zombie it just spawned. A
	// maker on this leaf is an `FElysiumEntity` and owns no weapons, so the honest answer is false,
	// which is the arm that goes on to look the item up.
	return false;
}

bool FElysiumNpcMaker::ZombieFistsItemExists() const
{
	// SEAM for `thunk_FUN_10136580("item_w_zombie_fists")`. See the header: the class registry IS
	// this runtime's entity factory, and the item catalogue only populates it once
	// `ElysiumItems::Install` has run.
#if WITH_DEV_AUTOMATION_TESTS
	if (ZombieFistsItemForTests.IsSet())
	{
		return ZombieFistsItemForTests.GetValue();
	}
#endif
	return FElysiumClassRegistry::Get().Find(FName(GZombieFistsItem)) != nullptr;
}

FElysiumNpc* FElysiumNpcMaker::EquipZombieFists(bool bBypass)
{
	// `CNPCMaker_Zombie::MakeNPC` `0x1034d140`, slot 617, read off the listing — the checklist's walk
	// stops at the hand-over and misses the last four steps.
	//
	//   1. `thunk_FUN_1034b7b0(this, param_1)` — the base `MakeNPC`. A null answer returns null.
	if (TrySpawn(bBypass) != EAttempt::Spawned)
	{
		return nullptr;
	}
	FElysiumEntity* Spawned = World != nullptr ? World->Resolve(LastSpawnedChild) : nullptr;
	FElysiumNpc* Child = Spawned != nullptr ? Spawned->AsNpc() : nullptr;
	if (Child == nullptr)
	{
		return nullptr;
	}

	//   2. `Weapon_OwnsThisType` on the maker. **RETAIL FAULTS on the true arm**: `XOR ESI,ESI` at
	//      entry leaves the item pointer null, `1034d173 JNZ 0x1034d19c` skips the lookup that would
	//      have filled it, and `1034d1a3 MOV EBP,[ESI]` then dereferences null. The decompiled C has
	//      the same shape (`piVar5` stays 0 and `iVar2 = *piVar5` runs unconditionally).
	//
	//      DIVERGENCE, named: this port takes the refusal instead of faulting. The arm is
	//      unreachable in retail anyway — a maker owns no weapons — and a crash guard where retail
	//      faults is the one divergence this story allows.
	if (ZombieMakerOwnsFists())
	{
		return Child;
	}

	//   3. `thunk_FUN_10136580("item_w_zombie_fists")` — the entity factory. A missing definition
	//      releases the spawned zombie through `thunk_FUN_101cd940` (`UTIL_Remove`) and answers null.
	//      SEAM: the classname is LOOKED UP rather than created, because creating an item entity here
	//      would be a second spawn event at a site retail's own `DispatchSpawn` (step 5) covers.
	if (!ZombieFistsItemExists())
	{
		Child->Kill();   // thunk_FUN_101cd940(this_00)
		return nullptr;
	}
	LastZombieFistsItem = GZombieFistsItem;

	//   4. `item->SetOrigin(this->EyePosition())` — vtable `+0xf8` is slot 62 `SetOrigin` and
	//      `+0x304` is slot **193 `EyePosition`**, not `GetAbsOrigin`: the item is placed at the
	//      MAKER'S EYE, which is its origin plus `m_vecViewOffset`. The checklist's walk says
	//      "origin".
	//   5. `item->+0x204 |= 0x40000000` (the flag word), `thunk_FUN_101d1280(item)` (`DispatchSpawn`),
	//      and — when the item's own `+0x1d0` check answers FALSE — `zombie->Weapon_Equip(item->+0xa0,
	//      false)` (vtable `+0x5fc`, slot 383). `+0xa0` is read BEFORE the flag is OR'd in
	//      (`1034d1bd MOV EBP,[ESI+0xa0]` precedes `1034d1ca MOV [ESI+0x204],ECX`).
	//
	//      SEAM: this runtime stands no item entity for the fists at kernel level and
	//      `FElysiumItemContainer` is the equip path; the hand-over is recorded.
	++ZombieFistsEquips;

	//   6. `___RTDynamicCast(this_00, 0, 0x10587908, 0x1062567c, 0)` and, on a hit,
	//      `thunk_FUN_103e0980(child, this->+0x76d0)`. **SEAM, unrecovered**: the cast's target type
	//      name lives at `0x1062567c`, whose bytes the corpus does not hold, and `+0x76d0` is an
	//      unregistered maker keyfield. Neither has a port carrier, so the arm is skipped — which is
	//      retail's own answer for a child whose class the cast refuses.
	//
	//   7. The spawn emitter: `"Zombies_spawning_emitter"` at the maker's `GetAbsOrigin` (slot 217)
	//      and its `GetAbsAngles(-1.0)` (slot 219), given a 15.0-second life. SEAM: this runtime
	//      stands no Source particle emitters, so the request is recorded.
	FZombieSpawnEmitter Emitter;
	Emitter.Origin = Origin;
	Emitter.Angles = Angles;
	Emitter.LifetimeSeconds = GZombieSpawnEmitterLifetimeSeconds;
	ZombieSpawnEmitters.Add(Emitter);
	(void)GZombieSpawnEmitterName;

	//   8. `child->Unhide()` (vtable `+0x10c`, slot 67) and `thunk_FUN_1029f300(child, false)` —
	//      `SetDisableAI(false)`, which OVERRIDES the copy `MakeNPC` made from the maker's own
	//      `m_bDisableAI` a few steps earlier. A zombie maker's child always thinks.
	//
	//      Slot 67's own body (`0x1009d380`) is a later story's and its generated stub still stands;
	//      it is DISPATCHED here rather than guessed at, which is what keeps the call in the tally
	//      instead of silently dropping it.
	Child->Unhide();
	Child->SetDisableAi(false);
	return Child;
}
