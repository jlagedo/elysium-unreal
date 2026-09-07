// The anchor grammar — `FUN_10071e00` (parse) and `FUN_1006f080` (resolve), and the grapple role
// pair the two grapple keywords read.
//
// Every case here is a shipped file read verbatim or the exact shape retail's parser produces from
// one, because the grammar is authored content: a keyword the port answers differently is a shot
// that frames the wrong thing, and there are 66 files of them. No world geometry and no RHI — the
// anchor step is entities, bounds and an offset frame, all of which the recording services stand in
// for.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraComponent.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Player/ElysiumCameraShots.h"

#include "Misc/ScopeExit.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumCameraAnchorTests
{
// One context flag (runs anywhere) + the product filter, the same convention every other Elysium
// suite declares (`ElysiumCameraTests.cpp:120-135`).
static constexpr EAutomationTestFlags GElysiumCameraAnchorFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A world with a worldspawn at the origin (which is what `World` and the unrecognised-keyword
// fallthrough resolve to) and two combat characters to hang a grapple pair off.
FElysiumEntityDefs MakeAnchorWorldDefs()
{
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__camera_anchor_test__");
	FElysiumEntityDef World;
	World.Classname = TEXT("worldspawn");
	Defs.Defs.Add(MoveTemp(World));
	FElysiumEntityDef Attacker;
	Attacker.Classname = TEXT("npc_VPedestrian");
	Attacker.TargetName = TEXT("attacker");
	Defs.Defs.Add(MoveTemp(Attacker));
	FElysiumEntityDef Victim;
	Victim.Classname = TEXT("npc_VPedestrian");
	Victim.TargetName = TEXT("victim");
	Defs.Defs.Add(MoveTemp(Victim));
	return Defs;
}

// One NPC line, which is all `OpenDialog` needs to make an entity the player's dialogue partner —
// retail's `player+0xFE8`, and what a `DialogTarget` anchor resolves to. Copied from
// `ElysiumDialogueCameraTests.cpp`, where it is file-static.
TSharedRef<FElysiumDlgConversation> MakeOneLineConversation()
{
	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	FElysiumDlgLine& Line = File->Lines.AddDefaulted_GetRef();
	Line.Id = 1;
	Line.TextMale = TEXT("Test line");
	Line.Link = TEXT("#");
	Line.Role = EElysiumDlgRole::NpcLine;
	File->IndexById.Add(1, 0);
	TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
		File, true, false, [](const FString&) { return true; }, [](const FString&) {});
	Conversation->Start();
	return Conversation;
}

const FElysiumCameraShotDef* FindShot(const TArray<FElysiumCameraShotDef>& Shots, const TCHAR* Name)
{
	return Shots.FindByPredicate([Name](const FElysiumCameraShotDef& Def)
		{ return Def.Name.Equals(Name, ESearchCase::IgnoreCase); });
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraAnchorTest, "Elysium.Substrate.CameraAnchors",
	ElysiumCameraAnchorTests::GElysiumCameraAnchorFlags)

bool FElysiumCameraAnchorTest::RunTest(const FString&)
{
	using namespace ElysiumCameraAnchorTests;

	// Two files author values the parser now warns about; both warnings are the point of their case.
	AddExpectedError(TEXT("is not a keyword"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("differs from retail's"), EAutomationExpectedErrorFlags::Contains, 0);

	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	// --- `centerfullview.txt`, verbatim ---------------------------------------------------------
	// The one shipped file that writes `AbsMin` / `AbsMax`, and the one that writes a
	// `FollowEntAngles` offset on a `Center` anchor.
	{
		const FString Text = TEXT(R"KV(
CameraShotTable
{
	CenterFullView
	{
		End
		{
			"Position"	"Named"
			"AttachPos"	"Center"
			"AttachType"	"FollowEntAngles"
			"OffsetOrigin"	"[16, 0, 0]"
		}
		Target
		{
			Point1 { "Position" "Named"  "AttachPos" "AbsMin"  "AttachType" "Follow" }
			Point2 { "Position" "Named"  "AttachPos" "AbsMax"  "AttachType" "Follow" }
		}
		CameraConstraints
		{
			"MoveAccel"		"100.0"
			"TurnAccel"		"60"
			"MoveSpeed"		"250"
			"MaxTurnRate"		"[90, 90, 90]"
			"DistanceTolerance"	"5"
			"AngularTolerance"	"[5, 5, 5]"
			"FieldOfView"		"75"
			"DialogPOV"		"0"
			"AutoPositionFromTarget" "1"
		}
	}
}
)KV");
		FElysiumCameraShotDef Def;
		if (TestTrue(TEXT("centerfullview parses"), ElysiumCameraShots::ParseText(Text, Def)))
		{
			TestTrue(TEXT("its End is a Center anchor following the entity's angles"),
				Def.End.AttachPoint == EElysiumShotAttachPos::Center
					&& Def.End.Attach == EElysiumShotAttach::FollowEntAngles);
			TestTrue(TEXT("Point1 is AbsMin and Point2 AbsMax"),
				Def.Target1.AttachPoint == EElysiumShotAttachPos::AbsMin
					&& Def.Target2.AttachPoint == EElysiumShotAttachPos::AbsMax);
			TestEqual(TEXT("and both target points are present, in order"), Def.TargetPointCount, 2);
			TestTrue(TEXT("so both presence flags are raised"),
				Def.bTargetPoint1Flagged && Def.bTargetPoint2Flagged);
			TestTrue(TEXT("AutoPositionFromTarget parses"), Def.Constraints.bAutoPositionFromTarget);

			// A known bounds box, deliberately NOT centred on the entity's origin, so `AbsMin` /
			// `AbsMax` / `Center` / `Top` / `Bottom` are all distinguishable from it.
			FElysiumRecordingServices Services;
			Services.bHasPlayer = true;
			const FVector Origin(300.0f, 40.0f, 0.0f);
			const FBox Box(FVector(280.0f, 10.0f, 5.0f), FVector(340.0f, 70.0f, 185.0f));
			Services.UseBodyBounds = Box;
			Services.bHasUseBodyBounds = true;

			FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
			World.Load(MakeAnchorWorldDefs());
			FElysiumEntity* Subject = World.FindByName(TEXT("attacker"));
			if (TestNotNull(TEXT("the framed entity"), Subject))
			{
				Subject->Origin = Origin;
				Subject->Angles = FVector(0.0f, 90.0f, 0.0f);   // Source yaw

				// **The anchor step is asserted with `AutoPositionFromTarget` cleared.** The shipped
				// file sets the flag, and SC7 landed the solve that reads it, so a resolve of the
				// file verbatim answers the *framed* origin rather than the anchor's — which is the
				// point of `Elysium.Substrate.CameraAutoPosition` and would hide this case's own
				// subject, the `FollowEntAngles` offset frame. Both are asserted, in order.
				FElysiumCameraShotDef Unframed = Def;
				Unframed.Constraints.bAutoPositionFromTarget = false;

				FElysiumCameraShot Shot;
				if (TestTrue(TEXT("centerfullview resolves"),
					FElysiumCameraDirector::Resolve(&World, Unframed, Subject->Handle, Shot)))
				{
					// Neither target point authors an `OffsetOrigin`, so retail's offset step is
					// gated off (`0x20000` clear) and the two points are the raw bounds corners.
					// Two points track their midpoint, which for mins/maxs is the box centre.
					TestTrue(TEXT("AbsMin/AbsMax track the bounds' own centre"),
						Shot.LookAt.Equals(Box.GetCenter(), 0.01f));
					// The End anchor is the box centre plus [16,0,0] turned by the ENTITY's angles.
					const FVector Expected = Box.GetCenter()
						+ ElysiumSkeletalBasis::FromSourceAngles(Subject->Angles)
							.RotateVector(FVector(16.0f, 0.0f, 0.0f) * ElysiumCam::U);
					TestTrue(TEXT("FollowEntAngles turns the offset by the entity's facing"),
						Shot.Origin.Equals(Expected, 0.01f));
					TestTrue(TEXT("and the shot carries its authored FOV"),
						FMath::IsNearlyEqual(Shot.FieldOfView, 75.0f));

					// The file as shipped: the flag is set, so the origin is pulled back along the
					// camera->look-at axis to frame the lower target point (SC7).
					FElysiumCameraShot Framed;
					if (TestTrue(TEXT("and the shipped, flagged file resolves too"),
						FElysiumCameraDirector::Resolve(&World, Def, Subject->Handle, Framed)))
					{
						TestTrue(TEXT("AutoPositionFromTarget re-frames the origin"),
							Framed.Origin.Equals(ElysiumCam::AutoPositionFromTarget(Shot.Origin,
								Shot.LookAt, Box.Min, Box.Max, 75.0f), 0.01f));
						TestTrue(TEXT("and leaves the look-at where the anchors put it"),
							Framed.LookAt.Equals(Shot.LookAt, 0.01f));
					}
				}

				// --- `Top` / `Bottom` on an off-origin body ---------------------------------
				// Retail takes the ORIGIN's XY and only Z from the bounds, so an anchor on a body
				// whose box is offset does not slide sideways with it. The port used to take a
				// bounds fraction on all three axes.
				FElysiumCameraShotDef TopDef;
				TestTrue(TEXT("a Top/Bottom shot parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Edges { End { "Position" "Named" "AttachPos" "Top" }
	Target { Point1 { "Position" "Named" "AttachPos" "Bottom" } } } }
)KV"), TopDef));
				FElysiumCameraShot Edges;
				if (TestTrue(TEXT("the Top/Bottom shot resolves"),
					FElysiumCameraDirector::Resolve(&World, TopDef, Subject->Handle, Edges)))
				{
					TestTrue(TEXT("Top is the origin's XY with only Z from the bounds"),
						Edges.Origin.Equals(FVector(Origin.X, Origin.Y, Box.Max.Z), 0.01f));
					TestTrue(TEXT("Bottom is the same with the bounds' minimum Z"),
						Edges.LookAt.Equals(FVector(Origin.X, Origin.Y, Box.Min.Z), 0.01f));
				}
			}
		}
	}

	// --- `stealth_kill.txt`, verbatim, through both grapple roles --------------------------------
	{
		const FString Text = TEXT(R"KV(
CameraShotTable
{
	Stealth_Kill_1
	{
		Start
		{
			"Position"	"GrappleAttacker"
			"AttachPos"	"Origin"
			"AttachType"	"Follow"
			"OffsetOrigin"	"[64, 64, 96]"
		}
		Target { Point1 { "Position" "GrappleAttacker" "AttachPos" "Bone: Bip01 Head" "AttachType" "Follow" } }
		CameraConstraints { "TurnAccel" "60" "MaxTurnRate" "[180,180,180]"
			"DistanceTolerance" "10" "AngularTolerance" "[2, 2, 2]" }
	}
	Stealth_Kill_4
	{
		Start
		{
			"Position"	"GrappleVictim"
			"AttachPos"	"Origin"
			"AttachType"	"FollowEntAngles"
			"OffsetOrigin"	"[-32, 0, 128]"
		}
		Target { Point1 { "Position" "GrappleVictim" "AttachPos" "Bone: Bip01 Head" "AttachType" "Follow" } }
		CameraConstraints { "TurnAccel" "60" "MaxTurnRate" "[180,180,180]"
			"DistanceTolerance" "10" "AngularTolerance" "[2, 2, 2]" }
	}
}
)KV");
		TArray<FElysiumCameraShotDef> Shots;
		if (TestTrue(TEXT("stealth_kill parses"), ElysiumCameraShots::ParseAllText(Text, Shots)))
		{
			const FElysiumCameraShotDef* Kill1 = FindShot(Shots, TEXT("Stealth_Kill_1"));
			const FElysiumCameraShotDef* Kill4 = FindShot(Shots, TEXT("Stealth_Kill_4"));
			TestTrue(TEXT("Stealth_Kill_1 anchors on the attacker"),
				Kill1 && Kill1->Start.Position == EElysiumShotPosition::GrappleAttacker);
			TestTrue(TEXT("Stealth_Kill_4 anchors on the victim"),
				Kill4 && Kill4->Start.Position == EElysiumShotPosition::GrappleVictim);

			FElysiumRecordingServices Services;
			Services.bHasPlayer = true;
			FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
			World.Load(MakeAnchorWorldDefs());
			FElysiumEntity* AttackerEnt = World.FindByName(TEXT("attacker"));
			FElysiumEntity* VictimEnt = World.FindByName(TEXT("victim"));
			FElysiumCombatCharacter* Attacker = AttackerEnt ? AttackerEnt->AsCombatCharacter() : nullptr;
			FElysiumCombatCharacter* Victim = VictimEnt ? VictimEnt->AsCombatCharacter() : nullptr;
			if (Kill1 && Kill4 && TestNotNull(TEXT("the attacker"), Attacker)
				&& TestNotNull(TEXT("the victim"), Victim))
			{
				AttackerEnt->Origin = FVector(100.0f, 0.0f, 0.0f);
				VictimEnt->Origin = FVector(400.0f, 0.0f, 0.0f);

				// `StartGrappleAttack(victim, 3)` — the stealth kill. Retail's role literals: the
				// attacker enters with role 0, the victim with role 1.
				TestTrue(TEXT("the stealth-kill pair enters"),
					Attacker->EnterGrapplePair(*Victim, EElysiumGrappleType::StealthKill));
				TestTrue(TEXT("the attacker holds role 0"),
					Attacker->Grapple.Role == EElysiumGrappleRole::Attacker);
				TestTrue(TEXT("the victim holds role 1"),
					Victim->Grapple.Role == EElysiumGrappleRole::Victim);
				TestTrue(TEXT("and each points at the other"),
					Attacker->Grapple.Partner == VictimEnt->Handle
						&& Victim->Grapple.Partner == AttackerEnt->Handle);
				TestTrue(TEXT("only the victim carries the anim driver"),
					!Attacker->Grapple.AnimDriver.IsSet()
						&& Victim->Grapple.AnimDriver == AttackerEnt->Handle);

				// The discriminator is the **`Start` anchor**, whose `AttachPos` is `Origin`: it
				// carries whichever entity the keyword picked into a distinct world position.
				// (`Target Point1` is `Bone: Bip01 Head`, and neither the bone nor a body exists
				// headlessly, so the look-at is `vec3_origin` for every one of these — retail's
				// non-animating answer, asserted once below rather than four times.)
				auto ResolveFor = [&World](const FElysiumCameraShotDef& Def,
					const FElysiumEntityHandle& Subject, FElysiumCameraShot& Out)
				{
					return FElysiumCameraDirector::Resolve(&World, Def, Subject, Out);
				};
				// `Follow` turns the offset in the attach point's frame, `FollowEntAngles` in the
				// entity's; with `AttachPos Origin` both are the entity's own abs angles.
				const FVector OnAttacker = AttackerEnt->Origin
					+ ElysiumSkeletalBasis::FromSourceAngles(AttackerEnt->Angles)
						.RotateVector(FVector(64.0f, 64.0f, 96.0f) * ElysiumCam::U);
				const FVector OnVictim = VictimEnt->Origin
					+ ElysiumSkeletalBasis::FromSourceAngles(VictimEnt->Angles)
						.RotateVector(FVector(-32.0f, 0.0f, 128.0f) * ElysiumCam::U);

				FElysiumCameraShot Shot;
				// `GrappleAttacker` with the ATTACKER as the subject resolves to **the subject
				// itself** (role 0), not to the partner.
				if (TestTrue(TEXT("Stealth_Kill_1 resolves for the attacker"),
					ResolveFor(*Kill1, AttackerEnt->Handle, Shot)))
				{
					TestTrue(TEXT("GrappleAttacker on the attacker is the subject itself"),
						Shot.Origin.Equals(OnAttacker, 0.01f));
					TestTrue(TEXT("and a Bone: target on an entity that does not animate is the "
						"world origin, retail's non-animating answer"),
						Shot.LookAt.IsNearlyZero(0.01f));
				}
				// The same shot from the VICTIM's point of view resolves to the partner.
				if (TestTrue(TEXT("Stealth_Kill_1 resolves for the victim"),
					ResolveFor(*Kill1, VictimEnt->Handle, Shot)))
				{
					TestTrue(TEXT("GrappleAttacker on the victim is the partner"),
						Shot.Origin.Equals(OnAttacker, 0.01f));
				}
				// `GrappleVictim` is the mirror on both halves.
				if (TestTrue(TEXT("Stealth_Kill_4 resolves for the attacker"),
					ResolveFor(*Kill4, AttackerEnt->Handle, Shot)))
				{
					TestTrue(TEXT("GrappleVictim on the attacker is the partner"),
						Shot.Origin.Equals(OnVictim, 0.01f));
				}
				if (TestTrue(TEXT("Stealth_Kill_4 resolves for the victim"),
					ResolveFor(*Kill4, VictimEnt->Handle, Shot)))
				{
					TestTrue(TEXT("GrappleVictim on the victim is the subject itself"),
						Shot.Origin.Equals(OnVictim, 0.01f));
				}

				// **The dead-handle fall-through.** Both grapple arms additionally require the
				// `+0x1538` handle to be live; without it the anchor takes the `World` tail, which
				// is worldspawn — the origin, plus the anchor's own offset.
				Attacker->LeaveGrapplePair();
				TestFalse(TEXT("leaving clears both halves"),
					Attacker->Grapple.IsPaired() || Victim->Grapple.IsPaired());
				if (TestTrue(TEXT("an unpaired stealth-kill shot still resolves"),
					ResolveFor(*Kill1, AttackerEnt->Handle, Shot)))
				{
					TestTrue(TEXT("a dead grapple partner falls through to worldspawn"),
						Shot.Origin.Equals(FVector(64.0f, 64.0f, 96.0f) * ElysiumCam::U, 0.01f));
					TestFalse(TEXT("which is neither of the two characters"),
						Shot.Origin.Equals(OnAttacker, 0.01f));
				}
			}
		}
	}

	// --- the unrecognised `Position` value --------------------------------------------------------
	// The port used to take it as an entity name. Retail's `_strstr` chain has no such arm: it falls
	// through to `World`, and `World` is worldspawn — the origin.
	{
		FElysiumCameraShotDef Def;
		TestTrue(TEXT("an unrecognised Position parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Vantage { End { "Position" "cam_marker_1" "AttachPos" "Origin"
	"AttachType" "None" "OffsetOrigin" "[0, 0, 10]" } } }
)KV"), Def));
		TestTrue(TEXT("and falls through to World, not to a named entity"),
			Def.End.Position == EElysiumShotPosition::World && Def.End.NamedEntity.IsEmpty());

		FElysiumRecordingServices Services;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeAnchorWorldDefs());
		FElysiumCameraShot Shot;
		if (TestTrue(TEXT("the fallthrough shot resolves"),
			FElysiumCameraDirector::Resolve(&World, Def, FElysiumEntityHandle::Invalid(), Shot)))
		{
			TestTrue(TEXT("World is worldspawn's own origin plus the authored offset"),
				Shot.Origin.Equals(FVector(0.0f, 0.0f, 10.0f) * ElysiumCam::U, 0.01f));
		}
	}

	// --- the `Follow` offset frame is the ATTACH POINT's, `FollowEntAngles` the entity's ----------
	{
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		const FVector HeadPos(500.0f, 0.0f, 170.0f);
		// The bone's own frame, turned 90 degrees away from the pawn's.
		Services.BodyAttachments.Add(FName(TEXT("Bip01 Head")),
			FTransform(FRotator(0.0f, 90.0f, 0.0f), HeadPos));

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeAnchorWorldDefs());
		FElysiumEntity* Subject = World.FindByName(TEXT("attacker"));
		if (TestNotNull(TEXT("the boned entity"), Subject))
		{
			Subject->Origin = FVector(500.0f, 0.0f, 0.0f);
			Subject->Angles = FVector::ZeroVector;   // the pawn faces Unreal +X

			FElysiumCameraShotDef Def;
			TestTrue(TEXT("a bone-offset pair parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { BoneFrames
{
	End    { "Position" "Named" "AttachPos" "Bone: Bip01 Head" "AttachType" "Follow"
	         "OffsetOrigin" "[100, 0, 0]" }
	Target { Point1 { "Position" "Named" "AttachPos" "Bone: Bip01 Head"
	         "AttachType" "FollowEntAngles" "OffsetOrigin" "[100, 0, 0]" } }
} }
)KV"), Def));
			TestEqual(TEXT("the inline bone name is trimmed off the prefix"),
				Def.End.AttachPointName, FString(TEXT("Bip01 Head")));

			FElysiumCameraShot Shot;
			if (TestTrue(TEXT("the bone-offset shot resolves"),
				FElysiumCameraDirector::Resolve(&World, Def, Subject->Handle, Shot)))
			{
				TestTrue(TEXT("Follow turns the offset in the BONE's frame"),
					Shot.Origin.Equals(HeadPos + FVector(0.0f, 100.0f, 0.0f) * ElysiumCam::U, 0.01f));
				TestTrue(TEXT("FollowEntAngles turns the same offset in the PAWN's"),
					Shot.LookAt.Equals(HeadPos + FVector(100.0f, 0.0f, 0.0f) * ElysiumCam::U, 0.01f));
			}

			// --- the bone index is resolved ONCE, at bind time -------------------------------
			// Retail resolves the inline name to an index inside `SetShotAnchorEntity` and re-uses
			// it; `+0x620` is written on the `0x200` / `0x400` arms alone (`FUN_1006ef50`), so a
			// non-animating entity and a non-bone anchor both leave the previous index **stale**,
			// and nothing looks the name up again either way.
			FElysiumShotBindings Bindings;
			FElysiumCameraDirector::BindAnchors(&World, Def, Subject->Handle, Bindings);
			TestTrue(TEXT("the bone index resolves at bind time"),
				Bindings.Anchors[1].IsPointIndexResolved());

			FElysiumCameraShotDef Missing;
			TestTrue(TEXT("a shot on an absent bone parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { NoBone { End { "Position" "Named" "AttachPos" "Bone: Bip01 Tail" } } }
)KV"), Missing));
			FElysiumShotBindings Unresolved;
			FElysiumCameraDirector::BindAnchors(&World, Missing, Subject->Handle, Unresolved);
			TestFalse(TEXT("an entity that does not animate that name leaves the index unresolved"),
				Unresolved.Anchors[1].IsPointIndexResolved());

			// Supplying the bone AFTER the bind does not rescue the anchor: the resolve never looks
			// a name up, which is the whole point of caching the index.
			Services.BodyAttachments.Add(FName(TEXT("Bip01 Tail")),
				FTransform(FVector(900.0f, 0.0f, 0.0f)));
			FElysiumCameraShot Late;
			if (TestTrue(TEXT("the absent-bone shot still resolves"),
				FElysiumCameraDirector::Resolve(&World, Missing, Subject->Handle, Late,
					&Unresolved, EElysiumShotResolvePass::Think)))
			{
				// `FUN_1006f080`'s `0x200` arm at `0x1006f32e`: `GetBaseAnimating()` (vfunc `0x224`)
				// answering NULL writes **`vec3_origin`** and returns, skipping `LAB_1006f430` — so
				// the anchor is the world origin and its `OffsetOrigin` is not applied on top. This
				// is the terminal case (`Attachment: screen` on a brush monitor), and the port used
				// to answer the entity's own origin instead, a different world position every time.
				TestTrue(TEXT("an unresolved bone index answers the WORLD origin, as retail does"),
					Late.Origin.IsNearlyZero(0.01f));
				TestFalse(TEXT("which is not the entity's own origin"),
					Late.Origin.Equals(Subject->Origin, 0.01f));
			}
			// Re-binding is what picks it up — retail's `SetShotAnchorEntity`, run again.
			FElysiumCameraDirector::BindAnchors(&World, Missing, Subject->Handle, Unresolved);
			TestTrue(TEXT("re-binding resolves the newly present bone"),
				Unresolved.Anchors[1].IsPointIndexResolved());
		}
	}

	// --- `Point2` alone ---------------------------------------------------------------------------
	// The parser raises the presence flag by ORDER while writing the fixed slot, so this raises the
	// `Point1` bit and the look-at reads the empty slot 2. Retail aims the shot at `(0,0,0)`.
	{
		FElysiumCameraShotDef Def;
		TestTrue(TEXT("a Point2-only shot parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Orphan { End { "Position" "Named" "AttachPos" "Origin" }
	Target { Point2 { "Position" "Named" "AttachPos" "Origin" } } } }
)KV"), Def));
		TestEqual(TEXT("one target block was found"), Def.TargetPointCount, 1);
		TestTrue(TEXT("and it raised the Point1 bit while filling the Point2 slot"),
			Def.bTargetPoint1Flagged && !Def.bTargetPoint2Flagged && Def.Target2.bPresent
				&& !Def.Target1.bPresent);

		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeAnchorWorldDefs());
		FElysiumEntity* Subject = World.FindByName(TEXT("attacker"));
		if (TestNotNull(TEXT("the Point2-only subject"), Subject))
		{
			Subject->Origin = FVector(700.0f, 25.0f, 60.0f);
			FElysiumCameraShot Shot;
			if (TestTrue(TEXT("the Point2-only shot resolves"),
				FElysiumCameraDirector::Resolve(&World, Def, Subject->Handle, Shot)))
			{
				TestTrue(TEXT("the shot still believes it has a target"), Shot.bUseLookAt);
				TestTrue(TEXT("but it aims at the world origin, not at the authored point"),
					Shot.LookAt.IsNearlyZero());
			}
		}
	}

	// --- a mis-cased `AttachType` -----------------------------------------------------------------
	// Retail's compare is an exact byte compare including the NUL, so `follow` is `None` — and
	// `None` on a `Start` block is what makes the whole shot LATCH. M3.
	{
		FElysiumCameraShotDef Def;
		TestTrue(TEXT("a mis-cased AttachType parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Lower { Start { "Position" "Named" "AttachPos" "Origin" "AttachType" "follow" } } }
)KV"), Def));
		TestTrue(TEXT("lowercase 'follow' reads as None"),
			Def.Start.Attach == EElysiumShotAttach::None);
		TestTrue(TEXT("and the shot therefore latches its anchors at shot start"),
			ElysiumCameraShots::LatchesAnchors(Def));
		// The correctly-cased twin does not.
		FElysiumCameraShotDef Cased;
		TestTrue(TEXT("the cased twin parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Upper { Start { "Position" "Named" "AttachPos" "Origin" "AttachType" "Follow" } } }
)KV"), Cased));
		TestTrue(TEXT("'Follow' is Follow"), Cased.Start.Attach == EElysiumShotAttach::Follow);
		TestFalse(TEXT("and a following shot does not latch"),
			ElysiumCameraShots::LatchesAnchors(Cased));
	}

	// --- `Position` is a case-sensitive SUBSTRING (`FUN_10071e00` 0x10071e1d-0x10071eaf) ---------
	// Five `_strstr` calls in one order, so a compound value takes the first arm that hits and a
	// mis-cased one hits nothing and falls through to `World`. No shipped file writes either — all
	// 156 corpus values are canonical — which is exactly why the negative has to be asserted here.
	{
		FElysiumCameraShotDef Compound;
		TestTrue(TEXT("a compound Position parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Compound { End { "Position" "PlayerEye" "AttachPos" "Origin" } } }
)KV"), Compound));
		TestTrue(TEXT("'PlayerEye' contains 'Player', so retail's _strstr chain reads it as Player"),
			Compound.End.Position == EElysiumShotPosition::Player);

		FElysiumCameraShotDef Lower;
		TestTrue(TEXT("a lower-cased Position parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Lower { End { "Position" "player" "AttachPos" "Origin" } } }
)KV"), Lower));
		TestTrue(TEXT("'player' matches nothing case-sensitively and falls through to World"),
			Lower.End.Position == EElysiumShotPosition::World);

		// The order matters as much as the comparison: `DialogTarget` is tested before `Named`, so a
		// value carrying both keywords is the earlier arm.
		FElysiumCameraShotDef Both;
		TestTrue(TEXT("a two-keyword Position parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Both { End { "Position" "NamedDialogTarget" "AttachPos" "Origin" } } }
)KV"), Both));
		TestTrue(TEXT("the chain's order decides: DialogTarget is tested before Named"),
			Both.End.Position == EElysiumShotPosition::DialogTarget);
	}

	// --- `AttachPos` likewise (`0x10071ec6`-`0x10071fb7`) ----------------------------------------
	{
		FElysiumCameraShotDef Lower;
		TestTrue(TEXT("a lower-cased AttachPos parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Lower { End { "Position" "Named" "AttachPos" "center" } } }
)KV"), Lower));
		TestTrue(TEXT("'center' matches nothing case-sensitively and falls through to Origin"),
			Lower.End.AttachPoint == EElysiumShotAttachPos::Origin);

		FElysiumCameraShotDef Compound;
		TestTrue(TEXT("a compound AttachPos parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Compound { End { "Position" "Named" "AttachPos" "EyePositionOffset" } } }
)KV"), Compound));
		TestTrue(TEXT("'EyePositionOffset' contains 'EyePosition', which is the arm that hits"),
			Compound.End.AttachPoint == EElysiumShotAttachPos::EyePosition);

		// The inline name is taken from `value + 5` / `value + 11` — the value, not the hit.
		FElysiumCameraShotDef Bone;
		TestTrue(TEXT("a Bone: AttachPos parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Boned { End { "Position" "Named" "AttachPos" "Bone: Bip01 Head" } } }
)KV"), Bone));
		TestTrue(TEXT("the inline name is what follows the five-character prefix"),
			Bone.End.AttachPointName == TEXT("Bip01 Head"));
	}

	// --- the bracket-vector parser is three ordered scans (`FUN_10071cd0`) -----------------------
	// `npcfollowmove.txt`'s own text, unquoted, which is why this is a substrate case and not only a
	// corpus one: both tokenizers end the bare run at the whitespace after the comma, so the parser
	// is handed `[-60,` and retail answers `(-60, 0, 0)`.
	{
		FElysiumCameraShotDef Def;
		TestTrue(TEXT("an unquoted OffsetOrigin parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { NPCFollowMove { End { "Position" "Named" "AttachPos" "EyePosition"
	"AttachType" "FollowEntAngles" "OffsetOrigin" [-60, 0, 72] } } }
)KV"), Def));
		TestTrue(TEXT("the truncated token still yields retail's x, with y and z at their 0 default"),
			Def.End.OffsetOrigin.Equals(FVector(-60.0f * ElysiumCam::U, 0.0f, 0.0f), 0.01f));

		// Quoted, the whole value survives the tokenizer and all three scans hit.
		FElysiumCameraShotDef Quoted;
		TestTrue(TEXT("a quoted OffsetOrigin parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Quoted { End { "Position" "Named" "AttachPos" "Origin"
	"OffsetOrigin" "[-60, 0, 72]" } } }
)KV"), Quoted));
		TestTrue(TEXT("and answers all three components"),
			Quoted.End.OffsetOrigin.Equals(
				FVector(-60.0f, 0.0f, 72.0f) * ElysiumCam::U, 0.01f));

		// No bracket at all: the first scan misses and leaves x at 0, and the two comma scans then
		// read the SECOND and THIRD numbers. Retail reads `"1, 2, 3"` as `(0, 2, 3)`.
		FElysiumCameraShotDef Bare;
		TestTrue(TEXT("a bracketless OffsetOrigin parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
CameraShotTable { Bare { End { "Position" "Named" "AttachPos" "Origin"
	"OffsetOrigin" "1, 2, 3" } } }
)KV"), Bare));
		TestTrue(TEXT("a missing bracket costs x and only x, exactly as retail's chain does"),
			Bare.End.OffsetOrigin.Equals(FVector(0.0f, 2.0f, 3.0f) * ElysiumCam::U, 0.01f));
	}

	// --- the latch, and retail's anchor-0 bug ------------------------------------------------------
	// `special-case.txt`'s `Follow` is the ONE shipped shot with `Start { AttachType None }`: it
	// holds its shot-start sample while the entity walks away. `jack.txt` has no `Start` at all, so
	// `FUN_1006f010`'s literal-0 flags read finds a zeroed record and re-resolves everything.
	{
		const FString SpecialCase = TEXT(R"KV(
CameraShotTable { Follow { Start { "Position" "Named" "AttachPos" "Center" "AttachType" "None" } } }
)KV");
		const FString Jack = TEXT(R"KV(
CameraShotTable { Jack
{
	End { "Position" "DialogTarget" "AttachPos" "Origin" "AttachType" "Follow"
	      "OffsetOrigin" "[50, 0, 65]" }
	Target { Point1 { "Position" "DialogTarget" "AttachPos" "Origin" "AttachType" "None" } }
	CameraConstraints { "MoveSpeed" "500" "MoveAccel" "250" "FieldOfView" "40" "DialogPOV" "1" }
} }
)KV");
		FElysiumCameraShotDef LatchDef;
		FElysiumCameraShotDef JackDef;
		TestTrue(TEXT("special-case's Follow parses"),
			ElysiumCameraShots::ParseText(SpecialCase, LatchDef));
		TestTrue(TEXT("jack parses"), ElysiumCameraShots::ParseText(Jack, JackDef));
		TestTrue(TEXT("the one shipped AttachType None Start latches"),
			ElysiumCameraShots::LatchesAnchors(LatchDef));
		TestFalse(TEXT("a shot with no Start block reads a zeroed anchor 0 and does not latch"),
			ElysiumCameraShots::LatchesAnchors(JackDef));
		TestTrue(TEXT("even though its own Point1 authors AttachType None"),
			JackDef.Target1.Attach == EElysiumShotAttach::None);

		ElysiumCameraShots::Install(TEXT("special-case-follow"), LatchDef);
		ElysiumCameraShots::Install(TEXT("jack"), JackDef);

		// No seeded bounds here on purpose: the fallback hull is measured from the entity's OWN
		// origin, so a `Center` anchor moves with it — which is what makes "held" and "re-resolved"
		// distinguishable while the entity walks away.
		FElysiumRecordingServices Services;
		Services.bHasPlayer = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MakeAnchorWorldDefs());
		FElysiumEntity* Walker = World.FindByName(TEXT("attacker"));
		UElysiumCameraComponent* Camera = NewObject<UElysiumCameraComponent>();
		if (TestNotNull(TEXT("the walking entity"), Walker) && TestNotNull(TEXT("a camera"), Camera))
		{
			Walker->Origin = FVector(0.0f, 0.0f, 0.0f);

			FElysiumCameraDirector Director;
			const int32 LatchId =
				Director.Push(&World, Camera, TEXT("special-case-follow"), Walker->Handle);
			TestTrue(TEXT("the latching shot pushes"), LatchId != 0);
			const FElysiumCameraShot* Live = Camera->GetShots().Top();
			const FVector Sampled = Live ? Live->Origin : FVector::ZeroVector;

			Walker->Origin = FVector(1000.0f, 0.0f, 0.0f);
			Director.Tick(&World, Camera);
			Live = Camera->GetShots().Top();
			if (TestNotNull(TEXT("the latched shot is still up"), Live))
			{
				TestTrue(TEXT("an AttachType None shot holds its shot-start sample"),
					Live->Origin.Equals(Sampled, 0.01f));
			}
			Director.Clear(Camera);

			// --- `jack.txt` with NO conversation open -----------------------------------------
			// Its `End` is `Position DialogTarget`, which is `player+0xFE8` — a dead EHANDLE
			// outside a conversation. `FUN_1006f080` resolves that handle first and, on a miss,
			// writes `vec3_origin` and returns at `0x1006f09b`, skipping `LAB_1006f430` — so the
			// authored `OffsetOrigin` is not added on top either. `SetShot` fails only on a name
			// the table does not carry, so the shot still goes up and frames the world origin.
			Walker->Origin = FVector(600.0f, 0.0f, 0.0f);
			FElysiumCameraDirector Orphan;
			TestTrue(TEXT("a DialogTarget shot with no conversation open still pushes"),
				Orphan.Push(&World, Camera, TEXT("jack"), Walker->Handle) != 0);
			if (const FElysiumCameraShot* Adrift = Camera->GetShots().Top())
			{
				TestTrue(TEXT("and frames the world origin, with no offset applied"),
					Adrift->Origin.IsNearlyZero(0.01f));
			}
			Orphan.Clear(Camera);

			// The same walk under `jack.txt`, now with the walker as the player's dialogue partner
			// — which is what `DialogTarget` names on every shipped path (`SetShot` arm 2 reads
			// `subject+0xFE8`, and every shipped caller's subject is the player).
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);   // the NPC mind admits its body on its first think
			World.OpenDialog(Walker->Handle, MakeOneLineConversation());
			TestTrue(TEXT("the walker is now the open conversation's owner"),
				World.GetOpenDialogOwner() == Walker->Handle);

			Walker->Origin = FVector::ZeroVector;
			FElysiumCameraDirector Tracking;
			TestTrue(TEXT("jack pushes"),
				Tracking.Push(&World, Camera, TEXT("jack"), Walker->Handle) != 0);
			const FElysiumCameraShot* Tracked = Camera->GetShots().Top();
			const FVector Before = Tracked ? Tracked->Origin : FVector::ZeroVector;
			Walker->Origin = FVector(1000.0f, 0.0f, 0.0f);
			Tracking.Tick(&World, Camera);
			Tracked = Camera->GetShots().Top();
			if (TestNotNull(TEXT("jack's shot is still up"), Tracked))
			{
				TestFalse(TEXT("a shot with no Start re-resolves every anchor (the anchor-0 bug)"),
					Tracked->Origin.Equals(Before, 0.01f));
				TestTrue(TEXT("and lands on the entity's new position"),
					Tracked->Origin.Equals(Walker->Origin
						+ ElysiumSkeletalBasis::FromSourceAngles(Walker->Angles)
							.RotateVector(FVector(50.0f, 0.0f, 65.0f) * ElysiumCam::U), 0.01f));
			}

			// --- `SetShotAnchorEntity` -------------------------------------------------------
			// Retail's `Named` resolves to NULL and the caller fills the slot. `Push` seeds it from
			// the subject; this is the override the death path uses for `DeathCam`'s corpse.
			FElysiumEntity* Corpse = World.FindByName(TEXT("victim"));
			if (TestNotNull(TEXT("a second entity to re-anchor onto"), Corpse))
			{
				Corpse->Origin = FVector(-400.0f, 0.0f, 0.0f);
				FElysiumCameraDirector Named;
				const int32 Id = Named.Push(&World, Camera, TEXT("jack"), Walker->Handle);
				TestTrue(TEXT("SetShotAnchorEntity re-binds the End anchor"),
					Named.SetShotAnchorEntity(&World, Camera, Id, /*End*/ 1, Corpse->Handle));
				const FElysiumCameraShot* Rebound = Camera->GetShots().Top();
				if (TestNotNull(TEXT("the re-anchored shot"), Rebound))
				{
					TestTrue(TEXT("and the shot moves onto the supplied entity"),
						Rebound->Origin.Equals(Corpse->Origin
							+ ElysiumSkeletalBasis::FromSourceAngles(Corpse->Angles)
								.RotateVector(FVector(50.0f, 0.0f, 65.0f) * ElysiumCam::U), 0.01f));
				}
				Named.Clear(Camera);
			}
			Tracking.Clear(Camera);
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
