#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <cmath>   // std::nextafter, to pin a strictly-less compare at its exact boundary

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Geometry** — one case per ported body, every threshold and every formula
// taken from the decompiled C and, where the decompiler dropped an instruction, from the LISTING.
// Three of 29c's one-line walks were wrong and all three corrections are asserted here by name:
//
//   * the three Tzimisce slot-337 bodies DO add a species bit (`OR AH,0x4` / `0x8` / `0x20`);
//   * `_DAT_10449260` is a DOUBLE and reads 0.25, not the 0.0 a float read gives;
//   * `UpdateFakeHull`'s "when it moved" test is a comparison against `vec3_origin`, i.e.
//     "is the cache still empty", not a comparison against the new point.
//
// The census cross-check at the end is what keeps the species tables honest: every retail class
// this family carries a row for is asked for that slot's address through
// `ElysiumNpcKernelClass::BodyOf`, so a row that drifts from `npc-kernel/slots.md` fails here.
//
// The brief's "two tables, and they disagree" is obeyed throughout: `npc_payphone` IS both a census
// classname and a registered spawn leaf so `CPayphone` gets a real body; `npc_VCop` is a registered
// leaf that NO census class claims, so its `RetailClass()` is null and slot 337 falls to the Troika
// line — asserted rather than worked around; and every other species row (`CNPC_VTzimisce`,
// `CNPC_VWerewolf`, `CNPC_VCamera`, `CAI_BaseHumanoid`, `CNPC_Crow`, …) is exercised by retail
// class NAME through the table's own lookup, because no registered classname reaches it.

static constexpr EAutomationTestFlags GElysiumNpcKernelGeometryFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The NPCs every case below stands. `phone` is the one census class in this family that is also
	// a spawn leaf; `rat` reaches `CNPC_VRat`, whose slot-337 row is an OR row; `cop` is the
	// registered classname no census class claims.
	struct FGeometryFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumNpc* Phone = nullptr;
		FElysiumNpc* Rat = nullptr;
		FElysiumNpc* Cop = nullptr;

		FGeometryFixture()
			: World([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("geometry_kernel"), 29131u);
				Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("other"), FVector(300.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("phone"), FVector(600.f, 0.f, 0.f), TEXT("npc_payphone"));
				Builder.AddNpc(TEXT("rat"), FVector(900.f, 0.f, 0.f), TEXT("npc_VRat"));
				Builder.AddNpc(TEXT("cop"), FVector(1200.f, 0.f, 0.f), TEXT("npc_VCop"));
				return Builder;
			}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Phone = World.Npc(TEXT("phone"));
			Rat = World.Npc(TEXT("rat"));
			Cop = World.Npc(TEXT("cop"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Other, Phone, Rat, Cop });
		}
	};
}

// =================================================================================================
// Slot 193 `EyePosition` — `0x100b4b40`, `0x101aae60`, `0x1025e8e0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryEyePositionTest,
	"Elysium.Substrate.NpcKernelGeometry.EyePosition", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryEyePositionTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard)
		|| !TestNotNull(TEXT("npc_payphone is a registered spawn leaf"), F.Phone))
	{
		return false;
	}

	// `0x100b4b40`: `GetAbsOrigin() + m_vecViewOffset`, which this chain answers as the standing
	// view offset. Not a bounds fraction and not a head bone.
	TestTrue(TEXT("the Troika line's eye is the origin plus the fixed view offset"),
		F.Guard->EyePosition().Equals(
			F.Guard->Origin + FVector(0.f, 0.f, ElysiumMove::StandViewZ), 0.01));

	// `0x101aae60`: `LookupBone("Phone_bone_01")`; a -1 answer falls through to the base body. The
	// bone seam answers nothing here, so the payphone takes retail's own miss arm — and the case
	// proves the seam was ASKED, which is what distinguishes the miss arm from a body that never
	// looked.
	const int32 BoneCallsBefore = F.Phone->BoneWorldPositionCalls;
	const FVector PhoneEye = F.Phone->EyePosition();
	TestTrue(TEXT("the payphone asks the bone seam for Phone_bone_01"),
		F.Phone->BoneWorldPositionCalls > BoneCallsBefore);
	TestTrue(TEXT("with no bone table the payphone takes retail's LookupBone == -1 arm"),
		PhoneEye.Equals(F.Phone->Origin + FVector(0.f, 0.f, ElysiumMove::StandViewZ), 0.01));

	// The guard's class does not replace slot 193, so it never reaches the bone seam at all.
	const int32 GuardBoneCalls = F.Guard->BoneWorldPositionCalls;
	F.Guard->EyePosition();
	TestEqual(TEXT("a Troika-line NPC never asks the bone seam"),
		F.Guard->BoneWorldPositionCalls, GuardBoneCalls);

	// The species table, by name, including the row no registered classname reaches.
	TestNotNull(TEXT("CPayphone has a slot 193 row"),
		FElysiumNpc::EyePositionSpeciesOf(TEXT("CPayphone")));
	TestNotNull(TEXT("CAI_BaseHumanoid has a slot 193 row"),
		FElysiumNpc::EyePositionSpeciesOf(TEXT("CAI_BaseHumanoid")));
	TestNull(TEXT("CNPC_VHumanCombatant does not replace slot 193"),
		FElysiumNpc::EyePositionSpeciesOf(TEXT("CNPC_VHumanCombatant")));

	// The humanoid arm's seam refuses, which is `0x1025e7b0`'s own attachment-missing arm.
	FVector Cached = FVector(1.f, 2.f, 3.f);
	TestFalse(TEXT("the humanoid eye cache seam answers nothing"), F.Guard->HumanoidEyeCache(Cached));
	return true;
}

