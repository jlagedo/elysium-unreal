// Arm tests: the melee contact against retail (spec 0002 V11-2, differences D1-D8, D10, D11).
//
// `Elysium.Arm.MeleeSwingStep.*` is `CBaseCombatCharacter::MeleeSwingStep 0x10343020` -- the
// per-record hit test: the NPC's relation filter, the sample rays, the candidates and their gates,
// the wall arm. `Elysium.Arm.MeleeContact.*` is the weapon's slot 270 `0x102579f0` -- no roll,
// the dispatch condition, the side effects, the reaction.
//
// Every case drives the real walk (`FElysiumWeapon::AdvanceSwingContact`) against the recording
// embodiment: what the sweep's box holds and what a sample ray clips are the seam's answers,
// every gate and every commit is the substrate's.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumSwingContact.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumMeleeTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumMeleeSwingStepTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	using EC = EElysiumTraitContainer;

	// Suite-local classnames: `ElysiumItems::Install` registers a class once per process.
	const TCHAR* const GClub = TEXT("item_w_swingstep_club");
	// Lethality 0: against a defence that fails safe at 0 the margin is never positive.
	const TCHAR* const GTwig = TEXT("item_w_swingstep_twig");

	const TCHAR* const GSwingLabel = TEXT("swing_step");
	const TCHAR* const GSwingBank = TEXT("cast_bank");
	const TCHAR* const GSwingBone = TEXT("Bip01 R Hand");

	const FVector GVictimOrigin = FVector::ZeroVector;
	const FVector GAttackerOrigin = FVector(100.0f, 0.0f, 0.0f);

	FElysiumWeaponMode MakeMode(const TCHAR* Dmg, int32 BaseLethality)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = BaseLethality;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = 0.5f;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeTable()
	{
		FElysiumItemTable Table;
		for (const TPair<const TCHAR*, int32>& Row : { TPair<const TCHAR*, int32>(GClub, 10),
			TPair<const TCHAR*, int32>(GTwig, 0) })
		{
			FElysiumItemDef Item;
			Item.Classname = Row.Key;
			Item.PrintName = Row.Key;
			Item.Type = EElysiumItemType::WeaponMelee;
			Item.bWieldable = true;
			Item.Modes.Add(MakeMode(TEXT("3 Bashing Close_Combat_Melee DMG_CLUB"), Row.Value));
			Table.Items.Add(MoveTemp(Item));
		}
		Table.Reindex();
		return Table;
	}

	FElysiumEntityDefs MakeDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__melee_swing_step_test__");

		auto AddNpc = [&Defs](const TCHAR* Name, const FVector& Origin, bool bTemplate)
		{
			FElysiumEntityDef Def;
			Def.Classname = TEXT("npc_VHumanCombatant");
			Def.TargetName = Name;
			Def.Origin = Origin;
			Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
			if (bTemplate)
			{
				// Targetable: NPCInit sets `m_bIsBCCTargetable (+0x1480)` only with a template
				// (`0x1029a4a0`).
				Def.Keys.Add(TEXT("stattemplate"), TEXT("Thug"));
			}
			Defs.Defs.Add(MoveTemp(Def));
		};
		AddNpc(TEXT("attacker"), GAttackerOrigin, /*bTemplate*/ true);
		AddNpc(TEXT("victim"), GVictimOrigin, /*bTemplate*/ true);
		// No template: `m_bIsBCCTargetable` stays 0.
		AddNpc(TEXT("untargetable"), FVector(0.0f, 4000.0f, 0.0f), /*bTemplate*/ false);

		// A non-character (`+0x9c == 0`): no class answers `prop_base`, so it is a plain entity.
		FElysiumEntityDef Crate;
		Crate.Classname = TEXT("prop_base");
		Crate.TargetName = TEXT("crate");
		Crate.Origin = FVector(40.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Crate));
		return Defs;
	}

	void SeedHealth(FElysiumCombatCharacter& Char, int32 MaxHealth)
	{
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::MaxHealth, MaxHealth);
		Char.Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 0);
		Char.RecomputeSheet();
	}

	int32 DamageTaken(const FElysiumCombatCharacter& Char)
	{
		return Char.Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health);
	}

	int32 CountCalls(const FElysiumRecordingServices& Services, const TCHAR* Prefix,
		const TCHAR* Contains = nullptr)
	{
		int32 Count = 0;
		for (const FString& Call : Services.Calls)
		{
			if (Call.StartsWith(Prefix) && (Contains == nullptr || Call.Contains(Contains)))
			{
				++Count;
			}
		}
		return Count;
	}

	FElysiumWeapon* GiveWeapon(FElysiumCombatCharacter& Char, const TCHAR* Classname)
	{
		const FElysiumEntityHandle Handle = Char.Inventory.GiveNamedItem(Char, Classname);
		FElysiumEntity* Ent = Char.World ? Char.World->Resolve(Handle) : nullptr;
		FElysiumItem* Item = Ent ? Ent->AsItem() : nullptr;
		return Item ? Item->AsWeapon() : nullptr;
	}

	struct FStepFixture
	{
		FElysiumRecordingServices Services;
		TUniquePtr<FElysiumEntityWorld> World;
		FElysiumCombatCharacter* Attacker = nullptr;
		FElysiumCombatCharacter* Victim = nullptr;
		FElysiumCombatCharacter* Untargetable = nullptr;
		FElysiumCombatCharacter* Player = nullptr;
		FElysiumEntity* Crate = nullptr;
		FElysiumWeapon* Weapon = nullptr;

		bool Stand(FAutomationTestBase& Test, const TArray<FElysiumSwingRecord>* Records = nullptr)
		{
			ElysiumRng::SeedAll(0x56313132);
			Services.bNpcActivitiesResolve = true;
			Services.ResolvedNpcActivityLabel = GSwingLabel;
			Services.ResolvedNpcActivityClip = GSwingLabel;
			Services.ResolvedNpcActivityOwner = GSwingBank;
			Services.bNpcOneShotsPlay = true;
			Services.bBodyClipPhaseSet = true;
			Services.BodyClipPhase = FElysiumClipPhase();
			Services.BodyClipPhase.OwnerStem = GSwingBank;
			Services.BodyClipPhase.Label = GSwingLabel;
			Services.BodyClipPhase.Length = 1.0f;
			Services.BodyClipPhase.PlayId = 1;
			Services.BoneFrames.Add(FString(GSwingBone).ToLower(), FTransform::Identity);
			if (Records != nullptr)
			{
				Services.SwingsByClip.Add(FString(GSwingLabel).ToLower(), *Records);
			}
			else
			{
				Services.SwingsByClip.Add(FString(GSwingLabel).ToLower(), { Window(0.30f, 0.70f) });
			}

			World = MakeUnique<FElysiumEntityWorld>(nullptr, nullptr, Services.Bundle());
			World->Load(MakeDefs());
			World->SpawnPlayer();
			World->Activate(0.0);
			World->Tick(0.0);

			auto Find = [this](const TCHAR* Name)
			{
				FElysiumEntity* Ent = World->FindByName(Name);
				return Ent ? Ent->AsCombatCharacter() : nullptr;
			};
			Attacker = Find(TEXT("attacker"));
			Victim = Find(TEXT("victim"));
			Untargetable = Find(TEXT("untargetable"));
			Crate = World->FindByName(TEXT("crate"));
			Player = World->FindPlayer();
			if (!Test.TestNotNull(TEXT("the attacker exists"), Attacker)
				|| !Test.TestNotNull(TEXT("the victim exists"), Victim)
				|| !Test.TestNotNull(TEXT("the untargetable NPC exists"), Untargetable)
				|| !Test.TestNotNull(TEXT("the crate exists"), Crate)
				|| !Test.TestNotNull(TEXT("the player exists"), Player))
			{
				return false;
			}
			Player->SetRuntimeModel(TEXT("models/character/pc/male/male_pc.mdl"));
			for (FElysiumCombatCharacter* Char : { Attacker, Victim, Untargetable, Player })
			{
				SeedHealth(*Char, 100000);
			}
			Attacker->Origin = GAttackerOrigin;
			Attacker->Angles = FVector(0.0f, 180.0f, 0.0f);   // Source yaw 180: facing the victim
			Victim->Origin = GVictimOrigin;
			Victim->Angles = FVector::ZeroVector;
			Player->Origin = GAttackerOrigin;
			Player->Angles = FVector(0.0f, 180.0f, 0.0f);
			Services.Calls.Reset();
			return true;
		}

		static FElysiumSwingRecord Window(float Start, float End, float LengthCm = 30.0f)
		{
			FElysiumSwingRecord Record;
			Record.Start = Start;
			Record.End = End;
			Record.Bone = GSwingBone;
			Record.BCm = FVector(LengthCm, 0.0f, 0.0f);
			return Record;
		}

		// Slot 404's answer for `Target`, as the relationship table gives it.
		void Relate(FElysiumCombatCharacter& Self, const FElysiumEntity& Target, EElysiumRelationship Value)
		{
			if (FElysiumNpc* Npc = Self.AsNpc())
			{
				Npc->Relationships.AddEntityRelationship(Target.Handle, Value, 10);
			}
		}

		// One accepted swing by `Swinger`, walked into the authored window: the first frame is the
		// swing's first live one (the roll and the notice are staged), the second carries the
		// cycle to 0.5.
		bool Swing(FAutomationTestBase& Test, FElysiumCombatCharacter& Swinger, const TCHAR* Classname)
		{
			Weapon = GiveWeapon(Swinger, Classname);
			if (!Test.TestNotNull(TEXT("the swinger is armed"), Weapon))
			{
				return false;
			}
			const FElysiumWeapon::EVerdict Verdict = Weapon->AttackIntent(FElysiumWeapon::EIntent::Primary);
			if (!Test.TestEqual(TEXT("the swing is accepted"), static_cast<int32>(Verdict),
				static_cast<int32>(FElysiumWeapon::EVerdict::Accepted)))
			{
				return false;
			}
			World->Tick(1.0);
			Services.BodyClipPhase.Cycle = 0.0f;
			World->AdvanceMeleeSwings(0.02f);
			Services.BodyClipPhase.Cycle = 0.50f;
			World->AdvanceMeleeSwings(0.02f);
			return true;
		}

		int32 Impacts() const
		{
			return Weapon != nullptr ? Weapon->MeleeImpactSeams.CombatSoundInserts : 0;
		}
	};
}

