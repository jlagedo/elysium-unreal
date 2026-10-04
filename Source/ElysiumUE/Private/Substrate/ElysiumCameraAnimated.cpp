#include "Substrate/ElysiumCameraAnimated.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "Substrate/ElysiumClassFields.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCamAnimated, Log, All);

// Unity build: the file's helpers take a named namespace (the project's convention).
namespace ElysiumCameraAnimatedImpl
{
	// `_DAT_104493d0 = 0.1` (double, image) — `CCameraAnimated`'s own think interval, ten times
	// slower than the 1/24 s the cine camera it drives runs at.
	constexpr float AnimatedThinkInterval = 0.1f;
}

void FElysiumCameraAnimated::Spawn()
{
	// `0x10071330`, verbatim:
	//   name = GetModelName(); if (!name) name = "";
	//   if (*name) { Precache(); SetModel(); return; }
	//   Warning("%s at %.0f %.0f %0.f missing modelname\n", ...); UTIL_Remove(this);
	if (Model.IsEmpty())
	{
		UE_LOG(LogElysiumCamAnimated, Warning, TEXT("%s at %s missing modelname"),
			*DebugString(), *Origin.ToCompactString());
		Kill();
		return;
	}
	BuildCameraVisual();
}

void FElysiumCameraAnimated::BuildCameraVisual()
{
	// Retail's `SetModel` gives the entity one model and `LookupSequence` runs on it. The port has
	// three model manifests behind one `model` key, so the build order IS the vocabulary choice, and
	// it is the order `FElysiumLockableEntity::Spawn` already uses for the same reason:
	//
	//  1. `animated_props` — a camera rig is a prop rig. This is the route that makes the class work
	//     at all: `FElysiumAnimating::BuildBody` stands a CHARACTER body only, and the clip resolve
	//     under it (`UElysiumAnimSubsystem::ResolveClip`) refuses any stem outside the manifest's
	//     `npcs`/`banks` groups — which a camera rig is never in. `special-case.txt`'s
	//     `AttachPos "Bone: cam_bone"` names a bone on exactly this body.
	//  2. the character manifest, for a rig authored as one.
	//  3. a static prop, which stands the model and plays nothing — the same observable state
	//     retail's `seq < 0` arm leaves behind, reached for the same reason (no sequence).
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def)
	{
		return;   // headless: no body, and `animname` then strands exactly as a missing sequence does
	}

	AnimatedStem = Embodiment->AnimatedPropStemForModel(Model);
	if (!AnimatedStem.IsEmpty())
	{
		const FQuat Rotation(ElysiumSkeletalBasis::FromSourceAngles(Angles));
		Visual = Embodiment->BuildAnimatedPropVisual(AnimatedStem, Origin, Rotation,
			Embodiment->BodyScaleFor(*Def), Handle.Index);
		if (Visual != nullptr)
		{
			// Teardown only: no use anchor. `CCameraAnimated` declares no `+USE` verb, so the rig
			// must not become a focus target the way a lockable's body does.
			World->RegisterPropBody(Visual);
			return;
		}
		AnimatedStem.Reset();
	}

	BuildBody();
	if (Visual != nullptr)
	{
		return;
	}

	StaticBody = Embodiment->BuildPropVisual(Model, Origin,
		FQuat(FRotator(0.0f, -Angles.Y, 0.0f)), Embodiment->BodyScaleFor(*Def));
	if (StaticBody != nullptr)
	{
		World->RegisterPropBody(StaticBody);
	}
}

