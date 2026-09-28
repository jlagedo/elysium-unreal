// `CAI_BaseNPC`'s bodies of the `Anim` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseAnim.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnimShared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScriptedSequence.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// The three retail activity numbers this family compares against, named once.
	constexpr int32 GAnimActIdle = 9;                  // the sequence fallback both misses take
	constexpr int32 GAnimActRun = 0x13;                // rewritten to `GAnimActIdle` by the run-to-walk rung
	constexpr int32 GAnimActScriptCustomMove = 0x18;   // ACT_SCRIPT_CUSTOM_MOVE
}

// --- Moved from `ElysiumNpcAnim.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::LookupSequenceByName(const TCHAR* Name) const
{
	// `CBaseAnimating::LookupSequence(const char*)`. **SEAM**, answering -1 — retail's own "this
	// model authors no such sequence", which every caller here already branches on.
	(void)Name;
	return INDEX_NONE;
}

int32 FElysiumNpcBase::SequenceActivityOf(int32 Sequence) const
{
	// `CBaseAnimating::GetSequenceActivity(int)`. **SEAM**, answering -1.
	(void)Sequence;
	return INDEX_NONE;
}

void FElysiumNpcBase::ResetSequenceInfo()
{
	// `CBaseAnimating::ResetSequenceInfo` (`0x10090950`): `m_nSequence == -1` becomes 0, the
	// finished byte drops, `m_bSequenceLoops (+0x65d) = GetSequenceFlags(seq) & 1`, the playback
	// rate is 1.0. The yaw/ground speeds and the playback rate are the clip player's in this runtime;
	// the sequence's own loop bit and its length come back from the play hook (the sequence bridge).
	if (SequenceNumber == INDEX_NONE)
	{
		SequenceNumber = 0;
	}
	bSequenceFinished = false;
	SequenceFinishesAt = -1.0;
	float Seconds = 0.f;
	bool bLoops = false;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (PlaySequenceClip(SequenceNumber, Seconds, bLoops))
	{
		bSequenceLoopedOnce = bLoops;                                   // +0x65d m_bSequenceLoops
		SequenceFinishesAt = Seconds > 0.f ? Now + static_cast<double>(Seconds) : Now;
	}
	else if (SequenceNumber == 0)
	{
		// Retail's floor sequence plays the model's sequence 0 and finishes; this runtime's row 0
		// plays nothing, so it has finished at once (the next `RunAnimation` raises the byte). A
		// numbered clip the body could not play (another owner holds it) keeps the clock unarmed.
		bSequenceLoopedOnce = false;
		SequenceFinishesAt = Now;
	}
}

void FElysiumNpcBase::CommitForcedSequence(int32 Sequence)
{
	// `ResetSequence` `0x10260a50`: the debug line (`+0x224 < 0` with a Troika self-cast) is absent;
	// `m_nSequence = seq`, then `ResetSequenceInfo` (`0x10090950`), which is where this runtime's
	// sequence bridge starts the clip.
	SequenceNumber = Sequence;
	ResetSequenceInfo();
}

float FElysiumNpcBase::RunAnimation()
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	// `StudioFrameAdvance(0)` (slot 250, `0x1026c55f`): the committed clip's first pass ends at
	// `SequenceFinishesAt`; reaching it raises `m_bSequenceFinished`, which stays up until the next
	// `ResetSequence` (retail's wrap test raises it for a looping sequence too).
	if (SequenceFinishesAt >= 0.0 && Now >= SequenceFinishesAt)
	{
		bSequenceFinished = true;
		SequenceFinishesAt = -1.0;
	}
	// `DAT_1092053c & 2` with `0x102ee6a0(navigator)` false zeroes the interval: the interval is
	// already this runtime's 0.0.
	const float Interval = 0.f;
	// Slot 513 `CapabilitiesGet` bit `0x20000000` (`CAP_AIM_GUN`) -> slot 538 `AimGun`.
	if ((static_cast<uint32>(CapabilitiesGet()) & 0x20000000u) != 0)          // 0x1026c5a6 / 0x1026c5ae
	{
		AimGun();                                                          // 0x1026c5b8 slot 538
	}
	// The idle re-pick: not SCRIPT (4) or DEAD (7), `m_Activity == ACT_IDLE` (1), slot 251.
	const int32 State = NpcStateRetail();
	if (State != 4 && State != 7 && ActivityNumber == 1                      // 0x1026c5bd..0x1026c5d0
		&& IsActivityFinished())                                           // 0x1026c5d4 slot 251
	{
		const int32 Sequence = !bSequenceLoopedOnce                         // 0x1026c5e0 +0x65d
			? SelectHeaviestSequence(TranslatedActivity, INDEX_NONE)        // 0x1026c5f2
			: SelectWeightedSequenceForActivity(TranslatedActivity);        // 0x1026c602
		if (Sequence != INDEX_NONE)                                        // 0x1026c609
		{
			CommitForcedSequence(Sequence);                                // 0x1026c611 0x10260a50
		}
	}
	return Interval;
}