// --- D1: the NPC attacker's relation filter, `0x1034394d..0x103439a7` ------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSwingStepRelationTest,
	"Elysium.Arm.MeleeSwingStep.Relation", GElysiumTestFlags)
bool FElysiumMeleeSwingStepRelationTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	struct FCase
	{
		EElysiumRelationship Relation;
		bool bHit;
		const TCHAR* What;
	};
	static const FCase Cases[] =
	{
		{ EElysiumRelationship::Neutral, false, TEXT("0x1034394d: an NPC's swing skips a character slot 404 answers D_NU for") },
		{ EElysiumRelationship::Like,    false, TEXT("0x1034394d: an NPC's swing skips a character slot 404 answers D_LI for") },
		{ EElysiumRelationship::Hate,    true,  TEXT("0x1034394d: an NPC's swing lands on a character slot 404 answers D_HT (1) for") },
		{ EElysiumRelationship::Fear,    true,  TEXT("0x1034394d: an NPC's swing lands on a character slot 404 answers D_FR (2) for") },
	};
	for (const FCase& Case : Cases)
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, Case.Relation);
		F.Services.SwingContacts = { F.Victim->Handle };
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestTrue(Case.What, (F.Impacts() > 0) == Case.bHit);
		TestTrue(TEXT("...and the damage follows the hit"), (DamageTaken(*F.Victim) > 0) == Case.bHit);
	}

	// A player attacker has no relation filter (`this+0x94 == 0`): the same neutral NPC is hit.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Attacker->Origin = FVector(0.0f, 8000.0f, 0.0f);   // out of the way
		F.Services.SwingContacts = { F.Victim->Handle };
		if (!F.Swing(*this, *F.Player, GClub))
		{
			return false;
		}
		TestTrue(TEXT("0x1034394d: a player attacker (no +0x94) hits a character whatever its relation"),
			F.Impacts() > 0);
	}

	// The attacker itself is in retail's box and is never hit: the owner-root gate (`0x1012c9c0`)
	// lists it, and the relation filter does not ask about `victimCC == this`.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Services.SwingContacts = { F.Attacker->Handle };
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestEqual(TEXT("0x1012c9c0: the attacker in its own box is listed, not hit"), F.Impacts(), 0);
	}
	return true;
}

