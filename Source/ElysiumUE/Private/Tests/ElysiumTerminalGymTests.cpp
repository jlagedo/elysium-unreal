// The terminal gym and the terminal content contract (docs/project/plans/terminals.md, slice B).
//
// `Elysium.Content.TerminalAttachments` is the content half: every model a live `prop_hacking`
// references across the export must carry the `screen` material slot and both `$attachment`s, or the
// screen cone reads nothing and the camera has nowhere to sit.
//
// `Elysium.Content.TerminalGym*` stand the real thing: the movement gym's empty stage, the faithful
// pawn, the `sp_tutorial_1` terminal slice with its real baked bodies, and the production
// embodiment behind them.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraComponent.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumViewState.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPresentationSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumTerminal.h"
#include "Substrate/ElysiumTerminalCone.h"
#include "Tests/ElysiumTerminalGym.h"
#include "UI/ElysiumTerminalProjection.h"
#include "UObject/SoftObjectPath.h"
#include "Visual/ElysiumPreparedPropModels.h"

namespace ElysiumTerminalGymTests
{
static constexpr EAutomationTestFlags GGymFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static const TCHAR* GGymMap = TEXT("sp_tutorial_1");

static const TArray<FString>& GymRoots()
{
	// The terminal beat only: the monitor, the padlock, the safe, the popups and the door.
	static const TArray<FString> Roots = {
		TEXT("tuthack"), TEXT("tutsafelock"), TEXT("tutsafe"), TEXT("trig_popup_safe"),
		TEXT("trig_popup_note"), TEXT("item_k_tutorial_chopshop_stairs_key"),
		TEXT("tutdoordknob"), TEXT("tutdoordknob-wesp"), TEXT("tutchopdoord") };
	return Roots;
}
}

using namespace ElysiumTerminalGymTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalAttachmentsTest,
	"Elysium.Content.TerminalAttachments", GGymFlags)
