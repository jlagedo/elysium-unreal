#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29c-1, family **Damage**. Every assertion below is read off the decompiled C — or, for
// `0x100b4ea0`'s tail call, `0x102664c0`'s vector algebra and `0x10266780`'s stack-shifted
// arguments, off the LISTING, which is where those three bodies actually are — of the body it
// names: the threshold, the arm order, the id, what is written. Never off 29c's one-line walk.
//
// Most of this family's species are not spawnable: of the fourteen retail classes its rows touch,
// only `CNPC_VAndreiBlood` and `CNPC_VSabbatLeader` have a registered classname in
// `Substrate/ElysiumNpcClasses.cpp`. Every other species row is exercised through its table's own
// lookup BY RETAIL CLASS NAME, exactly as the brief prescribes, and the two that are spawnable are
// exercised through a live NPC as well.
//
// Every seam that can only answer nothing gets a case saying the seam is asked and that the refusal
// is the recovered one.

static constexpr EAutomationTestFlags GDamageTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	constexpr float U = ElysiumMove::U;

	FElysiumNpc::FElysiumTraceHit MakeTrace(int32 HitGroup, const FVector& EndPosUnits)
	{
		FElysiumNpc::FElysiumTraceHit Trace;
		Trace.HitGroup = HitGroup;
		Trace.EndPosUnits = EndPosUnits;
		return Trace;
	}

	FElysiumDmg MakeResolvedDmg(int32 Applied, uint32 Mask)
	{
		FElysiumDmg Dmg;
		Dmg.Family = EElysiumDmgFamily::Lethal;
		Dmg.BaseDamage = Applied;
		Dmg.AppliedDamage = Applied;
		Dmg.DmgMask = Mask;
		Dmg.bResolved = true;
		return Dmg;
	}
}

// -------------------------------------------------------------------------------------------------
// Slots 576/577 — `0x10266630` / `0x10266660`, the two damage-magnitude predicates.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageMagnitudeTest,
	"Elysium.Substrate.NpcKernelDamage.LightAndHeavy", GDamageTestFlags)
bool FElysiumNpcKernelDamageMagnitudeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_magnitude"), 0x29c1d001);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// `0x10266630`: `return _DAT_104454c4 < damage`, and that cell is 0.0f. STRICTLY greater.
	TestFalse(TEXT("zero damage is not light damage"), Npc->IsLightDamage(0.f, 0));
	TestFalse(TEXT("negative damage is not light damage"), Npc->IsLightDamage(-1.f, 0));
	TestTrue(TEXT("any positive damage is light damage"), Npc->IsLightDamage(0.001f, 0));
	TestTrue(TEXT("a big hit is also light damage"), Npc->IsLightDamage(500.f, 0));

	// `0x10266660`: `return _DAT_1044eb0c < damage`, and that cell is 20.0f. STRICTLY greater, so
	// exactly 20 is NOT heavy. SDK 2013's own IsHeavyDamage returns a flat false; the 20.0 threshold
	// is this fork's.
	TestFalse(TEXT("20 is not heavy damage"), Npc->IsHeavyDamage(20.f, 0));
	TestFalse(TEXT("19.999 is not heavy damage"), Npc->IsHeavyDamage(19.999f, 0));
	TestTrue(TEXT("20.001 is heavy damage"), Npc->IsHeavyDamage(20.001f, 0));

	// Neither body reads its second argument.
	TestEqual(TEXT("the damage-type argument changes nothing for light"),
		Npc->IsLightDamage(5.f, 0), Npc->IsLightDamage(5.f, 0x7fffffff));
	TestEqual(TEXT("the damage-type argument changes nothing for heavy"),
		Npc->IsHeavyDamage(50.f, 0), Npc->IsHeavyDamage(50.f, 0x7fffffff));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 615 — `0x102ad0c0`, plus `CNPC_VGhoulCroucher`'s `0x1037c420`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageCanBeSetOnFireTest,
	"Elysium.Substrate.NpcKernelDamage.CanBeSetOnFire", GDamageTestFlags)
bool FElysiumNpcKernelDamageCanBeSetOnFireTest::RunTest(const FString&)
{
	// The species table: one row, and it is the one `vtmb_slot 615` names.
	int32 Count = 0;
	const FElysiumNpc::FCanBeSetOnFireSpecies* Rows = FElysiumNpc::CanBeSetOnFireSpeciesRows(Count);
	TestEqual(TEXT("slot 615 has exactly one species override"), Count, 1);
	TestEqual(TEXT("and it is CNPC_VGhoulCroucher"), FString(Rows[0].RetailClass),
		FString(TEXT("CNPC_VGhoulCroucher")));
	TestEqual(TEXT("filled by 0x1037c420"), FString(Rows[0].Body), FString(TEXT("0x1037c420")));
	TestTrue(TEXT("and it refuses while m_bSpawnBurning is set"),
		Rows[0].bRefusesWhileSpawnBurning);
	// `npc_VGhoulCroucher` is not a registered spawn leaf, so the row is reached by retail name.
	TestNotNull(TEXT("the row is reachable by retail class name"),
		FElysiumNpc::CanBeSetOnFireSpeciesOf(TEXT("CNPC_VGhoulCroucher")));
	TestNull(TEXT("and an unrelated class has no row"),
		FElysiumNpc::CanBeSetOnFireSpeciesOf(TEXT("CNPC_VHumanCombatant")));

	FElysiumNpcWorldBuilder Builder(TEXT("damage_fire"), 0x29c1d002);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	const double Now = Fixture.World.NowSeconds();

	// Arm 1: `HasCondition(0x30)` — COND_ON_FIRE — refuses, whatever the timer says.
	Npc->NextBurnTime = Now - 100.0;
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::OnFire);
	TestFalse(TEXT("a body already on fire cannot be set on fire"), Npc->CanBeSetOnFire());
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::OnFire);

	// Arm 2: `m_flNextBurnTime (+0x65bc) < curtime`, STRICTLY.
	TestTrue(TEXT("an expired burn timer admits fire"), Npc->CanBeSetOnFire());
	Npc->NextBurnTime = Now + 5.0;
	TestFalse(TEXT("a live burn timer refuses"), Npc->CanBeSetOnFire());
	Npc->NextBurnTime = Now;
	TestFalse(TEXT("a timer exactly at curtime refuses: the compare is strict"),
		Npc->CanBeSetOnFire());

	// The species arm, on the one runtime word it reads. This NPC's class carries no row, so the
	// flag alone must not change the answer — the gate is the ROW, not the word.
	Npc->NextBurnTime = Now - 100.0;
	Npc->bGhoulSpawnBurning = true;
	TestTrue(TEXT("m_bSpawnBurning alone does not refuse on a class with no slot-615 row"),
		Npc->CanBeSetOnFire());
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 154 — `0x100b4ea0`, `DamageDecal`. Read off the listing.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageDecalTest,
	"Elysium.Substrate.NpcKernelDamage.DamageDecal", GDamageTestFlags)
bool FElysiumNpcKernelDamageDecalTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_decal"), 0x29c1d003);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// Arm 1: `m_nRenderMode == 4` (kRenderTransAlpha) answers -1 — and it is tested FIRST, so the
	// glass arm cannot reach it.
	Npc->RenderMode = 4;
	TestEqual(TEXT("kRenderTransAlpha answers -1"), Npc->DamageDecal(0, 0x47), INDEX_NONE);
	TestEqual(TEXT("and -1 whatever the material"), Npc->DamageDecal(0xffff, 0), INDEX_NONE);

	// Arm 2: any other NON-ZERO render mode, on game material `0x47` ('G', glass), answers 0x34.
	Npc->RenderMode = 1;
	TestEqual(TEXT("a non-normal render mode on glass answers 0x34"),
		Npc->DamageDecal(0, 0x47), 0x34);
	Npc->RenderMode = 5;
	TestEqual(TEXT("and any other non-normal mode does too"), Npc->DamageDecal(0, 0x47), 0x34);

	// The glass arm requires BOTH: render mode 0 with glass falls through, and a non-zero mode on
	// another material falls through too.
	Npc->RenderMode = 0;
	for (int32 i = 0; i < 32; ++i)
	{
		const int32 Answer = Npc->DamageDecal(0, 0x47);
		TestTrue(TEXT("kRenderNormal on glass draws a pool index in [0,4]"),
			Answer >= 0 && Answer <= 4);
	}
	Npc->RenderMode = 1;
	for (int32 i = 0; i < 32; ++i)
	{
		const int32 Answer = Npc->DamageDecal(0, 0x41);
		TestTrue(TEXT("a non-glass material draws a pool index in [0,4]"),
			Answer >= 0 && Answer <= 4);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 16 — `0x1009b030`, `GetAttackExtents`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageAttackExtentsTest,
	"Elysium.Substrate.NpcKernelDamage.AttackExtents", GDamageTestFlags)
bool FElysiumNpcKernelDamageAttackExtentsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_extents"), 0x29c1d004);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	// `CAI_BaseNPCTroika::NPCInit` `0x1029a0b0` writes `m_vecSavedAttackExtents = (-1, -1, -1)`
	// in SOURCE units (`1029a5xx`). Activate runs that body; the getter answers centimetres.
	TestEqual(TEXT("1029a0b0 NPCInit seeds saved attack extents (-1,-1,-1)"),
		Npc->GetAttackExtents(), FVector(-1.0, -1.0, -1.0) * ElysiumMove::U);
	Npc->SetAttackExtents(FVector(3.0, 5.0, 7.0));
	TestEqual(TEXT("the getter answers exactly what the setter wrote"),
		Npc->GetAttackExtents(), FVector(3.0, 5.0, 7.0));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 141 — `0x10266780`, `TraceAttack`, and its three species prologues.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageTraceAttackTest,
	"Elysium.Substrate.NpcKernelDamage.TraceAttack", GDamageTestFlags)
