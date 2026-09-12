#pragma once

// The shared headless NPC world fixture. Nine `Elysium.Substrate.Npc*` suites (and a handful of
// inline preambles) each built their own copy of the same five lines — seed the RNG stream, stand
// an `FElysiumRecordingServices` + `FElysiumEntityWorld`, hand-assemble `FElysiumEntityDef` rows
// for NPCs/counters/wiring, `Load`/`SpawnPlayer`/`Activate`/`Tick(0.0)` the world, then fish the
// named entities back out by `FindByName`. This header is that preamble, factored into a builder
// (assembles the `FElysiumEntityDefs`) and a fixture (stands the world from one). What is NOT here
// is anything a single suite does on its own — a suite's own NPC list, keys, seed, counters and
// service knobs (`bProvideNpcMotor`, `bPlayerDucking`, light rigs, item catalogues, …) stay in the
// file that owns them; only the repeated plumbing moved.
//
// Pure refactor: this header changes no assertion, no test name and no observable sequencing. A
// suite's own fixture becomes a thin wrapper holding one `FElysiumNpcWorldFixture` plus its named
// entity pointers.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <initializer_list>

#include "ElysiumEntity.h"       // ELYSIUM_NEVER_THINK
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Misc/AssertionMacros.h"
#include "Substrate/ElysiumNpc.h"
#include "Templates/Function.h"
#include "Tests/ElysiumTestServices.h"

// Assembles one map's `FElysiumEntityDefs` the way every fixture's constructor body did by hand:
// an NPC row here, a counter there, an output wired between two rows by targetname. Seeds the NPC
// schedule RNG stream up front, the same way every fixture's first statement did, so a chance roll
// stays reproducible.
//
// `AddEntity`/`AddNpc`/`AddCounter` return a reference to the row just added so the caller can
// finish it off (`.Keys.Add(...)`) exactly as it would have on a local `FElysiumEntityDef` — but
// that reference is only good until the NEXT `Add*` call, because it is a `TArray` element and a
// further append may reallocate. Wire cross-entity outputs by targetname (`WireOutput`) instead of
// by reference; that lookup is safe at any point because it re-resolves the row each call.
struct FElysiumNpcWorldBuilder
{
	FElysiumEntityDefs Defs;

	FElysiumNpcWorldBuilder(const TCHAR* MapName, uint32 Seed)
	{
		ElysiumRng::SeedAll(Seed);
		Defs.MapName = MapName;
	}

	// Any entity: a `worldspawn`, an `events_world`, a `point_target` goal, a `prop_physics`, an
	// `aiscripted_schedule`, … Every fixture's non-NPC rows are this shape.
	FElysiumEntityDef& AddEntity(const TCHAR* Classname, const TCHAR* TargetName,
		const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef Def;
		Def.Classname = Classname;
		Def.TargetName = TargetName;
		Def.Origin = Origin;
		Defs.Defs.Add(MoveTemp(Def));
		return Defs.Defs.Last();
	}

	// The one row every NPC fixture repeats. `Classname` defaults to the combatant leaf almost
	// every suite here stands; a case that wants a different leaf (`npc_VHuman`, `npc_maker`, …)
	// states it.
	FElysiumEntityDef& AddNpc(const TCHAR* TargetName, const FVector& Origin = FVector::ZeroVector,
		const TCHAR* Classname = TEXT("npc_VHumanCombatant"))
	{
		return AddEntity(Classname, TargetName, Origin);
	}

	// A `math_counter`, the recording surface every I/O-wired case reads back as a number.
	FElysiumEntityDef& AddCounter(const TCHAR* Name)
	{
		return AddEntity(TEXT("math_counter"), Name);
	}

	FElysiumEntityDef* Find(const TCHAR* TargetName)
	{
		return Defs.Defs.FindByPredicate([TargetName](const FElysiumEntityDef& Def)
		{
			return Def.TargetName == TargetName;
		});
	}

	// The "Add 1" wiring every counter-driven case repeats: `SourceTargetName`'s `Output` fires
	// `Input Param` at `CounterTargetName`. `Input`/`Param` default to the counter increment every
	// existing case uses; a case that wants a different input (still "Add 1" on a different row
	// shape) can override them.
	void WireOutput(const TCHAR* SourceTargetName, const TCHAR* Output, const TCHAR* CounterTargetName,
		const TCHAR* Input = TEXT("Add"), const TCHAR* Param = TEXT("1"))
	{
		FElysiumEntityDef* Source = Find(SourceTargetName);
		check(Source != nullptr);
		FElysiumOutputDef Row;
		Row.Name = Output;
		Row.Target = CounterTargetName;
		Row.Input = Input;
		Row.Param = Param;
		Source->Outputs.Add(MoveTemp(Row));
	}
};

// Stands a world from a builder's defs: `Load`, `SpawnPlayer`, `Activate(0.0)`, `Tick(0.0)` — the
// four calls every fixture's constructor ended on. A suite's own fixture holds one of these plus
// its named entity pointers (`Guard`, `Player`, …), fetched with `Npc`/`Player` right after
// construction, exactly where every existing fixture fetched them.
struct FElysiumNpcWorldFixture
{
	FElysiumRecordingServices Services;
	FElysiumEntityWorld World;

