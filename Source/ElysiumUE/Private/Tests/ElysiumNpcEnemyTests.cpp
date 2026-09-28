// Content-free Substrate automation: the NPC decision pass — condition gathering, the enemy
// transaction, the schedule interrupt gate, and the two-layer ideal-state selection.
//
// Everything under test is substrate rule. The engine contributes one world term (is the segment
// between two points clear), which the recording services script, and the NPC schedule RNG stream
// is seeded per fixture so a chance roll is reproducible rather than sampled.
//
// The authored facts: `docs/vtmb/npc-ai/social.md` -> "Enemy acquisition and
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
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
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
		FElysiumNpcWorldFixture Fixture;
		FElysiumRecordingServices& Services;
		FElysiumEntityWorld& World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* ThugA = nullptr;
		FElysiumNpc* ThugB = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__npcenemy_test__"), 0x454E454D);

			FElysiumEntityDef& GuardDef = Builder.AddNpc(TEXT("guard"));
			GuardDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			GuardDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnFoundEnemy"),  TEXT("c_foundenemy"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnFoundPlayer"), TEXT("c_foundplayer"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnLostEnemy"),   TEXT("c_lostenemy"));
			Builder.WireOutput(TEXT("guard"), TEXT("OnLostPlayer"),  TEXT("c_lostplayer"));

			for (const TCHAR* Name : { TEXT("thug_a"), TEXT("thug_b") })
			{
				Builder.AddNpc(Name, FVector(Cm(500.f), 0.0, 0.0));
			}

			for (const TCHAR* Name : { TEXT("c_foundenemy"), TEXT("c_foundplayer"),
				TEXT("c_lostenemy"), TEXT("c_lostplayer") })
			{
				Builder.AddCounter(Name);
			}
			return Builder;
		}

		FEnemyFixture()
			: Fixture(BuildWorld())
			, Services(Fixture.Services)
			, World(Fixture.World)
		{
			Guard = Fixture.Npc(TEXT("guard"));
			ThugA = Fixture.Npc(TEXT("thug_a"));
			ThugB = Fixture.Npc(TEXT("thug_b"));
			Player = Fixture.Player();
			// Whatever the admission think may have selected is not this case's setup: every
			// schedule under test is started explicitly.
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
			Quiet();
		}

		// Nothing here wants an NPC's own think competing with the pass the case is driving.
		void Quiet()
		{
			FElysiumNpcWorldFixture::Quiet({ Guard, ThugA, ThugB });
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
				Guard->EnemyMemory.Update(*Guard, Target->Handle, 0.0);
			}
		}

		void Fear(const FElysiumEntity* Target, int32 Priority)
		{
			if (Guard && Target)
			{
				Guard->Relationships.SetEntity(Target->Handle, EElysiumRelationship::Fear, Priority);
				Guard->EnemyMemory.Update(*Guard, Target->Handle, 0.0);
			}
		}

		// The mind's state is private to the leaf; its inspector row is the read side everything
		// else uses, so a test asserts through the same surface a developer would look at.
		FString Debug(const FElysiumEntity* Entity, const TCHAR* Key) const
		{
			return FElysiumNpcWorldFixture::Debug(Entity, Key);
		}

		float Counter(const TCHAR* Name)
		{
			return Fixture.Counter(Name);
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
		bool bVisible = false;
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
	F.Guard->BaseMemory.Enemy = F.ThugA->Handle;
	F.ThugA->bDead = true;

	FElysiumNpcWorldFixture::GatherConditionsAt(*F.Guard, 10.0);

	const FElysiumNpcConditions& Cond = F.Guard->Cognition.Conditions;
	TestTrue(TEXT("the dead committed enemy raises ENEMY_DEAD in the same pass"),
		Cond.Has(EElysiumNpcCond::EnemyDead));
	TestTrue(TEXT("...and the pass replaces it rather than carrying a corpse"),
		F.Guard->BaseMemory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...raising NEW_ENEMY"), Cond.Has(EElysiumNpcCond::NewEnemy));
	// `SetEnemy` (`0x10279a50`) hands the old enemy to the last-enemy helper `0x10279b70` ONLY when
	// its handle is still live (`0x10279a96` -1, `0x10279ab2` serial, `0x10279ab7` null entry). A
	// killed-and-removed entity (`bDead`, `EFL_KILLME`) no longer resolves, so retail writes no last
	// enemy; the port's old unconditional transfer asserted here was port-invented. Corrected to
	// retail (story 8 L11 integration).
	TestFalse(TEXT("a removed old enemy does not reach the last-enemy path"),
		F.Guard->BaseMemory.LastEnemy == F.ThugA->Handle);

	// Step 5 gathers the committed enemy's own conditions AFTER the choice, so they describe the
	// player and not the corpse: nothing has failed a LOS check for the new target yet.
	TestTrue(TEXT("the committed-enemy conditions describe the NEW enemy"),
		Cond.Has(EElysiumNpcCond::HaveEnemyLos));
	TestFalse(TEXT("...and ENEMY_UNREACHABLE is never set — it has no producer"),
		Cond.Has(EElysiumNpcCond::EnemyUnreachable));

	// `m_bConditionsGathered` latched (`0x1026eca9`), in the stamp form: the pass's own `curtime`.
	TestTrue(TEXT("the pass latches m_bConditionsGathered at curtime"),
		F.Guard->Cognition.GatheredAt == F.World.NowSeconds());
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
	F.Guard->BaseMemory.Enemy = F.ThugA->Handle;
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
	F.Guard->EnemyMemory.Update(*F.Guard, F.ThugA->Handle, 0.0);
	F.Guard->EnemyMemory.MarkEluded(F.ThugA->Handle);
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
		F.Guard->BaseMemory.Enemy = F.ThugA->Handle;

		// `SCHED_TROIKA_MELEE_ATTACK1_SWING` is the one program whose EMPTY mask is recovered rather
		// than merely undecoded: "once that terminal attack task owns the NPC it is not reevaluated
		// as a fresh attack choice each tick". It is therefore the honest driver for the starvation
		// rule — the two idle programs now carry a mask, which is what makes live acquisition work.
		TestTrue(TEXT("the terminal swing program starts"),
			ElysiumSchedule::Start(F.Guard->Schedule,
				ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, *F.Guard));
		if (const FElysiumScheduleProgram* Swing = ElysiumScheduleFor(
			ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING)))
		{
			TestTrue(TEXT("...and its recovered mask really is empty"), Swing->Interrupts.IsEmpty());
		}

		// `0x10279dd0` reads and writes the NPC's own condition word (slot 480 `ShouldChooseNewEnemy`
		// reads `HasCondition(0x43)`), so the sight lands there.
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::SeeHate);
		// Retail's answer is "an enemy is held", not "the enemy changed": the gate's refusal
		// (`0x10279f44` -> `0x10279f5d`) returns `GetEnemy() != NULL` (`0x1027a105`), TRUE here.
		TestTrue(TEXT("an uninterested schedule skips the search and answers 'enemy held'"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		TestTrue(TEXT("...and keeps the enemy it already had"),
			F.Guard->BaseMemory.Enemy == F.ThugA->Handle);

		// The disposition idle's own registered mask admits `NEW_ENEMY`, so the same pass under it
		// takes the better candidate — no test-only mask installed.
		TestTrue(TEXT("the idle program starts"),
			ElysiumSchedule::Start(F.Guard->Schedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, *F.Guard));
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::SeeHate);
		TestTrue(TEXT("a schedule that admits NEW_ENEMY lets the replacement through"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		TestTrue(TEXT("...and the higher-priority candidate wins"),
			F.Guard->BaseMemory.Enemy == F.ThugB->Handle);
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
		TestTrue(TEXT("with no schedule running the gate is open"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		TestTrue(TEXT("...so a first enemy can be acquired at all"),
			F.Guard->BaseMemory.Enemy == F.ThugA->Handle);
	}

	// --- A null enemy under an uninterested schedule -----------------------------------------------
	// Retail (`0x10279dd0`): the went-null case is `m_afMemory & 0x18000` set at entry with
	// `GetEnemy()` null (`0x10279e85`/`0x10279e89`). It only WARNS (`DevMsg(2, ...)` `0x10279fd7`, a
	// developer-level message, when neither NEW_ENEMY `0x54` nor LOST_ENEMY `0x47` interrupts the
	// running program, `0x10279fb8`-`0x10279fca`) and FALLS THROUGH to the choice at `0x10279fe5`;
	// there is no refusal and no per-schedule latch. The port's old guess refused the choice and
	// raised a one-per-schedule error; corrected to retail.
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.ThugA == nullptr)
		{
			return false;
		}
		F.Guard->BaseMemory.Enemy = StaleHandle(F.ThugA->Handle);
		TestTrue(TEXT("the terminal swing program starts"),
			ElysiumSchedule::Start(F.Guard->Schedule,
				ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, *F.Guard));

		// Without the entry bits there is no went-null: the gate refuses (`0x10279f44`, the enemy is
		// not eluded) and the stale handle is left as it stands.
		TestFalse(TEXT("no memory bit: the refusal answers 'no enemy held'"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		TestTrue(TEXT("...and the stale handle is left alone"), F.Guard->BaseMemory.Enemy.IsSet());

		// With `0x8000` (a non-player enemy was held, `0x1027a0ff`) the enemy went null: the change
		// work runs even under the uninterested program.
		F.Guard->BaseScheduleHost.MemoryBits |= 0x8000u;
		TestFalse(TEXT("went null: nothing chosen, so no enemy held"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		TestFalse(TEXT("...the committed handle is cleared by SetEnemy(NULL) (0x1027a077)"),
			F.Guard->BaseMemory.Enemy.IsSet());
		TestEqual(TEXT("...both enemy bits cleared (0x1027a027, 0x1027a08b)"),
			F.Guard->BaseScheduleHost.MemoryBits & 0x38000u, 0u);
		TestTrue(TEXT("...and LOST_ENEMY raised (0x1027a0b0)"),
			F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::LostEnemy));
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
		F.Guard->BaseMemory.Enemy = F.ThugA->Handle;
		F.Guard->EnemyMemory.MarkEluded(F.ThugA->Handle);
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
		F.Services.LineOfSightQuery = [](const FVector&, const FVector& To) { return To.X > Cm(1000.f); };
		F.Player->Origin = FVector(Cm(2000.f), 0.0, 0.0);
		F.ThugA->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Hate(F.Player, 5);
		F.Hate(F.ThugA, 5);
		F.Guard->Senses.TickSight(*F.Guard, 10.0);
		TestTrue(TEXT("the sight pass sees the player"), F.Guard->Senses.Memory.bPlayerVisible);

		TestTrue(TEXT("a visible candidate displaces a nearer unseen incumbent"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.Player->Handle);

		// With the player unseen, the plain distance rule returns: the nearer thug wins.
		F.Player->Origin = FVector(Cm(-2000.f), 0.0, 0.0);   // behind the guard, out of cone
		F.Guard->Senses.TickSight(*F.Guard, 20.0);
		TestFalse(TEXT("the player is no longer seen"), F.Guard->Senses.Memory.bPlayerVisible);
		TestTrue(TEXT("between two unseen candidates the nearer one wins"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.ThugA->Handle);

		// And a closer UNSEEN candidate does not displace a visible incumbent. The extra candidate
		// is spawned at runtime so it is arbitrated AFTER the player in entity-list order, which is
		// the only ordering in which this rule is distinguishable from the one above.
		F.Player->Origin = FVector(Cm(2000.f), 0.0, 0.0);
		F.Guard->Senses.TickSight(*F.Guard, 30.0);
		TestTrue(TEXT("the player is visible again"), F.Guard->Senses.Memory.bPlayerVisible);

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
		LateEntity->NextThink = ELYSIUM_NEVER_THINK;   // a plain entity, not an NPC: no AI to disable
		F.Hate(LateEntity, 5);
		TestTrue(TEXT("a closer unseen candidate does NOT displace a visible incumbent"),
			ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.Player->Handle);
	}
	return true;
}

// CAI_Memory is admission, not a relationship-world scan. The tutorial's thug is about 2380
// Source units from Jack's dialogue; its 540-unit sight admission leaves a hostile player absent
// from both the store and selection until an actual sight pass writes the record.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyMemoryAdmissionTest,
	"Elysium.Substrate.NpcEnemy.MemoryAdmission", GElysiumTestFlags)
bool FElysiumNpcEnemyMemoryAdmissionTest::RunTest(const FString&)
{
	FEnemyFixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.Guard->Senses.Perception.VisionDistanceCm = Cm(540.f);
	F.Player->Origin = FVector(Cm(2380.f), 0.0, 0.0);
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);

	F.Guard->Senses.TickSight(*F.Guard, 10.0);
	FElysiumNpcWorldFixture::GatherConditionsAt(*F.Guard, 10.0);
	TestFalse(TEXT("the unseen hostile tutorial-distance player has no memory record"),
		F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);
	TestFalse(TEXT("...and relationship alone cannot select it"),
		ElysiumNpcEnemy::BestEnemy(*F.Guard).IsSet());
	TestFalse(TEXT("...including through the real decision pass"),
		F.Guard->BaseMemory.Enemy.IsSet());

	// Hearing records a sound stimulus only. It does not manufacture an enemy-memory candidate.
	FElysiumGameSoundRequest Sound;
	Sound.Position = FVector(Cm(100.f), 0.0, 0.0);
	Sound.Category = FName(TEXT("PLAYER_GUNSHOT_BASE"));
	Sound.RadiusCm = Cm(1200.f);
	Sound.Source = F.Player->Handle;
	Sound.TypeMask = ElysiumGameSounds::Combat;
	F.World.GameSounds().Emit(Sound, 10.05);
	F.Guard->Senses.TickHearing(*F.Guard, 10.1);
	TestTrue(TEXT("hearing consumed the player stimulus"),
		F.Guard->Senses.Memory.LastHeardSource == F.Player->Handle);
	TestFalse(TEXT("...without an enemy-memory write"),
		F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);

	F.Player->Origin = FVector(Cm(500.f), 0.0, 0.0);
	F.Guard->Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	F.Guard->Senses.TickSight(*F.Guard, 11.0);
	FElysiumNpcWorldFixture::GatherConditionsAt(*F.Guard, 11.0);
	TestTrue(TEXT("a seen hostile player gains the actor record"),
		F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);
	TestTrue(TEXT("...and can now be selected"),
		ElysiumNpcEnemy::BestEnemy(*F.Guard) == F.Player->Handle);

	// No age expiry: time does not remove the observation. Elusion is per record and a later
	// UpdateMemory sight write clears it; refresh only removes dead/invalid handles.
	F.Guard->EnemyMemory.MarkEluded(F.Player->Handle);
	TestFalse(TEXT("an eluded record is excluded"), ElysiumNpcEnemy::BestEnemy(*F.Guard).IsSet());
	F.Guard->EnemyMemory.Refresh(F.World, 11.5);
	TestTrue(TEXT("refresh keeps the record's elusion marker"),
		F.Guard->EnemyMemory.IsEluded(F.Player->Handle));
	TestTrue(TEXT("a live record has no time expiry"),
		F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);
	F.Guard->EnemyMemory.MarkEluded(F.Player->Handle, false);
	F.Guard->EnemyMemory.Update(*F.Guard, F.Player->Handle, 20.0);
	F.Player->Origin = FVector(Cm(501.f), 0.0, 0.0);
	F.Guard->EnemyMemory.Refresh(F.World, 20.249);
	TestEqual(TEXT("free knowledge refreshes position strictly before .25 seconds"),
		F.Guard->EnemyMemory.Find(F.Player->Handle)->LastPosition, F.Player->Origin);
	F.Player->Origin = FVector(Cm(502.f), 0.0, 0.0);
	F.Guard->EnemyMemory.Refresh(F.World, 20.25);
	TestFalse(TEXT("free knowledge expires at the equality boundary"),
		F.Guard->EnemyMemory.Find(F.Player->Handle)->LastPosition == F.Player->Origin);
	F.Player->bDead = true;
	F.Guard->EnemyMemory.Refresh(F.World, 21.0);
	TestEqual(TEXT("a dead actor is removed at refresh"), F.Guard->EnemyMemory.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyDamageMemoryTest,
	"Elysium.Substrate.NpcEnemy.DamageMemory", GElysiumTestFlags)
bool FElysiumNpcEnemyDamageMemoryTest::RunTest(const FString&)
{
	auto Damage = [](FElysiumNpc& Npc, const FElysiumEntityHandle& Source, const FVector& Position)
	{
		FElysiumDmg Dmg;
		Dmg.Source = Source;
		Dmg.AttackPosition = Position;
		Dmg.bHasAttackPosition = true;
		return ElysiumNpcEnemy::RememberDamage(Npc, Dmg, 10.0);
	};
	// Unknown, unseen attacker writes only an anonymous position record.
	{
		FEnemyFixture F;
		if (!F.Guard || !F.Player) return false;
		F.Player->Origin = FVector(Cm(-100.f), 0.f, 0.f);
		TestTrue(TEXT("unknown unseen damage enters the producer"),
			Damage(*F.Guard, F.Player->Handle, FVector(77.f, 0.f, 0.f)));
		TestFalse(TEXT("unknown damage does not manufacture an actor record"),
			F.Guard->EnemyMemory.Find(F.Player->Handle) != nullptr);
		TestTrue(TEXT("...but retains its position-only record"),
			F.Guard->EnemyMemory.Records()[0].bPositionOnly);
	}
	// A known attacker refreshes its actor record at the packet attack position.
	{
		FEnemyFixture F;
		if (!F.Guard || !F.Player) return false;
		F.Player->Origin = FVector(Cm(-100.f), 0.f, 0.f);
		F.Guard->EnemyMemory.Update(*F.Guard, F.Player->Handle, 0.0);
		Damage(*F.Guard, F.Player->Handle, FVector(88.f, 0.f, 0.f));
		TestEqual(TEXT("known damage updates that actor's position"),
			F.Guard->EnemyMemory.Find(F.Player->Handle)->LastPosition, FVector(88.f, 0.f, 0.f));
	}
	// An unknown attacker with a current unseen enemy refreshes that committed target instead.
	{
		FEnemyFixture F;
		if (!F.Guard || !F.Player || !F.ThugA) return false;
		F.Player->Origin = FVector(Cm(-100.f), 0.f, 0.f);
		F.Guard->EnemyMemory.Update(*F.Guard, F.ThugA->Handle, 0.0);
		F.Guard->BaseMemory.Enemy = F.ThugA->Handle;
		Damage(*F.Guard, F.Player->Handle, FVector(99.f, 0.f, 0.f));
		TestEqual(TEXT("current enemy receives the unknown attack position"),
			F.Guard->EnemyMemory.Find(F.ThugA->Handle)->LastPosition, FVector(99.f, 0.f, 0.f));
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
	F.Guard->BaseMemory.Enemy = F.Player->Handle;
	F.Guard->GatherEnemyConditions(F.Player);   // slot 481 `0x10270b20`
	F.Flush(1.0);
	TestEqual(TEXT("the first committed-enemy LOS fires OnFoundEnemy"),
		F.Counter(TEXT("c_foundenemy")), 1.f);
	TestTrue(TEXT("...and latches m_afMemory 0x20000 (0x10270d1e)"),
		(F.Guard->BaseScheduleHost.MemoryBits & 0x20000u) != 0);
	// The LOS edge publishes detection; it does NOT count a sighting. Retail's two writers of
	// `m_iEnemySightings` are both in the sense pass (see `LookaroundChance`).
	const int32 SightingsAfterFirst = F.Guard->EnemySightings;
	TestEqual(TEXT("...and counts no enemy sighting: that is the sense pass's write, not this edge"),
		SightingsAfterFirst, 0);

	// A replacement transfers the old handle. Retail's `SetEnemy` (`0x10279a50`) writes `m_hEnemy`
	// (`+0x5ce0`), `m_hLastEnemy` through `0x10279b70`, runs slot 560 and the discipline sweep, and
	// nothing else: the LOS episode is NOT reset here (the port's old body forgot the latch, the
	// debounce and the occlusion flag -- port-invented; the episode belongs to
	// `GatherEnemyConditions` `0x10270b20`, lane L07). Corrected to retail.
	F.Guard->BaseMemory.EnemyOccludedCheck = 3;
	ElysiumNpcEnemy::SetEnemy(*F.Guard, F.ThugA->Handle);
	TestTrue(TEXT("the old handle goes through the last-enemy path"),
		F.Guard->BaseMemory.LastEnemy == F.Player->Handle);
	TestTrue(TEXT("the new handle is committed"),
		F.Guard->BaseMemory.Enemy == F.ThugA->Handle);
	TestTrue(TEXT("the LOS latch is not SetEnemy's to clear"),
		(F.Guard->BaseScheduleHost.MemoryBits & 0x20000u) != 0);
	TestEqual(TEXT("...nor its debounce (+0x5b98)"), F.Guard->BaseMemory.EnemyOccludedCheck, 3);
	TestEqual(TEXT("the non-null write runs the discipline sweep (0x10279b0c)"),
		F.Guard->SetEnemyDisciplineStripCalls, 1);
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
		F.Guard->BaseMemory.Enemy = F.Player->Handle;
		F.Guard->EnemyMemory.MarkEluded(F.Player->Handle);
		F.Guard->Schedule.Clear();
		// The output is picked by the ENTRY `m_afMemory` player bit `0x10000` (`0x1027a0c5`), which
		// the acquisition of the player wrote (`0x1027a0ff`).
		F.Guard->BaseScheduleHost.MemoryBits |= 0x10000u;

		// Retail answers "enemy held" (`0x1027a105`): the eluded player is dropped for nothing.
		TestFalse(TEXT("an eluded enemy is dropped: no enemy held"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		F.Flush(10.0);
		TestEqual(TEXT("the eluded PLAYER fires OnLostPlayer"),
			F.Counter(TEXT("c_lostplayer")), 1.f);
		TestEqual(TEXT("...and not OnLostEnemy"), F.Counter(TEXT("c_lostenemy")), 0.f);
		TestTrue(TEXT("...and raises LOST_ENEMY"),
			F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::LostEnemy));

		// The eluded target was excluded from the search, so the enemy went null and the entry bits
		// cleared with the transition. A second pass is not a second loss.
		ElysiumNpcEnemy::ChooseEnemy(*F.Guard);
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
		F.Guard->BaseMemory.Enemy = StaleHandle(F.ThugA->Handle);
		F.Guard->Schedule.Clear();
		// "Went null" is retail's `m_afMemory & 0x18000` at entry with `GetEnemy()` null
		// (`0x10279e85`); `0x8000` is the non-player enemy bit the acquisition wrote.
		F.Guard->BaseScheduleHost.MemoryBits |= 0x8000u;

		TestFalse(TEXT("a handle that went null is a transition to no enemy held"),
			ElysiumNpcEnemy::ChooseEnemy(*F.Guard));
		F.Flush(10.0);
		TestEqual(TEXT("a non-player enemy fires OnLostEnemy"),
			F.Counter(TEXT("c_lostenemy")), 1.f);
		TestEqual(TEXT("...and not OnLostPlayer"), F.Counter(TEXT("c_lostplayer")), 0.f);
		TestFalse(TEXT("the committed enemy is cleared, not replaced with a guess"),
			F.Guard->BaseMemory.Enemy.IsSet());
		TestFalse(TEXT("NEW_ENEMY is cleared when there is no enemy to be new"),
			F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy));
	}

	// --- Losing LINE OF SIGHT is not losing the enemy ----------------------------------------------
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.Player == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		F.Guard->BaseMemory.Enemy = F.Player->Handle;
		F.Guard->GatherEnemyConditions(F.Player);   // slot 481 `0x10270b20`
		F.Services.bLineOfSightClear = false;
		for (int32 i = 0; i <= ElysiumNpcSense::EnemyLosFailureLimit; ++i)
		{
			F.Guard->GatherEnemyConditions(F.Player);
		}
		F.Flush(20.0);
		TestEqual(TEXT("a full LOS debounce fires neither lost-the-actor output"),
			F.Counter(TEXT("c_lostplayer")) + F.Counter(TEXT("c_lostenemy")), 0.f);
		TestTrue(TEXT("...and the enemy is still committed"),
			F.Guard->BaseMemory.Enemy == F.Player->Handle);
	}
	return true;
}

// The damage conditions, and the recovered 15%-in-one-second repeated-damage window.
//
// STORY8-TWIN: this case pins `ElysiumNpcCond::AccumulateDamage` / `GatherDamage`, the damage twin
// that still runs from the typed commit (no live path reaches slots 142/390 yet). Its 20 % heavy
// and 15 % repeated fractions are the twin's, not retail's: `0x10265ed0` raises HEAVY through slot
// 577 (`> 20.0`, `_DAT_1044eb0c`) at `0x10266293` and REPEATED at `m_iMaxHealth * 0.3
// (_DAT_1047b868) < m_flSumDamage`, reset when `curtime - m_flLastDamageTime >= 1.0` (`0x1026632e`,
// `0x102662a8`) — pinned by `Elysium.Substrate.NpcKernelDamage19.BaseOnTakeDamageAlive_10265ed0_*`.
// Delete this case with the twin at wave 2 (L13).

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

	auto Hit = [&Mem, &F](int32 Amount, double At)
	{
		Mem.LastDamageAmount = Amount;
		F.Guard->BaseMemory.LastDamageTime = At;
		ElysiumNpcCond::AccumulateDamage(F.Guard->BaseMemory, Amount, At);
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
			F.Guard->BaseMemory.RepeatedDamageAccumulated, 17);
		TestTrue(TEXT("a window sum over 15% of Source max health raises REPEATED_DAMAGE"),
			C.Has(EElysiumNpcCond::RepeatedDamage));
	}

	// A hit past the window RESETS it rather than decaying it, so the sum starts over.
	Hit(12, 12.0);
	{
		FElysiumNpcConditions C;
		ElysiumNpcCond::GatherDamage(*F.Guard, 11.9, C);
		TestEqual(TEXT("an expired window is reset, not decayed"),
			F.Guard->BaseMemory.RepeatedDamageAccumulated, 12);
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

// `SelectIdealState`, both layers. Corrected to retail: `CAI_BaseNPCTroika::SelectIdealState`
// (`0x102ad660`) gates every arm but four on `HasInterruptCondition` (`0x10269d30`), whose FIRST
// statement is `if (*(int *)(this + 0x5c38) == 0) return 0;` — so with no program installed a
// committed enemy does NOT take combat from idle and idle damage does not promote. The exceptions
// are the four flee arms (`0x21` at `0x452b`/`0x456b`, `0x1f` at `0x4532`/`0x4573`) and case
// `0xe`'s damage arms (`0x45f0`), which call the bare `HasCondition` (`0x10269aa0`) instead.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcEnemyIdealStateTest,
	"Elysium.Substrate.NpcEnemy.IdealState", GElysiumTestFlags)
bool FElysiumNpcEnemyIdealStateTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("enemy_ideal_state"), 0x49444c45);
	// The guard is the bare Troika line (story 5 step 2): the body under test is
	// `CAI_BaseNPCTroika::SelectIdealState` with no species override in the way.
	Builder.AddTroikaNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f));
	FElysiumNpcWorldFixture World(MoveTemp(Builder));
	FElysiumNpc* Guard = World.Npc(TEXT("guard"));
	FElysiumNpc* Other = World.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the guard leaf constructs"), Guard)
		|| !TestNotNull(TEXT("the other leaf constructs"), Other))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other });

	Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	ElysiumNpcEnemy::SetEnemy(*Guard, Other->Handle);
	TestEqual(TEXT("102ad660: a committed enemy does NOT take combat from idle with no schedule"),
		Guard->SelectIdealStateRetail(), 1);

	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("102ad660: idle LIGHT_DAMAGE does not promote with no schedule"),
		Guard->SelectIdealStateRetail(), 1);

	// `102ad660` case 1 `0x452b` / `0x4532`: the two idle flee arms are the BARE `HasCondition`,
	// they skip the `m_bNoAlertState` gate, and each ORs `0x100 INITIAL_FLEE` into
	// `m_bfAINPCFlags` before answering FLEE (8) — with no program installed at all.
	Guard->Cognition.Conditions.Reset();
	Guard->NpcFlags.Clear(EElysiumNpcFlag::INITIAL_FLEE);
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::SupernaturalFleeLevel);
	TestEqual(TEXT("102ad660 0x452b: idle SUPERNATURAL_FLEE takes FLEE with no schedule"),
		Guard->SelectIdealStateRetail(), 8);
	TestTrue(TEXT("...and ORs 0x100 INITIAL_FLEE"),
		Guard->NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));

	Guard->Cognition.Conditions.Reset();
	Guard->NpcFlags.Clear(EElysiumNpcFlag::INITIAL_FLEE);
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::CriminalFleeLevel);
	TestEqual(TEXT("102ad660 0x4532: idle CRIMINAL_FLEE takes FLEE with no schedule"),
		Guard->SelectIdealStateRetail(), 8);
	TestTrue(TEXT("...and ORs 0x100 INITIAL_FLEE"),
		Guard->NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));

	// The same two arms in case 3 (`0x456b` / `0x4573`), which is otherwise untested.
	Guard->WriteNpcStateRetail(3);
	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::SupernaturalFleeLevel);
	TestEqual(TEXT("102ad660 0x456b: alert SUPERNATURAL_FLEE takes FLEE with no schedule"),
		Guard->SelectIdealStateRetail(), 8);
	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::CriminalFleeLevel);
	TestEqual(TEXT("102ad660 0x4573: alert CRIMINAL_FLEE takes FLEE with no schedule"),
		Guard->SelectIdealStateRetail(), 8);

	// `102ad660` case 3 `0x4562`: alert → combat is the INTERRUPT form, so it too needs the mask.
	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
	Guard->WriteIdealStateRetail(3);
	TestEqual(TEXT("102ad660 0x4562: alert NEW_ENEMY does not take combat with no schedule"),
		Guard->SelectIdealStateRetail(), 3);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::NewEnemy);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(Guard->Schedule, ElysiumSched::IDLE_STAND, *Guard);
		Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		TestEqual(TEXT("...and does with one"), Guard->SelectIdealStateRetail(), 2);
	}
	Guard->Schedule.Clear();

	// `102ad660` case `0xe`: the three damage arms are the bare form, and the attacker must BE the
	// committed enemy (`0x45f0`).
	Guard->WriteNpcStateRetail(0xe);
	Guard->WriteIdealStateRetail(0xe);
	Guard->Cognition.Conditions.Reset();
	Guard->Cognition.Conditions.Set(EElysiumNpcCond::HeavyDamage);
	ElysiumNpcEnemy::SetEnemy(*Guard, Other->Handle);
	Guard->BaseMemory.LastDamageAttacker = Other->Handle;
	TestEqual(TEXT("102ad660 0x45f0: case 0xe HEAVY_DAMAGE from the enemy takes combat, bare"),
		Guard->SelectIdealStateRetail(), 2);

	// `1026f660` case 1 `0x137c`: HEAVY_DAMAGE promotes idle → alert in the BASE, and
	// `no_alert_state` does not gate it — the Troika layer skips its own `0x4522` arm and the
	// unconditional base tail promotes anyway. This is the load-bearing two-layer assertion.
	Guard->WriteNpcStateRetail(1);
	Guard->WriteIdealStateRetail(1);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::HeavyDamage);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(Guard->Schedule, ElysiumSched::IDLE_STAND, *Guard);
		Guard->Cognition.Conditions.Reset();
		Guard->Cognition.Conditions.Set(EElysiumNpcCond::HeavyDamage);
		Guard->bNoAlertState = true;
		TestEqual(TEXT("1026f660 0x137c: no_alert_state still promotes on HEAVY_DAMAGE"),
			Guard->SelectIdealStateRetail(), 3);
	}
	Guard->Schedule.Clear();

	Guard->WriteNpcStateRetail(1);
	Guard->WriteIdealStateRetail(1);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::LightDamage);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(Guard->Schedule, ElysiumSched::IDLE_STAND, *Guard);
		Guard->Cognition.Conditions.Reset();
		Guard->Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
		Guard->bNoAlertState = true;
		TestEqual(TEXT("1026f660 0x1373: and on LIGHT_DAMAGE — the base tail has no such test"),
			Guard->SelectIdealStateRetail(), 3);
	}

	// `1026f660` case 1 `0x1397`: the hear family, again with `no_alert_state` set, and the base's
	// own extra gate — slot 474's type word must be 1, 8 or 0x10.
	Guard->WriteNpcStateRetail(1);
	Guard->WriteIdealStateRetail(1);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::HearCombat);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(Guard->Schedule, ElysiumSched::IDLE_STAND, *Guard);
		Guard->Cognition.Conditions.Reset();
		Guard->Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
		Guard->bNoAlertState = true;
		Guard->Senses.Memory.BestSound.TypeMask = ElysiumGameSounds::Combat;
		TestEqual(TEXT("1026f660 0x1397: no_alert_state still promotes on the hear family"),
			Guard->SelectIdealStateRetail(), 3);
		Guard->WriteIdealStateRetail(1);
		Guard->Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
		Guard->Senses.Memory.BestSound.TypeMask = ElysiumGameSounds::Carcass;
		TestEqual(TEXT("...but a sound type outside {1, 8, 0x10} refuses the arm"),
			Guard->SelectIdealStateRetail(), 1);
	}
	Guard->Schedule.Clear();
	Guard->bNoAlertState = false;
	Guard->Senses.Memory.BestSound.TypeMask = 0;

	// `1026f660` case 7 `0x13dd`: the dead state WRITES 7 rather than being left alone.
	Guard->WriteNpcStateRetail(7);
	Guard->WriteIdealStateRetail(1);
	Guard->Cognition.Conditions.Reset();
	TestEqual(TEXT("1026f660 0x13dd: case 7 writes the dead state"),
		Guard->SelectIdealStateRetail(), 7);

	// `1026f660` case 4: a scripted NPC with none of `0x5c`/`0x4c`/`0x4d` falls to the default and
	// answers `m_IdealNPCState` untouched.
	Guard->WriteNpcStateRetail(4);
	Guard->WriteIdealStateRetail(4);
	TestEqual(TEXT("1026f660 case 4 quiet answers m_IdealNPCState unchanged"),
		Guard->SelectIdealStateRetail(), 4);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::TaskFailed);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(Guard->Schedule, ElysiumSched::IDLE_STAND, *Guard);
		Guard->Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
		const int32 ExitsBefore = Guard->SelectIdealStateScriptExitCalls;
		TestEqual(TEXT("...and TASK_FAILED still answers it unchanged"),
			Guard->SelectIdealStateRetail(), 4);
		// TWICE, and that is retail: `102ad660` case 4 runs `0x1027d0a0` and then `break`s into
		// the unconditional `return CAI_BaseNPC::SelectIdealState(this)`, whose own case 4
		// (`0x5c`/`0x4c`/`0x4d`) runs it again. Both layers see the same standing condition.
		TestEqual(TEXT("...having run the script exit (0x1027d0a0) once per layer"),
			Guard->SelectIdealStateScriptExitCalls, ExitsBefore + 2);
	}
	Guard->Schedule.Clear();

	// `1026f660` case 2 `0x13cc`: combat with no enemy writes 3 and emits. The ideal is seeded to 2
	// first so the answer can only come from that arm.
	Guard->Cognition.Conditions.Reset();
	Guard->WriteNpcStateRetail(2);
	Guard->WriteIdealStateRetail(2);
	ElysiumNpcEnemy::SetEnemy(*Guard, FElysiumEntityHandle::Invalid());
	AddExpectedError(TEXT("Combat state with no enemy"), EAutomationExpectedErrorFlags::Contains, 0);
	TestEqual(TEXT("1026f660 0x13cc combat with no enemy falls back to alert"),
		Guard->SelectIdealStateRetail(), 3);

	// The converse: `goto default; return m_IdealNPCState` — the arm does NOT write, so a seeded
	// ideal of 3 survives a live enemy.
	Guard->WriteNpcStateRetail(2);
	Guard->WriteIdealStateRetail(3);
	ElysiumNpcEnemy::SetEnemy(*Guard, Other->Handle);
	TestEqual(TEXT("1026f660 combat with an enemy answers m_IdealNPCState unchanged"),
		Guard->SelectIdealStateRetail(), 3);
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
	FElysiumNpcWorldFixture::Wake({ F.Guard }, 1.0);
	F.World.Tick(1.0);
	FElysiumNpcWorldFixture::Wake({ F.Guard }, 2.0);
	F.World.Tick(2.0);
	F.Quiet();

	// A heard combat sound promotes idle -> alert through the real think.
	FElysiumGameSoundRequest Sound;
	Sound.Category = FName(TEXT("PLAYER_GUNSHOT_BASE"));
	Sound.TypeMask = ElysiumGameSounds::Combat;
	Sound.Position = F.Guard->EyePosition();
	F.World.GameSounds().Emit(Sound, 3.0);
	// Two retail passes: slot 433's `PerformSensing` (`0x1026ee04`) hears at 3.0 and promotes the
	// delayed sound on the pass at 3.91, whose `OnListened` merge raises the HEAR bit.
	FElysiumNpcWorldFixture::GatherConditionsTickedTo(*F.Guard, 3.0);
	FElysiumNpcWorldFixture::GatherConditionsTickedTo(*F.Guard, 3.91);
	TestTrue(TEXT("the heard stimulus raises HEAR_COMBAT"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::HearCombat));
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::HearCombat);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(F.Guard->Schedule, ElysiumSched::IDLE_STAND, *F.Guard);
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
		F.Guard->UpdateIdealState(3.5);
	}
	TestTrue(TEXT("the NPC promotes to alert"),
		F.Guard->GetMind().State() == EElysiumNpcState::Alert);

	// Alert selects `0x4b SCHED_TROIKA_ALERT_WAIT`, never the lookaround: `CAI_BaseNPCTroika::
	// SelectSchedule` case 3 ends at `0x102af961..0x102af985` (`m_bGoToIdleState = m_bForceStateChange
	// = 1`, `MOV EAX,0x4b`); the lookaround `0x4f` is read ONLY in case 1 (`0x102af829`).
	// `conditions-and-states.md` § "The port's NPC-core guesses, settled" 1. (Story 8 L06.)
	TestEqual(TEXT("alert selects ALERT_WAIT 0x4b"), F.Guard->SelectSchedule(), 0x4b);

	// A committed enemy takes it to combat, and combat selects a real fight program.
	F.Hate(F.ThugA, 5);
	F.Guard->BaseMemory.Enemy = F.ThugA->Handle;
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::NewEnemy);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(F.Guard->Schedule, ElysiumSched::IDLE_STAND, *F.Guard);
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		F.Guard->UpdateIdealState(4.0);
	}
	TestTrue(TEXT("a committed enemy takes the NPC to combat"),
		F.Guard->GetMind().State() == EElysiumNpcState::Combat);

	// The guard is unarmed — a content-free fixture installs no item catalogue. Story 8 L06
	// integration (corrected to retail): `CNPC_VHuman::SelectSchedule` then reads weapon word 0
	// (`0x10385008`) and asks the RANGED slot 605 (`0x10385022`), not the melee one.
	FElysiumNpcWorldFixture::GatherConditionsTickedTo(*F.Guard, 4.0);
	// NEW_ENEMY, set above for the promotion, stands until `SetSchedule` (`0x10280e50`) zeroes the
	// word -- the retail pass clears nothing it does not own -- and the Troika combat ladder answers
	// its own NEW_ENEMY arm first. The next program install clears it; stated here.
	F.Guard->Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
	TestTrue(TEXT("a distant committed enemy raises TOO_FAR_TO_ATTACK"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::TooFarToAttack));
	// The recovered slot-605 body's own answer, a loaded program.
	const int32 Ranged605 = F.Guard->SelectScheduleRangedCombat(0);
	TestNotEqual(TEXT("slot 605 answers for the unarmed guard"), Ranged605, 0);
	TestEqual(TEXT("combat selects slot 605's answer rather than an idle stance"),
		F.Guard->SelectSchedule(), Ranged605);
	TestNotNull(TEXT("...a loaded program"), ElysiumScheduleFor(ElysiumScheduleGlobalId(Ranged605)));
	TestEqual(TEXT("...and does so every pass, with no once-latched refusal in the way"),
		F.Guard->SelectSchedule(), Ranged605);
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

	// The producers. Retail has exactly two INCREMENTS of `m_iEnemySightings` (+0x60a8), both
	// inside the sense pass (the third writer is the spawn reset `0x1029a0b0`):
	//   - Troika `OnLooked` `FUN_102b39a0` (slot 469): base call, then
	//     `if (HasCondition(NEW_ENEMY 0x54)) ++m_iEnemySightings`.
	//   - the outer-band see-unknown path `FUN_102b3e00` @ `0x102b3e90`: incremented immediately
	//     after `m_hBestSeeUnknown` (+0x6088) takes a NEW candidate.
	// `FElysiumNpcSenses::TickSight` reproduces both. There is NO writer in the committed-enemy LOS
	// edge and no player-only rule; this test previously asserted both, and neither is in the image.
	{
		FEnemyFixture F;
		if (F.Guard == nullptr || F.Player == nullptr || F.ThugA == nullptr)
		{
			return false;
		}
		F.Player->Origin = FVector(Cm(100.f), 0.0, 0.0);
		TestEqual(TEXT("an NPC that has never seen an enemy starts at zero"),
			F.Guard->EnemySightings, 0);

		// The committed-enemy LOS edge is NOT a writer, however many times it fires.
		F.Guard->BaseMemory.Enemy = F.Player->Handle;
		F.Guard->GatherEnemyConditions(F.Player);   // slot 481 `0x10270b20`
		TestTrue(TEXT("the found edge latched (m_afMemory 0x20000)"),
			(F.Guard->BaseScheduleHost.MemoryBits & 0x20000u) != 0);
		TestEqual(TEXT("...but the LOS edge does not count a sighting: retail has no writer there"),
			F.Guard->EnemySightings, 0);
		for (int32 i = 0; i < 5; ++i)
		{
			F.Guard->GatherEnemyConditions(F.Player);
		}
		TestEqual(TEXT("...nor does staying in sight"), F.Guard->EnemySightings, 0);

		// `OnLooked`: the sense pass counts one when NEW_ENEMY stands.
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		F.Guard->Senses.TickSight(*F.Guard, 10.0);
		TestEqual(TEXT("a sense pass with NEW_ENEMY standing counts one sighting"),
			F.Guard->EnemySightings, 1);

		// ...and nothing when it does not. This is the whole condition: the enemy's identity is
		// never consulted, so a non-player acquisition counts exactly the same.
		F.Guard->Cognition.Conditions.Clear(EElysiumNpcCond::NewEnemy);
		F.Guard->Senses.TickSight(*F.Guard, 11.0);
		TestEqual(TEXT("a sense pass without NEW_ENEMY counts nothing"),
			F.Guard->EnemySightings, 1);

		ElysiumNpcEnemy::SetEnemy(*F.Guard, F.ThugA->Handle);
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::NewEnemy);
		F.Guard->Senses.TickSight(*F.Guard, 12.0);
		TestEqual(TEXT("an NPC acquisition counts too: there is no player-only rule"),
			F.Guard->EnemySightings, 2);
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
				// Story 8 L06 integration: the roll is `0x102af660` case 1's (`0x102af804..0x102af829`);
				// the port-only `SelectIdleSchedule` is deleted.
				if (F.Guard->TroikaSelectSchedule() == ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI)
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

	// --- The door-obstruction family's masks, as its texts declare them --------------------------
	//
	// This block used to assert that all three were EMPTY, and called that "a real recovered
	// posture". It was not recovered -- it was the absence of a decoded registration site, and the
	// hand-typed programs left the field default. Retail's own texts declare seven, nine and two
	// conditions; `SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE` alone admits `COND_NEW_ENEMY`,
	// `COND_SEE_ENEMY`, `COND_LIGHT_DAMAGE`, `COND_SEE_FEAR`, `COND_HEAVY_DAMAGE`,
	// `COND_SQUAD_LOS_ENEMY` and `COND_HEAR_FLANK_SOUND`. An NPC backing out of a doorway was
	// uninterruptible in this port and is not in the game.
	for (const int32 Id : { ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE,
		ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE,
		ElysiumSched::SCHED_TROIKA_TAKE_COVER_HINT_DOOR })
	{
		const FElysiumScheduleProgram* Schedule =
			ElysiumScheduleFor(ElysiumScheduleGlobalId(Id));
		if (!TestNotNull(TEXT("the program is loaded"), Schedule))
		{
			return false;
		}
		TestTrue(*FString::Printf(TEXT("%s declares an interrupt mask"),
			ElysiumScheduleName(ElysiumScheduleGlobalId(Id))),
			Schedule->Interrupts.Has(EElysiumNpcCond::NewEnemy));
		TestFalse(TEXT("...and does not claim DELAY_INTERRUPTS"),
			Schedule->HasFlag(ElysiumScheduleFlags::DelayInterrupts));
	}

	// The one mask that IS empty, and the only one whose emptiness was ever recovered rather than
	// merely undecoded: the terminal swing declares `Interrupts` with nothing after it.
	if (const FElysiumScheduleProgram* Swing = ElysiumScheduleFor(
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING)))
	{
		TestTrue(TEXT("SCHED_TROIKA_MELEE_ATTACK1_SWING really declares nothing"),
			Swing->Interrupts.IsEmpty());
	}

	// --- A no-interrupt schedule finishes its task despite the conditions -------------------------
	// `SCHED_TROIKA_MELEE_ATTACK1_SWING` is the recovered empty mask: "once that terminal attack
	// task owns the NPC it is not reevaluated as a fresh attack choice each tick."
	{
		FKernelRunner Runner;
		FElysiumScheduleState State;
		// The terminal swing, whose EMPTY mask is recovered. Its two tasks are
		// `TASK_ANNOUNCE_ATTACK 1` and `TASK_MELEE_ATTACK1`, and a runner with no weapon refuses
		// both -- so the claim is made of the MASK rather than of the program's survival, which is
		// what the case is about.
		TestTrue(TEXT("the terminal swing program starts"),
			ElysiumSchedule::Start(State,
				ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, Runner));
		TestTrue(TEXT("a schedule with no mask lists nothing to be interrupted by"),
			ElysiumSchedule::EffectiveInterrupts(State, Runner)
				.Intersection(Firing).IsEmpty());
		TestFalse(TEXT("...so the storm does not raise an interrupt"),
			ElysiumSchedule::HasInterruptCondition(State, Runner, Firing,
				EElysiumNpcCond::NewEnemy));
	}

	// --- A masked condition aborts mid-schedule, into reselection rather than the fail schedule ---
	{
		FKernelRunner Runner;
		FElysiumScheduleState State;
		// The cover program is the only registered one with a fail schedule, so it is what proves
		// an interrupt does not take that route.
		ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::SCHED_TROIKA_TAKE_COVER_HINT_DOOR,
			FElysiumNpcConditions::Of({ EElysiumNpcCond::NewEnemy }));
		TestTrue(TEXT("the cover program starts"),
			ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_TAKE_COVER_HINT_DOOR, Runner));
		TestTrue(TEXT("its first task runs"),
			ElysiumSchedule::Tick(State, Runner, 0.0, nullptr));
		TestTrue(TEXT("...leaving it mid-program"), State.IsRunning());

		TestFalse(TEXT("a masked condition ends the program"),
			ElysiumSchedule::Tick(State, Runner, 0.1, &Firing));
		TestFalse(TEXT("...and nothing is running afterwards"), State.IsRunning());
		TestFalse(TEXT("...specifically NOT its fail schedule, which is task failure's route"),
			State.Current == ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE));
		TestTrue(TEXT("the trace names the condition that fired"), Runner.Saw(TEXT("NEW_ENEMY")));
	}

	// --- A condition outside the mask does not interrupt ------------------------------------------
	FElysiumNpcConditions IdleMaskBefore;
	if (const FElysiumScheduleProgram* Idle = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)))
	{
		IdleMaskBefore = Idle->Interrupts;
	}
	{
		FKernelRunner Runner;
		FElysiumScheduleState State;
		// The scope REPLACES the registered mask rather than adding to it, which is what makes this
		// a statement about the mask under test and not about the idle program's own.
		ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION,
			FElysiumNpcConditions::Of({ EElysiumNpcCond::EnemyDead }));
		ElysiumSchedule::Start(State, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Runner);
		ElysiumSchedule::Tick(State, Runner, 0.0, nullptr);
		TestTrue(TEXT("a condition the mask does not carry leaves the program alone"),
			ElysiumSchedule::Tick(State, Runner, 0.1, &Firing));
	}

	// --- The mask scope restores what it borrowed --------------------------------------------------
	{
		const FElysiumScheduleProgram* Idle = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION));
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

	// The identities are retail's own numbers, from the base condition table `0x102c8ce0`
	// (`docs/vtmb/npc-ai/conditions-and-states.md` -> "The base condition table"). The seven that
	// used to sit in a placeholder band above 0x66 are decoded too, so the whole enum is one
	// namespace with the binary's now.
	TestEqual(TEXT("SEE_HATE is 0x43"), static_cast<int32>(EElysiumNpcCond::SeeHate), 0x43);
	TestEqual(TEXT("HAVE_ENEMY_LOS is 0x4a"),
		static_cast<int32>(EElysiumNpcCond::HaveEnemyLos), 0x4a);
	TestEqual(TEXT("NEW_ENEMY is 0x54"), static_cast<int32>(EElysiumNpcCond::NewEnemy), 0x54);
	TestEqual(TEXT("SEE_NEMESIS is 0x5b"), static_cast<int32>(EElysiumNpcCond::SeeNemesis), 0x5b);
	TestEqual(TEXT("SEE_ENEMY is 0x46"), static_cast<int32>(EElysiumNpcCond::SeeEnemy), 0x46);
	TestEqual(TEXT("SEE_FEAR is 0x44"), static_cast<int32>(EElysiumNpcCond::SeeFear), 0x44);
	TestEqual(TEXT("HEAR_COMBAT is 0x6d"), static_cast<int32>(EElysiumNpcCond::HearCombat), 0x6d);
	TestEqual(TEXT("HEAR_DANGER is 0x6a"), static_cast<int32>(EElysiumNpcCond::HearDanger), 0x6a);
	TestEqual(TEXT("INVESTIGATE_LEVEL is 0x1e"),
		static_cast<int32>(EElysiumNpcCond::InvestigateLevel), 0x1e);

	C.Set(EElysiumNpcCond::SeeHate);
	C.Set(EElysiumNpcCond::HearCombat);
	TestTrue(TEXT("a set condition is present"), C.Has(EElysiumNpcCond::SeeHate));
	TestTrue(TEXT("...and a second one beside it"), C.Has(EElysiumNpcCond::HearCombat));
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
	// Through the real persistence path: `m_hEnemy`, `m_iEnemySightings`, `m_flSumDamage` and
	// `m_flLastDamageTime` are retail `SAVE` rows the generated datamap walk carries since 0019/2
	// pass C, while the `CAI_Memory` record list stays in the leaf blob (retail's own nested
	// datamap). Only `Freeze`/`ApplySnapshot` drives both, and only it runs the restore hook.
	FEnemyFixture F;
	FEnemyFixture G;
	if (F.Guard == nullptr || F.Player == nullptr || G.Guard == nullptr || G.Player == nullptr)
	{
		return false;
	}
	F.Guard->EnemySightings = 3;
	F.Guard->bNoAlertState = true;
	F.Guard->BaseMemory.Enemy = F.Player->Handle;
	F.Guard->EnemyMemory.Update(*F.Guard, F.Player->Handle, 12.5);
	F.Guard->EnemyMemory.MarkEluded(F.Player->Handle);
	if (FElysiumNpcEnemyMemoryRecord* Record = F.Guard->EnemyMemory.FindMutable(F.Player->Handle))
	{
		Record->LastPosition = FVector(101.f, 202.f, 303.f);
		Record->Anchor = FVector(404.f, 505.f, 606.f);
		Record->Velocity = FVector(7.f, 8.f, 9.f);
		Record->LastNavNode = 11;
		Record->AnchorNavNode = 12;
	}
	F.Guard->BaseMemory.RepeatedDamageWindowStart = 12.5;
	F.Guard->BaseMemory.RepeatedDamageAccumulated = 17;
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::SeeHate);

	ElysiumRoundTripSnapshot(F.World, G.World);

	const FElysiumNpcMemory& Restored = G.Guard->Senses.Memory;
	TestTrue(TEXT("the committed enemy survives"), G.Guard->BaseMemory.Enemy == G.Player->Handle);
	TestEqual(TEXT("the sighting count survives, through the generated walk"),
		G.Guard->EnemySightings, 3);
	TestTrue(TEXT("the eluded marker survives"), G.Guard->EnemyMemory.IsEluded(G.Player->Handle));
	const FElysiumNpcEnemyMemoryRecord* RestoredRecord =
		G.Guard->EnemyMemory.Find(G.Player->Handle);
	if (!TestNotNull(TEXT("the actor record rebase survives"), RestoredRecord))
	{
		return false;
	}
	TestEqual(TEXT("the record's last position survives"), RestoredRecord->LastPosition,
		FVector(101.f, 202.f, 303.f));
	TestEqual(TEXT("...with its anchor"), RestoredRecord->Anchor, FVector(404.f, 505.f, 606.f));
	TestEqual(TEXT("...velocity and nav identities"), RestoredRecord->LastNavNode, 11);
	TestEqual(TEXT("...and its anchor nav identity"), RestoredRecord->AnchorNavNode, 12);
	TestEqual(TEXT("the repeated-damage window sum survives"),
		G.Guard->BaseMemory.RepeatedDamageAccumulated, 17);
	TestTrue(TEXT("...with its window root"),
		FMath::IsNearlyEqual(G.Guard->BaseMemory.RepeatedDamageWindowStart, 12.5, 0.001));

	// Conditions are NOT saved: they are rebuilt from the memory above on the first think after a
	// load, which is what the recovered pass does on every think anyway.
	TestTrue(TEXT("the gathered conditions do not travel in the payload"),
		G.Guard->Cognition.Conditions.IsEmpty());
	// `m_bConditionsGathered` (+0x5ca4) is not a datamap row: a restored body has not gathered. (The
	// port's old stamp-the-load-time edge served its re-deriving gather, deleted at story 8 wave 2.)
	TestTrue(TEXT("m_bConditionsGathered is clear on restore"), G.Guard->Cognition.GatheredAt < 0.0);

	// The hook's own half: a record whose actor the restored world no longer carries is dropped
	// rather than left naming a dead index (`FElysiumNpcEnemyMemory::Rebase`).
	{
		FEnemyFixture H;
		FEnemyFixture I;
		if (H.Guard == nullptr || H.Player == nullptr || I.Guard == nullptr)
		{
			return false;
		}
		H.Guard->EnemyMemory.Update(*H.Guard, H.Player->Handle, 5.0);
		if (FElysiumNpcEnemyMemoryRecord* Row = H.Guard->EnemyMemory.FindMutable(H.Player->Handle))
		{
			Row->Handle = FElysiumEntityHandle(9999, 1);   // an index no world ever had
			Row->bPositionOnly = false;
		}
		ElysiumRoundTripSnapshot(H.World, I.World);
		TestEqual(TEXT("a record naming an actor the restored world lacks is dropped"),
			I.Guard->EnemyMemory.Num(), 0);
	}
	return true;
}

}   // namespace ElysiumNpcEnemyTests

#endif // WITH_DEV_AUTOMATION_TESTS
