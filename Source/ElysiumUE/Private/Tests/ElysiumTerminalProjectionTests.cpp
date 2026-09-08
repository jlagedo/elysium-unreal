// The world-lifetime terminal projection.
//
// The render target, the material instance and the widget renderer belong to the MONITOR, not to a
// CommonUI session: retail's screensaver think writes into the entity's cell buffer from map load
// onward (`docs/vtmb/computer-terminals.md` §13), so the glass has to exist before the first `+use`
// and survive every exit. This case proves the ownership and the lifecycle; the real baked body and
// the `RegisterUseAnchor` wire are proven on the terminal gym (`Elysium.Content.TerminalGym*`).
//
// Every automation tier runs `-nullrhi`, so `FApp::CanEverRender()` is false here: `Bind` records
// the named "no renderer" state, owns the slot, and consumes revisions without allocating a target.
// That is what makes the lifecycle assertable at all without a rendered frame.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumMapActor.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumViewState.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/App.h"
#include "Tests/AutomationCommon.h"
#include "UI/ElysiumTerminalCells.h"
#include "UI/ElysiumTerminalProjection.h"
#include "UI/ElysiumTerminalScreen.h"

namespace ElysiumTerminalProjectionTests
{
static constexpr EAutomationTestFlags GProjectionFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A body that authors the exact `screen` material slot and nothing else. `GetMaterialSlotNames` and
// `GetMaterialIndex` read the static mesh's material list, so no geometry and no render data are
// needed to answer the one question `Bind` asks.
static UStaticMeshComponent* StandScreenBody(AActor* Owner, const TArray<FName>& Slots)
{
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Owner);
	TArray<FStaticMaterial> Materials;
	for (const FName& Slot : Slots)
	{
		Materials.Add(FStaticMaterial(nullptr, Slot, Slot));
	}
	Mesh->SetStaticMaterials(Materials);
	UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Owner);
	Body->SetStaticMesh(Mesh);
	Body->SetupAttachment(Owner->GetRootComponent());
	Body->RegisterComponent();
	return Body;
}

static FElysiumTerminalView IdleView(const FElysiumEntityHandle& Owner, uint32 Revision)
{
	FElysiumTerminalView View;
	View.Owner = Owner;
	View.SessionSerial = 0;
	View.Revision = Revision;
	View.ScreenRows.SetNum(View.Rows);
	View.ScreenRows[9] = FString(TEXT("            screen saver")).RightPad(View.Columns);
	return View;
}
}

using namespace ElysiumTerminalProjectionTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalProjectionOwnershipTest,
	"Elysium.Substrate.Terminal.ProjectionOwnership", GProjectionFlags)
bool FElysiumTerminalProjectionOwnershipTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UElysiumPresentationSubsystem* Presentation = UElysiumPresentationSubsystem::Get(World);
	if (!TestNotNull(TEXT("the world carries a presentation subsystem"), Presentation))
	{
		return false;
	}

	AActor* Host = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("the host actor stands"), Host))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Host);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	// --- the slot rule ------------------------------------------------------------------------
	TestEqual(TEXT("the exact authored screen slot resolves"),
		UElysiumTerminalProjection::FindScreenMaterialSlot(
			{ FName(TEXT("body")), FName(TEXT("screen")), FName(TEXT("keys")) }), 1);
	TestEqual(TEXT("a similar screensaver slot is not accepted"),
		UElysiumTerminalProjection::FindScreenMaterialSlot(
			{ FName(TEXT("body")), FName(TEXT("screensaver")) }), INDEX_NONE);

	// --- registration happens with the BODY, before any session -------------------------------
	const FElysiumEntityHandle Owner(7, 1);
	UStaticMeshComponent* Body =
		StandScreenBody(Host, { FName(TEXT("body")), FName(TEXT("screen")) });
	Presentation->RegisterTerminalProjection(Owner, Body);
	UElysiumTerminalProjection* Projection = Presentation->FindTerminalProjection(Owner);
	if (!TestNotNull(TEXT("a projection exists for the body before any session"), Projection))
	{
		return false;
	}
	TestTrue(TEXT("and it owns the monitor's screen slot"), Projection->IsBound());
	TestTrue(TEXT("bound to that exact component"), Projection->BoundBody() == Body);
	TestEqual(TEXT("under -nullrhi it records the named no-renderer state"),
		Projection->HasRenderer(), FApp::CanEverRender());
	TestEqual(TEXT("one projection, not one per registration call"),
		Presentation->NumTerminalProjections(), 1);

	// Re-registering the same body is idempotent — this is the model-rebuild path.
	Presentation->RegisterTerminalProjection(Owner, Body);
	TestTrue(TEXT("re-registering the same body reuses the projection"),
		Presentation->FindTerminalProjection(Owner) == Projection);
	TestEqual(TEXT("and stands no second one"), Presentation->NumTerminalProjections(), 1);

	// A body with no authored `screen` slot is not a terminal surface and holds no projection.
	const FElysiumEntityHandle Slotless(9, 1);
	Presentation->RegisterTerminalProjection(Slotless,
		StandScreenBody(Host, { FName(TEXT("body")), FName(TEXT("screensaver")) }));
	TestNull(TEXT("a body with no exact screen slot registers nothing"),
		Presentation->FindTerminalProjection(Slotless));
	TestEqual(TEXT("and leaves the register at one"), Presentation->NumTerminalProjections(), 1);

	// --- the revision gate --------------------------------------------------------------------
	TestTrue(TEXT("a fresh projection needs its first draw"),
		Projection->NeedsRedraw(IdleView(Owner, 4)));
	Projection->Draw(IdleView(Owner, 4));
	TestEqual(TEXT("the draw consumed the revision"), Projection->DrawnRevision, 4u);
	TestEqual(TEXT("and ran once"), Projection->DrawCount, 1);
	TestFalse(TEXT("an unchanged revision is skipped"),
		Projection->NeedsRedraw(IdleView(Owner, 4)));
	TestTrue(TEXT("a moved revision is not"), Projection->NeedsRedraw(IdleView(Owner, 5)));
	Projection->Draw(IdleView(Owner, 5));
	TestEqual(TEXT("the second draw ran"), Projection->DrawCount, 2);
	TestEqual(TEXT("and the gate closed again"), Projection->DrawnRevision, 5u);

	// --- it survives a session ------------------------------------------------------------------
	// A session is a serial on the SAME grid: the projection neither knows nor cares that one
	// opened, which is the whole point of moving it off the CommonUI screen.
	FElysiumTerminalView Session = IdleView(Owner, 6);
	Session.SessionSerial = 3;
	TestTrue(TEXT("the live session view is drawn through the same projection"),
		Projection->NeedsRedraw(Session));
	Projection->Draw(Session);
	TestTrue(TEXT("the projection survived the session opening"),
		Presentation->FindTerminalProjection(Owner) == Projection);
	TestTrue(TEXT("and is still bound"), Projection->IsBound());
	// ... and the screensaver revision that follows the exit lands on the same object.
	TestTrue(TEXT("the post-quit screensaver revision redraws the same glass"),
		Projection->NeedsRedraw(IdleView(Owner, 7)));

	// --- the input screen owns none of it --------------------------------------------------------
	// Read off the class, so this asserts the DECLARATION rather than one instance's state.
	const UClass* ScreenClass = UElysiumTerminalScreen::StaticClass();
	TestNull(TEXT("the terminal input screen declares no render target"),
		ScreenClass->FindPropertyByName(FName(TEXT("RenderTarget"))));
	TestNull(TEXT("nor a projection material"),
		ScreenClass->FindPropertyByName(FName(TEXT("ProjectionMaterial"))));

	// --- release with the anchors ---------------------------------------------------------------
	AElysiumMapActor* Map = World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity);
	if (!TestNotNull(TEXT("a map actor stands"), Map))
	{
		return false;
	}
	Map->ClearUseAnchors();
	TestNull(TEXT("clearing the use anchors releases the projection"),
		Presentation->FindTerminalProjection(Owner));
	TestEqual(TEXT("the register is empty"), Presentation->NumTerminalProjections(), 0);
	TestFalse(TEXT("and the released projection no longer owns a slot"), Projection->IsBound());

	// The targeted release is the body-teardown path.
	Presentation->RegisterTerminalProjection(Owner, Body);
	TestNotNull(TEXT("the projection can be stood again on the same body"),
		Presentation->FindTerminalProjection(Owner));
	Presentation->ReleaseTerminalProjection(Owner);
	TestNull(TEXT("and released by owner"), Presentation->FindTerminalProjection(Owner));

	// --- a MODEL CHANGE hands the glass to the new component -----------------------------------
	// `SetModel` destroys the old body and stands a new one, so re-registration must re-bind rather
	// than keep the stale component. Nothing else notices: the same projection object survives.
	Presentation->RegisterTerminalProjection(Owner, Body);
	UElysiumTerminalProjection* Rebound = Presentation->FindTerminalProjection(Owner);
	UStaticMeshComponent* Replacement =
		StandScreenBody(Host, { FName(TEXT("screen")), FName(TEXT("body")) });
	Presentation->RegisterTerminalProjection(Owner, Replacement);
	TestTrue(TEXT("a rebuilt body reuses the same projection object"),
		Presentation->FindTerminalProjection(Owner) == Rebound);
	TestTrue(TEXT("re-bound to the NEW component"), Rebound->BoundBody() == Replacement);
	TestEqual(TEXT("with no second projection standing"),
		Presentation->NumTerminalProjections(), 1);

	// --- and the anchor record is REPLACED, not appended ----------------------------------------
	// `AElysiumMapActor::RegisterUseAnchor` de-duplicates on the component, and a rebuilt body is a
	// new component every time — so without an unregister every `SetModel` would leave a second
	// record, a second `ELYSIUM_USE_CHANNEL` proxy and a second projection registration behind.
	AElysiumMapActor* AnchorMap = World->SpawnActorDeferred<AElysiumMapActor>(
		AElysiumMapActor::StaticClass(), FTransform::Identity);
	if (TestNotNull(TEXT("a second map actor stands for the anchor rule"), AnchorMap))
	{
		const FElysiumEntityHandle Anchored(11, 1);
		AnchorMap->RegisterUseAnchor(Body, Anchored);
		TestEqual(TEXT("one body, one anchor record"), AnchorMap->NumUseAnchors(Anchored), 1);
		// De-duplication is on the SOURCE component, not on the proxy box the actor made from it —
		// otherwise every dormancy toggle and every re-register would append.
		AnchorMap->RegisterUseAnchor(Body, Anchored);
		TestEqual(TEXT("re-registering the same body appends nothing"),
			AnchorMap->NumUseAnchors(Anchored), 1);
		AnchorMap->RegisterUseAnchor(Replacement, Anchored);
		TestEqual(TEXT("registering a second body without unregistering appends"),
			AnchorMap->NumUseAnchors(Anchored), 2);
		AnchorMap->UnregisterUseAnchor(Anchored);
		TestEqual(TEXT("unregistering drops every record for that owner"),
			AnchorMap->NumUseAnchors(Anchored), 0);
		AnchorMap->RegisterUseAnchor(Replacement, Anchored);
		TestEqual(TEXT("so the rebuild sequence leaves exactly one"),
			AnchorMap->NumUseAnchors(Anchored), 1);
		FBox Bounds(ForceInit);
		TestTrue(TEXT("and the surviving record still answers its bounds"),
			AnchorMap->GetUseBodyWorldBounds(Anchored, Bounds));
		AnchorMap->UnregisterUseAnchor(Anchored);
	}
	Presentation->ReleaseTerminalProjection(Owner);
	return true;
}

