#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Bosses** — the unnamed bodies of the boss species between `0x10381000` and
// `0x1039a000`. This file carries the seams, `CNPC_VHengeyokai`'s pickup chain, `CNPC_VManBat`'s
// (the Sheriff's bat form) flight and carry chain and the melee-slot bodies of the
// `CNPC_VAndreiBlood` human line; `CNPC_VMingXiao`'s tentacle rules and pedestal pick are
// `Substrate/ElysiumNpcKernelBosses2.cpp`. The declarations and the family's three standing facts
// are `Substrate/ElysiumNpcKernelBosses.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address (image base `0x10000000`), so the numbers are recovered facts and not estimates. Where an
// address is quoted with no number beside it, the datum lives past `.data`'s raw size and is filled
// at runtime — those are named as seams, never guessed. Where the decompiled C had lost the body
// (`0x103983d0`'s jump table and ST0 return) the LISTING was read instead and the section says so.

namespace
{
	// Retail's `.rdata`, one line per constant. Distances are SOURCE units.
	constexpr float BossesZero = ElysiumNpcTunables::Zero;   // also `_DAT_1044fab0`, the double zero
	constexpr float BossesOne = ElysiumNpcTunables::One;
	// `0x10381e90`'s grab-bone search: the initial best is 1025 units SQUARED.
	constexpr float PickupGrabBoneRangeSq = 1050625.0f;

	// `0x103822a0`'s facing cone, degrees. `_DAT_1049ae98` is the same cell the kick clamp
	// (`0x102b6890`) uses as its lower bound and `_DAT_1044eb0c` its upper — a +-20 degree cone.
	constexpr float PickupConeLo = -20.0f;         // _DAT_1049ae98
	constexpr float PickupConeHi = 20.0f;          // _DAT_1044eb0c

	// `UTIL_AngleDiff` `0x1013d580`'s two wrap bounds.
	constexpr float AngleDiffLo = -180.0f;         // _DAT_10462948
	constexpr float AngleDiffHi = ElysiumNpcTunables::OneEighty;
	constexpr float DegreesPerTurn = 360.0f;       // _DAT_10450568

	// `0x10382970`'s blacklist duration, seconds. The same 20.0 cell as the cone's upper bound;
	// `shape.md` records MingXiao blacklisting a thrown object for the same 20 s at `+0x665c`.
	constexpr float BlacklistSeconds = 20.0f;      // _DAT_1044eb0c

	// The release bodies' aim offset above the target's origin.
	constexpr float ThrowAimHeight = 48.0f;        // _DAT_10447ee8

	// `0x102c43b0`'s collision-ignore re-arm, per species.
	constexpr float HengeyokaiIgnoreSeconds = 0.75f;
	constexpr float ManBatIgnoreSeconds = 2.0f;

	// `0x1038b370`. `_DAT_10449270`, `_DAT_1046eca8`, `_DAT_10449280`, `_DAT_10450010` and
	// `_DAT_10449198` are DOUBLES in `.rdata` that the body converts to float at the point of use;
	// the decompiled C's `(float)_DAT_…` cast is what says so.
	constexpr double ManBatStationarySeconds = ElysiumNpcTunables::HalfDouble;
	constexpr float ManBatChaseHeight = 150.0f;          // _DAT_1046eca8
	constexpr float ManBatFlyByHeightPad = static_cast<float>(ElysiumNpcTunables::OneDouble);
	constexpr float ManBatDownAccelScale = 3.0f;         // _DAT_10450010
	constexpr float ManBatOverspeedScale = 0.2f;         // _DAT_10449198
	constexpr float ManBatFastSpeed = 700.0f;
	constexpr float ManBatSlowSpeed = 500.0f;
	constexpr float ManBatSlowZThreshold = -30.0f;       // _DAT_10462868
	constexpr float ManBatVelocityProbeZ = 10.0f;        // the literal 10.0 the probe's Z is seeded with
	constexpr int32 ManBatMoveGoalNodeModeChase = 6;     // FUN_1042fbf0(0xfa0b069a)
	constexpr int32 ManBatMoveGoalNodeModeFlyBy = 7;     // FUN_1042fbf0(0xfa0b069b)
	constexpr int32 ManBatMoveGoalNodeModeTeleport = 2;  // FUN_1042fb50(2), the forced search mode
	constexpr float ManBatTeleportHintRangeUnits = 15000.0f;
	constexpr int32 ManBatTeleportHintType = 20000;
	constexpr const TCHAR* ManBatTeleportEmitter = TEXT("sheriff_teleport_emitter");

	// `0x1038bec0`'s trace: the direction is scaled by `speed * 0.1` and swept with this mask.
	constexpr float ManBatProbeScale = 0.1f;             // _DAT_104491b4
	constexpr int32 ManBatProbeMask = 0x202400b;
	// `m_Activity` values `0x1038b370` and `0x1038bec0` compare against.
	constexpr int32 ActivityStillZero30 = 0x30;
	constexpr int32 ActivityStillZeroB0 = 0xb0;
	constexpr int32 ActivityStillZero4B = 0x4b;
	constexpr int32 ActivityStillZero1171 = 0x1171;
	constexpr int32 ActivityFlapStamp = 0x22;

	// `0x1038b370`'s TaskFail code on an unreachable fly-by target.
	constexpr int32 ManBatUnreachableFailure = 0x1a;

	// `CTraceFilterManBatNoIBeamEntity`'s one name, `DAT_10642d28`.
	constexpr const TCHAR* ManBatNoHitName = TEXT("lbeam*");

	// `UTIL_AngleDiff` `0x1013d580`: `a - b` walked back into `[-180, 180]` by whole turns, wrapping
	// only on the side the `a <= b` test selects. Families Facing and Positions each keep an
	// identical private copy for the same reason: neither owns the other's file.
	float BossesAngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < AngleDiffLo)
			{
				Delta += DegreesPerTurn;
			}
		}
		else
		{
			while (Delta > AngleDiffHi)
			{
				Delta -= DegreesPerTurn;
			}
		}
		return Delta;
	}

	// `UTIL_VecToYaw` `0x101d2c70` over a delta in THIS world's axes, whose Y is the negated Source
	// one (`bsp.source_to_unreal`). Retail answers `0.0` for a delta whose X and Y are both zero and
	// folds a negative result up by a whole turn.
	float BossesVecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return BossesZero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < BossesZero)
		{
			Yaw += DegreesPerTurn;
		}
		return Yaw;
	}

	// `VectorNormalize` `0x10137220`: `1.0 / (FLT_EPSILON + length)`, so a zero vector normalizes to
	// zero rather than to NaN and a unit vector comes back a hair short. Both are observable, and
	// both are reproduced.
	constexpr float BossesNormalizeEpsilon = ElysiumNpcTunables::FloatEpsilon;

	FVector BossesNormalize(const FVector& V)
	{
		const float Scale = BossesOne / (BossesNormalizeEpsilon + static_cast<float>(V.Size()));
		return V * Scale;
	}

	// This image's `NPC_STATE` ordinals, which are NOT SDK 2013's: `0x1027e660`'s name table and
	// `0x1026e3e0`'s switch give **1 IDLE, 2 COMBAT, 3 ALERT, 7 DEAD**
	// (`docs/vtmb/npc-ai/conditions-and-states.md`). Families Anim, Conditions and Sounds each keep
	// an identical private copy; this is a fourth, because no family owns another's file.
	int32 BossesRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}

	// Slot 158 `IsAlive` (`0x100b4dc0`) is `m_lifeState == LIFE_ALIVE` on `CBaseEntity`, so retail
	// can ask it of ANY entity. This runtime declares it only on the NPC leaf (the generated
	// `ElysiumNpcKernelSlots.inl`), so a non-NPC entity answers through the one liveness word it
	// does carry.
	bool BossesEntityIsAlive(FElysiumEntity* Entity)
	{
		if (Entity == nullptr)
		{
			return false;
		}
		if (FElysiumNpc* Npc = Entity->AsNpc())
		{
			return Npc->IsAlive();
		}
		return !Entity->IsDead();
	}

	// `0x1038b370`'s watchdog triple is `_DAT_1093b8b8..c0` plus `_DAT_1093b8c4` — FILE STATICS, one
	// per level rather than one per NPC, installed by the `atexit`-registered initializer at the top
	// of the body. Ported as a static for exactly that reason.
	FElysiumNpc::FManBatStationaryWatch GManBatWatch;
}

