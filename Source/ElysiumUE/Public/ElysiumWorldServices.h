#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"   // FElysiumAudioVoiceHandle + FElysiumPlayParams (passed by value)
#include "ElysiumEntityHandle.h"

class FElysiumDlgConversation;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FElysiumCameraShot;
struct FElysiumEntityDef;
struct FElysiumSignData;

// The substrate's outbound seam (runtime-architecture.md §7, roadmap 11.2).
//
// FElysiumEntityWorld and every entity class under it are plain C++. What they need from the
// engine — a body to stand, a voice to play, a map to travel to, a panel to put on screen — comes
// through these four interfaces and nothing else: no Cast<AElysiumMapActor>, no
// GetWorld()->GetFirstPlayerController(), no GetSubsystem<> walk off the owning actor. The
// direction of dependency is one-way (§2): the substrate knows IElysiumWorldServices, actors know
// the substrate.
//
// **Any member of the bundle may be null**, and every call site must handle it. That is not a
// defensive habit — it is the existing A/B path formalised: `elysium.NpcBodies 0` and
// `elysium.BrushBodies 0` already run the whole logic layer with no embodiment, and a Substrate-
// tier test runs it with no engine at all. A null service means "this capability is absent", which
// is a state the game already ships.
//
// What it buys: a Substrate-tier test can drive a whole map's logic headlessly against a recording
// stub — with no RHI, no actors and no `$ELYSIUM_EXPORT_ROOT` — which is the missing middle tier between
// variant arithmetic and launching the game (Elysium.Substrate.WorldServices).

// --------------------------------------------------------------------------------------------
// Embodiment — bodies, meshes, clips, skins, and the player's own body.
//
// Implemented by AElysiumMapActor: every component it builds belongs to it and dies with it, so
// "the world logically owns the embodiments, the actor physically owns them" stays true (R1).
// The player half is here because the pawn IS the player's body (S3); 11.4 re-homes the player's
// *state* onto an entity, and these calls become ordinary entity operations at that point.
// --------------------------------------------------------------------------------------------
class IElysiumEmbodiment
{
public:
	virtual ~IElysiumEmbodiment() = default;

	// The uniform scale a body built for this def takes (the 3D-skybox miniature's scale for a
	// sky-scope entity, 1 for everything else).
	virtual float BodyScaleFor(const FElysiumEntityDef& Def) const = 0;

	// B3/8.5 — stand one NPC skeletal body, playing the standing idle its disposition selects.
	// Null on a missing/failed glb or an empty stem.
	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) = 0;
	// Re-run the default-idle policy on a live body and crossfade to the result (a disposition change).
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 IdleVariant) = 0;
	// Crossfade a live body to a named clip; OutSeconds (optional) receives its authored length,
	// which is what a scripted_sequence schedules its OnEndSequence off.
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds) = 0;

	// 12.1 — a choreo scene's whole-cast performance. The clip lives in a cinematic anim set that
	// no NPC's include tree names, so it is addressed by the scene's own anim-set model plus the
	// actor's `bonerename` root (PL16) rather than through the clip vocabulary.
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) = 0;
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) = 0;
	virtual void StopCinematicClip(USkeletalMeshComponent* Body) = 0;

	// v4 animated props. Model selection is explicit and manifest-backed; ordinary props stay on
	// the existing static representation. The skeletal surface remains non-solid.
	virtual FString AnimatedPropStemForModel(const FString& ModelPath) const = 0;
	virtual USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem,
		const FVector& Location, const FQuat& Rotation, float UniformScale) = 0;
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) = 0;
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent* Comp,
		const FString& StaticStem, int32 Family) = 0;

	// A BSP brush entity's baked, local-pivot render surface. ParentBody owns its transform and
	// collision; the returned mesh is visual-only and attached at identity.
	virtual UStaticMeshComponent* BuildBrushVisual(const FString& Stem,
		USceneComponent* ParentBody, float UniformScale, bool bSky) = 0;

	// 8.3 — stand a non-solid dynamic-prop body. 8.4 — stand the same mesh with its `.phy` collision
	// and authored mass, ready for the leaf to drive SetSimulatePhysics. Null on an unbaked model.
	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) = 0;
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) = 0;
	// Repaint a prop body to one of its model's alternate skin families (VtMB's `skin`).
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) = 0;

	// Stand/clear the player surface through the same glTF skeletal path as NPCs, then seat it on
	// whichever movement pawn is active. The returned component is also the FElysiumPlayer visual,
	// so scene clips and understudy model-copying use the ordinary animating contract.
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) = 0;
	virtual void ClearPlayerVisual() = 0;

	// --- The player's body ------------------------------------------------------------------
	// The eye: where the player is looking from and along. False when there is no player (the menu
	// backdrop seats no pawn), which every caller treats as "the player cannot see it".
	virtual bool GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const = 0;
	// The body: its world position and facing yaw. False when there is no player.
	virtual bool GetPlayerOrigin(FVector& OutLocation, float& OutYaw) const = 0;
	// Place the player at a Source absorigin (feet) with the given yaw. The body owns the capsule
	// compensation — Source places feet, an Unreal capsule is centred.
	virtual void TeleportPlayer(const FVector& FeetOrigin, float Yaw) = 0;
	// trigger_hurt / a door closing on the player. No-op when there is no player.
	virtual void DamagePlayer(float Amount) = 0;
	// P4.2 — trace the +use look-cursor along a segment and return the brush entity it landed on
	// (Invalid for a miss, or a hit on anything that is not an entity body). The trace runs on the
	// dedicated ELYSIUM_USE_CHANNEL so world geometry occludes it; the pawn is ignored.
	virtual FElysiumEntityHandle TraceUseCursor(const FVector& Start, const FVector& End) const = 0;

	// 11.7 — the scripted-shot channel. `SetCamera(shotfile)`, `camera_keyframe`, the conversation
	// camera and the feed camera all push onto the player camera's one weight stack through here, and
	// `RemoveCamera` pops. `ShotFile` keys `vdata/camerashots/`; `Subject` is the entity the shot is
	// about, which is what its `DialogTarget` anchors resolve to. Returns 0 when the shot does not
	// parse, nothing it anchors to is there, or there is no camera (a headless world runs the
	// conversation without one). The channel is here, on the player's *body*, rather than on
	// IElysiumPresenter: the camera is part of the body (S3), and the presenter carries what is put
	// on *screen*, not what the player's body does.
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) = 0;
	// Value shots are the camera_track path: the entity world owns the sampler and publishes the
	// resulting world-space value without teaching the camera component about entities.
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) = 0;
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) = 0;
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) = 0;
};

