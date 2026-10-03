// Story 0019/8 (29e under the strict verdict), family **Damaged19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelDamaged19.` and the retail address. Every assertion is
// read off `vtmb_asm 0x102c1ce0` (packet `families-19-29/Damaged19-READING.md` § `0x102c1ce0`).
//
// Damaged19's other eight `rule` rows are species bodies whose full tests live beside the family that
// owns the species' sibling bodies; the gate's `tests` check credits a rule cited in another
// (non-census) test file, so they are not duplicated here:
//   0x103c43b0 CNPC_VTzimisceRunner::vfunc330     -> ElysiumNpcKernelBossTests.cpp
//   0x1037e240 CNPC_VGuard1::NPCInit              -> ElysiumNpcKernelLifecycle2Tests.cpp
//   0x10387140 CNPC_VHumanCombatant::NPCInit      -> ElysiumNpcKernelLifecycle2Tests.cpp
//   0x103dd800 CNPC_VYukie::NPCInit               -> ElysiumNpcKernelLifecycle2Tests.cpp
//   0x103c32c0 CNPC_VTzimisceRunner::HandleAnimEvent -> ElysiumNpcFootstepTests.cpp
//   0x103a4700 CNPC_VPlayerController::NPCThink   -> ElysiumNpcKernelPlayerControllerTests.cpp
//   0x103cb590 CNPC_VWerewolf::NPCThink           -> ElysiumNpcKernelThinkTests.cpp
//   0x10371b70 CNPC_VCop::StartTask               -> ElysiumNpcKernelStartTaskTests_4.cpp
//
// Owns (Damaged19's `rule` rows): 0x102c1ce0 CAI_BaseNPCTroika::ScriptHide ‼.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumScheduleNumbers.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Tests/ElysiumNpcTestFixture.h"

static constexpr EAutomationTestFlags GDamaged19TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr int32 GDamaged19TestStateIdle = 1;
	constexpr int32 GDamaged19TestStateScript = 4;
	constexpr int32 GDamaged19TestStateDead = 7;
	constexpr int32 GDamaged19TestForcedSentinel = 0x7777;

	// `jack` (a Troika guard) and the director `s1` naming him.
	FElysiumNpcWorldBuilder Damaged19HideWorld(const TCHAR* Map, uint32 Seed)
	{
		FElysiumNpcWorldBuilder Builder(Map, Seed);
		Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
		Builder.AddNpc(TEXT("jack"));
		FElysiumEntityDef& Cine = Builder.AddEntity(TEXT("scripted_sequence"), TEXT("s1"),
			FVector(300.f, 40.f, 0.f));
		Cine.Keys.Add(TEXT("m_iszEntity"), TEXT("jack"));
		return Builder;
	}

	FElysiumScriptedSequence* Damaged19Cine(FElysiumEntityWorld& World)
	{
		FElysiumEntity* Entity = World.FindByName(TEXT("s1"));
		FElysiumNpcBase* Base = Entity != nullptr ? Entity->AsNpcBase() : nullptr;
		return Base != nullptr ? Base->AsSpecies<FElysiumScriptedSequence>() : nullptr;
	}

	const TCHAR* const GDamaged19TestKatana = TEXT("item_w_katana");

	// A headless world has no `vdata/items` behind it: one melee weapon, installed for the case.
	FElysiumItemTable MakeDamaged19ItemTable()
	{
		FElysiumItemTable Table;
		FElysiumItemDef Katana;
		Katana.Classname = GDamaged19TestKatana;
		Katana.PrintName = TEXT("Katana");
		Katana.Type = EElysiumItemType::WeaponMelee;
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = TEXT("3 Lethal Close_Combat_Melee DMG_SLASH");
		Katana.Modes.Add(MoveTemp(Mode));
		Table.Items.Add(MoveTemp(Katana));
		Table.Reindex();
		return Table;
	}

	// A held weapon, active: the receiver of the tail slot 77 (`0x102c1e48`).
	FElysiumItem* Damaged19GiveActiveWeapon(FElysiumNpc& Npc)
	{
		const FElysiumEntityHandle Handle = Npc.Inventory.GiveNamedItem(Npc, GDamaged19TestKatana);
		FElysiumEntity* Entity = Npc.World != nullptr ? Npc.World->Resolve(Handle) : nullptr;
		FElysiumItem* Item = Entity != nullptr ? Entity->AsItem() : nullptr;
		if (Item != nullptr)
		{
			Npc.Inventory.SetActiveWeapon(Npc, *Item);
		}
		return Item;
	}

	struct FDamaged19HideCase
	{
		FElysiumItemTable Items;
		bool bInstalled = false;
		FElysiumNpcWorldFixture F;
		FElysiumNpc* Jack = nullptr;
		FElysiumScriptedSequence* Cine = nullptr;
		FElysiumItem* Weapon = nullptr;

		FDamaged19HideCase(const TCHAR* Map, uint32 Seed)
			: Items(MakeDamaged19ItemTable())
			, F(Damaged19HideWorld(Map, Seed), [this](FElysiumRecordingServices&)
				{
					ElysiumItems::Install(Items);
					bInstalled = true;
				})
		{
			Jack = F.Npc(TEXT("jack"));
			Cine = Damaged19Cine(F.World);
			if (Jack != nullptr)
			{
				FElysiumNpcWorldFixture::Quiet({ Jack });
				Weapon = Damaged19GiveActiveWeapon(*Jack);
				Jack->ScheduleHost.ForcedSchedule = GDamaged19TestForcedSentinel;
			}
		}

		~FDamaged19HideCase()
		{
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		FDamaged19HideCase(const FDamaged19HideCase&) = delete;
		FDamaged19HideCase& operator=(const FDamaged19HideCase&) = delete;

		bool Valid(FAutomationTestBase& Test) const
		{
			return Test.TestNotNull(TEXT("jack"), Jack) && Test.TestNotNull(TEXT("s1"), Cine)
				&& Test.TestNotNull(TEXT("the held tire iron"), Weapon)
				&& Test.TestFalse(TEXT("the weapon starts drawn"), Weapon->IsHidden());
		}

		// `m_hCine` on the NPC and `m_hTargetEnt` on the director, without the beat think.
		void Own() const
		{
			Jack->ScriptOwner = Cine->Handle;
			Cine->SetTarget(Jack->Handle);
		}
	};
}

