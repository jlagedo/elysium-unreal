#pragma once

#include "CoreMinimal.h"
#include "ElysiumMoveSolve.h"   // ElysiumMove::U — the blink gate's radius is authored in Source units
#include "ElysiumEyeRig.generated.h"

class UElysiumEyeTuningConfig;
class UTexture2D;

// VtMB's eye system, as reflected value data plus pure arithmetic. The cache that hands one out is
// `UElysiumAnimSubsystem`, and the per-body application is `UElysiumEntityBodies`.
//
// An eye is not a UV-mapped feature of the mesh. The original renderer rebuilds a basis for each
// eyeball every frame from the model's `StudioEyeball` record and the character's gaze point, turns
// that basis into two planes, and the shader dots those planes against the position to find the
// iris. The same pass writes the eyelid flexdescs back into the flex weights *after* the flex rules
// have run — so this record, not a reconstruction, is the authored bridge between the four eyelid
// rules and the lid morphs.
//
// Deliberately independent of the flex rig: 57 of the 59 player bodies carry a pair of eyeball
// records and no flex data at all, so their irises aim while their lids have no flexdesc to land
// on. A rig-gated load would give the player no eyes.
//
// The recovered specification — the record layout, the basis math, the shader composite and the
// gaze behaviour that supplies the target — is `docs/vtmb/facial_animation.md` -> Eyes.

// One `StudioEyeball`, as `npc/eyes/<stem>.json` states it.
//
// Two coordinate regimes meet here and mixing them is the classic way to get a plausible-but-wrong
// eye. `Org`/`Up`/`Forward` are geometry: Unreal-native in the sidecar, written by the same
// conversion the body's own bones are, so they line up with the loaded skeleton verbatim.
// `Radius`, `ZOffset` and the lid targets are **eyeball units** (the model's own inches) and stay
// that way: the lid math is `asin(target / radius)`, a ratio, so converting one without the other
// silently changes the lid shape. `IrisScale` is neither — it becomes an inverse length, and
// `ElysiumEyes::BuildState` is the only place that may convert it.
USTRUCT()
struct ELYSIUMUE_API FElysiumEyeball
{
	GENERATED_BODY()

	// The record's own index, and what `StudioMesh.materialparam` selects: 0 or 1.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	int32 Index = 0;

	// The eye's bone, by NAME. A model with more than one parent-less bone gets a synthetic root
	// appended at export assembly, so the runtime skeleton's bone order is not the `.mdl`'s and an
	// index would aim off the wrong bone.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FName Bone;
	// The `.mdl`'s own index. Diagnostics only, for the same reason the composition rig keeps one.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	int32 BoneIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 BodyPart = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 BodyModel = 0;

	// Bone-local, in centimetres.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FVector Org = FVector::ZeroVector;
	// Bone-local unit vectors: the authored resting basis.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FVector Up = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FVector Forward = FVector::ZeroVector;

	// Eyeball units, left as authored.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	float ZOffset = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	float Radius = 0.5f;
	// Enters the plane scale as `1 / (1 / IrisScale + EyeSize)`, so it is an inverse length by the
	// time it reaches the material and converts *by division*.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	float IrisScale = 1.f;

	// The three lid-state flexdescs the eyelid rules compute, and the morph-carrying flexdesc the
	// eye pass writes from them. `INDEX_NONE`/zeroed on a model with no flex rig — which is every
	// player body, and is a normal load.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	int32 UpperFlexDesc[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	int32 LowerFlexDesc[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
	// **Linear offsets in eyeball units, not angles.** The renderer takes `asin(t / Radius)`;
	// reading them as radians still produces a lid that moves, which is what makes it easy to keep.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	float UpperTarget[3] = { 0.f, 0.f, 0.f };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	float LowerTarget[3] = { 0.f, 0.f, 0.f };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	int32 UpperLidFlexDesc = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	int32 LowerLidFlexDesc = INDEX_NONE;

