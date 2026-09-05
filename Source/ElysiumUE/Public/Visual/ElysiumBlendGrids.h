#pragma once

#include "CoreMinimal.h"
// The timeline record this file parses, which crosses the outbound service seam and therefore
// lives in `Public/`.
#include "ElysiumAnimEvent.h"
// The authored displacement path this file parses, which crosses to the player's own mover and
// therefore lives in `Public/` for the same reason.
#include "ElysiumClipMovement.h"
#include "ElysiumGaitSpeeds.h"   // FElysiumGaitSpeedTable — what a locomotion fan's motion becomes
#include "ElysiumBlendGrids.generated.h"

// What a model's own sequence descriptors declare beside their clips, off `npc/blends/<stem>.json`:
// its blend spaces, its autolayer bindings and its event timelines. All three come from
// the same 764-byte record, so they ship in one file and are read by one parser. Plain C++ with no
// UObject reflection, like `FElysiumFacialRig` and `FElysiumCompositionRig`; the cache that hands
// one out is `UElysiumAnimSubsystem`.
//
// The file is per **owning** model, never per resolving character: a bank sequence's grid, binding
// and timeline are stated once on the bank that owns the label rather than on each of the ~1,400
// characters that resolve it.
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
USTRUCT()
struct ELYSIUMUE_API FElysiumPoseParamDesc
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Name;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Flags = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float Start = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float End = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
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
USTRUCT()
struct ELYSIUMUE_API FElysiumClipMotion
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float CycleSeconds = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float GroundDistanceCm = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
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
USTRUCT()
struct ELYSIUMUE_API FElysiumBlendCell
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Axis[2] = { 0, 0 };
	// The owner-local MDL animation index. Provenance only — the runtime addresses the clip by name.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 Anim = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Clip;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FElysiumClipMotion Motion;
};

// One sequence's blend space. Axis 0 takes the row stride; `ParamIndex[a]` is -1 when that axis is
// unused, in which case its range is a degenerate 0/0 that must never be divided by.
USTRUCT()
struct ELYSIUMUE_API FElysiumBlendGrid
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	FString Label;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 GroupSize[2] = { 1, 1 };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	int32 ParamIndex[2] = { INDEX_NONE, INDEX_NONE };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float ParamStart[2] = { 0.f, 0.f };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
	float ParamEnd[2] = { 0.f, 0.f };
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation")
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

// The clips one host sequence is composed with, in the order the engine walks them. Read from the
// same sequence descriptor as the grids, which is why it rides in the same file.
//
// **The order is data.** An overlay blends toward its own pose with complementary weights, so it
// overwrites an additive already accumulated onto the bones it owns; walking the array in the order
// it declares is what keeps both contributions. Nearly every host declares its overlay first, and
// exactly one — `throwing_star_midcrouch_idle`, on both shared banks — declares its additive first.
// A consumer that sorts, dedupes by kind, or hardcodes overlay-first composes that host differently
// from retail (`docs/vtmb/animation_and_movers.md` A.3).
//
// The record carries no weight, ramp or flags: what a layer contributes is the caller's to supply.
struct FElysiumAutoLayerBinding
{
	TArray<FString> Clips;
};

struct FElysiumBlendTable
{
	FString Stem;
	TArray<FElysiumPoseParamDesc> PoseParams;
	// Keyed by sequence label. FString keys hash and compare case-insensitively in Unreal, which is
	// what the clip vocabulary relies on too — content spells a label however it likes.
	TMap<FString, FElysiumBlendGrid> Grids;
	// Keyed the same way, by HOST label. Only the two shared `move_and_ranged` banks carry any.
	TMap<FString, FElysiumAutoLayerBinding> AutoLayers;
	// Keyed the same way again, by the label of the sequence the timeline belongs to. **The array
	// order is the file's**, which is the order the dispatcher fires records that share a cycle in.
	TMap<FString, TArray<FElysiumAnimEvent>> Events;
	// The authored displacement paths, keyed the same way again. A label that is absent authors no
	// record at all — see `bMovementStated`, which is what tells that apart from a file nothing
	// looked in.
	TMap<FString, FElysiumClipMovementPath> Movement;
	// Whether the sidecar carried `movement_fields`. **The two absences are different and a reader
	// must not collapse them**: false means this file never read the
	// `mstudiomovement_t` array, so it says nothing about any clip and a consumer reports the gap;
	// true with a label missing from `Movement` is the file stating that the clip authors no
	// movement, which is the value retail's own `Studio_AnimMovement` returns false for.
	bool bMovementStated = false;
	// **Why `bMovementStated` is false, when it is.** A file that carries no `movement_fields` at all
	// omitted the column and is fixed by re-exporting it; a file that carries one this reader cannot
	// address names columns the reader does not, and re-exporting it changes nothing. Both leave every
	// clip unanswerable, and they have opposite remedies, so the consumer is told which it has.
	bool bMovementSchemaUnreadable = false;
	// Movement rows dropped for being malformed while the rest of the table installed. Non-zero means
	// a clip's authored path is short or missing on a table the runtime still uses, which no `IsValid`
	// answer reports.
	int32 MalformedMovementRows = 0;