int32 FElysiumNpcBase::GrowSceneEventCapacity(int32 Current, int32 GrowSize, int32 Needed)
{
	// `CUtlMemory::Grow`, as `0x100b5e60` inlines it. A grow step of -1 is retail's "external memory,
	// do not grow" and the insert is skipped entirely rather than reallocating.
	if (GrowSize == -1 || Current >= Needed)
	{
		return Current;
	}
	int32 Capacity = Current;
	while (Capacity < Needed)
	{
		if (Capacity == 0)
		{
			Capacity = 2;
		}
		else if (GrowSize == 0)
		{
			Capacity = Capacity * 2;
		}
		else
		{
			Capacity = Capacity + GrowSize;
		}
	}
	return Capacity;
}

void FElysiumNpcBase::AddSceneEventBase(const FElysiumSceneData* Scene, const FElysiumSceneEvent* Event)
{
	// `CBaseFlex::AddSceneEvent` `0x100b5e60` — the base-line body of slot 286, reached from the
	// Troika override's `default` arm.
	if (Scene == nullptr || Event == nullptr)
	{
		// Retail's `Msg("CBaseFlex::AddExpression: scene or event was null!\n")`.
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("CBaseFlex::AddExpression: scene or event was null!"));
		return;
	}

	FSceneEventRecord Record;
	Record.Event = Event;      // +0x00
	Record.Scene = Scene;      // +0x04
	Record.bResolved = false;  // +0x08

	// Two of the nineteen event types arm the record; every other type queues it inert.
	if (Event->Type == EElysiumChoreoEvent::Gesture)
	{
		// Type 6. `LookupSequence(event->GetParameters())`, then — only on a hit — the scene's own
		// "actor is busy" mark (`0x100766d0` / `0x1007a340`), the LOOPING warning, and the cycle the
		// gesture is entered at.
		const int32 Sequence = LookupSequenceByName(*Event->Param);
		Record.Handle = -1;
		Record.Sequence = Sequence;
		if (Sequence >= 0)
		{
			if ((SequenceFlagsOf(Sequence) & 0x1) != 0)
			{
				// Retail: `DevMsg(1, "vcd error: gesture %s of model %s is marked as a loop!\n", …)`.
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("vcd error: gesture %s is marked as a loop"), *Event->Param);
			}
			// The stamp is the animation clock wound BACK by however far into the event the scene
			// already is, so a gesture queued mid-event enters at the right cycle rather than at 0.
			Record.StartTime =
				AnimTime - (SceneTimeOf(*Scene) - Event->StartTime);
			Record.LastGestureCycle = static_cast<float>(
				((AnimTime - Record.StartTime) + NpcKernelAnimShared::GAnimTimeEpsilon)
					/ FMath::Max(Event->GetDuration(), UE_KINDA_SMALL_NUMBER));
		}
	}
	else if (Event->Type == EElysiumChoreoEvent::Sequence)
	{
		// Type 7. The same lookup, the same busy mark, and the stamp is simply NOW — a sequence event
		// always starts at its own head.
		const int32 Sequence = LookupSequenceByName(*Event->Param);
		Record.Sequence = Sequence;
		Record.Handle = -1;
		if (Sequence >= 0)
		{
			Record.StartTime = AnimTime;
		}
	}

	// The queue itself. `m_Size + 1` against `m_nAllocationCount`, the grow, then an insert AT THE
	// END — retail's inlined `InsertBefore(m_Size)` computes a shift count of exactly zero, so the
	// `memmove` at `0x10430fa0` never moves anything on an add. The mirror at +0x0a68 is written and
	// never read back.
	const int32 OldCount = SceneEvents.Num();
	const int32 Needed = OldCount + 1;
	if (SceneEventsAllocated < Needed && SceneEventsGrowSize != -1)
	{
		SceneEventsAllocated = GrowSceneEventCapacity(SceneEventsAllocated, SceneEventsGrowSize,
			Needed);
		SceneEvents.Reserve(SceneEventsAllocated);
	}
	SceneEvents.Add(MoveTemp(Record));
}

