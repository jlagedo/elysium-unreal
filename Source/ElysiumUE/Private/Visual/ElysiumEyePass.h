#pragma once

#include "CoreMinimal.h"
// By value: the debug seam carries the tuning knobs and a binding holds a shared eye set.
#include "Visual/ElysiumEyeRig.h"
// By value: the per-character iris textures are owned here for the epoch.

class UMaterialInstanceDynamic;
class USkeletalMeshComponent;

// The green room stands a bare visual: no `FElysiumNpc`, so no gaze cascade, so nothing ever
// supplies a view target and every eye sits on its authored resting aim. That is a correct state
// and a useless one to look at — the whole of what the rig does is only visible when the aim
// moves. This is the surface that moves it, and it sits exactly where `elysium.EyeTrackPlayer`
// already sits in the priority order rather than beside it.
struct FElysiumEyeDebug
{
	// Off leaves the shipping priority alone. Rest pins `bEyeMove` false, which is the retail
	// configuration the cascade-less state already produces and is worth being able to A/B
	// against. Camera aims at the player camera manager — in the lab, the orbit — which is the
	// cheapest unambiguous check that the basis math is right. Point aims at `Target`.
	enum class EGaze : uint8 { Off, Rest, Camera, Point };
	EGaze Gaze = EGaze::Off;
	// World space, read only under `EGaze::Point`.
	FVector Target = FVector::ZeroVector;

	// Hold the lids open regardless of the disposition's cadence, so a lid shape can be read
	// without waiting for a blink that lands when it likes.
	bool bHoldBlink = false;
	// Consumed by the next tick, which starts one envelope on every bound body. The manual half of
	// the same control: a blink is 300 ms and watching for a random one is not a way to inspect it.
	bool bBlinkNow = false;

	// The renderer-config knobs. Defaults reproduce the shipped config, so an untouched override
	// changes nothing about what is drawn.
	FElysiumEyeTuning Tuning;
};

// What one body's eye pass resolved, for a panel to draw. The failure this answers is silent
// otherwise: a body whose slots match no record binds nothing, ticks nothing, and draws the eye
// master's default iris — which is a texture, so it still looks like an eye.
struct FElysiumEyeReadout
{
	// Whether the model authors eye records at all, and how many.
	bool bHasSet = false;
	int32 RecordCount = 0;
	// Slots whose base material IS the eye master — the sections that will draw as eyes whether or
	// not a record was found for them.
	int32 EyeSlotCount = 0;
	// Records actually bound to a slot and ticking. Fewer than `EyeSlotCount` is the regression.
	int32 BoundCount = 0;
	// Every eye-master slot's name, and the record index it joined to (`INDEX_NONE` when none).
	TArray<TPair<FString, int32>> Slots;
	// This frame's blink weight, 0 open to 1 closed, and where the pass aimed.
	float Blink = 0.f;
	bool bAiming = false;
};

// The frame inputs the eye pass cannot reach on its own. It is not a UObject and it sits under
// `UElysiumEntityBodies`, so the clock and the player's position arrive from the map actor's
// post-move pass beside the gaze decision that already reads them.
struct FElysiumEyeFrame
{
	// The PAUSABLE game clock (`FElysiumEntityWorld::NowSeconds`), which is the clock the gaze
	// cascade's fidget is on. The blink cadence rides it too, so a held world freezes both halves of
	// the face together — retail runs the cadence on the server think, not on wall time.
	float NowSeconds = 0.f;
	// The player's eye point, for the blink cadence's distance gate
	// (`ElysiumEyes::BlinkPlayerDistance`). Absent in a world with no player, which gates every
	// body out — the same answer retail gives when `m_flPlayerDist` is never brought in range.
	FVector PlayerPosition = FVector::ZeroVector;
	bool bHavePlayer = false;
};

// The per-body application of VtMB's eye system: which slots draw as eyes, the per-frame
// basis rebuild and material publish, the blink cadence, and the gaze debug seam. Plain C++ owned
// by `UElysiumEntityBodies` (one instance per map epoch), which forwards its public eye methods
// here; the iris textures are strong-ref'd by the cache below, so lifetime does not depend on the
// owner being GC-visible. The recovered specification is `docs/vtmb/facial_animation.md` -> Eyes.
class FElysiumEyePass
{
public:
	// Find the eye sections on a freshly built body, build their per-component material instances,
	// and register them for TickEyes. A body whose model authors no eyeball binds nothing.
	void InstallEyes(USkeletalMeshComponent* Comp, const TSharedPtr<const FElysiumEyeSet>& Set,
		const FString& Disposition);

	// Rebuild every bound eye's basis against this frame's final pose and publish it to the
	// material. `Context` is the owning component: the world, the rulebook subsystem and the
	// player camera manager are all reached through it, because this pass is not a UObject.
	// `Frame` carries what the pass cannot: the game clock and the player's position.
	void TickEyes(const UObject* Context, const FElysiumEyeFrame& Frame);

	// Fill `Out` for `Comp`, or return false when this pass has no eye binding for it.
	bool DescribeEyes(const USkeletalMeshComponent* Comp, FElysiumEyeReadout& Out) const;

	// The one value crossing from the gaze decision to the eye pass, and the head frame the
	// decision measures itself in.
	bool SetViewTarget(USkeletalMeshComponent* Body, const FVector& WorldTarget);
	bool GetHeadFrame(USkeletalMeshComponent* Body, FVector& OutPosition, FVector& OutForward) const;

