#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumNpcKernelBindings.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveTypes.h"
#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumNpcTestFixture.h"

// The generated NPC field table, asserted rather than described.
//
// The generator (`research/tooling/gen_kernel_bindings.py`) writes `AddNpcFields` and the two
// name tables off the datamap replay; this suite holds the emission to its own counts. It
// stands a fresh descriptor, runs the generated registration into it, and requires the field
// table to carry exactly the rows the generator counted — so a generated row dropped by a bad
// regeneration, or a hand row reintroduced beside the generated one under the same external,
// fails here. The lowercase-unique walk is the FName trap: the registry's keys fold case, so
// two externals differing only in case would silently collapse into one row.

static constexpr EAutomationTestFlags GElysiumNpcBindingsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBindingsCountsTest,
	"Elysium.Substrate.NpcKernelBindings.Counts", GElysiumNpcBindingsFlags)
bool FElysiumNpcKernelBindingsCountsTest::RunTest(const FString&)
{
	FElysiumClassDesc D;
	ElysiumNpcKernelBindings::AddNpcFields(D);

	const ElysiumNpcKernelBindings::FCounts Counts = ElysiumNpcKernelBindings::Counts();
	TestEqual(TEXT("the field table carries exactly the generated bound rows"), D.Fields.Num(),
		Counts.Bound);

	// The `SAVE`-only walk, added to the same descriptor as `BuildNpcClass` adds it. Two things
	// are asserted at once: the emission carries its own count, and no save row collides with a
	// keyed one — the table would silently swallow the second under one FName if it did, and the
	// `m_` prefix is the only thing keeping the two namespaces apart.
	{
		FElysiumClassDesc Both;
		ElysiumNpcKernelBindings::AddNpcFields(Both);
		ElysiumNpcKernelBindings::AddNpcSaveFields(Both);
		TestEqual(TEXT("the save walk adds exactly the generated saved rows, none colliding"),
			Both.Fields.Num(), Counts.Bound + Counts.Saved);
		for (const TPair<FName, FElysiumFieldAccessor>& Pair : Both.Fields)
		{
			if (!Pair.Key.ToString().StartsWith(TEXT("m_")))
			{
				continue;
			}
			// A save row is persistence and nothing else: `ReadKeyField`'s gate is `KEY|OUTPUT`
			// and `AcceptInput`'s is `INPUT`, and a row with no external name carries neither.
			TestFalse(FString::Printf(TEXT("%s is not keyable"), *Pair.Key.ToString()),
				Pair.Value.bKeyable);
			TestTrue(FString::Printf(TEXT("%s is saved"), *Pair.Key.ToString()),
				Pair.Value.bSave);
		}
	}
	TestEqual(TEXT("the output table carries exactly the generated output rows"),
		ElysiumNpcKernelBindings::Outputs().Num(), Counts.Outputs);
	TestEqual(TEXT("the inputfunc table carries exactly the generated inputfunc rows"),
		ElysiumNpcKernelBindings::InputFuncs().Num(), Counts.InputFuncs);

	TSet<FString> Seen;
	for (const TPair<FName, FElysiumFieldAccessor>& Pair : D.Fields)
	{
		const FString Name = Pair.Key.ToString();
		TestTrue(FString::Printf(TEXT("%s is lowercase"), *Name), Name.ToLower() == Name);
		TestTrue(FString::Printf(TEXT("%s is unique"), *Name), !Seen.Contains(Name));
		Seen.Add(Name);
	}

	// The maker and the interesting place: same walk, per class. The maker's two save-only rows
	// and the place's testflags are hand rows outside the generated tables and are not counted.
	{
		FElysiumClassDesc Maker;
		ElysiumNpcKernelBindings::AddNpcMakerFields(Maker);
		const ElysiumNpcKernelBindings::FCounts MakerCounts =
			ElysiumNpcKernelBindings::Counts(ElysiumNpcKernelBindings::EClass::NpcMaker);
		TestEqual(TEXT("the maker's field table carries exactly the generated bound rows"),
			Maker.Fields.Num(), MakerCounts.Bound);
		TestEqual(TEXT("the maker's output table carries exactly the generated output rows"),
			ElysiumNpcKernelBindings::Outputs(
				ElysiumNpcKernelBindings::EClass::NpcMaker).Num(), MakerCounts.Outputs);
		TestEqual(TEXT("the maker's inputfunc table carries exactly the generated inputfunc rows"),
			ElysiumNpcKernelBindings::InputFuncs(
				ElysiumNpcKernelBindings::EClass::NpcMaker).Num(), MakerCounts.InputFuncs);

		// No lowercase check: CNPCMaker's datamap spells its externals in mixed case
		// (`Flag_Fade`, `MaxNPCCount`), and the table carries retail's spelling.
		TSet<FString> MakerSeen;
		for (const TPair<FName, FElysiumFieldAccessor>& Pair : Maker.Fields)
		{
			const FString Name = Pair.Key.ToString();
			TestTrue(FString::Printf(TEXT("the maker: %s is unique"), *Name),
				!MakerSeen.Contains(Name));
			MakerSeen.Add(Name);
		}
	}
	{
		FElysiumClassDesc Place;
		ElysiumNpcKernelBindings::AddInterestingPlaceFields(Place);
		const ElysiumNpcKernelBindings::FCounts PlaceCounts =
			ElysiumNpcKernelBindings::Counts(ElysiumNpcKernelBindings::EClass::InterestingPlace);
		TestEqual(TEXT("the place's field table carries exactly the generated bound rows"),
			Place.Fields.Num(), PlaceCounts.Bound);
		TestEqual(TEXT("the place's output table carries exactly the generated output rows"),
			ElysiumNpcKernelBindings::Outputs(
				ElysiumNpcKernelBindings::EClass::InterestingPlace).Num(), PlaceCounts.Outputs);
		TestEqual(TEXT("the place's inputfunc table carries exactly the generated inputfunc rows"),
			ElysiumNpcKernelBindings::InputFuncs(
				ElysiumNpcKernelBindings::EClass::InterestingPlace).Num(),
			PlaceCounts.InputFuncs);

		TSet<FString> PlaceSeen;
		for (const TPair<FName, FElysiumFieldAccessor>& Pair : Place.Fields)
		{
			const FString Name = Pair.Key.ToString();
			TestTrue(FString::Printf(TEXT("the place: %s is lowercase"), *Name),
				Name.ToLower() == Name);
			TestTrue(FString::Printf(TEXT("the place: %s is unique"), *Name),
				!PlaceSeen.Contains(Name));
			PlaceSeen.Add(Name);
		}
	}

	return true;
}