void FElysiumCameraAnimated::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("AnimName"), AnimName.IsEmpty() ? TEXT("<none>") : AnimName);
	// Which of the three routes `BuildCameraVisual` took, because it is also which clip vocabulary
	// `animname` was looked up in.
	FString BodyRow(TEXT("<none>"));
	if (!AnimatedStem.IsEmpty())
	{
		BodyRow = FString::Printf(TEXT("animated prop %s"), *AnimatedStem);
	}
	else if (Visual != nullptr)
	{
		BodyRow = TEXT("character");
	}
	else if (StaticBody != nullptr)
	{
		BodyRow = TEXT("static prop");
	}
	Out.Emplace(TEXT("Body"), MoveTemp(BodyRow));
	Out.Emplace(TEXT("Camera"), CineCamera.IsSet() ? TEXT("live") : TEXT("<none>"));
	Out.Emplace(TEXT("SequenceEnds"), SequenceEndTime >= 0.0
		? FString::Printf(TEXT("%.3f"), SequenceEndTime) : TEXT("<not playing>"));
	// `m_bSequenceLoops` is shown beside the deadline precisely because the think's exit never reads it.
	Out.Emplace(TEXT("SequenceLoops"), bSequenceLoops ? TEXT("yes (ends on the first lap's look-ahead)")
		: TEXT("no"));
	// `m_flLastEventCheck` (`+0x658`) and `m_bSequenceFinished` (`+0x65c`), the dispatcher's words.
	Out.Emplace(TEXT("LastEventCheck"), FString::Printf(TEXT("%.3f"), SequenceWords.LastEventCheck));
	Out.Emplace(TEXT("SequenceFinished"), SequenceWords.bSequenceFinished ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("FreezePlayer"),
		(SpawnFlags & ElysiumCineCam::SF_FreezePlayer) != 0 ? TEXT("yes") : TEXT("no"));
}

void FElysiumCameraAnimated::InputStartCamera()
{
	// `FUN_10071550`:
	//   FUN_10071660(this);                        // stop whatever was running
	//   m_iEFlags &= ~0x42; RemoveFlag(0x40000);   // undo EndCamera's dormancy
	//   player = UTIL_PlayerByIndex(1); if (!player) return;
	//   cam = FUN_10070690(this);                  // the CamMode 4 camera, anchored to THIS entity
	//   SetCineCamera(player, cam);
	//   FUN_10071770(this, m_sAnimName);
	//   if (spawnflags & 1) SetImmobilized(player, true);
	InputEndCamera();
	// `m_iEFlags &= ~0x42`: EndCamera's `EFL_DORMANT` (and `0x40`) cleared.
	bEflDormant = false;
	if (!World)
	{
		return;
	}
	ScriptUnhide();

	FElysiumPlayer* PlayerEnt = World->FindPlayer();
	if (!PlayerEnt)
	{
		return;
	}

	// `FUN_10070690(entity)`: create the disposable camera, `SetShot("Animated", 4, NULL)` — its
	// only failure path — then `SetShotAnchorEntity(cam, entity, 0)` with the **full** resolve, so
	// the `Bone: cam_bone` index the authored shot asks for is looked up on this model. (The mode-3
	// factory is the one that stores its handle raw.)
	FElysiumEntityHandle Anchors[FElysiumShotBindings::Num];
	Anchors[0] = Handle;
	CineCamera = FElysiumCameraCinematic::CreateRuntimeCamera(*World, TEXT("Animated"),
		static_cast<int32>(EElysiumCineCamMode::Animated), Anchors);

	// `SetCineCamera(player, resolve(+0x7f0))` runs even when the factory failed, which is
	// `SetCineCamera(player, NULL)` — the slot is cleared rather than left holding the old camera.
	FElysiumEntity* Camera = World->Resolve(CineCamera);
	FElysiumCameraCinematic* Cine = Camera ? Camera->AsCameraCinematic() : nullptr;
	World->SetCineCamera(CineCamera, Cine ? Cine->PublishedShotId : 0,
		/*bDisposable*/ Cine != nullptr, TEXT("Animated"));

	PlayCameraAnimation();

	// The freeze bit. **This is the one class that reads `spawnflags & 1`** — on
	// `camera_cinematic` the same bit is authored 50 times and dead, because `StartShot`
	// immobilizes unconditionally.
	if ((SpawnFlags & ElysiumCineCam::SF_FreezePlayer) != 0)
	{
		PlayerEnt->SetImmobilized(true);
	}
}

