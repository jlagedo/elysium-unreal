#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnimShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumRetailActivities.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumAnimEvent.h"                  // FElysiumAnimEvent — `SequenceEvents`' view
#include "Visual/ElysiumNpcClips.h"           // FElysiumNpcClip — `MeleeSequencesForActivity`'s list
#include "Visual/ElysiumAnimationPick.h"

// Story 29c-1, family **Anim** — the animation layers, the flex/expression controllers and the
// scene-event queue of `order.md` layers 0–9.
//
// 35 rows, 24 of which fill a Troika-line vtable slot. Four concerns:
//
//   * **The gesture-layer table** (`m_AnimOverlay`, +0x0734) and **the scene-event queue**
//     (`m_SceneEvents`, +0x0a58) are retail's own DATA STRUCTURES. Their bookkeeping is the rule:
//     the strides, the search order, the growth arithmetic and the `memmove` compaction below are
//     reproduced field for field, and every constant in them was read out of the cited body.
//   * **The flex controllers and the pose parameters** are MECHANISMS in an animation instance.
//     This substrate's NPC reaches no studio header, so each goes through a seam that answers
//     nothing and names the retail call it stands for; the bodies around the seam are still
//     retail's, arm for arm, because the arms are what decide which seam is asked.
//   * **The activity commit** — `SetIdealActivity`, `ResolveActivityToSequence`,
//     `ForcePreTranslatedSequenceAndActivity`, `IsActivityFinished`, `ShouldMaintainActivity`,
//     `CanPlaySequence` — is the kernel's own, and lands whole.
//   * **The species branches** of slot 259; slots 245/246's branch is `FElysiumNpcPlayerController`'s
//     (story 5 fold A2) and slot 250's `CAI_BaseHumanoid` branch was deleted by 0019 story 5 step 1
//     (no instance).
//
// The walked prose is `docs/vtmb/npc-ai/shape.md`.

namespace
{

	// `0x102b8a10`'s two constants: the activity it probes for and the schedule number it answers.
	constexpr int32 GIdleGateActivity = 0x61;
	constexpr int32 GIdleGateSchedule = 8;

	// The schedule id `CAI_BaseNPCTroika::ShouldMaintainActivity` (`0x102bf510`) refuses for.
	constexpr int32 GScheduleRefusingMaintain = 0xb9;
}

// --- The animating-tier seams -------------------------------------------------------------------
//
// Every one of these is a studio-header read this substrate's NPC cannot make. They are here, at the
// top, because the bodies below are written against them and a reader has to be able to see in one
// place exactly how much of this family is standing on nothing.

bool FElysiumNpc::SceneEntityForcesCutsceneLod(const void* SceneEntity) const
{
	// `CSceneEntity + 0x57d`, the byte slots 59/60/61 gate `m_bCutsceneForceLOD` on. **SEAM**: the
	// scene entity arrives as the generated `void*` and this substrate stands no `CSceneEntity`
	// layout, so the answer is retail's own "this scene does not force LOD".
	(void)SceneEntity;
	return false;
}

// --- Slots 245/246's forwarding gate -----------------------------------------------------------

bool FElysiumNpc::OwnerIsThePlayer() const
{
	// Slot 97 `GetOwnerEntity()` followed by that entity's `+0xa8`. This runtime's player entity is
	// the one `FElysiumEntityWorld::PlayerHandle()` names, so the two-step is one comparison.
	return World != nullptr && OwnerEntity.IsSet() && OwnerEntity == World->PlayerHandle();
}

// --- The gesture-layer table (slots 265, 269, 270, 273, 274, 275) -------------------------------
//
// Four records of 0x30 bytes from +0x0734. `GetFirstGestureLayer()` (slot 267) answers 0 for every
// class in the hierarchy, so every scan below starts at 0 and no slot is reserved.

// --- The expression writers (slots 288, 289) ----------------------------------------------------

// --- The scene-event queue (slots 283, 286, 290, 291) -------------------------------------------

bool FElysiumNpc::HasLiveDialogPartner() const
{
	// `m_hDialogPartner` (+0x0fe8) resolving to a live entity -- the EHANDLE read RunAI's gather skip
	// makes (`0x1026f1f0`). The word is written by `SetDialogPartner 0x10107050`: on an NPC set only
	// by `StartTalking 0x102c0270` and cleared only by `0x102c0360` (V3d; the session bit and the
	// talking term this used to read are gone).
	const FElysiumEntityHandle& Partner = GetDialogPartner();
	return World != nullptr && Partner.IsSet() && World->Resolve(Partner) != nullptr;
}

bool FElysiumNpc::DispositionStanceReaction(float& OutThreshold, float& OutChancePercent) const
{
	// `0x100ec5d0` over the disposition record: `+0x108` is the intensity threshold the event's own
	// parameter must EXCEED and `+0x10c` the percentage `RandomInt(1, 100)` is rolled against. The
	// index is clamped to the table's default (`table+0x4`) when it is outside `0..table+0x14`.
	// **SEAM**: `FElysiumDisposition` carries the row's animation, fidget and eye blocks and not this
	// pair, so the Silence arm of slot 286 refuses.
	OutThreshold = 0.f;
	OutChancePercent = 0.f;
	return false;
}

