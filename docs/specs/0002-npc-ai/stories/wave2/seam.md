# Wave 2 seam — the AI trace event (lane B adds it, lane A consumes it)

Debug output only: no rule reads it, it is never saved, and with no sink installed every tap is one
branch and nothing else.

## Declarations — `Source/ElysiumUE/Public/ElysiumEntityWorld.h`, beside `AppendAiDebugTrace`

```cpp
// One observable AI event, for a harness that records a run (the arena scenarios).
struct FElysiumAiTraceEvent
{
	double Time = 0.0;              // FElysiumEntityWorld::NowSeconds() when it happened
	FElysiumEntityHandle Entity;    // who it happened to: an NPC, a hint, any entity
	FString Name;                   // that entity's targetname at the time (may be empty)
	FName Kind;                     // one of the kinds below
	FString Text;                   // the event's own text, in the format its kind states
};
using FElysiumAiTraceSink = TFunction<void(const FElysiumAiTraceEvent&)>;

// On FElysiumEntityWorld:
void SetAiTraceSink(FElysiumAiTraceSink Sink);   // an empty function clears it
bool HasAiTraceSink() const;
// No-op without a sink. `Entity` may be any entity; the event carries its handle and targetname.
void EmitAiTrace(const FElysiumEntity& Entity, FName Kind, FString Text);
```

