#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Senses** — one case per ported body, every threshold and every arm order
// taken from the decompiled C rather than from 29c's one-line walks (which this family found wrong
// in three places; the three corrections are asserted here by name).
//
// The census cross-check at the end is what keeps the six species rows honest: every retail class
// this family carries a body for is asked for that slot's address through
// `ElysiumNpcKernelClass::BodyOf`, so a row that drifts from `npc-kernel/slots.md` fails here.
//
// Two spawn facts this suite obeys (the brief's "Two tables, and they disagree"): `npc_VYukie`,
// `npc_VWerewolf` and `npc_VCameraSecurity` are NOT registered spawn leaves, so those rows are
// exercised by retail class NAME and the behaviour by calling the ported method on an ordinary
// spawned NPC; `npc_payphone` IS both a census classname and a spawn leaf, so `CPayphone` gets a
// real body.

static constexpr EAutomationTestFlags GElysiumNpcKernelSensesFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The two NPCs and the player every case below stands.
	struct FSensesFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;

		FSensesFixture()
			: World([]
			{
				FElysiumNpcWorldBuilder Builder(TEXT("senses_kernel"), 29103u);
				Builder.AddNpc(TEXT("guard"), FVector(0.f, 0.f, 0.f));
				Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f));
				return Builder;
			}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
		}
	};
}

// =================================================================================================
// Slot 196 `EarPosition` (`0x100b4c00`) and the two aim cones (`0x10326bd0`, `0x10326ae0`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesConesTest,
	"Elysium.Substrate.NpcKernelSenses.Cones", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesConesTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }

	// `0x100b4c00` — slot 196 tail-calls slot 193 and returns the same vector. The ear IS the eye.
	TestEqual(TEXT("0x100b4c00: EarPosition is EyePosition"), F.Guard->EarPosition(),
		F.Guard->EyePosition());

	// `0x10326bd0` — the aim cone's own arithmetic, driven through `AimConeAdmits` because slot 370
	// `HeadDirection2D` (which slot 372 forwards to) is still a generated stub.
	const FVector At = FVector::ZeroVector;
	const FVector Ahead(1000.f, 0.f, 0.f);
	TestTrue(TEXT("0x10326bd0: dead ahead is inside the 0.994 aim cone"),
		FElysiumNpc::AimConeAdmits(At, Ahead, FVector(1.f, 0.f, 0.f)));
	// cos(6.28 deg) == 0.994; 7 degrees off axis is outside, 5 degrees is inside.
	{
		const float Seven = FMath::DegreesToRadians(7.f);
		const FVector AimSeven(FMath::Cos(Seven), FMath::Sin(Seven), 0.f);
		TestFalse(TEXT("0x10326bd0: 7 degrees off axis is outside (0.994 is a 6.28 degree cone)"),
			FElysiumNpc::AimConeAdmits(At, Ahead, AimSeven));
		const float Five = FMath::DegreesToRadians(5.f);
		const FVector AimFive(FMath::Cos(Five), FMath::Sin(Five), 0.f);
		TestTrue(TEXT("0x10326bd0: 5 degrees off axis is inside"),
			FElysiumNpc::AimConeAdmits(At, Ahead, AimFive));
	}
	// **The correction 29c's walk missed**: the Z is zeroed BEFORE the normalise, so the cone is
	// planar in both operands. A target 1000 units ahead and 1000 up is still dead ahead.
	TestTrue(TEXT("0x10326bd0: the delta's Z is zeroed before the normalise, so height is ignored"),
		FElysiumNpc::AimConeAdmits(At, FVector(1000.f, 0.f, 1000.f), FVector(1.f, 0.f, 0.f)));
	// A 3-D normalise would give dot == 0.707 here and refuse, which is the whole difference.
	TestFalse(TEXT("0x10326bd0: and a 3-D normalise would have refused that same target"),
		FVector(1000.f, 0.f, 1000.f).GetSafeNormal().X > 0.994f);
	// Strictly greater, and a degenerate delta refuses.
	TestFalse(TEXT("0x10326bd0: a zero-length delta refuses"),
		FElysiumNpc::AimConeAdmits(At, At, FVector(1.f, 0.f, 0.f)));

	// The seam that makes the slot itself answer false today, asserted as the refusal it is.
	TestEqual(TEXT("slot 370 HeadDirection2D is still a stub, so EyeDirection2D is the zero vector"),
		F.Guard->EyeDirection2D(), FVector::ZeroVector);
	TestFalse(TEXT("0x10326bd0: so slot 364 refuses every target until that stub lands"),
		F.Guard->FInAimCone(FVector(1000.f, 0.f, 0.f)));

	// `0x10326ae0` — slot 365 is three dispatches; the null guard is this port's, and the entity
	// arm routes through slot 197 `BodyTarget`.
	TestFalse(TEXT("0x10326ae0: a null target refuses"), F.Guard->FInAimCone(nullptr));
	TestFalse(TEXT("0x10326ae0: and a live one falls to slot 364's stubbed aim"),
		F.Guard->FInAimCone(F.Other));

	// `0x1025e920` — `CAI_BaseActor::ValidEyeTarget`, the 0.5 cone, normalised in 3-D.
	TestTrue(TEXT("0x1025e920: dead ahead passes the 0.5 eye-target floor"),
		FElysiumNpc::EyeTargetConeAdmits(At, Ahead, FVector(1.f, 0.f, 0.f)));
	{
		// dot == 0.5 exactly is 60 degrees, and the compare is STRICT, so it refuses.
		const float Sixty = FMath::DegreesToRadians(60.f);
		const FVector HeadSixty(FMath::Cos(Sixty), FMath::Sin(Sixty), 0.f);
		TestFalse(TEXT("0x1025e920: exactly 60 degrees is refused - the compare is strict"),
			FElysiumNpc::EyeTargetConeAdmits(At, Ahead, HeadSixty));
		const float Fifty = FMath::DegreesToRadians(50.f);
		const FVector HeadFifty(FMath::Cos(Fifty), FMath::Sin(Fifty), 0.f);
		TestTrue(TEXT("0x1025e920: 50 degrees passes"),
			FElysiumNpc::EyeTargetConeAdmits(At, Ahead, HeadFifty));
	}
	// Unlike the aim cone, this one does NOT zero the Z. A target almost straight up but barely
	// ahead is refused here; the planar rule of slot 364 would have admitted the same pair.
	TestFalse(TEXT("0x1025e920: no Z zeroing - a near-vertical target is dotted in 3-D and fails"),
		FElysiumNpc::EyeTargetConeAdmits(At, FVector(100.f, 0.f, 1000.f),
			FVector(1.f, 0.f, 0.f)));
	TestTrue(TEXT("0x10326bd0: while the aim cone's planar rule admits that very pair"),
		FElysiumNpc::AimConeAdmits(At, FVector(100.f, 0.f, 1000.f), FVector(1.f, 0.f, 0.f)));
	TestFalse(TEXT("0x1025e920: slot 371 is a stub too, so the humanoid body refuses"),
		F.Guard->HumanoidValidEyeTarget(Ahead));

	// The census row this species body came from.
	const FElysiumNpcClass* Humanoid = ElysiumNpcKernelClass::Find(TEXT("CAI_BaseHumanoid"));
	TestNotNull(TEXT("CAI_BaseHumanoid is a census class"), Humanoid);
	TestEqual(TEXT("and its slot 587 body is 0x1025e920"),
		FString(ElysiumNpcKernelClass::BodyOf(Humanoid, 587)), FString(TEXT("0x1025e920")));
	return true;
}

