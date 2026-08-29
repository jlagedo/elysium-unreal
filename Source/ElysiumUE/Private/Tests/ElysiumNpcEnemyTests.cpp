// Content-free Substrate automation: the NPC decision pass — condition gathering, the enemy
// transaction, the schedule interrupt gate, and the two-layer ideal-state selection.
//
// Everything under test is substrate rule. The engine contributes one world term (is the segment
// between two points clear), which the recording services script, and the NPC schedule RNG stream
// is seeded per fixture so a chance roll is reproducible rather than sampled.
//
// The authored facts: `docs/vtmb/npc-ai-reverse-engineering.md` -> "Enemy acquisition and
// replacement", "Interrupt conditions", "The idle branch, decided" and "`no_alert_state` does not
// suppress the alert state"; `docs/vtmb/combat-and-damage.md` -> "NPC damage response and stagger
// boundaries".

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumTestServices.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumNpcEnemyTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	float Cm(float Units) { return Units * ElysiumMove::U; }

	// One guard facing +X at the origin, two other combat characters the cases place, the player,
	// and four counters wired off the outputs this cycle owns. `vision`/`hearing` are authored so
	// the derive path stays out of every case that is not about it.
	struct FEnemyFixture
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* ThugA = nullptr;
		FElysiumNpc* ThugB = nullptr;
		FElysiumPlayer* Player = nullptr;

		FEnemyFixture()
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x454E454D);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__npcenemy_test__");

			FElysiumEntityDef GuardDef;
			GuardDef.Classname = TEXT("npc_VHumanCombatant");
			GuardDef.TargetName = TEXT("guard");
			GuardDef.Origin = FVector::ZeroVector;
			GuardDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
			auto Wire = [&GuardDef](const TCHAR* Output, const TCHAR* Counter)
			{
				FElysiumOutputDef Row;
				Row.Name = Output;
				Row.Target = Counter;
				Row.Input = TEXT("Add");
				Row.Param = TEXT("1");
				GuardDef.Outputs.Add(MoveTemp(Row));
			};
			Wire(TEXT("OnFoundEnemy"),  TEXT("c_foundenemy"));
			Wire(TEXT("OnFoundPlayer"), TEXT("c_foundplayer"));
			Wire(TEXT("OnLostEnemy"),   TEXT("c_lostenemy"));
			Wire(TEXT("OnLostPlayer"),  TEXT("c_lostplayer"));
			Defs.Defs.Add(MoveTemp(GuardDef));

			for (const TCHAR* Name : { TEXT("thug_a"), TEXT("thug_b") })
			{
				FElysiumEntityDef Thug;
				Thug.Classname = TEXT("npc_VHumanCombatant");
				Thug.TargetName = Name;
				Thug.Origin = FVector(Cm(500.f), 0.0, 0.0);
				Defs.Defs.Add(MoveTemp(Thug));
			}

			for (const TCHAR* Name : { TEXT("c_foundenemy"), TEXT("c_foundplayer"),
				TEXT("c_lostenemy"), TEXT("c_lostplayer") })
			{
				FElysiumEntityDef Counter;
				Counter.Classname = TEXT("math_counter");
				Counter.TargetName = Name;
				Defs.Defs.Add(MoveTemp(Counter));
			}

			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);

			Guard = static_cast<FElysiumNpc*>(World.FindByName(TEXT("guard")));
			ThugA = static_cast<FElysiumNpc*>(World.FindByName(TEXT("thug_a")));
			ThugB = static_cast<FElysiumNpc*>(World.FindByName(TEXT("thug_b")));
			Player = World.FindPlayer();
			if (Guard)
			{
				// The Source health ceiling the damage predicates read. A headless world has no
				// rulebook behind `SeedSheet`, so it is stated here rather than derived.
				Guard->MaxHealth = 100;
				// Whatever the admission think may have selected is not this case's setup: every
				// schedule under test is started explicitly.
				Guard->Schedule.Clear();
			}
			Quiet();
		}

		// Nothing here wants an NPC's own think competing with the pass the case is driving.
		void Quiet()
		{
			for (FElysiumNpc* Npc : { Guard, ThugA, ThugB })
			{
				if (Npc)
				{
					Npc->NextThink = ELYSIUM_NEVER_THINK;
				}
			}
		}

		void Flush(double Now)
		{
			Quiet();
			World.Tick(Now);
		}

		void Hate(const FElysiumEntity* Target, int32 Priority)
		{
			if (Guard && Target)
			{
				Guard->Relationships.SetEntity(Target->Handle, EElysiumRelationship::Hate, Priority);
			}
		}

		void Fear(const FElysiumEntity* Target, int32 Priority)
		{
			if (Guard && Target)
			{
				Guard->Relationships.SetEntity(Target->Handle, EElysiumRelationship::Fear, Priority);
			}
		}

		// The mind's state is private to the leaf; its inspector row is the read side everything
		// else uses, so a test asserts through the same surface a developer would look at.
		FString Debug(const FElysiumEntity* Entity, const TCHAR* Key) const
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

		float Counter(const TCHAR* Name)
		{
			const FElysiumEntity* Ent = World.FindByName(Name);
			return Ent ? FCString::Atof(*Debug(Ent, TEXT("Value"))) : -1.f;
		}
	};

	// A handle bound to a real index in a world generation that no longer exists: the "went null"
	// case, produced without killing anything (a killed entity is still an actor, and the
	// transaction has to tell the two apart).
	FElysiumEntityHandle StaleHandle(const FElysiumEntityHandle& Live)
	{
		return FElysiumEntityHandle(Live.Index, Live.Epoch + 1);
	}

	// The minimum a schedule kernel tick needs. The interrupt cases assert the kernel, not a body.
	struct FKernelRunner final : IElysiumScheduleRunner
	{
		TArray<FString> Calls;
		bool bVisible = false;   // WAIT_PVS holds, so a program stays mid-flight

		virtual float RunSpecialIdleActivity(double) override
		{
			Calls.Add(TEXT("SpecialIdleActivity"));
			return 2.f;
		}
		virtual bool IsBodyVisible() const override { return bVisible; }
		virtual float PlayActivity(const FString& Activity) override
		{
			Calls.Add(FString::Printf(TEXT("SetActivity %s"), *Activity));
			return 1.f;
		}
		virtual float RandomSeconds(float Max) override { return Max; }
		virtual void RecordScheduleEvent(const FString& Row) override
		{
			Calls.Add(FString::Printf(TEXT("trace: %s"), *Row));
		}
		bool Saw(const TCHAR* Needle) const
		{
			return Calls.ContainsByPredicate([Needle](const FString& C) { return C.Contains(Needle); });
		}
	};
}


