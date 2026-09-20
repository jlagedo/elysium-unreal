#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29e, family **Lifecycle19** — the spine: Base/Troika `NPCInit`, `StartNPC`, `OnRestore`,
// the three slot dispatchers, and the helpers those bodies share. Species `NPCInit` arms live in
// `ElysiumNpcKernelLifecycle19_2.cpp`.

namespace
{
	constexpr int32 GLifecycle19Slot420 = 420;
	constexpr int32 GLifecycle19Slot422 = 422;
	constexpr int32 GLifecycle19Slot130 = 130;
	constexpr uint32 GFlOnGround = 1u;
	constexpr float GFltMax = 3.402823466e+38f;

	bool GLifecycle19InNpcInit = false;
	int32 GLifecycle19NodeGraphHull = 0;
	int32 GLifecycle19NodeIndexErrors = 0;
	FElysiumEntityHandle GLifecycle19FleshpileAndrei;
	FElysiumEntityHandle GLifecycle19MingXiaoTentacle;

	double Lifecycle19Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	bool Lifecycle19IsNoneSentinel(const FString& Authored)
	{
		if (Authored.IsEmpty())
		{
			return true;
		}
		if (Authored.Len() >= 1 && Authored[0] == TEXT('0')
			&& (Authored.Len() == 1 || Authored[1] == 0))
		{
			return true;
		}
		return Authored.Equals(TEXT("0"), ESearchCase::CaseSensitive);
	}

	bool Lifecycle19IsUnarmedSentinel(const FString& Authored)
	{
		// 15-byte `strncmp` against `"item_w_unarmed"`.
		const TCHAR* Literal = TEXT("item_w_unarmed");
		int32 Remain = 15;
		int32 Index = 0;
		while (Remain > 0)
		{
			const TCHAR A = Index < Authored.Len() ? Authored[Index] : 0;
			const TCHAR B = Literal[Index];
			if (A != B)
			{
				return false;
			}
			if (A == 0)
			{
				return true;
			}
			++Index;
			--Remain;
		}
		return true;
	}

	FElysiumEntity* Lifecycle19ResolveHandle(const FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		if (Npc.World == nullptr || !Handle.IsSet())
		{
			return nullptr;
		}
		return Npc.World->Resolve(Handle);
	}
}

bool& FElysiumNpc::InNpcInit()
{
	return GLifecycle19InNpcInit;
}

int32& FElysiumNpc::NodeGraphHullIndex()
{
	return GLifecycle19NodeGraphHull;
}

int32& FElysiumNpc::NodeIndexErrorCount()
{
	return GLifecycle19NodeIndexErrors;
}

FElysiumEntityHandle& FElysiumNpc::FleshpileAndreiSingleton()
{
	return GLifecycle19FleshpileAndrei;
}

FElysiumEntityHandle& FElysiumNpc::MingXiaoTentacleCache()
{
	return GLifecycle19MingXiaoTentacle;
}

const TCHAR* FElysiumNpc::NpcInitThinkFunction()
{
	return TEXT("0x10273aa0");
}

const TCHAR* FElysiumNpc::StartNpcThinkFunction()
{
	return TEXT("LAB_1000f4e8");
}

void FElysiumNpc::ThinkSet(const TCHAR* Function, double Delay)
{
	++ThinkSetCalls;                                                     // ThinkSet
	ThinkFunctionName = Function != nullptr ? FString(Function) : FString();
	ThinkSetDelay = Delay;
	if (Function == nullptr || Function[0] == 0)
	{
		NextThink = ELYSIUM_NEVER_THINK;                                 // ThinkSet(NULL)
		return;
	}
	if (Delay != 0.0)
	{
		ArmThinkAt(Lifecycle19Now(*this) + Delay);
	}
}

bool FElysiumNpc::MoveProbeFloorDrop(FVector& InOutOriginUnits)
{
	// SEAM for `CAI_MoveProbe::TraceHull` `0x102e7880` on `m_pMoveProbe +0x5d40`, mask `0x202400b`,
	// swept from `0.0` down to `-256.0`. Retail answers 1 when the sweep FOUND floor (fraction != 1)
	// and writes `param_5 = trace.endpos`; it answers 0 — the "stuck in wall" warning — otherwise.
	// Three searches: the address has no port body; `+0x5d40` is the CHAIN row onto Motor; the two
	// probe bodies this runtime does carry are `MoveProbeCheckStandPosition` (`0x102e7270`) and
	// `MotorMoveTraceSweep` (`0x102e6d70`), neither of which is this hull sweep. Answers the
	// found-floor arm with a zero-length drop, so the origin is left where the caller put it.
	(void)InOutOriginUnits;
	return true;
}

void FElysiumNpc::RestoreGiveUp()
{
	// `0x1027be60`.
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();                                    // 1027be6x
	}
	++NavigationGoalClears;
	ClearSchedule();                                                     // 10280d30
	if (GetEnemy() == nullptr)
	{
		Cognition.Conditions.Reset();                                    // six-word block +0x5c5c
	}
	if (NpcStateRetail() == 4 && !ScriptOwnerIsLive())
	{
		SetState(1);                                                     // 1026e340 IDLE
		WriteIdealStateRetail(1);
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Scripted Sequence stripped on level transition"));     // 1027bexx
	}
}

bool FElysiumNpc::RefindPostRestorePath()
{
	// SEAM for `0x102ee1e0`. No navigator: answers failure.
	++PostRestorePathRefinds;
	return false;
}

int32 FElysiumNpc::ResolveCombatStartActivity(const FString& Name) const
{
	// `0x1029f340` whole: `if (name == NULL) return -1; return ActivityNameToId(name);`. Its only
	// callee is `0x10412520`, which family Hints already stands as `ActivityIdForName`, so this is
	// the two-line body and not a second seam beside it.
	if (Name.IsEmpty())
	{
		return INDEX_NONE;                                               // 1029f34x null arm
	}
	return ActivityIdForName(Name);                                      // 10412520
}

void FElysiumNpc::ClearFollowerBossName()
{
	// `0x102c4430("")`: SetFollowerBoss then `m_sFollowerBoss = NULL` when empty.
	SetFollowerBossByAuthoredName(FString());
	FollowerBossName.Reset();
}

void FElysiumNpc::SetFollowerBossByAuthoredName(const FString& Name)
{
	// SEAM for `0x102c44e0`. Overlay row of another family; Squad's `SetFollowerBossName` is
	// `0x102c4470` and already states this body is unported; `FollowerBoss` (`+0x647c`) has no
	// writer. Counted so StartNPC's call is observable.
	++FollowerBossOnStartCalls;
	(void)Name;
}

void FElysiumNpc::InstallScheduleRetail(int32 RawId, bool bForce)
{
	++SetScheduleRetailCalls;
	LastSetScheduleRetail = RawId;
	bLastSetScheduleForce = bForce;
	const int32 Stamp = ResolveIdealScheduleStamp(RawId);                // 10280de0 first half
	LastIdealScheduleStamp = Stamp;
	const EElysiumScheduleId Mapped = ScheduleFromRetailNumber(Stamp);
	if (Mapped != EElysiumScheduleId::None)
	{
		ChangeSchedule(Mapped);
		return;
	}
	// Story 25 miss arm: registry miss installs `IDLE_STAND` untranslated.
	ElysiumSchedule::Start(Schedule, EElysiumScheduleId::IdleStand, *this);
}