// =================================================================================================
// Slots 167 / 168 / 541 / 543 — the enemy accessors and the `CAI_Enemies` store
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesEnemyTest,
	"Elysium.Substrate.NpcKernelSenses.Enemy", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesEnemyTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	// `0x101a67e0` — slot 167 resolves `m_hEnemy` through the handle table.
	TestNull(TEXT("0x101a67e0: no enemy resolves to null"),
		static_cast<const FElysiumNpc*>(F.Guard)->GetEnemy());
	F.Guard->Senses.Memory.Enemy = F.Other->Handle;
	TestEqual(TEXT("0x101a67e0: a live m_hEnemy resolves to the entity"),
		static_cast<const FElysiumNpc*>(F.Guard)->GetEnemy(),
		static_cast<FElysiumEntity*>(F.Other));

	// `0x10027020` — the BASE line's slot 168 is a bare tail jump to slot 167, no fallback.
	TestEqual(TEXT("0x10027020: the base line answers exactly slot 167"),
		F.Guard->GetEnemyBaseLine(), static_cast<FElysiumEntity*>(F.Other));

	// `0x102b5360` — the Troika line adds the last-enemy fallback, and it is GATED on
	// `m_bfNPCStateFlags & 0x40`, which belongs to retail states 0xb and 0xe alone.
	TestEqual(TEXT("0x102b5360: with a live enemy the Troika line answers it too"),
		F.Guard->GetEnemy(), static_cast<FElysiumEntity*>(F.Other));
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle();
	F.Guard->Senses.Memory.LastEnemy = F.Other->Handle;
	TestEqual(TEXT("0x102b5360: bit 6 of m_bfNPCStateFlags is clear for every port state"),
		static_cast<int32>(F.Guard->NpcStateFlags() & 0x40), 0);
	TestNull(TEXT("0x102b5360: so the m_hLastEnemy fallback is unreachable and it answers null"),
		F.Guard->GetEnemy());
	// The bit's own table is the reason, asserted at the source rather than inferred.
	TestEqual(TEXT("retail state 0xb (HUNT) is the state whose flag byte carries 0x40"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(0xb) & 0x40), 0x40);
	TestEqual(TEXT("and combat (2) does not"),
		static_cast<int32>(FElysiumNpcFlags::NpcStateFlagsForRetailState(2) & 0x40), 0);

	// `0x10273e10` — slot 541. The gate is `m_iSquadDisconnected < 1`, not `== 0`.
	F.Guard->ScheduleHost.SquadDisconnected = 0;
	TestEqual(TEXT("0x10273e10: a connected NPC gets its own CAI_Memory"), F.Guard->GetEnemies(),
		static_cast<void*>(&F.Guard->EnemyMemory));
	F.Guard->ScheduleHost.SquadDisconnected = -2;
	TestEqual(TEXT("0x10273e10: a NEGATIVE count is still connected - the test is < 1"),
		F.Guard->GetEnemies(), static_cast<void*>(&F.Guard->EnemyMemory));
	F.Guard->ScheduleHost.SquadDisconnected = 1;
	F.Other->ScheduleHost.SquadDisconnected = 1;
	TestNotEqual(TEXT("0x10273e10: disconnected gets the one shared global store instead"),
		F.Guard->GetEnemies(), static_cast<void*>(&F.Guard->EnemyMemory));
	TestEqual(TEXT("0x10273e10: and every disconnected NPC gets the SAME global"),
		F.Guard->GetEnemies(), F.Other->GetEnemies());
	F.Other->ScheduleHost.SquadDisconnected = 0;
	F.Guard->ScheduleHost.SquadDisconnected = 0;

	// `0x10273e40` — slot 543 frees the store only when there is no squad, and this substrate has
	// none, so the free always runs.
	F.Guard->EnemyMemory.UpdateAtPosition(*F.Guard, F.Other->Handle, F.Other->Origin, 1.0);
	TestTrue(TEXT("the store has a record to lose"), F.Guard->EnemyMemory.Num() > 0);
	TestNull(TEXT("0x10273e40: ConnectedSquad is null on every NPC here"),
		F.Guard->ConnectedSquad());
	F.Guard->RemoveMemory();
	TestEqual(TEXT("0x10273e40: so RemoveMemory releases the store"), F.Guard->EnemyMemory.Num(), 0);
	return true;
}