bool FElysiumCameraAnimated::PlayCameraSequence(float& OutSeconds, bool& bOutLoops)
{
	// `CBaseAnimating::LookupSequence(this, animName)` on the entity's own model, then play it.
	// Whichever manifest stood the body owns the lookup; there is no second attempt, because retail
	// has exactly one and because a stem answered by one manifest is never in the other.
	OutSeconds = 0.0f;
	bOutLoops = false;
	if (AnimName.IsEmpty())
	{
		return false;   // `m_sAnimName ? ... : ""` — LookupSequence("") answers < 0
	}
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (Embodiment != nullptr && Visual != nullptr && !AnimatedStem.IsEmpty())
	{
		// `FindAnimatedPropClip` IS the lookup and it hands back the clip's own `STUDIO_LOOPING`
		// bit, which is what `ResetSequenceInfo` copies into `m_bSequenceLoops`. The clip is played
		// with that bit, exactly as retail plays the sequence with its own flags; the shot still
		// ends on the first wrap (see `SequenceEndTime`).
		if (!Embodiment->FindAnimatedPropClip(AnimatedStem, AnimName, bOutLoops))
		{
			return false;
		}
		return Embodiment->PlayAnimatedPropClip(Visual, AnimatedStem, AnimName, bOutLoops,
			&OutSeconds);
	}
	// The character vocabulary. `PlayAnimClip` returns false with no body at all, which is the
	// headless world and the static-prop body alike.
	return PlayAnimClip(AnimName, /*bLoop*/ false, &OutSeconds);
}

void FElysiumCameraAnimated::PlayCameraAnimation()
{
	// `FUN_10071770`:
	//   seq = LookupSequence(this, animName);
	//   if (seq < 0) { Msg("%s no sequence named:%s\n"); m_nSequence = 0; return; }
	//   m_nSequence = seq; m_flCycle = 0; ResetSequenceInfo();
	//   FireOutput(m_OnCameraBegin);
	//   ThinkSet(CameraAnimatedThink, 0.0); m_flNextThink = curtime + 0.1;
	float Seconds = 0.0f;
	bool bLoops = false;
	const bool bPlaying = PlayCameraSequence(Seconds, bLoops);
	const double Now = World ? World->NowSeconds() : 0.0;

	if (!bPlaying)
	{
		// **The strand, reproduced (RC15.3 §3.2).** Retail's `seq < 0` arm prints, writes
		// `m_nSequence = 0` — not `-1` — and *returns*. It fires no `OnCameraBegin`, calls no
		// `ThinkSet`, and never touches `m_flNextThink`. So the think never runs, `EndCamera`
		// (`FUN_10071660`, the only exit) is never reached, the `CamMode 4` camera adopted two lines
		// earlier in `StartCamera` stays adopted frozen at sequence 0 cycle 0, `OnCameraComplete`
		// never fires, and the `spawnflags & 1` freeze `StartCamera` applies immediately AFTER this
		// call is never released. The view is stuck on a still camera, with the player frozen, until
		// something else takes the adoption slot or the map fires `EndCamera` by hand.
		//
		// Everything below this line is left exactly as it was: no deadline, no think. The warning
		// is the port's own diagnostic, since a stranded view is otherwise silent; retail's `Msg` is
		// itself defective (`"%s no sequence named:%s\n"` pushes ONE argument for two `%s`, so the
		// name it prints is the entity's and the sequence is never shown). Recorded in
		// `docs/vtmb/retail-defects.md` §7.
		UE_LOG(LogElysiumCamAnimated, Warning,
			TEXT("%s no sequence named:%s — the camera is stranded (retail FUN_10071770 seq < 0)"),
			*DebugString(), *AnimName);
		SequenceEndTime = -1.0;
		SequenceStartTime = -1.0;
		SequenceSeconds = 0.0f;
		SequenceWords = FElysiumSequenceWords();
		bSequenceLoops = false;
		NextThink = ELYSIUM_NEVER_THINK;
		return;
	}
	// `m_nSequence = seq; m_flCycle = 0; ResetSequenceInfo()` (`0x10090950`): the loop bit copied
	// from the descriptor, `m_flLastEventCheck` (`+0x658`) zeroed, `m_bSequenceFinished` (`+0x65c`)
	// cleared, the playback rate 1.0.
	bSequenceLoops = bLoops;
	SequenceStartTime = Now;
	SequenceSeconds = FMath::Max(0.0f, Seconds);
	SequenceWords = FElysiumSequenceWords();
	SequenceEndTime = Now + SequenceSeconds;
	FireOutput(FName(TEXT("OnCameraBegin")), FElysiumEntityHandle::Invalid());
	NextThink = static_cast<float>(Now + ElysiumCameraAnimatedImpl::AnimatedThinkInterval);
}

