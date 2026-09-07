#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumHUDModel.h"
#include "ElysiumHUDTypes.h"
#include "ElysiumPresentationSubsystem.h"
#include "Substrate/ElysiumSignData.h"
#include "UI/ElysiumActionButton.h"
#include "UI/ElysiumCharacterScreen.h"
#include "UI/ElysiumChargenPopup.h"
#include "UI/ElysiumDialogueScreen.h"
#include "UI/ElysiumDialogueWidget.h"
#include "UI/ElysiumCommonUIInputData.h"
#include "UI/ElysiumMainMenu.h"
#include "UI/ElysiumNotificationScreen.h"
#include "UI/ElysiumSignScreen.h"
#include "UI/ElysiumTerminalScreen.h"
#include "UI/ElysiumUIRoot.h"
#include "UI/ElysiumUIStyle.h"

#include "CommonActivatableWidget.h"
#include "CommonInputSettings.h"
#include "ICommonInputModule.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"

namespace
{
	int32 CountSlateWidgetsOfType(const TSharedRef<SWidget>& Widget, FName WidgetType)
	{
		int32 Count = Widget->GetType() == WidgetType ? 1 : 0;
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			Count += CountSlateWidgetsOfType(Children->GetChildAt(Index), WidgetType);
		}
		return Count;
	}

	// One published response row, the shape the presentation subsystem hands the screen.
	FElysiumDialogueChoiceView ElysiumTestDialogueRow(const TCHAR* Text, bool bEnabled = true,
		const TCHAR* Label = TEXT(""))
	{
		FElysiumDialogueChoiceView Row;
		Row.Text = Text;
		Row.bEnabled = bEnabled;
		Row.Label = Label;
		return Row;
	}

	// The blood rail is the one horizontal box built with 15 droplet slots and the two group
	// spacers between them. Finding it by that shape keeps the test off the widget's private
	// internals while still asserting on the real constructed Slate.
	constexpr int32 GExpectedBloodRowChildren = 17;   // 15 droplets + 2 group spacers

	TSharedPtr<SWidget> FindBloodRow(const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == FName(TEXT("SHorizontalBox")))
		{
			FChildren* Own = Widget->GetChildren();
			if (Own && Own->Num() == GExpectedBloodRowChildren)
			{
				return Widget;
			}
		}
		FChildren* Children = Widget->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindBloodRow(Children->GetChildAt(Index)))
			{
				return Found;
			}
		}
		return nullptr;
	}

	// How many of the rail's droplets are drawn, and how many groups they fall into. A group is a
	// run of uncollapsed droplets bounded by the spacers, so this reads the rail exactly as the
	// player does.
	void MeasureBloodRail(const TSharedRef<SWidget>& Row, int32& OutDroplets, int32& OutGroups)
	{
		OutDroplets = 0;
		OutGroups = 0;
		int32 RunLength = 0;
		FChildren* Children = Row->GetChildren();
		for (int32 Index = 0; Children && Index < Children->Num(); ++Index)
		{
			const TSharedRef<SWidget> Child = Children->GetChildAt(Index);
			if (Child->GetType() == FName(TEXT("SSpacer")))
			{
				if (RunLength > 0) { ++OutGroups; }
				RunLength = 0;
				continue;
			}
			if (Child->GetVisibility() != EVisibility::Collapsed)
			{
				++OutDroplets;
				++RunLength;
			}
		}
		if (RunLength > 0) { ++OutGroups; }
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNotificationPresentationRulesTest,
	"Elysium.Substrate.UI.NotificationPresentationRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumNotificationPresentationRulesTest::RunTest(const FString& Parameters)
{
	FElysiumNotification Item;
	Item.Kind = EElysiumNotificationKind::ItemAcquired;
	Item.Subject = TEXT("Tire Iron");
	Item.Quantity = 4;
	TestEqual(TEXT("item category is the modern fixed label"),
		ElysiumNotificationUI::CategoryLabel(Item.Kind).ToString(), FString(TEXT("ITEM ACQUIRED")));
	TestEqual(TEXT("stack quantities append a multiplication count"),
		ElysiumNotificationUI::SubjectLabel(Item).ToString(), FString(TEXT("Tire Iron \u00d74")));

	FElysiumNotification Quest;
	Quest.Kind = EElysiumNotificationKind::QuestCompleted;
	Quest.Subject = TEXT("The Tutorial");
	TestEqual(TEXT("quest completion has its own semantic label"),
		ElysiumNotificationUI::CategoryLabel(Quest.Kind).ToString(),
		FString(TEXT("QUEST COMPLETED")));
	TestEqual(TEXT("quest subjects are unchanged"),
		ElysiumNotificationUI::SubjectLabel(Quest).ToString(), FString(TEXT("The Tutorial")));

	TestEqual(TEXT("the card begins transparent"),
		ElysiumNotificationUI::OpacityAt(0.0f), 0.0f);
	TestEqual(TEXT("the card finishes its entrance opaque"),
		ElysiumNotificationUI::OpacityAt(ElysiumNotificationUI::EnterSeconds), 1.0f);
	TestEqual(TEXT("the hold remains opaque"),
		ElysiumNotificationUI::OpacityAt(ElysiumNotificationUI::EnterSeconds + 1.0f), 1.0f);
	TestEqual(TEXT("the card ends transparent"),
		ElysiumNotificationUI::OpacityAt(ElysiumNotificationUI::TotalSeconds), 0.0f);
	TestEqual(TEXT("the entrance begins twelve virtual pixels high"),
		ElysiumNotificationUI::OffsetYAt(0.0f), -12.0f);
	TestEqual(TEXT("the hold reaches its authored position"),
		ElysiumNotificationUI::OffsetYAt(ElysiumNotificationUI::EnterSeconds), 0.0f);

	UElysiumNotificationScreen* Screen = NewObject<UElysiumNotificationScreen>();
	TestFalse(TEXT("notification cards never take keyboard focus"), Screen->IsFocusable());
	Screen->ApplyNotification(Item);
	Screen->SetSuspended(true);
	TestTrue(TEXT("the owner can suspend card timing"), Screen->IsSuspended());
	TestEqual(TEXT("the payload remains immutable through suspension"),
		Screen->GetNotification(), Item);

	UElysiumPresentationSubsystem* Publisher = NewObject<UElysiumPresentationSubsystem>();
	for (int32 Index = 0; Index < 65; ++Index)
	{
		FElysiumNotification Pending;
		Pending.Subject = FString::Printf(TEXT("Notice %02d"), Index);
		Publisher->PostNotification(Pending);
	}
	TestEqual(TEXT("the publisher bounds retained notifications"),
		Publisher->NumPendingNotifications(), 64);
	const TArray<FElysiumNotification>& Pending = Publisher->PendingNotificationQueue();
	TestEqual(TEXT("FIFO retains the oldest event first"),
		Pending[0].Subject, FString(TEXT("Notice 00")));
	TestEqual(TEXT("overflow drops the newest event"),
		Pending.Last().Subject, FString(TEXT("Notice 63")));
	Publisher->Publish(); // no game world means the player surface is suppressed
	TestEqual(TEXT("suppression retains every pending notification"),
		Publisher->NumPendingNotifications(), 64);
	return !HasAnyErrors();
}

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
	TestEqual(TEXT("unwired zone stays collapsed"), Model->ZoneState, EElysiumZoneState::None);

	View.bPlayerSurface = true;
	View.Vitals.bValid = true;
	View.Vitals.Health = 73;
	View.Vitals.MaxHealth = 110;
	View.Vitals.BloodPool = 7;
	View.Vitals.MaxBloodPool = 12;
	View.Vitals.Humanity = 6;
	View.Vitals.Masquerade = 2;
	View.Interaction.Icon = 0;
	Model->Apply(View);

	TestTrue(TEXT("player surface displays HUD"), Model->bVisible);
	TestEqual(TEXT("health projects exactly"), Model->Health, 73);
	TestEqual(TEXT("maximum health projects exactly"), Model->MaxHealth, 110);
	TestEqual(TEXT("blood projects exactly"), Model->BloodPool, 7);
	TestEqual(TEXT("blood capacity projects exactly"), Model->BloodCapacity, 12);
	TestEqual(TEXT("ordinary aim resolves to cross"), Model->Reticle, EElysiumHUDReticle::Cross);
	TestFalse(TEXT("production projection never invents equipment"), Model->Equipment.bValid);

	View.Interaction.bVisible = true;
	View.Interaction.bActionable = true;
	View.Interaction.Icon = 4;
	View.Interaction.PromptAlpha = 0.75f;
	Model->Apply(View);
	TestEqual(TEXT("published use icon resolves to context cursor"),
		Model->Reticle, EElysiumHUDReticle::UseIcon);
	TestEqual(TEXT("prompt fade alpha projects exactly"), Model->UsePromptAlpha, 0.75f);
	TestTrue(TEXT("actionable focus projects exactly"), Model->bUseActionable);
	TestEqual(TEXT("interaction publishes the semantic Use action"),
		Model->UseAction, FName(TEXT("Use")));
	TestEqual(TEXT("keyboard use binding text"),
		ElysiumInteraction::UseBindingText(false).ToString(), FString(TEXT("E")));
	TestEqual(TEXT("gamepad use binding text"),
		ElysiumInteraction::UseBindingText(true).ToString(), FString(TEXT("RT")));

	View.Feed.bVisible = true;
	View.Feed.bPaired = true;
	View.Feed.BloodPool = 5;
	View.Feed.MaxBloodPool = 9;
	Model->Apply(View);
	TestTrue(TEXT("a focused feed victim publishes the top blood meter"),
		Model->bFeedVictimVisible);
	TestEqual(TEXT("victim blood projects exactly"), Model->FeedVictimBlood, 5);
	TestEqual(TEXT("victim blood capacity projects exactly"), Model->FeedVictimBloodCapacity, 9);
	TestEqual(TEXT("a paired feed suppresses only the center reticle"),
		Model->Reticle, EElysiumHUDReticle::None);
	TestTrue(TEXT("the paired feed retains the ordinary side HUD"), Model->bVisible);
	View.Feed.bPaired = false;

	View.bSignHidesHUD = true;
	Model->Apply(View);
	TestFalse(TEXT("HideHUD suppresses the complete heads-up model"), Model->bVisible);
	TestEqual(TEXT("HideHUD suppresses the cursor through the shared rule"),
		Model->Reticle, EElysiumHUDReticle::None);

	View.bSignHidesHUD = false;

	// **Owning the view is not a reason to hide the HUD.** A Worldcraft `camera_track` authors no
	// `ShowHud` key, so a cutscene that runs on one keeps the heads-up layer up, exactly as retail
	// does (`docs/vtmb/camera-view-modes.md` §5).
	View.Camera.bScriptedCameraOwnsView = true;
	Model->Apply(View);
	TestTrue(TEXT("a camera_track owning the view leaves the heads-up layer up"), Model->bVisible);

	// A named `SetCamera` shot does hide it, because both its keys parse with a default of 0.
	View.Camera.bShowHud = false;
	Model->Apply(View);
	TestFalse(TEXT("a named shot's ShowHud 0 suppresses the heads-up layer"), Model->bVisible);
	TestEqual(TEXT("and suppresses the cursor through the shared rule"),
		Model->Reticle, EElysiumHUDReticle::None);
	TestEqual(TEXT("suppression retains live health for an immediate return"),
		Model->Health, 73);
	View.Camera.bShowHud = true;
	View.Camera.bScriptedCameraOwnsView = false;

	// The mode toggle selects the other crosshair path rather than taking the HUD down.
	View.Camera.ReticlePath = EElysiumReticlePath::ThirdPerson;
	Model->Apply(View);
	TestTrue(TEXT("third person keeps the heads-up layer"), Model->bVisible);
	TestEqual(TEXT("and draws the plain third-person reticle"),
		Model->Reticle, EElysiumHUDReticle::ThirdPerson);
	View.Camera.ReticlePath = EElysiumReticlePath::FirstPerson;

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Weapon);
	TestTrue(TEXT("preview can exercise the HUD without gameplay owners"), Model->bVisible);
	TestTrue(TEXT("weapon preview supplies representative equipment"), Model->Equipment.bValid);
	TestEqual(TEXT("weapon preview opens the selector"),
		Model->Selector.Type, EElysiumHUDSelector::Weapons);
	TestTrue(TEXT("weapon preview has a selected entry"),
		Model->Selector.Entries.IsValidIndex(Model->Selector.SelectedIndex));
	TestFalse(TEXT("weapon preview is the full list, not the brief peek"),
		Model->Selector.bBriefMode);
	TestEqual(TEXT("weapon preview uses the exported .38 stem"),
		Model->Equipment.Icon, ElysiumHUDArt::Inventory(TEXT("weapons_ranged/thirtyeight")));
	TestEqual(TEXT("weapon preview names the ranged class"),
		Model->Equipment.WeaponClass, EElysiumWeaponClass::Ranged);
	TestTrue(TEXT("ranged equipment publishes ammo"),
		ElysiumHUDArt::ShowsAmmo(Model->Equipment.WeaponClass));
	TestEqual(TEXT("weapon preview is a combat zone"),
		Model->ZoneState, EElysiumZoneState::Combat);
	TestTrue(TEXT("weapon rows carry inventory art paths"),
		Model->Selector.Entries.Num() >= 4
			&& Model->Selector.Entries[3].Icon
				== ElysiumHUDArt::Inventory(TEXT("weapons_ranged/grenade_frag")));

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Brief);
	TestTrue(TEXT("brief preview is the three-item peek"), Model->Selector.bBriefMode);
	TestEqual(TEXT("brief preview keeps the weapons selector"),
		Model->Selector.Type, EElysiumHUDSelector::Weapons);
	TestEqual(TEXT("brief preview stays a combat zone"),
		Model->ZoneState, EElysiumZoneState::Combat);

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Radial);
	TestEqual(TEXT("radial preview is its own selector type"),
		Model->Selector.Type, EElysiumHUDSelector::Radial);
	TestFalse(TEXT("radial preview is not brief mode"), Model->Selector.bBriefMode);
	TestEqual(TEXT("radial preview carries a discipline icon path"),
		Model->Selector.Entries[0].Icon, ElysiumHUDArt::Discipline(TEXT("bloodheal")));
	TestFalse(TEXT("radial preview includes a disabled row"),
		Model->Selector.Entries.Last().bEnabled);
	TestEqual(TEXT("active discipline publishes its icon"),
		Model->Discipline.Icon, ElysiumHUDArt::Discipline(TEXT("bloodheal")));
	TestEqual(TEXT("active discipline publishes its blood cost"),
		Model->Discipline.BloodCost, 1);

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Inventory);
	TestEqual(TEXT("inventory preview opens the inventory selector"),
		Model->Selector.Type, EElysiumHUDSelector::Inventory);
	TestEqual(TEXT("inventory preview uses the exported lockpicks stem"),
		Model->Selector.Entries[1].Icon,
		ElysiumHUDArt::Inventory(TEXT("general_items/lockpicks")));
	TestEqual(TEXT("inventory preview keeps the blood-pack detail"),
		Model->Selector.Entries[0].Detail.ToString(), FString(TEXT("Restores 3 blood")));
	TestEqual(TEXT("inventory preview keeps the blood-pack quantity"),
		Model->Selector.Entries[0].Quantity, 4);
	TestEqual(TEXT("inventory preview is a masquerade zone"),
		Model->ZoneState, EElysiumZoneState::Masquerade);
	TestFalse(TEXT("melee equipment does not publish ammo"),
		ElysiumHUDArt::ShowsAmmo(Model->Equipment.WeaponClass));

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Elysium);
	TestEqual(TEXT("elysium preview shows the Elysium zone"),
		Model->ZoneState, EElysiumZoneState::Elysium);
	TestFalse(TEXT("elysium preview does not invent a selector"), Model->Selector.IsOpen());
	TestEqual(TEXT("area art for Elysium is the exported safe-area sibling"),
		ElysiumHUDArt::Area(EElysiumZoneState::Elysium),
		FName(TEXT("hud/area_icons/area_icon_elysium")));
	TestEqual(TEXT("area art for Masquerade is the exported safe-area icon"),
		ElysiumHUDArt::Area(EElysiumZoneState::Masquerade),
		FName(TEXT("hud/area_icons/area_icon_safearea")));

	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Passive);
	TestEqual(TEXT("passive preview collapses the zone glyph"),
		Model->ZoneState, EElysiumZoneState::None);

	return true;
}