void FElysiumNpc::SeedStatListOnNpcInit()
{
	++StatListSeeds;
	// `1029a5a1`–`1029a67f`. The walk over `+0x13bc`/`+0x13c0` picks the stat list whose `+0x10` is
	// 0 — the ATTRIBUTES container — and falls back to the process-wide `DAT_109f0b40` when the NPC
	// carries none. This runtime's sheet IS that container set, so the two writes land on it:
	// `CVStatList_t::Set(0xf, 0)` is slot 15 Health (damage taken) and `SetBase(0xc, x)` slot 12
	// BloodPool, with `x` the stat template's `+0xd0 -> +0x30`, which `SeedSheet` already resolved
	// into `TemplateBloodPoolValue`.
	Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);          // 1029a5f4
	// `1029a661`: the blood value comes from `0x101d5f10(DAT_10738d10, this)->+0xd0->+0x30`, a
	// per-NPC record retail always has. This runtime resolves that record from the NPC's
	// `stattemplate`, so an NPC with no template has none — and writing 0 there would DRAIN it,
	// which is not what the missing record means. The seeded pool stands in that case.
	if (TemplateBloodPoolValue > 0)
	{
		Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool,
			TemplateBloodPoolValue);                                                   // 1029a67a
	}
}

void FElysiumNpc::SeedCriminalLevelWitnessed()
{
	// `0x1042fde0(0)` then the scramble into `+0x6364`. The port carries the PLAIN level on
	// `Witness.Channel`; the two uninitialised tag bytes write 0 (Senses.inl named defect).
	CriminalWitnessByte6360 = 0;
	CriminalWitnessByte6361 = 0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Level = 0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Level = 0;
	(void)EncodeWitnessedLevel(0);
}

void FElysiumNpc::SpawnEquipLoadout()
{
	// Spawn-equip block, gated on slot 513 bit `0x200000`. Item *creation* stays deferred to
	// `ResolveLoadout` on first think (creating an entity inside Activate would invalidate the
	// spawn array). The DECISION is reproduced here: skip `"0"` (2-byte) and `"item_w_unarmed"`
	// (15-byte), else record a request.
	const uint32 Caps = static_cast<uint32>(CapabilitiesGet());          // 1029a1xx slot 513
	if ((Caps & CapabilitySpawnEquip) == 0)
	{
		return;
	}
	auto TryOne = [this](const FString& Authored)
	{
		if (Authored.IsEmpty() || Lifecycle19IsNoneSentinel(Authored)
			|| Lifecycle19IsUnarmedSentinel(Authored))
		{
			return;
		}
		++SpawnEquipRequests;
	};
	TryOne(AlternateEquipment);
	TryOne(AdditionalEquipment);
}

void FElysiumNpc::HideActiveWeaponIfAny()
{
	// `1038714a CALL GetActiveWeapon` / `TEST EAX,EAX / JZ` / `MOV EDX,[EAX]; JMP [EDX+0x108]` —
	// slot 66 `Hide` on the ACTIVE WEAPON, not on this NPC. A null weapon returns with nothing
	// written. `FElysiumWeapon::Hide` is the same body family State19 already dispatches from
	// `CopHumanCombatantOnStateChange`, so it is called rather than counted.
	FElysiumItem* const Active = Inventory.Active(*this);
	FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		return;                                                          // 1038714f JZ
	}
	++HideActiveWeaponCalls;
	Weapon->Hide(this);                                                  // 1038715f JMP [+0x108]
}

namespace
{
	// `0x101e8c50` / `0x101e8c70` / `0x101e8c30` / `0x101e8bf0` are NOT cvars: each is a seven-byte
	// `__fastcall` getter of a float field on the process-global `CVFeatList_t` (`0x10739d08`,
	// `101e4d90`), whose fields `0x101e6310` loads out of `vdata/system/Rules.txt` through
	// `KeyValues::GetFloat(key, default)`. The defaults below are the immediates in that loader;
	// the live value comes from the rulebook, exactly as `FElysiumNpcSenses::ResolveTuning` already
	// reads `FreeKnowledgeDuration` from the same section.
	float Lifecycle19RulesFloat(const FElysiumNpc& Npc, const TCHAR* Section, const TCHAR* Key,
		float ImageDefault)
	{
		UElysiumSessionSubsystem* GameState = Npc.World != nullptr ? Npc.World->GetGameState() : nullptr;
		UElysiumRulebookSubsystem* Rules = GameState != nullptr ? GameState->Rulebook() : nullptr;
		return Rules != nullptr ? Rules->Rules().Flt(Section, Key, ImageDefault) : ImageDefault;
	}
}

float FElysiumNpc::TuningOccludedDelayNormal() const
{
	// `0x101e8c50` → `CVFeatList_t +0x288`, `Npc_Combat_Info/OccludedDelayNormal`, image default 0.5.
	return Lifecycle19RulesFloat(*this, TEXT("Npc_Combat_Info"), TEXT("OccludedDelayNormal"), 0.5f);
}

float FElysiumNpc::TuningOccludedDelayCover() const
{
	// `0x101e8c70` → `+0x28c`, `Npc_Combat_Info/OccludedDelayCover`, image default 5.0.
	return Lifecycle19RulesFloat(*this, TEXT("Npc_Combat_Info"), TEXT("OccludedDelayCover"), 5.f);
}

float FElysiumNpc::TuningEnemyStoreInterval() const
{
	// `0x101e8c30` → `+0x284`, `Npc_Combat_Info/FreeKnowledgeDuration`, image default 0.25. This is
	// the same word `FElysiumNpcSenses::ResolveTuning` already seeds onto `EnemyMemory`, which is
	// what `*(m_pEnemyStore + 0x10)` IS here; the accessor answers it rather than a second copy.
	return static_cast<float>(EnemyMemory.FreeKnowledgeDuration);
}

float FElysiumNpc::TuningZombieGrappleReadyInterval() const
{
	// `0x101e8bf0` → `+0x27c`, `Zombie_Grapple_Info/DelayInitial`, image default 20.0 (the shipped
	// `rules.txt` authors 10.0, which the rulebook read picks up).
	return Lifecycle19RulesFloat(*this, TEXT("Zombie_Grapple_Info"), TEXT("DelayInitial"), 20.f);
}

int32 FElysiumNpc::FrenzyShadowHostileRecount()
{
	// `0x10376c10`, the tail `JMP` of `CNPC_VFrenzyShadow::NPCInit`.
	FrenzyShadowHostileEnemyCount = 0;                                   // 10376c17 +0x6664
	// `10376c2x`: the relation argument is `m_hFriendPlayer`'s entity `+0xa8`, resolved through the
	// handle table, or NULL when the handle is stale. Resolved here for the same reason.
	(void)Lifecycle19ResolveHandle(*this, FriendPlayer);
	// `10376c65`–`10376cb5` then walks `DAT_1093ada8[0 .. DAT_1093ae9c)`, counting `slot 404 == D_HT`
	// and calling slot 544 `UpdateEnemyMemory` per entry. That array is a 32-entry static scratch
	// list whose ONLY producer is `0x10376d00` — a ±1024/±1024/±128 `EntitiesInBox` sweep filtered
	// on the ground flag, `+0x340`, slot 158 and `0x100b5190` — which `NPCInit` never calls. On a
	// fresh map `DAT_1093ae9c` is 0, so the whole loop is skipped and the count reset is the body's
	// only observable effect. (The unguarded `10376c98 MOV EDX,[ESI]` is therefore unreachable: the
	// producer admits an entry only when both pointers are non-null.)
	//
	// SEAM for that scratch list: this runtime has no `0x10376d00` sweep, so the list is empty and
	// the loop does not run. Three searches: `0x10376d00` has no port body; `DAT_1093ada8` has no
	// port datum; no `HostileScratch`/`ShadowScratch` identifier exists in the substrate. Walking
	// `World->Entities()` instead would seed enemy memory for the whole map at spawn, which retail
	// does not do.
	return FrenzyShadowHostileEnemyCount;
}