	// The general form. `Configure` runs after `Services`/`World` exist but before `Load` spawns
	// anything, which is where a suite sets the service knobs a leaf's constructor reads at spawn
	// time (`bProvideNpcMotor`, `bNpcActivitiesResolve`, `bPlayerDucking`, an installed item
	// catalogue, …) — exactly where each fixture's own constructor body set them before building
	// its `FElysiumEntityDefs`.
	FElysiumNpcWorldFixture(FElysiumNpcWorldBuilder&& Builder,
		TFunctionRef<void(FElysiumRecordingServices&)> Configure)
		: World(nullptr, nullptr, Services.Bundle())
	{
		Configure(Services);
		World.Load(MoveTemp(Builder.Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
		World.Tick(0.0);
	}

	// The common case: nothing needs configuring before the world stands up.
	explicit FElysiumNpcWorldFixture(FElysiumNpcWorldBuilder&& Builder)
		: FElysiumNpcWorldFixture(MoveTemp(Builder), [](FElysiumRecordingServices&) {})
	{
	}

	FElysiumNpcWorldFixture(const FElysiumNpcWorldFixture&) = delete;
	FElysiumNpcWorldFixture& operator=(const FElysiumNpcWorldFixture&) = delete;

	// Non-const because `FElysiumEntityWorld::FindByName` is a non-const name walk.
	FElysiumNpc* Npc(const TCHAR* Name)
	{
		FElysiumEntity* Entity = World.FindByName(Name);
		return Entity ? Entity->AsNpc() : nullptr;
	}

	FElysiumPlayer* Player() const { return World.FindPlayer(); }

	// The mind's/entity's inspector row is the read side a Substrate case asserts through, the same
	// surface a developer would look at. Shared because three suites (`NpcEnemy`, `NpcSenses`,
	// `AiScriptedSchedule`) each wrote this exact loop.
	static FString Debug(const FElysiumEntity* Entity, const TCHAR* Key)
	{
		if (Entity == nullptr)
		{
			return FString();
		}
		TArray<TPair<FString, FString>> Rows;
		Entity->GetDebugState(Rows);
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == Key)
			{
				return Row.Value;
			}
		}
		return FString();
	}

	// A wired `math_counter`'s accumulated value, or -1 when the name does not resolve — the
	// `OnFoundEnemy`/`OnDamaged`/... wiring's read side.
	float Counter(const TCHAR* Name)
	{
		const FElysiumEntity* Entity = World.FindByName(Name);
		return Entity ? FCString::Atof(*Debug(Entity, TEXT("Value"))) : -1.f;
	}

	// Nothing here wants an NPC's own think competing with the pass a case is driving. That is
	// retail's `m_bDisableAI` (+0x6080), which `NPCThink` tests immediately after clearing
	// `SCHEDULE_CHANGED` -- and stating it that way matters now that `NextThink` is the think
	// cadence's own output: a pinned think is a lie about the clock, a disabled AI is a fact about
	// the NPC. `Wake` is the other half, for a case that then wants exactly one real think.
	static void Quiet(std::initializer_list<FElysiumNpc*> Npcs)
	{
		for (FElysiumNpc* Npc : Npcs)
		{
			if (Npc != nullptr)
			{
				Npc->SetDisableAi(true);
			}
		}
	}

	// Hand the AI back and put all four clocks on `Now`, which is slot 614's own effect
	// (`ResetThinkTimers` `0x102c23f0`). A case that used to force a think by writing
	// `NextThink = 0` says this instead.
	static void Wake(std::initializer_list<FElysiumNpc*> Npcs, double Now)
	{
		for (FElysiumNpc* Npc : Npcs)
		{
			if (Npc != nullptr)
			{
				Npc->SetDisableAi(false);
				Npc->ResetThinkTimers(Now);
			}
		}
	}

	// Step the world to `To` at frame granularity. The think cadence's due test is
	// `(stamp - Now) <= FrameSeconds()`, so a case that jumped the clock in one call would run
	// every think on the clamped 0.1 s epsilon and could never observe a stamp being skipped.
	void Advance(double To, double StepSeconds = ElysiumWorldClock::DefaultFrameSeconds)
	{
		double Now = World.NowSeconds();
		while (Now + StepSeconds < To)
		{
			Now += StepSeconds;
			World.Tick(Now);
		}
		if (Now < To)
		{
			World.Tick(To);
		}
	}

	// The Source health ceiling the damage predicates read, plus a cleared schedule — the opt-in
	// seeding a case performs before driving a schedule/interrupt pass directly rather than through
	// admission. A headless world has no rulebook behind `SeedSheet`, so it is stated here rather
	// than derived.
	static void PrepareForKernelDrive(FElysiumNpc* Npc, int32 MaxHealth = 100)
	{
		if (Npc != nullptr)
		{
			Npc->MaxHealth = MaxHealth;
			Npc->Schedule.Clear();
		}
	}
};

#endif // WITH_DEV_AUTOMATION_TESTS
