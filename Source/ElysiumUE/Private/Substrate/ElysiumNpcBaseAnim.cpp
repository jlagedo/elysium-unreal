// `CAI_BaseNPC`'s bodies of the `Anim` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseAnim.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
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
	// `StudioFrameAdvance` `0x1008f120`'s literals: a zero interval argument becomes 0.1
	// (`0x1008f1ca MOV [ESP+0x10],0x3dcccccd`), and an advance at or under 0.001 (the DOUBLE
	// `_DAT_1044f020`) answers 0. `GetSequenceCycleRate`'s zero-duration answer is 10.0
	// (`0x100912c8 FLD [0x1044e664]`).
	constexpr float GAnimDefaultFrameInterval = 0.1f;
	constexpr double GAnimMinFrameInterval = ElysiumNpcTunables::FrameAdvanceMinInterval;
	constexpr float GAnimZeroDurationCycleRate = ElysiumNpcTunables::Ten;
}

// --- Moved from `ElysiumNpcAnim.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::LookupSequenceByName(const TCHAR* Name) const
{
	// `CBaseAnimating::LookupSequence(const char*)` (`StartSequence 0x101a82d0` calls it at
	// `0x101a830d`): the index of the sequence the body's model authors under that name
	// (case-insensitive, as the vocabulary's `FString` key is), or -1 -- retail's own "unknown
	// scripted sequence", which every caller branches on. Through the sequence bridge (the named
	// modernization: the studio sequence table swapped for the name-keyed clip vocabulary), the index
	// is the bridge row of the clip the vocabulary names. Only the Troika line carries the bridge
	// (`ResetSequenceInfo` plays through it the same way), so a base-only NPC authors nothing.
	const FElysiumNpc* const Troika = AsNpc();
	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Troika == nullptr || Embodiment == nullptr || Visual == nullptr || Name == nullptr
		|| *Name == TEXT('\0'))
	{
		// No body, or retail's null name (`StartSequence`'s `0x101a830d` passes `""`): no sequence --
		// `SequenceForActivity`'s same guard.
		return INDEX_NONE;
	}
	const FString Label(Name);
	const FString Stem = ModelStem();
	if (!Embodiment->HasNpcClip(Stem, Label))
	{
		return INDEX_NONE;   // the model authors no such sequence: retail's -1
	}
	// The bank the vocabulary names the label under: the owner half of the bridge row's key, which is
	// what `PlaySequenceClip` hands to the clip player. Empty is a clip of the body's own model.
	const FString OwnerStem = Embodiment->NpcClipOwner(Stem, Label);
	// K2's residue (`stories/v3/README.md` § 7, named): the row's `STUDIO_LOOPING` bit
	// (`GetSequenceFlags & 1`, which `ResetSequenceInfo` copies to `+0x65d` at `0x10090a14`) is the
	// clip's own, and the bake carries it (`FElysiumNpcClip::IsLooping`), but no embodiment query
	// answers it by label. Until one does, today's rule stands: the live cine's `m_iszPlay` (+0x5f48)
	// runs once, every other name -- the pre-idle `m_iszIdle`, the post-idle `m_iszPostIdle`, a
	// custom move, an arrival or scene-event sequence -- loops.
	const FElysiumScriptedSequence* const Cine = ResolveCine();
	const bool bLoops = !(Cine != nullptr && Cine->Play.Equals(Label, ESearchCase::IgnoreCase));
	// Retail's lookup is a read of the studio header; the bridge numbers a clip on first sight, which
	// is the modernization's own session bookkeeping (`ResolveDispositionActivity`'s same const cast).
	return const_cast<FElysiumNpc*>(Troika)->SequenceRowFor(OwnerStem, Label, bLoops);
}

int32 FElysiumNpcBase::SequenceActivityOf(int32 Sequence) const
{
	// `CBaseAnimating::GetSequenceActivity(int)`. **SEAM**, answering -1.
	(void)Sequence;
	return INDEX_NONE;
}

