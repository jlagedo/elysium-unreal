#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Tests/ElysiumTestServices.h"
#include "Misc/ScopeExit.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumV6PlaceTests
{
static constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
class FVisitor : public FElysiumNpc
{
public:
	virtual void Spawn() override {}
	virtual void Activate() override {}
	virtual void OnRuntimeModelChanged() override {}
	void HoldPlace(int32 PlaceIndex) { CurrentSpotIndex = PlaceIndex; bAmbientArrived = true; }
	int32 HeldPlace() const { return CurrentSpotIndex; }
	bool ArrivedLatch() const { return bAmbientArrived; }
};
static TUniquePtr<FElysiumEntity> MakeVisitor() { return MakeUnique<FVisitor>(); }
static FElysiumClassRegistrar Registration(TEXT("__v6_place_visitor"), TEXT("CAI_BaseNPCTroika"), &MakeVisitor,
	[](FElysiumClassDesc&) {});
static FElysiumEntityDefs Defs()
{
	FElysiumEntityDefs Result; Result.MapName = TEXT("__v6_place__");
	for (const TCHAR* VisitorName : {TEXT("visitorA"), TEXT("visitorB")})
	{
		FElysiumEntityDef Visitor; Visitor.Classname = TEXT("__v6_place_visitor"); Visitor.TargetName = VisitorName; Result.Defs.Add(Visitor);
	}
	for (const TCHAR* PlaceName : {TEXT("placeA"), TEXT("placeB")})
	{
		FElysiumEntityDef Place; Place.Classname = TEXT("intersting_place"); Place.TargetName = PlaceName;
		Place.Keys.Add(TEXT("max_npcs"), TEXT("3"));
		Place.Keys.Add(TEXT("min_bounds"), TEXT("-100 -100 0")); Place.Keys.Add(TEXT("max_bounds"), TEXT("100 100 0"));
		Result.Defs.Add(Place);
	}
	return Result;
}
static FVisitor* Visitor(FElysiumEntityWorld& World, const TCHAR* Name = TEXT("visitorA")) { return static_cast<FVisitor*>(World.FindByName(Name)); }
static FElysiumInterestingPlace* Place(FElysiumEntityWorld& World, const TCHAR* Name = TEXT("placeA")) { return static_cast<FElysiumInterestingPlace*>(World.FindByName(Name)); }
static void ClearTrace(FElysiumRecordingServices& Services)
{
	Services.TraceRetailQuery = [](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Result)
	{
		Result.Fraction = 1; Result.bStartSolid = Result.bAllSolid = false; return true; // real stationary-hull seam, 0x102a0fb0
	};
}
static bool Codec(FElysiumMapSnapshot& Snapshot, FElysiumMapSnapshot& Decoded)
{
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes, true); Writer << Snapshot;
	FMemoryReader Reader(Bytes, true); Reader << Decoded;
	return !Writer.IsError() && !Reader.IsError();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6PlaceRowsTest, "Elysium.Arm.V6.PlaceRestore.RowsRebaseReleaseAndLastScan", ElysiumV6PlaceTests::Flags)