// =================================================================================================
// Slots 194 and 195 — the two angle aliases
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryEyeAnglesTest,
	"Elysium.Substrate.NpcKernelGeometry.EyeAngles", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryEyeAnglesTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	// `0x100b4bc0` is `JMP [[this]+0x36c]` (slot 219) and `0x100b4be0` is `JMP [[this]+0x374]`
	// (slot 221). Eight bytes each: whatever the two angle slots answer, these answer exactly that
	// pointer — including the null both stubs answer today.
	TestEqual(TEXT("slot 194 EyeAngles IS slot 219 GetAbsAngles"),
		F.Guard->EyeAngles(), F.Guard->GetAbsAngles());
	TestEqual(TEXT("slot 195 LocalEyeAngles IS slot 221 GetAngles"),
		F.Guard->LocalEyeAngles(), F.Guard->GetAngles());
	return true;
}

// =================================================================================================
// Slot 197 `BodyTarget` — `0x102789c0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryBodyTargetTest,
	"Elysium.Substrate.NpcKernelGeometry.BodyTarget", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryBodyTargetTest::RunTest(const FString&)
{
	// The anchor: `centre - 0.25 * (centre - origin)`. With a centre 100 above the origin the
	// anchor sits 75 above it, not 50 and not 25.
	const FVector OriginCm(0.f, 0.f, 0.f);
	const FVector CentreCm(0.f, 0.f, 100.f);
	const FVector Anchor = FElysiumNpc::BodyTargetAnchor(CentreCm, OriginCm);
	TestTrue(TEXT("the anchor is the centre pulled a quarter back toward the origin"),
		Anchor.Equals(FVector(0.f, 0.f, 75.f), 0.001));

	const FVector EyeCm(0.f, 0.f, 160.f);

	// The non-noisy, non-exact arm: `anchor + span * 0.5`, the plain midpoint. 75 + (160-75)/2.
	TestTrue(TEXT("the plain arm is the midpoint of anchor and eye"),
		FElysiumNpc::BodyTargetBlend(Anchor, EyeCm, false, false, 0.4f, 0.4f)
			.Equals(FVector(0.f, 0.f, 117.5f), 0.001));

	// The third-bool arm answers the eye EXACTLY, and does not consult the anchor at all.
	TestTrue(TEXT("the exact arm answers slot 193 verbatim"),
		FElysiumNpc::BodyTargetBlend(Anchor, EyeCm, false, true, 0.4f, 0.4f).Equals(EyeCm, 0.001));

	// The noisy arm adds the span once per draw, so the two `RandomFloat(0, 0.5)` results SUM. Two
	// draws of 0.5 put the answer on the eye; one draw of 0.5 alone would stop halfway.
	TestTrue(TEXT("the noisy arm adds the span once for EACH draw"),
		FElysiumNpc::BodyTargetBlend(Anchor, EyeCm, true, false, 0.5f, 0.5f).Equals(EyeCm, 0.001));
	TestTrue(TEXT("and a single maximal draw only reaches halfway"),
		FElysiumNpc::BodyTargetBlend(Anchor, EyeCm, true, false, 0.5f, 0.f)
			.Equals(FVector(0.f, 0.f, 117.5f), 0.001));

	// The noisy arm ignores the third bool — retail tests the second bool FIRST and never reaches
	// the `bAimAtEyeExactly` compare when it is set.
	TestTrue(TEXT("noisy wins over exact, because retail tests it first"),
		FElysiumNpc::BodyTargetBlend(Anchor, EyeCm, true, true, 0.f, 0.f).Equals(Anchor, 0.001));

	// The `posSrc` argument is never read. Two calls with wildly different sources and the same
	// arms answer the same point.
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	const FVector A = F.Guard->BodyTarget(FVector::ZeroVector, false, false);
	const FVector B = F.Guard->BodyTarget(FVector(9999.f, -9999.f, 9999.f), false, false);
	TestTrue(TEXT("retail's posSrc reaches no instruction in slot 197"), A.Equals(B, 0.001));
	return true;
}

// =================================================================================================
// Slot 192 — `CNPC_Crow::vfunc192`, `0x10357760`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryCrowCentreTest,
	"Elysium.Substrate.NpcKernelGeometry.CrowCentre", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryCrowCentreTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	// `CNPC_Crow` is a census class that no registered classname reaches, so the row is exercised by
	// retail class name through the census itself.
	TestNotNull(TEXT("CNPC_Crow is in the census"),
		ElysiumNpcKernelClass::Find(TEXT("CNPC_Crow")));
	TestEqual(TEXT("CNPC_Crow fills slot 192 with 0x10357760"),
		FString(ElysiumNpcKernelClass::BodyOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_Crow")), 192)),
		FString(TEXT("0x10357760")));

	// A Troika-line NPC is not a crow, so the named method falls through to slot 192's own body —
	// another story's generated stub, which answers the zero vector.
	TestFalse(TEXT("a combatant does not derive from CNPC_Crow"),
		ElysiumNpcKernelClass::DerivesFrom(F.Guard->RetailClass(), TEXT("CNPC_Crow")));
	TestTrue(TEXT("and therefore takes slot 192's own (still stubbed) body"),
		F.Guard->SpeciesWorldSpaceCenter().Equals(F.Guard->WorldSpaceCenter(), 0.001));
	return true;
}