int32 FElysiumNpc::FindInterestingPlaceHoldingMe() const
{
	// `0x102db5e0`: walk every interesting place (`DAT_10927194`, `+0x540` next) and answer the LAST
	// one whose marker table (`+0x580`, `+0x588` records of stride `0x1c`, first dword the occupant)
	// holds this NPC. Retail keeps scanning after a hit rather than breaking, so the last wins.
	if (World == nullptr)
	{
		return INDEX_NONE;
	}
	int32 Found = INDEX_NONE;
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		if (!Candidate.IsValid() || Candidate->Def == nullptr
			|| !Candidate->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FElysiumInterestingPlace* Place =
			static_cast<const FElysiumInterestingPlace*>(Candidate.Get());
		for (const FElysiumInterestingPlace::FMarker& Marker : Place->Markers)
		{
			if (Marker.Occupant == Handle)
			{
				Found = Place->Handle.Index;
			}
		}
	}
	return Found;
}

void FElysiumNpc::ValidateRestoredInterestingPlace()
{
	// `0x10299a80`, the pass `CAI_BaseNPCTroika::OnRestore` runs over the place the scan above just
	// wrote. Two refusals, each a `DevMsg` and a cleared `+0x62ec`.
	if (CurrentSpotIndex == INDEX_NONE)
	{
		return;                                                          // 10299a97
	}
	FElysiumInterestingPlace* const Place = CurrentAmbientSpot();
	const FVector OriginUnits = Origin / ElysiumMove::U;
	if (Place == nullptr)
	{
		// `10299aa6`: the place is not on the live list at all.
		++RestorePlaceRejections;
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("ERROR: %s loc( %6.2f, %6.2f, %6.2f) thinks they are at an interesting place that ")
			TEXT("no longer seems to be a valid entity!"),
			*DebugString(), OriginUnits.X, OriginUnits.Y, OriginUnits.Z);
		CurrentSpotIndex = INDEX_NONE;                                   // 10299b02
		return;
	}
	for (const FElysiumInterestingPlace::FMarker& Marker : Place->Markers)
	{
		if (Marker.Occupant == Handle)
		{
			return;                                                      // 10299b2c / 10299b3f
		}
	}
	// `10299b45`: on the list, but its marker table does not name this NPC.
	++RestorePlaceRejections;
	const FVector PlaceUnits = Place->Origin / ElysiumMove::U;
	UE_LOG(LogElysiumNpcEnt, Log,
		TEXT("ERROR: %s loc( %6.2f, %6.2f, %6.2f) thinks they are at %s but that InterestingPlace, ")
		TEXT("loc(%6.2f, %6.2f, %6.2f) does not know that."),
		*DebugString(), OriginUnits.X, OriginUnits.Y, OriginUnits.Z, *Place->DebugString(),
		PlaceUnits.X, PlaceUnits.Y, PlaceUnits.Z);
	CurrentSpotIndex = INDEX_NONE;                                       // 10299be2
}

void FElysiumNpc::WerewolfRearm()
{
	// `0x103cac20`, run from BOTH `CNPC_VWerewolf::NPCInit` and `CNPC_VWerewolf::OnRestore`.
	const double Now = Lifecycle19Now(*this);
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());    // 103cac2a
	Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();        // 103cac32 +0x628c
	WerewolfMorphTimerA = 0.f;                                           // 103cac38 +0x66a4
	bWerewolfTaskFailed = false;                                         // 103cac3e +0x66a1
	WerewolfSnapWordA = 0;                                               // 103cac44 +0x66a8
	bWerewolfPlayFrustration = false;                                    // 103cac4a +0x66a9
	WerewolfMorphTimerB = 0.f;                                           // 103cac50 +0x66d4
	WerewolfMorphTimerC = 0.f;                                           // 103cac56 +0x66d8
	WerewolfFakeHullPosUnits = FVector::ZeroVector;                      // 103cac62/6e/7a vec3_origin
	// `+0x66ec` is ONE retail word carried twice in this port (`WerewolfUnhideStamp` from family
	// Lifecycle, `WerewolfLastSeenTime` from family Positions). Both take the stamp so the two
	// copies cannot disagree; collapsing them is the owning families' to do.
	WerewolfUnhideStamp = Now;                                           // 103cac89 +0x66ec
	WerewolfLastSeenTime = Now;
	WerewolfFakeHullPushTime = Now;                                      // 103cac9f +0x66f4
	WerewolfMoveHintSearchStart = 0;                                     // 103caca5 +0x66b8 (NULL)
	WerewolfWord66ac = 0;                                                // 103cacab +0x66ac (NULL)
	WerewolfHintNodeCacheA = INDEX_NONE;                                 // 103cacb1 +0x6708
	RandomMoveHintNodeZone = INDEX_NONE;                                 // 103cacb7 +0x670c
	WerewolfHintFlags = 0;                                               // 103cacbd +0x66e8
	WerewolfWord66f8 = 0;                                                // 103cacc3 +0x66f8
	WerewolfWord66fc = 0;                                                // 103cacc9 +0x66fc
	NearestNodeToPlayerRefreshedAt = 0.0;                                // 103caccf +0x6700
	NearestNodeToPlayer = 0;                                             // 103cacd5 +0x6704
	// The teleport-distance floor. BOTH writes are `+=` (`103cad59` / `103cad6d` are `FADD`/`FSTP`),
	// neither word is in the datamap, and the body runs from `NPCInit` AND `OnRestore`: retail
	// defect 2, the floor grows with every load. The first term is the hull's horizontal half-extent
	// (`0x102d6100` mins / `0x102d6120` maxs, `_DAT_104454d0` = 0.5), which this runtime answers
	// through `HullMinsUnits`/`HullMaxsUnits` — a seam that is zero until a hull table stands.
	const FVector HullMins = HullMinsUnits(false);                       // 103cace2 0x102d6100
	const FVector HullMaxs = HullMaxsUnits(false);                       // 103cacf6 0x102d6120
	const float HalfX = static_cast<float>(HullMaxs.X - (HullMaxs.X + HullMins.X) * HullCentreHalf);
	const float HalfY = static_cast<float>(HullMaxs.Y - (HullMins.Y + HullMaxs.Y) * HullCentreHalf);
	WerewolfTeleportDistanceA += FMath::Sqrt(HalfX * HalfX + HalfY * HalfY);   // 103cad59 +0x66cc
	WerewolfTeleportDistanceB +=
		static_cast<float>(FMath::Sqrt(WerewolfTeleportFloorSquare));    // 103cad6d +0x66d0
}

// =================================================================================================
// `0x10273390` — `CAI_BaseNPC::NPCInit`
// =================================================================================================

