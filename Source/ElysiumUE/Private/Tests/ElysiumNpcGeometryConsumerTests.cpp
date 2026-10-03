#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

// 0018 story 6, lane C: the kernel's consumers of the geometry seam. Each case states its world
// through the recording doubles' knobs (`FElysiumRecordingServices::TraceRetailQuery`,
// `FElysiumRecordingNpcMotor::NavRaycastQuery`) and asserts what the retail body asks and decides:
//   - `KernelHullTrace` (`UTIL_TraceHull 0x1026e940` under the kernel filters, R1 §3): the seam's
//     character list filtered by MONSTER, BCC / hidden and slot 68 both ways, where
//     `m_bForceNPCCheck` (`+0x63da`) is what makes another NPC solid;
//   - `NavCanFitAtNode` (`CanFitAtNode 0x102f1900`, R1 §4);
//   - the wander's radial probe (`0x102ed610`, R1 §6) and its fail rule;
//   - the floor drop (`CAI_MoveProbe::TraceHull 0x102e7880`, the `CheckOnGround` rule).
// Kernel positions are SOURCE units in the port's axes; the seam is centimetres.

static constexpr EAutomationTestFlags GElysiumGeometryKernelFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr double GKernelU = ElysiumMove::U;
	constexpr int32 GMaskNpcSolid = 0x202400b;   // MASK_NPCSOLID: MONSTER is in it
	constexpr int32 GMaskNpc = 0x2400b;          // no MONSTER: no character is ever considered
}

// --- `KernelHullTrace`: the character list, kept or dropped ------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryKernelHullTraceTest,
	"Elysium.Arm.Geometry.Kernel.HullTraceCharacters", GElysiumGeometryKernelFlags)
bool FElysiumGeometryKernelHullTraceTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("geometry_kernel_hull_trace"), 6101);
	Builder.AddNpc(TEXT("guard"));
	Builder.AddNpc(TEXT("other"), FVector(200.0, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	FElysiumPlayer* Player = Fixture.Player();
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("other"), Other)
		|| !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard, Other });

	const FVector StartUnits(0.0, 0.0, 10.0);
	const FVector EndUnits(100.0, 0.0, 10.0);
	FElysiumNpcBase::FKernelHullTrace Trace;

	// No collision world: false, retail's clear defaults, `endpos` at the end.
	TestFalse(TEXT("a world with no collision answers nothing"),
		Guard->KernelHullTrace(StartUnits, EndUnits, FVector::ZeroVector, FVector::ZeroVector, GMaskNpcSolid,
			Trace));
	TestEqual(TEXT("and leaves the clear fraction"), Trace.Fraction, 1.0f);
	TestTrue(TEXT("with endpos at the end"), Trace.EndPosUnits.Equals(EndUnits, 1e-6));

	// The stated world: the world answer is `WorldFraction`, and the characters listed in `Listed`.
	FElysiumRetailTrace Asked;
	float WorldFraction = 1.0f;
	TArray<FElysiumRetailTraceCharacter> Listed;
	Fixture.Services.TraceRetailQuery = [&Asked, &WorldFraction, &Listed](const FElysiumRetailTrace& Asking,
		FElysiumRetailTraceResult& Out)
	{
		Asked = Asking;
		Out.Fraction = WorldFraction;
		Out.EndPosCm = Asking.StartCm + (Asking.EndCm - Asking.StartCm) * WorldFraction;
		for (const FElysiumRetailTraceCharacter& Character : Listed)
		{
			Out.Characters.Add(Character);
		}
		return true;
	};
	auto Run = [Guard, &StartUnits, &EndUnits](int32 Mask)
	{
		FElysiumNpcBase::FKernelHullTrace Out;
		Guard->KernelHullTrace(StartUnits, EndUnits, FVector::ZeroVector, FVector::ZeroVector, Mask, Out);
		return Out;
	};

	// The frame: Source units in the port's axes to centimetres; the box's Y pair mirrored.
	Trace = FElysiumNpcBase::FKernelHullTrace();
	TestTrue(TEXT("a stated world answers"),
		Guard->KernelHullTrace(StartUnits, EndUnits, FVector(-1.0, -2.0, -3.0), FVector(4.0, 5.0, 6.0),
			GMaskNpcSolid, Trace));
	TestTrue(TEXT("start in centimetres"), Asked.StartCm.Equals(StartUnits * GKernelU, 1e-4));
	TestTrue(TEXT("end in centimetres"), Asked.EndCm.Equals(EndUnits * GKernelU, 1e-4));
	TestTrue(TEXT("the box's Y pair is mirrored into the port's axes (mins)"),
		Asked.MinsCm.Equals(FVector(-1.0, -5.0, -3.0) * GKernelU, 1e-4));
	TestTrue(TEXT("(maxs)"), Asked.MaxsCm.Equals(FVector(4.0, 2.0, 6.0) * GKernelU, 1e-4));
	TestEqual(TEXT("the mask verbatim"), Asked.RetailMask, GMaskNpcSolid);
	TestTrue(TEXT("the tester is the pass entity"), Asked.Ignore.Contains(Guard->Handle));

	// Another NPC in the way, at 0.4, the world clear.
	FElysiumRetailTraceCharacter NpcHit;
	NpcHit.Entity = Other->Handle;
	NpcHit.Fraction = 0.4f;
	Listed = { NpcHit };

	// Plain: a non-sleeping NPC is solid to a tester without NAV_IGNORE_NPC (0x1029afc0's else arm).
	Trace = Run(GMaskNpcSolid);
	TestEqual(TEXT("an NPC in the way is folded in"), Trace.Fraction, 0.4f);
	TestTrue(TEXT("as the hit entity"), Trace.HitEntity == Other->Handle);
	TestTrue(TEXT("with endpos where it met it"),
		Trace.EndPosUnits.Equals(FVector(40.0, 0.0, 10.0), 1e-3));
	TestEqual(TEXT("0x101d3080: no MONSTER in the mask, no character"), Run(GMaskNpc).Fraction, 1.0f);

	// NAV_IGNORE_NPC: the tester's slot 68 ignores every NPC and the player -- unless
	// m_bForceNPCCheck is up, which skips that arm (`1029afcf JNZ`).
	Guard->NpcFlags.Set(EElysiumNpcFlag::NAV_IGNORE_NPC);
	TestEqual(TEXT("with NAV_IGNORE_NPC and no bracket the NPC is dropped"), Run(GMaskNpcSolid).Fraction, 1.0f);
	Guard->bForceNpcCheck = true;
	Trace = Run(GMaskNpcSolid);
	TestEqual(TEXT("under m_bForceNPCCheck it is kept"), Trace.Fraction, 0.4f);
	TestTrue(TEXT("and named"), Trace.HitEntity == Other->Handle);
	FElysiumRetailTraceCharacter PlayerHit;
	PlayerHit.Entity = Player->Handle;
	PlayerHit.Fraction = 0.3f;
	Listed = { PlayerHit };
	TestTrue(TEXT("the player too, under the bracket"), Run(GMaskNpcSolid).HitEntity == Player->Handle);
	Guard->bForceNpcCheck = false;
	TestEqual(TEXT("and dropped without it"), Run(GMaskNpcSolid).Fraction, 1.0f);
	Guard->NpcFlags.Clear(EElysiumNpcFlag::NAV_IGNORE_NPC);

	// Without NAV_IGNORE_NPC the tester ignores only a SLEEPING Troika NPC -- again only without the
	// bracket.
	Listed = { NpcHit };
	Other->NpcFlags.Set(EElysiumNpcFlag::SLEEPING);
	TestEqual(TEXT("a sleeping NPC is dropped"), Run(GMaskNpcSolid).Fraction, 1.0f);
	Guard->bForceNpcCheck = true;
	TestEqual(TEXT("and kept under m_bForceNPCCheck"), Run(GMaskNpcSolid).Fraction, 0.4f);
	Guard->bForceNpcCheck = false;
	Other->NpcFlags.Clear(EElysiumNpcFlag::SLEEPING);

	// `101d3284`: a script-hidden character is skipped, bracket or not.
	Other->bHidden = true;
	Guard->bForceNpcCheck = true;
	TestEqual(TEXT("a script-hidden NPC is never solid"), Run(GMaskNpcSolid).Fraction, 1.0f);
	Guard->bForceNpcCheck = false;
	Other->bHidden = false;

	// The nearest of the two: a world hit before the character wins; a start-solid character wins.
	WorldFraction = 0.2f;
	Trace = Run(GMaskNpcSolid);
	TestEqual(TEXT("a nearer world hit stands"), Trace.Fraction, 0.2f);
	TestFalse(TEXT("and names no character"), Trace.HitEntity == Other->Handle);
	NpcHit.Fraction = 0.f;
	NpcHit.bStartSolid = true;
	Listed = { NpcHit };
	Trace = Run(GMaskNpcSolid);
	TestEqual(TEXT("a character the trace starts inside is a hit at fraction 0"), Trace.Fraction, 0.0f);
	TestTrue(TEXT("and startsolid"), Trace.bStartSolid);
	TestTrue(TEXT("on it"), Trace.HitEntity == Other->Handle);

	Fixture.Services.TraceRetailQuery = nullptr;
	return true;
}

