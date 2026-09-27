// `CAI_BaseNPC`'s bodies of the `Sounds10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSounds10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSounds10Shared.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `10268900` @ `10268a3a`: the filter word is `ammoFlags | 0x1000`.
	constexpr int32 GSounds10FireBulletsFilterBit = 0x1000;
	// The ammo-def flag that SUPPRESSES spread for everyone but a player with a zeroed `+0x1e78`.
	constexpr int32 GSounds10AmmoFlagNoNpcSpread = 0x2000000;
	// `info+0xa0` bit 0, which latches the tracer arm on its own.
	constexpr int32 GSounds10TracerFlagAlways = 0x1;
	// The skill above which an NPC shooter latches the tracer arm (`2 < stat`).
	constexpr int32 GSounds10TracerSkillThreshold = 2;
	// `DAT_104994c8`, six floats read out of the pinned image: the divisor `info+0xa4` is divided
	// by, indexed by the ranged skill clamped to 0..5.
	constexpr float GSounds10SkillDivisor[6] = { 1.0f, 1.0f, 1.0f, 1.1f, 1.3f, 1.5f };
	// `_DAT_104454c0`, the numerator of the tracer's per-shot fraction AND the rejection sampler's
	// radius test. 1.0 in the image.
	constexpr float GSounds10One = 1.0f;
	// `0x10268170`'s four draws are `RandomFloat(0xbf000000, 0x3f000000)`.
	constexpr float GSounds10SpreadDrawMin = -0.5f;
	constexpr float GSounds10SpreadDrawMax = 0.5f;
	// NAMED DECISION: `FireBullets`' spread draws come off `EElysiumRngStream::Reaction`.
	// Retail draws from the one engine stream (`DAT_1070b244`); this runtime splits streams so two
	// systems cannot walk each other's position, and `Reaction` is the combat-side stream (the
	// damage flinch's coin and jitter). The alternative — `NpcSchedule` — would move the idle
	// branch's position by four draws per bullet. Nothing fires a weapon in this runtime yet, so
	// the choice costs nothing today and is recorded here rather than rediscovered.
	constexpr EElysiumRngStream GSounds10SpreadStream = EElysiumRngStream::Reaction;
}

// --- Moved from `ElysiumNpcSounds10.cpp` (story 5 step 5) ---

FString FElysiumNpcBase::FormatKeyValueVector(const FVector& Value)
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

FString FElysiumNpcBase::FormatKeyValueFloat(float Value)
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
// reaches is THIS object's — `FElysiumNpcBase::KeyValue(const TCHAR*, const TCHAR*)` below.
bool FElysiumNpcBase::KeyValue(const TCHAR* Key, FVector Value)
{
	return KeyValue(Key, *FormatKeyValueVector(Value));
}

// `CAI_BaseNPC::FUN_1004fbf0` (`0x1004fbf0`), slot 109. THIRTEEN bytes — the same pure forward, into
// `CAISound::FUN_1009ebb0`.
//
// Both `0x1009eca0` and `0x1009ebb0` return `void` in retail and both of these slots are declared
// `bool`; the forwarded value is whatever slot 110 answered, which is what this returns.
bool FElysiumNpcBase::KeyValue(const TCHAR* Key, float Value)
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
bool FElysiumNpcBase::KeyValue(const TCHAR* Key, const TCHAR* Value)
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

bool FElysiumNpcBase::BaseEntityKeyValue(const TCHAR* Key, const TCHAR* Value)
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

bool FElysiumNpcBase::FireBulletsShooterIsPlayer() const
{
	// `+0x00a8 m_pPlayer`. Never an NPC. `FElysiumPlayer` is a different leaf entirely and does not
	// dispatch this slot.
	return false;
}

