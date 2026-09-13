#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Bosses**. Every assertion below is read off the decompiled C (or, for
// `0x103983d0` and `0x103989b0`, off the LISTING, whose two bodies the decompiler lost) of the body
// it names — the threshold, the arm order, the id, what is written — never off 29c's one-line walk.
//
// Two of the family's four species (`CNPC_VHengeyokai`, `CNPC_VManBat`) have no registered
// classname in `Substrate/ElysiumNpcClasses.cpp`, so their rows are exercised through the table's
// own lookup and the row-explicit body forms, exactly as the brief prescribes. Every seam that can
// only answer nothing gets a case saying the seam is asked and the refusal is the recovered one.

static constexpr EAutomationTestFlags GBossesTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float U = ElysiumMove::U;

	// `thunk_FUN_101e8da0(0x10739d08)` — `CNPC_VMingXiao`'s tuning record, read by field offset.
	// The record lives past `.data`'s raw size in the pinned image, so the runtime seam answers 0
	// (families Facing and Motor record the same gap). A test hands the bodies a record of its own
	// so the blends, the clamps and the cell CHOICE are all measurable.
	struct FTuning
	{
		TMap<int32, float> Cells;
		float operator()(int32 Offset) const { return Cells.FindRef(Offset); }
	};

	FElysiumNpc::FBlacklistedEntity MakeBlacklistRow(double ExpiresAt)
	{
		FElysiumNpc::FBlacklistedEntity Row;
		Row.ExpiresAt = ExpiresAt;
		return Row;
	}
}

// -------------------------------------------------------------------------------------------------
// `0x10381e90` — `CNPC_VHengeyokai`'s grab-bone search.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesGrabBoneTest,
	"Elysium.Substrate.NpcKernelBosses.PickupGrabBone", GBossesTestFlags)