bool FElysiumTerminalAttachmentsTest::RunTest(const FString&)
{
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Dirs*/ false);
	if (EntsFiles.Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported maps under $ELYSIUM_EXPORT_ROOT"));
		return true;
	}
	if (!FPackageName::DoesPackageExist(
		TEXT("/ElysiumBaked/Models/scenery/furniture/computer/SM_monitor_useable")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: /ElysiumBaked carries no baked models"));
		return true;
	}

	// Every distinct model a live `prop_hacking` names, with one map for the diagnostic.
	TMap<FString, FString> Models;
	for (const FString& EntsFile : EntsFiles)
	{
		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(EntsFile, Defs))
		{
			continue;
		}
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.Classname != TEXT("prop_hacking"))
			{
				continue;
			}
			const FString* ModelPath = Def.Keys.Find(TEXT("model"));
			if (ModelPath && !ModelPath->IsEmpty())
			{
				Models.FindOrAdd(ModelPath->ToLower(), FPaths::GetBaseFilename(EntsFile));
			}
		}
	}
	if (Models.Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no prop_hacking in the exported maps"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d distinct prop_hacking models across the export"), Models.Num()));

	// The one authored defect in the shipped corpus. `hw_warrens_5` hangs its single `prop_hacking`
	// on `Computer_New/monitor.mdl`, the DECORATIVE monitor of that set — the hackable members are
	// `monitor_hackable` and `largemonitor_hackable`, and both carry the pair. The decorative model
	// bakes no skeletal asset at all, so it authors neither attachment nor a `screen` material slot.
	//
	// It is broken in retail too, and silently: `CBaseAnimating::GetAttachment02` returns 0 and
	// **writes neither out-parameter** on a lookup miss (`slice-bc-decompiles.md` §2.1, correction
	// C9), so that terminal's screen-facing test reads uninitialized stack — its usability is
	// undefined, not merely different. The port refuses it with the named error instead, which is
	// the divergence `docs/architecture/computer-terminal-architecture.md` §3.3 asks for. Listed
	// here so the nine models that DO carry the contract stay gated and a tenth cannot creep in.
	static const TSet<FString> KnownUnusable = {
		TEXT("models/scenery/furniture/computer_new/monitor.mdl") };

	for (const TPair<FString, FString>& Entry : Models)
	{
		if (KnownUnusable.Contains(Entry.Key))
		{
			AddInfo(FString::Printf(
				TEXT("known authored defect: %s (%s) is the decorative model of its set and carries ")
				TEXT("no screen slot and no attachments; the port refuses that terminal by name"),
				*Entry.Key, *Entry.Value));
			continue;
		}
		const FString Id = ElysiumPreparedProps::ModelId(Entry.Key);
		const FString StaticPath = FElysiumContentPaths::BakedUnit(Id, TEXT("SM"));
		const FString SkeletalPath = FElysiumContentPaths::BakedUnit(Id, TEXT("SK"));
		const FString Where = FString::Printf(TEXT("%s (%s)"), *Entry.Key, *Entry.Value);

		// The `screen` MATERIAL slot is where the projection binds (slices C/D).
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(FSoftObjectPath(StaticPath).TryLoad());
		if (TestNotNull(*FString::Printf(TEXT("%s bakes a static mesh"), *Where), StaticMesh))
		{
			bool bHasScreenSlot = false;
			for (const FStaticMaterial& Material : StaticMesh->GetStaticMaterials())
			{
				bHasScreenSlot |= Material.MaterialSlotName == FName(TEXT("screen"));
			}
			TestTrue(*FString::Printf(TEXT("%s carries an exact 'screen' material slot"), *Where),
				bHasScreenSlot);
		}

		// Both `$attachment`s, which the cone and the camera both read.
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(FSoftObjectPath(SkeletalPath).TryLoad());
		if (TestNotNull(*FString::Printf(TEXT("%s bakes a skeletal mesh"), *Where), SkeletalMesh))
		{
			TestNotNull(*FString::Printf(TEXT("%s carries the 'screen' attachment"), *Where),
				SkeletalMesh->FindSocket(FName(TEXT("screen"))));
			TestNotNull(*FString::Printf(TEXT("%s carries the 'screen_axis' attachment"), *Where),
				SkeletalMesh->FindSocket(FName(TEXT("screen_axis"))));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalGymStandsTest,
	"Elysium.Content.TerminalGymStands", GGymFlags)
bool FElysiumTerminalGymStandsTest::RunTest(const FString&)
{
	if (!FElysiumTerminalGym::Available(GGymMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the sp_tutorial_1 export or /ElysiumBaked is absent"));
		return true;
	}
	FElysiumTerminalGym Gym;
	// The monitor stands at (668, -324, 376); the pawn is seated a few metres off it on the stage.
	if (!Gym.Build(*this, GGymMap, GymRoots(), FVector(300.0f, -324.0f, 0.0f), 0.0f))
	{
		return false;
	}
	AddInfo(Gym.Report());

	TestNotNull(TEXT("the movement gym's stage stands"), Gym.Stage);
	TestNotNull(TEXT("the pawn stands"), Gym.Host.Pawn);
	TestNotNull(TEXT("the map actor stands"), Gym.Host.MapActor);
	TestNotNull(TEXT("the entity world is adopted"), Gym.World());

	FElysiumPropHacking* Terminal = Gym.Terminal(TEXT("tuthack"));
	if (!TestNotNull(TEXT("tuthack resolves as a prop_hacking"), Terminal))
	{
		return false;
	}
	TestTrue(TEXT("tuthack loaded its authored hack_file"), Terminal->bStartEnabled);
	TestNotNull(TEXT("the monitor body stands"), Gym.Bodies.FindRef(TEXT("tuthack")));

	// Both attachments resolve off the real baked asset, to finite world points.
	if (TestTrue(TEXT("both screen attachments resolve"), Terminal->bScreenAttachmentsResolved))
	{
		TestTrue(TEXT("the screen point is finite"), Terminal->ScreenPointCm.ContainsNaN() == false);
		TestTrue(TEXT("the screen_axis point is finite"),
			Terminal->ScreenAxisPointCm.ContainsNaN() == false);
		TestTrue(TEXT("and they are not the same point"),
			!Terminal->ScreenPointCm.Equals(Terminal->ScreenAxisPointCm, 0.1f));
		AddInfo(FString::Printf(TEXT("screen %s, screen_axis %s"),
			*Terminal->ScreenPointCm.ToCompactString(),
			*Terminal->ScreenAxisPointCm.ToCompactString()));
	}

	// The registered anchor is what `+use` reaches the world through: put the eye on the screen's
	// own axis, look at it, and the geometric query must offer the terminal.
	const FVector Forward =
		(Terminal->ScreenAxisPointCm - Terminal->ScreenPointCm).GetSafeNormal2D();
	const FVector Stand = Terminal->ScreenPointCm + Forward * (60.0f * ElysiumMove::U);
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	const FElysiumUseQueryResult Query =
		Gym.Host.MapActor->QueryPlayerUse(FElysiumEntityHandle::Invalid());
	bool bOffered = false;
	for (const FElysiumUseCandidate& Candidate : Query.Candidates)
	{
		bOffered |= Candidate.Owner == Terminal->Handle;
	}
	TestTrue(TEXT("the monitor's use anchor answers QueryPlayerUse from in front of the glass"),
		bOffered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalGymConeTest,
	"Elysium.Content.TerminalGymCone", GGymFlags)
bool FElysiumTerminalGymConeTest::RunTest(const FString&)
{
	if (!FElysiumTerminalGym::Available(GGymMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the sp_tutorial_1 export or /ElysiumBaked is absent"));
		return true;
	}
	FElysiumTerminalGym Gym;
	if (!Gym.Build(*this, GGymMap, GymRoots(), FVector(300.0f, -324.0f, 0.0f), 0.0f))
	{
		return false;
	}
	AddInfo(Gym.Report());

	FElysiumPropHacking* Terminal = Gym.Terminal(TEXT("tuthack"));
	FElysiumEntityWorld* World = Gym.World();
	if (!TestNotNull(TEXT("tuthack resolves"), Terminal) || !TestNotNull(TEXT("the world"), World)
		|| !TestTrue(TEXT("its attachments resolved"), Terminal->bScreenAttachmentsResolved))
	{
		return false;
	}

	// The cone boundary derived from the REAL socket transforms.
	const FVector Screen = Terminal->ScreenPointCm;
	const FVector Forward = (Terminal->ScreenAxisPointCm - Screen).GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(FVector::UpVector, Forward);
	const float Cos = ElysiumTerminalCone::FacingCosine;
	const float Sin = FMath::Sqrt(1.0f - Cos * Cos);
	const float Reach = 150.0f;

	FElysiumUseContext Gate;
	Gate.Owner = Terminal->Handle;
	Gate.Activator = World->PlayerHandle();
	Gate.bHasEyeOrigin = true;
	// The 3D forward's XY part is shorter than one whenever `screen_axis` is lifted in Z, so the
	// boundary is computed against the SAME number the predicate uses rather than assumed.
	Gate.EyeOrigin = Screen + Forward * Reach;
	TestTrue(TEXT("straight in front of the real glass is inside the cone"),
		Terminal->CanPlayerFocus(Gate));
	Gate.EyeOrigin = Screen + (Forward * Cos + Side * Sin) * Reach;
	TestEqual(TEXT("the constructed edge lands on 0.7 of the flat forward"),
		FMath::IsNearlyEqual(FVector::DotProduct(
			(Gate.EyeOrigin - Screen).GetSafeNormal2D(), Forward), Cos, 1.e-3f), true);
	Gate.EyeOrigin = Screen + Side * Reach;
	TestFalse(TEXT("90 degrees off the real glass is outside it"), Terminal->CanPlayerFocus(Gate));

	// --- the session on the real bodies ------------------------------------------------------
	const FVector Stand = Screen + Forward * (60.0f * ElysiumMove::U);
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Gym.Host.Pawn);
	UElysiumCameraComponent* Camera = Body ? Body->GetCameraComponent() : nullptr;
	if (!TestNotNull(TEXT("the pawn carries a camera"), Camera))
	{
		return false;
	}
	const int32 ShotsBefore = Camera->GetShots().Num();

	const FElysiumUseBeginResult Opened =
		World->BeginPlayerUseSession(Terminal->Handle, World->PlayerHandle());
	if (!TestEqual(TEXT("+use opens the session on the real body"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	TestTrue(TEXT("the terminal holds a camera handle"), Terminal->CameraShot != 0);
	TestEqual(TEXT("and the camera stack grew by one"), Camera->GetShots().Num(), ShotsBefore + 1);
	if (const FElysiumCameraShot* Live = Camera->GetShots().Top())
	{
		// The shot's End anchor is `Attachment: screen_axis` and its Target1 `Attachment: screen`,
		// resolved through the same `GetBodyAttachmentPoint` the cone measured with — so these are
		// the identical two vectors by construction.
		TestTrue(TEXT("the live shot sits on the screen_axis attachment"),
			Live->Origin.Equals(Terminal->ScreenAxisPointCm, 0.1f));
		TestTrue(TEXT("and looks at the screen attachment"),
			Live->bUseLookAt && Live->LookAt.Equals(Terminal->ScreenPointCm, 0.1f));
		TestEqual(TEXT("at the shot file's FOV 75"), Live->FieldOfView, 75.0f);
		TestTrue(TEXT("with the HUD shown"), Live->Presentation.bShowHud);
		TestFalse(TEXT("and the viewmodel hidden"), Live->Presentation.bDrawViewmodel);
		// The retail shot is not a snap: it eases in on its own constraints, and the existing
		// scripted-shot tracker is what consumes them (correction C19).
		TestTrue(TEXT("the shot carries MoveSpeed 300 u/s"),
			FMath::IsNearlyEqual(Live->MoveSpeed, 300.0f * ElysiumCam::U, 0.01f));
		TestTrue(TEXT("MoveAccel 250 u/s^2"),
			FMath::IsNearlyEqual(Live->MoveAccel, 250.0f * ElysiumCam::U, 0.01f));
		TestTrue(TEXT("TurnAccel 180 deg/s^2"),
			FMath::IsNearlyEqual(Live->TurnAccel, 180.0f, 0.01f));
		TestTrue(TEXT("MaxTurnRate [200,200,200]"),
			Live->MaxTurnRate.Equals(FVector(200.0f, 200.0f, 200.0f), 0.01f));
		TestTrue(TEXT("DistanceTolerance 1 u"),
			FMath::IsNearlyEqual(Live->DistanceTolerance, 1.0f * ElysiumCam::U, 0.01f));
		TestTrue(TEXT("AngularTolerance [1,1,1] deg"),
			Live->AngularTolerance.Equals(FVector(1.0f, 1.0f, 1.0f), 0.01f));
	}

	// --- the pin ------------------------------------------------------------------------------
	// 60 Source units out along the screen's own axis, well past the 80-unit slot-37 reach measured
	// to the monitor's bounds, so the far arm sweeps the pawn in.
	const FVector Away = Screen + Forward * (60.0f * ElysiumMove::U + 200.0f);
	Gym.PlacePawnFeet(FVector(Away.X, Away.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	const FVector Started = Gym.Host.Pawn->GetActorLocation();
	Gym.Frame(0.0);
	const FVector AfterOne = Gym.Host.Pawn->GetActorLocation();
	TestTrue(TEXT("one frame of the held session moves the pawn toward the machine"),
		FVector::Dist2D(AfterOne, Terminal->Origin) < FVector::Dist2D(Started, Terminal->Origin));
	for (int32 Frame = 1; Frame <= 10; ++Frame)
	{
		Gym.Frame(Frame * 0.05);
	}
	TestTrue(TEXT("and ten more frames leave it where the first put it"),
		Gym.Host.Pawn->GetActorLocation().Equals(AfterOne, 1.0f));

	// --- the exit -----------------------------------------------------------------------------
	TestTrue(TEXT("quit closes the session"),
		World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, TEXT("quit")));
	TestEqual(TEXT("the camera handle is released"), Terminal->CameraShot, 0);
	TestEqual(TEXT("and the camera stack is back where it started"),
		Camera->GetShots().Num(), ShotsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalGymScreenSaverTest,
	"Elysium.Content.TerminalGymScreensaver", GGymFlags)
bool FElysiumTerminalGymScreenSaverTest::RunTest(const FString&)
{
	if (!FElysiumTerminalGym::Available(GGymMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the sp_tutorial_1 export or /ElysiumBaked is absent"));
		return true;
	}
	FElysiumTerminalGym Gym;
	if (!Gym.Build(*this, GGymMap, GymRoots(), FVector(300.0f, -324.0f, 0.0f), 0.0f))
	{
		return false;
	}
	AddInfo(Gym.Report());

	FElysiumPropHacking* Terminal = Gym.Terminal(TEXT("tuthack"));
	FElysiumEntityWorld* World = Gym.World();
	if (!TestNotNull(TEXT("tuthack resolves"), Terminal) || !TestNotNull(TEXT("the world"), World))
	{
		return false;
	}

	// --- the projection is world state, stood with the body -------------------------------------
	// `AElysiumMapActor::RegisterUseAnchor` is the only creation site, and the gym reaches it through
	// the same call the production spawn path makes. Nobody has used the machine yet.
	UElysiumPresentationSubsystem* Presentation =
		UElysiumPresentationSubsystem::Get(Gym.Host.World);
	if (!TestNotNull(TEXT("the gym world carries a presentation subsystem"), Presentation))
	{
		return false;
	}
	UElysiumTerminalProjection* Projection =
		Presentation->FindTerminalProjection(Terminal->Handle);
	if (!TestNotNull(TEXT("the real monitor body carries a projection before any session"),
		Projection))
	{
		return false;
	}
	TestTrue(TEXT("bound to the SM_monitor_useable body's exact `screen` slot"),
		Projection->IsBound());
	TestTrue(TEXT("and to the body the gym stood"),
		Projection->BoundBody() == Gym.Bodies.FindRef(TEXT("tuthack")));
	// `-nullrhi` allocates no target; the named state is what the case asserts instead of a texture.
	TestEqual(TEXT("the renderer state matches this process"), Projection->HasRenderer(),
		FApp::CanEverRender());

	// The idle publication reaches it: a terminal with a body and no user is published every frame.
	{
		TArray<FElysiumTerminalView> Idle;
		World->BuildIdleTerminalViews(Idle);
		TestTrue(TEXT("tuthack is published as an idle terminal"),
			Idle.ContainsByPredicate([Terminal](const FElysiumTerminalView& Candidate)
				{ return Candidate.Owner == Terminal->Handle; }));
	}

	// --- a session, then quit -------------------------------------------------------------------
	const FVector Screen = Terminal->ScreenPointCm;
	const FVector Forward = (Terminal->ScreenAxisPointCm - Screen).GetSafeNormal2D();
	const FVector Stand = Screen + Forward * (60.0f * ElysiumMove::U);
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	if (!TestEqual(TEXT("+use opens the session on the real body"),
		World->BeginPlayerUseSession(Terminal->Handle, World->PlayerHandle()).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	TestEqual(TEXT("entry cancels the screensaver think"), Terminal->NextThink,
		ELYSIUM_NEVER_THINK);
	TestTrue(TEXT("quit closes the session"),
		World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, TEXT("quit")));

	// --- and the glass keeps moving on the clock afterwards -------------------------------------
	TestTrue(TEXT("quit re-armed the screensaver at ss_start"),
		FMath::IsNearlyEqual(Terminal->NextThink,
			static_cast<float>(Gym.Now()) + Terminal->ScreenSaverStart, 0.01f));
	const uint32 AfterQuit = Terminal->ViewRevision;
	Gym.Advance(Gym.Now() + Terminal->ScreenSaverStart + 0.1);
	const uint32 AfterFirstTick = Terminal->ViewRevision;
	TestEqual(TEXT("the first post-quit screensaver tick lands at ss_start"),
		AfterFirstTick, AfterQuit + 1);
	Gym.Advance(Gym.Now() + FElysiumPropHacking::ScreenSaverDelayFloor + 0.1);
	TestEqual(TEXT("and it keeps bumping the revision every ss_delay after that"),
		Terminal->ViewRevision, AfterFirstTick + 1);
	{
		TArray<FElysiumTerminalView> Idle;
		World->BuildIdleTerminalViews(Idle);
		const FElysiumTerminalView* Tuthack = Idle.FindByPredicate(
			[Terminal](const FElysiumTerminalView& Candidate)
			{ return Candidate.Owner == Terminal->Handle; });
		if (TestNotNull(TEXT("the idle view carries the post-quit screensaver"), Tuthack))
		{
			TestEqual(TEXT("at the authority's current revision"), Tuthack->Revision,
				Terminal->ViewRevision);
			TestTrue(TEXT("which the projection has not drawn yet under -nullrhi"),
				Projection->NeedsRedraw(*Tuthack));
		}
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
