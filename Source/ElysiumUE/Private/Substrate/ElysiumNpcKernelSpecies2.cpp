#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Species**, the second half — `CNPC_Crow`, `CNPC_VAndreiBlood`,
// `CNPC_VChangBros`, `CNPC_VMingXiaoTentacle`, `CNPC_VNewscaster`, `CNPC_VTzimisce`'s carry chain,
// `CNPC_VTzimisceHeadClaw`'s slow read, `CNPC_VWerewolf`, `CNPC_VZombie`, `CNPC_VCamera` and
// `CScriptedTarget`. Slot 323, the species slot table, its dispatchers and the melee quartet are
// `Substrate/ElysiumNpcKernelSpecies.cpp`; the declarations are
// `Substrate/ElysiumNpcKernelSpecies.inl`.
//
// As in the first half, every `.rdata` constant below was READ out of the pinned retail
// `vampire.dll` and the value is quoted beside its cell address. A `.data` cell filled at runtime is
// a seam and says so.

namespace
{
	// --- Retail `.rdata`, read out of the pinned image --------------------------------------------

	constexpr float Species2Zero = 0.0f;       // _DAT_104454c4 / _DAT_1044fab0
	constexpr float Species2One = 1.0f;        // _DAT_104454c0

	// `0x10357be0`'s scale clamp. `_DAT_10449280` is **0.0f** as a float (it is `1.0` read as a
	// DOUBLE at the same address, which is how families Damage and BaseHelpers read it; this body's
	// decompiled C casts it to float explicitly, `(float)_DAT_10449280`, so the float is what it
	// compares). The arm is therefore `if (0.0f < scale) scale = 1.0f` — **any positive scale
	// becomes exactly 1.0**, which is a clamp to one and not a clamp at one.
	constexpr float CrowScaleClampFloor = 0.0f;      // (float)_DAT_10449280

	// `0x10357be0`'s speed. The same 170.0 appears twice: as the distance threshold the offset is
	// compared against and as the metres-per-second the offset is scaled by.
	constexpr float CrowFlySpeed = 170.0f;           // _DAT_10454028
	// And a third time, as the literal `0x432a0000` handed to `thunk_FUN_10357e50`.
	constexpr float CrowAvoidRadius = 170.0f;        // 0x432a0000
	// `thunk_FUN_102e1c10(motor, yaw, -2.0)` — the yaw-hold mode the fly step asks for.
	constexpr float CrowMotorYawMode = -2.0f;

	// `CNPC_VAndreiBlood`'s runner cap, `_DAT_10452dc4`. TWO bodies threshold on it and both read
	// it as a float against `m_iActiveRunnerCount`: `0x1035e920` (`count < 2`) and `0x1034c2d0`
	// (`2 <= count` refuses).
	constexpr int32 AndreiMaxActiveRunners = 2;      // _DAT_10452dc4 = 2.0f

	// `0x1035e950`'s draw — `(*DAT_1070b244 + 8)(2, 4)` is `IUniformRandomStream::RandomInt`,
	// inclusive at both ends, the same object and slot family Sounds and Damage read.
	constexpr int32 AndreiHitMaxMin = 2;
	constexpr int32 AndreiHitMaxMax = 4;

	// `0x103be0b0`'s carry timer, `curtime + RandomFloat(7.5, 10.0)` off `0x40f00000` / `0x41200000`.
	constexpr float TzimisceBodyTimerMin = 7.5f;
	constexpr float TzimisceBodyTimerMax = 10.0f;

	// `0x103be3d0`'s initial best distance — 1025 units SQUARED, spelled as the literal
	// `1050625.0` the decompiler folded. Family Bosses' `0x10381e90` uses the same number, which is
	// what makes the two bodies "the same search over a different bone table".
	constexpr float GrabBoneRangeSqUnits = 1050625.0f;

	// `0x103be8e0`'s two grab-distance bounds and `0x103bea90`'s nudge, all off the same two cells
	// families Bosses and Damage read as the +-20 pickup cone.
	constexpr float TzimisceGrabLowerBound = -20.0f;  // _DAT_1049ae98
	constexpr float TzimisceGrabUpperBound = 20.0f;   // _DAT_1044eb0c

	// `0x103bea90`'s aim height and its impulse floor.
	constexpr float TzimisceThrowAimHeightUnits = 48.0f;   // _DAT_10447ee8
	constexpr float TzimisceThrowSpeedFloor = 1000.0f;     // _DAT_10447ee0
	constexpr float TzimisceThrowSpeedFloorImpulse = 1000.0f;   // 0x447a0000
	// `thunk_FUN_102c43b0(this, 0.75)` — the collision-ignore renewal the release ends on.
	constexpr float TzimisceReleaseIgnoreSeconds = 0.75f;

	// `0x103bef20`'s attach range gate: `distSq <= 25600` units squared, i.e. **160 units**.
	constexpr float TzimisceAttachRangeSqUnits = 25600.0f;   // _DAT_104cc51c

	// `0x103d1e50`'s weighted distance and its threshold. The weighting is Source's classic
	// octagonal approximation of a length: the largest axis plus a quarter of the other two.
	constexpr float WerewolfDoorMinorAxisWeight = 0.25f;     // _DAT_1044bef8
	constexpr float WerewolfDoorMaxDistanceUnits = 135.0f;   // _DAT_104cf498

	// `0x103d1e50`'s two door-state numbers, read off family Hints' `WerewolfDoorState` (+0x6680).
	constexpr int32 WerewolfDoorStateNone = 0;
	constexpr int32 WerewolfDoorStateSettled = 2;

	// `0x103e0980`'s reroll: `if (type == 4) type = RandomInt(1, 3)`.
	constexpr int32 ZombieAiTypeRerolled = 4;
	constexpr int32 ZombieAiTypeRerollMin = 1;
	constexpr int32 ZombieAiTypeRerollMax = 3;

	// `0x103e1080`'s two literals. `0x20000` is `m_bfAINPCFlags` `SLEEPING`, which
	// `Substrate/ElysiumNpcFlags.h` names.
	constexpr int32 ZombieFloatSoundFrequency = 9;
	constexpr EElysiumNpcFlag ZombieFloatSoundBlockingFlag = EElysiumNpcFlag::SLEEPING;

	// `CBaseCombatCharacter::IsUnconscious` (`0x10341aa0`) — `(m_iMiscFlags & 1) != 0`. Family
	// **Sounds** keeps an identical private copy (`SoundsIsUnconscious`) for the same
	// file-ownership reason.
	bool Species2IsUnconscious(const FElysiumNpc& Npc)
	{
		return ElysiumMiscFlags::Has(Npc.MiscFlags, 0x1u);
	}

	// `CNPC_VMingXiaoTentacle`'s condition. `0x1039ef90` raises **0x78**, which is one past the
	// base registrar's dense `0x00..0x76` namespace and has NO NAME in `EElysiumNpcCond` — family
	// Conditions enumerated every identity it could name and this one is not among them. It is
	// pushed BY NUMBER with this note rather than left out, because a body that dropped a condition
	// would be a body silently losing an interrupt.
	constexpr EElysiumNpcCond TentacleCoordinateCondition = static_cast<EElysiumNpcCond>(0x78);

	// `CNPC_VZombie`'s two output slots. Retail fires the SAME `COutputEvent` (`+0x66e8`) from both.
	const FName ZombieOnAttackedVictim(TEXT("OnAttackedVictim"));

	// `CNPC_VTzimisce`'s slot-488 script event.
	constexpr const TCHAR* TzimisceDeathScriptEvent = TEXT("SPI_DIES");

	// `CNPC_VNewscaster`'s two debug headers, verbatim from `.rdata`.
	constexpr const TCHAR* NewscasterNotPlayingText = TEXT("not playing VCD");
	constexpr const TCHAR* NewscasterMainHeader = TEXT("Main Stories (%d)");
	constexpr const TCHAR* NewscasterSideHeader = TEXT("Side Stories (%d)");

	// `0x103be3d0` walks a NULL-TERMINATED TABLE of bone names starting at
	// `PTR_s_Bip01_L_Forearm_106530e8`, stepping one pointer at a time until the pointed-at string
	// is empty. Only the FIRST entry's text survives in the corpus as a named string; the rest of
	// the table lives past what the decompiler recovered.
	//
	// **Unrecovered:** every entry after the first. The search is written as the walk retail runs
	// and the table carries the one name that IS recovered, so the body's shape — nearest bone wins,
	// index is the table position — is exercised and a longer table needs no other edit.
	const TCHAR* const TzimisceGrabBoneTable[] = { TEXT("Bip01 L Forearm") };