void FElysiumNpc::BaseNPCInit()
{
	// Retail sets both hull words in a CONSTRUCTOR, long before this. The port has no constructor
	// chain to hang them on -- one `FElysiumNpc` wears every class -- so they are resolved here,
	// which is the first body that runs with the retail class known and still ahead of everything
	// that reads a hull. The values and their order are the constructors'; only the moment moves.
	ApplyRetailHulls();
	bNpcTransparent = true;                                              // 1027339x SetNPCTransparent(1)
	Senses.Memory.LastDamageAttacker = FElysiumEntityHandle::Invalid();  // m_hLastDamageEnt = -1
	Cognition.bCondTookDamage = false;                                   // 102733xx
	++BaseInitAnimatingResets;                                           // ClearAllClientRagdolling
	Flags |= NpcInitAddFlags;                                            // AddFlag(0x12000)
	Gravity = 1.f;                                                       // m_flGravity
	TakeDamageMode = 2;                                                  // m_takedamage
	// `thunk_FUN_102e0b40(m_pMotor)` — `*(motor+0x2c) = -1.0`, the yaw-speed hold. SEAM: the same
	// one families Conditions and Hints already stand for this call; no motor word carries it.
	// Then yaw from `GetAbsAngles()[1]`, `±180` when the motor's animation-movement byte is set, and
	// `motor+0x1c == 180.0` writes `motor+0x34` directly while anything else goes through
	// `0x102e0a80`. SEAM: no motor `+0x1c`; `DesiredMoveYaw` takes the direct write.
	ScheduleHost.DesiredMoveYaw = static_cast<float>(Angles.Y);
	if (ScheduleHost.bMotorAnimationMovement)
	{
		if (ScheduleHost.DesiredMoveYaw < MotorYawHalfTurn)
		{
			ScheduleHost.DesiredMoveYaw += MotorYawHalfTurn;
		}
		else
		{
			ScheduleHost.DesiredMoveYaw -= MotorYawHalfTurn;
		}
	}
	MaxHealth = 100;                                                     // 102733xx
	bDead = false;                                                       // m_lifeState = 0
	SetDeathReportedForRestore(false);
	// +0x1b3c/+0x1b40 provenance ABSENT (shape map). Line 0x1af1 is not stored.
	WriteIdealStateRetail(1);                                            // m_IdealNPCState = IDLE
	const int32 Sequence = SelectHeaviestSequence(1, INDEX_NONE);
	if (Sequence < 0)
	{
		const int32 Fallback = SelectHeaviestSequence(0xf1, INDEX_NONE);
		if (Fallback < 0)
		{
			// m_nSequence = 0. No sequence member on the kernel surface; ActivityNumber stays.
		}
		else
		{
			SetIdealActivity(0xf1);                                      // 10272650
		}
	}
	else
	{
		SetIdealActivity(1);
	}
	ScheduleHost.bShouldMove = false;                                    // +0x1a40
	CollisionMask = NpcInitCollisionMask;                                // +0x1a44
	// `102734cb`: `*(m_pNavigator + 0x2c) = DAT_1093407c` — the process-wide node network the map's
	// `.ain` loaded (`0x102f65b0` / `0x102f6690` are its only writers). SEAM: no navigator and no
	// node network stand here, so there is nothing to point at; `TroikaOnRestore`'s ped-link arm
	// states the same absence from the reading side.
	ClearSchedule();                                                     // 10280d30 — every class
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();                                    // 102ee270
	}
	++NavigationGoalClears;
	++BaseInitAnimatingResets;                                           // 10095be0 + 1008f540
	ScheduleHost.HintNode = INDEX_NONE;                                  // m_pHintNode = 0
	ScheduleHost.MemoryBits = 0;                                         // m_afMemory
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
	DistTooFar = BaseInitDistTooFar;
	SetDistLook(BaseInitDistLookUnits * ElysiumMove::U);                 // 1026a2a0(3072)
	bKeepSound = false;
	if ((static_cast<uint32>(SpawnFlags) & SpawnFlagFarSight) != 0)
	{
		DistTooFar = FarSightDistTooFar;
		SetDistLook(FarSightDistLookUnits * ElysiumMove::U);
	}
	Cognition.Conditions.Reset();                                        // six words +0x5c5c
	Cognition.DelayedConditions.Reset();                                 // 102cc7e0 +0x1a9c
	++DelayedConditionListClears;
	Senses.ClearPendingSounds();                                         // 102cc7e0 +0x1ae0
	++DelayedConditionListClears;
	SetDefaultEyeOffset();                                               // 10274ca0
	++BaseInitTailCalls;
	// `1027359x`: `m_pfnUse = &LAB_10004da9`, a retail member-function pointer. This runtime
	// dispatches `Use` through the entity's own virtual, so there is no pointer word to write.
	const double Now = Lifecycle19Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(NpcInitThinkFunction(), 0.0);                           // 10273aa0
		ArmThinkAt(Now + NpcInitThinkDelay);
	}
	else
	{
		++NpcInitInlineThinkCalls;                                       // inline 10273aa0
	}
	Mind.ClearForceStateChange();                                        // +0x1b28
	Unknown5b58 = 0;                                                     // +0x5b58
	NpcFlags.SetFrenziedWord(0);                                         // +0x5b84
	NpcInitTime = Now;                                                   // +0x5b5c
	WeaponBlockedByFriendTimer = 0.0;
	ExtendedBlockedByFriendTimer = static_cast<double>(GFltMax);
	Mind.StampLastStateChangeTime(0.0);                                  // +0x5cc8
	Senses.Memory.EnemyOccludedCheck = 10;
	ShootTargetOverride = FElysiumEntityHandle::Invalid();
	// +0x5b90 m_pSurfaceData ABSENT.
	bCineScriptHidden = false;                                           // 102735xx +0x5d78
	bInChoreoScene = false;
	UpdateEnemyWentOccluded(nullptr, false);                             // 10270180(NULL, 0)
	++BaseInitChoreoClears;
	Senses.Memory.LastDamageTime = 0.0;
	LastAttackTime = 0.0;
	Senses.Memory.SoundWaitTime = 0.0;
	NextEyeLookTime = 0.f;                                               // +0x5d6c
	NextWeaponSearchTime = 0.0;
	ScheduleHost.WaitFinished = 0.0;
}

// =================================================================================================
// `0x1029a0b0` — `CAI_BaseNPCTroika::NPCInit`
// =================================================================================================

