// The world-lifetime terminal projection (`docs/project/plans/terminals.md`, slice C).
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
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