// -------------------------------------------------------------------------------------------------
// The seams. Each answers nothing and names the retail call it stands for.
// -------------------------------------------------------------------------------------------------

FElysiumEntityHandle FElysiumNpc::CreatePhysAnimlink()
{
	// SEAM for `CBaseEntity::CreateNoSpawn(this, "phys_animlink", &vec3_origin)`. This runtime
	// registers no `phys_animlink` class, so there is nothing to create; the invalid handle is
	// retail's own failed-create answer.
	return FElysiumEntityHandle::Invalid();
}

int32 FElysiumNpc::LookupBoneByName(const TCHAR* BoneName) const
{
	// SEAM for `CBaseAnimating::GetModelPtr(-1)` and the `studiohdr_t` bone-table scan. No bone table
	// reaches the kernel here.
	(void)BoneName;
	return INDEX_NONE;
}

bool FElysiumNpc::RagdollElementForBone(const FElysiumEntity* Carried, int32 BoneIndex) const
{
	// SEAM for the `__RTDynamicCast` to `CRagdollProp` (`0x1057fff0`) and `+0x424`'s element lookup.
	(void)Carried;
	(void)BoneIndex;
	return false;
}

bool FElysiumNpc::RagdollBonePosition(const FElysiumEntity* InTarget, const TCHAR* BoneName,
	FVector& OutPositionUnits) const
{
	// SEAM for `0x10381e90`'s cast (`0x1057c684`), `+0x424`'s element lookup by NAME and the
	// element's `+0x94 GetPosition`.
	(void)InTarget;
	(void)BoneName;
	(void)OutPositionUnits;
	return false;
}

void FElysiumNpc::WirePhysAnimlink(const FElysiumEntityHandle& Link, int32 CarrierBone,
	int32 CarriedElement)
{
	// SEAM for `thunk_FUN_1014f210(link, this, bone, element, &vec3_origin, &vec3_origin)`.
	(void)Link;
	(void)CarrierBone;
	(void)CarriedElement;
}

void FElysiumNpc::RemovePhysAnimlink(const FElysiumEntityHandle& Link)
{
	// SEAM for `thunk_FUN_101cd970(link)`, the `UTIL_Remove` of the link entity.
	(void)Link;
}

void FElysiumNpc::SolveThrowImpulse(const FVector& FromUnits, const FVector& ToUnits,
	FVector& InOutImpulse) const
{
	// SEAM for `0x102c4cc0`. The recovered algebra, for whoever stands the solver:
	//     g   = sv_gravity * m_flGravity (+0x3ec) * _DAT_1044f030;
	//     d   = impulse.z * impulse.z - -(to.z - from.z) * g * _DAT_10450aa0;
	//     if (d < 0) d = 0;
	//     t   = (-impulse.z - sqrt(d)) / (g + g);
	//     impulse.x = t <= _DAT_1049a1c8 ? 0 : (to.x - from.x) / t;
	//     impulse.y = t <= _DAT_1049a1c8 ? 0 : (to.y - from.y) / t;
	// Note that the Z the caller supplied is NOT rewritten and that the negated Z term makes the
	// discriminant clamp reachable for an aim point above the launch point.
	(void)FromUnits;
	(void)ToUnits;
	(void)InOutImpulse;
}

void FElysiumNpc::ApplyThrowImpulse(const FElysiumEntityHandle& Carried, const FVector& ImpulseUnits)
{
	// SEAM for the `CRagdollProp` `+0x428` arm and the `IPhysicsObject` `+0xa0`/`+0x9c` arm.
	(void)Carried;
	(void)ImpulseUnits;
}

bool FElysiumNpc::IsCarriedBreakable(const FElysiumEntity* Carried) const
{
	// SEAM for `thunk_FUN_101578b0(ent)`.
	(void)Carried;
	return false;
}

void FElysiumNpc::SetCarriedBreakable(const FElysiumEntityHandle& Carried, bool bBreakable)
{
	// SEAM for `thunk_FUN_101578d0(ent, b)`.
	(void)Carried;
	(void)bBreakable;
}

void FElysiumNpc::SetCarriedRagdollHeld(const FElysiumEntityHandle& Carried, bool bHeld)
{
	// SEAM for `thunk_FUN_10157890(ragdoll, b)`, which `0x10382670` sets before it reads the element.
	(void)Carried;
	(void)bHeld;
}

void FElysiumNpc::StartIgnoringCollision(const FElysiumEntityHandle& Other)
{
	// NO LONGER A SEAM. `0x102c4380` is family **TroikaHelpers**' row and it landed the body as
	// `FUN_102c4380` in `ElysiumNpcKernelTroikaHelpers2.cpp`: it writes `IgnoreCollisionEntity`
	// (`+0x055c`) and pins `m_flIgnoreCollisionTimer` (`+0x6458`) to `FLT_MAX`. This name stays
	// because the boss call sites read better with it, and it now forwards rather than recording.
	FUN_102c4380(World != nullptr ? World->Resolve(Other) : nullptr);
}

void FElysiumNpc::ArmIgnoreCollisionExpiry(float Seconds)
{
	// Likewise `0x102c43b0`, landed as `FUN_102c43b0`: with an ignore live it renews
	// `m_flIgnoreCollisionExpire` to `curtime + Seconds` and re-checks through `0x102c43f0`, so a
	// zero or negative duration expires on the same call.
	FUN_102c43b0(Seconds);
}

void FElysiumNpc::CallFormBit(bool bSet)
{
	// `0x10381c00` — family **Misc** landed the body as `FElysiumNpc::FormBit`, so this forwards
	// rather than recording: the `CARRYING_BODY` bit, the `m_flFishTimer` (+0x666c) stamp and the
	// `m_bDidFakeThrow` (+0x667d) clear are all real now. The two counters stay because the two
	// Hengeyokai call sites are still asserted through them.
	++FormBitCalls;
	bLastFormBitArm = bSet;
	FormBit(bSet);
}

bool FElysiumNpc::BossBlacklistHolds(const FElysiumEntity* Candidate) const
{
	// NO LONGER A SEAM. Family **Species** landed `0x10366400` as `FUN_10366400` over the
	// `CNPC_VBaseBoss::m_BlacklistedEntities` array (+0x665c) it also declared, so the walk over
	// the blacklist is real; an empty blacklist still admits every candidate, which is what the
	// permissive arm was standing in for. `const_cast` because the retail body prunes expired rows
	// as it walks, which is a write this query has always made.
	return const_cast<FElysiumNpc*>(this)->FUN_10366400(Candidate);
}

