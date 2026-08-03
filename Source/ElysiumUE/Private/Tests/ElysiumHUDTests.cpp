#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumHUDModel.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHUDModelProjectionTest,
	"Elysium.Substrate.UI.HUDModelProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumHUDModelProjectionTest::RunTest(const FString& Parameters)
{
	UElysiumHUDModel* Model = NewObject<UElysiumHUDModel>();
	FElysiumViewState View;
	Model->Apply(View);
	TestFalse(TEXT("default view suppresses HUD"), Model->bVisible);
	TestFalse(TEXT("default vitals are invalid"), Model->bVitalsValid);
	TestFalse(TEXT("unwired equipment remains invalid"), Model->Equipment.bValid);
	TestFalse(TEXT("unwired selector remains closed"), Model->Selector.IsOpen());

	View.bPlayerSurface = true;
	View.Vitals.bValid = true;
	View.Vitals.Health = 73;
	View.Vitals.MaxHealth = 110;
	View.Vitals.BloodPool = 7;
	View.Vitals.MaxBloodPool = 12;
	View.Vitals.Humanity = 6;
	View.Vitals.Masquerade = 2;
	View.ReticleIcon = 0;
	Model->Apply(View);

	TestTrue(TEXT("player surface displays HUD"), Model->bVisible);
	TestEqual(TEXT("health projects exactly"), Model->Health, 73);
	TestEqual(TEXT("maximum health projects exactly"), Model->MaxHealth, 110);
	TestEqual(TEXT("blood projects exactly"), Model->BloodPool, 7);
	TestEqual(TEXT("blood capacity projects exactly"), Model->BloodCapacity, 12);
	TestEqual(TEXT("ordinary aim resolves to cross"), Model->Reticle, EElysiumHUDReticle::Cross);
	TestFalse(TEXT("production projection never invents equipment"), Model->Equipment.bValid);

	View.ReticleIcon = 4;
	Model->Apply(View);
	TestEqual(TEXT("published use icon resolves to context cursor"),
		Model->Reticle, EElysiumHUDReticle::UseIcon);

	View.bSignHidesHUD = true;
	Model->Apply(View);
	TestEqual(TEXT("HideHUD suppresses the cursor through the shared rule"),
		Model->Reticle, EElysiumHUDReticle::None);

	View.bSignHidesHUD = false;
	View.bCinematic = true;
	Model->Apply(View);
	TestFalse(TEXT("scripted camera suppresses the heads-up layer"), Model->bVisible);
	TestEqual(TEXT("scripted camera suppresses the cursor through the shared rule"),
		Model->Reticle, EElysiumHUDReticle::None);
	TestEqual(TEXT("cinematic suppression retains live health for an immediate return"),
		Model->Health, 73);

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Weapon);
	TestTrue(TEXT("preview can exercise the HUD without gameplay owners"), Model->bVisible);
	TestTrue(TEXT("weapon preview supplies representative equipment"), Model->Equipment.bValid);
	TestEqual(TEXT("weapon preview opens the selector"),
		Model->Selector.Type, EElysiumHUDSelector::Weapons);
	TestTrue(TEXT("weapon preview has a selected entry"),
		Model->Selector.Entries.IsValidIndex(Model->Selector.SelectedIndex));

	return true;
}

#endif
