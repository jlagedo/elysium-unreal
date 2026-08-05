#pragma once

#include "CoreMinimal.h"

// The simulated-garment spike's solver setup, as data. Plain C++ with no UObject reflection, like
// `FElysiumFacialRig` and `FElysiumCompositionRig`; the cache that hands one out is
// `UElysiumNpcAnimSubsystem`.
//
// Unlike the two composition stages, this reproduces nothing. VtMB has no cloth solver at all: a
// skirt or coat is skinned rigidly to one bone and swings as a single shell, and the one authored
// per-bone stage the format does carry names hair, ponytail, mane and breast bones only, with no
// garment bone in it or in any `.phy` (`docs/vtmb/secondary_motion.md`). So there is no faithful
// garment motion to diverge from — the artist authored the garment's shape, and its stillness is a
// 2003 engine limit rather than a decision.
//
// The mesh this rig drives is a *separate* artifact. `enhancement/cloth_spike.py` writes
// `npc/cloth/<stem>.glb` — the same mesh with a bone lattice appended and its garment shell
// re-weighted onto the lattice — beside the untouched faithful `npc/<stem>.glb`. Nothing in
// `npc_index.json` names either file; `elysium.Cloth` plus the files existing is the whole
// selection rule, the same shape as `elysium.EnhancedTextures` over `tex_hi/`.
//
// Row 0 of the lattice is an anchor row that is never simulated. A rigid parent-child pair with no
// relative motion skins identically to the parent alone, so the waistband reproduces the original
// silhouette by construction rather than by tuning — and a rig that fails to load leaves the
// garment exactly as it shipped.

// One simulated body: a lattice bone plus the constraint that holds it. The cone widens down the
// chain, which is what separates a garment that flows from one that swings as a board.
struct FElysiumClothBody
{
	FName Bone;
	int32 Row = 0;
	// Degrees. `FAnimPhysConstraintSetup::ConeAngle` clamps to 0..90, so the exporter's ramp
	// already tops out there rather than relying on the engine to clamp silently.
	float ConeAngleDeg = 0.f;
	// Centimetres after import. Isotropic: box extents are stated in the body's own frame, where
	// an axis mistake is invisible in the data and baffling in motion.
	float BoxExtent = 0.f;
};

// One vertical panel of the garment: a chain of bodies hanging off the anchor row. `Root` and
// `End` are what `FAnimNode_AnimDynamics` calls `BoundBone` and `ChainEnd`.
struct FElysiumClothChain
{
	int32 Column = 0;
	FName Root;
	FName End;
	TArray<FElysiumClothBody> Bodies;
};

// A sphere the garment cannot pass through, driven by a bone that moves inside it.
// `FAnimPhysSphericalLimit` is the only collision AnimDynamics offers, so without these the legs
// walk straight through the skirt.
struct FElysiumClothCollider
{
	FName Bone;
	FVector Offset = FVector::ZeroVector;   // centimetres after import
	float Radius = 0.f;                     // centimetres after import
};

// Whole-chain solver settings, shared by every body in every chain of one garment.
struct FElysiumClothSolver
{
	float LinearDamping = 0.78f;
	float AngularDamping = 0.82f;
	float GravityScale = 1.f;
	// A chain is stiffer to solve than a single body; the engine's 4/1 default leaves a long panel
	// visibly rubbery at the hem.
	int32 IterationsPre = 8;
	int32 IterationsPost = 2;
	// `FAnimNode_AnimDynamics` zero-initialises both of these, which makes a body react only to its
	// bound bone *rotating* and not at all to the character translating. Left at the engine default
	// a skirt on a walking character barely moves; these two are the difference.
	float ComponentLinearVelScale = 0.6f;
	float ComponentLinearAccScale = 0.25f;
};

struct FElysiumClothRig
{
	FString Stem;

	// The bone the lattice hangs from — `Bip01 Pelvis` on every garment in the corpus.
	FName Root;
	int32 Rows = 0;
	int32 Columns = 0;

