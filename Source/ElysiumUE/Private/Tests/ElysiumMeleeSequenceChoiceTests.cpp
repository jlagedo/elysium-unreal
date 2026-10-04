#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Math/RandomStream.h"
#include "Substrate/ElysiumMeleeSequenceChoice.h"

// Spec 0002 V11-3: slot 331 `CBaseCombatCharacter::ChooseMeleeAttackSequence 0x10347180` and its
// picker `ChooseSequenceFromList 0x10348100`, driven on the body's own record
// (`ElysiumMeleeSequenceChoice::FQuery`). Every assertion names the address it reads. Source units.

static constexpr EAutomationTestFlags GMeleeSequenceChoiceFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	namespace MeleeChoice = ElysiumMeleeSequenceChoice;

	// Three entity identities. The body compares `m_pEnt` pointers and never dereferences one here.
	const FElysiumEntity* MeleeChoiceEntity(int32 Which)
	{
		static const uint8 Storage[3] = {};
		return reinterpret_cast<const FElysiumEntity*>(&Storage[Which]);
	}
	const FElysiumEntity* MeleeChoiceEnemy() { return MeleeChoiceEntity(0); }
	const FElysiumEntity* MeleeChoiceThird() { return MeleeChoiceEntity(1); }
	const FElysiumEntity* MeleeChoicePartner() { return MeleeChoiceEntity(2); }

	// An NPC at the origin with a melee weapon and an enemy 40 units ahead whose bounds are a
	// 32 x 32 x 72 box on its origin: `dist2D = 40`, line-frame box (24, -16, 0) .. (56, 16, 72).
	MeleeChoice::FQuery MeleeChoiceQuery()
	{
		MeleeChoice::FQuery Query;
		Query.Enemy = MeleeChoiceEnemy();
		Query.EnemyOriginUnits = FVector(40.0, 0.0, 0.0);
		Query.EnemyBoundsMinUnits = FVector(24.0, -16.0, 0.0);
		Query.EnemyBoundsMaxUnits = FVector(56.0, 16.0, 72.0);
		Query.LineStartUnits = FVector(0.0, 0.0, 36.0);
		Query.LineEndUnits = FVector(40.0, 0.0, 36.0);
		Query.NpcStepUnits = 18.0f;
		Query.bHasWeapon = true;
		Query.WeaponCapabilityWord = 0x18000u;
		Query.WeaponMinRange1Units = 0.0f;     // CWeaponMelee 0x103e9ac0: 0 / 50
		Query.WeaponMaxRange1Units = 50.0f;
		return Query;
	}

	// A candidate in range of the fixture's enemy (20..60 against `dist2D` 40), no movement, no box.
	MeleeChoice::FCandidate MeleeChoiceCandidate(int32 Sequence, int32 Weight = 1)
	{
		MeleeChoice::FCandidate Candidate;
		Candidate.Sequence = Sequence;
		Candidate.Weight = Weight;
		Candidate.LowEdgeUnits = 20.0f;
		Candidate.HighEdgeUnits = 60.0f;
		return Candidate;
	}

	MeleeChoice::FEnvelope MeleeChoiceEnvelope(double MinX, double MaxX)
	{
		MeleeChoice::FEnvelope Envelope;
		Envelope.Min = FVector(MinX, -8.0, 30.0);
		Envelope.Max = FVector(MaxX, 8.0, 50.0);
		return Envelope;
	}

	int32 MeleeChoiceFlagsOf(const MeleeChoice::FQuery& Query, const MeleeChoice::FCandidate& Candidate)
	{
		return MeleeChoice::CandidateFlags(Query, MeleeChoice::EnemyGeometry(Query), Candidate, Query.NpcStepUnits);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSequenceChoiceFlagsTest,
	"Elysium.Arm.MeleeSequenceChoice.Flags", GMeleeSequenceChoiceFlags)
