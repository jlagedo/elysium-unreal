// Story 0019/8 (29e under the strict verdict), family **Script19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelScript19.` and the retail address. Every assertion is
// read off the listing of the body its name cites (`E:/elysium-work/research/npc-kernel-checklist/
// families-19-29/Script19-READING.md` and `vtmb_asm`). Bodies are driven directly; a director's
// ownership is stood by hand (`m_hCine` + `m_hTargetEnt`) where a case must not start the beat
// stand-in's think.
//
// Owns (Script19's `rule` rows): 0x101a8c30 FUN_101a8c30, 0x101a7140 CCineNPC::UpdateOnRemove,
// 0x101a8640 FUN_101a8640, 0x1027d0a0 FUN_1027d0a0, 0x1029f460 FUN_1029f460, 0x1038b1a0
// FUN_1038b1a0, 0x101a7880 CCineNPC::vfunc583, 0x101a8460 SequenceDone, 0x101a8890 FUN_101a8890,
// 0x101a9080 CCineAI::vfunc583, 0x10278220 FUN_10278220, 0x102800c0 ScheduledMoveToGoalEntity,
// 0x102801e0 ScheduledFollowPath, 0x102aa640 FUN_102aa640, 0x102aa860 FUN_102aa860, 0x1037c1c0
// CNPC_VGhoulCroucher::ScriptHide, 0x1038b120 CNPC_VManBat::OverrideMove, 0x101a82d0
// CCineAISchedule::FUN_101a82d0, 0x101a9510 CCineAI::vfunc584, 0x101a9790
// CCineAISchedule::vfunc583.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumAiScriptedSchedule.h"
#include "Substrate/ElysiumAiScriptedSequence.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GScript19TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	template <class T = FElysiumScriptedSequence>
	T* Script19Director(FElysiumEntityWorld& World, const TCHAR* Name)
	{
		FElysiumEntity* Entity = World.FindByName(Name);
		FElysiumNpcBase* Base = Entity != nullptr ? Entity->AsNpcBase() : nullptr;
		return Base != nullptr ? Base->AsSpecies<T>() : nullptr;
	}

	FElysiumEntityDef& Script19AddDirector(FElysiumNpcWorldBuilder& Builder, const TCHAR* Classname,
		const TCHAR* Name, const TCHAR* Target, const FVector& Origin = FVector::ZeroVector)
	{
		FElysiumEntityDef& Def = Builder.AddEntity(Classname, Name, Origin);
		Def.Keys.Add(TEXT("m_iszEntity"), Target);
		return Def;
	}

	// `m_hCine` on the NPC and `m_hTargetEnt` on the director: a possession without the beat think.
	void Script19Own(FElysiumScriptedSequence& Cine, FElysiumNpc& Npc)
	{
		Npc.ScriptOwner = Cine.Handle;
		Cine.SetTarget(Npc.Handle);
	}

	bool Script19Printed(const FElysiumScriptedSequence& Cine, const FString& Line)
	{
		return Cine.DiagnosticsForTest().Contains(Line);
	}

	// The standard two-director world: `jack`, directors `s1`/`s2` of `Classname`, counter `c_end`
	// wired to `s1`'s `OnEndSequence`.
	FElysiumNpcWorldBuilder Script19DirectorWorld(const TCHAR* Map, uint32 Seed, const TCHAR* Classname)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("jack"));
		Builder.AddCounter(TEXT("c_end"));
		Script19AddDirector(Builder, Classname, TEXT("s1"), TEXT("jack"), FVector(300.f, 40.f, 0.f));
		Script19AddDirector(Builder, TEXT("scripted_sequence"), TEXT("s2"), TEXT("jack"));
		Builder.WireOutput(TEXT("s1"), TEXT("OnEndSequence"), TEXT("c_end"));
		return Builder;
	}
}

// -------------------------------------------------------------------------------------------------
// 0x101a8c30 CancelScript
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19CancelScriptTest,
	"Elysium.Substrate.NpcKernelScript19.CancelScript_101a8c30", GScript19TestFlags)
bool FElysiumNpcKernelScript19CancelScriptTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_cancel"), 19001);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("jack"));
	Builder.AddNpc(TEXT("bob"), FVector(200.f, 0.f, 0.f));
	Script19AddDirector(Builder, TEXT("scripted_sequence"), TEXT("grp"), TEXT("jack"));
	Script19AddDirector(Builder, TEXT("scripted_sequence"), TEXT("grp"), TEXT("nobody"), FVector(10.f, 0.f, 0.f));
	Script19AddDirector(Builder, TEXT("scripted_sequence"), TEXT(""), TEXT("bob"), FVector(20.f, 0.f, 0.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumNpc* Bob = F.Npc(TEXT("bob"));
	TArray<FElysiumScriptedSequence*> Grp;
	FElysiumScriptedSequence* Unnamed = nullptr;
	for (const TUniquePtr<FElysiumEntity>& Entity : F.World.Entities())
	{
		FElysiumNpcBase* Base = Entity.IsValid() ? Entity->AsNpcBase() : nullptr;
		FElysiumScriptedSequence* Cine = Base != nullptr ? Base->AsSpecies<FElysiumScriptedSequence>() : nullptr;
		if (Cine == nullptr)
		{
			continue;
		}
		if (Cine->TargetName.IsEmpty())
		{
			Unnamed = Cine;
		}
		else
		{
			Grp.Add(Cine);
		}
	}
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("bob"), Bob) || !TestEqual(TEXT("two grp"), Grp.Num(), 2)
		|| !TestNotNull(TEXT("unnamed"), Unnamed))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack, Bob });
	FElysiumScriptedSequence* Holder = Grp[0]->TargetEntity == TEXT("jack") ? Grp[0] : Grp[1];
	FElysiumScriptedSequence* Other = Holder == Grp[0] ? Grp[1] : Grp[0];
	Script19Own(*Holder, *Jack);
	Jack->WriteNpcStateRetail(4);
	Holder->Delay = 3;
	Other->Play = TEXT("wave");

	// 0x101a8c4b the DevMsg with m_iszPlay; 0x101a8c76..0x101a8cad every same-named entity is
	// ScriptEntityCancel'd: the holder's NPC (in SCRIPT) is released and its m_iDelay zeroed.
	Other->CancelScript();
	TestTrue(TEXT("0x101a8c4b prints 'Cancelling script: wave'"), Script19Printed(*Other, TEXT("Cancelling script: wave")));
	TestFalse(TEXT("0x101a8c82 cancels the SAME-NAMED holder: m_hCine cleared"), Jack->ScriptOwner.IsSet());
	TestEqual(TEXT("ScriptEntityCancel zeroes the holder's m_iDelay (+0x5f70)"), Holder->Delay, 0);

	// 0x101a8c5c an unnamed cine cancels only itself (0x101a8c5f).
	Script19Own(*Unnamed, *Bob);
	Bob->WriteNpcStateRetail(4);
	Script19Own(*Holder, *Jack);
	Jack->WriteNpcStateRetail(4);
	Unnamed->CancelScript();
	TestFalse(TEXT("0x101a8c5f the unnamed cine releases its own NPC"), Bob->ScriptOwner.IsSet());
	TestTrue(TEXT("and nothing else"), Jack->ScriptOwner == Holder->Handle);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a7140 CCineNPC::UpdateOnRemove
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19UpdateOnRemoveTest,
	"Elysium.Substrate.NpcKernelScript19.UpdateOnRemove_101a7140", GScript19TestFlags)