bool FElysiumNpc::DispositionLoudExpression(FString& OutExpression, float& OutFadeIn,
	float& OutFadeOut, float& OutMinLevel, float& OutMaxLevel) const
{
	// `0x100ec450` over the same record: `RandomInt(0, record[+0x21c] - 1)` picks one of the row's
	// 0x40-byte expression names at `record + 0x11c + roll * 0x40`, and the four floats come from
	// `+0x220` (fade in), `+0x224` (fade out), `+0x228` (minimum level) and `+0x22c` (maximum).
	// A fifth out-parameter at `+0x230` is filled and never read by this caller.
	// **SEAM**: the same unbuilt block; the Loud arm refuses.
	OutExpression.Reset();
	OutFadeIn = 0.f;
	OutFadeOut = 0.f;
	OutMinLevel = 0.f;
	OutMaxLevel = 0.f;
	return false;
}

void FElysiumNpc::AddScriptedExpression(const FString& Expression, float FadeIn, float FadeOut,
	float Scale, float Delay, float Duration)
{
	// `CBaseCombatCharacter::AddScriptedExpression`. **SEAM**: the list is
	// `CUtlVector<ScriptedExpression_t>` at +0x1568 and no port member claims it. Recorded so the
	// two arms that raise one are assertable.
	ScriptedExpressions.Add(FScriptedExpressionRequest{ Expression, FadeIn, FadeOut, Scale, Delay,
		Duration });
}

int32 FElysiumNpc::ChangeStanceForReaction()
{
	// `ChangeStance` `0x102c1230` as slot 286's Silence arm consumes it. Retail:
	//     do { n = RandomInt(0, 2); } while (n == m_CurrStance);
	//     seq = g_DispositionTable.TransitionSequence(this, m_CurrStance, n);   // 0x100ecfc0
	//     m_CurrStance = n;  m_flStanceTime = curtime;
	//     return seq;                                     // left in EAX and read by the caller
	//
	// `ElysiumStance::ChangeStance` is that body in this runtime and is cited there; it answers by
	// CLIP NAME, so the transition sequence is resolved back through `LookupSequence` (the bridge row
	// of the clip the model authors under that name; -1, the arm on which the whole reaction is
	// dropped, when it authors none).
	if (!EnsureStanceResolved())
	{
		return INDEX_NONE;
	}
	FRandomStream& StanceStream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule); // 0x102c1230
	int32 NewStance = 0; // 0x102c1230
	do { NewStance = StanceStream.RandRange(0, 2); } while (NewStance == Stance.Current); // 0x102c1230
	const int32 FromStance = Stance.Current; // 0x102c1230
	const int32 Transition = LookupSequenceByName(*StanceClips.Trans[FromStance][NewStance]); // 0x100ecfc0
	Stance.Current = NewStance; // 0x102c1230 +0x64c8
	Stance.LastChangeTime = static_cast<float>(World != nullptr ? World->NowSeconds() : 0.0); // 0x102c1230
	return Transition; // 0x102c1230: this body does not write either latch
}

int32 FElysiumNpc::SelectDispositionStance()
{
	if (!EnsureStanceResolved()) { return SequenceNumber; } // 0x102c12a0: -1 keeps current
	const int32 CurrentStance = Stance.Current; // 0x102c12a0 +0x64c8
	const double StanceNow = World != nullptr ? World->NowSeconds() : 0.0; // 0x102c12a0
	int32 StanceSequence = INDEX_NONE; // 0x102c12a0
	FRandomStream& StanceStream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule); // 0x102c12a0
	if (bIsTalking || Stance.bInFidget || Stance.bInChange) // 0x102c12a0 +0x64c0/+0x64e0/1
	{
		StanceSequence = LookupSequenceByName(*StanceClips.Idle[CurrentStance]); // 0x102c12a0
		Stance.bInFidget = false; Stance.bInChange = false; // 0x102c12a0
	}
	else if (StanceClips.Idle[CurrentStance] != StanceClips.Fidget[CurrentStance] // 0x102c12a0
		&& StanceStream.RandRange(1, 100) < StanceTuning.StandingFidgetChance) // 0x102c12a0
	{
		StanceSequence = LookupSequenceByName(*StanceClips.Fidget[CurrentStance]); // 0x102c12a0
		Stance.bInFidget = true; Stance.bInChange = false; // 0x102c12a0
	}
	else if (StanceTuning.StandingStanceChangeThreshold < StanceNow - Stance.LastChangeTime // 0x102c12a0
		&& StanceStream.RandRange(1, 100) < StanceTuning.StandingStanceChangeChance) // 0x102c12a0
	{
		StanceSequence = ChangeStanceForReaction(); // 0x102c1230, redraw loop after both chance draws
		Stance.bInFidget = false; Stance.bInChange = true; // 0x102c12a0
	}
	else
	{
		StanceSequence = LookupSequenceByName(*StanceClips.Idle[CurrentStance]); // 0x102c12a0
		Stance.bInFidget = false; Stance.bInChange = false; // 0x102c12a0
	}
	return StanceSequence == INDEX_NONE ? SequenceNumber : StanceSequence; // 0x102c12a0
}

void FElysiumNpc::CallPythonDialogFunction(const FString& FunctionName)
{
	// `CDialogDependency::CallPyDialogFunc(func, partner, this, 0x102, nullptr)`. **SEAM**: the
	// port's Python dialogue surface is reached through the dialogue session rather than from the
	// kernel, so the request is recorded and nothing is called.
	PythonDialogCalls.Add(FunctionName);
}

