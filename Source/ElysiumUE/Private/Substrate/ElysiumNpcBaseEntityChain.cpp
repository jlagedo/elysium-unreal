// `CAI_BaseNPC`'s bodies of the `EntityChain` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseEntityChain.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_104454c0` — the image's shared `1.0f` (`docs/vtmb/animation_and_movers.md` line 659
	// reads the same word as `1.0f`; `docs/vtmb/npc-ai/shape.md` line 1029 "clamped up to
	// `_DAT_104454c0 = 1.0`").
	constexpr float GChainOne = 1.0f;
	// `_DAT_104454c8` = **80.0f** (`docs/vtmb/computer-terminals.md` line 475, `docs/vtmb/npc-ai/
	// conditions-and-states.md` line 460: "farther than 80 units (`_DAT_104454c8`)"). This is slot
	// 37's whole answer, which the ledger's `checklist-0-9.md` records as unrecovered; recovered
	// here from the same word's other readers, exactly as that row predicted it would be.
	constexpr float GChainSlot37 = 80.0f;
	// `_DAT_1045d650` = **1024.0f** (`docs/vtmb/computer-terminals.md` line 1508,
	// `docs/vtmb/npc-ai/conditions-and-states.md` line 885). Slot 550 `CoverRadius`'s answer.
	constexpr float GChainCoverRadius = 1024.0f;
	constexpr int32 GChainSolidBbox = 2;      // SOLID_BBOX
	// `FSOLID_NOT_SOLID`, the `GetSolidFlags()` bit slot 164 refuses on.
	constexpr int32 GChainSolidNotSolid = 0x10;
	// `MoveType_t` as slot 226 switches on it. 7 takes the read-back-from-physics arm; 1 and 8 take
	// `VPhysicsUpdatePusher`. Troika's enum is NOT stock Source's here, so the numbers are carried
	// as numbers and the names are not claimed.
	constexpr int32 GChainMoveTypePhysicsRead = 7;
	constexpr int32 GChainMoveTypePusherA = 1;
	constexpr int32 GChainMoveTypePusherB = 8;
}

// --- Moved from `ElysiumNpcEntityChain.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::PhysicsObjectPosition(const void* PhysicsObject, FVector& OutOrigin,
	FRotator& OutAngles) const
{
	// SEAM for `IPhysicsObject::GetPosition(&origin, &angles)` (the `+0x94` dispatch inside
	// `0x100b4f30`). No `IPhysicsObject` in this substrate; the generated slot signature hands the
	// pointer in as `void*` and nothing can be read off it.
	(void)PhysicsObject;
	OutOrigin = FVector::ZeroVector;
	OutAngles = FRotator::ZeroRotator;
	return false;
}

float FElysiumNpcBase::Slot37()
{
	// 0x10026710 — the whole body is `return (float10)_DAT_104454c8;`.
	//
	// The ledger's `checklist-0-9.md` records the value as unrecovered and names where it would be
	// recovered ("UpdateEnemyPos 0x10271900 and UpdateTargetPos 0x10271b10 read the same constant").
	// It is **80.0f**: `docs/vtmb/npc-ai/conditions-and-states.md` line 460 reads the same word as
	// "farther than `_DAT_104454c8 = 80.0` units from the goal point", and
	// `docs/vtmb/computer-terminals.md` line 475 as `CPropDoorknob`'s 80-unit break-off.
	return GChainSlot37;
}

FElysiumEntity* FElysiumNpcBase::Slot38(FElysiumEntity* Other)
{
	// 0x10026730 — `return this;`, ignoring the argument. The declared signature really is
	// `CBaseEntity* vfunc38(CBaseEntity*)`, so this is a genuine always-answers-itself default and
	// not a decompiler artefact: what the caller passes never reaches anything.
	(void)Other;
	return this;
}

void FElysiumNpcBase::SetAngles(float Pitch, float Yaw, float Roll)
{
	// 0x10026a50, slot 65 — the three-scalar overload. The BODY is the packing: it lays the three
	// scalars into one stack record and dispatches slot 64 (`+0x100`) with its address. The vtable
	// hop is retail's, so a species that replaced slot 64 is reached through it, and this is the
	// same body on 501 classes.
	SetAngles(FRotator(Pitch, Yaw, Roll));
}