	TArray<FElysiumClothChain> Chains;
	// The rigid row every chain hangs from, one bone per column. Nothing simulates these — they are
	// carried so a debug view can tell a chain that is hanging from its anchor apart from one that
	// has collapsed onto it, which look identical when only the simulated rows are drawn.
	TArray<FName> AnchorRow;
	TArray<FElysiumClothCollider> Colliders;
	FElysiumClothSolver Solver;

	// Nothing to install. A rig that parses but names no chain is a normal load, not a failure.
	bool HasWork() const { return !Chains.IsEmpty(); }

	// The door the runtime uses: read `npc/cloth/<Stem>.json` and convert it into Unreal space.
	bool Load(const FString& InStem, FString& OutError, bool bBaked = false);
	// The same parse without the import conversion, so a test can see exactly what the exporter
	// wrote. The sidecar ships in the glb's own basis and in metres, because the mesh beside it
	// does; skipping `ApplyAssetImport` is a factor of 100 on every length, silently.
	bool LoadJsonText(const FString& JsonText, FString& OutError);
	void ApplyAssetImport(bool bBaked);
};

// A live edit sitting on top of an authored rig, never replacing it. The rig is what the exporter
// derived from the model's own measurements — the shape of the cone ramp down a panel, the radius
// of the thigh a hem has to clear — and tuning is the magnitude those were guessed at, which is the
// half that only a moving body can answer. So the three geometry knobs are *scales* over the
// authored values rather than absolutes: they keep the ramp and re-size it.
//
// Which of these the engine re-reads per frame is not a detail — it is the difference between a
// slider that answers while you drag it and one that does not. `FAnimNode_AnimDynamics::UpdateLimits`
// rebuilds the cone constraint and every spherical limit every frame, and the gravity, iteration
// counts and component scales are read straight off the node inside the evaluate. Damping and box
// extents are the exception: both are baked into `FAnimPhysRigidBody` at `InitPhysics`, so changing
// either re-seats the chain (`RequestInitialise`) and the garment drops from the pose again.
struct FElysiumClothTuning
{
	FElysiumClothSolver Solver;

	// Multiplies every body's authored `ConeAngleDeg`. The engine clamps a cone to 0..90 and so does
	// the exporter, so the product is clamped rather than allowed to saturate silently.
	float ConeScale = 1.f;
	// Multiplies every collider's authored radius — how much clearance the legs are given.
	float ColliderRadiusScale = 1.f;
	// Multiplies every body's authored `BoxExtent`, which is both its inertia and — through
	// `SphereCollisionRadius` — the footprint it meets a leg sphere with. Moving it therefore
	// changes how readily the garment is pushed as well as how heavily it swings.
	float BoxExtentScale = 1.f;

	// What the sidecar itself asks for: the authored solver at every scale 1. This is the baseline a
	// Revert returns to and the one "modified" is measured against — NOT the struct's own defaults,
	// which are the fallback for a sidecar that omits a field and say nothing about what this file
	// wrote.
	static FElysiumClothTuning FromRig(const FElysiumClothRig& Rig);
	bool EqualsTuning(const FElysiumClothTuning& Other) const;
	// Whether moving from Other to this needs the chain re-seated rather than just re-read.
	bool NeedsReseat(const FElysiumClothTuning& Other) const;
};

namespace ElysiumClothRig
{
	// Bake a tuning into `npc/cloth/<Stem>.json` so it survives the session.
	//
	// This patches the document the exporter wrote rather than re-serialising the parsed rig: the
	// sidecar carries fields the runtime never reads, and a save that quietly dropped them would
	// make the file a worse record than the exporter left it. Every value written is the authored
	// number times a dimensionless scale, so nothing here converts units — the file stays in metres
	// and the runtime's centimetres never touch it.
	bool SaveTuning(const FString& Stem, const FElysiumClothTuning& Tuning, FString& OutError);
}
