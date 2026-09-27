// `CBaseAnimating (and CBaseToggle)`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumAnimatingSlots.inl` (a slot body) or in `ElysiumAnimatingSlotBodies.inl`.

#include "ElysiumPlayer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLifecycleShared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSounds10Shared.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcBaseEntityChain.cpp` (story 5 step 6) ---

float FElysiumAnimating::Slot135(float Interval)
{
	// 0x101c10d0, slot 135 — retail's named MOVE-REBOUND easing, and the whole body.
	//
	// Five gates, all of them in retail's order and all of them strict:
	//   Interval > 0, m_flMoveDoneTime > 0, m_flMoveReboundStartTime > 0,
	//   m_flMoveReboundStartTime < m_flMoveDoneTime, m_flMoveReboundDuration > 0.
	// Then `t = (Interval + m_flLocalTime) - m_flMoveReboundStartTime`, clamped ABOVE at the
	// duration and refused at or below zero (the early `return Interval`).
	//
	// The blend is `f(t) = (t*t + 1)*t - (t/D)*(D*D + 1)*t` — a cubic minus a linear, with the
	// linear term scaled so `f(D) == 0`. It is applied to the rebound velocity to produce an
	// OFFSET from the final destination, and the velocity written is that offset over `Interval`:
	//
	//   SetLocalVelocity((m_vecFinalDest + f(t)*m_flMoveReboundVelocity - GetLocalOrigin()) / Interval)
	//
	// and the same shape again for the angular half against `m_vecFinalAngle`, slot 221 and
	// `SetLocalAngularVelocity`. Each half runs only when its rebound triple is not all zero.
	//
	// The answer is ALWAYS `Interval`, on every path.
	//
	// SEAM: `m_flMoveDoneTime`, `m_flMoveReboundStartTime`, `m_flMoveReboundDuration`,
	// `m_flMoveReboundVelocity`, `m_flMoveReboundAngVelocity`, `m_vecFinalDest` and
	// `m_vecFinalAngle` are `CBaseEntity`'s mover words and no port member claims one
	// (`docs/vtmb/npc-kernel/layout.md`); they are read through `MoveReboundState()` below, which
	// answers the resting state and makes the first gate refuse. The arithmetic is stood as a static
	// so the formula is assertable without inventing a mover.
	FMoveRebound Rebound;
	if (!MoveReboundState(Rebound))
	{
		return Interval;
	}
	if (!(Interval > NpcKernelEntityChainShared::GChainZero && Rebound.MoveDoneTime > NpcKernelEntityChainShared::GChainZero
		&& Rebound.StartTime > NpcKernelEntityChainShared::GChainZero && Rebound.StartTime < Rebound.MoveDoneTime
		&& Rebound.Duration > NpcKernelEntityChainShared::GChainZero))
	{
		return Interval;
	}
	float T = (Interval + Rebound.LocalTime) - Rebound.StartTime;
	if (T <= NpcKernelEntityChainShared::GChainZero)
	{
		return Interval;
	}
	if (T > Rebound.Duration)
	{
		T = Rebound.Duration;
	}
	const float Blend = MoveReboundBlend(T, Rebound.Duration);

	if (Rebound.Velocity != FVector::ZeroVector)
	{
		const FVector Destination = Rebound.FinalDest + Blend * Rebound.Velocity;
		SetLocalVelocity((Destination - Rebound.LocalOrigin) * (NpcBaseEntityChainShared::GChainOne / Interval));
	}
	if (Rebound.AngVelocity != FVector::ZeroVector)
	{
		const FVector Destination = Rebound.FinalAngle + Blend * Rebound.AngVelocity;
		SetLocalAngularVelocity((Destination - Rebound.LocalAngle) * (NpcBaseEntityChainShared::GChainOne / Interval));
	}
	return Interval;
}

float FElysiumAnimating::MoveReboundBlend(float T, float Duration)
{
	// The easing inside `0x101c10d0`, on its own so the formula is assertable:
	//     (t*t + 1) * t   -   (t / D) * (D*D + 1) * t
	// `f(0) == 0` and `f(D) == 0`, and it peaks between them — a rebound that leaves and returns.
	return (T * T + NpcBaseEntityChainShared::GChainOne) * T - (T / Duration) * (Duration * Duration + NpcBaseEntityChainShared::GChainOne) * T;
}