void FElysiumNpc::AddSceneEvent(void* Scene, void* Event)
{
	// `CAI_BaseNPCTroika::AddSceneEvent` `0x102c1680`, slot 286. A five-way dispatch on the choreo
	// event type, and the `default` arm is the base `CBaseFlex::AddSceneEvent`. The port's own
	// `EElysiumChoreoEvent` carries retail's numbering unchanged, which is why the arms read as names
	// here and as `2 / 7 / 0xd / 0xe / 0xf` in the listing.
	const FElysiumSceneData* const SceneData = static_cast<const FElysiumSceneData*>(Scene);
	const FElysiumSceneEvent* const SceneEvent = static_cast<const FElysiumSceneEvent*>(Event);
	if (SceneEvent == nullptr)
	{
		AddSceneEventBase(SceneData, SceneEvent);
		return;
	}

	switch (SceneEvent->Type)
	{
	case EElysiumChoreoEvent::Expression:
	{
		// Type 2 — `0x102c1a80`, the Troika expression installer. **SEAM**: it walks the disposition
		// table's expression block and `CBaseCombatCharacter::AddScriptedExpression`, neither of which
		// this substrate carries (`docs/vtmb/npc-kernel/layout.md` types +0x1568 as
		// `CUtlVector<ScriptedExpression_t>` and no port member claims it).
		AddScriptedExpression(SceneEvent->Param, 0.f, 0.f, 1.f, 0.f, SceneEvent->GetDuration());
		return;
	}
	case EElysiumChoreoEvent::Sequence:
	{
		// Type 7 — look the named sequence up and, ON A HIT ONLY, force it as both the ideal and the
		// current activity through slot 0x4dc (`ForcePreTranslatedSequenceAndActivity`, slot 311)
		// with the sequence's OWN activity on both arguments, then drop the two stance latches. A
		// miss falls out of the whole body: it does NOT reach the base.
		const int32 Sequence = LookupSequenceByName(*SceneEvent->Param);
		if (Sequence >= 0)
		{
			const int32 Activity = SequenceActivityOf(Sequence);
			ForcePreTranslatedSequenceAndActivity(Activity, Activity, Sequence);
			Stance.bInFidget = false;   // +0x64e0 m_bInDispositionFidget
			Stance.bInChange = false;   // +0x64e1 m_bInStanceChange
		}
		return;
	}
	case EElysiumChoreoEvent::Silence:
	{
		// Type 0xd — the stance-change reaction, gated on a LIVE dialogue partner. The event's
		// parameter string is an intensity, `atof`'d and compared against the disposition row's own
		// threshold (`0x100ec5d0` -> record +0x108); above it, `RandomInt(1, 100)` is rolled against
		// the row's percentage (record +0x10c).
		if (!HasLiveDialogPartner())
		{
			return;
		}
		float Threshold = 0.f;
		float ChancePercent = 0.f;
		if (!DispositionStanceReaction(Threshold, ChancePercent))
		{
			return;
		}
		const float Intensity = FCString::Atof(*SceneEvent->Param);
		if (!(Threshold < Intensity))
		{
			return;
		}
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		if (!(Stream.RandRange(1, 100) < static_cast<int32>(ChancePercent)))
		{
			return;
		}
		// The reaction IS a stance change: retail calls `ChangeStance` (`0x102c1230`) and reads the
		// transition sequence it leaves in `EAX` — the return of the disposition table's own
		// transition lookup. `ElysiumStance::ChangeStance` is that body in this runtime and answers
		// by CLIP NAME, so the sequence index is resolved back through `LookupSequence`.
		const int32 Sequence = ChangeStanceForReaction();
		if (Sequence < 0)
		{
			return;
		}
		SequenceNumber = Sequence;                // +0x06f0
		ResetSequenceInfo();                      // 0x10090950
		SequenceCycle = 0.f;                      // +0x06f8
		AnimTime = static_cast<float>(World != nullptr ? World->NowSeconds() : 0.0);   // +0x0174
		IdealActivityNumber = NpcKernelAnimShared::GAnimActDisposition;    // +0x0ff0
		ActivityNumber = NpcKernelAnimShared::GAnimActDisposition;         // +0x0fec
		Stance.bInFidget = false;                 // +0x64e0
		Stance.bInChange = true;                  // +0x64e1
		return;
	}
	case EElysiumChoreoEvent::Loud:
	{
		// Type 0xe — the loud-line scripted expression, gated on the cooldown `m_flLoudExpressionTime`
		// (+0x6574) having passed. The row (`0x100ec450`) picks ONE of its authored expression names
		// at random (`RandomInt(0, count - 1)` over record +0x21c) and hands back a fade-in
		// (+0x220), a fade-out (+0x224), a minimum (+0x228) and a maximum (+0x22c).
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		if (!(static_cast<double>(LoudExpressionTime) < Now))
		{
			return;
		}
		FString ExpressionName;
		float FadeIn = 0.f;
		float FadeOut = 0.f;
		float MinLevel = 0.f;
		float MaxLevel = 0.f;
		if (!DispositionLoudExpression(ExpressionName, FadeIn, FadeOut, MinLevel, MaxLevel))
		{
			return;
		}
		// The event's own parameter, clamped into [min, max]. Retail re-`atof`s the SAME string on
		// each of the three arms rather than caching it; the value is identical, so one read is
		// faithful.
		const float Authored = FCString::Atof(*SceneEvent->Param);
		const float Level = (Authored <= MaxLevel)
			? ((MinLevel <= Authored) ? Authored : MinLevel)
			: MaxLevel;
		AddScriptedExpression(ExpressionName, FadeIn, FadeOut, 1.f, 0.f, Level);
		// The cooldown covers the whole envelope: the level's own duration plus both fades, from now.
		LoudExpressionTime = Level + Now + FadeIn + FadeOut + NpcKernelAnimShared::GAnimTimeEpsilon;
		return;
	}
	case EElysiumChoreoEvent::Python:
	{
		// Type 0xf — `CDialogDependency::CallPyDialogFunc(func, partner, this, 0x102, nullptr)`. The
		// dialogue partner is resolved first and, when there is none, retail substitutes the object
		// `0x101cd9e0(1)` answers — the player. Both strings go through the interning helper
		// `0x101d3730`; the FIRST is a constant (`DAT_10547e6c`) whose result is discarded, and the
		// function actually called is the event's SECOND parameter (`0x10075ca0`).
		CallPythonDialogFunction(SceneEvent->Param2);
		return;
	}
	default:
		break;
	}

	// Every other type — including the gesture events the base line arms — reaches the base body.
	AddSceneEventBase(SceneData, SceneEvent);
}

