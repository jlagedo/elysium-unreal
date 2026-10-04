#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

// One arena scenario record: a tracked JSON file under `<ProjectDir>/Arena/scenarios/**/*.json`,
// whose schema is `Arena/README.md`. The record is DATA ONLY -- a plain struct and its reader, with no
// world, no actor and no entity in it -- so a scenario is added or tuned without a build, and the two
// hosts that run one (`FElysiumArenaRun`, headless; `elysium.gr_scenario`, the lab) read the same thing.
//
// What a record states and nothing more: the stage, the cast as map-shaped rows, any further rows,
// the player's seat, a timed script, and the expectations over the run's AI trace
// (`FElysiumAiTraceEvent`, `docs/specs/0002-npc-ai/stories/wave2/seam.md`). Every keyvalue a cast row
// carries is in the record; the staging adds none of its own.
//
// Every parse error names the file and the field (`<file>: expect[2].kind: ...`), because a record is
// edited by hand and a refusal that does not say where is a refusal nobody can act on.

// A place: a name the host resolves, or explicit coordinates.
struct FElysiumArenaAt
{
	bool bSet = false;
	// Arena host: a seat (`start`, `cover_seat`, `cover_behind`), a pad, an anchor or a node of
	// `ElysiumArena::FSpec`. Map host: a targetname.
	FString Name;
	// `[x, y, z]`: arena-relative centimetres on the arena host, map centimetres on a map host.
	bool bCoordinates = false;
	FVector Coordinates = FVector::ZeroVector;
};

// Which way a placed thing looks.
struct FElysiumArenaFace
{
	enum class EKind : uint8
	{
		Default,   // the place's own facing (a pad's, an anchor's, a seat's), 0 for coordinates
		Player,    // toward the player's seat
		Name,      // toward a named place (or, on a map host, an entity)
		Yaw,       // an Unreal-native yaw, degrees
	};
	EKind Kind = EKind::Default;
	FString Name;
	float Yaw = 0.0f;
};

// A JSON scalar kept with its type: a probe's expected value, an input's parameter.
struct FElysiumArenaValue
{
	enum class EType : uint8 { None, Bool, Number, String };
	EType Type = EType::None;
	bool bBool = false;
	double Number = 0.0;
	FString String;

	FString Describe() const;
};

// One entity row: a `cast` entry, a `rows` entry, or a `spawn` action's row.
struct FElysiumArenaRow
{
	FString Name;                  // the targetname
	FString Classname;             // empty on a map host's cast row, which names an entity the map has
	FElysiumArenaAt At;
	FElysiumArenaFace Face;
	// A cast row's body: a stem, an alias, a source path or a `vtmb:model:` id, written as the `model`
	// key a shipped map row carries (`models/<unit>.mdl`).
	FString Body;
	TMap<FString, FString> Keys;   // ordinary map keyvalues, stated whole
};

// `from_map`: rows of a baked map's `DA_<map>_Entities`, verbatim, moved so `Anchor` stands at `At`.
struct FElysiumArenaFromMap
{
	bool bSet = false;
	FString Map;
	TArray<FString> Names;
	FString Anchor;
	FElysiumArenaAt At;
};

enum class EElysiumArenaAction : uint8
{
	PlayerTeleport,
	PlayerWalk,
	Fire,
	Console,
	Spawn,
	Kill,
	PlayerCrouch,
	LightPin,
	DialogChoose,
};

// One `script` entry: when (an absolute scenario time, or a delay after a labelled expectation's
// match) and what.
struct FElysiumArenaAction
{
	EElysiumArenaAction Do = EElysiumArenaAction::Fire;

	bool bAtTime = false;
	double Time = 0.0;
	FString After;                 // an expectation's `label`
	double Delay = 0.0;

	FElysiumArenaAt At;            // player_teleport, player_walk
	FElysiumArenaFace Face;        // player_teleport
	FString Target;                // fire, kill
	FString Input;                 // fire
	FElysiumArenaValue Param;      // fire; None is a void parameter
	FString Command;               // console
	FElysiumArenaRow Row;          // spawn
	bool bOn = false;              // player_crouch: `on`, the posture the press heads for
	// light_pin: `value`, the normalized body light pinned in [0, 1]; `null` releases the pin.
	bool bLightRelease = false;
	double Light = 0.0;
	// dialog_choose: `index`, the response row as the open turn lists it (0-based), or `end: true`,
	// retail's pick -1 (`CDialog::Pick` -> `Release`). Exactly one of the two.
	int32 ChoiceIndex = INDEX_NONE;
	bool bDialogEnd = false;
};

// One `expect` or `never` entry: a matcher over trace events.
struct FElysiumArenaMatch
{
	FString Label;                 // expect only: what a script `after` names
	FString Who;                   // a targetname; empty matches any entity
	FName Kind;                    // one of `ElysiumArenaScenario::TraceKinds()`
	FString Match;                 // case-sensitive substring of the event's text; empty matches any
	bool bRegex = false;           // `Match` is a regular expression instead

