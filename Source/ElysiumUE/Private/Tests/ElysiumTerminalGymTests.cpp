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
#include "ElysiumGameClock.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumScriptHost.h"
#include "ElysiumViewState.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumUseIcons.h"
#include "ElysiumPresentationSubsystem.h"
#include "Misc/ScopeExit.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemContainer.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumLockable.h"
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
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

// The `Gym` prefix on the three helpers below is unity-blob safety: `ElysiumTerminalSliceTests.cpp`
// declares `Locked` / `BoxRule` / `BoxRow` in its own namespace and both files put a file-scope
// `using namespace` over them, so identical names go ambiguous the moment the build merges the two
// into one translation unit.
static bool GymLocked(const FElysiumEntity* Entity)
{
	return ElysiumEntityDebugTest::Row(Entity, TEXT("Locked")) == TEXT("yes");
}

static bool GymTriggerEnabled(const FElysiumEntity* Entity)
{
	const FString Enabled = ElysiumEntityDebugTest::Row(Entity, TEXT("Enabled"));
	if (!Enabled.IsEmpty())
	{
		return Enabled == TEXT("yes");
	}
	const FString Disabled = ElysiumEntityDebugTest::Row(Entity, TEXT("Disabled"));
	if (!Disabled.IsEmpty())
	{
		return Disabled == TEXT("no");
	}
	return ElysiumEntityDebugTest::Row(Entity, TEXT("Armed")) == TEXT("yes");
}

// One framed row, as `FUN_1021b330` builds it: `|`, the row's spaces, the text blitted at `Margin`,
// `|`, all `columns - 2` wide from the left margin.
static FString GymBoxRow(int32 Columns, const FString& Text, int32 Margin)
{
	FString Row = FString::ChrN(Columns - 2, TEXT(' '));
	Row[0] = TEXT('|');
	Row[Columns - 3] = TEXT('|');
	for (int32 Offset = 0; Offset < Text.Len() && Margin + Offset < Columns - 2; ++Offset)
	{
		Row[Margin + Offset] = Text[Offset];
	}
	return TEXT(" ") + Row;
}

// Where the authored screensaver label sits on the grid, or `INDEX_NONE` when it is not drawn.
static int32 FindLabelRow(const FElysiumTerminalScreenBuffer& Screen, const FString& Label)
{
	for (int32 Index = 0; Index < Screen.Rows(); ++Index)
	{
		if (Screen.RowText(Index).Contains(Label, ESearchCase::CaseSensitive))
		{
			return Index;
		}
	}
	return INDEX_NONE;
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
	// M8: the terminal keeps no handle of its own — the world's one adoption slot is where a live
	// cine shot is, and a terminal reaches it through the same `FUN_1017cef0` `SetCamera` does.
	TestTrue(TEXT("the opener adopted a camera into the cine slot"),
		World->CineCameraShotId() != 0);
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
	TestFalse(TEXT("the closer drops the cine slot to the player view"),
		World->HasScriptedCamera());
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
		const int32 ConeShot = World->CineCameraShotId();
		TestEqual(TEXT("with a live shot on the stack"), Camera->GetShots().Num(), ShotsBefore + 1);
		// 90 degrees off the glass, where the cone answers 0.
		const FVector OffAxis = Screen + Side * 200.0f;
		Gym.PlacePawnFeet(FVector(OffAxis.X, OffAxis.Y, Terminal->Origin.Z - 60.0f),
			(-Side).Rotation().Yaw);
		Gym.Frame(2.0);
		FElysiumTerminalView Lost;
		TestFalse(TEXT("one tick outside the cone releases the session"),
			World->BuildTerminalView(Lost));
		TestFalse(TEXT("and drops the camera it pushed"), World->HasScriptedCamera());
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
		TestFalse(TEXT("and releases the camera"), World->HasScriptedCamera());
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

// The whole tutorial terminal beat, end to end, on the real bodies
// (`docs/project/plans/terminals.md`, slice H).
//
// The plan's acceptance for this beat is the Play tier's `do`/`wait`/`assert`/`shot` script, which
// needs 11.10 and is not landed. This is the plan's stated interim: the same steps in the same
// order, driven natively on the terminal gym, asserting authority state at every one. What it
// CANNOT do is the picture — every tier runs `-nullrhi`, so the projection allocates no render
// target and the `shot` comparisons against `retail-shots/01-home-menu.png` remain Play-tier and
// owner-piloted. The cell grid stands in for the shot: it is the same characters in the same
// places, read off the authority instead of off the glass.
//
// The pawn walks into the cone and the session is opened through the WORLD'S USE EDGE — the same
// `QueuePlayerUseEdge` / `UpdatePlayerInteraction` pair the player controller's `+use` binding
// drives — rather than by calling `BeginPlayerUseSession`, so the focus walk, the cone gate and
// the rising-edge arm are all part of what is proven.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalGymBeatTest,
	"Elysium.Content.TerminalGymBeat", GGymFlags)