void FElysiumCameraAnimated::Think()
{
	// `FUN_10071840`, a 10 Hz clock:
	//   dt = StudioFrameAdvance(0); DispatchAnimEvents(dt, this, ...);
	//   if (!m_bSequenceFinished) m_flNextThink = curtime + 0.1; else FUN_10071660(this);
	//   this->+0x828 = camera_showdebug.IsCommand() ? 0 : (camera_showdebug.GetInt() != 0);
	//
	// Advance -> dispatch -> the finished test, in that order, and the test is `m_bSequenceFinished`
	// (`+0x65c`, `param_1[0x197]`) alone: no cycle compare and no `m_bSequenceLoops` consultation in
	// the think. The byte it reads is the one the dispatcher just wrote — cleared at entry, set when
	// the 0.1 s look-ahead end reaches 1.0 — so the camera ends, and `OnCameraComplete` fires, one
	// look-ahead before the pose's own end.
	const double Now = World ? World->NowSeconds() : 0.0;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;

	// --- slot 250 `StudioFrameAdvance(0)` (`vt+1000`). ---
	// `m_flCycle` from the sequence's start and length at playback rate 1.0 (`ResetSequenceInfo`'s):
	// wrapped into `[0,1)` for a looping sequence, clamped to 1.0 for a one-shot (`0x1008f120`). A
	// zero-length clip has no rate to advance at and stands at its end, so it finishes on the first
	// think, as it did under the time compare this replaces.
	FElysiumSequenceWords& Words = SequenceWords;
	const float Elapsed = static_cast<float>(FMath::Max(0.0, Now - SequenceStartTime));
	if (SequenceSeconds > 0.0f)
	{
		const float Raw = Elapsed / SequenceSeconds;
		Words.Cycle = bSequenceLoops ? FMath::Frac(Raw) : FMath::Min(Raw, 1.0f);
		Words.CycleRate = 1.0f / SequenceSeconds;   // GetSequenceCycleRate x m_flPlaybackRate (1.0)
	}
	else
	{
		Words.Cycle = 1.0f;
		Words.CycleRate = 0.0f;
	}
	Words.Sequence = 0;   // the port's camera carries no sequence index; the census names the clip
	Words.CensusOwner = AnimatedStem.IsEmpty() ? ModelStem() : AnimatedStem;
	Words.CensusLabel = AnimName;
	Words.AnimTime = static_cast<float>(Now);   // `m_flAnimTime`, stamped by the advance
	// `m_bSequenceLoops` (`+0x65d`) is `ResetSequenceInfo`'s copy of `seqdesc.flags & 1`.
	Words.bLoops = bSequenceLoops;
	Words.bDescriptorLoops = bSequenceLoops;
	Words.bHasDescriptor = true;

	// The sequence's event table, under the bank and label the pose layer publishes for the rig's
	// base channel. A body publishing no phase names no table: the window still advances and the
	// finish is still written.
	TConstArrayView<FElysiumAnimEvent> Events;
	FElysiumClipPhase Phase;
	if (Embodiment != nullptr && Visual != nullptr
		&& Embodiment->GetBodyClipPhase(Visual, EElysiumAnimChannel::Base, Phase) && Phase.IsValid())
	{
		if (const TArray<FElysiumAnimEvent>* Timeline =
			Embodiment->GetNpcEventTimeline(Phase.OwnerStem, Phase.Label, Phase.OwnerRoot))
		{
			Events = TConstArrayView<FElysiumAnimEvent>(*Timeline);
		}
	}

	// --- slot 258 `DispatchAnimEvents(dt, this)` (`vt+0x408`). ---
	// `CCameraAnimated` is a `CBaseAnimating`: the base dispatcher `0x10091880` only, no overlay
	// layers, the camera as source and handler. Its slot 259 is `CBaseAnimating::HandleAnimEvent`
	// `0x10091da0` (2070, 2071, 4005, anything else a `DevWarning`). The rising edge's
	// `OnSequenceFinished` (`0x10091c80`) is an empty body.
	// The words are this entity's own, written in place: `bSequenceFinished` goes in as the live
	// `+0x65c` (retail's `cVar2`) and the dispatcher writes all three words before its event loop,
	// so nothing is copied back afterwards.
	ElysiumAnimEvents::DispatchBase(Words, Events, *this, *this);
	if (SequenceEndTime < 0.0)
	{
		// A handler ended the camera inside the dispatch (`FUN_10071660` ran `ResetSequenceInfo` and
		// cleared the think): the reset words stand, and there is nothing left to test or re-arm.
		return;
	}

	// --- `if (!m_bSequenceFinished) m_flNextThink = curtime + 0.1; else FUN_10071660(this);` ---
	if (IsSequenceFinished())
	{
		InputEndCamera();
		return;
	}
	NextThink = static_cast<float>(Now + ElysiumCameraAnimatedImpl::AnimatedThinkInterval);
}

