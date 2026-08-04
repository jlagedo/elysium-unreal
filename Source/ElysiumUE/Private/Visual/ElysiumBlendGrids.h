#pragma once

#include "CoreMinimal.h"

// A model's blend spaces, off `npc/blends/<stem>.json` (CAP7.3). Plain C++ with no UObject
// reflection, like `FElysiumFacialRig` and `FElysiumCompositionRig`; the cache that hands one out is
// `UElysiumNpcAnimSubsystem`.
//
// A VtMB sequence does not always name one animation. 279 of the exported sequences name a **grid**
// of them — a 9x1 fan of `walk_0`..`walk_315` selected by `move_yaw`, a 3x3 weapon-aim layer on
// `aim_yaw`/`aim_pitch` — and the engine picks a cell from the pose parameters the axes bind to
// (`docs/vtmb/animation_and_movers.md` A.3). The exporter bakes every cell as its own clip and
// writes the axes beside them; the label itself is baked from the grid's base cell `[0][0]`.
//
// That last detail is why this exists rather than being an enhancement. A `move_yaw` axis runs
// -180..180, so the base cell is the **-180 degree** cell: the animation named `walk` is the
// backward walk, and `run` is the backward run. Resolving the grid at `move_yaw` 0 selects cell 4
// and the character walks forward — not as a special case, but because 0 degrees means straight
// ahead. Nothing here diverges from VtMB; it stops a nine-cell fan being silently collapsed onto one
// arbitrary corner of itself.

// One pose parameter as a model declares it. `Loop` is a wrap modulus and is **zero on the aim
// parameters**, which therefore do not wrap — the branch is not decoration.
struct FElysiumPoseParamDesc
{
	FString Name;
	int32 Flags = 0;
	float Start = 0.f;
	float End = 0.f;
	float Loop = 0.f;
};

// The values a body is posed at, in the parameters' own units (degrees). Absent means zero, which is
// the resting value for every parameter VtMB declares: `move_yaw` 0 is straight ahead and `aim_yaw`
// 0 is level, so a body nothing has driven resolves to the middle of every fan it plays.
struct FElysiumPoseParams
{
	TMap<FString, float> Values;

	// The shared all-zero set. Every caller passes this until something drives a parameter.
	static const FElysiumPoseParams& Neutral();

	float Get(const FString& Name) const
	{
		const float* Found = Values.Find(Name);
		return Found != nullptr ? *Found : 0.f;
	}
	void Set(const FString& Name, float Value) { Values.Add(Name, Value); }
};

// Scalar movement authored beside an in-place animation. The offline decoder converts Source
// inches to centimetres; the skeleton remains in place and the route motor consumes GroundSpeed.
struct FElysiumClipMotion
{
	float CycleSeconds = 0.f;
	float GroundDistanceCm = 0.f;
	float GroundSpeedCmPerSecond = 0.f;

	bool IsUsable() const
	{
		return FMath::IsFinite(GroundSpeedCmPerSecond) && GroundSpeedCmPerSecond > 0.f;
	}
};

// One cell of a grid. `Clip` is empty when the cell's animation did not bake, which the exporter
// records as a null — no shipped grid carries one today, but the field is nullable by construction
// so every reader tolerates it rather than assuming. Motion is optional for backwards-compatible
// exports and for non-locomotion cells.
struct FElysiumBlendCell
{
	int32 Axis[2] = { 0, 0 };
	// The owner-local MDL animation index. Provenance only — the runtime addresses the clip by name.
	int32 Anim = INDEX_NONE;
	FString Clip;
	FElysiumClipMotion Motion;
};

// One sequence's blend space. Axis 0 takes the row stride; `ParamIndex[a]` is -1 when that axis is
// unused, in which case its range is a degenerate 0/0 that must never be divided by.
struct FElysiumBlendGrid
{
	FString Label;
	int32 GroupSize[2] = { 1, 1 };
	int32 ParamIndex[2] = { INDEX_NONE, INDEX_NONE };
	float ParamStart[2] = { 0.f, 0.f };
	float ParamEnd[2] = { 0.f, 0.f };
	TArray<FElysiumBlendCell> Cells;

	// A single-cell grid is not written by the exporter, so this is a corruption test rather than the
	// ordinary "does this label name a grid" question — that one is answered by finding it at all.
	bool IsMultiCell() const { return Cells.Num() > 1; }
	// Addressed by the cell's own `Axis`, never by array position: the exporter preserves the model's
	// declaration order, which is not the same as row-major order.
	const FElysiumBlendCell* CellAt(int32 Axis0, int32 Axis1) const;
};

// Which cell a grid resolved to, and how far past it the parameters actually sat. The fractions are
// what a two-cell blend will weigh on; the nearest-cell pick ignores them.
struct FElysiumBlendPick
{
	const FElysiumBlendCell* Cell = nullptr;
	int32 Index[2] = { 0, 0 };
	float Fraction[2] = { 0.f, 0.f };
};

struct FElysiumBlendTable
{
	FString Stem;
	TArray<FElysiumPoseParamDesc> PoseParams;
	// Keyed by sequence label. FString keys hash and compare case-insensitively in Unreal, which is
	// what the clip vocabulary relies on too — content spells a label however it likes.
	TMap<FString, FElysiumBlendGrid> Grids;

	// A table that parses but declares no grid is not worth caching as a table. The exporter writes
	// no file at all in that case, so this only fires on a damaged one.
	bool IsValid() const { return !Grids.IsEmpty(); }
	const FElysiumBlendGrid* Find(const FString& Label) const { return Grids.Find(Label); }
	const FElysiumPoseParamDesc* Param(int32 Index) const
	{
		return PoseParams.IsValidIndex(Index) ? &PoseParams[Index] : nullptr;
	}

	// The door the runtime uses. RelPath is `npc_index.json`'s own `blends` value, so one function
	// reads both "blends/<stem>.json" and "animated_props/blends/<stem>.json".
	bool Load(const FString& RelPath, FString& OutError);
	// The same parse without touching the disk, so a test can hand it a document directly. There is
	// no import conversion here — a grid is labels, ints and degrees, and carries no geometry.
	bool LoadJsonText(const FString& JsonText, FString& OutError);
};

namespace ElysiumBlendGrids
{
	// One axis -> the cell it lands on and the fraction to the next, per
	// `docs/vtmb/animation_and_movers.md`: wrap the parameter into its loop range, normalize over the
	// descriptor's start..end, remap through the grid's own paramstart..paramend, clamp to 0..1 and
	// scale against `groupsize`. An axis with no parameter yields cell 0 and weight 0, as documented.
	//
	// The scale is against `GroupSize - 1` because the cells are the range's endpoints, not its
	// buckets: on a 9-cell -180..180 fan, cell 0 IS -180 and cell 8 IS +180 — which is why those two
	// share one clip, the wrap seam. It is also why 0 degrees lands exactly on cell 4.
	void ResolveAxis(const FElysiumBlendGrid& Grid, int32 Axis, const FElysiumPoseParamDesc* Desc,
		float Value, int32& OutCell, float& OutFraction);

	// The cell a grid selects at these pose parameters. Nearest cell — the fractions ride along on
	// the pick for the two-cell blend that comes later.
	//
	// A cell whose clip did not bake is skipped rather than played as silence: the pick walks to the
	// neighbour the fraction points at, then outward, so a hole in a fan costs accuracy and never a
	// missing animation.
	FElysiumBlendPick SelectCell(const FElysiumBlendGrid& Grid, const FElysiumBlendTable& Table,
		const FElysiumPoseParams& Pose);
}
