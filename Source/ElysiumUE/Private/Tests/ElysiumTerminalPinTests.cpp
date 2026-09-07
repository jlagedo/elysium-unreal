// Slice B, headless: the screen cone as a pure predicate, and the held-session body — immobilize,
// the two maintenance arms (`slice-bc-decompiles.md` §1 and §5), and the `Hacking` camera handle's
// push/pop across every exit reason.
//
// Nothing here needs a world: the cone is three vectors, and everything the session does to the
// player and the camera goes through `IElysiumEmbodiment`, which the recording double answers.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInteraction.h"
#include "ElysiumPlayer.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumTerminal.h"
#include "Substrate/ElysiumTerminalCone.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumTerminalPinTests
{
static constexpr EAutomationTestFlags GPinFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The synthetic screen the cone cases measure: glass at the origin, forward along -X.
static const FVector GScreen(0.0f, 0.0f, 0.0f);
static const FVector GAxis(-100.0f, 0.0f, 0.0f);

// A terminal that has already loaded content, in a world over `Services`. The definition is
// project-authored, so no retail string is embedded.
static FElysiumPropHacking* StandTerminal(FAutomationTestBase& Test, FElysiumEntityWorld& World,
	FElysiumEntityHandle& OutPlayer)
{
	FElysiumEntity* Entity = World.FindByName(TEXT("terminal"));
	FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
	if (!Test.TestNotNull(TEXT("the terminal resolves"), Base))
	{
		return nullptr;
	}
	FElysiumPropHacking* Terminal = static_cast<FElysiumPropHacking*>(Base);
	FElysiumTerminalDefinition Definition;
	FString ParseError;
	FElysiumTerminalDefinition::ParseText(TEXT(R"KV(
TerminalDefinition
{
	"screen saver" "Synthetic pin"
	LogonScreen { "line0" "Test console" }
	SubDir { "name" "Vault" "description" "Door controls" }
}
)KV"), Definition, ParseError);
	Terminal->InstallDefinition(MoveTemp(Definition));
	Terminal->InputEnable();
	OutPlayer = World.PlayerHandle();
	return Terminal;
}

static FElysiumEntityDefs OneTerminal()
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__terminal_pin__");
	FElysiumEntityDef TerminalDef;
	TerminalDef.Classname = TEXT("prop_hacking");
	TerminalDef.TargetName = TEXT("terminal");
	TerminalDef.Origin = FVector(0.0f, 0.0f, 0.0f);
	TerminalDef.Keys.Add(TEXT("start_enabled"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("difficulty"), TEXT("1"));
	TerminalDef.Keys.Add(TEXT("skilltype"), TEXT("2"));
	Defs.Defs.Add(MoveTemp(TerminalDef));
	return Defs;
}
}

using namespace ElysiumTerminalPinTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalConeTest,
	"Elysium.Substrate.TerminalCone", GPinFlags)