	// `0x103bef20`'s carrier bone, a plain `__strcmpi` against the model's own bone table.
	constexpr const TCHAR* TzimisceAttachCarrierBone = TEXT("Bip01 R Finger1");

	// `UTIL_VecToYaw` `0x101d2c70` and `UTIL_VecToPitch` `0x101d2ce0` over a delta in THIS world's
	// axes, whose Y is the negated Source one. Private copies for the same file-ownership reason
	// families Bosses, Facing and Positions each keep one.
	float Species2VecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return Species2Zero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < Species2Zero)
		{
			Yaw += 360.0f;
		}
		return Yaw;
	}

	float Species2VecToPitch(const FVector& PortDelta)
	{
		// `0x101d2ce0`: `-atan2(z, sqrt(x*x + y*y))` in degrees, answering 90 or -90 for a purely
		// vertical delta. Source's pitch is DOWN-POSITIVE, hence the negation.
		const double Flat = FMath::Sqrt(PortDelta.X * PortDelta.X + PortDelta.Y * PortDelta.Y);
		if (Flat == 0.0)
		{
			return PortDelta.Z > 0.0 ? -90.0f : 90.0f;
		}
		return -FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(PortDelta.Z, Flat)));
	}
}

// -------------------------------------------------------------------------------------------------
// The seams.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::EngineFrameNumber() const
{
	// SEAM. See the declaration: `INDEX_NONE` is "no frame number", which the one caller reads as
	// "the cache is stale", so it recomputes on every call.
	return INDEX_NONE;
}

FElysiumEntity* FElysiumNpc::MingXiaoTentacleHead() const
{
	// SEAM for `thunk_FUN_1039ede0(this)`. Family **Motor** stands the same retail call over the
	// same absent proxy chain (`ElysiumNpcKernelMotor.cpp:543`). Answers null — retail's
	// "no companion" arm, which forwards nothing.
	return nullptr;
}

bool FElysiumNpc::TzimisceDeathScriptArgument(int32 SingletonIndex, int32& OutArgument) const
{
	// SEAM for `DAT_1093cf94`, `DAT_1093cfdc` and `DAT_1093cebc`. See the declaration: false is the
	// arm that substitutes 0, which is what retail does for a singleton whose own `vtable+0x4`
	// answers true, so the event still fires with three zeroes.
	(void)SingletonIndex;
	OutArgument = 0;
	return false;
}

// -------------------------------------------------------------------------------------------------
// `CScriptedTarget::Spawn` — `0x1034d6e0`, slot 103.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_1034d6e0()
{
	// `0x1034d6e0`, arm by arm:
	//
	//     if (DAT_10938054 == 0) DAT_10938054 = GetInteractionID();   // one-time global registration
	//     VPROF("CBaseEntity::SetSolid", m_iName);                     // a scope push/pop
	//     SetSolid(&m_Collision, SOLID_NONE 0);
	//     Relink();
	//     m_vLastPosition = GetAbsOrigin();                            // vtable +0x364, slot 217
	//     if (m_iDisabled == 0) thunk_FUN_1034d5b0(this);              // arm the think
	//     AddFlag2(FL_NOTARGET-family bit 0x10);
	//
	// `CScriptedTarget` is NOT the NPC leaf — `classes.md` gives it no entity classname, so no map
	// stands one and `RetailClass()` never answers it. The body is ported all the same, against the
	// two words the `.inl` declares for it, exactly as family **BaseHelpers** ported
	// `CAI_BaseHumanoid`'s five off-line rows.
	//
	// **Seams, all named and none guessed:**
	//   * `CBaseCombatCharacter::GetInteractionID()` into the file-static `DAT_10938054` is a global
	//     interaction-id allocator this substrate has no counterpart for. Nothing in layers 0–9
	//     reads the id back, so the allocation is recorded and reaches nothing.
	//   * `SetSolid(SOLID_NONE)` + `Relink()` are the collision tier's; this runtime's entity has no
	//     solidity word on the kernel surface.
	//   * `thunk_FUN_1034d5b0` is the enabled-arm think arm and is no row of this family's.
	//   * `AddFlag2(0x10)` writes `m_fFlags2`, which the shape map does not bind.
	//
	// What IS written is the one word a reader can check: the cached position.
	ScriptedTargetLastPositionUnits = Origin / ElysiumMove::U;
	ElysiumStub::Fired(TEXT("method"), TEXT("CScriptedTarget::Spawn 0x1034d6e0 tail"),
		DebugString(),
		FString::Printf(TEXT("disabled=%d"), bScriptedTargetDisabled ? 1 : 0),
		TEXT("0002/29c-1: no solidity, relink, interaction-id or m_fFlags2 surface"));
}

// -------------------------------------------------------------------------------------------------
// `CNPC_Crow` — `0x103577d0` (slot 197) and `0x10357be0`.
// -------------------------------------------------------------------------------------------------

FVector FElysiumNpc::FUN_103577d0(const FVector& InUnits)
{
	// `0x103577d0`, twenty bytes and the whole of slot 197:
	//     (**(this + 0x300))(param_1);    // slot 192, WorldSpaceCenter
	//     return param_1;
	//
	// A same-object vtable forward that DISCARDS what it called and answers its own argument back.
	// The call is still made — slot 192 is `WorldSpaceCenter` and has no side effect, so the forward
	// is observationally a no-op — and the return is the input, unchanged. So a crow's body target
	// is wherever the caller asked, never its own centre: the opposite of what slot 197 does on
	// every other class.
	//
	// The call is reproduced because it is what the body does, and its result is dropped because
	// that is what the body does with it.
	(void)WorldSpaceCenter();
	return InUnits;
}

void FElysiumNpc::FUN_10357be0(float Scale)
{
	// `0x10357be0`, `CNPC_Crow`'s fly step toward the entity at `m_pHintNode` (`+0x5ddc`):
	//
	//     if ((float)_DAT_10449280 (0.0) < param_1) param_1 = 1.0;       // ANY positive -> exactly 1
	//     if (m_pHintNode == NULL) { GetOrigin(); SetAbsVelocity(vec3_origin); return; }
	//     to      = m_pHintNode->GetOrigin();                            // vtable +0x370, slot 220
	//     offset  = to - GetOrigin();
	//     if (Length(offset) < param_1 * 170.0) field_0x5f54 = 1;        // "arrived"
	//     if (AvoidStep(this, &offset, 170.0, &adjusted)) offset = adjusted;   // thunk 0x10357e50
	//     SetAbsVelocity(offset * 170.0);
	//     SetMotorYaw(m_pMotor, VecToYaw(offset), -2.0);                 // thunk 0x102e1c10
	//     angles    = GetAngles();                                       // vtable +0x374, slot 221
	//     angles.x  = VecToPitch(offset);                                // thunk 0x101d2ce0
	//     SetAngles(angles);                                             // vtable +0x100, slot 64
	//
	// Three retail facts worth stating because each looks like a slip and is not:
	//   * **the clamp is a flatten.** `0.0 < scale` is true for every positive argument, so the
	//     parameter is replaced by 1.0 on every ordinary call and only a zero or negative scale
	//     survives. The caller's number is therefore almost never used.
	//   * **the offset is NOT normalised before it becomes a velocity.** It is the raw delta scaled
	//     by 170, so a crow far from its hint is given an enormous velocity; the arrival latch at
	//     `field_0x5f54` is what stops that mattering.
	//   * **the "arrived" test compares a LENGTH against `scale * 170`**, i.e. against 170 units on
	//     every ordinary call, using the pre-avoidance offset.
	//
	// The three `_DAT_10454028` reads are one cell and one number — **170.0** — and the literal
	// `0x432a0000` handed to the avoidance step is the same 170.
	//
	// **Seams:** `thunk_FUN_10357e50` (the avoidance adjust) has no counterpart here and answers
	// "no adjustment"; `SetAbsVelocity` and the motor yaw are the movement tier's, reached through
	// family Hints' `SetMotorHintYaw` and recorded. `field_0x5f54` is `ElysiumNpcKernelShapeMap.cpp`
	// `_IMPLICIT` — no port member — so the arrival latch is recorded, not stored, and says so.
	// `m_pHintNode` (+0x5ddc) is family Schedule's `ScheduleHost.HintNode` — a hint NODE INDEX in
	// this runtime and a `CAI_Hint*` in retail. There is no hint store, so the ENTITY behind the
	// index cannot be resolved and the body takes retail's NULL arm: read the origin, zero the
	// velocity, return. `CrowFlyStep` below is the pure half and carries the arithmetic of the
	// non-null arm, so the recovered numbers are exercised rather than parked behind the seam.
	if (CrowScaleClampFloor < Scale)
	{
		Scale = Species2One;
	}
	const FElysiumEntity* To = nullptr;   // SEAM: `m_pHintNode`'s entity, see above
	if (To == nullptr)
	{
		(void)Origin;                     // `GetOrigin()`, called and discarded on this arm too
		Velocity = FVector::ZeroVector;   // `SetAbsVelocity(vec3_origin)`
		return;
	}
	const FCrowFlyStep Step = CrowFlyStep(Scale, To->Origin / ElysiumMove::U,
		Origin / ElysiumMove::U);
	if (Step.bArrived)
	{
		// `field_0x5f54 = 1`. `ElysiumNpcKernelShapeMap.cpp` calls `+0x5f44..+0x5fbc` `_IMPLICIT` —
		// no port member — so the arrival latch is recorded and not stored, and says so.
		ElysiumStub::Fired(TEXT("field"), TEXT("CNPC_Crow field_0x5f54 (arrival latch)"),
			DebugString(), TEXT("value=1"), TEXT("0002/29c-1: +0x5f54 has no port member"));
	}
	Velocity = Step.VelocityUnits * ElysiumMove::U;
	SetMotorHintYaw(Step.MotorYaw);     // `thunk_FUN_102e1c10(m_pMotor, yaw, -2.0)`
	Angles.X = Step.Pitch;              // `GetAngles()`, overwrite pitch, `SetAngles()`
}