// --- The activity commit ------------------------------------------------------------------------

void FElysiumNpc::ResolveDispositionActivity(int32& OutSequence, int32& OutTranslatedActivity) const
{
	// `0x10295a80`, the Troika resolver `ACT_DISPOSITION` is handed to when `m_pBaseNPCTroika`
	// (+0x98) is set — which it is on every spawned NPC, so this arm is always the one taken for
	// 0xf1: `*param_2 = slot 611()` (`0x10295a83`), the translated activity untouched by the body.
	// Slot 611 is the stance machine and it ROLLS on every call, as retail's does — the ladder asks
	// once at `SetIdealActivity` and again at `SetActivity`, and so does retail's.
	OutSequence = const_cast<FElysiumNpc*>(this)->Slot611();                 // 0x10295a83 slot 611
	// `0x10295a88..0x10295a8e`, `RET 0x10`: `*param_2` is the only write; the translated activity
	// stays whatever the caller holds.
	(void)OutTranslatedActivity;
}

// --- The sequence bridge (story 8 wave 2, L13) ------------------------------------------------
//
// A named modernization: the studio sequence table is swapped for the name-keyed clip resolver.
// Every clip the resolver, the stance machine or `LookupSequence` by name (`LookupSequenceByName`,
// `ElysiumNpcBaseAnim.cpp`) answered for this body gets a stable number the kernel's sequence words
// carry; `ResetSequence` plays it.

int32 FElysiumNpc::SequenceRowFor(const FString& OwnerStem, const FString& Label, bool bLoops,
	int32 InRawIndex)
{
	if (SequenceRows.IsEmpty())
	{
		// Row 0: retail's floor sequence, the model's own sequence 0. Its clip is the body's
		// `RawIndex 0` row, held in `SequenceZero` (N19); the row's label stays empty.
		SequenceRows.AddDefaulted();
	}
	for (int32 Index = 1; Index < SequenceRows.Num(); ++Index)
	{
		const FSequenceRow& Row = SequenceRows[Index];
		if (Row.OwnerStem == OwnerStem && Row.Label == Label && Row.bLoops == bLoops)
		{
			if (InRawIndex != INDEX_NONE && Row.RawIndex != INDEX_NONE && Row.RawIndex != InRawIndex)
			{
				continue; // 0x1008dc40: distinct raw rows can share owner/label
			}
			if (InRawIndex != INDEX_NONE) { SequenceRows[Index].RawIndex = InRawIndex; } // 0x1008dc40
			return Index;
		}
	}
	FSequenceRow& Row = SequenceRows.AddDefaulted_GetRef();
	Row.OwnerStem = OwnerStem;
	Row.Label = Label;
	Row.RawIndex = InRawIndex; // 0x1008dc40: unselected candidates retain their own identity
	Row.bLoops = bLoops;
	// The row's `seqdesc.flags & 2` (`STUDIO_SNAP`, read by `SetLayer 0x10099020` at `0x100990a4`),
	// taken here so every way a row is numbered -- by activity, by name (`LookupSequenceByName`), the
	// stance set, the melee and grapple picks -- carries it. A world with no embodiment has no
	// descriptor to read: false.
	IElysiumEmbodiment* const DescriptorSource = World != nullptr ? World->Embodiment() : nullptr;
	FElysiumSequenceDescriptor Descriptor;
	if (DescriptorSource != nullptr && Visual != nullptr
		&& DescriptorSource->GetNpcSequenceDescriptor(ModelStem(), OwnerStem, Label, Descriptor))
	{
		Row.bSnap = Descriptor.bStudioSnap;
	}
	return SequenceRows.Num() - 1;
}

int32 FElysiumNpc::SequenceForActivity(int32 Activity)
{
	return SelectWeightedSequence(Activity); // 0x1008dc40: no selected-clip cache
}

