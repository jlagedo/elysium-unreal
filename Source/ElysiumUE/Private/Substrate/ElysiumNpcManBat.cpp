#include "Substrate/ElysiumNpcManBat.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcBossesShared.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcLifecycle2_2Shared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcScheduleShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcSoundsShared.h"
#include "Substrate/ElysiumNpcSpeciesMisc10Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr float BossesOne = ElysiumNpcTunables::One;
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
	// `VectorNormalize` `0x10137220`: `1.0 / (FLT_EPSILON + length)`, so a zero vector normalizes to
	// zero rather than to NaN and a unit vector comes back a hair short. Both are observable, and
	// both are reproduced.
	constexpr float BossesNormalizeEpsilon = ElysiumNpcTunables::FloatEpsilon;
	FVector BossesNormalize(const FVector& V)
	{
		const float Scale = BossesOne / (BossesNormalizeEpsilon + static_cast<float>(V.Size()));
		return V * Scale;
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
	constexpr float ThrownModelThinkDelay = 20.0f;  // _DAT_1044eb0c
	// `CNPC_VManBat`'s five name templates, at their `.rdata` addresses in the pinned image. Read out
	// of `vampire.dll` at `address - 0x10000000` because the corpus's string table does not hold the
	// two that no decompiled body names as a literal.
	// `ManBat Landpoint` (`0x10642c74`) is the only one without a `%d`; the four formats are spelled
	// inline at their `FString::Printf` call sites because the engine's checked-format-string
	// sanitiser only accepts a literal there. Each carries its `.rdata` address at the call.
	const TCHAR* const GHints10ManBatLandpoint = TEXT("ManBat Landpoint");            // 0x10642c74
	constexpr double GMiscZeroDouble = ElysiumNpcTunables::ZeroDouble;
	//
	// The model table at `0x10640ce0` is FOUR entries and the corpus pins only two of the four
	// strings. Entries 2 and 3 are recorded by table index, the convention family Lifecycle used
	// for `PTR_s_weapons_ar2_ar2_fire1_wav_106244c0`'s unnamed tail.
	const TCHAR* const GManBatThrowModels[] = {
		TEXT("models/character/monster/manbat/Throw_Objects/ThrowTaxi.mdl"),
		TEXT("models/character/monster/manbat/Throw_Objects/supportb.mdl"),
		TEXT("PTR_0x10640ce0[2]"),   // unrecovered
		TEXT("PTR_0x10640ce0[3]"),   // unrecovered
	};
	const TCHAR* const GManBatEmitters[] = {
		TEXT("Manbat_screechcone_emitter"),
		TEXT("Manbat_player_emitter"),
		TEXT("HUD_Manbat_emitter"),
		TEXT("Manbat_blast_player"),   // the one of the four with no `_emitter` suffix
	};
	const TCHAR* const GManBatWingflaps[] = {   // 0x10640d10, to 0xc
		TEXT("character/male/sheriff_manbat/wingflap_1.wav"),
		TEXT("character/male/sheriff_manbat/wingflap_2.wav"),
		TEXT("character/male/sheriff_manbat/wingflap_3.wav"),
	};
	const TCHAR* const GManBatExerts[] = {      // 0x10640d1c, to 0xc
		TEXT("character/male/sheriff_manbat/exert_heavy_1.wav"),
		TEXT("character/male/sheriff_manbat/exert_heavy_2.wav"),
		TEXT("character/male/sheriff_manbat/exert_heavy_3.wav"),
	};
	const TCHAR* const GManBatFlyBys[] = {      // 0x10640d28, to 0xc
		TEXT("character/male/sheriff_manbat/fly_by_1.wav"),
		TEXT("character/male/sheriff_manbat/fly_by_2.wav"),
		TEXT("character/male/sheriff_manbat/fly_by_3.wav"),
	};
	const TCHAR* const GManBatScreech = TEXT("character/male/sheriff_manbat/screech.wav");
	const TCHAR* const GManBatFall = TEXT("character/male/sheriff_manbat/fall.wav");
	const TCHAR* const GManBatWeapon = TEXT("item_w_manbat_claw");
	// `_DAT_1044fab0` — a **DOUBLE**, `0.0` (`103c1db6` is `FCOMP double ptr [0x1044fab0]`). The "no
	// slow running" sentinel the ManBat and the head claw both compare their expiry against.
	constexpr double GSlowExpireSentinel = ElysiumNpcTunables::ZeroDouble;
	// The ManBat cone's screen shake (`1038ea4b`..`1038ea60`) and its slow restamp (`1038eb85`).
	constexpr float GManBatShakeAmplitude = 2.5f;
	constexpr float GManBatShakeFrequency = 0.2f;    // 0x3e4ccccd
	constexpr float GManBatShakeDuration = 3.0f;     // 0x40400000
	constexpr float GManBatShakeRadius = 0.f;
	constexpr float GManBatSlowSecondsMin = 15.f;    // 0x41700000
	constexpr float GManBatSlowSecondsMax = 25.f;    // 0x41c80000
	// `1038eb5e` / `103c1dca` — `BeginSlowEntity`/`EndSlowEntity`'s only argument in this image.
	constexpr float GSlowEntityMagnitude = 500.f;    // 0x43fa0000
}