FElysiumNpc::FCrowFlyStep FElysiumNpc::CrowFlyStep(float Scale, const FVector& ToUnits,
	const FVector& FromUnits)
{
	// The pure half of `0x10357be0`'s non-null arm, in retail's order and with retail's numbers.
	// `Scale` arrives ALREADY CLAMPED — the flatten is the caller's, because it happens before the
	// null test and therefore on both arms.
	FCrowFlyStep Step;
	const FVector Offset = ToUnits - FromUnits;
	// `if (Length(offset) < scale * 170.0) field_0x5f54 = 1;` — a LENGTH, not a squared length, and
	// against the PRE-avoidance offset.
	if (static_cast<float>(Offset.Size()) < Scale * CrowFlySpeed)
	{
		Step.bArrived = true;
	}
	// `thunk_FUN_10357e50(this, &offset, 170.0, &adjusted)` — the avoidance adjust. **SEAM**: no
	// counterpart here, so the answer is "no adjustment" and the raw offset survives, which is
	// retail's own arm when nothing is in the way. `CrowAvoidRadius` names the radius it is asked
	// with so the number is not lost.
	(void)CrowAvoidRadius;
	// `SetAbsVelocity(offset * 170.0)` — the offset is NOT normalised first. See the walk.
	Step.VelocityUnits = Offset * CrowFlySpeed;
	Step.MotorYaw = Species2VecToYaw(Offset);
	Step.Pitch = Species2VecToPitch(Offset);
	// `thunk_FUN_102e1c10(m_pMotor, yaw, -2.0)` — the mode the caller passes on.
	(void)CrowMotorYawMode;
	return Step;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VAndreiBlood` — `0x1035e920` and `0x1035e950`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_1035e920() const
{
	// `0x1035e920`, twenty-five bytes: `return m_iActiveRunnerCount (+0x66b8) < 2.0` — the cap read
	// out of `.rdata` at `_DAT_10452dc4` as **2.0f**.
	//
	// The datamap types `+0x66b8` `FIELD_INTEGER` (`m_iActiveRunnerCount`) while every body that
	// touches it uses x87 float instructions on it. It is a COUNT either way, and the three other
	// bodies that read and write it — `CNPCMaker_Fleshpile::MakeNPC` (`0x1034c2d0`, the `2 <= count`
	// refusal and the `+1` on success) and its `DeathNotice` (`0x1034c8e0`, the `-1`) — are the rest
	// of the same budget. So Andrei's fleshpile may have at most TWO runners alive at once, and this
	// is the predicate its schedule selector asks.
	//
	// Family **Schedule** stands a seam for exactly this body — `AndreiBloodSelectGate`
	// (`ElysiumNpcKernelSchedule.cpp:1354`) — which answers false because `+0x66b8` had no port
	// member. It does now. The one-line forward is that family's file and is reported rather than
	// edited here.
	return ActiveRunnerCount < AndreiMaxActiveRunners;
}

void FElysiumNpc::FUN_1035e950()
{
	// `0x1035e950`, twenty-six bytes: `m_iHitMax (+0x66dc) = RandomInt(2, 4)`.
	//
	// `(*DAT_1070b244 + 8)` is `IUniformRandomStream::RandomInt`, INCLUSIVE at both ends — the same
	// object and slot families Sounds, Damage and Positions read. So Andrei's hit budget for a phase
	// is 2, 3 or 4, drawn once. `m_iHitCounter` (`+0x66e0`) is family **Damage**'s
	// `AndreiBloodEmitter` neighbour and is not this row's.
	//
	// The draw IS made rather than skipped: the RNG stream's position is observable, and a body that
	// refused to draw would walk every later draw on the same stream off by one.
	AndreiHitMax = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
		.RandRange(AndreiHitMaxMin, AndreiHitMaxMax);
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VChangBros` — `0x1036c7f0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_1036c7f0(int32 InChangType)
{
	// `0x1036c7f0`, thirteen bytes: `m_ChangType (+0x66b8) = param_1`.
	//
	// A plain setter over family **Squad**'s `ChangType`, which `CNPC_VChangBros::SelectUnitedNode`
	// (`0x1036d100`) later reads to pick the twin's node. Note the OFFSET COLLISION this family's
	// standing facts warn about: `+0x66b8` is `CNPC_VAndreiBlood::m_iActiveRunnerCount` two bodies
	// up this file, `CNPC_VManBat::m_bHasPlayedFlyBySound` on family Sounds' and
	// `CNPC_VAsianVampire::m_vLastJumpPosition` on family Motor's. Four species, one offset, four
	// members — writing through Squad's is what keeps the Chang reading in one place.
	//
	// Retail name unrecovered; `SetChangType` is the concern, not a recovered symbol.
	ChangType = InChangType;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VMingXiaoTentacle` — `0x1039ef90` and slots 21, 22, 23.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_1039ef90(const FVector& PositionUnits)
{
	// `0x1039ef90`, fifty-seven bytes, in retail's order:
	//     (*DAT_10924a6c)->vfunc1();                 // result DISCARDED
	//     SetCondition(this, 0x78);                  // thunk 0x10269a20
	//     field_0x668c = param_1[0];
	//     field_0x6690 = param_1[1];
	//     field_0x6694 = param_1[2];
	//
	// The caller is `CNPC_VMingXiao::CoordinateTroops`'s severed-tentacle arm (`0x103998d0`), which
	// family **Squad** already ports as far as this call and names it as unported. This is that row.
	//
	// `DAT_10924a6c` is the same discarded `ConVar` read families Hints, Schedule and Positions each
	// found in front of a `SetCondition` — a folded log gate whose result nothing uses. Its name and
	// default are **UNRECOVERED** and it is not asked here, because nothing downstream can observe
	// it either.
	//
	// Condition **0x78** has no name in `EElysiumNpcCond`: the base registrar's namespace ends at
	// 0x76 and family Conditions named every identity it could. It is set BY NUMBER, with the
	// constant above carrying the note, rather than dropped.
	Cognition.Conditions.Set(TentacleCoordinateCondition);
	TentacleCoordinatePosUnits = PositionUnits;
}

void FElysiumNpc::FUN_1039e800(FElysiumEntity* Arg)
{
	// `0x1039e800`, `CNPC_VMingXiaoTentacle`'s slot 21, twenty-nine bytes:
	//     head = GetHead(this);                        // thunk 0x1039ede0
	//     if (head) TellHead(head, this, param_1);      // thunk 0x10397dd0
	//
	// Slot 21's base across the family is `CAISound::FUN_10026530`, a no-op, and
	// `CAI_BaseNPCTroika` overrides it generically. This class overrides it AGAIN to hand the call
	// to its head — so a tentacle answers nothing itself and the head decides.
	//
	// `MingXiaoTentacleHead()` is this family's seam and answers null, so the forward does not
	// happen; `TentacleHeadForwards` counts the asks so a test can read that the arm was taken.
	++TentacleHeadForwards;
	FElysiumEntity* Head = MingXiaoTentacleHead();
	if (Head != nullptr)
	{
		// `thunk_FUN_10397dd0(head, this, param_1)` — family **Damage** declared the body this
		// reaches (`CNPC_VMingXiao`'s per-tentacle notice). Unreachable while the seam answers null.
		(void)Arg;
	}
}

void FElysiumNpc::FUN_1039e830(FElysiumEntity* Arg)
{
	// `0x1039e830`, slot 22 — byte-identical to `0x1039e800`, verified against the decompiled C of
	// both. Three consecutive slots, one body written three times.
	FUN_1039e800(Arg);
}

void FElysiumNpc::FUN_1039e860(FElysiumEntity* Arg)
{
	// `0x1039e860`, slot 23 — the same body a third time.
	FUN_1039e800(Arg);
}

bool FElysiumNpc::SpeciesSlot21(FElysiumEntity* Arg)
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(21);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x1039e800")) != 0)
	{
		return false;
	}
	FUN_1039e800(Arg);
	return true;
}