// The entity chain the NPC stands on: `CBaseEntity`, `CBaseToggle`, `CBaseAnimating` and
// `CBaseCombatCharacter`, each generated from its own retail datamap table (0019 story 2 pass B).
// Same contract as the NPC's above — the emission is held to its own counts — plus the one thing
// only this chain has: the character sheet, whose 148 rows are retail datamap ROWS and must line
// up slot for slot with the port's own compiled slot table.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBindingsChainTest,
	"Elysium.Substrate.NpcKernelBindings.Chain", GElysiumNpcBindingsFlags)
bool FElysiumNpcKernelBindingsChainTest::RunTest(const FString&)
{
	using EClass = ElysiumNpcKernelBindings::EClass;

	struct FNode
	{
		const TCHAR* Name;
		EClass Class;
		void (*Add)(FElysiumClassDesc&);
	};
	const FNode Nodes[] = {
		{ TEXT("CBaseEntity"), EClass::BaseEntity, &ElysiumNpcKernelBindings::AddBaseEntityFields },
		{ TEXT("CBaseToggle"), EClass::Toggle, &ElysiumNpcKernelBindings::AddToggleFields },
		{ TEXT("CBaseAnimating"), EClass::Animating, &ElysiumNpcKernelBindings::AddAnimatingFields },
		{ TEXT("CBaseCombatCharacter"), EClass::CombatCharacter,
			&ElysiumNpcKernelBindings::AddCombatCharacterFields },
	};
	for (const FNode& Node : Nodes)
	{
		FElysiumClassDesc D;
		Node.Add(D);
		const ElysiumNpcKernelBindings::FCounts Counts =
			ElysiumNpcKernelBindings::Counts(Node.Class);
		TestEqual(FString::Printf(
			TEXT("%s's field table carries exactly the generated bound rows"), Node.Name),
			D.Fields.Num(), Counts.Bound);
		TestEqual(FString::Printf(
			TEXT("%s's output table carries exactly the generated output rows"), Node.Name),
			ElysiumNpcKernelBindings::Outputs(Node.Class).Num(), Counts.Outputs);
		TestEqual(FString::Printf(
			TEXT("%s's inputfunc table carries exactly the generated inputfunc rows"), Node.Name),
			ElysiumNpcKernelBindings::InputFuncs(Node.Class).Num(), Counts.InputFuncs);

		// No lowercase rule on the chain: `StartHidden` and `Relationship` are retail's own
		// spellings, and the table carries what the datamap says.
		TSet<FString> Seen;
		for (const TPair<FName, FElysiumFieldAccessor>& Pair : D.Fields)
		{
			const FString Name = Pair.Key.ToString();
			TestTrue(FString::Printf(TEXT("%s: %s is unique"), Node.Name, *Name),
				!Seen.Contains(Name));
			Seen.Add(Name);
		}
	}

	// The sheet. `AddCombatCharacterFields` names every trait row from the replay, so the port's
	// compiled slot table is what says whether those names address the sheet this runtime holds:
	// each container's every slot must be reachable under some current-value name and some
	// base-value name. Retail's own spellings are the test's side of the comparison, which is why
	// the trailing underscore of `base_gender_` and the triple `base_active_active_active_dominate`
	// are not typos to fix here.
	{
		FElysiumClassDesc D;
		ElysiumNpcKernelBindings::AddCombatCharacterFields(D);
		int32 Slots = 0;
		for (uint8 i = 0; i < (uint8)EElysiumTraitContainer::Count; ++i)
		{
			const EElysiumTraitContainer Container = (EElysiumTraitContainer)i;
			for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
			{
				++Slots;
				const FElysiumFieldAccessor* Current = D.Fields.Find(FName(Slot.Datamap));
				const FElysiumFieldAccessor* Base =
					D.Fields.Find(FName(*ElysiumSheetBaseDatamap(Slot)));
				if (Current == nullptr && Slot.Alias != nullptr)
				{
					Current = D.Fields.Find(FName(Slot.Alias));
				}
				if (Base == nullptr && Slot.Alias != nullptr)
				{
					Base = D.Fields.Find(FName(*FString::Printf(TEXT("base_%s"), Slot.Alias)));
				}
				TestNotNull(FString::Printf(TEXT("%s slot %d (%s) has a current-value row"),
					ElysiumTraitContainerName(Container), Slot.Index, Slot.Datamap), Current);
				TestNotNull(FString::Printf(TEXT("%s slot %d (%s) has a base-value row"),
					ElysiumTraitContainerName(Container), Slot.Index, Slot.Datamap), Base);
			}
		}
		TestEqual(TEXT("the compiled sheet is 74 slots"), Slots, 74);
	}

	return true;
}