void FElysiumNpc::TroikaNPCInit()
{
	InNpcInit() = true;                                                  // 1029a0b3 DAT_10937cf1 = 1
	// `1029a0c0`: `m_fEffects = 0`. This runtime spells that word's one modelled bit — `EF_NODRAW`
	// (`0x40`) — as `FElysiumEntity::bHidden`, which is what `FElysiumWeapon::Hide` writes and what
	// `Weapon_TranslateActivity` reads; clearing the word therefore UNHIDES. Retail does exactly
	// that here, before the base body runs, and `CNPC_VZombie::NPCInit` re-hides itself afterwards.
	bHidden = false;                                                     // 1029a0c0 m_fEffects +0x19c
	// `1029a0c6`–`1029a0ef`: `m_iHealth = ftol(DAT_10923f14->IsCommand() ? 0.0 : cvar+0x28)`.
	// SEAM: `DAT_10923f14` is an unnamed `ConCommandBase*` this image never writes — its four
	// referrers are this body and its three thunks — so the value it seeds is unrecoverable from
	// the corpus and `Health` is left where `SeedSheet` put it. Counted so the arm is visible.
	++NpcInitHealthSeeds;
	WriteNpcStateRetail(0);                                              // 1029a0f5 m_NPCState = NONE
	Senses.ResetListenClock();                                           // 1029a0fb +0x63d0 = 0
	// `1029a101`: `m_pfnTouch = &LAB_1000cb76`, a retail member-function pointer with no port word.
	BaseNPCInit();                                                       // 1029a10b -> 10273390
	const double Now = Lifecycle19Now(*this);
	Senses.Memory.bPlayerInPvs = true;
	Senses.Memory.bPlayerLos = true;
	Senses.Memory.PlayerPvsLastClearTime = Now;
	Senses.Memory.PlayerLosLastClearTime = Now;
	Senses.Memory.PlayerLosNextUpdateTime = 0.0;
	Senses.Memory.NextCheckEnterPvsTime = 0.0;
	NpcFlags.AssignAiFlagsWord(0);                                       // m_bfAINPCFlags = 0
	bForceNpcCheck = false;
	OccludedDelayNormal = TuningOccludedDelayNormal();
	OccludedDelayCover = TuningOccludedDelayCover();
	OccludedDelay = OccludedDelayNormal;
	OccludedReportTimeE = Now;
	OccludedReportTimeT = Now;
	OccludedReportTimeW = Now;
	ScheduleHost.NextUpdate = Now;
	ScheduleHost.NextNormal = Now;
	ScheduleHost.NextAI = Now;
	ScheduleHost.NextMove = Now;
	ScheduleHost.LastUpdate = Now;
	ScheduleHost.LastNormal = Now;
	ScheduleHost.LastMove = Now;
	ScheduleHost.LastAI = Now;
	LeaveInterestingPlaceOnRemove();                                     // 102b53d0(curtime)
	++TroikaInitInterestingPlaceReleases;
	LastSpotIndex = 0;                                                   // +0x62fc
	ScheduleHost.Unknown6300 = 0;
	NextCrosswalkUpdateTime = 0.0;                                       // 1029a22a +0x6318
	NextPedInteractTime = 0.0;                                           // 1029a230 +0x631c
	// `1029a236`–`1029a248`: the two `sPatrolPath` smart-pointer pairs — `m_sppPatrolPath`
	// (`+0x658c` byte, `+0x6590` pointer) and `m_sppPatrolPathHunt` (`+0x6594`, `+0x6598`) — are
	// zeroed whole. This runtime carries each pair as the route resolved to its points, so dropping
	// the points IS dropping the pair. The authored route NAME is a keyfield and is not touched.
	PatrolPoints.Reset();                                                // 1029a236 / 1029a23c
	HuntPatrolPoints.Reset();                                            // 1029a242 / 1029a248
	ScheduleHost.Unknown659c = 0;                                        // 1029a24e
	ScheduleHost.bPatrolPathUseHint = false;                             // 1029a254 +0x65a0
	ScheduleHost.GoalToleranceCm = 0.f;
	ScheduleHost.InsideInterruptDistanceSqr = 0.f;
	ScheduleHost.OutsideInterruptDistanceSqr = 0.f;
	ScheduleHost.InterruptTime = 0.0;
	ScheduleHost.WaitFinishedDelta = 0.f;
	ScheduleHost.bWaitFinishedSet = false;
	ScheduleHost.MoveTarget = FElysiumEntityHandle::Invalid();           // 1029a27e +0x6240
	HitBuildupCount = 0;                                                 // 1029a28b +0x6064
	WeaponScareTime = -1.0;                                              // 1029a291 +0x63dc
	SpawnEquipLoadout();                                                 // 1029a29d slot 513
	// `1029a3b8`: `*m_pEnemyStore = this` and `m_pEnemyStore->+0x10 = FreeKnowledgeDuration`. This
	// runtime's enemy store is `FElysiumNpcEnemyMemory`, owned by the NPC (so its back-pointer is
	// implicit) and already carrying that interval, which `Senses.ResolveTuning` below seeds from
	// the same `Rules.txt` key. Read here so the arm is exercised, written by the seeder.
	(void)TuningEnemyStoreInterval();
	if (!PlayerReaction.IsEmpty())
	{
		FElysiumInputArgs Args;
		Args.Param = FElysiumVariant::String(FString::Printf(TEXT("Player %s"), *PlayerReaction));
		InputSetRelationship(Args);                                      // 10273790
	}
	InNpcInit() = false;                                                 // 1029a406 DAT_10937cf1 = 0
	ScheduleHost.bSavePositionWalk = false;                              // 1029a40f +0x63e0
	AlertLevel = 0;                                                      // 1029a415 102b5dc0(this, 0)
	++TroikaInitAlertLevelResets;
	bGoToIdleState = false;                                              // 1029a41a +0x63fc
	bLeaningLeft = false;                                                // 1029a420 +0x63fd
	ScheduleHost.NextCoverLosCheck = 0.0;                                // 1029a426 +0x6400
	ScheduleHost.FailedCoverLosChecks = 0;                               // 1029a42c +0x6404
	ScheduleHost.bForceCoverLosCheck = false;                            // 1029a432 +0x6408
	CowerAnimOffset = 0;                                                 // 1029a438 +0x6414
	Senses.Memory.NextSeeSoundSourceTime = 0.0;                          // 1029a43e +0x6418
	Senses.Memory.NextInvestigateSoundTime = 0.0;                        // 1029a444 +0x623c
	Senses.Memory.SeeUnknownRepeatSightings = 0;                         // 1029a44a +0x60a4
	EnemySightings = 0;                                                  // 1029a450 +0x60a8
	// `+0x641c NextFleeSoundTime` is NOT written here — the listing runs `+0x6418`, `+0x623c`,
	// `+0x60a4`, `+0x60a8` and stops. Only the four species bodies that clear it do.
	if (!StatTemplate.IsEmpty())
	{
		bIsBccTargetable = true;                                         // 1029a4a2, template only
	}
	bNpcIsAlive = true;                                                  // 1029a4a9 +0x1481
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).IgnoreUntil = 0.0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).IgnoreUntil = 0.0;
	Witness.NosferatuIgnoreUntil = 0.0;
	Witness.CriminalWitnessedTime = 0.0;
	Witness.SupernaturalWitnessedTime = 0.0;
	Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Processed = -1;
	Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Processed = -1;
	LastMeleeStepbackTime = 0.0;
	MeleeCanEnterTimer = 0.0;
	MeleeMustLeaveTimer = 0.0;
	bInMelee = false;
	CanSeekCoverTimer = 0.0;
	ScheduleHost.KickPropSearchTimer = 0.0;
	ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();
	ScheduleHost.NextShootAtHintSearchTime = 0.0;
	ScheduleHost.ShootAtHintNode = 0;
	AlternateAi = 0;
	IgnoreCollisionUntil = static_cast<double>(GFltMax);
	Senses.Memory.DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
	Senses.Memory.DetectedAttackTime = 0.0;
	ScheduleHost.ForcedSchedule = EElysiumScheduleId::None;
	Slot593();
	SetAttackExtents(FVector(-1.f, -1.f, -1.f) * ElysiumMove::U);         // source (-1,-1,-1)
	ResetFakeReloadCount();                                              // 102c54c0
	bCameFromSpawner = false;
	StandingOnHeadTimer = 0.f;
	WeaponThroughWallTime = 0.0;
	Senses.Memory.StealthVisionOverrideUntil = 0.0;
	CorpseConditionTime = 0.0;
	bJumping = false;                                                    // 1029a579 +0x6498
	JumpGravity = 1.f;                                                   // 1029a57f +0x64b8
	DisciplineFlags = 0;                                                 // 1029a589 +0x0eb0
	DisciplineFlags2 = 0;                                                // 1029a58f +0x0eb4
	DisciplinePreFlags = 0;                                              // 1029a595 +0x0eb8
	DisciplinePreFlags2 = 0;                                             // 1029a59b +0x0ebc
	SeedStatListOnNpcInit();                                             // 1029a5a1
	bReturnToInitialPos = false;                                         // 1029a684 +0x6494
	Senses.Memory.SeeUnknownGraceUntil = -1.0;                           // 1029a68a +0x6084
	Senses.Memory.SeeUnknownRunTimer = 0.0;                              // 1029a690 +0x609c
	Senses.Memory.SeeUnknownStartTimer = 0.0;                            // 1029a696 +0x60a0
	MeleeHeightDiffTimer = -1.0;                                         // 1029a69e +0x6274
	Senses.ResolveTuning(*this);                                         // 1029a6a4 1028fb70
	// `1029a6a9`: `m_nCurrDisposition = DAT_10924984`. SEAM: that datum has NO writer in the image
	// — its only reader is this body and its three thunks — so the index it holds is not recoverable
	// from the corpus. This runtime's disposition is a NAME, applied by
	// `ApplyDefaultDispositionOnActivate` immediately before this body runs, and is left standing.
	++DispositionSeeds;
	EyeLookTargetHandle = FElysiumEntityHandle::Invalid();               // 1029a6b4 +0x0e64
	RelativeEyeTarget = 0;                                               // 1029a6ba +0x5b94
	if (TeleportMoveTimer > TeleportMoveTimerFloor)                      // 1029a6c1 FCOMP 0.0
	{
		TeleportMoveTimer = static_cast<float>(Now) + TeleportMoveTimer + TeleportMoveTimerExtra;
		// `1029a6f9 CALL 0x10001041` -> `0x102ae7f0`, whose WHOLE body is
		// `*(this + 0x65c8) = param_1`. It writes `m_iForcedSchedule` and installs nothing: the
		// schedule the NPC is running is not touched here.
		ScheduleHost.ForcedSchedule =
			static_cast<EElysiumScheduleId>(TeleportForcedScheduleRetailId);
		return;
	}
	TeleportMoveTimer = 0.f;                                             // 1029a704
}

