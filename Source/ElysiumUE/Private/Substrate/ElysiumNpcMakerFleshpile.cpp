#include "Substrate/ElysiumNpcMakerFleshpile.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"

// `CNPCMaker_Fleshpile` — story 5 fold A4. Bodies read off the listing (`vtmb_asm`) and the
// decompilation; the walked prose is `docs/vtmb/npc-ai/lifecycle.md` ("The fleshpile maker").

namespace
{
	// `_DAT_10452dc4` = 2.0f — Andrei's live-runner cap (`CNPC_VAndreiBlood`'s own `0x1035e920` reads
	// the same cell). The count is a datamap `FIELD_INTEGER` every body reads and writes as a float.
	constexpr int32 GFleshpileMaxActiveRunners = 2;
	// `_DAT_104454c0` = 1.0f — the step the runner count and the kill count move by.
	constexpr int32 GFleshpileRunnerStep = 1;
	// `s_npc_VAndreiBlood_1062525c`, the singleton search's classname.
	const TCHAR* const GFleshpileOwnerClassname = TEXT("npc_VAndreiBlood");
	// `s_npc_maker_fleshpile_106251f4` and case `0x154`'s search radius (`fVar8 = 1024.0`).
	const TCHAR* const GFleshpileClassname = TEXT("npc_maker_fleshpile");
	constexpr float GFleshpileSummonRadiusUnits = 1024.0f;
	// `MakeNPC`'s spawnflags: ORed in here (`m_spawnflags | 4`, `| 0x204` with `m_bFade`), where the
	// base maker overwrites.
	constexpr int32 GFleshpileChildSpawnFlags = 0x4;
	constexpr int32 GFleshpileChildFadeSpawnFlags = 0x204;
}

// Slot 82: `0x1034bdc0` returns `&datamap_CNPCMaker_Fleshpile` (`0x106250f0`).
void* FElysiumNpcMakerFleshpile::GetDataDescMap()
{
	return const_cast<FElysiumClassDesc*>(FElysiumClassRegistry::Get().Find(FName(RetailClassName)));
}

// Slot 103: `0x1034c020`.
void FElysiumNpcMakerFleshpile::Spawn()
{
	SpawnPrefix();
	if (!bDisabled)
	{
		// `PUSH 0x10010dd4` -> `0x1034c8b0`, the re-arm; `m_flNextThink = freq + curtime`.
		InstalledThink = EMakerThink::Fleshpile;
		ArmThinkAt((World != nullptr ? World->NowSeconds() : 0.0) + static_cast<double>(SpawnFrequency));
	}
	else
	{
		// `PUSH 0x1000572c` -> the inert `0x101c0b60`; `m_flNextThink` untouched.
		InstalledThink = EMakerThink::Inert;
	}
	++RelinkCalls;          // `CBaseEntity::Relink`, inside either arm
	CachedGroundZ = 0.0f;   // `m_flGround = 0`, last
}

// Slot 104: `0x1034c180`.
void FElysiumNpcMakerFleshpile::Precache()
{
	// The model half WITHOUT the developer overlay (the base arm's alone).
	if (!PrecacheMakerModel(/*bBadModelOverlay=*/false))
	{
		return;
	}
	// `CAI_BaseNPC::Precache` `0x1027bb50`, DIRECT.
	FElysiumNpcBase::Precache();
	// Nothing is tested after the chain: its reject arm `UTIL_Remove`s and returns, and this body
	// carries on regardless (a removal is deferred in retail).
	// `UTIL_PrecacheOther(m_iszNPCClassname)` with no emptiness check: an empty classname reaches
	// `UTIL_PrecacheOther("")`, whose own warning is the diagnostic, and the maker stays.
	NpcKernelPrecache10Shared::Precache10Other(*this, NpcType);
}

FElysiumNpcAndreiBlood* FElysiumNpcMakerFleshpile::CachedFleshpileOwner() const
{
	FElysiumEntity* Cached = World != nullptr ? World->Resolve(FElysiumNpc::FleshpileAndreiSingleton()) : nullptr;
	FElysiumNpc* Npc = Cached != nullptr && !Cached->IsDead() ? Cached->AsNpc() : nullptr;
	return Npc != nullptr ? Npc->AsSpecies<FElysiumNpcAndreiBlood>() : nullptr;
}

