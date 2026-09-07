#include "Substrate/ElysiumCameraAnimated.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumCameraCinematic.h"
#include "Substrate/ElysiumClassFields.h"

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
	BuildBody();
}

void FElysiumCameraAnimated::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("AnimName"), AnimName.IsEmpty() ? TEXT("<none>") : AnimName);
	Out.Emplace(TEXT("Camera"), CineCamera.IsSet() ? TEXT("live") : TEXT("<none>"));
	Out.Emplace(TEXT("SequenceEnds"), SequenceEndTime >= 0.0
		? FString::Printf(TEXT("%.3f"), SequenceEndTime) : TEXT("<not playing>"));
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

void FElysiumCameraAnimated::PlayCameraAnimation()
{
	// `FUN_10071770`:
	//   seq = LookupSequence(this, animName);
	//   if (seq < 0) { Msg("%s no sequence named:%s\n"); m_nSequence = 0; return; }
	//   m_nSequence = seq; m_flCycle = 0; ResetSequenceInfo();
	//   FireOutput(m_OnCameraBegin);
	//   ThinkSet(CameraAnimatedThink, 0.0); m_flNextThink = curtime + 0.1;
	float Seconds = 0.0f;
	const bool bPlaying = !AnimName.IsEmpty()
		&& PlayAnimClip(AnimName, /*bLoop*/ false, &Seconds);
	const double Now = World ? World->NowSeconds() : 0.0;

	if (!bPlaying)
	{
		// **A named divergence, stated.** Retail's `seq < 0` arm `Msg`s and *returns* — it fires no
		// `OnCameraBegin`, arms no think, and leaves the cine camera adopted with nothing to end it,
		// so the view is stuck on a still camera until something else takes the slot. The port's
		// animation seam resolves a clip through the NPC clip manifest
		// (`FElysiumAnimating::PlayAnimClip`), which a camera rig's own model need not be in, so
		// this arm is reachable for a reason retail's is not. Ending the camera on the next think is
		// chosen over reproducing a view with no exit; the missing piece is "play an arbitrary named
		// sequence on a bare animating entity".
		UE_LOG(LogElysiumCamAnimated, Warning, TEXT("%s no sequence named:%s"),
			*DebugString(), *AnimName);
		SequenceEndTime = Now;
		NextThink = static_cast<float>(Now + ElysiumCameraAnimatedImpl::AnimatedThinkInterval);
		return;
	}
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
	// (`FElysiumEntityWorld::AdvanceAnimEvents`), so what is left is the finished test.
	const double Now = World ? World->NowSeconds() : 0.0;
	if (SequenceEndTime >= 0.0 && Now >= SequenceEndTime)
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
	SequenceEndTime = -1.0;
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
