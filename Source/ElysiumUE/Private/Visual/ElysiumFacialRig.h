#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

// A rigged NPC's facial flex rig, read off `$ELYSIUM_EXPORT_ROOT/npc/facial/<stem>.json` (roadmap 12.3,
// pipeline PL10). Plain C++ with no UObject reflection, like `FElysiumNpcClipSet`: this holds names
// and arithmetic, and the UObject-side cache that hands one out is `UElysiumNpcAnimSubsystem`.
//
// The morph targets themselves are baked into the NPC's `.glb`. Three layers sit between a flex
// controller and one of those morph targets, and all three are replayed here rather than flattened
// offline, because the middle one is where the eyelid interaction lives:
//
//     44 controller values  ->  60 RPN flex rules  ->  65 flexdesc weights
//                           ->  the per-flex target ramp  ->  one morph target's weight
//
// Format, opcodes and the ramp: `docs/vtmb/facial_animation.md`.

// `StudioFlexOp_t`. Only these seven appear across the 201 rigged models in the install.
enum class EElysiumFlexOp : uint8
{
	None   = 0,
	Const  = 1,   // operand: a literal float
	Fetch1 = 2,   // operand: a flex-controller index
	Fetch2 = 3,   // operand: a flexdesc index — the weight an *earlier* rule already wrote
	Add    = 4,
	Sub    = 5,
	Mul    = 6,
	Div    = 7,
};

struct FElysiumFlexOpCode
{
	EElysiumFlexOp Op = EElysiumFlexOp::None;
	// Meaningful for Const (Value) and Fetch1/Fetch2 (Index); unused by the arithmetic ops.
	int32 Index = INDEX_NONE;
	float Value = 0.f;
};

// One rule: an RPN program whose final stack value is the weight of a single flexdesc.
struct FElysiumFlexRule
{
	int32 FlexDesc = INDEX_NONE;
	TArray<FElysiumFlexOpCode> Ops;
};

// One of the 44 named inputs. `Type` groups them into the five shipped families — `eyelid`, `brow`,
// `nose`, `mouth`, `phoneme` — and the phoneme family is exactly the set `expressions/phonemes.txt`
// writes, i.e. the surface 12.5's lipsync drives.
struct FElysiumFlexController
{
	FString Name;
	FString Type;
	float Min = 0.f;
	float Max = 1.f;

	// A controller write is stored normalized across the authored range, then clamped — Source's
	// `SetFlexWeight`. Every shipped VtMB controller is 0..1, so this is the identity on the whole
	// cast; a degenerate range is stored raw, as Source does.
	float Normalize(float Value) const
	{
		return Max != Min ? FMath::Clamp((Value - Min) / (Max - Min), 0.f, 1.f) : Value;
	}
};

// One glTF morph target: a flex *record*, not a flexdesc. A flexdesc can carry two flexes under
// different ramps — the eyelid hinge — and the ramp decides which half a given weight drives, so
// they are two morphs. The second and later ramp of one flexdesc carries a `#k` suffix, which is
// what keeps the names unique; glTFRuntime keys a `UMorphTarget` by name.
struct FElysiumFlexMorph
{
	FString Name;
	// `Name` as an FName: the anim curve the morph target of the same name is driven by.
	FName Curve;
	int32 FlexDesc = INDEX_NONE;
	// The trapezoid the flexdesc's weight is remapped through, as `R_StudioFlexVerts` evaluates it.
	float Targets[4] = { 0.f, 1.f, 10.f, 11.f };
};

// The amplitude-driven jaw: one per rigged character, pointing at the `mouth` flexdesc and the jaw
// bone. `mstudiomouth_t`, read off the sidecar's `mouths` rather than assumed: across the 86
// exported rigs the flexdesc is 16 on 85 (`female_raver_1`, whose whole rig is one flexdesc, is the
// exception), `forward` is (0,-1,0) on every one, and the bone is 6 on 75 but also 7, 12 and 14.
struct FElysiumFlexMouth
{
	int32 Bone = INDEX_NONE;
	FVector Forward = FVector::ZeroVector;
	int32 FlexDesc = INDEX_NONE;

