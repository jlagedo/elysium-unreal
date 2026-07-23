#pragma once

#include "CoreMinimal.h"

// One row of an entity's `.ents` `outputs[]` array. VtMB writes SEVEN comma fields on an
// On*/Out* key where stock Source writes five (entity_io.md): the four stock fields
// (target, input, param, delay) plus `times` and a Python call string (field 5). Field 6
// ("extra") is unused by the game and dropped at export, so it is not carried here. `Name`
// is the output's key (e.g. "OnPressed"); one entity may fire several rows under one Name.
struct FElysiumOutputDef
{
	FString Name;    // the On*/Out* key this output fires on
	FString Target;  // targetname to deliver to (`!self`/`!activator` resolve at fire time)
	FString Input;   // input name on the target
	FString Param;   // parameter string passed to the input
	float   Delay = 0.0f;   // seconds to defer the delivery
	int32   Times = -1;     // remaining fire count; -1 = unlimited (counted down at runtime)
	FString Python;         // field 5: a call string the engine evals as `__main__.<expr>`

	// Fires nothing but Python (no I/O target). 6,851 of the game's outputs are this shape —
	// the queue entry is still real (python_bridge.md).
	bool IsPythonOnly() const { return Target.IsEmpty() && !Python.IsEmpty(); }
};

// One brush entity's convex volume, in entity-local Unreal centimetres (world position =
// def origin + vertex). Vertices are an unordered point cloud; the collision cooker (P1.5)
// builds the hull, so order is irrelevant.
struct FElysiumConvexHull
{
	TArray<FVector> Vertices;
};

// R3/R7 — one immutable parsed `.ents` record: everything the runtime needs to spawn one
// entity and give it I/O identity. The def array is the map's entity "asset"; an entity's
// stable handle index (R3) is its position in that array. `Keys` holds the raw keyvalues
// verbatim (classname/targetname hoisted out but the source-space `origin` string, `model`,
// `StartHidden`, spawnflags, … all still present); the class field tables (P1.3) read typed
// values back out of it at spawn. `Origin` is the Unreal-space vector to use for placement.
struct FElysiumEntityDef
{
	FString Classname;
	FString TargetName;                     // may be empty; targetnames are non-unique (R3)
	FVector Origin = FVector::ZeroVector;   // Unreal cm, read verbatim (UE_ exporter)

	// Raw keyvalues minus classname/targetname. Case-folded typed reads happen in P1.3.
	TMap<FString, FString> Keys;

	// Brush-entity fields (present only when the entity carried a "*N" brush model).
	int32 Model = INDEX_NONE;               // brush model index, or INDEX_NONE for point ents
	TArray<FElysiumConvexHull> Hulls;       // entity-local convex volumes
	int32 Contents = 0;                     // OR of the brushes' CONTENTS flags
	bool bBlocksPlayer = false;             // Contents & BLOCK_MASK (computed at export)

	// Spawns fully OFF — non-solid, non-thinking, undrawn — until a ScriptUnhide (R6).
	bool bStartHidden = false;

	TArray<FElysiumOutputDef> Outputs;

	bool IsBrush() const { return Model != INDEX_NONE; }
};

// The parsed `<map>.ents` file: the immutable def array for one map load. Built once at map
// load (P1.4 hands it to the entity world). Parse is a plain function — no UObject, no
// reflection (R1) — owned entirely by us and trivially testable.
struct FElysiumEntityDefs
{
	FString MapName;
	TArray<FElysiumEntityDef> Defs;

	int32 Num() const { return Defs.Num(); }

	// The map's `worldspawn.levelscript` value (e.g. "tutorial"), or empty when worldspawn carries
	// no such key. Names the hub Python module at scripts/<module>/<module>.py, which the runtime
	// imports at map load (roadmap 9.3). Read off the parsed defs rather than the live world so the
	// import happens before the spawn pass — a level script's module-level code must be in place
	// before any entity can evaluate a field-6 payload against it.
	FString LevelScriptModule() const;

	// Parse `<map>.ents` (JSON) from disk into Out. Returns false (leaving Out empty) if the
	// file is missing or not valid `.ents` JSON. Every field is read verbatim — the exporter
	// already emits Unreal space (the UE_ convention), so there is no conversion here.
	static bool Parse(const FString& EntsPath, FElysiumEntityDefs& Out);
};
