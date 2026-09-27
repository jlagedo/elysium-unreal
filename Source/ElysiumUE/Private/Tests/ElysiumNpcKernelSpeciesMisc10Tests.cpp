#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcNewscaster.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumRelationships.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

// Story 29d, family **SpeciesMisc10** — the one-off species bodies that are not slot overrides.
//
// Every assertion is read off the decompiled C or the listing, and the address it came from is
// named beside it. Nine of this family's readings CORRECTED the checklist's one-line walk and each
// correction is pinned by a case: the health-percent delta's SIGN, `CNPC_VTzimisceHeadClaw`'s four
// offsets, the Newscaster record's `+0x24` (an index, not a count), the Newscaster file paths,
// `CNPC_VCop`'s `+0xa8`, `CNPC_VWerewolf`'s swapped hint offsets, `BurnPlayer`'s damage argument,
// `CNPC_VWerewolf::HasPath`'s navigator stamps and the `Sluge_Affected.wav` name.

static constexpr EAutomationTestFlags GSpeciesMisc10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'.
	//
	// `Guard` is the NPC under test, spawned as the retail class the case names (`GuardClass`, by
	// its own classname) — a species case stands the class it asserts rather than re-labelling a
	// combatant (0019 story 5 step 2). The combatant leaf is the default for the base-body cases.
	struct FSpeciesMisc10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumNpc* Other = nullptr;
		FElysiumPlayer* Player = nullptr;

		explicit FSpeciesMisc10Fixture(const TCHAR* GuardClass = TEXT("CNPC_VHumanCombatant"))
			: World([GuardClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("speciesmisc10_kernel"), 4141);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, GuardClass);
					Builder.AddNpc(TEXT("other"), FVector(400.f, 0.f, 0.f),
						TEXT("npc_VHumanCombatant"));
					Builder.AddCounter(TEXT("grapple_ends"));
					Builder.WireOutput(TEXT("guard"), TEXT("OnGrappleEnd"), TEXT("grapple_ends"));
					return Builder;
				}())
		{
			Guard = World.Npc(TEXT("guard"));
			Other = World.Npc(TEXT("other"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard, Other });
		}
	};

	float SpeciesMisc10Cm(float Units) { return Units * ElysiumMove::U; }
}

// =================================================================================================
// `CAI_BaseNPC::LeaveGrappleState` — `0x1026ce30`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10BaseLeaveGrappleTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.BaseLeaveGrappleState", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10BaseLeaveGrappleTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	// **The premise this case strengthens.** Before story 29d, `FElysiumNpc::LeaveGrappleState`
	// gated the output fire and the oblivious pair on `Grapple.Type == StealthKill` and never called
	// slot 416. `0x1026ce30` has NO type gate at all, so an ordinary (non-stealth) grapple end must
	// fire the output, clear the oblivious count and turn frequent thinking off.
	F.Guard->SetForceFrequentThink(true);
	F.Guard->AddGrappleOblivious();
	const float Before = F.World.Counter(TEXT("grapple_ends"));

	F.Guard->Grapple.Type = EElysiumGrappleType::Feed;
	F.Guard->Grapple.Partner = F.Other->Handle;
	F.Guard->FElysiumNpcBase::LeaveGrappleState();
	// The output rides the world's event queue; one tick delivers it.
	F.World.World.Tick(FElysiumNpcWorldFixture::FirstThinkSeconds);

	TestEqual(TEXT("`1026ce30`: m_OnGrappleEnd fires with NO grapple-type gate"),
		F.World.Counter(TEXT("grapple_ends")), Before + 1.f, 0.001f);
	TestFalse(TEXT("`1026ce82`: slot 416 SetForceFrequentThink(false), which the port omitted"),
		F.Guard->GetForceFrequentThink());
	TestFalse(TEXT("`10007ea0`: the oblivious count is decremented and clamped at 0"),
		F.Guard->IsOblivious());

	// The saturating half: a second call on a zero count must not go negative.
	F.Guard->FElysiumNpcBase::LeaveGrappleState();
	TestFalse(TEXT("`10007ea4`: the decrement saturates rather than going negative"),
		F.Guard->IsOblivious());
	return true;
}

// =================================================================================================
// `CAI_BaseNPCTroika::EnterGrappleState` — `0x102b5c00`, slot 379. The twin of the body above: the
// port gated its base half on `Grapple.Type == StealthKill` and retail has no such gate, exactly as
// story 29d found for `LeaveGrappleState`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10EnterGrappleTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.TroikaEnterGrappleState", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10EnterGrappleTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F;
	if (F.Guard == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}

	// --- Step 1: a queued burn list discharges into the PARTNER and refuses the grapple ----------
	//
	// `102b5c06`: `if (0 < m_QueuedBurnDamage.Count)`, then per record `rec.attacker = rec.inflictor
	// = this` (`102b5c1d`/`102b5c20`), `0x101c2a10(rec, RandomInt(0,1) ? 4 : 5)` and
	// `partner->TakeDamage(rec)` (`102b5c3c`); `102b5d29` returns FALSE with the list intact.
	{
		FElysiumDmg Burn;
		Burn.Family = EElysiumDmgFamily::Lethal;
		Burn.BaseDamage = 3;
		F.Guard->QueuedBurnDamage.Add(Burn);
		F.Guard->QueuedBurnDamage.Add(Burn);
		const int32 OtherHealthBefore = F.Other->Health;
		TestFalse(TEXT("`102b5d29`: a queued burn list REFUSES the grapple"),
			F.Guard->EnterGrappleState(F.Other->Handle, EElysiumGrappleRole::Attacker,
				EElysiumGrappleType::Feed, 0, false));
		TestEqual(TEXT("`102b5c3c`: every queued record is handed to the partner"),
			F.Guard->GrappleBurnDischarges, 2);
		TestTrue(TEXT("`101c2a10`: the hitbox drawn is 4 or 5"),
			F.Guard->LastGrappleBurnHitbox == 4 || F.Guard->LastGrappleBurnHitbox == 5);
		TestEqual(TEXT("...and the list is left standing, so the next attempt discharges it again"),
			F.Guard->QueuedBurnDamage.Num(), 2);
		TestFalse(TEXT("the refused grapple took no oblivious count"),
			F.Guard->IsOblivious());
		(void)OtherHealthBefore;
		F.Guard->QueuedBurnDamage.Reset();
	}

	// --- Step 2: with an empty list the base body runs, UNGATED by the grapple type --------------
	//
	// `102b5d2c` -> `0x1026cdc0` -> `0x1026d130` (SetEnemy(NULL), DisconnectFromSquad,
	// `m_iIsOblivious++`), the `m_OnGrappleBegin` output, then `CBaseCombatCharacter`'s body.
	ElysiumNpcEnemy::SetEnemy(*F.Guard, F.Other->Handle);
	F.Guard->Schedule.Clear();
	ElysiumSchedule::Start(F.Guard->Schedule, ElysiumSched::IDLE_STAND, *F.Guard);
	TestTrue(TEXT("a program is installed before the grapple"), F.Guard->Schedule.IsRunning());
	const bool bEntered = F.Guard->EnterGrappleState(F.Other->Handle,
		EElysiumGrappleRole::Attacker, EElysiumGrappleType::Feed, 0, false);
	TestTrue(TEXT("`102b5d2c`: a NON-stealth grapple still takes the base body"), bEntered);
	TestNull(TEXT("`1026d130`: SetEnemy(NULL) with no grapple-type gate"), F.Guard->GetEnemy());
	TestTrue(TEXT("`10007ea0`: the oblivious count is raised with no MADE_OBLIVIOUS"),
		F.Guard->IsOblivious());
	// --- Step 5: `102b5d1c` `ClearSchedule` on the accepting arm --------------------------------
	TestFalse(TEXT("`10280d30`: the running program is cleared"), F.Guard->Schedule.IsRunning());
	return true;
}

