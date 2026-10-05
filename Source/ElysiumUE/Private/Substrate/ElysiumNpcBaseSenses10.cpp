// `CAI_BaseNPC`'s bodies of the `Senses10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSenses10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumWorldServices.h"            // IElysiumEmbodiment::TraceRetail, the weapon line of fire's ray
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcEngineRandom.h"
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

	// `0x1024f424 PUSH 0x46004003`: the weapon line of fire's contents mask (slot 573's as well).
	constexpr int32 GWeaponLineOfFireMask = 0x46004003;
	// `0x1024f509 CMP [ECX+0x368],4`: the collision group a shot passes through and re-traces behind.
	constexpr int32 GWeaponLineOfFirePassGroup = 4;
	// NOT RETAIL: `0x1024f3d0` re-enters itself with no bound. The port stops after this many
	// re-traces and takes the `0x66` arm (a crash guard, named).
	constexpr int32 GWeaponLineOfFireMaxDepth = 8;
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
	// Mask `0x46004003` with the self filter `0x101d3190(this, 0)`: `KernelHullTrace` with a zero
	// box, which answers `IElysiumEmbodiment::TraceRetail` (this NPC ignored, characters folded by
	// `KernelTraceKeepsCharacter`). It takes SOURCE units; the slot's vectors are centimetres and
	// are converted here. False from it = no world / no embodiment: clear, the headless fault path.
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
	// carries every arm in retail's order: the six-entry `ClearConditions` table at `0x105c979c`
	// (`1026a2cf`, at `GatherSight`'s head; it was named here and never run until Q-H3, story V13), the
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
	const FElysiumNpcEnemyMemory* const SelectedEnemies = static_cast<FElysiumNpcEnemyMemory*>(GetEnemies()); // 0x102743eb slot541
	if (SelectedEnemies == nullptr) { return nullptr; } // named missing selected-store seam
	for (const FElysiumNpcEnemyMemoryRecord& Record : SelectedEnemies->Records()) // 0x102743eb
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
		if (!Candidate->IsAlive()) // 0x10274475 / 0x100b4dc0: visible resolvable corpses also rejected
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
		if (SelectedEnemies->IsEluded(Candidate->Handle)) // 0x102744a7: same selected store
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
	FElysiumNpcEnemyMemory& SelectedEnemies = *static_cast<FElysiumNpcEnemyMemory*>(GetEnemies()); // 0x102df700 selected store
	// `CAI_Memory::UpdateMemory` (`0x102df700`) with the node array at `m_pNavigator+0x2c`, the
	// enemy, the position and the enemy's velocity. The node array is the AI network and does not
	// exist here, so the record's two node ids stay `INDEX_NONE`.
	const bool bFirstRecord = Enemy != nullptr && SelectedEnemies.Find(Enemy->Handle) == nullptr;
	const double Now = NpcKernelSenses10Shared::NowOf(*this);
	if (Enemy == nullptr)
	{
		SelectedEnemies.UpdatePositionOnly(PositionCm, Now);
		return false;
	}
	SelectedEnemies.UpdateAtPosition(*this, Enemy->Handle, PositionCm, Now);
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
		if (static_cast<FElysiumNpcEnemyMemory*>(GetEnemies())->IsEluded(Enemy->Handle))
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
	// `0x102dfed0` itself: the record's position, else the last position-only record's, else
	// `vec3_origin` -- not the enemy's live origin (story 8 L07).
	const FVector LastKnownCm = Conditions19LastKnownPosition(Enemy);
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
		// `1026fc76`: the weapon's own slot 364 (`+0x5b0`), `CBaseCombatWeapon 0x1024f330`
		// `(ownerPos, target, bSet)`: the owner is the weapon's `m_hOwner (+0x88c)` NPC pointer
		// (`+0x94`, no null guard) -- this body; `shootPos = owner slot 389 (+0x614)(ownerPos)`
		// (`0x1024f330`'s first call); then the weapon's vtable `+0x470` = `0x1024f3d0(owner,
		// ignore = owner, &shootPos, target, bSet)`. This runtime stands no `CBaseCombatWeapon`
		// vtable, so both are the owner's `WeaponLineOfFire`.
		const FVector ShootPosCm = Weapon_ShootPosition(OwnerPosCm);                 // 0x1024f330 slot 389 (+0x614)
		bAnswer = WeaponLineOfFire(ShootPosCm, TargetPosCm, Handle, bSetConditions); // 0x1024f330 CALL [+0x470] -> 0x1024f3d0
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

