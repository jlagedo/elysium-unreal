#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Conditions10** — the flag-word writers, slot 404 `IRelationType` and its
// species arms, `CanBeFedUponBy`, the seven `TaskFail` species arms and the condition-debug string.
//
// Every assertion is read off the decompiled C or the listing and the address it came from is named
// beside it. Seven of this family's corrections to the checklist's one-line walks are pinned by a
// case: slot 404's fall-through answer is the CANDIDATE's opinion of the boss and not the boss's;
// slot 532's case 8 clears `m_eAlternateAI` for 1..3; `0x10379040` never clobbers `ECX` so the
// Gargoyle arm is a plain `FINDING_BODY` clear and not a lost-`this` bug; `0x10382970` blacklists
// the pickup target for twenty seconds rather than releasing it; `0x10398d90` is the throwable MODE
// setter; `0x1028d990` renders TWO ladders and not three; and slot 419's pair is already bound.

static constexpr EAutomationTestFlags GElysiumNpcKernelConditions10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'.
	struct FCond10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumNpc* Boss = nullptr;
		FElysiumNpc* Pedestrian = nullptr;
		FElysiumNpc* Hunter = nullptr;
		FElysiumNpc* Cop = nullptr;
		FElysiumNpc* Newscaster = nullptr;
		FElysiumPlayer* Player = nullptr;

		FCond10Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("conditions10_kernel"), 404);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpc(TEXT("guard"), FVector::ZeroVector, TEXT("npc_VHumanCombatant"));
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f),
						TEXT("npc_VHumanCombatant"));
					Builder.AddNpc(TEXT("boss"), FVector(800.f, 0.f, 0.f),
						TEXT("npc_VHumanCombatant"));
					// The two species arms of slot 404 the census CAN reach.
					Builder.AddNpc(TEXT("ped"), FVector(1200.f, 0.f, 0.f), TEXT("npc_VPedestrian"));
					Builder.AddNpc(TEXT("hunter"), FVector(1600.f, 0.f, 0.f), TEXT("npc_VHunter"));
					// `CNPC_VCop`'s census classname list is null, so this one's `RetailClass()` is
					// null and it takes the Troika line — the recovered answer, not a gap.
					Builder.AddNpc(TEXT("cop"), FVector(2000.f, 0.f, 0.f), TEXT("npc_VCop"));
					// `CNPC_VNewscaster#404` is `0x103a01b0`, one of the two slot-404 bodies this
					// family dispatches without owning.
					Builder.AddNpc(TEXT("news"), FVector(2400.f, 0.f, 0.f), TEXT("npc_VNewscaster"));
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Boss = World.Npc(TEXT("boss"));
			Pedestrian = World.Npc(TEXT("ped"));
			Hunter = World.Npc(TEXT("hunter"));
			Cop = World.Npc(TEXT("cop"));
			Newscaster = World.Npc(TEXT("news"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet(
				{ Guard, Other, Boss, Pedestrian, Hunter, Cop, Newscaster });
			FElysiumNpc::ResetSpeciesSuspectGlobals();
			FElysiumNpc::SetDebugConVar(nullptr, 0);
		}
	};

	// Retail's `Disposition_t`.
	constexpr int32 GCond10T_D_ER = 0;
	constexpr int32 GCond10T_D_HT = 1;
	constexpr int32 GCond10T_D_FR = 2;
	constexpr int32 GCond10T_D_LI = 3;
	constexpr int32 GCond10T_D_NU = 4;
}