// =================================================================================================
// Slot 470 `OnListened` (`0x102b39e0`) and `CAI_Senses::GetClosestSound` (`0x103105d0`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesListenTest,
	"Elysium.Substrate.NpcKernelSenses.Listen", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesListenTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }

	FElysiumNpcSenses& Senses = F.Guard->Senses;
	FElysiumNpcMemory& Memory = Senses.Memory;

	// `0x103105d0` — the closest-sound reader, over a hand-built list so the two arms are visible.
	FElysiumGameSoundEvent Near;
	Near.TypeMask = ElysiumGameSounds::Combat;
	Near.Position = F.Guard->EarPosition() + FVector(100.f, 0.f, 0.f);
	FElysiumGameSoundEvent Far;
	Far.TypeMask = ElysiumGameSounds::Combat;
	Far.Position = F.Guard->EarPosition() + FVector(5000.f, 0.f, 0.f);
	Far.Source = F.Other->Handle;
	FElysiumGameSoundEvent OtherType;
	OtherType.TypeMask = ElysiumGameSounds::World;
	OtherType.Position = F.Guard->EarPosition();
	Senses.HeardThisPass = { Near, Far, OtherType };

	TestEqual(TEXT("0x103105d0: with no enemy the NEAREST of that type wins"),
		Senses.ClosestSound(*F.Guard, ElysiumGameSounds::Combat)->Position, Near.Position);
	TestEqual(TEXT("0x103105d0: a different type is never a candidate"),
		Senses.ClosestSound(*F.Guard, ElysiumGameSounds::World)->Position, OtherType.Position);
	TestNull(TEXT("0x103105d0: a type nobody emitted answers null"),
		Senses.ClosestSound(*F.Guard, ElysiumGameSounds::Bugbait));
	Memory.Enemy = F.Other->Handle;
	TestEqual(TEXT("0x103105d0: the ENEMY's sound outranks a nearer one and ends the walk"),
		Senses.ClosestSound(*F.Guard, ElysiumGameSounds::Combat)->Position, Far.Position);
	Memory.Enemy = FElysiumEntityHandle();

	// `0x102b39e0` — the seven snapshots, each gated on its own `m_HeardConditions` bit. Nothing
	// heard, nothing written.
	Memory.LastSoundCombat = FElysiumGameSoundEvent();
	Memory.LastSoundWorld = FElysiumGameSoundEvent();
	Senses.HeardConditions.Reset();
	F.Guard->OnListened();
	TestEqual(TEXT("0x102b39e0: an unheard category is not snapshotted"),
		Memory.LastSoundCombat.Position, FVector::ZeroVector);

	Senses.HeardConditions.Set(EElysiumNpcCond::HearCombat);
	F.Guard->OnListened();
	TestEqual(TEXT("0x102b39e0: HEAR_COMBAT snapshots the closest combat sound"),
		Memory.LastSoundCombat.Position, Near.Position);
	TestEqual(TEXT("0x102b39e0: and leaves the world record alone - one bit, one record"),
		Memory.LastSoundWorld.Position, FVector::ZeroVector);

	Senses.HeardConditions.Set(EElysiumNpcCond::HearWorld);
	F.Guard->OnListened();
	TestEqual(TEXT("0x102b39e0: HEAR_WORLD snapshots the world record"),
		Memory.LastSoundWorld.Position, OtherType.Position);

	// The tail reads `m_Conditions` (+0x5c5c), NOT `m_HeardConditions` (+0x5ca8) — a sound still
	// inside its reaction delay snapshots but does not extend the stealth-vision override. The
	// heard bits are cleared first so only the tail runs and the record under test stays put.
	Senses.HeardConditions.Reset();
	Memory.StealthVisionOverrideUntil = -1.0;
	Memory.LastSoundCombat.Source = F.Other->Handle;
	F.Guard->Cognition.Conditions.Clear(EElysiumNpcCond::HearCombat);
	F.Guard->OnListened();
	TestEqual(TEXT("0x102b39e0: the heard bit alone does not extend the vision override"),
		Memory.StealthVisionOverrideUntil, -1.0);
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::HearCombat);
	F.Guard->OnListened();
	TestTrue(TEXT("0x102b39e0: the PROMOTED condition does, with strength 1.0"),
		Memory.StealthVisionOverrideUntil > 0.0);
	return true;
}