void FElysiumNpcBase::Slot89()
{
	// 0x10026b70 — tail-jumps into `0x10146790`, which clears bytes +1 and +2 of the
	// `m_NetworkChangeState` record at `+0x01b0` (`docs/vtmb/npc-kernel/layout.md`): `m_bChanged`
	// and the second flag. The interval and the countdown at +4/+6 are NOT touched, and neither is
	// byte +0, which the static prop/brush Spawns own.
	NetworkChangeState.bChanged = false;
	NetworkChangeState.bByte2 = false;
}

bool FElysiumNpcBase::ReflectGauss()
{
	// 0x10026f20, slot 159 — `IsStandableSolid() && m_takedamage == 0`. Both terms, in retail's
	// order; `m_takedamage` is `+0x01fc`, family Damage's `TakeDamageMode`, and `0` is `DAMAGE_NO`.
	// So the answer is "a solid I could stand on that takes no damage" — world brush, not a body.
	return IsStandableSolid() && TakeDamageMode == 0;
}

bool FElysiumNpcBase::IsStandable()
{
	// 0x100b50a0, slot 164.
	//
	//   GetSolidFlags() & FSOLID_NOT_SOLID -> false, immediately
	//   GetSolid() is SOLID_BSP, SOLID_VPHYSICS or SOLID_BBOX -> true
	//   otherwise -> the shared helper
	//
	// The three solid tests are three SEPARATE dispatches of slot 92 in retail, in the order
	// 1, 6, 2, and the port keeps them that way. Note the asymmetry with the helper: slot 164 takes
	// `SOLID_VPHYSICS` as standable OUTRIGHT, while the helper asks the physics object — so a moving
	// physics prop is standable to slot 164 and not to `ReflectGauss`.
	if ((GetSolidFlags() & GChainSolidNotSolid) != 0)
	{
		return false;
	}
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidBsp)
	{
		return true;
	}
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidVPhysics)
	{
		return true;
	}
	if (GetSolid() == GChainSolidBbox)
	{
		return true;
	}
	return IsStandableSolid();
}

bool FElysiumNpcBase::CanStandOn(void* Edict)
{
	// 0x10026fb0, slot 165 — the `edict_t*` overload. It selects an ARGUMENT and dispatches slot 166
	// (`+0x298`), which is the `CBaseEntity*` overload and family Motor's body: the networkable at
	// `edict+0x40` if the edict and the networkable are both non-null, else literal 0. Retail
	// dispatches slot 166 on BOTH arms — a null edict is not a refusal, it is `CanStandOn(nullptr)`.
	if (Edict != nullptr)
	{
		return CanStandOn(EntityOfEdict(Edict));
	}
	return CanStandOn(static_cast<FElysiumEntity*>(nullptr));
}

void FElysiumNpcBase::VPhysicsUpdate(void* PhysicsObject)
{
	// 0x100b4f30, slot 226 — retail's per-movetype physics-tick ordering, on slot 94's answer.
	//
	//   movetype 7 : read the object's transform back (`IPhysicsObject +0x94`), warn on any
	//                component whose exponent field is all-ones (`& 0x7f800000 == 0x7f800000`,
	//                i.e. inf or NaN), then SetAbsOrigin (slot 216), SetAbsAngles (slot 218),
	//                PhysicsTouchTriggers(0) and PhysicsRelinkChildren — IN THAT ORDER.
	//   movetype 1 or 8 : `CBaseEntity::VPhysicsUpdatePusher(object)`.
	//   anything else   : nothing at all.
	//
	// The warn is `Msg("Infinite values from vphysics!...")` and it does NOT abort the arm: retail
	// prints and then writes the bad transform anyway, which is a fact a program can observe.
	const int32 MoveType = GetMoveType();
	if (MoveType == GChainMoveTypePhysicsRead)
	{
		FVector PhysOrigin = FVector::ZeroVector;
		FRotator PhysAngles = FRotator::ZeroRotator;
		if (PhysicsObjectPosition(PhysicsObject, PhysOrigin, PhysAngles))
		{
			if (!FMath::IsFinite(PhysOrigin.X) || !FMath::IsFinite(PhysOrigin.Y)
				|| !FMath::IsFinite(PhysOrigin.Z))
			{
				// `Msg(s_Infinite_values_from_vphysics__105591dc)` — and then the write anyway.
				UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Infinite values from vphysics! (%s)"),
					*DebugString());
			}
			SetAbsOrigin(PhysOrigin);
			SetAbsAngles(PhysAngles);
		}
		// The two calls run on this arm whether or not the transform read answered; retail's read
		// cannot fail, and the seam's refusal must not remove them from the sequence.
		PhysicsUpdateCalls.Add(TEXT("PhysicsTouchTriggers"));
		PhysicsUpdateCalls.Add(TEXT("PhysicsRelinkChildren"));
		return;
	}
	if (MoveType == GChainMoveTypePusherA || MoveType == GChainMoveTypePusherB)
	{
		VPhysicsUpdatePusher(PhysicsObject);
	}
}