bool FElysiumTerminalConeTest::RunTest(const FString&)
{
	// --- the pure predicate (`FUN_10218710` + `FUN_101d1120`) -------------------------------
	// Straight in front of the glass along its own forward: the cosine is exactly 1.
	TestTrue(TEXT("the screen's own axis is inside the cone"),
		ElysiumTerminalCone::Faces(GScreen, GAxis, FVector(-300.0f, 0.0f, 40.0f)));
	TestEqual(TEXT("and its cosine is 1"),
		ElysiumTerminalCone::FacingDot(GScreen, GAxis, FVector(-300.0f, 0.0f, 40.0f)), 1.0f);

	// The threshold is `_DAT_10457f54` = 0.7, compared strictly (`102187b7` AND 0x4100 -> equal
	// fails). An eye placed at exactly cos = 0.7 must be refused, and one a hair inside admitted.
	{
		const float Cos = ElysiumTerminalCone::FacingCosine;
		const float Sin = FMath::Sqrt(1.0f - Cos * Cos);
		const FVector OnTheEdge(-Cos * 200.0f, Sin * 200.0f, 0.0f);
		TestTrue(TEXT("exactly 0.7 is on the boundary"),
			FMath::IsNearlyEqual(ElysiumTerminalCone::FacingDot(GScreen, GAxis, OnTheEdge), Cos,
				1.e-5f));
		TestFalse(TEXT("and the boundary fails: the comparison is strict"),
			ElysiumTerminalCone::Faces(GScreen, GAxis, OnTheEdge));

		const float Inside = 0.71f;
		const FVector JustInside(-Inside * 200.0f, FMath::Sqrt(1.0f - Inside * Inside) * 200.0f, 0.0f);
		TestTrue(TEXT("0.71 passes"), ElysiumTerminalCone::Faces(GScreen, GAxis, JustInside));
	}

	TestFalse(TEXT("90 degrees off the axis fails"),
		ElysiumTerminalCone::Faces(GScreen, GAxis, FVector(0.0f, 200.0f, 0.0f)));
	TestFalse(TEXT("standing behind the screen fails"),
		ElysiumTerminalCone::Faces(GScreen, GAxis, FVector(200.0f, 0.0f, 0.0f)));
	// `FUN_101d1120`'s zero-length branch: an eye directly over the glass in XY answers 0.
	TestEqual(TEXT("an eye coincident in XY answers 0"),
		ElysiumTerminalCone::FacingDot(GScreen, GAxis, FVector(0.0f, 0.0f, 250.0f)), 0.0f);
	TestFalse(TEXT("and therefore fails the gate"),
		ElysiumTerminalCone::Faces(GScreen, GAxis, FVector(0.0f, 0.0f, 250.0f)));
	// A `screen_axis` coincident with `screen` is a degenerate forward: 0, fail.
	TestEqual(TEXT("a coincident axis answers 0"),
		ElysiumTerminalCone::FacingDot(GScreen, GScreen, FVector(-200.0f, 0.0f, 0.0f)), 0.0f);

	// The forward is normalized in 3D and its XY part is used UN-renormalized, so an axis lifted in
	// Z shortens it and *widens* the cone: a direction that just failed on a flat axis passes.
	{
		const FVector LiftedAxis(-100.0f, 0.0f, 100.0f);
		const FVector Eye(-100.0f, 100.0f, 0.0f);   // 45 degrees off, cos 0.7071 on a flat axis
		TestTrue(TEXT("a flat axis admits 45 degrees"),
			ElysiumTerminalCone::Faces(GScreen, GAxis, Eye));
		TestFalse(TEXT("a Z-lifted axis shortens the XY forward and refuses the same eye"),
			ElysiumTerminalCone::Faces(GScreen, LiftedAxis, Eye));
	}

	// --- focus, availability and the icon all read that one predicate ------------------------
	FElysiumRecordingServices Services;
	Services.StandTerminalScreen(GScreen, FVector(-1.0f, 0.0f, 0.0f), 300.0f);
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(OneTerminal());
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);

	FElysiumEntityHandle Ignored;
	FElysiumPropHacking* Terminal = StandTerminal(*this, World, Ignored);
	if (!Terminal)
	{
		return false;
	}
	TestTrue(TEXT("the terminal resolved both attachments off the double"),
		Terminal->bScreenAttachmentsResolved);

	auto Context = [&Player, &Terminal](const FVector& Eye)
	{
		FElysiumUseContext Out;
		Out.Activator = Player;
		Out.Owner = Terminal->Handle;
		Out.EyeOrigin = Eye;
		Out.bHasEyeOrigin = true;
		return Out;
	};

	// The candidate loop, the focus query and the use icon all run `CanPlayerFocus`, so one boundary
	// walk covers all three: the icon and the prompt appear exactly where a session can start.
	Services.UseQuery = FElysiumUseQueryResult();
	FElysiumUseCandidate Candidate;
	Candidate.Owner = Terminal->Handle;
	Candidate.Selection = EElysiumUseSelection::Exact;
	Services.UseQuery.Candidates.Add(Candidate);

	Services.PlayerLocation = FVector(-300.0f, 0.0f, 0.0f);
	World.UpdatePlayerInteraction();
	TestTrue(TEXT("inside the cone the terminal takes focus"),
		World.GetInteractionView().bActionable);
	TestTrue(TEXT("and the same context passes the gate"),
		Terminal->CanPlayerFocus(Context(Services.PlayerLocation)));

	Services.PlayerLocation = FVector(0.0f, 300.0f, 0.0f);   // 90 degrees off
	World.UpdatePlayerInteraction();
	TestFalse(TEXT("outside the cone nothing is actionable"),
		World.GetInteractionView().bActionable);
	TestFalse(TEXT("and the gate refuses the same context"),
		Terminal->CanPlayerFocus(Context(Services.PlayerLocation)));

	// Fails closed with no eye at all: retail reaches the eye through the player's own vtable slot,
	// so no player is no gate.
	FElysiumUseContext NoEye = Context(FVector(-300.0f, 0.0f, 0.0f));
	NoEye.bHasEyeOrigin = false;
	TestFalse(TEXT("no eye fails the gate closed"), Terminal->CanPlayerFocus(NoEye));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalPinTest,
	"Elysium.Substrate.TerminalPin", GPinFlags)