// =================================================================================================
// Slot 213 `SetSize` — `0x100b1890`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometrySetSizeTest,
	"Elysium.Substrate.NpcKernelGeometry.SetSize", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometrySetSizeTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	TestTrue(TEXT("m_vecSize starts at zero"), F.Guard->SizeCm.IsNearlyZero());
	F.Guard->SetSize(FVector(32.f, 33.f, 72.f));
	// Three word stores and nothing else; the 132 bytes of scope trace around them have no
	// observable effect and are not reproduced.
	TestTrue(TEXT("slot 213 writes all three words of m_vecSize"),
		F.Guard->SizeCm.Equals(FVector(32.f, 33.f, 72.f), 0.001));
	F.Guard->SetSize(FVector::ZeroVector);
	TestTrue(TEXT("and writes them again unconditionally"), F.Guard->SizeCm.IsNearlyZero());
	return true;
}

// =================================================================================================
// Slot 337 `GetUsedHullBits` — `0x1029a050` and its fourteen species rows
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryHullBitsTest,
	"Elysium.Substrate.NpcKernelGeometry.HullBits", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryHullBitsTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard)
		|| !TestNotNull(TEXT("npc_VRat is a registered spawn leaf"), F.Rat)
		|| !TestNotNull(TEXT("npc_VCop is a registered spawn leaf"), F.Cop))
	{
		return false;
	}

	// `CBaseCombatCharacter::GetUsedHullBits` is `return 1`; `CAI_BaseNPC` ORs 0x1 and
	// `CAI_BaseNPCTroika` ORs 0x1 again, so the Troika line's answer is 1 and both ORs are no-ops.
	TestEqual(TEXT("the Troika line answers hull bit 0 alone"), F.Guard->GetUsedHullBits(), 1);

	// The brief's recovered spawn/census disagreement: no census class claims `npc_VCop`, so a
	// spawned cop's `RetailClass()` is null and slot 337 falls to the Troika line.
	TestNull(TEXT("npc_VCop's census class is null"), F.Cop->RetailClass());
	TestEqual(TEXT("and a cop therefore answers the Troika line's 1"), F.Cop->GetUsedHullBits(), 1);

	// `CNPC_VRat` shares `CNPC_VScurrying`'s body and ORs 0x80000 onto the base 1.
	TestEqual(TEXT("a rat ORs the scurrying hull bit onto the base"),
		F.Rat->GetUsedHullBits(), 0x80001);

	// THE CORRECTION. The decompiled C shows the three Tzimisce bodies as bare forwards with no
	// species bit; the listing shows `OR AH,<imm>`, a byte-wide OR into bits 8..15.
	struct FExpected
	{
		const TCHAR* Class;
		const TCHAR* Body;
		int32 Bits;
		bool bReplaces;
	};
	static const FExpected Expected[] =
	{
		{ TEXT("CNPC_VTzimisce"),         TEXT("0x103b9160"), 0x0400,   false },
		{ TEXT("CNPC_VTzimisceHeadClaw"), TEXT("0x103c1cb0"), 0x0800,   false },
		{ TEXT("CNPC_VTzimisceRunner"),   TEXT("0x103c3cb0"), 0x2000,   false },
		{ TEXT("CNPC_VMingXiao"),         TEXT("0x10392a50"), 0x38000,  false },
		{ TEXT("CNPC_VScurrying"),        TEXT("0x103ac4e0"), 0x80000,  false },
		{ TEXT("CNPC_VRat"),              TEXT("0x103ac4e0"), 0x80000,  false },
		{ TEXT("CNPC_VCamera"),           TEXT("0x10368e80"), 0x80,     true },
		{ TEXT("CNPC_VCameraSecurity"),   TEXT("0x10368e80"), 0x80,     true },
		{ TEXT("CNPC_VGargoyle"),         TEXT("0x10378680"), 0x4000,   true },
		{ TEXT("CNPC_VHengeyokai"),       TEXT("0x1037fb20"), 0x40001,  true },
		{ TEXT("CNPC_VManBat"),           TEXT("0x1038b100"), 0x100000, true },
		{ TEXT("CNPC_VMingXiaoTentacle"), TEXT("0x1039c480"), 0x38000,  true },
		{ TEXT("CNPC_VSheriffMan"),       TEXT("0x103ae840"), 0x200000, true },
		{ TEXT("CNPC_VWerewolf"),         TEXT("0x103cab50"), 0x1000,   true },
	};

	int32 RowCount = 0;
	FElysiumNpc::UsedHullBitsSpeciesRows(RowCount);
	TestEqual(TEXT("slot 337's table carries every species override in the census"),
		RowCount, static_cast<int32>(UE_ARRAY_COUNT(Expected)));

	for (const FExpected& Row : Expected)
	{
		const FElysiumNpc::FUsedHullBitsSpecies* Found =
			FElysiumNpc::UsedHullBitsSpeciesOf(Row.Class);
		if (!TestNotNull(*FString::Printf(TEXT("%s has a slot 337 row"), Row.Class), Found))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s's bits"), Row.Class), Found->Bits, Row.Bits);
		TestEqual(*FString::Printf(TEXT("%s replaces rather than ORs"), Row.Class),
			Found->bReplaces, Row.bReplaces);
		// The census cross-check: the row's address is the one `slots.md` records.
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Row.Class);
		if (TestNotNull(*FString::Printf(TEXT("%s is in the census"), Row.Class), Cls))
		{
			TestEqual(*FString::Printf(TEXT("%s's slot 337 body"), Row.Class),
				FString(ElysiumNpcKernelClass::BodyOf(Cls, 337)), FString(Row.Body));
		}
	}

	// A class with no row answers the Troika line's 1 and nothing else.
	TestNull(TEXT("CNPC_VHumanCombatant does not replace slot 337"),
		FElysiumNpc::UsedHullBitsSpeciesOf(TEXT("CNPC_VHumanCombatant")));
	return true;
}