// =================================================================================================
// Slot 404 `IRelationType` — `0x10299da0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10RelationTroikaTest,
	"Elysium.Substrate.NpcKernelConditions10.IRelationTypeTroika",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10RelationTroikaTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}

	// `10299daa` / `10299db7`: self and null both answer `D_ER`, ahead of everything else.
	TestEqual(TEXT("self answers D_ER (10299daa)"), F.Guard->IRelationType(F.Guard), GCond10T_D_ER);
	TestEqual(TEXT("null answers D_ER (10299db7)"), F.Guard->IRelationType(nullptr), GCond10T_D_ER);

	// `10299fae`: with no boss on either side the body is the `CBaseCombatCharacter` tail, and the
	// store `FElysiumRelationships` never answers `D_ER` — a target with no row is neutral.
	TestEqual(TEXT("no row is D_NU (10299fb1)"), F.Guard->IRelationType(F.Other), GCond10T_D_NU);

	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 10);
	TestEqual(TEXT("a hate row is D_HT"), F.Guard->IRelationType(F.Other), GCond10T_D_HT);
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Fear, 20);
	TestEqual(TEXT("a fear row is D_FR"), F.Guard->IRelationType(F.Other), GCond10T_D_FR);
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Like, 30);
	TestEqual(TEXT("a like row is D_LI"), F.Guard->IRelationType(F.Other), GCond10T_D_LI);

	// Family Senses10's `IRelationTypeOf` is now a one-line forward to this slot.
	TestEqual(TEXT("Senses10's helper forwards to slot 404"),
		F.Guard->IRelationTypeOf(F.Other), GCond10T_D_LI);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10RelationInsaneTest,
	"Elysium.Substrate.NpcKernelConditions10.IRelationTypeInsaneArm",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10RelationInsaneTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}

	// `10299dd2`: arm A needs the candidate's own NPC to carry `D_INSANE`.
	F.Guard->Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 10);

	// Without the bit, the arm is skipped entirely and the tail decides.
	TestEqual(TEXT("no D_INSANE means the table tail (10299de2)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_NU);

	F.Other->NpcFlags.Set(EElysiumNpcFlag2::D_INSANE);
	// `10299e51`: hating the closest player makes the INSANE candidate `D_HT` even though nothing
	// in the table says anything about the candidate at all.
	TestEqual(TEXT("insane plus a hated closest player is D_HT (10299e51)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);

	// `10299e8e`: the second half of the arm — the enemy test, slot 168.
	F.Guard->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Neutral, 20);
	TestEqual(TEXT("a neutral closest player who is not the enemy falls through (10299e96)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_NU);
	F.Guard->Senses.Memory.Enemy = F.Player->Handle;
	TestEqual(TEXT("the closest player BEING my enemy is D_HT (10299e8e)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);

	// `10299df1`: with no closest player the whole arm is skipped.
	F.Guard->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestEqual(TEXT("no closest player skips arm A (10299df1)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_NU);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10RelationBossTest,
	"Elysium.Substrate.NpcKernelConditions10.IRelationTypeBossArms",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10RelationBossTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Boss == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}

	// --- Arm B, `10299eaa`: the CANDIDATE's follower boss ----------------------------------------
	F.Other->FollowerBoss = F.Boss->Handle;
	TestEqual(TEXT("a candidate boss I am neutral to falls through (10299f0f)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_NU);
	F.Guard->Relationships.SetEntity(F.Boss->Handle, EElysiumRelationship::Hate, 10);
	TestEqual(TEXT("hating the candidate's boss makes the candidate D_HT (10299ee4)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);
	F.Guard->Relationships.SetEntity(F.Boss->Handle, EElysiumRelationship::Neutral, 20);
	F.Guard->Senses.Memory.Enemy = F.Boss->Handle;
	TestEqual(TEXT("the candidate's boss BEING my enemy makes it D_HT (10299ef3)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);
	F.Other->FollowerBoss = FElysiumEntityHandle::Invalid();
	F.Guard->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();

	// --- Arm C, `10299f0f`: MY follower boss ------------------------------------------------------
	F.Guard->FollowerBoss = F.Boss->Handle;
	// `10299f45`: my boss IS the candidate — `D_LI`, whatever the table says.
	F.Guard->Relationships.SetEntity(F.Boss->Handle, EElysiumRelationship::Hate, 30);
	TestEqual(TEXT("my own boss is D_LI regardless of the table (10299f45)"),
		F.Guard->IRelationType(F.Boss), GCond10T_D_LI);

	// `10299f5a`: the BOSS's opinion of the candidate decides.
	F.Boss->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 10);
	TestEqual(TEXT("the boss hating the candidate makes it D_HT (10299f5a)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);

	// `10299f6b`: the BOSS's slot 167 — the CONST `GetEnemy`, no last-enemy fallback. The
	// asymmetry against arms A and B (slot 168) is retail's.
	F.Boss->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Neutral, 20);
	F.Boss->Senses.Memory.Enemy = F.Other->Handle;
	TestEqual(TEXT("the boss's enemy being the candidate makes it D_HT (10299f6b)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);
	F.Boss->Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();

	// **CORRECTION, pinned.** `10299f84` reassigns the return register with the CANDIDATE's opinion
	// of the boss. The boss is NEUTRAL toward the candidate; the candidate FEARS the boss; retail
	// therefore answers `D_FR`, not the boss's `D_NU`, and the checklist's walk ("the answer is the
	// BOSS's slot 0x650 toward the target") is wrong for an NPC candidate.
	F.Other->Relationships.SetEntity(F.Boss->Handle, EElysiumRelationship::Fear, 10);
	TestEqual(TEXT("an NPC candidate's own opinion of my boss is the answer (10299f84)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_FR);

	// `10299f7e`: and when the candidate HATES the boss it is `D_HT` outright.
	F.Other->Relationships.SetEntity(F.Boss->Handle, EElysiumRelationship::Hate, 20);
	TestEqual(TEXT("an NPC candidate hating my boss is D_HT (10299f7e)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);

	// `10299f8f`: the candidate's slot 168 against the boss.
	F.Other->Relationships.SetEntity(F.Boss->Handle, EElysiumRelationship::Neutral, 30);
	F.Other->Senses.Memory.Enemy = F.Boss->Handle;
	TestEqual(TEXT("the candidate's enemy being my boss is D_HT (10299f8f)"),
		F.Guard->IRelationType(F.Other), GCond10T_D_HT);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10RelationSpeciesTest,
	"Elysium.Substrate.NpcKernelConditions10.IRelationTypeSpeciesArms",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10RelationSpeciesTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Pedestrian == nullptr || F.Hunter == nullptr
		|| F.Cop == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}

	// --- The census dispatch ----------------------------------------------------------------------
	TestEqual(TEXT("npc_VPedestrian's slot 404 is 0x103a2930"),
		FString(ElysiumNpcKernelClass::BodyOf(F.Pedestrian->RetailClass(), 404)),
		FString(TEXT("0x103a2930")));
	TestEqual(TEXT("npc_VHunter's slot 404 is 0x10388bb0"),
		FString(ElysiumNpcKernelClass::BodyOf(F.Hunter->RetailClass(), 404)),
		FString(TEXT("0x10388bb0")));
	// `CNPC_VCop`'s census classname list is null, so a spawned `npc_VCop` takes the TROIKA body.
	TestNull(TEXT("a spawned npc_VCop has no retail class"), F.Cop->RetailClass());

	// --- `CNPC_VPedestrian::IRelationType` (`0x103a2930`) ----------------------------------------
	TestEqual(TEXT("the pedestrian's null arm is D_ER (103a2936)"),
		F.Pedestrian->IRelationType(nullptr), GCond10T_D_ER);
	// `103a2946`: the insane candidate answers `D_FR` WITHOUT consulting the table — the table here
	// says LIKE and the answer is still fear.
	F.Pedestrian->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Like, 10);
	TestEqual(TEXT("a sane candidate reaches the table (103a2946)"),
		F.Pedestrian->IRelationType(F.Other), GCond10T_D_LI);
	F.Other->NpcFlags.Set(EElysiumNpcFlag2::D_INSANE);
	TestEqual(TEXT("pedestrians fear the insane, table or no table (103a2951)"),
		F.Pedestrian->IRelationType(F.Other), GCond10T_D_FR);
	F.Other->NpcFlags.Clear(EElysiumNpcFlag2::D_INSANE);

	// The Troika body reached through the same NPC proves the species arm is what changed the
	// answer, not the store.
	TestEqual(TEXT("the Troika body under the pedestrian arm still says D_LI"),
		F.Pedestrian->TroikaIRelationType(F.Other), GCond10T_D_LI);

	// --- `CNPC_VHunter::IRelationType` (`0x10388bb0`) ---------------------------------------------
	TestEqual(TEXT("the hunter's null arm is D_ER (10388bb6)"),
		F.Hunter->IRelationType(nullptr), GCond10T_D_ER);
	TestEqual(TEXT("with no grudge the hunter defers (10388c1a)"),
		F.Hunter->IRelationType(F.Other), GCond10T_D_NU);
	// `0x10387fd0`'s stamp (family Senses10's writer), then the read.
	F.Hunter->StampHunterSuspect(F.Other);
	TestEqual(TEXT("the hunter's shared grudge is D_HT (10388bf0)"),
		F.Hunter->IRelationType(F.Other), GCond10T_D_HT);
	// **No player-side arms.** A heightened-alert player is NOT hostile to a hunter.
	F.Player->Police.HeightenedAlertExpiry = 1000.0;
	F.Player->Police.CopsInPursuit = 3;
	TestEqual(TEXT("the hunter has no player-side arm (10388bb0 is 109 bytes)"),
		F.Hunter->IRelationType(F.Player), GCond10T_D_NU);

	// --- `CNPC_VCop::IRelationType` (`0x10372b70`) ------------------------------------------------
	// UNREACHABLE through the dispatcher (the null classname list above), so the arm is driven
	// directly — which is also how a test would drive it once a cop census row exists.
	TestEqual(TEXT("the cop's null arm is D_ER (10372b76)"),
		F.Cop->CopIRelationType(nullptr), GCond10T_D_ER);
	TestEqual(TEXT("with nothing set the cop defers (10372c0a)"),
		F.Cop->CopIRelationType(F.Other), GCond10T_D_NU);
	F.Cop->StampCopSuspect(F.Player);
	TestEqual(TEXT("the cop's shared timed grudge is D_HT (10372b84)"),
		F.Cop->CopIRelationType(F.Player), GCond10T_D_HT);
	FElysiumNpc::ResetSpeciesSuspectGlobals();
	// `10372bc6`: the two player words, both of which stand on `FElysiumPlayer`.
	TestEqual(TEXT("a heightened-alert player is D_HT to a cop (0x1017f8d0)"),
		F.Cop->CopIRelationType(F.Player), GCond10T_D_HT);
	F.Player->Police.HeightenedAlertExpiry = 0.0;
	TestEqual(TEXT("a non-zero cops-in-pursuit count is D_HT (0x1017f770)"),
		F.Cop->CopIRelationType(F.Player), GCond10T_D_HT);
	F.Player->Police.CopsInPursuit = 0;
	TestEqual(TEXT("with both player words clear the cop defers (10372c0a)"),
		F.Cop->CopIRelationType(F.Player), GCond10T_D_NU);
	// The Troika body for the same plain `npc_VCop`, which is what the dispatcher gives it today.
	TestEqual(TEXT("a plain npc_VCop takes the Troika body"),
		F.Cop->IRelationType(F.Player), GCond10T_D_NU);

	// --- `CNPC_VNewscaster::IRelationType` (`0x103a01b0`) ------------------------------------------
	// Eight bytes, `return 4;`. It never looks at the candidate and never reaches the Troika body,
	// so a newscaster is `D_NU` toward EVERYTHING — including a hated entity, itself and null.
	if (F.Newscaster != nullptr)
	{
		TestEqual(TEXT("npc_VNewscaster's slot 404 is 0x103a01b0"),
			FString(ElysiumNpcKernelClass::BodyOf(F.Newscaster->RetailClass(), 404)),
			FString(TEXT("0x103a01b0")));
		F.Newscaster->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 10);
		TestEqual(TEXT("a newscaster answers D_NU even for a hated entity (103a01b0)"),
			F.Newscaster->IRelationType(F.Other), GCond10T_D_NU);
		TestEqual(TEXT("a newscaster answers D_NU for null too"),
			F.Newscaster->IRelationType(nullptr), GCond10T_D_NU);
		TestEqual(TEXT("and for itself, where the Troika body would say D_ER"),
			F.Newscaster->IRelationType(F.Newscaster), GCond10T_D_NU);
		// The Troika body under it still reads the table, which proves the arm is what answered.
		TestEqual(TEXT("the Troika body under the newscaster arm says D_HT"),
			F.Newscaster->TroikaIRelationType(F.Other), GCond10T_D_HT);
	}

	// --- `0x103a48b0`, story 29c-1's `SpeciesIRelationType` (family Squad) -------------------------
	// Three census classes fill slot 404 with it; none of the three is a registered spawn leaf here,
	// so the dispatcher's case is proved by the census row and the body by family Squad's own suite.
	for (const TCHAR* Name : { TEXT("CNPC_VFrenzyShadow"), TEXT("CNPC_VPlayerController"),
			TEXT("CNPC_VWolfMorph") })
	{
		TestEqual(*FString::Printf(TEXT("%s's slot 404 is 0x103a48b0"), Name),
			FString(ElysiumNpcKernelClass::BodyOf(ElysiumNpcKernelClass::Find(Name), 404)),
			FString(TEXT("0x103a48b0")));
	}

	// --- `CNPC_VYukie::IRelationType` (`0x103dd880`) ----------------------------------------------
	// `npc_VYukie` is not a registered spawn leaf here, so this arm is unreachable too; the whole
	// species override is the null guard, driven directly.
	TestEqual(TEXT("the Yukie null guard answers D_ER (103dd888)"),
		F.Guard->YukieIRelationType(nullptr), GCond10T_D_ER);
	F.Guard->Relationships.SetEntity(F.Other->Handle, EElysiumRelationship::Hate, 10);
	TestEqual(TEXT("anything else tail-jumps to the Troika body (103dd88f)"),
		F.Guard->YukieIRelationType(F.Other), GCond10T_D_HT);
	return true;
}

// =================================================================================================
// Slot 342 `CanBeFedUponBy` — `0x102c4a60` over `0x10339800`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10CanBeFedUponByTest,
	"Elysium.Substrate.NpcKernelConditions10.CanBeFedUponBy",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10CanBeFedUponByTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr || F.Boss == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}

	// The admitting baseline: no invincibility, no boss, and the base body's five terms all pass.
	TestTrue(TEXT("a plain live NPC can be fed upon (102c4a9e)"),
		F.Guard->CanBeFedUponBy(F.Player));

	// `102c4a66`: `m_bInvincible` refuses ahead of everything.
	F.Guard->bInvincible = true;
	TestFalse(TEXT("m_bInvincible refuses first (102c4a66)"), F.Guard->CanBeFedUponBy(F.Player));
	F.Guard->bInvincible = false;

	// `102c4a74`: the follower-boss arm. A boss who is NOT the feeder changes nothing.
	F.Guard->FollowerBoss = F.Boss->Handle;
	TestTrue(TEXT("a boss who is not the feeder changes nothing (102c4a7e)"),
		F.Guard->CanBeFedUponBy(F.Player));
	// The boss IS the feeder: refused unless `No_Resist_Feeding` (misc flag 0x40000) stands.
	TestFalse(TEXT("my own boss may not feed on me without No_Resist_Feeding (102c4a8a)"),
		F.Guard->CanBeFedUponBy(F.Boss));
	ElysiumMiscFlags::Set(F.Guard->MiscFlags, ElysiumMiscFlags::NoResistFeeding);
	TestTrue(TEXT("No_Resist_Feeding lets my own boss feed (102c4a96)"),
		F.Guard->CanBeFedUponBy(F.Boss));
	ElysiumMiscFlags::Clear(F.Guard->MiscFlags, ElysiumMiscFlags::NoResistFeeding);
	F.Guard->FollowerBoss = FElysiumEntityHandle::Invalid();

	// --- `CBaseCombatCharacter::CanBeFedUponBy` (`0x10339800`), the base body --------------------
	// `1033989a`: `NOT_FEEDABLE`.
	F.Guard->NpcFlags.Set(EElysiumNpcFlag2::NOT_FEEDABLE);
	TestFalse(TEXT("NOT_FEEDABLE refuses (1033989a)"), F.Guard->CanBeFedUponBy(F.Player));
	F.Guard->NpcFlags.Clear(EElysiumNpcFlag2::NOT_FEEDABLE);

	// `103398fa`: `IsUnconscious()` — misc-flag bit 0.
	ElysiumMiscFlags::Set(F.Guard->MiscFlags, ElysiumMiscFlags::Unconscious);
	TestFalse(TEXT("an unconscious body refuses (103398fa)"), F.Guard->CanBeFedUponBy(F.Player));
	ElysiumMiscFlags::Clear(F.Guard->MiscFlags, ElysiumMiscFlags::Unconscious);

	// **The feeder argument is never read by the base body.** A null feeder reaches it unchanged and
	// answers the same as any other, which is the recovered asymmetry the Troika arm compensates for.
	TestTrue(TEXT("the base body ignores the feeder entirely (0x10339800)"),
		F.Guard->BaseCanBeFedUponBy(nullptr));
	return true;
}

// =================================================================================================
// Slot 587 `CanWitnessSupernatural` — `0x1028ef20`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10WitnessTest,
	"Elysium.Substrate.NpcKernelConditions10.CanWitnessSupernatural",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10WitnessTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `1028ef6e`: the admitting baseline is a flee level below 3.
	F.Guard->PlSupernaturalFlee = 0;
	F.Guard->PlSupernaturalAttack = 0;
	TestTrue(TEXT("flee below 3 answers true (1028ef6e)"), F.Guard->CanWitnessSupernatural(0));
	// The argument is DEAD in every arm.
	TestTrue(TEXT("the int argument is never read (1028ef20)"),
		F.Guard->CanWitnessSupernatural(999));

	// `1028ef7e`: flee at or above 3 falls through to the ATTACK level.
	F.Guard->PlSupernaturalFlee = 3;
	TestTrue(TEXT("flee 3 with attack 0 still witnesses (1028ef7e)"),
		F.Guard->CanWitnessSupernatural(0));
	F.Guard->PlSupernaturalAttack = 3;
	TestFalse(TEXT("flee 3 and attack 3 cannot witness (1028ef7e)"),
		F.Guard->CanWitnessSupernatural(0));
	F.Guard->PlSupernaturalFlee = 0;
	F.Guard->PlSupernaturalAttack = 0;

	// The five refusals, each on its own.
	F.Guard->NpcFlags.AddOblivious();
	TestFalse(TEXT("m_iIsOblivious > 0 refuses (1028ef44)"), F.Guard->CanWitnessSupernatural(0));
	F.Guard->NpcFlags.RemoveOblivious();

	F.Guard->NpcFlags.SetFrenziedWord(FElysiumNpcFlags::FrenziedDoesNotWitness);
	TestFalse(TEXT("the frenzied does-not-witness bit refuses (1028ef53)"),
		F.Guard->CanWitnessSupernatural(0));
	F.Guard->NpcFlags.SetFrenziedWord(0);

	F.Guard->NpcFlags.Set(EElysiumNpcFlag::D_IS_BUSY);
	TestFalse(TEXT("IsBusyWithDiscipline refuses (1028ef60)"),
		F.Guard->CanWitnessSupernatural(0));
	F.Guard->NpcFlags.Clear(EElysiumNpcFlag::D_IS_BUSY);

	TestTrue(TEXT("past all five it witnesses again"), F.Guard->CanWitnessSupernatural(0));
	return true;
}

// =================================================================================================
// Slot 532 — the door-failure cleanup, `0x10290570`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10Slot532Test,
	"Elysium.Substrate.NpcKernelConditions10.Slot532",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10Slot532Test::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// The tail, `102905f4`: every path clears both door words, whatever the bits were.
	F.Guard->OpeningDoor = F.Other->Handle;
	F.Guard->bOpeningDoorWait = true;
	F.Guard->AlternateAi = 0;
	F.Guard->Slot532(3);   // not in the jump table's live set
	TestFalse(TEXT("the base body clears m_hOpeningDoor (1027e0f0)"), F.Guard->OpeningDoor.IsSet());
	TestFalse(TEXT("the base body clears m_bOpeningDoorWait (1027e0f0)"),
		F.Guard->bOpeningDoorWait);

	// Case 1, `10290587`: `m_eAlternateAI` outside `[1,4]` takes no clear.
	F.Guard->AlternateAi = 5;
	F.Guard->Slot532(1);
	TestEqual(TEXT("case 1 leaves m_eAlternateAI 5 alone (10290591)"), F.Guard->AlternateAi, 5);
	F.Guard->AlternateAi = 2;
	F.Guard->Slot532(1);
	TestEqual(TEXT("case 1 clears m_eAlternateAI in [1,4] (102905ea)"), F.Guard->AlternateAi, 0);

	// Cases 2 and 4, `10290598`: ONLY `m_eAlternateAI == 4`, and the call at `102905a7` is slot 448
	// — `TaskFail(0xe)` — which raises `TASK_FAILED` before the door words are cleared.
	F.Guard->AlternateAi = 3;
	F.Guard->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	F.Guard->Slot532(2);
	TestEqual(TEXT("case 2 with m_eAlternateAI 3 does nothing (1029059f)"), F.Guard->AlternateAi, 3);
	TestFalse(TEXT("case 2 with m_eAlternateAI 3 does not fail the task"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	F.Guard->AlternateAi = 4;
	F.Guard->Slot532(4);
	TestTrue(TEXT("case 4 with m_eAlternateAI 4 runs TaskFail(0xe) (102905a7, slot 448)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	TestEqual(TEXT("and then clears m_eAlternateAI (102905ea)"), F.Guard->AlternateAi, 0);

	// **CORRECTION, pinned.** Case 8 with `m_eAlternateAI` in 1..3 DOES clear it — `102905ca
	// CMP EAX,0x3 / JLE 0x102905ea`. The checklist's walk says "no clear for values 1-3".
	F.Guard->AlternateAi = 2;
	F.Guard->Slot532(8);
	TestEqual(TEXT("case 8 clears m_eAlternateAI 1..3 (102905ca)"), F.Guard->AlternateAi, 0);
	F.Guard->AlternateAi = 4;
	F.Guard->Slot532(8);
	TestEqual(TEXT("case 8 clears m_eAlternateAI 4 too (102905d2)"), F.Guard->AlternateAi, 0);
	F.Guard->AlternateAi = 0;
	F.Guard->Slot532(8);
	TestEqual(TEXT("case 8 with 0 takes no clear (102905c8)"), F.Guard->AlternateAi, 0);
	F.Guard->AlternateAi = 7;
	F.Guard->Slot532(8);
	TestEqual(TEXT("case 8 with 5 or more takes no clear (102905d2)"), F.Guard->AlternateAi, 7);

	// The default arm, `102905f4`: a value outside `[1,8]` never reaches the table.
	F.Guard->AlternateAi = 4;
	F.Guard->Slot532(16);
	TestEqual(TEXT("a bit outside the table takes no clear (1029057b)"), F.Guard->AlternateAi, 4);
	return true;
}

// =================================================================================================
// Slot 35 — `0x102c0220`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10Slot35Test,
	"Elysium.Substrate.NpcKernelConditions10.Slot35",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10Slot35Test::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `102c0235`: slot 295 `CanTalk` is dispatched WITH the caller's argument, and `102c024d` is
	// `14 + (IsMale != 0)`. The expectation is DERIVED from slot 295's own answer rather than
	// assuming one, because `CanTalk` is another band's row: what this case pins is the rule, which
	// stays true whichever way slot 295 goes.
	const bool bCanTalk = F.Guard->CanTalk(F.Player);
	const int32 Gendered = F.Guard->Sheet.IsMale() ? 15 : 14;
	TestEqual(TEXT("the answer is 0 when CanTalk refuses, else 14/15 (102c024d)"),
		F.Guard->Slot35(F.Player), bCanTalk ? Gendered : 0);
	TestTrue(TEXT("the id space is exactly {0, 14, 15}"),
		F.Guard->Slot35(F.Player) == 0 || F.Guard->Slot35(F.Player) == 14
			|| F.Guard->Slot35(F.Player) == 15);

	// `102c0223`: `IsInDialog()` refuses ahead of slot 295, so a talking body answers 0 whatever
	// `CanTalk` would have said.
	F.Guard->Dialogue.bInDialog = true;
	TestEqual(TEXT("a body in dialogue answers 0 (102c022a)"), F.Guard->Slot35(F.Player), 0);
	F.Guard->Dialogue.bInDialog = false;
	return true;
}

// =================================================================================================
// Slot 419 `UpdateBurstShootPause` — `0x102c5500`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10BurstPauseTest,
	"Elysium.Substrate.NpcKernelConditions10.UpdateBurstShootPause",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10BurstPauseTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	F.Guard->BurstShootPauseMin = -1.f;
	F.Guard->BurstShootPauseMax = -1.f;
	F.Guard->UpdateBurstShootPause();
	// `102c552e` / `102c5538`: the two UNARMED literals, `0x3e99999a` and `0x3f000000`.
	TestEqual(TEXT("the unarmed min is 0.3 (102c552e)"), F.Guard->BurstShootPauseMin, 0.3f,
		KINDA_SMALL_NUMBER);
	TestEqual(TEXT("the unarmed max is 0.5 (102c5538)"), F.Guard->BurstShootPauseMax, 0.5f,
		KINDA_SMALL_NUMBER);

	// `0x102c5570`'s arithmetic, exercised as a pure function because no weapon-data record stands.
	// Range at or below the threshold leaves the scale at 1.0, so the answer is `Value - Base`.
	TestEqual(TEXT("a zero range leaves the scale at 1.0 (102c5588)"),
		FElysiumNpc::ScaleWeaponBurstPause(/*Value*/ 2.f, /*Base*/ 0.5f, /*Range*/ 0.f,
			/*Distance*/ 100.f, /*bHasTarget*/ true), 1.5f, KINDA_SMALL_NUMBER);
	// With a range and a distance, `sqrt(distance / range) * (Value - Base)`.
	TestEqual(TEXT("the scale is sqrt(distance/range) (102c56a5)"),
		FElysiumNpc::ScaleWeaponBurstPause(2.f, 0.5f, 4.f, 16.f, true),
		FMath::Sqrt(4.f) * 1.5f, KINDA_SMALL_NUMBER);
	// With no target at all the seeded 1.0 is divided instead.
	TestEqual(TEXT("no target divides the seeded 1.0 (102c56a1)"),
		FElysiumNpc::ScaleWeaponBurstPause(2.f, 0.5f, 4.f, 16.f, false),
		FMath::Sqrt(0.25f) * 1.5f, KINDA_SMALL_NUMBER);
	return true;
}

// =================================================================================================
// `0x102c54c0` — the fake-reload reroll.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10FakeReloadTest,
	"Elysium.Substrate.NpcKernelConditions10.ResetFakeReloadCount",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10FakeReloadTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// The seam answers false with both ends zero and names the template columns it stands for.
	int32 Min = -1;
	int32 Max = -1;
	TestFalse(TEXT("the char template carries no +0x34/+0x38 pair"),
		F.Guard->CharTemplateFakeReloadRange(Min, Max));
	TestEqual(TEXT("the seam answers a zero low end"), Min, 0);
	TestEqual(TEXT("the seam answers a zero high end"), Max, 0);

	// The write still happens: retail's body has no arm that skips its only write, and
	// `RandomInt(0, 0)` is 0.
	F.Guard->FakeReloadCount = 7;
	F.Guard->ResetFakeReloadCount();
	TestEqual(TEXT("m_iFakeReloadCount is rerolled from the template range (+0x65f0)"),
		F.Guard->FakeReloadCount, 0);
	return true;
}

// =================================================================================================
// `0x1028d990` — the condition/flag debug string.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10DebugStringTest,
	"Elysium.Substrate.NpcKernelConditions10.BuildConditionDebugString",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10DebugStringTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `1028d9a8`: a buffer size at or below zero writes NOTHING AT ALL.
	TestTrue(TEXT("a zero-size buffer writes nothing (1028d9a8)"),
		F.Guard->BuildConditionDebugString(TEXT("hello"), 0, 0).IsEmpty());

	// The shipped default: both debug bytes clear, so the third format string — the `DevMsg` arm
	// with `GetDebugName()` in a 20-wide left-aligned field — and all four optional blocks built.
	const FString Named = F.Guard->BuildConditionDebugString(TEXT("hello"), 0, 512);
	TestTrue(TEXT("the DevMsg arm is prefixed with GetDebugName (1028dcff)"),
		Named.StartsWith(TEXT("guard")));
	TestTrue(TEXT("the message is in it"), Named.Contains(TEXT("hello")));
	TestTrue(TEXT("it ends in two newlines (0x105d8828)"), Named.EndsWith(TEXT("\n\n")));

	// `1028d9c1` / `1028d9d4`: a null message becomes the empty string and a negative indent
	// clamps to 0, so neither faults and neither changes the shape.
	const FString NullMessage = F.Guard->BuildConditionDebugString(nullptr, -5, 512);
	TestFalse(TEXT("a null message and a negative indent still build (1028d9c1)"),
		NullMessage.IsEmpty());

	// The indent is `%*s` over the empty string — `IndentLevel` spaces.
	const FString Indented = F.Guard->BuildConditionDebugString(TEXT("x"), 3, 512);
	TestTrue(TEXT("the indent is three spaces before the message (1028dcc4)"),
		Indented.Contains(TEXT(":     x")));

	// `1028dbd6`: `DAT_10920534` set and `DAT_10920535` clear selects the SHORT format, and that is
	// also the one arm in which none of the optional blocks was built.
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10920534"), 1);
	const FString Short = F.Guard->BuildConditionDebugString(TEXT("hello"), 0, 512);
	TestTrue(TEXT("the short arm ends in one newline (0x105d8854)"),
		Short.EndsWith(TEXT("hello\n")));
	TestFalse(TEXT("the short arm carries no GetDebugName prefix (1028dc57)"),
		Short.StartsWith(TEXT("guard")));

	// Both set selects the FULL format, which is the `DevMsg` arm without the name prefix.
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10920535"), 1);
	const FString Full = F.Guard->BuildConditionDebugString(TEXT("hello"), 0, 512);
	TestFalse(TEXT("the full arm carries no name prefix (1028dc3c)"),
		Full.StartsWith(TEXT("guard")));
	TestTrue(TEXT("the full arm ends in two newlines (0x105d8868)"), Full.EndsWith(TEXT("\n\n")));

	// --- The two ladders (there are TWO, not three) ------------------------------------------------
	// A clear word is all dots; a set bit takes the legend's character at that index.
	TestEqual(TEXT("a clear mask is all dots (1028dadb)"),
		FElysiumNpc::DebugMaskLadder(TEXT("ABCD"), 0u, 4), FString(TEXT("....")));
	TestEqual(TEXT("bit 0 takes the legend's first character (1028dad1)"),
		FElysiumNpc::DebugMaskLadder(TEXT("ABCD"), 1u, 4), FString(TEXT("A...")));
	TestEqual(TEXT("bit 2 takes the third (1028dad1)"),
		FElysiumNpc::DebugMaskLadder(TEXT("ABCD"), 4u, 4), FString(TEXT("..C.")));
	// The memory ladder is 32 wide and the flags ladder 30 — `1028dae1` and `1028db3f`.
	TestEqual(TEXT("the memory ladder is 32 glyphs (1028dae1)"),
		FElysiumNpc::DebugMaskLadder(TEXT("PIS__PF_T_L__TTEPLM________ICCCC"), 0u, 0x20).Len(),
		0x20);
	TestEqual(TEXT("the flags ladder is 30 glyphs (1028db3f)"),
		FElysiumNpc::DebugMaskLadder(TEXT("RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_"), 0u, 0x1e).Len(),
		0x1e);
	// The real word: `m_bfAINPCFlags` bit 0 is `D_IS_BUSY` and the legend's first character is 'R'.
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::D_IS_BUSY);
	TestTrue(TEXT("the flags ladder reads m_bfAINPCFlags (1028db18)"),
		F.Guard->BuildConditionDebugString(TEXT("x"), 0, 512).Contains(TEXT("R.....")));
	F.Guard->NpcFlags.Clear(EElysiumNpcFlag::D_IS_BUSY);

	// --- The NAV pair -----------------------------------------------------------------------------
	// `1028db72`: only nav types 3 and 1 build it at all.
	F.Guard->NavSetType(0);
	TestFalse(TEXT("a walking body prints no NAV pair (1028db83)"),
		F.Guard->BuildConditionDebugString(TEXT("x"), 0, 512).Contains(TEXT("NAV ")));
	F.Guard->NavSetType(3);
	TestTrue(TEXT("nav type 3 prints CLIMB first (1028dbad)"),
		F.Guard->BuildConditionDebugString(TEXT("x"), 0, 512).Contains(TEXT("NAV CLIMB     ")));
	F.Guard->NavSetType(1);
	TestTrue(TEXT("nav type 1 prints JUMP second, with the first field blank (1028db97)"),
		F.Guard->BuildConditionDebugString(TEXT("x"), 0, 512).Contains(TEXT("NAV       JUMP")));
	F.Guard->NavSetType(0);

	// --- The `CONDS:` block, gated by `DAT_10924a6c` alone -----------------------------------------
	TestFalse(TEXT("the schedule-debug ConVar is off by default (1028da14)"),
		F.Guard->ScheduleDebugConditionsEnabled());
	TestFalse(TEXT("so no CONDS: block is built (1028da0d)"),
		F.Guard->BuildConditionDebugString(TEXT("x"), 0, 512).Contains(TEXT("CONDS:")));
	FElysiumNpc::SetDebugConVar(TEXT("DAT_10924a6c"), 1);
	TestTrue(TEXT("with the ConVar set the block is built (1028da19)"),
		F.Guard->ScheduleDebugConditionsEnabled());
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	const FString Conds = F.Guard->ConditionDebugList();
	TestTrue(TEXT("the list opens with CONDS: (0x105d8908)"), Conds.StartsWith(TEXT("CONDS:")));
	TestTrue(TEXT("each entry is ' %s' with a LEADING space (0x105a3060)"),
		Conds.Contains(FString(TEXT(" ")) + F.Guard->GetShortConditionName(
			static_cast<int32>(EElysiumNpcCond::TaskFailed))));
	TestTrue(TEXT("the list ends in a newline (0x10547e40)"), Conds.EndsWith(TEXT("\n")));

	FElysiumNpc::SetDebugConVar(nullptr, 0);
	return true;
}

// =================================================================================================
// Slot 448 `TaskFail` — the seven species arms.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10TaskFailDispatchTest,
	"Elysium.Substrate.NpcKernelConditions10.TaskFailSpeciesDispatch",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10TaskFailDispatchTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Pedestrian == nullptr || F.Cop == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// **The census gap, named.** None of the seven retail classes that override slot 448 carries an
	// entity classname this port registers, and `CNPC_VTzimisceRunner` — which does — descends from
	// `CNPC_VBaseBoss` rather than `CNPC_VTzimisce`, so it does not inherit `0x103ba350` either.
	// Every one of the seven arms is therefore unreachable through the dispatcher today and is
	// driven directly below. What IS proved here is that the dispatcher adds nothing for a body
	// whose class has no slot-448 override.
	for (const FElysiumNpc* Npc : { F.Guard, F.Pedestrian, F.Cop })
	{
		const TCHAR* const Body = ElysiumNpcKernelClass::BodyOf(Npc->RetailClass(), 448);
		TestTrue(TEXT("a plain leaf's slot 448 is the Troika body or nothing"),
			Body == nullptr || FCString::Strcmp(Body, TEXT("0x1029adb0")) == 0);
	}

	// The prologue is a no-op for those leaves: `TaskFail` runs the Troika chain and nothing else.
	F.Guard->bSpeciesPathBlocked = false;
	F.Guard->SpeciesShunnedFindCount = 9;
	F.Guard->TaskFail(13);
	TestFalse(TEXT("no species arm ran for a plain combatant"), F.Guard->bSpeciesPathBlocked);
	TestEqual(TEXT("and no species word was touched"), F.Guard->SpeciesShunnedFindCount, 9);
	// The Troika body did run: `COND_TASK_FAILED` and the reason word.
	TestTrue(TEXT("the Troika body still raised TASK_FAILED (0x1029adb0)"),
		F.Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10TaskFailPathTest,
	"Elysium.Substrate.NpcKernelConditions10.TaskFailAsianVampireAndChangBros",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10TaskFailPathTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `CNPC_VAsianVampire::TaskFail` (`0x10362390`): the window is `0xb < code && code < 0x10`.
	for (const int32 Code : { 0, 11, 16, 100 })
	{
		F.Guard->bSpeciesPathBlocked = false;
		F.Guard->AsianVampireTaskFail(Code);
		TestFalse(TEXT("codes outside 12..15 set nothing (103623c5)"),
			F.Guard->bSpeciesPathBlocked);
	}
	for (const int32 Code : { 12, 13, 14, 15 })
	{
		F.Guard->bSpeciesPathBlocked = false;
		F.Guard->AsianVampireTaskFail(Code);
		TestTrue(TEXT("codes 12..15 set m_bPathBlocked (+0x66d4)"), F.Guard->bSpeciesPathBlocked);
	}

	// `CNPC_VChangBros::TaskFail` (`0x1036d1d0`): the same window, a different write.
	const int32 Expected = 0x15d;
	F.Guard->Schedule.FailScheduleOverride = ElysiumScheduleId::None;
	F.Guard->ChangBrosTaskFail(11);
	TestTrue(TEXT("code 11 writes no fail schedule (1036d205)"),
		F.Guard->Schedule.FailScheduleOverride == ElysiumScheduleId::None);
	F.Guard->ChangBrosTaskFail(12);
	TestTrue(TEXT("code 12 writes m_failSchedule 0x15d (+0x5c54)"),
		F.Guard->Schedule.FailScheduleOverride == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10TaskFailBodyTest,
	"Elysium.Substrate.NpcKernelConditions10.TaskFailGargoyleHengeyokaiTzimisce",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10TaskFailBodyTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// --- The shared memory-bit clear --------------------------------------------------------------
	// `10379063`: it needs BOTH `m_NPCState == 2` and `HasInterruptCondition(0x5c)` — the INTERRUPT
	// form, which needs an installed schedule and the bit in the custom mask, not just the raw
	// condition. With no schedule installed the gate is shut and the bit survives.
	F.Guard->ScheduleHost.MemoryBits = 0xffffffffu;
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	F.Guard->GargoyleTaskFail(0);
	TestEqual(TEXT("without an installed schedule the memory bit survives (0x10269d30)"),
		F.Guard->ScheduleHost.MemoryBits, 0xffffffffu);

	// --- The Gargoyle arm, `0x10379060` -----------------------------------------------------------
	// **CORRECTION, pinned.** `0x10379040` never clobbers `ECX`, so `0x10379000` is reached with a
	// live `this`: the pair is a plain `FINDING_BODY` read-then-clear and not a lost-`this` bug.
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);
	F.Guard->SpeciesShunnedFindCount = 4;
	F.Guard->GargoyleTaskFail(0);
	TestFalse(TEXT("the Gargoyle arm clears FINDING_BODY (1037908e)"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY));
	TestEqual(TEXT("and zeroes m_iShunnedFindPillar (+0x6680)"),
		F.Guard->SpeciesShunnedFindCount, 0);

	// --- The Hengeyokai arm, `0x10380510` ---------------------------------------------------------
	// **CORRECTION, pinned.** `0x10382970` does not release the pickup target: it appends
	// `(handle, curtime + 20.0)` to `m_BlacklistedEntities`.
	F.Guard->SpeciesBlacklistedEntities.Reset();
	F.Guard->SpeciesPickupTarget = F.Other->Handle;
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::CARRYING_BODY);
	F.Guard->SpeciesShunnedFindCount = 2;
	F.Guard->HengeyokaiTaskFail(0);
	TestEqual(TEXT("the pickup target is blacklisted, not released (0x10382970)"),
		F.Guard->SpeciesBlacklistedEntities.Num(), 1);
	if (F.Guard->SpeciesBlacklistedEntities.Num() == 1)
	{
		TestTrue(TEXT("the blacklisted entity is the pickup target"),
			F.Guard->SpeciesBlacklistedEntities[0].Entity == F.Other->Handle);
		TestEqual(TEXT("the window is twenty seconds (_DAT_1044eb0c)"),
			F.Guard->SpeciesBlacklistedEntities[0].ExpiresAt - F.World.World.NowSeconds(), 20.0,
			0.001);
	}
	TestFalse(TEXT("FINDING_BODY is cleared (0x10381ba0)"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY));
	// `1038057c`: with `CARRYING_BODY` STANDING the second arm does not run, so the target survives.
	TestTrue(TEXT("a carrying body keeps m_hPickupTarget (10381c80)"),
		F.Guard->SpeciesPickupTarget == F.Other->Handle);
	TestEqual(TEXT("m_iShunnedFindFish is always zeroed (+0x6678)"),
		F.Guard->SpeciesShunnedFindCount, 0);

	// With `CARRYING_BODY` CLEAR the second arm runs and releases the handle.
	F.Guard->NpcFlags.Clear(EElysiumNpcFlag::CARRYING_BODY);
	F.Guard->SpeciesBlacklistedEntities.Reset();
	F.Guard->HengeyokaiTaskFail(0);
	TestFalse(TEXT("a non-carrying body releases m_hPickupTarget (10381c80)"),
		F.Guard->SpeciesPickupTarget.IsSet());
	TestEqual(TEXT("and FINDING_BODY being clear means no second blacklist (10380548)"),
		F.Guard->SpeciesBlacklistedEntities.Num(), 0);

	// --- The Tzimisce arm, `0x103ba350` -----------------------------------------------------------
	// The same shape with its own words, and the same two `m_bfAINPCFlags` bits.
	F.Guard->SpeciesBlacklistedEntities.Reset();
	F.Guard->SpeciesPickupTarget = F.Other->Handle;
	F.Guard->NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);
	F.Guard->SpeciesShunnedFindCount = 5;
	F.Guard->TzimisceTaskFail(0);
	TestEqual(TEXT("the Tzimisce arm blacklists too (0x103bf200)"),
		F.Guard->SpeciesBlacklistedEntities.Num(), 1);
	TestFalse(TEXT("and clears FINDING_BODY (0x103be050)"),
		F.Guard->NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY));
	TestFalse(TEXT("and releases m_hPickupTarget (+0x6670)"), F.Guard->SpeciesPickupTarget.IsSet());
	TestEqual(TEXT("and zeroes m_iShunnedFindBody (+0x66b8)"),
		F.Guard->SpeciesShunnedFindCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelConditions10TaskFailBossTest,
	"Elysium.Substrate.NpcKernelConditions10.TaskFailMingXiaoAndSheriffMan",
	GElysiumNpcKernelConditions10Flags)
bool FElysiumNpcKernelConditions10TaskFailBossTest::RunTest(const FString&)
{
	FCond10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}

	// `CNPC_VMingXiao::TaskFail` (`0x10394090`). Modes 3 and 4 touch the motor and NOTHING else —
	// in particular the mode and the throw handle survive.
	for (const int32 Mode : { 3, 4 })
	{
		F.Guard->SpeciesThrowableObjectMode = Mode;
		F.Guard->SpeciesThrowObject = F.Other->Handle;
		F.Guard->MingXiaoTaskFail(0);
		TestEqual(TEXT("modes 3 and 4 leave m_eThrowableObjectMode alone (103940a5)"),
			F.Guard->SpeciesThrowableObjectMode, Mode);
		TestTrue(TEXT("and leave m_hThrowObject alone"), F.Guard->SpeciesThrowObject.IsSet());
	}

	// **CORRECTION, pinned.** `0x10398d90` is `m_eThrowableObjectMode = arg`, so the default arm
	// sets the MODE to 0 and then releases the handle.
	for (const int32 Mode : { 0, 1, 2, 5 })
	{
		F.Guard->SpeciesThrowableObjectMode = Mode;
		F.Guard->SpeciesThrowObject = F.Other->Handle;
		F.Guard->MingXiaoTaskFail(0);
		TestEqual(TEXT("every other mode is set to 0 (0x10398d90)"),
			F.Guard->SpeciesThrowableObjectMode, 0);
		TestFalse(TEXT("and m_hThrowObject is released (+0x6718)"),
			F.Guard->SpeciesThrowObject.IsSet());
	}

	// `CNPC_VSheriffMan::TaskFail` (`0x103b0290`): the recovered fact is the ABSENCE of an arm.
	F.Guard->bSpeciesPathBlocked = false;
	F.Guard->SpeciesShunnedFindCount = 3;
	F.Guard->SpeciesThrowableObjectMode = 6;
	F.Guard->SpeciesPickupTarget = F.Other->Handle;
	F.Guard->SheriffManTaskFail(12);
	TestFalse(TEXT("the sheriff sets no path-blocked flag (103b0290)"),
		F.Guard->bSpeciesPathBlocked);
	TestEqual(TEXT("the sheriff zeroes no shunned count"), F.Guard->SpeciesShunnedFindCount, 3);
	TestEqual(TEXT("the sheriff touches no throwable mode"),
		F.Guard->SpeciesThrowableObjectMode, 6);
	TestTrue(TEXT("the sheriff releases no pickup target"), F.Guard->SpeciesPickupTarget.IsSet());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