void FElysiumCameraAnimated::InputEndCamera()
{
	// `FUN_10071660`:
	//   FireOutput(m_OnCameraComplete);
	//   if (handleLive(+0x7f0)) UTIL_Remove(resolve(+0x7f0));   // DIRECTLY, not SetCineCamera(NULL)
	//   MakeDormant(this); AddFlag(0x40000);
	//   m_nSequence = 0; m_flCycle = 0; ResetSequenceInfo();
	//   if (player && (spawnflags & 1)) SetImmobilized(player, false);
	//
	// It clears no `m_iVFlags` and restores nothing else.
	const bool bWasRunning = CineCamera.IsSet() || SequenceEndTime >= 0.0;
	if (!bWasRunning)
	{
		return;   // `StartCamera`'s leading call on an idle entity is inert
	}
	FireOutput(FName(TEXT("OnCameraComplete")), FElysiumEntityHandle::Invalid());

	if (World)
	{
		if (FElysiumEntity* Camera = World->Resolve(CineCamera))
		{
			Camera->Kill();
		}
		// Retail leaves `m_iCameraOverrideIdx` pointing at the dead entity index until the client's
		// handle stops resolving, which it does on the very next frame — the rendered result is the
		// player's own eye. The port cannot render a dead entity's pose at all, so the slot is
		// released here instead, and only when it is still this camera: same observable frame, no
		// dangling index.
		if (World->CineCameraEntity().IsSet() && World->CineCameraEntity() == CineCamera)
		{
			World->ClearScriptedCamera();
		}
		if ((SpawnFlags & ElysiumCineCam::SF_FreezePlayer) != 0)
		{
			if (FElysiumPlayer* PlayerEnt = World->FindPlayer())
			{
				PlayerEnt->SetImmobilized(false);
			}
		}
	}
	CineCamera = FElysiumEntityHandle::Invalid();
	// `m_nSequence = 0; m_flCycle = 0; ResetSequenceInfo()` — which is also the only reset of
	// `m_bSequenceFinished` and of `m_bSequenceLoops`.
	SequenceEndTime = -1.0;
	SequenceStartTime = -1.0;
	SequenceSeconds = 0.0f;
	SequenceWords = FElysiumSequenceWords();
	bSequenceLoops = false;
	NextThink = ELYSIUM_NEVER_THINK;
	ScriptHide();
	// `MakeDormant` (`0x100a8060`) also sets `EFL_DORMANT`, which the activation pass reads.
	bEflDormant = true;
}

// --- Registration -------------------------------------------------------------------------------

namespace ElysiumCameraAnimatedImpl
{
	TUniquePtr<FElysiumEntity> MakeCameraAnimated()
	{
		return MakeUnique<FElysiumCameraAnimated>();
	}

	void BuildCameraAnimatedClass(FElysiumClassDesc& D)
	{
		D.Input(TEXT("StartCamera"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumCameraAnimated&>(E).InputStartCamera(); });
		D.Input(TEXT("EndCamera"), [](FElysiumEntity& E, const FElysiumInputArgs&)
			{ static_cast<FElysiumCameraAnimated&>(E).InputEndCamera(); });
		ElysiumAddClassField(D, TEXT("animname"), &FElysiumCameraAnimated::AnimName);
	}

	// `vtmb_fields CCameraAnimated`: the chain is `CCameraAnimated -> CBaseAnimating -> ...`, so the
	// class inherits `model`, `angles`, `StartHidden`, the eight `OnScriptEventNN` outputs and the
	// whole `CBaseAnimating` set beside its own `animname` row.
	FElysiumClassRegistrar GRegCameraAnimated(TEXT("camera_animated"), ElysiumAnimatingClassName(),
		&MakeCameraAnimated, &BuildCameraAnimatedClass);
}