// -------------------------------------------------------------------------------------------------
// 0x102c1ce0 CAI_BaseNPCTroika::ScriptHide -- the gate refuses: not SCRIPT, no live m_hCine
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamaged19ScriptHideGateTest,
	"Elysium.Arm.NpcKernelDamaged19.ScriptHide_0x102c1ce0.GateRefused", GDamaged19TestFlags)
bool FElysiumNpcKernelDamaged19ScriptHideGateTest::RunTest(const FString&)
{
	FDamaged19HideCase C(TEXT("damaged19_hide_gate"), 19101);
	if (!C.Valid(*this))
	{
		return false;
	}
	C.Jack->WriteNpcStateRetail(GDamaged19TestStateIdle);
	const int32 SolidSets = C.Jack->RetailSolidSets;

	C.Jack->ScriptHide();
	// No warning is expected: an unexpected one does not fail the test, so the arm's other writes are
	// what prove it did not run.
	TestEqual(TEXT("0x102c1cfc m_hCine -1 skips to 0x102c1e29: no forced schedule"),
		C.Jack->ScheduleHost.ForcedSchedule, GDamaged19TestForcedSentinel);
	TestEqual(TEXT("and no CineCleanup (0x102c1e12)"), C.Jack->RetailSolidSets, SolidSets);
	TestTrue(TEXT("0x102c1e2b CBaseEntity::ScriptHide hides the body"), C.Jack->IsHidden());
	TestTrue(TEXT("0x102c1e48 slot 77 on the active weapon"), C.Weapon->IsHidden());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102c1ce0 -- a live m_hCine outside SCRIPT: warn, CancelScript, forced 0x6b, base, weapon
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamaged19ScriptHideLiveCineTest,
	"Elysium.Arm.NpcKernelDamaged19.ScriptHide_0x102c1ce0.LiveCine", GDamaged19TestFlags)