// =================================================================================================
// Slot 45's two species bodies (`0x101aaf80`, `0x103a4bb0`) and `CAI_Hint#163` (`0x102d1320`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesFovTraceTest,
	"Elysium.Substrate.NpcKernelSenses.FovTrace", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesFovTraceTest::RunTest(const FString&)
{
	// `npc_payphone` IS a registered spawn leaf AND a census classname, so `CPayphone` gets a body.
	FElysiumNpcWorldBuilder Builder(TEXT("senses_fov"), 29104u);
	Builder.AddNpc(TEXT("phone"), FVector::ZeroVector, TEXT("npc_payphone"));
	Builder.AddNpc(TEXT("caller"), FVector(50.f, 0.f, 0.f));
	Builder.AddNpc(TEXT("distant"), FVector(100000.f, 0.f, 0.f));
	FElysiumNpcWorldFixture World(MoveTemp(Builder));
	FElysiumNpc* Phone = World.Npc(TEXT("phone"));
	FElysiumNpc* Caller = World.Npc(TEXT("caller"));
	FElysiumNpc* Distant = World.Npc(TEXT("distant"));
	if (!TestNotNull(TEXT("npc_payphone is a registered spawn leaf"), Phone)) { return false; }
	if (!TestNotNull(TEXT("caller spawned"), Caller)) { return false; }
	if (!TestNotNull(TEXT("distant spawned"), Distant)) { return false; }
	FElysiumNpcWorldFixture::Quiet({ Phone, Caller, Distant });

	// `0x101aaf80` — the MANHATTAN gate at 85.0 Source units. `distant` is 100000 cm away, so it
	// fails on distance whatever the boxes say.
	TestFalse(TEXT("0x101aaf80: past the 85-unit Manhattan limit it refuses"),
		Phone->PayphonePassesFindEntityFovTrace(*Distant));
	// Inside the limit it reaches the AABB overlap, whose only source — family Motor's
	// `RetailCollisionExtents` — is a seam, so the second half refuses. That refusal is the
	// recovered seam, not the recovered rule.
	TestFalse(TEXT("0x101aaf80: inside it, the OBB overlap's extents seam refuses"),
		Phone->PayphonePassesFindEntityFovTrace(*Caller));
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	TestFalse(TEXT("0x101aaf80: and that seam is RetailCollisionExtents answering nothing"),
		FElysiumNpc::RetailCollisionExtents(*Caller, Mins, Maxs));

	// The census rows both slot-45 bodies came from.
	const FElysiumNpcClass* Payphone = ElysiumNpcKernelClass::Find(TEXT("CPayphone"));
	const FElysiumNpcClass* Prone = ElysiumNpcKernelClass::Find(TEXT("CNPC_ProneDialog"));
	TestEqual(TEXT("CPayphone#45 is 0x101aaf80"),
		FString(ElysiumNpcKernelClass::BodyOf(Payphone, 45)), FString(TEXT("0x101aaf80")));
	TestEqual(TEXT("CNPC_ProneDialog#45 is 0x103a4bb0"),
		FString(ElysiumNpcKernelClass::BodyOf(Prone, 45)), FString(TEXT("0x103a4bb0")));
	TestEqual(TEXT("and npc_payphone resolves to CPayphone"),
		FString(ElysiumNpcKernelClass::OfClassname(TEXT("npc_payphone"))->Name),
		FString(TEXT("CPayphone")));

	// `0x103a4bb0` — the prone-dialog ray. `npc_VProneDialog` is a census classname but NOT a
	// registered spawn leaf, so the body is driven on an ordinary NPC.
	bool bRayValid = false;
	TestTrue(TEXT("0x103a4bb0: a clear segment passes (the tr.m_pEnt == NULL arm)"),
		Caller->ProneDialogPassesFindEntityFovTrace(FVector::ZeroVector, FVector(100.f, 0.f, 0.f),
			0x202400b, bRayValid));
	TestTrue(TEXT("0x103a4bb0: and the ray's own IsRay byte is the != 0.0 squared length"),
		bRayValid);
	Caller->ProneDialogPassesFindEntityFovTrace(FVector(7.f, 8.f, 9.f), FVector(7.f, 8.f, 9.f),
		0x202400b, bRayValid);
	TestFalse(TEXT("0x103a4bb0: a degenerate ray clears that byte"), bRayValid);

	// `0x102d1320` — `CAI_Hint::IsViewable`, pure over the hint's own words.
	FElysiumNpc::FHintWords Hint;
	Hint.HintType = 13;
	Hint.Disabled = 0;
	TestTrue(TEXT("0x102d1320: hint type 13 is viewable"), FElysiumNpc::IsHintViewable(Hint));
	Hint.HintType = 12;
	TestFalse(TEXT("0x102d1320: and 12 is not - the literal is 0xd and nothing else"),
		FElysiumNpc::IsHintViewable(Hint));
	Hint.HintType = 13;
	Hint.Disabled = 1;
	TestFalse(TEXT("0x102d1320: a disabled hint is never viewable"),
		FElysiumNpc::IsHintViewable(Hint));
	Hint.Disabled = 0x100;
	TestFalse(TEXT("0x102d1320: including one whose m_iDisabled has a non-zero HIGH byte - the "
		"returned mask's low byte is what the caller reads"), FElysiumNpc::IsHintViewable(Hint));
	return true;
}