FElysiumEntity* FElysiumNpc::ManBatFindMoveGoalHint(int32 HintType, float RadiusUnits)
{
	// SEAM for `thunk_FUN_102d1af0(this, 20000, 0, 15000.0, 0, 0)`. Family Hints states the whole
	// hint-store gap; this is the entity-answering form `0x1038b370` needs.
	(void)HintType;
	(void)RadiusUnits;
	return nullptr;
}

void FElysiumNpc::PlaceNamedEmitter(const TCHAR* Name, const FVector& PositionUnits)
{
	// SEAM for `thunk_FUN_102c41b0(this, name, &position)`. Recorded so the teleport's two
	// placements and their ORDER are measurable.
	TeleportEmitterPlacements.Add(FTeleportEmitterPlacement{ FString(Name), PositionUnits });
}

FVector FElysiumNpc::AbsVelocityUnits() const
{
	// SEAM for `CBaseEntity::CalcAbsoluteVelocity` plus `m_vecAbsVelocity` (+0x3bc). This runtime
	// keeps one velocity, in centimetres per second, and has no separate absolute copy to
	// recalculate; the conversion to Source units is what every constant in `0x1038b370` is in.
	return Velocity / ElysiumMove::U;
}

bool FElysiumNpc::NavigatorCanReach(const FVector& PositionUnits) const
{
	// SEAM for `thunk_FUN_102f1a20(m_pNavigator, &position, 0x2400b)`. Family Motor states the whole
	// navigator gap. False is retail's refusal, which is the arm that fails the task.
	(void)PositionUnits;
	return false;
}

int32 FElysiumNpc::MingXiaoPedestalCvar() const
{
	// `DAT_1093ba8c` `+0x2c`: `ming_xiao_pickup`, shipped "1", which opens the search.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::MingXiaoPickup);
}

float FElysiumNpc::ManBatAccelerationCvar() const
{
	// `DAT_1093b7cc` `+0x28`: `manbat_delta`, shipped "600.0".
	return ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::ManbatDelta);
}