void* FElysiumNpcBase::Slot240()
{
	// 0x1014f8b0, slot 240 — `return DAT_1072b360;`. One global word, not a literal, which is why
	// the generator could not emit it as a `default:`.
	return PythonInteropObject();
}

float FElysiumNpcBase::Slot135(float Interval)
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
		SetLocalVelocity((Destination - Rebound.LocalOrigin) * (GChainOne / Interval));
	}
	if (Rebound.AngVelocity != FVector::ZeroVector)
	{
		const FVector Destination = Rebound.FinalAngle + Blend * Rebound.AngVelocity;
		SetLocalAngularVelocity((Destination - Rebound.LocalAngle) * (GChainOne / Interval));
	}
	return Interval;
}

float FElysiumNpcBase::MoveReboundBlend(float T, float Duration)
{
	// The easing inside `0x101c10d0`, on its own so the formula is assertable:
	//     (t*t + 1) * t   -   (t / D) * (D*D + 1) * t
	// `f(0) == 0` and `f(D) == 0`, and it peaks between them — a rebound that leaves and returns.
	return (T * T + GChainOne) * T - (T / Duration) * (Duration * Duration + GChainOne) * T;
}

void FElysiumNpcBase::Slot266()
{
	// 0x100997f0, slot 266 — clear the three flinch records at `+0x07f4` (stride 0x1c). Two writes
	// per record and no more: the SEQUENCE to -1 and the EXPIRE TIME (word 6, `+0x18`) to
	// `curtime - 1.0`. The latch, the two fades and the pose parameter survive, which is why a
	// cleared record still remembers which pose parameter it drove.
	//
	// `_DAT_104454c0` is the image's shared `1.0f`, so the stamp is one second in the PAST: already
	// expired, on purpose.
	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	for (int32 Index = 0; Index < NumFlinchRecords; ++Index)
	{
		Flinch[Index].Sequence = -1;
		Flinch[Index].ExpireTime = Now - GChainOne;
	}
}

void FElysiumNpcBase::SetLayer(int32 SlotIndex, int32 Activity, int32 Sequence, bool bAutoKill)
{
	// 0x10099020, slot 268 — `CBaseAnimatingOverlay::SetLayer`. Family Anim already ported this body
	// field for field as `SetOverlayLayer` (`ElysiumNpcAnim.cpp`), because its own rows
	// dispatch through it and could not do so against a stub. This is the SLOT, and it is that body:
	// one spelling, so the two cannot drift.
	SetOverlayLayer(SlotIndex, Activity, Sequence, bAutoKill);
}

int32 FElysiumNpcBase::FirstGestureLayerOrRefusal() const
{
	// `GetFirstGestureLayer()` (slot 267, `0x10098a40`) answers 0 for every class in the hierarchy,
	// which family Anim recovered and every scan in that family relies on. Stated here rather than
	// dispatched, because slot 267's own port body is still 29c's stub and dispatching it would
	// report a stub for a fact that is already recovered.
	return 0;
}

