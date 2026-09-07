// `Elysium.Content.TheatreCamera` — the Play-tier theatre witness, run headless.
//
// `docs/project/camera_scripted.md` §9 rows the theatre beat as "the `courtroom_*` and `walk_out_*`
// chains (87 authored `MoveTime 0.03` edits) and the `embrace_*` chain (28 exact zeros)", proving
// that **SC2's composition shape does not turn the authored cuts into slews** and that **SC3's
// crossfade stack survives the folded edits and repeated owner replacement without drift**.
//
// Everything here is the real `sp_theatre` export driven on a bare `FElysiumEntityWorld` +
// `FElysiumRecordingServices`: no UWorld, no pawn, no renderer, no RHI. The case abstains — never
// fails — when the map has not been exported.
//
// What the fixture can and cannot reach, said out loud rather than assumed:
//
//  * `FElysiumMapSlice` closes over `output.target`, `parentname` and `target`. It does **not**
//    follow `NextKey`, so a camera chain sliced by its root alone loses every keyframe behind it.
//    The roots handed to `Build` are therefore *every* `camera_track` / `camera_keyframe`
//    targetname in the map, plus the two `logic_relay`s that fire two of the four chain pairs.
//  * The driven world keeps only the camera entities and those two relays, and the relays keep only
//    their `PlayAsCamera*` rows: the rest of each relay's fan-out is choreographed scenes, teleports
//    and `npc_VVampire`s that a headless world has no bodies for. That is a deliberate narrowing of
//    the closure, not a claim that the rest does not exist — the authored rows the narrowing drops
//    are still asserted, verbatim, in the wire census below.
//  * The escort pair is fired by a `logic_pythoncheck` (`FindPlayer().IsMale()`) and the embrace
//    pair by an **unnamed** `trigger_once` brush. Neither can run headless — the first needs the
//    level-script namespace, the second a brush body — so those two pairs are played through the
//    same authored inputs the two firers send, and both seams are named in the report.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Misc/Paths.h"
#include "Substrate/ElysiumCameraOverride.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "Tests/ElysiumMapSlice.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumTheatreCameraTests
{
// The flags convention every camera suite declares (`ElysiumCameraTests.cpp:120-135`).
static constexpr EAutomationTestFlags GTheatreFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static const TCHAR* const GTheatreMap = TEXT("sp_theatre");

// One rendered frame's worth of substrate clock. The chains' shortest folded dwell is the
// re-attributed 0.03 s, so a 60 Hz step samples every one of them at least once.
static constexpr double GTheatreStep = 1.0 / 60.0;

static bool TheatreIsCameraClass(const FElysiumEntityDef& Def)
{
	return Def.Classname.Equals(TEXT("camera_track"), ESearchCase::IgnoreCase)
		|| Def.Classname.Equals(TEXT("camera_keyframe"), ESearchCase::IgnoreCase);
}

static float TheatreFloatKey(const FElysiumEntityDef& Def, const TCHAR* Name, float Default)
{
	const FString* Value = Def.Keys.Find(Name);
	return Value ? FCString::Atof(**Value) : Default;
}

static bool TheatreBoolKey(const FElysiumEntityDef& Def, const TCHAR* Name)
{
	const FString* Value = Def.Keys.Find(Name);
	return Value != nullptr && Value->Equals(TEXT("1"));
}

// One authored camera key, carried through the spawn-time fold exactly as the entity carries it.
struct FTheatreKey
{
	FString Name;      // lowered targetname
	FString NextKey;   // lowered
	ElysiumCameraTrack::FPoint Point;
};

// `FElysiumCameraKeyframe::FoldShortEdit` (`Private/Substrate/ElysiumCameraTrack.cpp`), mirrored so
// the case can hold the same path the live entity built and compare them value for value.
//
// The fold is order-sensitive in principle — a key whose pause was just raised from zero by its
// predecessor's fold becomes a legal re-attribution target for its own — so it is run here in the
// `.ents` order the world spawns in, which is the order `PostSpawn` runs in. (On `sp_theatre` the
// two orders happen to give the same answer; the mirror does not rely on that.)
static void FoldTheatreKeys(TArray<FTheatreKey>& Keys, const TMap<FString, int32>& ByName)
{
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		// `Activate` normalises the authored roll once, before anything samples it.
		Keys[Index].Point.Roll = FRotator::NormalizeAxis(Keys[Index].Point.Roll);
		if (!ElysiumCameraTrack::ShouldFold(Keys[Index].Point.bTimeControl, Keys[Index].Point.MoveTime))
		{
			continue;
		}
		const int32* Next = Keys[Index].NextKey.IsEmpty() ? nullptr : ByName.Find(Keys[Index].NextKey);
		const float MoveTime = Keys[Index].Point.MoveTime;
		if (MoveTime > 0.0f)
		{
			// The folded time moves to the NEXT key's pause when it has one, otherwise to this key's
			// own when it has one, otherwise it is dropped and the chain is genuinely shorter.
			if (Next != nullptr && Keys[*Next].Point.Pause > 0.0f)
			{
				Keys[*Next].Point.Pause += MoveTime;
			}
			else if (Keys[Index].Point.Pause > 0.0f)
			{
				Keys[Index].Point.Pause += MoveTime;
			}
		}
		Keys[Index].Point.MoveTime = 0.0f;
		Keys[Index].Point.bCorner = true;
		if (Next != nullptr)
		{
			Keys[*Next].Point.bCorner = true;   // forced on BOTH ends
		}
	}
}