bool FElysiumMeleeSequenceChoiceFlagsTest::RunTest(const FString&)
{
	// --- Bit 8, `0x103475b1..0x103475df`: `seq+0x2cc <= dist2D <= seq+0x2d0`, inclusive both ends.
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		MeleeChoice::FCandidate Candidate = MeleeChoiceCandidate(1);
		TestEqual(TEXT("0x103475df: in range, no movement -> 8 | 4 | 0x10"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1c);
		Candidate.LowEdgeUnits = 40.0f;
		Candidate.HighEdgeUnits = 40.0f;
		TestEqual(TEXT("0x103475c9 / 0x103475dd: both edges equal to dist2D still set bit 8"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1c);
		Candidate.LowEdgeUnits = 41.0f;
		Candidate.HighEdgeUnits = 60.0f;
		TestEqual(TEXT("0x103475c9: dist2D below seq+0x2cc clears bit 8"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x14);
		Candidate.LowEdgeUnits = 20.0f;
		Candidate.HighEdgeUnits = 39.0f;
		TestEqual(TEXT("0x103475dd: dist2D above seq+0x2d0 clears bit 8"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x14);
		Query.Enemy = nullptr;
		TestEqual(TEXT("0x103475b3: no enemy never sets bit 8"),
			MeleeChoiceFlagsOf(Query, MeleeChoiceCandidate(1)), 0x14);
	}

	// --- The movement arm, `0x1034766d..0x10347bac`.
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		MeleeChoice::FCandidate Candidate = MeleeChoiceCandidate(1);
		Candidate.bHasMovement = true;
		Candidate.MovementUnits = FVector(200.0, 10.0, 0.0);
		FVector SeenStart = FVector::ZeroVector;
		FVector SeenEnd = FVector::ZeroVector;
		MeleeChoice::FTrace Answer;
		Query.HullSweep = [&SeenStart, &SeenEnd, &Answer](const FVector& Start, const FVector& End)
		{
			SeenStart = Start;
			SeenEnd = End;
			return Answer;
		};

		TestEqual(TEXT("0x1034763f: a clear sweep keeps bit 4, and 0x10 is not set"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x0c);
		TestEqual(TEXT("0x10347681: the sweep starts at the origin raised by slot 522"),
			SeenStart, FVector(0.0, 0.0, 18.0));
		TestEqual(TEXT("0x103476af..0x10347758: both delta terms lie along dir (the copy at +0x90), "
			"end raised by slot 522"), SeenEnd, FVector(210.0, 0.0, 18.0));

		// Blocked by nothing-in-particular, the remainder beyond `debug_melee_npc_range` (128).
		Answer.Fraction = 0.25f;   // remainder 0.75 * 210 = 157.5
		TestEqual(TEXT("0x10347ba2: blocked beyond 128 units clears bit 4"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x08);
		Answer.Fraction = 0.5f;    // remainder 105
		TestEqual(TEXT("0x10347b82: blocked with the remainder within 128 keeps bit 4"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x0c);
		Answer.bStartSolid = true;
		TestEqual(TEXT("startsolid is never kept (the NPC arm tests +0x37 ahead of the range)"), MeleeChoiceFlagsOf(Query, Candidate), 0x08);
		Answer.bStartSolid = false;
		Answer.Hit = MeleeChoiceThird();
		TestEqual(TEXT("a third entity within 128 is not `hit == 0 || hit == enemy`: bit 4 cleared"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x08);

		// Blocked by the enemy, beyond the range.
		Answer.Fraction = 0.25f;
		Answer.Hit = MeleeChoiceEnemy();
		TestEqual(TEXT("0x10347b94: the enemy without +0xfe0 clears bit 4"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x08);
		Query.bAllowsInterpenetratingAttacks = true;
		TestEqual(TEXT("0x10347b9a: the enemy with +0xfe0 sets bit 1 and keeps bit 4"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x0d);
		Query.EnemyGrapplePartner = MeleeChoicePartner();
		Answer.Hit = MeleeChoicePartner();
		TestEqual(TEXT("the entity of the enemy's +0x1538 handle counts as the enemy"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x0d);

		// `seq+0x2d4 >= 0` zeroes the word after the sweep (NPC arm).
		Answer = MeleeChoice::FTrace();
		Candidate.bStatesButtonMask = true;
		TestEqual(TEXT("0x1034795d..0x10347967: a stated +0x2d4 zeroes bits 8 and 4"),
			MeleeChoiceFlagsOf(Query, Candidate), 0);
		Candidate.Envelopes.Add(MeleeChoiceEnvelope(10.0, 30.0));
		TestEqual(TEXT("0x10347c52: the envelope bit is still written into the zeroed word"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x02);
		Candidate.bHasMovement = false;
		TestEqual(TEXT("0x10347baa: with no movement the +0x2d4 test is not reached"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1e);

		// The player arm: `fraction >= 0.9` or the remainder `<= 8.0`.
		Query = MeleeChoiceQuery();
		Query.bNpc = false;
		Query.bPlayer = true;
		Query.HullSweep = [&Answer](const FVector&, const FVector&) { return Answer; };
		Candidate = MeleeChoiceCandidate(1);
		Candidate.bHasMovement = true;
		Candidate.MovementUnits = FVector(100.0, 0.0, 0.0);
		Answer.Fraction = 0.9f;
		TestEqual(TEXT("0x10450a9c: the player keeps bit 4 at fraction 0.9"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x0c);
		Answer.Fraction = 0.5f;
		TestEqual(TEXT("0x1045597c: the player's remainder 50 > 8 clears bit 4"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x08);
	}

	// --- Bit 2, `0x10347bb0..0x10347c52`: strict on all six sides against (24,-16,0)..(56,16,72).
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		MeleeChoice::FCandidate Candidate = MeleeChoiceCandidate(1);
		Candidate.Envelopes.Add(MeleeChoiceEnvelope(0.0, 25.0));
		TestEqual(TEXT("0x10347c52: an envelope reaching the box sets bit 2"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1e);
		Candidate.Envelopes[0] = MeleeChoiceEnvelope(0.0, 24.0);
		TestEqual(TEXT("0x10347bef: rec.max.x == box.min.x does not overlap (strict)"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1c);
		Candidate.Envelopes[0] = MeleeChoiceEnvelope(56.0, 80.0);
		TestEqual(TEXT("0x10347bdf: rec.min.x == box.max.x does not overlap (strict)"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1c);
		Candidate.Envelopes.Add(MeleeChoiceEnvelope(30.0, 40.0));
		TestEqual(TEXT("0x10347c44: any record of the list is enough"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x1e);
		Query.Enemy = nullptr;
		TestEqual(TEXT("0x10347bb2: no enemy never sets bit 2"),
			MeleeChoiceFlagsOf(Query, Candidate), 0x14);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSequenceChoiceLineGateTest,
	"Elysium.Arm.MeleeSequenceChoice.LineGate", GMeleeSequenceChoiceFlags)
bool FElysiumMeleeSequenceChoiceLineGateTest::RunTest(const FString&)
{
	MeleeChoice::FQuery Query = MeleeChoiceQuery();
	MeleeChoice::FCandidate Reaching = MeleeChoiceCandidate(7);
	Reaching.Envelopes.Add(MeleeChoiceEnvelope(10.0, 30.0));
	Query.Candidates.Add(Reaching);
	MeleeChoice::FTrace Answer;
	int32 Traces = 0;
	FVector SeenStart = FVector::ZeroVector;
	FVector SeenEnd = FVector::ZeroVector;
	Query.LineTrace = [&Answer, &Traces, &SeenStart, &SeenEnd](const FVector& Start, const FVector& End)
	{
		++Traces;
		SeenStart = Start;
		SeenEnd = End;
		return Answer;
	};
	bool bBit10 = false;
	Query.HitDerivedTypeBit10 = [&bBit10](const FElysiumEntity*) { return bBit10; };

	// A clear line: no gate, the pick stands.
	MeleeChoice::FResult Result = MeleeChoice::Choose(Query);
	TestEqual(TEXT("0x1034727b: one line trace, my slot 192 point to the enemy's"), Traces, 1);
	TestEqual(TEXT("0x10347272: the start is the NPC's slot 192 point"), SeenStart, Query.LineStartUnits);
	TestEqual(TEXT("0x1034725e: the end is the enemy's slot 192 point"), SeenEnd, Query.LineEndUnits);
	TestFalse(TEXT("0x103472a8: a clear line raises no 0x3a"), Result.bEnemyBlocked);
	TestTrue(TEXT("...and the body answers true"), Result.bChosen);
	TestEqual(TEXT("...with the candidate"), Result.Sequence, 7);

	// A third entity on the line.
	Answer.Fraction = 0.5f;
	Answer.Hit = MeleeChoiceThird();
	Result = MeleeChoice::Choose(Query);
	TestTrue(TEXT("0x103472f6: a third entity on the line raises SetCondition(0x3a)"), Result.bEnemyBlocked);
	TestFalse(TEXT("0x103472fb: ...and answers false"), Result.bChosen);
	TestEqual(TEXT("...with *out = -1"), Result.Sequence, static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...before any candidate is scored"), Result.Flags.Num(), 0);
	TestEqual(TEXT("...and before the weapon's +0x8c0 is folded"), Result.WeaponMaxRange1Units, 50.0f);

	// The enemy itself, its grapple partner, a bit-10 entity, the world (no entity): no gate.
	Answer.Hit = MeleeChoiceEnemy();
	TestFalse(TEXT("0x103472b7: the enemy itself is no gate"), MeleeChoice::Choose(Query).bEnemyBlocked);
	Answer.Hit = MeleeChoicePartner();
	Query.EnemyGrapplePartner = MeleeChoicePartner();
	TestFalse(TEXT("0x103472da: the entity of the enemy's +0x1538 handle is no gate"),
		MeleeChoice::Choose(Query).bEnemyBlocked);
	Query.EnemyGrapplePartner = nullptr;
	TestTrue(TEXT("0x103472c0: with +0x153c == -1 the same entity gates"), MeleeChoice::Choose(Query).bEnemyBlocked);
	bBit10 = true;
	TestFalse(TEXT("0x103472e5: hit+0x4c bit 10 set is no gate"), MeleeChoice::Choose(Query).bEnemyBlocked);
	bBit10 = false;
	Answer.Hit = nullptr;
	TestFalse(TEXT("0x103472b3: a blocked trace with no entity is no gate"), MeleeChoice::Choose(Query).bEnemyBlocked);

	// Not an NPC, or no enemy: the gate is not run.
	Answer.Hit = MeleeChoiceThird();
	Traces = 0;
	Query.bNpc = false;
	TestFalse(TEXT("0x1034722f: this+0x98 == 0 skips the gate"), MeleeChoice::Choose(Query).bEnemyBlocked);
	Query.bNpc = true;
	Query.Enemy = nullptr;
	TestFalse(TEXT("0x10347237: no enemy skips the gate"), MeleeChoice::Choose(Query).bEnemyBlocked);
	TestEqual(TEXT("...with no trace at all"), Traces, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSequenceChoicePickerTest,
	"Elysium.Arm.MeleeSequenceChoice.Picker", GMeleeSequenceChoiceFlags)
bool FElysiumMeleeSequenceChoicePickerTest::RunTest(const FString&)
{
	TArray<MeleeChoice::FCandidate> Candidates;
	Candidates.Add(MeleeChoiceCandidate(10, 0));
	Candidates.Add(MeleeChoiceCandidate(11, 0));
	Candidates.Add(MeleeChoiceCandidate(12, 0));
	TArray<int32> Flags = { 0x0c, 0x04, 0x0e };
	FRandomStream Rng(331);

	TestEqual(TEXT("0x103481d8: no candidate carries the mask -> -1"),
		MeleeChoice::ChooseSequenceFromList(Candidates, Flags, 0x01, &Rng), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("0x103481ef: exactly one -> it, no draw"),
		MeleeChoice::ChooseSequenceFromList(Candidates, Flags, 0x0a, &Rng), 12);
	TestEqual(TEXT("0x10348199: the test is (flags & mask) == mask, not any bit"),
		MeleeChoice::ChooseSequenceFromList(Candidates, Flags, 0x06, &Rng), 12);

	// Total weight < 1: `RandomInt(0, n - 1)` over the admitted list (mask 0x08 admits 10 and 12).
	{
		FRandomStream Expected(77);
		FRandomStream Actual(77);
		for (int32 Draw = 0; Draw < 16; ++Draw)
		{
			const int32 Want = Expected.RandRange(0, 1) == 0 ? 10 : 12;
			TestEqual(TEXT("0x10348207: weights under 1 draw RandomInt(0, n - 1)"),
				MeleeChoice::ChooseSequenceFromList(Candidates, Flags, 0x08, &Actual), Want);
		}
	}

	// The weighted walk: `r = RandomInt(0, total - 1)`, `r -= w[i]` over the first n - 1.
	{
		Candidates[0].Weight = 3;
		Candidates[1].Weight = 5;
		Candidates[2].Weight = 2;
		Flags = { 0x04, 0x04, 0x04 };
		FRandomStream Expected(1234);
		FRandomStream Actual(1234);
		bool bSawEach[3] = { false, false, false };
		for (int32 Draw = 0; Draw < 64; ++Draw)
		{
			const int32 Roll = Expected.RandRange(0, 9);
			const int32 Want = Roll < 3 ? 10 : (Roll < 8 ? 11 : 12);
			const int32 Got = MeleeChoice::ChooseSequenceFromList(Candidates, Flags, 0x04, &Actual);
			TestEqual(TEXT("0x1034822b..0x10348247: RandomInt(0, total - 1) walked down the weights"),
				Got, Want);
			bSawEach[Got - 10] = true;
		}
		TestTrue(TEXT("the seeded stream reached all three candidates"),
			bSawEach[0] && bSawEach[1] && bSawEach[2]);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSequenceChoicePassesTest,
	"Elysium.Arm.MeleeSequenceChoice.Passes", GMeleeSequenceChoiceFlags)
bool FElysiumMeleeSequenceChoicePassesTest::RunTest(const FString&)
{
	// One candidate whose flag word is shaped by its range, its envelope and its sweep.
	struct FCase
	{
		bool bInRange;
		bool bEnvelope;
		bool bBlocked;       // the sweep blocked beyond range by a third entity: bit 4 cleared
		bool bCloses;        // ...by the enemy with +0xfe0: bit 1
		int32 WantFlags;
		int32 WantMask;
		bool bWantTrue;
	};
	const FCase Cases[] = {
		// flags 0xf: first pass, mask 7 | 8.
		{ true, true, false, true, 0x0f, 0x0f, true },
		// flags 0xd: mask 5 | 8.
		{ true, false, false, true, 0x0d, 0x0d, true },
		// flags 0xe: mask 6 | 8.
		{ true, true, false, false, 0x0e, 0x0e, true },
		// flags 0xa: bit 4 gone -> first pass mask 2 | 8: a pick, FALSE.
		{ true, true, true, false, 0x0a, 0x0a, false },
		// flags 0xc: first pass mask 4 | 8: a pick, FALSE (no bit of 3).
		{ true, false, false, false, 0x0c, 0x0c, false },
		// flags 0x8: first pass mask 0 | 8: a pick, FALSE.
		{ true, false, true, false, 0x08, 0x08, false },
		// Out of range, flags 6: only the second pass admits it, mask 6: a pick, FALSE (no bit 8).
		{ false, true, false, false, 0x06, 0x06, false },
		// Out of range, flags 7: second pass mask 7: FALSE.
		{ false, true, false, true, 0x07, 0x07, false },
		// Out of range, flags 0: second pass mask 0: FALSE.
		{ false, false, true, false, 0x00, 0x00, false },
	};
	for (const FCase& Case : Cases)
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		Query.bAllowsInterpenetratingAttacks = Case.bCloses;
		MeleeChoice::FCandidate Candidate = MeleeChoiceCandidate(5);
		if (!Case.bInRange)
		{
			Candidate.HighEdgeUnits = 30.0f;
		}
		if (Case.bEnvelope)
		{
			Candidate.Envelopes.Add(MeleeChoiceEnvelope(10.0, 30.0));
		}
		Candidate.bHasMovement = true;
		Candidate.MovementUnits = FVector(400.0, 0.0, 0.0);
		const bool bBlocked = Case.bBlocked;
		const bool bCloses = Case.bCloses;
		Query.HullSweep = [bBlocked, bCloses](const FVector&, const FVector&)
		{
			MeleeChoice::FTrace Trace;
			if (bBlocked || bCloses)
			{
				Trace.Fraction = 0.1f;   // remainder 360 > 128
				Trace.Hit = bCloses ? MeleeChoiceEnemy() : MeleeChoiceThird();
			}
			return Trace;
		};
		Query.Candidates.Add(Candidate);
		const MeleeChoice::FResult Result = MeleeChoice::Choose(Query);
		const FString What = FString::Printf(TEXT("flags 0x%x"), Case.WantFlags);
		TestEqual(*FString::Printf(TEXT("0x10347c6c: %s"), *What), Result.Flags[0], Case.WantFlags);
		TestEqual(*FString::Printf(TEXT("0x10347cf3..: %s is picked by mask 0x%x"), *What, Case.WantMask),
			Result.PickedMask, Case.WantMask);
		TestEqual(*FString::Printf(TEXT("%s writes the pick to *out"), *What), Result.Sequence, 5);
		TestEqual(*FString::Printf(TEXT("%s: true only on the first pass's 7, 5, 6"), *What),
			Result.bChosen, Case.bWantTrue);
	}

	// Mask order inside a pass: a 0xf candidate beats a 0xd and a 0xe one whatever the list order.
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		Query.bAllowsInterpenetratingAttacks = true;
		Query.HullSweep = [](const FVector&, const FVector& End)
		{
			// Candidates whose movement is 400 long run into the enemy; shorter ones are clear.
			MeleeChoice::FTrace Trace;
			if (End.X > 300.0)
			{
				Trace.Fraction = 0.1f;
				Trace.Hit = MeleeChoiceEnemy();
			}
			return Trace;
		};
		MeleeChoice::FCandidate Six = MeleeChoiceCandidate(1);             // 0xe: envelope, clear
		Six.Envelopes.Add(MeleeChoiceEnvelope(10.0, 30.0));
		Six.bHasMovement = true;
		Six.MovementUnits = FVector(10.0, 0.0, 0.0);
		MeleeChoice::FCandidate Five = MeleeChoiceCandidate(2);            // 0xd: closes, no envelope
		Five.bHasMovement = true;
		Five.MovementUnits = FVector(400.0, 0.0, 0.0);
		MeleeChoice::FCandidate Seven = Five;                              // 0xf: closes and envelope
		Seven.Sequence = 3;
		Seven.Envelopes.Add(MeleeChoiceEnvelope(10.0, 30.0));
		Query.Candidates = { Six, Five, Seven };
		MeleeChoice::FResult Result = MeleeChoice::Choose(Query);
		TestEqual(TEXT("mask 7 | 8 is tried first"), Result.Sequence, 3);
		Query.Candidates = { Six, Five };
		Result = MeleeChoice::Choose(Query);
		TestEqual(TEXT("then 5 | 8, ahead of 6 | 8"), Result.Sequence, 2);
		TestTrue(TEXT("...true"), Result.bChosen);
	}

	// No enemy: mask 0x10, then 0; a pick, and FALSE.
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		Query.Enemy = nullptr;
		MeleeChoice::FCandidate Moving = MeleeChoiceCandidate(1);
		Moving.bHasMovement = true;
		Moving.MovementUnits = FVector(10.0, 0.0, 0.0);
		Query.Candidates = { Moving, MeleeChoiceCandidate(2) };
		MeleeChoice::FResult Result = MeleeChoice::Choose(Query);
		TestEqual(TEXT("0x10347ca7: no enemy picks under mask 0x10 first (the sequence with no movement)"),
			Result.Sequence, 2);
		TestFalse(TEXT("0x10347cba: ...and returns false with the pick written"), Result.bChosen);
		Query.Candidates = { Moving };
		Result = MeleeChoice::Choose(Query);
		TestEqual(TEXT("0x10347cdb: then mask 0"), Result.Sequence, 1);
		TestEqual(TEXT("...mask 0"), Result.PickedMask, 0);
		TestFalse(TEXT("0x10347ce5: ...false"), Result.bChosen);
	}

	// The early exits.
	{
		MeleeChoice::FQuery Query = MeleeChoiceQuery();
		Query.Candidates.Add(MeleeChoiceCandidate(1));
		Query.Candidates[0].Envelopes.Add(MeleeChoiceEnvelope(10.0, 30.0));
		TestTrue(TEXT("the baseline answers true"), MeleeChoice::Choose(Query).bChosen);
		Query.bHasModel = false;
		TestEqual(TEXT("0x10347216: no model -> false, *out = -1"), MeleeChoice::Choose(Query).Sequence,
			static_cast<int32>(INDEX_NONE));
		Query.bHasModel = true;
		Query.bHasWeapon = false;
		TestFalse(TEXT("0x1034746f: no weapon -> false"), MeleeChoice::Choose(Query).bChosen);
		Query.bHasWeapon = true;
		Query.WeaponCapabilityWord = 0x2000u;
		MeleeChoice::FResult Result = MeleeChoice::Choose(Query);
		TestFalse(TEXT("0x10347484: weapon slot 360 & 0x18000 == 0 -> false"), Result.bChosen);
		TestEqual(TEXT("...and *out = -1"), Result.Sequence, static_cast<int32>(INDEX_NONE));
		Query.WeaponCapabilityWord = 0x18000u;
		Query.Candidates.Reset();
		Result = MeleeChoice::Choose(Query);
		TestFalse(TEXT("no sequence for the activity -> false"), Result.bChosen);
		TestEqual(TEXT("...and *out = -1 after both passes"), Result.Sequence, static_cast<int32>(INDEX_NONE));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMeleeSequenceChoiceUnstatedTest,
	"Elysium.Arm.MeleeSequenceChoice.Unstated", GMeleeSequenceChoiceFlags)
bool FElysiumMeleeSequenceChoiceUnstatedTest::RunTest(const FString&)
{
	// S6 item 4: `+0x2cc` unstated is `FLT_MIN` (raw 0x00800000), `+0x2d0` unstated is `FLT_MAX`;
	// the body compares and folds them raw.
	TestEqual(TEXT("the low marker is FLT_MIN"), MeleeChoice::LowEdgeUnstated, 1.17549435e-38f);
	TestEqual(TEXT("the far marker is FLT_MAX"), MeleeChoice::HighEdgeUnstated, 3.40282347e+38f);

	MeleeChoice::FQuery Query = MeleeChoiceQuery();
	MeleeChoice::FCandidate Candidate = MeleeChoiceCandidate(1);
	Candidate.LowEdgeUnits = MeleeChoice::LowEdgeUnstated;
	Candidate.HighEdgeUnits = MeleeChoice::HighEdgeUnstated;

	// (1) Bit 8 on an unstated far edge: true at any distance.
	Query.EnemyBoundsMinUnits = FVector(99984.0, -16.0, 0.0);
	Query.EnemyBoundsMaxUnits = FVector(100016.0, 16.0, 72.0);
	TestEqual(TEXT("0x103475cb..0x103475dd: an unstated +0x2d0 sets bit 8 at 100000 units"),
		MeleeChoiceFlagsOf(Query, Candidate) & MeleeChoice::FlagInRange, MeleeChoice::FlagInRange);

	// (2) Bit 8 on an unstated low edge: false only at dist2D == 0 exactly.
	Query.EnemyBoundsMinUnits = FVector(-16.0, -16.0, 0.0);
	Query.EnemyBoundsMaxUnits = FVector(16.0, 16.0, 72.0);
	TestEqual(TEXT("0x103475b5..0x103475c9: FLT_MIN <= 0 is false, bit 8 clear at dist2D == 0"),
		MeleeChoiceFlagsOf(Query, Candidate) & MeleeChoice::FlagInRange, 0);
	Query.EnemyBoundsMinUnits = FVector(-15.0, -16.0, 0.0);
	Query.EnemyBoundsMaxUnits = FVector(16.0, 16.0, 72.0);
	TestEqual(TEXT("...and set at dist2D == 0.5"),
		MeleeChoiceFlagsOf(Query, Candidate) & MeleeChoice::FlagInRange, MeleeChoice::FlagInRange);

	// (3) / (4) The weapon's running pair, folded raw.
	Query = MeleeChoiceQuery();
	Query.WeaponMinRange1Units = 5.0f;
	Query.WeaponMaxRange1Units = 50.0f;
	Query.Candidates.Add(Candidate);
	MeleeChoice::FResult Result = MeleeChoice::Choose(Query);
	TestEqual(TEXT("0x10347610: an unstated low edge drags +0x8b8 to FLT_MIN"),
		Result.WeaponMinRange1Units, MeleeChoice::LowEdgeUnstated);
	TestEqual(TEXT("0x10347639: an unstated far edge drags +0x8c0 to FLT_MAX"),
		Result.WeaponMaxRange1Units, MeleeChoice::HighEdgeUnstated);

	// Stated edges fold as min / max, for every candidate, in range or not.
	Query.Candidates.Reset();
	MeleeChoice::FCandidate Near = MeleeChoiceCandidate(1);
	Near.LowEdgeUnits = 2.0f;
	Near.HighEdgeUnits = 30.0f;
	MeleeChoice::FCandidate Far = MeleeChoiceCandidate(2);
	Far.LowEdgeUnits = 10.0f;
	Far.HighEdgeUnits = 90.0f;
	Query.Candidates = { Near, Far };
	Result = MeleeChoice::Choose(Query);
	TestEqual(TEXT("0x103475e4..0x10347610: +0x8b8 = min(+0x8b8, seq+0x2cc)"), Result.WeaponMinRange1Units, 2.0f);
	TestEqual(TEXT("0x10347616..0x10347639: +0x8c0 = max(+0x8c0, seq+0x2d0)"), Result.WeaponMaxRange1Units, 90.0f);
	Query.WeaponMinRange1Units = 0.0f;
	Query.WeaponMaxRange1Units = 108.0f;
	Result = MeleeChoice::Choose(Query);
	TestEqual(TEXT("a lower +0x8b8 already standing is kept"), Result.WeaponMinRange1Units, 0.0f);
	TestEqual(TEXT("a higher +0x8c0 already standing is kept"), Result.WeaponMaxRange1Units, 108.0f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