int32 FElysiumNpcBase::FindLayerByOwner(int32 Activity)
{
	// 0x100994c0, slot 271 — `CBaseAnimatingOverlay::FindGestureLayer`. The scan STARTS at
	// `GetFirstGestureLayer()` and refuses outright when that is 4 or more; each slot is tested on
	// three terms in retail's order — the weight is not zero (`_DAT_104454c4`), the owner is not
	// `ACT_INVALID` (-1), and the owner is the activity asked for. -1 on a miss.
	//
	// Family Anim carries the scan as `FindGestureLayerByOwner`; this is the slot, and it is that
	// body behind retail's own starting index.
	if (FirstGestureLayerOrRefusal() >= ElysiumOverlay::NumSlots)
	{
		return INDEX_NONE;
	}
	return FindGestureLayerByOwner(Activity);
}

void FElysiumNpcBase::SetFlexWeight(int32 Index, float Value)
{
	// 0x100b5ba0, slot 279 — the INDEX overload, and the normalising half of the flex pair.
	//
	//   index < 0                      -> nothing
	//   index >= GetNumFlexControllers -> nothing
	//   no studio header               -> nothing
	//   max != min                     -> store (value - min) / (max - min)
	//   max == min                     -> store value unchanged
	//
	// Note what retail does NOT do: it does not clamp. A value outside the controller's authored
	// range stores outside 0..1 and slot 281 maps it straight back out again.
	//
	// Slot 281 (`GetFlexWeight(int)`, family Anim) is the exact inverse and reads the same range
	// through the same seam, so the round trip is lossless wherever the range is non-degenerate.
	if (Index < 0 || Index >= NumFlexControllers() || Index >= NumFlexWeightSlots)
	{
		return;
	}
	float Min = 0.f;
	float Max = 0.f;
	if (!FlexControllerRange(Index, Min, Max))
	{
		// Retail's `GetModelPtr()` null arm: the write is DROPPED, not stored raw.
		return;
	}
	FlexWeight[Index] = (Max != Min) ? (Value - Min) / (Max - Min) : Value;
}

void FElysiumNpcBase::ClearSceneEvents(void* Scene)
{
	// 0x100b5d80, slot 285. TWO bodies in one:
	//
	//   Scene == nullptr : the count at `+0x0a64` is set to 0 and NOTHING ELSE HAPPENS — no release,
	//                      no zeroing, no compaction. The records are still there; retail simply
	//                      stops counting them.
	//   Scene != nullptr : walk the array, and for every record whose SCENE (word 1) is this scene,
	//                      release the event (`0x10075b70`), zero words 0, 1 and the byte at 2,
	//                      `memmove` the tail down one stride and decrement both the count and the
	//                      cursor — so the compacted-in record is re-tested, which is what lets one
	//                      pass remove every event of a scene.
	if (Scene == nullptr)
	{
		// Retail's clear-all. The port's `TArray` has no separate count word, so emptying it is the
		// same observable state; `SceneEventsAllocated` is deliberately left alone, exactly as
		// retail leaves `+0x0a5c` alone.
		SceneEvents.Reset();
		return;
	}
	for (int32 Index = 0; Index < SceneEvents.Num(); )
	{
		if (SceneEvents[Index].Scene == static_cast<const FElysiumSceneData*>(Scene))
		{
			ReleaseSceneEvent(SceneEvents[Index]);
			SceneEvents.RemoveAt(Index);
			continue;   // retail's `iVar2 + -1` then `+1`: the cursor does not advance
		}
		++Index;
	}
}

void FElysiumNpcBase::RemoveSceneEvent(void* Event)
{
	// 0x100b6180, slot 287 — the same array keyed by POINTER EQUALITY on word 0 (the event), and
	// unlike slot 285 it stops at the FIRST match: the search loop returns, releases, compacts and
	// falls out. A second record carrying the same event would survive, which is a fact a program
	// can observe.
	//
	// A null `Event` is not special-cased here the way a null scene is in slot 285: it searches for
	// a record whose event pointer is null and removes the first one it finds.
	for (int32 Index = 0; Index < SceneEvents.Num(); ++Index)
	{
		if (SceneEvents[Index].Event == static_cast<const FElysiumSceneEvent*>(Event))
		{
			ReleaseSceneEvent(SceneEvents[Index]);
			SceneEvents.RemoveAt(Index);
			return;
		}
	}
}