// --- `CanFitAtNode 0x102f1900` ----------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryKernelCanFitTest,
	"Elysium.Arm.Geometry.Kernel.CanFitAtNode", GElysiumGeometryKernelFlags)
bool FElysiumGeometryKernelCanFitTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("geometry_kernel_can_fit"), 6102);
	Builder.AddNpc(TEXT("guard"));
	const int32 Ground = Builder.AddPlace(2, FVector(300.0, 0.0, 0.0));
	const int32 Air = Builder.AddPlace(3, FVector(600.0, 0.0, 0.0));
	const int32 ClimbPlain = Builder.AddPlace(4, FVector(900.0, 0.0, 0.0));
	const int32 ClimbFlagged = Builder.AddPlace(4, FVector(1200.0, 0.0, 0.0));
	Builder.Places[ClimbFlagged].Flags = 0x4;   // one of `0x1d`
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	// The knob tells the two traces apart: the stand test drops (end below start), the fit trace
	// rises 0.01 (end above start).
	int32 StandTraces = 0;
	int32 FitTraces = 0;
	float StandFraction = 0.5f;
	bool bFitStartSolid = false;
	FElysiumRetailTrace FitAsked;
	Fixture.Services.TraceRetailQuery = [&StandTraces, &FitTraces, &StandFraction, &bFitStartSolid, &FitAsked](
		const FElysiumRetailTrace& Asking, FElysiumRetailTraceResult& Out)
	{
		if (Asking.EndCm.Z < Asking.StartCm.Z)
		{
			++StandTraces;
			Out.Fraction = StandFraction;
			return true;
		}
		++FitTraces;
		FitAsked = Asking;
		Out.bStartSolid = bFitStartSolid;
		Out.Fraction = bFitStartSolid ? 0.f : 1.f;
		return true;
	};

	TestTrue(TEXT("a ground node that stands and fits"), Guard->NavCanFitAtNode(Ground, 0, GMaskNpc));
	TestEqual(TEXT("was stand-tested once"), StandTraces, 1);
	TestEqual(TEXT("and fit-traced once"), FitTraces, 1);
	TestTrue(TEXT("0x102f1a20: the fit trace rises 0.01 (0x1044e658)"),
		FMath::IsNearlyEqual(FitAsked.EndCm.Z - FitAsked.StartCm.Z, 0.01 * GKernelU, 1e-4));
	TestTrue(TEXT("on the hull's FULL row (0x102d6100 / 0x102d6120)"),
		FitAsked.MaxsCm.Equals(FVector(13.0, 13.0, 72.0) * GKernelU, 1e-3));
	TestEqual(TEXT("under the caller's mask"), FitAsked.RetailMask, GMaskNpc);

	bFitStartSolid = true;
	TestFalse(TEXT("102f1c44: a fit trace that starts solid does not fit"),
		Guard->NavCanFitAtNode(Ground, 0, GMaskNpc));
	bFitStartSolid = false;

	StandTraces = 0;
	FitTraces = 0;
	StandFraction = 1.0f;
	TestFalse(TEXT("a ground node with no floor under it fails the stand test"),
		Guard->NavCanFitAtNode(Ground, 0, GMaskNpc));
	TestEqual(TEXT("and never reaches the fit trace"), FitTraces, 0);

	StandTraces = 0;
	TestTrue(TEXT("an air node is never stand-tested"), Guard->NavCanFitAtNode(Air, 0, GMaskNpc));
	TestEqual(TEXT("(no stand trace)"), StandTraces, 0);
	TestTrue(TEXT("nor a climb node without an 0x1d info bit"), Guard->NavCanFitAtNode(ClimbPlain, 0, GMaskNpc));
	TestEqual(TEXT("(still none)"), StandTraces, 0);
	TestFalse(TEXT("a climb node WITH one is, and here it has no floor"),
		Guard->NavCanFitAtNode(ClimbFlagged, 0, GMaskNpc));
	TestEqual(TEXT("(one stand trace)"), StandTraces, 1);
	TestFalse(TEXT("the stand test runs with no m_bForceNPCCheck bracket"), Guard->bForceNpcCheck);

	Fixture.Services.TraceRetailQuery = nullptr;
	return true;
}

