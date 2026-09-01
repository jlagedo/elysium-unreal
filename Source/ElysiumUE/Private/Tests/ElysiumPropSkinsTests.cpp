// Content-free Substrate automation for UElysiumPropSkinSet::Find's skin-index clamp
// (docs/architecture/seam_map_model.md -> "Import" -> "Material binding" and -> "Skins table"):
// VtMB's `skin` keyfield/input is an unclamped int write, so a placement can name a family past
// the model's own count, and the engine clamps to the last family rather than falling back to 0
// or refusing to draw. The clamp lives in `Find`, not at any call site.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumPropSkins.h"

static constexpr EAutomationTestFlags GElysiumPropSkinsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One stem with three real families (0, 1, 2) plus a fourth (3) identical to family 0 and so
	// carrying no row -- the array is trimmed after the last family that repaints something, and
	// FamilyCount (4) is the true count the clamp target reads.
	UElysiumPropSkinSet* BuildSkinSet()
	{
		UElysiumPropSkinSet* Asset = NewObject<UElysiumPropSkinSet>(GetTransientPackage(), NAME_None, RF_Transient);

		FElysiumPropSkinModel Model;
		Model.Stem = FName(TEXT("scenery_lamp"));
		Model.FamilyCount = 4;

		FElysiumSkinFamily Family0; // the authored set: always empty
		FElysiumSkinFamily Family1;
		FElysiumSkinOverride Override1;
		Override1.SlotName = FName(TEXT("glass"));
		Family1.Overrides.Add(Override1);
		FElysiumSkinFamily Family2;
		FElysiumSkinOverride Override2;
		Override2.SlotName = FName(TEXT("base"));
		Family2.Overrides.Add(Override2);
		// Family 3 is identical to family 0 and is trimmed: the array stops at index 2.

		Model.Families = {Family0, Family1, Family2};
		Asset->Models.Add(Model);
		return Asset;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSkinsFindInRangeTest,
	"Elysium.Substrate.PropSkins.FindInRange", GElysiumPropSkinsTestFlags)