bool FElysiumNpcKernelDamaged19ScriptHideLiveCineTest::RunTest(const FString&)
{
	FDamaged19HideCase C(TEXT("damaged19_hide_live"), 19102);
	if (!C.Valid(*this))
	{
		return false;
	}
	C.Jack->WriteNpcStateRetail(GDamaged19TestStateIdle);
	C.Own();

	C.Cine->Delay = 5;

	// 0x102c1d9b / 0x102c1da2: two `Warning()` prints, dead (no output device), deleted by 0019/6.
	C.Jack->ScriptHide();
	// `CancelScript 0x101a8c30` -> `ScriptEntityCancel` (`0x101a8c82`) zeroes the cine's `m_iDelay`
	// and releases the target only in SCRIPT state (`0x101a7149`): an IDLE NPC keeps its `m_hCine`.
	TestEqual(TEXT("0x102c1e00 CancelScript ran on the live cine (m_iDelay := 0)"), C.Cine->Delay, 0);
	TestTrue(TEXT("and, not in SCRIPT, the NPC is not released"), C.Jack->ScriptOwner == C.Cine->Handle);
	TestEqual(TEXT("0x102c1e24 m_iForcedSchedule := 0x6b SCHED_TROIKA_IDLE_DISPOSITION"),
		C.Jack->ScheduleHost.ForcedSchedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestTrue(TEXT("0x102c1e2b the body hides"), C.Jack->IsHidden());
	TestTrue(TEXT("0x102c1e48 and the active weapon"), C.Weapon->IsHidden());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102c1ce0 -- SCRIPT state with a dead m_hCine: **UNKNOWN**, CineCleanup on this NPC, forced 0x6b
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamaged19ScriptHideScriptStateTest,
	"Elysium.Arm.NpcKernelDamaged19.ScriptHide_0x102c1ce0.ScriptStateDeadHandle", GDamaged19TestFlags)
bool FElysiumNpcKernelDamaged19ScriptHideScriptStateTest::RunTest(const FString&)
{
	FDamaged19HideCase C(TEXT("damaged19_hide_script"), 19103);
	if (!C.Valid(*this))
	{
		return false;
	}
	C.Jack->WriteNpcStateRetail(GDamaged19TestStateScript);
	C.Jack->ScriptOwner = FElysiumEntityHandle::Invalid();
	const int32 SolidSets = C.Jack->RetailSolidSets;

	// 0x102c1cf1 SCRIPT enters the warn arm; 0x102c1d32 -> 0x102c1d87 names the cine **UNKNOWN**.
	C.Jack->ScriptHide();
	TestEqual(TEXT("0x102c1e12 CineCleanup ran on this NPC (the dead-cine SetSolid)"),
		C.Jack->RetailSolidSets, SolidSets + 1);
	TestEqual(TEXT("and wrote SetSolidFlags(0x10)"), static_cast<int32>(C.Jack->RetailSolidFlags), 0x10);
	TestEqual(TEXT("0x102c1e24 m_iForcedSchedule := 0x6b"),
		C.Jack->ScheduleHost.ForcedSchedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestTrue(TEXT("0x102c1e2b the body hides"), C.Jack->IsHidden());
	TestTrue(TEXT("0x102c1e48 and the active weapon"), C.Weapon->IsHidden());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102c1ce0 -- SCRIPT state with a live m_hCine: the cancel releases the NPC
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamaged19ScriptHideScriptCineTest,
	"Elysium.Arm.NpcKernelDamaged19.ScriptHide_0x102c1ce0.ScriptStateLiveCine", GDamaged19TestFlags)
bool FElysiumNpcKernelDamaged19ScriptHideScriptCineTest::RunTest(const FString&)
{
	FDamaged19HideCase C(TEXT("damaged19_hide_script_cine"), 19105);
	if (!C.Valid(*this))
	{
		return false;
	}
	C.Jack->WriteNpcStateRetail(GDamaged19TestStateScript);
	C.Own();

	C.Jack->ScriptHide();
	TestFalse(TEXT("0x102c1e00 CancelScript -> ScriptEntityCancel released the SCRIPT-state NPC"),
		C.Jack->ScriptOwner.IsSet());
	TestEqual(TEXT("0x102c1e24 m_iForcedSchedule := 0x6b"),
		C.Jack->ScheduleHost.ForcedSchedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	TestTrue(TEXT("0x102c1e2b the body hides"), C.Jack->IsHidden());
	TestTrue(TEXT("0x102c1e48 and the active weapon"), C.Weapon->IsHidden());
	return true;
}

// -------------------------------------------------------------------------------------------------
// 0x102c1ce0 -- DEAD with a live m_hCine: the cancel runs, the forced schedule is skipped
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamaged19ScriptHideDeadTest,
	"Elysium.Arm.NpcKernelDamaged19.ScriptHide_0x102c1ce0.DeadSkipsForced", GDamaged19TestFlags)
bool FElysiumNpcKernelDamaged19ScriptHideDeadTest::RunTest(const FString&)
{
	FDamaged19HideCase C(TEXT("damaged19_hide_dead"), 19104);
	if (!C.Valid(*this))
	{
		return false;
	}
	C.Jack->WriteNpcStateRetail(GDamaged19TestStateDead);
	C.Own();
	C.Cine->Delay = 5;

	C.Jack->ScriptHide();
	TestEqual(TEXT("0x102c1e00 CancelScript still runs (m_iDelay := 0)"), C.Cine->Delay, 0);
	TestEqual(TEXT("0x102c1e1e JZ 0x102c1e29: DEAD skips the 0x6b write"),
		C.Jack->ScheduleHost.ForcedSchedule, GDamaged19TestForcedSentinel);
	TestTrue(TEXT("0x102c1e2b the body hides"), C.Jack->IsHidden());
	TestTrue(TEXT("0x102c1e48 and the active weapon"), C.Weapon->IsHidden());
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
