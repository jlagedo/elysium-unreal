// `CAI_BaseNPC`'s bodies of the `Senses10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSenses10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSenses10Shared.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	constexpr float GHalf = 0.5f;
	constexpr float GOne = 1.0f;
}

// --- Moved from `ElysiumNpcSenses10.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::IRelationPriorityOf(const FElysiumEntity* Candidate) const
{
	// `IRelationPriority` (`0x10333700`) — the raw integer, with `FElysiumRelationships`' own
	// recovered defaults (5 for a live actor with no row, 0 for a null target).
	//
	// **This one does NOT become a forward, and the reason is a slot boundary rather than a gap.**
	// Story 29d's family Conditions10 owns slot **404**, not slot 405: `0x10333700` is a LAYER 0 row
	// of story 29c's band whose overlay target is still the generated stub, so forwarding would tell
	// every caller that every entity has priority 0 — which is the same error the `.inl` above
	// records for slot 404 before it landed. The store this reads IS what `0x10333700` reads.
	if (Candidate == nullptr)
	{
		return 0;
	}
	const FString Classname = Candidate->Def ? Candidate->Def->Classname : FString();
	return Relationships.ResolvePriority(Candidate->Handle, Classname);
}

bool FElysiumNpcBase::InnateWeaponLosTrace(const FVector& StartCm, const FVector& EndCm,
	FElysiumEntity*& OutBlocker) const
{
	OutBlocker = nullptr;
	// SEAM: mask `0x46004003` with the self filter `0x101d3190(this, 0)`. Family Motor's hull trace
	// is the nearest seam this substrate has and it reports no hit, which is a clear trace.
	FKernelHullTrace Trace;
	const FVector StartUnits = StartCm / ElysiumMove::U;
	const FVector EndUnits = EndCm / ElysiumMove::U;
	if (KernelHullTrace(StartUnits, EndUnits, FVector::ZeroVector, FVector::ZeroVector,
		0x46004003, Trace))
	{
		if (World != nullptr && Trace.HitEntity.IsSet())
		{
			OutBlocker = World->Resolve(Trace.HitEntity);
		}
		return Trace.Fraction >= 1.f;
	}
	return true;
}

void FElysiumNpcBase::BaseOnLooked()
{
	// `CAI_BaseNPC::OnLooked` (`0x1026a2c0`). The body is `ElysiumNpcCond::GatherSight`, which
	// carries every arm in retail's order: the six-entry `ClearCondition` table at `0x105c979c`, the
	// skip entity resolved off `m_bfAINPCFlags2 & 0x400000`, `SEE_PLAYER` with the `0x1017ff40`
	// per-relation stamp, the `relation != D_NU` gate, `SEE_ENEMY` for the committed enemy, the
	// D_CALM divert and the three `IRelationPriority` thresholds, and the slot-544
	// `UpdateEnemyMemory` write.
	//
	// The classifier reads the sense pass's memory, which the senses runner on the Troika holds
	// (transitional, fold 9): it is reached through `m_pBaseNPCTroika` (`AsNpc()`), and a base-only
	// NPC, which has no sense pass yet, classifies nothing.
	FElysiumNpc* const Troika = AsNpc();
	if (World == nullptr || Troika == nullptr)
	{
		return;
	}
	ElysiumNpcCond::GatherSight(*Troika, World->NowSeconds(), Cognition.Conditions);
}

bool FElysiumNpcBase::BestEnemyCandidateVisible(FElysiumEntity* Candidate)
{
	// `10274585` / `10274686`: `CAI_Senses::DidSeeEntity(m_pSenses, cand)` (`0x1030fb10`) — THIS
	// Look pass's accepted set — OR slot 201 `FVisible(cand, 0x2804091, 0, 0)`.
	if (Candidate == nullptr)
	{
		return false;
	}
	const FElysiumNpcSenses* const SensesPtr = SensesObject();
	if (SensesPtr != nullptr && SensesPtr->Sighted().Contains(Candidate->Handle))
	{
		return true;
	}
	return FVisible(Candidate, 0x2804091, nullptr, 0);
}