// `FElysiumCameraTrack::BuildPath`: walk `NextKey` from a root and rebuild the chain's clock.
static bool BuildTheatreChain(const TArray<FTheatreKey>& Keys, const TMap<FString, int32>& ByName,
	const TCHAR* Root, ElysiumCameraTrack::FPath& OutPath, TArray<FString>& OutNames)
{
	OutPath = ElysiumCameraTrack::FPath();
	OutNames.Reset();
	const int32* Cursor = ByName.Find(FString(Root).ToLower());
	TSet<FString> Seen;
	while (Cursor != nullptr)
	{
		const FTheatreKey& Key = Keys[*Cursor];
		if (Seen.Contains(Key.Name))
		{
			return false;   // a cycle; the content test owns that diagnostic
		}
		Seen.Add(Key.Name);
		OutPath.Points.Add(Key.Point);
		OutNames.Add(Key.Name);
		Cursor = Key.NextKey.IsEmpty() ? nullptr : ByName.Find(Key.NextKey);
	}
	OutPath.RebuildTimes();
	return OutPath.Points.Num() > 0;
}

// One authored wire row, as the census asserts it.
struct FTheatreWire
{
	const TCHAR* Firer;
	const TCHAR* Row;
	const TCHAR* Target;
	const TCHAR* Input;
	float Delay;
};

// One chain pair and how the map starts it. `Firer` null means the authored firer cannot run
// headless and the pair is played through the inputs that firer sends.
struct FTheatreGroup
{
	const TCHAR* Label;
	const TCHAR* PositionRoot;
	const TCHAR* TargetRoot;
	const TCHAR* Firer;
	const TCHAR* FirerInput;
	float FireDelay;
	const TCHAR* Seam;   // null when the authored firer is the one that runs
};
}

using namespace ElysiumTheatreCameraTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTheatreCameraTest, "Elysium.Content.TheatreCamera",
	GTheatreFlags)