// The generated SAVE walk, driven end to end through the real persistence path.
//
// `Counts` above asserts the SHAPE of the emission — that the rows exist and carry the right
// flags. Nothing asserted that they survive a save. That matters here more than it usually would,
// because until 0019/2 pass C the NPC leaf ALSO wrote 62 of these words by hand, and
// `ApplyEntityRecord` restores registered fields first and replays the leaf blob after — so a
// hand-written word silently overrode the generated one and the walk's own value never landed.
// The failure mode was a value mismatch with no error anywhere, which is exactly the kind of
// thing only a round trip catches.
//
// So: stamp every registered `Save` row with a value it could not hold by accident, freeze, apply
// onto a world that was just built and knows nothing about any of it, and require each row to
// read back. A row that does not is either a hand serializer that has come back, or a restore
// hook overreaching — and the second is a real answer, which is why the exceptions below are
// named one by one rather than skipped in bulk.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBindingsSaveRoundTripTest,
	"Elysium.Substrate.NpcKernelBindings.SaveRoundTrip", GElysiumNpcBindingsFlags)
bool FElysiumNpcKernelBindingsSaveRoundTripTest::RunTest(const FString&)
{
	// What the restore hook is SUPPOSED to overwrite, each with the reason it does. Retail's own
	// slot 130 re-derives rather than trusts for exactly these, so a row here is the walk working
	// and the hook working, not a leak. Anything not on this list must survive untouched.
	struct FDerived { const TCHAR* Name; const TCHAR* Why; };
	static const FDerived Derived[] =
	{
		// --- The restart divergence, through retail's own slot 435 -------------------------------
		// `BaseOnRestore 0x1027bf50` re-finds the saved program and retail then RESUMES it, because
		// its datamap also restored the task cursor at `+0x5c50`. This port does not save that
		// cursor (a task holds a clip, a pending move or a deadline, none of which survive a load),
		// so it restarts the program instead -- and a restart runs `TroikaOnScheduleChange`
		// `0x102a0940`, retail's own slot 435, which releases exactly the per-run words below. They
		// are the price of the restart, they belong to the task that is not coming back, and the
		// restarted program's first tasks set them again.
		{ TEXT("m_flGoalTolerance"),              TEXT("released by slot 435 `0x102a09ce`") },
		{ TEXT("m_flInsideInterruptDistanceSqr"), TEXT("released by slot 435 `0x102a09d4`") },
		{ TEXT("m_flOutsideInterruptDistanceSqr"),TEXT("released by slot 435 `0x102a09da`") },
		{ TEXT("m_flInterruptTime"),              TEXT("released by slot 435 `0x102a09e0`") },
		{ TEXT("m_hMoveTargetEnt"),               TEXT("released by slot 435 `0x102a09e6`") },
		{ TEXT("m_flDesiredMoveYaw"),             TEXT("released by slot 435 `0x102a0a8d`") },
		{ TEXT("m_bWaitFinishedSet"),             TEXT("released by slot 435 `0x102a0a93`") },
		{ TEXT("m_flMoveWaitFinished"),           TEXT("released by slot 435 `0x1027a716`") },
		{ TEXT("m_bShouldMove"),                  TEXT("released by slot 435 `0x102a09c8`") },
		{ TEXT("m_hOpeningDoor"),
		  TEXT("slot 435's live-door arm `0x102a09f8` dispatches slot 532, which clears the pair") },
		{ TEXT("m_bOpeningDoorWait"),             TEXT("cleared with `m_hOpeningDoor`") },
		{ TEXT("m_bDidMaintainSchedule"),
		  TEXT("false by every install, restated as a rule of the install rather than a side "
		       "effect -- a restarted program gets the one think of DELAY_INTERRUPTS immunity a "
		       "fresh install gives") },
		{ TEXT("m_iFeedPhase"),
		  TEXT("the restart interrupts a running feed, and its teardown leaves the phase in "
		       "`ReleaseTail` rather than wherever the record found it") },
		{ TEXT("m_IdealSchedule"),
		  TEXT("the restart re-seeds the host's ideal alongside the program it installs") },

		// --- Values another build could have written, refused rather than trusted ---------------
		{ TEXT("m_iPLCriminalLevelWitnessed"),
		  TEXT("clamped to `ElysiumLaw::MaxActivityLevel` by the witness hook") },
		{ TEXT("m_iPLSupernaturalLevelWitnessed"),
		  TEXT("clamped to `ElysiumLaw::MaxActivityLevel` by the witness hook") },

		// --- Derived, not stored ----------------------------------------------------------------
		{ TEXT("m_hActiveWeapon"),
		  TEXT("`Inventory.RebuildFrom` re-derives the handle list after every record has landed: "
		       "it is a cache of what the items' own Save rows say, not a fact of its own") },
		{ TEXT("m_eForcedState"),
		  TEXT("a word of the scripted-schedule order, which is session state -- the hook retires "
		       "the order rather than resuming a route out of the previous map epoch") },
		{ TEXT("m_flLastInPlayerLOS"),
		  TEXT("re-headed against the live clock by the senses hook") },
		{ TEXT("m_flLastInPlayerPVS"),
		  TEXT("re-headed against the live clock by the senses hook") },
		{ TEXT("m_flNextPlayerLOS"),
		  TEXT("re-headed against the live clock by the senses hook") },
		{ TEXT("m_flSeekDistInspection"),
		  TEXT("`ResolveTuning` re-derives the perception pair from the restored keyfields") },
		{ TEXT("m_flHearingScalarInspection"),
		  TEXT("`ResolveTuning` re-derives the perception pair from the restored keyfields") },
	};
	auto IsDerived = [](FName Row, const TCHAR*& OutWhy) -> bool
	{
		for (const FDerived& D : Derived)
		{
			if (Row == FName(D.Name)) { OutWhy = D.Why; return true; }
		}
		return false;
	};

	// A value of each type that no spawn default and no re-derivation produces, so "it came back"
	// cannot be confused with "it was never written". Handles are the exception and have to name a
	// LIVE index: the applier re-stamps a saved handle's epoch and drops one whose index no longer
	// exists, so a bogus index would read back Invalid for a correct reason.
	int32 Salt = 0;
	auto Stamp = [&Salt](const FElysiumFieldAccessor& Acc,
		const FElysiumEntityHandle& Live) -> FElysiumVariant
	{
		++Salt;
		switch (Acc.Type)
		{
		case EElysiumVariantType::Bool:   return FElysiumVariant::Bool(true);
		// Inside a `uint8` so an enum-backed row round-trips its storage rather than its top
		// bits: pass B widened the marshaller to every integral type and every enum precisely
		// because retail's `FIELD_INTEGER` lands on the port's narrow words, and a stamp that
		// overflowed one would fail here for the marshaller's reason instead of the record's.
		case EElysiumVariantType::Int:    return FElysiumVariant::Int(100 + (Salt % 100));
		case EElysiumVariantType::Float:  return FElysiumVariant::Float(2000.f + Salt);
		case EElysiumVariantType::String: return FElysiumVariant::String(
			FString::Printf(TEXT("roundtrip_%d"), Salt));
		case EElysiumVariantType::Vector: return FElysiumVariant::Vector(
			FVector(Salt, Salt + 1, Salt + 2));
		case EElysiumVariantType::Handle: return FElysiumVariant::Handle(Live);
		default:                          return FElysiumVariant::Void();
		}
	};

	FElysiumNpcWorldFixture F(([]
	{
		FElysiumNpcWorldBuilder B(TEXT("bindings_roundtrip"), 20260921u);
		B.AddNpc(TEXT("subject"));
		// A second NPC so every saved handle can name a live entity that is not the subject: a
		// handle that pointed at itself would round-trip even if the rebase dropped it.
		B.AddNpc(TEXT("other"), FVector(256.0, 0.0, 0.0));
		return B;
	})());

	FElysiumNpc* Subject = F.Npc(TEXT("subject"));
	FElysiumNpc* Other = F.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the subject NPC stands"), Subject)
		|| !TestNotNull(TEXT("the witness NPC stands"), Other))
	{
		return false;
	}

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	if (!TestNotNull(TEXT("the subject carries a class descriptor"), Subject->Class))
	{
		return false;
	}

	// Only the generated walk's own rows: they are the ones with no external name, which is what
	// the `m_` prefix means here (`docs/vtmb/python_bridge.md` § "The three gates, read together").
	TMap<FName, FElysiumVariant> Written;
	for (const FName& Row : Reg.SaveFields(*Subject->Class))
	{
		if (!Row.ToString().StartsWith(TEXT("m_")))
		{
			continue;
		}
		const FElysiumFieldAccessor* Acc = Reg.FindField(*Subject->Class, Row);
		if (Acc == nullptr || !Acc->Set || !Acc->Get)
		{
			continue;
		}
		const FElysiumVariant Value = Stamp(*Acc, Other->Handle);
		if (Value.Type == EElysiumVariantType::Void)
		{
			continue;
		}
		Acc->Set(*Subject, Value);
		Written.Emplace(Row, Value);
	}

	// The walk is the NPC's whole retail persistence, so an empty one would make every assertion
	// below vacuously true.
	if (!TestTrue(TEXT("the generated walk carries rows to round-trip"), Written.Num() > 100))
	{
		return false;
	}

	FElysiumMapSnapshot Snapshot;
	F.World.Freeze(Snapshot);
	if (!TestTrue(TEXT("the frozen map carries records"), Snapshot.Entities.Num() > 0))
	{
		return false;
	}

	// A world built from the same defs and nothing else: every value below has to have come out of
	// the record, because nothing here ever wrote one.
	FElysiumNpcWorldFixture G(([]
	{
		FElysiumNpcWorldBuilder B(TEXT("bindings_roundtrip"), 20260921u);
		B.AddNpc(TEXT("subject"));
		B.AddNpc(TEXT("other"), FVector(256.0, 0.0, 0.0));
		return B;
	})());
	TestTrue(TEXT("the snapshot applies"), G.World.ApplySnapshot(Snapshot) > 0);

	FElysiumNpc* Restored = G.Npc(TEXT("subject"));
	FElysiumNpc* RestoredOther = G.Npc(TEXT("other"));
	if (!TestNotNull(TEXT("the subject restores"), Restored)
		|| !TestNotNull(TEXT("the witness restores"), RestoredOther))
	{
		return false;
	}

	int32 Survived = 0;
	int32 Rederived = 0;
	for (const TPair<FName, FElysiumVariant>& Row : Written)
	{
		const FElysiumFieldAccessor* Acc = Reg.FindField(*Restored->Class, Row.Key);
		if (!TestNotNull(*FString::Printf(TEXT("%s is still a registered row"), *Row.Key.ToString()),
			Acc))
		{
			continue;
		}
		const FElysiumVariant Back = Acc->Get(*Restored);
		bool bSame = false;
		switch (Row.Value.Type)
		{
		case EElysiumVariantType::Bool:   bSame = Back.AsBool == Row.Value.AsBool; break;
		case EElysiumVariantType::Int:    bSame = Back.AsInt == Row.Value.AsInt; break;
		case EElysiumVariantType::Float:
			bSame = FMath::IsNearlyEqual(Back.AsFloat, Row.Value.AsFloat, 0.01f); break;
		case EElysiumVariantType::String: bSame = Back.AsString == Row.Value.AsString; break;
		case EElysiumVariantType::Vector:
			bSame = Back.AsVector.Equals(Row.Value.AsVector, 0.01); break;
		case EElysiumVariantType::Handle:
			// The epoch is the applier's to re-stamp, so the INDEX is what round-trips.
			bSame = Back.AsHandle.Index == RestoredOther->Handle.Index; break;
		default: break;
		}

		const TCHAR* Why = nullptr;
		if (IsDerived(Row.Key, Why))
		{
			++Rederived;
			continue;
		}
		if (bSame)
		{
			++Survived;
		}
		else
		{
			AddError(FString::Printf(
				TEXT("the generated save row '%s' did not survive the record — either a hand ")
				TEXT("serializer writes it again after the field walk, or a restore hook ")
				TEXT("overwrites it without saying so"),
				*Row.Key.ToString()));
		}
	}

	TestEqual(TEXT("every generated save row that is not re-derived survived the record"),
		Survived + Rederived, Written.Num());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