bool FElysiumNpcKernelScript19UpdateOnRemoveTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_remove"), 19002, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	Script19Own(*S1, *Jack);
	Jack->WriteNpcStateRetail(4);
	S1->UpdateOnRemove();   // 0x101a7143 base, then 0x101a7149 ScriptEntityCancel(this)
	TestFalse(TEXT("0x101a7149 ScriptEntityCancel releases the NPC in SCRIPT"), Jack->ScriptOwner.IsSet());
	TestEqual(TEXT("CineCleanup leaves the ideal state IDLE"), Jack->IdealStateRetail(), 1);
	TestFalse(TEXT("and clears its m_hTargetEnt"), Jack->GetTarget().IsSet());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a8640 Finish
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19FinishHoldTest,
	"Elysium.Substrate.NpcKernelScript19.FinishHold_101a8640", GScript19TestFlags)
bool FElysiumNpcKernelScript19FinishHoldTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_hold"), 19003, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	using EThink = FElysiumScriptedSequence::EThinkFunction;
	Script19Own(*S1, *Jack);
	S1->PostIdle = TEXT("idle_hold");
	S1->SpawnFlags = 0x100;
	S1->NextCine = FElysiumEntityHandle::Invalid();

	// 0x101a864d / 0x101a865c / 0x101a8690: post-idle set, 0x100, no live m_hNextCine -> the hold.
	S1->Finish(*Jack);
	TestTrue(TEXT("0x101a86d5 'Post Idle %s finished' with the OWNING cine's post-idle"),
		Script19Printed(*S1, TEXT("Post Idle idle_hold finished")));
	TestEqual(TEXT("0x101a86e7 npc m_scriptState := 2"), FElysiumScriptedSequence::ScriptStateOf(*Jack), 2);
	TestTrue(TEXT("0x101a8708 returns BEFORE cleanup: the NPC stays possessed"), Jack->ScriptOwner == S1->Handle);
	TestTrue(TEXT("and no SUB_Remove is armed"), S1->ThinkFunction == EThink::None);

	// Spawnflag 0x100 clear: the cleanup arm -- SUB_Remove at curtime + 0.1 (spawnflag 4 clear),
	// CineCleanup, slot 586.
	S1->SpawnFlags = 0;
	const double Now = F.World.NowSeconds();
	S1->Finish(*Jack);
	TestTrue(TEXT("0x101a871f ThinkSet(SUB_Remove)"), S1->ThinkFunction == EThink::SubRemove);
	TestTrue(TEXT("0x101a8733 m_flNextThink = curtime + 0.1"), FMath::IsNearlyEqual(S1->CineThinkAt, Now + 0.1, 1.0e-6));
	TestFalse(TEXT("0x101a873f CineCleanup released the NPC"), Jack->ScriptOwner.IsSet());
	TestEqual(TEXT("0x101a8749 slot 586 0x101a8840: ideal state IDLE"), Jack->IdealStateRetail(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19FinishChainTest,
	"Elysium.Substrate.NpcKernelScript19.FinishChain_101a8640", GScript19TestFlags)
bool FElysiumNpcKernelScript19FinishChainTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_chain"), 19004, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	FElysiumScriptedSequence* S2 = Script19Director(F.World, TEXT("s2"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1) || !TestNotNull(TEXT("s2"), S2))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	using EThink = FElysiumScriptedSequence::EThinkFunction;

	// A live m_hNextCine skips the hold even with 0x100 and a post-idle, and gets the NPC:
	// SetTarget(next, npc) then next's slot 583 (0x101a87b7 / 0x101a87c0).
	Script19Own(*S1, *Jack);
	S1->PostIdle = TEXT("idle_hold");
	S1->SpawnFlags = 0x100 | 0x4;
	S1->NextCine = S2->Handle;
	S2->NextScript = TEXT("keep");   // so S2's own possession keeps its m_hNextCine
	S1->Finish(*Jack);
	TestTrue(TEXT("0x101a8712 spawnflag 4 arms no SUB_Remove"), S1->ThinkFunction == EThink::None);
	TestTrue(TEXT("0x101a87c0 the next cine possessed the NPC"), Jack->ScriptOwner == S2->Handle);
	TestTrue(TEXT("0x101a87b7 and targets it"), S2->TargetNpc() == Jack);

	// m_hNextCine == this with spawnflag 4 clear: no self re-possession (0x101a87a9 / 0x101a87b2).
	Script19Own(*S1, *Jack);
	S1->SpawnFlags = 0;
	S1->PostIdle.Reset();
	S1->NextCine = S1->Handle;
	S1->Finish(*Jack);
	TestFalse(TEXT("0x101a87b2 a non-repeatable cine does not re-take its NPC"), Jack->ScriptOwner.IsSet());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x1027d0a0 ExitScriptedSequence
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19ExitScriptTest,
	"Elysium.Substrate.NpcKernelScript19.ExitScriptedSequence_1027d0a0", GScript19TestFlags)
bool FElysiumNpcKernelScript19ExitScriptTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_exit"), 19005, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });

	TestTrue(TEXT("0x1027d0d4 no m_hCine: TRUE, nothing cancelled"), Jack->ExitScriptedSequence());

	Script19Own(*S1, *Jack);
	Jack->WriteNpcStateRetail(4);
	TestTrue(TEXT("0x1027d12a a live m_hCine answers TRUE"), Jack->ExitScriptedSequence());
	TestFalse(TEXT("0x1027d125 after CancelScript released the NPC"), Jack->ScriptOwner.IsSet());

	// LIFE_DYING: ideal DEAD, FALSE, the script stays installed (0x1027d0a9..0x1027d0c9).
	Script19Own(*S1, *Jack);
	Jack->SetDeathReportedForRestore(true);
	TestTrue(TEXT("Event_Killed's latch reads as LIFE_DYING"), Jack->LifeStateIsDying());
	TestFalse(TEXT("0x1027d0c7 a dying NPC answers FALSE"), Jack->ExitScriptedSequence());
	TestEqual(TEXT("0x1027d0bd m_IdealNPCState := 7"), Jack->IdealStateRetail(), 7);
	TestTrue(TEXT("and keeps its m_hCine"), Jack->ScriptOwner == S1->Handle);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a7880 CCineNPC::PossessEntity
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19PossessTest,
	"Elysium.Substrate.NpcKernelScript19.Possess_101a7880", GScript19TestFlags)
