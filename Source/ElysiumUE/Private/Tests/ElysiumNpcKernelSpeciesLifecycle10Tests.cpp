#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMaker.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **SpeciesLifecycle10** — one case per `rule` row, twelve in all. Every
// expectation is read off the decompiled C of the body it names and, where the decompiler aliased an
// argument or mislabelled a field, off the listing: this family corrected the checklist's walk in
// eight places and each correction has a case that states the corrected fact.
//
// The suite is in four parts, matching the family's four shapes: the maker's three `Spawn` arms, the
// two species `Restore`s, the five species arms on slots another family owns, and the two
// destructors.
//
// Every species case stands its subject through `SetRetailClassForTests` and puts it back
// afterwards, because `FElysiumClassRegistry` is a process-wide singleton and a latched retail class
// that outlived its case would change the next one's answers.

static constexpr EAutomationTestFlags GSpeciesLifecycle10TestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One NPC every species arm is driven through, an `npc_VCop` control whose `RetailClass()` is
	// deliberately null, a `pillar` for the Gargoyle's touch and two bare entities standing for
	// Andrei's owned emitters.
	struct FSpeciesLifecycle10Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Species = nullptr;
		FElysiumNpc* Troika = nullptr;

		FSpeciesLifecycle10Fixture()
			: World(Build())
		{
			Species = World.Npc(TEXT("species"));
			Troika = World.Npc(TEXT("troika"));
			FElysiumNpcWorldFixture::Quiet({ Species, Troika });
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("specieslifecycle10"), 20260914);
			Builder.AddNpc(TEXT("species"), FVector(100.0, 0.0, 0.0));
			Builder.AddNpc(TEXT("troika"), FVector(200.0, 0.0, 0.0), TEXT("npc_VCop"));
			Builder.AddNpc(TEXT("partner"), FVector(300.0, 0.0, 0.0));
			// `pillar` and `central_pillar` are not registered classnames in this runtime, so each
			// stands as an inert base record — which is exactly what a `CNPC_VGargoyle` touches in
			// retail: a prop from a hierarchy the NPC census does not carry.
			Builder.AddEntity(TEXT("pillar"), TEXT("pillar1"), FVector(400.0, 0.0, 0.0));
			Builder.AddEntity(TEXT("central_pillar"), TEXT("pillar2"), FVector(500.0, 0.0, 0.0));
			Builder.AddEntity(TEXT("prop_physics"), TEXT("notapillar"), FVector(600.0, 0.0, 0.0));
			Builder.AddEntity(TEXT("prop_physics"), TEXT("bloodemitter"), FVector(700.0, 0.0, 0.0));
			Builder.AddEntity(TEXT("prop_physics"), TEXT("summonemitter"), FVector(800.0, 0.0, 0.0));
			return Builder;
		}

		FElysiumEntity* Entity(const TCHAR* Name) { return World.World.FindByName(Name); }
	};

	// The maker fixture: one of each of the two registered maker classnames.
	struct FSpeciesLifecycle10MakerFixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpcMaker* Base = nullptr;
		FElysiumNpcMaker* Fleshpile = nullptr;

		FSpeciesLifecycle10MakerFixture()
			: World(Build())
		{
			Base = Get(TEXT("maker"));
			Fleshpile = Get(TEXT("fleshmaker"));
		}

		static FElysiumNpcWorldBuilder Build()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("specieslifecycle10maker"), 20260914);
			// Far from the player, so the admission checks a `Spawn` does not run are moot.
			//
			// BOTH keyfields are authored, and they have to be: `Spawn` (`0x1034afe0`) dispatches
			// slot 104 `Precache`, whose missing-model arm `UTIL_Remove`s the maker — so a maker
			// with no `model` key does not survive its own spawn, in retail or here. That is the
			// dispatch this family added; a fixture that omitted the key would be testing the
			// removal, not the spawn.
			FElysiumEntityDef& Base =
				Builder.AddEntity(TEXT("npc_maker"), TEXT("maker"), FVector(50000.0, 0.0, 0.0));
			Base.Keys.Add(TEXT("model"), TEXT("models/maker.mdl"));
			Base.Keys.Add(TEXT("NPCType"), TEXT("npc_VCop"));
			FElysiumEntityDef& Flesh = Builder.AddEntity(TEXT("npc_maker_fleshpile"),
				TEXT("fleshmaker"), FVector(51000.0, 0.0, 0.0));
			Flesh.Keys.Add(TEXT("model"), TEXT("models/fleshpile.mdl"));
			Flesh.Keys.Add(TEXT("NPCType"), TEXT("npc_VTzimisceRunner"));
			return Builder;
		}

		FElysiumNpcMaker* Get(const TCHAR* Name)
		{
			FElysiumEntity* Entity = World.World.FindByName(Name);
			return Entity != nullptr ? static_cast<FElysiumNpcMaker*>(Entity) : nullptr;
		}
	};
}