// `CNPC_VManBat`'s pickup row (0019 story 5 commit B moved it onto the class: it was a row of a
// class-keyed table read by retail class name). The release re-arms `0x102c43b0`'s collision
// ignore at 2.0 s.
const FElysiumNpc::FPickupSpecies& FElysiumNpcManBat::PickupRow()
{
	static constexpr FPickupSpecies Row = { TEXT("CNPC_VManBat"), TEXT("0x1038f430"), TEXT("0x1038f790"),
		TEXT("Bip01_R_Foot"), 0x668c, 2.0f, true };
	return Row;
}

bool FElysiumNpcManBat::AttachPickupAnimlink(FElysiumEntity* Carried, int32 ElementKey)
{
	// `0x1038f430`.
	int32 Bone = INDEX_NONE;
	const FElysiumEntityHandle Link = BeginPickupLink(PickupRow(), Carried, Bone);
	if (!Link.IsSet() || !FinishPickupLink(Link, Bone, Carried, ElementKey))
	{
		return false;
	}
	ManBatPhysicsAnimlink = Link;
	NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);    // 0x1038f600(this, true)
	bManBatPickupTargetBreakable = IsCarriedBreakable(Carried);
	SetCarriedBreakable(Carried->Handle, false);
	return true;
}

void FElysiumNpcManBat::ReleasePickupAnimlink(const FElysiumEntity* AimTarget)
{
	// `0x1038f790` ignores its argument and aims at the cached closest player; the carried word is
	// cleared AFTER the re-arm, which is why `StartIgnoringCollision` still resolves it.
	(void)AimTarget;
	const FElysiumEntity* Aim = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (ReleasePickupLink(ManBatPhysicsAnimlink, ManBatPickupTarget, Aim))
	{
		SetCarriedBreakable(ManBatPickupTarget, bManBatPickupTargetBreakable);
	}
	StartIgnoringCollision(ManBatPickupTarget);
	ArmIgnoreCollisionExpiry(PickupRow().IgnoreCollisionSeconds);
	ManBatPickupTarget = FElysiumEntityHandle::Invalid();
	NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);  // 0x1038f600(this, false)
}

// `CNPC_VManBat`'s constructor `0x10389cc0` writes both hull words at `0x10389d56`, after the
// `CAI_BaseNPC` constructor `0x1027c300` zeroed both; the port's constructor chain runs in the same
// order.
FElysiumNpcManBat::FElysiumNpcManBat()
{
	HullKind = 20;
	PathingHullKind = 20;
}

// Slot 420: `0x1038b070`.
// `0x1038b070`
void FElysiumNpcManBat::NPCInit()
{
	const double Now = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);
	ManBatFlapTimer = Now + ManBatFlapDelaySeconds;                      // BEFORE the base
	bManBatHasScaredMinions = false;
	bHasPlayedFlyBySound = false;
	ManBatFlyTimer = Now + NpcInitThinkDelay;
	TroikaNPCInit();
	NodeGraphHullIndex() = HullIndexManBat;
}

