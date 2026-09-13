#include "Substrate/ElysiumNpcMaker.h"

#include "ElysiumEntityDefs.h"
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

void FElysiumNpcMaker::Spawn()
{
	LiveChildren = 0;
	CachedGroundZ = 0.0f;
	if (bInfinite)
	{
		bFade = true;
	}
	NextThink = bDisabled ? ELYSIUM_NEVER_THINK
		: static_cast<float>((World ? World->NowSeconds() : 0.0) + SpawnFrequency);
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

	static const FName OnSpawnNpc(TEXT("OnSpawnNPC"));
	FireOutput(OnSpawnNpc, Handle);
	ChildEntity->SpawnFlags = bFade ? 0x204 : 4;
	// `MakeNPC` `0x1034b7b0`, right after the spawnflags: `child->SetDisableAI(this->GetDisableAI())`.
	if (FElysiumNpc* ChildNpc = ChildEntity->AsNpc())
	{
		ChildNpc->SetDisableAi(bDisableAi);
	}
	World->CallEntitySpawn(*ChildEntity);
	if (ChildEntity->IsDead())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' removed itself during Spawn"),
			*DebugString(), *NpcType);
		return LastAttempt = EAttempt::InvalidChild;
	}
	ChildEntity->SetOwnerEntity(Handle);
	World->RenameEntity(*ChildEntity, ChildTargetName);
	++LiveChildren;
	if (!bInfinite)
	{
		--RemainingTotal;
		if (IsDepleted())
		{
			NextThink = ELYSIUM_NEVER_THINK;
		}
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