int32 FElysiumNpcBase::BestEnemyDistanceKey(const FElysiumEntity& Candidate) const
{
	// `1027452b`..`10274552`: the three slot-217 deltas, squared and summed, then `__ftol`
	// (`0x10431320`, 39 bytes, no callees — there is NO root). SOURCE units squared.
	const double UnitsSquared = FVector::DistSquared(Origin, Candidate.Origin)
		/ (static_cast<double>(ElysiumMove::U) * static_cast<double>(ElysiumMove::U));
	return static_cast<int32>(FMath::TruncToInt64(UnitsSquared));
}

FElysiumEntity* FElysiumNpcBase::BestEnemy()
{
	// `0x102743c0`, slot 478's base body. The one species arm, `CNPC_VFrenzyShadow#478`
	// (`0x103766d0`), replaces it as `FElysiumNpcFrenzyShadow::BestEnemy` (fold A2).
	if (World == nullptr)
	{
		return nullptr;
	}
	// `102743c0`: the four incumbent words, at retail's seeds.
	FBestEnemyState State;

	// `102743eb`: `GetEnemies()->+0xc` is the memory list head; `+0x38` is the next link and `+0x24`
	// the record's handle. An empty list answers null immediately.
	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		FElysiumEntity* Candidate = World->Resolve(Record.Handle);
		// `1027440e`: an unresolvable or null handle is skipped.
		if (Candidate == nullptr)
		{
			continue;
		}
		// `10274442`: `GetFlags()` bit `0x8000` — `FL_NOTARGET`, read as the SIGN of `(flags >> 8)`
		// (`TEST AH,AH / JS`). THE FIRST GATE THE PORT WAS MISSING. `FElysiumEntity` carries no
		// `FL_NOTARGET` word, so this reads false — retail's own "targetable" answer, the admitting
		// one, and the retail flag it stands for is named here.
		if (HasNoTargetFlag(*Candidate))
		{
			continue;
		}
		// `10274451`: an entity whose `+0x9c` combat character is live but whose `m_bIsBCCTargetable`
		// (`+0x1480`) is CLEAR is rejected; an entity with NO combat character passes this gate.
		// THE SECOND GATE THE PORT WAS MISSING. `m_bIsBCCTargetable` has no port word and no
		// recovered clearer, so it reads true — again the admitting arm.
		const FElysiumCombatCharacter* Character = Candidate->AsCombatCharacter();
		if (Character != nullptr && !IsBccTargetable(*Candidate))
		{
			continue;
		}
		// `10274469`: never self.
		if (Candidate->Handle == Handle)
		{
			continue;
		}
		// `10274475`: slot 158 `IsAlive` on the CANDIDATE.
		if (Candidate->IsInert())
		{
			continue;
		}
		// `10274483`: slot 404 `IRelationType`, D_HT or D_FR alone. Retail dispatches it TWICE when
		// the first answer is not 1; the query is pure, so one call is the same observation.
		const int32 Relation = IRelationType(Candidate);
		if (Relation != NpcKernelSenses10Shared::GD_HT && Relation != NpcKernelSenses10Shared::GD_FR)
		{
			continue;
		}
		// `102744a7`: `HasEludedMe` (`0x102e0210`) on the same `GetEnemies()` list.
		if (EnemyMemory.IsEluded(Candidate->Handle))
		{
			continue;
		}
		// `102744c1`: slot 530 `IsUnreachable`.
		const bool bCandidateUnreachable = IsUnreachable(Candidate);

		if (!State.bUnreachable)
		{
			// `102744de`: the incumbent is REACHABLE. An unreachable candidate is dropped outright —
			// it can never displace a reachable incumbent, whatever its priority or distance.
			if (bCandidateUnreachable)
			{
				continue;
			}
		}
		else if (!bCandidateUnreachable)
		{
			// `1027456a`: the incumbent is unreachable (or is the seed) and the candidate is
			// reachable — the OUTRIGHT WIN, with no priority and no distance comparison at all.
			// `10274572`: but slot 479 `IsValidEnemy` must pass first. THE THIRD GATE THE PORT WAS
			// MISSING, and retail requires it here as well as on the replacement paths.
			if (!IsValidEnemy(Candidate))
			{
				continue;
			}
			State.bVisible = BestEnemyCandidateVisible(Candidate);   // 10274585
			State.Priority = IRelationPriorityOf(Candidate);         // 102745b6
			State.Distance = BestEnemyDistanceKey(*Candidate);       // 102745c9
			State.bUnreachable = false;                              // 1027460b
			State.Best = Candidate;                                  // 10274708
			continue;
		}

		// `102744e6`: the two matching reachability classes fall here.
		const int32 Priority = IRelationPriorityOf(Candidate);
		if (Priority > State.Priority)
		{
			// `102744fe`: slot 479 again. A candidate that fails it is DROPPED, not demoted to the
			// equal-priority comparison.
			if (!IsValidEnemy(Candidate))
			{
				continue;
			}
			State.Priority = Priority;                          // 10274517
			State.Distance = BestEnemyDistanceKey(*Candidate);  // 1027455d
			State.bUnreachable = bCandidateUnreachable;         // 10274561
			State.Best = Candidate;                             // 10274708
			// `bVisible` is deliberately NOT touched: the next equal-priority candidate observes the
			// PREVIOUS incumbent's visibility byte. A retail selection quirk, kept.
			continue;
		}
		// `10274615`: only an EQUAL priority carries on; a lower one is dropped.
		if (Priority != State.Priority)
		{
			continue;
		}
		const int32 Distance = BestEnemyDistanceKey(*Candidate);    // 10274627
		const bool bCloser = Distance < State.Distance;             // 1027466b SETL
		// `1027467a`: not closer AND the incumbent is visible — dropped before the candidate's own
		// visibility is even computed.
		if (!bCloser && State.bVisible)
		{
			continue;
		}
		const bool bCandidateVisible = BestEnemyCandidateVisible(Candidate);   // 10274686
		// `102746b4`: closer replaces when the candidate is visible OR the incumbent is not;
		// `102746ca`: farther replaces only when the incumbent is unseen AND the candidate is seen.
		const bool bDisplaces = bCloser
			? (bCandidateVisible || !State.bVisible)
			: (!State.bVisible && bCandidateVisible);
		if (!bDisplaces)
		{
			continue;
		}
		// `102746d6`: slot 479 once more.
		if (!IsValidEnemy(Candidate))
		{
			continue;
		}
		State.Distance = Distance;                          // 102746eb
		State.bVisible = bCandidateVisible;                 // 102746f2
		State.Priority = IRelationPriorityOf(Candidate);    // 102746f6
		State.bUnreachable = bCandidateUnreachable;         // 10274704
		State.Best = Candidate;                             // 10274708
	}
	return State.Best;
}

