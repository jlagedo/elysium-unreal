// Story 0019/8 (29e under the strict verdict), family **Boss19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelBoss19.` and the retail address.
//
// Owns (Boss19's `rule` rows): 0x102b52a0 FUN_102b52a0, 0x102c51a0 DoPossession, 0x102c5310
// DoFrenzy, 0x103830e0 FUN_103830e0, 0x10395c70 FUN_10395c70, 0x1039e970 FUN_1039e970, 0x103aa3b0
// CNPC_VSabbatLeader::StartTransformation, 0x10397410 FUN_10397410, 0x10397e90 FUN_10397e90,
// 0x10397f00 FUN_10397f00, 0x10395750 FUN_10395750. Lane L12 also carries Damaged19's
// `CNPC_VTzimisceRunner::vfunc330` `0x103c43b0` here.
//
// Story 8, lane L12. Every assertion is read off the decompiled C or the listing (the address in the
// assertion text). Schedule installs are read through `LastSetScheduleRetail` /
// `bLastSetScheduleForce`, the recorder `0x102ae750`'s port (`FElysiumNpc::SetSchedule`) keeps.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GBoss19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace NpcKernelBoss19Tests
{
	struct FBoss19Fixture
	{
		FElysiumNpcWorldFixture W;
		FElysiumNpc* Subject = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumNpc* Second = nullptr;
		FElysiumPlayer* Player = nullptr;

		// `SubjectClass` and `SecondClass` are the retail classes the two subjects are built as.
		FBoss19Fixture(const TCHAR* SubjectClass, const TCHAR* SecondClass = TEXT("CNPC_VHumanCombatant"))
			: W([SubjectClass, SecondClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("boss19_kernel"), 1919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("subject"), FVector::ZeroVector, SubjectClass);
					Builder.AddNpcOfClass(TEXT("second"), FVector(0.f, 300.f, 0.f), SecondClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f), TEXT("npc_VHumanCombatant"));
					return Builder;
				}())
		{
			Subject = W.Npc(TEXT("subject"));
			Second = W.Npc(TEXT("second"));
			Other = W.Npc(TEXT("other"));
			Player = W.Player();
			FElysiumNpcWorldFixture::Quiet({ Subject, Second, Other });
		}
	};
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19ResetAiStateTest,
	"Elysium.Substrate.NpcKernelBoss19.ResetAiState", GBoss19Flags)