// The recovered `GatherConditions` order: a stale enemy's death is seen by ChooseEnemy in
// the SAME pass, and the committed-enemy conditions describe the enemy that pass chose.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyGatherOrderTest,
	"Elysium.Substrate.NpcEnemy.GatherOrder", GElysiumTestFlags)
bool FElysiumNpcEnemyGatherOrderTest::RunTest(const FString&)
{
	FEnemyFixture F;
	if (!TestNotNull(TEXT("the guard leaf constructs"), F.Guard)
		|| !TestNotNull(TEXT("thug A exists"), F.ThugA)
		|| !TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}

	// Both hostile; the guard is already committed to thug A, who then dies.
	F.Hate(F.ThugA, 5);
	F.Hate(F.Player, 5);
	F.Guard->Senses.Memory.Enemy = F.ThugA->Handle;
	F.ThugA->bDead = true;

	ElysiumNpcEnemy::GatherConditions(*F.Guard, 10.0);

	const FElysiumNpcConditions& Cond = F.Guard->Cognition.Conditions;
	TestTrue(TEXT("the dead committed enemy raises ENEMY_DEAD in the same pass"),
		Cond.Has(EElysiumNpcCond::EnemyDead));
	TestTrue(TEXT("...and the pass replaces it rather than carrying a corpse"),
		F.Guard->Senses.Memory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...raising NEW_ENEMY"), Cond.Has(EElysiumNpcCond::NewEnemy));
	TestTrue(TEXT("the old handle went through the last-enemy path"),
		F.Guard->Senses.Memory.LastEnemy == F.ThugA->Handle);

	// Step 5 gathers the committed enemy's own conditions AFTER the choice, so they describe the
	// player and not the corpse: nothing has failed a LOS check for the new target yet.
	TestTrue(TEXT("the committed-enemy conditions describe the NEW enemy"),
		Cond.Has(EElysiumNpcCond::HaveEnemyLos));
	TestFalse(TEXT("...and ENEMY_UNREACHABLE is never set — it has no producer"),
		Cond.Has(EElysiumNpcCond::EnemyUnreachable));

	// The pass clock advanced, which is what makes a stimulus edge-triggered.
	TestTrue(TEXT("the pass stamps its own clock"), F.Guard->Cognition.GatheredAt == 10.0);
	return true;
}


// `ShouldChooseNewEnemy`: the trigger set, and the deliberate SEE_FEAR omission.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyShouldChooseTest,
	"Elysium.Substrate.NpcEnemy.ShouldChoose", GElysiumTestFlags)
bool FElysiumNpcEnemyShouldChooseTest::RunTest(const FString&)
{
	FEnemyFixture F;
	if (F.Guard == nullptr || F.ThugA == nullptr || F.Player == nullptr)
	{
		return false;
	}
	const FElysiumNpcConditions Empty;

	// No enemy at all: search.
	TestTrue(TEXT("no current enemy searches immediately"),
		ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, Empty));

	// A living, non-eluded enemy with none of the trigger conditions stays selected.
	F.Guard->Senses.Memory.Enemy = F.ThugA->Handle;
	TestFalse(TEXT("a living, non-eluded enemy is sticky"),
		ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, Empty));

	// The four trigger conditions.
	for (const EElysiumNpcCond Trigger : { EElysiumNpcCond::SeeHate, EElysiumNpcCond::SeeDislike,
		EElysiumNpcCond::SeeNemesis, EElysiumNpcCond::EnemyDead })
	{
		const FElysiumNpcConditions One = FElysiumNpcConditions::Of({ Trigger });
		TestTrue(*FString::Printf(TEXT("%s triggers a search"), ElysiumNpcCondName(Trigger)),
			ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, One));
	}

	// SEE_FEAR is NOT one of them. The retail body does not test it, although a `D_FR` candidate is
	// eligible in `BestEnemy` — so a fear relation can be chosen but never triggers a choice. This
	// assertion is the whole reason the omission is safe to keep.
	const FElysiumNpcConditions Fearful = FElysiumNpcConditions::Of({ EElysiumNpcCond::SeeFear });
	TestFalse(TEXT("SEE_FEAR deliberately does NOT trigger a search"),
		ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, Fearful));

	// A dead enemy and an eluded one both search, with no condition at all.
	F.ThugA->bDead = true;
	TestTrue(TEXT("a dead enemy searches"),
		ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, Empty));
	F.ThugA->bDead = false;
	F.Guard->Senses.Memory.bEnemyEluded = true;
	TestTrue(TEXT("an eluded enemy searches"),
		ElysiumNpcEnemy::ShouldChooseNewEnemy(*F.Guard, Empty));
	return true;
}


// The starvation rule: the active schedule's interrupt mask is consulted BEFORE any
// search, and an uninterested schedule keeps ownership of the enemy it has.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyScheduleGateTest,
	"Elysium.Substrate.NpcEnemy.ScheduleGate", GElysiumTestFlags)
bool FElysiumNpcEnemyScheduleGateTest::RunTest(const FString&)
{
	// --- An uninterested schedule keeps its enemy against a strictly better candidate -------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr || F.ThugB == nullptr)
		{
			return false;
		}
		F.Hate(F.ThugA, 1);
		F.Hate(F.ThugB, 9);   // strictly better on step 2 of the arbitration
		F.Guard->Senses.Memory.Enemy = F.ThugA->Handle;

		// `SCHED_TROIKA_MELEE_ATTACK1_SWING` is the one program whose EMPTY mask is recovered rather
		// than merely undecoded: "once that terminal attack task owns the NPC it is not reevaluated
		// as a fresh attack choice each tick". It is therefore the honest driver for the starvation
		// rule — the two idle programs now carry a mask, which is what makes live acquisition work.
		TestTrue(TEXT("the terminal swing program starts"),
			ElysiumSchedule::Start(F.Guard->Schedule, EElysiumScheduleId::MeleeAttack1Swing, *F.Guard));
		if (const FElysiumSchedule* Swing = ElysiumScheduleFor(EElysiumScheduleId::MeleeAttack1Swing))
		{
			TestTrue(TEXT("...and its recovered mask really is empty"), Swing->Interrupts.IsEmpty());
		}

		FElysiumNpcConditions Cond = FElysiumNpcConditions::Of({ EElysiumNpcCond::SeeHate });
		TestFalse(TEXT("an uninterested schedule skips the search entirely"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 10.0));
		TestTrue(TEXT("...and keeps the enemy it already had"),
			F.Guard->Senses.Memory.Enemy == F.ThugA->Handle);

		// The disposition idle's own registered mask admits `NEW_ENEMY`, so the same pass under it
		// takes the better candidate — no test-only mask installed.
		TestTrue(TEXT("the idle program starts"),
			ElysiumSchedule::Start(F.Guard->Schedule, EElysiumScheduleId::IdleDisposition, *F.Guard));
		TestTrue(TEXT("a schedule that admits NEW_ENEMY lets the replacement through"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 11.0));
		TestTrue(TEXT("...and the higher-priority candidate wins"),
			F.Guard->Senses.Memory.Enemy == F.ThugB->Handle);
	}

	// --- An NPC running no program at all is interested in everything -----------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr)
		{
			return false;
		}
		F.Hate(F.ThugA, 5);
		F.Guard->Schedule.Clear();
		FElysiumNpcConditions Cond;
		TestTrue(TEXT("with no schedule running the gate is open"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 10.0));
		TestTrue(TEXT("...so a first enemy can be acquired at all"),
			F.Guard->Senses.Memory.Enemy == F.ThugA->Handle);
	}

	// --- A null enemy under an uninterested schedule warns, once ---------------------------------
	{
		AddExpectedError(TEXT("does not interrupt on LOST_ENEMY"),
			EAutomationExpectedErrorFlags::Contains, 1);

		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr)
		{
			return false;
		}
		F.Guard->Senses.Memory.Enemy = StaleHandle(F.ThugA->Handle);
		TestTrue(TEXT("the idle program starts"),
			ElysiumSchedule::Start(F.Guard->Schedule, EElysiumScheduleId::IdleDisposition, *F.Guard));

		FElysiumNpcConditions Cond;
		for (int32 i = 0; i < 4; ++i)
		{
			// Four passes, one warning: the latch is per NPC per schedule, and `AddExpectedError`
			// above asserts the count rather than merely tolerating it.
			TestFalse(TEXT("the search stays skipped every pass"),
				ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 10.0 + i));
		}
		TestTrue(TEXT("the enemy handle is left alone rather than given a plausible fallback"),
			F.Guard->Senses.Memory.Enemy.IsSet());
	}
	return true;
}