FElysiumNpcBase::FWeaponLosRay FElysiumNpcBase::WeaponLosRay(const FVector& StartCm,
	const FVector& EndCm, const FElysiumEntityHandle& Ignore) const
{
	FWeaponLosRay Ray;
	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		// No world or no embodiment: nothing is traced and the ray is clear. The headless fault
		// path, not a rule.
		return Ray;
	}
	// `0x10015929 Ray_t::Init(start, end)`: a line, no extents. `TraceRetail` takes centimetres, the
	// slot's own unit -- no conversion on this side (unlike `KernelHullTrace`, which takes Source
	// units and is not used here because its filter's pass entity is always this NPC).
	FElysiumRetailTrace Trace;
	Trace.StartCm = StartCm;
	Trace.EndCm = EndCm;
	Trace.RetailMask = GWeaponLineOfFireMask;                                    // 0x1024f424 PUSH 0x46004003
	// `0x1000bd7f CTraceFilterSimple(ignore, 0)`: the pass entity is the CALLER's `ignore` -- the
	// owner on the first ray, the hit entity on a re-trace (`0x1024f59a PUSH ECX`).
	if (Ignore.IsSet())
	{
		Trace.Ignore.Add(Ignore);
	}
	FElysiumRetailTraceResult Result;
	if (!Embodiment->TraceRetail(Trace, Result))                                 // 0x1024f42a CALL [EDX+0x10]
	{
		return Ray;
	}
	Ray.Fraction = Result.Fraction;
	Ray.Hit = Result.HitEntity.IsSet() ? World->Resolve(Result.HitEntity) : nullptr;
	// The characters, nearest first, as `KernelHullTrace` folds them: the first one the filter keeps
	// (`CTraceFilterSimple::ShouldHitEntity 0x101d31c0`, `KernelTraceKeepsCharacter`) is the only one
	// that can stop the ray. `KernelTraceKeepsCharacter` drops this NPC itself whatever `Ignore` is:
	// on a re-trace retail's filter no longer passes the owner, but the ray starts at the hit point,
	// beyond the owner's own body.
	for (const FElysiumRetailTraceCharacter& Character : Result.Characters)
	{
		if (Character.Entity == Ignore)
		{
			continue;   // the filter's pass entity (`PassServerEntityFilter 0x101d2fc0`)
		}
		FElysiumEntity* const MetBody = World->Resolve(Character.Entity);
		// A body recorded `FSOLID_NOT_SOLID` is not in the engine's solid partition and never reaches
		// the filter: a corpse (`BecomeClientRagdoll 0x10090180`: `AddSolidFlags(w | 4)`) stops
		// blocking on the frame it is killed.
		if (MetBody == nullptr || MetBody->IsRetailNotSolid())
		{
			continue;
		}
		if (!KernelTraceKeepsCharacter(Character.Entity, GWeaponLineOfFireMask))
		{
			continue;
		}
		const bool bNearer = Character.Fraction < Ray.Fraction;
		const bool bSolidFirst = Character.bStartSolid && !Result.bStartSolid;
		if (bNearer || bSolidFirst)
		{
			Ray.Fraction = Character.bStartSolid ? 0.f : Character.Fraction;
			Ray.Hit = MetBody;
		}
		break;
	}
	return Ray;
}

bool FElysiumNpcBase::WeaponLineOfFire(const FVector& ShootPosCm, const FVector& TargetCm,
	const FElysiumEntityHandle& Ignore, bool bSetConditions, int32 Depth)
{
	// `0x1024f3d0(owner EBX, ignore, start* ESI, end* EDI, bSet)`, `RET 0x14`.
	// `0x1024f3f3..0x1024f42a`: one ray `start -> end`, filter `(ignore, group 0)`, mask `0x46004003`.
	const FWeaponLosRay Ray = WeaponLosRay(ShootPosCm, TargetCm, Ignore);
	// `0x1024f42d..0x1024f45a`: the debug overlay line under the cvar at `0x10738960`. NOT PORTED
	// (a debug draw; no state).

	// `0x1024f45d..0x1024f46e`: `fraction == _DAT_10449280` (a DOUBLE 1.0; `TEST AH,0x44 / JP`) -> true.
	if (Ray.Fraction == 1.f)
	{
		return true;                                                             // 0x1024f471 MOV AL,1
	}
	// `0x1024f4a4..0x1024f4b0`: the hit entity is owner slot 167 `GetEnemy()` (`+0x29c`) -> true.
	// The port's null hit is the static world (retail's `worldspawn`, never a null `m_pEnt`), so it
	// is never the enemy, whatever `GetEnemy()` answers.
	if (Ray.Hit != nullptr && Ray.Hit == GetEnemy())                             // 0x1024f4ae CMP ECX,EAX
	{
		return true;                                                             // 0x1024f4b5
	}
	// `0x1024f4c1..0x1024f4c7`: the hit's combat-character cast (`hit+0x9c`, taken at `0x1024f48f`).
	if (Ray.Hit != nullptr && Ray.Hit->AsCombatCharacter() != nullptr)
	{
		// `0x1024f4ce`: owner slot 404 `IRelationType(hit)` (`+0x650`) `== 1` (`D_HT`) -> true: a
		// hated body in the way is shot through (`0x1024f4dc MOV AL,AL`, the relation's low byte).
		if (IRelationType(Ray.Hit) == NpcKernelSenses10Shared::GD_HT)            // 0x1024f4d4 CMP EAX,1
		{
			return true;
		}
		// `0x1024f4e8..0x1024f504`: anyone else is a friend in the line of fire.
		if (bSetConditions)                                                      // 0x1024f4ef TEST AL,AL
		{
			Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x63));        // 0x1024f502 / 0x1024f5cb
		}
		return false;                                                            // 0x1024f5d3 XOR AL,AL
	}
	// `0x1024f509`: `CMP [hit+0x368],4`. Retail reads the word off a NULL `m_pEnt` here and faults
	// (a blocked ray always names an entity there: the static world is `worldspawn`, group 0). The
	// port's static world is a null hit: it takes the `0x66` arm, which is `worldspawn`'s.
	if (Ray.Hit != nullptr
		&& Ray.Hit->CollisionGroup == GWeaponLineOfFirePassGroup                 // 0x1024f509 m_CollisionGroup (+0x368) == 4
		&& Ray.Fraction > 0.f                                                    // 0x1024f516..0x1024f527 f32 0x104454c4 (0.0); AND 0x4100 / JNZ skips on <=
		&& Depth < GWeaponLineOfFireMaxDepth)                                    // the port's guard: retail's recursion is unbounded
	{
		// `0x1024f52d..0x1024f588`: the hit point, `start + (end - start) * fraction`.
		const FVector HitPointCm = ShootPosCm + (TargetCm - ShootPosCm) * static_cast<double>(Ray.Fraction);
		// `0x1024f58c..0x1024f59e`: the same body again `(owner, ignore = the hit entity, &hitPoint,
		// end, bSet)` -- the new filter passes the HIT ENTITY, not the owner -- and its answer is
		// this one's (`0x1024f5a4`, straight to the epilogue).
		return WeaponLineOfFire(HitPointCm, TargetCm, Ray.Hit->Handle, bSetConditions, Depth + 1);
	}
	// `0x1024f5b1..0x1024f5cb`: the world (or anything not group 4, or a group-4 hit at fraction 0).
	// Unlike slot 573 (`0x1026fe73`) this body does NOT record `m_hEnemyOccluder`.
	if (bSetConditions)                                                          // 0x1024f5b8 TEST AL,AL
	{
		Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x66));            // 0x1024f5c7 / 0x1024f5cb
	}
	return false;                                                                // 0x1024f5d3 XOR AL,AL
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
}