bool FElysiumNpcKernelScript19PossessTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_possess"), 19006, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	FElysiumScriptedSequence* S2 = Script19Director(F.World, TEXT("s2"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1) || !TestNotNull(TEXT("s2"), S2))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });

	// No target: nothing (0x101a7891).
	S1->SetTarget(FElysiumEntityHandle::Invalid());
	S1->PossessEntity();
	TestFalse(TEXT("0x101a7891 no m_hTargetEnt: nothing taken"), Jack->ScriptOwner.IsSet());

	// The take, in order.
	Jack->BaseScheduleHost.bRanAi = false;
	Jack->RetailMoveType = 3;
	Jack->RetailMoveCollide = 1;
	Jack->RetailSolidType = 2;
	Jack->RetailSolidFlags = 0x10u;
	const uint32 FlagsBefore = Jack->NpcFlags.RawWord1();
	const int32 Oblivious = Jack->ObliviousCount;
	S1->bInterruptable = false;
	S1->NextScript.Reset();
	S1->NextCine = S2->Handle;
	S1->SpawnFlags = 0x1000;
	S1->MoveTo = 1;
	S1->SetTarget(Jack->Handle);
	S1->PossessEntity();
	TestTrue(TEXT("0x101a78da the not-run-AI block, with CCineNPC's own third line"),
		Script19Printed(*S1, TEXT("   that has not run it's AI yet.....")));
	TestEqual(TEXT("0x101a7c41 m_interruptable clear: 0x1026d130 increments m_iIsOblivious"),
		Jack->ObliviousCount, Oblivious + 1);
	TestFalse(TEXT("0x101a7c50 empty m_iszNextScript clears m_hNextCine"), S1->NextCine.IsSet());
	TestTrue(TEXT("0x101a7c5a m_pGoalEnt := this"), Jack->BaseScheduleHost.GoalEnt == S1->Handle);
	TestTrue(TEXT("0x101a7c6c m_hCine := this"), Jack->ScriptOwner == S1->Handle);
	TestTrue(TEXT("0x101a7c72 SetTarget(npc, this)"), Jack->GetTarget() == S1->Handle);
	TestEqual(TEXT("0x101a7c81 m_saved_movetype"), S1->SavedMoveType, 3);
	TestEqual(TEXT("0x101a7c91 m_saved_movecollide"), S1->SavedMoveCollide, 1);
	TestEqual(TEXT("0x101a7ca1 m_saved_solid"), S1->SavedSolid, 2);
	TestEqual(TEXT("0x101a7cb1 m_saved_solidflags"), S1->SavedSolidFlags, 0x10);
	TestEqual(TEXT("0x101a7cdd m_saved_troika_flags holds the word BEFORE the OR"),
		static_cast<uint32>(S1->SavedTroikaFlags), FlagsBefore);
	TestTrue(TEXT("0x101a7cf6 spawnflag 0x1000 ORs NAV_IGNORE_NPC (0x40)"), (Jack->NpcFlags.RawWord1() & 0x40u) != 0);
	TestEqual(TEXT("0x101a7d33 m_fMoveTo 1: m_scriptState 4"), FElysiumScriptedSequence::ScriptStateOf(*Jack), 4);
	TestEqual(TEXT("0x101a7d3d DelayStart(1) counts this literal scripted_sequence"), S1->Delay, 1);
	TestEqual(TEXT("0x101a7e98 m_IdealNPCState := 4 SCRIPT"), Jack->IdealStateRetail(), 4);

	// The queue arm (0x101a7954..0x101a7b65): a busy NPC is queued behind its cine.
	S2->SetTarget(Jack->Handle);
	S2->PossessEntity();
	TestTrue(TEXT("0x101a7b5a the challenger is queued as the owner's m_hNextCine"), S1->NextCine == S2->Handle);
	TestTrue(TEXT("0x101a7b65 and the NPC is not stolen"), Jack->ScriptOwner == S1->Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19PossessTeleportTest,
	"Elysium.Substrate.NpcKernelScript19.PossessTeleport_101a7880", GScript19TestFlags)
bool FElysiumNpcKernelScript19PossessTeleportTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_teleport"), 19007, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	Jack->BaseScheduleHost.bRanAi = true;
	Jack->SetRuntimeAngles(FVector(10.f, 5.f, 0.f));
	S1->SetRuntimeAngles(FVector(0.f, 135.f, 0.f));
	S1->MoveTo = 4;
	S1->SetTarget(Jack->Handle);
	S1->PossessEntity();
	TestFalse(TEXT("0x101a78d8 m_bRanAI set: no warning block"),
		Script19Printed(*S1, TEXT("   that has not run it's AI yet.....")));
	TestTrue(TEXT("0x101a7d9f slot 181 Teleport to this cine's origin"), Jack->Origin.Equals(S1->Origin, 0.01));
	TestEqual(TEXT("0x101a7e6b only the YAW becomes this cine's"), static_cast<float>(Jack->Angles.Y), 135.f);
	TestEqual(TEXT("...the pitch is the NPC's own"), static_cast<float>(Jack->Angles.X), 10.f);
	TestEqual(TEXT("0x101a7e04 the motor's ideal yaw (no +0x28 flip)"), Jack->MotorIdealYaw, 135.f);
	TestEqual(TEXT("0x101a7e7a case 4 FALLS THROUGH into script state 1"), FElysiumScriptedSequence::ScriptStateOf(*Jack), 1);
	TestEqual(TEXT("and runs no DelayStart"), S1->Delay, 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a8460 SequenceDone
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19SequenceDoneTest,
	"Elysium.Substrate.NpcKernelScript19.SequenceDone_101a8460", GScript19TestFlags)
bool FElysiumNpcKernelScript19SequenceDoneTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_done"), 19008, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });

	// A post-idle and no live m_hNextCine: state 2 and slot 584, the NPC stays possessed.
	Script19Own(*S1, *Jack);
	S1->PostIdle = TEXT("idle_after");
	S1->SpawnFlags = 0x4;
	S1->SequenceDone(*Jack);
	TestEqual(TEXT("0x101a8554 npc m_scriptState := 2"), FElysiumScriptedSequence::ScriptStateOf(*Jack), 2);
	TestTrue(TEXT("0x101a856c slot 584 marks the sequence started"), S1->bSequenceStarted);
	TestTrue(TEXT("and the NPC stays possessed"), Jack->ScriptOwner == S1->Handle);
	F.Advance(F.World.NowSeconds() + 0.1);
	TestEqual(TEXT("0x101a85b4 OnEndSequence fires even so"), F.Counter(TEXT("c_end")), 1.f);

	// No post-idle: Finish (0x101a857b), and OnEndSequence again LAST.
	S1->PostIdle.Reset();
	S1->SequenceDone(*Jack);
	TestFalse(TEXT("0x101a857b Finish released the NPC"), Jack->ScriptOwner.IsSet());
	F.Advance(F.World.NowSeconds() + 0.1);
	TestEqual(TEXT("0x101a85c9 OnEndSequence fires on the Finish arm too"), F.Counter(TEXT("c_end")), 2.f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a8890 AllowInterrupt
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19AllowInterruptTest,
	"Elysium.Substrate.NpcKernelScript19.AllowInterrupt_101a8890", GScript19TestFlags)
