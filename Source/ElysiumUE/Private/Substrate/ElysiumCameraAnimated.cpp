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
	// `m_bSequenceLoops` is shown beside the deadline precisely because it does not move it.
	Out.Emplace(TEXT("SequenceLoops"), bSequenceLoops ? TEXT("yes (ends on the first wrap)")
		: TEXT("no"));
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
		bSequenceLoops = false;
		NextThink = ELYSIUM_NEVER_THINK;
		return;
	}
	bSequenceLoops = bLoops;
	SequenceEndTime = Now + FMath::Max(0.0f, Seconds);
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
	// The frame advance and the event dispatch are the world's own animation pass here
	// (`FElysiumEntityWorld::AdvanceAnimEvents`), so what is left is the finished test — and that
	// test is `m_bSequenceFinished` alone. No cycle compare, and no `m_bSequenceLoops` consultation:
	// a looping clip ends the camera on its first wrap.
	const double Now = World ? World->NowSeconds() : 0.0;
	if (IsSequenceFinished(Now))
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
	bSequenceLoops = false;
	NextThink = ELYSIUM_NEVER_THINK;
	ScriptHide();
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