FVector FElysiumNpcBase::BulletSpreadOffset(const FVector& SpreadUnits, const FVector& RightAxis,
	const FVector& UpAxis)
{
	// `0x10268170`. The rejection sampler first, exactly as retail writes it: FOUR draws per
	// attempt, summed in pairs, redrawn while the pair is outside the unit disc.
	FRandomStream& Stream = ElysiumRng::Stream(GSounds10SpreadStream);
	float X = 0.f;
	float Y = 0.f;
	do
	{
		X = Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax)
			+ Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax);
		Y = Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax)
			+ Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax);
	}
	while (X * X + Y * Y > GSounds10One);

	// `this+0xa8` (`m_pPlayer`): a PLAYER shooter replaces BOTH spread components with
	// `ScaleField_0x1ddc()` (`0x10160680`, family **Lifecycle**). This leaf is never the player, so
	// the info's own pair stands and the arm is named rather than run.
	const float SpreadX = FireBulletsShooterIsPlayer()
		? static_cast<float>(ScaleField_0x1ddc()) : static_cast<float>(SpreadUnits.X);
	const float SpreadY = FireBulletsShooterIsPlayer()
		? static_cast<float>(ScaleField_0x1ddc()) : static_cast<float>(SpreadUnits.Y);

	return RightAxis * (SpreadX * X) + UpAxis * (SpreadY * Y);
}