// Slot 104: `0x1038aec0`.
// 0x1038aec0
void FElysiumNpcManBat::Precache()
{
	// `CNPC_VManBat::Precache` `0x1038aec0` — 280 bytes, the longest arm here: the Troika body, the
	// four-entry throw-object model table with preload 0, four preload-1 emitters (the exact four
	// the screech-cone body `0x1038e9c0` spawns), three 0xc-byte sound tables, two singles,
	// `sheriff_teleport_emitter` TWICE in a row — a retail duplicate that is kept — and the claw.
	TroikaPrecache();
	for (const TCHAR* ThrowModel : GManBatThrowModels)
	{
		NpcKernelPrecache10Shared::Precache10Model(*this, ThrowModel, /*Preload=*/0);
	}
	for (const TCHAR* Emitter : GManBatEmitters)
	{
		NpcKernelPrecache10Shared::Precache10Particle(*this, Emitter, /*Preload=*/1);
	}
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GManBatWingflaps, UE_ARRAY_COUNT(GManBatWingflaps));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GManBatExerts, UE_ARRAY_COUNT(GManBatExerts));
	NpcKernelPrecache10Shared::Precache10SoundTable(*this, GManBatFlyBys, UE_ARRAY_COUNT(GManBatFlyBys));
	NpcKernelPrecache10Shared::Precache10Sound(*this, GManBatScreech);
	NpcKernelPrecache10Shared::Precache10Sound(*this, GManBatFall);
	NpcKernelPrecache10Shared::Precache10Particle(*this, NpcKernelPrecache10Shared::GSheriffTeleportEmitter, /*Preload=*/1);
	NpcKernelPrecache10Shared::Precache10Particle(*this, NpcKernelPrecache10Shared::GSheriffTeleportEmitter, /*Preload=*/1);
	NpcKernelPrecache10Shared::Precache10Other(*this, GManBatWeapon);
}

// Slot 438: `0x1038e340`, which replaces the whole selector (the Troika selector's species hook).
// Slot 438: `0x1038e340`, the body of its class's `SpeciesSelectSchedule` override (story 5 step 3).
int32 FElysiumNpcManBat::SpeciesSelectSchedule()
{
	// CNPC_VManBat. `GetGoalType`-shaped navigator probe first; anything but 2 writes
	// `m_iMoveGoalNodeID = 1` and answers 0x158.
	if (NavigatorGoalType() != 2)
	{
		ManBatMoveGoalNodeId = 1;
		return 0x158;
	}
	// The obfuscated equality: `Hash((f & 0x710935 ^ 0x148739) + 0x4094ab & 0x18ef6ca ^ f ^
	// 0x412a96ec) == Hash(0xfa0b0694)`, where `f` is `field_0x6670`, a species word with no port
	// member, and `Hash` is `0x1042fbf0`. Reproduced as the arithmetic it is, over a zero word;
	// the comparison therefore fails, which is the `m_iMoveGoalNodeID = 1` / 0x159 arm.
	constexpr uint32 ManBatWord = 0u;   // SEAM: `CNPC_VManBat +0x6670`
	const uint32 Obfuscated =
		(((((ManBatWord & 0x710935u) ^ 0x148739u) + 0x4094abu) & 0x18ef6cau) ^ ManBatWord)
		^ 0x412a96ecu;
	if (Obfuscated != 0xfa0b0694u)
	{
		ManBatMoveGoalNodeId = 1;
		return 0x159;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions,
		EElysiumNpcCond::HeavyDamage))
	{
		return 0x15f;
	}
	// `(*DAT_1093b814 + 4)()` and `DAT_1093b814[0xb]` — a CNPC_VManBat-owned global object and
	// its eleventh word. UNRECOVERED; the false/non-zero arm is the one that answers 0x15d.
	const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(1, 10);
	switch (Roll)
	{
	case 1:  return 0x15c;
	case 2:  return 0x15d;
	case 3:  return 0x161;
	case 4:
	case 5:
	case 6:  return 0x163;
	default: return 0x159;
	}
}