// The blood rail's droplet count, read off the constructed Slate.
//
// The number of droplets is the `BloodPool` stat DEFINITION's Max — `stats.txt` authors `Max 15`
// on slot 12 and the `"Max" "Generation_Blood_Pool_Max"` line beside it is commented out in the
// shipped file, so `CVStatList_t::IncBase(0xc)` `0x10200d60` (reached from `IncBloodPool`
// `0x10338cb0`) stops every character at 15 whatever its Generation. That is three groups of five,
// which is how retail's right rail reads the pool (`docs/vtmb/vtmb-ui.md`). The defect this covers
// was publishing slot 13 `BloodPool_Max` — a critter's STARTING pool, Default 10 — as the
// capacity, which collapsed the third group. That the capacity itself comes from the stat
// definition is asserted against the real `stats.txt` in `Elysium.Content.Sheet` and against a
// live player in `Elysium.Content.NpcMakerBlueblood`; this test owns the rail that draws it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHUDBloodRailTest,
	"Elysium.Substrate.UI.HUDBloodRail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumHUDBloodRailTest::RunTest(const FString& Parameters)
{
	// One rail per capacity: a widget is prepassed once, which is what evaluates the droplets'
	// visibility attributes, so each case gets its own construction rather than re-prepassing.
	auto MeasureRailAtCapacity = [this](int32 Capacity, int32 Banked,
		int32& OutDroplets, int32& OutGroups) -> bool
	{
		UElysiumHUDModel* Model = NewObject<UElysiumHUDModel>();

		FElysiumViewState View;
		View.bPlayerSurface = true;
		View.Vitals.bValid = true;
		View.Vitals.BloodPool = Banked;
		View.Vitals.MaxBloodPool = Capacity;
		Model->Apply(View);
		if (!TestEqual(TEXT("the published capacity projects onto the rail"),
			Model->BloodCapacity, Capacity))
		{
			return false;
		}

		// The HUD surface is only constructible through the composition root: CommonUI resolves a
		// screen's owner from the tree it was constructed in, and the root is what hands the HUD
		// widget its model. Same path the live game takes.
		UElysiumUIRoot* Root = NewObject<UElysiumUIRoot>();
		Root->SetModel(Model);
		const TSharedRef<SWidget> Slate = Root->TakeWidget();
		Slate->SlatePrepass(1.0f);
		TSharedPtr<SWidget> Row = FindBloodRow(Slate);
		if (!TestTrue(TEXT("the HUD builds one blood rail of 15 droplets and 2 group spacers"),
			Row.IsValid()))
		{
			return false;
		}
		MeasureBloodRail(Row.ToSharedRef(), OutDroplets, OutGroups);
		return true;
	};

	// What the shipped rulebook answers for `BloodPool`'s effective Max.
	int32 Droplets = 0;
	int32 Groups = 0;
	if (!MeasureRailAtCapacity(15, 12, Droplets, Groups))
	{
		return false;
	}
	TestEqual(TEXT("the shipped cap of 15 draws all 15 droplets"), Droplets, 15);
	TestEqual(TEXT("...in three groups of five — the third group is the one BloodPool_Max hid"),
		Groups, 3);

	// A narrower ceiling still draws correctly — the rail is not hardcoded to three groups. Nothing
	// in the shipped data lowers `BloodPool`'s Max, but a trait effect's `Max` would, and 10 is the
	// value the former defect published (slot 13 `BloodPool_Max`'s Default), so it is the case that
	// used to be indistinguishable from correct.
	if (!MeasureRailAtCapacity(10, 4, Droplets, Groups))
	{
		return false;
	}
	TestEqual(TEXT("a capacity of 10 draws ten droplets"), Droplets, 10);
	TestEqual(TEXT("...in two groups"), Groups, 2);

	// The banked count still follows `BloodPool`, not the capacity: `DecBloodPool` -> `DecBase`
	// `0x10200ea0` floors at the stat's Min 0 and the lit droplets are that live value.
	UElysiumHUDModel* Model = NewObject<UElysiumHUDModel>();
	FElysiumViewState View;
	View.bPlayerSurface = true;
	View.Vitals.bValid = true;
	View.Vitals.BloodPool = 12;
	View.Vitals.MaxBloodPool = 15;
	Model->Apply(View);
	TestEqual(TEXT("the banked count is the live BloodPool, not the capacity"), Model->BloodPool, 12);
	TestEqual(TEXT("...and the capacity is the stat definition's Max"), Model->BloodCapacity, 15);

	return true;
}