const FVector* FElysiumNpcBase::TranslateNavGoalPosition(const FVector* GoalPosition)
{
	// 0x101a6420, slot 410 — `return param_1;`. An IDENTITY PASSTHROUGH, not a fixed literal: the
	// base answer is whatever the caller supplied, so a caller that passes null gets null back. A
	// species that wants to move the goal overrides the slot.
	return GoalPosition;
}

int32 FElysiumNpcBase::GetLocalScheduleId(int32 GlobalId)
{
	// 0x101a6620, slot 447 — `0x102ea280(GetClassScheduleIdSpace(), id)`, the GLOBAL-to-LOCAL
	// direction of the range translation family Schedule ports the other half of. The walk is:
	// -1 stays -1; otherwise follow the chain at `+0x10`, and for the first space whose local base
	// is not the 9999 sentinel and whose `[globalBase, localTop]` range holds the id, answer
	// `(localBase - globalBase) + id`.
	return GlobalToLocalId(IdSpace(EElysiumIdCategory::Schedule), GlobalId);
}

int32 FElysiumNpcBase::ResolveScheduleId(int32 Id) const
{
	// `GetScheduleOfType` 0x102cc260: slot 580 belongs to the receiving NPC, not Troika.
	if (ElysiumScheduleId::IsGlobal(Id)) return Id;
	const FElysiumLocalIdSpace* Space = IdSpace(EElysiumIdCategory::Schedule);
	return Space != nullptr ? Space->LocalToGlobal(Id) : INDEX_NONE;
}

int32 FElysiumNpcBase::GetLocalTaskId(int32 GlobalId)
{
	// 0x101a6640, slot 450 — the same call with the space pointer advanced by `+0x18`, which is the
	// TASK sub-space of the same `CAI_ClassScheduleIdSpace` (the schedule space is at +0x00, tasks
	// at +0x18, conditions at +0x30 and squad slots at +0x48; family Squad reaches +0x48 the same
	// way).
	//
	// The seam is CLOSED. All four sub-spaces are the corpus's own, filled by the registration pass
	// that runs each class's `InitCustomSchedules` recipe, so this is retail's translation over
	// retail's ranges.
	return GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), GlobalId);
}

const FElysiumLocalIdSpace* FElysiumNpcBase::IdSpace(EElysiumIdCategory Category) const
{
	// Slot 580 `GetClassScheduleIdSpace`, answered out of the corpus rather than out of a table
	// typed here: the class -> space map is the sidecar's, so a class whose space is SHARED with a
	// sibling gets the sibling's, and a class with no slot-580 body of its own falls to the Troika
	// line exactly as the vtable would take it.
	FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
	Corpus.EnsureLoaded();
	const FString ClassName = RetailClass() != nullptr ? FString(RetailClass()->Name) : FString();
	if (const FElysiumLocalIdSpace* Own = Corpus.SpaceFor(ClassName, Category))
	{
		return Own;
	}
	return Corpus.SpaceFor(TEXT("CAI_BaseNPCTroika"), Category);
}

bool FElysiumNpcBase::CanPlaySentence(bool bDisregardState)
{
	// 0x101a6840, slot 483 — forwards to slot 158 `IsAlive` (`+0x278`) AND DROPS ITS OWN ARGUMENT on
	// the way. `bDisregardState` never reaches the callee; a caller that passed true to mean "ask me
	// anyway" is answered exactly as one that passed false. That is a fact a program can observe and
	// it is ported as such, not tidied.
	(void)bDisregardState;
	return IsAlive();
}

float FElysiumNpcBase::CoverRadius()
{
	// 0x101a6c20, slot 550 — `return (float10)_DAT_1045d650;` = **1024.0f**
	// (`docs/vtmb/computer-terminals.md` line 1508 reads the same word as `1024.0`;
	// `docs/vtmb/npc-ai/conditions-and-states.md` line 885 repeats it). `CNPC_VPedestrian` and
	// `CNPC_VTzimisce` override it for real elsewhere; this is the line's own answer.
	return GChainCoverRadius;
}