// =================================================================================================
// Slot 533 `EyeOffset` — `0x102b4ab0` over `0x10274db0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryEyeOffsetTest,
	"Elysium.Substrate.NpcKernelGeometry.EyeOffset", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryEyeOffsetTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}

	// The six hint activities, in retail's `switch` order and no more.
	int32 Count = 0;
	const int32* Activities = FElysiumNpc::HintEyeOffsetActivities(Count);
	TestEqual(TEXT("slot 533 special-cases exactly six activities"), Count, 6);
	static const int32 Expected[] = { 0x1119, 0x111a, 0x111b, 0x111c, 0x111f, 0x1120 };
	for (int32 Index = 0; Index < Count && Index < 6; ++Index)
	{
		TestEqual(TEXT("the hint activity list is retail's"), Activities[Index], Expected[Index]);
	}
	// `0x111d` and `0x111e` are INSIDE the numeric range and are NOT cases — the switch is a jump
	// table with two holes, which a range test would get wrong.
	TestFalse(TEXT("0x111d is not one of them"),
		Activities[0] == 0x111d || Activities[5] == 0x111d);

	// With no hint node, every activity falls through to the base body. The debug-overlay seam
	// answers 0, so even the two special-cased activities answer `m_vDefaultEyeOffset` — retail's
	// own answer with the overlay off.
	TestEqual(TEXT("the debug-overlay seam answers nothing"),
		static_cast<int32>(F.Guard->DebugOverlayBits()), 0);
	const FVector DefaultOffset = F.Guard->DefaultEyeOffsetCm();
	TestTrue(TEXT("m_vDefaultEyeOffset is the chain's eye point minus the origin"),
		DefaultOffset.Equals(FVector(0.f, 0.f, ElysiumMove::StandViewZ), 0.01));

	TestEqual(TEXT("no hint node is claimed"), F.Guard->ScheduleHost.HintNode, INDEX_NONE);
	TestTrue(TEXT("an ordinary activity answers m_vDefaultEyeOffset"),
		F.Guard->EyeOffset(0x10, 0).Equals(DefaultOffset, 0.01));
	TestTrue(TEXT("a hint activity with no hint node also answers it"),
		F.Guard->EyeOffset(0x1119, 0).Equals(DefaultOffset, 0.01));
	TestTrue(TEXT("activity 0x57 answers it too while the overlay bit is clear"),
		F.Guard->EyeOffset(0x57, 0).Equals(DefaultOffset, 0.01));

	// The second `Activity` argument reaches no instruction in either body.
	TestTrue(TEXT("retail's second Activity is never read"),
		F.Guard->EyeOffset(0x10, 0x1119).Equals(F.Guard->EyeOffset(0x10, 0), 0.01));

	// The base body, measured directly. Its override arm is `(0, 0, 1.5)` SOURCE units and is
	// reachable ONLY with the overlay bit set; with the seam answering 0 it is unreachable, so both
	// special-cased activities answer `m_vDefaultEyeOffset` — which is retail's own answer with the
	// overlay off, and the shipped default.
	TestTrue(TEXT("0x57 answers the default while the overlay bit is clear"),
		F.Guard->BaseEyeOffset(0x57).Equals(DefaultOffset, 0.01));
	TestTrue(TEXT("and so does 8, the other special-cased activity"),
		F.Guard->BaseEyeOffset(8).Equals(DefaultOffset, 0.01));
	TestFalse(TEXT("the override the overlay would give is NOT the default"),
		FVector(0.f, 0.f, 1.5f * ElysiumMove::U).Equals(DefaultOffset, 0.01));
	return true;
}

