// `CBaseFlex`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumFlexSlots.inl` (a slot body) or in `ElysiumFlexSlotBodies.inl`.

#include "ElysiumFlex.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnimShared.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"

// --- Moved from `ElysiumNpcBaseAnim.cpp` (story 5 step 6) ---

bool FElysiumFlex::FlexControllerRange(int32 Index, float& OutMin, float& OutMax) const
{
	// The studio flex-controller descriptor `studiohdr + studiohdr->flexcontrollerindex (+0x164) +
	// index * 0x14`, whose `+0xc` is `min` and `+0x10` is `max` (`0x100b5ba0` writes through it,
	// `0x100b5c50` reads back through it). **SEAM**: `GetModelPtr()` answers null here, which is the
	// arm both slots already have — they return 0 / pass the stored weight through untouched.
	(void)Index;
	OutMin = 0.f;
	OutMax = 0.f;
	return false;
}

float FElysiumFlex::SequenceDurationOf(int32 Sequence) const
{
	// `CBaseAnimating::SequenceDuration(int)`. **SEAM**, answering 0.
	(void)Sequence;
	return 0.f;
}

void FElysiumFlex::SetFlexWeight(TCHAR* Name, float Value)
{
	// `0x100b5b60`, slot 280: resolve the name and dispatch slot 279 (vtable +0x45c), which is the
	// INDEX overload and 29c's, not this family's. Two statements, and the vtable hop is retail's —
	// a species that replaced slot 279 would be reached through it.
	SetFlexWeight(LookupFlexController(Name), Value);
}

float FElysiumFlex::GetFlexWeight(TCHAR* Name)
{
	// `0x100b5c20`, slot 282: the mirror of slot 280 — resolve, then dispatch slot 281 (+0x464).
	return GetFlexWeight(LookupFlexController(Name));
}

float FElysiumFlex::GetFlexWeight(int32 Index)
{
	// `0x100b5c50`, slot 281. Read off the listing, because the decompilation is DAMAGED exactly
	// where the answer is: the two `FLD`s at `0x100b5c8d` and `0x100b5ca2` are the two arms.
	//
	//   index < 0                       -> 0
	//   index >= GetNumFlexControllers  -> 0
	//   no studio header                -> 0
	//   max != min                      -> (max - min) * m_flexWeight[index] + min
	//   max == min                      -> m_flexWeight[index]
	//
	// So slot 281 DE-NORMALISES: slot 279 stores `(value - min) / (max - min)` and this maps it back
	// into the controller's authored range. 29c named the port method `FlexControllerRange` for the
	// range read inside it; the range read is the seam above and the body is the slot.
	if (Index < 0 || Index >= NumFlexControllers() || Index >= NumFlexWeightSlots)
	{
		return 0.f;
	}
	float Min = 0.f;
	float Max = 0.f;
	if (!FlexControllerRange(Index, Min, Max))
	{
		return 0.f;
	}
	if (Max != Min)
	{
		return (Max - Min) * FlexWeight[Index] + Min;
	}
	return FlexWeight[Index];
}

void FElysiumFlex::AddFlexSetting(const TCHAR* Expression, float Scale, void* SettingsHeader,
	void* OverrideHeader, bool bCheckStateChange)
{
	// `0x100b6cf0`, slot 288, over a `flexsettinghdr_t` — a `.vfe` expression file. The body's SHAPE
	// is recovered and reproduced below; its operand BINDING is not, and that is stated rather than
	// guessed.
	//
	// Recovered, in retail's order:
	//   1. linear `__strcmpi` scan of the header's setting table (`hdr+0x8c` count, `hdr+0x90`
	//      offset, 0x18-byte stride) for `Expression`; a miss is the whole refusal.
	//   2. when `bCheckStateChange` and the found setting's `type` is 1, run the preset chain
	//      (`0x100b68d0`).
	//   3. resolve an optional preset/sub-index indirection (`type == 1` -> `index` -> the setting
	//      that index names, `0x100b6f10`), then an optional OVERRIDE header lookup by the same name
	//      (`0x100b6850`), which REPLACES both the setting and the header it is read from.
	//   4. for every `(key, weight)` pair the setting carries, map `key` through the header's index
	//      table (`hdr+0xa8`) to a flex-controller number, read that controller through slot 281 and
	//      write it back through slot 279, blended by `Scale`.
	//
	// **Unrecovered:** the exact blend. Ghidra scrambles the `__thiscall` argument order here — the
	// same `param_1` is used as a `char*` for the name compare and as a `float` in the blend — so
	// whether the write is `current + Scale * weight` or the SDK's
	// `current * (1 - s) + weight * s` cannot be settled from this body, and the corpus holds exactly
	// one caller, which passes constants through a thunk. It is left unported rather than invented.
	//
	// **SEAM**: nothing in this runtime carries a `flexsettinghdr_t` — no `.vfe` is exported and the
	// animating tier stands no expression table — so both headers arrive null and step 1 refuses,
	// which is retail's own answer for a name no setting file holds.
	(void)Expression;
	(void)Scale;
	(void)bCheckStateChange;
	if (SettingsHeader == nullptr && OverrideHeader == nullptr)
	{
		return;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("AddFlexSetting: a flexsettinghdr_t reached the kernel, which this runtime does not ")
		TEXT("carry (retail 0x100b6cf0)"));
}