FElysiumNpcAndreiBlood* FElysiumNpcMakerFleshpile::FleshpileOwner() const
{
	if (FElysiumNpcAndreiBlood* Cached = CachedFleshpileOwner())
	{
		return Cached;
	}
	if (World == nullptr)
	{
		return nullptr;
	}
	// `FindEntityByClassname(NULL, "npc_VAndreiBlood")` (`0x100f7380`) — the FIRST listed — then
	// `___RTDynamicCast(found, 0, CBaseEntity, CNPC_VAndreiBlood 0x10625234, 0)`.
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate.IsValid() || Candidate->IsDead() || Candidate->Def == nullptr
			|| !Candidate->Def->Classname.Equals(GFleshpileOwnerClassname, ESearchCase::IgnoreCase))
		{
			continue;
		}
		FElysiumNpc* Npc = Candidate->AsNpc();
		FElysiumNpcAndreiBlood* Andrei = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcAndreiBlood>() : nullptr;
		FElysiumNpc::FleshpileAndreiSingleton() = Andrei != nullptr ? Andrei->Handle : FElysiumEntityHandle::Invalid();
		return Andrei;
	}
	return nullptr;
}

// Slot 130: `0x1034c260`.
void FElysiumNpcMakerFleshpile::OnRestore(bool bFromLoad)
{
	// `DAT_10938040 = cast(FindEntityByClassname(NULL, "npc_VAndreiBlood"))` — unconditionally, null
	// included — then `CAI_BaseNPCTroika::OnRestore` `0x102998c0`.
	FElysiumNpc::FleshpileAndreiSingleton() = FElysiumEntityHandle::Invalid();
	FleshpileOwner();
	FElysiumNpc::OnRestore(bFromLoad);
}

// Slot 139: `0x1034c8e0`.
void FElysiumNpcMakerFleshpile::DeathNotice(FElysiumEntity* Child)
{
	// `if (DAT_10938040 != 0)`: the cache as it stands, never filled here.
	if (FElysiumNpcAndreiBlood* Andrei = CachedFleshpileOwner())
	{
		// `___RTDynamicCast(child, 0, CBaseEntity, CNPC_VTzimisceRunner 0x10625270, 0)` — a typed test on
		// the tree, not a classname.
		FElysiumNpc* Npc = Child != nullptr ? Child->AsNpc() : nullptr;
		FElysiumNpcTzimisceRunner* Runner = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcTzimisceRunner>() : nullptr;
		if (Runner != nullptr && !Runner->bRunnerDeathNoticeProcessed)
		{
			Andrei->ActiveRunnerCount -= GFleshpileRunnerStep;   // +0x66b8 -= 1.0
			Andrei->AndreiKillCount += GFleshpileRunnerStep;     // +0x66bc += 1.0
			Runner->bRunnerDeathNoticeProcessed = true;          // +0x6671
		}
	}
	// `thunk_FUN_1034bc90` — `CNPCMaker::DeathNotice`, on every path.
	FElysiumNpcMaker::DeathNotice(Child);
}

// Slots 362-365: `0x1034bf10`, `0x1034bef0`, `0x1034bf50`, `0x1034bf30`.
bool FElysiumNpcMakerFleshpile::FInViewCone(const FVector& PointCm)
{
	(void)PointCm;
	return false;
}

bool FElysiumNpcMakerFleshpile::FInViewCone(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return false;
}

bool FElysiumNpcMakerFleshpile::FInAimCone(const FVector& TargetCm)
{
	(void)TargetCm;
	return false;
}

bool FElysiumNpcMakerFleshpile::FInAimCone(FElysiumEntity* AimTarget)
{
	(void)AimTarget;
	return false;
}