bool FElysiumNpcKernelScript19AllowInterruptTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_allow"), 19009, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	S1->SpawnFlags = 0;
	const int32 Oblivious = Jack->ObliviousCount;

	// No target: the latch still tracks the argument (0x101a88aa -> 0x101a8900), no refcount edge.
	S1->SetTarget(FElysiumEntityHandle::Invalid());
	S1->bInterruptable = true;
	S1->AllowInterrupt(false);
	TestFalse(TEXT("0x101a8900 the latch takes the argument with no NPC"), S1->bInterruptable);
	TestEqual(TEXT("and no oblivious edge runs"), Jack->ObliviousCount, Oblivious);

	// Edge set -> clear: 0x1026d130 (+1); clear -> set: 0x10007ea0 (-1); no edge: nothing.
	S1->SetTarget(Jack->Handle);
	S1->bInterruptable = true;
	S1->AllowInterrupt(false);
	TestEqual(TEXT("0x101a88e7 1 -> 0 increments m_iIsOblivious"), Jack->ObliviousCount, Oblivious + 1);
	S1->AllowInterrupt(false);
	TestEqual(TEXT("0x101a88f9 0 -> 0 runs nothing"), Jack->ObliviousCount, Oblivious + 1);
	S1->AllowInterrupt(true);
	TestEqual(TEXT("0x101a88fb 0 -> 1 decrements it"), Jack->ObliviousCount, Oblivious);
	TestTrue(TEXT("0x101a8900 and stores the argument"), S1->bInterruptable);

	// Spawnflag 0x20: the whole body is refused (0x101a889a).
	S1->SpawnFlags = 0x20;
	S1->AllowInterrupt(false);
	TestTrue(TEXT("0x101a889a spawnflag 0x20 leaves the latch alone"), S1->bInterruptable);
	TestEqual(TEXT("and the NPC"), Jack->ObliviousCount, Oblivious);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a82d0 StartSequence (CCineNPC / CCineAISchedule) and 0x101a9510 (CCineAI)
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19StartSequenceTest,
	"Elysium.Substrate.NpcKernelScript19.StartSequence_101a82d0", GScript19TestFlags)
bool FElysiumNpcKernelScript19StartSequenceTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_start"), 19010, TEXT("scripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumScriptedSequence* S1 = Script19Director(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1"), S1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	Script19Own(*S1, *Jack);
	S1->SpawnFlags = 0x4;
	S1->bSequenceStarted = false;
	TestTrue(TEXT("0x101a8403 a named sequence answers TRUE"), S1->StartSequence(*Jack, TEXT("wave"), false));
	TestTrue(TEXT("0x101a82da m_sequenceStarted := 1"), S1->bSequenceStarted);
	TestTrue(TEXT("and leaves the NPC possessed"), Jack->ScriptOwner == S1->Handle);

	// Null name with completeOnEmpty: SequenceDone, FALSE (0x101a82f0 / 0x101a82f9).
	S1->bSequenceStarted = false;
	TestFalse(TEXT("0x101a82f9 the empty play answers FALSE"), S1->StartSequence(*Jack, FString(), true));
	TestTrue(TEXT("0x101a82da the latch is written FIRST"), S1->bSequenceStarted);
	TestFalse(TEXT("0x101a82f0 SequenceDone -> Finish released the NPC"), Jack->ScriptOwner.IsSet());

	// Null name WITHOUT completeOnEmpty: carries on with the empty string and answers TRUE.
	TestTrue(TEXT("0x101a82e9 low byte clear: the empty name continues"), S1->StartSequence(*Jack, FString(), false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19AiStartSequenceTest,
	"Elysium.Substrate.NpcKernelScript19.AiStartSequence_101a9510", GScript19TestFlags)
bool FElysiumNpcKernelScript19AiStartSequenceTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_ai_start"), 19011, TEXT("aiscripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumAiScriptedSequence* A1 = Script19Director<FElysiumAiScriptedSequence>(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1 builds CCineAI"), A1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	Script19Own(*A1, *Jack);
	A1->SpawnFlags = 0x4;
	A1->bSequenceStarted = false;
	TestTrue(TEXT("0x101a9532 the empty play answers TRUE (CCineNPC's answers FALSE)"),
		A1->StartSequence(*Jack, FString(), true));
	TestTrue(TEXT("0x101a9517 m_sequenceStarted := 1"), A1->bSequenceStarted);
	TestFalse(TEXT("0x101a952d SequenceDone ran (Finish released the NPC)"), Jack->ScriptOwner.IsSet());
	TestTrue(TEXT("0x101a9595 a named sequence answers TRUE"), A1->StartSequence(*Jack, TEXT("wave"), false));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a9080 CCineAI::PossessEntity
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19AiPossessTest,
	"Elysium.Substrate.NpcKernelScript19.AiPossess_101a9080", GScript19TestFlags)
bool FElysiumNpcKernelScript19AiPossessTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_ai_possess"), 19012, TEXT("aiscripted_sequence")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumAiScriptedSequence* A1 = Script19Director<FElysiumAiScriptedSequence>(F.World, TEXT("s1"));
	FElysiumScriptedSequence* S2 = Script19Director(F.World, TEXT("s2"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("a1"), A1) || !TestNotNull(TEXT("s2"), S2))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });

	// m_fMoveTo 1: state 4 and NO DelayStart; a standing cine is overwritten, not queued.
	Script19Own(*S2, *Jack);
	Jack->BaseScheduleHost.bRanAi = false;
	A1->MoveTo = 1;
	A1->NextCine = S2->Handle;
	A1->SetTarget(Jack->Handle);
	A1->PossessEntity();
	TestTrue(TEXT("0x101a90d9 the warning block runs..."), Script19Printed(*A1, TEXT("   is targeting an entity(jack)")));
	TestFalse(TEXT("...WITHOUT CCineNPC's 'has not run' line"),
		Script19Printed(*A1, TEXT("   that has not run it's AI yet.....")));
	TestTrue(TEXT("0x101a9180 no queue arm: the NPC is taken from the standing cine"), Jack->ScriptOwner == A1->Handle);
	TestFalse(TEXT("and nothing is queued on it"), S2->NextCine.IsSet());
	TestTrue(TEXT("no m_hNextCine clear on CCineAI"), A1->NextCine == S2->Handle);
	TestEqual(TEXT("0x101a925d m_fMoveTo 1: state 4"), FElysiumScriptedSequence::ScriptStateOf(*Jack), 4);
	TestEqual(TEXT("and no DelayStart"), A1->Delay, 0);
	TestTrue(TEXT("0x101a93c1 '\"jack\" found and used'"), Script19Printed(*A1, TEXT("\"jack\" found and used")));
	TestEqual(TEXT("0x101a93e7 ideal SCRIPT"), Jack->IdealStateRetail(), 4);

	// m_fMoveTo 4: teleport, state 1, FL_ONGROUND removed.
	Jack->Flags |= 0x1;
	A1->MoveTo = 4;
	A1->PossessEntity();
	TestEqual(TEXT("0x101a9391 case 4 writes state 1"), FElysiumScriptedSequence::ScriptStateOf(*Jack), 1);
	TestEqual(TEXT("0x101a939b RemoveFlag(FL_ONGROUND)"), Jack->Flags & 0x1, 0);
	TestTrue(TEXT("0x101a92b2 teleported to the cine"), Jack->Origin.Equals(A1->Origin, 0.01));

	// An out-of-range m_fMoveTo: the DevWarning arm the sequence version has no twin for.
	A1->MoveTo = 9;
	A1->PossessEntity();
	TestTrue(TEXT("0x101a93a9 'aiscript:  invalid Move To Position value!'"),
		Script19Printed(*A1, TEXT("aiscript:  invalid Move To Position value!")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x101a9790 CCineAISchedule::PossessEntity
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19AiSchedulePossessTest,
	"Elysium.Substrate.NpcKernelScript19.AiSchedulePossess_101a9790", GScript19TestFlags)
bool FElysiumNpcKernelScript19AiSchedulePossessTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19DirectorWorld(TEXT("script19_sched_possess"), 19013, TEXT("aiscripted_schedule")));
	FElysiumNpc* Jack = F.Npc(TEXT("jack"));
	FElysiumAiScriptedSchedule* A1 = Script19Director<FElysiumAiScriptedSchedule>(F.World, TEXT("s1"));
	if (!TestNotNull(TEXT("jack"), Jack) || !TestNotNull(TEXT("s1 builds CCineAISchedule"), A1))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Jack });
	const int32 Oblivious = Jack->ObliviousCount;
	A1->SetTarget(FElysiumEntityHandle::Invalid());
	A1->PossessEntity();
	TestTrue(TEXT("0x101a979d no target: nothing printed"), A1->DiagnosticsForTest().IsEmpty());

	Jack->BaseScheduleHost.bRanAi = false;
	A1->bInterruptable = false;
	A1->SetTarget(Jack->Handle);
	// Slot 586 `0x101a98c0` then finds no `goalent` and says so (a Warning in this runtime).
	AddExpectedError(TEXT("Can't find goal entity"), EAutomationExpectedErrorFlags::Contains, 1);
	A1->PossessEntity();
	TestTrue(TEXT("0x101a97e6 the warning block"), Script19Printed(*A1, TEXT("   Otherwise, talk to a programmer.")));
	TestFalse(TEXT("...without the 'has not run' line"), Script19Printed(*A1, TEXT("   that has not run it's AI yet.....")));
	TestEqual(TEXT("0x101a9866 m_interruptable clear: 0x1026d130"), Jack->ObliviousCount, Oblivious + 1);
	TestFalse(TEXT("no m_hCine is written"), Jack->ScriptOwner.IsSet());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x1029f460 BuildPatrolPath
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19BuildPatrolPathTest,
	"Elysium.Substrate.NpcKernelScript19.BuildPatrolPath_1029f460", GScript19TestFlags)
