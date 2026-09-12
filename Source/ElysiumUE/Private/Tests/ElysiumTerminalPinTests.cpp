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

// The same fixture plus a `point_teleport` aimed at whoever fires it, for the forced-exit case.
static FElysiumEntityDefs TerminalAndTeleport()
{
	FElysiumEntityDefs Defs = OneTerminal();
	Defs.MapName = TEXT("__terminal_forced_exit__");
	FElysiumEntityDef Teleport;
	Teleport.Classname = TEXT("point_teleport");
	Teleport.TargetName = TEXT("yank");
	Teleport.Origin = FVector(5000.0f, 0.0f, 0.0f);
	Teleport.Keys.Add(TEXT("target"), TEXT("!activator"));
	Defs.Defs.Add(MoveTemp(Teleport));
	return Defs;
}

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

	// The candidate loop drives focus off slot 32; the reticle icon is the union of slots 32/34/35
	// (`PlayerUseIconFilter` `0x10342590`). Both are walked here.
	Services.UseQuery = FElysiumUseQueryResult();
	FElysiumUseCandidate Candidate;
	Candidate.Owner = Terminal->Handle;
	Candidate.Selection = EElysiumUseSelection::Exact;
	Services.UseQuery.Candidates.Add(Candidate);

	Services.PlayerLocation = FVector(-300.0f, 0.0f, 0.0f);
	const FVector InCone = Services.PlayerLocation;
	World.UpdatePlayerInteraction();
	World.Tick(1.0);   // past the 0.10 s prompt fade-in, so `bVisible` reads the settled prompt
	TestTrue(TEXT("inside the cone the terminal takes focus"),
		World.GetInteractionView().bActionable);
	TestTrue(TEXT("and the same context passes the gate"),
		Terminal->CanPlayerFocus(Context(InCone)));
	TestTrue(TEXT("slot 34 agrees while nobody holds it"), Terminal->CanBeUsed(Context(InCone)));
	TestTrue(TEXT("and slot 35 answers the cone alone"),
		Terminal->HasUseIconCaps(Context(InCone)));
	TestTrue(TEXT("so the use icon is drawn"), World.GetInteractionView().bVisible);

	Services.PlayerLocation = FVector(0.0f, 300.0f, 0.0f);   // 90 degrees off
	World.UpdatePlayerInteraction();
	World.Tick(2.0);   // past the 0.15 s fade-out
	TestFalse(TEXT("outside the cone nothing is actionable"),
		World.GetInteractionView().bActionable);
	TestFalse(TEXT("and the gate refuses the same context"),
		Terminal->CanPlayerFocus(Context(Services.PlayerLocation)));
	TestFalse(TEXT("slot 34 refuses it too"), Terminal->CanBeUsed(Context(Services.PlayerLocation)));
	TestFalse(TEXT("and slot 35, so the icon goes away with the cone"),
		Terminal->HasUseIconCaps(Context(Services.PlayerLocation)));
	TestFalse(TEXT("the prompt is gone"), World.GetInteractionView().bVisible);

	// --- the three bodies part company ------------------------------------------------------
	// A DISABLED terminal inside the cone: slot 32 and slot 34 both test `m_bEnabled` and refuse,
	// slot 35 is the cone alone and passes — so the icon shows over a machine that cannot be used.
	Services.PlayerLocation = InCone;
	Terminal->InputDisable();
	TestFalse(TEXT("a disabled terminal fails slot 32"), Terminal->CanPlayerFocus(Context(InCone)));
	TestFalse(TEXT("and slot 34"), Terminal->CanBeUsed(Context(InCone)));
	TestTrue(TEXT("but slot 35 still answers the cone"),
		Terminal->HasUseIconCaps(Context(InCone)));
	World.UpdatePlayerInteraction();
	World.Tick(3.0);
	TestTrue(TEXT("so a disabled terminal in the cone still draws the use icon"),
		World.GetInteractionView().bVisible);
	TestFalse(TEXT("while nothing about it is actionable"),
		World.GetInteractionView().bActionable);
	Terminal->InputEnable();

	// A terminal held by SOMEONE ELSE: slot 32 refuses that requester, slot 34 refuses everyone
	// (`this+0x8c` resolves), slot 35 still passes. Same-player re-entry is slot 32's own arm.
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("the session opens for the player"),
		World.BeginPlayerUseSession(Terminal->Handle, Player).Outcome,
		EElysiumUseOutcome::SessionStarted);
	TestTrue(TEXT("the holder still passes slot 32 — same-player re-entry"),
		Terminal->CanPlayerFocus(Context(InCone)));
	TestFalse(TEXT("but slot 34 refuses ANY live user, including the holder"),
		Terminal->CanBeUsed(Context(InCone)));
	TestTrue(TEXT("and slot 35 is untouched by the user"),
		Terminal->HasUseIconCaps(Context(InCone)));
	FElysiumUseContext Stranger = Context(InCone);
	Stranger.Activator = FElysiumEntityHandle(4242, 1);
	TestFalse(TEXT("a second activator fails slot 32"), Terminal->CanPlayerFocus(Stranger));
	TestTrue(TEXT("yet the icon union still admits it through slot 35"),
		Terminal->HasUseIconCaps(Stranger));
	World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Completed);

	// Fails closed with no eye at all: retail reaches the eye through the player's own vtable slot,
	// so no player is no gate. All three bodies read the same cone, so all three fail.
	FElysiumUseContext NoEye = Context(InCone);
	NoEye.bHasEyeOrigin = false;
	TestFalse(TEXT("no eye fails the gate closed"), Terminal->CanPlayerFocus(NoEye));
	TestFalse(TEXT("no eye fails availability closed"), Terminal->CanBeUsed(NoEye));
	TestFalse(TEXT("no eye fails the icon closed"), Terminal->HasUseIconCaps(NoEye));
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
		// M8: the shot lives in the world's one adoption slot, not on the terminal.
		TestTrue(TEXT("and the opener adopted it into the cine slot"),
			World.CineCameraShotId() != 0);
		const int32 Handle = World.CineCameraShotId();

		World.EndPlayerUseSession(Terminal->Handle, Reason);
		TestTrue(TEXT("every exit reason mobilizes the player"), PlayerEntity->IsMobile());
		TestEqual(TEXT("every exit reason pops the shot exactly once"),
			Services.Count(FString::Printf(TEXT("PopCameraShot %d"), Handle)), 1);
		TestFalse(TEXT("and the closer drops to the player view"), World.HasScriptedCamera());
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

	// A second `+use` press releases: slot 44 inherits `return 1` (correction C11). And retail runs
	// `FUN_10167e00` only when there is NO rising `IN_USE` edge (§5, steps 3 and 4e), so the frame
	// that releases runs neither arm of the maintenance.
	const int32 SweepsBeforeRelease = Services.Count(TEXT("SweepPlayerHullToward"));
	const int32 SnapsBeforeRelease = Services.Count(TEXT("SnapPlayerViewTo"));
	World.QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerInteraction();
	FElysiumTerminalView View;
	TestFalse(TEXT("a second +use press closes the terminal"), World.BuildTerminalView(View));
	const int32 SweepsAtClose = Services.Count(TEXT("SweepPlayerHullToward"));
	const int32 SnapsAtClose = Services.Count(TEXT("SnapPlayerViewTo"));
	TestEqual(TEXT("the releasing frame runs no pin"), SweepsAtClose, SweepsBeforeRelease);
	TestEqual(TEXT("and no view snap: a rising +use edge skips the maintenance arm"),
		SnapsAtClose, SnapsBeforeRelease);
	World.UpdatePlayerInteraction();
	World.UpdatePlayerInteraction();
	TestEqual(TEXT("nothing is swept after the session ends"),
		Services.Count(TEXT("SweepPlayerHullToward")), SweepsAtClose);
	TestEqual(TEXT("and nothing is snapped after it either"),
		Services.Count(TEXT("SnapPlayerViewTo")), SnapsAtClose);
	return true;
}