// `BestEnemy`: eligibility and the four arbitration rules.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyBestEnemyTest,
	"Elysium.Substrate.NpcEnemy.BestEnemy", GElysiumTestFlags)
bool FElysiumNpcEnemyBestEnemyTest::RunTest(const FString&)
{
	// --- Eligibility: only D_HT and D_FR, never self, never a dead or eluded actor ----------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr || F.ThugB == nullptr || F.Player == nullptr)
		{
			return false;
		}
		TestFalse(TEXT("a table with no hostile row yields no enemy"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard).IsSet());

		F.Guard->Relationships.SetEntity(F.ThugA->Handle, EElysiumRelationship::Like, 5);
		F.Guard->Relationships.SetEntity(F.ThugB->Handle, EElysiumRelationship::Neutral, 5);
		TestFalse(TEXT("D_LI and D_NU are not eligible"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard).IsSet());

		// D_FR IS eligible, even though SEE_FEAR does not trigger a search.
		F.Fear(F.ThugA, 5);
		TestTrue(TEXT("a D_FR candidate is eligible"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugA->Handle);

		// Self is never a candidate, whatever the table says.
		F.Guard->Relationships.SetEntity(F.Guard->Handle, EElysiumRelationship::Hate, 99);
		TestTrue(TEXT("self is excluded regardless of its own row"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugA->Handle);

		// A dead actor drops out.
		F.ThugA->bDead = true;
		TestFalse(TEXT("a dead candidate is not a living actor"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard).IsSet());
		F.ThugA->bDead = false;

		// The eluded marker excludes its own target.
		F.Guard->Senses.Memory.Enemy = F.ThugA->Handle;
		F.Guard->Senses.Memory.bEnemyEluded = true;
		TestFalse(TEXT("an eluded target is excluded"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard).IsSet());
	}

	// --- Priority beats distance, and distance breaks a priority tie ------------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr || F.ThugB == nullptr)
		{
			return false;
		}
		// The rows below only ever move UP in priority: `SetEntity` refuses a rewrite below the
		// installed priority, which is the relationship table's own determinism rule.
		F.ThugA->Origin = FVector(Cm(100.f), 0.0, 0.0);    // near
		F.ThugB->Origin = FVector(Cm(2000.f), 0.0, 0.0);   // far

		// Equal priority: the nearer candidate wins.
		F.Hate(F.ThugA, 5);
		F.Hate(F.ThugB, 5);
		TestTrue(TEXT("at equal priority the smaller integer distance wins"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugA->Handle);

		// Priority outranks distance outright.
		F.Hate(F.ThugB, 9);
		TestTrue(TEXT("larger IRelationPriority beats smaller integer distance"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugB->Handle);

		// The raw integer is used unclamped: the corpus writes 0 and 99, and the diagnostic 1-10
		// range is not a rule.
		F.Hate(F.ThugA, 99);
		TestTrue(TEXT("an out-of-diagnostic-range priority still orders"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugA->Handle);
	}

	// --- Visibility modifies the distance comparison ----------------------------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr || F.Player == nullptr)
		{
			return false;
		}
		// The player is far and SEEN; the thug is near and unseen. Same priority.
		F.Player->Origin = FVector(Cm(2000.f), 0.0, 0.0);
		F.ThugA->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Hate(F.Player, 5);
		F.Hate(F.ThugA, 5);
		F.Guard->Senses.TickSight(*F.Guard, 10.0);
		TestTrue(TEXT("the sight pass sees the player"), F.Guard->Senses.Memory.bPlayerLos);

		TestTrue(TEXT("a visible candidate displaces a nearer unseen incumbent"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.Player->Handle);

		// With the player unseen, the plain distance rule returns: the nearer thug wins.
		F.Player->Origin = FVector(Cm(-2000.f), 0.0, 0.0);   // behind the guard, out of cone
		F.Guard->Senses.TickSight(*F.Guard, 20.0);
		TestFalse(TEXT("the player is no longer seen"), F.Guard->Senses.Memory.bPlayerLos);
		TestTrue(TEXT("between two unseen candidates the nearer one wins"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugA->Handle);

		// And a closer UNSEEN candidate does not displace a visible incumbent. The extra candidate
		// is spawned at runtime so it is arbitrated AFTER the player in entity-list order, which is
		// the only ordering in which this rule is distinguishable from the one above.
		F.Player->Origin = FVector(Cm(2000.f), 0.0, 0.0);
		F.Guard->Senses.TickSight(*F.Guard, 30.0);
		TestTrue(TEXT("the player is visible again"), F.Guard->Senses.Memory.bPlayerLos);

		FElysiumEntityDef LateDef;
		LateDef.Classname = TEXT("npc_VHumanCombatant");
		LateDef.TargetName = TEXT("thug_late");
		LateDef.Origin = FVector(Cm(50.f), 0.0, 0.0);
		const FElysiumEntityHandle Late = F.World.SpawnRuntimeEntity(MoveTemp(LateDef));
		FElysiumEntity* LateEntity = F.World.Resolve(Late);
		if (!TestNotNull(TEXT("the late candidate spawned"), LateEntity))
		{
			return false;
		}
		LateEntity->NextThink = ELYSIUM_NEVER_THINK;
		F.Hate(LateEntity, 5);
		TestTrue(TEXT("a closer unseen candidate does NOT displace a visible incumbent"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.Player->Handle);
	}
	return true;
}


// `SetEnemy` and the `ChooseEnemy` effects: the last-enemy transfer, NEW_ENEMY, the
// forgotten LOS claim, and the two lost-the-actor outputs.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemySetEnemyTest,
	"Elysium.Substrate.NpcEnemy.SetEnemy", GElysiumTestFlags)
bool FElysiumNpcEnemySetEnemyTest::RunTest(const FString&)
{
	FEnemyFixture F;
	if (F.Guard == nullptr || F.ThugA == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);

	// One acquisition episode against the player: the found edge fires once and latches.
	F.Guard->Senses.Memory.Enemy = F.Player->Handle;
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 1.0);
	F.Flush(1.0);
	TestEqual(TEXT("the first committed-enemy LOS fires OnFoundEnemy"),
		F.Counter(TEXT("c_foundenemy")), 1.f);
	TestTrue(TEXT("...and latches"), F.Guard->Senses.Memory.bEnemyLosLatched);
	const int32 SightingsAfterFirst = F.Guard->EnemySightings;
	TestEqual(TEXT("...and counts one enemy sighting, because the enemy is the player"),
		SightingsAfterFirst, 1);

	// A replacement transfers the old handle and clears the episode.
	F.Guard->Senses.Memory.EnemyLosFailures = 3;
	F.Guard->Senses.Memory.bEnemyOccluded = true;
	ElysiumNpcEnemy::SetEnemy(*F.Guard, F.ThugA->Handle);
	TestTrue(TEXT("the old handle goes through the last-enemy path"),
		F.Guard->Senses.Memory.LastEnemy == F.Player->Handle);
	TestTrue(TEXT("the new handle is committed"),
		F.Guard->Senses.Memory.Enemy == F.ThugA->Handle);
	TestFalse(TEXT("the previous LOS claim is forgotten"),
		F.Guard->Senses.Memory.bEnemyLosLatched);
	TestEqual(TEXT("...along with its debounce"), F.Guard->Senses.Memory.EnemyLosFailures, 0);
	TestFalse(TEXT("...and its occlusion flag"), F.Guard->Senses.Memory.bEnemyOccluded);

	// Which is what lets the found edge fire again for the NEW enemy rather than being swallowed.
	F.Guard->Senses.GatherEnemyLos(*F.Guard, 2.0);
	F.Flush(2.0);
	TestEqual(TEXT("the new enemy gets its own found edge"), F.Counter(TEXT("c_foundenemy")), 2.f);
	TestEqual(TEXT("...but not an OnFoundPlayer, because it is not the player"),
		F.Counter(TEXT("c_foundplayer")), 1.f);
	TestEqual(TEXT("...and does not count as an enemy sighting"),
		F.Guard->EnemySightings, SightingsAfterFirst);
	return true;
}


// The went-null / eluded transaction: `OnLostPlayer` and `OnLostEnemy`, once each, on the
// real transition and not on losing sight.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyLostOutputsTest,
	"Elysium.Substrate.NpcEnemy.LostOutputs", GElysiumTestFlags)
bool FElysiumNpcEnemyLostOutputsTest::RunTest(const FString&)
{
	// --- Eluded player: OnLostPlayer, once --------------------------------------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Hate(F.Player, 5);
		F.Guard->Senses.Memory.Enemy = F.Player->Handle;
		F.Guard->Senses.Memory.bEnemyEluded = true;
		F.Guard->Schedule.Clear();

		FElysiumNpcConditions Cond;
		TestTrue(TEXT("an eluded enemy is a transition"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 10.0));
		F.Flush(10.0);
		TestEqual(TEXT("the eluded PLAYER fires OnLostPlayer"),
			F.Counter(TEXT("c_lostplayer")), 1.f);
		TestEqual(TEXT("...and not OnLostEnemy"), F.Counter(TEXT("c_lostenemy")), 0.f);
		TestTrue(TEXT("...and raises LOST_ENEMY"), Cond.Has(EElysiumNpcCond::LostEnemy));

		// The eluded target was excluded from the search, so the enemy went null and the marker
		// cleared with the transition. A second pass is not a second loss.
		ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 11.0);
		F.Flush(11.0);
		TestEqual(TEXT("the loss is not re-fired"), F.Counter(TEXT("c_lostplayer")), 1.f);
	}

	// --- A non-player enemy whose handle went null: OnLostEnemy, once -----------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr)
		{
			return false;
		}
		F.Guard->Senses.Memory.Enemy = StaleHandle(F.ThugA->Handle);
		F.Guard->Schedule.Clear();

		FElysiumNpcConditions Cond;
		TestTrue(TEXT("a handle that went null is a transition"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard, Cond, 10.0));
		F.Flush(10.0);
		TestEqual(TEXT("a non-player enemy fires OnLostEnemy"),
			F.Counter(TEXT("c_lostenemy")), 1.f);
		TestEqual(TEXT("...and not OnLostPlayer"), F.Counter(TEXT("c_lostplayer")), 0.f);
		TestFalse(TEXT("the committed enemy is cleared, not replaced with a guess"),
			F.Guard->Senses.Memory.Enemy.IsSet());
		TestFalse(TEXT("NEW_ENEMY is cleared when there is no enemy to be new"),
			Cond.Has(EElysiumNpcCond::NewEnemy));
	}

	// --- Losing LINE OF SIGHT is not losing the enemy ----------------------------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Guard->Senses.Memory.Enemy = F.Player->Handle;
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 1.0);
		F.Services.bLineOfSightClear = false;
		for (int32 i = 0; i <= ElysiumNpcSense::EnemyLosFailureLimit; ++i)
		{
			F.Guard->Senses.GatherEnemyLos(*F.Guard, 2.0 + i);
		}
		F.Flush(20.0);
		TestEqual(TEXT("a full LOS debounce fires neither lost-the-actor output"),
			F.Counter(TEXT("c_lostplayer")) + F.Counter(TEXT("c_lostenemy")), 0.f);
		TestTrue(TEXT("...and the enemy is still committed"),
			F.Guard->Senses.Memory.Enemy == F.Player->Handle);
	}
	return true;
}