// The victim blood meter's value and lifetime contract — `client.dll` `CFeedBar`.
//
// THE DEFECT THIS COVERS. The publisher used to own the panel with gameplay ownership: visible
// while `IsFeedPaired()`, then held while the released victim stayed the focused usable. Every
// ordinary feed ends with the victim at zero — `FElysiumCombatCharacter::ShouldReleaseFeed`
// selects the release family exactly at `BloodPoolValue() < 1` — and the pair is still held
// through Release and ReleaseTail, so the last thing that rule published, for the whole tail of
// every feed and then for as long as the drained body stayed in focus, was `bVisible` with
// `BloodPool` zero: a bar that appears and is empty.
//
// Retail cannot produce that. `CFeedBar::vfunc114` `0x100503d0` takes the value the `FeedBar`
// usermsg carries and hides the panel outright below 1 and at or above 15; only 1..14 draws. Its
// persistence after the fangs come off is `vfunc98` `0x10050560` comparing curtime against a
// deadline `vfunc114` refreshed — three seconds (`_DAT_10227ee0`) — and there is no focus test in
// either function.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHUDFeedBarTest,
	"Elysium.Substrate.UI.HUDFeedBar",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumHUDFeedBarTest::RunTest(const FString& Parameters)
{
	const FElysiumEntityHandle Victim(7, 1);
	const FElysiumEntityHandle Other(9, 1);

	// A blueblood's authored pool is 9 and it stands up full, so the engaged bar is full. Nine is
	// inside `vfunc114`'s 1..14 window, which is the whole reason it draws at all.
	ElysiumFeedBar::FUpdate Engage;
	Engage.bHasSource = true;
	Engage.Source = Victim;
	Engage.BloodPool = 9;
	Engage.AuthoredMax = 9;
	Engage.bPaired = true;
	FElysiumFeedView View = ElysiumFeedBar::Update(FElysiumFeedView(), Engage, 100.0);
	TestTrue(TEXT("the engaged victim raises the meter"), View.bVisible);
	TestEqual(TEXT("...at the authored denominator"), View.MaxBloodPool, 9);
	TestEqual(TEXT("...drawn full"), View.Percent, 1.0f);
	TestTrue(TEXT("...with the three-second hold armed"),
		FMath::IsNearlyEqual(View.HideDeadline, 103.0, UE_DOUBLE_KINDA_SMALL_NUMBER));

	// The projection the widget actually draws.
	UElysiumHUDModel* Model = NewObject<UElysiumHUDModel>();
	FElysiumViewState Published;
	Published.bPlayerSurface = true;
	Published.Feed = View;
	Model->Apply(Published);
	TestTrue(TEXT("a full meter projects onto the HUD model"), Model->bFeedVictimVisible);
	TestEqual(TEXT("...as a full bar, not as two counters the widget divides"),
		Model->FeedVictimPercent, 1.0f);
	TestEqual(TEXT("...beside the numbers the log reports"), Model->FeedVictimBlood, 9);
	TestEqual(TEXT("...and its denominator"), Model->FeedVictimBloodCapacity, 9);

	// THE ROOT CAUSE, as one assertion. The pair is still held — this is the Release/ReleaseTail
	// frame of every completed feed — and the victim has nothing left. Retail's value gate hides
	// the panel here; the old ownership rule published exactly this state with `bVisible` true.
	ElysiumFeedBar::FUpdate Drained = Engage;
	Drained.BloodPool = 0;
	View = ElysiumFeedBar::Update(View, Drained, 106.0);
	TestFalse(TEXT("a drained victim takes the meter down even though the pair still holds"),
		View.bVisible);
	TestEqual(TEXT("...and it never draws an empty bar"), View.Percent, 0.0f);
	Published.Feed = View;
	Model->Apply(Published);
	TestFalse(TEXT("the HUD model stops publishing the meter with it"),
		Model->bFeedVictimVisible);

	// The upper arm, `CMP EDI,0xf / JGE 0x100504d4`. Retail's own quirk: a critter standing at the
	// 15-point stat ceiling shows no meter at all, because 15 is also the client's default
	// denominator and the client refuses to draw a whole bar it cannot have been told about.
	ElysiumFeedBar::FUpdate Ceiling;
	Ceiling.bHasSource = true;
	Ceiling.Source = Other;
	Ceiling.BloodPool = 15;
	Ceiling.AuthoredMax = 15;
	Ceiling.bPaired = true;
	const FElysiumFeedView AtCeiling =
		ElysiumFeedBar::Update(FElysiumFeedView(), Ceiling, 200.0);
	TestFalse(TEXT("a victim at the stat ceiling shows no meter"), AtCeiling.bVisible);
	Ceiling.BloodPool = 14;
	TestTrue(TEXT("...and one point below it does"),
		ElysiumFeedBar::Update(FElysiumFeedView(), Ceiling, 200.0).bVisible);

	// The interrupted feed: the player let go with blood left. The panel's remaining lifetime is
	// the three-second hold `vfunc114` armed at the last value change, and NOTHING else — no focus
	// test exists in either recovered function, so a player who walks away and one who keeps
	// staring at the body both lose the meter at the same instant.
	ElysiumFeedBar::FUpdate Partial;
	Partial.bHasSource = true;
	Partial.Source = Victim;
	Partial.BloodPool = 3;
	Partial.AuthoredMax = 6;
	Partial.bPaired = true;
	FElysiumFeedView Held = ElysiumFeedBar::Update(FElysiumFeedView(), Partial, 300.0);
	TestTrue(TEXT("the interrupted feed leaves a partially drained meter up"), Held.bVisible);
	TestEqual(TEXT("...drawn at its true fraction"), Held.Percent, 0.5f);

	// Teardown: no feed target, no continuation grapple, so the server sends nothing at all.
	ElysiumFeedBar::FUpdate Silent;
	Held = ElysiumFeedBar::Update(Held, Silent, 302.9);
	TestTrue(TEXT("silence does not take the meter down inside the hold"), Held.bVisible);
	TestEqual(TEXT("...and it keeps the value the last message left"), Held.BloodPool, 3);
	Held = ElysiumFeedBar::Update(Held, Silent, 303.0);
	TestFalse(TEXT("the hold expires three seconds after the last change"), Held.bVisible);

	// `vfunc98` only ever hides. A value that has not changed sends no message, so a panel the
	// hold took down cannot come back just because the victim is observable again.
	Held = ElysiumFeedBar::Update(Held, Partial, 310.0);
	TestFalse(TEXT("an unchanged value cannot re-raise the expired meter"), Held.bVisible);
	Partial.BloodPool = 2;
	Held = ElysiumFeedBar::Update(Held, Partial, 311.0);
	TestTrue(TEXT("...a changed one does"), Held.bVisible);

	// The pre-pulse anticipation (`vfunc98` `0x100507a5`): while the feed pulses, one blood point's
	// worth of bar slides off across each interval, so the meter drains continuously instead of
	// stepping once per pulse.
	ElysiumFeedBar::FUpdate Pulsing;
	Pulsing.bHasSource = true;
	Pulsing.Source = Other;
	Pulsing.BloodPool = 5;
	Pulsing.AuthoredMax = 10;
	Pulsing.bPaired = true;
	Pulsing.bPulsing = true;
	Pulsing.PulseInterval = 1.0f;
	FElysiumFeedView Sliding = ElysiumFeedBar::Update(FElysiumFeedView(), Pulsing, 400.0);
	TestEqual(TEXT("the pulse's first instant draws the whole value"), Sliding.Percent, 0.5f);
	Sliding = ElysiumFeedBar::Update(Sliding, Pulsing, 400.5);
	TestEqual(TEXT("...and half an interval later it has slid half a point down"),
		Sliding.Percent, 0.45f);
	Sliding = ElysiumFeedBar::Update(Sliding, Pulsing, 402.0);
	TestEqual(TEXT("...clamped at one whole point, never past the next pulse"),
		Sliding.Percent, 0.4f);

	return true;
}