The sink is called synchronously, on the game thread, at the point the event happens. It is cleared
when the world is torn down (the sink's owner re-installs it on the rebuilt world).

## Kinds and their text

Names are retail's where retail prints one; a number rides along in the form the existing trace
uses (`ElysiumScheduleLabel`, `TraceCondLabel`).

| Kind | Emitted when | Text |
|---|---|---|
| `schedule` | a schedule is installed (`SetSchedule 0x10280e50`) | the label the existing "Schedule: %s" line prints |
| `task` | a task starts (`StartTask`) | the task's name and operand, as the existing "Task: %s" line prints |
| `taskdone` | a task completes | the task's name |
| `taskfail` | `TaskFail` | the fail code's name and number |
| `break` | `IsScheduleValid` breaks the schedule | `COND_NAME (0xNN)`, `!` prefix for an inverted condition |
| `cond+` / `cond-` | a condition is set / cleared across one gather | `COND_NAME (0xNN)` |
| `state` | `m_NPCState` changes | `OLD -> NEW` |
| `sequence` | the body is handed a sequence (`ResetSequenceInfo 0x10090950`'s site) | `<activity or sequence name> rate=<playback rate>` |
| `seqfinished` | `m_bSequenceFinished` rises | the sequence name |
| `animevent` | an animation event is dispatched to the NPC | `<event id> <options>` |
| `move` | a navigation goal is issued, reached or failed | `goal <x> <y> <z>` / `arrived` / `fail <code>` |
| `damage` | `OnTakeDamage` applies damage | `<amount> type=<bits> from=<attacker targetname or none>` |
| `death` | the NPC dies (slot 144's entry) | the killer's targetname or `none` |
| `corpse` | the corpse arm lands the body | `ragdoll` / `static` |
| `hint+` / `hint-` | a hint is claimed / released by this NPC | `<hint targetname> node=<id>` |
| `output` | any entity fires an output | `<OutputName> -> <target>.<Input> <param>` |
| `input` | any entity receives an input | `<InputName> <param> from=<activator targetname or none>` |
| `stealthkill` | the player's stealth-kill query (`CStealthKillRules::FindVictim 0x101be1f0`, `FElysiumStealthKillRules::FindVictim`) computes a result that differs (victim, verdict or gate) from the last `stealthkill` event emitted since the sink was installed (`FElysiumEntityWorld::EmitAiTraceOnChange`: the HUD asks every frame, so a steady answer is one event), only when the player's eligibility (`0x10167320`) and `StealthKillDistMax > 0` pass. The event's entity is the victim candidate, the player when the ray found none | `<victim targetname or none> <admit\|refuse> gate=<gate>`; the gate is the first that refuses, or the last (`can_grapple`) on `admit`: `ray` (the forward `MASK_PLAYERSOLID` trace found no NPC), `valid_target` (`IsValidStealthKillTarget 0x102c2300`), `deaf_arc` (`InDeafArc 0x101be500` and not oblivious), `can_grapple` (`CanStartGrappleAttack(3) 0x103285a0`) |

An event whose producer does not exist yet in the port (a stub, an unbuilt chain) is simply never
emitted; the tap is added when the body lands. Lane B lists in its report which kinds have a live
tap and which wait for a body.

## The launch contract (lane A's headless run, lane B's `uv run elysium arena`)

Scenario records live at `<ProjectDir>/Arena/scenarios/**/*.json` (tracked text; the schema is
`Arena/README.md`, written by lane A). Lane B's launcher reads each record's `name`, `stage`,
`expect_fail`, `shares_map` and `seed` only.

Command line of the headless run (lane A parses, lane B passes), modelled on the `cast` harness's
launch in `unreal.py` (`-nullrhi -unattended -UseFixedTimeStep -FPS=<hz> -nosplash -nosound -stdout
-FullStdOutLogOutput`):

| Switch | Meaning |
|---|---|
| `-ElysiumArena` | arm the run |
| `-ArenaScenarios=<a,b,c>` | which records, by `name`; absent = every record of this host |
| `-ArenaOut=<absolute dir>` | where the report goes; absent = `Saved/Elysium/_arena/` |
| `-ArenaHz=<n>` | the fixed step (default 60; the launcher passes the same value as `-FPS=`) |
| `-ElysiumMap=<map>` | the map host: only records with `"stage": "map:<map>"` run; absent = the arena host, records with `"stage": "arena"` |
| `-ArenaSeed=<n>` | map boots only: the boot's first record's `seed` (default 0). Read only under `-ElysiumArena` (`FElysiumArenaRun::LaunchSeed`, the H17 harness door); New Game (`UElysiumSessionSubsystem::BeginNewGame`) seeds every `ElysiumRng` stream and `FMath::Rand`/`SRand` from it instead of the clock, so the map's activation replays. Absent = the clock, as the game does |

One boot runs one host. The launcher groups the requested records by stage and boots once per
stage, arena first; a map record boots alone unless it sets `shares_map`, and a shared boot is
seeded by its first record (each record still re-seeds from its own `seed` at its start).

Exit code: 0 when no scenario's result is `fail`, `unexpected-pass` or `error`; 1 otherwise or on a
harness error (no record matched, the stage never became ready).

`<out>/index.json`:

```json
{
  "host": "arena",
  "hz": 60,
  "scenarios": [
    {
      "name": "cover",
      "result": "pass | fail | expected-fail | unexpected-pass | error",
      "known_red": "",
      "error": "",
      "first_unmet": { "index": 2, "expect": { "who": "arena_gunman", "kind": "seqfinished", "match": "" }, "deadline": 14.0 },
      "game_seconds": 0.0,
      "wall_seconds": 0.0,
      "events": 0,
      "trace": "cover.trace.tsv"
    }
  ]
}
```

`result`: `pass` every expectation met, no `never` matched, every probe held; `fail` otherwise;
`expected-fail` a fail on a record with a non-empty `known_red`; `unexpected-pass` a pass on a record
with `known_red` (the red is fixed: remove the field); a record with `expect_fail: true` inverts
pass and fail (harness self-tests). `<out>/<name>.trace.tsv` is every event of the run: time, entity
name, kind, text.

`uv run elysium arena [names…]` writes to `$ELYSIUM_WORK_ROOT/reports/arena/<timestamp>/`, merges
the hosts' `index.json` files into one there, prints one line per scenario and the first unmet
expectation of each failure, and exits 7 on any `fail` / `unexpected-pass` / `error` (the test
command's verdict code).
