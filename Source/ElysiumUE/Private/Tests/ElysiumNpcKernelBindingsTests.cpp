#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumNpcKernelBindings.h"

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

#endif  // WITH_DEV_AUTOMATION_TESTS