	// expect only: the deadline. `By` is scenario seconds; `Within` is seconds after the previous
	// expectation's match (after zero, for the first). Neither: the record's duration.
	bool bBy = false;
	double By = 0.0;
	bool bWithin = false;
	double Within = 0.0;

	// never only: the window closes here (absent: the whole run).
	bool bUntil = false;
	double Until = 0.0;

	// never only: where the window opens. `From` scenario seconds, or `Delay` seconds after the
	// expectation labelled `After` (`AfterIndex` into `Expect`) is met -- never, if it is not. Neither:
	// zero.
	bool bFrom = false;
	double From = 0.0;
	FString After;
	int32 AfterIndex = INDEX_NONE;
	double Delay = 0.0;
	// never only: the matches the window tolerates. Match number `AtMost + 1` fails the run.
	int32 AtMost = 0;
};

enum class EElysiumArenaProbe : uint8
{
	Alive,           // bool: the entity is not dead and its life state is alive
	Schedule,        // string: the running schedule's authored name, `SCHED_NONE` when none
	State,           // string: the mind's NPC state (`Idle`, `Alert`, `Combat`, ...)
	Health,          // number
	Enemy,           // string: the committed enemy's targetname, `none`
	Hint,            // string: the claimed hint's targetname (`m_pHintNode`), `none`
	HasCondition,    // bool: `condition` is set in the gathered conditions
	OnGround,        // bool: the body's motor reports a floor under it
	DistanceTo,      // number, centimetres: to `to` (a targetname, `player`, or a place)
	// `who: "player"` only.
	PlayerWeapon,    // string: the active item's classname, `none`
	PlayerCrouched,  // bool: `FL_DUCKING`, as every game reader of the posture sees it
	PlayerGrappling, // bool: paired in a grapple (feed, stealth kill) whose partner still resolves
};

enum class EElysiumArenaCompare : uint8
{
	Equals,
	Match,           // string contains
	Less,
	Greater,
};

struct FElysiumArenaProbeSpec
{
	bool bAtEnd = true;
	double Time = 0.0;
	FString Who;                   // a targetname, or `player`
	EElysiumArenaProbe Probe = EElysiumArenaProbe::Alive;
	int32 Condition = INDEX_NONE;  // has_condition: the condition's retail number
	FString ConditionName;
	FElysiumArenaAt To;            // distance_to
	EElysiumArenaCompare Compare = EElysiumArenaCompare::Equals;
	FElysiumArenaValue Value;
};

struct FElysiumArenaPlayer
{
	FElysiumArenaAt At;            // absent: `start` on the arena host; left where it is on a map host
	FElysiumArenaFace Face;
	bool bArmed = false;           // `true`: the cast harness's whole arsenal
	FString ArmedItem;             // `"armed": "<item classname>"`: that one item
	bool bNoTarget = false;
};

struct FElysiumArenaScenario
{
	FString File;                  // where it was read from, for every message about it
	FString Name;
	FString About;
	FString Stage;                 // `arena`, or `map:<map>`
	FString StageMap;              // `<map>` of `map:<map>`; empty on the arena host
	int32 Seed = 0;
	double Duration = 0.0;
	FString KnownRed;
	bool bExpectFail = false;
	bool bSharesMap = false;

	FElysiumArenaPlayer Player;
	TArray<FElysiumArenaRow> Cast;
	TArray<FElysiumArenaRow> Rows;
	FElysiumArenaFromMap FromMap;
	TArray<FElysiumArenaAction> Script;
	TArray<FElysiumArenaMatch> Expect;
	TArray<FElysiumArenaMatch> Never;
	TArray<FElysiumArenaProbeSpec> Probes;

	bool IsArenaStage() const { return StageMap.IsEmpty(); }
};

namespace ElysiumArenaScenario
{
	// `<ProjectDir>/Arena/scenarios`.
	FString ScenarioRoot();

	// The trace kinds a matcher may name: `seam.md`'s, then `script`, the runner's own record of each
	// action it ran.
	TArrayView<const TCHAR* const> TraceKinds();

	// Parse one record. False with `OutError` naming the file and the field.
	bool ParseText(const FString& Text, const FString& File, FElysiumArenaScenario& Out,
		FString& OutError);
	bool LoadFile(const FString& Path, FElysiumArenaScenario& Out, FString& OutError);

	// Every record under the root, sorted by name. A file that does not parse is reported in
	// `OutErrors` (one line each, the file named) and left out; two records with one name are both
	// reported and the second is left out.
	void LoadAll(TArray<FElysiumArenaScenario>& Out, TArray<FString>& OutErrors);

	const TCHAR* ActionName(EElysiumArenaAction Action);
	const TCHAR* ProbeName(EElysiumArenaProbe Probe);
	const TCHAR* CompareName(EElysiumArenaCompare Compare);
	// `who kind "match"` for a log line or a report.
	FString DescribeMatch(const FElysiumArenaMatch& Match);
}

#endif // !UE_BUILD_SHIPPING