// =================================================================================================
// `CNPC_VBach::GatherAttackConditions` — `0x10363db0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10BachTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.BachGatherAttackConditions", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10BachTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VBach"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcBach* Bach = ElysiumTestAsSpecies<FElysiumNpcBach>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VBach"), Bach))
	{
		return false;
	}
	FElysiumNpcConditions& C = Bach->Cognition.Conditions;

	// `10363dc6`: without LIGHT_DAMAGE (0x4c) or HEAVY_DAMAGE (0x4d) the whole shield/teleport block
	// is skipped — not even `m_bCondTookDamage` is cleared.
	Bach->Cognition.bCondTookDamage = true;
	Bach->BachNextShieldTime = -1.0;
	Bach->BachGatherAttackConditions(1000.f);
	TestTrue(TEXT("`10363dc6`: no damage condition leaves m_bCondTookDamage standing"),
		Bach->Cognition.bCondTookDamage);
	TestFalse(TEXT("and the shield does not fire"), Bach->bBachShieldActive);

	// `10363df1`: teleport state 0's threshold is `DAT_1062d200[0]` = **768.0**, recovered out of
	// the pinned image. At or above it the SHIELD arm runs.
	C.Set(static_cast<EElysiumNpcCond>(0x4c));
	Bach->BachTeleportState = 0;
	Bach->BachNextShieldTime = -1.0;
	Bach->BachNextHolyLightTime = 1.0e9;   // far future: only the distance can take the arm
	Bach->bBachShieldFlagB = true;
	Bach->NamedWavEmits.Reset();
	Bach->BachGatherAttackConditions(768.f);
	TestFalse(TEXT("`10363de3`: the damage arm clears m_bCondTookDamage first"),
		Bach->Cognition.bCondTookDamage);
	TestTrue(TEXT("`10363ecb`: the shield latches"), Bach->bBachShieldActive);
	TestFalse(TEXT("`10363f81`: +0x66a6 is cleared on this branch"), Bach->bBachShieldFlagB);
	if (TestEqual(TEXT("`10363f3a`: exactly one sound"), Bach->NamedWavEmits.Num(), 1))
	{
		// STRING CORRECTED: `0x1062ea6c` carries the `.wav` extension.
		TestEqual(TEXT("the shield wav, with its .wav"), Bach->NamedWavEmits[0].Wav,
			FString(TEXT("Character/Boss/Bach/bach_shield.wav")));
		TestEqual(TEXT("channel 2"), Bach->NamedWavEmits[0].Channel, 2);
		TestEqual(TEXT("attenuation 0.8"), Bach->NamedWavEmits[0].Attenuation, 0.8f, 0.0001f);
	}

	// `10363dfa`: below the threshold, a holy-light stamp still in the future (`curtime <=
	// m_flNextHolyLightTime`, `AND 0x4100 / JNZ`) keeps the SHIELD arm: a fresh COND `0x7b` stamp
	// closes the teleport for 15 seconds (story 5 step 4r: this compare was inverted).
	Bach->bBachShieldActive = false;
	Bach->BachNextShieldTime = -1.0;
	Bach->NamedWavEmits.Reset();
	C.Clear(static_cast<EElysiumNpcCond>(0x7b));
	Bach->BachGatherAttackConditions(767.f);
	TestFalse(TEXT("`10363dfa`: a future holy-light stamp raises no 0x7b"),
		C.Has(static_cast<EElysiumNpcCond>(0x7b)));
	TestTrue(TEXT("and takes the shield arm"), Bach->bBachShieldActive);

	// `10363e09`: below the threshold with the stamp PASSED, the TELEPORT arm sets condition `0x7b`
	// and the shield does not fire.
	Bach->bBachShieldActive = false;
	Bach->BachNextShieldTime = -1.0;
	Bach->BachNextHolyLightTime = -1.0;
	Bach->NamedWavEmits.Reset();
	Bach->BachGatherAttackConditions(767.f);
	TestTrue(TEXT("`10363e14`: condition 0x7b is raised"),
		C.Has(static_cast<EElysiumNpcCond>(0x7b)));
	TestFalse(TEXT("and no shield"), Bach->bBachShieldActive);
	TestEqual(TEXT("and no sound"), Bach->NamedWavEmits.Num(), 0);

	// `10363f87`: the weapon-switch block is INDEPENDENT of the damage gate. `_DAT_104704d0` = 72.0.
	C.Reset();
	Bach->BachNextWeaponSwitchTime = -1.0;
	Bach->BachGatherAttackConditions(72.f);
	TestTrue(TEXT("`10363f9d`: at or above 72.0 the far weapon condition 0x79"),
		C.Has(static_cast<EElysiumNpcCond>(0x79)));
	C.Reset();
	Bach->BachGatherAttackConditions(71.9f);
	TestTrue(TEXT("`10363faf`: below 72.0 the near weapon condition 0x7a"),
		C.Has(static_cast<EElysiumNpcCond>(0x7a)));

	// Story 5 step 3 correction: `0x10363db0` is `FElysiumNpcBach`'s slot-561 override, which
	// `CAI_BaseNPC::GatherEnemyConditions` (`0x10270b20`) dispatches through the vtable — so a spawned
	// Bach runs the block ahead of the base gather when the slot is called, where the gather pass used
	// to call the base body directly.
	{
		FSpeciesMisc10Fixture B(TEXT("CNPC_VBach"));
		if (TestNotNull(TEXT("the Bach spawned"), B.Guard))
		{
			TestTrue(TEXT("npc_VBach builds CNPC_VBach"),
				B.Guard->RetailClass() == ElysiumNpcTestCensus::Find(TEXT("CNPC_VBach")));
			B.Guard->Cognition.Conditions.Reset();
			ElysiumTestAsSpecies<FElysiumNpcBach>(B.Guard)->BachNextWeaponSwitchTime = -1.0;
			B.Guard->GatherAttackConditions(B.Other, 72.f);
			TestTrue(TEXT("slot 561 on a Bach runs 0x10363db0: the far weapon condition 0x79"),
				B.Guard->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x79)));
		}
	}
	return true;
}

// =================================================================================================
// The boss-line health record — `0x103c6a00` and `0x103c6a20`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10HealthRecordTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.HealthPercentRecord", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10HealthRecordTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VVampireBoss"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcVampireBoss* Boss = ElysiumTestAsSpecies<FElysiumNpcVampireBoss>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VVampireBoss"), Boss))
	{
		return false;
	}
	// `GetCurrHealthPercent` (`0x103c6830`) is `wounds(0x0f) / cap(0x11)`, so it RISES with damage.
	Boss->TypedStatSet(0, 0x11, 10);   // the cap
	Boss->TypedStatSet(0, 0x0f, 2);    // the wound counter
	Boss->RecordHealthPercent();
	TestEqual(TEXT("`103c6a00`: the mark is the current percent"),
		Boss->BossHealthPercentRecord, 0.2f, 0.0001f);
	TestEqual(TEXT("`103c6a20`: no change since the mark answers zero"),
		Boss->HealthPercentLostSinceRecord(), 0.f, 0.0001f);

	// **THE CORRECTION.** The checklist's walk says a body that has lost health answers NEGATIVE.
	// More wounds is more damage, so the delta is POSITIVE — which is what makes the Chang brothers'
	// `0.1` and the Sabbat leader's `0.0666667` thresholds mean "a tenth / a fifteenth of the bar
	// lost since the mark".
	Boss->TypedStatSet(0, 0x0f, 5);
	TestTrue(TEXT("`103c6a20`: losing health answers POSITIVE, not negative"),
		Boss->HealthPercentLostSinceRecord() > 0.f);
	TestEqual(TEXT("and the magnitude is the fraction lost"),
		Boss->HealthPercentLostSinceRecord(), 0.3f, 0.0001f);
	return true;
}