// `CAISound::FUN_10268900` (`0x10268900`), slot 185, 1212 bytes, shared by `CAI_BaseNPC#185` and
// `CAI_BaseNPCTroika#185`. Ten steps, in retail's order:
//
//   1. `m_pBaseNPCTroika` (`+0x98`, `AsNpc()`) set → `--m_iFakeReloadCount` (`+0x65f0`) on it.
//   2. `info.m_iFlags (+0x58) = GetAmmoDef()->Flags(info.m_iAmmoType (+0x8c))`.
//   3. `info.m_pAttacker (+0x94)` defaults to the shooter when unset.
//   4. the two trace filters (`0x101c2c60`, `0x101c2c30`) and `DAT_1072cb48 = flags | 0x1000`.
//   5. `VectorVectors(info+0x14, right, up)` (`0x10138a90`).
//   6. open the per-victim tally (`0x1027f940`).
//   7. `for (repeat : info[0]) for (bullet : info[1])` — the pass below.
//   8. slot 186 (`vtable +0x2e8`) false → the tracer arm OR the trace-and-damage pass.
//   9. after EACH repeat, `RangedDamagePerVictim(victim, info, hits / info[1])` for every victim in
//      the tally — which is NOT cleared between repeats, so repeat 2 re-pays repeat 1's victims
//      with their accumulated counts. Retail's own behaviour, reproduced.
//  10. `0x10160560(m_pPlayer)` and free the tally.
//
// Step 8's gate is slot 186, `FElysiumNpcBase::Slot186` (family **Closure**), whose whole retail body
// (`0x100270c0`) is `return false;` — so the pass below always runs.
void FElysiumNpcBase::FireBullets(void* InInfo)
{
	FElysiumFireBulletsInfo* Info = static_cast<FElysiumFireBulletsInfo*>(InInfo);
	if (Info == nullptr)
	{
		// CRASH GUARD: retail dereferences the packet unconditionally. No caller in this runtime
		// passes null yet; refusing is the only answer that is not a fault.
		return;
	}

	// 1. The outstanding-shot counter, on the shooter's own NPC self-pointer.
	if (FElysiumNpc* const Troika = AsNpc())
	{
		--Troika->FakeReloadCount;
	}

	// 2. The ammo flags, WRITTEN back into the packet — every later arm reads them from there.
	Info->AmmoFlags = AmmoDefFlags(Info->AmmoType);

	// 3. The attacker default.
	if (!Info->Attacker.IsSet())
	{
		Info->Attacker = Handle;
	}

	// 4. The filter word. `DAT_1072cb48` is a file static in retail with this as its only writer in
	// the closure.
	FireBulletsFilterWord = Info->AmmoFlags | GSounds10FireBulletsFilterBit;

	// 5. The basis, from the SHOOTING DIRECTION and not from a set of angles: `0x10138a90` is
	// `VectorVectors`, not `AngleVectors`. (The checklist's walk named it `AngleVectors`; the body
	// at `0x10138a90` is the cross-product basis builder, and `info+0x14` is the same word the
	// per-shot direction is copied from two lines later, which a set of Euler angles could not be.)
	FVector RightAxis = FVector::ZeroVector;
	FVector UpAxis = FVector::ZeroVector;
	VectorVectors(Info->DirShooting, RightAxis, UpAxis);

	// 6. The tally: one `{ victim, hits }` pair per distinct victim, appended in first-hit order.
	TArray<FRangedDamagePerVictimCall> Tally;

	for (int32 Repeat = 0; Repeat < Info->Repeats; ++Repeat)
	{
		for (int32 Bullet = 0; Bullet < Info->Bullets; ++Bullet)
		{
			// 7a. The working direction starts as the forward, unchanged.
			Info->DirCurrent = Info->DirShooting;

			// 7b. The spread gate. `(flags & 0x2000000) == 0 || (m_pPlayer && m_pPlayer->+0x1e78
			// == 0)` — for an NPC shooter the second disjunct is dead, so the bit alone decides.
			// The offset is added UNSCALED: retail does not multiply it by the distance.
			const bool bNoSpreadBit = (Info->AmmoFlags & GSounds10AmmoFlagNoNpcSpread) != 0;
			if (!bNoSpreadBit || FireBulletsShooterIsPlayer())
			{
				Info->DirCurrent += BulletSpreadOffset(Info->Spread, RightAxis, UpAxis);
			}

			// 7c. The endpoint. `end = src + dir * distance`, component by component, in retail's
			// own x/z/y store order (which is unobservable and is written x/y/z here).
			Info->EndUnits = Info->SrcUnits + Info->DirCurrent * Info->DistanceUnits;

			// 7d. The ranged skill and the tracer latch.
			const int32 Skill = ShooterRangedSkill();
			bool bTracer = false;
			if ((Info->TracerFlags & GSounds10TracerFlagAlways) != 0
				|| (Skill > GSounds10TracerSkillThreshold && !FireBulletsShooterIsPlayer()))
			{
				bTracer = true;
				// `uVar11 & ((int)uVar11 < 0) - 1` below 6, `5` at or above it: the clamp to 0..5.
				const int32 Index = Skill < 6 ? (Skill < 0 ? 0 : Skill) : 5;
				Info->TracerScale = Info->TracerScale / GSounds10SkillDivisor[Index];
			}

			// 8. Slot 186 gates the whole emission half.
			if (Slot186())
			{
				continue;
			}
			if (bTracer && !Info->TracerName.IsEmpty())
			{
				// The tracer arm REPLACES the trace-and-damage pass: a bullet that draws a tracer
				// does no damage in this body at all.
				EmitBulletTracer(*Info, GSounds10One / static_cast<float>(Info->Bullets));
			}
			else
			{
				FireBulletsTracePass(*Info);
				if (Info->LastVictim.IsSet())
				{
					FRangedDamagePerVictimCall* Row = Tally.FindByPredicate(
						[Info](const FRangedDamagePerVictimCall& Candidate)
						{
							return Candidate.Victim.Index == Info->LastVictim.Index;
						});
					if (Row == nullptr)
					{
						FRangedDamagePerVictimCall New;
						New.Victim = Info->LastVictim;
						New.Fraction = 0.f;
						Row = &Tally.Add_GetRef(New);
					}
					// `*(int*)(entry + 4) += 1` — the hit count, carried in `Fraction` until the
					// division below turns it into one.
					Row->Fraction += 1.f;
				}
			}
		}

		// 9. The per-victim payout, once per REPEAT and guarded on a non-zero bullet count (the
		// guard is retail's own divide-by-zero check).
		if (Info->Bullets != 0)
		{
			for (const FRangedDamagePerVictimCall& Row : Tally)
			{
				RangedDamagePerVictim(Row.Victim, *Info,
					Row.Fraction / static_cast<float>(Info->Bullets));
			}
		}
	}

	// 10. `0x10160560(m_pPlayer)` — unreachable for an NPC shooter, and named so.
	if (FireBulletsShooterIsPlayer())
	{
		// SEAM: `0x10160560`, the player-side post-fire bookkeeping. No NPC reaches it.
	}
}