bool FElysiumNpcBase::ScriptOwnerIsLive() const
{
	// `m_hCine` (+0x5d74), which the shape map binds to `FElysiumEntity::ScriptOwner`.
	return ScriptOwner.IsSet() && World != nullptr && World->Resolve(ScriptOwner) != nullptr;
}

bool FElysiumNpcBase::IsActivityFinished()
{
	// `0x10272900`, slot 251, the whole 35-byte body. BOTH terms, and the second is what stops a
	// body that is still blending into its ideal from reporting done.
	return bSequenceFinished && SequenceNumber == IdealSequence;
}

void FElysiumNpcBase::ForcePreTranslatedSequenceAndActivity(int32 Activity, int32 TranslatedAct,
	int32 Sequence)
{
	// `0x10272400`, slot 311. A NEGATIVE sequence is the whole refusal: nothing at all is written.
	if (Sequence < 0)
	{
		return;
	}

	// `OnChangeActivity` (slot 465, vtable +0x744) fires BEFORE the store and only when the activity
	// actually changes, so a re-force of the same activity raises no change notification.
	if (ActivityNumber != Activity)
	{
		OnChangeActivity(Activity);
	}

	ActivityNumber = Activity;                 // +0x0fec m_Activity
	IdealActivityNumber = Activity;            // +0x0ff0 m_IdealActivity
	IdealWeaponActivity = TranslatedAct;       // +0x5cd4
	IdealTranslatedActivity = TranslatedAct;   // +0x5cd0
	TranslatedActivity = TranslatedAct;        // +0x0ff4
	IdealSequence = Sequence;                  // +0x5ccc m_nIdealSequence
	CommitForcedSequence(Sequence);            // 0x10260a50
	SequenceCycle = 0.f;                       // +0x06f8 m_flCycle
	PrevAnimTime = 0.f;                        // +0x0170 m_flPrevAnimTime
}

int32 FElysiumNpcBase::TranslateActivityNumber(int32 Activity, int32& OutWeaponActivity) const
{
	// `CAI_BaseNPC::TranslateActivity` (`0x10271ff0`). **SEAM**: the recovered per-species and
	// per-weapon translation is `Visual/ElysiumAnimationResolve.cpp`'s and is keyed on activity
	// NAMES, so there is no retail-numbered table at this tier. Answering the activity unchanged is
	// what retail's own EMPTY translation table gives, which is the unarmed body's case.
	OutWeaponActivity = Activity;
	return Activity;
}

FString FElysiumNpcBase::ScriptCustomMoveSequenceName() const
{
	// `m_hCine + 0x5f50`, the director's `m_iszCustomMove`. Empty is retail's own null-string case,
	// which it replaces with `""` before the lookup.
	const FElysiumScriptedSequence* Cine = ResolveCine();
	return Cine != nullptr ? Cine->CustomMove : FString();
}