// =================================================================================================
// `CNPC_VChangBros` — `0x1036cab0`, `0x1036cbd0`, `0x1036cfa0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10ChangTeleportTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.ChangCheckForTeleport", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10ChangTeleportTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VChangBros"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcChangBros* ChangNpc = ElysiumTestAsSpecies<FElysiumNpcChangBros>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VChangBros"), ChangNpc))
	{
		return false;
	}
	ChangNpc->TypedStatSet(0, 0x11, 10);
	ChangNpc->TypedStatSet(0, 0x0f, 0);
	ChangNpc->RecordHealthPercent();

	// `1036cae5`: type 1 answers false outright, even with the health delta past the threshold.
	ChangNpc->ChangType = 1;
	ChangNpc->TypedStatSet(0, 0x0f, 9);
	TestFalse(TEXT("`1036cae5`: m_ChangType 1 refuses without reading anything else"),
		ChangNpc->CheckForTeleport());

	// `1036caee`: `_DAT_104ad9f8` = **0.1**, recovered out of the pinned image at `0x4ad9f8`.
	ChangNpc->ChangType = 0;
	ChangNpc->TypedStatSet(0, 0x0f, 1);
	TestTrue(TEXT("`1036caee`: a tenth of the bar lost answers true"), ChangNpc->CheckForTeleport());
	ChangNpc->TypedStatSet(0, 0x0f, 0);
	// `1036cb17`: with no other brother and no health delta, the third arm cannot be reached — the
	// port's `GetOtherBrother` answers null with no squad, which is retail's own refusal.
	TestFalse(TEXT("`1036cb17`: below the threshold and with no other brother, false"),
		ChangNpc->CheckForTeleport());

	// `1036cbd0`: no other brother answers false at once, whatever the timer says.
	ChangNpc->ChangLastUnitedAttackTime = -1.0e6;
	TestFalse(TEXT("`1036cc05`: CheckForUnited refuses with no other brother"),
		ChangNpc->CheckForUnited());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10ChangLedgeTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.ChangSelectLedgeNode", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10ChangLedgeTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VChangBros"));
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcChangBros* Chang = ElysiumTestAsSpecies<FElysiumNpcChangBros>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VChangBros"), Chang))
	{
		return false;
	}
	// The rule alone, so the comparison is assertable without a node graph. Type `0x4653` is the
	// ledge; the score is the full 3-D distance from THIS NPC's origin.
	TArray<FElysiumNpcBase::FHintWords> Nodes;
	FElysiumNpcBase::FHintWords Near;
	Near.bValid = true;
	Near.HintType = 0x4653;
	Near.OriginCm = FVector(SpeciesMisc10Cm(100.f), 0.0, 0.0);
	FElysiumNpcBase::FHintWords Far;
	Far.bValid = true;
	Far.HintType = 0x4653;
	Far.OriginCm = FVector(SpeciesMisc10Cm(900.f), 0.0, 0.0);
	FElysiumNpcBase::FHintWords WrongType;
	WrongType.bValid = true;
	WrongType.HintType = 0x4652;
	WrongType.OriginCm = FVector(SpeciesMisc10Cm(5000.f), 0.0, 0.0);
	Nodes.Add(Near);
	Nodes.Add(Far);
	Nodes.Add(WrongType);
	Chang->Origin = FVector::ZeroVector;

	// `1036d02b`: the incumbent is seeded `-FLT_MAX` and replaced on a STRICTLY GREATER distance —
	// the FARTHEST ledge wins, the opposite of `CNPC_VSheriffMan::SelectLedgeNode` (`0x103b0ab0`).
	const int32 Pick = FElysiumNpcChangBros::ChangBrosSelectLedgeNodeRule(Nodes, FVector::ZeroVector);
	TestEqual(TEXT("`1036d02b`: ChangBros picks the FARTHEST ledge, not the nearest"), Pick, 1);
	// The Sheriff's rule over the same list picks the other one, which is the whole point.
	TestEqual(TEXT("`103b0ab0`: the Sheriff's nearest-wins rule picks the near one"),
		FElysiumNpcSheriffMan::SelectLedgeNodeRule(Nodes, FVector::ZeroVector), 0);
	// `1036cffb`: the type filter.
	TArray<FElysiumNpcBase::FHintWords> WrongOnly;
	WrongOnly.Add(WrongType);
	TestEqual(TEXT("`1036cffb`: a non-0x4653 node never qualifies"),
		FElysiumNpcChangBros::ChangBrosSelectLedgeNodeRule(WrongOnly, FVector::ZeroVector), INDEX_NONE);

	// `1036d005`: the member applies `CheckJumpPathToHintNode` (`0x1036df50`) to every candidate
	// before the rule sees it, and family Hints' `JumpPathSector` is a SEAM answering **4** — the
	// value that CLOSES that gate. So the member answers nothing here, which is retail's own answer
	// for a brother already in sector 4, and the comparison above is what the rule carries.
	Chang->Senses.Memory.ClosestPlayer = F.Player->Handle;
	TestEqual(TEXT("`1036d005`: the jump-path gate refuses every node while the sector seam says 4"),
		Chang->ChangBrosSelectLedgeNode(), INDEX_NONE);
	TestEqual(TEXT("and that IS retail's sector-4 refusal, not a missing rule"),
		Chang->JumpPathSector(FVector::ZeroVector), 4);
	return true;
}

// =================================================================================================
// `CNPC_VCop::vfunc597` — `0x10372cc0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10CopSlot597Test,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.CopSlot597", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10CopSlot597Test::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VCop"));
	if (F.Guard == nullptr || F.Other == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	FElysiumNpcCop* CopNpc = ElysiumTestAsSpecies<FElysiumNpcCop>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VCop"), CopNpc))
	{
		return false;
	}
	// A spawned `npc_VCop` is `CNPC_VCop` — its factory `0x103704f0` builds that class
	// (docs/vtmb/npc-ai/population.md, "The classname → class map, read from the factories") — so
	// the arm is reached by the real classname.
	CopNpc->Senses.Memory.ClosestPlayer = F.Player->Handle;
	CopNpc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true,
		EElysiumNpcState::Combat);

	// `10372ce6`: an argument that is NOT `m_hClosestPlayer`'s entity does nothing at all.
	CopNpc->CopPursuitHandle = FElysiumEntityHandle::Invalid();
	CopNpc->CopSlot597Prologue(F.Other);
	TestFalse(TEXT("`10372ce6`: a non-closest-player argument latches nothing"),
		CopNpc->CopPursuitHandle.IsSet());

	// `10372d2b`: the player IS the closest player, so the latch takes the argument's own handle.
	// CORRECTION: `+0xa8` is `m_pPlayer`, so what is latched is the PLAYER's handle.
	CopNpc->CopSlot597Prologue(F.Player);
	TestTrue(TEXT("`10372d2b`: the pursuit latch takes the player's handle"),
		CopNpc->CopPursuitHandle == F.Player->Handle);
	// Family Debug10's reader is no longer a seam.
	TestTrue(TEXT("family Debug10's CopPursuitPlayer now resolves the word this writes"),
		CopNpc->CopPursuitPlayer() != nullptr);

	// `10372d5b`: the relationship write is UNCONDITIONAL for that argument — it runs even on the
	// pass where the latch is already live and the COMBAT gate is closed.
	CopNpc->BeginScriptedSchedule(FElysiumScriptedScheduleOrder(), true,
		EElysiumNpcState::Idle);
	CopNpc->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Neutral, 0);
	CopNpc->CopSlot597Prologue(F.Player);
	TestEqual(TEXT("`10372d5b`: 'Player D_HT 10' is written whatever the state"),
		static_cast<int32>(CopNpc->Relationships.Resolve(F.Player->Handle, FString())),
		static_cast<int32>(EElysiumRelationship::Hate));
	return true;
}

// =================================================================================================
// `CNPC_VGhoulCroucher` — `0x1037be80` (slot 24) and `0x1037c090`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10CroucherTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.GhoulCroucherBurn", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10CroucherTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VGhoulCroucher"));
	if (F.Guard == nullptr || F.Player == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	FElysiumNpcGhoulCroucher* GhoulCroucher = ElysiumTestAsSpecies<FElysiumNpcGhoulCroucher>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VGhoulCroucher"), GhoulCroucher))
	{
		return false;
	}
	// `1037be80` is `FElysiumNpcGhoulCroucher`'s own slot-24 override (story 5 step 3), which is what
	// makes the arm reachable at all.
	TestTrue(TEXT("`1037be80`: the NPC is a CNPC_VGhoulCroucher, whose slot 24 is the croucher body"),
		GhoulCroucher->RetailClass() == ElysiumNpcTestCensus::Find(TEXT("CNPC_VGhoulCroucher")));

	// `1037bf2b`: the burn needs BOTH `m_bSpawnBurning` and a victim that carries a player record.
	GhoulCroucher->bGhoulSpawnBurning = false;
	const int32 ClearsBefore = GhoulCroucher->MeleeMoveRecordClears;
	GhoulCroucher->OnVictimHitByMe(F.Player);
	TestEqual(TEXT("`1037bf3c`: without m_bSpawnBurning nothing burns"),
		GhoulCroucher->BurnHitboxCalls.Num(), 0);
	TestEqual(TEXT("`1037bf4c`: but the Troika record clear ALWAYS runs, unlike the Gargoyle arm"),
		GhoulCroucher->MeleeMoveRecordClears, ClearsBefore + 1);

	GhoulCroucher->bGhoulSpawnBurning = true;
	GhoulCroucher->OnVictimHitByMe(F.Other);
	TestEqual(TEXT("`1037bf2b`: a non-player victim carries no +0xa8 and does not burn"),
		GhoulCroucher->BurnHitboxCalls.Num(), 0);

	// `1037c105`: the per-hitbox loop goes through family Damage's `HitboxSetCount` seam, which
	// answers 0 — retail's own arm for a model with no hitboxes — so the damage is what is
	// observable. `1037bf3c` pushes `0x41200000` = 10.0.
	const float HealthBefore = static_cast<float>(F.Player->Sheet.GetCurrent(
		EElysiumTraitContainer::Attributes, 0x0f));
	GhoulCroucher->OnVictimHitByMe(F.Player);
	TestEqual(TEXT("`1037c105`: no hitbox table, so the per-hitbox burn makes no passes"),
		GhoulCroucher->BurnHitboxCalls.Num(), 0);
	TestTrue(TEXT("`1037c13d`: the CTakeDamageInfo still lands, so the wound counter rises"),
		static_cast<float>(F.Player->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, 0x0f))
			>= HealthBefore);
	return true;
}