// Slot 337: `0x1038b100`.
int32 FElysiumNpcManBat::GetUsedHullBits()
{
	// A bare `return 0x100000`: no call up the chain, so the Troika line's bit 0 is absent.
	return 0x100000;
}

// Slot 566: `0x1038e480`, a whole replacement body.
/** `CNPC_VManBat::FValidateHintType` (`0x1038e480`) — the body of the class's override. */
bool FElysiumNpcManBat::FValidateHintType(void* Hint)
{
	// `CNPC_VManBat::FValidateHintType` `0x1038e480`, the body of the class's override.
	// `1038e5b3` — the ManBat body dereferences its hint unconditionally; a null one faults in
	// retail. CRASH GUARD, named: a null hint answers false, which is the arm every other refusal in
	// the body reaches.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);
	return Words != nullptr && ManBatValidateHintType(*Words);
}

// Slot 546: `0x10389f50`, the class's own schedule id space.
const TCHAR* FElysiumNpcManBat::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093b89c`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VManBat"), TEXT("0x10389f50"), TEXT("0x1093b89c") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcBosses.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcManBat::ManBatFindMoveGoalHint(int32 HintType, float RadiusUnits)
{
	// SEAM for `thunk_FUN_102d1af0(this, 20000, 0, 15000.0, 0, 0)`. Family Hints states the whole
	// hint-store gap; this is the entity-answering form `0x1038b370` needs.
	(void)HintType;
	(void)RadiusUnits;
	return nullptr;
}

void FElysiumNpcManBat::PlaceNamedEmitter(const TCHAR* Name, const FVector& PositionUnits)
{
	// SEAM for `thunk_FUN_102c41b0(this, name, &position)`. Recorded so the teleport's two
	// placements and their ORDER are measurable.
	TeleportEmitterPlacements.Add(FTeleportEmitterPlacement{ FString(Name), PositionUnits });
}

FVector FElysiumNpcManBat::AbsVelocityUnits() const
{
	// SEAM for `CBaseEntity::CalcAbsoluteVelocity` plus `m_vecAbsVelocity` (+0x3bc). This runtime
	// keeps one velocity, in centimetres per second, and has no separate absolute copy to
	// recalculate; the conversion to Source units is what every constant in `0x1038b370` is in.
	return Velocity / ElysiumMove::U;
}

bool FElysiumNpcManBat::NavigatorCanReach(const FVector& PositionUnits) const
{
	// SEAM for `thunk_FUN_102f1a20(m_pNavigator, &position, 0x2400b)`. Family Motor states the whole
	// navigator gap. False is retail's refusal, which is the arm that fails the task.
	(void)PositionUnits;
	return false;
}

float FElysiumNpcManBat::ManBatAccelerationCvar() const
{
	// `DAT_1093b7cc` `+0x28`: `manbat_delta`, shipped "600.0".
	return ElysiumNpcTunables::ConVarFloat(ElysiumNpcTunables::EConVar::ManbatDelta);
}

const FElysiumNpc::FFlapActivity* FElysiumNpcManBat::FlapActivityRows(int32& OutCount)
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

const FElysiumNpc::FFlapActivity* FElysiumNpcManBat::FlapActivityOf(const TCHAR* Body)
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

void FElysiumNpcManBat::SetFlapActivity(int32 InActivityNumber, float Seconds)
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

bool FElysiumNpcManBat::FUN_1038bec0(const FVector& DirUnits, float Speed, FVector& OutSteerUnits)
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

FElysiumNpc::FManBatStationaryWatch& FElysiumNpcManBat::ManBatStationaryWatch()
{
	return NpcKernelBossesShared::GManBatWatch;
}

void FElysiumNpcManBat::FUN_1038b370(float Interval, FVector& OutVelocityUnits)
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
	if (Interval > NpcKernelBossesShared::BossesZero)
	{
		const float NewLen = static_cast<float>(OutVelocityUnits.Size());
		if (DistToGoal < NewLen * ManBatOverspeedScale)
		{
			bManBatReachedMoveGoal = true;        // +0x6664 m_bReachedMoveGoal
		}
	}
}

void FElysiumNpcManBat::PhysicsTraceEntityManBat(FElysiumEntity* Entity, const FVector& StartUnits,
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

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

bool FElysiumNpcManBat::ThrowModel(const FString& ModelName, const FString& ThrowParentName)
{
	// 1. `CreateNoSpawn("prop_physics", GetAbsOrigin())`. A failure answers false with nothing done.
	const FElysiumEntityHandle Prop =
		CreateNamedEntity(TEXT("prop_physics"), Origin / ElysiumMove::U);
	if (!Prop.IsSet())
	{
		return false;
	}
	// 2. `DevMsg("ManBat is throwing model %s", model)` — the literal that names the body.
	UE_LOG(LogTemp, Verbose, TEXT("ManBat is throwing model %s"), *ModelName);
	// 3. SetModel(model), Spawn(), LookupBone(model) — retail passes the SAME string to the bone
	//    lookup that it passed to SetModel, which is a retail quirk and is reproduced as written.
	const int32 Bone = LookupBoneByName(*ModelName);
	// 4. `thunk_FUN_10157da0(prop, bone, &info)` makes the corpse-shaped ragdoll, then the template
	//    prop is removed (`thunk_FUN_101cd940`).
	RemoveNamedEntity(Prop);
	(void)Bone;
	// 5. The ragdoll's think is armed at `curtime + 20.0` (`_DAT_1044eb0c`) and, only when a parent
	//    name is given, it is parented and owned by it. Then family Bosses' ManBat animlink arm
	//    attaches it, and its handle lands in `m_hPickupTarget` (+0x668c).
	(void)ThrownModelThinkDelay;
	(void)ThrowParentName;
	// The ragdoll was never created, so the ManBat pickup word stays as it was — retail's own
	// `if (this_01 != 0)` guard, which answers false without writing it.
	return false;
}

// --- Moved from `ElysiumNpcHints10.cpp` (story 5 step 4) ---

uint32 FElysiumNpcManBat::ManBatHintMode(uint32 ScrambledWord)
{
	// `1038e49b`..`1038e4bf`, verbatim:
	//     MOV ECX,[ESI + 0x6670] ; MOV EAX,ECX
	//     AND EAX,0x710935 ; XOR EAX,0x148739 ; ADD EAX,0x4094ab
	//     AND EAX,0x18ef6ca ; XOR EAX,ECX ; XOR EAX,0x412a96ec
	//     CALL 0x1042fbf0
	uint32 Acc = (ScrambledWord & 0x00710935u) ^ 0x00148739u;
	Acc = (Acc + 0x004094abu) & 0x018ef6cau;
	Acc = Acc ^ ScrambledWord ^ 0x412a96ecu;
	return HintObfuscationFold(Acc);
}

FString FElysiumNpcManBat::ManBatHintName(uint32 Mode, int32 Index)
{
	// `1038e4c7 DEC EAX / CMP EAX,0x7 / JA default / JMP [EAX*4 + 0x1038e5c0]`. The jump table holds
	// eight entries for modes 1..8; 5, 6 and 7 point at the default label `1038e51b`. Anything
	// outside 1..8 — including 0 — takes the default too, because the `DEC` makes it wrap above 7.
	switch (Mode)
	{
	case 1:
		// `1038e4f5` — a raw byte-copy loop, NOT a `sprintf`: mode 1 carries no `%d` and the index
		// is never read on this arm.
		return GHints10ManBatLandpoint;
	case 2:
	case 4:
		return FString::Printf(TEXT("ManBat Divepoint %d"), Index);          // 0x10642ca8
	case 3:
		return FString::Printf(TEXT("ManBat Divepoint %d Bottom"), Index);   // 0x10642c88
	case 8:
		return FString::Printf(TEXT("ManBat Script Node %d"), Index);        // 0x10642c58
	default:
		return FString::Printf(TEXT("ManBat %d"), Index);                    // 0x10642c4c
	}
}

bool FElysiumNpcManBat::ManBatValidateHintType(const FHintWords& Hint) const
{
	// `1038e48b CMP [EBX + 0x5dc],0x4e20 / JNZ 0x1038e5b3` — only hint type 20000 is considered.
	if (Hint.HintType != 20000)
	{
		return false;
	}

	const FString Name = ManBatHintName(ManBatHintMode(ManBatHintModeWord), ManBatHintIndex);

	// `1038e534`: `in_EAX = hint->m_iName; if (in_EAX == 0 || in_EAX != local_50) { ... }`. The
	// second test compares the NAME POINTER against the stack buffer's address, which can never be
	// equal, so the compare below always runs. Reproduced as the unconditional compare it is.
	//
	// `1038e556 JNZ` — the `repne scasb` length. When the built name is EMPTY retail does NOT
	// compare strings at all: it loads the hint's name POINTER into `EAX` and tests it for zero at
	// `1038e5a0`. So an unnamed hint matches an empty template and a named one does not. Retail's
	// own arm, reproduced: none of the five templates can produce an empty string, so it is
	// unreachable from `ManBatHintName` and reachable only from a hand-built name in a test.
	if (Name.IsEmpty())
	{
		return Hint.Name.IsEmpty();
	}

	// `1038e560 MOV AL,[ESP + ECX*1 + 0x7] / CMP AL,0x2a` — the LAST character of the template. A
	// `*` takes `__strnicmp` over `strlen - 1` characters (`1038e590 DEC ECX`), which excludes the
	// `*` itself; anything else takes `__strcmpi` over the whole string. A null hint name is
	// replaced by the shared empty literal `DAT_106b8540` on both arms.
	if (Name[Name.Len() - 1] == TEXT('*'))
	{
		const FString Prefix = Name.Left(Name.Len() - 1);
		return Hint.Name.Left(Prefix.Len()).Equals(Prefix, ESearchCase::IgnoreCase);
	}
	return Hint.Name.Equals(Name, ESearchCase::IgnoreCase);
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

bool FElysiumNpcManBat::SlowedExpire() const
{
	// `0x1038f290` — the whole body is `return (float)_DAT_1044fab0 < m_flSlowedExpire (+0x6684)`.
	//
	// `_DAT_1044fab0` is the shared `0.0` DOUBLE (`docs/vtmb/footsteps.md` § the 2-D speed gate), so
	// this is a compare against ZERO and NOT against curtime: `m_flSlowedExpire` is read here as a
	// FLAG spelled as a float, not as a deadline. A body that read it as a deadline would answer the
	// opposite for every ManBat whose slow has not been armed.
	return static_cast<double>(ManBatSlowedExpire) > GMiscZeroDouble;
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSounds.cpp` (story 5 step 4) ---