	bool IsValid() const { return FlexDesc != INDEX_NONE; }
};

// What the amplitude jaw asks of a face on one evaluation: how open the mouth is, 0 at rest and 1 at
// the line's own peak.
struct FElysiumJawInput
{
	float Open = 0.f;
	// Whether the reconstruction below runs. Off leaves the faithful write alone — which, on the
	// shipped rigs, moves nothing.
	bool bBridge = true;

	bool IsOpen() const { return Open > 0.f; }
};

// A lid-value flexdesc — the eyelid hinge that no rule computes.
//
// Source drives it from `mstudioeyeball_t`: `upperlidflexdesc` takes the sum of the
// lowerer/neutral/raiser flexdesc weights against `uppertarget[3]`, the three authored lid angles in
// radians, and the flexdesc's two hinged ramps split that angle into a lowered half and a raised
// half. VtMB ships no eyeball records anywhere (`NumEyeballs == 0` on all 4,444 models), so the
// three angles are recovered from the ramps instead: the lowered angle is the low ramp's `Target2`,
// the hinge (the neutral angle) is where the two ramps meet, and the raised angle is the high ramp's
// `Target1`. The three source flexdescs are the rig's own `<lid>_lowerer`/`_neutral`/`_raiser`.
//
// Without this step the four eyelid rules compute weights nothing consumes, `blink` moves nothing,
// and the resting face sits at 0 — off the hinge, with a lid morph half-applied.
struct FElysiumFlexLid
{
	int32 FlexDesc = INDEX_NONE;
	int32 Lowerer = INDEX_NONE;
	int32 Neutral = INDEX_NONE;
	int32 Raiser = INDEX_NONE;
	float LoweredAngle = 0.f;
	float NeutralAngle = 0.f;
	float RaisedAngle = 0.f;
};

struct FElysiumFacialRig
{
	FString Stem;
	// The FACS names, index = flexdesc id.
	TArray<FString> FlexDescs;
	// Index = the `FETCH1` operand.
	TArray<FElysiumFlexController> Controllers;
	// Evaluated in file order: `FETCH2` reads a flexdesc an earlier rule wrote.
	TArray<FElysiumFlexRule> Rules;
	// In the glb's own morph-target order.
	TArray<FElysiumFlexMorph> Morphs;
	// Derived at load from the ramps and the flexdesc names, one per hinged lid.
	TArray<FElysiumFlexLid> Lids;
	FElysiumFlexMouth Mouth;

	// The controller the jaw bridge writes, derived at load. INDEX_NONE on a rig that has none.
	//
	// **Faithful behaviour: writing the mouth flexdesc moves nothing.** `mstudiomouth_t` names a
	// flexdesc, and on all 86 exported rigs that flexdesc carries **zero** flex records and is read
	// by **zero** `FETCH2` op — while a rule *computes* it, on 85 of them, as a weighted sum of the
	// jaw and lip action units (`AU27R x 0.5 + AU25R x 0.35 + AU12AU25 x 0.8 + …`). So `mouth` is a
	// read-out of how open the face already is, not a driver of it, and Source's own `ControlMouth`
	// write lands on a value with no consumer. Same shape as the eyelid bridge above: the rig was
	// authored for a structure the shipped files do not contain.
	//
	// **The divergence, gated by `elysium.FacialJawBridge`:** the same weight is also raised into the
	// `jaw_drop` flex controller, which the shipped rules turn into AU26/AU27 — the authored jaw-open
	// morphs. 85 of the 86 rigs carry that controller and 84 of the 84 that deform anything carry an
	// AU26/AU27 morph, so the reconstruction reaches the whole cast. Raised, not assigned: an
	// expression that already opens the jaw wider keeps its value, because the jaw layer opens a
	// mouth and never closes one.
	//
	// Evidence that would settle whether retail's jaw moves at all: capture the flexdesc weight
	// `mstudiomouth_t` names across a spoken line on the retail build and see whether anything
	// downstream of it ever leaves zero.
	int32 MouthBridge = INDEX_NONE;