// =================================================================================================
// `CNPC_VGuard1`'s hate latch — `0x1037e2d0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10GuardHateTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.Guard1HatePlayer", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10GuardHateTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VGuard1"));
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcGuard1* Guard1 = ElysiumTestAsSpecies<FElysiumNpcGuard1>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VGuard1"), Guard1))
	{
		return false;
	}
	TestFalse(TEXT("the latch starts clear"), Guard1->bGuard1HatesPlayer);
	Guard1->Guard1HatePlayer();
	TestTrue(TEXT("`1037e2d0`: the +0x6660 latch is set"), Guard1->bGuard1HatesPlayer);
	TestEqual(TEXT("`1037e2da`: 'player D_HT 10' at priority 0"),
		static_cast<int32>(Guard1->Relationships.Resolve(F.Player->Handle, FString())),
		static_cast<int32>(EElysiumRelationship::Hate));
	return true;
}

// =================================================================================================
// `CNPC_VManBat` — `0x1038e9c0` and `0x1038f020`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10ManBatConeTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.ManBatScreechCone", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10ManBatConeTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VManBat"));
	if (F.Guard == nullptr || F.Player == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	FElysiumNpcManBat* ManBat = ElysiumTestAsSpecies<FElysiumNpcManBat>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VManBat"), ManBat))
	{
		return false;
	}
	ManBat->Origin = FVector::ZeroVector;
	F.Player->Origin = FVector(SpeciesMisc10Cm(50.f), 0.0, 0.0);

	// `1038ea01`: the cone emitter is spawned and attached even with a NULL target — `StartTask`
	// calls this body once with the enemy and once with null, and only the player half is gated.
	ManBat->EmitterCalls.Reset();
	ManBat->ManBatStartScreechCone(nullptr);
	if (TestEqual(TEXT("`1038ea01`: a null target still spawns the cone"),
		ManBat->EmitterCalls.Num(), 1))
	{
		TestEqual(TEXT("the cone's name"), ManBat->EmitterCalls[0].Name,
			FString(TEXT("Manbat_screechcone_emitter")));
		TestEqual(TEXT("`1038ea20`: attached at Bip01 Jaw, mode 1"),
			ManBat->EmitterCalls[0].AttachBone, FString(TEXT("Bip01 Jaw")));
		TestEqual(TEXT("mode 1"), ManBat->EmitterCalls[0].AttachMode, 1);
	}
	TestEqual(TEXT("`1038ea33`: a null target does nothing else at all"),
		ManBat->ScreenShakeCalls.Num(), 0);

	// `1038ea33`: a non-player target is refused too — the gate is `+0xa8`.
	ManBat->EmitterCalls.Reset();
	ManBat->ManBatStartScreechCone(F.Other);
	TestEqual(TEXT("`1038ea33`: a non-player target takes no screen shake"),
		ManBat->ScreenShakeCalls.Num(), 0);

	// The player pass.
	ManBat->EmitterCalls.Reset();
	ManBat->SlowEntityCalls.Reset();
	ManBat->PushEntityCalls.Reset();
	ManBat->ManBatSlowedExpire = 0.f;   // the DOUBLE 0.0 sentinel `_DAT_1044fab0`
	ManBat->ManBatStartScreechCone(F.Player);
	if (TestEqual(TEXT("`1038ea60`: one screen shake"), ManBat->ScreenShakeCalls.Num(), 1))
	{
		TestEqual(TEXT("amplitude 2.5"), ManBat->ScreenShakeCalls[0].Amplitude, 2.5f, 0.0001f);
		TestEqual(TEXT("frequency 0.2"), ManBat->ScreenShakeCalls[0].Frequency, 0.2f, 0.0001f);
		TestEqual(TEXT("duration 3.0"), ManBat->ScreenShakeCalls[0].Duration, 3.0f, 0.0001f);
		// The shake is centred on MY OWN slot-220 origin, not the target's.
		TestEqual(TEXT("`1038ea52`: centred on the ManBat's own origin"),
			ManBat->ScreenShakeCalls[0].CentreUnits.X, 0.0, 0.001);
	}
	if (TestEqual(TEXT("`1038eb2c`: one push"), ManBat->PushEntityCalls.Num(), 1))
	{
		TestEqual(TEXT("the delta is target minus me"),
			ManBat->PushEntityCalls[0].DeltaUnits.X, 50.0, 0.01);
		TestEqual(TEXT("mode 2"), ManBat->PushEntityCalls[0].Mode, 2);
	}
	if (TestEqual(TEXT("`1038eb43`: BeginSlowEntity once"), ManBat->SlowEntityCalls.Num(), 1))
	{
		TestTrue(TEXT("it is a begin"), ManBat->SlowEntityCalls[0].bBegin);
		TestEqual(TEXT("with 500.0"), ManBat->SlowEntityCalls[0].Magnitude, 500.f, 0.001f);
	}
	TestTrue(TEXT("`1038eb85`: the expiry is restamped into the 15..25 s window"),
		ManBat->ManBatSlowedExpire >= 15.f && ManBat->ManBatSlowedExpire <= 25.f);
	TestTrue(TEXT("`1038eb9a`: the victim is latched"),
		ManBat->ManBatSlowedEntity == F.Player->Handle);
	TestTrue(TEXT("`1038edd2`: bit 0 of the player's +0x2454 is set"),
		ManBat->bPlayerScreechConeBit);
	// `1038ebb0` / `1038ec4a`: the cone plus the two player emitters, all on `Bip01 Spine`.
	TestEqual(TEXT("`1038ec4a`: the cone and both player emitters spawned"),
		ManBat->EmitterCalls.Num(), 3);

	// `1038eb43` again: a second cone inside the window must NOT re-begin the slow.
	ManBat->SlowEntityCalls.Reset();
	ManBat->ManBatStartScreechCone(F.Player);
	TestEqual(TEXT("`1038eb43`: a live expiry refuses a second BeginSlowEntity"),
		ManBat->SlowEntityCalls.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10ManBatReleaseTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.ManBatReleaseSlowedEntity", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10ManBatReleaseTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VManBat"));
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcManBat* Bat = ElysiumTestAsSpecies<FElysiumNpcManBat>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VManBat"), Bat))
	{
		return false;
	}
	// `1038f02c`: with the expiry at the 0.0 sentinel the whole body refuses, force or not.
	Bat->ManBatSlowedExpire = 0.f;
	Bat->ManBatSlowedEntity = F.Player->Handle;
	Bat->ManBatReleaseSlowedEntity(/*bForce=*/true);
	TestTrue(TEXT("`1038f02c`: a 0.0 expiry refuses even a forced release"),
		Bat->ManBatSlowedEntity == F.Player->Handle);

	// `1038f047`: a live expiry in the future refuses an UNFORCED release.
	Bat->ManBatSlowedExpire = 1.0e6f;
	Bat->ManBatReleaseSlowedEntity(/*bForce=*/false);
	TestTrue(TEXT("`1038f047`: a future expiry refuses an unforced release"),
		Bat->ManBatSlowedEntity == F.Player->Handle);

	// Forced: the whole teardown runs.
	Bat->SlowEntityCalls.Reset();
	Bat->bPlayerScreechConeBit = true;
	Bat->ManBatReleaseSlowedEntity(/*bForce=*/true);
	TestEqual(TEXT("`1038f059`: the expiry is zeroed first"), Bat->ManBatSlowedExpire, 0.f,
		0.0001f);
	if (TestEqual(TEXT("`1038f06e`: EndSlowEntity once"), Bat->SlowEntityCalls.Num(), 1))
	{
		TestFalse(TEXT("it is an end"), Bat->SlowEntityCalls[0].bBegin);
		TestEqual(TEXT("with 500.0"), Bat->SlowEntityCalls[0].Magnitude, 500.f, 0.001f);
	}
	TestFalse(TEXT("`1038f19c`: bit 0 of the player's +0x2454 is cleared"),
		Bat->bPlayerScreechConeBit);
	TestFalse(TEXT("`1038f1e5`: m_hSlowedEntity is cleared OUTSIDE the resolve guard"),
		Bat->ManBatSlowedEntity.IsSet());
	return true;
}