// `FUN_10390040` (`0x10390040`). Eight bytes: `*(bool*)(this + 0x66b8) = false`.
//
// UNRECOVERED: the body has NO caller anywhere in the image and nothing sets the byte either, so the
// fly-by sound this latch exists for was cut or is emitted through a path the census does not reach.
// The reset is ported verbatim; the latch has no producer and no consumer.
void FElysiumNpcManBat::ClearHasPlayedFlyBySound()
{
	bHasPlayedFlyBySound = false;
}

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 4) ---

void FElysiumNpcManBat::ManBatStartScreechCone(FElysiumEntity* ConeTarget)
{
	// `1038e9d2`: when the cone emitter at `+0x66a4` still resolves, `UTIL_Remove` it and null the
	// handle. Retail nulls it through `0x100a0ae0(handle, 0)`, so a stale handle is left alone.
	if (World != nullptr && World->Resolve(ManBatScreechCone) != nullptr)
	{
		RemoveNamedEntity(ManBatScreechCone);
		ManBatScreechCone = FElysiumEntityHandle::Invalid();
	}
	// `1038ea01`: spawn `Manbat_screechcone_emitter` at MY slot-217 origin, store the handle, attach
	// it at `Bip01 Jaw` with mode 1 and start it. The attach/start pair is guarded on the fresh
	// handle resolving, which a failed spawn does not.
	const int32 ConeIndex = CreateNamedEmitter(TEXT("Manbat_screechcone_emitter"),
		Origin / ElysiumMove::U, /*AttachMode*/ 1, Handle, TEXT("Bip01 Jaw"));
	StartNamedEmitter(ConeIndex);

	// `1038ea33`: everything below runs only for a non-null target that carries a PLAYER record.
	if (ConeTarget == nullptr || !NpcKernelSpeciesMisc10Shared::SpeciesMisc10IsPlayer(*this, ConeTarget))
	{
		return;
	}
	// `1038ea60`: `UTIL_ScreenShake(slot 220 GetOrigin(), 2.5, 0.2, 3.0, 0.0, 0, 0)` around MY OWN
	// origin — `vt+0x370` is slot 220 on `this`, not on the target.
	// The one `UTIL_ScreenShake` (`0x101cdba0`) recorder, `RecordAnimEventShake`
	// (`ElysiumNpcMisc2.inl`); `(…, 0, 0)` is no air shake.
	RecordAnimEventShake(Origin / ElysiumMove::U, GManBatShakeAmplitude, GManBatShakeFrequency,
		GManBatShakeDuration, GManBatShakeRadius, false);

	// `1038ea69`: `param_1[0x27]` is `+0x9c`, the target's combat-character self-downcast — null for
	// anything that is not a combat character, and the whole rest of the body is gated on it.
	FElysiumCombatCharacter* Victim = ConeTarget->AsCombatCharacter();
	if (Victim == nullptr)
	{
		return;
	}
	// `1038eb2c`: `0x10344f80(victim, target.slot217 - my.slot217, 2, 0)` — the push, BEFORE the
	// slow. Both origins are slot 217.
	PushEntityCalls.Add(FPushEntityCall{ ConeTarget->Handle,
		(ConeTarget->Origin - Origin) / ElysiumMove::U, 2 });
	// `1038eb43`: `BeginSlowEntity(victim, 500.0)` only while the expiry still equals the 0.0
	// sentinel, so a second cone inside the window does not re-begin the slow.
	if (static_cast<double>(ManBatSlowedExpire) == GSlowExpireSentinel)
	{
		BeginSlowEntity(ConeTarget->Handle, GSlowEntityMagnitude);
	}
	// `1038eb85`: restamp unconditionally — `curtime + RandomFloat(15.0, 25.0)`.
	ManBatSlowedExpire = static_cast<float>(NpcKernelSpeciesMisc10Shared::SpeciesMisc10Now(*this)
		+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(GManBatSlowSecondsMin,
			GManBatSlowSecondsMax));
	// `1038eb9a`: cache the VICTIM's handle (the combat character's `vt+4` accessor, which is the
	// entity itself — family Senses10's standing fact one).
	ManBatSlowedEntity = ConeTarget->Handle;

	// `1038ebb0`: `Manbat_player_emitter` at the TARGET's origin, `Bip01 Spine` mode 1, started —
	// only when `+0x669c` holds nothing.
	if (World == nullptr || World->Resolve(ManBatPlayerEmitter) == nullptr)
	{
		const int32 Index = CreateNamedEmitter(TEXT("Manbat_player_emitter"),
			ConeTarget->Origin / ElysiumMove::U, /*AttachMode*/ 1, ConeTarget->Handle, TEXT("Bip01 Spine"));
		StartNamedEmitter(Index);
	}
	// `1038ec4a`: the same shape for `Manbat_blast_player` at `+0x66a8`, at the SAME level rather
	// than nested inside the block above.
	if (World == nullptr || World->Resolve(ManBatBlastEmitter) == nullptr)
	{
		const int32 Index = CreateNamedEmitter(TEXT("Manbat_blast_player"),
			ConeTarget->Origin / ElysiumMove::U, /*AttachMode*/ 1, ConeTarget->Handle, TEXT("Bip01 Spine"));
		StartNamedEmitter(Index);
	}
	// `1038ed3c`: with the player's inventory slot 0 holding an entity AND `+0x66a0` stale, spawn
	// `HUD_Manbat_emitter`, attach it to THAT ITEM with mode `0xe` and an EMPTY bone name, start it,
	// and set the spawned emitter's `+0x4a1` byte to 1.
	const FElysiumEntity* Item = PlayerInventorySlot0();
	if (Item != nullptr && (World == nullptr || World->Resolve(ManBatHudEmitter) == nullptr))
	{
		const int32 Index = CreateNamedEmitter(TEXT("HUD_Manbat_emitter"), FVector::ZeroVector,
			/*AttachMode*/ 0xe, Item->Handle, TEXT(""));
		StartNamedEmitter(Index);
		// `+0x4a1` on the spawned emitter entity has no port counterpart and no reader in the
		// corpus; the emitter record carries the decision and the byte is recorded as unrecovered.
	}
	// `1038edd2`: whenever the player record stands, `player->+0x2454 |= 1`. It is inside the
	// `+0x9c` block, so a target that is the player but not a combat character never reaches it.
	bPlayerScreechConeBit = true;
}