// Slot 370: `0x1034be90`.
FVector FElysiumNpcMakerFleshpile::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x1034bec0`.
FVector FElysiumNpcMakerFleshpile::HeadDirection3D()
{
	return BodyDirection3D();
}

// Slot 619: `0x1034bf70`.
void FElysiumNpcMakerFleshpile::ChildPreSpawn(FElysiumNpc* Child)
{
	(void)Child;
}

// Slot 620: `0x1034bf90`.
void FElysiumNpcMakerFleshpile::ChildPostSpawn(FElysiumNpc* Child)
{
	(void)Child;
}

// Slot 617: `0x1034c2d0`, in the listing's order.
FElysiumNpc* FElysiumNpcMakerFleshpile::MakeNPC(bool bBypass)
{
	if (World == nullptr)
	{
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	// 1. `DAT_10938040`, filled lazily, BEFORE the bypass test.
	FElysiumNpcAndreiBlood* Andrei = FleshpileOwner();
	// 2. Without the bypass: the three admission terms the base does not have — and NONE of the
	//    base's live-ceiling, scene, visibility, cone and distance terms.
	if (!bBypass)
	{
		if (Andrei == nullptr)
		{
			LastAttempt = EAttempt::InvalidChild;
			return nullptr;
		}
		// `if (2.0 <= Andrei->m_iActiveRunnerCount) return NULL;`
		if (Andrei->ActiveRunnerCount >= GFleshpileMaxActiveRunners)
		{
			LastAttempt = EAttempt::LiveLimit;
			return nullptr;
		}
		// The ground cache (`m_flGround == 0.0` re-traces), then the 34-unit box whose floor is the
		// cached ground — or the maker's own Z under `m_bNoDrop`, the one reader of `Flag_NoDrop`.
		CacheGroundZ();
		if (IsSpawnBoxOccupied(bNoDrop ? static_cast<float>(Origin.Z) : CachedGroundZ))
		{
			LastAttempt = EAttempt::Occupied;
			return nullptr;
		}
	}
	// 3. `CreateEntityByName(m_iszNPCClassname)`. No map-data replay on this class: the child is built
	//    from its classname and the copies below alone. `SetAbsOrigin(GetAbsOrigin())` and
	//    `SetAbsAngles(GetAbsAngles())` (child slots 62/64) come after the output; the def carries them.
	FElysiumEntityDef ChildDef;
	ChildDef.Classname = NpcType;
	ChildDef.Origin = Origin;
	ChildDef.Keys.Add(TEXT("angles"), FString::Printf(TEXT("%g %g %g"), Angles.X, Angles.Y, Angles.Z));
	const FElysiumEntityHandle ChildHandle = World->CreateRuntimeEntityNoSpawn(MoveTemp(ChildDef));
	FElysiumEntity* ChildEntity = World->Resolve(ChildHandle);
	if (ChildEntity == nullptr || ChildEntity->IsRecordOnly())
	{
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
		// `Warning("Non Troika Ent in NPCMaker!\n")`; the port removes the orphan (named divergence).
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: Non Troika Ent in NPCMaker! ('%s')"), *DebugString(), *NpcType);
		ChildEntity->Kill();
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	// 4. `child+0x1584 = this+0x1584` — `m_RelationshipString`, a SEAM (no port string member).
	// 5. `m_OnSpawnNPC.FireOutput(this, this)`.
	static const FName OnSpawnNpc(TEXT("OnSpawnNPC"));
	FireOutput(OnSpawnNpc, Handle);
	// 6. Spawnflags ORed: `| 4`, or `| 0x204` with `m_bFade`.
	Child->SpawnFlags |= bFade ? GFleshpileChildFadeSpawnFlags : GFleshpileChildSpawnFlags;
	// 7. The copy set, in the listing's order.
	Child->AdditionalEquipment = AdditionalEquipment;          // +0x5dec m_spawnEquipment
	Child->SquadName = SquadName;                              // +0x5da8 m_SquadName
	Child->SetHintGroup(BaseScheduleHost.HintGroup);           // 0x102781e0(child, +0x5db0)
	Child->AlternateEquipment = AlternateEquipment;            // +0x1a98 m_altEquipment
	Child->AuthoredPerception = AuthoredPerception;            // +0x63b0
	Child->AuthoredVision = AuthoredVision;                    // +0x63b4
	Child->AuthoredHearing = AuthoredHearing;                  // +0x63bc
	Child->Model = Model;                                      // child slot 105 SetModel(GetModelName())
	Child->TimesTalked = TimesTalked;                          // +0x64bc
	Child->Disposition = Disposition;                          // +0x6558 m_sDefaultDisposition
	Child->DefaultCamera = DefaultCamera;                      // +0x64c4
	Child->InterestingPlaceGroups = InterestingPlaceGroups;    // +0x62d8, the string only (+0x62dc is not copied)
	Child->InvestigateMode = InvestigateMode;                  // +0x6338
	Child->InvestigateModeCombat = InvestigateModeCombat;      // +0x633c
	Child->BrightRoutePenalty = BrightRoutePenalty;            // +0x6344
	Child->PlInvestigate = PlInvestigate;                      // +0x6348
	Child->PlCriminalFlee = PlCriminalFlee;                    // +0x634c
	Child->PlCriminalAttack = PlCriminalAttack;                // +0x6350
	Child->PlSupernaturalFlee = PlSupernaturalFlee;            // +0x6354
	Child->PlSupernaturalAttack = PlSupernaturalAttack;        // +0x6358
	Child->AuthoredPerception = AuthoredPerception;            // +0x63b0, the listing copies the triple twice
	Child->AuthoredVision = AuthoredVision;                    // +0x63b4
	Child->AuthoredHearing = AuthoredHearing;                  // +0x63bc
	Child->PlayerReaction = PlayerReaction;                    // +0x63ac
	Child->bUseInteresting = bUseInteresting;                  // +0x63d9
	Child->Skin = Skin;                                        // +0x670 m_nSkin
	Child->StatTemplate = StatTemplate;                        // +0x10e4 m_statTemplate
	// 8. Slot 619, `DispatchSpawn`, `SetOwnerEntity(this)`, the name, slot 620.
	ChildPreSpawn(Child);
	World->CallEntitySpawn(*Child);
	if (Child->IsDead())
	{
		// **Named divergence**, as in `FElysiumNpcMaker::MakeNPC`: retail owns, names and budgets a
		// child its own spawn removed (the removal is deferred and slot 139 settles it); the port's
		// immediate `Kill` cannot reach the owner, so the child is dropped here and Andrei's
		// `m_iActiveRunnerCount` is not incremented.
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s Spawn: '%s' removed itself during Spawn"),
			*DebugString(), *NpcType);
		LastAttempt = EAttempt::InvalidChild;
		return nullptr;
	}
	Child->SetOwnerEntity(Handle);
	if (!ChildTargetName.IsEmpty())
	{
		World->RenameEntity(*Child, ChildTargetName);
	}
	ChildPostSpawn(Child);
	// 9. `DAT_10938040->m_iActiveRunnerCount += 1.0`. No `m_cLiveChildren`, no `m_iMaxNumNPCs`, no
	//    `m_bCameFromSpawner`: this body writes none of the base's counters. On the bypass path with
	//    no Andrei retail dereferences null here; the port skips the increment (named divergence).
	if (Andrei != nullptr)
	{
		Andrei->ActiveRunnerCount += GFleshpileRunnerStep;
	}
	LastAttempt = EAttempt::Spawned;
	UE_LOG(LogElysiumNpcEnt, Log, TEXT("%s Spawn -> %s (Andrei runners %d)"), *DebugString(),
		*World->DescribeHandle(ChildHandle), Andrei != nullptr ? Andrei->ActiveRunnerCount : -1);
	return Child;
}

void FElysiumNpcMakerFleshpile::Think()
{
	if (InstalledThink == EMakerThink::Fleshpile)
	{
		FleshpileMakerThink();
		return;
	}
	// `Enable` installs the BASE think on this class too; the inert body and NULL do nothing.
	FElysiumNpcMaker::Think();
}

// `0x1034c8b0`: `FLD [ECX+0x6664]; FADD [curtime]; FSTP [ECX+0x17c]; RET`.
void FElysiumNpcMakerFleshpile::FleshpileMakerThink()
{
	ArmThinkAt((World != nullptr ? World->NowSeconds() : 0.0) + static_cast<double>(SpawnFrequency));
}

FElysiumNpc* FElysiumNpcMakerFleshpile::SummonRunnerNear(FElysiumEntityWorld& InWorld,
	const FVector& AndreiOriginCm)
{
	FElysiumEntity* Nearest = FindNearestByClassname(InWorld, GFleshpileClassname, AndreiOriginCm,
		GFleshpileSummonRadiusUnits);
	FElysiumNpc* Npc = Nearest != nullptr ? Nearest->AsNpc() : nullptr;
	FElysiumNpcMakerFleshpile* Maker = Npc != nullptr ? Npc->AsSpecies<FElysiumNpcMakerFleshpile>() : nullptr;
	// `(**(code **)(*maker + 0x9a4))(0)` — slot 617, VIRTUAL.
	return Maker != nullptr ? Maker->MakeNPC(/*bBypass=*/false) : nullptr;
}