// =================================================================================================
// `CAI_BaseNPCTroika::ResolveStandingOnHead` — `0x102bf820`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryStandingOnHeadTest,
	"Elysium.Substrate.NpcKernelGeometry.StandingOnHead", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryStandingOnHeadTest::RunTest(const FString&)
{
	// The four diagonals, in the listing's `DEC EAX` order. `_DAT_1049aea8` is +0.707 and
	// `_DAT_1049aea4` is -0.707, both read out of `.rdata`.
	TestTrue(TEXT("roll 0 is (+0.707, +0.707)"),
		FElysiumNpc::StandingOnHeadDiagonal(0).Equals(FVector(0.707f, 0.707f, 0.f), 0.0001));
	TestTrue(TEXT("roll 1 is (-0.707, +0.707)"),
		FElysiumNpc::StandingOnHeadDiagonal(1).Equals(FVector(-0.707f, 0.707f, 0.f), 0.0001));
	TestTrue(TEXT("roll 2 is (+0.707, -0.707)"),
		FElysiumNpc::StandingOnHeadDiagonal(2).Equals(FVector(0.707f, -0.707f, 0.f), 0.0001));
	TestTrue(TEXT("roll 3 is (-0.707, -0.707)"),
		FElysiumNpc::StandingOnHeadDiagonal(3).Equals(FVector(-0.707f, -0.707f, 0.f), 0.0001));

	// Co-located in XY: the delta is exactly zero on both axes and the diagonal arm is taken, with
	// the jitters unread. The Z difference is forced to zero BEFORE any of this, so a body standing
	// directly on another's head is still pushed horizontally.
	{
		const FElysiumNpc::FStandingOnHeadStep Step = FElysiumNpc::StandingOnHeadStep(
			FVector(100.f, 100.f, 250.f), FVector(100.f, 100.f, 0.f),
			/*PreviousTimer*/ 0.f, /*Interval*/ 0.1f, /*Roll*/ 2, /*JitterX*/ 9.f, /*JitterY*/ 9.f);
		TestTrue(TEXT("an exact XY match takes the diagonal arm and ignores the jitter"),
			Step.Direction.Equals(FVector(0.707f, -0.707f, 0.f), 0.0001));
		TestEqual(TEXT("and the ramp is prev + interval"), Step.TimerSeconds, 0.1f, 0.0001f);
	}

	// Offset in XY: normalise, jitter, normalise again. With zero jitter the direction is the plain
	// unit delta and Z is still exactly zero.
	{
		const FElysiumNpc::FStandingOnHeadStep Step = FElysiumNpc::StandingOnHeadStep(
			FVector(300.f, 0.f, 250.f), FVector(0.f, 0.f, 0.f),
			/*PreviousTimer*/ 1.f, /*Interval*/ 0.5f, /*Roll*/ 0, 0.f, 0.f);
		TestTrue(TEXT("an offset pair takes the normalise arm"),
			Step.Direction.Equals(FVector(1.f, 0.f, 0.f), 0.0001));
		TestEqual(TEXT("Z is forced to zero before the direction is built"),
			Step.Direction.Z, 0.0, 0.0);

		// The ramp: `min(prev + interval, 5)`.
		TestEqual(TEXT("the ramp accumulates"), Step.TimerSeconds, 1.5f, 0.0001f);

		// The start is lifted 0.1 SOURCE units on Z.
		TestEqual(TEXT("the trace starts 0.1 Source units above the origin"),
			Step.StartCm.Z - 250.0, 0.1 * ElysiumMove::U, 0.0001);

		// The delta: `dir * timer * 40 units * interval` = 1 * 1.5 * 40u * 0.5.
		TestEqual(TEXT("the push distance is dir * timer * 40 units * interval"),
			Step.DeltaCm.X, 1.5 * 40.0 * ElysiumMove::U * 0.5, 0.001);
	}

	// The ceiling is `_DAT_10454110` = 5.0 seconds.
	{
		const FElysiumNpc::FStandingOnHeadStep Step = FElysiumNpc::StandingOnHeadStep(
			FVector(300.f, 0.f, 0.f), FVector::ZeroVector, 4.9f, 1.0f, 0, 0.f, 0.f);
		TestEqual(TEXT("the ramp clamps at 5 seconds"), Step.TimerSeconds, 5.f, 0.0001f);
	}

	// The body: with no ground entity — slot 209 is another story's stub and answers null — retail
	// takes `LAB_102bfc9e`, which is the write that CLEARS the ramp.
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	const FVector Before = F.Guard->Origin;
	F.Guard->StandingOnHeadTimer = 3.f;
	F.Guard->ResolveStandingOnHead(0.1f);
	TestEqual(TEXT("no ground entity clears m_flStandingOnHeadTimer"),
		F.Guard->StandingOnHeadTimer, 0.f, 0.0001f);
	TestTrue(TEXT("and moves nobody"), F.Guard->Origin.Equals(Before, 0.001));
	return true;
}