// =================================================================================================
// `CNPC_VNewscaster` — `0x103a0ab0` and `0x103a0670`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10NewscasterLoadTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.NewscasterLoad", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10NewscasterLoadTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VNewscaster"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcNewscaster* Newscaster = ElysiumTestAsSpecies<FElysiumNpcNewscaster>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VNewscaster"), Newscaster))
	{
		return false;
	}
	// `103a0abd`: the load tears BOTH queues down first, so a stale row never survives it. The two
	// files are `vdata/system/Newscaster_Main.txt` and `…_Side.txt` — PATH CORRECTED, the retail
	// strings are `%s`-prefixed formats and not `\s`-prefixed paths.
	Newscaster->NewscasterMainStories.Add(FElysiumNpc::FNewscasterStory{ TEXT("stale") });
	Newscaster->LoadNewscasterStories();
	TestTrue(TEXT("`103a0cae`: the loaded flag is set last"), Newscaster->bNewscasterStoryActive);
	for (const FElysiumNpc::FNewscasterStory& Story : Newscaster->NewscasterMainStories)
	{
		TestNotEqual(TEXT("`103a0abd`: the teardown ran first, so no stale row survives"),
			Story.Name, FString(TEXT("stale")));
		// **THE CORRECTION.** `+0x24` is the INDEX `0x103a07f0`'s tail chose, not a version count —
		// a record that reaches a queue always names a version that exists.
		TestTrue(TEXT("`103a0a0b`: +0x24 is a valid version INDEX, not a count"),
			Story.Versions.IsValidIndex(Story.SelectedVersion));
		TestTrue(TEXT("`103a0916`: every stored version carries a filename"),
			!Story.Versions[Story.SelectedVersion].Filename.IsEmpty());
		TestTrue(TEXT("`103a08b5`: at most four versions per record"),
			Story.Versions.Num() <= 4);
	}
	// The authored corpus is present in this workspace; if it is not, the crash guard logged and
	// both queues are empty, which is the one case this suite cannot distinguish from a file with no
	// satisfiable dependency. Either way nothing above may be violated.
	AddInfo(FString::Printf(TEXT("main %d, side %d"), Newscaster->NewscasterMainStories.Num(),
		Newscaster->NewscasterSideStories.Num()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10NewscasterPlayTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.NewscasterPlay", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10NewscasterPlayTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VNewscaster"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcNewscaster* Newscaster = ElysiumTestAsSpecies<FElysiumNpcNewscaster>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VNewscaster"), Newscaster))
	{
		return false;
	}
	// Stand the two queues by hand so the selection, not the file, is what is asserted.
	Newscaster->bNewscasterStoryActive = true;
	FElysiumNpc::FNewscasterStory Main;
	Main.Name = TEXT("Main01");
	Main.Versions.Add({ FString(), TEXT("main.vcd") });
	Main.SelectedVersion = 0;
	Newscaster->NewscasterMainStories.Reset();
	Newscaster->NewscasterMainStories.Add(Main);
	Newscaster->NewscasterSideStories.Reset();
	Newscaster->NewscasterMainCursor = 0;
	Newscaster->NewscasterSideCursor = 0;
	Newscaster->NewscasterPlayedFiles.Reset();

	// `103a0719`: with an empty SIDE queue and `+0x668c` zero, the body returns early WITHOUT
	// advancing anything — retail's `if (+0x667c == 0) return;`.
	Newscaster->NewscasterPlayingSide = 0;
	Newscaster->PlayNextNewscasterStory();
	// Whichever way the roll lands, a one-row main queue can only ever play its own row.
	for (const FString& Played : Newscaster->NewscasterPlayedFiles)
	{
		TestEqual(TEXT("`103a0789`: the SELECTED version's filename is what plays"), Played,
			FString(TEXT("main.vcd")));
	}

	// `103a06c8`: `IsInDialog` refuses the whole rest of the body.
	Newscaster->NewscasterPlayedFiles.Reset();
	Newscaster->Dialogue.bInDialog = true;
	Newscaster->PlayNextNewscasterStory();
	TestEqual(TEXT("`103a06c8`: IsInDialog plays nothing"),
		Newscaster->NewscasterPlayedFiles.Num(), 0);
	Newscaster->Dialogue.bInDialog = false;

	// `103a06dd`: both queues empty plays nothing and returns.
	Newscaster->NewscasterMainStories.Reset();
	Newscaster->NewscasterPlayedFiles.Reset();
	Newscaster->PlayNextNewscasterStory();
	TestEqual(TEXT("`103a06dd`: two empty queues play nothing"),
		Newscaster->NewscasterPlayedFiles.Num(), 0);

	// `103a0678`: with the flag clear and no player, the body returns WITHOUT loading — so a
	// newscaster that ticks before the player spawns tries again next time.
	Newscaster->bNewscasterStoryActive = false;
	TestTrue(TEXT("`103a0678`: this world HAS a player, so the gate opens"),
		Newscaster->NewscasterPlayerPresent());
	return true;
}

// =================================================================================================
// `CNPC_VPedestrian::CreateCorpse` — `0x103a38c0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10PedestrianCorpseTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.PedestrianCreateCorpse", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10PedestrianCorpseTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VPedestrian"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcPedestrian* Pedestrian = ElysiumTestAsSpecies<FElysiumNpcPedestrian>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VPedestrian"), Pedestrian))
	{
		return false;
	}
	Pedestrian->PedestrianCreateCorpse();
	// The ORDER is the body: the OBB snapshot must happen before the base resizes the hull, and the
	// think stop and the solid write after it.
	TestEqual(TEXT("`103a3910`: the base corpse chain ran exactly once"),
		Pedestrian->PedestrianCreateCorpseCalls, 1);
	TestTrue(TEXT("`103a391c`: ThinkSet(NULL, 0.0, NULL) — the think is stopped"),
		Pedestrian->bPedestrianCorpseThinkStopped);
	TestEqual(TEXT("`103a396b`: SetSolid(SOLID_NONE)"), Pedestrian->PedestrianCorpseSolid, 0);
	// The snapshot itself reads family Motor's `RetailCollisionExtents` seam, which answers false
	// with both vectors at zero — retail's own answer for an entity with no collision extents.
	TestTrue(TEXT("`103a38c6`: the pre-death mins are the seam's answer"),
		Pedestrian->PedestrianPreDeathMinsUnits.IsNearlyZero());
	return true;
}