bool FElysiumNpcKernelScript19BuildPatrolPathTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_build_patrol"), 19014);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumNpc::ResetPatrolPathPool();
	using EBuild = FElysiumNpc::EPatrolPathBuild;

	// A dead NPC and a null cell build nothing (0x1029f46b / 0x1029f477).
	const int32 Ids[] = { 5, 6, -1 };
	const int32 StateBefore = Guard->NpcStateRetail();
	Guard->WriteNpcStateRetail(7);
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, -1, 0, Ids, EBuild::Extend);
	TestNull(TEXT("0x1029f46b NPC_STATE_DEAD refuses"), Guard->PatrolPathCell.Path);
	Guard->WriteNpcStateRetail(StateBefore);

	// Extend on an empty cell (InputFollowPatrolPath's shape): allocate, type FORCED 0, repeat 0.
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 9, 3, 0, Ids, EBuild::Extend);
	const FElysiumNpc::FPatrolPathRecord* Path = Guard->PatrolPathCell.Path;
	if (!TestNotNull(TEXT("0x1029f4d3 a path was allocated"), Path))
	{
		return false;
	}
	TestTrue(TEXT("0x1029f4dd the owned byte"), Guard->PatrolPathCell.bOwned);
	TestEqual(TEXT("0x1029f4f4 Extend forces type 0"), Path->Type, 0);
	TestEqual(TEXT("0x1029f4ea and repeat 0"), Path->Repeat, 0);
	TestEqual(TEXT("0x1029f50d two ids appended"), Path->Count, 2);
	TestEqual(TEXT("0x1029f520 type 0 starts at node 0"), Path->Current, 0);
	TestEqual(TEXT("no schedule"), Path->Schedule, 0);

	// Extend onto the existing path: straight to the append (0x1029f4d1 JNZ 0x1029f4f9).
	const int32 More[] = { 7, -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 1, 0, More, EBuild::Extend);
	TestEqual(TEXT("0x1029f4f9 the existing record keeps its type"), Path->Type, 0);
	TestEqual(TEXT("and gains the id"), Path->Count, 3);

	// Replace (InputSetupPatrolType's shape): re-seed, repeat and type as given; type 1 starts LAST.
	const int32 Three[] = { 1, 2, 3, -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 4, 1, 0, Three, EBuild::Replace);
	TestEqual(TEXT("0x1029f4c9 repeat := 4"), Path->Repeat, 4);
	TestEqual(TEXT("0x1029f4f4 type := 1"), Path->Type, 1);
	TestEqual(TEXT("0x1029f4b9 the reset zeroed the count before the append"), Path->Count, 3);
	TestEqual(TEXT("0x10307c20 type 1 starts at min(count-1, 0x7fff)"), Path->Current, 2);

	// A schedule: stored and installed; the interest draw resets m_bPatrolPathUseHint first.
	Guard->ScheduleHost.bPatrolPathUseHint = true;
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 0, ElysiumSched::SCHED_TROIKA_IDLE_STAND, nullptr, EBuild::Replace);
	TestEqual(TEXT("0x1029f531 path +4 := the schedule"), Path->Schedule, ElysiumSched::SCHED_TROIKA_IDLE_STAND);
	TestFalse(TEXT("0x1029f547 0x1029f650 cleared the draw flag (no interest record)"),
		Guard->ScheduleHost.bPatrolPathUseHint);

	// The pool runs dry at 32 (0x10307d30): a Replace on an empty cell then prints and returns.
	FElysiumNpc::ResetPatrolPathPool();
	for (int32 Index = 0; Index < FElysiumNpc::PatrolPathPoolSize; ++Index)
	{
		FElysiumNpc::AllocPatrolPath();
	}
	FElysiumNpc::FPatrolPathCell Dry;
	AddExpectedError(TEXT("Patrol path pool is dry"), EAutomationExpectedErrorFlags::Contains, 1);
	Guard->BuildPatrolPath(&Dry, 1, 0, 0, nullptr, EBuild::Replace);
	TestNull(TEXT("0x1029f49c a dry pool leaves the cell empty"), Dry.Path);
	TestTrue(TEXT("0x1029f494 but the owned byte was already written"), Dry.bOwned);
	FElysiumNpc::ResetPatrolPathPool();
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102aa640 / 0x102aa860 IssuePatrolMove
// -------------------------------------------------------------------------------------------------