// The damage conditions, and the recovered 15%-in-one-second repeated-damage window.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyDamageConditionsTest,
	"Elysium.Substrate.NpcEnemy.DamageConditions", GElysiumTestFlags)
bool FElysiumNpcEnemyDamageConditionsTest::RunTest(const FString&)
{
	FEnemyFixture F;
	if (F.Guard == nullptr)
	{
		return false;
	}
	FElysiumNpcMemory& Mem = F.Guard->Senses.Memory;

	auto Hit = [&Mem](int32 Amount, double At)
	{
		Mem.LastDamageAmount = Amount;
		Mem.LastDamageTime = At;
		ElysiumNpcCond::AccumulateDamage(Mem, Amount, At);
	};

	// A small hit inside the pass window is LIGHT and nothing else. 5 of 100 is below both the
	// chosen 20% heavy threshold and the recovered 15% repeated sum.
	Hit(5, 10.0);
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, /*PreviousGatherTime=*/9.0, C);
		TestTrue(TEXT("any committed packet raises LIGHT_DAMAGE"),
			C.Has(EElysiumNpcCond::LightDamage));
		TestFalse(TEXT("...but a small one is not HEAVY_DAMAGE"),
			C.Has(EElysiumNpcCond::HeavyDamage));
		TestFalse(TEXT("...and one hit is not REPEATED_DAMAGE"),
			C.Has(EElysiumNpcCond::RepeatedDamage));
	}

	// The same memory read by a LATER pass is not a new packet: the condition lives for one pass.
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, /*PreviousGatherTime=*/10.0, C);
		TestFalse(TEXT("a packet already consumed by a pass is not gathered twice"),
			C.Has(EElysiumNpcCond::LightDamage));
	}

	// A second hit inside the one-second window carries the sum past 15 of 100.
	Hit(12, 10.5);
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, 10.4, C);
		TestEqual(TEXT("the window accumulates rather than replacing"),
			Mem.RepeatedDamageAccumulated, 17);
		TestTrue(TEXT("a window sum over 15% of Source max health raises REPEATED_DAMAGE"),
			C.Has(EElysiumNpcCond::RepeatedDamage));
	}

	// A hit past the window RESETS it rather than decaying it, so the sum starts over.
	Hit(12, 12.0);
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, 11.9, C);
		TestEqual(TEXT("an expired window is reset, not decayed"),
			Mem.RepeatedDamageAccumulated, 12);
		TestFalse(TEXT("...so the sum no longer clears the threshold"),
			C.Has(EElysiumNpcCond::RepeatedDamage));
	}

	// A single big packet is HEAVY. The threshold itself is CHOSEN, NOT RECOVERED; what is asserted
	// here is that the predicate reads the Source max-health pool and not a bare number.
	Hit(25, 20.0);
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, 19.0, C);
		TestTrue(TEXT("a packet at or over the heavy fraction raises HEAVY_DAMAGE"),
			C.Has(EElysiumNpcCond::HeavyDamage));
		TestTrue(TEXT("...and LIGHT_DAMAGE with it"), C.Has(EElysiumNpcCond::LightDamage));
	}

	// A body with no health ceiling cannot answer the heavy question and does not guess.
	F.Guard->MaxHealth = 0;
	Hit(25, 30.0);
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, 29.0, C);
		TestTrue(TEXT("a body with no Source ceiling still takes light damage"),
			C.Has(EElysiumNpcCond::LightDamage));
		TestFalse(TEXT("...but the heavy predicate declines rather than inventing a pool"),
			C.Has(EElysiumNpcCond::HeavyDamage));
	}
	return true;
}