// A runtime `SetModel` on a terminal (slice C review, D1). The glass and the `+use` box are both
// registered off ONE component, and `RegisterUseAnchor` de-duplicates on that component — so a
// rebuild that only re-registers appends a second anchor record and leaves the projection bound to
// the body it just replaced. The sequence a rebuild must run is the one `FElysiumProp` runs:
// unregister, destroy, build, register.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalBodyRebuildTest,
	"Elysium.Substrate.TerminalBodyRebuild", GPinFlags)
bool FElysiumTerminalBodyRebuildTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.StandTerminalScreen(GScreen, FVector(-1.0f, 0.0f, 0.0f), 300.0f);
	FElysiumEntityDefs Defs = OneTerminal();
	Defs.MapName = TEXT("__terminal_rebuild__");
	Defs.Defs[0].Keys.Add(TEXT("model"), TEXT("models/synthetic/monitor.mdl"));

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntityHandle Ignored;
	FElysiumPropHacking* Terminal = StandTerminal(*this, World, Ignored);
	if (!Terminal)
	{
		return false;
	}
	const FString Registered =
		FString::Printf(TEXT("RegisterUseAnchor %s"), *Terminal->Handle.ToString());
	const FString Unregistered =
		FString::Printf(TEXT("UnregisterUseAnchor %s"), *Terminal->Handle.ToString());

	UPrimitiveComponent* FirstBody = Terminal->GetAttachBody();
	if (!TestNotNull(TEXT("the spawn pass stood a body"), FirstBody))
	{
		return false;
	}
	TestEqual(TEXT("and registered its anchor once"), Services.Count(Registered), 1);
	TestEqual(TEXT("nothing was unregistered"), Services.Count(Unregistered), 0);
	TestTrue(TEXT("the attachments resolved off it"), Terminal->bScreenAttachmentsResolved);

	Terminal->SetRuntimeModel(TEXT("models/synthetic/monitor_hackable.mdl"));
	TestEqual(TEXT("the rebuild unregisters the old anchor exactly once"),
		Services.Count(Unregistered), 1);
	TestEqual(TEXT("and registers the new one, for two in total"),
		Services.Count(Registered), 2);
	UPrimitiveComponent* SecondBody = Terminal->GetAttachBody();
	if (TestNotNull(TEXT("the rebuild stood a new body"), SecondBody))
	{
		TestTrue(TEXT("which is a different component from the one it replaced"),
			SecondBody != FirstBody);
	}
	TestTrue(TEXT("and the attachments were re-resolved off it"),
		Terminal->bScreenAttachmentsResolved);
	TestEqual(TEXT("the model field followed"), Terminal->Model,
		TEXT("models/synthetic/monitor_hackable.mdl"));
	return true;
}