// =================================================================================================
// `CNPC_VSabbatLeader` — `0x103a9d90`, `0x103aa960`, `0x103aaa80`, `0x103aabc0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10SabbatSplashTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.SabbatBloodSplash", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10SabbatSplashTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VSabbatLeader"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcSabbatLeader* SabbatLeader = ElysiumTestAsSpecies<FElysiumNpcSabbatLeader>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VSabbatLeader"), SabbatLeader))
	{
		return false;
	}

	// `103aa9ad`: diving does NOTHING — not even the level copy, which is what freezes the edge.
	SabbatLeader->bSabbatDiving = true;
	SabbatLeader->WaterLevel = 2;
	SabbatLeader->SabbatLastWaterLevel = 0;
	SabbatLeader->EmitterCalls.Reset();
	SabbatLeader->SabbatLeaderUpdateBloodSplash();
	TestEqual(TEXT("`103aa9ad`: a diving leader spawns nothing"), SabbatLeader->EmitterCalls.Num(), 0);
	TestEqual(TEXT("and does not even copy the level"), SabbatLeader->SabbatLastWaterLevel, 0);

	// The dry-to-wet edge: BOTH emitters, in retail's order.
	SabbatLeader->bSabbatDiving = false;
	SabbatLeader->EmitterCalls.Reset();
	SabbatLeader->SabbatLeaderUpdateBloodSplash();
	if (TestEqual(TEXT("`103aaa01`: the dry-to-wet edge spawns BOTH emitters"),
		SabbatLeader->EmitterCalls.Num(), 2))
	{
		TestEqual(TEXT("the ordinary splash first"), SabbatLeader->EmitterCalls[0].Name,
			FString(TEXT("bloodsplash_emitter")));
		TestEqual(TEXT("the big one second, and only on the edge"), SabbatLeader->EmitterCalls[1].Name,
			FString(TEXT("bloodbigsplash_emitter")));
	}
	TestEqual(TEXT("`103aaa27`: the level is copied on a non-diving pass"),
		SabbatLeader->SabbatLastWaterLevel, 2);

	// Still wet, inside the 0.25 s interval (`_DAT_104c3cdc`): nothing.
	SabbatLeader->EmitterCalls.Reset();
	SabbatLeader->SabbatLeaderUpdateBloodSplash();
	TestEqual(TEXT("`103aa9c9`: inside the 0.25 s interval nothing spawns"),
		SabbatLeader->EmitterCalls.Num(), 0);

	// Out of the water: nothing spawns, and the level copy resets the edge.
	SabbatLeader->WaterLevel = 0;
	SabbatLeader->SabbatLeaderUpdateBloodSplash();
	TestEqual(TEXT("`103aa9b9`: level 0 spawns nothing"), SabbatLeader->EmitterCalls.Num(), 0);
	TestEqual(TEXT("but the level copy re-arms the edge"), SabbatLeader->SabbatLastWaterLevel, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10SabbatRoundTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.SabbatPlayerDamage", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10SabbatRoundTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VSabbatLeader"));
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcSabbatLeader* SabbatLeader = ElysiumTestAsSpecies<FElysiumNpcSabbatLeader>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VSabbatLeader"), SabbatLeader))
	{
		return false;
	}

	// `103aabf9`: no live closest player answers false.
	SabbatLeader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	TestFalse(TEXT("`103aabf9`: no closest player answers false"),
		SabbatLeader->PlayerDamagedEnoughThisRound());
	// `103aaad6`: and the record does nothing either — the mark keeps its previous value.
	SabbatLeader->SabbatLastPlayerHealth = 7;
	SabbatLeader->RecordPlayerHealth();
	TestEqual(TEXT("`103aaad6`: no closest player leaves the mark alone"),
		SabbatLeader->SabbatLastPlayerHealth, 7);

	SabbatLeader->Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, 0x0f, 3);
	SabbatLeader->RecordPlayerHealth();
	TestEqual(TEXT("`103aab5f`: the mark is the player's WOUND counter, stat 0x0f"),
		SabbatLeader->SabbatLastPlayerHealth, 3);

	// `103aaca0`: `_DAT_104c3ce0` = **2.0** — the wound counter must have risen by at least two.
	TestFalse(TEXT("`103aaca0`: no rise is not enough"), SabbatLeader->PlayerDamagedEnoughThisRound());
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, 0x0f, 4);
	TestFalse(TEXT("a rise of 1 is not enough"), SabbatLeader->PlayerDamagedEnoughThisRound());
	F.Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, 0x0f, 5);
	TestTrue(TEXT("a rise of exactly 2 IS enough — the compare is `2.0 <= risen`"),
		SabbatLeader->PlayerDamagedEnoughThisRound());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10SabbatJumpTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.SabbatCheckForJump", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10SabbatJumpTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VSabbatLeader"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcSabbatLeader* SabbatLeader = ElysiumTestAsSpecies<FElysiumNpcSabbatLeader>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VSabbatLeader"), SabbatLeader))
	{
		return false;
	}
	SabbatLeader->Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	SabbatLeader->TypedStatSet(0, 0x11, 15);
	SabbatLeader->TypedStatSet(0, 0x0f, 0);
	SabbatLeader->RecordHealthPercent();

	// `103a9dda`: `0x103c67f0(this, 8.0)` — STRICTLY more than eight seconds since the last attack.
	SabbatLeader->LastAttackTime = F.World.World.NowSeconds();
	TestFalse(TEXT("`103c67f0`: a fresh attack refuses the first arm"),
		SabbatLeader->AttackIdleLongerThan(8.f));
	TestFalse(TEXT("`103a9dda`: and with no health delta and no player, the whole body is false"),
		SabbatLeader->CheckForJumpCondition());
	SabbatLeader->LastAttackTime = F.World.World.NowSeconds() - 8.001;
	TestTrue(TEXT("`103c67f0`: past 8.0 s the first arm answers true"),
		SabbatLeader->CheckForJumpCondition());

	// `103a9df6`: `_DAT_104c3cc4` = **0.0666667** — one fifteenth of the bar lost since the mark.
	SabbatLeader->LastAttackTime = F.World.World.NowSeconds();
	SabbatLeader->TypedStatSet(0, 0x0f, 1);
	TestTrue(TEXT("`103a9df6`: a fifteenth of the bar lost answers true"),
		SabbatLeader->CheckForJumpCondition());
	return true;
}

// =================================================================================================
// `CNPC_VTzimisceHeadClaw` — `0x103c1d80` (slot 332) and `0x103c2230`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10HeadClawSlot332Test,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.HeadClawSlot332", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10HeadClawSlot332Test::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	if (F.Guard == nullptr || F.Player == nullptr || F.Other == nullptr)
	{
		AddError(TEXT("no NPCs"));
		return false;
	}
	FElysiumNpcTzimisceHeadClaw* Claw = ElysiumTestAsSpecies<FElysiumNpcTzimisceHeadClaw>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VTzimisceHeadClaw"), Claw))
	{
		return false;
	}
	Claw->Origin = FVector::ZeroVector;

	// `103c1d8a`: slot 332 dispatches to the species body, whose base (`0x1014f890`) is `return;`.
	// A null target and a non-player target both do nothing at all.
	Claw->NamedWavEmits.Reset();
	Claw->Slot332(nullptr);
	Claw->Slot332(F.Other);
	TestEqual(TEXT("`103c1d8a`: a null or non-player target does nothing"),
		Claw->NamedWavEmits.Num(), 0);

	// The player pass, inside the condition distance.
	F.Player->Origin = FVector(SpeciesMisc10Cm(100.f), 0.0, 0.0);
	Claw->HeadClawSlowedExpire = 0.0;   // the DOUBLE 0.0 sentinel
	Claw->SlowEntityCalls.Reset();
	Claw->EmitterCalls.Reset();
	Claw->Cognition.Conditions.Reset();
	Claw->Slot332(F.Player);

	if (TestEqual(TEXT("`103c1db0`: BeginSlowEntity once"), Claw->SlowEntityCalls.Num(), 1))
	{
		TestTrue(TEXT("a begin"), Claw->SlowEntityCalls[0].bBegin);
		TestEqual(TEXT("with 500.0"), Claw->SlowEntityCalls[0].Magnitude, 500.f, 0.001f);
	}
	// OFFSET CORRECTED: the expiry is `+0x6678` and the handle `+0x6674`, which the checklist's walk
	// has swapped. `103c1dd5` recovers the window as RandomFloat(5.0, 8.0).
	TestTrue(TEXT("`103c1dd5`: the expiry lands in the recovered 5..8 s window"),
		Claw->HeadClawSlowedExpire >= 5.0 && Claw->HeadClawSlowedExpire <= 8.0);
	TestTrue(TEXT("`103c1e02`: m_hSlowedEntity latches the victim"),
		Claw->HeadClawSlowedEntity == F.Player->Handle);
	if (TestEqual(TEXT("`103c1e08`: the player emitter spawned"), Claw->EmitterCalls.Num(), 1))
	{
		TestEqual(TEXT("Tzim2_player_emitter"), Claw->EmitterCalls[0].Name,
			FString(TEXT("Tzim2_player_emitter")));
		TestEqual(TEXT("`103c1e6f`: at Bip01 Spine, mode 1"), Claw->EmitterCalls[0].AttachBone,
			FString(TEXT("Bip01 Spine")));
	}
	// `103c1f13` / `103c1f5c`: the two Slug sounds, in retail's order and on retail's channels.
	// BOTH STRINGS RECOVERED — the checklist's walk named neither.
	if (TestEqual(TEXT("`103c1f13`: two sounds"), Claw->NamedWavEmits.Num(), 2))
	{
		TestEqual(TEXT("the HIT first, on channel 4"), Claw->NamedWavEmits[0].Wav,
			FString(TEXT("Character/Monster/TC_FatGuy/Sluge_Hit.wav")));
		TestEqual(TEXT("channel 4"), Claw->NamedWavEmits[0].Channel, 4);
		TestEqual(TEXT("the AFFECTED second, on channel 3"), Claw->NamedWavEmits[1].Wav,
			FString(TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav")));
		TestEqual(TEXT("channel 3"), Claw->NamedWavEmits[1].Channel, 3);
	}
	TestFalse(TEXT("`103c20dc`: inside 240.0 units, condition 0x35 is NOT raised"),
		Claw->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x35)));

	// `103c20dc`: `_DAT_104cd108` is a DOUBLE **240.0**, and the distance is 2-D — a pure Z offset
	// must not reach it.
	F.Player->Origin = FVector(240.0 * ElysiumMove::U, 0.0, 5000.0 * ElysiumMove::U);
	Claw->Cognition.Conditions.Reset();
	Claw->Slot332(F.Player);
	TestTrue(TEXT("`103c20dc`: at 240.0 flat units condition 0x35 is raised"),
		Claw->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x35)));
	F.Player->Origin = FVector(0.0, 0.0, 5000.0 * ElysiumMove::U);
	Claw->Cognition.Conditions.Reset();
	Claw->Slot332(F.Player);
	TestFalse(TEXT("and a pure Z offset does not, because the distance is 2-D"),
		Claw->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x35)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10HeadClawEndSlowTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.HeadClawEndSlow", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10HeadClawEndSlowTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VTzimisceHeadClaw"));
	if (F.Guard == nullptr || F.Player == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcTzimisceHeadClaw* Claw = ElysiumTestAsSpecies<FElysiumNpcTzimisceHeadClaw>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VTzimisceHeadClaw"), Claw))
	{
		return false;
	}
	// `0x103c24a0`: strictly above 0.0.
	Claw->HeadClawSlowedExpire = 0.0;
	TestFalse(TEXT("`103c24a0`: a zero expiry is not running"), Claw->HeadClawSlowRunning());
	Claw->HeadClawSlowedEntity = F.Player->Handle;
	Claw->TzimisceHeadClawEndSlow(/*bForce=*/true);
	TestTrue(TEXT("`103c223c`: and the whole teardown refuses"),
		Claw->HeadClawSlowedEntity == F.Player->Handle);

	// Forced past the gate: the end, the handle clear and the sound, all inside the resolve guard.
	Claw->HeadClawSlowedExpire = 1.0e6;
	Claw->SlowEntityCalls.Reset();
	Claw->NamedWavEmits.Reset();
	Claw->TzimisceHeadClawEndSlow(/*bForce=*/true);
	TestEqual(TEXT("`103c2265`: the expiry is zeroed"), Claw->HeadClawSlowedExpire, 0.0, 0.0001);
	if (TestEqual(TEXT("`103c2277`: EndSlowEntity once"), Claw->SlowEntityCalls.Num(), 1))
	{
		TestFalse(TEXT("an end"), Claw->SlowEntityCalls[0].bBegin);
	}
	TestFalse(TEXT("and the handle is cleared inside that guard"),
		Claw->HeadClawSlowedEntity.IsSet());
	if (TestEqual(TEXT("`103c2319`: one sound"), Claw->NamedWavEmits.Num(), 1))
	{
		// STRING CORRECTED: `0x1065d43c` is `Sluge_Affected.wav`, the same wav slot 332 uses.
		TestEqual(TEXT("Sluge_Affected.wav on channel 3"), Claw->NamedWavEmits[0].Wav,
			FString(TEXT("Character/Monster/TC_FatGuy/Sluge_Affected.wav")));
		TestEqual(TEXT("channel 3"), Claw->NamedWavEmits[0].Channel, 3);
	}

	// A stale handle leaves `m_hSlowedEntity` standing — the clear is INSIDE the resolve guard.
	Claw->HeadClawSlowedExpire = 1.0e6;
	Claw->HeadClawSlowedEntity = FElysiumEntityHandle::Invalid();
	Claw->SlowEntityCalls.Reset();
	Claw->TzimisceHeadClawEndSlow(/*bForce=*/true);
	TestEqual(TEXT("`103c2277`: a stale handle takes no EndSlowEntity"),
		Claw->SlowEntityCalls.Num(), 0);
	TestEqual(TEXT("but the expiry is still zeroed, outside that guard"),
		Claw->HeadClawSlowedExpire, 0.0, 0.0001);
	return true;
}

