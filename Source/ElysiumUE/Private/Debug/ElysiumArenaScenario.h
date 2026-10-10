#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityDefs.h"

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

// Explicit non-authored initial state for the fourth-sitting same-arm reload witness (0x10334e70).
struct FElysiumArenaInitialWeaponState
{
	FString Who, Weapon;
	int32 Magazine = 0, Reserve = 0, FakeReloadCount = 0;
};

// One typed fixture of the record's `fixtures` catalog (`docs/specs/layers/harness.md`): staged by the
// runner before the observation window, then named by an `entity_call` argument (`{"fixture": id}`)
// or an `entity_field` probe (`who: "fixture:<id>"`). Kinds are `ElysiumArenaScenario::FixtureKinds()`.
struct FElysiumArenaFixture
{
	FString Id;
	FString Kind;
	TMap<FString, FString> Values; // `keyvalues`: a controlled KeyValues table, text as a map row spells it
	FString Text;                  // `text`: raw text handed whole to a reader (the KeyValues lexer / parser)
	// `sound_folder`: the typed configuration of a structured fixture, kept as the record's own JSON
	// object and read by the runner when it stages the fixture (a malformed one fails the staging).
	TSharedPtr<class FJsonObject> Config;
	// `sprite_model`: one row of the engine's model table a runtime-created sprite's `CSprite::Spawn`
	// asks (`VEngineServer014` slot 26): the model name and its frame count (L0-r013).
	FString Model;
	int32 Frames = 0;
	// `save_blocks` (L0-r029): a `CSaveRestoreBlockSet` of the record's own handlers, each writing
	// `header` dwords in WriteSaveHeaders and `body` dwords in Save, registered in order at staging
	// (set slot 9 0x101a5020); the engine's save buffer beside it (`capacity` 0: grows). `patch`
	// edits a directory record (`locHeader` / `locBody`) of the saved stream before every restore,
	// the controlled save bytes a −1 arm needs.
	struct FSaveBlock { FString Name; int32 HeaderWords = 1; int32 BodyWords = 1; };
	struct FSaveBlockPatch { FString Name; FString Field; int32 Value = -1; };
	TArray<FSaveBlock> Blocks;
	TArray<FSaveBlockPatch> Patches;
	int32 Capacity = 0;
	// `game: true` (L0-r030): the set is the static "Game" set over the host world -- `SaveRestore_Save`
	// is the world's own Freeze (slots 17, 23, 18, 19 with the five retail handlers), so the stream
	// carries the entity table, its transition flags and the ADJACENCY rows a `SaveRestore_
	// CreateEntityTransitionList` selects from. `blocks` is refused with it.
	bool bGame = false;
};

// One argument of an `entity_call`: a typed scalar, or a staged fixture's handle.
struct FElysiumArenaCallArg
{
	FElysiumArenaValue Value;
	FString Fixture; // non-empty: the argument is this fixture, `Value` unused
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
	TArray<FElysiumOutputDef> Outputs; // explicit stage wires, same map-shaped representation
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
	SeedHealth, // fixture-only high-cap measurement; no gameplay input
	DamagePacket, // fixture-only packet with a real attacker; 0x1032ef60 admission
	EntityCall,   // a retail entry point through the allowlist (inputs, use, touch, think, spawn, damage)
	// Harness transaction/fixture doors; 0x20096010/0x200975f0/0x1011a620.
	Save, Load, FreshMap, Travel, RestoreCompare,
	NpcSingleRoundFinishReload, CorruptCheckpoint, InvalidMarker, RestoreBase,
	NoRagdollDeath, DamageMemory, ReserveSpot, StartNpcGroundGate,
};

// Typed retail words at capture/apply fences (0x1027bf50/0x1011a620).
struct FElysiumArenaWitness
{
	FString Who;
	FString Field;
	double Tolerance = 0.0;
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
	FString Activator;             // fire: optional live input activator (0x102c29a0)
	FString Attacker;              // damage_packet: live named actor or explicit none
	FString Inflictor;
	FString Slot, Map, Landmark, Checkpoint, Control;
	TArray<FElysiumArenaWitness> Fields;
	double Timeout = 60.0; // harness wall bound, never simulation time; 0x200975f0

	FString Function;              // entity_call: the retail operation, checked against the allowlist
	TArray<FElysiumArenaCallArg> Args; // entity_call
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
	FString Site;                  // `retail_site` only: the event's `tag=` token; empty matches any site
	FString Match;                 // case-sensitive substring of the event's text; empty matches any
	bool bRegex = false;           // `Match` is a regular expression instead