bool FElysiumTheatreCameraTest::RunTest(const FString&)
{
	if (!FElysiumMapSlice::Available(GTheatreMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre.ents is not under $ELYSIUM_EXPORT_ROOT"));
		return true;
	}

	FElysiumEntityDefs All;
	if (!TestTrue(TEXT("sp_theatre's entities parse"),
		FElysiumEntityDefs::Parse(FElysiumMapSlice::EntsPath(GTheatreMap), All)))
	{
		return false;
	}

	// ------------------------------------------------------------------------------------------
	// The census, measured off the export rather than quoted from the plan.
	// ------------------------------------------------------------------------------------------
	int32 CameraKeyCount = 0;
	int32 TrackRootCount = 0;
	int32 ShortEditCount = 0;     // `TimeControl 1` + `MoveTime 0.03`
	int32 ZeroEditCount = 0;      // `TimeControl 1` + `MoveTime` exactly 0
	int32 EmbraceZeroCount = 0;
	int32 FoldedKeyCount = 0;     // every key the spawn-time fold rewrites
	for (const FElysiumEntityDef& Def : All.Defs)
	{
		if (!TheatreIsCameraClass(Def))
		{
			continue;
		}
		++CameraKeyCount;
		if (Def.Classname.Equals(TEXT("camera_track"), ESearchCase::IgnoreCase))
		{
			++TrackRootCount;
		}
		if (!TheatreBoolKey(Def, TEXT("TimeControl")))
		{
			continue;
		}
		const float MoveTime = TheatreFloatKey(Def, TEXT("MoveTime"), 0.0f);
		if (ElysiumCameraTrack::ShouldFold(true, MoveTime))
		{
			++FoldedKeyCount;
		}
		if (FMath::IsNearlyEqual(MoveTime, 0.03f, 1.e-6f))
		{
			++ShortEditCount;
		}
		if (MoveTime == 0.0f)
		{
			++ZeroEditCount;
			if (Def.TargetName.StartsWith(TEXT("embrace"), ESearchCase::IgnoreCase))
			{
				++EmbraceZeroCount;
			}
		}
	}
	AddInfo(FString::Printf(
		TEXT("sp_theatre camera census: %d camera entities (%d camera_track roots); ")
		TEXT("%d authored `MoveTime 0.03` edits, %d exact-zero edits (%d of them embrace_*), ")
		TEXT("%d keys folded at spawn"),
		CameraKeyCount, TrackRootCount, ShortEditCount, ZeroEditCount, EmbraceZeroCount,
		FoldedKeyCount));

	TestEqual(TEXT("sp_theatre still carries its 152 camera entities"), CameraKeyCount, 152);
	TestEqual(TEXT("across eight camera_track roots"), TrackRootCount, 8);
	TestEqual(TEXT("the courtroom and walk-out chains author 87 `MoveTime 0.03` edits"),
		ShortEditCount, 87);
	TestEqual(TEXT("and the map authors 28 exact-zero edits"), ZeroEditCount, 28);
	// **Measured correction to §9.** The plan calls the 28 "the `embrace_*` chain"; read against the
	// export, 23 of them are `embrace_*` and the other five are `courtroom_target_8a` (an orphan key
	// no chain reaches) plus the four `walk_back_*` / `walk_out_back_*` roots.
	TestEqual(TEXT("23 of which are the embrace chain's"), EmbraceZeroCount, 23);
	TestEqual(TEXT("so the spawn-time fold rewrites all 115 of them and nothing else"),
		FoldedKeyCount, ShortEditCount + ZeroEditCount);

	// ------------------------------------------------------------------------------------------
	// The authored wires that start the four chain pairs, asserted verbatim.
	// ------------------------------------------------------------------------------------------
	static const FTheatreWire Wires[] = {
		{ TEXT("courtroom_scene_relay"), TEXT("OnTrigger"), TEXT("courtroom_target_1"),
			TEXT("PlayAsCameraTarget"), 0.5f },
		{ TEXT("courtroom_scene_relay"), TEXT("OnTrigger"), TEXT("courtroom_camera_1"),
			TEXT("PlayAsCameraPosition"), 0.5f },
		{ TEXT("walk_out_relay"), TEXT("OnTrigger"), TEXT("walk_out_back_target"),
			TEXT("PlayAsCameraTarget"), 0.0f },
		{ TEXT("walk_out_relay"), TEXT("OnTrigger"), TEXT("walk_out_back_camera"),
			TEXT("PlayAsCameraPosition"), 0.0f },
		{ TEXT("male_or_female"), TEXT("OnTrue"), TEXT("cinematic_shot_2"),
			TEXT("PlayAsCameraTarget"), 0.1f },
		{ TEXT("male_or_female"), TEXT("OnTrue"), TEXT("cinematic_shot_1"),
			TEXT("PlayAsCameraPosition"), 0.1f },
		{ TEXT("male_or_female"), TEXT("OnFalse"), TEXT("cinematic_shot_2"),
			TEXT("PlayAsCameraTarget"), 0.1f },
		{ TEXT("male_or_female"), TEXT("OnFalse"), TEXT("cinematic_shot_1"),
			TEXT("PlayAsCameraPosition"), 0.1f },
	};
	for (const FTheatreWire& Wire : Wires)
	{
		bool bFound = false;
		for (const FElysiumEntityDef& Def : All.Defs)
		{
			if (!Def.TargetName.Equals(Wire.Firer, ESearchCase::IgnoreCase))
			{
				continue;
			}
			for (const FElysiumOutputDef& Row : Def.Outputs)
			{
				bFound |= Row.Name.Equals(Wire.Row, ESearchCase::IgnoreCase)
					&& Row.Target.Equals(Wire.Target, ESearchCase::IgnoreCase)
					&& Row.Input.Equals(Wire.Input, ESearchCase::IgnoreCase)
					&& FMath::IsNearlyEqual(Row.Delay, Wire.Delay, 1.e-4f);
			}
		}
		TestTrue(*FString::Printf(TEXT("%s.%s -> %s.%s at +%.2f s is still authored"),
			Wire.Firer, Wire.Row, Wire.Target, Wire.Input, Wire.Delay), bFound);
	}

	// The embrace pair's firer has no targetname at all: it is a `trigger_once` brush, which is a
	// named seam headless rather than something to invent a body for.
	bool bEmbraceBrushFirer = false;
	for (const FElysiumEntityDef& Def : All.Defs)
	{
		if (!Def.Classname.Equals(TEXT("trigger_once"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		for (const FElysiumOutputDef& Row : Def.Outputs)
		{
			if (Row.Target.Equals(TEXT("embrace_camera"), ESearchCase::IgnoreCase)
				&& Row.Input.Equals(TEXT("PlayAsCameraPosition"), ESearchCase::IgnoreCase))
			{
				bEmbraceBrushFirer = Def.TargetName.IsEmpty()
					&& FMath::IsNearlyEqual(Row.Delay, 1.0f, 1.e-4f);
			}
		}
	}
	TestTrue(TEXT("the embrace pair is started by an unnamed trigger_once brush at +1 s"),
		bEmbraceBrushFirer);

	// ------------------------------------------------------------------------------------------
	// The slice, and the world it drives.
	// ------------------------------------------------------------------------------------------
	TArray<FString> Roots;
	for (const FElysiumEntityDef& Def : All.Defs)
	{
		if (TheatreIsCameraClass(Def) && !Def.TargetName.IsEmpty())
		{
			Roots.Add(Def.TargetName);
		}
	}
	Roots.Add(TEXT("courtroom_scene_relay"));
	Roots.Add(TEXT("walk_out_relay"));

	FElysiumMapSlice Slice;
	FString SliceError;
	if (!FElysiumMapSlice::Build(GTheatreMap, Roots, Slice, SliceError))
	{
		AddError(SliceError);
		return false;
	}
	AddInfo(FString::Printf(TEXT("map slice %s: %d entities kept from %d roots, %d seams"),
		GTheatreMap, Slice.Kept.Num(), Roots.Num(), Slice.Seams.Num()));
	for (const FString& Seam : Slice.Seams)
	{
		AddInfo(TEXT("  slice seam: ") + Seam);
	}
	AddInfo(TEXT("  fixture seam: FElysiumMapSlice does not close over `NextKey`, so every camera "
		"key is named as a root rather than reached from its chain's head"));
	AddInfo(TEXT("  fixture seam: the driven world keeps the camera entities and the two relays "
		"only; the relays' scene/teleport/NPC rows are dropped (asserted in the wire census above)"));

	// The authored keys, folded once, held beside the world so every comparison below is against
	// the map rather than against the port's own answer.
	TArray<FTheatreKey> Keys;
	TMap<FString, int32> KeysByName;
	for (const FElysiumEntityDef& Def : All.Defs)
	{
		if (!TheatreIsCameraClass(Def) || Def.TargetName.IsEmpty())
		{
			continue;
		}
		FTheatreKey Key;
		Key.Name = Def.TargetName.ToLower();
		Key.NextKey = Def.Keys.FindRef(TEXT("NextKey")).ToLower();
		Key.Point.Position = Def.Origin;
		Key.Point.Roll = TheatreFloatKey(Def, TEXT("Roll"), 0.0f);
		Key.Point.FocalLength = TheatreFloatKey(Def, TEXT("FocalLength"), 0.0f);
		Key.Point.bTimeControl = TheatreBoolKey(Def, TEXT("TimeControl"));
		Key.Point.MoveSpeed = TheatreFloatKey(Def, TEXT("MoveSpeed"), 64.0f);
		Key.Point.MoveTime = TheatreFloatKey(Def, TEXT("MoveTime"), 0.0f);
		Key.Point.Pause = TheatreFloatKey(Def, TEXT("Pause"), 0.0f);
		Key.Point.RateIn = TheatreFloatKey(Def, TEXT("RateIn"), 1.0f);
		Key.Point.RateOut = TheatreFloatKey(Def, TEXT("RateOut"), 1.0f);
		Key.Point.bCorner = TheatreBoolKey(Def, TEXT("Corner"));
		KeysByName.Add(Key.Name, Keys.Num());
		Keys.Add(MoveTemp(Key));
	}
	FoldTheatreKeys(Keys, KeysByName);

	FElysiumEntityDefs Driven;
	Driven.MapName = TEXT("__sp_theatre_camera_witness__");
	for (const FElysiumEntityDef& Def : Slice.Defs.Defs)
	{
		const bool bRelay = Def.TargetName.Equals(TEXT("courtroom_scene_relay"), ESearchCase::IgnoreCase)
			|| Def.TargetName.Equals(TEXT("walk_out_relay"), ESearchCase::IgnoreCase);
		if (!TheatreIsCameraClass(Def) && !bRelay)
		{
			continue;
		}
		FElysiumEntityDef Copy = Def;
		if (bRelay)
		{
			Copy.Outputs.RemoveAll([](const FElysiumOutputDef& Row)
			{
				return !Row.Input.StartsWith(TEXT("PlayAsCamera"), ESearchCase::IgnoreCase);
			});
		}
		else
		{
			// The keys' own `OnReachedKeyframe` fan-out is fades, NPC gaze and `Kill` rows against
			// entities this narrowed world does not stand. `Elysium.Content.OpeningCameraTracks`
			// owns that wire; this case owns the camera.
			Copy.Outputs.Reset();
		}
		Driven.Defs.Add(MoveTemp(Copy));
	}
	AddInfo(FString::Printf(TEXT("driven world: %d entities"), Driven.Num()));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Driven));
	World.Activate(0.0);

	// Retail's `curtime` is never zero while a map is up, and `GetCameraOverrideWeight`'s `mark > 0`
	// test is read literally, so the witness starts its clock above zero exactly as a live map does.
	double Now = 1.0;
	World.Tick(Now);

	// ------------------------------------------------------------------------------------------
	// The four chain pairs, in one world, so the position and target roles are replaced under a
	// live channel three times over — which is the "repeated owner replacement" SC3 has to survive.
	// ------------------------------------------------------------------------------------------
	static const FTheatreGroup Groups[] = {
		{ TEXT("courtroom"), TEXT("courtroom_camera_1"), TEXT("courtroom_target_1"),
			TEXT("courtroom_scene_relay"), TEXT("Trigger"), 0.5f, nullptr },
		{ TEXT("walk-out (back)"), TEXT("walk_out_back_camera"), TEXT("walk_out_back_target"),
			TEXT("walk_out_relay"), TEXT("Trigger"), 0.0f, nullptr },
		{ TEXT("walk-out (escort)"), TEXT("cinematic_shot_1"), TEXT("cinematic_shot_2"),
			nullptr, nullptr, 0.1f,
			TEXT("male_or_female is a logic_pythoncheck (`FindPlayer().IsMale()`); its field-6 "
				"payload needs the level-script namespace, so the pair is played through the "
				"PlayAsCamera* inputs both of its arms send") },
		{ TEXT("embrace"), TEXT("embrace_camera"), TEXT("embrace_target"),
			nullptr, nullptr, 1.0f,
			TEXT("the embrace pair's firer is an unnamed trigger_once brush with no body headless, "
				"so the pair is played through the PlayAsCamera* inputs it sends") },
	};

	const FTheatreGroup* LastGroup = nullptr;
	for (const FTheatreGroup& Group : Groups)
	{
		ElysiumCameraTrack::FPath PositionPath;
		ElysiumCameraTrack::FPath TargetPath;
		TArray<FString> PositionNames;
		TArray<FString> TargetNames;
		if (!TestTrue(*FString::Printf(TEXT("the %s chains compile"), Group.Label),
			BuildTheatreChain(Keys, KeysByName, Group.PositionRoot, PositionPath, PositionNames)
				&& BuildTheatreChain(Keys, KeysByName, Group.TargetRoot, TargetPath, TargetNames)))
		{
			return false;
		}
		if (Group.Seam != nullptr)
		{
			AddInfo(FString::Printf(TEXT("  seam (%s): %s"), Group.Label, Group.Seam));
		}

		// -- (b) part one: every folded edit is a ZERO-LENGTH segment on the chain's own clock, and
		//    sampling it switches to the destination key with nothing in between.
		auto AssertEdits = [this](const ElysiumCameraTrack::FPath& Path, const TCHAR* Label)
		{
			int32 Folded = 0;
			int32 ZeroLength = 0;
			int32 Isolated = 0;
			int32 CleanCuts = 0;
			for (int32 Index = 0; Index + 1 < Path.Points.Num(); ++Index)
			{
				if (!ElysiumCameraTrack::IsHardCut(Path.Points[Index]))
				{
					continue;
				}
				++Folded;
				ZeroLength += FMath::IsNearlyEqual(Path.Arrivals[Index + 1], Path.Departures[Index],
					KINDA_SMALL_NUMBER) ? 1 : 0;
				// Only an edit with a real dwell on both sides can be read in isolation; two edits
				// whose re-attributed pause was dropped land on the same instant, and the sampler
				// walks past both at once (which is itself the cut, just not a readable pair).
				const bool bIsolated = (Path.Departures[Index] - Path.Arrivals[Index]) > 0.005f
					&& (Path.Departures[Index + 1] - Path.Arrivals[Index + 1]) > 0.005f;
				if (!bIsolated)
				{
					continue;
				}
				++Isolated;
				ElysiumCameraTrack::FSample Before;
				ElysiumCameraTrack::FSample After;
				const float EditTime = Path.Departures[Index];
				const bool bSampled = Path.Sample(EditTime - 0.001f, Before)
					&& Path.Sample(EditTime, After);
				CleanCuts += (bSampled
					&& Before.Position.Equals(Path.Points[Index].Position, 0.01f)
					&& After.Position.Equals(Path.Points[Index + 1].Position, 0.01f)) ? 1 : 0;
			}
			AddInfo(FString::Printf(
				TEXT("  %s: %d folded edits, %d readable in isolation"), Label, Folded, Isolated));
			TestTrue(*FString::Printf(TEXT("%s authors at least one folded edit"), Label),
				Folded > 0);
			TestEqual(*FString::Printf(
				TEXT("%s: every folded edit is a zero-length segment, not a 30 ms slew"), Label),
				ZeroLength, Folded);
			TestEqual(*FString::Printf(
				TEXT("%s: every readable edit switches straight to its destination key"), Label),
				CleanCuts, Isolated);
			return Folded;
		};
		const int32 PositionEdits = AssertEdits(PositionPath,
			*FString::Printf(TEXT("%s position"), Group.Label));
		AssertEdits(TargetPath, *FString::Printf(TEXT("%s target"), Group.Label));

		// -- drive the authored entities ------------------------------------------------------
		const FElysiumEntity* PositionEntity = World.FindByName(Group.PositionRoot);
		if (!TestNotNull(*FString::Printf(TEXT("%s stands its position track"), Group.PositionRoot),
			PositionEntity))
		{
			return false;
		}
		const FElysiumEntityHandle PositionHandle = PositionEntity->Handle;

		Services.Calls.Reset();   // one chain group's worth of recording, so the counts below are its own

		if (Group.Firer != nullptr)
		{
			World.AcceptInput(Group.Firer, FName(Group.FirerInput), FElysiumVariant::Void(),
				FElysiumEntityHandle(), FElysiumEntityHandle());
		}
		else
		{
			// The firer's own row order: target first, position second.
			World.AcceptInput(Group.TargetRoot, FName(TEXT("PlayAsCameraTarget")),
				FElysiumVariant::Void(), FElysiumEntityHandle(), FElysiumEntityHandle());
			World.AcceptInput(Group.PositionRoot, FName(TEXT("PlayAsCameraPosition")),
				FElysiumVariant::Void(), FElysiumEntityHandle(), FElysiumEntityHandle());
		}

		double StartNow = -1.0;
		int32 Observed = 0;
		int32 TrackedTicks = 0;
		int32 OriginMisses = 0;
		int32 LookAtMisses = 0;
		int32 LensMisses = 0;
		int32 WeightMisses = 0;
		int32 ResidueMisses = 0;
		int32 CutTicks = 0;
		double WorstOrigin = 0.0;
		auto Observe = [&]()
		{
			if (StartNow < 0.0)
			{
				if (World.TrackCameraOwner(/*bTargetRole*/ false) != PositionHandle)
				{
					return;
				}
				StartNow = Now;
			}
			const float Elapsed = static_cast<float>(Now - StartNow);
			ElysiumCameraTrack::FSample PositionSample;
			ElysiumCameraTrack::FSample TargetSample;
			if (!PositionPath.Sample(Elapsed, PositionSample)
				|| !TargetPath.Sample(Elapsed, TargetSample))
			{
				return;
			}
			++Observed;

			const FElysiumCameraShot& Live = Services.LastCameraShot;
			// (a) The composed track shot is DIRECT. Retail applies this channel through `CInput`'s
			// view override, never through `C_BaseCineCamera`'s tracker, so nothing between the
			// authored sample and the published value can soften an edit into a pan.
			TrackedTicks += Live.bTracked ? 1 : 0;

			const double OriginError = FVector::Distance(Live.Origin, PositionSample.Position);
			WorstOrigin = FMath::Max(WorstOrigin, OriginError);
			OriginMisses += OriginError > 0.01 ? 1 : 0;
			LookAtMisses += (Live.bUseLookAt
				&& Live.LookAt.Equals(TargetSample.Position, 0.01f)) ? 0 : 1;
			LensMisses += FMath::IsNearlyEqual(Live.FieldOfView, PositionSample.FieldOfView, 0.01f)
				? 0 : 1;

			// (c) The SC3 channel underneath: `FromPlayerTime 0` on every theatre track means an
			// instantaneous arm, so the weight is pinned at 1 and the outgoing list is drained by
			// the same fold that consumed it. Anything else is drift.
			const FElysiumCameraOverrideChannel& Channel = World.CameraOverrideChannel();
			WeightMisses += FMath::IsNearlyEqual(Channel.PublishedState().Weight, 1.0f, 1.e-4f)
				? 0 : 1;
			ResidueMisses += Channel.OutgoingEntries().IsEmpty() ? 0 : 1;

			if (Observed > 1 && Live.bCameraCut)
			{
				++CutTicks;
			}
		};

		Observe();   // the direct path publishes on the tick the input was accepted on

		const double ChainSeconds = FMath::Max(PositionPath.EndTime, TargetPath.EndTime);
		const double Deadline = Now + static_cast<double>(Group.FireDelay) + ChainSeconds + 0.5;
		for (int32 Guard = 0; Guard < 200000 && Now < Deadline; ++Guard)
		{
			Now += GTheatreStep;
			World.Tick(Now);
			Observe();
		}

		AddInfo(FString::Printf(
			TEXT("  %s: %d observed ticks over %.2f s, %d of them carrying a hard cut ")
			TEXT("(%d folded position edits); worst published-origin error %.4f cm"),
			Group.Label, Observed, ChainSeconds, CutTicks, PositionEdits, WorstOrigin));

		TestTrue(*FString::Printf(TEXT("the %s pair actually played"), Group.Label), Observed > 100);
		// (a)
		TestEqual(*FString::Printf(
			TEXT("the composed %s shot is direct on every tick - retail's CInput override, not the "
				"C_BaseCineCamera tracker"), Group.Label), TrackedTicks, 0);
		// (b) part two: the live entities publish the folded path value for value, so the cuts the
		// path holds are the cuts the camera received.
		TestEqual(*FString::Printf(TEXT("the %s position stream publishes the folded path exactly"),
			Group.Label), OriginMisses, 0);
		TestEqual(*FString::Printf(TEXT("the %s target stream publishes the folded path exactly"),
			Group.Label), LookAtMisses, 0);
		TestEqual(*FString::Printf(TEXT("and the %s lens with it"), Group.Label), LensMisses, 0);
		TestTrue(*FString::Printf(TEXT("the %s chain reports its authored cuts to the camera"),
			Group.Label), CutTicks > 0);
		// (c)
		TestEqual(*FString::Printf(
			TEXT("the %s chain never drops the override channel below full weight"), Group.Label),
			WeightMisses, 0);
		TestEqual(*FString::Printf(
			TEXT("and the %s chain never leaves an outgoing crossfade entry behind"), Group.Label),
			ResidueMisses, 0);

		// (c) at rest: every theatre chain authors `HoldAtEnd 1`, so the last key is what the camera
		// is left sitting on. It must be the authored key exactly, with the channel clean under it.
		const FElysiumCameraOverrideChannel& Channel = World.CameraOverrideChannel();
		TestTrue(*FString::Printf(TEXT("the %s chain comes to rest on its final position key"),
			Group.Label),
			Services.LastCameraShot.Origin.Equals(PositionPath.Points.Last().Position, 0.01f));
		TestTrue(*FString::Printf(TEXT("looking at the %s chain's final target key"), Group.Label),
			Services.LastCameraShot.bUseLookAt
				&& Services.LastCameraShot.LookAt.Equals(TargetPath.Points.Last().Position, 0.01f));
		TestTrue(*FString::Printf(TEXT("with the %s channel still at weight 1"), Group.Label),
			FMath::IsNearlyEqual(Channel.PublishedState().Weight, 1.0f, 1.e-4f));
		TestTrue(*FString::Printf(TEXT("at full %s view coverage"), Group.Label),
			FMath::IsNearlyEqual(Channel.PublishedState().ViewCoverage, 1.0f, 1.e-4f));
		TestEqual(*FString::Printf(TEXT("and no %s crossfade residue at rest"), Group.Label),
			Channel.OutgoingEntries().Num(), 0);
		TestTrue(*FString::Printf(TEXT("the %s pair still owns the composed value shot"),
			Group.Label), World.HasTrackCamera());

		LastGroup = &Group;
	}

	// ------------------------------------------------------------------------------------------
	// (d) The restore is a CUT: the player view is back on the same tick, with nothing left armed.
	//
	// `sp_theatre` never fires `RestoreCameraToPlayerControl` itself — every chain authors
	// `HoldAtEnd 1` and the scene ends by travel — so the release asserted here is the entity's own
	// input carrying the authored `ToPlayerTime 0`, which is what a map that did fire it would send.
	// ------------------------------------------------------------------------------------------
	if (TestNotNull(TEXT("a chain group ran"), LastGroup))
	{
		Services.Calls.Reset();
		World.AcceptInput(LastGroup->PositionRoot, FName(TEXT("RestoreCameraToPlayerControl")),
			FElysiumVariant::Void(), FElysiumEntityHandle(), FElysiumEntityHandle());

		// No tick between the input and the assertions: this is the same tick.
		TestFalse(TEXT("the restore returns the camera to the player on the tick it arrives"),
			World.HasTrackCamera());
		TestEqual(TEXT("popping the composed value shot exactly once"),
			Services.Count(TEXT("PopCameraShot")), 1);
		TestTrue(TEXT("with no blend - the authored ToPlayerTime 0 is a cut, not a fade"),
			Services.Log().Contains(TEXT("blend=0.00")));
		TestFalse(TEXT("neither role is owned any more"),
			World.TrackCameraOwner(false).IsSet() || World.TrackCameraOwner(true).IsSet());
		TestTrue(TEXT("and the override channel is reaped outright, mark and stack together"),
			World.CameraOverrideChannel().IsClear());
	}

	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