bool FElysiumNpcKernelBoss19ResetAiStateTest::RunTest(const FString&)
{
	// 0x102b52a0
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Subject))
	{
		return false;
	}
	FElysiumNpc& N = *F.Subject;
	ElysiumNpcEnemy::SetEnemy(N, F.Other->Handle);
	N.BaseScheduleHost.MemoryBits = 0xffffffffu;
	N.ResetAiState(false, true);
	TestFalse(TEXT("0x102b52a5 SetEnemy(NULL)"), N.BaseMemory.Enemy.IsSet());
	TestFalse(TEXT("0x102b52ae SetLastEnemy(NULL)"), N.BaseMemory.LastEnemy.IsSet());
	TestEqual(TEXT("0x102b52c5 m_afMemory &= 0xf7fc7fff"), static_cast<int64>(N.BaseScheduleHost.MemoryBits),
		static_cast<int64>(0xf7fc7fffu));
	TestEqual(TEXT("0x102b52f8 the enemy store is cleared"), N.EnemyMemory.Num(), 0);
	TestTrue(TEXT("0x102b52e3 with the \"%s(%d) :\" reason at line 0x5626"),
		N.EnemyStoreClearReason.EndsWith(TEXT("AI_BaseNPCTroika.cpp(22054) :")));
	TestEqual(TEXT("0x102b5319 m_IdealNPCState = IDLE directly"), N.IdealStateRetail(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19DoPossessionTest,
	"Elysium.Substrate.NpcKernelBoss19.DoPossession", GBoss19Flags)
bool FElysiumNpcKernelBoss19DoPossessionTest::RunTest(const FString&)
{
	// 0x102c51a0
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Subject) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Subject;
	N.SetFrenziedWord(0u);
	N.DoPossession(nullptr);
	TestEqual(TEXT("0x102c51aa a null caster is a no-op"), static_cast<int32>(N.FrenziedWord), 0);

	const int32 DisconnectedBefore = N.BaseScheduleHost.SquadDisconnected;
	N.DoPossession(F.Player);
	TestEqual(TEXT("0x102c52a4 m_bfNPCFrenziedFlags = 0x3b1c"), static_cast<int32>(N.FrenziedWord), 0x3b1c);
	TestTrue(TEXT("0x102c5226 D_POSSESSED"), N.NpcFlags.Has(EElysiumNpcFlag2::D_POSSESSED));
	TestTrue(TEXT("0x102c5226 D_DISCONNECT_SQUAD"), N.NpcFlags.Has(EElysiumNpcFlag2::D_DISCONNECT_SQUAD));
	TestTrue(TEXT("0x102c5226 and bit 31"), N.NpcFlags.HasRawWord2Bits(0x80000000u));
	TestEqual(TEXT("0x102c5200 DisconnectFromSquad"), N.BaseScheduleHost.SquadDisconnected,
		DisconnectedBefore + 1);
	TestEqual(TEXT("0x102c523f the player caster gets \"player D_LI 99\""),
		N.Relationships.ResolvePriority(F.Player->Handle, FString()), 99);
	TestEqual(TEXT("0x102c5247 SetFollowerBossName(caster): !player"), N.FollowerBossName,
		FString(TEXT("!player")));
	TestTrue(TEXT("0x102c5282 SetTarget(caster)"), N.GetTarget() == F.Player->Handle);
	TestTrue(TEXT("0x102c5292 m_hFriendPlayer = caster"), N.FriendPlayer == F.Player->Handle);
	TestEqual(TEXT("0x102c527a SetState(IDLE)"), N.NpcStateRetail(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19DoFrenzyTest,
	"Elysium.Substrate.NpcKernelBoss19.DoFrenzy", GBoss19Flags)
bool FElysiumNpcKernelBoss19DoFrenzyTest::RunTest(const FString&)
{
	// 0x102c5310
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VHumanCombatant"));
	if (!TestNotNull(TEXT("the subject stands"), F.Subject) || !TestNotNull(TEXT("the player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Subject;
	N.DoFrenzy(nullptr);
	TestFalse(TEXT("0x102c531a a null caster is a no-op"), N.NpcFlags.Has(EElysiumNpcFlag2::D_INSANE));

	N.DoFrenzy(F.Player);
	TestEqual(TEXT("0x102c5405 m_bfNPCFrenziedFlags = 0x9fbd"), static_cast<int32>(N.FrenziedWord), 0x9fbd);
	TestTrue(TEXT("0x102c53a0 D_INSANE"), N.NpcFlags.Has(EElysiumNpcFlag2::D_INSANE));
	TestFalse(TEXT("0x102c53a0 not D_POSSESSED"), N.NpcFlags.Has(EElysiumNpcFlag2::D_POSSESSED));
	TestEqual(TEXT("0x102c53ec m_eInvestigateMode = 6"), N.InvestigateMode, 6);
	TestEqual(TEXT("0x102c53f2 m_eInvestigateModeCombat = 6"), N.InvestigateModeCombat, 6);
	TestTrue(TEXT("0x102c53ff m_hFriendPlayer = caster"), N.FriendPlayer == F.Player->Handle);
	TestFalse(TEXT("DoFrenzy never calls SetTarget"), N.GetTarget() == F.Player->Handle);
	TestTrue(TEXT("0x102c5247 and never SetFollowerBossName"), N.FollowerBossName.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19HengeyokaiEnterMorphTest,
	"Elysium.Substrate.NpcKernelBoss19.HengeyokaiEnterMorph", GBoss19Flags)
bool FElysiumNpcKernelBoss19HengeyokaiEnterMorphTest::RunTest(const FString&)
{
	// 0x103830e0
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VHengeyokai"));
	FElysiumNpcHengeyokai* H = F.W.NpcAs<FElysiumNpcHengeyokai>(TEXT("subject"));
	if (!TestNotNull(TEXT("the Hengeyokai stands"), H))
	{
		return false;
	}
	H->Skin = 0;
	H->HengeyokaiEnterMorph();
	TestEqual(TEXT("0x103830fe installs 0x16e"), H->LastSetScheduleRetail, 0x16e);
	TestFalse(TEXT("0x103830e3 not forced"), H->bLastSetScheduleForce);
	// `_DAT_1044fab0` is 0.0 in the image (`0x1008d61f FCOMP double`); the lane read it as 0.01.
	TestEqual(TEXT("0x10383107 SetSkinFadeTime(0.0) stores the 0.0 floor (_DAT_1044fab0)"),
		H->HengeyokaiSkinCrossfadeTime, 0.f, 1e-6f);
	TestEqual(TEXT("0x10383110 FadeToSkin(1): m_nSkin = 1"), H->Skin, 1);
	TestEqual(TEXT("0x10383110 m_nSkinCrossfade = the old skin"), H->HengeyokaiSkinCrossfade, 0);
	H->HengeyokaiSkinCrossfade = 7;
	H->HengeyokaiEnterMorph();
	TestEqual(TEXT("FadeToSkin to the same skin writes nothing"), H->HengeyokaiSkinCrossfade, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19MingXiaoEnterDeathTest,
	"Elysium.Substrate.NpcKernelBoss19.MingXiaoEnterDeath", GBoss19Flags)
bool FElysiumNpcKernelBoss19MingXiaoEnterDeathTest::RunTest(const FString&)
{
	// 0x10395c70
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VMingXiao"), TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* Head = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("subject"));
	FElysiumNpcMingXiao* Proxy = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("second"));
	if (!TestNotNull(TEXT("the head stands"), Head) || !TestNotNull(TEXT("the proxy stands"), Proxy))
	{
		return false;
	}
	Head->MingXiaoTentacleId = INDEX_NONE;
	Head->MingXiaoEnterDeath();
	TestEqual(TEXT("0x10395c9b the head installs 0x16d"), Head->LastSetScheduleRetail, 0x16d);
	TestTrue(TEXT("0x10395c90 FORCED"), Head->bLastSetScheduleForce);
	TestEqual(TEXT("0x10395ca5 m_lifeState = LIFE_DYING"), Head->LifeState, 1);
	TestTrue(TEXT("0x10395caf m_bPlayedDeathAnim"), Head->bMingXiaoPlayedDeathAnim);
	TestTrue(TEXT("0x10395cb6 m_bInvincible"), Head->bInvincible);

	Proxy->MingXiaoTentacleId = 2;
	Proxy->MingXiaoEnterDeath();
	TestEqual(TEXT("0x10395c94 a proxy installs 0x16e"), Proxy->LastSetScheduleRetail, 0x16e);

	const int32 Calls = Head->SetScheduleRetailCalls;
	Head->BeginDefeatSequenceOnce();
	TestEqual(TEXT("0x10395ce8 the latch refuses a second death"), Head->SetScheduleRetailCalls, Calls);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19TentacleEnterDeathTest,
	"Elysium.Substrate.NpcKernelBoss19.MingXiaoTentacleEnterDeath", GBoss19Flags)
bool FElysiumNpcKernelBoss19TentacleEnterDeathTest::RunTest(const FString&)
{
	// 0x1039e970
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiaoTentacle* T = F.W.NpcAs<FElysiumNpcMingXiaoTentacle>(TEXT("subject"));
	if (!TestNotNull(TEXT("the tentacle stands"), T))
	{
		return false;
	}
	T->TentaclePhase = 2;
	T->MingXiaoTentacleEnterDeath();
	TestEqual(TEXT("0x1039e991 phase 2 installs 0x16a"), T->LastSetScheduleRetail, 0x16a);
	TestTrue(TEXT("0x1039e9fc FORCED"), T->bLastSetScheduleForce);
	TestEqual(TEXT("0x1039ea01 m_lifeState = LIFE_DYING"), T->LifeState, 1);
	TestTrue(TEXT("0x1039ea0b +0x6699"), T->bTentaclePlayedDeathAnim);
	TestTrue(TEXT("0x1039ea12 m_bInvincible"), T->bInvincible);

	T->TentaclePhase = 3;
	T->MingXiaoTentacleEnterDeath();
	TestEqual(TEXT("0x1039e9d1 phase 3 with no 0x1d sequence installs 0x16c"), T->LastSetScheduleRetail, 0x16c);

	T->TentaclePhase = 0;
	T->MingXiaoTentacleEnterDeath();
	TestEqual(TEXT("0x1039e9eb any other phase installs 0x16b"), T->LastSetScheduleRetail, 0x16b);

	const int32 Calls = T->SetScheduleRetailCalls;
	T->BeginTentacleDefeatOnce();
	TestEqual(TEXT("0x1039ea68 the latch refuses a second death"), T->SetScheduleRetailCalls, Calls);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19SabbatLeaderStartTransformationTest,
	"Elysium.Substrate.NpcKernelBoss19.SabbatLeaderStartTransformation", GBoss19Flags)
bool FElysiumNpcKernelBoss19SabbatLeaderStartTransformationTest::RunTest(const FString&)
{
	// 0x103aa3b0
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VSabbatLeader"));
	FElysiumNpcSabbatLeader* L = F.W.NpcAs<FElysiumNpcSabbatLeader>(TEXT("subject"));
	if (!TestNotNull(TEXT("the leader stands"), L))
	{
		return false;
	}
	L->bSabbatLeaderActivated = false;
	L->SabbatLeaderStartTransformation();
	TestTrue(TEXT("0x103aa409 m_bActivated = 1"), L->bSabbatLeaderActivated);
	EElysiumRelationship Value = EElysiumRelationship::Neutral;
	int32 Priority = 0;
	const bool bRow = L->Relationships.ResolvePersistentRow(FElysiumEntityHandle::Invalid(),
		TEXT("player"), Value, Priority);
	TestTrue(TEXT("0x103aa410 AddClassRelationship(CLASS_PLAYER, ...)"), bRow);
	TestTrue(TEXT("0x103aa410 ... D_HT"), Value == EElysiumRelationship::Hate);
	TestEqual(TEXT("0x103aa410 ... priority 10"), Priority, 10);
	TestEqual(TEXT("0x103aa432 installs 0x163"), L->LastSetScheduleRetail, 0x163);
	TestFalse(TEXT("0x103aa415 not forced"), L->bLastSetScheduleForce);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19SpawnTentacleTest,
	"Elysium.Substrate.NpcKernelBoss19.MingXiaoSpawnTentacle", GBoss19Flags)
bool FElysiumNpcKernelBoss19SpawnTentacleTest::RunTest(const FString&)
{
	// 0x10397410. No `TentacleGenerator` maker stands in this world, so the body runs to the maker
	// resolve and answers retail's own null (`0x10310bc0`'s "Could not find NPCMaker").
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* Head = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("subject"));
	if (!TestNotNull(TEXT("the head stands"), Head))
	{
		return false;
	}
	Head->MingXiaoThrowableObjectMode = 3;
	Head->MingXiaoThrowingTentacle = 3;
	Head->MingXiaoConnectedTentacleCount = 6;
	Head->MingXiaoParticleRequests.Reset();
	TestNull(TEXT("0x103977da no maker, no tentacle"), Head->MingXiaoSpawnTentacle(3));
	TestEqual(TEXT("0x10397424 +0x6714 = the index"), Head->MingXiaoLastLostTentacle, 3);
	TestEqual(TEXT("0x1039743e installs 0x16c"), Head->LastSetScheduleRetail, 0x16c);
	TestFalse(TEXT("0x1039743e not forced"), Head->bLastSetScheduleForce);
	TestEqual(TEXT("0x10397469 mode 3 on the throwing limb runs the throw cleanup"),
		Head->MingXiaoThrowableObjectMode, 0);
	if (TestEqual(TEXT("0x10397480 the burst emitter"), Head->MingXiaoParticleRequests.Num(), 1))
	{
		TestEqual(TEXT("0x10397471 at the limb's bone"), Head->MingXiaoParticleRequests[0].Attachment,
			FString(TEXT("Bip01 L ForearmPiercing")));
	}
	TestEqual(TEXT("0x103977c2 no tentacle, no count change"), Head->MingXiaoConnectedTentacleCount, 6);
	TestEqual(TEXT("0x103986b0 all four attack limbs: (400+400+300+300)/8"),
		Head->MingXiaoIdealRangeFromLimbs(), 175.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19KillTentaclesTest,
	"Elysium.Substrate.NpcKernelBoss19.MingXiaoKillTentacles", GBoss19Flags)
bool FElysiumNpcKernelBoss19KillTentaclesTest::RunTest(const FString&)
{
	// 0x10397e90
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VMingXiao"), TEXT("CNPC_VMingXiaoTentacle"));
	FElysiumNpcMingXiao* Head = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("subject"));
	FElysiumNpcMingXiaoTentacle* T = F.W.NpcAs<FElysiumNpcMingXiaoTentacle>(TEXT("second"));
	if (!TestNotNull(TEXT("the head stands"), Head) || !TestNotNull(TEXT("the tentacle stands"), T))
	{
		return false;
	}
	T->TentaclePhase = 2;
	Head->SeveredTentacles[4] = T->Handle;
	Head->MingXiaoKillTentacles();
	TestTrue(TEXT("0x10397ec7 each live handle starts its death"), T->bTentaclePlayedDeathAnim);
	TestEqual(TEXT("0x1039e991 through the tentacle's own entry"), T->LastSetScheduleRetail, 0x16a);
	TestTrue(TEXT("no handle is cleared afterwards"), Head->SeveredTentacles[4] == T->Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19KillSpawnedBodiesTest,
	"Elysium.Substrate.NpcKernelBoss19.MingXiaoKillSpawnedBodies", GBoss19Flags)
bool FElysiumNpcKernelBoss19KillSpawnedBodiesTest::RunTest(const FString&)
{
	// 0x10397f00
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VMingXiao"), TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* Head = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("subject"));
	FElysiumNpcMingXiao* Proxy = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("second"));
	if (!TestNotNull(TEXT("the head stands"), Head) || !TestNotNull(TEXT("the proxy stands"), Proxy))
	{
		return false;
	}
	Proxy->MingXiaoTentacleId = 1;
	Head->Proxies[1] = Proxy->Handle;
	Head->MingXiaoKillSpawnedBodies();
	TestTrue(TEXT("0x10397f37 the proxy's death starts"), Proxy->bMingXiaoPlayedDeathAnim);
	TestEqual(TEXT("0x10395c94 a proxy installs 0x16e"), Proxy->LastSetScheduleRetail, 0x16e);
	TestFalse(TEXT("the head itself is not killed by the sweep"), Head->bMingXiaoPlayedDeathAnim);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19ApplyTentacleDamageTest,
	"Elysium.Substrate.NpcKernelBoss19.MingXiaoApplyTentacleDamage", GBoss19Flags)
bool FElysiumNpcKernelBoss19ApplyTentacleDamageTest::RunTest(const FString&)
{
	// 0x10395750
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VMingXiao"));
	FElysiumNpcMingXiao* Head = F.W.NpcAs<FElysiumNpcMingXiao>(TEXT("subject"));
	if (!TestNotNull(TEXT("the head stands"), Head))
	{
		return false;
	}
	Head->MingXiaoSeveredTentacleMask = 0u;
	Head->MingXiaoHitPoints[0] = 10.f;
	Head->bInvincible = false;
	Head->MingXiaoParticleRequests.Reset();
	Head->MingXiaoQueuedBodyDamage.Reset();

	FElysiumNpcBase::FElysiumTakeDamageInfo Info;
	Info.Damage = 4.f;
	Head->MingXiaoApplyTentacleDamage(0, Info, nullptr);
	TestEqual(TEXT("0x103957e3 m_rflHitPoints[0] -= damage"), Head->MingXiaoHitPoints[0], 6.f);
	TestEqual(TEXT("0x10395881 the packet's m_flDamage is consumed"), Info.Damage, 0.f);
	TestEqual(TEXT("0x10395847 the damage emitter"), Head->MingXiaoParticleRequests.Num(), 1);
	if (TestEqual(TEXT("0x10395875 not invincible: the amount is queued"),
		Head->MingXiaoQueuedBodyDamage.Num(), 1))
	{
		TestEqual(TEXT("0x1039586e as an integer"), Head->MingXiaoQueuedBodyDamage[0], 4);
	}

	Info.Damage = 7.f;
	Head->MingXiaoApplyTentacleDamage(0, Info, nullptr);
	TestEqual(TEXT("0x103957fa a limb at or below zero is lost: 0x16c"), Head->LastSetScheduleRetail, 0x16c);

	Head->MingXiaoConnectedTentacleCount = 2;
	Info.Damage = 5.f;
	Head->MingXiaoApplyTentacleDamage(-2, Info, nullptr);
	TestEqual(TEXT("0x10395951 the head with at most two limbs: 0x16b"), Head->LastSetScheduleRetail, 0x16b);
	TestEqual(TEXT("0x10395974 that arm leaves the damage alone"), Info.Damage, 5.f);

	Head->MingXiaoSeveredTentacleMask = 0x2u;
	Head->MingXiaoHitPoints[1] = 10.f;
	Head->MingXiaoApplyTentacleDamage(1, Info, nullptr);
	TestEqual(TEXT("0x10395896 a severed limb takes no hit points"), Head->MingXiaoHitPoints[1], 10.f);
	// The rescale tail: `(int)5 / NohitDamageDivide` (Rules.txt, default 4.0, `0x101e73d7`) = 1.25,
	// kept above 1.0 and truncated.
	TestEqual(TEXT("0x103959a5..0x103959fc (int)5 / NohitDamageDivide 4.0 -> 1"), Info.Damage, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelBoss19TzimisceRunnerSlot330Test,
	"Elysium.Substrate.NpcKernelBoss19.TzimisceRunnerSlot330", GBoss19Flags)
bool FElysiumNpcKernelBoss19TzimisceRunnerSlot330Test::RunTest(const FString&)
{
	// 0x103c43b0 (family Damaged19, carried by lane L12). The tail call's target is slot 320
	// `PlayerKnockbackReaction`, lane L09's row; what it does is asserted there. Here: the body
	// dereferences the info (named crash guard on null), picks the shooter's combat character, and
	// dispatches without touching the float.
	using namespace NpcKernelBoss19Tests;
	FBoss19Fixture F(TEXT("CNPC_VTzimisceRunner"));
	FElysiumNpcTzimisceRunner* R = F.W.NpcAs<FElysiumNpcTzimisceRunner>(TEXT("subject"));
	if (!TestNotNull(TEXT("the Runner stands"), R))
	{
		return false;
	}
	R->Slot330(123.f, nullptr);
	FElysiumNpc::FFireBulletsInfo Info;
	Info.Attacker = F.Other;
	R->Slot330(0.f, &Info);
	Info.Attacker = nullptr;
	R->Slot330(0.f, &Info);
	TestTrue(TEXT("0x103c43d4 the Runner answers a near miss through slot 320 without faulting"), true);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