// --- D4: the samples, `n = ceil(|B - A| * 0.1666667)`, `Q -> P`, no occlusion ----------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSwingStepSamplesTest,
	"Elysium.Arm.MeleeSwingStep.Samples", GElysiumTestFlags)
bool FElysiumMeleeSwingStepSamplesTest::RunTest(const FString&)
{
	const double CmPerUnit = static_cast<double>(ElysiumMove::U);
	const FVector Origin = FVector::ZeroVector;
	TestEqual(TEXT("0x10488874: a 5-unit segment is under 2 samples -> 1 (the midpoint alone)"),
		ElysiumSwing::SampleCount(Origin, FVector(5.0 * CmPerUnit, 0.0, 0.0)), 1);
	TestEqual(TEXT("0x10488874: a 6-unit segment is 2 samples (the f32 constant is above 1/6)"),
		ElysiumSwing::SampleCount(Origin, FVector(6.0 * CmPerUnit, 0.0, 0.0)), 2);
	TestEqual(TEXT("0x10488874: a 13-unit segment is ceil(2.1667) = 3 samples"),
		ElysiumSwing::SampleCount(Origin, FVector(13.0 * CmPerUnit, 0.0, 0.0)), 3);
	TestEqual(TEXT("a zero-length segment is 1 sample"), ElysiumSwing::SampleCount(Origin, Origin), 1);

	TestEqual(TEXT("0x10343fc4: n == 1 -> f = 0"), ElysiumSwing::SampleFraction(0, 1), 0.0f);
	TestEqual(TEXT("0x10343fc4: sample 0 of 3 -> f = 1 (the B end)"), ElysiumSwing::SampleFraction(0, 3), 1.0f);
	TestEqual(TEXT("0x10343fc4: sample 1 of 3 -> f = 0.5"), ElysiumSwing::SampleFraction(1, 3), 0.5f);
	TestEqual(TEXT("0x10343fc4: sample 2 of 3 -> f = 0 (the A end)"), ElysiumSwing::SampleFraction(2, 3), 0.0f);

	FElysiumSwingRecord Record;
	Record.Start = 0.30f;
	Record.End = 0.70f;
	TestTrue(TEXT("0x10343020: the window is open at start == cycle"),
		ElysiumSwing::StepWindowOpen(Record, 0.10f, 0.30f));
	TestTrue(TEXT("0x10343020: the window is open at end == prevCycle"),
		ElysiumSwing::StepWindowOpen(Record, 0.70f, 0.90f));
	TestFalse(TEXT("0x10343020: start > cycle closes"), ElysiumSwing::StepWindowOpen(Record, 0.10f, 0.29f));
	TestFalse(TEXT("0x10343020: end < prevCycle closes"), ElysiumSwing::StepWindowOpen(Record, 0.71f, 0.90f));

	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// Through the walk: a 13-unit record whose far end stands inside the victim's box. The bone is
	// placed at the world origin and does not move, so each sample's `Q` and `P` coincide.
	{
		const float LengthCm = 13.0f * ElysiumMove::U;
		const TArray<FElysiumSwingRecord> Records = { FStepFixture::Window(0.30f, 0.70f, LengthCm) };
		FStepFixture F;
		if (!F.Stand(*this, &Records))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		// The standing hull (16 units either side of its feet) stands on the segment's B end.
		F.Services.PlaceSwingBody(F.Victim->Handle, FVector(LengthCm, 0.0f, -10.0f));
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		if (TestTrue(TEXT("0x101d2530: the walk clips sample rays to the candidate"),
			F.Services.SwingRays.Num() > 0))
		{
			const FElysiumRecordingServices::FSwingRay& First = F.Services.SwingRays[0];
			TestTrue(TEXT("0x101d2530: the ray is clipped to that one entity"),
				First.Entity == F.Victim->Handle);
			TestTrue(TEXT("0x10343020 3.9: sample 0 is P = A + (B - A) * 1, the B end"),
				First.ToCm.Equals(FVector(LengthCm, 0.0f, 0.0f), 1e-3));
			TestTrue(TEXT("0x10343020 3.9: the ray runs from Q (the sample's last position) to P"),
				First.FromCm.Equals(First.ToCm, 1e-3));
		}
		TestEqual(TEXT("0x10343020 3.9: the first sample that hits is the hit -- one ray, one impact"),
			F.Impacts(), 1);
		TestEqual(TEXT("...and no later sample is traced for that entity"), F.Services.SwingRays.Num(), 1);
		TestEqual(TEXT("0x10343020: no world trace stands between limb and entity (mask 0x200400b is the clip's)"),
			CountCalls(F.Services, TEXT("TraceRetail"), TEXT("mask=0x200400b")), 0);
		TestEqual(TEXT("0x10343f96: ...and the wall ray (mask 0x400b) is not this attacker's"),
			CountCalls(F.Services, TEXT("TraceRetail"), TEXT("mask=0x400b")), 0);
	}

	// The midpoint case: a 5-unit record is one sample at `(A + B) / 2`.
	{
		const float LengthCm = 5.0f * ElysiumMove::U;
		const TArray<FElysiumSwingRecord> Records = { FStepFixture::Window(0.30f, 0.70f, LengthCm) };
		FStepFixture F;
		if (!F.Stand(*this, &Records))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		// A hull standing on the midpoint (6.35 cm). Corrected by the V11 integrator from the listing
		// (a test-setup error: the case placed a hull past the midpoint and expected a candidate):
		// the collapse `A = B = (A + B) / 2` is written BEFORE the candidate box is built
		// (`0x10343020`: `if (n < 2) { n = 1; A = B = midpoint; }`, then the mins / maxs over `A`, `B`
		// and the stored last points), so with a still bone the box IS the midpoint.
		F.Services.PlaceSwingBody(F.Victim->Handle, FVector(LengthCm * 0.5f, 0.0f, -10.0f));
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		if (TestTrue(TEXT("the candidate on the midpoint is ray-tested"), F.Services.SwingRays.Num() > 0))
		{
			TestTrue(TEXT("0x10343020: n < 2 -> A = B = the midpoint, the one sample"),
				F.Services.SwingRays[0].ToCm.Equals(FVector(LengthCm * 0.5f, 0.0f, 0.0f), 1e-3));
		}
		TestEqual(TEXT("0x10343020: one sample, one ray"), F.Services.SwingRays.Num(), 1);
		TestEqual(TEXT("0x10343020 3.9: the midpoint sample starts inside the hull -- the hit"),
			F.Impacts(), 1);
	}

	// The collapsed box reaches nothing past the midpoint: a hull that starts at x = 10 cm, past the
	// midpoint (6.35 cm) and short of the record's own B end (12.7 cm), is not a candidate at all.
	{
		const float LengthCm = 5.0f * ElysiumMove::U;
		const TArray<FElysiumSwingRecord> Records = { FStepFixture::Window(0.30f, 0.70f, LengthCm) };
		FStepFixture F;
		if (!F.Stand(*this, &Records))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		F.Services.PlaceSwingBody(F.Victim->Handle,
			FVector(10.0f + ElysiumMove::HullHalfWidth, 0.0f, -10.0f));
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestEqual(TEXT("0x10343020: the box is built from the collapsed points -- no candidate, no ray"),
			F.Services.SwingRays.Num(), 0);
		TestEqual(TEXT("...and no impact"), F.Impacts(), 0);
	}
	return true;
}