// `SelectIdealState`, both layers. The `no_alert_state` case is the load-bearing one:
// the keyvalue skips the Troika layer's promotions and the base tail promotes anyway.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyIdealStateTest,
	"Elysium.Substrate.NpcEnemy.IdealState", GElysiumTestFlags)
bool FElysiumNpcEnemyIdealStateTest::RunTest(const FString&)
{
	using namespace ElysiumNpcCond;

	auto Ideal = [](EElysiumNpcState Current, bool bNoAlert, bool bEnemy,
		const FElysiumNpcConditions& Cond, bool& bWarned)
	{
		FIdealStateInput In;
		In.Current = Current;
		In.bNoAlertState = bNoAlert;
		In.bHasEnemy = bEnemy;
		return SelectIdealState(In, Cond, bWarned);
	};
	auto CheckState = [this](const TCHAR* What, EElysiumNpcState Actual, EElysiumNpcState Expected)
	{
		TestTrue(FString::Printf(TEXT("%s (got %s, expected %s)"), What, LexToString(Actual),
			LexToString(Expected)), Actual == Expected);
	};

	bool bWarned = false;
	const FElysiumNpcConditions Quiet;
	const FElysiumNpcConditions Hurt = FElysiumNpcConditions::Of({ EElysiumNpcCond::LightDamage });
	const FElysiumNpcConditions Heavy = FElysiumNpcConditions::Of({ EElysiumNpcCond::HeavyDamage });
	const FElysiumNpcConditions Heard = FElysiumNpcConditions::Of({ EElysiumNpcCond::HearCombat });

	// --- The ordinary promotions ------------------------------------------------------------------
	CheckState(TEXT("a quiet idle NPC stays idle"),
		Ideal(EElysiumNpcState::Idle, false, false, Quiet, bWarned), EElysiumNpcState::Idle);
	CheckState(TEXT("light damage promotes idle -> alert"),
		Ideal(EElysiumNpcState::Idle, false, false, Hurt, bWarned), EElysiumNpcState::Alert);
	CheckState(TEXT("heavy damage promotes idle -> alert"),
		Ideal(EElysiumNpcState::Idle, false, false, Heavy, bWarned), EElysiumNpcState::Alert);
	CheckState(TEXT("a heard combat sound promotes idle -> alert"),
		Ideal(EElysiumNpcState::Idle, false, false, Heard, bWarned), EElysiumNpcState::Alert);

	// --- `no_alert_state` is NOT a suppression ----------------------------------------------------
	// The Troika layer skips ITS damage and sense promotions under the keyvalue, and then falls
	// into an unconditional `return CAI_BaseNPC::SelectIdealState(this)` whose case 1 carries no
	// such test. Collapsing the two layers is exactly the bug this assertion exists to catch.
	CheckState(TEXT("no_alert_state still promotes on damage — the base tail has no such test"),
		Ideal(EElysiumNpcState::Idle, true, false, Hurt, bWarned), EElysiumNpcState::Alert);
	CheckState(TEXT("...and on the hear family"),
		Ideal(EElysiumNpcState::Idle, true, false, Heard, bWarned), EElysiumNpcState::Alert);
	CheckState(TEXT("...and a quiet NPC is still idle either way"),
		Ideal(EElysiumNpcState::Idle, true, false, Quiet, bWarned), EElysiumNpcState::Idle);

	// The base layer alone, asserted directly, so the two layers are known to be two.
	{
		FIdealStateInput In;
		In.Current = EElysiumNpcState::Idle;
		In.bNoAlertState = true;
		bool bBaseWarned = false;
		CheckState(TEXT("the base layer promotes with no_alert_state set"),
			SelectIdealStateBase(In, Hurt, bBaseWarned), EElysiumNpcState::Alert);
	}

	// --- An enemy takes combat, from idle and from alert -------------------------------------------
	CheckState(TEXT("a committed enemy takes combat from idle"),
		Ideal(EElysiumNpcState::Idle, false, true, Quiet, bWarned), EElysiumNpcState::Combat);
	CheckState(TEXT("...and from alert"),
		Ideal(EElysiumNpcState::Alert, false, true, Quiet, bWarned), EElysiumNpcState::Combat);
	CheckState(TEXT("an alert NPC with no enemy stays alert"),
		Ideal(EElysiumNpcState::Alert, false, false, Quiet, bWarned), EElysiumNpcState::Alert);

	// --- Combat with no enemy: the recovered warning and the alert fallback ------------------------
	bWarned = false;
	CheckState(TEXT("combat with no enemy falls back to alert"),
		Ideal(EElysiumNpcState::Combat, false, false, Quiet, bWarned), EElysiumNpcState::Alert);
	TestTrue(TEXT("...and reports the recovered emission to its caller"), bWarned);

	bWarned = false;
	CheckState(TEXT("combat with an enemy stays combat"),
		Ideal(EElysiumNpcState::Combat, false, true, Quiet, bWarned), EElysiumNpcState::Combat);
	TestFalse(TEXT("...silently"), bWarned);

	// --- The states this pass does not own ---------------------------------------------------------
	for (const EElysiumNpcState Owned : { EElysiumNpcState::Scripted, EElysiumNpcState::Prone,
		EElysiumNpcState::Dead })
	{
		CheckState(TEXT("a state owned by another transaction is left alone"),
			Ideal(Owned, false, true, Hurt, bWarned), Owned);
	}
	return true;
}