// =================================================================================================
// The species sense overrides — `0x103cb810`, `0x103ddaf0`, `0x103ddaa0`, `0x1036a030`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesSpeciesTest,
	"Elysium.Substrate.NpcKernelSenses.Species", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesSpeciesTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }
	FElysiumPlayer* Player = F.World.Player();
	if (!TestNotNull(TEXT("the world stands a player"), Player)) { return false; }

	// The shared three-test gate. With both ConVars off — their default — everything live passes.
	TestFalse(TEXT("the gate refuses a null candidate"),
		F.Guard->SpeciesStealthSenseGate(nullptr));
	TestTrue(TEXT("and admits a live one with both debug ConVars off"),
		F.Guard->SpeciesStealthSenseGate(F.Other));
	TestTrue(TEXT("including the player"), F.Guard->SpeciesStealthSenseGate(Player));

	// `0x103cb810` — the werewolf has NO visibility test at all.
	FElysiumEntityHandle Blocker = F.Other->Handle;
	TestTrue(TEXT("0x103cb810: CNPC_VWerewolf::FVisible answers true unconditionally"),
		F.Guard->WerewolfFVisible(F.Other, &Blocker));
	TestTrue(TEXT("0x103cb810: and leaves the blocker alone on the success arm"),
		Blocker == F.Other->Handle);
	TestFalse(TEXT("0x103cb810: a null candidate refuses"),
		F.Guard->WerewolfFVisible(nullptr, &Blocker));
	TestTrue(TEXT("0x103cb810: and does NOT zero the blocker - retail's own asymmetry"),
		Blocker == F.Other->Handle);

	// `0x103ddaa0` — Yukie has no view cone either.
	TestTrue(TEXT("0x103ddaa0: CNPC_VYukie::FInViewCone admits any live candidate"),
		F.Guard->YukieFInViewCone(F.Other));
	TestFalse(TEXT("0x103ddaa0: and refuses only a null one"),
		F.Guard->YukieFInViewCone(nullptr));

	// `0x103ddaf0` — Yukie's FVisible chains slot 594 instead of answering true. Slot 594 is story
	// 29d's declared stub, so the chain answers false; the GATE above it is the recovered half.
	TestFalse(TEXT("0x103ddaf0: CNPC_VYukie::FVisible chains slot 594, which is still a stub"),
		F.Guard->YukieFVisible(F.Other, &Blocker));
	TestFalse(TEXT("0x103ddaf0: a null candidate refuses before the gate"),
		F.Guard->YukieFVisible(nullptr, &Blocker));

	// `0x1036a030` — the security camera sees the PLAYER and nothing else. It replaces the base
	// QuerySeeEntity rather than adding to it.
	TestTrue(TEXT("0x1036a030: CNPC_VCameraSecurity::QuerySeeEntity admits the player"),
		F.Guard->CameraSecurityQuerySeeEntity(*Player));
	TestFalse(TEXT("0x1036a030: and refuses every NPC, whatever the base body would say"),
		F.Guard->CameraSecurityQuerySeeEntity(*F.Other));

	// Every species row by name, against the census. None of these three classnames is a registered
	// spawn leaf, which is exactly why the rows are checked by retail class name.
	const FElysiumNpcClass* Werewolf = ElysiumNpcKernelClass::Find(TEXT("CNPC_VWerewolf"));
	const FElysiumNpcClass* Yukie = ElysiumNpcKernelClass::Find(TEXT("CNPC_VYukie"));
	const FElysiumNpcClass* Camera = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCameraSecurity"));
	TestEqual(TEXT("CNPC_VWerewolf#201 is 0x103cb810"),
		FString(ElysiumNpcKernelClass::BodyOf(Werewolf, 201)), FString(TEXT("0x103cb810")));
	TestEqual(TEXT("CNPC_VYukie#201 is 0x103ddaf0"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 201)), FString(TEXT("0x103ddaf0")));
	TestEqual(TEXT("CNPC_VYukie#363 is 0x103ddaa0"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 363)), FString(TEXT("0x103ddaa0")));
	TestEqual(TEXT("CNPC_VCameraSecurity#468 is 0x1036a030"),
		FString(ElysiumNpcKernelClass::BodyOf(Camera, 468)), FString(TEXT("0x1036a030")));
	TestEqual(TEXT("CNPC_VYukie#602 is 0x103dda10"),
		FString(ElysiumNpcKernelClass::BodyOf(Yukie, 602)), FString(TEXT("0x103dda10")));
	// The werewolf's census classname list is null, so a spawned one could not be found by
	// classname even if the spawn registry stood it. Assert the fact rather than working around it.
	TestEqual(TEXT("CNPC_VWerewolf claims no entity classname in the census"),
		Werewolf->ClassnameCount, 0);
	TestNull(TEXT("and npc_VYukie is not a registered spawn leaf, so nothing resolves it here"),
		F.World.Npc(TEXT("npc_VYukie")));
	return true;
}