bool FElysiumNpcBase::Slot579(int32 Argument)
{
	// 0x101a6ce0, slot 579 — `return slot158() == 0;`, i.e. the NEGATION of `IsAlive`, with its own
	// integer argument dropped exactly as slot 483 drops its bool. So slot 579 is "is this thing
	// dead", spelled as a wrapper rather than as a field read.
	(void)Argument;
	return !IsAlive();
}

const FElysiumLocalIdSpace* FElysiumNpcBase::ClassScheduleIdSpace() const
{
	// 0x101a6d00 — slot 580's BASE body, `return &DAT_1090ff08`. It is a DIFFERENT id space from the
	// Troika line's `&DAT_10924248`, and family Schedule's table has no row for it, so the row is
	// stood here as a static.
	//
	// 49 dispatch sites reach slot 580 and every species subclass overrides it; the base is reached
	// only by the classes between `CAI_BaseNPC` and `CAI_BaseNPCTroika`, none of which is an entity
	// classname this runtime spawns. It is the corpus's `cai_basenpc` unit -- 68 registered schedule
	// names for 64 texts, the root every other space parents on.
	return FElysiumScheduleCorpus::Get().SpaceFor(TEXT("CAI_BaseNPC"),
		EElysiumIdCategory::Schedule);
}

float FElysiumNpcBase::HearingSensitivity()
{
	// 0x101a67c0 — slot 476 `HearingSensitivity`'s base body, `return (float10)_DAT_104454c0;`.
	// `_DAT_104454c0` is the image's shared **1.0f**, so the base sensitivity is UNITY and
	// `CanHearSound`'s `volume * sensitivity` (`docs/vtmb/npc-ai/senses.md` § "radius =
	// HearingSensitivity") is the bare volume. Troika's own override (`0x101aa5f0`) reads `+0x63c0`
	// and is the body every spawned NPC actually gets; this is what the line under it answers.
	return GChainOne;
}

bool FElysiumNpcBase::CineCanInterrupt() const
{
	// 0x101a8930 — `m_interruptable` (+0x5f90) set AND the resolved `m_hTargetEnt` (+0x5ce4)
	// answering slot 158 `IsAlive`. The three terms are sequential and short-circuit in retail's own
	// order: the flag, the handle resolving to a non-null entity, and only then the virtual. A
	// missing target answers FALSE, not true — an interruptable beat whose actor has gone cannot be
	// interrupted, it is already over.
	//
	// `+0x5ce4` is bound to `FElysiumNpcBase::TargetEnt` in the shape map, which is the word this reads.
	// `m_interruptable` is read through the owning scripted-sequence leaf by
	// `CineIsInterruptable()` below.
	if (!CineIsInterruptable())
	{
		return false;
	}
	if (World == nullptr || !TargetEnt.IsSet())
	{
		return false;
	}
	FElysiumEntity* Resolved = World->Resolve(TargetEnt);
	if (Resolved == nullptr)
	{
		return false;
	}
	FElysiumNpc* TargetNpc = Resolved->AsNpc();
	return TargetNpc != nullptr ? TargetNpc->IsAlive() : false;
}

bool FElysiumNpcBase::CineIsInterruptable() const
{
	// `CCineNPC::m_interruptable` (+0x5f90, `FIELD_BOOLEAN`). The scripted-sequence leaf derives the
	// exact immutable value from spawnflag 0x20; the accessor keeps the NPC from guessing from the
	// different queue-lock bit.
	const FElysiumEntity* Owner = World != nullptr ? World->Resolve(ScriptOwner) : nullptr;
	return Owner != nullptr && Owner->IsScriptedSequenceInterruptable();
}

// --- Moved from `ElysiumNpcEntityChain.cpp` (story 5 step 5) ---