uint32 FElysiumNpcBase::SquadWord() const
{
	// `+0x5da4`. SEAM: no squad object stands here, so the word is retail's own "no squad" value.
	return 0;
}

bool FElysiumNpcBase::UpdateCaiMemory(FElysiumEntity* Enemy, const FVector& PositionCm)
{
	// `CAI_Memory::UpdateMemory` (`0x102df700`) with the node array at `m_pNavigator+0x2c`, the
	// enemy, the position and the enemy's velocity. The node array is the AI network and does not
	// exist here, so the record's two node ids stay `INDEX_NONE`.
	const bool bFirstRecord = Enemy != nullptr && EnemyMemory.Find(Enemy->Handle) == nullptr;
	const double Now = NpcKernelSenses10Shared::NowOf(*this);
	if (Enemy == nullptr)
	{
		EnemyMemory.UpdatePositionOnly(PositionCm, Now);
		return false;
	}
	EnemyMemory.UpdateAtPosition(*this, Enemy->Handle, PositionCm, Now);
	return bFirstRecord;
}

bool FElysiumNpcBase::UpdateEnemyMemory(FElysiumEntity* Enemy, const FVector& PositionCm,
	FElysiumEntity* /*Informer*/)
{
	// `102709c0`: `GetEnemies()` null answers TRUE at once and writes nothing. This runtime's store
	// is a member and is never null; the arm is named so the recovered answer is on record.
	if (GetEnemies() == nullptr)
	{
		return true;
	}
	if (Enemy != nullptr)
	{
		// `102709df`: the SQUADMATE gate. **CORRECTION to the pack-01 row, confirmed here**: the
		// enemy's NPC sub-object is `param_1[0x25]` = `+0x94`, not `+0xa8`; `m_iSquadDisconnected`
		// is `+0x5bb0`; and `+0x5d34` is `m_pNavigator`, not a squad word.
		const FElysiumNpcBase* EnemyNpc = Enemy->AsNpcBase();   // `param_1->field_0x94`
		if (EnemyNpc != nullptr && SquadDisconnected < 1 && SquadWord() != 0)
		{
			// `102709fd`: the enemy's squad word is `0` when its own `m_iSquadDisconnected` is above
			// zero, and its `+0x5da4` otherwise.
			const uint32 EnemySquad = EnemyNpc->SquadDisconnected > 0 ? 0u : EnemyNpc->SquadWord();
			// `10270a15`: equal squads AND the enemy still connected answers FALSE — squadmates
			// never enter each other's memory.
			if (EnemySquad == SquadWord() && EnemyNpc->SquadDisconnected < 1)
			{
				return false;
			}
		}
		// `10270a2b`: `IsEluded` (`0x102e0210`) on the same list fires slot 494 `FoundEnemySound`.
		if (EnemyMemory.IsEluded(Enemy->Handle))
		{
			FoundEnemySound();
		}
	}
	// `10270a55`: the unconditional forward, whose answer is this slot's answer.
	return UpdateCaiMemory(Enemy, PositionCm);
}