// The redraw path itself, which no case reached before: `-nullrhi` renders nothing, so
// `WasRecentlyRendered` is permanently false and the residency gate closed every draw. With the
// answer injected, the whole of `RedrawTerminalProjections` is assertable headless — including its
// order (the held session first, because it is the one a player is reading).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalProjectionRedrawTest,
	"Elysium.Substrate.Terminal.ProjectionRedraw", GProjectionFlags)
bool FElysiumTerminalProjectionRedrawTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	UElysiumPresentationSubsystem* Presentation = UElysiumPresentationSubsystem::Get(World);
	AActor* Host = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("the world carries a presentation subsystem"), Presentation)
		|| !TestNotNull(TEXT("the host actor stands"), Host))
	{
		return false;
	}
	USceneComponent* Root = NewObject<USceneComponent>(Host);
	Host->SetRootComponent(Root);
	Root->RegisterComponent();

	const FElysiumEntityHandle Held(3, 1);
	const FElysiumEntityHandle Idle(4, 1);
	Presentation->RegisterTerminalProjection(Held,
		StandScreenBody(Host, { FName(TEXT("screen")) }));
	Presentation->RegisterTerminalProjection(Idle,
		StandScreenBody(Host, { FName(TEXT("screen")) }));
	UElysiumTerminalProjection* HeldGlass = Presentation->FindTerminalProjection(Held);
	UElysiumTerminalProjection* IdleGlass = Presentation->FindTerminalProjection(Idle);
	if (!TestNotNull(TEXT("the held terminal has a projection"), HeldGlass)
		|| !TestNotNull(TEXT("and the idle one too"), IdleGlass))
	{
		return false;
	}

	FElysiumViewState State;
	State.Terminal = IdleView(Held, 2);
	State.Terminal.SessionSerial = 5;   // a LIVE session; `IsOpen` needs the serial, not the owner
	State.IdleTerminals.Add(IdleView(Idle, 9));

	// The residency gate first, on its own: a body that is not on screen consumes nothing.
	HeldGlass->ForcedResidency = false;
	IdleGlass->ForcedResidency = false;
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("an off-screen body is not drawn"), HeldGlass->DrawCount, 0);
	TestEqual(TEXT("nor an off-screen idle one"), IdleGlass->DrawCount, 0);
	TestTrue(TEXT("and the revision it skipped is still pending"),
		HeldGlass->NeedsRedraw(State.Terminal));

	// On screen: one draw each, and the session's glass is the one that goes first.
	HeldGlass->ForcedResidency = true;
	IdleGlass->ForcedResidency = true;
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("the held session is drawn"), HeldGlass->DrawCount, 1);
	TestEqual(TEXT("the idle terminal is drawn"), IdleGlass->DrawCount, 1);
	TestEqual(TEXT("each consuming its own revision"),
		HeldGlass->DrawnRevision * 100 + IdleGlass->DrawnRevision, 209u);

	// A second frame with nothing changed draws nothing.
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("an unchanged frame redraws neither"),
		HeldGlass->DrawCount + IdleGlass->DrawCount, 2);

	// One revision moves; only that glass redraws.
	State.IdleTerminals[0].Revision = 10;
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("only the terminal whose revision moved redraws"), IdleGlass->DrawCount, 2);
	TestEqual(TEXT("the other is left alone"), HeldGlass->DrawCount, 1);

	// An idle view is NOT a session: `IsOpen` is false for serial 0, so a terminal published only
	// as idle never reaches the session arm.
	State.Terminal.SessionSerial = 0;
	State.Terminal.Revision = 3;
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("a serial-0 terminal view is not a live session and is not drawn as one"),
		HeldGlass->DrawCount, 1);

	// --- the typed line is part of the picture ---------------------------------------------------
	// Retail composes the local line into the CLIENT's own cell buffer before rasterizing
	// (`FUN_100c6d50` -> `FUN_100c8060`, §8.1/TERM13), so a keystroke changes the glass without
	// moving any authority revision. The draft is therefore the second half of the redraw gate.
	State.Terminal.SessionSerial = 5;
	State.Terminal.Revision = 12;
	State.Terminal.CursorRow = 23;
	State.Terminal.CursorColumn = 1;
	State.Terminal.RightMargin = 1;
	// The client's line editor is open on that cell: entity message 3 (`FUN_100c82e0`) saved the
	// row and recorded column 1 as the edit origin. Nothing composes without it.
	State.Terminal.bLineEditActive = true;
	State.Terminal.EditEpoch = 1;
	State.Terminal.EditOriginColumn = 1;
	State.Terminal.EditOriginRow = 23;
	State.Terminal.Cells.Init(static_cast<uint16>(0x80 | ' '),
		State.Terminal.Columns * State.Terminal.Rows);
	Presentation->SetTerminalDraft(Held, FString());
	Presentation->RedrawTerminalProjections(State);
	const int32 AfterRevision = HeldGlass->DrawCount;
	TestEqual(TEXT("the moved revision drew once"), AfterRevision, 2);

	Presentation->SetTerminalDraft(Held, TEXT("cho"));
	TestTrue(TEXT("a keystroke needs a redraw even though the revision did not move"),
		HeldGlass->NeedsRedraw(State.Terminal, TEXT("cho")));
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("and it redrew"), HeldGlass->DrawCount, AfterRevision + 1);
	TestEqual(TEXT("consuming the draft"), HeldGlass->DrawnDraft, FString(TEXT("cho")));
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("the same draft on the same revision draws nothing more"),
		HeldGlass->DrawCount, AfterRevision + 1);

	// The composition itself: that draft on that grid puts the characters on the cursor row.
	const ElysiumTerminalCells::FElysiumTerminalComposed Composed =
		ElysiumTerminalCells::ComposeDraft(State.Terminal, TEXT("cho"));
	TestTrue(TEXT("the draft is composed onto the glass"), Composed.bDraftDrawn);
	TestEqual(TEXT("at the authority cursor"), Composed.RowText(23).Mid(1, 3),
		FString(TEXT("cho")));

	// A draft belonging to another terminal never reaches this one's glass.
	Presentation->SetTerminalDraft(Idle, TEXT("elsewhere"));
	Presentation->RedrawTerminalProjections(State);
	TestEqual(TEXT("the held glass drops a draft addressed to another terminal"),
		HeldGlass->DrawnDraft, FString());

	Presentation->ReleaseAllTerminalProjections();
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
