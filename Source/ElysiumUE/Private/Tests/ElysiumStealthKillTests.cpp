// Content-free Substrate automation for `CStealthKillRules::FindVictim` `0x101be1f0`.
//
// Every arm is a recovered gate from `docs/vtmb/stealth.md` § "Victim selection and per-frame
// cache". The headless trace is the NPC standing hull, because a `-nullrhi` run has no collision
// world; `TracePlayerSolid` is the live-map seam.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSheetSlots.h"
#include "Misc/ScopeExit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumStealthKillRules.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumStealthKillTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	float Cm(float Units) { return Units * ElysiumMove::U; }

	FElysiumFeatTable MakeFeats()
	{
		FElysiumFeatTable Feats;
		for (int32 Index = 0; Index <= 10; ++Index)
		{
			FElysiumFeat Feat;
			Feat.InternalName = FString::Printf(TEXT("stealth_kill_feat_%d"), Index);
			Feat.MaxValue = 20;
			if (Index == 1)
			{
				Feat.InternalName = TEXT("Sneaking");
				Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Dexterity")));
			}
			if (Index == 9)
			{
				Feat.InternalName = TEXT("Close_Combat_Brawl");
				Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Strength")));
			}
			if (Index == 10)
			{
				Feat.InternalName = TEXT("Close_Combat_Melee");
				Feat.Bases.Add(FElysiumTraitRef::Parse(TEXT("Dexterity")));
			}
			Feats.Feats.Add(Feat);
		}
		Feats.Reindex();
		return Feats;
	}

	FElysiumItemTable MakeItems()
	{
		FElysiumItemTable Items;
		auto AddWeapon = [&Items](const TCHAR* Name, EElysiumItemType Type, const TCHAR* Dmg)
		{
			FElysiumItemDef Item;
			Item.Classname = Name;
			Item.Type = Type;
			FElysiumWeaponMode Mode;
			Mode.Tag = TEXT("Primary");
			Mode.Type = EElysiumWeaponModeType::Attack;
			Mode.Dmg = Dmg;
			Item.Modes.Add(Mode);
			Items.Items.Add(Item);
		};
		AddWeapon(TEXT("item_w_sk_fists"), EElysiumItemType::WeaponMelee,
			TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"));
		AddWeapon(TEXT("item_w_sk_iron"), EElysiumItemType::WeaponMelee,
			TEXT("2 Lethal Close_Combat_Melee DMG_SLASH"));
		AddWeapon(TEXT("item_w_sk_pistol"), EElysiumItemType::WeaponFirearm,
			TEXT("2 Lethal Firearms DMG_BULLET"));
		Items.Reindex();
		return Items;
	}

	struct FKillFixture
	{
		FElysiumRecordingServices Services;
		FElysiumEntityWorld World;
		FElysiumNpc* Guard = nullptr;
		FElysiumPlayer* Player = nullptr;
		FElysiumStealthKillRules Rules;
		FElysiumFeatTable Feats;
		FElysiumItemTable Items;

		explicit FKillFixture(const TCHAR* DialogName = nullptr, bool bTutorialMaker = false)
			: World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumRng::SeedAll(0x534B494C);
			Services.bNpcActivitiesResolve = true;
			Services.ResolvedNpcActivityLabel = TEXT("stealth_pair_test");
			Services.ResolvedNpcActivityOwner = TEXT("stealth_bank_test");
			Feats = MakeFeats();
			Items = MakeItems();
			PreviousTables = ElysiumSheetRules::BoundTables();
			auto Bound = PreviousTables;
			Bound.Feats = &Feats;
			ElysiumSheetRules::BindTables(Bound);
			ElysiumItems::Install(Items);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__stealthkill_test__");
			FElysiumEntityDef GuardDef;
			GuardDef.Classname = TEXT("npc_VHumanCombatant");
			GuardDef.TargetName = TEXT("guard");
			GuardDef.Origin = FVector::ZeroVector;
			if (bTutorialMaker)
			{
				GuardDef.Classname = TEXT("npc_maker");
				GuardDef.TargetName = TEXT("stealth_victim_maker");
				GuardDef.Keys.Add(TEXT("NPCType"), TEXT("npc_VVampire"));
				GuardDef.Keys.Add(TEXT("NPCTargetname"), TEXT("guard"));
				GuardDef.Keys.Add(TEXT("Flag_StartDisabled"), TEXT("1"));
				GuardDef.Keys.Add(TEXT("Flag_InfChild"), TEXT("1"));
				GuardDef.Keys.Add(TEXT("MaxLiveChildren"), TEXT("1"));
				GuardDef.Keys.Add(TEXT("model"), TEXT("models/test_guard.mdl"));
				FElysiumOutputDef Death;
				Death.Name = TEXT("OnDeath");
				Death.Target = TEXT("thug_maker_4");
				Death.Input = TEXT("Spawn");
				GuardDef.Outputs.Add(Death);
				Death.Target = TEXT("death_count");
				Death.Input = TEXT("Add");
				Death.Param = TEXT("1");
				GuardDef.Outputs.Add(Death);
				FElysiumEntityDef NextMaker = GuardDef;
				NextMaker.TargetName = TEXT("thug_maker_4");
				NextMaker.Keys[TEXT("NPCTargetname")] = TEXT("thug_4");
				NextMaker.Outputs.Reset();
				NextMaker.Origin = FVector(1000, 0, 0);
				Defs.Defs.Add(MoveTemp(NextMaker));
				FElysiumEntityDef Counter;
				Counter.Classname = TEXT("math_counter");
				Counter.TargetName = TEXT("death_count");
				Defs.Defs.Add(MoveTemp(Counter));
			}
			if (DialogName)
			{
				GuardDef.Keys.Add(TEXT("dialogname"), DialogName);
			}
			Defs.Defs.Add(MoveTemp(GuardDef));
			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);
			if (bTutorialMaker)
			{
				World.EnqueueInput(TEXT("stealth_victim_maker"), TEXT("Spawn"), FElysiumVariant::Void(),
					0.0, World.PlayerHandle(), World.PlayerHandle());
				World.Tick(0.0);
			}
			Guard = static_cast<FElysiumNpc*>(World.FindByName(TEXT("guard")));
			Player = World.FindPlayer();
			if (Guard)
			{
				Guard->NextThink = ELYSIUM_NEVER_THINK;
			}

			Rules.DeafArcDegrees[0] = 60.f;
			for (int32 Index = 1; Index < 20; ++Index)
			{
				Rules.DeafArcDegrees[Index] = 80.f;
			}
			if (Player)
			{
				Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength, 10);
				Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Dexterity, 10);
				Player->Sheet.RecomputeCurrent(nullptr);
			}
		}

		~FKillFixture()
		{
			ElysiumItems::Uninstall(Items);
			ElysiumSheetRules::BindTables(PreviousTables);
		}

		bool Arm(const TCHAR* Classname)
		{
			if (!Player)
			{
				return false;
			}
			const auto Handle = Player->Inventory.GiveNamedItem(*Player, Classname);
			FElysiumEntity* Entity = World.Resolve(Handle);
			FElysiumItem* Item = Entity ? Entity->AsItem() : nullptr;
			return Item && Player->Inventory.SetActiveWeapon(*Player, *Item);
		}

		void DuckBehind(float DistanceUnits = 40.f)
		{
			if (!Player)
			{
				return;
			}
			Player->Origin = FVector(-Cm(DistanceUnits), 0, 0);
			Player->Angles = FVector::ZeroVector;
			Services.bPlayerDucking = true;
		}

		void StandInFront(float DistanceUnits = 40.f)
		{
			if (!Player)
			{
				return;
			}
			Player->Origin = FVector(Cm(DistanceUnits), 0, 0);
			Player->Angles = FVector(0.f, 180.f, 0.f);
			Services.bPlayerDucking = true;
		}

		ElysiumSheetRules::FBoundTables PreviousTables;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillWeaponTest,
	"Elysium.Substrate.StealthKill.Weapon", GElysiumTestFlags)
bool FElysiumStealthKillWeaponTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	F.DuckBehind();
	TestFalse(TEXT("no active weapon is not eligible"), F.Player->CanAttemptStealthKill());
	TestNull(TEXT("...and FindVictim answers nothing"), F.Rules.FindVictim(*F.Player));
	if (!TestTrue(TEXT("fists become active"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	TestTrue(TEXT("unarmed melee authors the stealth-kill byte"), F.Player->CanAttemptStealthKill());
	F.World.Tick(0.5);
	TestNotNull(TEXT("fists admit the rear victim"), F.Rules.FindVictim(*F.Player));
	F.World.Tick(1.0);
	if (!TestTrue(TEXT("a firearm becomes active"), F.Arm(TEXT("item_w_sk_pistol")))) return false;
	TestFalse(TEXT("a firearm does not author the stealth-kill byte"), F.Player->CanAttemptStealthKill());
	TestNull(TEXT("...and FindVictim refuses"), F.Rules.FindVictim(*F.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillPostureTest,
	"Elysium.Substrate.StealthKill.Posture", GElysiumTestFlags)
bool FElysiumStealthKillPostureTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	if (!TestTrue(TEXT("armed"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	F.Player->Origin = FVector(-Cm(40.f), 0, 0);
	F.Player->Angles = FVector::ZeroVector;
	TestFalse(TEXT("standing unaugmented is not a stealth-kill posture"),
		F.Player->CanAttemptStealthKill());
	F.Services.bPlayerDucking = true;
	TestTrue(TEXT("ducking is enough, without the light-query unseen window"),
		F.Player->CanAttemptStealthKill());
	F.Player->LastHostileAssessment = F.Guard->Handle;
	F.Player->LastHostileAssessmentTime = F.World.NowSeconds();
	TestTrue(TEXT("a recent D_HT observation does not refuse the kill posture"),
		F.Player->CanAttemptStealthKill());
	F.Services.bPlayerDucking = false;
	F.Player->Sheet.SetBase(EElysiumTraitContainer::ActiveDisciplines, 8, 1);
	F.Player->Sheet.RecomputeCurrent(nullptr);
	F.Player->Disciplines.bObfuscateCloaked = true;
	TestTrue(TEXT("Obfuscate admits without ducking"), F.Player->CanAttemptStealthKill());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillTraceTest,
	"Elysium.Substrate.StealthKill.Trace", GElysiumTestFlags)
bool FElysiumStealthKillTraceTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	if (!TestTrue(TEXT("armed"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	F.DuckBehind(40.f);
	TestEqual(TEXT("a body-forward hull inside DistMax is the victim"),
		F.Rules.FindVictim(*F.Player), F.Guard);
	F.World.Tick(1.0);
	F.DuckBehind(200.f);
	TestNull(TEXT("beyond DistMax the ray misses"), F.Rules.FindVictim(*F.Player));
	F.World.Tick(2.0);
	F.Player->Origin = FVector(0, Cm(40.f), 0);
	F.Player->Angles = FVector::ZeroVector;
	F.Services.bPlayerDucking = true;
	TestNull(TEXT("off to the side the body-forward ray misses"), F.Rules.FindVictim(*F.Player));
	F.World.Tick(3.0);
	F.Rules.DistanceMaxUnits = 95.f;
	F.DuckBehind(80.f);
	TestEqual(TEXT("an authored DistMax extends the ray"), F.Rules.FindVictim(*F.Player), F.Guard);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillCacheTest,
	"Elysium.Substrate.StealthKill.Cache", GElysiumTestFlags)
bool FElysiumStealthKillCacheTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	if (!TestTrue(TEXT("armed"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	F.DuckBehind();
	FElysiumNpc* First = F.Rules.FindVictim(*F.Player);
	TestEqual(TEXT("the first sample admits"), First, F.Guard);
	F.Player->Origin = FVector(-Cm(400.f), 0, 0);
	TestEqual(TEXT("the same Now reuses the cached victim"), F.Rules.FindVictim(*F.Player), F.Guard);
	F.World.Tick(1.0);
	TestNull(TEXT("the next frame recomputes and the walk-away misses"), F.Rules.FindVictim(*F.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillArcTest,
	"Elysium.Substrate.StealthKill.Arc", GElysiumTestFlags)
bool FElysiumStealthKillArcTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	if (!TestTrue(TEXT("armed"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	F.DuckBehind();
	TestEqual(TEXT("a rear approach inside the Brawl arc admits"), F.Rules.FindVictim(*F.Player), F.Guard);
	F.World.Tick(1.0);
	F.StandInFront();
	TestNull(TEXT("a front approach without obliviousness refuses"), F.Rules.FindVictim(*F.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillObliviousTest,
	"Elysium.Substrate.StealthKill.Oblivious", GElysiumTestFlags)
bool FElysiumStealthKillObliviousTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	if (!TestTrue(TEXT("armed"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	F.StandInFront();
	TestNull(TEXT("front-facing is refused while the victim senses"), F.Rules.FindVictim(*F.Player));
	F.Guard->MakeOblivious(true);
	F.World.Tick(1.0);
	TestEqual(TEXT("m_iIsOblivious bypasses only the rear-arc test"), F.Rules.FindVictim(*F.Player), F.Guard);
	F.Guard->bInvincible = true;
	F.World.Tick(2.0);
	TestNull(TEXT("obliviousness does not skip invincible"), F.Rules.FindVictim(*F.Player));
	F.Guard->bInvincible = false;
	F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::SeePlayer);
	F.World.Tick(3.0);
	TestNull(TEXT("obliviousness does not skip SEE_PLAYER"), F.Rules.FindVictim(*F.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillValidTargetTest,
	"Elysium.Substrate.StealthKill.ValidTarget", GElysiumTestFlags)
bool FElysiumStealthKillValidTargetTest::RunTest(const FString&)
{
	{
		FKillFixture F;
		if (!F.Guard || !F.Player) return false;
		TestTrue(TEXT("an Idle, conversable-less, living NPC is a valid target"),
			F.Guard->IsValidStealthKillTarget(*F.Player));
		F.Guard->bInvincible = true;
		TestFalse(TEXT("invincible refuses"), F.Guard->IsValidStealthKillTarget(*F.Player));
		F.Guard->bInvincible = false;
		F.Guard->bHidden = true;
		TestFalse(TEXT("script-hidden refuses"), F.Guard->IsValidStealthKillTarget(*F.Player));
		F.Guard->bHidden = false;
		F.Guard->bDead = true;
		TestFalse(TEXT("a dead life state refuses"), F.Guard->IsValidStealthKillTarget(*F.Player));
		F.Guard->bDead = false;
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::SeePlayer);
		TestFalse(TEXT("SEE_PLAYER refuses"), F.Guard->IsValidStealthKillTarget(*F.Player));
		F.Guard->Cognition.Conditions.Reset();
		F.Guard->Cognition.Conditions.Set(EElysiumNpcCond::HearPlayer);
		TestFalse(TEXT("HEAR_PLAYER refuses"), F.Guard->IsValidStealthKillTarget(*F.Player));
	}
	{
		FKillFixture Talking(TEXT("dlg/test/talk.dlg"));
		if (!Talking.Guard || !Talking.Player) return false;
		TestFalse(TEXT("an authored dialogname refuses"),
			Talking.Guard->IsValidStealthKillTarget(*Talking.Player));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillBusyTest,
	"Elysium.Substrate.StealthKill.Busy", GElysiumTestFlags)
bool FElysiumStealthKillBusyTest::RunTest(const FString&)
{
	FKillFixture F;
	if (!F.Guard || !F.Player) return false;
	if (!TestTrue(TEXT("armed"), F.Arm(TEXT("item_w_sk_fists")))) return false;
	F.DuckBehind();
	TestNotNull(TEXT("an unbusy player admits"), F.Rules.FindVictim(*F.Player));
	F.World.SetCineCamera(F.Guard->Handle, 1, false, TEXT(""));
	F.World.Tick(1.0);
	TestFalse(TEXT("a live cine camera is busy"), F.Player->CanAttemptStealthKill());
	TestNull(TEXT("...and FindVictim refuses"), F.Rules.FindVictim(*F.Player));
	F.World.SetCineCamera(FElysiumEntityHandle::Invalid(), 0, false, TEXT(""));
	F.Player->LifeState = EElysiumLifeState::Dead;
	F.World.Tick(2.0);
	TestFalse(TEXT("a dead player is not eligible"), F.Player->CanAttemptStealthKill());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillStandTest,
	"Elysium.Substrate.StealthKill.StandAndClips", GElysiumTestFlags)
bool FElysiumStealthKillStandTest::RunTest(const FString&)
{
	FKillFixture F;
	F.Arm(TEXT("item_w_sk_fists"));
	F.DuckBehind();
	F.Services.bStandClear = false;
	TestNull(TEXT("standing hull obstruction rejects admission"), F.Rules.FindVictim(*F.Player));
	F.Services.bStandClear = true;
	TestNull(TEXT("failed query is cached in this frame"), F.Rules.FindVictim(*F.Player));
	F.World.Tick(0.1);
	F.Services.bNpcActivitiesResolve = false;
	TestNull(TEXT("missing paired activity rejects admission"), F.Rules.FindVictim(*F.Player));
	F.World.Tick(0.2);
	F.Services.bNpcActivitiesResolve = true;
	TestNotNull(TEXT("clear hull and both clips admit"), F.Rules.FindVictim(*F.Player));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillCommitTest,
	"Elysium.Substrate.StealthKill.CommitAndCompletion", GElysiumTestFlags)
bool FElysiumStealthKillCommitTest::RunTest(const FString&)
{
	FKillFixture F;
	F.Arm(TEXT("item_w_sk_fists"));
	F.DuckBehind();
	F.Player->Visual = NewObject<USkeletalMeshComponent>();
	F.Guard->Visual = NewObject<USkeletalMeshComponent>();
	F.Guard->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
	F.Guard->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
	F.Guard->RecomputeSheet();
	F.Player->Observer.Observer = F.Guard->Handle;
	F.Player->Observer.bDetected = true;
	F.Player->Observer.DistanceCm = 10.f;
	if (!TestTrue(TEXT("qualified input commits a pair"), F.Player->TryStealthKill(F.Rules))) return false;
	TestFalse(TEXT("pre-commit detected observer is cleared"), F.Player->Observer.IsSet());
	TestTrue(TEXT("attacker owns mode 3"), F.Player->Grapple.Type == EElysiumGrappleType::StealthKill);
	TestTrue(TEXT("victim points to attacker driver"), F.Guard->Grapple.AnimDriver == F.Player->Handle);
	TestTrue(TEXT("opponent handles written"), F.Player->MeleeOpponent == F.Guard->Handle
		&& F.Player->LastOpponent == F.Guard->Handle);
	TestFalse(TEXT("no death on commitment"), F.Guard->HasReportedDeath());
	F.Services.bBodyClipPhaseSet = true;
	F.Services.BodyClipPhase.Label = F.Player->Grapple.ClipLabel;
	F.Services.BodyClipPhase.OwnerStem = F.Player->Grapple.ClipOwner;
	F.Services.BodyClipPhase.PlayId = 17;
	F.Services.BodyClipPhase.Cycle = 0.999f;
	F.Player->TickStealthKill();
	TestFalse(TEXT("no death before authored completion"), F.Guard->HasReportedDeath());
	F.Services.BodyClipPhase.Cycle = 1.f;
	F.Player->TickStealthKill();
	TestTrue(TEXT("finished attacker commits death"), F.Guard->HasReportedDeath());
	TestEqual(TEXT("retail damage counter copied from ceiling"),
		F.Guard->Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health), 100);
	TestTrue(TEXT("death attributed to attacker"), F.Guard->DeathAttacker == F.Player->Handle);
	TestFalse(TEXT("attacker released"), F.Player->IsGrappling());
	TestFalse(TEXT("victim released"), F.Guard->IsGrappling());
	F.Player->TickStealthKill();
	TestTrue(TEXT("replayed completion remains inert"), F.Guard->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillLoadFailureTest,
	"Elysium.Substrate.StealthKill.LoadFailure", GElysiumTestFlags)
bool FElysiumStealthKillLoadFailureTest::RunTest(const FString&)
{
	FKillFixture F;
	F.Arm(TEXT("item_w_sk_fists"));
	F.DuckBehind();
	F.Player->Visual = NewObject<USkeletalMeshComponent>();
	F.Guard->Visual = NewObject<USkeletalMeshComponent>();
	F.Services.bRootAvailable = false;
	AddExpectedError(TEXT("cannot load/sample"), EAutomationExpectedErrorFlags::Contains, 1);
	TestFalse(TEXT("missing sampled clip refuses commitment"), F.Player->TryStealthKill(F.Rules));
	TestTrue(TEXT("retail opponent write survives failed start"), F.Player->MeleeOpponent == F.Guard->Handle);
	TestFalse(TEXT("no half pair"), F.Player->IsGrappling() || F.Guard->IsGrappling());
	TestFalse(TEXT("failed start cannot kill"), F.Guard->HasReportedDeath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillTutorialOutputTest,
	"Elysium.Substrate.StealthKill.TutorialOutput", GElysiumTestFlags)
bool FElysiumStealthKillTutorialOutputTest::RunTest(const FString&)
{
	FKillFixture F(nullptr, true);
	if (!TestNotNull(TEXT("maker spawned victim"), F.Guard)) return false;
	F.Arm(TEXT("item_w_sk_fists"));
	F.DuckBehind();
	F.Player->Visual = NewObject<USkeletalMeshComponent>();
	F.Guard->Visual = NewObject<USkeletalMeshComponent>();
	F.Guard->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
	F.Guard->RecomputeSheet();
	if (!TestTrue(TEXT("paired action starts on maker child"), F.Player->TryStealthKill(F.Rules))) return false;
	TestNull(TEXT("lesson has not advanced at entry"), F.World.FindByName(TEXT("thug_4")));
	F.Services.bBodyClipPhaseSet = true;
	F.Services.BodyClipPhase.Label = F.Player->Grapple.ClipLabel;
	F.Services.BodyClipPhase.OwnerStem = F.Player->Grapple.ClipOwner;
	F.Services.BodyClipPhase.PlayId = 1;
	F.Services.BodyClipPhase.Cycle = 1.f;
	F.Player->TickStealthKill();
	F.World.Tick(0.1);
	TestNotNull(TEXT("inherited OnDeath spawns next tutorial thug through real queue"), F.World.FindByName(TEXT("thug_4")));
	F.Guard->CommitStealthDeath(F.Player->Handle);
	F.World.Tick(0.2);
	TArray<TPair<FString, FString>> CounterState;
	F.World.FindByName(TEXT("death_count"))->GetDebugState(CounterState);
	float Count = -1.f;
	for (const auto& Row : CounterState) if (Row.Key == TEXT("Value")) Count = FCString::Atof(*Row.Value);
	TestEqual(TEXT("OnDeath is emitted once despite duplicate completion"), Count, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumStealthKillWeaponSoundTest,
	"Elysium.Substrate.StealthKill.WeaponSoundData", GElysiumTestFlags)
bool FElysiumStealthKillWeaponSoundTest::RunTest(const FString&)
{
	FElysiumItemDef Item;
	FString Error;
	TestTrue(TEXT("authored success sound parses"), FElysiumItemTable::ParseText(TEXT("item_w_fists"),
		TEXT("WeaponData { SoundData { stealth_kill_success { sound1 Weapons/Melee/Fists/Stealth_Fists.wav } } }"), Item, Error));
	TestEqual(TEXT("one authoritative success sound"), Item.StealthKillSounds.Num(), 1);
	if (Item.StealthKillSounds.Num()) TestEqual(TEXT("path kept verbatim"), Item.StealthKillSounds[0],
		FString(TEXT("Weapons/Melee/Fists/Stealth_Fists.wav")));
	return true;
}

}

#endif