bool FElysiumAnimating::MoveReboundState(FMoveRebound& Out) const
{
	// SEAM: `CBaseEntity`'s mover words (`m_flMoveDoneTime`, `m_flMoveReboundStartTime`,
	// `m_flMoveReboundDuration`, `m_flMoveReboundVelocity`, `m_flMoveReboundAngVelocity`,
	// `m_vecFinalDest`, `m_vecFinalAngle`) have no port member — nothing in this substrate stands a
	// `CBaseToggle`-style mover on the NPC line. The resting state is all zeroes, which makes slot
	// 135's first gate refuse, and refusing is what retail does for an NPC that is not rebounding.
	Out = FMoveRebound();
	Out.LocalTime = static_cast<float>(LocalTime);
	Out.LocalOrigin = Origin;
	Out.LocalAngle = Angles;
	return false;
}

void FElysiumAnimating::SetLocalVelocity(const FVector& NewVelocity)
{
	// `CBaseEntity::SetLocalVelocity`, the write slot 135's linear half ends on. The port's
	// `FElysiumEntity::Velocity` IS that word.
	Velocity = NewVelocity;
}

void FElysiumAnimating::SetLocalAngularVelocity(const FVector& NewAngularVelocity)
{
	// `CBaseEntity::SetLocalAngularVelocity`, the angular half's write. `AngularVelocity` is
	// `avelocity`.
	AngularVelocity = NewAngularVelocity;
}

// --- Moved from `ElysiumNpcBaseLifecycle.cpp` (story 5 step 6) ---

// slot 137 `CBaseAnimating* GetBaseAnimating()` — 0x1004fc50
FElysiumEntity* FElysiumAnimating::GetBaseAnimating()
{
	// `return this;` — retail's default answers itself. This leaf IS on the animating chain
	// (`FElysiumAnimating`), so the answer is the same entity.
	return this;
}

// slot 152 `float GetDelay()` — 0x1004fc10
float FElysiumAnimating::GetDelay()
{
	// `return (float10)*(float *)(param_1 + 0x500);` — a plain read of the `CBaseDelay` field below
	// the NPC word table. SEAM (declared, unwritten): this runtime carries an output's delay on the
	// WIRE (`FElysiumOutputDef`) rather than on the entity, so nothing writes `EntityDelay`.
	return EntityDelay;
}