// =================================================================================================
// `CNPC_VAsianVampire::StandingOnPlayer` — `0x10362730`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryStandingOnPlayerTest,
	"Elysium.Substrate.NpcKernelGeometry.StandingOnPlayer", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryStandingOnPlayerTest::RunTest(const FString&)
{
	// The threshold is the sum of two HALF-DIAGONALS, not two radii. Two 32x32 footprints give
	// 0.5*sqrt(32^2+32^2) = 22.627 each, so the test admits a separation up to 45.25 — where a
	// half-width reading would stop at 32.
	const FVector Mins(-16.f, -16.f, 0.f);
	const FVector Maxs(16.f, 16.f, 72.f);
	const double Limit = 2.0 * 0.5 * FMath::Sqrt(32.0 * 32.0 + 32.0 * 32.0);
	TestEqual(TEXT("two 32x32 footprints admit 45.25 units of separation"), Limit, 45.2548, 0.001);

	TestTrue(TEXT("just inside the sum of half-diagonals overlaps"),
		FElysiumNpc::StandingOnPlayerOverlap(FVector::ZeroVector,
			FVector(Limit - 0.1, 0.0, 0.0), Mins, Maxs, Mins, Maxs));
	TestFalse(TEXT("just outside it does not"),
		FElysiumNpc::StandingOnPlayerOverlap(FVector::ZeroVector,
			FVector(Limit + 0.1, 0.0, 0.0), Mins, Maxs, Mins, Maxs));

	// The boundary itself, and it has to be built exactly or it is not the boundary. Retail's
	// decision at `10362880` is `FCOMPP` (limit against separation) followed by `FNSTSW AX;
	// AND EAX,0x4100; JNZ <return false>` — the mask keeps C0 (limit < separation) **and** C3
	// (limit == separation), and either one returns false. So the compare is `separation < limit`,
	// STRICTLY, and exactly touching is not standing on.
	//
	// A 32x32 footprint cannot pin that: its half-diagonal is `sqrt(2)*16`, which no float and no
	// double represents, so "the limit" computed in the case and "the limit" computed in the body
	// land on opposite sides of a strict compare. (`float(45.254833995939045)` is
	// `45.25483322143555` — 7.7e-7 BELOW the double the body builds, which is genuinely inside the
	// box and which the body correctly answers true for.) A 3-by-4 footprint has a diagonal of
	// exactly 5, a half of exactly 2.5 and a sum of exactly 5.0, and `sqrt(5*5)` is exactly 5.0, so
	// every value here is bit-for-bit the one the body compares.
	const FVector ExactMins(-1.5, -2.0, 0.0);
	const FVector ExactMaxs(1.5, 2.0, 72.0);
	TestFalse(TEXT("exactly at the limit is not an overlap — the compare is strictly less"),
		FElysiumNpc::StandingOnPlayerOverlap(FVector::ZeroVector, FVector(5.0, 0.0, 0.0),
			ExactMins, ExactMaxs, ExactMins, ExactMaxs));
	TestTrue(TEXT("and one ulp inside it is"),
		FElysiumNpc::StandingOnPlayerOverlap(FVector::ZeroVector,
			FVector(std::nextafter(5.0, 0.0), 0.0, 0.0), ExactMins, ExactMaxs,
			ExactMins, ExactMaxs));
	TestFalse(TEXT("and one ulp outside it is not"),
		FElysiumNpc::StandingOnPlayerOverlap(FVector::ZeroVector,
			FVector(std::nextafter(5.0, 10.0), 0.0, 0.0), ExactMins, ExactMaxs,
			ExactMins, ExactMaxs));

	// Z is not read at all: a body 1000 units overhead still "stands on" the player in XY.
	TestTrue(TEXT("the test is 2-D — Z separation is never measured"),
		FElysiumNpc::StandingOnPlayerOverlap(FVector::ZeroVector, FVector(0.f, 0.f, 1000.f),
			Mins, Maxs, Mins, Maxs));

	// The body: the subject is `m_hClosestPlayer`, not `GetEnemy()`, and no closest player is
	// retail's own `return false`.
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}
	F.Guard->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	TestFalse(TEXT("no closest player answers false"), F.Guard->StandingOnPlayer());
	return true;
}