// The two forced exits `slice-bc-decompiles.md` §4.3 lists that had no port wire:
// `CBasePlayer::OnTakeDamage` `0x10163020` and `CPointTeleport::InputTeleport` `0x1018dc00`.
// Both reach the ONE release body, so both must leave the terminal exactly as `quit` does.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalForcedExitTest,
	"Elysium.Substrate.TerminalForcedExit", GPinFlags)
bool FElysiumTerminalForcedExitTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.StandTerminalScreen(GScreen, FVector(-1.0f, 0.0f, 0.0f), 300.0f);
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	AddExpectedError(TEXT("terminal content failed: hack_file is empty"),
		EAutomationExpectedErrorFlags::Contains, 1);
	World.Load(TerminalAndTeleport());
	const FElysiumEntityHandle Player = World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumEntityHandle Ignored;
	FElysiumPropHacking* Terminal = StandTerminal(*this, World, Ignored);
	FElysiumPlayer* PlayerEntity = World.FindPlayer();
	if (!Terminal || !TestNotNull(TEXT("the player entity resolves"), PlayerEntity))
	{
		return false;
	}

	auto Open = [&]() -> int32
	{
		// A `point_teleport` really moves the pawn (the double's `TeleportPlayer` writes its player
		// location), so each session is staged back on the screen's own axis first.
		Services.PlayerLocation = FVector(-300.0f, 0.0f, 0.0f);
		const FElysiumUseBeginResult Result = World.BeginPlayerUseSession(Terminal->Handle, Player);
		TestEqual(TEXT("+use opens the session"), Result.Outcome,
			EElysiumUseOutcome::SessionStarted);
		return World.CineCameraShotId();
	};

	// --- damage: ANY accepted damage, not death (`10163126` precedes `10163278`) ---------------
	{
		const int32 Shot = Open();
		FElysiumTerminalView View;
		TestTrue(TEXT("the session publishes before the hit"), World.BuildTerminalView(View));
		TestFalse(TEXT("and the player is held"), PlayerEntity->IsMobile());
		PlayerEntity->TakeDamage(3.0f);
		TestFalse(TEXT("one point of damage closes the terminal"), World.BuildTerminalView(View));
		TestTrue(TEXT("the player is mobile again"), PlayerEntity->IsMobile());
		TestFalse(TEXT("the cine slot is empty again"), World.HasScriptedCamera());
		TestEqual(TEXT("and its shot was popped exactly once"),
			Services.Count(FString::Printf(TEXT("PopCameraShot %d"), Shot)), 1);
		TestEqual(TEXT("the release is reported as a cancellation, not a completion"),
			World.GetLastUseOutcome(), EElysiumUseOutcome::Cancelled);
		TestTrue(TEXT("the player survived it"), !PlayerEntity->HasReportedDeath());
	}

	// --- point_teleport: the player-only arm at the end of `InputTeleport` ---------------------
	{
		const int32 Shot = Open();
		FElysiumTerminalView View;
		TestTrue(TEXT("the reopened session publishes"), World.BuildTerminalView(View));
		World.AcceptInput(TEXT("yank"), FName(TEXT("Teleport")), FElysiumVariant::Void(),
			Player, Player);
		TestFalse(TEXT("being teleported closes the terminal"), World.BuildTerminalView(View));
		TestTrue(TEXT("the player is mobile again"), PlayerEntity->IsMobile());
		TestFalse(TEXT("the cine slot is empty again"), World.HasScriptedCamera());
		TestEqual(TEXT("and its shot was popped exactly once"),
			Services.Count(FString::Printf(TEXT("PopCameraShot %d"), Shot)), 1);
		TestTrue(TEXT("and the player really moved"),
			FMath::IsNearlyEqual(PlayerEntity->Origin.X, 5000.0f, 1.0f));
	}

	// A teleport that does NOT move the player leaves a live session alone: retail's release is
	// inside the `+0xa8` player-component arm, not on the entity path.
	{
		Open();
		FElysiumTerminalView View;
		World.AcceptInput(TEXT("yank"), FName(TEXT("Teleport")), FElysiumVariant::Void(),
			Terminal->Handle, Terminal->Handle);
		TestTrue(TEXT("teleporting something else leaves the session open"),
			World.BuildTerminalView(View));
		World.EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Completed);
	}
	return true;
}

}   // namespace ElysiumTerminalPinTests

#endif   // WITH_DEV_AUTOMATION_TESTS