void FElysiumNpcBase::CreateSecondaryDiscParticles()
{
	++SecondaryDiscParticleBursts;
}

bool FElysiumNpcBase::Event_Gibbed()
{
	// `102658f0`: slot 394 `CorpseGib` FIRST, and its answer is this body's answer on EVERY path.
	const bool bGibbed = CorpseGib();
	if (!bGibbed)
	{
		// `10265904`: slot 395 `CorpseFade` and `return 0` — note the return is the literal `0`,
		// which here equals the first gate's own answer.
		CorpseFade();
		return false;
	}
	// `10265915`: slot 398 `HasExplosiveGibs` decides the rest.
	if (!HasExplosiveGibs())
	{
		// `10265922`: the remove arm is the ONE path that does not run `CorpseFade`.
		UtilRemoveSelf();
		return bGibbed;
	}
	// `1026593a`: the disc particles are tied to the SECOND gate alone.
	CreateSecondaryDiscParticles();
	ScriptHide();    // slot 77, vtable +0x134
	CorpseFade();    // slot 395
	return bGibbed;
}

float FElysiumNpcBase::ModelMassKeyvalue() const
{
	return 0.f;   // SEAM: no studio header on this substrate; retail's at-or-below-zero arm.
}

bool FElysiumNpcBase::IsFemaleBody() const
{
	// The sheet's `Gender` slot, which is the same authored word retail's shadow-mass arm reads.
	return !Sheet.IsMale();
}

void FElysiumNpcBase::BuildVPhysicsShadow()
{
	// `0x10272f40`. Arm 1: slot 94 `GetMoveType()` answering `7` refuses outright.
	if (GetMoveType() == 7)
	{
		return;
	}
	// `VPhysicsInitShadow(true, false, NULL)` after destroying any existing object.
	VPhysicsShadow.bBuilt = true;
	const float AuthoredMass = ModelMassKeyvalue();
	VPhysicsShadow.MassKg = AuthoredMass > 0.f ? AuthoredMass : (IsFemaleBody() ? 65.f : 90.f);
	// The damping: the summed hull extents times `_DAT_10449270` (0.5), squared.
	const FVector Extents = HullMaxsUnits(false) - HullMinsUnits(false);
	const float Summed = (Extents.X + Extents.Y + Extents.Z) * GHalf;
	VPhysicsShadow.Damping = Summed * Summed;
	bHasPhysicsObject = true;
}

bool FElysiumNpcBase::CreateVPhysics()
{
	// `10273720`: BOTH slot 158 `IsAlive` and a null physics object are required before the shadow
	// is built, and the slot answers TRUE unconditionally — including when nothing was created.
	if (IsAlive() && !bHasPhysicsObject)
	{
		BuildVPhysicsShadow();
	}
	return true;
}