FElysiumNpcBase::EKeyValueArm FElysiumAnimating::ClassifyKeyValue(const FString& Key, FString& OutKey)
{
	// 0x1009e430, arm by arm. The FIRST thing the body does is truncate the key at a `#`
	// (`FUN_10431f30(param_1, '#')` then `*p = 0`), so every comparison below sees the truncated
	// name — and `"origin#2"` is `"origin"`.
	int32 Hash = INDEX_NONE;
	OutKey = Key.FindChar(TEXT('#'), Hash) ? Key.Left(Hash) : Key;

	// Retail guards the first two with `if (*param_1 == 'r')`, which is an optimisation and not a
	// rule: a key that reaches them starts with `r` by definition.
	if (OutKey.Equals(TEXT("rendercolor"), ESearchCase::IgnoreCase)
		|| OutKey.Equals(TEXT("rendercolor32"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::RenderColor;
	}
	if (OutKey.Equals(TEXT("renderamt"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::RenderAmt;
	}
	if (OutKey.Equals(TEXT("disableshadows"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::DisableShadows;
	}
	if (OutKey.Equals(TEXT("mins"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Mins;
	}
	if (OutKey.Equals(TEXT("maxs"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Maxs;
	}
	if (OutKey.Equals(TEXT("disablereceiveshadows"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::DisableReceiveShadows;
	}
	if (OutKey.Equals(TEXT("angle"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Angle;
	}
	if (OutKey.Equals(TEXT("angles"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Angles;
	}
	if (OutKey.Equals(TEXT("origin"), ESearchCase::IgnoreCase))
	{
		return EKeyValueArm::Origin;
	}
	// Nothing matched: retail walks the datamap chain (`vtable +0x148 GetDataDescMap`, following
	// `baseMap` at `+0xc`) and offers the key to each level's `ParseKeyvalue`. THIS RUNTIME ALREADY
	// DOES THAT — `FElysiumEntity::Construct` applies each raw keyvalue through the class-chain
	// field table (`FElysiumClassRegistry::FindField`), which is the same walk over the same data.
	// So this arm names the port's own path rather than standing a second one.
	//
	// **Not ported:** the `ent_debugkeys` cvar arm (`DAT_106cf424`), which `Msg`s every matched and
	// unmatched key for one classname. It is a console diagnostic with no game-visible effect.
	return EKeyValueArm::DataMap;
}

// --- Moved from `ElysiumNpcBaseMotor.cpp` (story 5 step 6) ---

void FElysiumAnimating::MoveDone()
{
	// slot 133. `CAI_BaseNPC::FUN_101c1720` `0x101c1720` (read from the listing — the decompiler
	// could not recover the tail's jump table):
	//     MoverData_CurrPos = MoverData_TargetPos;          // +0x498 <- +0x494
	//     if (m_movementType == 1) LinearMoveDone();        // +0x558
	//     else if (m_movementType == 2) AngularMoveDone();
	//     m_movementType = 0;
	//     if (m_pfnMoveDone) (this->*m_pfnMoveDone)();      // +0x114
	//
	// The base slot-133 body `0x10026c50` is the last line alone, a tail JMP through `+0x114`.
	//
	// **SEAM.** None of the four words is an NPC's: +0x494/+0x498 and +0x558 are `CBaseEntity`'s
	// func_-mover state, which this runtime carries on `FElysiumMover` and which an NPC is never a
	// party to, and +0x114 is a member-function-pointer think vocabulary this runtime does not have.
	// What is recorded is that the dispatch was reached, which is the whole of what slot 133 does
	// for an NPC.
	++MotorSeams.MoveDone;
}

void FElysiumAnimating::PerformMovement(float Interval, int32 MoveFlags)
{
	// `CAI_BaseNPC::PerformMovement` `0x1026c120` — VProf push/pop and an `rdtsc` pair around ONE
	// statement: `m_pNavigator->vtable[0x14/4 = 5](param_1, param_2)`, both parameters forwarded
	// untouched. The profiler bookkeeping is retail's own mechanism; the move step is the rule.
	//
	// **SEAM**: `IElysiumNpcMotor` has no per-interval move step to delegate to — it integrates on
	// the actor tick, which `FElysiumScriptedCharacter::SyncMovingRecord` states as this runtime's
	// named divergence from retail's "the entity origin IS the body". The delegate is recorded.
	MotorSeams.PerformMovementInterval = Interval;
	(void)MoveFlags;
	++MotorSeams.PerformMovement;
}

// --- Moved from `ElysiumNpcBaseSounds10.cpp` (story 5 step 6) ---

FString FElysiumAnimating::FormatKeyValueVector(const FVector& Value)
{
	// `CAISound::FUN_1009eca0` (`0x1009eca0`), 212 bytes. Past the `CBaseEntity::KeyValue`
	// scope-trace push (`s_CBaseEntity__KeyValue_105555f4`, the entity's `m_iName`, or
	// `"NULL ENTITY"` when `this` is null and the empty string when unnamed — this runtime has no
	// scope-trace stack, and family Facing already recorded that the push is a crash-report
	// breadcrumb with no game-visible effect), the WHOLE body is
	// `Q_snprintf(buf, 256, "%f %f %f", (double)x, (double)y, (double)z)` with the literal at
	// `0x10555584`, then a dispatch of this object's OWN slot 110 (`vtable +0x1b8`) with the buffer.
	//
	// The three floats are widened to `double` by the varargs call, and `%f` is C's six-decimal
	// default; `FString::Printf` matches both. The 256-byte buffer is retail's stack frame and has
	// no observable effect for three finite floats.
	return FString::Printf(TEXT("%f %f %f"), static_cast<float>(Value.X),
		static_cast<float>(Value.Y), static_cast<float>(Value.Z));
}

FString FElysiumAnimating::FormatKeyValueFloat(float Value)
{
	// `CAISound::FUN_1009ebb0` (`0x1009ebb0`), 190 bytes and the same shape with the format at
	// `0x10554f28`, which is `"%f"`.
	return FString::Printf(TEXT("%f"), Value);
}

// `CAI_BaseNPC::FUN_1004fbb0` (`0x1004fbb0`), slot 108, and CAI_BaseNPCTroika shares it. THIRTY-EIGHT
// bytes: the whole body is a tail call into `CAISound::FUN_1009eca0` — it does not format anything
// itself. The checklist's walk described the callee's body as this one's; the correction is that
// slot 108 on the NPC line is a pure forward and the formatting plus the slot-110 dispatch both
// belong to `0x1009eca0`.
//
// The forward is a `__thiscall` to a DIFFERENT class's body on the same object, so the slot 110 it
// reaches is THIS object's — `FElysiumAnimating::KeyValue(const TCHAR*, const TCHAR*)` below.
bool FElysiumAnimating::KeyValue(const TCHAR* Key, FVector Value)
{
	return KeyValue(Key, *FormatKeyValueVector(Value));
}

// `CAI_BaseNPC::FUN_1004fbf0` (`0x1004fbf0`), slot 109. THIRTEEN bytes — the same pure forward, into
// `CAISound::FUN_1009ebb0`.
//
// Both `0x1009eca0` and `0x1009ebb0` return `void` in retail and both of these slots are declared
// `bool`; the forwarded value is whatever slot 110 answered, which is what this returns.
bool FElysiumAnimating::KeyValue(const TCHAR* Key, float Value)
{
	return KeyValue(Key, *FormatKeyValueFloat(Value));
}

// `CAI_BaseNPC::FUN_101c1480` (`0x101c1480`), slot 110 on the Troika line (77 classes). 114 bytes,
// three arms, read off the LISTING because the decompiled C loses which store is which:
//
//   1. `__strcmpi(key, "lip")` (`0x10561fc0`) → `FSTP [ESI + 0x504]` (`101c14a4`), `return true`.
//   2. `__strcmpi(key, "distance")` (`0x1053f4c4`) → `FSTP [ESI + 0x4fc]` (`101c14d0`),
//      `return true`.
//   3. anything else → `CBaseEntity::KeyValue` (`0x1009e430`), whose answer is returned verbatim.
//
// The value is `atof`'d (`0x1043136f`) in both writing arms, so a non-numeric value writes 0.0 and
// still answers true — retail's own behaviour, kept.
//
// `__strcmpi` is case-INSENSITIVE, which is why `Lip` and `DISTANCE` match.
bool FElysiumAnimating::KeyValue(const TCHAR* Key, const TCHAR* Value)
{
	if (Key == nullptr)
	{
		// Retail would fault on a null key inside `__strcmpi`. CRASH GUARD: refuse instead, which
		// is the arm a key nothing matched takes anyway.
		return false;
	}
	if (FCString::Stricmp(Key, TEXT("lip")) == 0)
	{
		Lip = Value != nullptr ? FCString::Atof(Value) : 0.f;
		return true;
	}
	if (FCString::Stricmp(Key, TEXT("distance")) == 0)
	{
		MoveDistance = Value != nullptr ? FCString::Atof(Value) : 0.f;
		return true;
	}
	return BaseEntityKeyValue(Key, Value);
}

bool FElysiumAnimating::BaseEntityKeyValue(const TCHAR* Key, const TCHAR* Value)
{
	// `CBaseEntity::KeyValue` (`0x1009e430`). Family **Lifecycle** recovered the classification and
	// the `#` truncation retail performs FIRST; this is that classification, dispatched.
	FString Truncated;
	const EKeyValueArm Arm = ClassifyKeyValue(FString(Key != nullptr ? Key : TEXT("")), Truncated);
	if (Arm != EKeyValueArm::DataMap)
	{
		// The nine literal arms — `rendercolor`, `renderamt`, `disableshadows`,
		// `disablereceiveshadows`, `mins`, `maxs`, `angle`, `angles`, `origin`. Every one of them
		// writes a `CBaseEntity` word (`m_clrRender`, `m_fEffects`, the collision bounds, the abs
		// angles, the abs origin) and returns true. They are `CBaseEntity`'s story, NOT a row of
		// this kernel's closure, so none is run here — but the ANSWER is retail's, true, because a
		// key that matched is a key the caller must not treat as unhandled.
		return true;
	}
	// The `DataMap` arm: retail walks `GetDataDescMap()` (`vtable +0x148`) down `baseMap` and offers
	// the key to each level's `ParseKeyvalue`. `FElysiumEntity::Construct` applies a map's raw
	// keyvalues through the class-chain field table, which is the same walk over the same data, so
	// this arm IS that table.
	if (Class == nullptr || Key == nullptr)
	{
		return false;
	}
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const FElysiumFieldAccessor* Acc = Reg.FindField(*Class, FName(*Truncated));
	if (Acc == nullptr || !Acc->Set)
	{
		// Retail's own "no level of the chain claimed it" answer.
		return false;
	}
	Acc->Set(*this, FElysiumVariant::String(FString(Value != nullptr ? Value : TEXT(""))));
	return true;
}