FVector FElysiumNpc::EntityVelocityUnits(const FElysiumEntity& Entity) const
{
	// Slot 199 `GetVelocity(Vector*, AngularImpulse*)`, the linear half, in SOURCE units.
	return Entity.Velocity / ElysiumMove::U;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VHengeyokai`'s grab bone — `0x10381e90`.
// -------------------------------------------------------------------------------------------------

const TCHAR* const* FElysiumNpc::PickupGrabBoneNames()
{
	// `PTR_s_Bone01_1063bd88`, read from the image: two `char*` rows followed by the shared empty
	// string `DAT_106b8540`, which is what stops retail's walk (its condition is the FIRST CHARACTER
	// of the next row, not a null pointer).
	static const TCHAR* const Names[] = { TEXT("Bone01"), TEXT("Bone04"), nullptr };
	return Names;
}

bool FElysiumNpc::FindPickupTargetGrabBone(const FElysiumEntity* InTarget)
{
	// `0x10381e90`, arm for arm.
	//
	//     bool found = false;  float best = 1050625.0;  int index = 0;
	//     CRagdollProp* rag = dynamic_cast<...>(target);
	//     if (rag == NULL) {
	//         m_vecPickupTargetPos = target->GetOrigin();     // slot 220 (+0x370)
	//         m_iPickupTargetGrabBone = 0;
	//         return true;
	//     }
	//     for (name : { "Bone01", "Bone04" }) {
	//         element = rag->GetElement(name);                 // +0x424
	//         if (!element) { ++index; continue; }             // the index still advances
	//         element->GetPosition(&pos, &angles);             // +0x94
	//         d2 = (GetOrigin() - pos).LengthSqr();            // slot 220, re-read per bone
	//         if (d2 < best) { m_vecPickupTargetPos = pos; found = true;
	//                          m_iPickupTargetGrabBone = index; best = d2; }
	//         ++index;
	//     }
	//     return found;
	//
	// Retail does NOT clamp the best distance when the cast fails — the fallback arm writes bone 0
	// and answers true unconditionally, and that is the arm this substrate always takes because
	// `RagdollBonePosition` is a seam. Both arms are ported; only one is reachable today.
	if (InTarget == nullptr)
	{
		// Retail dereferences `param_1` through `__RTDynamicCast`, which answers NULL for a null
		// pointer, so a null target lands on the fallback arm and then dereferences the target's
		// vtable. This port refuses instead — the one divergence, and it is a crash retail would
		// take, not a behaviour a shipped program could have been tuned against.
		return false;
	}

	bool bFound = false;
	float Best = PickupGrabBoneRangeSq;
	int32 Index = 0;
	FVector BonePositionUnits = FVector::ZeroVector;
	const TCHAR* const* Names = PickupGrabBoneNames();
	// The RTTI cast: `RagdollBonePosition` answers false for every bone when the target is not a
	// ragdoll, which is retail's null-cast arm — but retail decides ONCE, before the walk, so the
	// fallback is taken only when the cast itself failed.
	bool bAnyElement = false;
	for (int32 i = 0; Names[i] != nullptr; ++i)
	{
		if (RagdollBonePosition(InTarget, Names[i], BonePositionUnits))
		{
			bAnyElement = true;
			const FVector MineUnits = Origin / ElysiumMove::U;
			const float DistSq = static_cast<float>((MineUnits - BonePositionUnits).SizeSquared());
			if (DistSq < Best)
			{
				HengeyokaiPickupTargetPos = BonePositionUnits;
				bFound = true;
				HengeyokaiPickupTargetGrabBone = Index;
				Best = DistSq;
			}
		}
		++Index;
	}
	if (bAnyElement)
	{
		return bFound;
	}
	// The cast failed: the target's own origin, bone 0, and true.
	HengeyokaiPickupTargetPos = InTarget->Origin / ElysiumMove::U;
	HengeyokaiPickupTargetGrabBone = 0;
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VHengeyokai`'s facing gate — `0x103822a0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::WithinPickupFacingCone(const FVector& Delta, float YawDegrees)
{
	// `0x103822a0`'s tail, read from the listing because the decompiler turned the two `FCOMP`s into
	// status-word arithmetic:
	//     1038237b  FLD  [0x1049ae98] ; FCOMP ; JP  -> AL = 0     (const  >  diff)
	//     1038238d  FCOMP [0x1044eb0c] ; JP        -> AL = 0     (diff   >  const)
	//     1038239a  MOV AL, 1
	// so the answer is `-20.0 <= AngleDiff(VecToYaw(delta), yaw) <= 20.0`, inclusive on both edges.
	const float Diff = BossesAngleDiff(BossesVecToYaw(Delta), YawDegrees);
	if (PickupConeLo > Diff)
	{
		return false;
	}
	return !(Diff > PickupConeHi);
}

bool FElysiumNpc::FUN_103822a0(const FElysiumEntity* InTarget) const
{
	// `0x103822a0`'s head, in retail's order. Every early-out answers TRUE (`MOV AL, 1`):
	//     if (param_1 == NULL) return true;
	//     if (!m_hPickupTarget.IsValid()) return true;          // +0x6664, the EHANDLE serial test
	//     delta = param_1->GetOrigin() - GetOrigin();            // slot 220, BOTH sides
	//     return -20 <= AngleDiff(VecToYaw(delta), GetAngles().y) <= 20;   // slot 221's yaw
	if (InTarget == nullptr)
	{
		return true;
	}
	if (World == nullptr || World->Resolve(HengeyokaiPickupTarget) == nullptr)
	{
		return true;
	}
	const FVector Delta = InTarget->Origin - Origin;
	return WithinPickupFacingCone(Delta, static_cast<float>(Angles.Y));
}

// -------------------------------------------------------------------------------------------------
// The pickup species table and the attach/release pair — `0x10382670`, `0x1038f430`, `0x10382400`,
// `0x1038f790`.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FPickupSpecies* FElysiumNpc::PickupSpeciesRows(int32& OutCount)
{
	static constexpr FPickupSpecies Rows[] =
	{
		{ TEXT("CNPC_VHengeyokai"), TEXT("0x10382670"), TEXT("0x10382400"),
			TEXT("Bip01 R Hand"), 0x6664, HengeyokaiIgnoreSeconds, false },
		{ TEXT("CNPC_VManBat"), TEXT("0x1038f430"), TEXT("0x1038f790"),
			TEXT("Bip01_R_Foot"), 0x668c, ManBatIgnoreSeconds, true },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FPickupSpecies* FElysiumNpc::PickupSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FPickupSpecies* Rows = PickupSpeciesRows(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		if (FCString::Strcmp(Rows[i].RetailClass, InRetailClass) == 0)
		{
			return &Rows[i];
		}
	}
	// A species with no body of its own inherits its base's, exactly as the vtable does. Neither
	// boss has a subclass in the census, so this walk answers null today; it is the same resolution
	// family Hints performs and is written the same way rather than special-cased.
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

const FElysiumNpc::FPickupSpecies* FElysiumNpc::PickupSpecies() const
{
	const FElysiumNpcClass* Cls = RetailClass();
	return PickupSpeciesOf(Cls != nullptr ? Cls->Name : nullptr);
}

bool FElysiumNpc::AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey)
{
	return AttachPickupAnimlinkFor(PickupSpecies(), Carried, ElementKey);
}

void FElysiumNpc::ReleasePickupAnimlink(const FElysiumEntity* AimTarget)
{
	ReleasePickupAnimlinkFor(PickupSpecies(), AimTarget);
}

bool FElysiumNpc::AttachPickupAnimlinkFor(const FPickupSpecies* Row, FElysiumEntity* Carried,
	int32 ElementKey)
{
	// `0x10382670` (Hengeyokai) and `0x1038f430` (ManBat), one body written twice:
	//
	//     if (param_1 == NULL) return false;
	//     link = CreateNoSpawn("phys_animlink", vec3_origin);   if (!link) return false;
	//     bone = <scan the model's bone table for the species' carrier bone>;
	//     if (bone < 0) return false;
	//     if (param_1->m_pPhysicsObject (+0x36c) == 0) return false;
	//     --- Hengeyokai only ---
	//       rag = dynamic_cast<CRagdollProp*>(param_1);
	//       if (rag) { rag->SetHeld(true);                     // 0x10157890
	//                  key = Decode(m_SecurePickupParam);       // 0x10430130 over +0x66a0
	//                  rag->SetDamage((float)key);              // +0x2fc
	//                  element = rag->GetElement(key);          // +0x424
	//                  if (!element) return false; }
	//     --- ManBat only ---
	//       element = param_1->GetElement(param_2);             // +0x424, no cast, no decode
	//       if (!element) return false;
	//     LinkAnimlink(link, this, bone, element, vec3_origin, vec3_origin);   // 0x1014f210
	//     m_hPhysicsAnimlink = link->GetRefEHandle();
	//     --- Hengeyokai only ---  FINDING_BODY off, then FormBit(true): CARRYING_BODY on,
	//                              m_flFishTimer = curtime + RandomFloat(5, 8), m_bDidFakeThrow = 0
	//     --- ManBat only ---      CARRYING_BODY on (0x1038f600),
	//                              m_bPickupTargetBreakable = IsBreakable(param_1),
	//                              SetBreakable(param_1, false)
	//     return true;
	//
	// The `bone < 0` guard is retail's and is reproduced exactly: its scan leaves the loop counter
	// at the bone COUNT when nothing matched, so a miss is a POSITIVE index and the guard does not
	// fire. Only an empty bone table (count 0, counter 0) reaches it, and even that is not negative
	// — which is why this port answers on the SEAM's `INDEX_NONE` instead and says so.
	if (Row == nullptr)
	{
		// Neither boss: no body fills this for the class, so there is nothing to run.
		return false;
	}
	if (Carried == nullptr)
	{
		return false;
	}
	const FElysiumEntityHandle Link = CreatePhysAnimlink();
	if (World == nullptr || World->Resolve(Link) == nullptr)
	{
		return false;
	}
	const int32 Bone = LookupBoneByName(Row->CarrierBone);
	if (Bone < 0)
	{
		return false;
	}
	// `param_1->m_pPhysicsObject != 0` — no physics object on a port entity, so this is part of the
	// element seam and is folded into it.
	const bool bHengeyokai = FCString::Strcmp(Row->RetailClass, TEXT("CNPC_VHengeyokai")) == 0;
	const int32 Key = bHengeyokai ? HengeyokaiPickupParam : ElementKey;
	if (bHengeyokai)
	{
		SetCarriedRagdollHeld(Carried->Handle, true);
	}
	if (!RagdollElementForBone(Carried, Key))
	{
		return false;
	}
	WirePhysAnimlink(Link, Bone, Key);
	if (bHengeyokai)
	{
		HengeyokaiPhysicsAnimlink = Link;
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);   // 0x10381ba0(this, false)
		CallFormBit(true);                               // 0x10381c00(this, true)
	}
	else
	{
		ManBatPhysicsAnimlink = Link;
		NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);    // 0x1038f600(this, true)
		bManBatPickupTargetBreakable = IsCarriedBreakable(Carried);
		SetCarriedBreakable(Carried->Handle, false);
	}
	return true;
}

