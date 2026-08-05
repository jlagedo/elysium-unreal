#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumHUDModel.h"
#include "UI/ElysiumUIRoot.h"

#include "CommonActivatableWidget.h"
#include "CommonInputSettings.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUIRootPushTest,
	"Elysium.Substrate.UI.CompositionRootPush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUIRootPushTest::RunTest(const FString& Parameters)
{
	UElysiumUIRoot* Root = NewObject<UElysiumUIRoot>();
	Root->TakeWidget();

	UCommonActivatableWidget* Screen = Root->PushWidget(
		EElysiumUILayer::SystemModal,
		UCommonActivatableWidget::StaticClass(),
		[](UCommonActivatableWidget&) {});

	TestNotNull(TEXT("a CommonUI screen can be created through the root layer"), Screen);
	TestEqual(TEXT("the pushed screen becomes the layer's active widget"),
		Root->GetActiveWidget(EElysiumUILayer::SystemModal), Screen);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUICompositionPolicyTest,
	"Elysium.Substrate.UI.CompositionPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUICompositionPolicyTest::RunTest(const FString& Parameters)
{
	UCommonInputSettings* CommonInput = GetMutableDefault<UCommonInputSettings>();
	CommonInput->LoadData();
	const FDataTableRowHandle Click = CommonInput->GetDefaultClickAction();
	const FDataTableRowHandle Back = CommonInput->GetDefaultBackAction();
	TestFalse(TEXT("CommonUI never installs a competing default input mode"),
		CommonInput->GetEnableDefaultInputConfig());
	TestNotNull(TEXT("native Accept action table resolves"), Click.DataTable.Get());
	TestEqual(TEXT("native Accept action row resolves"), Click.RowName, FName(TEXT("Accept")));
	TestNotNull(TEXT("native Back action table resolves"), Back.DataTable.Get());
	TestEqual(TEXT("native Back action row resolves"), Back.RowName, FName(TEXT("Back")));

	const FString SourceRoot = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Source/ElysiumUE"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *SourceRoot, TEXT("*.cpp"), true, false);

	struct FRule
	{
		const TCHAR* Needle;
		const TCHAR* AllowedFile;
	};
	const FRule Rules[] =
	{
		{ TEXT("AddToViewport("), nullptr },
		{ TEXT("AddViewportWidgetContent("), nullptr },
		{ TEXT("AddToPlayerScreen("), TEXT("ElysiumPlayerUISubsystem.cpp") },
		{ TEXT("ActivateWidget("), nullptr },
		{ TEXT("DeactivateWidget("), nullptr },
		{ TEXT("SetInputMode("), TEXT("ElysiumInputSubsystem.cpp") },
	};

	for (const FString& File : Files)
	{
		if (File.EndsWith(TEXT("ElysiumHUDTests.cpp")))
		{
			continue;
		}
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *File))
		{
			AddError(FString::Printf(TEXT("Could not inspect UI composition policy in %s"), *File));
			continue;
		}
		for (const FRule& Rule : Rules)
		{
			if (Contents.Contains(Rule.Needle) &&
				(!Rule.AllowedFile || !File.EndsWith(Rule.AllowedFile)))
			{
				AddError(FString::Printf(TEXT("%s uses forbidden composition call %s"),
					*File, Rule.Needle));
			}
		}
	}

	return !HasAnyErrors();
}

#endif