bool FElysiumV6PlaceRowsTest::RunTest(const FString&)
{
	using namespace ElysiumV6PlaceTests;
	TArray<ElysiumRng::FState> PriorRng; ElysiumRng::Snapshot(PriorRng); ON_SCOPE_EXIT { ElysiumRng::Restore(PriorRng); };
	FElysiumRecordingServices Services; ClearTrace(Services);
	FElysiumEntityWorld Source(nullptr, nullptr, Services.Bundle()); Source.Load(Defs()); Source.Activate(20);
	FVisitor* First = Visitor(Source); FVisitor* Second = Visitor(Source, TEXT("visitorB"));
	FElysiumInterestingPlace* Older = Place(Source); FElysiumInterestingPlace* Newer = Place(Source, TEXT("placeB"));
	if (!TestNotNull(TEXT("place"), Older) || !TestNotNull(TEXT("visitor"), First)) return false;
	ElysiumSchedule::Start(First->Schedule, ElysiumSched::IDLE_STAND, *First);
	ElysiumSchedule::Start(Second->Schedule, ElysiumSched::IDLE_STAND, *Second);
	FVector FirstSpot, SecondSpot, DuplicateSpot;
	if (!TestTrue(TEXT("sampler writes reservation before arrival"), Older->PickSpotFor(*First, FirstSpot))) return false;
	First->HoldPlace(Older->Handle.Index); First->InterestingPlacePosition = FirstSpot;
	TestTrue(TEXT("ClaimMarker uses existing row"), Older->Claim(First->Handle));
	// Separate far authored origin gives a nonoverlapping second reservation and distinct bounds.
	Older->Origin.X = 1000;
	if (!TestTrue(TEXT("second row"), Older->PickSpotFor(*Second, SecondSpot))) return false;
	const FVector LastMin = Older->Markers[1].MinBoundsCm, LastMax = Older->Markers[1].MaxBoundsCm;
	TestTrue(TEXT("second claim"), Older->Claim(Second->Handle));
	if (!TestTrue(TEXT("duplicate holding place for LAST linked-list scan"), Newer->PickSpotFor(*First, DuplicateSpot))) return false;
	TestEqual(TEXT("HEAD insertion plus LAST match chooses older"), First->FindInterestingPlaceHoldingMe(), Older->Handle.Index); // 0x102db5e0
	FElysiumMapSnapshot Snapshot, Decoded; Source.Freeze(Snapshot);
	if (!TestTrue(TEXT("normal marker codec"), Codec(Snapshot, Decoded))) return false;
	FElysiumEntityWorld Restored(nullptr, nullptr, Services.Bundle()); Restored.Load(Defs());
	if (!TestTrue(TEXT("common apply after all row decode/fixup"), Restored.ApplySnapshot(Decoded, 100) != INDEX_NONE)) return false;
	FVisitor* Resumed = Visitor(Restored); FElysiumInterestingPlace* SavedPlace = Place(Restored);
	TestEqual(TEXT("allocation"), SavedPlace->MarkersAllocated, 3);
	TestEqual(TEXT("used rows"), SavedPlace->MarkersUsed, 2);
	TestTrue(TEXT("marker handles use restored epoch before NPC scan"), SavedPlace->HasMarker(Resumed->Handle));
	TestEqual(TEXT("LAST scan survives restore"), Resumed->HeldPlace(), SavedPlace->Handle.Index);
	TestEqual(TEXT("in-use is separate unsaved word, no post-load Claim replay"), SavedPlace->InUse, 0);
	SavedPlace->Release(Resumed->Handle); // 0x102da600 real full-row swap
	TestEqual(TEXT("swap occupant"), SavedPlace->Markers[0].Occupant, Visitor(Restored, TEXT("visitorB"))->Handle);
	TestTrue(TEXT("swap lower POSITION"), SavedPlace->Markers[0].MinBoundsCm.Equals(LastMin));
	TestTrue(TEXT("swap upper POSITION"), SavedPlace->Markers[0].MaxBoundsCm.Equals(LastMax));
	TestFalse(TEXT("spare cleared"), SavedPlace->Markers[1].Occupant.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6PlaceSamplerTest, "Elysium.Arm.V6.PlaceRestore.CapacityOverlapAndClearance", ElysiumV6PlaceTests::Flags)
bool FElysiumV6PlaceSamplerTest::RunTest(const FString&)
{
	using namespace ElysiumV6PlaceTests;
	TArray<ElysiumRng::FState> PriorRng; ElysiumRng::Snapshot(PriorRng); ON_SCOPE_EXIT { ElysiumRng::Restore(PriorRng); };
	FElysiumRecordingServices Services; ClearTrace(Services);
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle()); World.Load(Defs()); World.Activate(20);
	FVisitor* Npc = Visitor(World); FElysiumInterestingPlace* Spot = Place(World); FVector Sample;
	Spot->MarkersAllocated = 0; TestFalse(TEXT("raw capacity0 refuses; no floor-to1"), Spot->PickSpotFor(*Npc, Sample));
	Spot->MarkersAllocated = 3; Spot->bEnabled = false; TestFalse(TEXT("disabled refuses"), Spot->PickSpotFor(*Npc, Sample)); Spot->bEnabled = true;
	Spot->MarkersUsed = 3; TestFalse(TEXT("used rows consume capacity"), Spot->PickSpotFor(*Npc, Sample)); Spot->MarkersUsed = 0;
	int32 HullCalls = 0;
	Services.TraceRetailQuery = [&HullCalls](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Result)
	{
		++HullCalls; Result.Fraction = HullCalls == 1 ? 0.f : 1.f; Result.bStartSolid = Result.bAllSolid = false; return true;
	};
	TestTrue(TEXT("clear second attempt still reserves"), Spot->PickSpotFor(*Npc, Sample)); // 0x102da435 warns on second clear too
	TestEqual(TEXT("two clearance attempts"), HullCalls, 2);
	TestEqual(TEXT("clearance failure never debits occupied counter"), Spot->FailedAttempts, 0);
	TestEqual(TEXT("failed-box expiry now+2"), Spot->FailedBoxes[0].Until, 22.0);
	TestEqual(TEXT("one marker before claim"), Spot->MarkersUsed, 1);
	TestEqual(TEXT("in-use remains0 until arrival"), Spot->InUse, 0);
	Spot->Release(Npc->Handle);
	Services.TraceRetailQuery = [](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Result)
	{ Result.Fraction = 0; return true; };
	TestTrue(TEXT("exhausted clearance still adds marker"), Spot->PickSpotFor(*Npc, Sample)); // shipped 0x102da4bb arm
	TestEqual(TEXT("both failed bounds recorded"), Spot->FailedBoxCursor, 3);
	Spot->Release(Npc->Handle);
	// Fixed XY sampler at origin, and a row below the feet: only shipped mins.X-for-Z bug overlaps it.
	FVector HullMin, HullMax; FElysiumNpcBase::RetailCollisionExtents(*Npc, HullMin, HullMax);
	HullMin.X -= 1; HullMin.Y -= 1; HullMax.X += 1; HullMax.Y += 1;
	Spot->MinBoundsUnits = HullMin; Spot->MaxBoundsUnits = HullMax;
	Spot->MarkersUsed = 1; Spot->Markers[0].Occupant = Visitor(World, TEXT("visitorB"))->Handle;
	Spot->Markers[0].MinBoundsCm = FVector(-1, -1, -20) * ElysiumMove::U;
	Spot->Markers[0].MaxBoundsCm = FVector(1, 1, -10) * ElysiumMove::U;
	FRandomStream ExpectedDraw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	for (int32 DrawIndex = 0; DrawIndex < 20; ++DrawIndex) ExpectedDraw.FRandRange(0, 0); // initial XY + nine replacement XY draws
	TestFalse(TEXT("ninth occupied replacement refuses"), Spot->PickSpotFor(*Npc, Sample));
	TestEqual(TEXT("one failed-attempt debit"), Spot->FailedAttempts, 1);
	TestEqual(TEXT("draw order preserves XY only and ninth replacement"), ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed(), ExpectedDraw.GetCurrentSeed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6PlaceInvalidTest, "Elysium.Arm.V6.PlaceRestore.BothConsistencyRefusals", ElysiumV6PlaceTests::Flags)
bool FElysiumV6PlaceInvalidTest::RunTest(const FString&)
{
	using namespace ElysiumV6PlaceTests;
	FElysiumRecordingServices Services; ClearTrace(Services); FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle()); World.Load(Defs()); World.Activate(20);
	FVisitor* Npc = Visitor(World); FElysiumInterestingPlace* Spot = Place(World);
	Npc->HoldPlace(Spot->Handle.Index); const int32 Before = Npc->RestorePlaceRejections;
	Npc->FinishAmbientUse(false); // 0x102b53d0 consistency-before-release
	TestEqual(TEXT("live place without occupant refuses"), Npc->RestorePlaceRejections, Before + 1);
	TestEqual(TEXT("clears held pointer"), Npc->HeldPlace(), INDEX_NONE);
	TestTrue(TEXT("invalid release early return preserves arrived"), Npc->ArrivedLatch());
	Npc->HoldPlace(999999); Npc->ValidateRestoredInterestingPlace();
	TestEqual(TEXT("missing list membership also refuses"), Npc->RestorePlaceRejections, Before + 2);
	TestEqual(TEXT("missing place pointer cleared"), Npc->HeldPlace(), INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6PlaceSharedCounterTest, "Elysium.Arm.V6.PlaceRestore.SharedOccupiedBudget", ElysiumV6PlaceTests::Flags)
bool FElysiumV6PlaceSharedCounterTest::RunTest(const FString&)
{
	using namespace ElysiumV6PlaceTests;
	TArray<ElysiumRng::FState> PriorRng; ElysiumRng::Snapshot(PriorRng); ON_SCOPE_EXIT { ElysiumRng::Restore(PriorRng); };
	// Select a deterministic fixture stream: first three XY samples occupied, fourth clear,
	// then seven occupied. Independent candidate streams never draw from the shared game stream.
	int32 FixtureSeed = INDEX_NONE;
	for (int32 CandidateSeed = 1; CandidateSeed <= 200000 && FixtureSeed == INDEX_NONE; ++CandidateSeed)
	{
		FRandomStream CandidateStream(CandidateSeed); bool bMatches = true;
		for (int32 SampleIndex = 0; SampleIndex < 11; ++SampleIndex)
		{
			const float X = CandidateStream.FRandRange(0, 1000); CandidateStream.FRandRange(0, 0);
			if ((SampleIndex == 3 && X <= 600) || (SampleIndex != 3 && X >= 400)) { bMatches = false; break; }
		}
		if (bMatches) FixtureSeed = CandidateSeed;
	}
	if (!TestTrue(TEXT("fixture stream found"), FixtureSeed != INDEX_NONE)) return false;
	FElysiumRecordingServices Services; int32 ClearanceCalls = 0;
	Services.TraceRetailQuery = [&ClearanceCalls](const FElysiumRetailTrace&, FElysiumRetailTraceResult& Result)
	{ ++ClearanceCalls; Result.Fraction = 0; return true; };
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle()); World.Load(Defs()); World.Activate(20);
	FVisitor* Npc = Visitor(World); FElysiumInterestingPlace* Spot = Place(World);
	FVector HullMin, HullMax; FElysiumNpcBase::RetailCollisionExtents(*Npc, HullMin, HullMax);
	HullMin.X -= 1; HullMin.Y -= 1; HullMax.X += 1; HullMax.Y += 1;
	Spot->MinBoundsUnits = HullMin; Spot->MaxBoundsUnits = HullMax + FVector(1000, 0, 0);
	Spot->MarkersUsed = 1; Spot->Markers[0].Occupant = Visitor(World, TEXT("visitorB"))->Handle;
	Spot->Markers[0].MinBoundsCm = FVector(-100, -100, -100) * ElysiumMove::U;
	Spot->Markers[0].MaxBoundsCm = FVector(500, 100, 1000) * ElysiumMove::U;
	ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).Initialize(FixtureSeed);
	FRandomStream ExpectedStream(FixtureSeed);
	for (int32 SampleIndex = 0; SampleIndex < 11; ++SampleIndex) { ExpectedStream.FRandRange(0, 1000); ExpectedStream.FRandRange(0, 0); }
	FVector Sample; TestFalse(TEXT("occupied budget survives first clearance failure"), Spot->PickSpotFor(*Npc, Sample)); // 0x102da0d0
	TestEqual(TEXT("second attempt refuses after six further replacements, total9"), ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed(), ExpectedStream.GetCurrentSeed());
	TestEqual(TEXT("only first clearance reached"), ClearanceCalls, 1);
	TestEqual(TEXT("one failure debit"), Spot->FailedAttempts, 1);
	TestEqual(TEXT("only hull failure wrote ring"), Spot->FailedBoxCursor, 1);
	TestEqual(TEXT("no new marker on occupied refusal"), Spot->MarkersUsed, 1);
	return true;
}
#endif