// --- D5-D7: the candidates and their gates ----------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSwingStepCandidatesTest,
	"Elysium.Arm.MeleeSwingStep.Candidates", GElysiumTestFlags)
bool FElysiumMeleeSwingStepCandidatesTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	// A prop is hit by the box overlap alone: no ray, the trace is origin to origin.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Services.SwingContacts = { F.Crate->Handle };
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestEqual(TEXT("0x10343eb0: a non-character (+0x9c == 0) in the box reaches slot 270"),
			F.Impacts(), 1);
		TestEqual(TEXT("0x10343b00: ...with no ray"), F.Services.SwingRays.Num(), 0);
		TestTrue(TEXT("0x10343eb0: the trace ends at the entity's origin"),
			F.Weapon->MeleeImpactSeams.LastCombatSoundCm.Equals(F.Crate->Origin, 1e-3));
	}

	// Two records sharing a window: the overlap hit marks EVERY record, so the prop is hit once.
	{
		const TArray<FElysiumSwingRecord> Records = {
			FStepFixture::Window(0.30f, 0.70f), FStepFixture::Window(0.30f, 0.70f) };
		FStepFixture F;
		if (!F.Stand(*this, &Records))
		{
			return false;
		}
		F.Services.SwingContacts = { F.Crate->Handle };
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestEqual(TEXT("0x10343eb0: an overlap hit is added to every record's list -- one impact"),
			F.Impacts(), 1);
	}

	// The gates, one per case; each starts from a contact that lands.
	enum class EGate { None, VictimNotSolid, AttackerNotSolid, Hidden, NotTargetable, OwnedByAttacker };
	struct FCase
	{
		EGate Gate;
		const TCHAR* What;
	};
	static const FCase Cases[] =
	{
		{ EGate::None,             TEXT("the ungated contact lands") },
		{ EGate::VictimNotSolid,   TEXT("0x10343020 3.2: a candidate with FSOLID_NOT_SOLID (+0x2b4 & 4) is skipped") },
		{ EGate::AttackerNotSolid, TEXT("0x10343020 3.3: a not-solid ATTACKER hits nothing") },
		{ EGate::Hidden,           TEXT("0x100b5190: a candidate whose +0xf4 byte is set is skipped") },
		{ EGate::NotTargetable,    TEXT("0x10343020 3.5: a character without m_bIsBCCTargetable (+0x1480) is skipped") },
		{ EGate::OwnedByAttacker,  TEXT("0x1012c9c0: an entity whose owner root is the attacker is listed, not hit") },
	};
	for (const FCase& Case : Cases)
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		FElysiumEntity* Target = F.Victim;
		switch (Case.Gate)
		{
		case EGate::VictimNotSolid:   F.Victim->RetailSolidFlags |= 0x4u; break;
		case EGate::AttackerNotSolid: F.Attacker->RetailSolidFlags |= 0x4u; break;
		case EGate::Hidden:           F.Victim->bHidden = true; break;
		case EGate::NotTargetable:    Target = F.Untargetable; break;
		case EGate::OwnedByAttacker:  Target = F.Crate; F.Crate->SetOwnerEntity(F.Attacker->Handle); break;
		default: break;
		}
		F.Relate(*F.Attacker, *Target, EElysiumRelationship::Hate);
		F.Services.SwingContacts = { Target->Handle };
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestEqual(Case.What, F.Impacts(), Case.Gate == EGate::None ? 1 : 0);
	}

	// Already in the record's list: the walk meets the victim on every open sub-step and lands once.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		F.Services.SwingContacts = { F.Victim->Handle };
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		F.Services.BodyClipPhase.Cycle = 0.60f;
		F.World->AdvanceMeleeSwings(0.05f);
		TestEqual(TEXT("0x10343020 3.6: an entity already in the record's list is skipped"), F.Impacts(), 1);
	}

	// Slot 329: the base answers false (`0x1014f850`), so a character takes the ray arm.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		TestFalse(TEXT("0x1014f850: slot 329's base body answers false"), F.Victim->Slot329());
		TestFalse(TEXT("0x1014f830: slot 328's base body answers false"), F.Attacker->Slot328());
	}
	return true;
}