void FElysiumNpcBase::ResolveActivityToSequence(int32 Activity, int32& OutSequence,
	int32& OutTranslatedActivity, int32& OutWeaponActivity) const
{
	// `0x10272130` — the fallback ladder, including its own retry loop. Written as retail wrote it:
	// an outer `do { … } while (true)` whose only exits are a resolved sequence, sequence zero, and
	// the two early returns inside the arms.
	//
	// The rungs, in the order the body reaches them:
	//   ACT_SCRIPT_CUSTOM_MOVE (0x18) with a live cine  -> the cine's own `m_iszCustomMove` by NAME,
	//                                                      else a warning and ACT_IDLE (9)
	//   ACT_DISPOSITION (0xf1) with `m_pBaseNPCTroika`  -> the Troika disposition resolver
	//   anything else                                   -> SelectWeightedSequence(translated)
	//   that missed, translated == ACT_RUN (0x13)       -> 9 instead
	//   still nothing                                   -> retry the WHOLE request as 0xf1
	//   even 0xf1 missed                                -> sequence 0
	LastResolveActivityRung = EResolveActivityRung::None;

	int32 Request = Activity;
	while (true)
	{
		OutSequence = INDEX_NONE;
		OutTranslatedActivity = TranslateActivityNumber(Request, OutWeaponActivity);

		bool bTookCustomMoveTail = false;
		if (Request == GAnimActScriptCustomMove && ScriptOwnerIsLive())
		{
			const FString CustomMove = ScriptCustomMoveSequenceName();
			OutSequence = LookupSequenceByName(*CustomMove);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::ScriptCustomMove;
				return;
			}
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("SCRIPT_CUSTOM_MOVE: %s has no sequence"), *DebugString());
			bTookCustomMoveTail = true;
		}
		else if (Request == NpcKernelAnimShared::GAnimActDisposition)
		{
			// Retail's guard is `m_pBaseNPCTroika != nullptr` (+0x98), the self-downcast cache every
			// spawned `CAI_BaseNPCTroika` fills in its own constructor: `AsNpc()`. A base-only NPC
			// resolves no disposition sequence.
			if (const FElysiumNpc* const Troika = AsNpc())
			{
				Troika->ResolveDispositionActivity(OutSequence, OutTranslatedActivity);
			}
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::DispositionTable;
				return;
			}
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s has no sequence for act ACT_DISPOSITION"), *DebugString());
		}
		else
		{
			OutSequence = SelectWeightedSequenceForActivity(OutTranslatedActivity);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::Weighted;
				return;
			}
			// Retail rate-limits this warning on a (this, activity, time) triple held in three
			// globals; the port logs on `Verbose`, which is this runtime's own rate limiter.
			UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s has no sequence for act %d"), *DebugString(),
				OutTranslatedActivity);
			if (OutTranslatedActivity == GAnimActRun)
			{
				OutTranslatedActivity = GAnimActIdle;
				OutSequence = SelectWeightedSequenceForActivity(GAnimActIdle);
				if (OutSequence != INDEX_NONE)
				{
					LastResolveActivityRung = EResolveActivityRung::RunToWalk;
					return;
				}
			}
		}

		if (bTookCustomMoveTail)
		{
			OutSequence = SelectWeightedSequenceForActivity(GAnimActIdle);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::CustomMoveIdle;
				return;
			}
		}

		if (Request == NpcKernelAnimShared::GAnimActDisposition)
		{
			// The retry has already happened and missed: sequence zero is the floor.
			OutSequence = 0;
			LastResolveActivityRung = EResolveActivityRung::SequenceZero;
			return;
		}
		Request = NpcKernelAnimShared::GAnimActDisposition;
		LastResolveActivityRung = EResolveActivityRung::DispositionRetry;
	}
}

void FElysiumNpcBase::SetIdealActivity(int32 Activity)
{
	// `0x10272650`, read off the listing. Activity 0 (`ACT_INVALID`) TAIL JUMPS to slot 310
	// `SetActivity(0)` — it does NOT store the word — and everything else stores `m_IdealActivity`
	// and re-resolves the ideal triple beside it WITHOUT committing it.
	if (Activity == 0)
	{
		SetActivity(0);
		return;
	}
	// Family Facing's `SetIdealActivityNumber` is the store half of this body; it is called rather
	// than respelt so the two cannot drift.
	SetIdealActivityNumber(Activity);
	ResolveActivityToSequence(Activity, IdealSequence, IdealTranslatedActivity, IdealWeaponActivity);
}