// =================================================================================================
// Slot 420 dispatcher
// =================================================================================================

namespace
{
	struct FLifecycle19NpcInitArm
	{
		const TCHAR* Address = nullptr;
		void (FElysiumNpc::*Body)() = nullptr;
	};
}

bool FElysiumNpc::SpeciesNPCInit()
{
	static const FLifecycle19NpcInitArm Arms[] =
	{
		{ TEXT("0x101aab90"), &FElysiumNpc::PayphoneNPCInit },
		{ TEXT("0x1035cec0"), &FElysiumNpc::AndreiBloodNPCInit },
		{ TEXT("0x10360ce0"), &FElysiumNpc::AsianVampireNPCInit },
		{ TEXT("0x10363940"), &FElysiumNpc::BachNPCInit },
		{ TEXT("0x103673b0"), &FElysiumNpc::BatSwarmNPCInit },
		{ TEXT("0x103692c0"), &FElysiumNpc::CameraNPCInit },
		{ TEXT("0x1036b050"), &FElysiumNpc::ChangBrosNPCInit },
		{ TEXT("0x1036f100"), &FElysiumNpc::ChangBrosBladeNPCInit },
		{ TEXT("0x1036f900"), &FElysiumNpc::ChangBrosClawNPCInit },
		{ TEXT("0x10372b00"), &FElysiumNpc::CopNPCInit },
		{ TEXT("0x10375c80"), &FElysiumNpc::FrenzyShadowNPCInit },
		{ TEXT("0x103785f0"), &FElysiumNpc::GargoyleNPCInit },
		{ TEXT("0x1037b290"), &FElysiumNpc::GhoulCroucherNPCInit },
		{ TEXT("0x1037e240"), &FElysiumNpc::Guard1NPCInit },
		{ TEXT("0x1037fa70"), &FElysiumNpc::HengeyokaiNPCInit },
		{ TEXT("0x10387140"), &FElysiumNpc::HumanCombatantNPCInit },
		{ TEXT("0x10388b30"), &FElysiumNpc::HunterNPCInit },
		{ TEXT("0x1038b070"), &FElysiumNpc::ManBatNPCInit },
		{ TEXT("0x103a0420"), &FElysiumNpc::NewscasterNPCInit },
		{ TEXT("0x103a2570"), &FElysiumNpc::PedestrianNPCInit },
		{ TEXT("0x103a4350"), &FElysiumNpc::PlaceholderNPCInit },
		{ TEXT("0x103a4580"), &FElysiumNpc::PlayerControllerNPCInit },
		{ TEXT("0x103a6d40"), &FElysiumNpc::SabbatLeaderNPCInit },
		{ TEXT("0x103ae6c0"), &FElysiumNpc::SheriffManNPCInit },
		{ TEXT("0x103b2360"), &FElysiumNpc::SheriffSwarmNPCInit },
		{ TEXT("0x103b35c0"), &FElysiumNpc::TaxiDriverNPCInit },
		{ TEXT("0x103b91d0"), &FElysiumNpc::TzimisceNPCInit },
		{ TEXT("0x103c1c80"), &FElysiumNpc::TzimisceHeadClawNPCInit },
		{ TEXT("0x103c5840"), &FElysiumNpc::VampireBossNPCInit },
		{ TEXT("0x103caef0"), &FElysiumNpc::WerewolfNPCInit },
		{ TEXT("0x103dce00"), &FElysiumNpc::WolfMorphNPCInit },
		{ TEXT("0x103dd800"), &FElysiumNpc::YukieNPCInit },
		{ TEXT("0x103defc0"), &FElysiumNpc::ZombieNPCInit },
	};
	if (SpeciesDispatchingSlot == GLifecycle19Slot420)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GLifecycle19Slot420);
	if (Override == nullptr)
	{
		return false;
	}
	for (const FLifecycle19NpcInitArm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GLifecycle19Slot420);
		(this->*Arm.Body)();
		return true;
	}
	return false;
}

void FElysiumNpc::NPCInit()
{
	if (SpeciesNPCInit())
	{
		return;
	}
	TroikaNPCInit();
}

// =================================================================================================
// `0x10273ad0` — `CAI_BaseNPC::StartNPC`
// =================================================================================================

void FElysiumNpc::BaseStartNPC()
{
	Schedule.bDidMaintainSchedule = false;                               // +0x5bb8
	ScheduleHost.bRanAi = false;                                         // +0x1b4c
	const int32 MoveType = GetMoveType();
	const uint32 Caps = static_cast<uint32>(CapabilitiesGet());
	const bool bSkipDrop = MoveType == 5 || MoveType == 6
		|| (Caps & CapabilityNoFloorDrop) != 0
		|| (static_cast<uint32>(SpawnFlags) & SpawnFlagNoFloorDrop) != 0;
	if (!bSkipDrop)
	{
		FVector OriginUnits = Origin / ElysiumMove::U;
		const bool bHit = MoveProbeFloorDrop(OriginUnits);               // 102e7880
		if (!bHit)
		{
			++FloorDropWarnings;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("NPC %s stuck in wall--level design error"),      // 10273b78 105cc558
				Def != nullptr ? *Def->Classname : TEXT(""));
		}
		Origin = OriginUnits * ElysiumMove::U;                           // 10273b8b slot 62
		++FloorDropPerformed;
	}
	else
	{
		Flags &= ~GFlOnGround;                                           // 10273b97 RemoveFlag(1)
		++FloorDropSkipped;
	}
	if (!Target.IsEmpty())
	{
		FElysiumEntity* Found = World != nullptr ? World->FindByName(Target) : nullptr;
		ScheduleHost.GoalEnt = Found != nullptr ? Found->Handle : FElysiumEntityHandle();
		if (Found == nullptr)
		{
			// `10273bd9`: THREE pushes — the classname AND the target name.
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("ReadyNPC()--%s couldn't find target %s"),        // 105cc528
				Def != nullptr ? *Def->Classname : TEXT(""), *Target);
		}
		else
		{
			SetState(1);                                                 // 1026e340 IDLE
			InstallScheduleRetail(StartNpcGoalEntityRetailId, false);    // 10280de0(3)
		}
	}
	InitSquad();                                                         // slot 545
	const double Now = Lifecycle19Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);
		const float Jitter = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(StartNpcDelayMin, StartNpcDelayMax);
		ArmThinkAt(Now + static_cast<double>(Jitter));
	}
	else
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);                          // BOTH arms ThinkSet
		ArmThinkAt(Now);
	}
	ScriptArrivalActivity = static_cast<int32>(0xffffffff);
	ScriptArrivalSequence.Reset();
	if ((static_cast<uint32>(SpawnFlags) & SpawnFlagPreAimed) != 0)
	{
		SetState(1);
		SetActivity(1);                                                  // slot 310
		InstallScheduleRetail(StartNpcAmbushRetailId, false);            // 0x2d
	}
}