// --- D8: the wall contact, `0x10343f96` -------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSwingStepWallTest,
	"Elysium.Arm.MeleeSwingStep.Wall", GElysiumTestFlags)
bool FElysiumMeleeSwingStepWallTest::RunTest(const FString&)
{
	const double CmPerUnit = static_cast<double>(ElysiumMove::U);
	const FVector Forward(1.0, 0.0, 0.0);
	const FVector Wall(-1.0, 0.0, 0.0);
	const FVector Origin = FVector::ZeroVector;
	const FVector Near(10.0 * CmPerUnit, 0.0, 50.0);

	TestTrue(TEXT("0x10344262: a vertical plane qualifies"), ElysiumSwing::WallPlaneQualifies(Wall));
	TestFalse(TEXT("0x10344221: a zero normal does not"), ElysiumSwing::WallPlaneQualifies(FVector::ZeroVector));
	TestFalse(TEXT("0x10451ab8: |normal.z| >= 0.3 does not (a floor)"),
		ElysiumSwing::WallPlaneQualifies(FVector(0.0, 0.0, 1.0)));
	TestTrue(TEXT("0x10451ab8: |normal.z| = 0.29 does"),
		ElysiumSwing::WallPlaneQualifies(FVector(0.957, 0.0, 0.29)));

	TestTrue(TEXT("0x10344374: a faced wall 10 units from the origin blocks the swing (slot 319)"),
		ElysiumSwing::WallBlocksSwing(Wall, Forward, Near, Origin));
	TestFalse(TEXT("0x1049e040: the same wall 21 units away does not (the test is < 20.0)"),
		ElysiumSwing::WallBlocksSwing(Wall, Forward, FVector(21.0 * CmPerUnit, 0.0, 0.0), Origin));
	TestTrue(TEXT("0x1049e040: ...and 19 units away does"),
		ElysiumSwing::WallBlocksSwing(Wall, Forward, FVector(19.0 * CmPerUnit, 0.0, 0.0), Origin));
	TestTrue(TEXT("0x1034435d: the distance is 2-D -- height is not counted"),
		ElysiumSwing::WallBlocksSwing(Wall, Forward, FVector(10.0 * CmPerUnit, 0.0, 900.0), Origin));
	TestFalse(TEXT("0x1049e03c: a wall met at 60 degrees (|dot| = 0.5) does not block"),
		ElysiumSwing::WallBlocksSwing(FVector(-0.5, 0.866, 0.0), Forward, Near, Origin));
	TestTrue(TEXT("0x10344330: the dot is absolute -- a wall facing away still blocks"),
		ElysiumSwing::WallBlocksSwing(FVector(1.0, 0.0, 0.0), Forward, Near, Origin));
	TestFalse(TEXT("0x1049e038: a forward with no horizontal part does not block"),
		ElysiumSwing::WallBlocksSwing(Wall, FVector(0.0, 0.0, 1.0), Near, Origin));
	TestTrue(TEXT("0x103442f8: the forward is flattened and normalised before the dot"),
		ElysiumSwing::WallBlocksSwing(Wall, FVector(0.3, 0.0, 0.9), Near, Origin));

	// The gate: only an attacker whose slot 328 is true runs the arm, and the base answers false.
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Services.TraceRetailQuery = [](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Out)
		{
			Out.Fraction = 0.5f;
			Out.Normal = FVector(-1.0, 0.0, 0.0);
			return true;
		};
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestEqual(TEXT("0x10343f96: slot 328 false (0x1014f830) -- the world is never traced"),
			CountCalls(F.Services, TEXT("TraceRetail"), TEXT("mask=0x400b")), 0);
		TestEqual(TEXT("...and no wall reaction or effect is produced"),
			F.Weapon->MeleeImpactSeams.WallBlockedReactions + F.Weapon->MeleeImpactSeams.ImpactEffects, 0);
	}
	return true;
}