bool FElysiumNpc::SpeciesSlot22(FElysiumEntity* Arg)
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(22);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x1039e830")) != 0)
	{
		return false;
	}
	FUN_1039e830(Arg);
	return true;
}

bool FElysiumNpc::SpeciesSlot23(FElysiumEntity* Arg)
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(23);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x1039e860")) != 0)
	{
		return false;
	}
	FUN_1039e860(Arg);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VNewscaster` — `0x103a0d50` and `0x103a0ff0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103a0d50()
{
	// `0x103a0d50`, the story-queue TEARDOWN:
	//
	//     while (m_MainCount (+0x6668) != 0) {
	//         Release(main[0].object);                            // FUN_10430964
	//         for (o = 0; o < 0x20; o += 8) { Release(*(main + 4 + o)); Release(*(main + 8 + o)); }
	//         if (0 < m_MainCount - 1) memmove(main, main + 0x28, (m_MainCount - 1) * 0x28);
	//         m_MainCount -= 1;
	//     }
	//     ... the identical loop again over the SIDE queue (+0x6670, count +0x667c) ...
	//     field_0x6690 = 0;
	//
	// Two facts about the loops, both retail's:
	//   * the release is a **FRONT** removal, not a swap-remove: the whole remainder is memmoved
	//     down by one `0x28` record each pass, so the queue keeps its order while it drains. The two
	//     blacklists in this family's other half do the opposite, which is why this one is spelled
	//     out rather than shared.
	//   * the inner `for` releases EIGHT handles per record — `+0x04`/`+0x08` at four strides of 8 —
	//     plus the record's own object at `+0x00`, so nine per row.
	//
	// This runtime carries a story as its NAME only (see `FNewscasterStory`): nothing here stands a
	// VCD, so there are no handles to release and the observable is the drain itself. The
	// story-active flag is cleared last, exactly as retail does.
	NewscasterMainStories.Reset();
	NewscasterSideStories.Reset();
	bNewscasterStoryActive = false;
}

