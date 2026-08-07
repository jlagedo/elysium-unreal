#pragma once

#include "CoreMinimal.h"

// The two composition stages VtMB runs between blended locals and the drawn skeleton, as data
// (roadmap CAP7.1/CAP7.2). Plain C++ with no UObject reflection, like `FElysiumFacialRig`: this
// holds a table and the arithmetic over it, and the cache that hands one out is
// `UElysiumNpcAnimSubsystem`.
//
// Neither stage is in the clips. Unreal owns decode, blending, skinning and LOD; these are the only
// two things it has no equivalent for, and both run after the graph has blended locals and before
// skinning — which is retail's own order. The rules are `docs/vtmb/animation_and_movers.md` A.4a
// and `docs/vtmb/procedural_bones.md`; the Unreal design is
// `docs/architecture/animation-architecture.md`.
//
//   1. Split inheritance (`Flags & 0x2`, normally one bone — `Bip01 Spine1`): rotation from the
//      component root rather than the parent, translation from the parent.
//   2. Axis interpolation (`ProcType == 1`, 12-21 bones): the driven bone's local transform is
//      *replaced* by a six-entry three-way blend of the control bone's local rotation. Its own
//      animation channels are decoded, carried through, and discarded.
//
// Only stage 2 is inherently un-bakeable: a driven bone reads its control bone's *live*
// orientation, so it has no value until a finished pose exists to read one from.
//
// Stage 1 is bakeable per clip, and is baked — `world_rot(parent)^-1 * local` is fixed once the
// clip and frame are, so `UE_mdl_skeletal.py` rewrites that one rotation curve at export
// (container v2) and a clip off the baked mount is played with this stage declined. What is NOT
// bakeable is the correction across a BLEND: the rule is non-linear, so normalising two clips
// against their own parent chains and blending is not the same as blending first and correcting
// once. That residual is bounded and measured — see `EvaluateComposition` in
// `ElysiumNpcAnimInstance.cpp`.

// One `mstudioaxisinterpbone_t`, as `npc/procedural/<stem>.json` states it.
//
// `Axis` is a **direction**, not an index. The rule's six entries and the Source axis its three
// terms are indexed by are Source quantities, and a change of basis conjugates a bone local — so
// the axis a rule names is not the same axis after conversion, and may be negated. The exporter
// therefore carries the axis as a converted direction and the three term weights as the images of
// the Source axes (`FElysiumCompositionRig::DriverAxes`), which states the rule in the artifact's
// own space and lets this evaluate it knowing nothing about Source's basis.
struct FElysiumAxisInterpRule
{
	FName Bone;                        // the driven bone
	FName Control;                     // the bone whose orientation drives it
	// The `.mdl`'s own bone indices. Diagnostics only — the runtime resolves by name, because a
	// bank-retargeted skeleton and a compact pose both renumber.
	int32 BoneIndex = INDEX_NONE;
	int32 ControlIndex = INDEX_NONE;

	FVector Axis = FVector::ZeroVector;
	// Entry order is the record's own: term k's positive entry is 2k and its negative entry 2k+1.
	FVector Pos[6] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector,
		FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
	FQuat Quat[6] = { FQuat::Identity, FQuat::Identity, FQuat::Identity,
		FQuat::Identity, FQuat::Identity, FQuat::Identity };
};

struct FElysiumCompositionRig
{
	FString Stem;

	// StudioBone names carrying `Flags & 0x2`, off `npc_index.json`'s `split_bones`. One per
	// ordinary biped; empty on animals, most props, and any model the export predates.
	TArray<FName> SplitBones;

	// Every `ProcType == 1` rule, off `npc/procedural/<stem>.json`. 130 of 185 exported models
	// carry a table; the rest evaluate the split stage alone, or nothing.
	TArray<FElysiumAxisInterpRule> AxisRules;

	// The images of Source's three axes, which is what a rule's three terms are indexed by. The
	// sidecar carries them beside the rules; these defaults are the identity basis a hand-built
	// fixture wants.
	FVector DriverAxes[3] = { FVector(1.f, 0.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(0.f, 0.f, 1.f) };

	// Whether either stage has anything to do. A body whose rig answers false runs the ordinary
	// Unreal composition and is a completely normal load — animals, crowd bodies and most props.
	bool HasWork() const { return !SplitBones.IsEmpty() || !AxisRules.IsEmpty(); }

	const FElysiumAxisInterpRule* FindRule(const FName Bone) const;

	// Read `npc/procedural/<RelPath>` — `npc_index.json`'s own `procedural` value — and carry it
	// onto the skeleton through the glb's import transform (see `ApplyAssetImport`). This is the
	// door the runtime uses.
	bool LoadAxisRules(const FString& RelPath, FString& OutError);
	// Parse the same JSON **verbatim**, in the glb's own space and metres. Split out so a test can
	// see what the file says before and after the import; a caller wanting rules that line up with
	// a loaded skeleton wants `LoadAxisRules`.
	bool LoadAxisRulesJson(const FString& JsonText, FString& OutError);

	// Carry a verbatim table onto the skeleton glTFRuntime built from the same .glb.
	//
	// The sidecar states its entries in the glb's own basis and metres — deliberately, because that
	// is the one space in which the table and the mesh and the clips are consistent by
	// construction rather than by agreement. glTFRuntime then imports that file under a declared
	// basis and scale, conjugating every node transform and scaling every translation, so the
	// skeleton in memory is *not* in the space the sidecar is written in. Passing the table through
	// the loader's own import transform is what puts the two back in one space; it is the glb
	// exemption's other half, not a coordinate conversion this runtime authors — the constants come
	// from the same `FglTFRuntimeConfig` the mesh was loaded with, and metres to centimetres alone
	// is a factor of 100 on every driven bone's translation.
	void ApplyAssetImport();

	// The rule body, verbatim from `docs/vtmb/procedural_bones.md`, as a pure function of one local
	// rotation — no pose, no component space, no skeleton:
	//
	//     w  = ControlLocalRotation * Axis
	//     ak = dot(DriverAxes[k], w);  ak >= 0 selects entry 2k, ak < 0 selects entry 2k+1
	//     a1, a2, a3 = |a0|, |a1|, |a2|
	//     if a1 + a2 > 0:  t = 1/(a1+a2+a3)
	//                      q = slerp(slerp(quat[i2], quat[i1], a1/(a1+a2)), quat[i3], a3*t)
	//                      p = a1*t*pos[i1] + a2*t*pos[i2] + a3*t*pos[i3]
	//     else:            q, p = entry i3
	//
	// The result **replaces** the driven bone's local transform; it does not adjust it.
	FTransform EvaluateRule(const FElysiumAxisInterpRule& Rule, const FQuat& ControlLocalRotation) const;
};