// The state machine end to end on a real NPC: Alert and Combat are admitted, the mind
// records the transitions, and combat selects a real fight program.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyStateMachineTest,
	"Elysium.Substrate.NpcEnemy.StateMachine", GElysiumTestFlags)
bool FElysiumNpcEnemyStateMachineTest::RunTest(const FString&)
{
	TestTrue(TEXT("Alert is an admitted state"),
		FElysiumNpcMind::IsSupportedState(EElysiumNpcState::Alert));
	TestTrue(TEXT("Combat is an admitted state"),
		FElysiumNpcMind::IsSupportedState(EElysiumNpcState::Combat));
	TestFalse(TEXT("Prone remains a named refusal — nothing produces it"),
		FElysiumNpcMind::IsSupportedState(EElysiumNpcState::Prone));

	FEnemyFixture F;
	if (F.Guard == nullptr || F.ThugA == nullptr)
	{
		return false;
	}
	// Admission first: the ordinary think path has to be past its barrier for any of this to be a
	// statement about the decision pass rather than about admission.
	F.Guard->NextThink = 0.0f;
	F.World.Tick(1.0);
	F.Guard->NextThink = 0.0f;
	F.World.Tick(2.0);
	F.Quiet();

	// A heard combat sound promotes idle -> alert through the real think.
	F.Guard->Senses.Memory.LastHeardCategory = TEXT("PLAYER_GUNSHOT_BASE");
	F.Guard->Senses.Memory.LastHeardTime = 3.0;
	F.Guard->Senses.Memory.LastHeardSource = FElysiumEntityHandle::Invalid();
	ElysiumNpcEnemy::GatherConditions(*F.Guard, 3.5);
	TestTrue(TEXT("the heard stimulus raises HEAR_COMBAT"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::HearCombat));
	F.Guard->UpdateIdealState(3.5);
	TestTrue(TEXT("the NPC promotes to alert"),
		F.Debug(F.Guard, TEXT("Mind")).Contains(TEXT("current=Alert")));

	// Alert selects the lookaround, which alert state is what makes reachable.
	TestTrue(TEXT("alert selects the lookaround program"),
		F.Guard->SelectSchedule() == EElysiumScheduleId::AlertLookAroundNi);

	// A committed enemy takes it to combat, and combat selects a real fight program.
	F.Hate(F.ThugA, 5);
	F.Guard->Senses.Memory.Enemy = F.ThugA->Handle;
	F.Guard->UpdateIdealState(4.0);
	TestTrue(TEXT("a committed enemy takes the NPC to combat"),
		F.Debug(F.Guard, TEXT("Mind")).Contains(TEXT("current=Combat")));

	// The guard is unarmed — a content-free fixture installs no item catalogue — so the recovered
	// capability split takes the melee branch with bare-hands defaults, and thug A standing 500
	// Source units away is what the melee selector's distance arm answers.
	ElysiumNpcEnemy::GatherConditions(*F.Guard, 4.0);
	TestTrue(TEXT("a distant committed enemy raises TOO_FAR_TO_ATTACK"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::TooFarToAttack));
	TestTrue(TEXT("combat selects the melee advance rather than an idle stance"),
		F.Guard->SelectSchedule() == EElysiumScheduleId::MeleeAdvance);
	TestTrue(TEXT("...and does so every pass, with no once-latched refusal in the way"),
		F.Guard->SelectSchedule() == EElysiumScheduleId::MeleeAdvance);
	return true;
}


// The alert-lookaround chance and its new producer.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyLookaroundChanceTest,
	"Elysium.Substrate.NpcEnemy.LookaroundChance", GElysiumTestFlags)