void FElysiumNpc::ReleasePickupAnimlinkFor(const FPickupSpecies* Row, const FElysiumEntity* AimTarget)
{
	// `0x10382400` (Hengeyokai) and `0x1038f790` (ManBat), one body written twice:
	//
	//     if (m_hPhysicsAnimlink resolves) UTIL_Remove(it);     // 0x101cd970
	//     m_hPhysicsAnimlink = -1;
	//     aim = <Hengeyokai: param_1 | ManBat: m_hClosestPlayer (+0x628c)>;
	//     if (aim) {
	//         if (m_hPickupTarget resolves) {
	//             impulse = vec3_origin;
	//             to = aim->GetAbsOrigin(); to.z += 48.0;        // slot 217, _DAT_10447ee8
	//             SolveThrow(&impulse, carried->GetAbsOrigin(), &to);         // 0x102c4cc0
	//             rag = dynamic_cast<CRagdollProp*>(carried);
	//             if (rag)  { rag->ApplyImpulse(impulse, vec3_origin);        // +0x428
	//                         --- ManBat only --- SetBreakable(rag, m_bPickupTargetBreakable); }
	//             else      { carried->m_pPhysicsObject->GetPosition(...);    // +0xa0
	//                         carried->m_pPhysicsObject->ApplyForceCenter(...); }   // +0x9c
	//         }
	//     }
	//     --- ManBat only --- StartIgnoringCollision(carried);   // 0x102c4380, expire = FLT_MAX
	//     ArmIgnoreCollisionExpiry(<0.75 | 2.0>);                 // 0x102c43b0
	//     m_hPickupTarget = -1;
	//     <Hengeyokai: FormBit(false) | ManBat: CARRYING_BODY off>;
	//
	// TWO ORDERING FACTS, both reproduced: the Hengeyokai arm clears `m_hPickupTarget` AFTER the
	// throw and BEFORE the collision re-arm, and the ManBat arm clears it AFTER the re-arm — which
	// is why its `StartIgnoringCollision` still resolves the carried entity and the Hengeyokai arm
	// never calls it at all.
	if (Row == nullptr)
	{
		return;
	}
	const bool bHengeyokai = FCString::Strcmp(Row->RetailClass, TEXT("CNPC_VHengeyokai")) == 0;
	FElysiumEntityHandle& AnimLink = bHengeyokai ? HengeyokaiPhysicsAnimlink : ManBatPhysicsAnimlink;
	FElysiumEntityHandle& PickupTargetWord =
		bHengeyokai ? HengeyokaiPickupTarget : ManBatPickupTarget;

	if (World != nullptr && World->Resolve(AnimLink) != nullptr)
	{
		RemovePhysAnimlink(AnimLink);
	}
	AnimLink = FElysiumEntityHandle::Invalid();

	const FElysiumEntity* Aim = AimTarget;
	if (!bHengeyokai)
	{
		// `0x1038f790` ignores any argument and aims at the cached closest player.
		Aim = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	}
	if (Aim != nullptr && World != nullptr)
	{
		const FElysiumEntity* Carried = World->Resolve(PickupTargetWord);
		if (Carried != nullptr)
		{
			FVector Impulse = FVector::ZeroVector;
			FVector ToUnits = Aim->Origin / ElysiumMove::U;
			ToUnits.Z += ThrowAimHeight;
			SolveThrowImpulse(Carried->Origin / ElysiumMove::U, ToUnits, Impulse);
			ApplyThrowImpulse(PickupTargetWord, Impulse);
			if (Row->bRestoresBreakable)
			{
				SetCarriedBreakable(PickupTargetWord, bManBatPickupTargetBreakable);
			}
		}
	}
	if (!bHengeyokai)
	{
		StartIgnoringCollision(PickupTargetWord);
		ArmIgnoreCollisionExpiry(Row->IgnoreCollisionSeconds);
		PickupTargetWord = FElysiumEntityHandle::Invalid();
		NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);   // 0x1038f600(this, false)
		return;
	}
	PickupTargetWord = FElysiumEntityHandle::Invalid();
	ArmIgnoreCollisionExpiry(Row->IgnoreCollisionSeconds);
	CallFormBit(false);                                   // 0x10381c00(this, false)
}

// -------------------------------------------------------------------------------------------------
// The expiring blacklist — `0x10382970`, `0x10382b30`, `0x10382aa0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::AddBlacklistedEntity(const FElysiumEntity* Entity)
{
	// `0x10382970`. The observable half, in retail's order:
	//     expiry = gpGlobals->curtime + 20.0;               // _DAT_1044eb0c
	//     <CUtlVector grow: 4 on an empty store, else double, else + m_nGrowSize>
	//     m_pElements = m_pMemory;  ++m_Size;
	//     memmove(base + (old+1)*8, base + old*8, ((m_Size - old) - 1) * 8);   // always 0 bytes
	//     base[old] = { param_1, expiry };
	// The `memmove` length is `(old + 1 - old) - 1`, which is zero on every path, so the append is
	// the whole of it. The grow arithmetic and `m_pElements` are `CUtlMemory` bookkeeping with no
	// observable effect on a `TArray` and are deliberately not reproduced.
	//
	// Retail stores the EHANDLE, not the pointer, and stamps the expiry whether or not the entity is
	// already in the store — a second add is a second row, not a refresh.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FBlacklistedEntity Row;
	Row.Entity = Entity != nullptr ? Entity->Handle : FElysiumEntityHandle::Invalid();
	Row.ExpiresAt = Now + BlacklistSeconds;
	HengeyokaiBlacklist.Add(Row);
}