// =================================================================================================
// The two witness-record setters — `0x1028ea60`, `0x1028eb30`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesWitnessTest,
	"Elysium.Substrate.NpcKernelSenses.Witness", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesWitnessTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }

	// The `CSecureType` pair, both directions, on the values a witnessed level can hold.
	for (uint32 Level = 0; Level <= 6; ++Level)
	{
		TestEqual(TEXT("0x1042fde0/0x1042fe90: the scramble round-trips"),
			FElysiumNpc::DecodeWitnessedLevel(FElysiumNpc::EncodeWitnessedLevel(Level)), Level);
	}
	TestNotEqual(TEXT("and it really is a scramble, not an identity"),
		FElysiumNpc::EncodeWitnessedLevel(3u), 3u);

	// `0x1028ea60` — the criminal record, written whole.
	const FVector Crime(120.f, 240.f, 8.f);
	F.Guard->RecordCriminalWitness(4, Crime, F.Other);
	const FElysiumNpcWitnessChannel& Criminal =
		F.Guard->Witness.Channel(ElysiumNpcWitness::EChannel::Criminal);
	TestEqual(TEXT("0x1028ea60: the level lands"), Criminal.Level, 4);
	TestEqual(TEXT("0x1028ea60: the three-float location lands"), Criminal.Location, Crime);
	TestTrue(TEXT("0x1028ea60: the offender handle lands"), Criminal.Offender == F.Other->Handle);
	F.Guard->RecordCriminalWitness(2, Crime, nullptr);
	TestFalse(TEXT("0x1028ea60: a null offender writes the -1 sentinel"),
		F.Guard->Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Offender.IsSet());
	// The recovered defect: both bytes come off uninitialised stack in retail and 0 here.
	TestEqual(TEXT("0x1028ea60: +0x6360 is an uninitialised retail read, ported as 0"),
		static_cast<int32>(F.Guard->CriminalWitnessByte6360), 0);
	TestEqual(TEXT("0x1028ea60: +0x6361 likewise"),
		static_cast<int32>(F.Guard->CriminalWitnessByte6361), 0);

	// `0x1028eb30` — the supernatural record. The level is PLAIN, and the flee-only byte is
	// written on BOTH arms.
	const FVector Sighting(-40.f, 12.f, 180.f);
	F.Guard->RecordSupernaturalWitness(5, Sighting, F.Other, true);
	const FElysiumNpcWitnessChannel& Super =
		F.Guard->Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural);
	TestEqual(TEXT("0x1028eb30: the level lands"), Super.Level, 5);
	TestEqual(TEXT("0x1028eb30: the location lands"), Super.Location, Sighting);
	TestTrue(TEXT("0x1028eb30: the offender lands"), Super.Offender == F.Other->Handle);
	TestTrue(TEXT("0x1028eb30: flee-only lands with an offender"),
		F.Guard->Witness.bSupernaturalFleeOnly);
	F.Guard->RecordSupernaturalWitness(1, Sighting, nullptr, false);
	TestFalse(TEXT("0x1028eb30: and on the NULL arm too - retail writes it on both"),
		F.Guard->Witness.bSupernaturalFleeOnly);
	TestFalse(TEXT("0x1028eb30: with the -1 offender sentinel"),
		F.Guard->Witness.Channel(ElysiumNpcWitness::EChannel::Supernatural).Offender.IsSet());
	// The two channels are independent, which is what two setters means.
	TestEqual(TEXT("the criminal record is untouched by the supernatural setter"),
		F.Guard->Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Location, Crime);
	return true;
}

