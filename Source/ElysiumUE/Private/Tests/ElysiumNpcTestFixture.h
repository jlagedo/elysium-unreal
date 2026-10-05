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
#include "ElysiumMapPlaces.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveTypes.h"
#include "Misc/AssertionMacros.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Templates/Function.h"
#include "Tests/ElysiumTestServices.h"
#include "Tests/ElysiumNpcTestCensus.h"

// `Entity` as species class `T` (`FElysiumNpc::AsSpecies`), or null when it is not an NPC of that
// class: the typed view a case takes of a fixture NPC before it reaches a species member.
template <class T>
T* ElysiumTestAsSpecies(FElysiumEntity* Entity)
{
	FElysiumNpc* Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
	return Npc != nullptr ? Npc->AsSpecies<T>() : nullptr;
}

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
	// The map's AI network (0018 story 4's place set), adopted BEFORE `Load` so the node rows the
	// defs author bind their hints to it exactly as a baked map's do (`CNodeEnt::Spawn`'s counter).
	// Empty is a world with no network, which is every fixture written before the place set.
	TArray<FElysiumPlaceRow> Places;
	TArray<FElysiumPlaceWanderCap> WanderCaps;
	// The network's crosswalk pairs (0018 story 7) and each one's hull-0 motion word, parallel.
	TArray<FIntPoint> CrosswalkPairs;
	TArray<int32> CrosswalkMotions;

	// One crosswalk pair joining two places `AddPlace` made: the link a crosswalk hint's
	// `Walk` / `DontWalk` writes. `Motion` is its hull-0 word -- 1 a walk, 2 jump-only. Returns the
	// pair's index. The two `info_node_crosswalk` rows that bind the nodes are the case's own
	// (`AddEntity`, in node order, so `CNodeEnt::Spawn`'s counter lands them on the nodes).
	int32 AddCrosswalkPair(int32 NodeA, int32 NodeB, int32 Motion = 1)
	{
		CrosswalkMotions.Add(Motion);
		return CrosswalkPairs.Add(FIntPoint(FMath::Min(NodeA, NodeB), FMath::Max(NodeA, NodeB)));
	}

	// One synthetic place: a node of `Type` at `OriginCm`, every hull's Z offset zero. Returns its
	// network index.
	int32 AddPlace(int32 Type, const FVector& OriginCm, float YawDeg = 0.0f)
	{
		FElysiumPlaceRow Row;
		Row.Type = Type;
		Row.OriginCm = OriginCm;
		Row.YawDeg = YawDeg;
		return Places.Add(Row);
	}

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
		FElysiumEntityDef& Def = AddEntity(Classname, TargetName, Origin);
		// Every NPC a shipped map stands carries a `stattemplate`: 2055 of the 2060 `npc_*` entities
		// in the exported `.ents` author one, and the five that do not are `npc_VNewscaster`. It is
		// not decoration — `CAI_BaseNPCTroika::NPCInit` (`1029a4a2`) sets `m_bIsBCCTargetable` only
		// when the string is non-empty, and an NPC without that byte is invisible to the sight and
		// relation gates that read it. A fixture NPC therefore carries one too, so a case is
		// standing what the game stands.
		Def.Keys.Add(TEXT("stattemplate"), TEXT("Thug"));
		return Def;
	}

	// A bare `CAI_BaseNPCTroika`: the Troika line with no species class over it. No classname
	// factory builds one (the descriptor is abstract), so it is stood by internal construction
	// (`FElysiumEntityDef::InternalFactory`), the way retail builds a class by code. A case that
	// asserts the Troika-line body with no species override in the way stands this, never a
	// species classname standing in for it (story 5 step 2).
	FElysiumEntityDef& AddTroikaNpc(const TCHAR* TargetName, const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef& Def = AddNpc(TargetName, Origin, TEXT("CAI_BaseNPCTroika"));
		Def.InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FElysiumNpc>(); };
		return Def;
	}

	// The NPC of retail class `RetailClass` (`CNPC_VWerewolf`), built by the classname retail's
	// factory builds it from (the census's classname for the class). `CAI_BaseNPCTroika`, or null,
	// stands the bare Troika line. A class no classname builds (`CNPC_VBaseBoss`, a deferred fold)
	// has no answer here and fails the case at the builder.
	FElysiumEntityDef& AddNpcOfClass(const TCHAR* TargetName, const FVector& Origin,
		const TCHAR* RetailClass)
	{
		if (RetailClass == nullptr || FCString::Strcmp(RetailClass, TEXT("CAI_BaseNPCTroika")) == 0)
		{
			return AddTroikaNpc(TargetName, Origin);
		}
		return AddNpc(TargetName, Origin, ClassnameOf(RetailClass));
	}

	// The classname retail's factory builds `RetailClass` from. A class no classname builds answers
	// an empty classname: the row stands as an inert record, so the case fails on its own missing NPC
	// (an `ensure`, not a `check`, so one bad row does not stop the whole automation run).
	static const TCHAR* ClassnameOf(const TCHAR* RetailClass)
	{
		const FElysiumNpcClass* Row = ElysiumNpcTestCensus::Find(RetailClass);
		if (!ensureMsgf(Row != nullptr && Row->ClassnameCount > 0, TEXT("%s: no classname builds it"),
			RetailClass))
		{
			return TEXT("");
		}
		return Row->Classnames[0];
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

// Round-trip one world's state onto another through the REAL persistence path: the registry's
// named field walk, then the leaf blob, then the restore hook, in the order `ApplyEntityRecord`
// runs them.
//
// A case that reaches for `Npc->Serialize(Ar)` instead is testing the leaf blob alone, and since
// 0019/2 pass C the leaf blob is no longer where most NPC state lives -- the generated datamap SAVE
// walk carries it, `ApplySnapshot` is what drives that walk, and `OnPostRestore` (retail's slot 130)
// is what re-derives from it. So this is the only call that exercises what a real save does.
inline void ElysiumRoundTripSnapshot(FElysiumEntityWorld& From, FElysiumEntityWorld& To)
{
	FElysiumMapSnapshot Snapshot;
	From.Freeze(Snapshot);
	To.ApplySnapshot(Snapshot);
}

// The AI network a hand-built world's patrol points bind to (0018 story 4): one ground node at each
// origin, in order, adopted BEFORE `Load` so the node rows take them in BSP order the way a baked
// map's do. Every hull's Z offset is zero, so a node's position is exactly the origin given.
inline void ElysiumAdoptPlacesAt(FElysiumEntityWorld& World, std::initializer_list<FVector> OriginsCm)
{
	TArray<FElysiumPlaceRow> Rows;
	for (const FVector& Origin : OriginsCm)
	{
		FElysiumPlaceRow& Row = Rows.AddDefaulted_GetRef();
		Row.Type = 2;
		Row.OriginCm = Origin;
	}
	World.Places().AdoptRows(MoveTemp(Rows));
}

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
		if (Builder.Places.Num() > 0)
		{
			World.Places().AdoptRows(MoveTemp(Builder.Places), 0, MoveTemp(Builder.WanderCaps),
				MoveTemp(Builder.CrosswalkPairs), MoveTemp(Builder.CrosswalkMotions));
		}
		ElysiumStandSpawnClock(World, -FElysiumNpcBase::NpcInitThinkDelay);
		World.Load(MoveTemp(Builder.Defs));
		World.SpawnPlayer();
		// The map stands up a tenth of a second BEFORE the case's zero. `CAI_BaseNPCTroika::NPCInit`
		// (`0x1029a0b0`) runs inside `Spawn` (`0x10299057`) and its first-second arm puts the first
		// think at `curtime + 0.1` (`_DAT_104493d0`), not at curtime — an NPC that has not reached
		// that stamp has not run `StartNPC`, is still in `NPC_STATE_NONE` (`1029a0f5`) and has not
		// passed this runtime's admission barrier, so it refuses every body claim. Spawning (and
		// activating) at `-0.1` is the honest way to say "the map has been up for a tenth of a
		// second": the retail delay is kept intact and the first think lands on the frame at zero,
		// which is the clock every case measures its absolute stamps from.
		World.Activate(-FElysiumNpcBase::NpcInitThinkDelay);
		World.Tick(0.0);
		// These arm worlds drive already-idle actors at the case's zero. 0x1029a0f5 now
		// survives admission, so supply that precondition explicitly through SetState
		// (0x1026e340); initialization tests re-run NPCInit to inspect its NONE state.
		for (const TUniquePtr<FElysiumEntity>& FixtureEntity : World.Entities())
		{
			FElysiumNpc* FixtureNpc = FixtureEntity ? FixtureEntity->AsNpc() : nullptr;
			if (FixtureNpc != nullptr && FixtureNpc->NpcStateRetail() == 0) FixtureNpc->SetState(1);
		}
	}

	// The clock at which the NPC's first think falls — the frame this fixture ends on.
	static constexpr double FirstThinkSeconds = 0.0;

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

	// The named NPC as species class `T`, or null when it is not one (`FElysiumNpc::AsSpecies`).
	// (`ElysiumTestAsSpecies` does the same for any entity pointer.)
	template <class T>
	T* NpcAs(const TCHAR* Name)
	{
		FElysiumNpc* Found = Npc(Name);
		return Found ? Found->AsSpecies<T>() : nullptr;
	}

	// The live NPC whose own C++ class is retail class `RetailClass` (exactly, not a descendant), the
	// `Nth` in entity-list order. For a body whose `Spawn` renames it the targetname no longer finds
	// it: the player controller line's `SetName("playercontroller")` (`0x103a4510`, story 5 fold A2).
	FElysiumNpc* NpcOfClass(const TCHAR* RetailClass, int32 Nth = 0)
	{
		const FElysiumNpcClass* Wanted = ElysiumNpcTestCensus::Find(RetailClass);
		for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
		{
			FElysiumNpc* Npc = Entity.IsValid() && !Entity->IsDead() ? Entity->AsNpc() : nullptr;
			if (Npc != nullptr && Wanted != nullptr && Npc->RetailClass() == Wanted && Nth-- == 0)
			{
				return Npc;
			}
		}
		return nullptr;
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

	// Slot 433 `GatherConditions` (story 8 wave 2: the retail pass replaced the port's
	// `ElysiumNpcEnemy::GatherConditions(Npc, Now)`). The retail bodies read `curtime`, which is the
	// world's clock; `Now` names the case's intended stamp and is not pushed onto the world (a tick
	// would run the world's own frame work -- motor sampling, other thinks -- over what the case set
	// up). A case that needs the clock moved advances the world itself. Like retail, the pass clears
	// nothing it does not own: a bit no lane re-derives stands until `SetSchedule` zeroes the word.
	static void GatherConditionsAt(FElysiumNpc& Npc, double Now)
	{
		(void)Now;
		Npc.GatherConditions();
	}

	// The same pass with the world's clock first ticked forward to `Now` (quiet NPCs think nothing
	// on that tick), for a case whose stimuli are timed: record expiry, the hearing cadence.
	static void GatherConditionsTickedTo(FElysiumNpc& Npc, double Now)
	{
		if (Npc.World != nullptr && Npc.World->NowSeconds() < Now)
		{
			Npc.World->Tick(Now);
		}
		Npc.GatherConditions();
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
