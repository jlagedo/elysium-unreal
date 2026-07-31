// 12.1 — the choreographed-scene file (`.vcd`) as parsed data.
//
// VtMB plays every cinematic and every spoken line out of a Faceposer choreo scene. The format is
// plain ASCII with one uniform rule: **every line is a whitespace-separated word list, optionally
// followed by a `{ … }` block** — actors, channels, events and every sub-block alike. Quoted words
// keep their spaces. That one rule parses the whole file.
//
// This is the data half only: no world, no entity, no engine. `ElysiumScenePlayer.h` walks a parsed
// scene against a clock, and `ElysiumChoreoScene.cpp` is the entity that owns both. The split is so
// the per-line dialogue path (12.2's CInstancedSceneEntity, ~5,300 of the 5,444 shipped scenes) can
// reuse the same reader and timeline without an entity.
//
// Format, event-type enum and the shipped corpus counts: `docs/vtmb/choreographed_scenes.md`.
// The reference implementation this is ported from is `research/tooling/probes/probe_scenes.py` (`tokenize_line` /
// `parse`); the two must stay in step, because `Elysium.Content.SceneCorpus` asserts this reader
// reproduces that doc's histograms over all 5,444 files.

#pragma once

#include "CoreMinimal.h"

// CChoreoEvent's type enum, in VtMB's own order (the name mapping at FUN_10074bf0 and the parse
// token table at 0x10547984). Types 1..12 are Valve's unchanged; 13..19 are Troika's additions
// where later Source put INTERRUPT/STOPPOINT/PERMIT_RESPONSES/GENERIC/CAMERA/SCRIPT.
//
// Nine are live in the shipped corpus (Speak, Silence, Loud, Expression, Gesture, Sequence,
// FireTrigger, Python, BodySound); the other ten have zero authored uses, and CameraShot has no
// handler in VtMB either. `Elysium.Content.SceneCorpus` asserts the dead ten stay at zero.
enum class EElysiumChoreoEvent : uint8
{
	Unknown = 0,
	Section,
	Expression,
	LookAt,
	MoveTo,
	Speak,
	Gesture,
	Sequence,
	Face,
	FireTrigger,
	FlexAnimation,
	Subscene,
	Loop,
	Silence,
	Loud,
	Python,
	CameraMove,
	CameraShot,
	CameraRestore,
	BodySound,
	Count
};

// One `event <type> "<name>" { … }`.
struct FElysiumSceneEvent
{
	EElysiumChoreoEvent Type = EElysiumChoreoEvent::Unknown;
	FString Name;
	FString Param;    // the payload string — a wav path, a sequence label, a trigger number, …
	FString Param2;   // the second payload — a dB level, an expression name. There is no param3.

	// Seconds from scene start. `time <start> <end>`, with end -1 meaning "instantaneous" — 39
	// events corpus-wide use it, including every firetrigger. When bHasEnd is false EndTime is held
	// equal to StartTime so callers never have to special-case it.
	float StartTime = 0.f;
	float EndTime = 0.f;
	bool  bHasEnd = false;
	// Actor/channel blocks with `active 0` remain parsed and inspectable, but their events never
	// enter the runtime timeline.
	bool  bActive = true;

	// `fixedlength` — the event's length is the asset's, not the authored range (12.2 acts on it).
	bool  bFixedLength = false;
	// `sequenceduration <s>` — 56 gesture events carry it. Parsed and surfaced, not acted on.
	float SequenceDuration = 0.f;

	// `event_ramp { <time> <value> … }` — a per-event intensity envelope (1,401 uses in 264 files,
	// almost all on `expression`). Sorted by time; see RampAt.
	TArray<FVector2f> Ramp;

	// Which actor/channel this event was authored under. INDEX_NONE for a scene-level event.
	// Channel names carry no semantics — the engine walks every event of every actor — so the
	// channel index exists for debug output, not dispatch.
	int32 ActorIndex = INDEX_NONE;
	int32 ChannelIndex = INDEX_NONE;

	// The ramp sampled at scene-relative time T, piecewise-linear and clamped at both ends.
	// An event with no authored ramp is at full intensity, so this returns 1.
	float RampAt(float T) const;
};

struct FElysiumSceneChannel
{
	FString Name;
	bool bActive = true;
};

struct FElysiumSceneActor
{
	FString Name;
	// `bonerename "<from>" "<to>"` (5,032 uses). Overwhelmingly identity on dialogue scenes, but
	// load-bearing on cinematics: one shared clip holds several co-located skeletons and each actor
	// takes a different bone root out of it (Bip01/Bip02/Bip03 → Bip01).
	FString BoneFrom;
	FString BoneTo;
	FString FaceposerModel;
	bool bActive = true;
};

struct FElysiumSceneData
{
	// The normalized mirror-relative key this was loaded under (lowercased, `sound/` stripped).
	FString SourceRel;
	// `// Choreo version 1` on 5,434 files; 10 carry no version line at all (INDEX_NONE).
	int32 Version = INDEX_NONE;
	// Faceposer's editing grid and snap flag. The runtime reads seconds, not frames — these are
	// carried for the corpus test and the inspector only.
	float Fps = 60.f;
	bool  bSnap = false;

	TArray<FElysiumSceneActor>   Actors;
	TArray<FElysiumSceneChannel> Channels;
	// One flat array, sorted by StartTime (stable, so same-time events keep authored order).
	TArray<FElysiumSceneEvent>   Events;

	// max over events of (EndTime, or StartTime when there is no end). The player extends this by
	// the audio mixahead for speak events; this value is the raw authored one.
	float LatestTime = 0.f;

	// Per-type counts, indexed by EElysiumChoreoEvent. Drives the inspector's "unrouted" rows and
	// the corpus test's histogram.
	int32 TypeCounts[static_cast<int32>(EElysiumChoreoEvent::Count)] = {};
	// Events whose authored end preceded their start (the corpus has a handful, one by 86 seconds).
	// Clamped on read, counted here.
	int32 NumDegenerate = 0;

	bool bValid = false;

	int32 CountOf(EElysiumChoreoEvent Type) const { return TypeCounts[static_cast<int32>(Type)]; }
};

namespace ElysiumScene
{
	// A SceneFile keyvalue -> the mirror-relative key. Folds `\` to `/`, strips a leading `sound/`
	// (case-insensitive) and any leading slash, then lowercases. The lowercasing is what makes the
	// parse cache single-entry per scene when two maps spell the same path differently.
	FString NormalizeSceneRel(const FString& SceneFile);

	const TCHAR* EventTypeName(EElysiumChoreoEvent Type);
	EElysiumChoreoEvent EventTypeFromToken(const FString& Token);

	// Parse in-memory text. Never asserts: the shipped corpus contains degenerate time ranges and
	// tokens no reader uses, and a scene that cannot be understood comes back with bValid false.
	void ParseText(const FString& Text, const FString& SourceRel, FElysiumSceneData& Out);

	// Load and parse through the shared cache, keyed on the normalized path. Returns null only when
	// the file is missing or unparseable — and that negative is cached too, so a broken SceneFile
	// does not re-hit the disk on every map load (8 of the 113 referenced scenes do not resolve).
	TSharedPtr<const FElysiumSceneData> Load(const FString& SceneFile);

	// Seed the cache with scene text under a key, so a headless test can drive a scene without
	// touching `$ELYSIUM_EXPORT_ROOT`. Overwrites any cached entry for that key.
	void RegisterInline(const FString& Key, const FString& Text);

	void ClearCache();
	void CacheStats(int32& OutEntries, int32& OutHits, int32& OutMisses);
}
