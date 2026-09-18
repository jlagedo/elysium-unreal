#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
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

#endif  // WITH_DEV_AUTOMATION_TESTS