namespace
{
	FElysiumNpcWorldBuilder Script19PatrolWorld(const TCHAR* Map, uint32 Seed)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("guard"));
		FElysiumEntityDef& Point = Builder.AddEntity(TEXT("info_node_patrol_point"), TEXT("pp"),
			FVector(400.0, 0.0, 0.0));
		Point.Keys.Add(TEXT("hinttype"), TEXT("10000"));
		Point.Keys.Add(TEXT("Group"), TEXT("pp"));
		return Builder;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19PatrolStartTest,
	"Elysium.Substrate.NpcKernelScript19.IssuePatrolMoveStart_102aa640", GScript19TestFlags)
bool FElysiumNpcKernelScript19PatrolStartTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19PatrolWorld(TEXT("script19_patrol_start"), 19015),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	const FElysiumEntity* Point = Guard != nullptr ? Guard->FindPatrolPoint(TEXT("pp")) : nullptr;
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("the patrol hint"), Point)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	FElysiumNpc::ResetPatrolPathPool();
	using EBuild = FElysiumNpc::EPatrolPathBuild;

	// No path: 0x3d39 / TaskFail(0x1d).
	FElysiumNpc::FPatrolPathCell Empty;
	Guard->IssuePatrolMoveStart(&Empty);
	TestEqual(TEXT("0x102aa7e2 no path fails 0x1d"), Guard->BaseScheduleHost.FailureReason, 0x1d);

	// Node -1 (an empty list reads its own -1 index): 0x3d65 / 0x1d.
	Guard->BaseScheduleHost.FailureReason = 0;
	const int32 None[] = { -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 0, 0, None, EBuild::Replace);
	Guard->IssuePatrolMoveStart(&Guard->PatrolPathCell);
	TestEqual(TEXT("0x102aa7ba node -1 fails 0x1d"), Guard->BaseScheduleHost.FailureReason, 0x1d);

	// Out of range: the miss counter and 0x1d.
	Guard->BaseScheduleHost.FailureReason = 0;
	const int32 Far[] = { 1000000, -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 0, 0, Far, EBuild::Replace);
	const int32 Misses = FElysiumNpc::PatrolNodeMissCounter();
	Guard->IssuePatrolMoveStart(&Guard->PatrolPathCell);
	TestEqual(TEXT("0x102aa750 DAT_106c994c counts the miss"), FElysiumNpc::PatrolNodeMissCounter(), Misses + 1);
	TestEqual(TEXT("0x102aa7e2 and fails 0x1d"), Guard->BaseScheduleHost.FailureReason, 0x1d);

	// A real node: the goal at the node, TaskComplete.
	Guard->BaseScheduleHost.FailureReason = 0;
	Guard->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
	const int32 Node[] = { Point->Handle.Index, -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 0, 0, Node, EBuild::Replace);
	Guard->IssuePatrolMoveStart(&Guard->PatrolPathCell);
	TestTrue(TEXT("0x102aa735 SetGoal routes to the node's position"), Motor->RequestedFeet.Equals(Point->Origin, 0.01));
	TestFalse(TEXT("0x102aa743 TaskComplete, no fail"), Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));

	// Unreachable: TaskFail(0xc).
	Motor->bAcceptMoves = false;
	Guard->IssuePatrolMoveStart(&Guard->PatrolPathCell);
	TestEqual(TEXT("0x102aa792 an unreachable node fails 0xc"), Guard->BaseScheduleHost.FailureReason, 0xc);
	FElysiumNpc::ResetPatrolPathPool();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19PatrolRunTest,
	"Elysium.Substrate.NpcKernelScript19.IssuePatrolMoveRun_102aa860", GScript19TestFlags)
bool FElysiumNpcKernelScript19PatrolRunTest::RunTest(const FString&)
{
	FElysiumNpcWorldFixture F(Script19PatrolWorld(TEXT("script19_patrol_run"), 19016),
		[](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	const FElysiumEntity* Point = Guard != nullptr ? Guard->FindPatrolPoint(TEXT("pp")) : nullptr;
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("the patrol hint"), Point)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	FElysiumNpc::ResetPatrolPathPool();
	using EBuild = FElysiumNpc::EPatrolPathBuild;

	FElysiumNpc::FPatrolPathCell Empty;
	Guard->IssuePatrolMoveRun(&Empty);
	TestEqual(TEXT("0x102aa97d no path fails 0x1d"), Guard->BaseScheduleHost.FailureReason, 0x1d);

	// Node -1: silent (0x102aa88a).
	Guard->BaseScheduleHost.FailureReason = 0;
	const int32 None[] = { -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 0, 0, None, EBuild::Replace);
	const FVector Before = Motor->RequestedFeet;
	Guard->IssuePatrolMoveRun(&Guard->PatrolPathCell);
	TestEqual(TEXT("0x102aa88a node -1 writes nothing"), Guard->BaseScheduleHost.FailureReason, 0);
	TestTrue(TEXT("and issues no goal"), Motor->RequestedFeet.Equals(Before, 0.01));

	// A real node at the hull tolerance; an unreachable one does NOT fail.
	const int32 Node[] = { Point->Handle.Index, -1 };
	Guard->BuildPatrolPath(&Guard->PatrolPathCell, 0, 0, 0, Node, EBuild::Replace);
	Motor->bAcceptMoves = false;
	Guard->IssuePatrolMoveRun(&Guard->PatrolPathCell);
	TestTrue(TEXT("0x102aa954 SetGoal to the node"), Motor->RequestedFeet.Equals(Point->Origin, 0.01));
	TestEqual(TEXT("0x102aa954 its answer is ignored: no fail"), Guard->BaseScheduleHost.FailureReason, 0);
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	Guard->RetailHullExtents(Guard->HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);
	TestEqual(TEXT("0x102aa8d3 the tolerance is NAI_Hull::Width"), Guard->ScheduleHost.GoalToleranceCm,
		static_cast<float>(HullMaxs.Y - HullMins.Y) * ElysiumMove::U);
	FElysiumNpc::ResetPatrolPathPool();
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x10278220 TryMoveToHiddenPosition
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19HiddenPositionTest,
	"Elysium.Substrate.NpcKernelScript19.TryMoveToHiddenPosition_10278220", GScript19TestFlags)
bool FElysiumNpcKernelScript19HiddenPositionTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_hidden"), 19017);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder), [](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();
	const FVector Threat(0.f, 1000.f, 60.f);
	const FVector Candidate(300.f, 0.f, 0.f);

	F.Services.bLineOfSightClear = true;
	TestFalse(TEXT("0x10278303 a candidate the threat SEES (fraction 1.0) is refused"),
		Guard->TryMoveToHiddenPosition(Threat, Candidate, nullptr));

	F.Services.bLineOfSightClear = false;
	Guard->BaseScheduleHost.HintGroup = TEXT("north");
	TestFalse(TEXT("0x10278318 slot 548 refuses (hint group with no hint)"),
		Guard->TryMoveToHiddenPosition(Threat, Candidate, nullptr));
	Guard->BaseScheduleHost.HintGroup.Reset();

	Motor->bLateralCoverReachable = false;
	TestFalse(TEXT("0x10278365 the move probe refuses"), Guard->TryMoveToHiddenPosition(Threat, Candidate, nullptr));

	Motor->bLateralCoverReachable = true;
	TestTrue(TEXT("0x102783fb a hidden, valid, reachable spot: SetGoal's TRUE"),
		Guard->TryMoveToHiddenPosition(Threat, Candidate, nullptr));
	TestTrue(TEXT("0x102783f6 the goal is the candidate"), Motor->RequestedFeet.Equals(Candidate, 0.01));
	TestEqual(TEXT("0x102783d2 activity ACT_RUN (0x13)"), Guard->ScheduleHost.NavigationActivity, 0x13);

	Motor->bAcceptMoves = false;
	TestFalse(TEXT("0x102783fd SetGoal's FALSE is the answer"), Guard->TryMoveToHiddenPosition(Threat, Candidate, nullptr));
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102800c0 ScheduledMoveToGoalEntity / 0x102801e0 ScheduledFollowPath
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19MoveToGoalTest,
	"Elysium.Substrate.NpcKernelScript19.ScheduledMoveToGoalEntity_102800c0", GScript19TestFlags)