int32 FElysiumNpc::FUN_103a0ff0(int32 FirstLine, TArray<FString>& OutLines) const
{
	// `0x103a0ff0`, the debug OVERLAY:
	//
	//     if (!Resolve(m_hDialogScene (+0x6554))) {
	//         EntityText(m_pScriptHost (+0x2e0), line, "not playing VCD", 0, 255,255,255,255);
	//         return line + 1;
	//     }
	//     EntityText(m_pScriptHost, ...);  Printf("Main Stories (%d)", ...);  line += 1;
	//     for (i = 0; i < m_MainCount; ++i)
	//         line = PrintStory(this, line, &main[i], field_0x668c == 0 && field_0x6684 == i);
	//     EntityText(m_pScriptHost, ...);  Printf("Side Stories (%d)", ...);  line += 1;
	//     for (i = 0; i < m_SideCount; ++i)
	//         line = PrintStory(this, line, &side[i], field_0x668c != 0 && field_0x6688 == i);
	//     return line;
	//
	// **The highlight predicates are opposites and that is the whole meaning of `+0x668c`**: a main
	// row is highlighted when `+0x668c == 0` and a side row when `+0x668c != 0`, so the word selects
	// WHICH QUEUE is playing and `+0x6684`/`+0x6688` are the two cursors within them. The C's
	// `cVar5 = '\x01'` short-circuit reads backwards at a glance; it is the ordinary
	// "both terms or nothing" and is written that way here.
	//
	// Retail's headers are reproduced verbatim, including the `(%d)` counts. `thunk_FUN_103a0eb0` is
	// the per-row printer (`0x103a0eb0`, not this family's row); this writes one line per row with
	// the highlight flag so a reader can see which one is current.
	//
	// The overlay colour (255,255,255,255) and the `NDebugOverlay::EntityText` channel are the
	// debug tier's and reach nothing here; the LINE NUMBERING is the body's own arithmetic and is
	// what the suite reads back.
	int32 Line = FirstLine;
	const bool bScenePlaying = World != nullptr
		&& World->Resolve(Dialogue.DialogScene) != nullptr;
	if (!bScenePlaying)
	{
		OutLines.Add(NewscasterNotPlayingText);
		return Line + 1;
	}
	OutLines.Add(FString::Printf(TEXT("Main Stories (%d)"), NewscasterMainStories.Num()));
	++Line;
	for (int32 i = 0; i < NewscasterMainStories.Num(); ++i)
	{
		const bool bCurrent = NewscasterPlayingSide == 0 && NewscasterMainCursor == i;
		OutLines.Add(FString::Printf(TEXT("%s%s"), bCurrent ? TEXT("* ") : TEXT("  "),
			*NewscasterMainStories[i].Name));
		++Line;
	}
	OutLines.Add(FString::Printf(TEXT("Side Stories (%d)"), NewscasterSideStories.Num()));
	++Line;
	for (int32 i = 0; i < NewscasterSideStories.Num(); ++i)
	{
		const bool bCurrent = NewscasterPlayingSide != 0 && NewscasterSideCursor == i;
		OutLines.Add(FString::Printf(TEXT("%s%s"), bCurrent ? TEXT("* ") : TEXT("  "),
			*NewscasterSideStories[i].Name));
		++Line;
	}
	return Line;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VTzimisce`'s carry chain — `0x103be0b0`, `0x103be150`, `0x103be3d0`, `0x103be8e0`,
// `0x103bea90`, `0x103bef20`, `0x103bf560`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103be0b0(bool bCarrying)
{
	// `0x103be0b0`, eighty-four bytes:
	//     if (param_1) {
	//         m_bfAINPCFlags |= CARRYING_BODY (0x20);
	//         m_flBodyTimer (+0x66a4) = curtime + RandomFloat(7.5, 10.0);
	//         m_bDidFakeThrow (+0x66b4) = 0;
	//     } else {
	//         m_bfAINPCFlags &= ~CARRYING_BODY;
	//     }
	//
	// This is `CNPC_VTzimisce`'s exact counterpart of `CNPC_VHengeyokai`'s `0x10381c00` — family
	// Bosses' `CallFormBit` seam, which stamps `m_flFishTimer` (+0x666c) and clears
	// `m_bDidFakeThrow` (+0x667d) on ITS class's offsets. Same two writes, same flag bit, different
	// species and different offsets; this one is a body and not a seam because both its words are
	// declared.
	//
	// The clearing arm writes ONLY the flag — the timer and the fake-throw byte are left standing,
	// so a Tzimisce that drops a body keeps whatever deadline it was carrying under.
	//
	// The random draw order matters and is kept: retail draws BEFORE it reads `curtime`.
	if (bCarrying)
	{
		NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
		const float Drawn = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(TzimisceBodyTimerMin, TzimisceBodyTimerMax);
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		bTzimisceDidFakeThrow = false;
		TzimisceBodyTimer = Now + static_cast<double>(Drawn);
		return;
	}
	NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
}

bool FElysiumNpc::FUN_103be150() const
{
	// `0x103be150`, thirty-two bytes: `return m_flBodyTimer (+0x66a4) <= curtime`.
	//
	// At-or-before, not strictly before — a timer armed at exactly `curtime` reads as already
	// elapsed. A Tzimisce that has never carried anything has `m_flBodyTimer` zero, so this answers
	// true from spawn; the `m_bfAINPCFlags` `CARRYING_BODY` bit is what the callers pair it with.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return TzimisceBodyTimer <= Now;
}

bool FElysiumNpc::FUN_103be3d0(FElysiumEntity* InTarget)
{
	// `0x103be3d0`, `CNPC_VTzimisce`'s grab-bone search:
	//
	//     best = 1050625.0;  found = false;
	//     rag = dynamic_cast<CRagdollProp*>(param_1);        // 0x10538764 -> 0x1057c684
	//     if (rag) for (i = 0; table[i][0] != '\0'; ++i) {
	//         el = rag->GetElement(table[i]);                 // vtable +0x424
	//         if (!el) continue;
	//         el->GetPosition(&pos, &unusedAngles);           // +0x94
	//         d = DistSq(GetOrigin(), pos);                   // vtable +0x370
	//         if (d < best) { m_vecPickupTargetPos = pos; found = true;
	//                         m_iPickupTargetGrabBone = i; best = d; }
	//     }
	//     return found;
	//
	// It is family Bosses' `0x10381e90` over a DIFFERENT bone table — the one starting at
	// `PTR_s_Bip01_L_Forearm_106530e8` — and with **no fallback branch when the cast fails**: where
	// `0x10381e90` has a second arm, this one simply answers false. 29c's walk says the same.
	//
	// The initial best is **1025 units squared** (1050625), so a bone further than 1025 units away
	// never wins even if it is the only one.
	//
	// **Seams:** `RagdollBonePosition` (family Bosses) stands the cast plus the element lookup plus
	// `GetPosition` and answers false, so no bone is ever offered and the body takes its
	// "nothing found" arm — which is retail's own answer for a target that is not a ragdoll. The
	// writes are real when a bone IS offered, which is what the suite drives.
	//
	// **Unrecovered:** every entry of the bone table after the first (see `TzimisceGrabBoneTable`).
	float Best = GrabBoneRangeSqUnits;
	bool bFound = false;
	const FVector OriginUnits = Origin / ElysiumMove::U;
	for (int32 i = 0; i < UE_ARRAY_COUNT(TzimisceGrabBoneTable); ++i)
	{
		FVector BoneUnits = FVector::ZeroVector;
		if (!RagdollBonePosition(InTarget, TzimisceGrabBoneTable[i], BoneUnits))
		{
			continue;
		}
		const float DistSq = static_cast<float>(FVector::DistSquared(OriginUnits, BoneUnits));
		if (DistSq < Best)
		{
			PickupTargetPos = BoneUnits;      // +0x6674 m_vecPickupTargetPos (family Motor's)
			bFound = true;
			TzimiscePickupGrabBone = i;       // +0x6680 m_iPickupTargetGrabBone
			Best = DistSq;
		}
	}
	return bFound;
}

bool FElysiumNpc::FUN_103be8e0(FElysiumEntity* InTarget)
{
	// `0x103be8e0`, "is the thing I am holding close enough?":
	//
	//     if (param_1 == NULL) return true;                       // <- note the answer
	//     held  = Resolve(m_hPickupTarget (+0x6670));
	//     from  = held->GetOrigin();                              // vtable +0x370
	//     gz    = ConVar(DAT_1093ca8c).IsCommand() ? 0.0 : value;
	//     lead  = ComputeTargetLeadPoint(this, from, param_1, gz);// thunk 0x102c36d0
	//     delta = lead - GetOrigin();
	//     yaw   = VecToYaw(delta);                                // thunk 0x101d2c70
	//     diff  = UTIL_AngleDiff(yaw, GetAngles().y);             // thunk 0x1013d580, +0x374 + 4
	//     if (diff <= -20.0) return <the flag word>;              // _DAT_1049ae98
	//     if (diff <= 20.0)  return <the flag word>;              // _DAT_1044eb0c
	//     return true;
	//
	// **29c's walk reads the last two tests as a DISTANCE comparison and they are an ANGLE
	// comparison.** `thunk_FUN_1013d580` is `UTIL_AngleDiff` — families Bosses, Damage2, Facing and
	// Positions all name it that — and `*(float *)(iVar8 + 4)` is `GetAngles().y`, the body's YAW,
	// not a model radius. So the body is a +-20 degree FACING CONE, the same cone
	// `CNPC_VHengeyokai`'s `0x103822a0` uses off the same two `.rdata` cells, and not a range test.
	// The recovered numbers are -20.0 and +20.0, read out of the image.
	//
	// The `param_1 == NULL` arm answering TRUE is retail's and is kept: with nothing to aim at, the
	// body reports "close enough" rather than refusing.
	//
	// **Seams:** `ChaseLeadPosition` (family Positions) stands `0x102c36d0` and answers the input
	// position unchanged; `DAT_1093ca8c` is an unnamed `ConVar` whose value is **UNRECOVERED** and
	// is passed as 0.0, which is retail's own `IsCommand()` arm.
	if (InTarget == nullptr)
	{
		return true;
	}
	const FElysiumEntity* Held = World != nullptr ? World->Resolve(PickupTarget) : nullptr;
	const FVector FromUnits = Held != nullptr ? Held->Origin / ElysiumMove::U : FVector::ZeroVector;
	FVector LeadUnits = FromUnits;
	ChaseLeadPosition(InTarget, FVector::ZeroVector, Species2Zero, FromUnits * ElysiumMove::U,
		LeadUnits);
	const FVector DeltaUnits = LeadUnits - Origin / ElysiumMove::U;
	const float Yaw = Species2VecToYaw(DeltaUnits);
	// `UTIL_AngleDiff` `0x1013d580` — family Bosses keeps a private copy for the same reason; this
	// half of it is the only part the two tests read.
	float Diff = Yaw - static_cast<float>(Angles.Y);
	while (Diff > 180.0f)
	{
		Diff -= 360.0f;
	}
	while (Diff < -180.0f)
	{
		Diff += 360.0f;
	}
	if (Diff <= TzimisceGrabLowerBound)
	{
		return false;
	}
	if (Diff <= TzimisceGrabUpperBound)
	{
		return false;
	}
	return true;
}

bool FElysiumNpc::FUN_103bef20(FElysiumEntity* InTarget, int32 ElementKey)
{
	// `0x103bef20`, the physics-animlink ATTACH:
	//
	//     if (param_1 == NULL) return false;
	//     if (DistSq(GetAbsOrigin(), param_1->GetAbsOrigin()) > 25600.0) return false;   // 160 units
	//     link = CreateNoSpawn("phys_animlink", vec3_origin);   if (!link) return false;
	//     bone = <scan the model's bone table for "Bip01 R Finger1">;
	//     if (bone < 0) return false;
	//     element = param_1[0xdb];                              // the carried thing's +0x36c payload
	//     if (element == 0) return false;
	//     rag = dynamic_cast<CRagdollProp*>(param_1);
	//     if (rag && rag->GetElement(param_2) == 0) return false;   // vtable +0x424
	//     LinkAnimlink(link, this, bone, element, vec3_origin, vec3_origin);   // 0x1014f210
	//     m_hPhysicsAnimlink (+0x6684) = link->GetRefEHandle();
	//     thunk_FUN_103be050(this, false);
	//     FUN_103be0b0(this, true);                             // CARRYING_BODY + the body timer
	//     return true;
	//
	// The range gate is the one number that makes this body different from family Bosses' two attach
	// arms: **160 units**, read as `25600.0` squared out of `.rdata`. Neither boss has one.
	//
	// The bone scan's retail quirk family Bosses recorded holds here too — the loop leaves its
	// counter at the bone COUNT when nothing matched, so a miss is a positive index and the
	// `bone < 0` guard never fires. This port answers on the seam's `INDEX_NONE` instead and says so.
	//
	// The tail is what wires this row to the rest of the chain: it calls `0x103be0b0` with true, so
	// a successful attach is what arms `m_flBodyTimer` and raises `CARRYING_BODY`.
	//
	// **Seams:** `CreatePhysAnimlink`, `LookupBoneByName`, `RagdollElementForBone` and
	// `WirePhysAnimlink` are family Bosses'; `thunk_FUN_103be050` is `CNPC_VTzimisce`'s other latch
	// and is no row of this family's.
	if (InTarget == nullptr)
	{
		return false;
	}
	const float DistSqUnits = static_cast<float>(
		FVector::DistSquared(Origin / ElysiumMove::U, InTarget->Origin / ElysiumMove::U));
	if (DistSqUnits > TzimisceAttachRangeSqUnits)
	{
		return false;
	}
	const FElysiumEntityHandle Link = CreatePhysAnimlink();
	if (World == nullptr || World->Resolve(Link) == nullptr)
	{
		return false;
	}
	const int32 Bone = LookupBoneByName(TzimisceAttachCarrierBone);
	if (Bone < 0)
	{
		return false;
	}
	if (!RagdollElementForBone(InTarget, ElementKey))
	{
		return false;
	}
	WirePhysAnimlink(Link, Bone, ElementKey);
	TzimiscePhysicsAnimlink = Link;
	FUN_103be0b0(/*bCarrying=*/true);
	return true;
}

void FElysiumNpc::FUN_103bea90(FElysiumEntity* AimTarget)
{
	// `0x103bea90`, the physics-animlink RELEASE — nine hundred and seventeen bytes, and it is the
	// throw as well as the release:
	//
	//     UTIL_Remove(Resolve(m_hPhysicsAnimlink (+0x6684)));   // thunk 0x101cd970
	//     m_hPhysicsAnimlink = NULL;                            // thunk 0x100a0ae0
	//     if (param_1 != NULL) {
	//         held  = Resolve(m_hPickupTarget (+0x6670));
	//         from  = held->GetOrigin();
	//         gz    = ConVar(DAT_1093ca8c).IsCommand() ? 0.0 : value;
	//         aim   = ComputeTargetLeadPoint(this, from, param_1, gz);   // thunk 0x102c36d0
	//         aim.z += 48.0;                                             // _DAT_10447ee8
	//         delta = aim - from;  distSq = Dot(delta, delta);
	//         yaw   = VecToYaw(delta);
	//         diff  = UTIL_AngleDiff(yaw, GetAngles().y);
	//         if (diff > -20.0) { if (20.0 <= diff) delta.xy = Rotate(from, radius + 20.0).xy; }
	//         else              {                   delta.xy = Rotate(from, radius - 20.0).xy; }
	//         speed = (distSq * ConVar(DAT_1093ca8c)) <= 1000.0 ? 1000.0 : distSq * ConVar(...);
	//         impulse.xy = delta.xy * speed;
	//         impulse.z  = ConVar(DAT_1093ca44) * distSq + delta.z * speed;
	//         rag = dynamic_cast<CRagdollProp*>(Resolve(m_hPickupTarget));
	//         if (rag) rag->ApplyImpulse(&impulse, &vec3_origin);        // vtable +0x428
	//         else     { phys = held->m_pPhysicsObject (+0x36c);
	//                    if (phys) { phys->GetPosition(&p, &a);          // +0xa0
	//                                phys->ApplyForceCenter(&impulse); } // +0x9c
	//     }
	//     m_hPickupTarget = -1;                                   // +0x6670, INVALID
	//     ArmIgnoreCollisionExpiry(0.75);                         // thunk 0x102c43b0
	//     FUN_103be0b0(this, false);                              // clear CARRYING_BODY
	//
	// Three recovered facts the one-line walk gets wrong or leaves out:
	//   * the `1049ae98`/`1044eb0c` pair is again the **+-20 degree cone**, not a dot product and
	//     not a distance — the same reading as `0x103be8e0` above and off the same two cells;
	//   * the speed floor is **1000.0** (`_DAT_10447ee0`, and the literal `0x447a0000` on the other
	//     arm is the same 1000), which is family Damage2's `ThrowSpeedFloor` off the same cell;
	//   * the aim point is lifted **48 units** (`_DAT_10447ee8`), the same lift family Bosses' two
	//     release arms use.
	//
	// The tail is the mirror of the attach's: `0x103be0b0(false)` clears `CARRYING_BODY`, and
	// `m_hPickupTarget` is cleared to INVALID whether or not there was anything to throw at.
	//
	// **Seams:** `RemovePhysAnimlink`, `ApplyThrowImpulse` and `ArmIgnoreCollisionExpiry` are family
	// Bosses'; `ChaseLeadPosition` is family Positions'; `DAT_1093ca8c` and `DAT_1093ca44` are two
	// unnamed `ConVar`s whose values are **UNRECOVERED** and are read as 0.0, which is retail's own
	// `IsCommand()` arm. With both at zero the impulse's magnitude collapses to the 1000-unit floor,
	// which is the value the floor exists to guarantee — so the THROW still happens and its
	// direction is the recovered one.
	RemovePhysAnimlink(TzimiscePhysicsAnimlink);
	TzimiscePhysicsAnimlink = FElysiumEntityHandle();

	if (AimTarget != nullptr)
	{
		const FElysiumEntity* Held = World != nullptr ? World->Resolve(PickupTarget) : nullptr;
		const FVector FromUnits = Held != nullptr
			? Held->Origin / ElysiumMove::U : FVector::ZeroVector;
		FVector AimUnits = FromUnits;
		ChaseLeadPosition(AimTarget, FVector::ZeroVector, Species2Zero,
			FromUnits * ElysiumMove::U, AimUnits);
		AimUnits.Z += TzimisceThrowAimHeightUnits;

		FVector DeltaUnits = AimUnits - FromUnits;
		const float DistSq = static_cast<float>(DeltaUnits.SizeSquared());
		const float Yaw = Species2VecToYaw(DeltaUnits);
		float Diff = Yaw - static_cast<float>(Angles.Y);
		while (Diff > 180.0f)
		{
			Diff -= 360.0f;
		}
		while (Diff < -180.0f)
		{
			Diff += 360.0f;
		}
		// The two nudge arms. `thunk_FUN_101d2f40(&from, radius +- 20.0)` rewrites X and Y only and
		// leaves Z, which is why retail saves and restores `delta.z` around each call. With no
		// radius seam the rewrite cannot be performed; the ARM taken is recorded and the delta is
		// left as it stands, which is retail's own middle arm (within the cone, nothing rotated).
		(void)Diff;

		// `speed = max(distSq * gravityScale, 1000.0)`, with the ConVar at its unrecovered 0.0 —
		// so the floor is what survives, which is the number the floor exists to guarantee.
		float Speed = DistSq * Species2Zero;
		if (Speed <= TzimisceThrowSpeedFloor)
		{
			Speed = TzimisceThrowSpeedFloorImpulse;
		}
		FVector ImpulseUnits(DeltaUnits.X * Speed, DeltaUnits.Y * Speed,
			Species2Zero * DistSq + DeltaUnits.Z * Speed);
		ApplyThrowImpulse(PickupTarget, ImpulseUnits);
	}

	PickupTarget = FElysiumEntityHandle();   // `+0x6670 = -1`
	ArmIgnoreCollisionExpiry(TzimisceReleaseIgnoreSeconds);
	FUN_103be0b0(/*bCarrying=*/false);
}

void FElysiumNpc::FUN_103bf560()
{
	// `0x103bf560`, fourteen bytes: `SetIdealYaw(m_pMotor (+0x5d44), -1)` — `thunk_FUN_102e1e20`
	// with the sentinel that means "use the live heading", which the motor computes by `ftol`-ing
	// the current angle.
	//
	// Family **Hints** stands the same retail call as `ReleaseMotorHintYaw()` and family **Motor**
	// found `0x10382d20` to be the identical one-liner on another class. It is called rather than
	// restated. Retail name unrecovered; single caller (`0x103bb1e0`), no vtable slot.
	ReleaseMotorHintYaw();
}

bool FElysiumNpc::FUN_103c24a0() const
{
	// `0x103c24a0`, thirty bytes: `return 0.0 < m_flSlowedExpire (+0x6678)` — `_DAT_1044fab0` read
	// out of `.rdata` as **0.0f**.
	//
	// STRICTLY greater, and against zero rather than against `curtime`: the word is a `FIELD_TIME`
	// on `CNPC_VTzimisceHeadClaw`'s datamap but this predicate only asks whether it was ever ARMED,
	// not whether it has expired. A head claw that was slowed once and whose window has long since
	// run out still answers true here until something writes the word back to zero.
	return Species2Zero < static_cast<float>(HeadClawSlowedExpire);
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VTzimisce`'s slots 488 and the two `CNPC_VCamera` empties.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103b92a0()
{
	// `0x103b92a0`, `CNPC_VTzimisce`'s slot 488 `DeathSound`. The decompiled C was damaged, so this
	// was read off the LISTING:
	//
	//     a = DAT_1093cf94->vfunc1() ? 0 : DAT_1093cf94[+0x2c];
	//     b = DAT_1093cfdc->vfunc1() ? 0 : DAT_1093cfdc[+0x2c];
	//     c = DAT_1093cebc->vfunc1() ? 0 : DAT_1093cebc[+0x28];
	//     ScriptFire(m_pScriptHost (+0x2e0), "SPI_DIES", c, b, 0, a);      // 0x10002414
	//     JMP  vtable[+0x79c];                                             // slot 487, a TAIL CALL
	//
	// Two things the listing settles that the walk does not:
	//   * the arguments are pushed in the order `a, 0, b, c` and read back by the callee as
	//     `(host, "SPI_DIES", c, b, 0, a)` — so the THIRD singleton is the first argument and the
	//     literal zero sits between the second and the first. The order is reproduced;
	//   * the last instruction is a `JMP`, not a `CALL`: the base slot 487 body runs with this
	//     frame, so slot 488's own work happens BEFORE the base's and the base's return value is
	//     what the caller sees.
	//
	// **Seam:** `TzimisceDeathScriptArgument` stands all three singletons and answers "not
	// available", which is the arm that substitutes 0 — so the event fires with three zeroes. The
	// event itself is the observable and it IS fired.
	int32 A = 0;
	int32 B = 0;
	int32 C = 0;
	TzimisceDeathScriptArgument(0, A);
	TzimisceDeathScriptArgument(1, B);
	TzimisceDeathScriptArgument(2, C);
	ElysiumStub::Fired(TEXT("method"),
		FString::Printf(TEXT("CNPC_VTzimisce::DeathSound 0x103b92a0 -> %s"),
			TzimisceDeathScriptEvent),
		DebugString(),
		FString::Printf(TEXT("args=%d,%d,0,%d"), C, B, A),
		TEXT("0002/29c-1: no script host for SPI_DIES"));
	// The tail call into slot 487. The generated `DeathSound` is 29c's stub for the base body
	// `0x10293ec0`; slot 487's own port method is not this family's row, so the base death sound is
	// what the caller sees and is left to the slot that already carries it.
}

void FElysiumNpc::FUN_103681d0()
{
	// `0x103681d0`, `CNPC_VCamera`'s slot 497 — ONE byte, a bare `ret`. The class replaces the
	// sound hook with nothing, so a security camera makes none of whatever sound slot 497 plays.
	// Reproduced as the empty body it is: the point of the row is that the base does NOT run.
}

void FElysiumNpc::FUN_103682f0()
{
	// `0x103682f0`, `CNPC_VCamera`'s slot 506 — the same empty body at the other end of the same
	// sound-hook cluster. `CNPC_VCameraSecurity` inherits both.
}

bool FElysiumNpc::SpeciesDeathSound()
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(488);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103b92a0")) != 0)
	{
		return false;
	}
	FUN_103b92a0();
	return true;
}