bool FElysiumNpcEnemyLookaroundChanceTest::RunTest(const FString&)
{
	// `min(30, (m_iEnemySightings + 2) * 5)`, at both boundaries.
	TestEqual(TEXT("no sightings reads the 10% floor"),
		ElysiumNpcCond::AlertLookaroundChance(0), 10);
	TestEqual(TEXT("one sighting reads 15%"), ElysiumNpcCond::AlertLookaroundChance(1), 15);
	TestEqual(TEXT("three sightings read 25%"), ElysiumNpcCond::AlertLookaroundChance(3), 25);
	TestEqual(TEXT("four sightings reach the 30% cap"),
		ElysiumNpcCond::AlertLookaroundChance(4), 30);
	TestEqual(TEXT("...and the cap holds above it"),
		ElysiumNpcCond::AlertLookaroundChance(50), 30);

	// The producer: an acquisition episode against the player, and only against the player.
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.Player == nullptr || F.ThugA == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		TestEqual(TEXT("an NPC that has never seen an enemy starts at zero"),
			F.Guard->EnemySightings, 0);

		F.Guard->Senses.Memory.Enemy = F.Player->Handle;
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 1.0);
		TestEqual(TEXT("one acquisition episode is one sighting"), F.Guard->EnemySightings, 1);

		// Staying visible is the same episode.
		for (int32 i = 0; i < 5; ++i)
		{
			F.Guard->Senses.GatherEnemyLos(*F.Guard, 2.0 + i);
		}
		TestEqual(TEXT("continued sight is not a second sighting"), F.Guard->EnemySightings, 1);

		// A fresh episode counts again.
		F.Services.bLineOfSightClear = false;
		for (int32 i = 0; i <= ElysiumNpcSense::EnemyLosFailureLimit; ++i)
		{
			F.Guard->Senses.GatherEnemyLos(*F.Guard, 10.0 + i);
		}
		F.Services.bLineOfSightClear = true;
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 40.0);
		TestEqual(TEXT("a re-acquisition is a second sighting"), F.Guard->EnemySightings, 2);

		// A non-player enemy does not count.
		ElysiumNpcEnemy::SetEnemy(*F.Guard, F.ThugA->Handle);
		F.Guard->Senses.GatherEnemyLos(*F.Guard, 50.0);
		TestEqual(TEXT("an NPC enemy is not a player sighting"), F.Guard->EnemySightings, 2);
	}

	// The chance actually reaches the selector: with the same seed, a capped NPC takes the
	// lookaround more often than a floored one over the same draw sequence.
	{
		auto CountLookarounds = [](int32 Sightings)
		{
			FEnemyFixture F;
			if (F.Guard == nullptr)
			{
				return -1;
			}
			F.Guard->bAllowAlertLookaround = true;
			F.Guard->EnemySightings = Sightings;
			ElysiumRng::SeedAll(0x4C4F4F4B);
			int32 Count = 0;
			for (int32 i = 0; i < 400; ++i)
			{
				if (F.Guard->SelectIdleSchedule() == EElysiumScheduleId::AlertLookAroundNi)
				{
					++Count;
				}
			}
			return Count;
		};
		const int32 AtFloor = CountLookarounds(0);
		const int32 AtCap = CountLookarounds(4);
		TestTrue(TEXT("the floor chance produces some lookarounds"), AtFloor > 0);
		TestTrue(TEXT("the capped chance produces strictly more over the same draws"),
			AtCap > AtFloor);
	}
	return true;
}


// The interrupt mask on the kernel: a masked condition aborts into reselection, and an
// empty mask finishes despite the same conditions.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyInterruptTest,
	"Elysium.Substrate.NpcEnemy.Interrupts", GElysiumTestFlags)
bool FElysiumNpcEnemyInterruptTest::RunTest(const FString&)
{
	const FElysiumNpcConditions Firing =
		FElysiumNpcConditions::Of({ EElysiumNpcCond::NewEnemy, EElysiumNpcCond::LightDamage });

	// --- The door-obstruction family's masks are empty, which is a real posture ---------------------
	// The two idle programs are deliberately NOT in this list any more: their masks are filled from
	// the interrupt census, with the CHOSEN mark at the registry, because an empty mask on them is
	// the one thing that stops live enemy acquisition from ever running
	// (`Elysium.Substrate.NpcCombat.IdleAcquisition` owns that assertion).
	for (const EElysiumScheduleId Id : { EElysiumScheduleId::BackAwayFromDoorNe,
		EElysiumScheduleId::BackAwayFromDoorWaitNe, EElysiumScheduleId::TakeCoverHintDoor })
	{
		const FElysiumSchedule* Schedule = ElysiumScheduleFor(Id);
		if (!TestNotNull(TEXT("the program is registered"), Schedule))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s declares no recovered interrupt mask"),
			ElysiumScheduleName(Id)), Schedule->Interrupts.IsEmpty());
		TestFalse(TEXT("...and does not claim DELAY_INTERRUPTS"), Schedule->bDelayInterrupts);
	}

	// --- A no-interrupt schedule finishes its task despite the conditions -------------------------
	// `SCHED_TROIKA_MELEE_ATTACK1_SWING` is the recovered empty mask: "once that terminal attack
	// task owns the NPC it is not reevaluated as a fresh attack choice each tick."
	{
		FKernelRunner Runner;
		FElysiumScheduleState State;
		double Delay = 0.0;
		TestTrue(TEXT("the door-cover program starts"),
			ElysiumSchedule::Start(State, EElysiumScheduleId::TakeCoverHintDoor, Runner));
		TestTrue(TEXT("its first task runs"),
			ElysiumSchedule::Tick(State, Runner, 0.0, Delay, &Firing));
		TestTrue(TEXT("a schedule with no mask is not interrupted by anything"),
			ElysiumSchedule::Tick(State, Runner, 0.1, Delay, &Firing));
		TestTrue(TEXT("...and is still running"), State.IsRunning());
	}

	// --- A masked condition aborts mid-schedule, into reselection rather than the fail schedule ---
	{
		FKernelRunner Runner;
		FElysiumScheduleState State;
		double Delay = 0.0;
		// The cover program is the only registered one with a fail schedule, so it is what proves
		// an interrupt does not take that route.
		ElysiumSchedule::FInterruptMaskScope Scope(EElysiumScheduleId::TakeCoverHintDoor,
			FElysiumNpcConditions::Of({ EElysiumNpcCond::NewEnemy }));
		TestTrue(TEXT("the cover program starts"),
			ElysiumSchedule::Start(State, EElysiumScheduleId::TakeCoverHintDoor, Runner));
		TestTrue(TEXT("its first task runs"),
			ElysiumSchedule::Tick(State, Runner, 0.0, Delay, nullptr));
		TestTrue(TEXT("...leaving it mid-program"), State.IsRunning());

		TestFalse(TEXT("a masked condition ends the program"),
			ElysiumSchedule::Tick(State, Runner, 0.1, Delay, &Firing));
		TestFalse(TEXT("...and nothing is running afterwards"), State.IsRunning());
		TestFalse(TEXT("...specifically NOT its fail schedule, which is task failure's route"),
			State.Current == EElysiumScheduleId::BackAwayFromDoorNe);
		TestTrue(TEXT("the trace names the condition that fired"), Runner.Saw(TEXT("NEW_ENEMY")));
	}

	// --- A condition outside the mask does not interrupt ------------------------------------------
	FElysiumNpcConditions IdleMaskBefore;
	if (const FElysiumSchedule* Idle = ElysiumScheduleFor(EElysiumScheduleId::IdleDisposition))
	{
		IdleMaskBefore = Idle->Interrupts;
	}
	{
		FKernelRunner Runner;
		FElysiumScheduleState State;
		double Delay = 0.0;
		// The scope REPLACES the registered mask rather than adding to it, which is what makes this
		// a statement about the mask under test and not about the idle program's own.
		ElysiumSchedule::FInterruptMaskScope Scope(EElysiumScheduleId::IdleDisposition,
			FElysiumNpcConditions::Of({ EElysiumNpcCond::EnemyDead }));
		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleDisposition, Runner);
		ElysiumSchedule::Tick(State, Runner, 0.0, Delay, nullptr);
		TestTrue(TEXT("a condition the mask does not carry leaves the program alone"),
			ElysiumSchedule::Tick(State, Runner, 0.1, Delay, &Firing));
	}

	// --- The mask scope restores what it borrowed --------------------------------------------------
	{
		const FElysiumSchedule* Idle = ElysiumScheduleFor(EElysiumScheduleId::IdleDisposition);
		TestTrue(TEXT("the borrowed mask is given back"),
			Idle != nullptr && Idle->Interrupts == IdleMaskBefore);
	}
	return true;
}