// =================================================================================================
// Slot 103 `Spawn` — `CNPCMaker` `0x1034afe0`, `CNPCMaker_Fleshpile` `0x1034c020`,
// `CNPCMaker_Zombie` `0x1034cc60`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10MakerSpawnTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.MakerSpawn", GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10MakerSpawnTest::RunTest(const FString&)
{
	FSpeciesLifecycle10MakerFixture Fix;
	if (!TestNotNull(TEXT("the base maker spawned"), Fix.Base))
	{
		return false;
	}
	FElysiumNpcMaker& M = *Fix.Base;

	// `CNPCMaker::Spawn` `0x1034afe0`, step by step, re-run so the case owns the inputs.
	M.SpawnFrequency = 4.0f;
	M.bDisabled = false;
	M.bInfinite = false;
	M.bFade = false;
	M.LiveChildren = 7;
	M.CachedGroundZ = 123.0f;
	M.RelinkCalls = 0;
	M.PrecacheLog.Reset();
	M.Spawn();

	// 1. `1034b04c LEA ECX,[ESI + 0x270]` — `SetSolid(SOLID_NONE)` on `m_Collision`. The walk puts
	//    this at `+0x17c`, which is `m_flNextThink`.
	TestEqual(TEXT("Spawn sets SOLID_NONE on m_Collision (+0x270), not +0x17c"),
		M.RetailSolidType, 0);

	// 2/3. `m_cLiveChildren` is `+0x66b0` and is zeroed BEFORE the slot-104 dispatch; the walk has
	//      `+0x66b0` and `+0x66b8` the wrong way round.
	TestEqual(TEXT("m_cLiveChildren (+0x66b0) is zeroed"), M.LiveChildren, 0);
	TestTrue(TEXT("slot 104 Precache was dispatched — the port had no such dispatch before"),
		M.PrecacheLog.Num() > 0);

	// 5. The enabled path: the base maker's own think, and `curtime + m_flSpawnFrequency`.
	TestEqual(TEXT("the enabled path installs the base maker think (0x1034bbf0)"),
		FString(FElysiumNpcMaker::MakerThinkName(M.InstalledThink)), FString(TEXT("base")));
	TestEqual(TEXT("and stamps m_flNextThink = curtime + m_flSpawnFrequency"),
		static_cast<double>(M.NextThink), M.World->NowSeconds() + 4.0, 1e-4);

	// 6/7. Relink, then `m_flGround` (+0x66b8) last.
	TestEqual(TEXT("Relink ran once"), M.RelinkCalls, 1);
	TestEqual(TEXT("m_flGround (+0x66b8) is the LAST write"), M.CachedGroundZ, 0.0f);

	// 4. `1034b075` — infinite implies fade, and it is tested before the enabled/disabled split.
	M.bInfinite = true;
	M.bFade = false;
	M.Spawn();
	TestTrue(TEXT("m_bInfChild forces m_bFade"), M.bFade);

	// The disabled path: the inert think at `0x1000572c` -> `0x101c0b60`, a bare RET, and NO
	// next-think stamp — retail leaves `m_flNextThink` where it stood.
	M.bDisabled = true;
	M.RelinkCalls = 0;
	M.Spawn();
	TestEqual(TEXT("the disabled base maker installs the INERT think, not a null one"),
		FString(FElysiumNpcMaker::MakerThinkName(M.InstalledThink)), FString(TEXT("inert")));
	TestEqual(TEXT("a disabled maker never becomes due"), M.NextThink, ELYSIUM_NEVER_THINK);
	TestEqual(TEXT("and Relink still ran — it is on both paths"), M.RelinkCalls, 1);

	// Nothing on the base arm touches the police thresholds.
	TestEqual(TEXT("the base maker leaves m_iPLInvestigateLevel alone"), M.PlInvestigate, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10MakerSpawnFleshpileTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.MakerSpawnFleshpile",
	GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10MakerSpawnFleshpileTest::RunTest(const FString&)
{
	FSpeciesLifecycle10MakerFixture Fix;
	if (!TestNotNull(TEXT("the fleshpile maker spawned"), Fix.Fleshpile))
	{
		return false;
	}
	FElysiumNpcMaker& M = *Fix.Fleshpile;
	TestTrue(TEXT("it is the fleshpile variant"), M.IsFleshpileMaker());

	// `CNPCMaker_Fleshpile::Spawn` `0x1034c020` is byte-identical to `0x1034afe0` but for the think
	// body it installs: `0x10010dd4` -> `0x1034c8b0` instead of `0x1000696a` -> `0x1034bbf0`.
	M.SpawnFrequency = 2.5f;
	M.bDisabled = false;
	M.LiveChildren = 3;
	M.CachedGroundZ = 9.0f;
	M.RelinkCalls = 0;
	M.PrecacheLog.Reset();
	M.Spawn();

	TestEqual(TEXT("the ONE difference: the fleshpile think (0x1034c8b0)"),
		FString(FElysiumNpcMaker::MakerThinkName(M.InstalledThink)), FString(TEXT("fleshpile")));
	TestEqual(TEXT("everything else is the base body: m_cLiveChildren zeroed"), M.LiveChildren, 0);
	TestTrue(TEXT("Precache dispatched"), M.PrecacheLog.Num() > 0);
	TestEqual(TEXT("SOLID_NONE"), M.RetailSolidType, 0);
	TestEqual(TEXT("Relink once"), M.RelinkCalls, 1);
	TestEqual(TEXT("m_flGround zeroed last"), M.CachedGroundZ, 0.0f);
	TestEqual(TEXT("next think = curtime + frequency, with no jitter"),
		static_cast<double>(M.NextThink), M.World->NowSeconds() + 2.5, 1e-4);

	// The disabled path is the base's, not the zombie's: the INERT think.
	M.bDisabled = true;
	M.Spawn();
	TestEqual(TEXT("the disabled fleshpile takes the inert think"),
		FString(FElysiumNpcMaker::MakerThinkName(M.InstalledThink)), FString(TEXT("inert")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10MakerSpawnZombieTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.MakerSpawnZombie", GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10MakerSpawnZombieTest::RunTest(const FString&)
{
	FSpeciesLifecycle10MakerFixture Fix;
	if (!TestNotNull(TEXT("a maker spawned"), Fix.Base))
	{
		return false;
	}
	FElysiumNpcMaker& M = *Fix.Base;

	// **GAP, named:** `ElysiumNpcClasses.cpp` registers no `npc_maker_zombie` leaf, so no map in this
	// runtime can stand a `CNPCMaker_Zombie` and the arm is unreachable at runtime. It is ported
	// whole and driven through the same latch `SetZombieMakerForTests` already is — a body whose
	// carrier no fixture can spawn is still a body.
	M.SetZombieMakerForTests();
	TestTrue(TEXT("the latch stands this maker as CNPCMaker_Zombie"), M.IsZombieMaker());

	M.SpawnFrequency = 3.0f;
	M.bDisabled = false;
	M.LiveChildren = 5;
	M.CachedGroundZ = 77.0f;
	M.RelinkCalls = 0;
	M.PrecacheLog.Reset();
	const double Before = M.World->NowSeconds();
	M.Spawn();

	// The shared prologue.
	TestEqual(TEXT("SOLID_NONE on m_Collision"), M.RetailSolidType, 0);
	TestEqual(TEXT("m_cLiveChildren zeroed before Precache"), M.LiveChildren, 0);
	TestTrue(TEXT("Precache dispatched"), M.PrecacheLog.Num() > 0);

	// Difference one, half a: the zombie think plus a `RandomFloat(1.0, 2.0)` jitter on the stamp.
	TestEqual(TEXT("the enabled path installs the zombie think (0x1034d2d0)"),
		FString(FElysiumNpcMaker::MakerThinkName(M.InstalledThink)), FString(TEXT("zombie")));
	const double Delay = static_cast<double>(M.NextThink) - Before;
	TestTrue(FString::Printf(TEXT("the stamp carries the 1.0-2.0 jitter over the 3.0 s frequency "
		"(got %f)"), Delay), Delay >= 4.0 - 1e-3 && Delay <= 5.0 + 1e-3);

	TestEqual(TEXT("Relink ran"), M.RelinkCalls, 1);
	TestEqual(TEXT("m_flGround zeroed"), M.CachedGroundZ, 0.0f);

	// Difference two: the five police thresholds, in offset order, all 999999. Neither the jitter
	// nor these constants existed in the port before this story.
	TestEqual(TEXT("m_iPLInvestigateLevel (+0x6348)"), M.PlInvestigate, 999999);
	TestEqual(TEXT("m_iPLCriminalFleeLevel (+0x634c)"), M.PlCriminalFlee, 999999);
	TestEqual(TEXT("m_iPLCriminalAttackLevel (+0x6350)"), M.PlCriminalAttack, 999999);
	TestEqual(TEXT("m_iPLSupernaturalFleeLevel (+0x6354)"), M.PlSupernaturalFlee, 999999);
	TestEqual(TEXT("m_iPLSupernaturalAttackLevel (+0x6358)"), M.PlSupernaturalAttack, 999999);

	// Difference one, half b: the DISABLED path installs a NULL think, not the inert one, and
	// Relink plus the ground zero still run.
	M.bDisabled = true;
	M.RelinkCalls = 0;
	M.CachedGroundZ = 55.0f;
	M.Spawn();
	TestEqual(TEXT("the disabled zombie maker installs a NULL think (ThinkSet(0))"),
		FString(FElysiumNpcMaker::MakerThinkName(M.InstalledThink)), FString(TEXT("none")));
	TestEqual(TEXT("and Relink still runs on the disabled path"), M.RelinkCalls, 1);
	TestEqual(TEXT("and m_flGround is still zeroed"), M.CachedGroundZ, 0.0f);
	return true;
}

// =================================================================================================
// Slot 127 `Restore` — `CNPC_VAndreiBlood` `0x1035cf80` and `CNPC_VChangBros` `0x1036b170`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10AndreiBloodRestoreTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.AndreiBloodRestore",
	GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10AndreiBloodRestoreTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// The census row: `CNPC_VAndreiBlood#127` is `0x1035cf80`, and family SaveRestore10's arm table
	// now names this body rather than routing the row to the base.
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(TEXT("CNPC_VAndreiBlood"));
	if (TestNotNull(TEXT("the census carries CNPC_VAndreiBlood"), Cls))
	{
		TestEqual(TEXT("its slot 127 is 0x1035cf80"),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 127)), FString(TEXT("0x1035cf80")));
	}

	// `0x1035cf80` has ONE call between its scope-frame push and pop, and it is
	// `CNPC_VVampireBoss::Restore`. So the whole observable body is the base's post-load reset, and
	// this species row carries no restore-time datum of its own. That is the recovered fact — an
	// EMPTY row, not an unwalked body.
	N.SetRetailClassForTests(TEXT("CNPC_VAndreiBlood"));
	N.VampireBossMonsterModelName = TEXT("models/character/monster/andrei.mdl");
	N.VampireBossMonsterClassname = TEXT("npc_VAndreiBlood");
	N.BodyEmitterNames[0] = TEXT("andrei_powerup_emitter");
	N.JumpGravity = 9.0f;
	const int32 Result = N.Restore(nullptr);

	TestEqual(TEXT("the row answers the base body's result verbatim"), Result, 1);
	TestTrue(TEXT("the base ran: m_pMonsterModelName is nulled"),
		N.VampireBossMonsterModelName.IsEmpty());
	TestTrue(TEXT("the base ran: ClearBodyEmitterNames"), N.BodyEmitterNames[0].IsEmpty());
	TestEqual(TEXT("the base ran: m_pszMonsterClassname reset to the literal"),
		N.VampireBossMonsterClassname, FString(TEXT("npc_VVampireBoss")));
	// And nothing of its own: Andrei's sibling bosses all write a jump gravity here; he does not.
	TestEqual(TEXT("and Andrei's row writes NO datum of its own — m_fJumpGravity is untouched"),
		N.JumpGravity, 9.0f);

	N.SetRetailClassForTests(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10ChangBrosRestoreTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.ChangBrosRestore", GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10ChangBrosRestoreTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// One body, three classes — `CNPC_VChangBros`, `CNPC_VChangBrosBlade` and `CNPC_VChangBrosClaw`
	// all carry `0x1036b170` at slot 127.
	const TCHAR* const Brothers[] = {
		TEXT("CNPC_VChangBros"), TEXT("CNPC_VChangBrosBlade"), TEXT("CNPC_VChangBrosClaw") };
	for (const TCHAR* Brother : Brothers)
	{
		const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(Brother);
		if (!TestNotNull(FString::Printf(TEXT("the census carries %s"), Brother), Cls))
		{
			continue;
		}
		TestEqual(FString::Printf(TEXT("%s's slot 127 is the shared 0x1036b170"), Brother),
			FString(ElysiumNpcKernelClass::BodyOf(Cls, 127)), FString(TEXT("0x1036b170")));

		N.SetRetailClassForTests(Brother);
		N.VampireBossMonsterModelName = TEXT("models/character/monster/chang.mdl");
		N.JumpGravity = 0.f;
		N.BodyEmitterNames[0] = FString();
		N.BodyEmitterNames[1] = FString();
		N.BodyEmitterNames[2] = FString();
		N.BodyEmitterNames[3] = TEXT("untouched");

		const int32 Result = N.Restore(nullptr);

		// `1036b1e3 MOV EDI,EAX` … `1036b210 MOV EAX,EDI` — the base's answer is kept and returned.
		TestEqual(FString::Printf(TEXT("%s returns the base's result"), Brother), Result, 1);
		TestTrue(FString::Printf(TEXT("%s ran the base reset"), Brother),
			N.VampireBossMonsterModelName.IsEmpty());
		// `_DAT_104ada44` = 2.3f, read at file offset 0x4ada44 of the pinned vampire.dll.
		TestEqual(FString::Printf(TEXT("%s sets m_fJumpGravity (+0x64b8) = 2.3f"), Brother),
			N.JumpGravity, 2.3f);
		// Indices 0 and 1 BOTH take the powerup emitter; index 2 the spine one; index 3 is never
		// written by this body, and `ClearBodyEmitterNames` inside the base is what empties it.
		TestEqual(FString::Printf(TEXT("%s emitter 0"), Brother), N.BodyEmitterNames[0],
			FString(TEXT("chang_powerup_emitter")));
		TestEqual(FString::Printf(TEXT("%s emitter 1 is the SAME name as 0"), Brother),
			N.BodyEmitterNames[1], FString(TEXT("chang_powerup_emitter")));
		TestEqual(FString::Printf(TEXT("%s emitter 2"), Brother), N.BodyEmitterNames[2],
			FString(TEXT("chang_spine_emitter")));
		TestTrue(FString::Printf(TEXT("%s writes no emitter 3 (the base cleared it)"), Brother),
			N.BodyEmitterNames[3].IsEmpty());
	}

	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// Slot 431 `NPCThink` — `CPayphone` `0x101aabf0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10PayphoneThinkTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.PayphoneThink", GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10PayphoneThinkTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species)
		|| !TestNotNull(TEXT("the control spawned"), Fix.Troika))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	FElysiumNpc* Partner = Fix.World.Npc(TEXT("partner"));
	if (!TestNotNull(TEXT("the partner spawned"), Partner))
	{
		return false;
	}

	// A plain NPC does not take the arm at all — the prologue answers false and the Troika body runs.
	TestFalse(TEXT("a non-payphone does not take slot 431's species arm"), N.PayphoneThink());

	N.SetRetailClassForTests(TEXT("CPayphone"));
	const double Now = N.World->NowSeconds();

	// --- Arm 3: no partner. This is the arm the `m_hDialogPartner` seam answers today. ------------
	N.DialogPartner = FElysiumEntityHandle();
	N.Dialogue.bInDialog = false;
	N.DialogUpkeepTicks = 0;
	N.IdealActivityNumber = 0x40;
	N.NextThink = 0.f;
	TestTrue(TEXT("a payphone's slot 431 owns the whole pass"), N.PayphoneThink());
	TestEqual(TEXT("IsInDialog false: the dialogue tick does NOT run"), N.DialogUpkeepTicks, 0);
	TestEqual(TEXT("SetIdealActivity(1 = ACT_IDLE) runs UNCONDITIONALLY, outside the IsInDialog test"),
		N.IdealActivityNumber, 1);
	TestEqual(TEXT("and the next think is curtime + _DAT_1044bef8 (0.25 s)"),
		static_cast<double>(N.NextThink), Now + 0.25, 1e-4);
	TestEqual(TEXT("the idle pass is counted"), N.PayphoneIdlePasses, 1);

	// The same arm with a dialogue open: only the tick changes.
	N.Dialogue.bInDialog = true;
	N.DialogUpkeepTicks = 0;
	N.PayphoneThink();
	TestEqual(TEXT("IsInDialog true: the tick runs on the no-partner arm too"),
		N.DialogUpkeepTicks, 1);
	N.Dialogue.bInDialog = false;

	// --- Arm 2: a live partner. Driven through the seam's handle, which nothing else writes. -----
	Partner->IdealActivityNumber = 0x21;
	Partner->SequenceCycle = 0.375f;
	N.DialogPartner = Partner->Handle;
	N.IdealActivityNumber = 0x11;
	N.SequenceCycle = 0.f;
	N.DialogUpkeepTicks = 0;
	N.NextThink = 0.f;
	TestTrue(TEXT("the partner arm owns the pass"), N.PayphoneThink());

	TestEqual(TEXT("the dialogue tick runs UNCONDITIONALLY on the partner arm"),
		N.DialogUpkeepTicks, 1);
	// `+0xff0` is `m_IdealActivity`, not `m_Activity` — the walk names the wrong field.
	TestEqual(TEXT("the partner's m_IdealActivity (+0xff0) is adopted"),
		N.IdealActivityNumber, 0x21);
	// `+0x6f8` is `m_flCycle`: the mirror is frame-accurate, which is why it needs a 0.01 s clock.
	TestEqual(TEXT("the partner's m_flCycle (+0x6f8) is copied"), N.SequenceCycle, 0.375f);
	TestEqual(TEXT("the next think is curtime + _DAT_10450aa4 (0.01 s), a 100 Hz tick"),
		static_cast<double>(N.NextThink), Now + 0.009999999776482582, 1e-6);
	TestEqual(TEXT("the mirror pass is counted"), N.PayphoneMirrorPasses, 1);
	TestEqual(TEXT("and the idle arm did NOT run — the partner arm returns"),
		N.PayphoneIdlePasses, 2);

	// The activity is re-resolved only on a CHANGE; the cycle is copied every pass.
	Partner->SequenceCycle = 0.5f;
	N.SequenceNumber = 4242;
	N.PayphoneThink();
	TestEqual(TEXT("an unchanged activity does not re-select the sequence"), N.SequenceNumber, 4242);
	TestEqual(TEXT("but the cycle is copied again"), N.SequenceCycle, 0.5f);

	// The control: an `npc_VCop`, whose `RetailClass()` is deliberately null.
	TestFalse(TEXT("the npc_VCop control never takes the payphone arm"),
		Fix.Troika->PayphoneThink());

	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// Slot 174 `StartTouch` — `CBaseEntity` `0x100a49d0` and `CNPC_VGhoulCroucher` `0x1037bf60`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10GhoulStartTouchTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.GhoulCroucherStartTouch",
	GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10GhoulStartTouchTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	FElysiumPlayer* Player = Fix.World.Player();
	FElysiumNpc* OtherNpc = Fix.World.Npc(TEXT("partner"));
	FElysiumEntity* Prop = Fix.Entity(TEXT("notapillar"));
	if (!TestNotNull(TEXT("the player stands"), Player) || !TestNotNull(TEXT("a prop stands"), Prop)
		|| !TestNotNull(TEXT("another NPC stands"), OtherNpc))
	{
		return false;
	}

	// `CBaseEntity::StartTouch` `0x100a49d0` first: the whole base body is the `m_pParent` forward,
	// and with no parent it does nothing at all.
	N.ParentTouchPropagations = 0;
	N.BaseEntityStartTouch(Prop);
	TestEqual(TEXT("the base StartTouch with no m_pParent forwards nothing"),
		N.ParentTouchPropagations, 0);
	N.MoveParent = OtherNpc->Handle;
	N.BaseEntityStartTouch(Prop);
	TestEqual(TEXT("with a parent it forwards the toucher to the parent's slot 174"),
		N.ParentTouchPropagations, 1);
	N.MoveParent = FElysiumEntityHandle();

	N.SetRetailClassForTests(TEXT("CNPC_VGhoulCroucher"));

	// Step 2's gate: `+0xa8` is `m_pPlayer` (the toucher IS the player) and `+0x94` is the cached
	// `CAI_BaseNPC*` (the toucher IS an NPC). The walk leaves both offsets unnamed. `OnDisturbed`
	// (`0x1037b6e0`) is a once-latch, so each probe clears `m_bWasDisturbed` first.
	N.bWasDisturbed = false;
	N.StartTouchSpecies(Prop);
	TestFalse(TEXT("a prop is neither a player nor an NPC: OnDisturbed does not run"),
		N.IsDisturbed());

	N.bWasDisturbed = false;
	N.StartTouchSpecies(OtherNpc);
	TestTrue(TEXT("an NPC toucher (+0x94, the cached CAI_BaseNPC*) disturbs"), N.IsDisturbed());

	N.bWasDisturbed = false;
	N.StartTouchSpecies(Player);
	TestTrue(TEXT("a player toucher (+0xa8, m_pPlayer) disturbs"), N.IsDisturbed());

	// DIVERGENCE, named in the `.inl`: retail dereferences a NULL toucher at `1037bfe7` and faults.
	// This port refuses the arm; nothing in the world can produce a null toucher.
	N.bWasDisturbed = false;
	N.StartTouchSpecies(nullptr);
	TestFalse(TEXT("a null toucher refuses the arm (retail faults here)"), N.IsDisturbed());

	// Step 3: the burn. All three terms are required.
	const double Now = N.World->NowSeconds();
	//
	// The burn ITSELF is family SpeciesMisc10's `BurnPlayer` (`0x1037c090`), reached here with 5.0.
	// The read side is that family's `BurnHitboxCalls`, plus the timer this row owns: with no studio
	// header the hitbox loop makes no passes, so the RE-ARM STAMP is what says the arm fired.
	N.bGhoulSpawnBurning = false;
	N.GhoulNextTouchBurnTime = 0.0;
	N.StartTouchSpecies(Player);
	TestEqual(TEXT("m_bSpawnBurning clear: the timer is not restamped"),
		N.GhoulNextTouchBurnTime, 0.0);

	N.bGhoulSpawnBurning = true;
	N.GhoulNextTouchBurnTime = Now + 100.0;
	N.StartTouchSpecies(Player);
	TestEqual(TEXT("m_flNextTouchBurnTime ahead of curtime: no burn, no restamp"),
		N.GhoulNextTouchBurnTime, Now + 100.0);

	N.GhoulNextTouchBurnTime = Now - 1.0;
	N.StartTouchSpecies(Player);
	// `_DAT_1044ffd0` is a DOUBLE and reads 5.0 (file offset 0x44ffd0).
	TestEqual(TEXT("burning, a player toucher and an expired timer: restamped to curtime + 5.0"),
		N.GhoulNextTouchBurnTime, Now + 5.0, 1e-6);

	// An NPC toucher entered step 2 through `+0x94` and carries a null `+0xa8`, so it cannot burn.
	N.GhoulNextTouchBurnTime = Now - 1.0;
	N.StartTouchSpecies(OtherNpc);
	TestEqual(TEXT("an NPC toucher never burns — the burn term is the +0xa8 player pointer"),
		N.GhoulNextTouchBurnTime, Now - 1.0);

	// The touch burn is 5.0; this class's own `OnVictimHitByMe` (`0x1037be80`) pushes 0x41200000 =
	// 10.0 to the SAME body. Standing in the ghoul's fire hurts half as much as being hit by it.
	TestEqual(TEXT("the touch burn is 5.0, against OnVictimHitByMe's 10.0"),
		FElysiumNpc::GhoulTouchBurnDamage, 5.0f);

	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// Slot 175 `Touch` — `CBaseEntity` `0x100a4af0` and `CNPC_VGargoyle` `0x1037a270`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10GargoyleTouchTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.GargoyleTouch", GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10GargoyleTouchTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	FElysiumEntity* Pillar = Fix.Entity(TEXT("pillar1"));
	FElysiumEntity* Central = Fix.Entity(TEXT("pillar2"));
	FElysiumEntity* Prop = Fix.Entity(TEXT("notapillar"));
	if (!TestNotNull(TEXT("a pillar stands"), Pillar)
		|| !TestNotNull(TEXT("a central_pillar stands"), Central)
		|| !TestNotNull(TEXT("a non-pillar prop stands"), Prop))
	{
		return false;
	}

	// `CBaseEntity::Touch` `0x100a4af0`: `m_pfnTouch` FIRST, the parent forward second. The order is
	// the fact; both are seams here.
	N.TouchFunctionCalls = 0;
	N.ParentTouchPropagations = 0;
	N.BaseEntityTouch(Prop);
	TestEqual(TEXT("the base Touch always consults m_pfnTouch"), N.TouchFunctionCalls, 1);
	TestEqual(TEXT("and with no parent forwards nothing"), N.ParentTouchPropagations, 0);

	N.SetRetailClassForTests(TEXT("CNPC_VGargoyle"));

	// The classname filter is the SAME predicate this class's slot-24 body uses; family Misc's
	// `GargoyleHitsPillar` is called, not restated.
	TestTrue(TEXT("pillar matches"), FElysiumNpc::GargoyleHitsPillar(TEXT("pillar")));
	TestTrue(TEXT("central_pillar matches"), FElysiumNpc::GargoyleHitsPillar(TEXT("central_pillar")));
	TestTrue(TEXT("the compare is case-insensitive"), FElysiumNpc::GargoyleHitsPillar(TEXT("PILLAR")));
	TestFalse(TEXT("and whole-name: neither literal carries a trailing star"),
		FElysiumNpc::GargoyleHitsPillar(TEXT("pillar_broken")));

	// A non-pillar toucher: no packet, and the base body still runs.
	N.GargoylePillarHits.Reset();
	N.TouchFunctionCalls = 0;
	N.TouchSpecies(Prop);
	TestEqual(TEXT("a non-pillar toucher takes no damage"), N.GargoylePillarHits.Num(), 0);
	TestEqual(TEXT("but BOTH paths end in CBaseEntity::Touch"), N.TouchFunctionCalls, 1);

	// A `pillar`: the packet, verbatim.
	N.TouchSpecies(Pillar);
	if (!TestEqual(TEXT("a pillar toucher takes one packet"), N.GargoylePillarHits.Num(), 1))
	{
		N.SetRetailClassForTests(nullptr);
		return false;
	}
	const FElysiumNpc::FGargoylePillarHit& Hit = N.GargoylePillarHits[0];
	// `1037a38c PUSH 0x1 / 1037a385 PUSH 0x80 / 1037a383 PUSH 0xa` — `Set(family, dmgTypes, dice)`.
	TestEqual(TEXT("CVDmg_t family 1 (Lethal)"), Hit.Family, 1);
	TestEqual(TEXT("m_bdmgTypes 0x80 (DMG_CLUB)"), static_cast<int32>(Hit.DamageTypes), 0x80);
	TestEqual(TEXT("m_iDiceAmt 10"), Hit.DiceAmount, 10);
	// `1037a3ab MOV dword ptr [ESP+0x34],0x1` — the CVDmg is at ESP+0x18 there, so the offset is
	// +0x0c, `m_iToHitSuccesses`.
	TestEqual(TEXT("m_iToHitSuccesses forced to 1"), Hit.ToHitSuccesses, 1);
	TestEqual(TEXT("the packet's scale is 1.0"), Hit.Damage, 1.0f);
	// `1037a3a7 PUSH ESI` is argument 0 of `0x101c26d0` — the INFLICTOR is the pillar itself, not
	// the gargoyle. Retail's, and reproduced.
	TestTrue(TEXT("the inflictor is the pillar itself"), Hit.Pillar == Pillar->Handle);
	// Slot 142 is dispatched ON THE PILLAR, which in this runtime carries no NPC leaf.
	TestFalse(TEXT("a pillar carries no slot-142 body here, so the dispatch is recorded only"),
		Hit.bDispatched);
	TestEqual(TEXT("and the base Touch ran after it"), N.TouchFunctionCalls, 2);

	// `central_pillar` takes the same arm — it is the second compare, not a different rule.
	N.TouchSpecies(Central);
	TestEqual(TEXT("central_pillar takes the same packet"), N.GargoylePillarHits.Num(), 2);

	// The control.
	Fix.Troika->GargoylePillarHits.Reset();
	Fix.Troika->TouchSpecies(Pillar);
	TestEqual(TEXT("an npc_VCop touching a pillar does nothing to it"),
		Fix.Troika->GargoylePillarHits.Num(), 0);

	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// Slot 463 `OnStateChange` — the `CNPC_VGuard1` `0x1037d020` and `CNPC_VHunter` `0x10388880`
// pre-steps.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10Guard1StateChangeTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.Guard1StateChange",
	GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10Guard1StateChangeTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	FElysiumNpc* OtherNpc = Fix.World.Npc(TEXT("partner"));
	if (!TestNotNull(TEXT("another NPC stands"), OtherNpc))
	{
		return false;
	}

	// vtable `+0x29c` is slot 167, and slot 167's body is `CAI_BaseNPC::FUN_101a67e0` — whose whole
	// text resolves `m_hEnemy`. **The checklist's walk calls it "a door reference resolved twice"
	// and `0x1037e2d0` "an obstructing-door hook"; there is no door in either body.**
	N.Senses.Memory.Enemy = FElysiumEntityHandle();
	TestNull(TEXT("GetEnemy() with no enemy answers null"), N.GetEnemyEntity());
	N.Senses.Memory.Enemy = OtherNpc->Handle;
	TestTrue(TEXT("GetEnemy() resolves m_hEnemy"),
		N.GetEnemyEntity() == static_cast<FElysiumEntity*>(OtherNpc));

	N.SetRetailClassForTests(TEXT("CNPC_VGuard1"));

	// No enemy at all: the arm does nothing, whatever the states.
	N.Senses.Memory.Enemy = FElysiumEntityHandle();
	N.PlayerHateRelationshipSets = 0;
	N.bGuard1HatesPlayer = false;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestEqual(TEXT("no enemy: nothing"), N.PlayerHateRelationshipSets, 0);

	// An enemy that is not the player: still nothing — the second term is `enemy->m_pPlayer`.
	N.Senses.Memory.Enemy = OtherNpc->Handle;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestEqual(TEXT("an enemy that is not the player: nothing"), N.PlayerHateRelationshipSets, 0);

	// The player as enemy: `0x1037e2d0` — the latch byte at `+0x6660` AND
	// `InputSetRelationship("player D_HT 10")`.
	N.Senses.Memory.Enemy = N.World->PlayerHandle();
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestEqual(TEXT("my enemy is the player: the relationship is set"),
		N.PlayerHateRelationshipSets, 1);
	TestTrue(TEXT("and the +0x6660 latch is raised — Guard1's body writes it, the Hunter's does not"),
		N.bGuard1HatesPlayer);
	TestEqual(TEXT("the literal is 0x1063bc28, 'player D_HT 10' with spaces"),
		FString(FElysiumNpc::PlayerHateRelationshipSpec()), FString(TEXT("player D_HT 10")));

	// The arm is UNCONDITIONAL on the states: retail tests neither `param_1` nor `param_2` for it.
	N.PlayerHateRelationshipSets = 0;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Dead, EElysiumNpcState::Idle);
	TestEqual(TEXT("the Guard1 arm does not look at either state"), N.PlayerHateRelationshipSets, 1);

	// And it runs from slot 463's own dispatch, ahead of the shared holster switch.
	N.PlayerHateRelationshipSets = 0;
	N.OnStateChange(EElysiumNpcState::Idle, EElysiumNpcState::Alert);
	TestEqual(TEXT("OnStateChange reaches the pre-step"), N.PlayerHateRelationshipSets, 1);

	// A class in the same HolsterOnState row with no pre-step of its own runs none.
	N.SetRetailClassForTests(TEXT("CNPC_VHumanCombatant"));
	N.PlayerHateRelationshipSets = 0;
	N.OnStateChange(EElysiumNpcState::Idle, EElysiumNpcState::Alert);
	TestEqual(TEXT("a holster class with no pre-step writes nothing"),
		N.PlayerHateRelationshipSets, 0);

	N.SetRetailClassForTests(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10HunterStateChangeTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.HunterStateChange",
	GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10HunterStateChangeTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	FElysiumNpc* OtherNpc = Fix.World.Npc(TEXT("partner"));
	if (!TestNotNull(TEXT("another NPC stands"), OtherNpc))
	{
		return false;
	}

	FElysiumPlayer* Player = Fix.World.Player();
	if (!TestNotNull(TEXT("the player stands"), Player))
	{
		return false;
	}
	N.SetRetailClassForTests(TEXT("CNPC_VHunter"));
	// The count itself is the PLAYER's `+0x1d14` — this runtime's
	// `FElysiumPoliceState::HuntersInPursuit`, which family Conditions' receiver correction put
	// there. Rebased so this case does not depend on what ran before it.
	Player->Police.HuntersInPursuit = 0;

	// Arm 1 needs all four terms: an enemy, that enemy being the player, and the NEW state being
	// COMBAT (retail's `m_NPCState` id 2).
	N.Senses.Memory.Enemy = OtherNpc->Handle;
	N.HunterPursuitStarts = 0;
	N.PlayerHateRelationshipSets = 0;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestEqual(TEXT("an enemy that is not the player does not start a pursuit"),
		N.HunterPursuitStarts, 0);

	N.Senses.Memory.Enemy = N.World->PlayerHandle();
	N.bGuard1HatesPlayer = false;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Idle, EElysiumNpcState::Alert);
	TestEqual(TEXT("the player as enemy but NewState != COMBAT does not start a pursuit"),
		N.HunterPursuitStarts, 0);

	N.StateChangeSpeciesPreStep(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestEqual(TEXT("entering COMBAT on the player starts the pursuit"), N.HunterPursuitStarts, 1);
	TestEqual(TEXT("and 0x10388c40 sets the relationship — WITHOUT the Guard1 latch byte"),
		N.PlayerHateRelationshipSets, 1);
	TestFalse(TEXT("the Hunter's body does not write +0x6660"), N.bGuard1HatesPlayer);
	TestTrue(TEXT("m_hPursuitPlayer (+0x6664) caches the player"),
		N.HunterPursuitPlayer == N.World->PlayerHandle());
	TestEqual(TEXT("the player-side refcount rose"), Player->Police.HuntersInPursuit, 1);

	// Arm 2: leaving COMBAT releases it. The clear comes first, the release second.
	N.HunterPursuitStops = 0;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Alert, EElysiumNpcState::Idle);
	TestEqual(TEXT("an OldState that is not COMBAT does not release"), N.HunterPursuitStops, 0);

	N.StateChangeSpeciesPreStep(EElysiumNpcState::Combat, EElysiumNpcState::Idle);
	TestEqual(TEXT("leaving COMBAT releases the pursuit"), N.HunterPursuitStops, 1);
	TestFalse(TEXT("and m_hPursuitPlayer is cleared"), N.HunterPursuitPlayer.IsSet());
	TestEqual(TEXT("the player-side refcount fell back"), Player->Police.HuntersInPursuit, 0);

	// A second release with no cached player does nothing: the handle term gates it.
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Combat, EElysiumNpcState::Idle);
	TestEqual(TEXT("a release with no cached pursuit does nothing"), N.HunterPursuitStops, 1);
	TestEqual(TEXT("so the refcount does not go negative here"),
		Player->Police.HuntersInPursuit, 0);

	// Both arms can fire on one call — retail evaluates arm 2 against the handle arm 1 just wrote.
	N.HunterPursuitStarts = 0;
	N.HunterPursuitStops = 0;
	N.StateChangeSpeciesPreStep(EElysiumNpcState::Combat, EElysiumNpcState::Combat);
	TestEqual(TEXT("COMBAT -> COMBAT acquires..."), N.HunterPursuitStarts, 1);
	TestEqual(TEXT("...and releases on the same call, which is retail's own order"),
		N.HunterPursuitStops, 1);
	TestEqual(TEXT("net refcount is unchanged"), Player->Police.HuntersInPursuit, 0);

	// And the pre-step is reached from slot 463's dispatch.
	N.HunterPursuitStarts = 0;
	N.OnStateChange(EElysiumNpcState::Idle, EElysiumNpcState::Combat);
	TestEqual(TEXT("OnStateChange reaches the Hunter pre-step"), N.HunterPursuitStarts, 1);

	Player->Police.HuntersInPursuit = 0;
	N.HunterPursuitPlayer = FElysiumEntityHandle();
	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// The two destructors — `CNPC_VAndreiBlood` `0x1035cd00` and `CNPC_VWerewolf` `0x103ca7c0`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10AndreiBloodDestroyTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.DestroyAndreiBlood",
	GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10AndreiBloodDestroyTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;
	FElysiumEntity* Blood = Fix.Entity(TEXT("bloodemitter"));
	FElysiumEntity* Summon = Fix.Entity(TEXT("summonemitter"));
	if (!TestNotNull(TEXT("the blood emitter stands"), Blood)
		|| !TestNotNull(TEXT("the summon emitter stands"), Summon))
	{
		return false;
	}

	// The walk calls the two handles "the two owned entity handles that sit past the class tail"
	// because the decompiler renders them as `this + 1` and `this[1].field_0x4`. The listing
	// (`1035cd62`, `1035cdd3`) gives `+0x66e0` and `+0x66e4`, and family Damage had already
	// recovered both — `0x1035e1a0 StartBloodEmitter` owns the first, `0x1035e3c0
	// StartSummonEmitter` the second.
	N.AndreiBloodEmitter = Blood->Handle;
	N.AndreiSummonEmitter = Summon->Handle;
	N.OutputListDestroys = 0;

	N.DestroyAndreiBlood();

	TestTrue(TEXT("the blood emitter (+0x66e0) is removed"), Blood->IsDead());
	TestTrue(TEXT("the summon emitter (+0x66e4) is removed"), Summon->IsDead());
	TestFalse(TEXT("and its handle is written back invalid"), N.AndreiBloodEmitter.IsSet());
	TestFalse(TEXT("likewise the summon handle"), N.AndreiSummonEmitter.IsSet());
	// `1035ce42 LEA ECX,[ESI + 0x6664]` — `m_OnTransformComplete`, exactly one list.
	TestEqual(TEXT("exactly one output list is torn down: m_OnTransformComplete (+0x6664)"),
		N.OutputListDestroys, 1);

	// An unset handle takes neither arm, and retail writes the invalid handle ONLY on the arm it
	// removed on — so a stale handle is left as it stands.
	N.AndreiBloodEmitter = FElysiumEntityHandle();
	N.AndreiSummonEmitter = FElysiumEntityHandle();
	N.OutputListDestroys = 0;
	N.DestroyAndreiBlood();
	TestEqual(TEXT("with no emitters the body still tears the output list down"),
		N.OutputListDestroys, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelSpeciesLifecycle10WerewolfDestroyTest,
	"Elysium.Substrate.NpcKernelSpeciesLifecycle10.DestroyWerewolf", GSpeciesLifecycle10TestFlags)
bool FElysiumNpcKernelSpeciesLifecycle10WerewolfDestroyTest::RunTest(const FString&)
{
	FSpeciesLifecycle10Fixture Fix;
	if (!TestNotNull(TEXT("the subject spawned"), Fix.Species))
	{
		return false;
	}
	FElysiumNpc& N = *Fix.Species;

	// The one thing outside the object the body touches: `DAT_1093fac4` and the `werewolf_show_debug`
	// ConVar behind it, both driven to 0.
	FElysiumNpc::WerewolfShowDebug() = 1;
	N.OutputListDestroys = 0;

	// `+0x6714` / `+0x6720` is the hint-data array, not an unnamed vector: `InitializeHintData`
	// (`0x103d7710`) fills it and four `GetHint*` bodies read it, and family Hints already carries it
	// as `WerewolfHintGroundpoints` with the 0x48-byte record the walk quotes.
	N.WerewolfHintGroundpoints.SetNum(3);

	N.DestroyWerewolf();

	TestEqual(TEXT("werewolf_show_debug is driven back to 0"), FElysiumNpc::WerewolfShowDebug(), 0);
	// The five outputs: `m_OnTeleportIn`, `m_OnTeleportOut`, `m_OnFinishCrushAnimation`,
	// `m_OnBeginCrushAnimation`, `m_OnConditionDeathTriggered`.
	TestEqual(TEXT("five output lists are torn down"), N.OutputListDestroys, 5);
	TestEqual(TEXT("the hint-data vector is emptied"), N.WerewolfHintGroundpoints.Num(), 0);
	// The walk is BACKWARDS from `count - 1`, which is the recovered order and not a `Reset()`.
	if (TestEqual(TEXT("and three records were visited"), N.WerewolfHintTeardownOrder.Num(), 3))
	{
		TestEqual(TEXT("descending: 2, 1, 0"),
			FString::Printf(TEXT("%d,%d,%d"), N.WerewolfHintTeardownOrder[0],
				N.WerewolfHintTeardownOrder[1], N.WerewolfHintTeardownOrder[2]),
			FString(TEXT("2,1,0")));
	}

	// An empty vector takes the loop zero times and still resets the debug pair.
	FElysiumNpc::WerewolfShowDebug() = 1;
	N.OutputListDestroys = 0;
	N.DestroyWerewolf();
	TestEqual(TEXT("an empty hint array visits nothing"), N.WerewolfHintTeardownOrder.Num(), 0);
	TestEqual(TEXT("the debug reset is unconditional"), FElysiumNpc::WerewolfShowDebug(), 0);
	TestEqual(TEXT("and the five outputs go either way"), N.OutputListDestroys, 5);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