	// Re-latch a live body's disposition for the blink cadence and restart its schedule.
	void UpdateDisposition(USkeletalMeshComponent* Body, const FString& Disposition,
		int32 DispositionLevel);

	FElysiumEyeDebug& EyeDebug() { return EyeDebugState; }

private:
	// One eye section on one body: which slot draws it, which record it draws, the head bone it
	// rides, and the material instance whose parameters carry the basis.
	//
	// The MID is per COMPONENT, never per mesh. NpcMeshCache shares one USkeletalMesh across every
	// NPC of a stem, and glTFRuntime's own material instances live on that shared mesh's slots —
	// writing an iris plane there would make every NPC of the model look wherever the last one
	// looked, which reads as a feature rather than a bug.
	struct FElysiumEyeSlot
	{
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
		int32 EyeIndex = 0;
		int32 BoneIndex = INDEX_NONE;
		// Resolved once; the per-frame writes go by index and never look a name up again.
		int32 ParamIrisU = INDEX_NONE;
		int32 ParamIrisV = INDEX_NONE;
		int32 ParamIrisOrigin = INDEX_NONE;
		int32 ParamNormalOrigin = INDEX_NONE;
		int32 ParamEyeUp = INDEX_NONE;
	};
	struct FElysiumEyeBinding
	{
		TWeakObjectPtr<USkeletalMeshComponent> Comp;
		TSharedPtr<const FElysiumEyeSet> Set;
		TArray<FElysiumEyeSlot> Slots;
		// The body's disposition, resolved against the table for this character's blink cadence.
		// Latched at build: a disposition change rebuilds the body.
		FString Disposition;
		int32 DispositionLevel = 1;

		// Blink is two halves in retail: the server picks *when* (a gated countdown reseeded from
		// the disposition table) and the client runs the 300 ms envelope. `ElysiumEyes::AdvanceBlink`
		// owns both, including the player-distance gate this body's countdown is frozen by when
		// nobody is near enough to read its face.
		//
		// The envelope is asymmetric and that is authored: `w = 2*sqrt(cos(pi*u/2))` folded about
		// 1 closes the lid 48 ms after the toggle and reopens it over the remaining 252 ms.
		FElysiumBlinkSchedule Blink;

		// Where the substrate says this character is looking, world space, pushed once per frame
		// through IElysiumEmbodiment::SetViewTarget. Held rather than pulled because the two halves
		// tick in different passes: the gaze decision runs over the entity world, the eye pass runs
		// over the bodies, and this is the one value that crosses.
		//
		// Frame-stamped, because retail recomputes the aim every think: a body the gaze pass did not
		// maintain this frame — unrendered, inert, or a conversation that ended and left nothing
		// supplying a point — rests on its authored aim rather than holding the last world point it
		// was handed forever.
		FElysiumEyeGazeLatch Gaze;

		// The head bone, resolved once at build. The gaze cone and the fidget grid are measured in
		// the live animated head frame, so this is looked up by name and cached — never mixed with
		// the `.mdl`'s own bone ordering, which is not the USkeleton's.
		int32 HeadBoneIndex = INDEX_NONE;

		// Retail's `m_vecHeadLocalForward`: the model's facing carried into head-bone space at bind
		// (`CBaseCombatCharacter::SetModel` inverse-rotates the model forward by the head's bind
		// matrix), then rotated by the live bone each think in `CalcLookData`. Cached for the same
		// reason: a Bip01 bone's own X runs up the skull, so the bone axes alone say nothing about
		// where the face points.
		FVector HeadLocalForward = FVector::ZeroVector;
		// The other half of `CalcLookData`: `m_vecViewOffset` inverse-transformed by the head's
		// bind matrix, so the eye-height point on the model's own axis rides the head bone rather
		// than the bone's pivot (which is the neck) standing in for the eyes.
		FVector HeadLocalEye = FVector::ZeroVector;
		bool bHasHeadLocalEye = false;

		// Every slot on this body whose base material IS the eye master, with the record index it
		// joined to or `INDEX_NONE`. Kept for the readout rather than for the pass: a slot that draws
		// as an eye and matched no record is the one eye failure that looks like a working eye, so it
		// has to be reportable and not merely logged once at build.
		TArray<TPair<FString, int32>> SlotJoins;
		// This frame's envelope weight and whether the pass had an aim, latched for the same readout.
		float LastBlink = 0.f;
		bool bLastAiming = false;
	};
	// One entry per body carrying eye sections, INCLUDING a body none of whose sections joined a
	// record — that one drives nothing and exists to be reported.
	TArray<FElysiumEyeBinding> EyeBindings;
	// The frame stamp `SetViewTarget` writes and `TickEyes` tests, incremented at the tail of every
	// eye frame. The gaze decision runs first in the same pass, so a body it maintained carries the
	// current value and a body it skipped carries a stale one.
	uint64 GazeFrame = 1;
	// The previous eye frame's game-clock reading, so the cadence advances by a GAME-clock delta.
	// Held here rather than passed in because the pass is the only thing that knows when it last ran
	// (the map actor's own delta is the frame's real time, which is not the clock the cadence is on).
	float LastNowSeconds = 0.f;
	bool bHasLastNow = false;
	FElysiumEyeDebug EyeDebugState;
	// The per-character iris is the `.vmt`'s `$iris`, decoded beside the glb rather than carried
	// inside it, so it loads through the same per-map dedup index the world uses. Strong-ref'd for
	// the epoch, released with the owning component.
};