// =================================================================================================
// `OnDoorBlocked` (`0x1027de00`) and slot 86 `ShouldTransmit` (`0x102c0420`)
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesDoorTest,
	"Elysium.Substrate.NpcKernelSenses.Door", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesDoorTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	// Arm 6 — the write that names this body. 29c filed it as `SetEnemy`; it writes
	// `m_hBlockedDoor` (+0x5d28) and never touches `m_hEnemy`.
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle();
	F.Guard->OnDoorBlocked(*F.Other);
	TestTrue(TEXT("0x1027de00: the door lands in m_hBlockedDoor"),
		F.Guard->BlockedDoor == F.Other->Handle);
	TestFalse(TEXT("0x1027de00: and m_hEnemy is untouched - this is not SetEnemy"),
		F.Guard->Senses.Memory.Enemy.IsSet());

	// Arm 4 — the door's own retry stamp is written whatever the flags say, because the seam
	// answers "a plain door" (no 0x10) and so the block runs.
	const int32 BeforeStamps = F.Guard->DoorNextTryWrites;
	F.Guard->OnDoorBlocked(*F.Other);
	TestEqual(TEXT("0x1027de00: the door's next-try stamp is written once per call"),
		F.Guard->DoorNextTryWrites, BeforeStamps + 1);
	// The navigator mark is gated on 0x102ee6a0, which has no node graph here.
	TestFalse(TEXT("0x1027de00: 0x102ee6a0 answers false - there is no AI network"),
		F.Guard->NavigatorHasNodeGraph());
	TestEqual(TEXT("0x1027de00: so the unreachable mark never runs"),
		F.Guard->NavigatorUnreachableMarks, 0);
	TestEqual(TEXT("0x1027de00: the door flag word is a seam answering a plain door"),
		static_cast<int32>(FElysiumNpc::DoorBlockFlags(*F.Other)), 0);

	// Arm 5 — the squad focus. `ConnectedSquad()` is null on every NPC here, so the whole arm is
	// skipped; that is retail's own answer for a squadless NPC.
	TestEqual(TEXT("0x1027de00: no squad, so no focus write"), F.Guard->SquadFocusWrites, 0);

	// Arm 7 — the alternate-AI promotion. Modes 1 and 2 advance to 3 and arm a 1.0 s window; any
	// other mode is left alone.
	F.Guard->AlternateAi = 0;
	F.Guard->AlternateAiExpireTime = 0.0;
	F.Guard->OnDoorBlocked(*F.Other);
	TestEqual(TEXT("0x1027de00: mode 0 is not promoted"), F.Guard->AlternateAi, 0);
	for (int32 Mode : { 1, 2 })
	{
		F.Guard->AlternateAi = Mode;
		F.Guard->AlternateAiExpireTime = 0.0;
		F.Guard->OnDoorBlocked(*F.Other);
		TestEqual(TEXT("0x1027de00: modes 1 and 2 advance to 3"), F.Guard->AlternateAi, 3);
		TestEqual(TEXT("0x1027de00: with curtime + 1.0 on the expire timer"),
			F.Guard->AlternateAiExpireTime, F.Guard->World->NowSeconds() + 1.0);
	}
	F.Guard->AlternateAi = 3;
	F.Guard->AlternateAiExpireTime = 0.0;
	F.Guard->OnDoorBlocked(*F.Other);
	TestEqual(TEXT("0x1027de00: mode 3 is not re-armed"), F.Guard->AlternateAiExpireTime, 0.0);

	// Arm 1 — a dead NPC does nothing at all.
	F.Guard->AlternateAi = 1;
	F.Guard->BlockedDoor = FElysiumEntityHandle();
	F.Guard->bDead = true;
	F.Guard->OnDoorBlocked(*F.Other);
	TestFalse(TEXT("0x1027de00: IsAlive() gates the whole body"), F.Guard->BlockedDoor.IsSet());
	F.Guard->bDead = false;

	// Slot 86 `ShouldTransmit` (`0x102c0420`). `0x102c1170` is NAMED `IsInDialog` and FORCES the
	// transmit; the base body has no port counterpart and this substrate has no PVS culling, so
	// both arms answer true.
	TestTrue(TEXT("0x102c0420: an NPC not in dialog still transmits (no PVS here)"),
		F.Guard->ShouldTransmit(0, nullptr, nullptr, 0, 0));
	return true;
}

// =================================================================================================
// The occlusion edge, the look distance and the two species pursuit bodies
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSensesVisionTest,
	"Elysium.Substrate.NpcKernelSenses.Vision", GElysiumNpcKernelSensesFlags)