void FElysiumNpcBase::AimGun()
{
	// `1026b4f0`: the whole body is gated on slot 167 `GetEnemy()` being non-null, and does nothing
	// without an enemy. No member is written here; every write happens inside the slots it calls.
	if (GetEnemy() == nullptr)
	{
		return;
	}
	const FVector OriginCm = Origin;                                    // slot 217 +0x364
	const FVector ShootPositionCm = Weapon_ShootPosition(OriginCm);     // slot 389 +0x614
	// slot 574 +0x8f8, with BOTH trailing arguments 0.
	const FVector Direction = GetShootEnemyDir(ShootPositionCm, 0, 0);
	SetAim(Direction);                                                  // slot 539 +0x86c
}

FVector FElysiumNpcBase::ShootEnemyAimPoint(const FVector& ShootPositionCm)
{
	// `0x10278650`, arm 1 (`10278654`): `m_hShootTargetOverride` (`+0x5ba8`) live answers that
	// entity's slot-217 origin, whole.
	if (World != nullptr && ShootTargetOverride.IsSet())
	{
		if (const FElysiumEntity* Override = World->Resolve(ShootTargetOverride))
		{
			return Override->Origin;
		}
	}
	FElysiumEntity* Enemy = GetEnemy();
	if (Enemy == nullptr)
	{
		// `102786af`: no enemy — the body's own forward through `AngleVectors` (`0x10139610`) off
		// slot `+0x374`. SEAM: this runtime's `+0x374` accessor is the entity's angles and the
		// vector build is family Geometry's; the aim point is this body's own eye position, which
		// is where retail's degenerate arm lands.
		return EyePosition();
	}
	// `102786e2`: the enemy-memory LKP (`0x102dfed0`) plus the enemy's `BodyTarget(shootPos)` minus
	// its slot-217 origin.
	FVector LastKnownCm = Enemy->Origin;
	if (const FElysiumNpcEnemyMemoryRecord* Record = EnemyMemory.Find(Enemy->Handle))
	{
		LastKnownCm = Record->LastPosition;
	}
	const FVector BodyTargetCm = Enemy->EyePosition();
	// `1027879f`: `+_DAT_104994e0` (-30.0, `ElysiumNpcTunables::EnemyAimPointZOffset`) is added to
	// Z when the enemy's stat `0x0b` reads `5`. SEAM: the `CVStatList_t` join by retail list TYPE
	// does not exist on this sheet, so the stat reads not-5 and the offset is not applied.
	(void)ShootPositionCm;
	return LastKnownCm + (BodyTargetCm - Enemy->Origin);
}

FVector FElysiumNpcBase::GetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B)
{
	// `10278918`: the aim point, then the caller's shoot position subtracted.
	const FVector Delta = ShootEnemyAimPoint(ShootPositionCm) - ShootPositionCm;
	// `10278959`: `VectorNormalize` IN PLACE, and the three floats stored out afterwards. The
	// decompiled C shows the raw delta because it misses the `POP ESI` at `10278969` that shifts the
	// stack reads; the listing stores `[ESP+0x4]`, `[ESP+0x4]` and `[ESP+0x8]` AFTER the pop, which
	// are the normalised components. So the slot answers a UNIT direction.
	return Delta.GetSafeNormal();
}

bool FElysiumNpcBase::WeaponLOSCondition(const FVector& OwnerPosCm, const FVector& TargetPosCm,
	bool bSetConditions)
{
	bool bAnswer = false;
	// `1026fbf1`: `GetActiveWeapon()`.
	if (Inventory.ActiveWeapon.IsSet())
	{
		// `1026fc76`: the weapon's own slot `+0x5b0`. SEAM: this runtime stands no
		// `CBaseCombatWeapon` vtable; the weapon's answer is ADMITTING (`true`), so the 0.92 test
		// below is what a weapon-carrying body is decided by.
		bAnswer = true;
	}
	else
	{
		// `1026fc00`: capabilities (slot 513) without bit `0x20000` answer 0, and only when
		// `bSetConditions` do they touch the ConVar object and raise COND `0x42`.
		const int32 Capabilities = CapabilitiesGet();
		if ((Capabilities & 0x20000) == 0)
		{
			if (bSetConditions)
			{
				Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x42));
			}
			bAnswer = false;
		}
		else
		{
			// `1026fc3c`: slot 573 `InnateWeaponLOSCondition`.
			bAnswer = InnateWeaponLOSCondition(OwnerPosCm, TargetPosCm, bSetConditions);
		}
	}
	// `1026fc8b`: capabilities are re-read, and bit `0x10000000` adds the friendly-fire test — which
	// OVERRIDES a weapon that said yes.
	if ((CapabilitiesGet() & 0x10000000) != 0)
	{
		if (PlayerInLineOfFire(OwnerPosCm, TargetPosCm))
		{
			if (bSetConditions)
			{
				Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(100));
			}
			return false;
		}
	}
	return bAnswer;
}