// --- The wander's radial probe `0x102ed610` ---------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryKernelWanderProbeTest,
	"Elysium.Arm.Geometry.Kernel.WanderRadialProbe", GElysiumGeometryKernelFlags)
bool FElysiumGeometryKernelWanderProbeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("geometry_kernel_wander"), 6103);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	FElysiumEntityDef& Def = Builder.AddNpc(TEXT("guard"));
	Def.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder),
		[](FElysiumRecordingServices& Services) { Services.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestTrue(TEXT("a recording motor"), Fixture.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = Fixture.Services.NpcMotors[0].Get();

	// No NavMesh behind the motor: the probe refuses and `SetWanderGoal` falls to its pick.
	Guard->StartTaskNav.LastSearchMinUnits = 100.0f;
	TestFalse(TEXT("no NavMesh, no probe"), Guard->StartTaskWanderRadialProbe(0.0f, 300.0f));

	// Heading 0 from the origin, 300 units out; the mesh ends `ReachedUnits` along it.
	FElysiumNpcNavRaycast Asked;
	double ReachedUnits = 300.0;
	bool bHit = false;
	const FVector From = Guard->Origin;
	Motor->NavRaycastQuery = [&Asked, &ReachedUnits, &bHit, From](const FElysiumNpcNavRaycast& Query,
		FElysiumNpcNavRaycastAnswer& Out)
	{
		Asked = Query;
		Out.bHit = bHit;
		Out.HitCm = From + FVector(ReachedUnits * GKernelU, 0.0, 0.0);
		return true;
	};
	TestTrue(TEXT("a clear heading sets the goal"), Guard->StartTaskWanderRadialProbe(0.0f, 300.0f));
	TestTrue(TEXT("the ray starts at the origin"), Asked.FromCm.Equals(From, 1e-3));
	TestTrue(TEXT("and runs dist * dir"), Asked.ToCm.Equals(From + FVector(300.0 * GKernelU, 0.0, 0.0), 1e-3));
	TestEqual(TEXT("under the default filter"), Asked.PedestrianCostMultiplier, 0);
	TestEqual(TEXT("a type-4 goal"), Guard->StartTaskNav.LastGoal.Type, 4);
	TestTrue(TEXT("at the end"), Guard->StartTaskNav.LastGoal.DestCm.Equals(Asked.ToCm, 1e-3));
	TestEqual(TEXT("SetGoal flags 0"), Guard->StartTaskNav.LastSetGoalFlags, 0);

	// 102ed6df: blocked AND `dist - flDistObstructed <= minDist` fails. Reaching 90 of 300 leaves 210
	// obstructed: 300 - 210 = 90 <= 100.
	bHit = true;
	ReachedUnits = 90.0;
	TestFalse(TEXT("a heading that gets no further than minDist fails"),
		Guard->StartTaskWanderRadialProbe(0.0f, 300.0f));
	ReachedUnits = 150.0;
	TestTrue(TEXT("one that gets further sets the goal"), Guard->StartTaskWanderRadialProbe(0.0f, 300.0f));
	TestTrue(TEXT("at tr.vEndPosition, where the walk stopped"),
		Guard->StartTaskNav.LastGoal.DestCm.Equals(From + FVector(150.0 * GKernelU, 0.0, 0.0), 1e-3));

	Motor->NavRaycastQuery = nullptr;
	return true;
}

// --- The floor drop `0x102e7880` -------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryKernelFloorDropTest,
	"Elysium.Arm.Geometry.Kernel.FloorDrop", GElysiumGeometryKernelFlags)
bool FElysiumGeometryKernelFloorDropTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("geometry_kernel_floor_drop"), 6104);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Guard = Fixture.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });

	const FVector Spawned(10.0, 20.0, 100.0);   // Source units, the port's axes
	FVector At = Spawned;
	TestTrue(TEXT("no collision world: the found-floor arm, a zero-length drop"), Guard->MoveProbeFloorDrop(At));
	TestTrue(TEXT("the origin is left where it was"), At.Equals(Spawned, 1e-6));

	FElysiumRetailTrace Asked;
	float Fraction = 0.25f;
	bool bForcedDuringTrace = false;
	Fixture.Services.TraceRetailQuery = [&Asked, &Fraction, &bForcedDuringTrace, Guard](
		const FElysiumRetailTrace& Asking, FElysiumRetailTraceResult& Out)
	{
		Asked = Asking;
		bForcedDuringTrace = Guard->bForceNpcCheck;
		Out.Fraction = Fraction;
		Out.EndPosCm = Asking.StartCm + (Asking.EndCm - Asking.StartCm) * Fraction;
		return true;
	};
	At = Spawned;
	TestTrue(TEXT("a sweep that finds floor answers 1"), Guard->MoveProbeFloorDrop(At));
	TestTrue(TEXT("and moves the point to tr.endpos, 64 units down"), At.Equals(FVector(10.0, 20.0, 36.0), 1e-3));
	TestTrue(TEXT("the sweep drops 256"), Asked.EndCm.Equals(FVector(10.0, 20.0, 100.0 - 256.0) * GKernelU, 1e-3));
	TestTrue(TEXT("on the FULL collision box, no foot box"),
		Asked.MinsCm.Equals(FVector(-13.0, -13.0, 0.0) * GKernelU, 1e-3)
			&& Asked.MaxsCm.Equals(FVector(13.0, 13.0, 72.0) * GKernelU, 1e-3));
	TestEqual(TEXT("mask 0x202400b"), Asked.RetailMask, GMaskNpcSolid);
	TestFalse(TEXT("no m_bForceNPCCheck bracket"), bForcedDuringTrace);
	Fraction = 1.0f;
	At = Spawned;
	TestFalse(TEXT("no floor within 256: \"stuck in wall\""), Guard->MoveProbeFloorDrop(At));
	TestTrue(TEXT("and the point is not moved"), At.Equals(Spawned, 1e-6));

	Fixture.Services.TraceRetailQuery = nullptr;
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