	// A table that parses but declares no grid, no binding, no timeline and no path is not worth
	// caching. The exporter writes no file at all in that case, so this only fires on a damaged one.
	bool IsValid() const
	{
		return !Grids.IsEmpty() || !AutoLayers.IsEmpty() || !Events.IsEmpty() || !Movement.IsEmpty();
	}
	const FElysiumBlendGrid* Find(const FString& Label) const { return Grids.Find(Label); }
	// The layers `Label` declares, or null. Never reordered — see FElysiumAutoLayerBinding.
	const FElysiumAutoLayerBinding* FindAutoLayers(const FString& Label) const
	{
		return AutoLayers.Find(Label);
	}
	// `Label`'s timeline, or null when that sequence declares none — which is most of them, and an
	// absence rather than a fault.
	const TArray<FElysiumAnimEvent>* FindEvents(const FString& Label) const
	{
		return Events.Find(Label);
	}
	// `Label`'s authored displacement path, or null when that sequence declares none — which is
	// almost all of them, and an absence rather than a fault.
	const FElysiumClipMovementPath* FindMovement(const FString& Label) const
	{
		return Movement.Find(Label);
	}
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

	// The cell a grid selects at these pose parameters. The **floor** cell, with the fractions riding
	// along on the pick — so `Cell` and `Cell + 1` are the pair a two-cell blend interpolates, and
	// `Fraction` is the weight of the second. A consumer wanting the nearest cell rounds the fraction
	// itself; the gait speed fan (`ElysiumGaitSpeeds.h`) interpolates instead.
	//
	// A cell whose clip did not bake is skipped rather than played as silence: the pick walks to the
	// neighbour the fraction points at, then outward, so a hole in a fan costs accuracy and never a
	// missing animation.
	FElysiumBlendPick SelectCell(const FElysiumBlendGrid& Grid, const FElysiumBlendTable& Table,
		const FElysiumPoseParams& Pose);

	// The single cell a body that cannot evaluate a fan collapses one onto: the floor pick,
	// stepped to its neighbour on whichever axis sat past the half-cell, with the fractions cleared.
	//
	// It exists for exactly one caller — a body with no compiled reaction branch, which has a montage
	// or a clip player and therefore one sequence to play. Flooring such a body would bias every
	// collapsed reaction a cell counter-clockwise; the nearer cell quantizes to +-22.5 degrees on a
	// nine-cell fan. A body that CAN blend never comes through here: the pair and the fraction on the
	// record are what the graph evaluates.
	//
	// The step clamps to the last cell, so a fan whose axis has already been wrapped and clamped
	// cannot be walked off the end. A neighbour that did not bake keeps the floor pick, which
	// `SelectCell` has already walked past the hole to reach.
	FElysiumBlendPick NearerCell(const FElysiumBlendPick& Pick, const FElysiumBlendGrid& Grid);

	// One locomotion fan's authored per-cell ground speeds, as the table the mover steers by.
	//
	// Refuses anything that is not a wrapping single-axis fan spanning its parameter's whole loop: a
	// partial slice or a two-axis grid is not a gait, and answering a speed for one would be worse
	// than falling back to the constants.
	//
	// A cell with no authored motion is a **hole**, and holes are interpolated across from the
	// nearest cells that do carry one, circularly. That is a deliberate improvement on retail, which
	// leaves such a cell holding whatever the previous frame's extraction wrote; our exporter simply
	// omits unusable motion, so there is no stale value to hold and a gap in the fan is better filled
	// than frozen. A fan with no usable cell at all is refused.
	bool SpeedFan(const FElysiumBlendGrid& Grid, const FElysiumBlendTable& Table, float Scale,
		FElysiumGaitSpeedTable& Out);
}