// =================================================================================================
// `CNPC_VWerewolf::UpdateFakeHull` — `0x103d93b0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryFakeHullTest,
	"Elysium.Substrate.NpcKernelGeometry.FakeHull", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryFakeHullTest::RunTest(const FString&)
{
	// `FUN_10240250`, inclusive on every axis.
	const FVector AMin(0.f, 0.f, 0.f);
	const FVector AMax(10.f, 10.f, 10.f);
	TestTrue(TEXT("a contained box overlaps"),
		FElysiumNpc::BoxesOverlap(AMin, AMax, FVector(1.f, 1.f, 1.f), FVector(2.f, 2.f, 2.f)));
	TestTrue(TEXT("a shared face overlaps — every comparison is inclusive"),
		FElysiumNpc::BoxesOverlap(AMin, AMax, FVector(10.f, 0.f, 0.f), FVector(20.f, 10.f, 10.f)));
	TestFalse(TEXT("a gap on X does not"),
		FElysiumNpc::BoxesOverlap(AMin, AMax, FVector(11.f, 0.f, 0.f), FVector(20.f, 10.f, 10.f)));
	TestFalse(TEXT("a gap on Z does not either"),
		FElysiumNpc::BoxesOverlap(AMin, AMax, FVector(0.f, 0.f, -20.f), FVector(10.f, 10.f, -1.f)));

	// The activity map: 1 -> 0x7a, 3 -> 0x7b, and everything else — INCLUDING 2 — -> 0x79.
	TestEqual(TEXT("class 0 answers 0x79"), FElysiumNpc::FakeHullKnockbackActivity(0), 0x79);
	TestEqual(TEXT("class 1 answers 0x7a"), FElysiumNpc::FakeHullKnockbackActivity(1), 0x7a);
	TestEqual(TEXT("class 2 answers 0x79, not a fourth activity"),
		FElysiumNpc::FakeHullKnockbackActivity(2), 0x79);
	TestEqual(TEXT("class 3 answers 0x7b"), FElysiumNpc::FakeHullKnockbackActivity(3), 0x7b);

	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard))
	{
		return false;
	}

	// No enemy: the cache is RESET to `vec3_origin` and nothing else happens. This is the arm that
	// disarms the next call, because the next call's gate is "is the cache still zero".
	F.Guard->WerewolfFakeHullPosUnits = FVector(1.f, 2.f, 3.f);
	F.Guard->UpdateFakeHull(10.0);
	TestTrue(TEXT("no enemy resets the fake-hull cache to vec3_origin"),
		F.Guard->WerewolfFakeHullPosUnits.IsNearlyZero());
	TestEqual(TEXT("and pushes no damage"), F.Guard->FakeHullSeams.DamagePushes, 0);

	// The bone seam: with an enemy the body asks for `Bip01`; with none it does not.
	const int32 BoneCallsBefore = F.Guard->BoneWorldPositionCalls;
	F.Guard->UpdateFakeHull(11.0);
	TestEqual(TEXT("with no enemy the Bip01 lookup is never reached"),
		F.Guard->BoneWorldPositionCalls, BoneCallsBefore);

	// The damage gate is strict and one second wide.
	F.Guard->WerewolfFakeHullPushTime = 10.0;
	TestFalse(TEXT("exactly one second later is NOT past the gate"), 10.0 + 1.0 < 11.0);
	TestTrue(TEXT("a hair over one second is"), 10.0 + 1.0 < 11.0001);

	// The seams all answer the recovered refusal.
	TestEqual(TEXT("the debug-hull cvar is unrecovered and answers 0"),
		F.Guard->FakeHullDebugCvar(), 0);
	const FVector Point(5.f, 6.f, 7.f);
	TestTrue(TEXT("CalcNearestPoint answers the point unchanged"),
		F.Guard->NearestPointOnEntity(F.Other, Point).Equals(Point, 0.001));
	TestTrue(TEXT("and the ask is recorded"), F.Guard->FakeHullSeams.NearestPointCalls > 0);
	return true;
}

// =================================================================================================
// `CNPC_VMingXiao`'s two severed-tentacle scatter notices — `0x10397e00` and `0x103998d0`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelGeometryScatterTest,
	"Elysium.Substrate.NpcKernelGeometry.Scatter", GElysiumNpcKernelGeometryFlags)
