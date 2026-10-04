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
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumStanceChoice Choice = ElysiumStance::ChangeStance(StanceClips, Stance, Now,
		Stream);
	if (!Choice.IsSet())
	{
		return INDEX_NONE;
	}
	return LookupSequenceByName(*Choice.Clip);
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

int32 FElysiumNpc::SequenceRowFor(const FString& OwnerStem, const FString& Label, bool bLoops)
{
	if (SequenceRows.IsEmpty())
	{
		SequenceRows.AddDefaulted();   // row 0: retail's floor sequence, which plays nothing here
	}
	for (int32 Index = 1; Index < SequenceRows.Num(); ++Index)
	{
		const FSequenceRow& Row = SequenceRows[Index];
		if (Row.OwnerStem == OwnerStem && Row.Label == Label && Row.bLoops == bLoops)
		{
			return Index;
		}
	}
	FSequenceRow& Row = SequenceRows.AddDefaulted_GetRef();
	Row.OwnerStem = OwnerStem;
	Row.Label = Label;
	Row.bLoops = bLoops;
	return SequenceRows.Num() - 1;
}

int32 FElysiumNpc::SequenceForActivity(int32 Activity)
{
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Visual == nullptr)
	{
		return INDEX_NONE;
	}
	// `ACT_DISPOSITION` (0xf1): the sequences a retail model tags with it are its stance clips, which
	// this runtime resolves as the stance set rather than through the activity resolver. The
	// weighted pick answers the current stance's idle — it does NOT roll the stance machine; only
	// slot 611 (through `0x10295a80`) does.
	if (Activity == NpcKernelAnimShared::GAnimActDisposition)
	{
		if (!EnsureStanceResolved())
		{
			return INDEX_NONE;
		}
		const int32 Current = FMath::Clamp(Stance.Current, 0, ElysiumStance::Count - 1);
		return StanceClips.Idle[Current].IsEmpty() ? INDEX_NONE
			: SequenceRowFor(FString(), StanceClips.Idle[Current], /*bLoops=*/true);
	}
	// The retail activity VALUE, keyed by the name its registration pushed
	// (`ActivityList_RegisterSharedActivities`' 4,460 registrations): the shared rows' name table
	// entry, or for one of the 29 grapple rows (which name nothing in retail's table) the name the
	// registration pushed -- the grapple activities resolve by id.
	const TCHAR* Name = ElysiumRetailActivities::RegistrationNameOf(Activity);
	if (Name == nullptr)
	{
		return INDEX_NONE;
	}
	FElysiumActivityClipRequest Request;
	FillActivityClipRequest(Request);
	Request.Activity = Name;
	Request.Variant = 0;   // the resolver's primary answer: this runtime carries no sequence weights
	Request.BodyKind = EElysiumAnimBodyKind::Cast;
	// The answer is a pure function of the request (the model, the activity, the class, the weapon
	// and the state the armed/alert branch reads), and the navigator asks every frame a body moves,
	// so it is cached on those words.
	const FString Key = FString::Printf(TEXT("%s|%s|%s|%s|%d"), *Request.Stem, Name,
		*Request.ActorClassname, *Request.WeaponClassname, static_cast<int32>(Request.ActorState));
	if (const int32* Cached = SequenceResolveCache.Find(Key))
	{
		return *Cached;
	}
	FElysiumActivityClip Clip;
	const int32 Row = Embodiment->ResolveNpcActivityClip(Request, Clip)
		? SequenceRowFor(Clip.OwnerStem, Clip.Label, Clip.bLooping)
		: INDEX_NONE;   // the body authors no clip for it: retail's own -1
	SequenceResolveCache.Add(Key, Row);
	return Row;
}

bool FElysiumNpc::PlaySequenceClip(int32 Sequence, float& OutSeconds, bool& bOutLoops)
{
	// The AI trace's `sequence` event (debug output only, behind its sink): the kernel's commit
	// reaches the body here whether or not the body plays it, and `rate` is the rate the body
	// actually plays it at -- `ResetSequenceInfo`'s `m_flPlaybackRate = 1.0` (`0x10090a23`) when the
	// clip starts, 0 when nothing plays it (row 0, or a row whose clip the body does not author).
	auto TraceSequence = [this, Sequence](float AppliedRate)
	{
		if (IsAiTraced())
		{
			EmitAiTrace(TEXT("sequence"),
				FString::Printf(TEXT("%s rate=%g"), *TraceSequenceName(Sequence), AppliedRate));
		}
	};
	if (!SequenceRows.IsValidIndex(Sequence) || Sequence == 0)
	{
		TraceSequence(0.f);
		return false;   // row 0 and an unknown number play nothing
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
	if (PlayAnimClip(Row.Label, Row.bLoops, &Seconds))   // 0x10090950 ResetSequenceInfo
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