bool FElysiumNpc::SpeciesSlot497()
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(497);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103681d0")) != 0)
	{
		return false;
	}
	FUN_103681d0();
	return true;
}

bool FElysiumNpc::SpeciesSlot506()
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(506);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103682f0")) != 0)
	{
		return false;
	}
	FUN_103682f0();
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VWerewolf` — `0x103d1e50` and `0x103d9c90`.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FUN_103d1e50() const
{
	// `0x103d1e50`:
	//
	//     if (m_DoorState (+0x6680) == 2) return true;
	//     if (m_DoorState == 0) return false;
	//     a = Resolve(m_hRotDoor1 (+0x6684));  if (!a) return false;
	//     b = Resolve(m_hRotDoor2 (+0x6688));  if (!b) return false;
	//     pa = a->WorldSpaceCenter();  pb = b->WorldSpaceCenter();   // vtable +0x300, slot 192
	//     dx = |pb.x - pa.x|;  dy = |pb.y - pa.y|;  dz = |pb.z - pa.z|;
	//     d  = <largest> + 0.25 * (<the other two, summed>);
	//     return !(d <= 135.0);
	//
	// The distance is Source's octagonal length approximation — the largest axis plus a quarter of
	// the sum of the other two (`_DAT_1044bef8` = **0.25**, read out of the image) — not a
	// Chebyshev distance as 29c's walk has it, and not a Euclidean one. The three-way `if` ladder in
	// the decompiled C is the max-of-three; it is written here as the same ladder rather than as a
	// sort, because the tie cases pick a different "other two" and the arithmetic differs by the
	// weighting.
	//
	// The threshold is **135.0** (`_DAT_104cf498`) and the answer is the NEGATION: true means the
	// two halves are further apart than 135 units, i.e. the door is OPEN. A door state of 2 short
	// circuits to true and a state of 0 to false, so the measurement only happens in between.
	//
	// `m_DoorState` (`+0x6680`) is family **Hints**' `WerewolfDoorState`, and the two halves are
	// family **Misc**'s `m_hRotDoor1` / `m_hRotDoor2` (`+0x6684` / `+0x6688`), which its
	// `0x103cade0` zone opener FILLS by the hardcoded map names `rotdoor1` and `rotdoor2`. This body
	// is the READER of that pair and goes through Misc's members; the two halves of one fact.
	if (WerewolfDoorState == WerewolfDoorStateSettled)
	{
		return true;
	}
	if (WerewolfDoorState == WerewolfDoorStateNone || World == nullptr)
	{
		return false;
	}
	const FElysiumEntity* A = World->Resolve(WerewolfRotDoor1);
	if (A == nullptr)
	{
		return false;
	}
	const FElysiumEntity* B = World->Resolve(WerewolfRotDoor2);
	if (B == nullptr)
	{
		return false;
	}
	const FVector Pa = A->Origin / ElysiumMove::U;
	const FVector Pb = B->Origin / ElysiumMove::U;
	const float Dx = static_cast<float>(FMath::Abs(Pb.X - Pa.X));
	const float Dy = static_cast<float>(FMath::Abs(Pb.Y - Pa.Y));
	const float Dz = static_cast<float>(FMath::Abs(Pb.Z - Pa.Z));
	// Retail's ladder verbatim: `if (dx <= dy) { if (dy <= dz) goto C; B; } else if (dx <= dz) C;
	// else A;` — three arms, each adding a quarter of the two it did not pick.
	float Distance;
	if (Dx <= Dy)
	{
		if (Dy <= Dz)
		{
			Distance = (Dy + Dx) * WerewolfDoorMinorAxisWeight + Dz;
		}
		else
		{
			Distance = (Dz + Dx) * WerewolfDoorMinorAxisWeight + Dy;
		}
	}
	else if (Dx <= Dz)
	{
		Distance = (Dy + Dx) * WerewolfDoorMinorAxisWeight + Dz;
	}
	else
	{
		Distance = (Dz + Dy) * WerewolfDoorMinorAxisWeight + Dx;
	}
	return !(Distance <= WerewolfDoorMaxDistanceUnits);
}