bool FElysiumNpcBase::InnateWeaponLOSCondition(const FVector& OwnerPosCm, const FVector& TargetPosCm,
	bool bSetConditions)
{
	// `1026fd01`: the ray starts at the caller's position plus `m_vecViewOffset` (`+0x0184`) and
	// ends at the caller's target; `0x1004f7a0` builds it; the trace uses mask `0x46004003` with the
	// self filter `0x101d3190(this, 0)`.
	const FVector StartCm = OwnerPosCm + (EyePosition() - Origin);
	FElysiumEntity* Blocker = nullptr;
	const bool bClear = InnateWeaponLosTrace(StartCm, TargetPosCm, Blocker);

	// `1026fdbd`: `fraction == _DAT_10449280` (a DOUBLE 1.0) answers TRUE — nothing in the way.
	if (bClear)
	{
		return true;
	}
	// `1026fddb`: the hit entity being slot 167 `GetEnemy()` answers TRUE.
	if (Blocker != nullptr && Blocker == GetEnemy())
	{
		return true;
	}
	// `1026fdfa`: a hit entity that is null, or whose `+0x9c` combat character is null.
	// **CORRECTION to the checklist's walk**: the walk calls `+0x9c` "the `+0x27` word", and it
	// reports both condition raises as gated on "trace bool bytes". The listing reads
	// `[ESP+0xb8]` at `1026fe25` and `1026fe51`, which — with `SUB ESP,0xa4` and two pushes — is the
	// THIRD ARGUMENT, `bSetConditions`. There is no trace byte in either arm.
	if (Blocker == nullptr || Blocker->AsCombatCharacter() == nullptr)
	{
		if (bSetConditions)
		{
			Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x66));
			// `1026fe73`: `0x10270aa0(this, blocker)` records the blocker into `+0x5d90`
			// `m_hEnemyOccluder`.
			BaseMemory.EnemyOccluder = Blocker != nullptr
				? Blocker->Handle : FElysiumEntityHandle::Invalid();
		}
		return false;
	}
	// `1026fe08`: slot 404 `IRelationType` equal to D_HT answers TRUE — shooting a hated blocker is
	// fine. Note `1026fe19`'s `MOV AL,AL`: the answer is the relation's own low byte, which is 1.
	if (IRelationType(Blocker) == NpcKernelSenses10Shared::GD_HT)
	{
		return true;
	}
	// `1026fe25`: otherwise COND `0x63` under `bSetConditions`, and false either way.
	if (bSetConditions)
	{
		Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x63));
	}
	return false;
}

void FElysiumNpcBase::StartTaskOverlay()
{
	// `10288710`: gated entirely on slot 529 `IsCurTaskContinuousMove` — a false answer leaves the
	// overlay untouched.
	if (!IsCurTaskContinuousMove())
	{
		return;
	}
	// `10288721`: slot 575 `ShouldMoveAndShoot` false disables the overlay and returns.
	if (!ShouldMoveAndShoot())
	{
		DisableMoveAndShootOverlay();
		return;
	}
	// `1028873e`: slot 419 `UpdateBurstShootPause` runs FIRST, and only then the arm.
	UpdateBurstShootPause();
	ArmMoveAndShootOverlay(BurstShootPauseMin, BurstShootPauseMax);
}

FVector FElysiumNpcBase::HullMinsUnits(bool bSmall) const
{
	// `CAI_Navigator`'s hull table: `0x102d6100` normal mins against `0x102d6140` small mins.
	// `bSmall` picks the TABLE, on this NPC's own hull — it is not a different hull id. The
	// earlier reading passed hull 1 for "small", which answers correctly for a human only by
	// coincidence (HUMAN_PATHING_HULL's full box is the same (-8,-8,0)..(8,8,72) as HUMAN_HULL's
	// small one) and wrongly for every other species. The fallback to the entity's own collision
	// bounds stays for a hull id the table does not carry.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	const EElysiumHullExtents Which =
		bSmall ? EElysiumHullExtents::Small : EElysiumHullExtents::Full;
	if (RetailHullExtents(HullKind, Which, MinsUnits, MaxsUnits))
	{
		return MinsUnits;
	}
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	return MinsUnits;
}

