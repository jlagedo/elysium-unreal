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
#include "ElysiumUseIcons.h"
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
	// The one box every held-use question is measured against: the registered use anchor, which is
	// the `ELYSIUM_USE_CHANNEL` proxy standing in for retail's `ent+0x274`/`ent+0x284`
	// (`slice-bc-decompiles.md` §5.1). The reach clamp, the `WorldSpaceCenter()` snap target and
	// the sweep stop all read it, so "the pawn came to rest against the machine" is asserted
	// against THAT box and not against "closer than before".
	FBox AnchorBox(ForceInit);
	if (!TestTrue(TEXT("the monitor's use anchor reports world bounds"),
		Gym.Host.MapActor->GetUseBodyWorldBounds(Terminal->Handle, AnchorBox)))
	{
		return false;
	}
	// The gap between two boxes in XY: zero when they touch or overlap. The sweep is XY-only, so
	// the Z overlap is not part of the question.
	auto PlanarGapTo = [&AnchorBox](const FBox& Other)
	{
		const double GapX = FMath::Max3(0.0, AnchorBox.Min.X - Other.Max.X, Other.Min.X - AnchorBox.Max.X);
		const double GapY = FMath::Max3(0.0, AnchorBox.Min.Y - Other.Max.Y, Other.Min.Y - AnchorBox.Max.Y);
		return FMath::Sqrt(GapX * GapX + GapY * GapY);
	};
	auto PawnBox = [&Gym]()
	{
		const UPrimitiveComponent* Hull =
			Cast<UPrimitiveComponent>(Gym.Host.Pawn->GetRootComponent());
		return Hull ? Hull->Bounds.GetBox() : FBox(ForceInit);
	};
	// Names what a sweep on each channel would stop on. A pin that halts on the stage or on a
	// neighbour body is indistinguishable from one that halts on the machine unless the case says
	// which, and on this map they are different answers.
	auto ProbePin = [&](const TCHAR* Label)
	{
		UPrimitiveComponent* Hull = Cast<UPrimitiveComponent>(Gym.Host.Pawn->GetRootComponent());
		const FVector SweepStart = Hull->GetComponentLocation();
		const FVector SweepEnd(Terminal->Origin.X, Terminal->Origin.Y, SweepStart.Z);
		FCollisionQueryParams Params(FName(TEXT("ElysiumGymPinProbe")), false);
		Params.AddIgnoredActor(Gym.Host.Pawn);
		for (const ECollisionChannel Channel :
			{ Hull->GetCollisionObjectType(), ELYSIUM_USE_CHANNEL })
		{
			FHitResult Hit;
			const bool bHit = Gym.Host.World->SweepSingleByChannel(Hit, SweepStart, SweepEnd,
				Hull->GetComponentQuat(), Channel, Hull->GetCollisionShape(), Params);
			AddInfo(FString::Printf(TEXT("%s: channel %d stops on %s"), Label,
				static_cast<int32>(Channel), bHit
					? *FString::Printf(TEXT("%s at %s"), *GetNameSafe(Hit.GetComponent()),
						*Hit.Location.ToCompactString())
					: TEXT("nothing")));
		}
	};

	// 60 Source units out along the screen's own axis, well past the 80-unit slot-37 reach measured
	// to the monitor's bounds, so the far arm sweeps the pawn in.
	const FVector Away = Screen + Forward * (60.0f * ElysiumMove::U + 200.0f);
	auto StandAway = [&]()
	{
		Gym.PlacePawnFeet(FVector(Away.X, Away.Y, Terminal->Origin.Z - 60.0f),
			(-Forward).Rotation().Yaw);
	};
	StandAway();
	const FVector Started = Gym.Host.Pawn->GetActorLocation();
	TestTrue(TEXT("the pawn starts clear of the machine's box"), PlanarGapTo(PawnBox()) > 50.0);
	AddInfo(FString::Printf(TEXT("anchor box %s .. %s; pawn box %s .. %s; start gap %.2f cm"),
		*AnchorBox.Min.ToCompactString(), *AnchorBox.Max.ToCompactString(),
		*PawnBox().Min.ToCompactString(), *PawnBox().Max.ToCompactString(),
		PlanarGapTo(PawnBox())));
	ProbePin(TEXT("as authored"));

	// (1) WORLD SOLIDITY. On this map `tutsafe_brush` — the safe's own `func_brush` — stands
	// between the desk and a player three and a half metres out, so the sweep stops on the safe
	// and NOT on the machine. That is retail: `0x10218320` traces `MASK_PLAYERSOLID` and writes
	// `endpos` back with no fraction and no start-solid test (§1.4), so the pin never walks the
	// player through world geometry to reach the terminal.
	// "Touching" to within a few centimetres, not exactly: the anchor proxy is built from the
	// source mesh's LOCAL render AABB and then rotated with it, while the pawn's own channel stops
	// on the mesh's collision hull, so the two surfaces differ by about 3 cm on this model. The
	// `ELYSIUM_USE_CHANNEL` arm reaches the proxy exactly (gap 0).
	constexpr double ContactToleranceCm = 5.0;
	Gym.Frame(0.0);
	const FVector AfterOne = Gym.Host.Pawn->GetActorLocation();
	TestTrue(TEXT("one frame of the held session moves the pawn toward the machine"),
		FVector::Dist2D(AfterOne, Terminal->Origin) < FVector::Dist2D(Started, Terminal->Origin));
	const double BlockedGap = PlanarGapTo(PawnBox());
	TestTrue(FString::Printf(
		TEXT("but stops on the intervening solid rather than reaching the machine (gap %.2f cm)"),
		BlockedGap), BlockedGap > ContactToleranceCm);
	for (int32 Frame = 1; Frame <= 10; ++Frame)
	{
		Gym.Frame(Frame * 0.05);
	}
	TestTrue(TEXT("and ten more frames leave it where the first put it"),
		Gym.Host.Pawn->GetActorLocation().Equals(AfterOne, 1.0f));

	// (2) THE MACHINE. Clear the corridor — every neighbour body and every other use anchor — so
	// the only thing on the segment is the monitor, and the contact assertion measures the machine.
	auto SetNeighboursSolid = [&](bool bSolid)
	{
		for (const TPair<FString, UStaticMeshComponent*>& Body : Gym.Bodies)
		{
			if (Body.Key != TEXT("tuthack") && Body.Value)
			{
				Body.Value->SetCollisionEnabled(bSolid
					? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			}
		}
		for (const TUniquePtr<FElysiumEntity>& Entity : World->Entities())
		{
			if (!Entity || Entity->Handle == Terminal->Handle)
			{
				continue;
			}
			Gym.Host.MapActor->SetUseAnchorEnabled(Entity->Handle, bSolid);
			if (UPrimitiveComponent* Brush = Entity->GetAttachBody())
			{
				Brush->SetCollisionEnabled(bSolid
					? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
			}
		}
	};
	SetNeighboursSolid(false);
	StandAway();
	ProbePin(TEXT("corridor cleared"));
	Gym.Frame(0.55);
	// The sweep runs to its contact and writes `endpos` back with no fraction test, so one frame is
	// the whole of it: the pawn's hull ends up against the box, not merely nearer to it.
	const double ContactGap = PlanarGapTo(PawnBox());
	TestTrue(FString::Printf(TEXT("with the corridor clear the pin leaves the hull touching the ")
		TEXT("anchor box (gap %.2f cm)"), ContactGap), ContactGap <= ContactToleranceCm);
	const FVector Pinned = Gym.Host.Pawn->GetActorLocation();
	for (int32 Frame = 1; Frame <= 10; ++Frame)
	{
		Gym.Frame(0.55 + Frame * 0.05);
	}
	TestTrue(TEXT("and holds it there"), Gym.Host.Pawn->GetActorLocation().Equals(Pinned, 1.0f));

	// (3) THE `ELYSIUM_USE_CHANNEL` ARM, ALONE. The pin is a named modernization: retail's single
	// `MASK_PLAYERSOLID` trace is reached here as two sweeps, the pawn's own movement channel for
	// world solidity and `ELYSIUM_USE_CHANNEL` where the use anchor stands in for the terminal's
	// `SOLID_BBOX`. Take the monitor mesh out of the pawn's channel entirely and the second arm
	// must still stop the pawn at the same box — otherwise the union is only the first trace.
	if (UStaticMeshComponent* MonitorBody = Gym.Bodies.FindRef(TEXT("tuthack")))
	{
		MonitorBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		StandAway();
		ProbePin(TEXT("use channel only"));
		Gym.Frame(1.1);
		const double UseChannelGap = PlanarGapTo(PawnBox());
		TestTrue(FString::Printf(
			TEXT("a body invisible to the pawn channel is still stopped by the use anchor ")
			TEXT("(gap %.2f cm)"), UseChannelGap), UseChannelGap <= ContactToleranceCm);
		MonitorBody->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	SetNeighboursSolid(true);

	// --- the exit -----------------------------------------------------------------------------
	TestTrue(TEXT("quit closes the session"),
		World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, TEXT("quit")));
	TestEqual(TEXT("the camera handle is released"), Terminal->CameraShot, 0);
	TestEqual(TEXT("and the camera stack is back where it started"),
		Camera->GetShots().Num(), ShotsBefore);

	// --- mid-session cone loss: the per-tick gate is what ends it ------------------------------
	// `CBasePlayer::PlayerUse` step 1 re-runs slot 32 on the held target every tick and a zero
	// answer runs `FUN_10167fd0` (§5). Walking out of the screen cone is exactly that.
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	if (TestEqual(TEXT("the session reopens for the cone-loss case"),
		World->BeginPlayerUseSession(Terminal->Handle, World->PlayerHandle()).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		const int32 ConeShot = Terminal->CameraShot;
		TestEqual(TEXT("with a live shot on the stack"), Camera->GetShots().Num(), ShotsBefore + 1);
		// 90 degrees off the glass, where the cone answers 0.
		const FVector OffAxis = Screen + Side * 200.0f;
		Gym.PlacePawnFeet(FVector(OffAxis.X, OffAxis.Y, Terminal->Origin.Z - 60.0f),
			(-Side).Rotation().Yaw);
		Gym.Frame(2.0);
		FElysiumTerminalView Lost;
		TestFalse(TEXT("one tick outside the cone releases the session"),
			World->BuildTerminalView(Lost));
		TestEqual(TEXT("and drops the camera it pushed"), Terminal->CameraShot, 0);
		TestEqual(TEXT("the stack is unwound"), Camera->GetShots().Num(), ShotsBefore);
		TestTrue(TEXT("the shot was popped, not abandoned"), ConeShot != 0);
	}

	// --- mid-session InputDisable --------------------------------------------------------------
	// `m_bEnabled` is an arm of slots 32 and 34, so disabling a machine somebody is using fails the
	// per-tick gate the same way. The collision box is NOT removed: retail's terminal keeps its
	// `SOLID_BBOX` either way, so the anchor still answers its bounds while disabled.
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	if (TestEqual(TEXT("the session reopens for the InputDisable case"),
		World->BeginPlayerUseSession(Terminal->Handle, World->PlayerHandle()).Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		Terminal->InputDisable();
		FElysiumTerminalView Disabled;
		TestFalse(TEXT("InputDisable closes a live session"), World->BuildTerminalView(Disabled));
		TestEqual(TEXT("and releases the camera"), Terminal->CameraShot, 0);
		TestEqual(TEXT("the stack is unwound"), Camera->GetShots().Num(), ShotsBefore);
		FBox StillThere(ForceInit);
		TestTrue(TEXT("a disabled terminal keeps its collision box"),
			Gym.Host.MapActor->GetUseBodyWorldBounds(Terminal->Handle, StillThere));
		FElysiumUseContext IconGate;
		IconGate.Owner = Terminal->Handle;
		IconGate.Activator = World->PlayerHandle();
		IconGate.bHasEyeOrigin = true;
		IconGate.EyeOrigin = Screen + Forward * 150.0f;
		TestFalse(TEXT("and refuses slot 32 while disabled"),
			Terminal->CanPlayerFocus(IconGate));
		TestTrue(TEXT("while slot 35 still draws its icon"), Terminal->HasUseIconCaps(IconGate));
		Terminal->InputEnable();
	}
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