	// Whether this rig can move a face. A sidecar can parse and still drive nothing: two exported
	// models carry flex data with no flex record that deforms a mesh, so they have no morph targets
	// to weight.
	bool IsValid() const { return !Morphs.IsEmpty() && !Controllers.IsEmpty(); }

	// Case-insensitive; INDEX_NONE when the name is not in this rig.
	int32 FindController(const FString& Name) const;
	int32 FindFlexDesc(const FString& Name) const;

	// Parse the sidecar `npc_index.json` names for a stem (`facial/<stem>.json`, relative to npc/).
	bool Load(const FString& RelPath, FString& OutError);
	bool LoadJsonText(const FString& JsonText, FString& OutError);

	// --- evaluation ------------------------------------------------------------------------
	// All three layers are pure functions of the controller values, so they are static/const and
	// testable without a mesh, a world or an anim instance.

	// The four-value ramp, exactly as `R_StudioFlexVerts` evaluates it: outside `[Target0, Target3]`
	// the flex contributes nothing, inside `[Target1, Target2]` it contributes fully, and the two
	// shoulders interpolate. The shoulder denominators cannot be zero — reaching one already proves
	// the weight lies strictly between the two targets being subtracted.
	static float RampWeight(const float Targets[4], float FlexWeight);

	// Run one rule's op stack. `FlexWeights` is the partially filled flexdesc array `FETCH2` reads.
	static float EvalRule(const FElysiumFlexRule& Rule, TArrayView<const float> ControllerValues,
		TArrayView<const float> FlexWeights);

	// Controller values (already normalized) -> the 65 flexdesc weights, including the lid combine.
	void EvalFlexWeights(TArrayView<const float> ControllerValues, TArray<float>& OutFlexWeights) const;
	// Flexdesc weights -> one weight per morph target, in `Morphs` order.
	void EvalMorphWeights(TArrayView<const float> FlexWeights, TArray<float>& OutMorphWeights) const;
	// Both halves. Every controller at rest leaves every morph weight at exactly zero.
	void Evaluate(TArrayView<const float> ControllerValues, TArray<float>& OutFlexWeights,
		TArray<float>& OutMorphWeights) const;

	// --- the amplitude jaw ---------------------------------------------------------------------
	//
	// The rig's second input, and the only one that is not a controller write. **Precedence is the
	// whole point of the ordering below**: 85 of the 86 exported rigs carry a rule that computes the
	// mouth flexdesc, so a jaw applied before the rules would be recomputed away on every single
	// evaluation. Source resolves it the same way — `C_BaseFlex::SetupWeights` calls `ControlMouth`
	// *after* `RunFlexRules` — so:
	//
	//   1. the bridge raises `MouthBridge`'s controller value (a rule input, so the rules see it);
	//   2. the rules run in file order, then the lid combine;
	//   3. the direct write lands on `Mouth.FlexDesc`, overwriting whatever step 2 left there — the
	//      jaw always wins, and it wins on every frame rather than only on the frame it changed;
	//   4. the target ramps turn flexdesc weights into morph weights.
	//
	// A rule that reads the mouth flexdesc through `FETCH2` therefore reads the *ruled* value, not
	// the jaw — again as Source does, and moot on this corpus, where no shipped rule reads it.
	void Evaluate(TArrayView<const float> ControllerValues, const FElysiumJawInput& Jaw,
		TArray<float>& OutFlexWeights, TArray<float>& OutMorphWeights) const;

	// Step 3 alone, exposed so the precedence is assertable without a mesh or an anim instance.
	void ApplyJawToFlexWeights(const FElysiumJawInput& Jaw, TArray<float>& InOutFlexWeights) const;
};