// --- Moved from `ElysiumNpcSounds10.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::AmmoDefFlags(int32 AmmoTypeIndex) const
{
	// `0x104276a0`: `(0 < i && i < m_nAmmoIndex) ? m_AmmoType[i].nFlags : 0`. No `CAmmoDef` here,
	// so `m_nAmmoIndex` is 0 and every index is out of range.
	(void)AmmoTypeIndex;
	return 0;
}

int32 FElysiumNpcBase::ShooterRangedSkill() const
{
	// `0x101cda50` → the local player, then its type-3 stat list's stat 3, else the empty static
	// list's `0`. Unjoined here; `0` is retail's own no-list answer.
	return 0;
}

void FElysiumNpcBase::FireBulletsTracePass(FElysiumFireBulletsInfo& Info)
{
	FFireBulletsTrace Trace;
	Trace.SrcUnits = Info.SrcUnits;
	Trace.EndUnits = Info.EndUnits;
	Trace.DirUnits = Info.DirCurrent;
	Trace.TracerScale = Info.TracerScale;
	Trace.FilterWord = FireBulletsFilterWord;
	FireBulletsTraces.Add(Trace);

	// `0x10267b60` writes `info+0xbc` with whatever it hit. Nothing traces here, so it stays unset —
	// the same word a bullet that hit world geometry leaves.
	Info.LastVictim = FElysiumEntityHandle();
}

void FElysiumNpcBase::EmitBulletTracer(const FElysiumFireBulletsInfo& Info, float Fraction)
{
	FBulletTracerCall Call;
	Call.Name = Info.TracerName;
	Call.EndUnits = Info.EndUnits;
	Call.Fraction = Fraction;
	Call.TracerScale = Info.TracerScale;
	BulletTracerCalls.Add(MoveTemp(Call));
}

void FElysiumNpcBase::RangedDamagePerVictim(const FElysiumEntityHandle& Victim,
	const FElysiumFireBulletsInfo& Info, float Fraction)
{
	(void)Info;
	FRangedDamagePerVictimCall Call;
	Call.Victim = Victim;
	Call.Fraction = Fraction;
	RangedDamagePerVictimCalls.Add(Call);
}

// --- Moved from `ElysiumNpcSounds10.cpp` (story 5 step 5) ---

void FElysiumNpcBase::VectorVectors(const FVector& Forward, FVector& OutRight, FVector& OutUp)
{
	// `0x10138a90`, Source's `VectorVectors`, with `_DAT_104454c4` (0.0) folded into the cross
	// products as the constant `up` axis's x and y.
	if (Forward.X == 0.0 && Forward.Y == 0.0)
	{
		// Retail's degenerate arm, verbatim: `right = (1,0,0)`, `up = (0, -forward.z, 0)`. Neither
		// is normalized and `up` is not perpendicular to anything; it is what the body writes.
		OutRight = FVector(1.0, 0.0, 0.0);
		OutUp = FVector(0.0, -Forward.Z, 0.0);
		return;
	}
	OutRight = FVector(Forward.Y, -Forward.X, 0.0);
	OutRight.Normalize();
	// `up = cross(right, forward)`, then normalized.
	OutUp = FVector(
		Forward.Z * OutRight.Y - Forward.Y * OutRight.Z,
		Forward.X * OutRight.Z - Forward.Z * OutRight.X,
		OutRight.X * Forward.Y - OutRight.Y * Forward.X);
	OutUp.Normalize();
}