int32 FElysiumNpc::FindBlacklistedEntity(const FElysiumEntity* Entity) const
{
	// `0x10382b30`: walk the store, resolve each row's EHANDLE and compare the POINTER against the
	// candidate. A row whose handle no longer resolves compares as NULL, so a null candidate matches
	// the first dead row — retail's behaviour, and reproduced.
	if (World == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 i = 0; i < HengeyokaiBlacklist.Num(); ++i)
	{
		const FElysiumEntity* Stored = World->Resolve(HengeyokaiBlacklist[i].Entity);
		if (Stored == Entity)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpc::BlacklistTestAndExpire(TArray<FBlacklistedEntity>& Store, int32 Index, double Now)
{
	// `0x10382aa0`'s tail, and `0x10366400`'s — the two are byte-for-byte the same body over two
	// different stores:
	//     if (index == -1) return false;
	//     if (curtime < store[index].expiry) return true;
	//     if (m_Size > 0) { memmove(&store[index], &store[m_Size - 1], 8); --m_Size; }
	//     return false;
	// The expire arm is a SWAP-REMOVE with the last row, not an ordered erase, and the `m_Size > 0`
	// guard is dead (the index came from a walk of that same size).
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (Now < Store[Index].ExpiresAt)
	{
		return true;
	}
	if (Store.Num() > 0)
	{
		Store[Index] = Store[Store.Num() - 1];
		Store.RemoveAt(Store.Num() - 1, EAllowShrinking::No);
	}
	return false;
}

bool FElysiumNpc::IsEntityBlacklisted(const FElysiumEntity* Entity)
{
	// `0x10382aa0`.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return BlacklistTestAndExpire(HengeyokaiBlacklist, FindBlacklistedEntity(Entity), Now);
}

// -------------------------------------------------------------------------------------------------
// Slot 482 `CanPlaySequence`, the species half — `0x103850a0`, `0x10396e90`.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FCanPlaySequenceSpecies* FElysiumNpc::CanPlaySequenceSpeciesRows(int32& OutCount)
{
	// The two rows this family owns. `CNPC_VAnimal`'s `0x1035fd40` (which also serves `CNPC_VDog`,
	// `CNPC_VRat`, `CNPC_VScurrying` and `CNPC_VZombie`) and `CNPC_VTzimisce`'s `0x103bd270` are
	// family **Species**' rows and are deliberately absent rather than claimed here; all four bodies
	// are byte-identical to the base `0x10278090`, which is why one rule serves every one of them.
	// `0x103850a0` fills the slot for `CNPC_VAndreiBlood` and 41 other classes of the same human
	// line (`CNPC_ProneDialog`, `CNPC_VAsianVampire`, `CNPC_VBach`, …); the row names the class the
	// ledger indexes the body under.
	static constexpr FCanPlaySequenceSpecies Rows[] =
	{
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x103850a0") },
		{ TEXT("CNPC_VMingXiao"), TEXT("0x10396e90") },
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FCanPlaySequenceSpecies* FElysiumNpc::CanPlaySequenceSpeciesOf(
	const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FCanPlaySequenceSpecies* Rows = CanPlaySequenceSpeciesRows(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		if (FCString::Strcmp(Rows[i].RetailClass, InRetailClass) == 0)
		{
			return &Rows[i];
		}
	}
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

int32 FElysiumNpc::CanPlaySequenceStateArm(int32 Result, bool bDisregardState, int32 InterruptLevel,
	EElysiumNpcState State, EElysiumNpcState IdealState)
{
	// The tail of `0x103850a0` / `0x10396e90` / `0x1035fd40` / `0x103bd270`, read from the LISTING
	// (`vtmb_asm 0x103850a0`, `0x1038512d`..`0x1038516f`) because the one instruction that matters
	// is a branchless mask the decompiler renders as arithmetic:
	//
	//     1038512d  MOV AL, [ESP+0xc]      ; param_1, fDisregardState
	//     10385135  MOV EAX, [ESI+0x5cc0]  ; m_NPCState      -> !=0, !=1
	//     10385144  CMP [ESI+0x5cc4], 1    ; m_IdealNPCState -> !=1
	//     1038514d  CMP EAX, 3 / JNZ ; CMP [ESP+0x10], 1 / JGE   ; ALERT with level >= 1 escapes
	//     10385159  XOR ECX,ECX / CMP EAX,4 / SETNZ CL / DEC ECX / AND ECX,EDI   ; state == 4 keeps
	//     1038516b  MOV EAX, EDI           ; the escape: the result stands
	//
	// **THIS IS THE ONE THING THE FOUR SPECIES BODIES DO THAT THE BASE DOES NOT.** The Troika line's
	// `0x10278090` (family **Anim**'s slot 482) reaches the same gate and answers a flat
	// `XOR EAX, EAX`; these four answer `((m_NPCState != 4) - 1) & result`, so a body in retail
	// state **4** keeps its 1-or-2 where the base would have refused. 29c's walk reads the four as
	// "byte-identical to `0x1035fd40`", which is true of the four but NOT of the base — that claim
	// is corrected here and in `docs/vtmb/npc-ai/shape.md`.
	//
	// Retail state 4 is `NPC_STATE_SCRIPT`. This image's `NPC_STATE` ordinals are NOT SDK 2013's:
	// `docs/vtmb/npc-ai/conditions-and-states.md` § "The species `SelectIdealState` overrides —
	// `0x10369060`, `0x103945a0`, `0x1039e310` (2026-09-13)" pins them off `0x1027e660`'s name table
	// and `0x1026e3e0`'s switch as **1 IDLE, 2 COMBAT, 3 ALERT, 7 DEAD**, which is why the `== 3`
	// escape is ALERT and the `== 4` survivor is SCRIPT. Families
	// Anim, Conditions and Sounds each keep a private copy of that mapping; this is a fourth, for
	// the same reason they are separate — no family owns another's file.
	//
	// This runtime's `EElysiumNpcState` has no NONE, so retail's `!= NONE && != IDLE` collapses to
	// `!= Idle`: both retail states take the same arm, so no answer changes.
	const int32 RetailState = BossesRetailNpcState(State);
	const int32 RetailIdealState = BossesRetailNpcState(IdealState);
	if (!bDisregardState && RetailState != 0 && RetailState != 1 && RetailIdealState != 1
		&& (RetailState != 3 || InterruptLevel < 1))
	{
		return RetailState == 4 ? Result : 0;
	}
	return Result;
}

int32 FElysiumNpc::CanPlaySequenceSpecies(bool bDisregardState, int32 InterruptLevel) const
{
	// The head, in retail's order, and identical to the Troika line's:
	//     result = 1;
	//     if (m_hCine resolves to a live entity) {
	//         if (!CineAllowsInterrupt(m_hCine)) return 0;       // 0x101a8ac0
	//         result = 2;
	//     }
	//     if (!IsAlive()) return 0;                              // slot 158 (+0x278)
	//     <the state arm, which is where these four diverge from the base>
	//
	// `ScriptOwnerIsLive` and `CineAllowsDynamicInteraction` are family **Anim**'s ports of the same
	// two reads (`ElysiumNpcKernelAnim.cpp`), and `IsAlive` is family **Lifecycle**'s slot 158; all
	// three are called rather than re-stated, so the head has ONE answer across both bodies.
	int32 Result = 1;
	if (ScriptOwnerIsLive())
	{
		if (!CineAllowsDynamicInteraction())
		{
			return 0;
		}
		Result = 2;
	}
	if (!const_cast<FElysiumNpc*>(this)->IsAlive())
	{
		return 0;
	}
	return CanPlaySequenceStateArm(Result, bDisregardState, InterruptLevel, Mind.State(),
		Mind.IdealState());
}

// -------------------------------------------------------------------------------------------------
// Slot 601's `CNPC_VAndreiBlood`-line body — `0x10385cf0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_10385cf0()
{
	// `0x10385cf0`, in retail's order and with the argument ignored, as retail ignores it:
	//     (*DAT_10924edc)->vfunc1();                          // the global melee-left event, FIRST
	//     m_bInMelee = 0;                                     // +0x6078
	//     if (HasUsableRangedWeapon())                        // slot 308 (+0x4d0)
	//         m_flMeleeCanEnterTimer = curtime + RandomFloat(5.0, 10.0);   // +0x6070
	//     ReleaseMeleeSlot(m_pAttackCoordinator, this);       // 0x1025ddd0, NO null guard
	//
	// Two DIFFERENCES from the Troika line's `0x102b5880`, both recovered and both ported: the
	// global event fires here and not there, and the coordinator forward is unguarded here while the
	// Troika body tests `m_pAttackCoordinator != 0` first. The unguarded forward is retail's, and on
	// the Troika line the same call site is guarded — that asymmetry is the fact, not a slip.
	//
	// The `RandomFloat(5.0, 10.0)` draw is NOT taken here: this substrate's melee timer is owned by
	// the Troika-line body 29d will land, the event and the release are seams, and a draw taken on
	// a path whose consumer does not exist would walk the schedule stream off the map. The arm that
	// WOULD draw is reproduced as the `bRangedArm` record below so the gate is measurable.
	// Slot 308 `HasUsableRangedWeapon` (`0x10336d70`) is still a generated stub answering false, so
	// the timer arm is not reached today; it is wired, not inlined, so the day the slot lands the
	// arm opens without a change here.
	++MeleeEventFires;
	bInMelee = false;
	if (HasUsableRangedWeapon())
	{
		// `m_flMeleeCanEnterTimer = curtime + RandomFloat(5.0, 10.0)` (`0x40a00000`, `0x41200000`).
		MeleeCanEnterTimer = (World != nullptr ? World->NowSeconds() : 0.0) + 5.0;
	}
	++MeleeCoordinatorReleases;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VManBat`'s four flap activities — `0x1038e640`, `0x1038e670`, `0x1038e6a0`, `0x1038e6e0`.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FFlapActivity* FElysiumNpc::FlapActivityRows(int32& OutCount)
{
	// Four 32/35-byte bodies that are one behaviour. The durations are DOUBLES in `.rdata`, read at
	// their cited addresses: `_DAT_104bc690` = 2.3, `_DAT_10449148` = 4.0, `_DAT_10449198` = 0.2 —
	// and the last two rows share that one cell, which is why two sibling activity ids get the same
	// timer.
	static constexpr FFlapActivity Rows[] =
	{
		{ TEXT("0x1038e640"), 0x22, 2.3f },     // _DAT_104bc690
		{ TEXT("0x1038e670"), 0x24, 4.0f },     // _DAT_10449148
		{ TEXT("0x1038e6a0"), 0x116d, 0.2f },   // _DAT_10449198
		{ TEXT("0x1038e6e0"), 0x116e, 0.2f },   // _DAT_10449198, the same cell
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FFlapActivity* FElysiumNpc::FlapActivityOf(const TCHAR* Body)
{
	if (Body == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FFlapActivity* Rows = FlapActivityRows(Count);
	for (int32 i = 0; i < Count; ++i)
	{
		if (FCString::Strcmp(Rows[i].Body, Body) == 0)
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

void FElysiumNpc::SetFlapActivity(int32 InActivityNumber, float Seconds)
{
	// All four bodies, verbatim:
	//     SetIdealActivity(act);                              // 0x10272650
	//     m_flFlapTimer = gpGlobals->curtime + T;             // +0x6678
	// `SetIdealActivityNumber` is family **Facing**'s port of `0x10272650`; this family reads it
	// rather than standing a second writer of `m_IdealActivity`.
	SetIdealActivityNumber(InActivityNumber);
	ManBatFlapTimer = (World != nullptr ? World->NowSeconds() : 0.0) + static_cast<double>(Seconds);
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VManBat`'s obstacle probe — `0x1038bec0`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_1038bec0(const FVector& DirUnits, float Speed, FVector& OutSteerUnits)
{
	// `0x1038bec0`, arm for arm:
	//     filter = CTraceFilterSimple(this, GetIgnoreCollisionEntity(), m_CollisionGroup (+0x368));
	//     d   = Speed * dir * 0.1;                             // _DAT_104491b4
	//     UTIL_TraceHull(GetAbsOrigin(), GetAbsOrigin() + d,
	//                    m_Collision->OBBMins(), m_Collision->OBBMaxs(), 0x202400b, filter, &tr);
	//     if (tr.fraction < 1.0) {                             // _DAT_104454c0
	//         out = (0, 0, 1.0);
	//         if (m_Activity (+0x0fec) != 0x22) m_flFlapTimer (+0x6678) = curtime;
	//         return true;
	//     }
	//     out = vec3_origin;
	//     return false;
	//
	// The steer is a LITERAL straight-up unit vector, not a reflection off the hit normal, and the
	// timer stamp is `curtime` itself rather than a deadline — both are retail's and both matter to
	// the caller, which multiplies the steer by its chosen speed.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);

	const FVector StartUnits = Origin / ElysiumMove::U;
	const FVector EndUnits = StartUnits + DirUnits * (Speed * ManBatProbeScale);
	FKernelHullTrace Trace;
	KernelHullTrace(StartUnits, EndUnits, MinsUnits, MaxsUnits, ManBatProbeMask, Trace);
	if (Trace.Fraction < BossesOne)
	{
		OutSteerUnits = FVector(0.0, 0.0, 1.0);
		if (ActivityNumber != ActivityFlapStamp)
		{
			ManBatFlapTimer = World != nullptr ? World->NowSeconds() : 0.0;
		}
		return true;
	}
	// `DAT_1070d1b0/b4/b8` is `vec3_origin`, filled at runtime with zeroes.
	OutSteerUnits = FVector::ZeroVector;
	return false;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VManBat`'s velocity producer — `0x1038b370`.
// -------------------------------------------------------------------------------------------------

FElysiumNpc::FManBatStationaryWatch& FElysiumNpc::ManBatStationaryWatch()
{
	return GManBatWatch;
}

void FElysiumNpc::ResetManBatStationaryWatch()
{
	GManBatWatch = FManBatStationaryWatch();
}

void FElysiumNpc::FUN_1038b370(float Interval, FVector& OutVelocityUnits)
{
	// `0x1038b370`, arm for arm. The three shapes are chosen by `m_pFlyNode` (+0x6688) and by the
	// DECODED `m_iMoveGoalNodeMode` (+0x6668); the encoded compares against `0xfa0b069a` and
	// `0xfa0b069b` decode to 6 and 7, which the family's standing facts record.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FVector MyOriginUnits = Origin / ElysiumMove::U;

	// --- Shape one: no fly node and a mode that is neither 6 nor 7. The animation-driven velocity,
	//     steered by the obstacle probe.
	const bool bHasFlyNode = World != nullptr && World->Resolve(ManBatFlyNode) != nullptr;
	if (!bHasFlyNode && ManBatMoveGoalNodeMode != ManBatMoveGoalNodeModeChase
		&& ManBatMoveGoalNodeMode != ManBatMoveGoalNodeModeFlyBy)
	{
		// `if (m_iEFlags (+0x268) & 0x1000) CalcAbsoluteVelocity();` then `m_vecAbsVelocity`'s X and
		// Y — and a Z SEEDED WITH THE LITERAL 10.0, not the velocity's own Z. The length the probe
		// is handed therefore always carries that 10, which is retail's and is reproduced.
		const FVector Abs = AbsVelocityUnits();
		FVector Probe(Abs.X, Abs.Y, ManBatVelocityProbeZ);
		const float Speed = static_cast<float>(Probe.Size());
		FVector Steer = FVector::ZeroVector;
		if (FUN_1038bec0(BossesNormalize(Probe), Speed, Steer))
		{
			Probe = Steer * Speed;
		}
		OutVelocityUnits = Probe;
		return;
	}

	// --- The four activities that answer a dead stop outright.
	if (ActivityNumber == ActivityStillZero30 || ActivityNumber == ActivityStillZeroB0
		|| ActivityNumber == ActivityStillZero4B || ActivityNumber == ActivityStillZero1171)
	{
		OutVelocityUnits = FVector::ZeroVector;
		return;
	}

	// --- The stationary watchdog, a LEVEL-WIDE static and not a per-NPC word.
	FManBatStationaryWatch& Watch = ManBatStationaryWatch();
	if (Watch.PositionUnits == MyOriginUnits)
	{
		if (Now - Watch.SinceTime > ManBatStationarySeconds)
		{
			// Force mode 2 and a fresh node id, run the hint search, then put both back. The save
			// and restore are retail's: the search reads the mode and the id, and nothing else may
			// observe the forced pair.
			const int32 SavedMode = ManBatMoveGoalNodeMode;
			const int32 SavedNodeId = ManBatMoveGoalNodeId;
			ManBatMoveGoalNodeMode = ManBatMoveGoalNodeModeTeleport;
			// `m_iMoveGoalNodeID = RandomInt(1, 3)` (`(*DAT_1070b244 + 8)(1, 3)`). NOT DRAWN here:
			// the only consumer of the forced id is the hint search, which is a seam and reads
			// nothing, and a draw whose result no consumer observes would walk the shared RNG stream
			// off the map for every other system. The pair is saved and restored exactly as retail
			// does, so what IS observable — that neither word survives the search — is reproduced.
			FElysiumEntity* Hint =
				ManBatFindMoveGoalHint(ManBatTeleportHintType, ManBatTeleportHintRangeUnits);
			ManBatMoveGoalNodeMode = SavedMode;
			ManBatMoveGoalNodeId = SavedNodeId;
			if (Hint == nullptr)
			{
				OutVelocityUnits = FVector::ZeroVector;
				return;
			}
			// Both emitters, in retail's order: mine first, the hint's second, then the teleport —
			// which moves me to MY OWN saved origin, not to the hint's. That is what the body does.
			const FVector MineAbsUnits = Origin / ElysiumMove::U;
			PlaceNamedEmitter(ManBatTeleportEmitter, MineAbsUnits);
			PlaceNamedEmitter(ManBatTeleportEmitter, Hint->Origin / ElysiumMove::U);
			bManBatTeleportRequested = true;
			ManBatTeleportPositionUnits = MineAbsUnits;
		}
	}
	else
	{
		Watch.SinceTime = Now;
		Watch.PositionUnits = MyOriginUnits;
	}

	// --- The destination, by mode.
	FVector DestUnits = FVector::ZeroVector;
	bool bPlainVelocity = false;
	if (ManBatMoveGoalNodeMode == ManBatMoveGoalNodeModeChase)
	{
		const FElysiumEntity* Player = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer)
			: nullptr;
		if (Player == nullptr)
		{
			// Retail dereferences the resolved pointer without a check here. The port refuses
			// instead; stated, not hidden.
			bPlainVelocity = true;
		}
		else
		{
			DestUnits = Player->Origin / ElysiumMove::U;
			DestUnits.Z += ManBatChaseHeight;       // _DAT_1046eca8 = 150.0
		}
	}
	else if (ManBatMoveGoalNodeMode == ManBatMoveGoalNodeModeFlyBy)
	{
		FElysiumEntity* Fly = World != nullptr ? World->Resolve(ManBatFlyByTarget) : nullptr;
		// `IsAlive()` (slot 158) on the resolved target; a dead or missing one falls straight to the
		// plain velocity.
		if (Fly == nullptr || !BossesEntityIsAlive(Fly))
		{
			bPlainVelocity = true;
		}
		else
		{
			DestUnits = Fly->Origin / ElysiumMove::U;
			FVector FlyMins = FVector::ZeroVector;
			FVector FlyMaxs = FVector::ZeroVector;
			RetailCollisionExtents(*Fly, FlyMins, FlyMaxs);
			DestUnits.Z += static_cast<float>(FlyMaxs.Z - FlyMins.Z) + ManBatFlyByHeightPad;
			// `m_pNavigator->+8 = m_eHull (+0x156c); m_pNavigator->+0xc = gpGlobals->frametime;`
			// then the reachability probe. Family Motor stands the navigator seam; the two scratch
			// writes have no counterpart and are not reproduced.
			if (!NavigatorCanReach(DestUnits))
			{
				TaskFail(ManBatUnreachableFailure);
				bPlainVelocity = true;
			}
		}
	}
	else
	{
		const FElysiumEntity* Node = World != nullptr ? World->Resolve(ManBatFlyNode) : nullptr;
		if (Node == nullptr)
		{
			bPlainVelocity = true;
		}
		else
		{
			DestUnits = Node->Origin / ElysiumMove::U;
		}
	}

	if (bPlainVelocity)
	{
		// `LAB_1038b93a`: the dirty-velocity recalculate and then `m_vecAbsVelocity` verbatim — all
		// three components this time, unlike the shape-one probe.
		OutVelocityUnits = AbsVelocityUnits();
		return;
	}

	// --- The homing arm.
	FVector Delta = DestUnits - MyOriginUnits;
	const float DistToGoal = static_cast<float>(Delta.Size());
	// 700 units, or 500 when the goal is not meaningfully below me. `_DAT_10462868` is -30.0, and
	// the test is on the RAW delta Z before normalization.
	const float Speed = (Delta.Z >= ManBatSlowZThreshold) ? ManBatSlowSpeed : ManBatFastSpeed;
	Delta = BossesNormalize(Delta);

	FVector Steer = FVector::ZeroVector;
	if (FUN_1038bec0(Delta, Speed, Steer))
	{
		OutVelocityUnits = Steer * Speed;
	}
	else
	{
		const FVector Want = Delta * Speed;
		const FVector Current = Velocity / ElysiumMove::U;   // slot 198 `GetLocalVelocity`
		FVector D = Want - Current;
		const float Accel = ManBatAccelerationCvar() * Interval;
		// X and Y clamp symmetrically; Z clamps UP at `Accel` and DOWN at `Accel * 3.0` — the
		// asymmetry is retail's, and the down limit's sign is applied to the product, not to the
		// clamp, so a zero cvar makes every limit zero.
		D.X = FMath::Min(D.X, static_cast<double>(Accel));
		D.X = FMath::Max(D.X, static_cast<double>(-Accel));
		D.Y = FMath::Min(D.Y, static_cast<double>(Accel));
		D.Y = FMath::Max(D.Y, static_cast<double>(-Accel));
		D.Z = FMath::Min(D.Z, static_cast<double>(Accel));
		D.Z = FMath::Max(D.Z, static_cast<double>(-(Accel * ManBatDownAccelScale)));
		OutVelocityUnits = Current + D;
	}

	// --- The overspeed latch. Retail compares the distance it measured BEFORE normalizing against
	//     the length of the velocity it just produced, scaled by 0.2 (`_DAT_10449198`, a double).
	if (Interval > BossesZero)
	{
		const float NewLen = static_cast<float>(OutVelocityUnits.Size());
		if (DistToGoal < NewLen * ManBatOverspeedScale)
		{
			bManBatReachedMoveGoal = true;        // +0x6664 m_bReachedMoveGoal
		}
	}
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VManBat`'s slot 102 trace filter — `0x1038fb20`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::RetailNameMatches(const FString& EntityName, const TCHAR* NameOrWildcard)
{
	// The `NameMatches` idiom `0x10006c4e` inlines, as the listing spells it. `uVar4` is
	// `strlen(pattern) + 1`:
	//     if (uVar4 == 1)                      -> the answer is `m_iName == NULL_STRING`
	//     else if (pattern[strlen - 1] == '*') -> strnicmp(name, pattern, strlen - 1) == 0
	//     else                                 -> stricmp(name, pattern) == 0
	// So an EMPTY pattern matches only an unnamed entity, and the bare `"*"` matches everything
	// (its `strnicmp` length is zero). A null name is compared as the empty string, which is
	// retail's `DAT_106b8540` substitution.
	if (NameOrWildcard == nullptr)
	{
		return false;
	}
	const int32 PatternLength = FCString::Strlen(NameOrWildcard);
	if (PatternLength == 0)
	{
		return EntityName.IsEmpty();
	}
	if (NameOrWildcard[PatternLength - 1] == TEXT('*'))
	{
		return FCString::Strnicmp(*EntityName, NameOrWildcard, PatternLength - 1) == 0;
	}
	return FCString::Stricmp(*EntityName, NameOrWildcard) == 0;
}

bool FElysiumNpc::ManBatTraceFilterShouldHit(const FString& EntityName)
{
	// `CTraceFilterManBatNoIBeamEntity::ShouldHitEntity` `0x10006c4e`: an entity whose `m_iName`
	// (+0x26c) matches `"lbeam*"` (`DAT_10642d28`, read from the image) is NOT hit; anything else
	// falls through to `CTraceFilterSimple::ShouldHitEntity`, whose own answer is the ordinary
	// collision-group test and is true for the candidates this filter is asked about.
	return !RetailNameMatches(EntityName, ManBatNoHitName);
}

void FElysiumNpc::PhysicsTraceEntityManBat(FElysiumEntity* Entity, const FVector& StartUnits,
	const FVector& EndUnits, uint32 Mask)
{
	// `0x1038fb20`:
	//     collideable = entity->GetCollideable();                      // +0x8
	//     group       = collideable->GetCollisionGroup();              // +0x38
	//     CTraceFilterSimple filter(entity, group);                    // 0x101ccf50
	//     *(void**)&filter = &vftable_CTraceFilterManBatNoIBeamEntity; // the whole point
	//     ray = collideable->SetupRay(0, end, &filter, out);           // +0x24
	//     enginetrace->SweepCollideable(collideable, entity, start, ray);   // (*DAT_1070b254)+0x14
	// The vtable swap is the recovered concern and `ManBatTraceFilterShouldHit` is its content; the
	// sweep itself is a seam and records the call.
	PhysicsTraceEntityCalls.Add(FPhysicsTraceEntityCall{
		Entity != nullptr ? Entity->Handle : FElysiumEntityHandle::Invalid(),
		StartUnits, EndUnits, Mask });
}