void FElysiumNpcBase::ResetSequenceInfo()
{
	// `CBaseAnimating::ResetSequenceInfo` (`0x10090950`), in its order.
	if (SequenceNumber == INDEX_NONE)                                  // 0x100909c3
	{
		SequenceNumber = 0;                                            // 0x100909c8
	}
	bGroundSpeedFromIntervalMovement = false;                          // 0x100909d8 +0x5ac
	// `GetSequenceYawSpeed` -> `m_flYawSpeed` (+0x560) and `GetSequenceGroundSpeed` ->
	// `m_flGroundSpeed` (+0x654): the clip player's speeds in this runtime (visual-only halves).
	float Seconds = 0.f;
	bool bLoops = false;
	// The sequence bridge's play hook (a named modernization) answers the sequence's length and its
	// `STUDIO_LOOPING` bit.
	const bool bKnown = PlaySequenceClip(SequenceNumber, Seconds, bLoops);
	bSequenceLoopedOnce = bLoops;                                      // 0x10090a14 +0x65d, unconditional
	if (FElysiumNpc* const Troika = AsNpc())
	{
		Troika->SequencePlaybackRate = 1.f;                            // 0x10090a23 m_flPlaybackRate = 1.0
		// `m_fEffects |= +0x5b0 | 0x300` (`0x10090a1a..0x10090a43`), then `+0x5b0 = 0`
		// (`0x10090a49`): the pending-effects word has no port member, so only the constant bits land.
		Troika->EffectsWord |= 0x300u;
	}
	bSequenceFinished = false;                                         // 0x10090a37 +0x65c
	// `+0x658 = 0` (`0x10090a3d`) has no port word; `+0x70c` -> `ResetClientsideFrame`
	// (`0x10090a53`) is the client's.
	// `GetSequenceCycleRate` for the frame advance: `1 / duration`, or 10.0 for a zero-length
	// sequence (`0x100912c8`) -- which is what this runtime's row 0 is (it plays nothing, so it
	// finishes on the first advance). A row whose length is unknown does not advance (the seam).
	if (bKnown || SequenceNumber == 0)
	{
		SequenceCycleRate = (bKnown && Seconds > 0.f) ? 1.f / Seconds : GAnimZeroDurationCycleRate;
	}
	else
	{
		SequenceCycleRate = 0.f;
	}
	// `UTIL_Remove(m_hAnimFollowModel)` (`0x10090a58..0x10090a87`): the ornament an animation event
	// hung on this body goes with every sequence reset (the handle is left to go stale).
	if (!AnimFollowModel.IsEmpty())
	{
		IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
		if (Embodiment != nullptr && Visual != nullptr)
		{
			Embodiment->DetachOrnamentModel(Visual);
		}
		AnimFollowModel.Reset();
	}
	// `+0x650 != m_nSequence` -> slot 247 `SetAttackExtentsForSequence(m_nSequence)` and
	// `+0x650 = m_nSequence` (`0x10090a92..0x10090ab0`).
	if (AttackExtentsSequence != SequenceNumber)                          // 0x10090a9b
	{
		SetAttackExtentsForSequence(SequenceNumber);                      // 0x10090aa4 slot 247
		AttackExtentsSequence = SequenceNumber;                           // 0x10090ab0
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
	// Slot 250 `StudioFrameAdvance(0)` (`0x1026c5a0`), the sequence clock; its answer is the interval.
	float Interval = StudioFrameAdvance(0.f);                              // 0x1026c5a0 / 0x1026c5ab FSTP
	// `DAT_1092053c & 2` (the `ai_step` debug bit, `0x1026c5a6` / `0x1026c5af`) with
	// `0x102ee6a0(navigator)` false (`0x1026c5b9`) zeroes the interval (`0x1026c5c2`). No console
	// command this runtime carries sets the bit, so it stands clear, as a normal session's does.
	constexpr bool bAiStepBit = false;
	if (bAiStepBit && !NavigatorGoalIsActive())
	{
		Interval = 0.f;
	}
	// Slot 513 `CapabilitiesGet` bit `0x20000000` (`CAP_AIM_GUN`) -> slot 538 `AimGun`.
	if ((static_cast<uint32>(CapabilitiesGet()) & 0x20000000u) != 0)          // 0x1026c5ce slot 513 / 0x1026c5d4 TEST
	{
		AimGun();                                                          // 0x1026c5df slot 538
	}
	// The idle re-pick: not SCRIPT (4) or DEAD (7), `m_Activity == ACT_IDLE` (1), slot 251.
	const int32 State = NpcStateRetail();
	if (State != 4 && State != 7 && ActivityNumber == 1                      // 0x1026c5e5..0x1026c5fc
		&& IsActivityFinished())                                           // 0x1026c602 slot 251
	{
		// `+0x65d` (`0x1026c60c`) set takes the weighted pick (`0x1026c621`), clear the heaviest
		// (`0x1026c628`), both over `m_TranslatedActivity` (+0xff4).
		const int32 Sequence = !bSequenceLoopedOnce
			? SelectHeaviestSequence(TranslatedActivity, INDEX_NONE)        // 0x1026c628
			: SelectWeightedSequenceForActivity(TranslatedActivity);        // 0x1026c621
		if (Sequence != INDEX_NONE)                                        // 0x1026c62d
		{
			CommitForcedSequence(Sequence);                                // 0x1026c635 0x10260a50
		}
	}
	return Interval;
}

float FElysiumNpcBase::StudioFrameAdvance(float IntervalArg)
{
	// `CBaseAnimatingOverlay::StudioFrameAdvance` `0x10098bb0` runs `CBaseAnimating`'s body
	// (`0x1008f120`) and then the four overlay layers (`CAnimationLayer::StudioFrameAdvance` per
	// live layer, slot 112 on a finished auto-kill layer); the layers are not carried by this port's
	// NPC (named gap). `0x1008f120`, in its order:
	const float Now = World != nullptr ? static_cast<float>(World->NowSeconds()) : 0.f;
	const bool bWasFinished = bSequenceFinished;                          // 0x1008f18c
	if (PrevAnimTime == 0.f)                                              // 0x1008f192..0x1008f1a3
	{
		AnimTime = Now;                                                   // 0x1008f1ad +0x174
		PrevAnimTime = Now;                                               // 0x1008f1b3 +0x170
	}
	float Interval = IntervalArg;
	if (Interval == 0.f)                                                  // 0x1008f1bd..0x1008f1c8
	{
		Interval = GAnimDefaultFrameInterval;                             // 0x1008f1ca 0.1
	}
	Interval = (Interval + Now) - AnimTime;                               // 0x1008f1d8..0x1008f1e5
	if (!(static_cast<double>(Interval) > GAnimMinFrameInterval))         // 0x1008f1e9 FCOMP 0.001 / 0x1008f1f4
	{
		return 0.f;                                                       // 0x1008f1fc
	}
	PrevAnimTime = AnimTime;                                              // 0x1008f21b
	const FElysiumNpc* const Troika = AsNpc();
	const float PlaybackRate = Troika != nullptr ? Troika->SequencePlaybackRate : 1.f;   // +0x6f4
	const float Cycle = SequenceCycleRate * PlaybackRate * Interval + SequenceCycle;     // 0x1008f221..0x1008f230
	SequenceCycle = Cycle;                                                // 0x1008f236
	AnimTime = Interval + AnimTime;                                       // 0x1008f23c..0x1008f246
	if (static_cast<double>(Cycle) < 0.0 || static_cast<double>(Cycle) >= 1.0)   // 0x1008f24c / 0x1008f259
	{
		if (bSequenceLoopedOnce)                                          // 0x1008f289 +0x65d
		{
			SequenceCycle = Cycle - static_cast<float>(static_cast<int32>(Cycle));   // 0x1008f295..0x1008f2a4
		}
		else
		{
			SequenceCycle = Cycle < 0.f ? 0.f : 1.f;                      // 0x1008f2ae..0x1008f2c9
		}
		bSequenceFinished = true;                                         // 0x1008f2cf +0x65c
	}
	// Else `m_fSequencePastHalf` (+0x568) = cycle >= 0.5 (`0x1008f268..0x1008f280`): no port word
	// (the Tzimisce's `Select19SequencePastHalf` seam stands for its one reader).
	// `m_flYawSpeed` / `m_flGroundSpeed` from the sequence (`0x1008f2e5`, `0x1008f2fa`): the clip
	// player's speeds. `OnSequenceFinished` on the rising edge (`0x1008f316`) is an empty body
	// (`0x10091c80`); the AI trace's `seqfinished` event is emitted on that same edge (debug output
	// only, behind its sink).
	if (!bWasFinished && bSequenceFinished && IsAiTraced())
	{
		EmitAiTrace(TEXT("seqfinished"), TraceSequenceName(SequenceNumber));
	}
	return Interval;                                                      // 0x1008f321
}

int32 FElysiumNpcBase::GrowSceneEventCapacity(int32 Current, int32 GrowSize, int32 Needed)
{
	// `CUtlMemory::Grow`, as `CBaseFlex::AddSceneEvent` inlines it. A grow step of -1 is retail's "external memory,
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
	// `CBaseFlex::AddSceneEvent` — the base-line body of slot 286, reached from the
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

// --- Moved from `ElysiumNpcAnim.cpp` (story 5 step 5) ---

// --- The flex controllers (slots 280, 281, 282) -------------------------------------------------

bool FElysiumNpcBase::CineAllowsDynamicInteraction() const
{
	// `0x101a8ac0` on the resolved `m_hCine`.
	const FElysiumScriptedSequence* Cine = ResolveCine();
	return Cine == nullptr || Cine->CanOverride();
}