// --- D2: `rolls == 0`, `0x10257b92` ------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeContactNoRollsTest,
	"Elysium.Arm.MeleeContact.NoRolls", GElysiumTestFlags)
bool FElysiumMeleeContactNoRollsTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FStepFixture F;
	if (!F.Stand(*this))
	{
		return false;
	}
	F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
	// Outside the roll's own 60-unit query (`SendIncomingSwingNotice 0x10346ac0`), so no record is
	// staged; the sweep's box still holds the victim.
	F.Victim->Origin = FVector(-2000.0f, 0.0f, 0.0f);
	F.Services.SwingContacts = { F.Victim->Handle };
	if (!F.Swing(*this, *F.Attacker, GClub))
	{
		return false;
	}
	TestNull(TEXT("the swing staged no opposed record on this victim"),
		F.Victim->FindMeleeRoll(F.Attacker->Handle));
	TestEqual(TEXT("0x102579f0: the contact is made"), F.Impacts(), 1);
	TestTrue(TEXT("0x10257b92: rolls == 0 -> unblocked, successes 1, the damage is dispatched"),
		DamageTaken(*F.Victim) > 0);
	return true;
}

// --- D3: the dispatch condition, `0x10257cb3` --------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeContactDispatchTest,
	"Elysium.Arm.MeleeContact.Dispatch", GElysiumTestFlags)
bool FElysiumMeleeContactDispatchTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };
	ElysiumMeleeTest::FRulesFixture Rules;

	// Unblocked, a non-positive margin: the damage is still dispatched, successes floored at 1.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		F.Services.SwingContacts = { F.Victim->Handle };
		if (!F.Swing(*this, *F.Attacker, GTwig))
		{
			return false;
		}
		const FElysiumMeleeRoll* Roll = F.Victim->FindMeleeRoll(F.Attacker->Handle);
		if (TestNotNull(TEXT("the swing staged its opposed record"), Roll))
		{
			TestTrue(TEXT("the margin is not positive (0x10349650 DamageWentThrough is false)"),
				Roll->Margin() <= 0);
		}
		TestTrue(TEXT("0x10257cb3: unblocked and not through still dispatches (successes < 1 -> 1)"),
			DamageTaken(*F.Victim) > 0);
	}

	// Blocked and not through: no damage, no reaction -- and the tail still runs.
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		// A melee weapon in the defender's hand: `WasMeleeBlocked 0x10345ab0`'s capability term.
		if (!TestNotNull(TEXT("the defender is armed"), GiveWeapon(*F.Victim, GClub)))
		{
			return false;
		}
		F.Services.SwingContacts = { F.Victim->Handle };
		if (!F.Swing(*this, *F.Attacker, GTwig))
		{
			return false;
		}
		TestEqual(TEXT("0x10257cb3: blocked && !DamageWentThrough -> no damage is dispatched"),
			DamageTaken(*F.Victim), 0);
		TestFalse(TEXT("...and no knockback is asked for"),
			CountCalls(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_KNOCKBACK")) > 0);
		TestEqual(TEXT("0x10258019: the tail's impact effect still runs"),
			F.Weapon->MeleeImpactSeams.ImpactEffects, 1);
		TestEqual(TEXT("0x1025803f: ...and the victim's slot 21 (0x1029f800 raises the hit buildup)"),
			F.Victim->HitBuildupCount, 1);
	}
	return true;
}