bool FElysiumNpcKernelBossesGrabBoneTest::RunTest(const FString&)
{
	// The bone table is retail's `PTR_s_Bone01_1063bd88`, read from the image: two names and an
	// empty string that stops the walk.
	const TCHAR* const* Names = FElysiumNpc::PickupGrabBoneNames();
	TestEqual(TEXT("the first bone is Bone01"), FString(Names[0]), FString(TEXT("Bone01")));
	TestEqual(TEXT("the second bone is Bone04"), FString(Names[1]), FString(TEXT("Bone04")));
	TestNull(TEXT("the table ends after two names"), Names[2]);

	FElysiumNpcWorldBuilder Builder(TEXT("bosses_grabbone"), 0x29c1b055);
	Builder.AddNpc(TEXT("boss"), FVector(0.0, 0.0, 0.0));
	Builder.AddNpc(TEXT("body"), FVector(100.0 * U, 0.0, 25.0 * U));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpc* Body = Fixture.Npc(TEXT("body"));
	FElysiumNpcWorldFixture::Quiet({ Boss, Body });
	if (Boss == nullptr || Body == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// A null target: retail would have run its RTTI cast on a null pointer and then dereferenced the
	// target's vtable on the fallback arm. The port refuses, which is the one stated divergence.
	TestFalse(TEXT("a null target answers false"), Boss->FindPickupTargetGrabBone(nullptr));

	// `RagdollBonePosition` is a seam and answers false for both bones, which is retail's null-cast
	// arm: the target's own origin in SOURCE units, bone 0, and TRUE.
	Boss->HengeyokaiPickupTargetGrabBone = 7;
	TestTrue(TEXT("the no-ragdoll arm answers true"), Boss->FindPickupTargetGrabBone(Body));
	TestEqual(TEXT("m_iPickupTargetGrabBone is 0 on the fallback arm"),
		Boss->HengeyokaiPickupTargetGrabBone, 0);
	TestEqual(TEXT("m_vecPickupTargetPos is the target origin in source units"),
		Boss->HengeyokaiPickupTargetPos.X, 100.0, 0.001);
	TestEqual(TEXT("and its Z with it"), Boss->HengeyokaiPickupTargetPos.Z, 25.0, 0.001);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x103822a0` — the +-20 degree pickup facing cone.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesFacingConeTest,
	"Elysium.Substrate.NpcKernelBosses.PickupFacingCone", GBossesTestFlags)
bool FElysiumNpcKernelBossesFacingConeTest::RunTest(const FString&)
{
	// The listing's two `FCOMP`s: `-20.0 <= AngleDiff(VecToYaw(delta), yaw) <= 20.0`, inclusive on
	// both edges. The delta is in THIS world's axes, whose Y is the negated Source one, so a +X
	// delta is yaw 0 and a -Y (port) delta is yaw +90.
	TestTrue(TEXT("dead ahead is inside the cone"),
		FElysiumNpc::WithinPickupFacingCone(FVector(100.0, 0.0, 0.0), 0.f));
	TestTrue(TEXT("exactly +20 is inside"),
		FElysiumNpc::WithinPickupFacingCone(FVector(100.0, 0.0, 0.0), -20.f));
	TestTrue(TEXT("exactly -20 is inside"),
		FElysiumNpc::WithinPickupFacingCone(FVector(100.0, 0.0, 0.0), 20.f));
	TestFalse(TEXT("just past +20 is outside"),
		FElysiumNpc::WithinPickupFacingCone(FVector(100.0, 0.0, 0.0), -20.5f));
	TestFalse(TEXT("just past -20 is outside"),
		FElysiumNpc::WithinPickupFacingCone(FVector(100.0, 0.0, 0.0), 20.5f));
	// The wrap: a yaw of 350 against a delta of yaw 0 is a difference of +10, not -350.
	TestTrue(TEXT("the angle wrap keeps 350 vs 0 inside the cone"),
		FElysiumNpc::WithinPickupFacingCone(FVector(100.0, 0.0, 0.0), 350.f));
	// A 90-degree-abeam target is outside.
	TestFalse(TEXT("abeam is outside"),
		FElysiumNpc::WithinPickupFacingCone(FVector(0.0, -100.0, 0.0), 0.f));

	FElysiumNpcWorldBuilder Builder(TEXT("bosses_cone"), 0x29c1b056);
	Builder.AddNpc(TEXT("boss"));
	Builder.AddNpc(TEXT("other"), FVector(0.0, 500.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	FElysiumNpcWorldFixture::Quiet({ Boss, Other });
	if (Boss == nullptr || Other == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}
	// Both early-outs answer TRUE — retail's `MOV AL, 1`, the permissive arm.
	TestTrue(TEXT("a null target answers true"), Boss->FUN_103822a0(nullptr));
	TestTrue(TEXT("no m_hPickupTarget answers true, whatever the angle"),
		Boss->FUN_103822a0(Other));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The pickup species table and the attach/release pair.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesPickupSpeciesTest,
	"Elysium.Substrate.NpcKernelBosses.PickupSpecies", GBossesTestFlags)
bool FElysiumNpcKernelBossesPickupSpeciesTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FPickupSpecies* Rows = FElysiumNpc::PickupSpeciesRows(Count);
	TestEqual(TEXT("two species carry the pickup chain"), Count, 2);

	// Every row by NAME, with the two retail bodies, the carrier bone, the `m_hPickupTarget` offset
	// and the collision-ignore re-arm the release passes to `0x102c43b0`.
	const FElysiumNpc::FPickupSpecies* Heng =
		FElysiumNpc::PickupSpeciesOf(TEXT("CNPC_VHengeyokai"));
	if (Heng == nullptr)
	{
		AddError(TEXT("CNPC_VHengeyokai has no pickup row"));
		return false;
	}
	TestEqual(TEXT("Hengeyokai's attach body"), FString(Heng->AttachBody),
		FString(TEXT("0x10382670")));
	TestEqual(TEXT("Hengeyokai's release body"), FString(Heng->ReleaseBody),
		FString(TEXT("0x10382400")));
	TestEqual(TEXT("Hengeyokai grabs on Bip01 R Hand"), FString(Heng->CarrierBone),
		FString(TEXT("Bip01 R Hand")));
	TestEqual(TEXT("Hengeyokai's m_hPickupTarget is +0x6664"), Heng->PickupTargetOffset, 0x6664);
	TestEqual(TEXT("Hengeyokai re-arms the ignore at 0.75 s"), Heng->IgnoreCollisionSeconds, 0.75f,
		0.0001f);
	TestFalse(TEXT("Hengeyokai does not restore the breakable latch"), Heng->bRestoresBreakable);

	const FElysiumNpc::FPickupSpecies* Bat = FElysiumNpc::PickupSpeciesOf(TEXT("CNPC_VManBat"));
	if (Bat == nullptr)
	{
		AddError(TEXT("CNPC_VManBat has no pickup row"));
		return false;
	}
	TestEqual(TEXT("ManBat's attach body"), FString(Bat->AttachBody), FString(TEXT("0x1038f430")));
	TestEqual(TEXT("ManBat's release body"), FString(Bat->ReleaseBody), FString(TEXT("0x1038f790")));
	TestEqual(TEXT("ManBat grabs on Bip01_R_Foot"), FString(Bat->CarrierBone),
		FString(TEXT("Bip01_R_Foot")));
	TestEqual(TEXT("ManBat's m_hPickupTarget is +0x668c"), Bat->PickupTargetOffset, 0x668c);
	TestEqual(TEXT("ManBat re-arms the ignore at 2.0 s"), Bat->IgnoreCollisionSeconds, 2.0f,
		0.0001f);
	TestTrue(TEXT("ManBat restores the breakable latch after the throw"), Bat->bRestoresBreakable);

	// A species with no row of its own and no boss ancestor.
	TestNull(TEXT("CNPC_VWerewolf carries no pickup row"),
		FElysiumNpc::PickupSpeciesOf(TEXT("CNPC_VWerewolf")));
	TestNull(TEXT("a null class carries no pickup row"), FElysiumNpc::PickupSpeciesOf(nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesPickupChainTest,
	"Elysium.Substrate.NpcKernelBosses.PickupChain", GBossesTestFlags)
bool FElysiumNpcKernelBossesPickupChainTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_pickup"), 0x29c1b057);
	Builder.AddNpc(TEXT("boss"));
	Builder.AddNpc(TEXT("body"), FVector(200.0 * U, 0.0, 0.0));
	Builder.AddNpc(TEXT("aim"), FVector(600.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpc* Body = Fixture.Npc(TEXT("body"));
	FElysiumNpc* Aim = Fixture.Npc(TEXT("aim"));
	FElysiumNpcWorldFixture::Quiet({ Boss, Body, Aim });
	if (Boss == nullptr || Body == nullptr || Aim == nullptr)
	{
		AddError(TEXT("fixture did not stand three NPCs"));
		return false;
	}
	const FElysiumNpc::FPickupSpecies* Heng =
		FElysiumNpc::PickupSpeciesOf(TEXT("CNPC_VHengeyokai"));
	const FElysiumNpc::FPickupSpecies* Bat = FElysiumNpc::PickupSpeciesOf(TEXT("CNPC_VManBat"));

	// No row: nothing runs at all. That is the answer for every classname this runtime registers,
	// since neither `npc_VHengeyokai` nor `npc_VManBat` is one of them.
	TestFalse(TEXT("a class with no pickup row attaches nothing"),
		Boss->AttachPickupAnimlinkFor(nullptr, Body, 0));

	// The attach arms both stop at `CreatePhysAnimlink`, the first seam: retail's
	// "CreateNoSpawn failed" arm, which returns false before writing a single word.
	TestFalse(TEXT("Hengeyokai's attach stops at the phys_animlink seam"),
		Boss->AttachPickupAnimlinkFor(Heng, Body, 0));
	TestFalse(TEXT("ManBat's attach stops at the same seam"),
		Boss->AttachPickupAnimlinkFor(Bat, Body, 0));
	TestFalse(TEXT("no animlink handle was written"),
		Boss->HengeyokaiPhysicsAnimlink.IsSet() || Boss->ManBatPhysicsAnimlink.IsSet());
	TestFalse(TEXT("and neither carry flag was raised"),
		Boss->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("and family Misc's FormBit was never asked"), Boss->FormBitCalls, 0);

	// The release arms run to the end whatever the seams answer, and what they CLEAR is the
	// recovered half. Hengeyokai's tail is `0x10381c00(this, false)` — family **Misc**'s `FormBit`,
	// which landed this wave, so the bit now really moves. The point of this block is that the
	// Hengeyokai chain does not write `CARRYING_BODY` itself: the write is `FormBit`'s, and the two
	// counters are what say the call was made and with which arm.
	Boss->HengeyokaiPickupTarget = Body->Handle;
	Boss->HengeyokaiPhysicsAnimlink = Body->Handle;
	Boss->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	// `0x10381c00`'s FALSE arm touches neither species word, so both must survive the release —
	// a cleared carry leaves the fish timer standing wherever the last pickup left it.
	Boss->HengeyokaiFishTimer = 1234.f;
	Boss->bHengeyokaiDidFakeThrow = true;
	Boss->ReleasePickupAnimlinkFor(Heng, Aim);
	TestFalse(TEXT("Hengeyokai's m_hPhysicsAnimlink is cleared"),
		Boss->HengeyokaiPhysicsAnimlink.IsSet());
	TestFalse(TEXT("Hengeyokai's m_hPickupTarget is cleared"),
		Boss->HengeyokaiPickupTarget.IsSet());
	TestEqual(TEXT("FormBit (0x10381c00) was asked exactly once"), Boss->FormBitCalls, 1);
	TestFalse(TEXT("with the false arm"), Boss->bLastFormBitArm);
	TestFalse(TEXT("and that call is what cleared CARRYING_BODY"),
		Boss->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("0x10381c00's false arm leaves m_flFishTimer standing"),
		Boss->HengeyokaiFishTimer, 1234.f, 0.0001f);
	TestTrue(TEXT("and leaves m_bDidFakeThrow standing"), Boss->bHengeyokaiDidFakeThrow);

	// The TRUE arm, which the attach half takes and which the seams above stop this fixture from
	// reaching through `AttachPickupAnimlinkFor`. Driven directly so the three writes `0x10381c00`
	// makes are all measured: the flag, `m_flFishTimer = curtime + RandomFloat(5, 8)` and the
	// `m_bDidFakeThrow` clear.
	const double Now = Fixture.World.NowSeconds();
	Boss->bHengeyokaiDidFakeThrow = true;
	Boss->FormBit(true);
	TestTrue(TEXT("0x10381c00's true arm raises CARRYING_BODY"),
		Boss->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestFalse(TEXT("and clears m_bDidFakeThrow"), Boss->bHengeyokaiDidFakeThrow);
	TestTrue(TEXT("and stamps m_flFishTimer inside curtime + [5, 8]"),
		Boss->HengeyokaiFishTimer >= static_cast<float>(Now) + 5.f
			&& Boss->HengeyokaiFishTimer <= static_cast<float>(Now) + 8.f);

	// ManBat: the same two clears, and `CARRYING_BODY` cleared by its OWN one-bit body
	// (`0x1038f600`) rather than through `0x10381c00`. That asymmetry between the two species is the
	// recovered fact, and `FormBitCalls` not moving is what proves this arm did not borrow it.
	Boss->ManBatPickupTarget = Body->Handle;
	Boss->ManBatPhysicsAnimlink = Body->Handle;
	Boss->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	Boss->HengeyokaiFishTimer = 4321.f;
	Boss->ReleasePickupAnimlinkFor(Bat, nullptr);
	TestFalse(TEXT("ManBat's m_hPhysicsAnimlink is cleared"), Boss->ManBatPhysicsAnimlink.IsSet());
	TestFalse(TEXT("ManBat's m_hPickupTarget is cleared"), Boss->ManBatPickupTarget.IsSet());
	TestFalse(TEXT("ManBat clears CARRYING_BODY through 0x1038f600"),
		Boss->NpcFlags.Has(EElysiumNpcFlag::CARRYING_BODY));
	TestEqual(TEXT("and never asks FormBit, so the Hengeyokai fish timer is untouched"),
		Boss->FormBitCalls, 1);
	TestEqual(TEXT("m_flFishTimer is not the ManBat arm's to write"), Boss->HengeyokaiFishTimer,
		4321.f, 0.0001f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10382970` / `0x10382b30` / `0x10382aa0` — the expiring blacklist.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesBlacklistTest,
	"Elysium.Substrate.NpcKernelBosses.Blacklist", GBossesTestFlags)
bool FElysiumNpcKernelBossesBlacklistTest::RunTest(const FString&)
{
	// The pure rule first: found and unexpired is TRUE; found and expired SWAP-REMOVES with the last
	// row and answers false; not found is false. `0x10382aa0` and `CNPC_VBaseBoss`'s `0x10366400`
	// are the same body over two stores, so this one rule is both.
	TArray<FElysiumNpc::FBlacklistedEntity> Store;
	TestFalse(TEXT("INDEX_NONE answers false"),
		FElysiumNpc::BlacklistTestAndExpire(Store, INDEX_NONE, 0.0));

	Store.Add(MakeBlacklistRow(10.0));
	Store.Add(MakeBlacklistRow(20.0));
	Store.Add(MakeBlacklistRow(30.0));
	TestTrue(TEXT("curtime strictly before the expiry holds"),
		FElysiumNpc::BlacklistTestAndExpire(Store, 1, 19.9));
	TestEqual(TEXT("and nothing was removed"), Store.Num(), 3);
	TestFalse(TEXT("curtime exactly at the expiry does NOT hold"),
		FElysiumNpc::BlacklistTestAndExpire(Store, 1, 20.0));
	TestEqual(TEXT("the expired row was removed"), Store.Num(), 2);
	TestEqual(TEXT("by SWAP with the last row, not an ordered erase"), Store[1].ExpiresAt, 30.0,
		0.0001);

	// The member form over `CNPC_VHengeyokai::m_BlacklistedEntities`.
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_blacklist"), 0x29c1b058);
	Builder.AddNpc(TEXT("boss"));
	Builder.AddNpc(TEXT("fish"), FVector(100.0 * U, 0.0, 0.0));
	Builder.AddNpc(TEXT("other"), FVector(200.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpc* Fish = Fixture.Npc(TEXT("fish"));
	FElysiumNpc* Other = Fixture.Npc(TEXT("other"));
	FElysiumNpcWorldFixture::Quiet({ Boss, Fish, Other });
	if (Boss == nullptr || Fish == nullptr || Other == nullptr)
	{
		AddError(TEXT("fixture did not stand three NPCs"));
		return false;
	}

	TestEqual(TEXT("an empty store finds nothing"), Boss->FindBlacklistedEntity(Fish), INDEX_NONE);
	const double Now = Fixture.World.NowSeconds();
	Boss->AddBlacklistedEntity(Fish);
	TestEqual(TEXT("the add appends one row"), Boss->HengeyokaiBlacklist.Num(), 1);
	TestEqual(TEXT("stamped curtime + 20.0 (_DAT_1044eb0c)"),
		Boss->HengeyokaiBlacklist[0].ExpiresAt, Now + 20.0, 0.001);
	TestEqual(TEXT("and the row is found by entity"), Boss->FindBlacklistedEntity(Fish), 0);
	TestEqual(TEXT("a different entity is not"), Boss->FindBlacklistedEntity(Other), INDEX_NONE);
	TestTrue(TEXT("and it is still blacklisted"), Boss->IsEntityBlacklisted(Fish));
	TestFalse(TEXT("while the other is not"), Boss->IsEntityBlacklisted(Other));

	// A second add is a second row, not a refresh — retail does not de-duplicate.
	Boss->AddBlacklistedEntity(Fish);
	TestEqual(TEXT("a second add of the same entity is a second row"),
		Boss->HengeyokaiBlacklist.Num(), 2);

	// Past the expiry the row is swept and the answer turns false.
	Boss->HengeyokaiBlacklist[0].ExpiresAt = Now - 1.0;
	Boss->HengeyokaiBlacklist[1].ExpiresAt = Now - 1.0;
	TestFalse(TEXT("an expired row answers false"), Boss->IsEntityBlacklisted(Fish));
	TestEqual(TEXT("and is removed"), Boss->HengeyokaiBlacklist.Num(), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 482 `CanPlaySequence` — `0x103850a0`, `0x10396e90`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesCanPlaySequenceTest,
	"Elysium.Substrate.NpcKernelBosses.CanPlaySequence", GBossesTestFlags)
bool FElysiumNpcKernelBossesCanPlaySequenceTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FCanPlaySequenceSpecies* Rows =
		FElysiumNpc::CanPlaySequenceSpeciesRows(Count);
	TestEqual(TEXT("this family owns two of slot 482's species bodies"), Count, 2);
	TMap<FString, FString> ByClass;
	for (int32 i = 0; i < Count; ++i)
	{
		ByClass.Add(FString(Rows[i].RetailClass), FString(Rows[i].Body));
	}
	TestEqual(TEXT("CNPC_VAndreiBlood's body"), ByClass.FindRef(TEXT("CNPC_VAndreiBlood")),
		FString(TEXT("0x103850a0")));
	TestEqual(TEXT("CNPC_VMingXiao's body"), ByClass.FindRef(TEXT("CNPC_VMingXiao")),
		FString(TEXT("0x10396e90")));
	const FElysiumNpc::FCanPlaySequenceSpecies* Andrei =
		FElysiumNpc::CanPlaySequenceSpeciesOf(TEXT("CNPC_VAndreiBlood"));
	TestTrue(TEXT("CNPC_VAndreiBlood resolves to its own row"),
		Andrei != nullptr && FCString::Strcmp(Andrei->Body, TEXT("0x103850a0")) == 0);
	const FElysiumNpc::FCanPlaySequenceSpecies* Ming =
		FElysiumNpc::CanPlaySequenceSpeciesOf(TEXT("CNPC_VMingXiao"));
	TestTrue(TEXT("CNPC_VMingXiao resolves to its own row"),
		Ming != nullptr && FCString::Strcmp(Ming->Body, TEXT("0x10396e90")) == 0);
	TestNull(TEXT("CNPC_VAnimal's body is family Species', not this family's"),
		FElysiumNpc::CanPlaySequenceSpeciesOf(TEXT("CNPC_VAnimal")));

	// The state arm, every branch of it. `Result` is 1 without a cine and 2 with one.
	using ES = EElysiumNpcState;
	auto Arm = [](int32 Result, bool bDisregard, int32 Level, ES State, ES Ideal)
	{
		return FElysiumNpc::CanPlaySequenceStateArm(Result, bDisregard, Level, State, Ideal);
	};
	// `fDisregardState` true skips the whole test.
	TestEqual(TEXT("fDisregardState hands the result straight back"),
		Arm(1, true, 0, ES::Combat, ES::Combat), 1);
	TestEqual(TEXT("and does so with a live cine too"),
		Arm(2, true, 0, ES::Combat, ES::Combat), 2);
	// Idle (retail's NONE and IDLE both) passes.
	TestEqual(TEXT("an idle NPC passes"), Arm(1, false, 0, ES::Idle, ES::Combat), 1);
	// An idle IDEAL state passes.
	TestEqual(TEXT("an idle ideal state passes"), Arm(1, false, 0, ES::Combat, ES::Idle), 1);
	// Alert (retail 3) with an interrupt level of at least 1 escapes the gate; below that it falls
	// through to the state-4 mask.
	TestEqual(TEXT("alert with level 1 passes"), Arm(1, false, 1, ES::Alert, ES::Combat), 1);
	TestEqual(TEXT("alert with level 0 falls to the mask and refuses"),
		Arm(1, false, 0, ES::Alert, ES::Combat), 0);
	// The mask keeps the result for retail state 4 ONLY — `Scripted` in this runtime's vocabulary,
	// since the image numbers 1 IDLE, 2 COMBAT, 3 ALERT, 7 DEAD and NOT SDK 2013's order. This is
	// the one thing these four species bodies do that the base `0x10278090` does not.
	TestEqual(TEXT("retail state 4 (Scripted) keeps the result"),
		Arm(1, false, 0, ES::Scripted, ES::Combat), 1);
	TestEqual(TEXT("and keeps a 2 as well"), Arm(2, false, 0, ES::Scripted, ES::Combat), 2);
	TestEqual(TEXT("combat (retail 2) is masked to 0"), Arm(1, false, 0, ES::Combat, ES::Combat), 0);
	TestEqual(TEXT("prone (retail 6) is masked to 0"), Arm(1, false, 0, ES::Prone, ES::Combat), 0);
	TestEqual(TEXT("dead (retail 7) is masked to 0"), Arm(2, false, 0, ES::Dead, ES::Combat), 0);

	// THE COMPOSITION with slot 482, asserted as a cross-product against the BASE body's own
	// recovered tail. `0x10278090` reaches the same gate and answers a flat 0 (`XOR EAX, EAX` at
	// `0x102780e8`); these four answer `((m_NPCState != 4) - 1) & result`. So the two agree on every
	// input except retail state 4, and that is exactly what this loop checks — no world needed, and
	// nothing here can drift if either body is edited.
	auto BaseTail = [](int32 Result, bool bDisregard, int32 Level, int32 RetailState,
		int32 RetailIdeal)
	{
		// `0x10278090`, verbatim: the identical gate, then `return 0`.
		if (!bDisregard && RetailState != 0 && RetailState != 1 && RetailIdeal != 1
			&& (RetailState != 3 || Level < 1))
		{
			return 0;
		}
		return Result;
	};
	struct FStateRow { ES State; int32 Retail; };
	const FStateRow StateRows[] =
	{
		{ ES::Idle, 1 }, { ES::Combat, 2 }, { ES::Alert, 3 }, { ES::Scripted, 4 },
		{ ES::Prone, 6 }, { ES::Dead, 7 },
	};
	int32 Divergences = 0;
	for (const FStateRow& S : StateRows)
	{
		for (const FStateRow& I : StateRows)
		{
			for (const int32 Result : { 1, 2 })
			{
				for (const int32 Level : { 0, 1 })
				{
					for (const bool bDisregard : { false, true })
					{
						const int32 Species = Arm(Result, bDisregard, Level, S.State, I.State);
						const int32 Base = BaseTail(Result, bDisregard, Level, S.Retail, I.Retail);
						if (S.Retail == 4 && Species != Base)
						{
							++Divergences;
							TestEqual(TEXT("at retail state 4 the species body keeps its result"),
								Species, Result);
							TestEqual(TEXT("where the base body refuses"), Base, 0);
						}
						else
						{
							TestEqual(TEXT("species and base agree everywhere but retail state 4"),
								Species, Base);
						}
					}
				}
			}
		}
	}
	// Six ideal states x two results x two levels, with `fDisregardState` false (true escapes the
	// gate entirely) and the ideal state not IDLE (which also escapes): 5 x 2 x 2 = 20.
	TestEqual(TEXT("the divergence appears only at retail state 4, 20 times"), Divergences, 20);

	// The member form, now that family Lifecycle has landed slot 158 `IsAlive` (`0x100b4dc0`) and
	// family Anim the Troika-line slot 482 (`0x10278090`). The head is SHARED — Anim's
	// `ScriptOwnerIsLive` and `CineAllowsDynamicInteraction`, Lifecycle's `IsAlive` — so the two
	// member bodies must agree on every state a fixture can reach, all of which are not 4.
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_cps"), 0x29c1b059);
	Builder.AddNpc(TEXT("andrei"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Andre = Fixture.Npc(TEXT("andrei"));
	FElysiumNpcWorldFixture::Quiet({ Andre });
	if (Andre == nullptr)
	{
		AddError(TEXT("fixture did not stand npc_VAndreiBlood"));
		return false;
	}
	// A LIVE body with no cine: the plain yes, and slot 482 says the same.
	TestTrue(TEXT("a fresh NPC is alive"), Andre->IsAlive());
	TestEqual(TEXT("a live body with no cine answers 1"), Andre->CanPlaySequenceSpecies(true, 0), 1);
	TestEqual(TEXT("and slot 482's base body agrees"), Andre->CanPlaySequence(true, 0), 1);
	TestEqual(TEXT("the state gate lets an idle body through too"),
		Andre->CanPlaySequenceSpecies(false, 0), 1);
	TestEqual(TEXT("as it does for the base"), Andre->CanPlaySequence(false, 0), 1);

	// The dead arm, driven by making the body dead rather than by leaning on a stub.
	Andre->bDead = true;
	TestFalse(TEXT("a dead body is not alive"), Andre->IsAlive());
	TestEqual(TEXT("and slot 158's refusal answers 0 whatever the state"),
		Andre->CanPlaySequenceSpecies(true, 0), 0);
	TestEqual(TEXT("for the base body as well"), Andre->CanPlaySequence(true, 0), 0);
	Andre->bDead = false;
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10385cf0` — slot 601's `CNPC_VAndreiBlood`-line body.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesMeleeReleaseTest,
	"Elysium.Substrate.NpcKernelBosses.MeleeRelease", GBossesTestFlags)
bool FElysiumNpcKernelBossesMeleeReleaseTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_melee"), 0x29c1b05a);
	Builder.AddNpc(TEXT("andrei"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Andre = Fixture.Npc(TEXT("andrei"));
	FElysiumNpcWorldFixture::Quiet({ Andre });
	if (Andre == nullptr)
	{
		AddError(TEXT("fixture did not stand npc_VAndreiBlood"));
		return false;
	}
	Andre->bInMelee = true;
	Andre->MeleeCanEnterTimer = 1234.0;
	Andre->FUN_10385cf0();
	// The global melee-left event fires FIRST and unconditionally — the difference from the Troika
	// line's `0x102b5880`, which does not fire it at all.
	TestEqual(TEXT("the global melee event fired once"), Andre->MeleeEventFires, 1);
	TestFalse(TEXT("m_bInMelee is cleared"), Andre->bInMelee);
	// Slot 308 `HasUsableRangedWeapon` is still a stub answering false, so the timer arm is not
	// taken and `m_flMeleeCanEnterTimer` is left exactly as it was.
	TestEqual(TEXT("m_flMeleeCanEnterTimer is untouched on the no-ranged arm"),
		Andre->MeleeCanEnterTimer, 1234.0, 0.0001);
	// The coordinator release is UNGUARDED here, unlike the Troika body, so it runs even with no
	// coordinator bound.
	TestEqual(TEXT("the coordinator release ran without a null guard"),
		Andre->MeleeCoordinatorReleases, 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1038e640`, `0x1038e670`, `0x1038e6a0`, `0x1038e6e0` — the four flap activities.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesFlapActivityTest,
	"Elysium.Substrate.NpcKernelBosses.FlapActivity", GBossesTestFlags)
bool FElysiumNpcKernelBossesFlapActivityTest::RunTest(const FString&)
{
	int32 Count = 0;
	const FElysiumNpc::FFlapActivity* Rows = FElysiumNpc::FlapActivityRows(Count);
	TestEqual(TEXT("four bodies, one behaviour"), Count, 4);

	// Every row by its retail address, with the activity it makes ideal and the duration it stamps.
	struct FExpect { const TCHAR* Body; int32 Activity; float Seconds; };
	const FExpect Expected[] =
	{
		{ TEXT("0x1038e640"), 0x22, 2.3f },     // _DAT_104bc690, a double
		{ TEXT("0x1038e670"), 0x24, 4.0f },     // _DAT_10449148, a double
		{ TEXT("0x1038e6a0"), 0x116d, 0.2f },   // _DAT_10449198, a double
		{ TEXT("0x1038e6e0"), 0x116e, 0.2f },   // the SAME cell as 0x1038e6a0
	};
	for (const FExpect& E : Expected)
	{
		const FElysiumNpc::FFlapActivity* Row = FElysiumNpc::FlapActivityOf(E.Body);
		if (Row == nullptr)
		{
			AddError(FString::Printf(TEXT("%s has no flap row"), E.Body));
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's activity"), E.Body), Row->Activity, E.Activity);
		TestEqual(FString::Printf(TEXT("%s's duration"), E.Body), Row->Seconds, E.Seconds, 0.0001f);
	}
	TestNull(TEXT("an address with no row"), FElysiumNpc::FlapActivityOf(TEXT("0x10000000")));

	FElysiumNpcWorldBuilder Builder(TEXT("bosses_flap"), 0x29c1b05b);
	Builder.AddNpc(TEXT("bat"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Bat = Fixture.Npc(TEXT("bat"));
	FElysiumNpcWorldFixture::Quiet({ Bat });
	if (Bat == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	const double Now = Fixture.World.NowSeconds();
	const FElysiumNpc::FFlapActivity* Row = FElysiumNpc::FlapActivityOf(TEXT("0x1038e640"));
	Bat->SetFlapActivity(Row->Activity, Row->Seconds);
	TestEqual(TEXT("m_IdealActivity took the row's activity"), Bat->IdealActivityNumber, 0x22);
	TestEqual(TEXT("m_flFlapTimer is curtime + the row's duration"), Bat->ManBatFlapTimer,
		Now + 2.3, 0.001);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1038bec0` and `0x1038b370` — the obstacle probe and the velocity producer.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesObstacleProbeTest,
	"Elysium.Substrate.NpcKernelBosses.ObstacleProbe", GBossesTestFlags)
bool FElysiumNpcKernelBossesObstacleProbeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_probe"), 0x29c1b05c);
	Builder.AddNpc(TEXT("bat"), FVector(10.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Bat = Fixture.Npc(TEXT("bat"));
	FElysiumNpcWorldFixture::Quiet({ Bat });
	if (Bat == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	// Family Motor's `KernelHullTrace` is a seam and answers a CLEAR sweep (fraction 1.0), which is
	// `0x1038bec0`'s "nothing in the way" arm: `vec3_origin` out, false back, and `m_flFlapTimer`
	// untouched. The blocked arm — a literal `(0, 0, 1)` steer and a `curtime` stamp unless the
	// current activity is already `0x22` — cannot be reached until a hull trace lands, and that is
	// stated rather than faked.
	Bat->ManBatFlapTimer = 99.0;
	FVector Steer(1.0, 2.0, 3.0);
	TestFalse(TEXT("the clear sweep answers false"),
		Bat->FUN_1038bec0(FVector(1.0, 0.0, 0.0), 500.f, Steer));
	TestTrue(TEXT("and writes vec3_origin"), Steer.IsNearlyZero());
	TestEqual(TEXT("and leaves m_flFlapTimer alone"), Bat->ManBatFlapTimer, 99.0, 0.0001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesFlightVelocityTest,
	"Elysium.Substrate.NpcKernelBosses.FlightVelocity", GBossesTestFlags)
bool FElysiumNpcKernelBossesFlightVelocityTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_flight"), 0x29c1b05d);
	Builder.AddNpc(TEXT("bat"), FVector(500.0 * U, 0.0, 0.0));
	Builder.AddNpc(TEXT("node"), FVector(520.0 * U, 0.0, 0.0));
	// Retail asks slot 158 `IsAlive` of ANY entity; this runtime declares it only on the NPC leaf,
	// so the body's liveness helper has two arms. A PROP fly-by target exercises the non-NPC one
	// (`!IsDead()`) and the NPC below exercises the slot itself, which family Lifecycle has landed.
	Builder.AddEntity(TEXT("prop_physics"), TEXT("flyby"), FVector(900.0 * U, 0.0, 0.0));
	Builder.AddNpc(TEXT("flybynpc"), FVector(900.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Bat = Fixture.Npc(TEXT("bat"));
	FElysiumNpc* Node = Fixture.Npc(TEXT("node"));
	FElysiumEntity* FlyBy = Fixture.World.FindByName(TEXT("flyby"));
	FElysiumNpcWorldFixture::Quiet({ Bat, Node });
	if (Bat == nullptr || Node == nullptr || FlyBy == nullptr)
	{
		AddError(TEXT("fixture did not stand the three entities"));
		return false;
	}
	FElysiumNpc::ResetManBatStationaryWatch();

	// --- Shape one: no fly node, mode neither 6 nor 7. The animation-driven velocity, with its Z
	//     SEEDED WITH THE LITERAL 10.0 rather than the velocity's own Z. The obstacle probe answers
	//     clear (its trace is a seam), so the probe vector is handed straight back.
	Bat->ManBatMoveGoalNodeMode = 0;
	Bat->Velocity = FVector(30.0 * U, 40.0 * U, 999.0 * U);
	FVector Out(-1.0, -1.0, -1.0);
	Bat->FUN_1038b370(0.1f, Out);
	TestEqual(TEXT("the X of m_vecAbsVelocity in source units"), Out.X, 30.0, 0.001);
	TestEqual(TEXT("the Y of m_vecAbsVelocity in source units"), Out.Y, 40.0, 0.001);
	TestEqual(TEXT("and a Z of the literal 10.0, NOT the velocity's own"), Out.Z, 10.0, 0.001);

	// --- The four activities that answer a dead stop before anything else runs.
	Bat->ManBatMoveGoalNodeMode = 6;
	for (const int32 Act : { 0x30, 0xb0, 0x4b, 0x1171 })
	{
		Bat->ActivityNumber = Act;
		Out = FVector(-1.0, -1.0, -1.0);
		Bat->FUN_1038b370(0.1f, Out);
		TestTrue(FString::Printf(TEXT("activity 0x%x answers a dead stop"), Act),
			Out.IsNearlyZero());
	}
	Bat->ActivityNumber = 0;

	// --- Mode 6 with no cached closest player: the plain-velocity fallback, all three components
	//     this time (unlike shape one's 10.0 seeding).
	FElysiumNpc::ResetManBatStationaryWatch();
	Bat->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	Bat->Velocity = FVector(11.0 * U, 12.0 * U, 13.0 * U);
	Out = FVector::ZeroVector;
	Bat->FUN_1038b370(0.1f, Out);
	TestEqual(TEXT("the plain fallback's X"), Out.X, 11.0, 0.001);
	TestEqual(TEXT("the plain fallback's Z is the real one"), Out.Z, 13.0, 0.001);

	// --- Mode 7 with a live fly-by target: the navigator seam refuses, which is retail's REFUSAL
	//     arm — `TaskFail(0x1a)` and the plain velocity.
	FElysiumNpc::ResetManBatStationaryWatch();
	Bat->ManBatMoveGoalNodeMode = 7;
	Bat->ManBatFlyByTarget = FlyBy->Handle;
	Out = FVector::ZeroVector;
	Bat->FUN_1038b370(0.1f, Out);
	TestEqual(TEXT("an unreachable fly-by target falls to the plain velocity"), Out.X, 11.0, 0.001);
	TestEqual(TEXT("and TaskFail recorded retail's 0x1a"), Bat->ScheduleHost.FailureReason, 0x1a);
	TestTrue(TEXT("with COND_TASK_FAILED raised"),
		Bat->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// The same arm with an NPC fly-by target, which reaches slot 158 itself. A LIVE one gets past
	// the gate and lands on the same navigator refusal; a DEAD one never reaches it, and the body
	// takes the plain velocity without failing a task.
	FElysiumNpc* FlyByNpc = Fixture.Npc(TEXT("flybynpc"));
	if (FlyByNpc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC fly-by target"));
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ FlyByNpc });
	FElysiumNpc::ResetManBatStationaryWatch();
	Bat->ManBatFlyByTarget = FlyByNpc->Handle;
	Bat->ScheduleHost.FailureReason = 0;
	Out = FVector::ZeroVector;
	Bat->FUN_1038b370(0.1f, Out);
	TestTrue(TEXT("a live NPC fly-by target passes slot 158"), FlyByNpc->IsAlive());
	TestEqual(TEXT("and reaches the same navigator refusal"), Bat->ScheduleHost.FailureReason, 0x1a);
	FElysiumNpc::ResetManBatStationaryWatch();
	FlyByNpc->bDead = true;
	Bat->ScheduleHost.FailureReason = 0;
	Out = FVector::ZeroVector;
	Bat->FUN_1038b370(0.1f, Out);
	TestEqual(TEXT("a dead fly-by target is refused by slot 158 before the probe, so no TaskFail"),
		Bat->ScheduleHost.FailureReason, 0);
	TestEqual(TEXT("and the plain velocity stands"), Out.X, 11.0, 0.001);
	FlyByNpc->bDead = false;
	Bat->ManBatFlyByTarget = FlyBy->Handle;

	// --- The fly-node arm and the overspeed latch. With the acceleration cvar unrecovered the
	//     clamp is zero, so the produced velocity IS the current one; the latch then fires when the
	//     remaining distance is under 0.2 of that velocity's length.
	FElysiumNpc::ResetManBatStationaryWatch();
	Bat->ManBatMoveGoalNodeMode = 0;
	Bat->ManBatFlyNode = Node->Handle;
	Bat->bManBatReachedMoveGoal = false;
	Bat->Velocity = FVector(1000.0 * U, 0.0, 0.0);   // |v| = 1000, 0.2 * 1000 = 200 > 20
	Out = FVector::ZeroVector;
	Bat->FUN_1038b370(0.1f, Out);
	TestEqual(TEXT("a zero acceleration clamp hands back the current velocity"), Out.X, 1000.0,
		0.01);
	TestTrue(TEXT("20 units to the node under 0.2 * 1000 latches m_bReachedMoveGoal"),
		Bat->bManBatReachedMoveGoal);

	// The same pass with a zero interval never reaches the latch at all.
	FElysiumNpc::ResetManBatStationaryWatch();
	Bat->bManBatReachedMoveGoal = false;
	Out = FVector::ZeroVector;
	Bat->FUN_1038b370(0.f, Out);
	TestFalse(TEXT("a zero interval skips the overspeed latch entirely"),
		Bat->bManBatReachedMoveGoal);

	// --- The stationary watchdog is a LEVEL-WIDE static, not a per-NPC word: standing still for
	//     more than 0.5 s runs the hint search, which is a seam and answers null, which zeroes the
	//     output velocity.
	FElysiumNpc::ResetManBatStationaryWatch();
	Bat->ManBatFlyNode = FElysiumEntityHandle::Invalid();
	Bat->ManBatMoveGoalNodeMode = 6;
	Bat->FUN_1038b370(0.1f, Out);                      // stamps the watch at this position
	FElysiumNpc::FManBatStationaryWatch& Watch = FElysiumNpc::ManBatStationaryWatch();
	TestEqual(TEXT("the watchdog stamped my position, in source units"), Watch.PositionUnits.X,
		500.0, 0.001);
	Watch.SinceTime -= 1.0;                            // more than 0.5 s ago
	Bat->TeleportEmitterPlacements.Reset();
	Out = FVector(9.0, 9.0, 9.0);
	Bat->FUN_1038b370(0.1f, Out);
	TestTrue(TEXT("a null hint answers a zero velocity"), Out.IsNearlyZero());
	TestEqual(TEXT("and places no emitter, because the search refused first"),
		Bat->TeleportEmitterPlacements.Num(), 0);
	TestFalse(TEXT("and requests no teleport"), Bat->bManBatTeleportRequested);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1038fb20` — `CNPC_VManBat`'s slot 102 trace filter.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesTraceFilterTest,
	"Elysium.Substrate.NpcKernelBosses.ManBatTraceFilter", GBossesTestFlags)
bool FElysiumNpcKernelBossesTraceFilterTest::RunTest(const FString&)
{
	// `CTraceFilterManBatNoIBeamEntity::ShouldHitEntity` `0x10006c4e`: `m_iName` matched against
	// `"lbeam*"` (`DAT_10642d28`, read from the image) by retail's `NameMatches`, which makes a
	// trailing `*` a case-insensitive PREFIX compare of the five characters before it.
	TestFalse(TEXT("an entity named lbeam is not hit"),
		FElysiumNpc::ManBatTraceFilterShouldHit(TEXT("lbeam")));
	TestFalse(TEXT("nor lbeam_01"), FElysiumNpc::ManBatTraceFilterShouldHit(TEXT("lbeam_01")));
	TestFalse(TEXT("and the compare is case-insensitive"),
		FElysiumNpc::ManBatTraceFilterShouldHit(TEXT("LBeam_Ceiling")));
	TestTrue(TEXT("a shorter name is hit"), FElysiumNpc::ManBatTraceFilterShouldHit(TEXT("lbea")));
	TestTrue(TEXT("a different name is hit"),
		FElysiumNpc::ManBatTraceFilterShouldHit(TEXT("beam_01")));
	TestTrue(TEXT("an unnamed entity is hit"), FElysiumNpc::ManBatTraceFilterShouldHit(FString()));

	// The wildcard idiom itself, which is the filter's whole content.
	TestTrue(TEXT("a pattern with no star is a whole-string compare"),
		FElysiumNpc::RetailNameMatches(TEXT("door01"), TEXT("door01")));
	TestFalse(TEXT("and refuses a prefix without the star"),
		FElysiumNpc::RetailNameMatches(TEXT("door01x"), TEXT("door01")));
	TestTrue(TEXT("the bare star matches everything — its strnicmp length is zero"),
		FElysiumNpc::RetailNameMatches(TEXT("anything"), TEXT("*")));
	TestFalse(TEXT("retail's `strlen + 1 == 1` arm answers m_iName == NULL_STRING"),
		FElysiumNpc::RetailNameMatches(TEXT("anything"), TEXT("")));
	TestTrue(TEXT("and so matches an unnamed entity"),
		FElysiumNpc::RetailNameMatches(FString(), TEXT("")));

	FElysiumNpcWorldBuilder Builder(TEXT("bosses_filter"), 0x29c1b05e);
	Builder.AddNpc(TEXT("bat"));
	Builder.AddNpc(TEXT("victim"), FVector(100.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Bat = Fixture.Npc(TEXT("bat"));
	FElysiumNpc* Victim = Fixture.Npc(TEXT("victim"));
	FElysiumNpcWorldFixture::Quiet({ Bat, Victim });
	if (Bat == nullptr || Victim == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}
	// The sweep is a seam and records the call; the vtable swap is the ported concern above.
	Bat->PhysicsTraceEntityManBat(Victim, FVector(0.0, 0.0, 0.0), FVector(100.0, 0.0, 0.0),
		0x202400b);
	TestEqual(TEXT("the sweep seam recorded one call"), Bat->PhysicsTraceEntityCalls.Num(), 1);
	TestEqual(TEXT("with retail's mask"),
		static_cast<int32>(Bat->PhysicsTraceEntityCalls[0].Mask), 0x202400b);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VMingXiao` — the tentacle rules.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesMingXiaoSelectorTest,
	"Elysium.Substrate.NpcKernelBosses.MingXiaoGrabSelector", GBossesTestFlags)
bool FElysiumNpcKernelBossesMingXiaoSelectorTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_mxsel"), 0x29c1b05f);
	Builder.AddNpc(TEXT("ming"));
	Builder.AddNpc(TEXT("thrown"), FVector(100.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpc* Thrown = Fixture.Npc(TEXT("thrown"));
	FElysiumNpcWorldFixture::Quiet({ Ming, Thrown });
	if (Ming == nullptr || Thrown == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}
	// `0x10396dc0`: a live `m_hThrowObject`, `m_eThrowableObjectMode` strictly inside (2, 5), then
	// condition 0x7b before 0x7c.
	Ming->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7b));
	TestEqual(TEXT("no throw object answers 0"), Ming->FUN_10396dc0(), 0);
	Ming->MingXiaoThrowObject = Thrown->Handle;
	Ming->MingXiaoThrowableObjectMode = 2;
	TestEqual(TEXT("mode 2 is outside the strict band"), Ming->FUN_10396dc0(), 0);
	Ming->MingXiaoThrowableObjectMode = 5;
	TestEqual(TEXT("mode 5 is outside the strict band"), Ming->FUN_10396dc0(), 0);
	Ming->MingXiaoThrowableObjectMode = 3;
	TestEqual(TEXT("mode 3 with condition 0x7b answers schedule 0x15c"), Ming->FUN_10396dc0(),
		0x15c);
	Ming->Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x7b));
	TestEqual(TEXT("neither condition answers 0"), Ming->FUN_10396dc0(), 0);
	Ming->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7c));
	Ming->MingXiaoThrowableObjectMode = 4;
	TestEqual(TEXT("mode 4 with condition 0x7c answers schedule 0x15d"), Ming->FUN_10396dc0(),
		0x15d);
	// 0x7b wins when both stand — retail tests it first.
	Ming->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7b));
	TestEqual(TEXT("0x7b is tested before 0x7c"), Ming->FUN_10396dc0(), 0x15c);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesMingXiaoRatesTest,
	"Elysium.Substrate.NpcKernelBosses.MingXiaoRates", GBossesTestFlags)
bool FElysiumNpcKernelBossesMingXiaoRatesTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_mxrate"), 0x29c1b060);
	Builder.AddNpc(TEXT("ming"));
	Builder.AddNpc(TEXT("tentacle"), FVector(50.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpc* Tentacle = Fixture.Npc(TEXT("tentacle"));
	FElysiumNpcWorldFixture::Quiet({ Ming, Tentacle });
	if (Ming == nullptr || Tentacle == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// `0x10398000` / `0x10398870`, the two one-line gates every MingXiao body reads.
	Ming->MingXiaoSeveredTentacleMask = 0;
	TestTrue(TEXT("an empty severed mask leaves every tentacle connected"),
		Ming->IsTentacleConnected(0) && Ming->IsTentacleConnected(5));
	Ming->MingXiaoSeveredTentacleMask = (1u << 4);
	TestFalse(TEXT("bit 4 severs tentacle 4"), Ming->IsTentacleConnected(4));
	TestTrue(TEXT("and leaves tentacle 5 alone"), Ming->IsTentacleConnected(5));
	TestFalse(TEXT("m_iTentacleID -1 is the head, not a proxy"), Ming->IsMingXiaoProxy());
	Ming->MingXiaoTentacleId = 2;
	TestTrue(TEXT("m_iTentacleID 2 is a proxy"), Ming->IsMingXiaoProxy());

	// `0x10397f70`: a proxy answers the flat cell, the head the clamped blend.
	FTuning Tuning;
	Tuning.Cells.Add(0x20, 7.5f);
	Tuning.Cells.Add(0x6c, 10.0f);
	Tuning.Cells.Add(0x70, -3.0f);
	auto Field = [&Tuning](int32 Offset) { return Tuning(Offset); };
	TestEqual(TEXT("a proxy answers Tuning[0x20] flat"), Ming->FUN_10397f70(Field), 7.5f, 0.0001f);
	Ming->MingXiaoTentacleId = INDEX_NONE;
	Ming->MingXiaoConnectedTentacleCount = 6;
	TestEqual(TEXT("six tentacles means a zero step and the base cell"), Ming->FUN_10397f70(Field),
		10.0f, 0.0001f);
	Ming->MingXiaoConnectedTentacleCount = 3;
	TestEqual(TEXT("three tentacles blend 10 + -3 * 3"), Ming->FUN_10397f70(Field), 1.0f, 0.0001f);
	Ming->MingXiaoConnectedTentacleCount = 0;
	TestEqual(TEXT("a negative blend clamps to the 0.0 floor"), Ming->FUN_10397f70(Field), 0.0f,
		0.0001f);

	// `0x10397a50`: the proxy-ready stamp and the sever, with the severed tentacle's OWN id.
	Tuning.Cells.Add(100, 4.0f);
	Tuning.Cells.Add(0x68, 0.5f);
	Ming->MingXiaoConnectedTentacleCount = 2;   // 6 - 2 = 4, so 4.0 + 0.5 * 4 = 6.0
	const double Now = Fixture.World.NowSeconds();
	Ming->MingXiaoProxyReadyTimer = 0.0;
	Ming->LastSeveredTentacle = INDEX_NONE;
	Tentacle->MingXiaoTentacleId = 3;
	Ming->Proxies[3] = Tentacle->Handle;
	Ming->FUN_10397a50(Tentacle, Field);
	TestEqual(TEXT("m_flProxyReadyTimer is curtime + the clamped blend"), Ming->MingXiaoProxyReadyTimer,
		Now + 6.0, 0.001);
	TestEqual(TEXT("and the registered proxy's own id was severed"), Ming->LastSeveredTentacle, 3);
	TestFalse(TEXT("the proxy handle is cleared"), Ming->Proxies[3].IsSet());

	// A tentacle that is not the registered proxy stamps the timer and severs nothing.
	Ming->LastSeveredTentacle = INDEX_NONE;
	Ming->FUN_10397a50(Tentacle, Field);
	TestEqual(TEXT("an unregistered tentacle severs nothing"), Ming->LastSeveredTentacle,
		INDEX_NONE);
	// A null tentacle does not even stamp.
	Ming->MingXiaoProxyReadyTimer = -1.0;
	Ming->FUN_10397a50(nullptr, Field);
	TestEqual(TEXT("a null tentacle leaves the timer alone"), Ming->MingXiaoProxyReadyTimer, -1.0,
		0.0001);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesMingXiaoAttackGateTest,
	"Elysium.Substrate.NpcKernelBosses.MingXiaoAttackGate", GBossesTestFlags)
bool FElysiumNpcKernelBossesMingXiaoAttackGateTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_mxgate"), 0x29c1b061);
	Builder.AddNpc(TEXT("ming"));
	Builder.AddNpc(TEXT("thrown"), FVector(100.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpc* Thrown = Fixture.Npc(TEXT("thrown"));
	FElysiumNpcWorldFixture::Quiet({ Ming, Thrown });
	if (Ming == nullptr || Thrown == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}
	Ming->MingXiaoSeveredTentacleMask = 0;
	Ming->SetBlockedByFriend(false);
	for (int32 i = 0; i < 6; ++i)
	{
		Ming->MingXiaoAttackTimers[i] = 0.0;
	}
	int32 Sched = 0;

	// Slots 0 and 1: `[100, 300)` units of closest-player distance, schedules 0x112a and 0x112b.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 200.f * U;
	TestTrue(TEXT("slot 0 inside [100, 300) opens"), Ming->FUN_10398030(0, false, Sched));
	TestEqual(TEXT("with schedule 0x112a"), Sched, 0x112a);
	TestTrue(TEXT("slot 1 opens on the same band"), Ming->FUN_10398030(1, false, Sched));
	TestEqual(TEXT("with schedule 0x112b"), Sched, 0x112b);
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 99.f * U;
	TestFalse(TEXT("under 100 units closes slot 0"), Ming->FUN_10398030(0, false, Sched));
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 300.f * U;
	TestFalse(TEXT("exactly 300 closes slot 0 — the ceiling is inclusive on the reject side"),
		Ming->FUN_10398030(0, false, Sched));

	// Slots 2 and 3: the same floor with a 200 ceiling.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 250.f * U;
	TestFalse(TEXT("250 units is past slot 2's 200 ceiling"), Ming->FUN_10398030(2, false, Sched));
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 150.f * U;
	TestTrue(TEXT("150 units opens slot 2"), Ming->FUN_10398030(2, false, Sched));
	TestEqual(TEXT("with schedule 0x112c"), Sched, 0x112c);
	TestTrue(TEXT("and slot 3"), Ming->FUN_10398030(3, false, Sched));
	TestEqual(TEXT("with schedule 0x112d"), Sched, 0x112d);

	// The severed-mask gate comes FIRST, before the timer and before the band.
	Ming->MingXiaoSeveredTentacleMask = (1u << 3);
	TestFalse(TEXT("a severed tentacle closes its slot outright"),
		Ming->FUN_10398030(3, false, Sched));
	Ming->MingXiaoSeveredTentacleMask = 0;

	// The per-slot timer gate is second.
	Ming->MingXiaoAttackTimers[3] = Fixture.World.NowSeconds() + 5.0;
	TestFalse(TEXT("a live m_rflAttackTimers entry closes the slot"),
		Ming->FUN_10398030(3, false, Sched));
	Ming->MingXiaoAttackTimers[3] = 0.0;

	// `m_bBlockedByFriend` closes every arm.
	Ming->SetBlockedByFriend(true);
	TestFalse(TEXT("blocked by a friend closes slot 3"), Ming->FUN_10398030(3, false, Sched));
	Ming->SetBlockedByFriend(false);

	// Slots 4 and 5: a 150 floor, a live `m_hThrowObject` and `m_eThrowingTentacle` matching the
	// slot — and the schedule STAYS -1, which is why the melee tail never runs for them.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 149.f * U;
	TestFalse(TEXT("under 150 units closes slot 4"), Ming->FUN_10398030(4, false, Sched));
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 150.f * U;
	TestFalse(TEXT("no throw object closes slot 4"), Ming->FUN_10398030(4, false, Sched));
	Ming->MingXiaoThrowObject = Thrown->Handle;
	Ming->MingXiaoThrowingTentacle = 5;
	TestFalse(TEXT("m_eThrowingTentacle 5 closes slot 4"), Ming->FUN_10398030(4, false, Sched));
	TestTrue(TEXT("and opens slot 5"), Ming->FUN_10398030(5, true, Sched));
	TestEqual(TEXT("slot 5 leaves the schedule at -1, so the melee tail is skipped"), Sched,
		INDEX_NONE);

	// A slot outside 0..5 falls through the switch and answers TRUE with no schedule, having passed
	// only the mask and timer gates. That is retail's, and it is reproduced.
	TestTrue(TEXT("a slot outside 0..5 falls through and answers true"),
		Ming->FUN_10398030(9, false, Sched));
	TestEqual(TEXT("with no schedule"), Sched, INDEX_NONE);

	// With `param_2` set, the melee tail is a seam that refuses, so a slot that would otherwise
	// open is closed.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 200.f * U;
	TestFalse(TEXT("the melee-tested form is refused by the slot-331 seam"),
		Ming->FUN_10398030(0, true, Sched));
	TestFalse(TEXT("and the seam answers retail's refusal"), Ming->ChooseMeleeAttackSequenceSeam());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesMingXiaoThrowCurveTest,
	"Elysium.Substrate.NpcKernelBosses.MingXiaoThrowCurve", GBossesTestFlags)
bool FElysiumNpcKernelBossesMingXiaoThrowCurveTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_mxthrow"), 0x29c1b062);
	Builder.AddNpc(TEXT("ming"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpcWorldFixture::Quiet({ Ming });
	if (Ming == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	// Cells chosen so the CHOICE is observable: each candidate is a distinct number.
	FTuning Tuning;
	Tuning.Cells.Add(0x34, 2.0f);    // slots 0/1, far
	Tuning.Cells.Add(0x38, 3.0f);    // slots 0/1, near
	Tuning.Cells.Add(0x3c, 4.0f);    // slots 2/3, far
	Tuning.Cells.Add(0x40, 5.0f);    // slots 2/3, near
	Tuning.Cells.Add(0x74, 10.0f);   // the shared blend base
	Tuning.Cells.Add(0x78, -2.0f);   // the shared blend step
	Tuning.Cells.Add(0x7c, 100.0f);  // slots 4/5, far base
	Tuning.Cells.Add(0x80, 1.0f);    // slots 4/5, far step
	Tuning.Cells.Add(0x84, 200.0f);  // slots 4/5, near base
	Tuning.Cells.Add(0x88, 2.0f);    // slots 4/5, near step
	auto Field = [&Tuning](int32 Offset) { return Tuning(Offset); };

	Ming->MingXiaoConnectedTentacleCount = 4;        // 6 - 4 = 2, so the blend is 10 + -2*2 = 6
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 100.f * U;   // <= 150, the near arm
	TestEqual(TEXT("slot 0 near: blend 6 times Tuning[0x38]"), Ming->FUN_103983d0(0, Field), 18.0f,
		0.001f);
	TestEqual(TEXT("slot 1 takes the same pair"), Ming->FUN_103983d0(1, Field), 18.0f, 0.001f);
	TestEqual(TEXT("slot 2 near: blend 6 times Tuning[0x40]"), Ming->FUN_103983d0(2, Field), 30.0f,
		0.001f);
	TestEqual(TEXT("slot 3 takes the same pair"), Ming->FUN_103983d0(3, Field), 30.0f, 0.001f);
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 200.f * U;   // > 150, the far arm
	TestEqual(TEXT("slot 0 far: blend 6 times Tuning[0x34]"), Ming->FUN_103983d0(0, Field), 12.0f,
		0.001f);
	TestEqual(TEXT("slot 2 far: blend 6 times Tuning[0x3c]"), Ming->FUN_103983d0(2, Field), 24.0f,
		0.001f);
	// Exactly 150 takes the NEAR arm — the compare is `<=`.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 150.f * U;
	TestEqual(TEXT("exactly 150 units is the near arm"), Ming->FUN_103983d0(0, Field), 18.0f,
		0.001f);

	// Slots 4/5 split on 200 units instead and take no second factor.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 200.f * U;   // <= 200, the near pair
	TestEqual(TEXT("slot 4 near: 200 + 2 * 2, with no scale multiply"),
		Ming->FUN_103983d0(4, Field), 204.0f, 0.001f);
	TestEqual(TEXT("slot 5 takes the same pair"), Ming->FUN_103983d0(5, Field), 204.0f, 0.001f);
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 201.f * U;   // > 200, the far pair
	TestEqual(TEXT("slot 4 far: 100 + 1 * 2"), Ming->FUN_103983d0(4, Field), 102.0f, 0.001f);

	// The floor on both shapes, and the out-of-range answer.
	Tuning.Cells.Add(0x74, -100.0f);
	TestEqual(TEXT("a negative blend clamps to 0 and the multiply keeps it there"),
		Ming->FUN_103983d0(0, Field), 0.0f, 0.001f);
	Tuning.Cells.Add(0x7c, -100.0f);
	TestEqual(TEXT("and slots 4/5 clamp the same way"), Ming->FUN_103983d0(4, Field), 0.0f, 0.001f);
	TestEqual(TEXT("a selector outside 0..5 answers 20.0 (_DAT_1044eb0c)"),
		Ming->FUN_103983d0(6, Field), 20.0f, 0.001f);
	TestEqual(TEXT("and so does a negative one"), Ming->FUN_103983d0(-1, Field), 20.0f, 0.001f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x103989b0` and `0x10398b20` — the pedestal pick.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesPedestalSideTest,
	"Elysium.Substrate.NpcKernelBosses.PedestalSide", GBossesTestFlags)
bool FElysiumNpcKernelBossesPedestalSideTest::RunTest(const FString&)
{
	// The pure rule, with a hand-built basis: forward +X, right +Y (this world's axes).
	const FVector Fwd(1.0, 0.0, 0.0);
	const FVector Right(0.0, 1.0, 0.0);
	int32 Task = -1;

	// The height gate is inclusive at 64 SOURCE units. The delta is abeam so the forward band lets
	// it through and the HEIGHT is the only thing under test: (50, 100) normalizes to a forward dot
	// of 0.447, inside `[-0.17, 0.5]`.
	TestFalse(TEXT("65 units of height difference closes it"),
		FElysiumNpc::PedestalTaskForSide(FVector(50.0, 100.0, 65.0), Fwd, Right, true, true, Task));
	TestTrue(TEXT("exactly 64 units is still inside"),
		FElysiumNpc::PedestalTaskForSide(FVector(50.0, 100.0, 64.0), Fwd, Right, true, true, Task));

	// The forward band: the NORMALIZED 2-D direction dotted with forward must be in [-0.17, 0.5].
	// Dead ahead is 1.0 and refused; abeam is 0.0 and admitted.
	TestFalse(TEXT("dead ahead is past the 0.5 ceiling"),
		FElysiumNpc::PedestalTaskForSide(FVector(100.0, 0.0, 0.0), Fwd, Right, true, true, Task));
	TestFalse(TEXT("dead behind is under the -0.17 floor"),
		FElysiumNpc::PedestalTaskForSide(FVector(-100.0, 0.0, 0.0), Fwd, Right, true, true, Task));

	// Abeam to the +Y side: right-dot is positive, so tentacle 4.
	Task = -1;
	TestTrue(TEXT("abeam to the right is admitted"),
		FElysiumNpc::PedestalTaskForSide(FVector(0.0, 100.0, 0.0), Fwd, Right, true, true, Task));
	TestEqual(TEXT("and takes tentacle 4"), Task, 4);
	// Abeam to the -Y side: right-dot is negative, so tentacle 5.
	Task = -1;
	TestTrue(TEXT("abeam to the left is admitted"),
		FElysiumNpc::PedestalTaskForSide(FVector(0.0, -100.0, 0.0), Fwd, Right, true, true, Task));
	TestEqual(TEXT("and takes tentacle 5"), Task, 5);

	// A severed tentacle closes its own side and does NOT fall through to the other.
	Task = -1;
	TestFalse(TEXT("a severed tentacle 4 closes the right side"),
		FElysiumNpc::PedestalTaskForSide(FVector(0.0, 100.0, 0.0), Fwd, Right, false, true, Task));
	TestFalse(TEXT("a severed tentacle 5 closes the left side"),
		FElysiumNpc::PedestalTaskForSide(FVector(0.0, -100.0, 0.0), Fwd, Right, true, false, Task));

	// A zero delta normalizes to zero rather than NaN (retail's `1 / (FLT_EPSILON + len)`), the
	// forward dot is 0 — inside the band — and the right dot's `<= 0` arm takes tentacle 5.
	Task = -1;
	TestTrue(TEXT("a candidate on top of me is admitted, not a NaN"),
		FElysiumNpc::PedestalTaskForSide(FVector::ZeroVector, Fwd, Right, true, true, Task));
	TestEqual(TEXT("and the r == 0 edge takes tentacle 5"), Task, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesPedestalSearchTest,
	"Elysium.Substrate.NpcKernelBosses.PedestalSearch", GBossesTestFlags)
bool FElysiumNpcKernelBossesPedestalSearchTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_pedestal"), 0x29c1b063);
	Builder.AddNpc(TEXT("ming"));
	Builder.AddEntity(TEXT("prop_physics"), TEXT("Pedestal_far"), FVector(200.0 * U, 0.0, 0.0));
	Builder.AddEntity(TEXT("prop_physics"), TEXT("Pedestal_near"), FVector(80.0 * U, 0.0, 0.0));
	Builder.AddEntity(TEXT("prop_physics"), TEXT("Pedestal_toofar"), FVector(300.0 * U, 0.0, 0.0));
	Builder.AddEntity(TEXT("prop_physics"), TEXT("Column_near"), FVector(40.0 * U, 0.0, 0.0));
	Builder.AddEntity(TEXT("prop_physics"), TEXT("Pedestal_high"), FVector(30.0 * U, 0.0, 90.0 * U));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpcWorldFixture::Quiet({ Ming });
	if (Ming == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	Ming->MingXiaoSeveredTentacleMask = 0;
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 400.f * U;

	// The whole body is closed by the `DAT_1093ba8c` cvar seam, whose name and default are
	// unrecovered: it answers 0, which is retail's own answer for an unconstructed cvar and the arm
	// that never searches at all.
	TestEqual(TEXT("the species cvar seam answers 0"), Ming->MingXiaoPedestalCvar(), 0);
	int32 Task = -1;
	FVector Aim = FVector::ZeroVector;
	FVector Forward = FVector::ZeroVector;
	TestNull(TEXT("so the whole pick answers null"), Ming->FUN_10398b20(Task, Aim, Forward));

	// The two gates in front of the cvar are recovered and are asserted through the same entry:
	// under 150 units of closest-player distance, and with both tentacles severed, it refuses.
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 149.f * U;
	TestNull(TEXT("under 150 units it refuses"), Ming->FUN_10398b20(Task, Aim, Forward));
	Ming->Senses.Memory.ClosestPlayerDistanceCm = 400.f * U;
	Ming->MingXiaoSeveredTentacleMask = (1u << 4) | (1u << 5);
	TestNull(TEXT("with both tentacles severed it refuses"),
		Ming->FUN_10398b20(Task, Aim, Forward));
	Ming->MingXiaoSeveredTentacleMask = 0;

	// The search itself, driven directly so the recovered rule is measured. `m_vecForward` and
	// `m_vecRight` are retail's cached basis and nothing writes them here, so every candidate inside
	// the height gate passes the abeam test on its `r == 0` edge and takes tentacle 5.
	Task = -1;
	FElysiumEntity* Winner = Ming->FindNearestPedestal(257.f, Task);
	if (Winner == nullptr)
	{
		AddError(TEXT("the search found no pedestal"));
		return false;
	}
	TestEqual(TEXT("the NEAREST Pedestal-named candidate wins"), Winner->TargetName,
		FString(TEXT("Pedestal_near")));
	TestEqual(TEXT("and the zero basis lands on the tentacle-5 edge"), Task, 5);

	// The name test is an eight-character case-insensitive PREFIX, so a differently named entity
	// closer than the winner is skipped, and one past 257 units is out of range.
	TestTrue(TEXT("Column_near is nearer but is not a Pedestal"),
		Winner->TargetName != TEXT("Column_near"));

	// The height gate rejects `Pedestal_high` (90 units up, past the 64-unit tolerance) even though
	// it is the nearest of all.
	TestTrue(TEXT("Pedestal_high is nearest but is 90 units up and is rejected"),
		Winner->TargetName != TEXT("Pedestal_high"));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The seams, asked and refusing.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBossesSeamsTest,
	"Elysium.Substrate.NpcKernelBosses.Seams", GBossesTestFlags)
bool FElysiumNpcKernelBossesSeamsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("bosses_seams"), 0x29c1b064);
	Builder.AddNpc(TEXT("boss"), FVector(100.0 * U, 200.0 * U, 300.0 * U));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpcWorldFixture::Quiet({ Boss });
	if (Boss == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	// Each of these answers NOTHING, and the refusal is retail's own arm, not a convenience.
	TestFalse(TEXT("CreateNoSpawn(\"phys_animlink\") answers an invalid handle"),
		Boss->CreatePhysAnimlink().IsSet());
	TestEqual(TEXT("the bone-table scan answers INDEX_NONE"),
		Boss->LookupBoneByName(TEXT("Bip01 R Hand")), INDEX_NONE);
	TestFalse(TEXT("CRagdollProp::GetElement answers false"),
		Boss->RagdollElementForBone(Boss, 0));
	FVector BonePos(1.0, 1.0, 1.0);
	TestFalse(TEXT("the named-bone world position answers false"),
		Boss->RagdollBonePosition(Boss, TEXT("Bone01"), BonePos));
	TestFalse(TEXT("the carried breakable read answers false"), Boss->IsCarriedBreakable(Boss));
	// NOT a seam any more: family Species landed `0x10366400` over `CNPC_VBaseBoss`'s own
	// `m_BlacklistedEntities` (+0x665c), so this is the real walk — false here because nothing has
	// been blacklisted, not because the store is missing.
	TestFalse(TEXT("CNPC_VBaseBoss's blacklist answers false for an unlisted entity"),
		Boss->BossBlacklistHolds(Boss));
	TestNull(TEXT("the ManBat move-goal hint search answers null"),
		Boss->ManBatFindMoveGoalHint(20000, 15000.f));
	TestFalse(TEXT("the navigator reachability probe answers false"),
		Boss->NavigatorCanReach(FVector::ZeroVector));
	TestEqual(TEXT("the ManBat acceleration cvar answers 0"), Boss->ManBatAccelerationCvar(), 0.f,
		0.0001f);

	// `SolveThrowImpulse` leaves its impulse exactly as it found it.
	FVector Impulse(1.0, 2.0, 3.0);
	Boss->SolveThrowImpulse(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), Impulse);
	TestEqual(TEXT("the ballistic solve leaves the impulse untouched"), Impulse.X, 1.0, 0.0001);

	// The two unit conversions the family's bodies depend on.
	Boss->Velocity = FVector(254.0, 0.0, 0.0);
	TestEqual(TEXT("m_vecAbsVelocity answers source units"), Boss->AbsVelocityUnits().X, 100.0,
		0.001);
	TestEqual(TEXT("and so does the entity velocity read"),
		Boss->EntityVelocityUnits(*Boss).X, 100.0, 0.001);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