void FElysiumFlex::AddFlexAnimation(void* SceneEventInfo)
{
	// `0x100b6960`, slot 289, over one queued scene-event record. Three phases in retail's order.
	FSceneEventRecord* const Record = static_cast<FSceneEventRecord*>(SceneEventInfo);
	if (Record == nullptr || Record->Event == nullptr || Record->Scene == nullptr)
	{
		// `param_1 != 0 && param_1[0] != 0 && param_1[1] != 0` — all three, and the record's own
		// "processed" byte is raised only at the very end.
		return;
	}
	const FElysiumSceneEvent& Event = *Record->Event;

	// Phase 1 — RESOLVE, once per event. Retail latches it on the event
	// (`0x10077710` reads the latch, `0x10077730(event, 1)` raises it); this port latches on the
	// queue record, because the parsed scene is shared immutable data here and a per-event write
	// would be a write into the scene cache.
	//
	// Each flex-animation track names a flex controller. A COMBO track (`0x10074a50`) names two —
	// the same base name prefixed `right_` and `left_` — and retail builds both by `Q_strncpy`ing the
	// prefix into a 512-byte buffer and appending the track name; a plain track resolves its own name
	// once. `LookupFlexController` is the resolver in every case, so a name that matches nothing
	// resolves to controller 0.
	if (!Record->bResolved)
	{
		// **SEAM**: `FElysiumSceneEvent` carries no flex-animation track list — the `.vcd` reader
		// parses the `flexanimations` block's presence but not its tracks — so the track count is
		// zero and the resolve pass has nothing to walk. The prefix rule is recorded here because it
		// is the recovered fact, and it is what the track list will be walked with.
		Record->bResolved = true;
	}

	// Phase 2 — the scene's own clock, and the event's ramp sampled at it. Retail calls
	// `CChoreoScene::GetTime()` (`0x1007dfa0`) and `CChoreoEvent::GetIntensity` (`0x10076280`) and
	// **discards** the intensity (`FSTP ST0`); the number is kept here so the arithmetic is
	// assertable without inventing an effect retail does not have.
	const float SceneTime = Record->StartTime;
	Record->LastIntensity = Event.RampAt(SceneTime - Event.StartTime);

	// Phase 3 — the write pass, once per ACTIVE track (`0x100740d0`), and twice for a combo track.
	// The formula, recovered by shape from `0x100b69xx`'s blend:
	//     SetFlexWeight(j, trackValue * t + (1 - t) * GetFlexWeight(j))
	// a LERP of the controller toward the track's own value by the track's intensity `t`, which is
	// `CFlexAnimationTrack::GetIntensity(sceneTime, side)` (`0x10074610`). A controller index below
	// zero is skipped. With no track list this pass is empty.

	// The record's "expressions applied" byte (+0x08), raised whatever the pass did.
	Record->bResolved = true;
}

float FElysiumFlex::SceneTimeOf(const FElysiumSceneData& Scene) const
{
	// `CChoreoScene::GetTime()` (`0x1007dfa0`) — one float at scene+0x7c. **SEAM**: `FElysiumSceneData`
	// is the PARSED file and holds no clock; the clock lives on `FElysiumScenePlayer`, which the
	// kernel does not reach. Answers 0, which makes a gesture's wind-back its own start time.
	(void)Scene;
	return 0.f;
}

void FElysiumFlex::ProcessSceneEvents()
{
	// `0x100b6250`, slot 283. The decompilation carries a DAMAGED banner on its tail jump only; the
	// listing shows the whole body.
	//
	// It does NOT walk the scene-event queue. It zeroes EVERY flex controller through slot 279 —
	// re-reading `GetNumFlexControllers()` each iteration, exactly as `LookupFlexController` does —
	// and then TAIL JUMPS to slot 284 `AddSceneExpressions` (+0x470), which is what rebuilds the
	// frame's expression from the queue. The reset-then-accumulate order is the mechanism: an
	// expression that stopped this frame leaves no residue.
	int32 Index = 0;
	int32 Count = NumFlexControllers();
	if (Count > 0)
	{
		do
		{
			SetFlexWeight(Index, 0.f);
			++Index;
			Count = NumFlexControllers();
		} while (Index < Count);
	}
	AddSceneExpressions();
}