void FElysiumNpc::TroikaStartNPC()
{
	BaseStartNPC();                                                      // 10273ad0
	ThinkSet(StartNpcThinkFunction(), 0.0);                              // re-arm
	SetFollowerBossByAuthoredName(FollowerBossName);                     // 102c44e0
	SetFollowerType(FollowerType);                                       // 102c4680
	Senses.SetClosestPlayer(*this, Lifecycle19Now(*this));               // 10293a80
}

namespace
{
	struct FLifecycle19StartNpcArm
	{
		const TCHAR* Address = nullptr;
		void (FElysiumNpc::*Body)() = nullptr;
	};
}

bool FElysiumNpc::SpeciesStartNPC()
{
	static const FLifecycle19StartNpcArm Arms[] =
	{
		{ TEXT("0x10369930"), &FElysiumNpc::CameraStartNPC },
		{ TEXT("0x103b9270"), &FElysiumNpc::TzimisceStartNPC },
	};
	if (SpeciesDispatchingSlot == GLifecycle19Slot422)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GLifecycle19Slot422);
	if (Override == nullptr)
	{
		return false;
	}
	for (const FLifecycle19StartNpcArm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GLifecycle19Slot422);
		(this->*Arm.Body)();
		return true;
	}
	return false;
}

void FElysiumNpc::StartNPC()
{
	if (SpeciesStartNPC())
	{
		return;
	}
	TroikaStartNPC();
}

void FElysiumNpc::CameraStartNPC()
{
	// `0x10369930` — replacement, not a chain. Drop-to-floor only when the model is `null.mdl`.
	FString ModelName = Model;
	if (ModelName.IsEmpty())
	{
		ModelName = TEXT("");
	}
	if (ModelName.Equals(TEXT("models/null.mdl"), ESearchCase::IgnoreCase))
	{
		// `10369994 CALL [EDX + 0x804]` is slot 513 `CapabilitiesGet`, `TEST AL,0x4` — the same
		// capability bit the base body tests, NOT a solid-flags read.
		const int32 MoveType = GetMoveType();
		const uint32 Caps = static_cast<uint32>(CapabilitiesGet());
		const bool bSkipDrop = MoveType == 5 || MoveType == 6
			|| (Caps & CapabilityNoFloorDrop) != 0
			|| (static_cast<uint32>(SpawnFlags) & SpawnFlagNoFloorDrop) != 0;
		if (!bSkipDrop)
		{
			FVector OriginUnits = Origin / ElysiumMove::U;
			const bool bHit = MoveProbeFloorDrop(OriginUnits);
			if (!bHit)
			{
				++FloorDropWarnings;
				UE_LOG(LogElysiumNpcEnt, Warning,
					TEXT("NPC %s stuck in wall--level design error"),   // 105cc558
					Def != nullptr ? *Def->Classname : TEXT(""));
			}
			Origin = OriginUnits * ElysiumMove::U;
			++FloorDropPerformed;
		}
		else
		{
			Flags &= ~GFlOnGround;
			++FloorDropSkipped;
		}
	}
	if (!Target.IsEmpty())                                               // gated m_target != 0
	{
		FElysiumEntity* Found = World != nullptr ? World->FindByName(Target) : nullptr;
		ScheduleHost.GoalEnt = Found != nullptr ? Found->Handle : FElysiumEntityHandle();
		if (Found == nullptr)
		{
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("ReadyNPC()--%s couldn't find target %s"),        // 10369a58 105cc528
				Def != nullptr ? *Def->Classname : TEXT(""), *Target);
		}
		else
		{
			SetState(1);
			InstallScheduleRetail(StartNpcGoalEntityRetailId, false);
		}
	}
	InitSquad();
	const double Now = Lifecycle19Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);
		const float Jitter = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(StartNpcDelayMin, StartNpcDelayMax);
		ArmThinkAt(Now + static_cast<double>(Jitter));
	}
	else
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);
		ArmThinkAt(Now);
	}
	ScriptArrivalActivity = static_cast<int32>(0xffffffff);
	ScriptArrivalSequence.Reset();
	if ((static_cast<uint32>(SpawnFlags) & SpawnFlagPreAimed) != 0)
	{
		SetState(1);
		SetActivity(1);
		InstallScheduleRetail(StartNpcAmbushRetailId, false);
	}
	ThinkSet(StartNpcThinkFunction(), 0.0);                              // final re-arm, no stamp
}

void FElysiumNpc::TzimisceStartNPC()
{
	TroikaStartNPC();                                                    // 1029a8b0
	ThinkSet(StartNpcThinkFunction(), 0.0);                              // redundant re-arm
	++TzimisceStartNpcRearms;
}

// =================================================================================================
// `0x1027bf50` / `0x102998c0` — OnRestore
// =================================================================================================

void FElysiumNpc::BaseOnRestore(bool /*bFromLoad*/)
{
	bool bGiveUp = true;
	if ((NpcStateRetail() != 4 || ScriptOwnerIsLive())
		&& !LastSavedExtendedHeader.ScheduleName.IsEmpty()
		&& LastSavedExtendedHeader.Version == 1)
	{
		bool bOk = true;
		if ((LastSavedExtendedHeader.Flags & 1u) != 0 && GetEnemy() == nullptr)
		{
			bOk = false;
		}
		if (bOk && (LastSavedExtendedHeader.Flags & 2u) != 0)
		{
			if (Lifecycle19ResolveHandle(*this, TargetEnt) == nullptr)
			{
				bOk = false;
			}
		}
		if (bOk)
		{
			bGiveUp = false;
		}
	}
	if (Schedule.TaskIndex > RestoreTaskIndexCeiling)
	{
		Schedule.TaskIndex = 1;
	}
	if (!bGiveUp)
	{
		EElysiumScheduleId Found = EElysiumScheduleId::None;
		if (ElysiumScheduleIdFromName(LastSavedExtendedHeader.ScheduleName, Found))
		{
			Schedule.Current = Found;                                    // 1027c020 m_pSchedule
		}
		else
		{
			Schedule.Current = EElysiumScheduleId::None;
		}
		if (Schedule.Current != EElysiumScheduleId::None)                // 1027c026
		{
			// `1027c02d`/`1027c048`/`1027c052`: CRC32 over the resolved schedule's task array —
			// `schedule+0x20` for `schedule+0x24 << 3` bytes, eight per task — compared against the
			// checksum the save wrote at `+0x1a3c`. A schedule whose task list has CHANGED since
			// the save is dropped (`1027c068`), which then takes the give-up arm below. This is the
			// same checksum `SaveWriteFields` already computes through `ScheduleTaskBytes`.
			TArray<uint8> TaskBytes;
			ScheduleTaskBytes(TaskBytes);
			uint32 Crc = SaveCrc32Init();
			Crc = SaveCrc32Update(Crc, TaskBytes.GetData(), TaskBytes.Num());
			if (SaveCrc32Final(Crc) != LastSavedExtendedHeader.ScheduleCrc)   // 1027c064
			{
				Schedule.Current = EElysiumScheduleId::None;             // 1027c068
			}
		}
	}
	if (Schedule.Current == EElysiumScheduleId::None || bGiveUp)
	{
		ScheduleHost.bDoPostRestoreRefindPath = false;
		RestoreGiveUp();
	}
	else
	{
		ScheduleHost.bDoPostRestoreRefindPath =
			((LastSavedExtendedHeader.Flags >> 2) & 1u) != 0;
	}
	// CBaseCombatCharacter::OnRestore `0x10323b60` — SEAM, no port body.
	// +0x5b90 m_pSurfaceData ABSENT.
	if (!ScheduleHost.bDoPostRestoreRefindPath)
	{
		if (Motor != nullptr)
		{
			Motor->ClearNavigationGoal();
		}
		++NavigationGoalClears;
	}
	else if (!RefindPostRestorePath())
	{
		RestoreGiveUp();
	}
}