void FElysiumNpcManBat::ManBatReleaseSlowedEntity(bool bForce)
{
	// `1038f02c` -> `0x1038f290`: act only while `m_flSlowedExpire` is ABOVE the 0.0 sentinel. Family
	// Misc already carries that gate as `SlowedExpire()`; it is called, not restated.
	if (!SlowedExpire())
	{
		return;
	}
	// `1038f047`: forced, or the expiry has reached curtime.
	if (!bForce && !(static_cast<double>(ManBatSlowedExpire) <= NpcKernelSpeciesMisc10Shared::SpeciesMisc10Now(*this)))
	{
		return;
	}
	// `1038f059`: zero the expiry first.
	ManBatSlowedExpire = 0.f;
	// `1038f06e`: `EndSlowEntity(victim->+0x9c, 500.0)` only while the handle resolves to a combat
	// character.
	FElysiumEntity* Victim = World != nullptr ? World->Resolve(ManBatSlowedEntity) : nullptr;
	if (Victim != nullptr && Victim->AsCombatCharacter() != nullptr)
	{
		EndSlowEntity(ManBatSlowedEntity, GSlowEntityMagnitude);
	}
	// `1038f096`, `1038f0e6`, `1038f142`: `UTIL_Remove` the three effects in THIS order — `+0x669c`,
	// `+0x66a8`, `+0x66a0` — each only while its handle resolves, and set each to -1 after.
	if (World != nullptr && World->Resolve(ManBatPlayerEmitter) != nullptr)
	{
		RemoveNamedEntity(ManBatPlayerEmitter);
	}
	ManBatPlayerEmitter = FElysiumEntityHandle::Invalid();
	if (World != nullptr && World->Resolve(ManBatBlastEmitter) != nullptr)
	{
		RemoveNamedEntity(ManBatBlastEmitter);
	}
	ManBatBlastEmitter = FElysiumEntityHandle::Invalid();
	if (World != nullptr && World->Resolve(ManBatHudEmitter) != nullptr)
	{
		RemoveNamedEntity(ManBatHudEmitter);
	}
	ManBatHudEmitter = FElysiumEntityHandle::Invalid();
	// `1038f19c`: clear bit 0 of the victim's `+0xa8` `+0x2454`, only while the handle resolves to
	// an entity that carries a player record.
	if (Victim != nullptr && NpcKernelSpeciesMisc10Shared::SpeciesMisc10IsPlayer(*this, Victim))
	{
		bPlayerScreechConeBit = false;
	}
	// `1038f1e5`: `m_hSlowedEntity = -1`, OUTSIDE that guard — it is cleared even when the victim
	// never resolved.
	ManBatSlowedEntity = FElysiumEntityHandle::Invalid();
}