bool FElysiumNpcKernelScript19MoveToGoalTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_move_goal"), 19018);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"));
	Builder.AddEntity(TEXT("info_target"), TEXT("goal"), FVector(500.f, 0.f, 0.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder), [](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumEntity* Goal = F.World.FindByName(TEXT("goal"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("goal"), Goal)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();

	TestTrue(TEXT("0x10280187 SetGoal's TRUE is the answer"),
		Guard->ScheduledMoveToGoalEntity(ElysiumSched::IDLE_WALK, Goal, 9));
	TestEqual(TEXT("0x102800cd 0x10280de0 stamped the ideal schedule"), Guard->BaseScheduleHost.IdealScheduleRetail,
		Guard->ResolveIdealScheduleStamp(ElysiumSched::IDLE_WALK));
	TestTrue(TEXT("0x102800d6 m_pGoalEnt := goal"), Guard->BaseScheduleHost.GoalEnt == Goal->Handle);
	TestTrue(TEXT("0x102800e6 the goal is its GetAbsOrigin"), Motor->RequestedFeet.Equals(Goal->Origin, 0.01));
	TestEqual(TEXT("0x10280150 tolerance 128 units"), Guard->ScheduleHost.GoalToleranceCm, 128.f * ElysiumMove::U);
	TestEqual(TEXT("0x10280126 the movement activity is the caller's"), Guard->ScheduleHost.NavigationActivity, 9);

	Motor->bAcceptMoves = false;
	TestFalse(TEXT("an unroutable goal answers FALSE"), Guard->ScheduledMoveToGoalEntity(ElysiumSched::IDLE_WALK, Goal, 0x13));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19FollowPathTest,
	"Elysium.Substrate.NpcKernelScript19.ScheduledFollowPath_102801e0", GScript19TestFlags)
bool FElysiumNpcKernelScript19FollowPathTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_follow_path"), 19019);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpc(TEXT("guard"));
	Builder.AddEntity(TEXT("path_corner"), TEXT("corner"), FVector(0.f, 600.f, 0.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder), [](FElysiumRecordingServices& S) { S.bProvideNpcMotor = true; });
	FElysiumNpc* Guard = F.Npc(TEXT("guard"));
	FElysiumEntity* Corner = F.World.FindByName(TEXT("corner"));
	if (!TestNotNull(TEXT("guard"), Guard) || !TestNotNull(TEXT("corner"), Corner)
		|| !TestTrue(TEXT("a recording motor"), F.Services.NpcMotors.Num() > 0))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Guard });
	FElysiumRecordingNpcMotor* Motor = F.Services.NpcMotors[0].Get();

	// The `-1.0` tolerance keeps the path's; a zero one takes the hull width.
	Guard->ScheduleHost.GoalToleranceCm = 0.f;
	TestTrue(TEXT("0x102802a8 SetGoal's TRUE"), Guard->ScheduledFollowPath(ElysiumSched::IDLE_WALK, Corner, 0x13));
	TestTrue(TEXT("0x102801f5 m_pGoalEnt := corner"), Guard->BaseScheduleHost.GoalEnt == Corner->Handle);
	TestTrue(TEXT("0x102801ff the goal is its origin"), Motor->RequestedFeet.Equals(Corner->Origin, 0.01));
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	Guard->RetailHullExtents(Guard->HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);
	TestEqual(TEXT("0x10280231 -1.0 on a zero path tolerance takes the hull width"),
		Guard->ScheduleHost.GoalToleranceCm, static_cast<float>(HullMaxs.Y - HullMins.Y) * ElysiumMove::U);
	TestEqual(TEXT("0x10280227 ACT_RUN"), Guard->ScheduleHost.NavigationActivity, 0x13);
	Guard->ScheduleHost.GoalToleranceCm = 77.f;
	Guard->ScheduledFollowPath(ElysiumSched::IDLE_WALK, Corner, 9);
	TestEqual(TEXT("and a standing tolerance is kept"), Guard->ScheduleHost.GoalToleranceCm, 77.f);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x1037c1c0 CNPC_VGhoulCroucher::ScriptHide
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19GhoulScriptHideTest,
	"Elysium.Substrate.NpcKernelScript19.GhoulCroucherScriptHide_1037c1c0", GScript19TestFlags)
bool FElysiumNpcKernelScript19GhoulScriptHideTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_ghoul_hide"), 19020);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpcOfClass(TEXT("ghoul"), FVector::ZeroVector, TEXT("CNPC_VGhoulCroucher"));
	Builder.AddEntity(TEXT("info_target"), TEXT("fire"), FVector(0.f, 0.f, 50.f));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcGhoulCroucher* Ghoul = F.NpcAs<FElysiumNpcGhoulCroucher>(TEXT("ghoul"));
	FElysiumEntity* Fire = F.World.FindByName(TEXT("fire"));
	if (!TestNotNull(TEXT("ghoul"), Ghoul) || !TestNotNull(TEXT("fire"), Fire))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Ghoul });

	// No particle: the base half only (0x1037c237 -> 0x1037c29f).
	Ghoul->BurningParticle = FElysiumEntityHandle::Invalid();
	Ghoul->GhoulCroucherScriptHide();
	TestTrue(TEXT("0x1037c229 the Troika ScriptHide ran"), Ghoul->IsHidden());
	TestFalse(TEXT("and nothing else is hidden"), Fire->IsHidden());

	Ghoul->ScriptUnhide();
	Ghoul->BurningParticle = Fire->Handle;
	Ghoul->GhoulCroucherScriptHide();
	TestTrue(TEXT("0x1037c229 the body hides"), Ghoul->IsHidden());
	TestTrue(TEXT("0x1037c286 slot 77 on the burning particle"), Fire->IsHidden());
	TestTrue(TEXT("and m_hBurningParticle is not cleared"), Ghoul->BurningParticle == Fire->Handle);
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x1038b120 CNPC_VManBat::OverrideMove / 0x1038b1a0 the flight step
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19ManBatOverrideMoveTest,
	"Elysium.Substrate.NpcKernelScript19.ManBatOverrideMove_1038b120", GScript19TestFlags)