FVector FElysiumNpcBase::HullMaxsUnits(bool bSmall) const
{
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	const EElysiumHullExtents Which =           // 0x102d6120 / 0x102d6160
		bSmall ? EElysiumHullExtents::Small : EElysiumHullExtents::Full;
	if (RetailHullExtents(HullKind, Which, MinsUnits, MaxsUnits))
	{
		return MaxsUnits;
	}
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	return MaxsUnits;
}

void FElysiumNpcBase::RestoreNormalHull()
{
	// `0x10273070`: restore the normal hull from the navigator, clear `+0x5f2d`, and re-run
	// `0x10272f40` when `+0x36c` stands.
	bIsUsingSmallHull = false;
	if (bHasPhysicsObject)
	{
		BuildVPhysicsShadow();
	}
}

void FElysiumNpcBase::HeadProbe()
{
	// `1026ab5a`: the WHOLE body runs only when the shrunk-hull latch `+0x5f2d` AND `+0x5f2c` are
	// both set. Neither has a port producer — the hull swap they record is `CAI_Navigator`'s — so
	// today this is always false and the probe does nothing, which is retail's own answer for a body
	// whose hull was never shrunk.
	if (!bIsUsingSmallHull || !bWantsLargeHull)
	{
		return;
	}
	// `1026ab72`: the box is centred on `GetAbsOrigin` raised by `_DAT_104454c0` (1.0), the hull
	// extents are halved by `_DAT_104454d0` (0.5) and scaled by `_DAT_104492dc` (-1.0), and the two
	// box flag bytes come from the zero-delta test against `_DAT_104454c4` (0.0) and the
	// `_DAT_104492e0` extent tests (the double 1e-6, `ElysiumNpcTunables::HeadProbeExtentEpsilon`;
	// the flag bytes feed only the hull sweep, which is a seam here).
	const FVector MinsUnits = HullMinsUnits(false);
	const FVector MaxsUnits = HullMaxsUnits(false);
	const FVector HalfExtentsUnits = (MaxsUnits - MinsUnits) * GHalf;
	const FVector CentreUnits = (MinsUnits + MaxsUnits) * GHalf;
	const FVector ProbeCentreCm = Origin
		+ FVector(0.0, 0.0, static_cast<double>(GOne) * ElysiumMove::U)
		+ CentreUnits * ElysiumMove::U;
	(void)HalfExtentsUnits;
	// `1026ac66`: the trace uses mask `0x200400b` with the self filter `0x101d3190(this, 0)`, and is
	// optionally drawn under a cvar. SEAM: this runtime has no hull sweep; a clean trace is the
	// admitting answer and is what the restore below reads.
	FElysiumEntity* Blocker = nullptr;
	const bool bClean = InnateWeaponLosTrace(Origin, ProbeCentreCm, Blocker);
	// `1026ac9c`: a clean trace (start-solid byte clear AND fraction equal to `_DAT_10449280` = 1.0)
	// calls `0x10273070`.
	if (bClean)
	{
		RestoreNormalHull();
	}
}

// --- Moved from `ElysiumNpcSenses10.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::HasNoTargetFlag(const FElysiumEntity& /*Candidate*/)
{
	return false;  // SEAM: `GetFlags() & 0x8000` (FL_NOTARGET) — the admitting arm.
}

void FElysiumNpcBase::UtilRemoveSelf()
{
	++UtilRemoveCalls;
	// `UTIL_Remove(this)` (`0x101cd940`). This substrate's removal is the world's, and a body that
	// removes itself from inside a slot would invalidate the caller's `this`; the call is recorded
	// and the entity is marked dead, which is the observable half.
	bDead = true;
}