void FElysiumNpcBase::ArmMoveAndShootOverlay(float PauseMin, float PauseMax)
{
	// `0x102e8270`, whole. Four refusals land on the disable `0x102e8250` (`102e83c4`), in this
	// order: slot 464 (`+0x740`) answering state 4; no active weapon; no sequence for the
	// translated `0x11` (the walk's aim twin); none for the translated `0x15` (the run's).
	// `SelectHeaviestSequence(…, -1)` passes at `>= 0` (`-1 < seq`). The `(*DAT_10924ab4)->vfunc1()`
	// calls on both exits are the profiler's and have no port line.
	const bool bStateFour = GetMind().State() == EElysiumNpcState::Scripted;   // 102e8270 slot 464 == 4
	if (bStateFour || ActiveWeaponEntity() == nullptr)                         // GetActiveWeapon
	{
		DisableMoveAndShootOverlay();                                          // 0x102e8250
		return;
	}
	int32 WeaponActivity = 0;
	if (SelectHeaviestSequence(TranslateActivityNumber(0x11, WeaponActivity), INDEX_NONE) < 0      // TranslateActivity(0x11) 0x10271ff0
		|| SelectHeaviestSequence(TranslateActivityNumber(0x15, WeaponActivity), INDEX_NONE) < 0)  // TranslateActivity(0x15)
	{
		DisableMoveAndShootOverlay();                                          // 0x102e8250
		return;
	}
	// `0x102517e0(GetActiveWeapon())`: `m_minBurst (+0x1c) = data[+0x3a4]`, `m_maxBurst (+0x20) =
	// data[+0x3a8]`, then the pause pair (`+0x24` / `+0x28`) from the two arguments.
	int32 MinBurst = 0;
	int32 MaxBurst = 0;
	ActiveWeaponBurstWords(MinBurst, MaxBurst);
	MoveAndShootOverlay.MinBurst = MinBurst;                                   // +0x1c
	MoveAndShootOverlay.MaxBurst = MaxBurst;                                   // +0x20
	MoveAndShootOverlay.PauseMin = PauseMin;                                   // +0x24
	MoveAndShootOverlay.PauseMax = PauseMax;                                   // +0x28
	// `m_nMoveShots (+0x14) = RandomInt(data[+0x3a4], data[+0x3a8])` — the weapon words re-read, one
	// draw on the engine stream (`(*DAT_1070b244)->vfunc2`).
	MoveAndShootOverlay.MoveShots = ElysiumNpcEngineRandom::RandomInt(MinBurst, MaxBurst);
	// `+0x18 = curtime + m_initialDelay (+0x2c)`.
	MoveAndShootOverlay.NextShotTime = static_cast<float>(NpcKernelSenses10Shared::NowOf(*this))
		+ MoveAndShootOverlay.InitialDelay;
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