bool FElysiumTerminalGymBeatTest::RunTest(const FString&)
{
	if (!FElysiumTerminalGym::Available(GGymMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the sp_tutorial_1 export or /ElysiumBaked is absent"));
		return true;
	}

	// The keycard is an `equip0` seed on the safe and spawns only through the installed catalogue,
	// exactly as the game installs it before a map's item entities exist.
	FElysiumItemTable Items;
	FString ItemsError;
	const bool bItems = Items.Load(ItemsError);
	if (bItems)
	{
		ElysiumItems::Install(Items);
	}
	else
	{
		AddInfo(FString::Printf(
			TEXT("seam: item catalogue unavailable (%s); the keycard beats are skipped"),
			*ItemsError));
	}
	ON_SCOPE_EXIT { if (bItems) { ElysiumItems::Uninstall(Items); } };

	// The screensaver's row, column and style are draws from the Terminal stream; seeded before the
	// build so the first tick `Activate` arms is reproducible.
	ElysiumRng::SeedAll(20260907);

	FElysiumTerminalGym Gym;
	// Standing well off the machine: the beat's first assertion is what an untouched monitor shows.
	if (!Gym.Build(*this, GGymMap, GymRoots(), FVector(300.0f, -324.0f, 0.0f), 0.0f))
	{
		return false;
	}
	AddInfo(Gym.Report());
	FElysiumEntityWorld* World = Gym.World();
	FElysiumPropHacking* Terminal = Gym.Terminal(TEXT("tuthack"));
	if (!TestNotNull(TEXT("the entity world stands"), World)
		|| !TestNotNull(TEXT("tuthack resolves"), Terminal)
		|| !TestTrue(TEXT("its screen attachments resolved"), Terminal->bScreenAttachmentsResolved))
	{
		return false;
	}
	// The authored `dependency` expressions on the directory and its functions are evaluated at
	// draw time, so the host can be installed after the build.
	if (Gym.State)
	{
		Gym.State->SetScriptHost(MakeUnique<FElysiumExprScriptHost>(Gym.State));
	}

	FElysiumEntity* Padlock = World->FindByName(TEXT("tutsafelock"));
	FElysiumEntity* SafeEntity = World->FindByName(TEXT("tutsafe"));
	FElysiumEntity* KnobA = World->FindByName(TEXT("tutdoordknob"));
	FElysiumEntity* KnobB = World->FindByName(TEXT("tutdoordknob-wesp"));
	FElysiumPlayer* Player = World->FindPlayer();
	if (!TestNotNull(TEXT("tutsafelock resolves"), Padlock)
		|| !TestNotNull(TEXT("tutsafe resolves"), SafeEntity)
		|| !TestNotNull(TEXT("tutdoordknob resolves"), KnobA)
		|| !TestNotNull(TEXT("tutdoordknob-wesp resolves"), KnobB)
		|| !TestNotNull(TEXT("the player entity resolves"), Player))
	{
		return false;
	}
	TestTrue(TEXT("the padlock starts locked"), GymLocked(Padlock));
	TestTrue(TEXT("the door knobs start locked"), GymLocked(KnobA) && GymLocked(KnobB));

	const FString Label = Terminal->ScreenSaverLabel();
	const int32 Columns = Terminal->TextColumns;
	auto Row = [Terminal](int32 Index) { return Terminal->Screen.RowTextTrimmed(Index); };

	// --- BEFORE THE FIRST APPROACH: the screensaver label is on the grid --------------------------
	// `CPropHacking::vfunc113` `0x1021a270` armed the first tick inside the first second at map
	// load. One second of the gym's own clock is enough for it to have run at least once, and the
	// think it schedules is the entity's, on the game clock, with nobody standing at the machine.
	TestEqual(TEXT("the authored screensaver label"), Label,
		FString(TEXT("Brothers Downtown Garage")));
	Gym.Advance(1.2);
	const int32 IdleLabelRow = FindLabelRow(Terminal->Screen, Label);
	TestTrue(TEXT("the screensaver label is on the grid before the first approach"),
		IdleLabelRow != INDEX_NONE);
	// `0x1021a788`-`0x1021a7a1`: row `[1, rows-1]`, so it never sits on row 0.
	TestTrue(TEXT("and never on row 0"), IdleLabelRow >= 1);
	{
		TArray<FElysiumTerminalView> Idle;
		World->BuildIdleTerminalViews(Idle);
		const FElysiumTerminalView* Published = Idle.FindByPredicate(
			[Terminal](const FElysiumTerminalView& Candidate)
			{ return Candidate.Owner == Terminal->Handle; });
		if (TestNotNull(TEXT("and the idle publication carries that glass"), Published))
		{
			TestEqual(TEXT("with no session serial"), Published->SessionSerial, 0u);
			TestEqual(TEXT("and the authored label"), Published->ScreenSaverLabel, Label);
		}
	}

	// --- THE APPROACH: into the cone, then `+use` through the world's own edge ---------------------
	const FVector Screen = Terminal->ScreenPointCm;
	const FVector Forward = (Terminal->ScreenAxisPointCm - Screen).GetSafeNormal2D();
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

	// One frame with a rising `+use` edge queued. `UpdatePlayerInteraction` runs the focus walk
	// first and the queued edges after it, so this single frame is the whole gesture.
	World->QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	Gym.Frame(Gym.Now());
	if (!TestTrue(TEXT("pressing +use in the cone opens the session through the use edge"),
		Terminal->CurrentUser.IsSet()))
	{
		AddError(TEXT("the use edge did not reach the terminal; the rest of the beat cannot run"));
		return false;
	}
	FElysiumTerminalView View;
	TestTrue(TEXT("and the session publishes a view"), World->BuildTerminalView(View));
	TestFalse(TEXT("entry immobilizes the player"), Player->IsMobile());
	TestEqual(TEXT("entry cancels the screensaver think"), Terminal->NextThink,
		ELYSIUM_NEVER_THINK);

	// --- THE CAMERA: on `screen_axis`, looking at `screen` -----------------------------------------
	TestTrue(TEXT("entry pushed the Hacking shot and adopted it"), World->CineCameraShotId() != 0);
	TestEqual(TEXT("the camera stack grew by exactly one"), Camera->GetShots().Num(),
		ShotsBefore + 1);
	const int32 FirstShot = World->CineCameraShotId();
	if (const FElysiumCameraShot* Live = Camera->GetShots().Top())
	{
		TestTrue(TEXT("the live shot settles on the screen_axis attachment"),
			Live->Origin.Equals(Terminal->ScreenAxisPointCm, 0.1f));
		TestTrue(TEXT("and looks at the screen attachment"),
			Live->bUseLookAt && Live->LookAt.Equals(Terminal->ScreenPointCm, 0.1f));
	}

	// --- THE LOGON BOX ----------------------------------------------------------------------------
	// `FUN_1021b140` frames the one authored `LogonScreen` line, centred: (36 - 14 - 2) / 2 = 10.
	TestEqual(TEXT("the logon box reads \"Welcome, Jack.\""), Row(2),
		GymBoxRow(Columns, TEXT("Welcome, Jack."), 10));
	TestEqual(TEXT("with the root menu under it"), Row(5), TEXT(" Home menu"));
	TestEqual(TEXT("the one directory listed lowercased"), Row(8), TEXT("    safe"));
	TestEqual(TEXT("and the prompt on row 22"), Row(22), TEXT(" Type menu or command:"));

	// --- THE COMMANDS, through the presentation intents where they reach the world -----------------
	// `UElysiumPresentationSubsystem`'s five intents are the widget's only channel to a terminal,
	// and each re-resolves the map through `UElysiumMapSubsystem::GetCurrentMap()`. The gym's map
	// actor is stood directly on a test world and is not that world's registered current map, so the
	// intents answer false here and the beat falls back to the world call they wrap. Which route ran
	// is reported rather than assumed.
	UElysiumPresentationSubsystem* Presentation =
		UElysiumPresentationSubsystem::Get(Gym.Host.World);
	bool bIntentsReachedTheWorld = false;
	auto Send = [&](const FString& Command)
	{
		if (Presentation
			&& Presentation->SubmitCommand(Terminal->Handle, Terminal->SessionSerial, Command))
		{
			bIntentsReachedTheWorld = true;
			return true;
		}
		return World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, Command);
	};
	auto SendAcknowledge = [&]()
	{
		if (Presentation && Presentation->Acknowledge(Terminal->Handle, Terminal->SessionSerial))
		{
			bIntentsReachedTheWorld = true;
			return true;
		}
		return World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial, FString());
	};
	auto SendBreak = [&]()
	{
		if (Presentation && Presentation->Break(Terminal->Handle, Terminal->SessionSerial))
		{
			bIntentsReachedTheWorld = true;
			return true;
		}
		return World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial,
			TEXT("break"));
	};
	auto SendQuit = [&]()
	{
		if (Presentation && Presentation->Quit(Terminal->Handle, Terminal->SessionSerial))
		{
			bIntentsReachedTheWorld = true;
			return true;
		}
		return World->SubmitTerminalCommand(Terminal->Handle, Terminal->SessionSerial,
			TEXT("quit"));
	};

	TestTrue(TEXT("`Safe` is accepted"), Send(TEXT("Safe")));
	TestEqual(TEXT("and asks for its password"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);
	TestEqual(TEXT("with the login prompt on the last row"), Row(23), TEXT(" Password:"));
	TestTrue(TEXT("the prompt publishes a view"), World->BuildTerminalView(View));
	TestEqual(TEXT("carrying InfoCtrl hint 3, the Ctrl-C offer"), View.HudHintType, 3);

	TestTrue(TEXT("`chopshop` is accepted"), Send(TEXT("chopshop")));
	TestEqual(TEXT("and enters Safe"), Terminal->CurrentDirectory, 0);
	TestEqual(TEXT("ending on the acknowledge prompt"), Row(23),
		TEXT(" [Press \"ENTER\" to continue]"));
	TestEqual(TEXT("in acknowledge mode"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);

	TestTrue(TEXT("Enter past the acknowledgement is accepted"), SendAcknowledge());
	TestEqual(TEXT("which draws the Safe menu"), Row(5), TEXT(" Safe Menu"));
	TestEqual(TEXT("and returns to the line editor"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Line);

	const double UnlockIssued = Gym.Now();
	TestTrue(TEXT("`Unlock` is accepted"), Send(TEXT("Unlock")));
	TestEqual(TEXT("the executor prints the authored runtext"), Row(6),
		TEXT(" Safe doors unlocked."));
	TestEqual(TEXT("and waits on the acknowledge prompt"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Acknowledge);
	TestTrue(TEXT("Enter past the runtext is accepted"), SendAcknowledge());

	// --- THE AUTHORED ROWS, on the game clock ------------------------------------------------------
	Gym.Advance(UnlockIssued);
	TestFalse(TEXT("OnTrigger0 unlocked tutsafelock through the authored row"), GymLocked(Padlock));
	TestFalse(TEXT("and the padlock is still drawn before the delayed hide"), Padlock->IsHidden());
	Gym.Advance(UnlockIssued + 0.6);
	TestTrue(TEXT("the +0.5 s ScriptHide row then hides it"), Padlock->IsHidden());

	// --- THE EXIT ---------------------------------------------------------------------------------
	TestTrue(TEXT("`quit` closes the session"), SendQuit());
	AddInfo(bIntentsReachedTheWorld
		? TEXT("the beat drove the presentation intents")
		: TEXT("the presentation intents do not resolve the gym's map actor "
			"(UElysiumMapSubsystem::GetCurrentMap is not this test world's); the beat drove "
			"FElysiumEntityWorld::SubmitTerminalCommand, which the intents wrap"));
	TestFalse(TEXT("the terminal stops publishing a session"), World->BuildTerminalView(View));
	TestTrue(TEXT("the exit mobilizes the player again"), Player->IsMobile());
	TestFalse(TEXT("the cine slot is dropped"), World->HasScriptedCamera());
	TestEqual(TEXT("and the stack is back where it started"), Camera->GetShots().Num(),
		ShotsBefore);
	TestTrue(TEXT("the shot that was popped is the one entry pushed"), FirstShot != 0);

	// --- THE SCREENSAVER IS BACK ON THE GLASS -------------------------------------------------------
	// The exit re-arms at `ss_start + now` (`0x1021a6f6`), unfloored. Nothing else drives the glass
	// once the session is gone, so the label returning IS the screensaver think running again.
	TestTrue(TEXT("the exit re-armed the screensaver at ss_start"),
		FMath::IsNearlyEqual(Terminal->NextThink,
			static_cast<float>(Gym.Now()) + Terminal->ScreenSaverStart, 0.01f));
	TestEqual(TEXT("the directory draw is still what is on the glass right after the exit"),
		FindLabelRow(Terminal->Screen, Label), INDEX_NONE);
	Gym.Advance(Gym.Now() + Terminal->ScreenSaverStart + 0.2);
	TestTrue(TEXT("and one ss_start later the screensaver label is back on the glass"),
		FindLabelRow(Terminal->Screen, Label) != INDEX_NONE);

	// --- THE SAFE, THE KEYCARD AND THE DOOR ---------------------------------------------------------
	if (bItems)
	{
		FElysiumItemContainer* Safe = SafeEntity->AsItemContainer();
		if (TestNotNull(TEXT("tutsafe is an item container"), Safe))
		{
			const FString KeyCard = TEXT("item_k_tutorial_chopshop_stairs_key");
			TestTrue(TEXT("the safe holds its equip0 keycard"),
				Safe->Inventory.Has(*Safe, KeyCard));
			bool bTaken = false;
			for (int32 Slot = 0; Slot < Safe->Inventory.Num() && !bTaken; ++Slot)
			{
				bTaken = Safe->TakeToPlayer(*Player, Slot);
			}
			TestTrue(TEXT("the keycard is taken out of the unlocked safe"), bTaken);
			TestTrue(TEXT("and the player carries it"), Player->Inventory.Has(*Player, KeyCard));
			Gym.Advance(Gym.Now());

			// The knobs are `prop_doorknob_electronic` with `key_name` = the keycard and
			// `delete_key 1`: the first accepts and consumes it, so the far-side knob has nothing
			// left to accept.
			World->BeginPlayerUseSession(KnobA->Handle, World->PlayerHandle());
			TestFalse(TEXT("the door knob opens with the keycard"), GymLocked(KnobA));
			TestFalse(TEXT("delete_key consumed it"), Player->Inventory.Has(*Player, KeyCard));
			World->BeginPlayerUseSession(KnobB->Handle, World->PlayerHandle());
			TestTrue(TEXT("so the far-side knob stays locked"), GymLocked(KnobB));
		}
	}

	// --- THE SECOND RUN: Ctrl+C, on the game clock ---------------------------------------------------
	// `break` is the string the client's Ctrl+C chord sends (`s_hackcmd_break_102b721c`), and only
	// the pending-password arm acts on it. Relock the directory so there is a prompt to break, then
	// re-approach and re-press: the whole second run goes through the use edge too.
	Terminal->DirectoryUnlocked[0] = 0;
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);
	World->QueuePlayerUseEdge(EElysiumUseEdge::Pressed);
	Gym.Frame(Gym.Now());
	if (!TestTrue(TEXT("a second +use reopens the session"), Terminal->CurrentUser.IsSet()))
	{
		return false;
	}
	const int32 SecondShot = World->CineCameraShotId();
	TestTrue(TEXT("with a fresh camera handle"), SecondShot != 0 && SecondShot != FirstShot);
	TestTrue(TEXT("`Safe` is accepted on the relocked directory"), Send(TEXT("Safe")));
	TestEqual(TEXT("which asks for its password again"), Terminal->InputMode(),
		EElysiumTerminalInputMode::Password);

	TestTrue(TEXT("Ctrl+C sends `break`, which the password prompt accepts"), SendBreak());
	if (TestTrue(TEXT("break started the timed attempt"), !Terminal->HackBuffer().IsEmpty()))
	{
		// Tier 3 fills the buffer with the REAL password and reveals it; below tier 3 the buffer is
		// scrambled and the typed submit rejects it. The roll is drawn from the seeded Skill stream,
		// so which arm runs is reproducible — both are asserted, and the one that ran is reported.
		const bool bTierThree = Terminal->HackBuffer() == TEXT("chopshop");
		const uint32 CrackingRevision = Terminal->ViewRevision;
		Gym.Advance(Gym.Now() + 1.0);
		TestTrue(TEXT("the cracking row redraws on the game clock"),
			Terminal->ViewRevision > CrackingRevision);
		TestFalse(TEXT("and the buffer is still live mid-crack"), Terminal->HackBuffer().IsEmpty());
		TestFalse(TEXT("a live cracking buffer swallows every typed line"),
			Send(TEXT("Unlock")));
		Gym.Advance(Gym.Now() + 6.0);
		TestTrue(TEXT("the buffer flushes when the interval is up"),
			Terminal->HackBuffer().IsEmpty());
		if (bTierThree)
		{
			AddInfo(TEXT("the seeded roll reached tier 3: break typed the real password"));
			TestEqual(TEXT("a tier-3 crack enters Safe without the player typing it"),
				Terminal->CurrentDirectory, 0);
		}
		else
		{
			AddInfo(TEXT("the seeded roll stayed below tier 3: break typed a scrambled buffer"));
			// `PasswordFailed`'s SKILL arm (`0x1021c560`) is not the typed arm: it does not reprint
			// the retry prompt. It counts the attempt, raises InfoCtrl hint 6 with the difficulty,
			// and `EnterDirectory(-1)` drops the player back at the root — still inside the
			// terminal, with the pending directory cleared.
			TestEqual(TEXT("a sub-tier-3 crack is refused and returns to the root directory"),
				Terminal->CurrentDirectory, INDEX_NONE);
			TestEqual(TEXT("with no directory left pending"), Terminal->PendingDirectory,
				INDEX_NONE);
			TestEqual(TEXT("drawing the root menu again"), Row(5), TEXT(" Home menu"));
			TestTrue(TEXT("counting against the directory's attempts"),
				Terminal->DirectoryAttempts[0] > 0);
			FElysiumTerminalView Failed;
			if (TestTrue(TEXT("and it publishes a view"), World->BuildTerminalView(Failed)))
			{
				TestEqual(TEXT("carrying InfoCtrl hint 6, the skill-too-low line"),
					Failed.HudHintType, 6);
				TestEqual(TEXT("with the directory's difficulty"), Failed.HudHintValue,
					Terminal->Definition.Directories[0].Difficulty > 0
						? Terminal->Definition.Directories[0].Difficulty : Terminal->Difficulty);
			}
		}
	}
	if (Terminal->CurrentUser.IsSet())
	{
		TestTrue(TEXT("the second run quits"), SendQuit());
		TestFalse(TEXT("dropping the cine slot"), World->HasScriptedCamera());
		TestEqual(TEXT("and unwinding the stack"), Camera->GetShots().Num(), ShotsBefore);
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