namespace
{
	TArray<ElysiumAnimationPick::FCandidate> AnimationCandidates(FElysiumNpc& BodyNpc, int32 Activity)
	{
		TArray<FElysiumNpcClip> Clips; // 0x1008dc40 GetSequencesForActivity
		BodyNpc.MeleeSequencesForActivity(Activity, Clips); // 0x1008dc40 / 0x103ea950: one gather
		TArray<ElysiumAnimationPick::FCandidate> Candidates; // 0x1008dc40
		IElysiumEmbodiment* const ClipSource = BodyNpc.World != nullptr ? BodyNpc.World->Embodiment() : nullptr;
		for (const FElysiumNpcClip& Clip : Clips) // 0x1008dc40: preserve table order and raw weights
		{
			FString ClipLabel; FElysiumNpcClip RawClip; // 0x1008dc40: bridge identity by raw index
			if (ClipSource == nullptr || !ClipSource->GetBodyClipByRawIndex(BodyNpc.Visual,
				BodyNpc.ModelStem(), Clip.RawIndex, ClipLabel, RawClip) || ClipLabel.IsEmpty()) // 0x1008dc40
			{
				continue; // 0x1008dc40: no descriptor input, no invented label
			}
			const int32 KernelRow = BodyNpc.SequenceRowFor(Clip.Owner, ClipLabel, Clip.IsLooping(), Clip.RawIndex); // 0x1008dc40
			BodyNpc.SequenceRows[KernelRow].bSnap = Clip.IsSnap(); // 0x10090a12
			Candidates.Add({KernelRow, Clip.Weight}); // 0x1008dc40
		}
		return Candidates; // 0x1008dc40
	}
}

int32 FElysiumNpc::SelectWeightedSequence(int32 Activity)
{
	const TArray<ElysiumAnimationPick::FCandidate> Candidates = AnimationCandidates(*this, Activity); // 0x1008dc40
	return ElysiumAnimationPick::Weighted(Candidates); // 0x10427fc0
}

int32 FElysiumNpc::SelectHeaviestSequence(int32 Activity)
{
	const TArray<ElysiumAnimationPick::FCandidate> Candidates = AnimationCandidates(*this, Activity); // 0x1008dd30
	return ElysiumAnimationPick::Heaviest(Candidates); // 0x104280f0
}

bool FElysiumNpc::SequenceBounds(int32 Seq, FVector& OutMinCm, FVector& OutMaxCm) const
{
	// 0x10090c80 / seqdesc +0x1c..+0x30: J2b's named false seam, no bbox import in V4c.
	(void)Seq; (void)OutMinCm; (void)OutMaxCm;
	return false;
}

void FElysiumNpc::MeleeSequencesForActivity(int32 Activity, TArray<FElysiumNpcClip>& OutSequences) const
{
	// `0x103ea950 GetSequencesForActivity` over the activity `0x103ea81c` (weapon `+0x5a4`) and
	// `0x103ea827` (owner `+0x5e0`) already translated by the caller. This collector is bare:
	// 0x1008dc40 and the slot331 band see the same ordered rows, with no fallback or random draw.
	OutSequences.Reset();
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	const TCHAR* Name = ElysiumRetailActivities::RegistrationNameOf(Activity);
	if (Embodiment == nullptr || Visual == nullptr || Name == nullptr)
	{
		return;   // no model: `0x103ea800` answers 0, and so does an empty list
	}
	FElysiumActivityClipRequest Request;
	FillActivityClipRequest(Request);
	Request.Activity = Name;
	Request.Variant = 0;
	Request.BodyKind = EElysiumAnimBodyKind::Cast;
	Request.bAllowFallbackLadder = false; // 0x1008dc40: bare table lookup, no resolver retry
	Embodiment->NpcActivitySequences(Request, OutSequences);
}

// --- The bridge row's descriptor accessors (spec 0002 V4a) -------------------------------------
// Each is one read of the studio sequence descriptor, answered from the row's cached descriptor
// record (`FElysiumNpcBase::SequenceDescriptorRow`: the baked clip data, asked of the embodiment
// on first sight). A row the embodiment answers nothing for reads as a descriptor with no events,
// no movement and the row's own loop bit.

namespace
{
	// The kernel's live value of one pose parameter (`m_flPoseParameter +0x690`): the LAST write of
	// that name in the port's name-keyed record; 0 when it was never written (retail's rest value).
	float LivePoseParameter(TConstArrayView<FElysiumNpc::FPoseParameterWrite> PoseParameters,
		const FString& Name)
	{
		for (int32 Index = PoseParameters.Num() - 1; Index >= 0; --Index)
		{
			if (PoseParameters[Index].Name.Equals(Name, ESearchCase::IgnoreCase))
			{
				return PoseParameters[Index].Value;
			}
		}
		return 0.f;
	}

	// The pose-weighted sum over a one-axis fan's two corners (`Studio_SeqMovement 0x100c5d10`:
	// `weight x motion` over the blend corners; a one-axis grid has two). The value is wrapped into
	// the fan's span and the cells are the span's endpoints, as `FElysiumGaitSpeedTable::SpeedAt`
	// reads the same table for the body.
	float FanWeightedSum(const float* Cells, int32 Count, float AxisMin, float AxisMax, float Value)
	{
		const float Span = AxisMax - AxisMin;
		if (Count < 2 || !(Span > 0.f) || !FMath::IsFinite(Value))
		{
			return 0.f;
		}
		const float Wrapped = AxisMin + FMath::Fmod(FMath::Fmod(Value - AxisMin, Span) + Span, Span);
		const float Position = (Wrapped - AxisMin) / (Span / static_cast<float>(Count - 1));
		const int32 Lower = FMath::Clamp(FMath::FloorToInt(Position), 0, Count - 2);
		const float Fraction = FMath::Clamp(Position - static_cast<float>(Lower), 0.f, 1.f);
		return Cells[Lower] * (1.f - Fraction) + Cells[Lower + 1] * Fraction;
	}
}