void FElysiumNpc::TroikaOnRestore(bool bFromLoad)
{
	// `102998cx`: `0x1029f610` validates a stored route against the node network (`0x10307ac0`) and
	// `0x1029f5d0` releases it when that fails — the two pairs `m_sppPatrolPath` and
	// `m_sppPatrolPathHunt`, each checked and released on its own. SEAM: no node network stands
	// here, so a stored route cannot validate and is released, which is retail's answer for a path
	// the network no longer carries.
	++PatrolPathRevalidations;
	if (PatrolPoints.Num() > 0)
	{
		PatrolPoints.Reset();                                            // 1029f5d0(&m_sppPatrolPath)
		++PatrolPathReleases;
	}
	++PatrolPathRevalidations;
	if (HuntPatrolPoints.Num() > 0)
	{
		HuntPatrolPoints.Reset();                                        // 1029f5d0(&…PathHunt)
		++PatrolPathReleases;
	}
	BaseOnRestore(bFromLoad);                                            // 1027bf50
	++RestorePlaceScans;
	CurrentSpotIndex = FindInterestingPlaceHoldingMe();                  // 102db5e0 -> +0x62ec
	if (RestorePedLinkNode != -1 && RestorePedLinkDestNode != -1)
	{
		++NodeIndexErrorCount();                                         // always OOB: no node array
	}
	const double Now = Lifecycle19Now(*this);
	ScheduleHost.ShootAtHintNode = 0;
	const float Jitter = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.FRandRange(ShootAtHintRearmMin, ShootAtHintRearmMax);
	ScheduleHost.NextShootAtHintSearchTime = Now + static_cast<double>(Jitter);
	SetFollowerBossByAuthoredName(FollowerBossName);                     // 102c44e0
	SetFollowerType(FollowerType);                                       // 102c4680
	CombatStartActivityId = ResolveCombatStartActivity(CombatStartActivity);  // 1029f340 -> +0x65e4
	bSpawnCalled = true;                                                 // 10299xxx +0x62e9 = 1
	ValidateRestoredInterestingPlace();                                  // 10299a80
	if (bHidden)                                                         // 100b5190 +0x0f4
	{
		ThinkSet(nullptr, 0.0);
		NextThink = NeverThinkSentinel;
	}
	Slot593();
}

namespace
{
	struct FLifecycle19OnRestoreArm
	{
		const TCHAR* Address = nullptr;
		void (FElysiumNpc::*Body)(bool) = nullptr;
	};
}

bool FElysiumNpc::SpeciesOnRestore(bool bFromLoad)
{
	static const FLifecycle19OnRestoreArm Arms[] =
	{
		{ TEXT("0x1039f000"), &FElysiumNpc::MingXiaoTentacleOnRestore },
		{ TEXT("0x103a25a0"), &FElysiumNpc::PedestrianOnRestore },
		{ TEXT("0x103c3c40"), &FElysiumNpc::TzimisceRunnerOnRestore },
		{ TEXT("0x103cabf0"), &FElysiumNpc::WerewolfOnRestore },
	};
	if (SpeciesDispatchingSlot == GLifecycle19Slot130)
	{
		return false;
	}
	const FElysiumNpcClassSlot* Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), GLifecycle19Slot130);
	if (Override == nullptr)
	{
		return false;
	}
	for (const FLifecycle19OnRestoreArm& Arm : Arms)
	{
		if (FCString::Strcmp(Arm.Address, Override->Address) != 0)
		{
			continue;
		}
		const FSpeciesDispatchScope Scope(*this, GLifecycle19Slot130);
		(this->*Arm.Body)(bFromLoad);
		return true;
	}
	return false;
}

void FElysiumNpc::OnRestore(bool bFromLoad)
{
	if (SpeciesOnRestore(bFromLoad))
	{
		return;
	}
	TroikaOnRestore(bFromLoad);
}

void FElysiumNpc::MingXiaoTentacleOnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	MingXiaoTentacleCache() = FElysiumEntityHandle::Invalid();            // DAT_1093bd34 = -1
}

void FElysiumNpc::PedestrianOnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	if (!bFromLoad)
	{
		return;
	}
	if (PedestrianLevelResetType == 2)
	{
		return;
	}
	if (!IsAlive() && PedestrianLevelResetType != 0)
	{
		return;
	}
	if (bHidden)                                                         // 100b5190
	{
		return;
	}
	++InventoryDestroys;
	if (!IsAlive())
	{
		// `0x101cf390` with pre-death mins/maxs at +0x6660/+0x666c. No collision OBB
		// member on this leaf; the two vectors are the restored words.
		(void)PedestrianPreDeathMinsUnits;
		(void)PedestrianPreDeathMaxsUnits;
	}
	NPCInit();                                                           // slot 420
	ClearFollowerBossName();                                             // 102c4430 ""
	Origin = InitialPosition;                                            // 101cf5c0 +0x62a8
	Angles = InitialAngles;                                              // 103a264e slot 64 +0x62b4
	// `103a26b4`–`103a27ff`: the collision block. `SetSolidFlags(0)`, then two `AddSolidFlags` of
	// the CURRENT 16-bit word (`+0x2b4`) ORed with `1` and then `0x40`, then `SetSolid(SOLID_BBOX)`.
	// SEAM: family Motor10's solid record is what this substrate has for a collision property, and
	// it reproduces retail's read-OR-pass-back-and-OR-again shape.
	RetailSolidFlags = 0;                                                // 103a26b4
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | 1u;               // 103a2726
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | 0x40u;            // 103a2798
	RetailSolidType = 2;                                                 // 103a27ff SOLID_BBOX
	++RetailSolidSets;
	SetMoveType(4, 0);                                                   // 103a2816 slot 93
	SetHullSizeNormal(false);                                            // 103a2820 10273070
	++RestoreRelinkCalls;                                                // 103a2826 CBaseEntity::Relink
	CreateVPhysics();                                                    // 103a2832 slot 223
}

void FElysiumNpc::TzimisceRunnerOnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	SetAttackExtents(FVector(RunnerAttackExtentX, RunnerAttackExtentY, RunnerAttackExtentZ)
		* ElysiumMove::U);
	++Flag2Removals;                                                     // RemoveFlag2(4)
}

void FElysiumNpc::WerewolfOnRestore(bool bFromLoad)
{
	TroikaOnRestore(bFromLoad);
	WerewolfRearm();                                                     // 103cac20
}