bool FElysiumPropSkinsFindInRangeTest::RunTest(const FString&)
{
	UElysiumPropSkinSet* Asset = BuildSkinSet();
	const FElysiumSkinFamily* Family1 = Asset->Find(FName(TEXT("scenery_lamp")), 1);
	if (!TestNotNull(TEXT("family 1 (in range) resolves"), Family1))
	{
		return false;
	}
	TestEqual(TEXT("family 1's own override"), Family1->Overrides[0].SlotName, FName(TEXT("glass")));

	const FElysiumSkinFamily* Family2 = Asset->Find(FName(TEXT("scenery_lamp")), 2);
	if (TestNotNull(TEXT("family 2 (in range, the last real family) resolves"), Family2))
	{
		TestEqual(TEXT("family 2's own override"), Family2->Overrides[0].SlotName, FName(TEXT("base")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSkinsFindOutOfRangeClampsToLastTest,
	"Elysium.Substrate.PropSkins.FindOutOfRangeClampsToLast", GElysiumPropSkinsTestFlags)
bool FElysiumPropSkinsFindOutOfRangeClampsToLastTest::RunTest(const FString&)
{
	UElysiumPropSkinSet* Asset = BuildSkinSet();

	// Family 5 is past FamilyCount (4): clamps to family 3 (FamilyCount - 1), which is identical
	// to family 0 and carries no row -- so the clamped lookup finds nothing, exactly the picture
	// VtMB itself would draw (family 3's authored set, which equals family 0's).
	const FElysiumSkinFamily* Clamped = Asset->Find(FName(TEXT("scenery_lamp")), 5);
	TestNull(TEXT("family 5 clamps to family 3 (no row -- identical to family 0)"), Clamped);

	// Exactly at FamilyCount (4, one past the last valid index 3): also clamps to family 3.
	const FElysiumSkinFamily* AtCount = Asset->Find(FName(TEXT("scenery_lamp")), 4);
	TestNull(TEXT("family == FamilyCount clamps to family 3 (no row)"), AtCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSkinsFindOutOfRangeClampsToLastWithOverrideTest,
	"Elysium.Substrate.PropSkins.FindOutOfRangeClampsToLastWithOverride", GElysiumPropSkinsTestFlags)
bool FElysiumPropSkinsFindOutOfRangeClampsToLastWithOverrideTest::RunTest(const FString&)
{
	// A stem whose true last family (FamilyCount - 1) DOES repaint something: the clamp must land
	// on that row, not merely fail to crash.
	UElysiumPropSkinSet* Asset = NewObject<UElysiumPropSkinSet>(GetTransientPackage(), NAME_None, RF_Transient);
	FElysiumPropSkinModel Model;
	Model.Stem = FName(TEXT("scenery_sign"));
	Model.FamilyCount = 2;
	FElysiumSkinFamily Family0;
	FElysiumSkinFamily Family1;
	FElysiumSkinOverride Override1;
	Override1.SlotName = FName(TEXT("panel"));
	Family1.Overrides.Add(Override1);
	Model.Families = {Family0, Family1};
	Asset->Models.Add(Model);

	const FElysiumSkinFamily* Row = Asset->Find(FName(TEXT("scenery_sign")), 99);
	if (!TestNotNull(TEXT("family 99 clamps to family 1, the real last family"), Row))
	{
		return false;
	}
	TestEqual(TEXT("clamped row is family 1's own override"), Row->Overrides[0].SlotName, FName(TEXT("panel")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSkinsFindZeroAndNegativeReturnNoneTest,
	"Elysium.Substrate.PropSkins.FindZeroAndNegativeReturnNone", GElysiumPropSkinsTestFlags)
bool FElysiumPropSkinsFindZeroAndNegativeReturnNoneTest::RunTest(const FString&)
{
	UElysiumPropSkinSet* Asset = BuildSkinSet();
	TestNull(TEXT("family 0 (the authored set) never resolves a row"),
		Asset->Find(FName(TEXT("scenery_lamp")), 0));
	TestNull(TEXT("a negative family index resolves nothing"),
		Asset->Find(FName(TEXT("scenery_lamp")), -1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPropSkinsFindWithNoFamilyCountFallsBackToArrayBoundTest,
	"Elysium.Substrate.PropSkins.FindWithNoFamilyCountFallsBackToArrayBound", GElysiumPropSkinsTestFlags)
bool FElysiumPropSkinsFindWithNoFamilyCountFallsBackToArrayBoundTest::RunTest(const FString&)
{
	// An asset authored before FamilyCount existed (the legacy per-map bake): FamilyCount defaults
	// to 0, so the clamp is skipped and the lookup is bounded by the array alone, exactly as
	// `Find` behaved before this change -- an old asset's behaviour must not change underneath it.
	UElysiumPropSkinSet* Asset = NewObject<UElysiumPropSkinSet>(GetTransientPackage(), NAME_None, RF_Transient);
	FElysiumPropSkinModel Model;
	Model.Stem = FName(TEXT("legacy_stem"));
	// Model.FamilyCount left at its default (0).
	FElysiumSkinFamily Family0;
	FElysiumSkinFamily Family1;
	FElysiumSkinOverride Override1;
	Override1.SlotName = FName(TEXT("skin"));
	Family1.Overrides.Add(Override1);
	Model.Families = {Family0, Family1};
	Asset->Models.Add(Model);

	if (const FElysiumSkinFamily* Row = Asset->Find(FName(TEXT("legacy_stem")), 1))
	{
		TestEqual(TEXT("in-range family 1 still resolves"), Row->Overrides[0].SlotName, FName(TEXT("skin")));
	}
	else
	{
		AddError(TEXT("family 1 should resolve with no FamilyCount recorded"));
	}
	// Out of the array's own bound, with no FamilyCount to clamp against: null, the pre-clamp
	// behaviour (out-of-range was never an error, just "no row").
	TestNull(TEXT("out-of-array-bound family with FamilyCount == 0 resolves nothing"),
		Asset->Find(FName(TEXT("legacy_stem")), 7));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