TConstArrayView<FElysiumAnimEvent> FElysiumNpc::SequenceEvents(int32 Sequence) const
{
	// `mstudioseqdesc_t`'s event table (`numevents` / `eventindex`), which `0x10091880` walks: the
	// row's clip's baked timeline, file order.
	const FSequenceDescriptorRow* const Row = SequenceDescriptorRow(Sequence);
	return (Row != nullptr && Row->Events != nullptr)
		? TConstArrayView<FElysiumAnimEvent>(*Row->Events) : TConstArrayView<FElysiumAnimEvent>();
}

bool FElysiumNpc::SequenceLoops(int32 Sequence) const
{
	// `mstudioseqdesc_t::flags & 1` (`STUDIO_LOOPING`): the baked flag where the descriptor is
	// known, else the bit the row was numbered with (row 0: its clip's own bit).
	const FSequenceDescriptorRow* const Row = SequenceDescriptorRow(Sequence);
	if (Row != nullptr && Row->bKnown)
	{
		return Row->bStudioLooping;
	}
	if (Sequence == 0)
	{
		return SequenceZero.bKnown && SequenceZero.bLoops;
	}
	return SequenceRows.IsValidIndex(Sequence) && SequenceRows[Sequence].bLoops;
}

// --- The bridge row's words for an overlay layer (spec 0002 V4o, lane O1) ----------------------

bool FElysiumNpc::SequenceSnaps(int32 Sequence) const
{
	// `mstudioseqdesc_t::flags & 2` (`STUDIO_SNAP`), `SetLayer 0x10099020`'s test at `0x100990a4`:
	// the bit the resolver's clip carried when the row was answered.
	return Sequence >= 1 && SequenceRows.IsValidIndex(Sequence) && SequenceRows[Sequence].bSnap;
}

float FElysiumNpc::SequenceCycleRateOf(int32 Sequence) const
{
	// `GetSequenceCycleRate 0x10091230`: `1 / SequenceDuration(seq)`, per second.
	const float RowSeconds = SequenceDurationSeconds(Sequence);
	if (RowSeconds > 0.f)
	{
		return 1.f / RowSeconds;
	}
	// `SequenceDuration` answered nothing above 0 (`0x100912a3 FCOM 0.0` at `0x104454c4`): retail's
	// other arm, the float 10.0 at `0x1044e664` (`0x100912c8 FLD`). In the port that is also a row no
	// baked cycle length and no play or draw has answered yet, so it is said once per (model, clip);
	// row 0 (a never-set record's sequence) is the base's own case and says nothing here.
	if (Sequence >= 1 && SequenceRows.IsValidIndex(Sequence))
	{
		static TSet<FString> ReportedRows;
		const FString Key = FString::Printf(TEXT("%s|%s|%s"), *ModelStem(),
			*SequenceRows[Sequence].OwnerStem, *SequenceRows[Sequence].Label);
		if (!ReportedRows.Contains(Key))
		{
			ReportedRows.Add(Key);
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("%s: layer clip '%s'@'%s' has no length; its cycle rate is retail's 10.0"),
				*ModelStem(), *SequenceRows[Sequence].Label, *SequenceRows[Sequence].OwnerStem);
		}
	}
	return ElysiumNpcTunables::Ten;                                           // 0x1044e664
}