void FElysiumNpc::FUN_103d9c90(FVector& OutPositionUnits)
{
	// `0x103d9c90`, the frame-memoised chase position:
	//
	//     if (field_0x6670 != gpGlobals->framecount) {         // DAT_1070b22c + 0x1e0
	//         enemy = GetEnemy();                              // vtable +0x29c, slot 167
	//         if (enemy) {
	//             tolerance = m_flGoalTolerance (+0x6320);
	//             cached    = enemy->GetAbsOrigin();           // vtable +0x364, slot 217
	//             field_0x6674/0x6678/0x667c = cached;
	//             ChaseLeadTolerance(this, enemy, cached, &cached, &tolerance);   // thunk 0x102c3b50
	//         }
	//         field_0x6670 = gpGlobals->framecount;
	//     }
	//     *param_1 = field_0x6674/0x6678/0x667c;
	//
	// Two retail details kept verbatim:
	//   * the frame stamp is written **whether or not there was an enemy**, so a werewolf with no
	//     enemy answers the previous frame's cached point for the rest of this frame rather than
	//     recomputing;
	//   * the goal tolerance is read into a local, handed to `0x102c3b50` and then **dropped** — the
	//     helper's only other output is the position, so the tolerance round-trip is dead.
	//
	// **Seam:** `EngineFrameNumber()` answers `INDEX_NONE`, which never equals the stored stamp, so
	// the recompute runs on every call. That is the named decision on the declaration: at retail's
	// one-call-per-frame rate it is retail's own behaviour, and the alternative — a constant stamp —
	// would freeze the cache after its first fill, which is the one thing retail never does.
	const int32 Frame = EngineFrameNumber();
	// `INDEX_NONE` is the seam's "there is no frame number", and it is the STORED value on a body
	// that has never been asked — so a bare `!=` would read "fresh" on the very first call and
	// answer a zero vector for ever. The staleness test is therefore explicit: no frame number
	// means always stale, which is the named decision on the declaration.
	const bool bStale = Frame == INDEX_NONE || WerewolfChaseFrame != Frame;
	if (bStale)
	{
		FElysiumEntity* Enemy = World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
		if (Enemy != nullptr)
		{
			float Tolerance = ScheduleHost.GoalToleranceCm;
			WerewolfChasePosUnits = Enemy->Origin / ElysiumMove::U;
			ChaseLeadTolerance(Enemy, WerewolfChasePosUnits * ElysiumMove::U, Tolerance);
			// The tolerance is dropped, exactly as retail drops it.
		}
		WerewolfChaseFrame = Frame;
	}
	OutPositionUnits = WerewolfChasePosUnits;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VZombie` — `0x103e0980`, `0x103e1080`, `0x103e12c0`, `0x103e12f0`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::FUN_103e0980(int32 InZombieAiType)
{
	// `0x103e0980`, `CNPC_VZombie::SetZombieAIType` — the writer behind the `ZombieAIType` mapper
	// keyvalue (`+0x6678`, datamap `FIELD_INTEGER`, key `ZombieAIType`):
	//
	//     if (param_1 == 4) param_1 = RandomInt(1, 3);
	//     m_iZombieAIType = param_1;
	//     switch (param_1) { case 2: case 3: case 5: case 6: PushOrder(this, 1, false); }   // 0x102ae840
	//
	// **4 is the "pick one for me" value** and is never stored: a mapper who writes 4 gets 1, 2 or 3
	// at spawn. The reroll is `RandomInt(1, 3)`, inclusive at both ends, so 4 can produce 2 or 3 and
	// therefore reach the side effect it could not have reached directly.
	//
	// The four values that push an order are 2, 3, 5 and 6 — NOT a contiguous band, and 1 and 4 are
	// the two that do not. `0x102ae840` is family **Schedule**'s scripted-order push, ported as
	// `PushScriptedScheduleOrder`; the arguments are the literal order id 1 and a false flag.
	int32 Type = InZombieAiType;
	if (Type == ZombieAiTypeRerolled)
	{
		Type = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.RandRange(ZombieAiTypeRerollMin, ZombieAiTypeRerollMax);
	}
	ZombieAiType = Type;
	switch (Type)
	{
	case 2:
	case 3:
	case 5:
	case 6:
		// `thunk_FUN_102ae840(this, 1, '\0')`.
		Mind.ForceStateChange();
		ElysiumStub::Fired(TEXT("method"),
			TEXT("CNPC_VZombie::SetZombieAIType 0x103e0980 order push 0x102ae840"),
			DebugString(), FString::Printf(TEXT("type=%d order=1"), Type),
			TEXT("0002/29c-1: family Schedule owns the scripted-order push"));
		break;
	default:
		break;
	}
}

bool FElysiumNpc::FUN_103e1080(bool bArg)
{
	// `0x103e1080`, `CNPC_VZombie`'s slot 510 `ShouldPlayFloatSound`:
	//
	//     m_iFloatSoundFrequency (+0x10e8) = 9;
	//     if (IsMoaning(this)) return false;                            // thunk 0x102c1170
	//     if (Resolve(+0x1538) && +0x153c != -1) return false;          // a live cached float sound
	//     if (IsUnconscious()) return false;
	//     if (m_bfAINPCFlags & 0x20000) return false;                   // SLEEPING
	//     player = Resolve(m_hClosestPlayer);  if (!player) return false;
	//     if (Resolve(player->+0xfe8)) return false;                    // the player already has one
	//     <lazily register the "Float Sound Info" keyvalues block, once>
	//     <roll _DAT_10940490 once, from that block>
	//     if (m_flPlayerDist <= _DAT_10940490) return CAI_BaseNPC::ShouldPlayFloatSound(param_1);
	//     return false;
	//
	// **The frequency write happens FIRST and on every call**, before any gate — so even a zombie
	// that refuses has set `m_iFloatSoundFrequency` to 9. That ordering is the row's main fact.
	//
	// The two `DAT_10940495` bits are a one-time lazy init of a KeyValues block named
	// `"Float Sound Info"` and of the distance threshold `_DAT_10940490` read out of it. Both live
	// in `.data` and are filled at RUNTIME, so the threshold is **UNRECOVERED** — it is not a
	// `.rdata` constant that could be read out of the image.
	//
	// **NAMED DECISION**: the threshold is treated as 0.0, and `m_flPlayerDist <= 0.0` is false for
	// any positive distance, so the body refuses. That is the conservative arm — it plays no sound
	// this substrate cannot attribute to a recovered number — and it is stated rather than papered
	// over. `ShouldPlayFloatSound` (family Sounds' base) is still the delegate the accepting arm
	// takes, so the day the KeyValues block is read nothing else moves.
	FloatSoundFrequency = ZombieFloatSoundFrequency;   // +0x10e8, unconditional and first

	if (Species2IsUnconscious(*this))
	{
		return false;
	}
	if (NpcFlags.Has(ZombieFloatSoundBlockingFlag))
	{
		return false;
	}
	const FElysiumEntity* ClosestPlayer = World != nullptr
		? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
	if (ClosestPlayer == nullptr)
	{
		return false;
	}
	// `_DAT_10940490` — UNRECOVERED, a runtime KeyValues read. See the note above.
	constexpr float ZombieFloatSoundMaxPlayerDistUnits = 0.0f;
	if (Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U
		<= ZombieFloatSoundMaxPlayerDistUnits)
	{
		return ShouldPlayFloatSound();
	}
	(void)bArg;
	return false;
}

bool FElysiumNpc::SpeciesShouldPlayFloatSound(bool& OutAnswer)
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(510);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103e1080")) != 0)
	{
		return false;
	}
	OutAnswer = FUN_103e1080(/*bArg=*/false);
	return true;
}

void FElysiumNpc::FUN_103e12c0(FElysiumEntity* Victim)
{
	// `0x103e12c0`, `CNPC_VZombie`'s slot 25, twenty-two bytes:
	//     FireOutput(&m_OnAttackedVictim (+0x66e8), param_1, this, 0);   // thunk 0x100cd660
	//
	// **No base forward.** Slot 25's base is unnamed (`Slot25` in the generated surface) and this
	// override replaces it outright, so whatever the base did for every other class does not happen
	// for a zombie — the only effect is the mapper-visible output.
	//
	// `m_OnAttackedVictim` is a datamap `FIELD_OUTPUT` with the mapper key `OnAttackedVictim`; the
	// ACTIVATOR is `param_1` (the victim) and the CALLER is this NPC, which is the ordinary
	// `COutputEvent::FireOutput` argument order and is preserved.
	FireOutput(ZombieOnAttackedVictim,
		Victim != nullptr ? Victim->Handle : FElysiumEntityHandle());
}

void FElysiumNpc::FUN_103e12f0(FElysiumEntity* Victim)
{
	// `0x103e12f0`, slot 26 — byte-identical to slot 25's `0x103e12c0`, firing the SAME output from
	// a second slot. Two vtable entries, one behaviour, and it is called rather than restated.
	FUN_103e12c0(Victim);
}

bool FElysiumNpc::SpeciesSlot25(FElysiumEntity* Arg)
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(25);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103e12c0")) != 0)
	{
		return false;
	}
	FUN_103e12c0(Arg);
	return true;
}

bool FElysiumNpc::SpeciesSlot26(FElysiumEntity* Arg)
{
	const FSpeciesSlotRow* Row = SpeciesSlotRow(26);
	if (Row == nullptr || FCString::Strcmp(Row->Address, TEXT("0x103e12f0")) != 0)
	{
		return false;
	}
	FUN_103e12f0(Arg);
	return true;
}