bool FElysiumNpcKernelScript19ManBatOverrideMoveTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_manbat_override"), 19021);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpcOfClass(TEXT("bat"), FVector::ZeroVector, TEXT("CNPC_VManBat"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcManBat* Bat = F.NpcAs<FElysiumNpcManBat>(TEXT("bat"));
	if (!TestNotNull(TEXT("bat"), Bat))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Bat });
	FElysiumNpcManBat::ManBatStunConVar() = 0;

	// Not flying: the masked compare of +0x6670 against 0xfa0b0699, and no movement.
	Bat->NavSetType(0);
	Bat->Velocity = FVector(1.f, 2.f, 3.f);
	Bat->ManBatHintModeWord = 0;
	const bool bExpected = FElysiumNpcManBat::ManBatHintMode(0) == FElysiumNpc::HintObfuscationFold(0xfa0b0699u);
	TestEqual(TEXT("0x1038b179 SETZ over the two 0x1042fbf0 folds"), Bat->OverrideMove(0.1f), bExpected);
	TestTrue(TEXT("and the flight step did not run"), Bat->Velocity.Equals(FVector(1.f, 2.f, 3.f)));

	// Flying with manbat_stun set: schedule 0x15b, the ConVar cleared, no movement; TRUE.
	Bat->NavSetType(2);
	FElysiumNpcManBat::ManBatStunConVar() = 1;
	TestTrue(TEXT("0x1038b139 navigator state 2 answers TRUE"), Bat->OverrideMove(0.1f));
	TestEqual(TEXT("0x1038b1ec the stun fires once: ConVar::SetValue(0)"), FElysiumNpcManBat::ManBatStunConVar(), 0);
	TestTrue(TEXT("0x1038b1f6 and returns before SetAbsVelocity"), Bat->Velocity.Equals(FVector(1.f, 2.f, 3.f)));
	Bat->NavSetType(0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19ManBatFlyTest,
	"Elysium.Substrate.NpcKernelScript19.ManBatOverrideMoveFly_1038b1a0", GScript19TestFlags)
bool FElysiumNpcKernelScript19ManBatFlyTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_manbat_fly"), 19022);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpcOfClass(TEXT("bat"), FVector::ZeroVector, TEXT("CNPC_VManBat"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcManBat* Bat = F.NpcAs<FElysiumNpcManBat>(TEXT("bat"));
	if (!TestNotNull(TEXT("bat"), Bat))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Bat });
	FElysiumNpcManBat::ManBatStunConVar() = 0;
	Bat->BaseScheduleHost.bMotorAnimationMovement = false;

	// One of the four still activities: velocity only, no facing block (0x1038b24e..0x1038b26d).
	Bat->ActivityNumber = 0x30;
	Bat->MotorIdealYaw = 12345.f;
	const int32 Resets = Bat->ManBatMotorYawSpeedResets;
	Bat->ManBatOverrideMoveFly(0.1f);
	TestEqual(TEXT("0x1038b306 activity 0x30 skips the motor yaw"), Bat->MotorIdealYaw, 12345.f);
	TestEqual(TEXT("and the yaw-speed reset"), Bat->ManBatMotorYawSpeedResets, Resets);

	// Any other activity: the motor yaw and the pitch follow the velocity SetAbsVelocity wrote.
	Bat->ActivityNumber = 0;
	Bat->ManBatFlapTimer = F.World.NowSeconds() + 100.0;   // not due: no selector
	Bat->IdealActivityNumber = -7;
	Bat->ManBatOverrideMoveFly(0.1f);
	const FVector VelocityUnits = Bat->Velocity / ElysiumMove::U;
	TestEqual(TEXT("0x1038b28d the motor's ideal yaw is VecToYaw(velocity)"), Bat->MotorIdealYaw,
		FElysiumNpcManBat::ManBatVecToYaw(VelocityUnits));
	TestEqual(TEXT("0x102e1cf0 the -1.0 rate resets the yaw speed"), Bat->ManBatMotorYawSpeedResets, Resets + 1);
	TestEqual(TEXT("0x1038b2ca the pitch is VecToPitch(velocity)"), static_cast<float>(Bat->Angles.X),
		FElysiumNpcManBat::ManBatVecToPitch(VelocityUnits));
	TestEqual(TEXT("0x1038b2e4 m_flFlapTimer not due: no selector"), Bat->IdealActivityNumber, -7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelScript19ManBatWingTurnTest,
	"Elysium.Substrate.NpcKernelScript19.ManBatWingTurnSelect_1038e720", GScript19TestFlags)
bool FElysiumNpcKernelScript19ManBatWingTurnTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("script19_manbat_wing"), 19023);
	Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
	Builder.AddNpcOfClass(TEXT("bat"), FVector::ZeroVector, TEXT("CNPC_VManBat"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpcManBat* Bat = F.NpcAs<FElysiumNpcManBat>(TEXT("bat"));
	if (!TestNotNull(TEXT("bat"), Bat))
	{
		return false;
	}
	FElysiumNpcWorldFixture::Quiet({ Bat });
	const double Now = F.World.NowSeconds();
	Bat->SetRuntimeAngles(FVector::ZeroVector);

	Bat->ActivityNumber = 0x28;
	Bat->IdealActivityNumber = -7;
	Bat->ManBatWingTurnSelect(FVector(0.f, 0.f, 100.f));
	TestEqual(TEXT("0x1038e72d activity 0x28 is refused"), Bat->IdealActivityNumber, -7);

	Bat->ActivityNumber = 0;
	Bat->ManBatWingTurnSelect(FVector(0.f, 0.f, 40.f));
	TestEqual(TEXT("0x1038e773 vel.z >= 30: 0x1038e640 flap (0x22)"), Bat->IdealActivityNumber, 0x22);
	TestTrue(TEXT("...m_flFlapTimer = curtime + 2.3"), FMath::IsNearlyEqual(Bat->ManBatFlapTimer, Now + 2.3, 1.0e-4));

	// Source yaw 90 (this world's -Y): a 90-degree turn -> 0x1038e6a0 (0x116d).
	Bat->ManBatWingTurnSelect(FVector(0.f, -100.f, 0.f));
	TestEqual(TEXT("0x1038e7eb a turn in [30, 180): 0x116d"), Bat->IdealActivityNumber, 0x116d);
	// Source yaw 270: 0x1038e6e0 (0x116e).
	Bat->ManBatWingTurnSelect(FVector(0.f, 100.f, 0.f));
	TestEqual(TEXT("0x1038e7f8 a turn in [180, 330]: 0x116e"), Bat->IdealActivityNumber, 0x116e);
	// Straight ahead: glide 0x24 (0x1038e670), unless already gliding, which flaps.
	Bat->ManBatWingTurnSelect(FVector(100.f, 0.f, 0.f));
	TestEqual(TEXT("0x1038e81b no turn: glide 0x24"), Bat->IdealActivityNumber, 0x24);
	TestTrue(TEXT("...for 4.0 s"), FMath::IsNearlyEqual(Bat->ManBatFlapTimer, Now + 4.0, 1.0e-4));
	Bat->ActivityNumber = 0x24;
	Bat->ManBatWingTurnSelect(FVector(100.f, 0.f, 0.f));
	TestEqual(TEXT("0x1038e80e already gliding: flap 0x22"), Bat->IdealActivityNumber, 0x22);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