void FElysiumNpcBase::VPhysicsUpdatePusher(const void* PhysicsObject)
{
	// SEAM for `CBaseEntity::VPhysicsUpdatePusher(physicsObject)`, the arm movetypes 1 and 8 take.
	(void)PhysicsObject;
	PhysicsUpdateCalls.Add(TEXT("VPhysicsUpdatePusher"));
}

FElysiumEntity* FElysiumNpcBase::EntityOfEdict(const void* Edict) const
{
	// SEAM for `edict->m_pNetworkable (+0x40)->GetBaseEntity() (+0x10)`. There are no edicts here.
	// Answering null is not a refusal of slot 165: retail's own null arm dispatches slot 166 with
	// 0, and so does this.
	(void)Edict;
	return nullptr;
}

bool FElysiumNpcBase::PhysicsObjectIsStandable(const FElysiumEntity& Entity) const
{
	// SEAM for `(*DAT_1070b250 + 0x18)(entityIndex)`, the physics-environment query `0x100b5110`
	// makes for a `SOLID_VPHYSICS` entity. False is retail's answer for an object that is awake and
	// moving, which is what an unmodelled physics world stands for.
	(void)Entity;
	return false;
}

void* FElysiumNpcBase::PythonInteropObject() const
{
	// SEAM for `DAT_1072b360`, slot 240's whole body. The global is the CPython interop side of the
	// entity — whatever the embedded interpreter last stored — and this runtime embeds none, so the
	// answer is the global's own pre-interpreter value.
	return nullptr;
}

bool FElysiumNpcBase::IsStandableSolid() const
{
	// 0x100b5110, the helper slots 159 and 164 both end in.
	//
	//   GetSolid() == SOLID_BSP                          -> true
	//   GetSolid() == SOLID_VPHYSICS and the physics
	//     environment says the object is standable       -> true
	//   anything else                                    -> false  (retail returns the movetype
	//                                                       word with its low byte zeroed, which
	//                                                       IS false and nothing else)
	//
	// Retail re-dispatches slot 92 for the second test rather than caching the first answer; the
	// port keeps the two calls so a species that answered differently on the second would be read
	// differently here too.
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidBsp)
	{
		return true;
	}
	if (GetSolid() == NpcKernelEntityChainShared::GChainSolidVPhysics)
	{
		return PhysicsObjectIsStandable(*this);
	}
	return false;
}

bool FElysiumNpcBase::MoveReboundState(FMoveRebound& Out) const
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

void FElysiumNpcBase::SetLocalVelocity(const FVector& NewVelocity)
{
	// `CBaseEntity::SetLocalVelocity`, the write slot 135's linear half ends on. The port's
	// `FElysiumEntity::Velocity` IS that word.
	Velocity = NewVelocity;
}

void FElysiumNpcBase::SetLocalAngularVelocity(const FVector& NewAngularVelocity)
{
	// `CBaseEntity::SetLocalAngularVelocity`, the angular half's write. `AngularVelocity` is
	// `avelocity`.
	AngularVelocity = NewAngularVelocity;
}

// -------------------------------------------------------------------------------------------------
// `CBaseAnimatingOverlay` / `CBaseFlex` — the six slots over family Anim's tables.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcBase::ReleaseSceneEvent(const FSceneEventRecord& Record)
{
	// SEAM for `thunk_FUN_10075b70(record.event)`, the release both removers call before they
	// compact. The port's scene events are parsed data owned by the scene asset and are not
	// reference-counted, so nothing is released; recorded so the CALL is assertable.
	(void)Record;
	++SceneEventReleases;
}

int32 FElysiumNpcBase::GlobalToLocalId(const FElysiumLocalIdSpace* Space, int32 GlobalId)
{
	// `0x102ea280`, whole, including the parent walk at `+0x10` and the bound against the
	// TRANSLATED top (`+0x0c`) rather than `m_localTop`. Both used to be missing here and both were
	// invisible, because every row this runtime carried was the 9999 sentinel and the body answered
	// -1 for every id. `FElysiumLocalIdSpace` carries all six words and is where the arms live now;
	// a null space is retail's end-of-chain and still answers -1.
	return Space != nullptr ? Space->GlobalToLocal(GlobalId) : INDEX_NONE;
}