void FElysiumFlex::ProcessSequenceSceneEvent(void* SceneEventInfo)
{
	// `0x100b70e0`, slot 290. Four guards and one call, and the call's result is DISCARDED
	// (`FSTP ST0` on the listing): retail computes the event's ramp intensity at the scene's current
	// time and does nothing with it. Reproduced as retail wrote it — the guards ARE the behaviour,
	// and inventing an effect for the intensity would be a divergence.
	FSceneEventRecord* const Record = static_cast<FSceneEventRecord*>(SceneEventInfo);
	if (Record == nullptr || Record->Event == nullptr || Record->Scene == nullptr
		|| Record->Handle < 0)
	{
		return;
	}
	const float SceneTime = SceneTimeOf(*Record->Scene);
	Record->LastIntensity = Record->Event->RampAt(SceneTime - Record->Event->StartTime);
}

void FElysiumFlex::ProcessGestureSceneEvent(void* SceneEventInfo)
{
	// `0x100b7040`, slot 291. The same four guards, then — read off the listing, because the
	// decompilation binds the divisor to the wrong call:
	//
	//   duration = CChoreoEvent::GetDuration(event)        // 0x10076b30, SAVED
	//   SequenceDuration(record.sequence)                  // 0x10009813, CALLED AND DISCARDED
	//   cycle    = ((m_flAnimTime - record.startTime) + 1e-4) / duration
	//   CChoreoEvent::GetOriginalPercentageFromPlaybackPercentage(event, cycle)   // discarded
	//   intensity = CChoreoEvent::GetIntensity(event, scene->GetTime())           // discarded
	//
	// The divisor is the EVENT's authored length, not the sequence's — `FElysiumSceneEvent::GetDuration`
	// is already this runtime's spelling of `0x10076b30`. `SequenceDuration` is still called, which is
	// why the seam is asked here at all.
	FSceneEventRecord* const Record = static_cast<FSceneEventRecord*>(SceneEventInfo);
	if (Record == nullptr || Record->Event == nullptr || Record->Scene == nullptr
		|| Record->Handle < 0)
	{
		return;
	}
	const FElysiumSceneEvent& Event = *Record->Event;
	const float Duration = Event.GetDuration();
	SequenceDurationOf(Record->Sequence);
	Record->LastGestureCycle = static_cast<float>(
		((AnimTime - Record->StartTime) + NpcKernelAnimShared::GAnimTimeEpsilon)
			/ FMath::Max(Duration, UE_KINDA_SMALL_NUMBER));
	Record->LastIntensity = Event.RampAt(SceneTimeOf(*Record->Scene) - Event.StartTime);
}

int32 FElysiumFlex::NumFlexControllers() const
{
	// `CBaseAnimating::GetNumFlexControllers` (`0x10012530`, reached from every body in the flex
	// half). **SEAM**: no studio header at this tier, so the table is empty.
	return 0;
}

FString FElysiumFlex::FlexControllerName(int32 Index) const
{
	// `CBaseAnimating::GetFlexControllerName(int)`. **SEAM**, and unreachable while the count above
	// answers 0.
	(void)Index;
	return FString();
}

int32 FElysiumFlex::LookupFlexController(const TCHAR* Name) const
{
	// `0x100b5d10`, the whole body. The count is re-read every iteration — retail calls
	// `GetNumFlexControllers()` again at the bottom of the loop rather than caching it — and the miss
	// answer is **0**, not -1.
	int32 Index = 0;
	int32 Count = NumFlexControllers();
	if (Count > 0)
	{
		do
		{
			if (FlexControllerName(Index).Equals(Name != nullptr ? Name : TEXT(""),
					ESearchCase::IgnoreCase))
			{
				return Index;
			}
			++Index;
			Count = NumFlexControllers();
		} while (Index < Count);
	}
	return 0;
}

// --- Moved from `ElysiumNpcBaseEntityChain.cpp` (story 5 step 6) ---

void FElysiumFlex::SetFlexWeight(int32 Index, float Value)
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

void FElysiumFlex::ClearSceneEvents(void* Scene)
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

void FElysiumFlex::RemoveSceneEvent(void* Event)
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

void FElysiumFlex::ReleaseSceneEvent(const FSceneEventRecord& Record)
{
	// SEAM for `thunk_FUN_10075b70(record.event)`, the release both removers call before they
	// compact. The port's scene events are parsed data owned by the scene asset and are not
	// reference-counted, so nothing is released; recorded so the CALL is assertable.
	(void)Record;
	++SceneEventReleases;
}

// --- Moved from `ElysiumNpcBaseFacing.cpp` (story 5 step 6) ---

void FElysiumFlex::SetViewtarget(const FVector& NewViewtarget)
{
	// `0x100b5b00`, the whole body: three floats into `m_viewtarget` (+0x0848). Retail networks the
	// word; nothing in this substrate reads it yet.
	Viewtarget = NewViewtarget;
}