// --- D10: slot 270's side effects --------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeContactSideEffectsTest,
	"Elysium.Arm.MeleeContact.SideEffects", GElysiumTestFlags)
bool FElysiumMeleeContactSideEffectsTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	FStepFixture F;
	if (!F.Stand(*this))
	{
		return false;
	}
	F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
	F.Services.SwingContacts = { F.Victim->Handle };
	FElysiumNpc* AttackerNpc = F.Attacker->AsNpc();
	FElysiumNpc* VictimNpc = F.Victim->AsNpc();
	if (!TestNotNull(TEXT("the attacker is an NPC"), AttackerNpc)
		|| !TestNotNull(TEXT("the victim is an NPC"), VictimNpc))
	{
		return false;
	}
	const int32 ClearsBefore = AttackerNpc->MeleeMoveRecordClears;
	if (!F.Swing(*this, *F.Attacker, GClub))
	{
		return false;
	}
	const FElysiumWeapon::FMeleeImpactSeams& Seams = F.Weapon->MeleeImpactSeams;
	TestEqual(TEXT("0x102579f0 step 2: weapon slot 339 (+0x54c) is called once per impact"),
		Seams.WeaponSlot339Calls, 1);
	TestEqual(TEXT("0x102579f0 step 2: CSoundEnt::InsertSound(0x10, endpos, ..., 0.2, ..., owner)"),
		Seams.CombatSoundInserts, 1);
	TestEqual(TEXT("0x10258032: the impact effect 0x101cfef0(trace, 0x80, 1, weapon)"),
		Seams.ImpactEffects, 1);
	TestTrue(TEXT("...at the trace's end, where the sound was inserted"),
		Seams.LastImpactEffectCm.Equals(Seams.LastCombatSoundCm, 1e-3));
	TestEqual(TEXT("0x1025803f: ent slot 21 with the owner -- 0x1029f800 raises m_iHitBuildupCount"),
		F.Victim->HitBuildupCount, 1);
	TestTrue(TEXT("0x1029f800: ...and sets COND_BEING_ATTACKED"),
		VictimNpc->Cognition.Conditions.Has(EElysiumNpcCond::BeingAttacked));
	TestEqual(TEXT("0x10258048: the owner's slot 24 OnVictimHitByMe(ent) (0x1029f8d0)"),
		AttackerNpc->MeleeMoveRecordClears, ClearsBefore + 1);
	return true;
}

// --- D11: plain hit or knockback, step 7 --------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeContactReactionTest,
	"Elysium.Arm.MeleeContact.Reaction", GElysiumTestFlags)
bool FElysiumMeleeContactReactionTest::RunTest(const FString&)
{
	const FElysiumItemTable Table = MakeTable();
	ElysiumItems::Install(Table);
	ON_SCOPE_EXIT { ElysiumItems::Uninstall(Table); };

	struct FCase
	{
		bool bDisallow;
		int32 Buildup;
		bool bKnockback;
		const TCHAR* What;
	};
	static const FCase Cases[] =
	{
		{ false, 0, true,  TEXT("0x102579f0 step 7: slot 326(record) true -> the knockback (slot 320)") },
		{ true,  0, false, TEXT("0x103482e0: the template's Disallow_Knockbacks -> slot 326 false -> the plain hit (slot 321)") },
		{ false, 3, false, TEXT("0x1029fec0: m_iHitBuildupCount above npc_hit_buildup_amount -> slot 326 false -> the plain hit") },
	};
	for (const FCase& Case : Cases)
	{
		FStepFixture F;
		if (!F.Stand(*this))
		{
			return false;
		}
		F.Relate(*F.Attacker, *F.Victim, EElysiumRelationship::Hate);
		F.Services.SwingContacts = { F.Victim->Handle };
		FElysiumNpc* VictimNpc = F.Victim->AsNpc();
		if (!TestNotNull(TEXT("the victim is an NPC"), VictimNpc))
		{
			return false;
		}
		VictimNpc->bDisallowKnockbacks = Case.bDisallow;
		F.Victim->HitBuildupCount = Case.Buildup;
		if (!F.Swing(*this, *F.Attacker, GClub))
		{
			return false;
		}
		TestTrue(TEXT("the damage is dispatched before the reaction is asked"), DamageTaken(*F.Victim) > 0);
		TestTrue(Case.What,
			(CountCalls(F.Services, TEXT("ResolveNpcActivityClip"), TEXT("ACT_KNOCKBACK")) > 0)
				== Case.bKnockback);
	}
	return true;
}

}   // namespace ElysiumMeleeSwingStepTests

#endif   // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