bool FElysiumTerminalPinTest::RunTest(const FString&)
{
	// Every exit reason converges on the one release body, so the camera pop and the mobilize are
	// asserted per reason rather than once.
	static const EElysiumUseEndReason Reasons[] = {
		EElysiumUseEndReason::Completed, EElysiumUseEndReason::Released,
		EElysiumUseEndReason::Cancelled, EElysiumUseEndReason::TargetInvalid,
		EElysiumUseEndReason::WorldTeardown };

	for (const EElysiumUseEndReason Reason : Reasons)
	{
		FElysiumRecordingServices Services;
		Services.StandTerminalScreen(GScreen, FVector(-1.0f, 0.0f, 0.0f), 300.0f);
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
			EAutomationExpectedErrorFlags::Contains, 1);
		World.Load(OneTerminal());
		const FElysiumEntityHandle Player = World.SpawnPlayer();
		World.Activate(0.0);
		FElysiumEntityHandle Ignored;
		FElysiumPropHacking* Terminal = StandTerminal(*this, World, Ignored);
		FElysiumPlayer* PlayerEntity = World.FindPlayer();
		if (!Terminal || !TestNotNull(TEXT("the player entity resolves"), PlayerEntity))
		{
			return false;
		}

		TestTrue(TEXT("the player starts mobile"), PlayerEntity->IsMobile());
		const FElysiumUseBeginResult Opened = World.BeginPlayerUseSession(Terminal->Handle, Player);
		if (!TestEqual(TEXT("+use opens the session"), Opened.Outcome,
			EElysiumUseOutcome::SessionStarted))
		{
			return false;
		}
		// `FUN_1015ef40(player)` at entry (§3.1 step 3).
		TestFalse(TEXT("entry immobilizes the player"), PlayerEntity->IsMobile());
		// `FUN_10070470("Hacking", ...)` at entry (§3.2 step 7), exactly once.
		TestEqual(TEXT("entry pushes the Hacking shot once, with the exposure clamp asked for"),
			Services.Count(TEXT("PushCameraShotNamed special-case:Hacking exposure=clamped")), 1);
		TestTrue(TEXT("and the terminal holds its handle"), Terminal->CameraShot != 0);
		const int32 Handle = Terminal->CameraShot;

		World.EndPlayerUseSession(Terminal->Handle, Reason);
		TestTrue(TEXT("every exit reason mobilizes the player"), PlayerEntity->IsMobile());
		TestEqual(TEXT("every exit reason pops the shot exactly once"),
			Services.Count(FString::Printf(TEXT("PopCameraShot %d"), Handle)), 1);
		TestEqual(TEXT("and the handle is cleared"), Terminal->CameraShot, 0);
	}

	// --- the two maintenance arms (`FUN_10167e00`) -------------------------------------------
	// The terminal stands at the origin with a 60 cm cube of a body; the eye is on its -X axis.
	FElysiumRecordingServices Services;
	Services.StandTerminalScreen(GScreen, FVector(-1.0f, 0.0f, 0.0f), 300.0f);
	Services.bHasUseBodyBounds = true;
	Services.UseBodyBounds = FBox(FVector(-30.0f, -30.0f, 0.0f), FVector(30.0f, 30.0f, 60.0f));
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(OneTerminal());
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntityHandle Ignored;
	FElysiumPropHacking* Terminal = StandTerminal(*this, World, Ignored);
	if (!Terminal)
	{
		return false;
	}

	FElysiumUseCandidate Candidate;
	Candidate.Owner = Terminal->Handle;
	Candidate.Selection = EElysiumUseSelection::Exact;
	Services.UseQuery.Candidates.Add(Candidate);

	// FAR: the eye 300 cm out is 270 cm from the box in manhattan XY, past the 80-unit (203.2 cm)
	// slot-37 reach, so the sweep runs. The double reports contact 100 cm out and moves its player.
	Services.bPlayerSweepMoves = true;
	Services.PlayerSweepContact = FVector(-100.0f, 0.0f, 0.0f);
	TestEqual(TEXT("+use opens the session"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted);
	const int32 SweepsBefore = Services.Count(TEXT("SweepPlayerHullToward"));
	TestEqual(TEXT("entry itself sweeps nothing"), SweepsBefore, 0);

	World.UpdatePlayerInteraction();
	TestEqual(TEXT("the far arm sweeps once per tick, toward the terminal's own origin"),
		Services.Count(FString::Printf(TEXT("SweepPlayerHullToward %s"),
			*Terminal->Origin.ToCompactString())), 1);
	TestEqual(TEXT("and the pawn is left at the double's contact"), Services.PlayerLocation,
		FVector(-100.0f, 0.0f, 0.0f));
	TestEqual(TEXT("the far arm does not snap the view"),
		Services.Count(TEXT("SnapPlayerViewTo")), 0);

	// NEAR: from 100 cm out the eye is 70 cm from the box, inside the reach, so the sweep stops and
	// slot 41's view snap takes over — at the bounds CENTRE, not an attachment (correction C4).
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("the near arm stops sweeping"),
		Services.Count(TEXT("SweepPlayerHullToward")), 1);
	TestEqual(TEXT("and snaps the view at the terminal's bounds centre"),
		Services.Count(FString::Printf(TEXT("SnapPlayerViewTo %s"),
			*Services.UseBodyBounds.GetCenter().ToCompactString())), 1);

	// A second `+use` press releases: slot 44 inherits `return 1` (correction C11).
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	FElysiumTerminalView View;
	TestFalse(TEXT("a second +use press closes the terminal"), World.BuildTerminalView(View));
	const int32 SweepsAtClose = Services.Count(TEXT("SweepPlayerHullToward"));
	const int32 SnapsAtClose = Services.Count(TEXT("SnapPlayerViewTo"));
	World.UpdatePlayerInteraction();
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("nothing is swept after the session ends"),
		Services.Count(TEXT("SweepPlayerHullToward")), SweepsAtClose);
	TestEqual(TEXT("and nothing is snapped after it either"),
		Services.Count(TEXT("SnapPlayerViewTo")), SnapsAtClose);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