	// The glTF material name this eye draws under — the key the material override and the slot
	// lookup both join on.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FString Material;
	// The per-character iris, relative to the glb ("tex/<file>.png"). Empty leaves the master's
	// default, which paints the whole eye and is meant to be visible.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FString IrisTexture;
	/** Cook dependency for the iris already bound on the imported material. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TSoftObjectPtr<UTexture2D> IrisAsset;
	// The `.vmt`'s `$vampire`: the iris is composited without scene lighting. 12 shipped materials.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	bool bVampire = false;

	// Whether this record can drive a lid. False on every player body.
	bool HasLids() const { return UpperLidFlexDesc != INDEX_NONE || LowerLidFlexDesc != INDEX_NONE; }
};

// One model's pair, off `npc/eyes/<stem>.json`.
USTRUCT()
struct ELYSIUMUE_API FElysiumEyeSet
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	FString Stem;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source")
	TArray<FElysiumEyeball> Eyeballs;

	// Every shipped character carries two. A model with none simply has no eyes to draw.
	bool IsValid() const { return !Eyeballs.IsEmpty(); }

	// Read `npc/<RelPath>` — `npc_index.json`'s own `eyes` value. The sidecar is Unreal-native, so
	// the geometry lines up with the loaded skeleton with nothing applied to it. This is the door
	// the runtime uses.
	// Parse the same JSON from a string rather than from the export root, so a test can state a
	// record inline. Same result; `Load` is the door.
	bool LoadJsonText(const FString& JsonText, FString& OutError);

	const FElysiumEyeball* Find(int32 Index) const;
	// The eye a material name draws, or null. The join the slot lookup uses.
	const FElysiumEyeball* FindByMaterial(const FString& MaterialName) const;
};

// The renderer-config knobs the original's eye pass reads. Defaults reproduce the shipped config.
struct FElysiumEyeTuning
{
	// Per-component nudge applied to the eye centre, by the sign of each component. Centimetres.
	// It shifts the iris planes but deliberately NOT the shading origin, which is why the state
	// below carries two origins.
	FVector EyeShift = FVector::ZeroVector;
	// Widens or narrows the iris: `1 / (1 / IrisScale + EyeSize)`.
	float EyeSize = 0.f;
	// False parks the eye on the record's own authored resting aim rather than the gaze point —
	// the original's `bEyeMove`, and the state a body with no gaze source sits in.
	bool bEyeMove = true;
};

// One eyeball's live basis, in the space `BuildState` was given (component space, for the material
// parameters that consume it). The analogue of the original's `eyeballstate_t`.
struct FElysiumEyeState
{
	// The eye centre the iris planes are measured from — includes the eye shift.
	FVector Org = FVector::ZeroVector;
	// The eye centre the shading normal is measured from — excludes it. Equal to `Org` whenever
	// the shift is zero, which is the shipped configuration.
	FVector NormalOrg = FVector::ZeroVector;

	FVector Forward = FVector::ZeroVector;
	FVector Right = FVector::ZeroVector;
	FVector Up = FVector::ZeroVector;
	// The record's own `Up`, rotated but not re-derived: the shading flatten axis, which is a
	// property of the head rather than of where the eye is looking.
	FVector AuthoredUp = FVector::ZeroVector;

	// `u = dot(P - Org, IrisU) + 0.5`, and likewise for v. Inverse centimetres.
	FVector IrisU = FVector::ZeroVector;
	FVector IrisV = FVector::ZeroVector;

	// The basis rotated back into the eye bone's own space, which is the only part of the pass the
	// flex layer sees (the eyelid write-back).
	FVector UpLocal = FVector::ZeroVector;
	FVector ForwardLocal = FVector::ZeroVector;

	bool bValid = false;
};

namespace ElysiumEyes
{
	// The material name a mesh's material slot draws under, which is the key `FindByMaterial` joins
	// on. Two spellings reach it and both are answered here so neither end can drift from the other:
	// the baked path names a slot for the container's own material verbatim (`Eyeball_r`), and a
	// glTFRuntime load spells the same name `LOD_<lod>_Section_<index>_<name>`. The prefix is trimmed
	// when present; anything else is already the material name.
	//
	// The join is exact and case-insensitive rather than a suffix test, because the sidecar lowercases
	// what the container capitalises and the two agree on nothing but the letters.
	FString MaterialNameFromSlot(const FName SlotName);

	// The original's `R_StudioEyeballPosition`, verbatim, in whatever space `BoneToSpace` and
	// `Target` are expressed in — the runtime passes component space, because that is what the
	// material's parameters are in and it keeps the arithmetic away from large world coordinates.
	//
	// Pure: no component, no world, no skeleton. The whole of it is assertable under `-nullrhi`.
	void BuildState(const FElysiumEyeball& Eye, const FTransform& BoneToSpace, const FVector& Target,
		const FElysiumEyeTuning& Tuning, FElysiumEyeState& Out);

	// The Green Room's live debug nudge (`DebugTuning`) composed additively with the corpus-wide
	// baseline (`Config`, `UElysiumEyeTuningConfig` / `/ElysiumAuthored/Eyes/DA_EyeTuning`, R4.5).
	// `Config` may be null (asset not on the mount), which composes as neutral — an untouched debug
	// state and an absent asset both leave `DebugTuning` unchanged. `bEyeMove` is the debug state's
	// alone; the asset carries no gaze-mode switch.
	//
	// Pure: no component, no world, no asset load (the caller already resolved `Config`). Assertable
	// under `-nullrhi` with a synthetic `UElysiumEyeTuningConfig` or none at all.
	FElysiumEyeTuning ComposeTuning(const FElysiumEyeTuning& DebugTuning, const UElysiumEyeTuningConfig* Config);

	// Centimetres per eyeball unit. The records are authored in the model's own inches and the
	// geometry is imported to centimetres, so anything that crosses the two — only the iris plane
	// scale does — passes through this.
	static constexpr float UnitsToCm = 2.54f;

	// The blink window, and the rate that carries it to a quarter-turn.
	static constexpr float BlinkSeconds = 0.3f;
	static constexpr float BlinkRate = 5.235987755982989f;   // (pi/2) / 0.3

	// The blink envelope, verbatim: `w = 2·sqrt(cos(a))` folded about 1, over `a` running from a
	// quarter-turn down to zero across the window. Not a symmetric curve — the lid slams shut in
	// 48.3 ms and opens over the remaining quarter second, which is the shape of a real blink and is
	// why the value is assigned rather than remapped through the controller's min/max.
	//
	// `SecondsRemaining` is time left in the window; at or below zero the eye is open.
	float BlinkWeight(float SecondsRemaining);

	// The blink cadence's player-distance gate.
	//
	// `CAI_BaseNPCTroika::MaintainEyeDirection` (`vampire.dll` 0x102BFF20) wraps the WHOLE cadence
	// in one test:
	//
	//     if (m_flPlayerDist < _DAT_10483AAC && (m_blinkTimer -= dt) < _DAT_104454C4) {
	//         vfunc 0x450;                                   // CBaseFlex::Blink, 0x100B5CE0
	//         m_blinkTimer = RandomFloat(m_flMinBlink, m_flMaxBlink);   // 2.5-6.0 s, per disposition
	//     }
	//
	// So an NPC far from the player does not blink AND does not run down its timer: the countdown is
	// frozen, not merely unread, which is why walking up to a distant NPC does not trigger a burst of
	// backed-up blinks. Only the 0.3 s envelope is client-side; the cadence is on the server think,
	// i.e. the game clock.
	//
	// TODO(gaze): retail `_DAT_10483AAC` unread. It is an unnamed `.rdata` float with eighteen
	// readers and no writer — `CAI_BaseNPCTroika::SetPlayerLOS` (0x10291610),
	// `CAI_BaseNPCTroika::SelectSchedule` (0x102AF660), `CNPC_Crow::vfunc481` (0x10357680),
	// `CNPC_VPedestrian::vfunc461` (0x103A2E30) and this one all compare `m_flPlayerDist` against
	// it — so it is the engine's single "the player is close enough to matter" radius, but the
	// corpus has not decoded its value. 1024 Source units is a PLACEHOLDER, chosen so that every
	// range a face is actually read at is inside it: a dialogue camera sits ~2 m from the speaker
	// and melee is fought well within 26 m.
	static constexpr float BlinkPlayerDistance = 1024.f * ElysiumMove::U;
}

// What the caller is asking the cadence for this frame. `Cadence` is the shipping path; the other
// two are the green room's overrides, which are inputs to the same schedule rather than a second one.
enum class EElysiumBlinkCommand : uint8
{
	Cadence,   // run retail's gated countdown
	Force,     // start one envelope now, leaving the countdown alone
	Hold,      // lids pinned open, and the countdown reseeded so releasing does not fire a backlog
};

// One body's blink state. Retail's `m_blinkTimer` is a COUNTDOWN (`0x102BFF20` decrements it by the
// think delta), not a deadline, and that is load-bearing: the gate freezes it rather than letting a
// wall-clock deadline slide past while nobody is watching.
struct FElysiumBlinkSchedule
{
	// Seconds until the next blink. Starts at zero, which is retail's own initial state: the first
	// think with the player in range blinks and reseeds.
	float Timer = 0.f;
	// Game-clock time the 0.3 s envelope closes at. Absolute, because the envelope is the client
	// half and is read as `BlinkWeight(BlinkEndsAt - Now)`.
	float BlinkEndsAt = 0.f;
};

// Retail recomputes the eye aim every think, so a body whose maintainer stopped running falls back
// to the eyeball record's authored resting aim on the very next frame — it does not keep staring at
// the last world point it was handed. The port's gaze decision and its eye pass are two passes, so
// the latch carries a frame stamp: the decision stamps it, the pass rests anything the frame did not.
struct FElysiumEyeGazeLatch
{
	FVector Target = FVector::ZeroVector;
	bool bHasTarget = false;
	uint64 Frame = 0;

	void Set(const FVector& WorldTarget, uint64 InFrame)
	{
		Target = WorldTarget;
		bHasTarget = true;
		Frame = InFrame;
	}

	// Called once per eye frame, before the aim is read. A latch not stamped this frame rests.
	bool Maintain(uint64 InFrame)
	{
		if (Frame != InFrame)
		{
			bHasTarget = false;
		}
		return bHasTarget;
	}
};

namespace ElysiumEyes
{
	// One body's blink cadence for one frame, and the envelope weight it produces (0 open, 1 closed).
	//
	// `ClockDelta` is measured on the PAUSABLE game clock (`FElysiumEntityWorld::NowSeconds`), the
	// same clock `Now` is on, so the cadence and the gaze fidget freeze together — a paused world
	// hands a delta of zero and the schedule stands still. `PlayerDistanceCm` is body-to-player;
	// `MinInterval`/`MaxInterval` come from the disposition table (`vdata/system/dispositiontable.txt`,
	// mostly 2.5/6.0 s).
	//
	// Pure but for the reseed draw: no component, no world, no clock of its own.
	float AdvanceBlink(FElysiumBlinkSchedule& Schedule, float Now, float ClockDelta,
		float PlayerDistanceCm, float MinInterval, float MaxInterval,
		EElysiumBlinkCommand Command);
}