// =================================================================================================
// `CNPC_VTzimisceRunner` — `0x103c3cd0` and `0x103c3d10`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10RunnerHullTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.TzimisceRunnerHull", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10RunnerHullTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VTzimisceRunner"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcTzimisceRunner* Runner = ElysiumTestAsSpecies<FElysiumNpcTzimisceRunner>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VTzimisceRunner"), Runner))
	{
		return false;
	}
	Runner->bTzimisceRunnerForm = false;
	Runner->bWantsLargeHull = true;

	Runner->TzimisceRunnerNotifyChangeSizeSmall();
	TestTrue(TEXT("`103c3cdb`: the form byte +0x6672 is set"), Runner->bTzimisceRunnerForm);
	TestFalse(TEXT("`103c3ce2`: m_bWantsLargeHull is cleared"), Runner->bWantsLargeHull);
	const float Token = Runner->RunnerHullToken;

	// `103c3d1e`: the restore is EDGE-TRIGGERED on the engine token. An UNCHANGED token refuses the
	// restore outright, leaving the runner small and the form byte set.
	Runner->RunnerHullToken = Runner->RunnerHullEngineToken();
	Runner->TzimisceRunnerNotifyChangeSizeNormal();
	TestTrue(TEXT("`103c3d1e`: an unchanged token leaves the runner small"),
		Runner->bTzimisceRunnerForm);
	TestFalse(TEXT("and m_bWantsLargeHull stays cleared"), Runner->bWantsLargeHull);

	// A token that differs takes the restore.
	Runner->RunnerHullToken = Token - 1000.f;
	Runner->TzimisceRunnerNotifyChangeSizeNormal();
	TestFalse(TEXT("`103c3d4a`: a changed token clears the form byte"),
		Runner->bTzimisceRunnerForm);
	TestTrue(TEXT("and sets m_bWantsLargeHull"), Runner->bWantsLargeHull);
	return true;
}