bool FElysiumNpcBase::PlayerInLineOfFire(const FVector& OwnerPosCm, const FVector& TargetPosCm) const
{
	// `0x10266b10`. `0x10137220` (`1057966c`) is `VectorNormalize`, which ANSWERS THE LENGTH — that
	// is where both distance terms come from, and why they are unsquared.
	if (World == nullptr)
	{
		return false;
	}
	FVector ToTarget = TargetPosCm - OwnerPosCm;
	const float TargetDistance = static_cast<float>(ToTarget.Size());
	// CRASH GUARD, named: retail divides by the length unguarded. A degenerate delta normalises to
	// the zero vector here, whose dot is 0 and which therefore fails the 0.92 test.
	ToTarget = ToTarget.GetSafeNormal();

	// `10266b52`: the walk is `1 .. gpGlobals->maxClients`. This runtime stands one player.
	const FElysiumPlayer* Player = World->FindPlayer();
	if (Player == nullptr)
	{
		return false;
	}
	FVector ToPlayer = Player->EyePosition() - OwnerPosCm;   // slot 0x300 WorldSpaceCenter
	const float PlayerDistance = static_cast<float>(ToPlayer.Size());
	ToPlayer = ToPlayer.GetSafeNormal();
	// `10266bd4`: `dot > 0.92` AND `targetDist > playerDist`, both STRICT (the `<` and `==` bits are
	// tested together and both must be clear).
	const float Dot = static_cast<float>(FVector::DotProduct(ToTarget, ToPlayer));
	return Dot > 0.92f && TargetDistance > PlayerDistance;
}

void FElysiumNpcBase::DisableMoveAndShootOverlay()
{
	// `0x102e8250`: `overlay+0x18 = FLT_MAX` (`0x7f7fffff`).
	MoveAndShootOverlay.NextShotTime = MAX_flt;
	++MoveAndShootOverlay.Disables;
}

void FElysiumNpcBase::ArmMoveAndShootOverlay(float PauseMin, float PauseMax)
{
	// `0x102e8270`: re-derive the shot counts from the active weapon's data (`+0x3a4`/`+0x3a8`),
	// store the pause pair at `overlay+0x24`/`+0x28` and re-arm `overlay+0x18 = curtime +
	// overlay+0x2c`. The same body falls back to `0x102e8250` when the NPC is in state 4, has no
	// weapon, or lacks either the `0x11` or the `0x15` activity sequence.
	//
	// SEAM: this runtime has no activity-sequence table, so the sequence half of that fallback is
	// never satisfiable; the two halves it CAN answer — state 4 and "no weapon" — are run, and the
	// sequence term is named rather than guessed.
	const bool bStateFour = GetMind().State() == EElysiumNpcState::Scripted;   // retail state 4
	if (bStateFour || !Inventory.ActiveWeapon.IsSet())
	{
		DisableMoveAndShootOverlay();
		return;
	}
	MoveAndShootOverlay.PauseMin = PauseMin;
	MoveAndShootOverlay.PauseMax = PauseMax;
	MoveAndShootOverlay.NextShotTime = static_cast<float>(NpcKernelSenses10Shared::NowOf(*this));
	++MoveAndShootOverlay.Arms;
}

// --- Moved from `ElysiumNpcSenses10.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::IsBccTargetable(const FElysiumEntity& Candidate)
{
	// `m_bIsBCCTargetable` (`+0x1480`) — no longer a seam. Story 29e's family Lifecycle19 landed the
	// byte itself: `CAI_BaseNPCTroika::NPCInit` (`1029a4a2`) sets it when and only when
	// `m_statTemplate` is a non-empty string, and five species bodies clear it afterwards (Camera,
	// Placeholder, Newscaster, PlayerController; the payphone sets it). 2055 of the 2060 `npc_*`
	// entities in the shipped maps author a `stattemplate`, and the five that do not are
	// `npc_VNewscaster`, whose own `NPCInit` (`0x103a0420`) clears the byte anyway — so the gate
	// costs nothing that retail keeps and refuses exactly what retail refuses.
	//
	// An entity that is NOT an NPC has no such byte in this port (the player's is unported): retail's
	// gate is on `+0x9c`'s combat character and every caller has already established that, so a
	// non-NPC combat character passes. Every NPC-base instance carries it (story 5 fold A3): a
	// scripted director's `Spawn` stores 0, so no NPC ever takes a director as an enemy.
	const FElysiumNpcBase* const CandidateNpc = Candidate.AsNpcBase();
	return CandidateNpc == nullptr || CandidateNpc->bIsBccTargetable;
}
