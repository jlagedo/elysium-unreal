// `CBaseAnimating (and CBaseToggle)`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumAnimatingSlots.inl` (a slot body) or in `ElysiumAnimatingSlotBodies.inl`.

#include "ElysiumAnimating.h"

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

// --- Moved from `ElysiumNpcBaseLifecycle.cpp` (story 5 step 6) ---

// slot 137 `CBaseAnimating* GetBaseAnimating()` — 0x1004fc50
FElysiumEntity* FElysiumAnimating::GetBaseAnimating()
{
	// `return this;` — retail's default answers itself. This leaf IS on the animating chain
	// (`FElysiumAnimating`), so the answer is the same entity.
	return this;
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
	// The navigator's own step (`m_pNavigator->vtable[5]`, `CAI_Navigator::Move` `0x102eff40`) on an
	// NPC: the route's end reaches the task that waits on it (story 8 wave 2).
	if (FElysiumNpcBase* const Npc = AsNpcBase())
	{
		// The motor's per-think upkeep first (0019/6 fix 3): slot 69 re-asked and `+0x38` re-read
		// while a kernel move is live, and slot 15's facing blend handed to the body.
		Npc->MotorThinkUpkeep();
		Npc->NavigatorMoveStep();
	}
}

// --- Moved from `ElysiumNpcBaseSounds10.cpp` (story 5 step 6) ---

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

// --- The speed words' readers (spec 0002 V4a, lane A2) ---

// slot 242 `float GetIdealYawSpeed()` — 0x100916a0
float FElysiumAnimating::GetIdealYawSpeed()
{
	// `0x100916a0`: `return m_flYawSpeed` (+0x560), a plain read. The word is kept on the NPC beside
	// its sequence words (`FElysiumNpcBase::YawSpeed`, written by `StudioFrameAdvance 0x1008f120`
	// and `ResetSequenceInfo 0x10090950`); an animating entity that is no NPC has no sequence clock
	// in this port and answers 0.
	const FElysiumNpcBase* const Npc = AsNpcBase();
	return Npc != nullptr ? Npc->YawSpeed : 0.f;
}

// slot 248 `float GetIdealSpeed() const` — 0x10091740
float FElysiumAnimating::GetIdealSpeed() const
{
	// `0x10091740`: `return m_flGroundSpeed` (+0x654), with no playback-rate term. The word is kept
	// on the NPC (`FElysiumNpcBase::GroundSpeed`) in CENTIMETRES per second -- the bake's unit;
	// retail's is Source units per second -- so this answers cm/s, as `GroundSpeedCm()` does.
	const FElysiumNpcBase* const Npc = AsNpcBase();
	return Npc != nullptr ? Npc->GroundSpeed : 0.f;
}

// Slot 247, 0x10090c80 -> entity slot 15 0x1009af40, never the motor's size setter.
void FElysiumAnimating::SetAttackExtentsForSequence(int32 ExtentsSequence)
{
	if ((EntityFlags2Word & 4u) == 0) return; // 0x10090c80 GetFlags2
	const FElysiumNpc* const ExtentsNpc = AsNpc(); // 0x10090c80
	FVector BoundsMinCm, BoundsMaxCm; // 0x10090c80
	if (ExtentsNpc == nullptr || !ExtentsNpc->SequenceBounds(ExtentsSequence, BoundsMinCm, BoundsMaxCm))
		return; // 0x10090c80 no seqdesc; J2b live bbox seam returns false, no partition write
	FVector ExtentsCm(FMath::Max(FMath::Abs(BoundsMinCm.X), BoundsMaxCm.X),
		FMath::Max(FMath::Abs(BoundsMinCm.Y), BoundsMaxCm.Y),
		FMath::Max(FMath::Abs(BoundsMinCm.Z), BoundsMaxCm.Z)); // 0x10090c80
	ExtentsCm.X = FMath::Sqrt(ExtentsCm.X * ExtentsCm.X + ExtentsCm.Y * ExtentsCm.Y); // 0x10090c80
	ExtentsCm.Y = ExtentsCm.X; // 0x10090c80 radial XY
	FVector CollisionMinUnits, CollisionMaxUnits; // 0x10090c80
	FElysiumNpcBase::RetailCollisionExtents(*this, CollisionMinUnits, CollisionMaxUnits); // 0x100dc830
	const FVector CollisionMaxCm = CollisionMaxUnits * ElysiumMove::U; // 0x10090c80
	for (int32 ExtentsAxis = 0; ExtentsAxis < 3; ++ExtentsAxis)
		ExtentsCm[ExtentsAxis] = FMath::Max(ExtentsCm[ExtentsAxis] - CollisionMaxCm[ExtentsAxis], 0.0); // 0x10090c80
	SetAttackExtents(ExtentsCm); // 0x1009af40 entity extents/partition seam, no motor write
}