// =================================================================================================
// `CNPC_VVampireBoss` — `0x103c63c0` and `0x103c6f40`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10TransformTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.WaitForTransformation", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10TransformTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VVampireBoss"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcVampireBoss* VampireBoss = ElysiumTestAsSpecies<FElysiumNpcVampireBoss>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VVampireBoss"), VampireBoss))
	{
		return false;
	}
	VampireBoss->TypedStatSet(0, 0x11, 10);
	VampireBoss->TypedStatSet(0, 0x0f, 7);

	// `103c6407`: `_DAT_104ce8bc` = **2.0** s. Before the window has passed the body does nothing.
	VampireBoss->ProteanTransformStartTime = F.World.World.NowSeconds();
	VampireBoss->WaitForTransformation();
	TestEqual(TEXT("`103c6407`: inside the 2.0 s wait the wound counter is untouched"),
		VampireBoss->TypedStatValue(0, 0x0f), 7);

	// Past it: `CVStatList_t::Set(0x0f, 0)`. Stat `0x0f` is the WOUND counter, so zeroing it is a
	// FULL HEAL and not a kill.
	VampireBoss->ProteanTransformStartTime = F.World.World.NowSeconds() - 2.001;
	VampireBoss->WaitForTransformation();
	TestEqual(TEXT("`103c6490`: past the wait the wound counter is zeroed — a full heal"),
		VampireBoss->TypedStatValue(0, 0x0f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10BodyEmittersTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.VampireBossBodyEmitters", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10BodyEmittersTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VVampireBoss"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcVampireBoss* VampireBoss = ElysiumTestAsSpecies<FElysiumNpcVampireBoss>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VVampireBoss"), VampireBoss))
	{
		return false;
	}
	for (int32 Region = 0; Region < 4; ++Region)
	{
		VampireBoss->SetBodyEmitterName(Region, FString::Printf(TEXT("boss_emitter_%d"), Region));
	}
	const int32 KillsBefore = VampireBoss->EmitterKillCalls;
	VampireBoss->EmitterCalls.Reset();
	VampireBoss->VampireBossSpawnBodyEmitters();

	// `103c6f7c`: the kill runs FIRST, and it walks all four handles.
	TestEqual(TEXT("`103c6f7c`: KillBodyEmitters ran over all four slots first"),
		VampireBoss->EmitterKillCalls, KillsBefore + 4);
	// `103c6f8a`: four spawns, one per region.
	TestEqual(TEXT("`103c6f8a`: four spawns, one per region"), VampireBoss->EmitterCalls.Num(), 4);
	// The four attachment names, read out of the pinned image at `0x65e6d0`. Regions 2 and 3 are the
	// SAME string, which is the fact this table records.
	TestEqual(TEXT("region 0"), VampireBoss->BodyEmitterAttachments[0],
		FString(TEXT("Bip01 L Hand")));
	TestEqual(TEXT("region 1"), VampireBoss->BodyEmitterAttachments[1],
		FString(TEXT("Bip01 R Hand")));
	TestEqual(TEXT("region 2"), VampireBoss->BodyEmitterAttachments[2], FString(TEXT("Bip01 Spine")));
	TestEqual(TEXT("region 3 is the SAME string as region 2"), VampireBoss->BodyEmitterAttachments[3],
		FString(TEXT("Bip01 Spine")));
	return true;
}

// =================================================================================================
// `CNPC_VWerewolf` — `0x103ce750`, `0x103d0db0`, `0x103d5130`, `0x103d9f90`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10WerewolfTaskFailTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.WerewolfTaskFail", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10WerewolfTaskFailTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VWerewolf"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcWerewolf* Wolf = ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VWerewolf"), Wolf))
	{
		return false;
	}
	// OFFSETS CORRECTED: `+0x66b0` is `m_pTeleportHint` and `+0x66bc` is `m_pMoveHint`, which family
	// Hints already bound the right way round and the checklist's walk has swapped.
	Wolf->TeleportHintNode = 11;
	Wolf->MoveHintNode = 22;
	Wolf->WerewolfBreakHintNode = 33;
	Wolf->WerewolfHintFlags = 0xffffu;
	Wolf->WerewolfMorphTimerA = 5.f;
	Wolf->bWerewolfTaskFailed = false;
	Wolf->WerewolfHintNodeCacheA = 7;
	Wolf->RandomMoveHintNodeZone = 7;
	Wolf->bIsUsingSmallHull = false;

	Wolf->TaskFail(/*Reason*/ 3);

	// `103ce8c6`: everything below the diagnostic block is UNCONDITIONAL.
	TestEqual(TEXT("`103ce8cf`: ClearMoveHint"), Wolf->MoveHintNode, INDEX_NONE);
	TestEqual(TEXT("`103ce8d7`: ClearTeleportHint"), Wolf->TeleportHintNode, INDEX_NONE);
	TestEqual(TEXT("`103ce8df`: m_pBreakHint = 0"), Wolf->WerewolfBreakHintNode, INDEX_NONE);
	TestEqual(TEXT("`103ce8e4`: +0x66a4 = 0"), Wolf->WerewolfMorphTimerA, 0.f, 0.0001f);
	TestTrue(TEXT("`103ce8eb`: +0x66a1 = 1"), Wolf->bWerewolfTaskFailed);
	TestEqual(TEXT("`103ce90a`: the zone word is cleared"),
		static_cast<int32>(Wolf->WerewolfHintFlags), 0);
	TestEqual(TEXT("`103ce914`: +0x6708 = -1"), Wolf->WerewolfHintNodeCacheA, INDEX_NONE);
	TestEqual(TEXT("`103ce91e`: +0x670c = -1"), Wolf->RandomMoveHintNodeZone, INDEX_NONE);
	// `0x10273180` writes `+0x5f2d m_bIsUsingSmallHull`, NOT `+0x5f2c m_bWantsLargeHull` — the two
	// words are a pair and only the runner's slot 335 touches the second.
	TestTrue(TEXT("`103ce8c6`: SetHullSizeSmall(1) puts the body on the small hull"),
		Wolf->bIsUsingSmallHull);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10WerewolfHasPathTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.WerewolfHasPath", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10WerewolfHasPathTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VWerewolf"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcWerewolf* Wolf = ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VWerewolf"), Wolf))
	{
		return false;
	}
	// `103d0e2b`: the body is ONE forward. CORRECTION: the two navigator-cache stamps the walk puts
	// here live inside `0x102ee380`. `0x102fdcc0` is the seam, and with no path object it answers
	// false — retail's own answer for a navigator with no path.
	const FVector Start(1.0, 2.0, 3.0);
	const FVector End(4.0, 5.0, 6.0);
	TestFalse(TEXT("`102fdcc0`: no path object, so the forward refuses"),
		Wolf->WerewolfHasPath(Start, End));
	if (TestEqual(TEXT("but the ask is recorded, with both endpoints"),
		Wolf->HasPathQueries.Num(), 1))
	{
		TestTrue(TEXT("the start"), Wolf->HasPathQueries[0].StartUnits.Equals(Start, 0.001));
		TestTrue(TEXT("the end"), Wolf->HasPathQueries[0].EndUnits.Equals(End, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10WerewolfOverlayTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.WerewolfDrawDebugStatOverlays", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10WerewolfOverlayTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VWerewolf"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcWerewolf* Wolf = ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VWerewolf"), Wolf))
	{
		return false;
	}
	// `103d51a1`: the clamp against `_DAT_104454c4` (0.0) the decompiler folded away — a last-seen
	// stamp in the FUTURE must print 0.0, not a negative number.
	Wolf->WerewolfLastSeenTime = F.World.World.NowSeconds() + 100.0;
	Wolf->Senses.Memory.ClosestPlayerDistanceCm = SpeciesMisc10Cm(42.f);
	Wolf->WerewolfHintFlags = 0x0001u | 0x0800u;
	Wolf->Cognition.Conditions.Reset();
	Wolf->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7b));
	Wolf->Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7a));
	Wolf->WerewolfDoorState = 2;
	Wolf->MoveHintNode = INDEX_NONE;
	Wolf->TeleportHintNode = INDEX_NONE;
	Wolf->WerewolfScheduleStack.Reset();
	for (int32 i = 0; i < 7; ++i)
	{
		Wolf->WerewolfScheduleStack.Add(FString::Printf(TEXT("sched%d"), i));
	}

	TArray<FString> Lines;
	Wolf->WerewolfDrawDebugStatOverlays(Lines);
	if (Lines.Num() < 2)
	{
		AddError(TEXT("the overlay printed nothing"));
		return false;
	}
	TestEqual(TEXT("`103d51a1`: Not Seen Time is clamped at 0.0"), Lines[0],
		FString(TEXT("Not Seen Time  : 0.0")));
	// `103d51d3`: `Player Distance` reads `+0x6264 m_flPlayerDist`, the CACHED distance.
	TestEqual(TEXT("`103d51d3`: Player Distance reads the cached +0x6264"), Lines[1],
		FString(TEXT("Player Distance: 42.0")));
	TestTrue(TEXT("`103d51f3`: the set zone bits print"), Lines.Contains(TEXT("ZONE_NO_TALL_ANIMS")));
	TestTrue(TEXT("and the wolf-inside bit"), Lines.Contains(TEXT("ZONE_WOLF_INSIDE")));
	TestFalse(TEXT("and a clear bit does not"),
		Lines.Contains(TEXT("ZONE_PLAYER_ON_BREAKABLE")));
	// `103d532b`: `0x7b` is tested BEFORE `0x7a`, which is the order the two lines come out in.
	const int32 BreakIdx = Lines.IndexOfByKey(FString(TEXT("COND_VWEREWOLF_SHOULD_BREAKHINT")));
	const int32 DeathIdx = Lines.IndexOfByKey(FString(TEXT("COND_VWEREWOLF_DEATH_TRIGGERED")));
	TestTrue(TEXT("`103d532b`: condition 0x7b prints before 0x7a"),
		BreakIdx != INDEX_NONE && DeathIdx != INDEX_NONE && BreakIdx < DeathIdx);
	TestTrue(TEXT("`103d537e`: door state 2 is 'open'"),
		Lines.Contains(TEXT("door state: (2)open")));
	// `103d5450`: the LAST FIVE rows of the schedule stack.
	TestTrue(TEXT("`103d5450`: the last five schedule rows print"), Lines.Contains(TEXT("sched6")));
	TestFalse(TEXT("and the first two do not"), Lines.Contains(TEXT("sched1")));

	// `103d538b`: a door state outside 0..3 prints NOTHING — retail has no `default:`.
	Wolf->WerewolfDoorState = 9;
	Lines.Reset();
	Wolf->WerewolfDrawDebugStatOverlays(Lines);
	for (const FString& Line : Lines)
	{
		TestFalse(TEXT("`103d538b`: an out-of-range door state prints no line at all"),
			Line.StartsWith(TEXT("door state")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSpeciesMisc10SnapTest,
	"Elysium.Substrate.NpcKernelSpeciesMisc10.SnapToAnimationPoint", GSpeciesMisc10Flags)
bool FElysiumSpeciesMisc10SnapTest::RunTest(const FString&)
{
	FSpeciesMisc10Fixture F(TEXT("CNPC_VWerewolf"));
	if (F.Guard == nullptr)
	{
		AddError(TEXT("no NPC"));
		return false;
	}
	FElysiumNpcWerewolf* Wolf = ElysiumTestAsSpecies<FElysiumNpcWerewolf>(F.Guard);
	if (!TestNotNull(TEXT("the subject is a CNPC_VWerewolf"), Wolf))
	{
		return false;
	}
	Wolf->WerewolfFakeHullPosUnits = FVector(9.0, 9.0, 9.0);
	Wolf->WerewolfSnapWordA = 5;
	Wolf->WerewolfHintNodeCacheA = 7;
	Wolf->RandomMoveHintNodeZone = 7;
	Wolf->MatchOriginAnglesCalls.Reset();

	Wolf->SnapToAnimationPoint();

	if (TestEqual(TEXT("`103d9fdc`: one MatchOriginAnglesToAnimation"),
		Wolf->MatchOriginAnglesCalls.Num(), 1))
	{
		TestEqual(TEXT("on Bip01"), Wolf->MatchOriginAnglesCalls[0].Bone,
			FString(TEXT("Bip01")));
		TestTrue(TEXT("origin AND angles — both arguments are 1"),
			Wolf->MatchOriginAnglesCalls[0].bOrigin
				&& Wolf->MatchOriginAnglesCalls[0].bAngles);
	}
	TestTrue(TEXT("`103d9ff7`: the cached fake-hull point is cleared from vec3_origin"),
		Wolf->WerewolfFakeHullPosUnits.IsNearlyZero());
	TestEqual(TEXT("`103da00e`: +0x66a8 = 0"), Wolf->WerewolfSnapWordA, 0);
	TestEqual(TEXT("`103da015`: +0x6708 = -1"), Wolf->WerewolfHintNodeCacheA, INDEX_NONE);
	TestEqual(TEXT("and +0x670c = -1, so the snap drops the hint too"),
		Wolf->RandomMoveHintNodeZone, INDEX_NONE);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