	// expect only: the deadline. `By` is scenario seconds; `Within` is seconds after the previous
	// expectation's match (after zero, for the first). Neither: the record's duration.
	bool bBy = false;
	double By = 0.0;
	bool bWithin = false;
	double Within = 0.0;

	// never only: the window closes here (absent: the whole run). A `never` may instead carry
	// `Within` (the field above): the window closes that many seconds after it opens at its `After`
	// label (the match plus `Delay`); `Until` and `Within` exclude each other.
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
	OnGround,        // bool: the motor CAPSULE's floor answer (false on every corpse: use CorpseOnFloor)
	DistanceTo,      // number, centimetres: to `to` (a targetname, `player`, or a place)
	// `who: "player"` only.
	PlayerWeapon,    // string: the active item's classname, `none`
	PlayerCrouched,  // bool: `FL_DUCKING`, as every game reader of the posture sees it
	PlayerGrappling, // bool: paired in a grapple (feed, stealth kill) whose partner still resolves
	// H18 (spec 0002 V4a seam): the walk's direct acceptance.
	Speed2d,         // number, cm/s: the body's horizontal speed (the motor's velocity)
	MoveYaw,         // number, degrees: the body's movement yaw relative to its facing (the motor's sample)
	GroundSpeed,     // number, cm/s: the kernel's `m_flGroundSpeed +0x654`
	// H20: the drawn mesh's `Bip01 Pelvis` bone within `max_height` cm of the floor under it and at rest.
	CorpseOnFloor,   // bool
	// H22's companion: a live (not removed) entity of that name is in the entity world.
	SameTeam, SwingRecordedHit, OneHitKill, TeamSymbol, Wounds, HealthCap, NpcFlags1, SpawnFlags, RenderAlpha, RenderMode, Activity, // V4c read-only retail words/contact
	Witness,        // typed dispatch, including checkpoint equality; 0x1027bf50
	Exists,          // bool
	EntityField,     // a retail field by its retail name (`m_iHealth`...), typed by the field; read-only
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
	FString Fence; // at: pre_init/captured/applied/ready, exact runtime observation (0x1011a620)
	double Time = 0.0;
	FString Who;                   // a targetname, or `player`
	EElysiumArenaProbe Probe = EElysiumArenaProbe::Alive;
	int32 Condition = INDEX_NONE;  // has_condition: the condition's retail number
	FString ConditionName;
	FElysiumArenaAt To;            // distance_to
	// corpse_on_floor: `max_height`, the pelvis bone's height bound over the floor under it,
	// centimetres -- per the record's body, measured (`stories/v4/packets-spike.md` finding 5).
	double MaxHeightCm = 0.0;
	EElysiumArenaCompare Compare = EElysiumArenaCompare::Equals;
	FElysiumArenaValue Value;
	FString Index, Member; // entity_field: optional indexed-table / vector-component selectors
	FString Field, Checkpoint; // witness / entity_field: selected retail word; 0x1027bf50
	double Tolerance = 0.0;
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
	TArray<FElysiumArenaInitialWeaponState> InitialWeaponState;
	TArray<FElysiumArenaFixture> Fixtures;
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

	// The fixture kinds a `fixtures` entry may name.
	TArrayView<const TCHAR* const> FixtureKinds();

	// The retail entry points `entity_call` may invoke (inputs, use, touch, think, spawn, damage, and the
	// KeyValues reader's lex/parse over a `text` fixture). A story adds the entry points its records
	// drive, in the slice that ports them, with their dispatch in the runner's `RunAction`.
	const TArray<FString>& EntityCallAllowlist();

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
	// Closed typed vocabulary; raw pointers/caches/shoot-at reroll are excluded (0x102998c0).
	bool WitnessType(const FString& Field, FElysiumArenaValue::EType& Out);
	bool WitnessEqual(const FElysiumArenaValue& Saved, const FElysiumArenaValue& Applied, double Tolerance);
	FElysiumArenaValue RebaseWitness(const FString& Field, const FElysiumArenaValue& Saved, double SaveBase, double RestoreBase); // 0x101a0a80/0x101a2a30
	// `who kind "match"` for a log line or a report.
	FString DescribeMatch(const FElysiumArenaMatch& Match);
}

#endif // !UE_BUILD_SHIPPING