// The equipment readout and the weapon selector, projected from a real published snapshot
// rather than from the preview fixture (the selector clause).
//
// The peek is a screen state the publisher resolved: the selector is open exactly while
// `PeekAlpha` is up, and the readout describing the hand stays up either way.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHUDEquipmentProjectionTest,
	"Elysium.Substrate.UI.HUDEquipmentProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumHUDEquipmentProjectionTest::RunTest(const FString&)
{
	UElysiumHUDModel* Model = NewObject<UElysiumHUDModel>();

	auto MakeWeapon = [](const TCHAR* Classname, const TCHAR* Label,
		EElysiumViewWeaponFamily Family, bool bMagazine, int32 Cur, int32 Reserve)
	{
		FElysiumInventoryEntryView Entry;
		Entry.Classname = Classname;
		Entry.Label = Label;
		Entry.Family = Family;
		Entry.bHasMagazine = bMagazine;
		Entry.AmmoCurrent = Cur;
		Entry.AmmoReserve = Reserve;
		return Entry;
	};

	FElysiumViewState View;
	View.bPlayerSurface = true;
	View.Camera.bShowHud = true;
	View.Equipment.bValid = true;
	View.Equipment.Entries = {
		MakeWeapon(TEXT("item_w_tire_iron"), TEXT("Tire Iron"), EElysiumViewWeaponFamily::Melee, false, 0, 0),
		MakeWeapon(TEXT("item_w_thirtyeight"), TEXT("Colt Police Positive Special"),
			EElysiumViewWeaponFamily::Firearm, true, 6, 24),
		MakeWeapon(TEXT("item_w_glock_17c"), TEXT("Glock 17c"), EElysiumViewWeaponFamily::Firearm, true, 0, 3),
	};
	View.Equipment.Section = EElysiumInvSection::WeaponRanged;
	View.Equipment.SelectedIndex = 1;
	View.Equipment.bEquippedValid = true;
	View.Equipment.Equipped = View.Equipment.Entries[1];

	// A published equipment view with the peek down: the readout describes the hand, the selector
	// stays closed.
	View.Equipment.PeekAlpha = 0.0f;
	Model->Apply(View);
	TestTrue(TEXT("the readout is valid from a real snapshot"), Model->Equipment.bValid);
	TestEqual(TEXT("the readout carries the record's printname"),
		Model->Equipment.Name.ToString(), FString(TEXT("Colt Police Positive Special")));
	TestEqual(TEXT("a firearm maps onto the ranged class"),
		Model->Equipment.WeaponClass, EElysiumWeaponClass::Ranged);
	TestEqual(TEXT("the loaded magazine reaches the readout"), Model->Equipment.AmmoCurrent, 6);
	TestEqual(TEXT("the reserve reaches the readout"), Model->Equipment.AmmoReserve, 24);
	TestEqual(TEXT("the classname stem resolves the exported icon"),
		Model->Equipment.Icon, ElysiumHUDArt::Inventory(TEXT("weapons_ranged/thirtyeight")));
	TestEqual(TEXT("a faded peek leaves the selector closed"),
		Model->Selector.Type, EElysiumHUDSelector::None);

	// The peek up: the selector opens on the same list, at the same index, in the same order.
	View.Equipment.PeekAlpha = 1.0f;
	Model->Apply(View);
	TestEqual(TEXT("the peek opens the weapons selector"),
		Model->Selector.Type, EElysiumHUDSelector::Weapons);
	TestTrue(TEXT("cycling shows the three-item peek"), Model->Selector.bBriefMode);
	TestEqual(TEXT("the selector carries every carried weapon"), Model->Selector.Entries.Num(), 3);
	TestEqual(TEXT("the selector agrees with the hand"), Model->Selector.SelectedIndex, 1);
	TestEqual(TEXT("the peek carries the publisher's opacity"), Model->Selector.Alpha, 1.0f);
	TestEqual(TEXT("a firearm row shows loaded and reserve"),
		Model->Selector.Entries[1].Detail.ToString(), FString(TEXT("6 / 24")));
	// A melee weapon authors no magazine, so its row is blank rather than reading `0 / 0`.
	TestTrue(TEXT("a melee row shows no ammunition at all"),
		Model->Selector.Entries[0].Detail.IsEmpty());
	// The alias table: `item_w_glock_17c`'s art is filed as `glock`, which the stem alone misses.
	TestEqual(TEXT("an aliased classname still resolves its art"),
		Model->Selector.Entries[2].Icon, ElysiumHUDArt::Inventory(TEXT("weapons_ranged/glock")));

	// A partially faded peek passes its opacity through rather than snapping.
	View.Equipment.PeekAlpha = 0.4f;
	Model->Apply(View);
	TestEqual(TEXT("a fading peek keeps its opacity"), Model->Selector.Alpha, 0.4f);

	// An invalid equipment view — no player, or no catalogue — leaves both regions collapsed. This
	// is the same "invalid is the contract" rule the disciplines region still runs under.
	View.Equipment = FElysiumEquipmentView();
	Model->Apply(View);
	TestFalse(TEXT("an invalid equipment view collapses the readout"), Model->Equipment.bValid);
	TestEqual(TEXT("an invalid equipment view closes the selector"),
		Model->Selector.Type, EElysiumHUDSelector::None);


	// --- The stealth readout ------------------------------------------------------------------
	// Situational: nothing is on screen unless the player is crouched, and each half of the cluster
	// states its own validity so an unmeasured gauge never renders as a confident zero.
	{
		FElysiumViewState Sneak;
		Sneak.bPlayerSurface = true;
		Sneak.Camera.bShowHud = true;

		Model->Apply(Sneak);
		TestFalse(TEXT("standing leaves the stealth cluster down"), Model->Stealth.bSneaking);

		// Crouched, with stealth authority not yet publishing: the cluster is up and the gauge
		// declares itself unmeasured rather than reading fully lit.
		Sneak.Stealth.bSneaking = true;
		Model->Apply(Sneak);
		TestTrue(TEXT("crouching raises the stealth cluster"), Model->Stealth.bSneaking);
		TestFalse(TEXT("concealment is unmeasured until it has a producer"),
			Model->Stealth.bConcealmentValid);
		TestFalse(TEXT("no observer is published yet"), Model->Stealth.bObserverValid);

		// PP6's committed snapshot, once it lands: the gauge and the observer project verbatim, and
		// centimetres become metres exactly once.
		Sneak.Stealth.bConcealmentValid = true;
		Sneak.Stealth.ConcealmentStep = 3;
		Sneak.Stealth.bObserverValid = true;
		Sneak.Stealth.ObserverDistanceCm = 1250.0f;
		Sneak.Stealth.Detection = EElysiumDetection::Searching;
		Model->Apply(Sneak);
		TestTrue(TEXT("a committed gauge projects"), Model->Stealth.bConcealmentValid);
		TestEqual(TEXT("the concealment step projects verbatim"), Model->Stealth.ConcealmentStep, 3);
		TestEqual(TEXT("the observer distance converts to metres"),
			Model->Stealth.ObserverDistanceMetres, 12.5f);
		TestTrue(TEXT("the detection state maps across"),
			Model->Stealth.Detection == EElysiumHUDDetection::Searching);

		// A step outside the five exported gauge frames is clamped rather than indexing past the art.
		Sneak.Stealth.ConcealmentStep = 99;
		Model->Apply(Sneak);
		TestEqual(TEXT("an out-of-range step clamps to the last gauge frame"),
			Model->Stealth.ConcealmentStep, ElysiumHUDArt::ConcealmentSteps - 1);
	}

	// --- The two standings --------------------------------------------------------------------
	// Masquerade is a five-mark countdown the sheet stores as violations counting UP, so the marks
	// still held are `MasqueradeMarks - Masquerade`; the fifth violation ends the run.
	{
		FElysiumViewState Standing;
		Standing.bPlayerSurface = true;
		Standing.Camera.bShowHud = true;
		Standing.Vitals.bValid = true;
		Standing.Vitals.Masquerade = 2;
		Standing.Vitals.Humanity = 7;
		Model->Apply(Standing);
		TestEqual(TEXT("the violation count projects as stored"), Model->Masquerade, 2);
		TestEqual(TEXT("two violations leave three marks held"),
			ElysiumHUDArt::MasqueradeMarks - Model->Masquerade, 3);
		TestEqual(TEXT("Humanity projects for the readout"), Model->Humanity, 7);
	}

	// The sneak preview carries a measured gauge, because the unmeasured state is what production
	// already draws and the fixture is the only way to see the finished readout.
	Model->Apply(FElysiumViewState(), EElysiumHUDPreview::Sneak);
	TestTrue(TEXT("the sneak preview raises the cluster"), Model->Stealth.bSneaking);
	TestTrue(TEXT("the sneak preview measures concealment"), Model->Stealth.bConcealmentValid);
	TestTrue(TEXT("the sneak preview publishes an observer"), Model->Stealth.bObserverValid);

	// The icon join's two branches, asserted directly so the table is a contract rather than a
	// convenience. `item_w_ithaca_m_37` is the one judgement call in it.
	TestEqual(TEXT("a melee stem resolves under the melee tree"),
		ElysiumHUDArt::ItemIcon(TEXT("item_w_tire_iron"), EElysiumWeaponClass::Melee),
		ElysiumHUDArt::Inventory(TEXT("weapons_melee/tire_iron")));
	TestEqual(TEXT("bare hands alias onto the fists art"),
		ElysiumHUDArt::ItemIcon(TEXT("item_w_unarmed"), EElysiumWeaponClass::Unarmed),
		ElysiumHUDArt::Inventory(TEXT("weapons_melee/fists")));
	TestEqual(TEXT("the dashed rifle art is reached by alias"),
		ElysiumHUDArt::ItemIcon(TEXT("item_w_remington_m_700"), EElysiumWeaponClass::Ranged),
		ElysiumHUDArt::Inventory(TEXT("weapons_ranged/remington_m-700")));
	// A classname the table has never seen still resolves to a readable icon rather than a hole.
	TestEqual(TEXT("an unknown stem still names a path"),
		ElysiumHUDArt::ItemIcon(TEXT("item_w_invented"), EElysiumWeaponClass::Melee),
		ElysiumHUDArt::Inventory(TEXT("weapons_melee/invented")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUIRootPushTest,
	"Elysium.Substrate.UI.CompositionRootPush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUIRootPushTest::RunTest(const FString& Parameters)
{
	UElysiumUIRoot* Root = NewObject<UElysiumUIRoot>();
	// Keep the retained root alive for the whole test. A temporary TakeWidget() reference tears the
	// Slate containers back down before AddWidget(), which can only prove UObject registration.
	const TSharedRef<SWidget> RootSlate = Root->TakeWidget();
	UOverlay* Overlay = Cast<UOverlay>(Root->GetWidgetFromName(TEXT("RootOverlay")));
	TestNotNull(TEXT("root exposes its structural overlay"), Overlay);
	if (Overlay)
	{
		TestEqual(TEXT("root has passive HUD plus five semantic layers"),
			Overlay->GetChildrenCount(), 6);
		for (int32 Index = 0; Index < Overlay->GetChildrenCount(); ++Index)
		{
			UWidget* Child = Overlay->GetChildAt(Index);
			UOverlaySlot* Slot = Child ? Cast<UOverlaySlot>(Child->Slot) : nullptr;
			TestNotNull(*FString::Printf(TEXT("root child %d has an overlay slot"), Index), Slot);
			if (Slot)
			{
				TestEqual(*FString::Printf(TEXT("root child %d fills horizontally"), Index),
					Slot->GetHorizontalAlignment(), HAlign_Fill);
				TestEqual(*FString::Printf(TEXT("root child %d fills vertically"), Index),
					Slot->GetVerticalAlignment(), VAlign_Fill);
			}
			if (UCommonActivatableWidgetContainerBase* Layer =
				Cast<UCommonActivatableWidgetContainerBase>(Child))
			{
				TestEqual(*FString::Printf(TEXT("layer %d has explicit instant transition"), Index),
					Layer->GetTransitionDuration(), 0.0f);
			}
		}
	}

	UCommonActivatableWidget* Screen = Root->PushWidget(
		EElysiumUILayer::SystemModal,
		UCommonActivatableWidget::StaticClass(),
		[](UCommonActivatableWidget&) {});

	TestNotNull(TEXT("a CommonUI screen can be created through the root layer"), Screen);
	UCommonActivatableWidgetContainerBase* SystemModalLayer = nullptr;
	if (Overlay)
	{
		for (int32 Index = 0; Index < Overlay->GetChildrenCount(); ++Index)
		{
			UWidget* Child = Overlay->GetChildAt(Index);
			if (Child && Child->GetFName() == TEXT("SystemModalStack"))
			{
				SystemModalLayer = Cast<UCommonActivatableWidgetContainerBase>(Child);
				break;
			}
		}
	}
	TestNotNull(TEXT("the system-modal layer exists"), SystemModalLayer);
	TestTrue(TEXT("the pushed screen is registered with the requested layer"),
		SystemModalLayer && SystemModalLayer->GetWidgetList().Contains(Screen));
	TestTrue(TEXT("the requested screen is the active CommonUI leaf"),
		SystemModalLayer && SystemModalLayer->GetActiveWidget() == Screen);
	TestTrue(TEXT("the active CommonUI leaf completed activation"), Screen && Screen->IsActivated());

	UCommonActivatableWidget* Over = Root->PushWidget(
		EElysiumUILayer::SystemModal,
		UCommonActivatableWidget::StaticClass(),
		[](UCommonActivatableWidget&) {});
	TestTrue(TEXT("a later same-layer modal becomes the active leaf"),
		SystemModalLayer && SystemModalLayer->GetActiveWidget() == Over);
	Root->RemoveWidget(EElysiumUILayer::SystemModal, Over);
	TestTrue(TEXT("closing the top modal restores the underlying leaf"),
		SystemModalLayer && SystemModalLayer->GetActiveWidget() == Screen);

	Root->SetNotificationSurfaceVisible(true);
	UCommonActivatableWidgetContainerBase* NotificationLayer = Overlay
		? Cast<UCommonActivatableWidgetContainerBase>(Root->GetWidgetFromName(TEXT("NotificationQueue")))
		: nullptr;
	TestNotNull(TEXT("the notification queue exists"), NotificationLayer);
	UCommonActivatableWidget* NoticeA = Root->PushWidget(
		EElysiumUILayer::Notification, UElysiumNotificationScreen::StaticClass(),
		[](UCommonActivatableWidget& Widget)
		{
			FElysiumNotification N;
			N.Subject = TEXT("First");
			CastChecked<UElysiumNotificationScreen>(&Widget)->ApplyNotification(N);
		});
	UCommonActivatableWidget* NoticeB = Root->PushWidget(
		EElysiumUILayer::Notification, UElysiumNotificationScreen::StaticClass(),
		[](UCommonActivatableWidget& Widget)
		{
			FElysiumNotification N;
			N.Subject = TEXT("Second");
			CastChecked<UElysiumNotificationScreen>(&Widget)->ApplyNotification(N);
		});
	TestTrue(TEXT("the FIFO activates its first notification only"),
		NotificationLayer && NotificationLayer->GetActiveWidget() == NoticeA);
	TestTrue(TEXT("the second notification is retained in arrival order"),
		NotificationLayer && NotificationLayer->GetWidgetList().Contains(NoticeB));
	Root->RemoveWidget(EElysiumUILayer::Notification, NoticeA);
	TestTrue(TEXT("removing the first notification advances the FIFO"),
		NotificationLayer && NotificationLayer->GetActiveWidget() == NoticeB);
	Root->DeactivateAllScreens();
	TestEqual(TEXT("root teardown clears transient notifications"),
		NotificationLayer ? NotificationLayer->GetWidgetList().Num() : -1, 0);
	(void)RootSlate;
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumActivatableRebuildNotificationTest,
	"Elysium.Substrate.UI.ActivatableRebuildNotification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumActivatableRebuildNotificationTest::RunTest(const FString& Parameters)
{
	ICommonInputModule::GetSettings().LoadData();
	UElysiumMainMenu* Menu = NewObject<UElysiumMainMenu>();
	Menu->SetMenuMode(EElysiumMenuMode::Pause);
	int32 RebuildNotifications = 0;
	const FDelegateHandle Handle = UCommonActivatableWidget::OnRebuilding.AddLambda(
		[Menu, &RebuildNotifications](UCommonActivatableWidget& Widget)
		{
			if (&Widget == Menu)
			{
				++RebuildNotifications;
			}
		});

	Menu->TakeWidget();
	UCommonActivatableWidget::OnRebuilding.Remove(Handle);

	TestEqual(TEXT("custom Slate screen announces its rebuild to the CommonUI action router"),
		RebuildNotifications, 1);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUINavigationStateTest,
	"Elysium.Substrate.UI.NavigationState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUINavigationStateTest::RunTest(const FString& Parameters)
{
	ICommonInputModule::GetSettings().LoadData();

	// Menu rows are real CommonUI targets, including unavailable rows whose captions explain why.
	UElysiumMainMenu* Menu = NewObject<UElysiumMainMenu>();
	Menu->SetMenuMode(EElysiumMenuMode::Pause);
	const TSharedRef<SWidget> MenuSlate = Menu->TakeWidget();
	TestEqual(TEXT("pause menu selects its first primary action"), Menu->GetSelectedActionId(),
		FName(TEXT("VMainMenu_BTN_CONTINUE")));
	UElysiumActionButton* Desired = Cast<UElysiumActionButton>(Menu->GetDesiredFocusTarget());
	TestNotNull(TEXT("desired focus is a semantic action rather than the screen wrapper"), Desired);
	TestTrue(TEXT("desired focus is the selected action"), Desired == Menu->GetSelectedAction());
	TestTrue(TEXT("menu action retains its visible Slate label inside the CommonButton"),
		Desired && Desired->HasSlateContent());
	TestTrue(TEXT("vertical menu wraps to its last action"),
		Menu->Navigate(EElysiumNavigationDirection::Up));
	TestEqual(TEXT("wrapped menu action is stable by id"), Menu->GetSelectedActionId(),
		FName(TEXT("VMainMenu_BTN_MAINMENU")));
	TestTrue(TEXT("vertical menu wraps back to the first action"),
		Menu->Navigate(EElysiumNavigationDirection::Down));
	TestEqual(TEXT("menu wrap returns to Continue"), Menu->GetSelectedActionId(),
		FName(TEXT("VMainMenu_BTN_CONTINUE")));

	UElysiumActionButton* Disabled = Menu->FindAction(TEXT("VMainMenu_BTN_LOADGAME"));
	TestNotNull(TEXT("disabled menu item remains an action target"), Disabled);
	if (Disabled)
	{
		TestTrue(TEXT("disabled explanatory item remains focusable"), Disabled->GetIsFocusable());
		TestFalse(TEXT("disabled explanatory item cannot execute"), Disabled->IsExecutable());
		TestTrue(TEXT("disabled explanatory item can be selected"),
			Menu->SelectAction(Disabled->GetActionId(), false));
		TestFalse(TEXT("disabled explanatory activation is a no-op"),
			Menu->ExecuteSelectedAction());
		TestFalse(TEXT("disabled item carries an explanatory caption"),
			Disabled->GetActionCaption().IsEmpty());
	}

	// Dialogue retains the durable response identity across a turn rebuild. If that response is
	// removed, it repairs to the nearest surviving visible row rather than a dead widget address.
	UElysiumDialogueScreen* Dialogue = NewObject<UElysiumDialogueScreen>();
	FElysiumDialogueView Turn;
	Turn.Choices = { ElysiumTestDialogueRow(TEXT("First")), ElysiumTestDialogueRow(TEXT("Second")),
		ElysiumTestDialogueRow(TEXT("Third")) };
	Turn.ChoiceIds = { 101, 202, 303 };
	Dialogue->ApplyDialogue(Turn);
	int32 ChoiceCount = 0;
	int32 LastChoice = INDEX_NONE;
	// S8: the pick carries the row's `.dlg` line id beside its position, so the substrate can refuse
	// a pick aimed at a band it has already replaced.
	int32 LastLineId = INDEX_NONE;
	Dialogue->OnChoice.BindLambda([&ChoiceCount, &LastChoice, &LastLineId](int32 Choice, int32 LineId)
	{
		++ChoiceCount;
		LastChoice = Choice;
		LastLineId = LineId;
	});
	const TSharedRef<SWidget> DialogueSlate = Dialogue->TakeWidget();
	TestEqual(TEXT("dialogue defaults to its first stable response"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.101")));
	UElysiumActionButton* First = Dialogue->GetSelectedAction();
	TestTrue(TEXT("dialogue moves to the next response"),
		Dialogue->Navigate(EElysiumNavigationDirection::Down));
	TestEqual(TEXT("dialogue selection names the response identity"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.202")));

	UElysiumActionButton* Second = Dialogue->GetSelectedAction();
	TestNotNull(TEXT("selected dialogue response is a CommonUI action"), Second);
	TestFalse(TEXT("the previous dialogue response loses its selected visual state"),
		First && First->IsActionSelected());
	TestTrue(TEXT("the focused dialogue response gains the selected visual state"),
		Second && Second->IsActionSelected());
	TestTrue(TEXT("dialogue action retains its visible response content inside the CommonButton"),
		Second && Second->HasSlateContent());
	if (Second)
	{
		Second->OnActionActivated().Broadcast(*Second);
		Second->OnActionActivated().Broadcast(*Second);
	}
	TestEqual(TEXT("CommonUI activation reaches the semantic choice exactly once"), ChoiceCount, 1);
	TestEqual(TEXT("semantic callback uses visible response order"), LastChoice, 1);
	TestEqual(TEXT("and carries that row's .dlg line id with it"), LastLineId, 202);

	Turn.Revision = 2;
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("same response identity survives dialogue refresh"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.202")));
	Dialogue->SelectAction(TEXT("Dialogue.Choice.303"), false);
	Turn.Revision = 3;
	Turn.Choices = { ElysiumTestDialogueRow(TEXT("First")), ElysiumTestDialogueRow(TEXT("Second")) };
	Turn.ChoiceIds = { 101, 202 };
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("contracted dialogue repairs to nearest surviving row"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Choice.202")));
	Turn.Revision = 4;
	Turn.Choices.Reset();
	Turn.ChoiceIds.Reset();
	Turn.bTerminal = true;
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("terminal dialogue exposes one Continue action"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Continue")));
	Turn.Revision = 5;
	Turn.bTerminal = false;
	Turn.bAwaitingAutomatic = true;
	Dialogue->ApplyDialogue(Turn);
	TestTrue(TEXT("a normal automatic wait exposes no semantic response action"),
		Dialogue->GetSelectedActionId().IsNone());
	Turn.Revision = 6;
	Turn.bTerminal = true; // invalid voice submission: explicit fallback
	Dialogue->ApplyDialogue(Turn);
	TestEqual(TEXT("an automatic failure fallback exposes Continue"),
		Dialogue->GetSelectedActionId(), FName(TEXT("Dialogue.Continue")));

	// Chargen uses the same stable-selection and contraction rules, with a vertical wrap.
	FElysiumWizPopup Popup;
	Popup.InternalName = TEXT("TestPopup");
	Popup.Text = TEXT("Choose");
	Popup.Actions.SetNum(3);
	Popup.Actions[0].Text = TEXT("One");
	Popup.Actions[1].Text = TEXT("Two");
	Popup.Actions[2].Text = TEXT("Three");
	TSharedPtr<FElysiumWizRun> Run = MakeShared<FElysiumWizRun>();
	Run->Popup = &Popup;
	for (const FElysiumWizAction& Action : Popup.Actions)
	{
		Run->Choices.Add(&Action);
	}
	UElysiumChargenPopup* Chargen = NewObject<UElysiumChargenPopup>();
	Chargen->SetRun(Run);
	const TSharedRef<SWidget> ChargenSlate = Chargen->TakeWidget();
	TestEqual(TEXT("chargen defaults to its first answer"), Chargen->GetSelectedActionId(),
		FName(TEXT("Chargen.TestPopup.0")));
	Chargen->Navigate(EElysiumNavigationDirection::Up);
	TestEqual(TEXT("chargen answers wrap upward"), Chargen->GetSelectedActionId(),
		FName(TEXT("Chargen.TestPopup.2")));
	Run->Choices.SetNum(2);
	Chargen->Refresh();
	TestEqual(TEXT("contracted chargen choices repair to nearest answer"),
		Chargen->GetSelectedActionId(), FName(TEXT("Chargen.TestPopup.1")));

	// Character/chargen rows use explicit region routing: vertical enters/leaves the body, while
	// horizontal selection executes the focused base option through the same semantic action.
	UElysiumCharacterScreen* Character = NewObject<UElysiumCharacterScreen>();
	FElysiumCharacterScreenMode CharacterMode;
	CharacterMode.Tabs = { EElysiumCharacterTab::Base, EElysiumCharacterTab::Sheet };
	CharacterMode.Spend = EElysiumSpendMode::Chargen;
	Character->SetMode(CharacterMode);
	TSharedPtr<FElysiumChargenState> CharacterState = MakeShared<FElysiumChargenState>();
	CharacterState->Clan = 2;
	Character->SetSpendState(CharacterState);
	Character->SetActiveTab(EElysiumCharacterTab::Base);
	int32 CharacterCancelCount = 0;
	Character->OnCancel.BindLambda([&CharacterCancelCount]() { ++CharacterCancelCount; });
	const TSharedRef<SWidget> CharacterSlate = Character->TakeWidget();
	TestEqual(TEXT("character screen defaults to its active major tab"),
		Character->GetSelectedActionId(),
		FName(*FString::Printf(TEXT("Character.Tab.%d"), int32(EElysiumCharacterTab::Base))));
	Character->Navigate(EElysiumNavigationDirection::Down);
	TestEqual(TEXT("Down enters the first base-option row"), Character->GetSelectedActionId(),
		FName(TEXT("Character.Base.Clan.0")));
	Character->Navigate(EElysiumNavigationDirection::Right);
	TestEqual(TEXT("Right cycles and selects the next base option"),
		Character->GetSelectedActionId(), FName(TEXT("Character.Base.Clan.1")));
	TestEqual(TEXT("base-option routing executes the same clan action"), CharacterState->Clan, 3);
	Character->Navigate(EElysiumNavigationDirection::Up);
	Character->Navigate(EElysiumNavigationDirection::Up);
	TestEqual(TEXT("vertical region routing wraps from tabs to the footer cancel action"),
		Character->GetSelectedActionId(), FName(TEXT("Character.Footer.Cancel")));
	TestTrue(TEXT("footer cancel is an ordinary executable semantic action"),
		Character->ExecuteSelectedAction());
	TestEqual(TEXT("footer cancel and Back share one callback"), CharacterCancelCount, 1);

	// A sign is one centered text panel and an ordinary visible Continue action. Legacy background
	// metadata is deliberately ignored; dwell/CloseOnLeftClick still controls executability.
	FElysiumSignData Sign;
	Sign.bParsed = true;
	Sign.SourceFile = TEXT("automation-sign");
	Sign.Background.bValid = true;
	Sign.Background.ImageName = TEXT("interface/pop_ups/automation_missing");
	Sign.Background.bHasWide = true;
	Sign.Background.bHasTall = true;
	Sign.Background.Wide = 2048;
	Sign.Background.Tall = 1024;
	Sign.Background.bCentre = true;
	FElysiumSignTextBlock BodyBlock;
	BodyBlock.Text = TEXT("Authored popup body");
	BodyBlock.XPos = 836;
	BodyBlock.YPos = 310;
	BodyBlock.Wide = 375;
	BodyBlock.Tall = 375;
	BodyBlock.TextColor = FLinearColor::White;
	Sign.Blocks.Add(BodyBlock);
	FElysiumSignTextBlock ContinueBlock;
	ContinueBlock.Text = TEXT("left-click to continue");
	ContinueBlock.XPos = 922;
	ContinueBlock.YPos = 706;
	ContinueBlock.Wide = 215;
	ContinueBlock.Tall = 28;
	ContinueBlock.TextColor = FLinearColor::White;
	Sign.Blocks.Add(ContinueBlock);
	UElysiumSignScreen* SignScreen = NewObject<UElysiumSignScreen>();
	SignScreen->ApplySign(Sign, 1.0f, false);
	int32 DismissCount = 0;
	SignScreen->OnDismiss.BindLambda([&DismissCount]() { ++DismissCount; });
	const TSharedRef<SWidget> SignSlate = SignScreen->TakeWidget();
	UElysiumActionButton* SignAction = SignScreen->FindAction(TEXT("Sign.Dismiss"));
	TestNotNull(TEXT("sign panel is one CommonUI action"), SignAction);
	TestTrue(TEXT("sign action retains its visible Continue presentation"),
		SignAction && SignAction->HasSlateContent());
	SignSlate->SlatePrepass(1.0f);
	TestTrue(TEXT("sign panel has a stable readable minimum presentation size"),
		SignSlate->GetDesiredSize().X > 600.0f && SignSlate->GetDesiredSize().Y > 100.0f);
	TestEqual(TEXT("sign renders one body and one replacement Continue label"),
		CountSlateWidgetsOfType(SignSlate, FName(TEXT("STextBlock"))), 2);
	TestEqual(TEXT("Continue is rendered inside the real CommonButton subtree"),
		SignAction ? CountSlateWidgetsOfType(SignAction->TakeWidget(), FName(TEXT("STextBlock"))) : -1,
		1);
	TestEqual(TEXT("sign action exposes the same semantic Continue label"),
		SignAction ? SignAction->GetActionLabel().ToString() : FString(), FString(TEXT("Continue")));
	TestTrue(TEXT("sign action is the desired focus target"),
		SignAction && SignScreen->GetDesiredFocusTarget() == SignAction);
	TestFalse(TEXT("sign cannot execute before the world authorizes dismissal"),
		SignScreen->ExecuteSelectedAction());
	TestEqual(TEXT("blocked sign input is consumed without dismissal"), DismissCount, 0);
	SignScreen->ApplySign(Sign, 1.0f, true);
	TestTrue(TEXT("authorized sign action dismisses"), SignScreen->ExecuteSelectedAction());
	TestFalse(TEXT("transition latch suppresses a repeated sign activation"),
		SignScreen->ExecuteSelectedAction());
	TestEqual(TEXT("sign dismissal reaches its semantic request exactly once"), DismissCount, 1);
	(void)MenuSlate;
	(void)DialogueSlate;
	(void)ChargenSlate;
	(void)CharacterSlate;
	(void)SignSlate;

	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalScreenTest,
	"Elysium.Substrate.Terminal.Screen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumTerminalScreenTest::RunTest(const FString&)
{
	ICommonInputModule::GetSettings().LoadData();
	FElysiumTerminalView View;
	View.Owner = FElysiumEntityHandle(7, 1);
	View.SessionSerial = 19;
	View.Revision = 3;
	View.ScreenSaverLabel = TEXT("TEST TERMINAL");
	View.Columns = 36;
	View.Rows = 24;
	View.MaxInput = 16;
	View.ScreenRows.SetNum(View.Rows);
	View.ScreenRows[0] = FString(TEXT("TEST TERMINAL")).RightPad(View.Columns);
	View.ScreenRows[2] = FString(TEXT("Available menus:")).RightPad(View.Columns);
	View.ScreenRows[3] = FString(TEXT("    Safe")).RightPad(View.Columns);
	FElysiumTerminalActionView Safe;
	Safe.Id = TEXT("dir:0");
	Safe.Label = TEXT("Safe");
	Safe.Command = TEXT("Safe");
	View.Actions.Add(Safe);
	FElysiumTerminalActionView Quit;
	Quit.Id = TEXT("quit");
	Quit.Label = TEXT("Quit");
	Quit.Command = TEXT("quit");
	View.Actions.Add(Quit);

	UElysiumTerminalScreen* Screen = NewObject<UElysiumTerminalScreen>();
	Screen->ApplyTerminal(View);
	FElysiumEntityHandle ReceivedOwner;
	uint32 ReceivedSerial = 0;
	FString ReceivedCommand;
	int32 SubmissionCount = 0;
	Screen->OnCommand.BindLambda(
		[&](const FElysiumEntityHandle& Owner, uint32 Serial, const FString& Command)
		{
			ReceivedOwner = Owner;
			ReceivedSerial = Serial;
			ReceivedCommand = Command;
			++SubmissionCount;
			return true;
		});

	const TSharedRef<SWidget> Slate = Screen->TakeWidget();
	TestFalse(TEXT("the input shell does not claim a physical projection"),
		Screen->HasProjection());
	TestEqual(TEXT("the terminal owns one real editable command line"),
		CountSlateWidgetsOfType(Slate, FName(TEXT("SEditableText"))), 1);
	TestEqual(TEXT("no terminal copy is painted into viewport Slate"),
		CountSlateWidgetsOfType(Slate, FName(TEXT("STextBlock"))), 0);
	TestEqual(TEXT("the exact authored screen slot resolves"),
		UElysiumTerminalScreen::FindScreenMaterialSlot(
			{ FName(TEXT("body")), FName(TEXT("screen")), FName(TEXT("keys")) }), 1);
	TestEqual(TEXT("a similar screensaver slot is not accepted"),
		UElysiumTerminalScreen::FindScreenMaterialSlot(
			{ FName(TEXT("body")), FName(TEXT("screensaver")) }), INDEX_NONE);
	TestNotNull(TEXT("the authoritative directory is also a semantic action"),
		Screen->FindAction(TEXT("dir:0")));
	TestNotNull(TEXT("quit is a semantic action"), Screen->FindAction(TEXT("quit")));

	Screen->SetDraftText(TEXT("1234567890abcdefghijklmnop"));
	TestEqual(TEXT("the local editor enforces the authoritative maximum"),
		Screen->GetDraftText(), FString(TEXT("1234567890abcdef")));
	TestTrue(TEXT("Enter submits the captured owner, serial and local line"), Screen->SubmitDraft());
	TestEqual(TEXT("one line produces one intent"), SubmissionCount, 1);
	TestEqual(TEXT("the terminal owner is retained"), ReceivedOwner, View.Owner);
	TestEqual(TEXT("the captured session serial is retained"), ReceivedSerial, View.SessionSerial);
	TestEqual(TEXT("the clamped line is submitted"), ReceivedCommand,
		FString(TEXT("1234567890abcdef")));
	TestTrue(TEXT("accepted submission clears only the local editor"),
		Screen->GetDraftText().IsEmpty());

	View.Revision = 4;
	View.InputMode = 1;
	View.Actions.Reset();
	FElysiumTerminalActionView Break;
	Break.Id = TEXT("hack");
	Break.Label = TEXT("Bypass password");
	Break.Command = TEXT("break");
	View.Actions.Add(Break);
	Screen->ApplyTerminal(View);
	TestTrue(TEXT("controller activation uses the same command delegate"),
		Screen->ExecuteAction(TEXT("hack")));
	TestEqual(TEXT("semantic password bypass submits literal break"),
		ReceivedCommand, FString(TEXT("break")));
	TestEqual(TEXT("the action is the second accepted intent"), SubmissionCount, 2);
	(void)Slate;
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumUIScalingAndDialogueInputTest,
	"Elysium.Substrate.UI.ScalingAndDialogueInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumUIScalingAndDialogueInputTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Full HD uses the shared 768-high virtual canvas"),
		ElysiumUI::ScaleFor(1080.0f), 1080.0f / ElysiumUI::VirtualH);
	TestEqual(TEXT("1440p uses the shared 768-high virtual canvas"),
		ElysiumUI::ScaleFor(1440.0f), 1440.0f / ElysiumUI::VirtualH);
	TestEqual(TEXT("4K uses the shared 768-high virtual canvas"),
		ElysiumUI::ScaleFor(2160.0f), 2160.0f / ElysiumUI::VirtualH);
	TestEqual(TEXT("4K HUD metrics are exactly twice Full HD metrics"),
		ElysiumUI::ScaleFor(2160.0f) / ElysiumUI::ScaleFor(1080.0f), 2.0f);

	// The response band is authored from the retail framing, then kept resolution-independent by
	// the shared virtual-canvas scale. All supported acceptance resolutions are 16:9, so both
	// normalized dimensions must remain stable across the whole range.
	for (const FVector2D Viewport : { FVector2D(1920.0, 1080.0), FVector2D(2560.0, 1440.0),
		FVector2D(3840.0, 2160.0) })
	{
		const float Scale = ElysiumUI::ScaleFor(static_cast<float>(Viewport.Y));
		// The type ramp is now handed to FElysiumUIFontLibrary in virtual pixels (the project's own
		// Inter face across the whole band, M-UI) rather than converted to typographic points for
		// FCoreStyle, so what has to hold is that the drawn size is the authored size times the one
		// canvas law -- and that the band's own order of emphasis survives every edit to the ramp.
		//
		// Retail draws the NPC line and the choices at ONE size and tells them apart by colour
		// alone (measured: cap height 19 px on both, `RetailBand*`), so the sentence may equal the
		// choices but must never fall under them. The speaker's name has no retail counterpart and
		// is an attribution above the line, not a header over it.
		TestTrue(*FString::Printf(TEXT("dialogue line never sinks under the choices at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::LineFontVirtualPixels
				>= ElysiumDialogueUI::ChoiceFontVirtualPixels);
		TestTrue(*FString::Printf(TEXT("dialogue speaker never outranks the line at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::SpeakerFontVirtualPixels
				<= ElysiumDialogueUI::LineFontVirtualPixels);
		TestTrue(*FString::Printf(TEXT("the requirement label sits under the sentence at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::LabelFontVirtualPixels
				< ElysiumDialogueUI::ChoiceFontVirtualPixels);
		TestTrue(*FString::Printf(TEXT("the skip hint is the quietest run at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::HintFontVirtualPixels
				<= ElysiumDialogueUI::LabelFontVirtualPixels);

		// The band's rhythm is measured against retail, not eyeballed. The reading size sits on
		// retail's em (17.8 vp) to within a rounding step, and the row's own widget padding must
		// stay inside retail's whole leading (~9.9 vp) -- Slate's text block already spends part
		// of that budget on the face's ascent and descent, so the widget may not spend all of it.
		// This is the assertion that keeps the wall of choices from growing back: it failed at the
		// pre-2026-09-07 padding, which cost 16 vp per row against a 9.9 vp retail budget.
		TestTrue(*FString::Printf(TEXT("the reading size tracks retail's em at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			FMath::Abs(ElysiumDialogueUI::ChoiceFontVirtualPixels
				- ElysiumDialogueUI::RetailBandEmVirtualPixels) <= 1.0f);
		TestTrue(*FString::Printf(TEXT("a choice row's padding fits retail's leading at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::ChoiceRowLeadingBudget
				< ElysiumDialogueUI::RetailBandLeadingVirtualPixels);
		TestEqual(*FString::Printf(TEXT("dialogue choice type follows the virtual canvas at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::ChoiceFontVirtualPixels * Scale,
			ElysiumDialogueUI::ChoiceFontVirtualPixels
				* static_cast<float>(Viewport.Y) / ElysiumUI::VirtualH);
		TestEqual(*FString::Printf(TEXT("dialogue width is 63.3%% at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::ResponsePanelWidth * Scale / static_cast<float>(Viewport.X),
			0.6328125f);
		TestEqual(*FString::Printf(TEXT("dialogue lower inset is 3.125%% at %.0fx%.0f"),
			Viewport.X, Viewport.Y),
			ElysiumDialogueUI::ResponsePanelBottomInset * Scale /
				static_cast<float>(Viewport.Y),
			0.03125f);
	}

	const TOptional<int32> Second = ElysiumDialogueUI::ChoiceForKey(EKeys::Two, 3, false);
	TestTrue(TEXT("dialogue number key resolves"), Second.IsSet());
	if (Second)
	{
		TestEqual(TEXT("dialogue number key uses visible choice order"), Second.GetValue(), 1);
	}
	TestFalse(TEXT("dialogue rejects a choice outside the visible range"),
		ElysiumDialogueUI::ChoiceForKey(EKeys::Four, 3, false).IsSet());
	const TOptional<int32> Continue = ElysiumDialogueUI::ChoiceForKey(EKeys::Enter, 0, true);
	TestTrue(TEXT("terminal dialogue accepts Enter"), Continue.IsSet());
	if (Continue)
	{
		TestEqual(TEXT("terminal dialogue reports advance"), Continue.GetValue(), -1);
	}
	TestFalse(TEXT("a live automatic turn rejects manual advance"),
		ElysiumDialogueUI::ChoiceForKey(EKeys::Enter, 0, false, true).IsSet());
	const TOptional<int32> AutomaticFallback =
		ElysiumDialogueUI::ChoiceForKey(EKeys::Enter, 0, true, true);
	TestTrue(TEXT("an automatic voice failure accepts explicit fallback advance"),
		AutomaticFallback.IsSet() && AutomaticFallback.GetValue() == -1);

	// M-DISABLED: the key-to-row map is positional and stays so — the refusal is the screen's.
	const TOptional<int32> ThirdOfMixed = ElysiumDialogueUI::ChoiceForKey(EKeys::Three, 3, false);
	TestTrue(TEXT("a disabled row still owns its number key"),
		ThirdOfMixed.IsSet() && ThirdOfMixed.GetValue() == 2);

	// M-SKIP: Space is the hurry verb while the voice runs, and only then.
	TestTrue(TEXT("Space skips while the NPC speaks"),
		ElysiumDialogueUI::IsSkipKey(EKeys::SpaceBar, true, true));
	TestFalse(TEXT("Space is not the skip verb once the voice is done"),
		ElysiumDialogueUI::IsSkipKey(EKeys::SpaceBar, false, false));
	TestFalse(TEXT("Space is not the skip verb when the world refuses the hurry"),
		ElysiumDialogueUI::IsSkipKey(EKeys::SpaceBar, true, false));
	TestFalse(TEXT("Enter is never the skip verb"),
		ElysiumDialogueUI::IsSkipKey(EKeys::Enter, true, true));
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
	const FDataTableRowHandle Use = GetDefault<UElysiumCommonUIInputData>()->GetUseAction();
	TestFalse(TEXT("CommonUI never installs a competing default input mode"),
		CommonInput->GetEnableDefaultInputConfig());
	TestNotNull(TEXT("native Accept action table resolves"), Click.DataTable.Get());
	TestEqual(TEXT("native Accept action row resolves"), Click.RowName, FName(TEXT("Accept")));
	TestNotNull(TEXT("native Back action table resolves"), Back.DataTable.Get());
	TestEqual(TEXT("native Back action row resolves"), Back.RowName, FName(TEXT("Back")));
	TestNotNull(TEXT("native Use action table resolves"), Use.DataTable.Get());
	TestEqual(TEXT("native Use action row resolves"), Use.RowName, FName(TEXT("Use")));
	const FElysiumCommonInputActionData* UseData =
		Use.GetRow<FElysiumCommonInputActionData>(TEXT("HUD test"));
	TestTrue(TEXT("native Use action binds keyboard E"),
		UseData && UseData->IsKeyBoundToInputActionData(EKeys::E));
	TestTrue(TEXT("native Use action binds gamepad RT"),
		UseData && UseData->IsKeyBoundToInputActionData(EKeys::Gamepad_RightTriggerAxis));

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