int32 FElysiumNpcBase::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	// Four species classes carry their own standalone slot-482 copy (`CNPC_VAnimal` `0x1035fd40`,
	// `CNPC_VHuman` `0x103850a0`, `CNPC_VMingXiao` `0x10396e90`, `CNPC_VTzimisce` `0x103bd270`),
	// overridden on their C++ classes (story 5 step 3). None calls this body; each ends in family
	// Bosses' `CanPlaySequenceStateArm`, which keeps a SCRIPT-state (4) body's answer where the
	// state gate below refuses it. `CNPC_VCamera`'s `0x10368f60` (`XOR EAX,EAX`) is unported.
	// `0x10278090`, slot 482. The return is retail's `CanPlaySequence_t`: 0 refuses, 1 is the plain
	// yes, and **2 is the yes of a body that already holds a cine** — the two are not the same
	// answer, which is why this returns an int rather than a bool.
	int32 Result = 1;
	if (ScriptOwnerIsLive())
	{
		if (!CineAllowsDynamicInteraction())
		{
			return 0;
		}
		Result = 2;
	}
	if (!IsAlive())   // slot 158, vtable +0x278
	{
		return 0;
	}
	// The state gate, refusing when EVERY term holds: the caller did not disregard state, the body is
	// neither NONE (0) nor IDLE (1), its ideal is not IDLE, and it is not an ALERT (3) body whose
	// caller asked with an interrupt level of at least 1.
	const int32 State = NpcKernelAnimShared::AnimRetailNpcState(Mind.State());
	const int32 IdealState = NpcKernelAnimShared::AnimRetailNpcState(Mind.IdealState());
	if (!bDisregardState && State != 0 && State != 1 && IdealState != 1
		&& (State != 3 || InterruptLevel < 1))
	{
		return 0;
	}
	return Result;
}

// --- The scene (slot 540) -----------------------------------------------------------------------

float FElysiumNpcBase::PlayScene(const TCHAR* SceneFile)
{
	// `0x10279060`, slot 540: nineteen bytes, a tail call into `CBaseFlex::PlayScene(this, name, 0)`
	// (`0x10084b40`) with a NULL out-handle. That body stands an `instanced_scripted_scene`, points
	// it at this NPC, loads the named scene and answers its play length; an unknown scene gets
	// `Msg("Unknown scene specified: %s\n")` and 0, with -1 written to the out-handle the NPC does
	// not pass.
	const float Length = PlayInstancedScene(SceneFile);
	if (Length < 0.f)
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Unknown scene specified: %s"),
			SceneFile != nullptr ? SceneFile : TEXT(""));
		return 0.f;
	}
	return Length;
}

// --- Moved from `ElysiumNpcAnim.cpp` (story 5 step 5) ---

float FElysiumNpcBase::PlayInstancedScene(const TCHAR* SceneFile)
{
	// `CBaseFlex::PlayScene`'s body (`0x10084b40`): `CreateNoSpawn("instanced_scripted_scene")`, set
	// the scene file and the target handle, `Spawn()` (slot 103) and slot 242, then
	// `LoadSceneFromFile`; on failure `Msg("Unknown scene specified: %s\n")` and 0, else the scene's
	// own play length.
	//
	// **SEAM**: standing that entity needs the world's factory and the scene cache, neither of which
	// the substrate's NPC reaches. -1 is this seam's "could not stand it" and `PlayScene` below turns
	// it into retail's own failure arm.
	(void)SceneFile;
	return -1.f;
}

// --- The flex controllers (slots 280, 281, 282) -------------------------------------------------

bool FElysiumNpcBase::CineAllowsDynamicInteraction() const
{
	// `0x101a8ac0` on the resolved `m_hCine`.
	const FElysiumScriptedSequence* Cine = ResolveCine();
	return Cine == nullptr || Cine->CanOverride();
}