void FElysiumNpc::OnOverlayLayerSet(int32 LayerIndex)
{
	// The draw of the layer slot 268 `SetLayer 0x10099020` just seeded (visual-only: the client's
	// layer blend, through the existing `UpperBody` door). Row 0 names no layer clip (`AddGesture
	// 0x100991b0` refuses a sequence `< 1`), and an unknown number draws nothing.
	if (LayerIndex < 0 || LayerIndex >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	const int32 LayerSequence = AnimOverlay[LayerIndex].Sequence;
	if (LayerSequence < 1 || !SequenceRows.IsValidIndex(LayerSequence))
	{
		return;
	}
	FSequenceRow& Row = SequenceRows[LayerSequence];
	FElysiumClipSegment Segment;
	Segment.ClipName = Row.Label;
	Segment.bLoop = false;
	Segment.Channel = EElysiumAnimChannel::UpperBody;
	Segment.bSnap = SequenceSnaps(LayerSequence);
	// The layer's owner activity by its registration name, where it has one (`AddGesture` seeds
	// -1 and writes the owner after: no name then).
	if (const TCHAR* const ActivityName =
			AnimOverlay[LayerIndex].Activity >= 0
			? ElysiumRetailActivities::RegistrationNameOf(AnimOverlay[LayerIndex].Activity) : nullptr)
	{
		Segment.Activity = ActivityName;
	}
	float DrawnSeconds = 0.f;
	if (PlayAnimSegment(Segment, &DrawnSeconds) && DrawnSeconds > 0.f)
	{
		Row.Seconds = DrawnSeconds;   // the one thing the kernel reads back: the row's length
	}
}

float FElysiumNpc::SequenceTurnYaw(int32 Sequence) const
{
	// `GetSequenceTurnYaw 0x1008f8f0` -> `0x10428690`'s `angles[1]`: the pose-weighted sum, over the
	// blend corners, of the last movement record's `angle` (the baked `YawDegrees`), at the live
	// pose parameters. Degrees. 0.0 on every shipped record.
	const FSequenceDescriptorRow* const Row = SequenceDescriptorRow(Sequence);
	if (Row == nullptr)
	{
		return 0.f;
	}
	if (Row->FanCells >= 2)
	{
		return FanWeightedSum(Row->FanTurnYawDegrees, Row->FanCells, Row->FanAxisMin,
			Row->FanAxisMax, LivePoseParameter(PoseParameterWrites, Row->FanParameter));
	}
	return Row->TurnYawDegrees;
}

float FElysiumNpc::SequenceGroundSpeedAt(int32 Sequence,
	TConstArrayView<FPoseParameterWrite> PoseParameters) const
{
	// `GetSequenceGroundSpeed 0x10091490` = `GetSequenceMoveDist / SequenceDuration`, pose-weighted
	// over the blend corners: a fan's speed at the kernel's own `move_yaw`, a plain clip's one
	// number. Centimetres per second -- the bake's unit (it converts Source units); no conversion
	// here. The pose value is retail's own (`m_flDesiredMoveYaw` -> `SetPoseParameter`), and the
	// grid's axis is in the pose parameter's own degrees, unconverted by the bake.
	const FSequenceDescriptorRow* const Row = SequenceDescriptorRow(Sequence);
	if (Row == nullptr)
	{
		return 0.f;
	}
	if (Row->FanCells >= 2)
	{
		return FanWeightedSum(Row->FanSpeedCm, Row->FanCells, Row->FanAxisMin, Row->FanAxisMax,
			LivePoseParameter(PoseParameters, Row->FanParameter));
	}
	return Row->GroundSpeedCm;
}

bool FElysiumNpc::PlaySequenceClip(int32 Sequence, float& OutSeconds, bool& bOutLoops)
{
	// The AI trace's `sequence` event (debug output only, behind its sink): the kernel's commit
	// reaches the body here whether or not the body plays it, and `rate` is the rate the body
	// actually plays it at -- `ResetSequenceInfo`'s `m_flPlaybackRate = 1.0` (`0x10090a23`) when the
	// clip starts, 0 when nothing plays it (a row whose clip the body does not author).
	auto TraceSequence = [this, Sequence](float AppliedRate)
	{
		if (IsAiTraced())
		{
			EmitAiTrace(TEXT("sequence"),
				FString::Printf(TEXT("%s rate=%g"), *TraceSequenceName(Sequence), AppliedRate));
		}
	};
	if (Sequence == 0)
	{
		// N19 (J1): the model's own sequence 0, which retail plays when a `LookupSequence` misses
		// (`StartSequence 0x101a82d0`: `m_nSequence := 0`, `0x101a833d`) and when the activity
		// ladder ends on its floor -- the body's `RawIndex 0` clip, with its own `STUDIO_LOOPING`
		// (`0x10090a12`), at `m_flPlaybackRate = 1.0` (`0x10090a23`). The trace names it `seq 0`.
		if (!ResolveSequenceZeroClip())
		{
			TraceSequence(0.f);
			return false;   // the body answers no sequence 0: today's row 0, which plays nothing
		}
		bOutLoops = SequenceZero.bLoops;
		float ZeroSeconds = 0.f;
		if (PlayAnimClip(SequenceZero.Label, SequenceZero.bLoops, &ZeroSeconds))
		{
			SequenceZero.Seconds = ZeroSeconds;
			if (!SequenceZero.OwnerStem.IsEmpty())
			{
				ScheduleIdealActivity =
					FElysiumClipIdentity(SequenceZero.OwnerStem, SequenceZero.Label);
			}
			OutSeconds = ZeroSeconds;
			TraceSequence(1.f);   // 0x10090a23 m_flPlaybackRate = 1.0
			return true;
		}
		TraceSequence(0.f);
		if (SequenceZero.Seconds > 0.f)
		{
			OutSeconds = SequenceZero.Seconds;
			return true;
		}
		return false;
	}
	if (!SequenceRows.IsValidIndex(Sequence))
	{
		TraceSequence(0.f);
		return false;   // an unknown number plays nothing
	}
	FSequenceRow& Row = SequenceRows[Sequence];
	// `GetSequenceFlags(seq) & 1` (`STUDIO_LOOPING`, `0x10090a12` -> +0x65d) is the row's own bit,
	// known whoever holds the body.
	bOutLoops = Row.bLoops;
	// `ResetSequenceInfo 0x10090950` plays `m_nSequence` on every body, whoever runs it; there is no
	// owner to ask. `StudioFrameAdvance` (slot 250, `0x1008f120`) then advances `m_flCycle` and raises
	// `m_bSequenceFinished` from the length this play answers. The only thing between the row and the
	// clip is the sequence bridge above (the named modernization: name-keyed clips for the studio
	// sequence table).
	float Seconds = 0.f;
	FElysiumClipSegment Segment;
	Segment.ClipName = Row.Label;
	Segment.bLoop = Row.bLoops;
	if (!Row.bLoops && Grapple.Type == EElysiumGrappleType::StealthKill
		&& Grapple.Role != EElysiumGrappleRole::None)
	{
		// The stealth kill's paired clip, committed as this body's own sequence by `SetGrappleActivity
		// 0x1032a100` (packet S10, D9). Retail's finished non-looping `m_nSequence` stays on its last
		// frame (`m_flCycle` clamps at 1, `m_bSequenceFinished`) until the next `ResetSequenceInfo`,
		// and `BecomeClientRagdoll 0x10090180` takes that pose; the bridge's plain one-shot would
		// hand the base channel back to the body's own classifier when the clip ends -- the victim
		// stood up between its clip's end and the attacker's (measured, `verbs_stealth_kill`: pelvis
		// 99.4 cm at the end). So the row plays under the paired clip's own presentation, the one
		// the attacker's half states (`FElysiumPlayer::StartStealthKill`): held at cycle 1, on the
		// scripted band, until `LeaveGrappleState` releases it. NAMED, scoped to mode 3: the general
		// rule (every finished non-looping row holds its last frame) is owed by the bridge.
		Segment.Source = EElysiumAnimSource::Interaction;
		Segment.Priority = EElysiumAnimPriority::Scripted;
		Segment.bHoldUntilReleased = true;
		Segment.bHoldFinalPose = true;
	}
	if (PlayAnimSegment(Segment, &Seconds))   // 0x10090950 ResetSequenceInfo
	{
		Row.Seconds = Seconds;
		if (!Row.OwnerStem.IsEmpty())
		{
			ScheduleIdealActivity = FElysiumClipIdentity(Row.OwnerStem, Row.Label);
		}
		OutSeconds = Seconds;
		TraceSequence(1.f);   // 0x10090a23 m_flPlaybackRate = 1.0
		return true;
	}
	// The clip player refused the row (the body authors no such clip): nothing plays, so the trace
	// says rate 0; a length an earlier play on this body reported still answers the cycle.
	TraceSequence(0.f);
	if (Row.Seconds > 0.f)
	{
		OutSeconds = Row.Seconds;
		return true;
	}
	// SEAM: the row has never played on this body, so its length is unknown to the kernel and the
	// sequence's cycle does not advance (the caller's named arm).
	return false;
}

bool FElysiumNpc::ShouldMaintainActivity()
{
	// `0x102bf510`, slot 466. Three arms, and the first one's answer is FALSE — retail returns
	// `EAX & 0xffffff00`, whose low byte is zero.
	if (Schedule.IsRunning()
		&& GetLocalScheduleId(Schedule.Current) == GScheduleRefusingMaintain)
	{
		return false;
	}
	if (bForceMaintainActivity)   // +0x65fa
	{
		return true;
	}
	// `CAI_BaseNPC::ShouldMaintainActivity` `0x10272790`: `GetState()` (slot 464) against 4
	// (`NPC_STATE_SCRIPT`) with `m_Activity != 2`. Everything else maintains.
	return !(NpcKernelAnimShared::AnimRetailNpcState(Mind.State()) == 4 && ActivityNumber != 2);
}

int32 FElysiumNpc::IdleSequenceGate() const
{
	// `0x102b8a10`. Two gates and one answer:
	//   HasCondition(0x58)                       — COND_ENEMY_DEAD
	//   SelectWeightedSequence(0x61) != -1       — the body authors a clip for the gate's activity
	//   -> schedule 8, after stamping the selector trace (+0x1b30 the source file, +0x1b34 line
	//      0x5f20). The shape map calls that word ABSENT: this runtime records selections in the
	//      mind's transition trace instead of carrying retail's file/line pair.
	// Retail schedule number 8 has no row in `int32` yet, so the NUMBER is the answer;
	// 0 is `SCHED_NONE`.
	if (!Cognition.Conditions.Has(EElysiumNpcCond::EnemyDead))
	{
		return 0;
	}
	if (SelectWeightedSequenceForActivity(GIdleGateActivity) == INDEX_NONE)
	{
		return 0;
	}
	return GIdleGateSchedule;
}

// --- The choreo-scene latches (slots 59, 60, 61) ------------------------------------------------

void FElysiumNpc::Slot59(void* SceneEntity)
{
	// `CAI_BaseNPCTroika::vfunc59` `0x102b51e0` — the scene ENTERS. `m_bInChoreoScene` (+0x5bc4) is
	// raised unconditionally; `m_bCutsceneForceLOD` (+0x1590) only when the scene entity's own
	// `+0x57d` byte is set.
	bInChoreoScene = true;
	if (SceneEntityForcesCutsceneLod(SceneEntity))
	{
		bCutsceneForceLOD = true;
	}
}

void FElysiumNpc::Slot60(void* SceneEntity)
{
	// `0x102b5220` — the scene LEAVES. The exact inverse, gated on the same byte, so a scene that
	// never forced LOD cannot clear a flag another scene raised.
	bInChoreoScene = false;
	if (SceneEntityForcesCutsceneLod(SceneEntity))
	{
		bCutsceneForceLOD = false;
	}
}

void FElysiumNpc::Slot61(void* SceneEntity)
{
	// `0x102b5260` — byte for byte the same body as slot 60. Two slots, one behaviour: retail
	// declares two exits (the ordinary one and the cancel) and gives them the same implementation.
	bInChoreoScene = false;
	if (SceneEntityForcesCutsceneLod(SceneEntity))
	{
		bCutsceneForceLOD = false;
	}
}