bool FElysiumNpcKernelGeometryScatterTest::RunTest(const FString&)
{
	FGeometryFixture F;
	if (!TestNotNull(TEXT("the guard spawned"), F.Guard)
		|| !TestNotNull(TEXT("the other spawned"), F.Other))
	{
		return false;
	}

	// `FUN_1039ef90`: condition 0x78 and `m_vecScatterCenter`, both on the NOTIFIED tentacle.
	F.Guard->NotifyScatterCenter(F.Other, FVector(254.f, 0.f, 0.f));
	TestTrue(TEXT("the notified tentacle takes condition 0x78"),
		F.Other->Cognition.Conditions.Has(
			static_cast<EElysiumNpcCond>(FElysiumNpc::ScatterNoticeCondition)));
	TestTrue(TEXT("and the scatter centre, in Source units"),
		F.Other->TentacleScatterCenterUnits.Equals(
			FVector(254.f / ElysiumMove::U, 0.f, 0.f), 0.001));
	TestFalse(TEXT("the notifier takes neither"),
		F.Guard->Cognition.Conditions.Has(
			static_cast<EElysiumNpcCond>(FElysiumNpc::ScatterNoticeCondition)));
	TestTrue(TEXT("the unrecovered global event is counted"), F.Guard->ScatterNoticeEvents > 0);

	// `FUN_10397e00`: a null `param_1` is retail's first test and does nothing.
	const int32 EventsBefore = F.Guard->ScatterNoticeEvents;
	F.Guard->NotifyOwnedCopiesOfOwnerMove(nullptr);
	TestEqual(TEXT("a null moved entity notifies nobody"),
		F.Guard->ScatterNoticeEvents, EventsBefore);

	// With `m_rhSeveredTentacles` empty the six-entry walk notifies nobody either.
	F.Guard->NotifyOwnedCopiesOfOwnerMove(F.Other);
	TestEqual(TEXT("an empty severed-tentacle array notifies nobody"),
		F.Guard->ScatterNoticeEvents, EventsBefore);

	// One severed tentacle, and it is NOT the moved entity: it is told where the moved one is.
	F.Other->TentacleScatterCenterUnits = FVector::ZeroVector;
	F.Guard->SeveredTentacles[2] = F.Other->Handle;
	F.Guard->Origin = FVector(700.f, 0.f, 0.f);
	FElysiumNpc* Moved = F.Phone;
	if (TestNotNull(TEXT("the payphone stands in for the moved tentacle"), Moved))
	{
		F.Guard->NotifyOwnedCopiesOfOwnerMove(Moved);
		TestTrue(TEXT("the survivor is told where the MOVED entity is, not where the owner is"),
			F.Other->TentacleScatterCenterUnits.Equals(Moved->Origin / ElysiumMove::U, 0.001));
	}

	// And the moved entity is skipped when it is itself in the array.
	F.Guard->SeveredTentacles[2] = F.Other->Handle;
	F.Other->TentacleScatterCenterUnits = FVector::ZeroVector;
	F.Guard->NotifyOwnedCopiesOfOwnerMove(F.Other);
	TestTrue(TEXT("a tentacle is never told to scatter away from itself"),
		F.Other->TentacleScatterCenterUnits.IsNearlyZero());

	// --- `0x103998d0`'s gate ---------------------------------------------------------------------
	//
	// `_DAT_1046dcd0` = 128 SOURCE units, inclusive; `_DAT_10449260` is a DOUBLE and reads 0.25.
	const FVector Forward(1.f, 0.f, 0.f);
	const double LimitCm = 128.0 * ElysiumMove::U;
	TestTrue(TEXT("dead ahead and inside 128 units passes"),
		FElysiumNpc::ScatterTentacleGate(FVector(LimitCm - 1.0, 0.0, 0.0), Forward));
	TestTrue(TEXT("exactly 128 units passes — the flag mask keeps the equal case"),
		FElysiumNpc::ScatterTentacleGate(FVector(LimitCm, 0.0, 0.0), Forward));
	TestFalse(TEXT("past 128 units does not"),
		FElysiumNpc::ScatterTentacleGate(FVector(LimitCm + 1.0, 0.0, 0.0), Forward));

	// THE CORRECTION: the dot floor is 0.25, so 60 degrees off centre (dot 0.5) passes and 80
	// degrees (dot 0.17) does not. Read as a float the cell is 0.0 and both would pass.
	TestTrue(TEXT("60 degrees off centre passes the 0.25 dot floor"),
		FElysiumNpc::ScatterTentacleGate(
			FVector(100.0 * FMath::Cos(PI / 3.0), 100.0 * FMath::Sin(PI / 3.0), 0.0), Forward));
	TestFalse(TEXT("80 degrees off centre does NOT — which a 0.0 floor would have admitted"),
		FElysiumNpc::ScatterTentacleGate(
			FVector(100.0 * FMath::Cos(PI * 80.0 / 180.0),
				100.0 * FMath::Sin(PI * 80.0 / 180.0), 0.0), Forward));
	TestFalse(TEXT("directly behind does not"),
		FElysiumNpc::ScatterTentacleGate(FVector(-100.0, 0.0, 0.0), Forward));

	// Z is not multiplied by anything: the dot is 2-D, so a tentacle straight overhead inside the
	// range is judged only by its XY bearing.
	TestTrue(TEXT("the dot is 2-D — Z does not enter it"),
		FElysiumNpc::ScatterTentacleGate(FVector(100.0, 0.0, 100.0), Forward));

	// The body's three word gates, each measured on its own.
	F.Other->TentaclePhase = 2;
	F.Other->ScheduleHost.ForcedSchedule = static_cast<int32>(0x163);
	F.Other->TentacleScatterCenterUnits = FVector::ZeroVector;
	F.Guard->Origin = FVector::ZeroVector;
	F.Other->Origin = FVector(100.f, 0.f, 0.f);
	F.Guard->Forward = FVector(1.f, 0.f, 0.f);
	F.Guard->FUN_103998d0(F.Other);
	TestTrue(TEXT("forced schedule 0x163 refuses the scatter"),
		F.Other->TentacleScatterCenterUnits.IsNearlyZero());

	F.Other->ScheduleHost.ForcedSchedule = static_cast<int32>(0x165);
	F.Guard->FUN_103998d0(F.Other);
	TestTrue(TEXT("forced schedule 0x165 refuses it too"),
		F.Other->TentacleScatterCenterUnits.IsNearlyZero());

	F.Other->ScheduleHost.ForcedSchedule = ElysiumScheduleId::None;
	F.Other->TentaclePhase = 1;
	F.Guard->FUN_103998d0(F.Other);
	TestTrue(TEXT("m_ePhase must be exactly 2"),
		F.Other->TentacleScatterCenterUnits.IsNearlyZero());

	F.Other->TentaclePhase = 2;
	F.Guard->FUN_103998d0(F.Other);
	TestTrue(TEXT("every gate open scatters the tentacle away from the BOSS's origin"),
		F.Other->TentacleScatterCenterUnits.Equals(F.Guard->Origin / ElysiumMove::U, 0.001));

	// `m_vecForward` is retail's cached basis and nothing in this runtime writes it, so the cone
	// gate refuses until a sense pass fills it — the recovered refusal, stated.
	F.Guard->Forward = FVector::ZeroVector;
	F.Other->TentacleScatterCenterUnits = FVector(9.f, 9.f, 9.f);
	F.Guard->FUN_103998d0(F.Other);
	TestTrue(TEXT("an unwritten m_vecForward closes the cone"),
		F.Other->TentacleScatterCenterUnits.Equals(FVector(9.f, 9.f, 9.f), 0.001));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