bool FElysiumNpcKernelSensesVisionTest::RunTest(const FString&)
{
	FSensesFixture F;
	if (!TestNotNull(TEXT("guard spawned"), F.Guard)) { return false; }
	if (!TestNotNull(TEXT("other spawned"), F.Other)) { return false; }

	FElysiumNpcSenses& Senses = F.Guard->Senses;
	FElysiumNpcMemory& Memory = Senses.Memory;

	// `0x1026a2a0` — `SetDistLook` writes `CAI_Senses+0x10`, which is `m_LookDist` and nothing
	// unrecovered.
	F.Guard->SetDistLook(777.f);
	TestEqual(TEXT("0x1026a2a0: SetDistLook writes m_LookDist"), Senses.LookDistCm, 777.f);

	// `0x1029c970` — the effective look distance. The default is the resolved vision channel.
	Senses.Perception.VisionDistanceCm = 1200.f;
	Memory.StealthVisionOverrideUntil = -1.0;
	Memory.bEnemyWentOccluded = false;
	TestEqual(TEXT("0x1029c970: with no override it is m_flVisionDistance"),
		Senses.EffectiveVisionDistanceCm(*F.Guard, 10.0), 1200.f);
	Memory.StealthVisionOverrideUntil = 20.0;
	TestEqual(TEXT("0x1029c970: inside the stealth-vision window it is m_LookDist instead"),
		Senses.EffectiveVisionDistanceCm(*F.Guard, 10.0), 777.f);
	TestEqual(TEXT("0x1029c970: and the window is strict - curtime AT the deadline is outside"),
		Senses.EffectiveVisionDistanceCm(*F.Guard, 20.0), 1200.f);
	// The floor: `_DAT_104454c4` is 0.0 and the clamp is upward.
	Senses.LookDistCm = -50.f;
	Memory.StealthVisionOverrideUntil = 20.0;
	TestEqual(TEXT("0x1029c970: the answer is clamped up to 0.0"),
		Senses.EffectiveVisionDistanceCm(*F.Guard, 10.0), 0.f);
	Senses.LookDistCm = 777.f;
	// The second override arm needs NPC state 2 (COMBAT). The mind is private and every write to it
	// is arbitrated, so a case cannot force it without standing a second producer of NPC state; the
	// arm is asserted through its OTHER operand, `m_bEnemyWentOccluded` (+0x5bc5) rather than
	// `bEnemyOccluded`, which is the field the recovered body reads.
	Memory.StealthVisionOverrideUntil = -1.0;
	Memory.bEnemyWentOccluded = true;
	TestEqual(TEXT("0x1029c970: an idle body is on the default whatever the occlusion edge says"),
		Senses.EffectiveVisionDistanceCm(*F.Guard, 10.0), 1200.f);

	// `0x10270180` — the occlusion edge, three arms.
	Memory.bEnemyWentOccluded = false;
	Memory.EnemyWentOccludedPosition = FVector(9.f, 9.f, 9.f);
	F.Guard->UpdateEnemyWentOccluded(nullptr, true);
	TestEqual(TEXT("0x10270180: a null enemy resets the remembered position to vec3_origin"),
		Memory.EnemyWentOccludedPosition, FVector::ZeroVector);
	TestTrue(TEXT("0x10270180: and stores the LOS BYTE itself, not 0 - retail writes param_2"),
		Memory.bEnemyWentOccluded);
	F.Guard->UpdateEnemyWentOccluded(nullptr, false);
	TestFalse(TEXT("0x10270180: so a null enemy without LOS clears it"), Memory.bEnemyWentOccluded);

	F.Other->Origin = FVector(500.f, 0.f, 0.f);
	Memory.bEnemyWentOccluded = true;
	F.Guard->UpdateEnemyWentOccluded(F.Other, false);
	TestEqual(TEXT("0x10270180: losing sight snapshots the enemy's current origin"),
		Memory.EnemyWentOccludedPosition, FVector(500.f, 0.f, 0.f));
	TestFalse(TEXT("0x10270180: and CLEARS the edge flag"), Memory.bEnemyWentOccluded);

	// The gate is 4096.0 SQUARED in Source units, i.e. 64 units of drift.
	const float SixtyThreeUnitsCm = 63.f * ElysiumMove::U;
	F.Other->Origin = FVector(500.f + SixtyThreeUnitsCm, 0.f, 0.f);
	F.Guard->UpdateEnemyWentOccluded(F.Other, true);
	TestFalse(TEXT("0x10270180: 63 units of drift does not trip the edge"),
		Memory.bEnemyWentOccluded);
	const float SixtyFiveUnitsCm = 65.f * ElysiumMove::U;
	F.Other->Origin = FVector(500.f + SixtyFiveUnitsCm, 0.f, 0.f);
	F.Guard->UpdateEnemyWentOccluded(F.Other, true);
	TestTrue(TEXT("0x10270180: 65 units does - the constant is 4096.0 SQUARED"),
		Memory.bEnemyWentOccluded);
	// A flag already set is never re-tested, so moving back does not clear it.
	F.Other->Origin = FVector(500.f, 0.f, 0.f);
	F.Guard->UpdateEnemyWentOccluded(F.Other, true);
	TestTrue(TEXT("0x10270180: and a set flag is never re-tested"), Memory.bEnemyWentOccluded);

	// `0x103cf5f0` — the werewolf's pursuit test. The flag bit skips it entirely; without the bit
	// both ConVar gates must fail, and both are UNRECOVERED at 0.0, so they do.
	F.Guard->WerewolfHintFlags = 0x4u;
	TestTrue(TEXT("0x103cf5f0: +0x66e8 bit 2 skips the whole test"),
		F.Guard->WerewolfShouldPursueEnemy());
	F.Guard->WerewolfHintFlags = 0u;
	TestEqual(TEXT("0x103cf5f0: DAT_1093f8ec is an unrecovered ConVar answering 0.0"),
		FElysiumNpc::WerewolfPursueElapsedLimitSeconds(), 0.f);
	TestEqual(TEXT("0x103cf5f0: DAT_1093d574 likewise"),
		FElysiumNpc::WerewolfPursuePlayerDistLimitUnits(), 0.f);
	F.Guard->WerewolfUnhideStamp = 0.0;
	Memory.ClosestPlayerDistanceCm = 500.f;
	TestFalse(TEXT("0x103cf5f0: with both at 0.0 neither gate can pass, so the werewolf gives up"),
		F.Guard->WerewolfShouldPursueEnemy());

	// `0x103dda10` — Yukie's melee exit. Slot 308 is a declared stub answering false, so the
	// distance arm is the one this runtime reaches; the melee range is TroikaHelpers' unrecovered
	// ConVar at 0.0, which makes `0.0 <= dist` true for every non-negative distance.
	TestFalse(TEXT("0x103dda10: slot 308 HasUsableRangedWeapon is still a stub"),
		F.Guard->HasUsableRangedWeapon());
	TestEqual(TEXT("0x103dda10: and the melee range ConVar is unrecovered at 0.0"),
		FElysiumNpc::MeleeRangeUnits(), 0.f);
	F.Guard->ScheduleHost.EnemyDistUnits = 300.f;
	TestTrue(TEXT("0x103dda10: so the distance arm leaves melee"),
		F.Guard->YukieShouldLeaveMelee());
	F.Guard->ScheduleHost.EnemyDistUnits = -1.f;
	TestFalse(TEXT("0x103dda10: and the compare really is 2*range*1.5 <= dist"),
		F.Guard->YukieShouldLeaveMelee());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