bool FElysiumNpcKernelDamageTraceAttackTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_traceattack"), 0x29c1d005);
	Builder.AddNpc(TEXT("npc"));
	Builder.AddNpc(TEXT("attacker"), FVector(200.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Attacker = Fixture.Npc(TEXT("attacker"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Attacker });
	if (Npc == nullptr || Attacker == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// The port's one stated divergence: a null packet or a null trace refuses instead of
	// dereferencing. Assert it so the refusal is the recorded one.
	Npc->TraceAttack(nullptr, FVector::ForwardVector, nullptr);
	TestEqual(TEXT("a null packet accumulates nothing"), Npc->MultiDamageAccumulator.Num(), 0);

	// `m_fNoDamageDecal = false` runs BEFORE the `m_takedamage` test, so a body that takes no damage
	// still has the flag cleared and nothing else written.
	FElysiumNpc::FElysiumTakeDamageInfo Info;
	Info.Damage = 50.f;
	Info.Attacker = Attacker->Handle;
	FElysiumNpc::FElysiumTraceHit Trace = MakeTrace(2, FVector(10.0, 0.0, 30.0));
	Trace.PhysicsBone = 9;
	Npc->bNoDamageDecal = true;
	Npc->TakeDamageMode = 0;
	Npc->LastHitGroup = -7;
	Npc->TraceAttack(&Info, FVector::ForwardVector, &Trace);
	TestFalse(TEXT("m_fNoDamageDecal is cleared even on the m_takedamage refusal"),
		Npc->bNoDamageDecal);
	TestEqual(TEXT("and m_LastHitGroup is untouched on that arm"), Npc->LastHitGroup, -7);
	TestEqual(TEXT("and nothing is accumulated"), Npc->MultiDamageAccumulator.Num(), 0);

	// The ordinary path. `m_LastHitGroup` and `m_nForceBone` are written before the switch, and the
	// bone is sign-extended from a `short`.
	Npc->TakeDamageMode = 2;
	Trace.PhysicsBone = 0xffff8000;   // -32768 as a short
	Npc->TraceAttack(&Info, FVector::ForwardVector, &Trace);
	TestEqual(TEXT("m_LastHitGroup takes the trace's hitgroup"), Npc->LastHitGroup, 2);
	TestEqual(TEXT("m_nForceBone is sign-extended from a short"), Npc->ForceBone, -32768);
	TestEqual(TEXT("the sub-packet reaches AddMultiDamage"), Npc->MultiDamageAccumulator.Num(), 1);

	// The GEAR arm: hitgroup 10 takes the flat `0.01` constant AND rewrites the trace's hitgroup to
	// 0. The rewrite is observable on the caller's own trace record.
	Npc->MultiDamageAccumulator.Reset();
	Npc->SpawnBloodCalls.Reset();
	FElysiumNpc::FElysiumTraceHit Gear = MakeTrace(10, FVector::ZeroVector);
	Npc->TraceAttack(&Info, FVector::ForwardVector, &Gear);
	TestEqual(TEXT("a gear hit rewrites the trace hitgroup to generic"), Gear.HitGroup, 0);
	TestEqual(TEXT("and m_LastHitGroup kept the ORIGINAL 10, written before the switch"),
		Npc->LastHitGroup, 10);
	TestEqual(TEXT("0.01 is under the 1.0 bleed floor, so no blood is spawned"),
		Npc->SpawnBloodCalls.Num(), 0);

	// The `>= 1.0` floor. Retail seeds the tested value from `CVDmg_t::Apply`'s own answer, so a
	// packet carrying NO descriptor never reaches 1.0 on a generic hitgroup — there is nothing to
	// apply and nothing to scale.
	Npc->SpawnBloodCalls.Reset();
	Npc->bNoDamageDecal = false;
	FElysiumNpc::FElysiumTakeDamageInfo NoDescriptor;
	NoDescriptor.Damage = 500.f;   // `m_flDamage` is NOT what this body tests
	NoDescriptor.Attacker = Attacker->Handle;
	FElysiumNpc::FElysiumTraceHit Generic = MakeTrace(0, FVector(1.0, 2.0, 3.0));
	Npc->TraceAttack(&NoDescriptor, FVector::ForwardVector, &Generic);
	TestEqual(TEXT("a hit under 1.0 spawns no blood"), Npc->SpawnBloodCalls.Num(), 0);
	TestFalse(TEXT("and it does NOT raise m_fNoDamageDecal: retail raises that flag only on the "
					"survived-headshot arm"),
		Npc->bNoDamageDecal);

	// `DMG_SHOCK` (0x100) suppresses the blood and the bleed even at full magnitude.
	Npc->SpawnBloodCalls.Reset();
	FElysiumDmg Shock = MakeResolvedDmg(30, 0x100u);
	FElysiumNpc::FElysiumTakeDamageInfo WithShock;
	WithShock.Dmg = &Shock;
	WithShock.Attacker = Attacker->Handle;
	Npc->TraceAttack(&WithShock, FVector::ForwardVector, &Generic);
	TestEqual(TEXT("DMG_SHOCK spawns no blood"), Npc->SpawnBloodCalls.Num(), 0);

	// The survived-headshot arm: hitgroup 1 with `m_iHealth - damage > 0` raises
	// `m_fNoDamageDecal` and skips the blood AND the bleed.
	Npc->SpawnBloodCalls.Reset();
	Npc->bNoDamageDecal = false;
	Npc->Health = 100;
	FElysiumDmg Head = MakeResolvedDmg(10, 0);
	FElysiumNpc::FElysiumTakeDamageInfo WithHead;
	WithHead.Dmg = &Head;
	WithHead.Attacker = Attacker->Handle;
	FElysiumNpc::FElysiumTraceHit HeadTrace = MakeTrace(1, FVector(4.0, 5.0, 6.0));
	Npc->TraceAttack(&WithHead, FVector::ForwardVector, &HeadTrace);
	TestTrue(TEXT("a survived headshot raises m_fNoDamageDecal"), Npc->bNoDamageDecal);
	TestEqual(TEXT("and spawns no blood"), Npc->SpawnBloodCalls.Num(), 0);

	// A LETHAL headshot takes the other arm: no flag, and the blood/bleed run.
	Npc->SpawnBloodCalls.Reset();
	Npc->bNoDamageDecal = false;
	Npc->Health = 5;
	Npc->TraceAttack(&WithHead, FVector::ForwardVector, &HeadTrace);
	TestFalse(TEXT("a lethal headshot leaves m_fNoDamageDecal clear"), Npc->bNoDamageDecal);
	TestEqual(TEXT("and spawns blood once"), Npc->SpawnBloodCalls.Num(), 1);
	TestEqual(TEXT("at the trace's endpos, in source units"),
		Npc->SpawnBloodCalls[0].PositionUnits, FVector(4.0, 5.0, 6.0));

	// The tail: the ammo type from `trace_t+0x50` and the attacker are re-copied into the sub-packet
	// before `AddMultiDamage`.
	Npc->MultiDamageAccumulator.Reset();
	HeadTrace.AmmoType = 17;
	Npc->TraceAttack(&WithHead, FVector::ForwardVector, &HeadTrace);
	if (Npc->MultiDamageAccumulator.Num() == 1)
	{
		TestEqual(TEXT("the sub-packet carries the trace's ammo type"),
			Npc->MultiDamageAccumulator[0].AmmoType, 17);
		TestTrue(TEXT("and the original attacker"),
			Npc->MultiDamageAccumulator[0].Attacker == Attacker->Handle);
	}
	else
	{
		AddError(TEXT("AddMultiDamage did not run exactly once"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageTraceAttackSpeciesTest,
	"Elysium.Substrate.NpcKernelDamage.TraceAttackSpecies", GDamageTestFlags)
bool FElysiumNpcKernelDamageTraceAttackSpeciesTest::RunTest(const FString&)
{
	// The table: three prologues, and `vtmb_slot 141` names exactly these three classes beside the
	// Troika line's 74 inheritors.
	int32 Count = 0;
	const FElysiumNpc::FTraceAttackSpecies* Rows = FElysiumNpc::TraceAttackSpeciesRows(Count);
	TestEqual(TEXT("slot 141 has three species prologues"), Count, 3);
	TestEqual(TEXT("row 0 is CNPC_Bullseye"), FString(Rows[0].RetailClass),
		FString(TEXT("CNPC_Bullseye")));
	TestEqual(TEXT("filled by 0x10356f60"), FString(Rows[0].Body), FString(TEXT("0x10356f60")));
	TestEqual(TEXT("row 1 is CNPC_VWerewolf"), FString(Rows[1].RetailClass),
		FString(TEXT("CNPC_VWerewolf")));
	TestEqual(TEXT("filled by 0x103ccbf0"), FString(Rows[1].Body), FString(TEXT("0x103ccbf0")));
	TestEqual(TEXT("row 2 is CNPC_VZombie"), FString(Rows[2].RetailClass),
		FString(TEXT("CNPC_VZombie")));
	TestEqual(TEXT("filled by 0x103e0430"), FString(Rows[2].Body), FString(TEXT("0x103e0430")));

	// None of the three is a registered spawn leaf, so each is reached by retail class name.
	TestNotNull(TEXT("CNPC_Bullseye is reachable by name"),
		FElysiumNpc::TraceAttackSpeciesOf(TEXT("CNPC_Bullseye")));
	TestNotNull(TEXT("CNPC_VWerewolf is reachable by name"),
		FElysiumNpc::TraceAttackSpeciesOf(TEXT("CNPC_VWerewolf")));
	TestNotNull(TEXT("CNPC_VZombie is reachable by name"),
		FElysiumNpc::TraceAttackSpeciesOf(TEXT("CNPC_VZombie")));
	TestNull(TEXT("and the Troika line has no row"),
		FElysiumNpc::TraceAttackSpeciesOf(TEXT("CNPC_VHumanCombatant")));

	// `0x103e0430`'s pure rule, arm by arm off the decompiled C.
	bool bGib = false;
	int32 Forced = 0;
	TestTrue(TEXT("a head hit forces an ammo type"),
		FElysiumNpc::ZombieTraceAttackPrologue(1, /*melee*/ false, 11, 22, bGib, Forced));
	TestTrue(TEXT("and raises the gib latch"), bGib);
	TestEqual(TEXT("with the SECOND cvar's value"), Forced, 22);

	Forced = 0;
	TestFalse(TEXT("a body hit from a non-melee weapon forces nothing"),
		FElysiumNpc::ZombieTraceAttackPrologue(2, /*melee*/ false, 11, 22, bGib, Forced));
	TestFalse(TEXT("and clears the gib latch"), bGib);
	TestEqual(TEXT("leaving the forced type untouched"), Forced, 0);

	TestTrue(TEXT("a body hit from a melee weapon forces one"),
		FElysiumNpc::ZombieTraceAttackPrologue(7, /*melee*/ true, 11, 22, bGib, Forced));
	TestFalse(TEXT("still with no gib"), bGib);
	TestEqual(TEXT("with the FIRST cvar's value — the swap is inside the capability test"),
		Forced, 11);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 146 — `0x10268ef0`, `TraceBleed`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageTraceBleedTest,
	"Elysium.Substrate.NpcKernelDamage.TraceBleed", GDamageTestFlags)
bool FElysiumNpcKernelDamageTraceBleedTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_bleed"), 0x29c1d006);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	FElysiumNpc::FElysiumTraceHit Trace = MakeTrace(0, FVector(0.0, 0.0, 40.0));

	// The three refusals. A refused pass records nothing, because the record is written past the
	// loop — which is exactly where retail's `return` arms leave it.
	Npc->TraceBleedPasses.Reset();

	// Refusal 1: a null descriptor.
	Npc->TraceBleed(nullptr, FVector::ForwardVector, &Trace);
	TestEqual(TEXT("a null descriptor bleeds nothing"), Npc->TraceBleedPasses.Num(), 0);

	// Refusal 2: word 1 exactly zero. Note it is the AUTHORED base damage, not the applied result.
	FElysiumDmg Zero = MakeResolvedDmg(0, 0x2u);
	Zero.BaseDamage = 0;
	Npc->TraceBleed(&Zero, FVector::ForwardVector, &Trace);
	TestEqual(TEXT("zero base damage bleeds nothing"), Npc->TraceBleedPasses.Num(), 0);

	// Refusal 3: the `0xc7` low-byte mask. `DMG_BUCKSHOT` (0x04000000) is above the byte and so
	// does NOT admit the bleed on its own — that is the recovered mask, not a transcription.
	FElysiumDmg Buckshot = MakeResolvedDmg(30, 0x04000000u);
	Buckshot.BaseDamage = 30;
	Npc->TraceBleed(&Buckshot, FVector::ForwardVector, &Trace);
	TestEqual(TEXT("a mask above the low byte bleeds nothing"), Npc->TraceBleedPasses.Num(), 0);
	// And each of the five bits that ARE in `0xc7` admits it on its own.
	for (const uint32 Bit : { 0x1u, 0x2u, 0x4u, 0x40u, 0x80u })
	{
		FElysiumDmg One = MakeResolvedDmg(30, Bit);
		One.BaseDamage = 30;
		Npc->TraceBleedPasses.Reset();
		Npc->TraceBleed(&One, FVector::ForwardVector, &Trace);
		TestEqual(TEXT("a bit inside 0xc7 admits the bleed"), Npc->TraceBleedPasses.Num(), 1);
	}

	// The noise/count table: `< 10` -> 0.1 and one trace, `< 25` -> 0.2 and two, else 0.3 and four.
	const auto BandFor = [&](int32 BaseDamage) -> FElysiumNpc::FTraceBleedPass
	{
		FElysiumDmg Dmg = MakeResolvedDmg(BaseDamage, 0x2u);
		Dmg.BaseDamage = BaseDamage;
		Npc->TraceBleedPasses.Reset();
		Npc->TraceBleed(&Dmg, FVector::ForwardVector, &Trace);
		return Npc->TraceBleedPasses.Num() == 1 ? Npc->TraceBleedPasses[0]
											    : FElysiumNpc::FTraceBleedPass();
	};
	TestEqual(TEXT("damage under 10 runs one trace"), BandFor(9).TraceCount, 1);
	TestEqual(TEXT("at noise 0.1"), BandFor(9).Noise, 0.1f, 0.0001f);
	// The band edges are inclusive on the LOWER side: exactly 10 is the two-trace band and exactly
	// 25 the four-trace one.
	TestEqual(TEXT("exactly 10 is the two-trace band"), BandFor(10).TraceCount, 2);
	TestEqual(TEXT("at noise 0.2"), BandFor(10).Noise, 0.2f, 0.0001f);
	TestEqual(TEXT("24 is still the two-trace band"), BandFor(24).TraceCount, 2);
	TestEqual(TEXT("exactly 25 is the four-trace band"), BandFor(25).TraceCount, 4);
	TestEqual(TEXT("at noise 0.3"), BandFor(25).Noise, 0.3f, 0.0001f);
	TestEqual(TEXT("and anything above it too"), BandFor(200).TraceCount, 4);

	// The trace segment: `endpos` to `endpos + (dir * -1 + jitter) * -172`. With the incoming
	// direction +X and a jitter under 0.3, the double negation lands the far end at roughly
	// `endpos + 172` on X — behind the victim, which is what paints a wall.
	const FElysiumNpc::FTraceBleedPass Pass = BandFor(9);
	TestEqual(TEXT("the trace starts at the trace's endpos"), Pass.LastStartUnits,
		FVector(0.0, 0.0, 40.0));
	TestTrue(TEXT("and runs 172 units downrange, give or take the jitter"),
		Pass.LastEndUnits.X > 120.0 && Pass.LastEndUnits.X < 225.0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 392 — `0x102664c0`, `OnTakeDamage_Dead`. Read off the listing.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageOnTakeDamageDeadTest,
	"Elysium.Substrate.NpcKernelDamage.OnTakeDamageDead", GDamageTestFlags)
bool FElysiumNpcKernelDamageOnTakeDamageDeadTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_dead"), 0x29c1d007);
	Builder.AddNpc(TEXT("npc"));
	Builder.AddNpc(TEXT("attacker"), FVector(0.0, 0.0, 100.0 * U));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Attacker = Fixture.Npc(TEXT("attacker"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Attacker });
	if (Npc == nullptr || Attacker == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// The body answers a flat 1 on every path, including the null one.
	TestEqual(TEXT("a null packet still answers 1"), Npc->OnTakeDamage_Dead(nullptr), 1);

	// The global impulse. It is a FILE-STATIC triple in retail, shared by the whole level.
	FElysiumNpc::ResetDeathThrowImpulse();
	FElysiumNpc::FElysiumTakeDamageInfo NoAttacker;
	NoAttacker.Damage = 40.f;
	NoAttacker.DamageBits = 0x1u;
	Npc->Health = 100;
	Npc->TakeDamageMode = 2;
	Npc->OnTakeDamage_Dead(&NoAttacker);
	TestEqual(TEXT("a packet with no attacker leaves the impulse at vec3_origin"),
		FElysiumNpc::DeathThrowImpulse(), FVector::ZeroVector);

	// With an attacker 100 units directly above: `a.z -= 10`, so the impulse is straight up.
	FElysiumNpc::FElysiumTakeDamageInfo WithAttacker = NoAttacker;
	WithAttacker.Attacker = Attacker->Handle;
	Npc->Health = 100;
	Npc->OnTakeDamage_Dead(&WithAttacker);
	TestEqual(TEXT("the impulse is normalized"),
		static_cast<float>(FElysiumNpc::DeathThrowImpulse().Size()), 1.0f, 0.001f);
	TestEqual(TEXT("and points from this body at the attacker, 10 units lower"),
		static_cast<float>(FElysiumNpc::DeathThrowImpulse().Z), 1.0f, 0.001f);

	// The `0xe1` low-byte gate. `DMG_BULLET` (0x2) is NOT in it, so a gunshot into a corpse takes
	// no health at all — a recovered fact, not a slip.
	Npc->Health = 100;
	FElysiumNpc::FElysiumTakeDamageInfo Bullet = WithAttacker;
	Bullet.DamageBits = 0x2u;
	Bullet.Damage = 100.f;
	TestEqual(TEXT("a non-0xe1 damage type answers 1"), Npc->OnTakeDamage_Dead(&Bullet), 1);
	TestEqual(TEXT("and takes no health"), Npc->Health, 100);

	// `m_takedamage == 1` (DAMAGE_EVENTS_ONLY) refuses too.
	Npc->TakeDamageMode = 1;
	Npc->Health = 100;
	Npc->OnTakeDamage_Dead(&WithAttacker);
	TestEqual(TEXT("DAMAGE_EVENTS_ONLY takes no health"), Npc->Health, 100);

	// The commit: `m_iHealth -= damage * 0.1` (`_DAT_104493d0`, a DOUBLE), truncated.
	Npc->TakeDamageMode = 2;
	Npc->Health = 100;
	WithAttacker.Damage = 40.f;
	Npc->OnTakeDamage_Dead(&WithAttacker);
	TestEqual(TEXT("a corpse takes a TENTH of the damage into m_iHealth"), Npc->Health, 96);

	// And the descriptor's `GetDmg()` is what the tenth is taken of when one is carried.
	FElysiumDmg Dmg = MakeResolvedDmg(70, 0x1u);
	FElysiumNpc::FElysiumTakeDamageInfo Typed = WithAttacker;
	Typed.Dmg = &Dmg;
	Typed.Damage = 0.f;
	Npc->Health = 100;
	Npc->OnTakeDamage_Dead(&Typed);
	TestEqual(TEXT("with a descriptor it is GetDmg() that is scaled"), Npc->Health, 93);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 319 — `0x1029fdb0`, `PlayerAttackerBlockedReaction`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageBlockedReactionTest,
	"Elysium.Substrate.NpcKernelDamage.PlayerAttackerBlockedReaction", GDamageTestFlags)
bool FElysiumNpcKernelDamageBlockedReactionTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_blocked"), 0x29c1d008);
	Builder.AddNpc(TEXT("npc"));
	Builder.AddNpc(TEXT("defender"), FVector(60.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpc* Defender = Fixture.Npc(TEXT("defender"));
	FElysiumNpcWorldFixture::Quiet({ Npc, Defender });
	if (Npc == nullptr || Defender == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}
	const double Now = Fixture.World.NowSeconds();

	// No roll record: `t` is 0, so the delay is `_DAT_1049a1b8` = 0.5 s exactly.
	TestTrue(TEXT("the body answers a flat 1"),
		Npc->PlayerAttackerBlockedReaction(Defender, nullptr, nullptr));
	TestEqual(TEXT("an ordinary block re-arms the attacker in 0.5 s"),
		Npc->NextAttackTime, Now + 0.5, 0.0001);
	TestEqual(TEXT("and the block-reaction activity was asked for once"),
		Npc->BlockedReactionActivityCalls, 1);
	TestEqual(TEXT("with the one recovered name"), Npc->LastBlockedReactionActivity,
		FString(ElysiumReactions::DefaultBlockedReaction));

	// A roll record whose type is NOT 2 also takes the short delay: only 2 selects the long one.
	FElysiumNpc::FElysiumMeleeDiceRollResult Roll;
	Roll.RollType = 1;
	Npc->PlayerAttackerBlockedReaction(Defender, &Roll, nullptr);
	TestEqual(TEXT("roll type 1 still re-arms in 0.5 s"), Npc->NextAttackTime, Now + 0.5, 0.0001);
	Roll.RollType = 3;
	Npc->PlayerAttackerBlockedReaction(Defender, &Roll, nullptr);
	TestEqual(TEXT("roll type 3 too"), Npc->NextAttackTime, Now + 0.5, 0.0001);

	// Roll type 2 takes `_DAT_1049a1bc` = 1.5 s — the one longer follow-up delay in the body.
	Roll.RollType = 2;
	Npc->PlayerAttackerBlockedReaction(Defender, &Roll, nullptr);
	TestEqual(TEXT("roll type 2 re-arms in 1.5 s"), Npc->NextAttackTime, Now + 1.5, 0.0001);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 374 — `0x10334180`, `GiveAmmo`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageGiveAmmoTest,
	"Elysium.Substrate.NpcKernelDamage.GiveAmmo", GDamageTestFlags)
bool FElysiumNpcKernelDamageGiveAmmoTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_ammo"), 0x29c1d009);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// The four bounds gates, each answering 0, in retail's own order.
	TestEqual(TEXT("a zero count answers 0"), Npc->GiveAmmo(0, 3, false), 0);
	TestEqual(TEXT("a negative count answers 0"), Npc->GiveAmmo(-5, 3, false), 0);
	TestEqual(TEXT("a negative index answers 0"), Npc->GiveAmmo(10, -1, false), 0);
	TestEqual(TEXT("index 0x20 is out of range"), Npc->GiveAmmo(10, 0x20, false), 0);
	TestEqual(TEXT("and so is anything above it"), Npc->GiveAmmo(10, 99, false), 0);

	// The rules gate is the permissive seam; the CLAMP is what refuses. `AmmoMaxCarry` answers 0,
	// so `room` is never positive and the body returns 0 without sounding — the recovered refusal,
	// because no table joins retail's `CAmmoDef` index to this runtime's ammo-type names.
	TestTrue(TEXT("the ammo-enabled gate is the permissive arm"), Npc->GameRulesAllowsAmmo(3));
	TestEqual(TEXT("MaxCarry answers nothing: the CAmmoDef index order is unrecovered"),
		Npc->AmmoMaxCarry(3), 0);
	TestTrue(TEXT("and so does the index-to-name join"), Npc->AmmoTypeNameForIndex(3).IsEmpty());
	TestEqual(TEXT("so an in-range grant still answers 0"), Npc->GiveAmmo(10, 3, false), 0);
	TestEqual(TEXT("and index 0x1f, the last legal one, answers 0 for the same reason"),
		Npc->GiveAmmo(10, 0x1f, false), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// Slot 292's species gate — `0x10378cb0` and `0x103802a0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageFlinchGateTest,
	"Elysium.Substrate.NpcKernelDamage.DamageFlinchGate", GDamageTestFlags)
bool FElysiumNpcKernelDamageFlinchGateTest::RunTest(const FString&)
{
	// The table: `vtmb_slot 292` lists 254 classes and exactly two of them gate the flinch.
	int32 Count = 0;
	const FElysiumNpc::FDamageFlinchSpecies* Rows = FElysiumNpc::DamageFlinchSpeciesRows(Count);
	TestEqual(TEXT("slot 292 has two species gates"), Count, 2);
	TestEqual(TEXT("row 0 is CNPC_VGargoyle"), FString(Rows[0].RetailClass),
		FString(TEXT("CNPC_VGargoyle")));
	TestEqual(TEXT("filled by 0x10378cb0"), FString(Rows[0].Body), FString(TEXT("0x10378cb0")));
	TestEqual(TEXT("row 1 is CNPC_VHengeyokai"), FString(Rows[1].RetailClass),
		FString(TEXT("CNPC_VHengeyokai")));
	TestEqual(TEXT("filled by 0x103802a0"), FString(Rows[1].Body), FString(TEXT("0x103802a0")));
	// Both bodies are byte-identical and the mask is exactly the port's own firearm mask.
	TestEqual(TEXT("the Gargoyle mask is DMG_BULLET|DMG_BUCKSHOT"), Rows[0].SuppressMask,
		ElysiumDamage::FirearmMask);
	TestEqual(TEXT("and the Hengeyokai mask is the same word"), Rows[1].SuppressMask,
		Rows[0].SuppressMask);
	TestEqual(TEXT("which is 0x4000002"), Rows[0].SuppressMask, 0x4000002u);

	// Neither classname is a registered spawn leaf, so both rows are reached by retail name.
	TestNotNull(TEXT("CNPC_VGargoyle is reachable by name"),
		FElysiumNpc::DamageFlinchSpeciesOf(TEXT("CNPC_VGargoyle")));
	TestNotNull(TEXT("CNPC_VHengeyokai is reachable by name"),
		FElysiumNpc::DamageFlinchSpeciesOf(TEXT("CNPC_VHengeyokai")));
	TestNull(TEXT("and the Troika line has no gate"),
		FElysiumNpc::DamageFlinchSpeciesOf(TEXT("CNPC_VHumanCombatant")));

	// The pure rule, arm by arm.
	const uint32 Mask = ElysiumDamage::FirearmMask;
	TestTrue(TEXT("a bullet hit is suppressed"),
		FElysiumNpc::DamageFlinchSuppressed(ElysiumDamage::DmgBullet, 25.f, Mask));
	TestTrue(TEXT("a buckshot hit is suppressed"),
		FElysiumNpc::DamageFlinchSuppressed(ElysiumDamage::DmgBuckshot, 25.f, Mask));
	TestFalse(TEXT("a slash hit is not"),
		FElysiumNpc::DamageFlinchSuppressed(ElysiumDamage::DmgSlash, 25.f, Mask));
	// The magnitude test is an EXACT inequality against 0.0, not a threshold.
	TestTrue(TEXT("a zero-magnitude hit is suppressed whatever the mask"),
		FElysiumNpc::DamageFlinchSuppressed(ElysiumDamage::DmgSlash, 0.f, Mask));
	TestFalse(TEXT("and the smallest non-zero magnitude is not"),
		FElysiumNpc::DamageFlinchSuppressed(ElysiumDamage::DmgSlash, 0.0001f, Mask));
	// The mask is tested FIRST: a firearm hit is refused before the magnitude is even read.
	TestTrue(TEXT("a firearm hit is suppressed even at full magnitude"),
		FElysiumNpc::DamageFlinchSuppressed(ElysiumDamage::DmgBullet, 1000.f, Mask));

	// A spawned NPC of a class with no row flinches exactly as it always has.
	FElysiumNpcWorldBuilder Builder(TEXT("damage_flinchgate"), 0x29c1d00a);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	FElysiumDmg Shot = MakeResolvedDmg(30, ElysiumDamage::DmgBullet);
	TestFalse(TEXT("an ordinary combatant does not suppress a gunshot flinch"),
		Npc->SuppressesDamageFlinch(Shot));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x102b8c40` — the took-damage schedule arm, and `0x1035d150` — Andrei's ideal state.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageCachePositionTest,
	"Elysium.Substrate.NpcKernelDamage.CacheDamagePosition", GDamageTestFlags)
bool FElysiumNpcKernelDamageCachePositionTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_cachepos"), 0x29c1d00b);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	Npc->Senses.Memory.LastDamageAttackPosition = FVector(11.0, 22.0, 33.0);
	Npc->SavePosition = FVector::ZeroVector;
	Npc->Cognition.bCondTookDamage = true;

	// Neither condition: the arm answers 0 and writes NOTHING — the latch is not spent.
	TestEqual(TEXT("no damage condition answers 0"), Npc->CacheDamagePosition(), 0);
	TestTrue(TEXT("and leaves m_bCondTookDamage alone"), Npc->Cognition.bCondTookDamage);
	TestEqual(TEXT("and m_vSavePosition alone"), Npc->SavePosition, FVector::ZeroVector);

	// `COND_LIGHT_DAMAGE` (0x4c) alone opens it.
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::LightDamage);
	TestEqual(TEXT("light damage answers schedule 0x8a"), Npc->CacheDamagePosition(), 0x8a);
	TestFalse(TEXT("and spends m_bCondTookDamage"), Npc->Cognition.bCondTookDamage);
	TestEqual(TEXT("and copies m_vecLastDamageAttackPos into m_vSavePosition"),
		Npc->SavePosition, FVector(11.0, 22.0, 33.0));

	// `COND_HEAVY_DAMAGE` (0x4d) alone opens it too — the test is an OR.
	Npc->Cognition.Conditions.Clear(EElysiumNpcCond::LightDamage);
	Npc->Cognition.Conditions.Set(EElysiumNpcCond::HeavyDamage);
	Npc->SavePosition = FVector::ZeroVector;
	TestEqual(TEXT("heavy damage answers the same schedule"), Npc->CacheDamagePosition(), 0x8a);
	TestEqual(TEXT("and copies the same position"), Npc->SavePosition, FVector(11.0, 22.0, 33.0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageAndreiIdealStateTest,
	"Elysium.Substrate.NpcKernelDamage.AndreiSelectIdealState", GDamageTestFlags)
bool FElysiumNpcKernelDamageAndreiIdealStateTest::RunTest(const FString&)
{
	// `npc_VAndreiBlood` IS a registered spawn leaf, so this row is exercised on a live NPC.
	FElysiumNpcWorldBuilder Builder(TEXT("damage_andrei"), 0x29c1d00c);
	Builder.AddNpc(TEXT("andrei"), FVector::ZeroVector, TEXT("npc_VAndreiBlood"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Andrei = Fixture.Npc(TEXT("andrei"));
	FElysiumNpcWorldFixture::Quiet({ Andrei });
	if (Andrei == nullptr)
	{
		AddError(TEXT("fixture did not stand npc_VAndreiBlood"));
		return false;
	}
	TestNotNull(TEXT("and the census claims it"), Andrei->RetailClass());
	TestTrue(TEXT("as CNPC_VAndreiBlood"), Andrei->IsRetailClass(TEXT("CNPC_VAndreiBlood")));

	// Twenty-five bytes: stamp the trace selector with 4, then answer 1 (IDLE) or 2 (ALERT).
	Andrei->SelectIdealStateSelector = 0;
	Andrei->bAndreiActivated = false;
	TestEqual(TEXT("an unactivated Andrei answers IDLE"),
		Andrei->CNPC_VAndreiBlood_vfunc461(), EElysiumNpcState::Idle);
	TestEqual(TEXT("and stamps the trace selector with 4"), Andrei->SelectIdealStateSelector, 4);

	Andrei->SelectIdealStateSelector = 0;
	Andrei->bAndreiActivated = true;
	TestEqual(TEXT("an activated Andrei answers ALERT"),
		Andrei->CNPC_VAndreiBlood_vfunc461(), EElysiumNpcState::Alert);
	TestEqual(TEXT("and stamps the selector on that arm too"),
		Andrei->SelectIdealStateSelector, 4);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x103c67f0` — the attack-recency test.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageLastAttackTest,
	"Elysium.Substrate.NpcKernelDamage.LastAttackTimeElapsed", GDamageTestFlags)
bool FElysiumNpcKernelDamageLastAttackTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_lastattack"), 0x29c1d00d);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	const double Now = Fixture.World.NowSeconds();

	// `threshold < curtime - m_flLastAttackTime`, STRICTLY.
	Npc->LastAttackTime = Now - 10.0;
	TestTrue(TEXT("ten seconds ago clears a five-second threshold"),
		Npc->LastAttackTimeElapsed(5.f));
	TestFalse(TEXT("but not a twenty-second one"), Npc->LastAttackTimeElapsed(20.f));
	TestFalse(TEXT("and exactly ten does not clear: the compare is strict"),
		Npc->LastAttackTimeElapsed(10.f));
	Npc->LastAttackTime = Now;
	TestFalse(TEXT("an attack this instant clears nothing"), Npc->LastAttackTimeElapsed(0.f));
	return true;
}

// -------------------------------------------------------------------------------------------------
// The emitter family — `0x103c6df0`, `0x103c6eb0`, `0x103c7010`, `0x103c7150`, `0x1036e8c0`,
// `0x103ab110`, `0x1035e1a0`, `0x1035e3c0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageEmittersTest,
	"Elysium.Substrate.NpcKernelDamage.Emitters", GDamageTestFlags)
bool FElysiumNpcKernelDamageEmittersTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_emitters"), 0x29c1d00e);
	Builder.AddNpc(TEXT("boss"), FVector(10.0 * U, 20.0 * U, 30.0 * U));
	Builder.AddNpc(TEXT("body"), FVector(0.0, 0.0, 77.0 * U));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpc* Body = Fixture.Npc(TEXT("body"));
	FElysiumNpcWorldFixture::Quiet({ Boss, Body });
	if (Boss == nullptr || Body == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// `0x103c6df0` — one store, four regions.
	Boss->SetBodyEmitterName(0, TEXT("gore_head"));
	Boss->SetBodyEmitterName(3, TEXT("gore_torso"));
	TestEqual(TEXT("region 0's name is stored"), Boss->BodyEmitterNames[0],
		FString(TEXT("gore_head")));
	TestEqual(TEXT("region 3's name is stored"), Boss->BodyEmitterNames[3],
		FString(TEXT("gore_torso")));
	TestTrue(TEXT("an untouched region stays empty"), Boss->BodyEmitterNames[1].IsEmpty());

	// `0x103c7010` — the two refusals, in retail's order.
	TestEqual(TEXT("no attach entity answers nothing, even with a name set"),
		Boss->SpawnBodyEmitter(0, FElysiumEntityHandle()), INDEX_NONE);
	TestEqual(TEXT("an unset name answers nothing, even with an attach entity"),
		Boss->SpawnBodyEmitter(1, Body->Handle), INDEX_NONE);

	// Region 3 one-shots on the boss itself (`+0x3cc(this, 1)`); every other region attaches at the
	// given entity (`+0x3cc(this, 2, attach)`). And retail never STARTS either.
	Boss->EmitterCalls.Reset();
	const int32 Region0 = Boss->SpawnBodyEmitter(0, Body->Handle);
	TestTrue(TEXT("region 0 creates an emitter"), Region0 != INDEX_NONE);
	if (Region0 != INDEX_NONE)
	{
		TestEqual(TEXT("with the region's name"), Boss->EmitterCalls[Region0].Name,
			FString(TEXT("gore_head")));
		TestEqual(TEXT("in attach mode 2"), Boss->EmitterCalls[Region0].AttachMode, 2);
		TestTrue(TEXT("at the given entity"),
			Boss->EmitterCalls[Region0].AttachEntity == Body->Handle);
		TestFalse(TEXT("and NOT started: SpawnBodyEmitter never calls +0x3c4"),
			Boss->EmitterCalls[Region0].bStarted);
	}
	const int32 Region3 = Boss->SpawnBodyEmitter(3, Body->Handle);
	if (Region3 != INDEX_NONE)
	{
		TestEqual(TEXT("region 3 one-shots in attach mode 1"),
			Boss->EmitterCalls[Region3].AttachMode, 1);
		TestFalse(TEXT("and attaches to nothing"),
			Boss->EmitterCalls[Region3].AttachEntity.IsSet());
	}

	// `0x103c6eb0` — all four names cleared, unconditionally.
	Boss->ClearBodyEmitterNames();
	for (int32 i = 0; i < 4; ++i)
	{
		TestTrue(TEXT("ClearBodyEmitterNames empties every region"),
			Boss->BodyEmitterNames[i].IsEmpty());
	}

	// `0x103c7150` — four iterations, every one of them asking the kill seam, and the handles are
	// NOT cleared afterwards.
	Boss->EmitterKillCalls = 0;
	Boss->ParticleEmitters[1] = Body->Handle;
	Boss->KillBodyEmitters();
	TestEqual(TEXT("KillBodyEmitters walks all four words"), Boss->EmitterKillCalls, 4);
	TestTrue(TEXT("and leaves the handles standing"), Boss->ParticleEmitters[1] == Body->Handle);

	// `0x1036e8c0` — the same pair on the single centre emitter.
	Boss->EmitterKillCalls = 0;
	Boss->ChangCenterEmitter = Body->Handle;
	Boss->KillCenterEmitter();
	TestEqual(TEXT("KillCenterEmitter asks once"), Boss->EmitterKillCalls, 1);
	TestTrue(TEXT("and leaves m_hCenterEmitter standing"),
		Boss->ChangCenterEmitter == Body->Handle);

	// `0x103ab110` — the blood pool takes X and Y from this body's origin and Z from the optional
	// orientation entity, and it DOES start its emitter.
	Boss->EmitterCalls.Reset();
	Boss->SpawnBloodPoolEmitter(TEXT("blood_pool"), Body);
	if (Boss->EmitterCalls.Num() == 1)
	{
		TestEqual(TEXT("the pool takes this body's X"),
			Boss->EmitterCalls[0].PositionUnits.X, 10.0, 0.001);
		TestEqual(TEXT("and the orientation entity's Z"),
			Boss->EmitterCalls[0].PositionUnits.Z, 77.0, 0.001);
		TestTrue(TEXT("and it IS started"), Boss->EmitterCalls[0].bStarted);
	}
	else
	{
		AddError(TEXT("SpawnBloodPoolEmitter did not create exactly one emitter"));
	}
	// An empty name refuses before anything is created.
	Boss->EmitterCalls.Reset();
	Boss->SpawnBloodPoolEmitter(FString(), Body);
	TestEqual(TEXT("an empty name creates nothing"), Boss->EmitterCalls.Num(), 0);

	// `0x1035e1a0` / `0x1035e3c0` — one body written twice; the whole difference is the word and
	// the attach.
	Boss->EmitterCalls.Reset();
	Boss->StartBloodEmitter(TEXT("andrei_blood"));
	if (Boss->EmitterCalls.Num() == 1)
	{
		TestEqual(TEXT("the blood arm attaches in mode 2"), Boss->EmitterCalls[0].AttachMode, 2);
		TestTrue(TEXT("to this entity"), Boss->EmitterCalls[0].AttachEntity == Boss->Handle);
		TestTrue(TEXT("names no bone"), Boss->EmitterCalls[0].AttachBone.IsEmpty());
		TestTrue(TEXT("and starts it"), Boss->EmitterCalls[0].bStarted);
	}
	else
	{
		AddError(TEXT("StartBloodEmitter did not create exactly one emitter"));
	}
	Boss->EmitterCalls.Reset();
	Boss->StartSummonEmitter(TEXT("andrei_summon"));
	if (Boss->EmitterCalls.Num() == 1)
	{
		TestEqual(TEXT("the summon arm attaches at Bip01_R_Hand"),
			Boss->EmitterCalls[0].AttachBone, FString(FElysiumNpc::SummonEmitterBoneName()));
		TestTrue(TEXT("and starts it too"), Boss->EmitterCalls[0].bStarted);
	}
	else
	{
		AddError(TEXT("StartSummonEmitter did not create exactly one emitter"));
	}
	// Both refuse a null name before touching the cached handle.
	Boss->EmitterCalls.Reset();
	Boss->StartBloodEmitter(FString());
	Boss->StartSummonEmitter(FString());
	TestEqual(TEXT("a null name creates nothing on either arm"), Boss->EmitterCalls.Num(), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x1036dd20` — `SpawnEnergyBall`, and `0x103c7230` — `CausePlayerAOEDamage`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageEnergyBallTest,
	"Elysium.Substrate.NpcKernelDamage.SpawnEnergyBall", GDamageTestFlags)
bool FElysiumNpcKernelDamageEnergyBallTest::RunTest(const FString&)
{
	// The pure offset rule, with an identity basis so each constant is readable on its own axis.
	const FVector Spawn = FElysiumNpc::EnergyBallSpawnPoint(FVector::ZeroVector,
		FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0));
	TestEqual(TEXT("forward is pushed by _DAT_104ada24 = 50"), Spawn.X, 50.0, 0.001);
	TestEqual(TEXT("right is pushed by _DAT_104ada2c = -10"), Spawn.Y, -10.0, 0.001);
	TestEqual(TEXT("up is pushed by _DAT_104ada28 = 40"), Spawn.Z, 40.0, 0.001);
	// And the origin is added, not replaced.
	const FVector Offset = FElysiumNpc::EnergyBallSpawnPoint(FVector(100.0, 200.0, 300.0),
		FVector(1.0, 0.0, 0.0), FVector(0.0, 1.0, 0.0), FVector(0.0, 0.0, 1.0));
	TestEqual(TEXT("the origin is added"), Offset.X, 150.0, 0.001);

	FElysiumNpcWorldBuilder Builder(TEXT("damage_energyball"), 0x29c1d00f);
	Builder.AddNpc(TEXT("chang"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Chang = Fixture.Npc(TEXT("chang"));
	FElysiumNpcWorldFixture::Quiet({ Chang });
	if (Chang == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}
	Chang->CreateEntityCalls.Reset();
	const FElysiumEntityHandle Ball = Chang->SpawnEnergyBall();
	TestFalse(TEXT("the create seam answers an invalid handle — no such registered class"),
		Ball.IsSet());
	if (Chang->CreateEntityCalls.Num() == 1)
	{
		TestEqual(TEXT("and it asked for item_w_chang_energy_ball by name"),
			Chang->CreateEntityCalls[0].Classname,
			FString(TEXT("item_w_chang_energy_ball")));
	}
	else
	{
		AddError(TEXT("SpawnEnergyBall did not ask the create seam exactly once"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageAoeTest,
	"Elysium.Substrate.NpcKernelDamage.CausePlayerAOEDamage", GDamageTestFlags)
bool FElysiumNpcKernelDamageAoeTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_aoe"), 0x29c1d010);
	Builder.AddNpc(TEXT("boss"));
	Builder.AddNpc(TEXT("victim"), FVector(100.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Boss = Fixture.Npc(TEXT("boss"));
	FElysiumNpc* Victim = Fixture.Npc(TEXT("victim"));
	FElysiumNpcWorldFixture::Quiet({ Boss, Victim });
	if (Boss == nullptr || Victim == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// No `m_hClosestPlayer`: nothing at all happens. The sense pass fills that word on the first
	// think, so the case clears it before it asks.
	Boss->Senses.Memory.ClosestPlayer = FElysiumEntityHandle();
	Boss->AoeImpactSounds.Reset();
	Boss->CausePlayerAOEDamage(FVector::ZeroVector, 500.f);
	TestEqual(TEXT("no closest player does nothing"), Boss->AoeImpactSounds.Num(), 0);

	// The radius gate is on the LENGTH of the delta and is STRICT.
	Boss->Senses.Memory.ClosestPlayer = Victim->Handle;
	Boss->AoeImpactSounds.Reset();
	Boss->CausePlayerAOEDamage(FVector::ZeroVector, 100.f);
	TestEqual(TEXT("a victim exactly at the radius is outside it"),
		Boss->AoeImpactSounds.Num(), 0);
	Boss->CausePlayerAOEDamage(FVector::ZeroVector, 100.001f);
	TestEqual(TEXT("a victim just inside it is hit"), Boss->AoeImpactSounds.Num(), 1);
	// The sound pick: the `+0x50c` seam answers 0, which is the default id.
	if (Boss->AoeImpactSounds.Num() == 1)
	{
		TestEqual(TEXT("and the default impact sound id is 0x79"), Boss->AoeImpactSounds[0], 0x79);
	}
	TestEqual(TEXT("the trace-attack result seam answers 0, the default arm"),
		Boss->AoeTraceAttackResultCode(Victim), 0);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x103b10f0` — `KillSheriff`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageKillSheriffTest,
	"Elysium.Substrate.NpcKernelDamage.KillSheriff", GDamageTestFlags)
bool FElysiumNpcKernelDamageKillSheriffTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_killsheriff"), 0x29c1d011);
	Builder.AddNpc(TEXT("sheriffman"));
	Builder.AddCounter(TEXT("logic_zap_player"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Sheriff = Fixture.Npc(TEXT("sheriffman"));
	FElysiumNpcWorldFixture::Quiet({ Sheriff });
	if (Sheriff == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// The relay half needs BOTH names. `sheriff` is absent from this map, so the relay must NOT
	// fire — the second lookup is a presence test and reproducing it is the point.
	TestNull(TEXT("this map stands no entity named sheriff"),
		Fixture.World.FindByName(TEXT("sheriff")));
	TestNotNull(TEXT("but it does stand logic_zap_player"),
		Fixture.World.FindByName(TEXT("logic_zap_player")));

	// The weapon half runs UNCONDITIONALLY on the relay half — and with no active weapon it does
	// nothing either, which is retail's own `if (weapon != 0)` guard.
	Sheriff->HideAndUnsolidifyCalls.Reset();
	Sheriff->KillSheriff();
	TestEqual(TEXT("with no active weapon nothing is hidden"),
		Sheriff->HideAndUnsolidifyCalls.Num(), 0);

	// With a weapon the bits are exactly EF_NODRAW and FSOLID_NOT_SOLID, and the weapon is NOT
	// removed — the sword stays on the corpse, invisible and non-solid.
	Sheriff->HideAndUnsolidifyWeapon(Sheriff->Handle);
	if (Sheriff->HideAndUnsolidifyCalls.Num() == 1)
	{
		TestEqual(TEXT("m_fEffects takes EF_NODRAW"),
			Sheriff->HideAndUnsolidifyCalls[0].EffectBits, 0x20u);
		TestEqual(TEXT("and the collision takes FSOLID_NOT_SOLID"),
			Sheriff->HideAndUnsolidifyCalls[0].SolidBits, 0x4u);
	}
	else
	{
		AddError(TEXT("the hide/unsolidify seam did not record the call"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------
// The `CNPC_VMingXiao` throw chain — `0x10397dd0`, `0x10398d90`, `0x10398fd0`, `0x103990c0`,
// `0x10396bc0`, `0x10397000`, `0x10399fe0`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageMingXiaoTest,
	"Elysium.Substrate.NpcKernelDamage.MingXiaoThrowChain", GDamageTestFlags)
bool FElysiumNpcKernelDamageMingXiaoTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_mingxiao"), 0x29c1d012);
	Builder.AddNpc(TEXT("ming"));
	Builder.AddNpc(TEXT("object"), FVector(200.0 * U, 0.0, 0.0));
	// A THIRD body for the slot-166 block: the cleanup bodies above reach `UTIL_Remove` on whatever
	// `object` is, and a killed entity no longer resolves, which would make the handle comparisons
	// below measure the fixture rather than the recovered walk.
	Builder.AddNpc(TEXT("standable"), FVector(400.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Ming = Fixture.Npc(TEXT("ming"));
	FElysiumNpc* Object = Fixture.Npc(TEXT("object"));
	FElysiumNpc* Standable = Fixture.Npc(TEXT("standable"));
	FElysiumNpcWorldFixture::Quiet({ Ming, Object, Standable });
	if (Ming == nullptr || Object == nullptr || Standable == nullptr)
	{
		AddError(TEXT("fixture did not stand all three NPCs"));
		return false;
	}
	const double Now = Fixture.World.NowSeconds();

	// `0x10397dd0` — the reset happens only when BOTH parameters are non-null.
	Ming->MingXiaoSpitAttackTimer = 55.0;
	Ming->SpitAttackTimer(false, true);
	TestEqual(TEXT("one null parameter resets nothing"), Ming->MingXiaoSpitAttackTimer, 55.0);
	Ming->SpitAttackTimer(true, false);
	TestEqual(TEXT("the other null parameter resets nothing either"),
		Ming->MingXiaoSpitAttackTimer, 55.0);
	Ming->SpitAttackTimer(true, true);
	TestEqual(TEXT("both set resets m_flSpitAttackTimer to 0"),
		Ming->MingXiaoSpitAttackTimer, 0.0);

	// `0x10398d90` — one store, thirteen bytes.
	Ming->ThrowableObjectMode(3);
	TestEqual(TEXT("ThrowableObjectMode is a straight store"),
		Ming->MingXiaoThrowableObjectMode, 3);

	// `0x103983d0`'s sibling `0x103990c0`'s speed rule: 1000 is a FLOOR, not a cap.
	TestEqual(TEXT("a sum under 1000 answers the 1000 floor"),
		FElysiumNpc::MingXiaoThrowSpeed(100.f, 1.f, 0.f), 1000.f, 0.001f);
	TestEqual(TEXT("a sum of exactly 1000 answers the floor too"),
		FElysiumNpc::MingXiaoThrowSpeed(1000.f, 1.f, 0.f), 1000.f, 0.001f);
	TestEqual(TEXT("a sum above it answers the sum"),
		FElysiumNpc::MingXiaoThrowSpeed(1000.f, 2.f, 5.f), 2005.f, 0.001f);
	// The three cvars are unrecovered and answer 0, which makes every real call take the floor.
	TestEqual(TEXT("the quadratic cvar is unrecovered"), Ming->MingXiaoThrowCvar(0), 0.f);
	TestEqual(TEXT("the constant cvar is unrecovered"), Ming->MingXiaoThrowCvar(1), 0.f);
	TestEqual(TEXT("the Z cvar is unrecovered"), Ming->MingXiaoThrowCvar(2), 0.f);

	// `0x10398fd0` — the cleanup's three unconditional writes.
	Ming->MingXiaoThrowObject = Object->Handle;
	Ming->MingXiaoPhysicsAnimlink = Object->Handle;
	Ming->MingXiaoThrowableObjectMode = 4;
	Ming->MingXiaoThrowCleanup();
	TestFalse(TEXT("the cleanup clears m_hThrowObject"), Ming->MingXiaoThrowObject.IsSet());
	TestFalse(TEXT("and m_hPhysicsAnimlink"), Ming->MingXiaoPhysicsAnimlink.IsSet());
	TestEqual(TEXT("and sets the throwable mode to 0"), Ming->MingXiaoThrowableObjectMode, 0);

	// `0x103990c0` — the SAME tail runs on every path, including the one with no enemy at all.
	Ming->MingXiaoThrowObject = Object->Handle;
	Ming->MingXiaoThrowableObjectMode = 5;
	Ming->Senses.Memory.Enemy = FElysiumEntityHandle();
	Ming->LaunchRagdollTowardTarget();
	TestFalse(TEXT("the launch clears m_hThrowObject even with no enemy"),
		Ming->MingXiaoThrowObject.IsSet());
	TestEqual(TEXT("and zeroes the throwable mode"), Ming->MingXiaoThrowableObjectMode, 0);

	// `0x10396bc0` — the three refusals, in retail's order.
	Ming->MingXiaoThrowObject = Object->Handle;
	TestEqual(TEXT("a live throw object answers 0"), Ming->MingXiaoFindThrowObject(0, 100), 0);
	Ming->MingXiaoThrowObject = FElysiumEntityHandle();
	Ming->MingXiaoPickupCooldownB = Now + 10.0;
	TestEqual(TEXT("a live cooldown B answers 0"), Ming->MingXiaoFindThrowObject(0, 100), 0);
	Ming->MingXiaoPickupCooldownB = 0.0;
	Ming->MingXiaoPickupCooldownA = Now + 10.0;
	TestEqual(TEXT("a live cooldown A answers 0"), Ming->MingXiaoFindThrowObject(0, 100), 0);
	// Past both cooldowns the condition-9 clear runs unconditionally, and the search only with a
	// draw strictly below the ceiling. Neither arm can find a pedestal here, so both answer 0.
	Ming->MingXiaoPickupCooldownA = 0.0;
	Ming->Cognition.Conditions.Set(EElysiumNpcCond::TooFarForMelee);
	TestEqual(TEXT("with no pedestal in reach the search answers 0"),
		Ming->MingXiaoFindThrowObject(0, 100), 0);
	TestFalse(TEXT("but condition 9 was cleared on the way"),
		Ming->Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee));
	// A draw at or above the ceiling skips the search entirely.
	Ming->Cognition.Conditions.Set(EElysiumNpcCond::TooFarForMelee);
	TestEqual(TEXT("a draw at the ceiling also answers 0"),
		Ming->MingXiaoFindThrowObject(100, 100), 0);
	TestFalse(TEXT("and still clears condition 9, which is ahead of the draw"),
		Ming->Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee));

	// `0x10397000` — slot 166.
	TestTrue(TEXT("an unrelated entity answers true"),
		Ming->SeveredTentaclesCanStandOn(Standable));
	Ming->Proxies[2] = Standable->Handle;
	TestFalse(TEXT("a registered proxy answers false"),
		Ming->SeveredTentaclesCanStandOn(Standable));
	Ming->Proxies[2] = FElysiumEntityHandle();
	Ming->SeveredTentacles[5] = Standable->Handle;
	TestFalse(TEXT("a severed tentacle answers false"),
		Ming->SeveredTentaclesCanStandOn(Standable));
	Ming->SeveredTentacles[5] = FElysiumEntityHandle();

	// **A null candidate answers FALSE while any of the twelve handles is dead**, and that is
	// retail's own behaviour rather than a transcription slip: the body compares the RESOLVED
	// pointer, a dead handle resolves to null, and the first comparison against a null candidate
	// therefore matches. The "null answers true" path exists only when all twelve resolve.
	TestFalse(TEXT("a null candidate matches the first dead handle and answers false"),
		Ming->SeveredTentaclesCanStandOn(nullptr));
	for (int32 i = 0; i < 6; ++i)
	{
		Ming->Proxies[i] = Standable->Handle;
		Ming->SeveredTentacles[i] = Standable->Handle;
	}
	TestTrue(TEXT("with every handle live, a null candidate answers true and skips IsStandable"),
		Ming->SeveredTentaclesCanStandOn(nullptr));
	for (int32 i = 0; i < 6; ++i)
	{
		Ming->Proxies[i] = FElysiumEntityHandle();
		Ming->SeveredTentacles[i] = FElysiumEntityHandle();
	}

	// `0x10399fe0` — slot 100. `m_bHasTransformed` is the first refusal and the hitbox-set count
	// the second; the seam answers 0 sets, which is retail's own "fewer than 7" arm.
	Ming->bMingXiaoHasTransformed = false;
	TestFalse(TEXT("an untransformed MingXiao tests no hitboxes"),
		Ming->TestHitboxesMingXiao(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), 0x2400b));
	Ming->bMingXiaoHasTransformed = true;
	TestEqual(TEXT("the hitbox-set seam answers nothing"), Ming->HitboxSetCount(), 0);
	TestFalse(TEXT("so the seven-set requirement refuses"),
		Ming->TestHitboxesMingXiao(FVector::ZeroVector, FVector(100.0, 0.0, 0.0), 0x2400b));
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x103bf170` — `CNPC_VTzimisce`'s pickup release (29c's `VGargoyleGibCleanup` row).
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageTzimisceReleaseTest,
	"Elysium.Substrate.NpcKernelDamage.VGargoyleGibCleanup", GDamageTestFlags)
bool FElysiumNpcKernelDamageTzimisceReleaseTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_tzimrelease"), 0x29c1d013);
	Builder.AddNpc(TEXT("tzim"));
	Builder.AddNpc(TEXT("carried"), FVector(50.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Tzim = Fixture.Npc(TEXT("tzim"));
	FElysiumNpc* Carried = Fixture.Npc(TEXT("carried"));
	FElysiumNpcWorldFixture::Quiet({ Tzim, Carried });
	if (Tzim == nullptr || Carried == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	Tzim->PickupTarget = Carried->Handle;
	Tzim->TzimiscePhysicsAnimlink = Carried->Handle;
	Tzim->RemovedEntities.Reset();
	Tzim->FormBitCalls = 0;
	Tzim->bLastFormBitArm = true;

	Tzim->VGargoyleGibCleanup();
	TestFalse(TEXT("m_hPickupTarget is cleared first"), Tzim->PickupTarget.IsSet());
	TestFalse(TEXT("and m_hPhysicsAnimlink after the removal"),
		Tzim->TzimiscePhysicsAnimlink.IsSet());
	TestEqual(TEXT("the link was removed once"), Tzim->RemovedEntities.Num(), 1);
	TestEqual(TEXT("the CARRYING_BODY flag seam was asked once"), Tzim->FormBitCalls, 1);
	TestFalse(TEXT("with the CLEAR arm"), Tzim->bLastFormBitArm);

	// The unguarded `UTIL_Remove`: retail calls it even on a dead handle, so the removal path runs
	// a second time on an already-empty word.
	Tzim->RemovedEntities.Reset();
	Tzim->VGargoyleGibCleanup();
	TestEqual(TEXT("the removal runs even with a dead link handle"),
		Tzim->RemovedEntities.Num(), 1);
	return true;
}

// -------------------------------------------------------------------------------------------------
// `0x10365860` — `ThrowGrenade`, and `0x1038f2c0` — `ThrowModel`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageThrowTest,
	"Elysium.Substrate.NpcKernelDamage.ThrowGrenadeAndModel", GDamageTestFlags)
bool FElysiumNpcKernelDamageThrowTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_throw"), 0x29c1d014);
	Builder.AddNpc(TEXT("bach"));
	Builder.AddNpc(TEXT("spot"), FVector(300.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Bach = Fixture.Npc(TEXT("bach"));
	FElysiumNpc* Spot = Fixture.Npc(TEXT("spot"));
	FElysiumNpcWorldFixture::Quiet({ Bach, Spot });
	if (Bach == nullptr || Spot == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}
	const double Now = Fixture.World.NowSeconds();

	// The cooldown is the FIRST gate: `curtime - m_flLastGrenadeTime >= 5.0`, inclusive.
	Bach->BachLastGrenadeTime = Now - 1.0;
	Bach->CreateEntityCalls.Reset();
	Bach->ThrowGrenade(TEXT("spot"), 900.f);
	TestEqual(TEXT("inside the 5 s cooldown nothing is created"),
		Bach->CreateEntityCalls.Num(), 0);

	// A missing target leaves the cooldown UNSTAMPED, so the next think retries at once.
	Bach->BachLastGrenadeTime = Now - 10.0;
	Bach->ThrowGrenade(TEXT("no_such_entity"), 900.f);
	TestEqual(TEXT("a missing target creates nothing"), Bach->CreateEntityCalls.Num(), 0);
	TestEqual(TEXT("and does not spend the cooldown"), Bach->BachLastGrenadeTime, Now - 10.0);

	// A real target: the stamp happens BEFORE the create, so a failed create still spends it.
	Bach->bBachCamperFlag = true;
	Bach->ThrowGrenade(TEXT("spot"), 900.f);
	TestEqual(TEXT("a real target asks the create seam once"),
		Bach->CreateEntityCalls.Num(), 1);
	if (Bach->CreateEntityCalls.Num() == 1)
	{
		TestEqual(TEXT("for item_w_grenade_frag"), Bach->CreateEntityCalls[0].Classname,
			FString(TEXT("item_w_grenade_frag")));
		TestEqual(TEXT("at the named target's origin, in source units"),
			Bach->CreateEntityCalls[0].PositionUnits.X, 300.0, 0.01);
	}
	TestEqual(TEXT("and the cooldown is stamped with curtime"), Bach->BachLastGrenadeTime, Now);
	// The create seam answers invalid, so the body takes retail's own `if (grenade != 0)` guard and
	// `m_bCamperFlag` — which is written LAST, past that guard — stays set.
	TestTrue(TEXT("a failed create leaves m_bCamperFlag alone: it is written past the guard"),
		Bach->bBachCamperFlag);

	// `0x1038f2c0`: create `prop_physics`, and the `if (ragdoll != 0)` guard answers false.
	Bach->CreateEntityCalls.Reset();
	TestFalse(TEXT("ThrowModel answers false when the ragdoll cannot be made"),
		Bach->ThrowModel(TEXT("models/prop/crate.mdl"), FString()));
	if (Bach->CreateEntityCalls.Num() == 1)
	{
		TestEqual(TEXT("and it asked for prop_physics by name"),
			Bach->CreateEntityCalls[0].Classname, FString(TEXT("prop_physics")));
	}
	else
	{
		AddError(TEXT("ThrowModel did not ask the create seam exactly once"));
	}
	TestTrue(TEXT("so the ManBat pickup word is left as it was"),
		!Bach->ManBatPickupTarget.IsSet());
	return true;
}

// -------------------------------------------------------------------------------------------------
// The `CNPC_VFrenzyShadow` / `CNPC_VPlayerController` line — `0x10376ae0`, `0x10376b10`,
// `0x10376b50`, `0x103a4950`.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageControllerLineTest,
	"Elysium.Substrate.NpcKernelDamage.PlayerControllerLine", GDamageTestFlags)
bool FElysiumNpcKernelDamageControllerLineTest::RunTest(const FString&)
{
	// Slot 300's table: three classes share `0x103a4950`, none of them spawnable, so all three are
	// exercised by retail class name.
	int32 Count = 0;
	const FElysiumNpc::FTookLifeSpecies* Rows = FElysiumNpc::TookLifeSpeciesRows(Count);
	TestEqual(TEXT("slot 300 has three species rows"), Count, 3);
	TestEqual(TEXT("row 0 is CNPC_VFrenzyShadow"), FString(Rows[0].RetailClass),
		FString(TEXT("CNPC_VFrenzyShadow")));
	TestEqual(TEXT("row 1 is CNPC_VPlayerController"), FString(Rows[1].RetailClass),
		FString(TEXT("CNPC_VPlayerController")));
	TestEqual(TEXT("row 2 is CNPC_VWolfMorph"), FString(Rows[2].RetailClass),
		FString(TEXT("CNPC_VWolfMorph")));
	for (int32 i = 0; i < Count; ++i)
	{
		TestEqual(TEXT("all three are filled by 0x103a4950"), FString(Rows[i].Body),
			FString(TEXT("0x103a4950")));
		TestNotNull(TEXT("and each is reachable by retail class name"),
			FElysiumNpc::TookLifeSpeciesOf(Rows[i].RetailClass));
	}
	TestNull(TEXT("an unrelated class has no row"),
		FElysiumNpc::TookLifeSpeciesOf(TEXT("CNPC_VHumanCombatant")));

	FElysiumNpcWorldBuilder Builder(TEXT("damage_controller"), 0x29c1d015);
	Builder.AddNpc(TEXT("shadow"));
	Builder.AddNpc(TEXT("victim"), FVector(100.0 * U, 0.0, 0.0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Shadow = Fixture.Npc(TEXT("shadow"));
	FElysiumNpc* Victim = Fixture.Npc(TEXT("victim"));
	FElysiumNpcWorldFixture::Quiet({ Shadow, Victim });
	if (Shadow == nullptr || Victim == nullptr)
	{
		AddError(TEXT("fixture did not stand both NPCs"));
		return false;
	}

	// The controller-object seam answers null, which takes the guarded arm of all four bodies.
	TestFalse(TEXT("the +0x184 controller-object seam answers nothing"),
		Shadow->HasPlayerControllerObject());

	// Both damage forwards ALWAYS answer 0, whatever the object does.
	FElysiumNpc::FElysiumTakeDamageInfo Info;
	Info.Damage = 500.f;
	TestEqual(TEXT("OnTakeDamage always answers 0"), Shadow->OnTakeDamageSpecies(&Info), 0);
	TestEqual(TEXT("OnTakeDamage_Alive always answers 0"),
		Shadow->OnTakeDamage_AliveSpecies(&Info), 0);

	// `Event_Killed` is three bytes and does nothing at all.
	Shadow->ControllerAiEvents.Reset();
	Shadow->Event_KilledSpecies(&Info);
	TestEqual(TEXT("Event_Killed writes nothing"), Shadow->ControllerAiEvents.Num(), 0);

	// `Event_TookLife` is guarded on the controller object, so it dispatches nothing here.
	Shadow->Event_TookLifeSpecies(Victim);
	TestEqual(TEXT("Event_TookLife dispatches nothing without the controller object"),
		Shadow->ControllerAiEvents.Num(), 0);
	// The literal it WOULD tag the event with is recovered and asserted by name.
	TestEqual(TEXT("the retail source literal"), FString(FElysiumNpc::TookLifeEventSource()),
		FString(TEXT("CNPC_VPlayerController::Event_TookLife")));
	return true;
}

// -------------------------------------------------------------------------------------------------
// Every seam this family stands, asked once, so the refusals are the recorded ones.
// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelDamageSeamsTest,
	"Elysium.Substrate.NpcKernelDamage.Seams", GDamageTestFlags)
bool FElysiumNpcKernelDamageSeamsTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("damage_seams"), 0x29c1d016);
	Builder.AddNpc(TEXT("npc"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Npc = Fixture.Npc(TEXT("npc"));
	FElysiumNpcWorldFixture::Quiet({ Npc });
	if (Npc == nullptr)
	{
		AddError(TEXT("fixture did not stand the NPC"));
		return false;
	}

	// The five hitgroup cvars. Their names and defaults are UNRECOVERED — no corpus function
	// constructs the pointer cells — so all five answer 0.0f, which is an unconstructed cvar's own
	// answer and what retail's inlined `IsCommand()` read would produce.
	for (int32 HitGroup = 1; HitGroup <= 7; ++HitGroup)
	{
		TestEqual(TEXT("the hitgroup damage-scale cvar is unrecovered"),
			Npc->HitGroupDamageScaleCvar(HitGroup), 0.f);
	}

	// `CVDmg_t::EvadeCheck`. This one is NOT an absence: the installed generic callback returns
	// zero, which `combat-and-damage.md` records, so false is the recovered answer.
	FElysiumDmg Dmg = MakeResolvedDmg(10, 0);
	TestFalse(TEXT("EvadeCheck answers false, which is the recovered retail answer"),
		Npc->TraceAttackEvadeCheck(&Dmg));

	// The ammo join and the rules gate.
	TestEqual(TEXT("MaxCarry is unrecovered"), Npc->AmmoMaxCarry(0), 0);
	TestTrue(TEXT("the ammo-type name join is unrecovered"),
		Npc->AmmoTypeNameForIndex(0).IsEmpty());
	TestTrue(TEXT("the ammo-enabled gate is permissive"), Npc->GameRulesAllowsAmmo(0));

	// The create seam records and refuses.
	Npc->CreateEntityCalls.Reset();
	TestFalse(TEXT("CreateNamedEntity answers an invalid handle"),
		Npc->CreateNamedEntity(TEXT("prop_physics"), FVector(1.0, 2.0, 3.0)).IsSet());
	TestEqual(TEXT("but records the request"), Npc->CreateEntityCalls.Num(), 1);

	// The emitter seam refuses an empty name and records a real one.
	Npc->EmitterCalls.Reset();
	TestEqual(TEXT("an empty emitter name creates nothing"),
		Npc->CreateNamedEmitter(FString(), FVector::ZeroVector, 0, FElysiumEntityHandle(), nullptr),
		INDEX_NONE);
	TestEqual(TEXT("and records nothing"), Npc->EmitterCalls.Num(), 0);
	TestEqual(TEXT("a real name records at index 0"),
		Npc->CreateNamedEmitter(TEXT("x"), FVector::ZeroVector, 0, FElysiumEntityHandle(), nullptr),
		0);

	// The hitbox seams.
	TestEqual(TEXT("the hitbox-set count is unrecovered"), Npc->HitboxSetCount(), 0);
	TestFalse(TEXT("and the per-hitbox ray test answers false"),
		Npc->TestOneHitbox(0, FVector::ZeroVector, FVector(1.0, 0.0, 0.0), 0));

	// `IsStandable` on another entity: an NPC answers its own slot, anything else TRUE.
	TestTrue(TEXT("a null candidate is standable, which is CBaseEntity's own answer"),
		Npc->CandidateIsStandable(nullptr));

	// The Zombie gib cvars.
	TestEqual(TEXT("the first zombie gib cvar is unrecovered"), Npc->ZombieGibAmmoTypeCvar(0), 0);
	TestEqual(TEXT("and the second"), Npc->ZombieGibAmmoTypeCvar(1), 0);

	// The controller object and the AOE result code.
	TestFalse(TEXT("the controller object is absent"), Npc->HasPlayerControllerObject());
	TestEqual(TEXT("and the AOE trace-attack result code is the default arm"),
		Npc->AoeTraceAttackResultCode(Npc), 0);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