// The bitset itself, and what a save carries of the new memory.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyConditionSetTest,
	"Elysium.Substrate.NpcEnemy.ConditionSet", GElysiumTestFlags)
bool FElysiumNpcEnemyConditionSetTest::RunTest(const FString&)
{
	FElysiumNpcConditions C;
	TestTrue(TEXT("a fresh set is empty"), C.IsEmpty());
	TestEqual(TEXT("...and names nothing"), C.Describe(), FString(TEXT("(none)")));

	// The recovered identities are retail's own numbers, and the named-without-ID members sit
	// outside that band so neither can collide with the other.
	TestEqual(TEXT("SEE_HATE is 0x43"), static_cast<int32>(EElysiumNpcCond::SeeHate), 0x43);
	TestEqual(TEXT("HAVE_ENEMY_LOS is 0x4a"),
		static_cast<int32>(EElysiumNpcCond::HaveEnemyLos), 0x4a);
	TestEqual(TEXT("NEW_ENEMY is 0x54"), static_cast<int32>(EElysiumNpcCond::NewEnemy), 0x54);
	TestEqual(TEXT("SEE_NEMESIS is 0x5b"), static_cast<int32>(EElysiumNpcCond::SeeNemesis), 0x5b);
	TestTrue(TEXT("a named-without-ID member sits above the recovered band"),
		static_cast<int32>(EElysiumNpcCond::SeeEnemy) > 0x66);

	C.Set(EElysiumNpcCond::SeeHate);
	C.Set(EElysiumNpcCond::HearCombat);
	TestTrue(TEXT("a set condition is present"), C.Has(EElysiumNpcCond::SeeHate));
	TestTrue(TEXT("...across word boundaries"), C.Has(EElysiumNpcCond::HearCombat));
	TestFalse(TEXT("an unset one is not"), C.Has(EElysiumNpcCond::NewEnemy));
	TestEqual(TEXT("the set counts what it carries"), C.Num(), 2);
	TestTrue(TEXT("the lowest identity is what a trace names first"),
		C.FirstSet() == EElysiumNpcCond::SeeHate);
	TestTrue(TEXT("the description names both"),
		C.Describe().Contains(TEXT("SEE_HATE")) && C.Describe().Contains(TEXT("HEAR_COMBAT")));

	const FElysiumNpcConditions Mask =
		FElysiumNpcConditions::Of({ EElysiumNpcCond::HearCombat, EElysiumNpcCond::NewEnemy });
	TestTrue(TEXT("intersection is detected"), C.Intersects(Mask));
	TestEqual(TEXT("...and yields exactly the shared bits"), Mask.Intersection(C).Describe(),
		FString(TEXT("HEAR_COMBAT")));

	C.Clear(EElysiumNpcCond::HearCombat);
	TestFalse(TEXT("clearing removes only what was named"), C.Intersects(Mask));
	C.Reset();
	TestTrue(TEXT("reset empties the set"), C.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemySaveTest,
	"Elysium.Substrate.NpcEnemy.Save", GElysiumTestFlags)
bool FElysiumNpcEnemySaveTest::RunTest(const FString&)
{
	FEnemyFixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.Guard->EnemySightings = 3;
	F.Guard->bNoAlertState = true;
	F.Guard->Senses.Memory.Enemy = F.Player->Handle;
	F.Guard->Senses.Memory.bEnemyEluded = true;
	F.Guard->Senses.Memory.RepeatedDamageWindowStart = 12.5;
	F.Guard->Senses.Memory.RepeatedDamageAccumulated = 17;
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::SeeHate);

	TArray<uint8> Payload;
	{
		FMemoryWriter Writer(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::Latest);
		F.Guard->Serialize(Ar);
	}

	FEnemyFixture G;
	if (G.Guard == nullptr || G.Player == nullptr)
	{
		return false;
	}
	{
		FMemoryReader Reader(Payload, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::Latest);
		G.Guard->Serialize(Ar);
	}

	const FElysiumNpcMemory& Restored = G.Guard->Senses.Memory;
	TestTrue(TEXT("the committed enemy survives"), Restored.Enemy == G.Player->Handle);
	TestTrue(TEXT("the eluded marker survives"), Restored.bEnemyEluded);
	TestEqual(TEXT("the repeated-damage window sum survives"),
		Restored.RepeatedDamageAccumulated, 17);
	TestTrue(TEXT("...with its window root"),
		FMath::IsNearlyEqual(Restored.RepeatedDamageWindowStart, 12.5, 0.001));

	// Conditions are NOT saved: they are rebuilt from the memory above on the first think after a
	// load, which is what the recovered pass does on every think anyway.
	TestTrue(TEXT("the gathered conditions do not travel in the payload"),
		G.Guard->Cognition.Conditions.IsEmpty());
	// ...and the pass clock is stamped rather than left at "nothing has ever run", so a remembered
	// stimulus from before the save cannot read as new.
	TestTrue(TEXT("the pass clock is stamped on restore"), G.Guard->Cognition.GatheredAt >= 0.0);

	// A payload written before this schema restores with the defaults rather than being refused.
	{
		TArray<uint8> Legacy;
		{
			FMemoryWriter Writer(Legacy, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(Writer, FElysiumSaveVersion::NpcSenses);
			F.Guard->Serialize(Ar);
		}
		FEnemyFixture H;
		if (H.Guard == nullptr)
		{
			return false;
		}
		FMemoryReader Reader(Legacy, /*bIsPersistent*/ true);
		FElysiumSaveArchive Ar(Reader, FElysiumSaveVersion::NpcSenses);
		H.Guard->Serialize(Ar);
		TestFalse(TEXT("a pre-cognition payload restores an un-eluded enemy"),
			H.Guard->Senses.Memory.bEnemyEluded);
		TestEqual(TEXT("...and no open damage window"),
			H.Guard->Senses.Memory.RepeatedDamageAccumulated, 0);
	}
	return true;
}

}   // namespace ElysiumNpcEnemyTests

#endif // WITH_DEV_AUTOMATION_TESTS