// --------------------------------------------------------------------------------------------
// Audio — the voice pool and the map's SoundScheme control.
//
// Implemented by AElysiumMapActor, which owns the per-map FElysiumSoundSchemeManager and forwards
// the voice calls to the GI-scoped UElysiumAudioSubsystem. The substrate never holds either.
// --------------------------------------------------------------------------------------------
class IElysiumAudio
{
public:
	virtual ~IElysiumAudio() = default;

	// The map implementation stamps its active epoch before forwarding. Stable ownership remains
	// logical here; a weak attachment is only resolved by the subsystem on the game thread.
	virtual FElysiumVoiceHandle Submit(FElysiumAudioRequest Request) = 0;
	virtual void Prefetch(const FElysiumAudioSource& Source) = 0;
	virtual void PauseVoice(FElysiumVoiceHandle Handle, bool bPaused) = 0;
	virtual void SeekVoice(FElysiumVoiceHandle Handle, float MediaOffsetSeconds) = 0;
	virtual void SetVoicePitch(FElysiumVoiceHandle Handle, float Pitch) = 0;
	virtual void CancelAudioOwner(FElysiumAudioOwner Owner, float FadeSeconds = 0.f) = 0;

	// Compatibility translation for the existing entity leaves. New integrations submit a typed
	// request above; this method constructs exactly that request rather than owning a second path.
	virtual FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params) = 0;
	virtual void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds) = 0;
	virtual void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume) = 0;
	virtual bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const = 0;

	// 6.3 — crossfade a SoundScheme in as the active one (bed + music stems + random scheduler),
	// or fade it out if it is the one running. Anchor is the ambient_soundscheme entity's origin.
	virtual void FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds) = 0;
	virtual void FadeOutScheme(const FString& SchemeRel, float FadeSeconds) = 0;
	// The scheme currently running, or empty. What an ambient_soundscheme reports as its own state.
	virtual FString ActiveSchemeRel() const = 0;
};

// --------------------------------------------------------------------------------------------
// Travel — the map lifecycle.
//
// Implemented by AElysiumMapActor, forwarding to the GI-scoped UElysiumMapSubsystem. Both calls
// are requests, not transitions: the subsystem decides when the travel actually happens.
// --------------------------------------------------------------------------------------------
class IElysiumTravel
{
public:
	virtual ~IElysiumTravel() = default;

	// P4.6 trigger_changelevel — queue a landmark transition. Offset is the player's displacement
	// from THIS map's landmark, re-added to the destination's same-named one; Yaw is carried across.
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) = 0;
	// A plain map change with no landmark (the player is placed at the destination's own spawn).
	virtual void ChangeMap(const FString& Map) = 0;
};

// --------------------------------------------------------------------------------------------
// Presenter — what the substrate puts on screen.
//
// Implemented by UElysiumPresentationSubsystem (11.8), the world-scoped publisher of
// FElysiumViewState. These are **announcements of discrete moments**, not the state itself: the
// fade, the open panel and the open conversation stay on FElysiumEntityWorld, because each is world
// state with a lifetime (the map epoch owns it, and 11.9 saves it). The publisher samples that state
// once per frame and uses these calls to know *when* something happened, which is what its discrete
// delegates carry — a diff cannot tell a conversation that closed and reopened in one frame from one
// that never moved.
//
// Null where there is no publisher at all: a Substrate-tier world with no engine behind it, or an
// editor preview world. A test stub implements it to assert "the chain faded the screen and opened
// this panel" with no HUD to look at.
// --------------------------------------------------------------------------------------------
class IElysiumPresenter
{
public:
	virtual ~IElysiumPresenter() = default;

	// P4.5 env_fade — a full-screen colour fade. Parameters are the Fade input's, verbatim.
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) = 0;

	// P4.10 game_sign — the one sign panel on screen. CloseSign is the dismissal (left-click,
	// CloseWindow, or the owner dying).
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) = 0;
	virtual void CloseSign() = 0;

	// 9.1/B4 — the one conversation on screen.
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) = 0;
	virtual void CloseDialog() = 0;
};

// The bundle FElysiumEntityWorld is constructed with. By value — four raw pointers to objects that
// outlive the world (the map actor owns the world; the subsystems outlive the map). Default-
// constructed is the fully headless case: a world with no engine behind it at all.
struct FElysiumWorldServices
{
	IElysiumEmbodiment* Embodiment = nullptr;
	IElysiumAudio*      Audio      = nullptr;
	IElysiumTravel*     Travel     = nullptr;
	IElysiumPresenter*  Presenter  = nullptr;
};
