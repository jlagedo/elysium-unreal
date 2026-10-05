#include "Tests/ElysiumMeleeStepFixture.h"
// Content-free Substrate automation: the combat schedule families — the NPC loadout, the weapon
// capability split, the committed-enemy attack conditions, the two recovered selectors, the chase
// and swing programs end to end over a recording motor, and the body-owner lifecycle around them.
//
// Every number and every ordering asserted here is a fact from
// `docs/vtmb/npc-ai/programs.md` -> "Ordinary humanoid combat selection" / "Schedules
// and tasks" / "Interrupt conditions", or from `docs/vtmb/combat-and-damage.md` -> "Target
// acquisition, sequence commit and recovery". Nothing loads a rulebook: the item catalogue is built
// in code for the length of a case, which is what makes these the runtime's statement of the
// contract rather than a reading of the export.

#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumCharacterProvenance.h"
#include "ElysiumPhysicsData.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace ElysiumNpcCombatTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

using ElysiumSaveTestHelpers::SaveTestCounterValue;
using ECond = EElysiumNpcCond;
using EId = int32;

namespace
{
	double Cm(double SourceUnits) { return SourceUnits * ElysiumMove::U; }

	// Typed cooked provenance, with a deliberately non-regular_cop native Spine2 at ordinal 2.
	void InstallCorpseSource(USkeletalMeshComponent* Body, bool bSourceRig, bool bSpinePresent = true)
	{
		USkeletalMesh* const CorpseMesh = NewObject<USkeletalMesh>(GetTransientPackage());
		FReferenceSkeleton CorpseBones;
		{
			FReferenceSkeletonModifier BoneWriter(CorpseBones, nullptr);
			BoneWriter.Add(FMeshBoneInfo(TEXT("fixture_root"), TEXT("fixture_root"), INDEX_NONE), FTransform::Identity);
			BoneWriter.Add(FMeshBoneInfo(TEXT("fixture_spine"), TEXT("fixture_spine"), 0), FTransform::Identity);
			const FName NativeSpine(bSpinePresent ? TEXT("fixture_spine2") : TEXT("fixture_missing"));
			BoneWriter.Add(FMeshBoneInfo(NativeSpine, NativeSpine.ToString(), 1), FTransform::Identity);
		}
		CorpseMesh->SetRefSkeleton(CorpseBones);
		UElysiumCharacterProvenance* const SourceRecord = NewObject<UElysiumCharacterProvenance>(CorpseMesh);
		UElysiumPhysicsData* const SourcePhysics = NewObject<UElysiumPhysicsData>(CorpseMesh);
		SourcePhysics->Data.bHasPhysics = bSourceRig;
		FElysiumPhysicsSourceBone& SourceSpine = SourcePhysics->Data.Bones.AddDefaulted_GetRef();
		SourceSpine.Index = 17; // source ordinal is deliberately distinct from the native ordinal
		SourceSpine.SourceName = TEXT("Bip01 Spine2");
		SourceSpine.NativeName = TEXT("fixture_spine2");
		SourceRecord->PhysicsSourceData = SourcePhysics;
		CorpseMesh->AddAssetUserData(SourceRecord);
		Body->SetSkeletalMeshAsset(CorpseMesh);
	}

	struct FV4dDeathNpc : FElysiumNpc
	{
		TArray<FElysiumNpcClip> DeathPickRows;
		mutable int32 SeedGathers = 0;
		mutable int32 ObservedForceBone = INDEX_NONE;
		virtual void MeleeSequencesForActivity(int32 Activity, TArray<FElysiumNpcClip>& OutRows) const override
		{
			if (Activity == 0x21) { ++SeedGathers; OutRows = DeathPickRows; }
			else { OutRows.Reset(); }
		}
		virtual int32 CorpseForceBone(const void* InInfo) const override
		{
			ObservedForceBone = FElysiumNpc::CorpseForceBone(InInfo);
			return ObservedForceBone;
		}
		void RestoreVisualForTest() { RestoreDeathBodyState(); }
		void CompleteHandoffForTest() { CompleteDeathHandoff(); }
	};


	// --- The synthetic catalogue ---------------------------------------------------------------
	// `item_w_fists` is spelled EXACTLY, because the loadout's fallback names that record and no
	// other suite claims the name. The two authored weapons are suite-local (`_npccombat_`):
	// `ElysiumItems::Install` registers a class once per process and never unregisters, so a name
	// shared with another suite would resolve to whichever suite ran first.
	const TCHAR* const GFists   = TEXT("item_w_fists");
	const TCHAR* const GKatana  = TEXT("item_w_npccombat_katana");
	const TCHAR* const GPistol  = TEXT("item_w_npccombat_pistol");
	const TCHAR* const GTrinket = TEXT("item_g_npccombat_trinket");

	// The pistol's authored far edge, in Source units — `TOO_FAR_TO_ATTACK`'s only recovered input.
	constexpr float GPistolRangeUnits = 2000.f;

	FElysiumWeaponMode MakeMode(const TCHAR* Tag, const TCHAR* Dmg, int32 BaseLethality,
		float AttackRate, float Range = 0.f, int32 AmmoCost = 0)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = Tag;
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = BaseLethality;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = AttackRate;
		Mode.Range = Range;
		Mode.AmmoCost = AmmoCost;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeCombatItemTable(bool bWithFists)
	{
		FElysiumItemTable Table;

		if (bWithFists)
		{
			// The real record's own shape: a `weapon_melee hidden` file with one `Attack` block.
			FElysiumItemDef Fists;
			Fists.Classname = GFists;
			Fists.PrintName = TEXT("Fists");
			Fists.Type = EElysiumItemType::WeaponMelee;
			Fists.bHidden = true;
			// Deliberately NOT `is_wieldable`: none of the shipped `item_w_*` records authors it,
			// which is exactly the case the loadout's explicit active-weapon switch exists for.
			Fists.Modes.Add(MakeMode(TEXT("Primary"),
				TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), /*BaseLethality*/ 8,
				/*Attack_Rate*/ 0.5f));
			Table.Items.Add(MoveTemp(Fists));
		}

		FElysiumItemDef Katana;
		Katana.Classname = GKatana;
		Katana.PrintName = TEXT("Katana");
		Katana.Type = EElysiumItemType::WeaponMelee;
		Katana.Modes.Add(MakeMode(TEXT("Primary"),
			TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 12, 1.0f));
		Table.Items.Add(MoveTemp(Katana));

		FElysiumItemDef Pistol;
		Pistol.Classname = GPistol;
		Pistol.PrintName = TEXT("Pistol");
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.AmmoType = TEXT("NpcCombatRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 6;
		Pistol.Modes.Add(MakeMode(TEXT("Primary"), TEXT("2 Lethal Ranged_Combat DMG_BULLET"), 9,
			/*Attack_Rate*/ 0.4f, GPistolRangeUnits, /*Ammo_Cost*/ 1));
		Table.Items.Add(MoveTemp(Pistol));

		FElysiumItemDef Trinket;
		Trinket.Classname = GTrinket;
		Trinket.PrintName = TEXT("Trinket");
		Trinket.Type = EElysiumItemType::Generic;
		Table.Items.Add(MoveTemp(Trinket));

		Table.Reindex();
		return Table;
	}

	// --- The world -------------------------------------------------------------------------------
	// One armed fighter at the origin facing +X, one NPC target 100 cm in front of it, the player
	// parked far away so it never becomes a melee acquisition candidate, and a counter wired off the
	// target's `OnDamaged` so the cycle-1 commit is observable as a number.
	struct FCombatFixture
	{
		FElysiumItemTable Items;
		bool bInstalled = false;
		FElysiumNpcWorldFixture Fixture;
		FElysiumRecordingServices& Services;
		FElysiumEntityWorld& World;
		FElysiumNpc* Fighter = nullptr;
		FElysiumNpc* Target = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld(const TCHAR* FighterEquip)
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__npccombat_test__"), 0x4E504343);

			FElysiumEntityDef& FighterDef = Builder.AddNpc(TEXT("fighter"));
			FighterDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
			FighterDef.Keys.Add(TEXT("vision"), TEXT("4000"));
			FighterDef.Keys.Add(TEXT("hearing"), TEXT("1.0"));
			FighterDef.Keys.Add(TEXT("additionalequipment"), FighterEquip);
			FighterDef.Keys.Add(TEXT("cantdropweapons"), TEXT("1"));

			FElysiumEntityDef& TargetDef = Builder.AddNpc(TEXT("target"), FVector(100.0, 0.0, 0.0));
			TargetDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
			Builder.WireOutput(TEXT("target"), TEXT("OnDamaged"), TEXT("damagedcount"));

			Builder.AddCounter(TEXT("damagedcount"));
			return Builder;
		}

		FCombatFixture(const TCHAR* FighterEquip, bool bWithFists = true, bool bInstallCatalogue = true)
			: Items(MakeCombatItemTable(bWithFists))
			, Fixture(BuildWorld(FighterEquip), [this, bInstallCatalogue](FElysiumRecordingServices& S)
				{
					if (bInstallCatalogue)
					{
						ElysiumItems::Install(Items);
						bInstalled = true;
					}
					// The recording motor is opt-in, and every movement task in this suite needs one.
					S.bProvideNpcMotor = true;
				})
			, Services(Fixture.Services)
			, World(Fixture.World)
		{
			// `AsNpc` rather than a blind downcast: the living-NPC leaf is what carries the senses,
			// memory and cognition every case here drives.
			Fighter = Fixture.Npc(TEXT("fighter"));
			Target = Fixture.Npc(TEXT("target"));
			Player = Fixture.Player();
			if (Player)
			{
				// Out of every reach and cone: this suite is about NPC-versus-NPC combat, and a
				// player standing on the fighter's own origin would be an acquisition candidate.
				Player->Origin = FVector(0.0, Cm(9000.0), 0.0);
			}
			for (FElysiumNpc* Npc : { Fighter, Target })
			{
				if (Npc)
				{
					// The Source health ceiling the damage predicates read; a headless world has no
					// rulebook behind `SeedSheet`, so it is stated here.
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
					Npc->RecomputeSheet();
					Npc->MaxHealth = 100;
				}
			}
		}

		~FCombatFixture()
		{
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		FCombatFixture(const FCombatFixture&) = delete;
		FCombatFixture& operator=(const FCombatFixture&) = delete;

		// Nothing here wants an NPC's own think competing with the pass a case is driving.
		void Quiet()
		{
			FElysiumNpcWorldFixture::Quiet({ Fighter, Target });
		}

		// Two ordinary thinks: the first crosses the admission barrier, the second resolves the
		// loadout. That is the real production path, not an injected grant.
		void RunAdmissionAndLoadout()
		{
			for (int32 i = 0; i < 2; ++i)
			{
				FElysiumNpcWorldFixture::Wake({ Fighter, Target }, 0.0);
				World.Tick(0.0);
			}
			Quiet();
		}

		void Flush(double Now)
		{
			Quiet();
			World.Tick(Now);
		}

		void Hate(const FElysiumEntity* Who, int32 Priority)
		{
			if (Fighter && Who)
			{
				Fighter->Relationships.SetEntity(Who->Handle, EElysiumRelationship::Hate, Priority);
			}
		}

		// The mind's state is private to the leaf; its inspector row is the read side everything else
		// uses, so a test asserts through the same surface a developer would look at.
		FString Debug(const FElysiumEntity* Entity, const TCHAR* Key) const
		{
			return FElysiumNpcWorldFixture::Debug(Entity, Key);
		}

		FElysiumRecordingNpcMotor* MotorFor(const FElysiumNpc* Npc) const
		{
			for (const TUniquePtr<FElysiumRecordingNpcMotor>& Motor : Services.NpcMotors)
			{
				if (Npc && Motor && Motor->Owner == Npc->Handle)
				{
					return Motor.Get();
				}
			}
			return nullptr;
		}

		FElysiumWeapon* ActiveWeapon(FElysiumNpc* Npc) const
		{
			FElysiumItem* Item = Npc ? Npc->Inventory.Active(*Npc) : nullptr;
			return Item ? Item->AsWeapon() : nullptr;
		}

		// Commit the fighter to the target and put the mind in combat state through the real
		// ideal-state pass.
		void CommitToTarget(double Now)
		{
			if (!Fighter || !Target)
			{
				return;
			}
			Hate(Target, 5);
			Fighter->BaseMemory.Enemy = Target->Handle;
			// `SelectIdealState` (`0x102ad660`) only promotes idle → combat through
			// `HasInterruptCondition`, which answers 0 with no program installed, and these
			// fixtures install none. `SetState(2)` (`0x1026e340`) is retail's OWN commit — it is
			// what `MaintainSchedule 0x102817c0` calls with `m_IdealNPCState` — so the fixture
			// reaches combat the way the kernel does, without inventing an ideal-state path.
			(void)Now;
			Fighter->SetState(2);
		}

		int32 DamageTaken(const FElysiumNpc* Npc) const
		{
			return Npc ? Npc->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health) : -1;
		}
	};
}


// The loadout: the authored keyfield, the fists fallback, and the marked unarmed path.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatLoadoutTest,
	"Elysium.Arm.NpcCombat.Loadout", GElysiumTestFlags)
bool FElysiumNpcCombatLoadoutTest::RunTest(const FString&)
{
	// --- The authored classname is granted and made the ACTIVE weapon --------------------------
	{
		FCombatFixture F(GPistol);
		if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
		{
			return false;
		}
		TestEqual(TEXT("the keyfield parsed"), F.Fighter->AdditionalEquipment, FString(GPistol));
		TestTrue(TEXT("`cantdropweapons` parses beside it"), F.Fighter->bCantDropWeapons);
		// The fixture's world has already run one think, and under `NPCThink`'s recovered shape
		// admission and the loadout are the same normal-due pass -- admission suppresses only the
		// AI pass. Retail resolves the loadout at spawn; the port defers it exactly one think,
		// because creating an item entity inside the spawn pass invalidates the array being
		// iterated.
		TestTrue(TEXT("admission and the loadout are one think"), F.Fighter->bLoadoutResolved);

		F.RunAdmissionAndLoadout();
		TestTrue(TEXT("...and the latch holds across further thinks"), F.Fighter->bLoadoutResolved);

		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("the authored weapon is the ACTIVE weapon"), Weapon))
		{
			return false;
		}
		TestEqual(TEXT("...and it is the classname the map authored"), Weapon->ClassName(),
			FString(GPistol));
		TestEqual(TEXT("its magazine spawned loaded from the record's Default_Size"),
			Weapon->MagazineCount, 6);
		// The record authors no `is_wieldable` — like every shipped `item_w_*` weapon — so this is
		// the assertion that the loadout's own active-weapon switch is what armed it.
		TestFalse(TEXT("the authored record is deliberately not `is_wieldable`"),
			Weapon->IsWieldable());

		// A second resolution must not hand a second gun to an NPC that is already armed.
		TestEqual(TEXT("re-resolving an armed NPC grants nothing"),
			ElysiumNpcLoadout::Resolve(*F.Fighter), ElysiumNpcLoadout::EResult::AlreadyArmed);
		TestEqual(TEXT("...and the carried list still holds exactly one item"),
			F.Fighter->Inventory.Num(), 1);
	}

	// --- Nothing authored falls back to the fists record ---------------------------------------
	{
		FCombatFixture F(TEXT("0"));   // the authored "none" sentinel, on 78 of the 267 rows
		if (F.Fighter == nullptr)
		{
			return false;
		}
		TestTrue(TEXT("`0` is the authored none sentinel"),
			ElysiumNpcLoadout::IsNoneSentinel(F.Fighter->AdditionalEquipment));
		F.RunAdmissionAndLoadout();

		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("an NPC with no authored weapon still holds its fists"), Weapon))
		{
			return false;
		}
		TestEqual(TEXT("...which is `item_w_fists`, not `item_w_unarmed`"), Weapon->ClassName(),
			FString(GFists));
	}

	// --- No catalogue at all: the marked unarmed path ------------------------------------------
	{
		// A fabricated-rulebook / headless world. Nothing is armed, and that is a supported state
		// rather than a failure — combat selection routes to melee with bare-hands defaults.
		FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
		if (F.Fighter == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		TestNull(TEXT("an NPC in a world with no item catalogue stays unarmed"),
			F.ActiveWeapon(F.Fighter));
		TestEqual(TEXT("...and reports the unarmed capability"),
			ElysiumNpcCond::WeaponCapability(*F.Fighter), ElysiumNpcCond::ECapability::Unarmed);
		TestTrue(TEXT("the inspector says so in one row"),
			F.Debug(F.Fighter, TEXT("Weapon")).Contains(TEXT("unarmed")));
	}

	// --- An authored classname that is not a wielded weapon is refused by name ------------------
	{
		AddExpectedError(TEXT("is a 'Generic' item, not a wielded weapon"),
			EAutomationExpectedErrorFlags::Contains, 1);
		FCombatFixture F(GTrinket);
		if (F.Fighter == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (TestNotNull(TEXT("the refusal still falls back to the fists"), Weapon))
		{
			TestEqual(TEXT("...rather than arming a trinket"), Weapon->ClassName(), FString(GFists));
		}
	}
	return true;
}


// The capability split: which selector a weapon routes to, and the two retail bits.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatCapabilityTest,
	"Elysium.Arm.NpcCombat.Capability", GElysiumTestFlags)
bool FElysiumNpcCombatCapabilityTest::RunTest(const FString&)
{
	TestEqual(TEXT("0x103eaea0: the full melee capability is 0x40018000"),
		ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::ECapability::Melee), 0x40018000);
	TestEqual(TEXT("a firearm reports 0x2000"),
		ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::ECapability::Ranged), 0x2000);

	// --- A melee weapon routes to the melee selector -------------------------------------------
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		TestEqual(TEXT("a `weapon_melee` record is melee capability"),
			ElysiumNpcCond::WeaponCapability(*F.Fighter), ElysiumNpcCond::ECapability::Melee);

		F.CommitToTarget(10.0);
		// Injected rather than gathered: this case is about the ROUTE, and the melee selector's
		// dodge branch is one no ranged program carries.
		F.Fighter->Cognition.Conditions.Reset();
		F.Fighter->Cognition.Conditions.Set(ECond::ShouldDodge);
		// The PROGRAM the branch answers is the recovered slot body's, not the CHOSEN ladder this
		// case used to read: those ladders are gone with the fold that made them necessary. What
		// the case is about -- which branch the capability split routes into -- is asserted against
		// the slot body's own answer.
		TestEqual(TEXT("melee capability enters the melee selector"), F.Fighter->SelectSchedule(),
			F.Fighter->SelectScheduleMeleeCombat(0));
	}

	// --- A firearm routes to the ranged selector -----------------------------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		TestEqual(TEXT("a `weapon_firearm` record is ranged capability"),
			ElysiumNpcCond::WeaponCapability(*F.Fighter), ElysiumNpcCond::ECapability::Ranged);

		F.CommitToTarget(10.0);
		F.Fighter->Cognition.Conditions.Reset();
		// The same injected condition the melee selector answers with a dodge: the ranged selector
		// has no such branch, so it must NOT be what decides here.
		F.Fighter->Cognition.Conditions.Set(ECond::ShouldDodge);
		F.Fighter->Cognition.Conditions.Set(ECond::CanRangeAttack1);
		// The ranged branch declines here -- slot 605 answers zero -- and the composition rule then
		// hands the decision to `CAI_BaseNPCTroika::SelectSchedule`, which is the idle. That it is
		// NOT the melee branch's answer is the claim.
		TestEqual(TEXT("ranged capability does not enter the melee selector"),
			F.Fighter->SelectScheduleRangedCombat(0), ElysiumScheduleId::None);
		// Story 8 L06: the ranged zero falls through `CNPC_VHuman::SelectSchedule` (`0x1038502f`) to
		// `CAI_BaseNPCTroika::SelectSchedule` case 2, whose no-`SEE_ENEMY`, no-`ENEMY_OCCLUDED` arm
		// answers `0xb COMBAT_FACE` (`0x102afe5d`), not the idle the port's composition rule chose.
		TestEqual(TEXT("...so the Troika combat ladder answers COMBAT_FACE (0x102afe5d)"),
			F.Fighter->SelectSchedule(), 0xb);
	}

	// --- Unarmed: no weapon entity, weapon word 0, slot 605 ---------------------------------------
	// Story 8 L06 integration (corrected to retail): `CNPC_VHuman::SelectSchedule` reads the weapon
	// word through the active weapon (`0x10384fec`); with none it is 0 (`0x10385008 XOR EAX,EAX`),
	// `TEST EAX,0x18000` fails and the ranged slot 605 answers (`0x10385022 CALL [EDX+0x974]`). The
	// port's "unarmed takes the melee branch" was its own composition rule (CHOSEN).
	{
		FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.CommitToTarget(10.0);
		F.Fighter->Cognition.Conditions.Reset();
		F.Fighter->Cognition.Conditions.Set(ECond::ShouldBlock);
		const int32 Ranged605 = F.Fighter->SelectScheduleRangedCombat(0);
		TestNotEqual(TEXT("slot 605 answers for the unarmed fighter"), Ranged605, 0);
		TestEqual(TEXT("an unarmed NPC takes slot 605's answer (0x10385022)"),
			F.Fighter->SelectSchedule(), Ranged605);
	}
	return true;
}


// The committed-enemy attack conditions: reach, facing, readiness, range bands, ammo.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatAttackConditionsTest,
	"Elysium.Arm.NpcCombat.AttackConditions", GElysiumTestFlags)
bool FElysiumNpcCombatAttackConditionsTest::RunTest(const FString&)
{
	// --- Melee: the weapon's slot 367 is the band `0x103ea7e0`, not a reach constant ------------------
	// Spec 0002 V5a-1 deleted this block's stand-in assertions (0x51 inside 64 units when faced,
	// 0x60 past 64 units, 0x2f off the katana's deadline): they pinned the port's CHOSEN band and the
	// exclusive melee/ranged split. The band is `Elysium.Arm.NpcKernelConditions.MeleeWeaponBand`.
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.Fighter->BaseMemory.Enemy = F.Target->Handle;
		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("the fighter holds its katana"), Weapon))
		{
			return false;
		}
		// `0x1026de3a`: 0x2f is the RANGED arm's (`caps & 0x2000` with a weapon, or `caps & 0x20000`).
		// A melee weapon carries neither bit, so its unexpired deadline raises no 0x2f; it withholds
		// 0x51 through the band's `ready` instead (`0x103ea84e`).
		Weapon->HoldAttacksUntil(50.0);
		FElysiumNpcConditions& Cond = F.Fighter->Cognition.Conditions;
		Cond.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0);
		TestFalse(TEXT("0x1026de3a: a melee weapon's deadline raises no WAITING_ATTACK_TIME (0x2f)"),
			Cond.Has(ECond::WaitingAttackTime));
		TestFalse(TEXT("0x103ea84e: ...and withholds CAN_MELEE_ATTACK1"), Cond.Has(ECond::CanMeleeAttack1));
	}

	// --- Ranged: weapon slot 365 (`0x1024f670`), one answer, first match -------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.Fighter->BaseMemory.Enemy = F.Target->Handle;
		// The weapon-sight occlusion is slot 481's `+0x5b98` debounce, which `NPCInit` seeds at its
		// limit of ten (`BaseMemory.EnemyOccludedCheck = 10`); a visible check zeroes it
		// (`0x10270bb1`). This case drives slot 561 alone, so the enemy is stated in sight.
		F.Fighter->BaseMemory.EnemyOccludedCheck = 0;
		FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
		if (!TestNotNull(TEXT("the fighter holds its pistol"), Weapon))
		{
			return false;
		}

		// The words are the `CWeaponRanged` constructor's (`0x10238070`), not the mode's `Range`.
		TestEqual(TEXT("m_fMinRange1 +0x8b8 is 150"), Weapon->RangeWords.MinRange1, 150.f);
		TestEqual(TEXT("m_fMinRange2 +0x8bc is 65"), Weapon->RangeWords.MinRange2, 65.f);
		TestEqual(TEXT("m_fMaxRange1 +0x8c0 is 1024"), Weapon->RangeWords.MaxRange1, 1024.f);
		TestEqual(TEXT("m_fMaxRange2 +0x8c4 is 300"), Weapon->RangeWords.MaxRange2, 300.f);

		// Slot 365 answers exactly one of these; the gather raises that one.
		const ECond Answers[] = { ECond::NoPrimaryAmmo, ECond::TooCloseForRanged, ECond::TooCloseToAttack,
			ECond::TooFarToAttack, ECond::NotFacingAttack, ECond::CanRangeAttack1, ECond::WeaponSightOccluded };
		auto Gather = [this, &F, &Answers](double AtUnits, ECond Expected, const TCHAR* What)
		{
			F.Target->Origin = FVector(Cm(AtUnits), 0.0, 0.0);
			// The gather works on the NPC's own set (slot 560 clears it, `0x1026de02`); each case
			// starts from an empty one so a band word of the previous case is not what is read.
			F.Fighter->Cognition.Conditions.Reset();
			ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0);
			const FElysiumNpcConditions Cond = F.Fighter->Cognition.Conditions;
			int32 Raised = 0;
			for (const ECond Answer : Answers)
			{
				Raised += Cond.Has(Answer) ? 1 : 0;
			}
			TestTrue(What, Cond.Has(Expected));
			TestEqual(*FString::Printf(TEXT("%s: exactly one answer"), What), Raised, 1);
			return Cond;
		};

		// 100 cm is ~39 units: under the 100-unit edge (`_DAT_10450564`).
		Gather(100.0 / ElysiumMove::U, ECond::TooCloseForRanged,
			TEXT("d < 100 is TOO_CLOSE_FOR_RANGED (0x08)"));
		Gather(125.0, ECond::TooCloseToAttack, TEXT("100 <= d < m_fMinRange1 150 is TOO_CLOSE_TO_ATTACK (0x5f)"));
		Gather(400.0, ECond::CanRangeAttack1, TEXT("150 <= d <= 1024, faced and ready, is CAN_RANGE_ATTACK1"));
		// Past 1024 but inside the mode's authored `Range` 2000: the `Range` key feeds no compare.
		Gather(1500.0, ECond::TooFarToAttack,
			TEXT("d > m_fMaxRange1 1024 is TOO_FAR_TO_ATTACK (0x60), whatever the mode's Range"));

		// The facing: slot 368's body direction against the flattened direction, `dot < 0.5` (0x61).
		F.Fighter->Angles.Y = 180.0;
		Gather(400.0, ECond::NotFacingAttack, TEXT("an enemy behind the NPC is NOT_FACING_ATTACK (0x61)"));
		F.Fighter->Angles.Y = 0.0;

		// The empty clip is the FIRST arm (`[+0x74c] JG`), reserve or not.
		Weapon->MagazineCount = 0;
		Gather(400.0, ECond::NoPrimaryAmmo, TEXT("an empty magazine is NO_PRIMARY_AMMO (0x40)"));
		F.Fighter->Inventory.AddReserve(TEXT("NpcCombatRound"), 12);
		Gather(125.0, ECond::NoPrimaryAmmo, TEXT("...with a reserve, and ahead of every range arm"));
		Weapon->MagazineCount = 6;

		// Slot 562's re-test of the 0x4f answer (spec 0002 V5a-3): the pistol's own line of fire,
		// weapon slot 364 `0x1024f330` -> `0x1024f3d0`, from owner slot 389's point. The eye's
		// occlusion latch (`+0x5b98`) is not what the gather reads any more.
		F.Fighter->BaseMemory.EnemyOccludedCheck = 10;
		Gather(400.0, ECond::CanRangeAttack1, TEXT("the eye's latch at its limit does not fail slot 562 (0x1026def2)"));
		F.Fighter->BaseMemory.EnemyOccludedCheck = 0;

		// A wall on both lines: `0x1024f5c7` raises WEAPON_SIGHT_OCCLUDED and no 0x4f is set.
		F.Services.TraceRetailQuery = [](const FElysiumRetailTrace& Asking, FElysiumRetailTraceResult& Out)
		{
			Out.Fraction = 0.5f;
			Out.EndPosCm = Asking.StartCm + (Asking.EndCm - Asking.StartCm) * 0.5;
			return true;
		};
		const FElysiumNpcConditions Occluded = Gather(400.0, ECond::WeaponSightOccluded,
			TEXT("a walled 0x4f raises WEAPON_SIGHT_OCCLUDED (0x1024f5c7)"));
		TestFalse(TEXT("...nor does WEAPON_BLOCKED_BY_FRIEND"), Occluded.Has(ECond::WeaponBlockedByFriend));
		Gather(1500.0, ECond::TooFarToAttack, TEXT("a walled enemy past 1024 is only TOO_FAR_TO_ATTACK (0x1026ded9)"));

		// A friend on both lines: `0x1024f502` raises WEAPON_BLOCKED_BY_FRIEND by the gather's own
		// path; the timers arm (`0x1026e006`, `0x1026dfe7`) and the tail clears 0x4f (`0x1026e099`).
		if (F.Player != nullptr)
		{
			const double BlockedBefore = F.Fighter->WeaponBlockedByFriendTimer;
			const double ExtendedBefore = F.Fighter->ExtendedBlockedByFriendTimer;
			F.Fighter->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Neutral, 5);
			const FElysiumEntityHandle Friend = F.Player->Handle;
			FVector FirstRayStartCm = FVector::ZeroVector;
			int32 Rays = 0;
			F.Services.TraceRetailQuery = [Friend, &FirstRayStartCm, &Rays](const FElysiumRetailTrace& Asking,
				FElysiumRetailTraceResult& Out)
			{
				if (Rays++ == 0)
				{
					FirstRayStartCm = Asking.StartCm;
				}
				FElysiumRetailTraceCharacter Met;
				Met.Entity = Friend;
				Met.Fraction = 0.5f;
				Out.Characters.Add(Met);
				Out.EndPosCm = Asking.EndCm;
				return true;
			};
			F.Target->Origin = FVector(Cm(400.0), 0.0, 0.0);
			F.Fighter->Cognition.Conditions.Reset();
			ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0);
			const FElysiumNpcConditions Blocked = F.Fighter->Cognition.Conditions;
			TestTrue(TEXT("0x1024f502: a non-hated character on the line raises WEAPON_BLOCKED_BY_FRIEND (0x63)"),
				Blocked.Has(ECond::WeaponBlockedByFriend));
			TestFalse(TEXT("0x1026e099: ...and no CAN_RANGE_ATTACK1"), Blocked.Has(ECond::CanRangeAttack1));
			TestEqual(TEXT("0x1026df3d: both tests ran (two rays)"), Rays, 2);
			TestTrue(TEXT("0x1024f330: the ray leaves from slot 389's point, origin + (0, 0, 55) units"),
				FirstRayStartCm.Equals(F.Fighter->Origin + FVector(0.0, 0.0, Cm(55.0)), 1e-2));
			TestEqual(TEXT("0x1026e006: +0x5b88 = curtime + 1.5"), F.Fighter->WeaponBlockedByFriendTimer, 11.5);
			TestEqual(TEXT("0x1026dfe7: +0x5b8c = curtime + 2.5"), F.Fighter->ExtendedBlockedByFriendTimer, 12.5);
			// The cases below start from the spawn timers again.
			F.Fighter->WeaponBlockedByFriendTimer = BlockedBefore;
			F.Fighter->ExtendedBlockedByFriendTimer = ExtendedBefore;
		}
		F.Services.TraceRetailQuery = nullptr;

		// The `+0x730` timer is the LAST arm: unexpired, slot 365 answers 0 (COND_NONE).
		Weapon->HoldAttacksUntil(50.0);
		{
			F.Target->Origin = FVector(Cm(400.0), 0.0, 0.0);
			F.Fighter->Cognition.Conditions.Reset();
			ElysiumNpcCond::GatherAttackConditions(*F.Fighter, 10.0);
			const FElysiumNpcConditions Cond = F.Fighter->Cognition.Conditions;
			TestTrue(TEXT("an unexpired deadline raises WAITING_ATTACK_TIME (0x2f)"),
				Cond.Has(ECond::WaitingAttackTime));
			for (const ECond Answer : Answers)
			{
				TestFalse(*FString::Printf(TEXT("...and slot 365 answers nothing: no %s"),
					ElysiumNpcCondName(Answer)), Cond.Has(Answer));
			}
		}
		Weapon->NextPrimaryAttackTime = 0.0;

		// `Weapon_Equip`'s spawnflag-0x100 arm pins both max words to 1e9.
		F.Fighter->SpawnFlags |= ElysiumWeapons::LongRangeSpawnflag;
		Weapon->OnEquipped(*F.Fighter);
		TestEqual(TEXT("spawnflag 0x100: +0x8c0 is 1e9"), Weapon->RangeWords.MaxRange1, 1.0e9f);
		TestEqual(TEXT("spawnflag 0x100: +0x8c4 is 1e9"), Weapon->RangeWords.MaxRange2, 1.0e9f);
		Gather(1500.0, ECond::CanRangeAttack1, TEXT("spawnflag 0x100: an enemy past 1024 is shootable"));
	}

	// --- The constructor words of the melee classes -------------------------------------------------
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		if (FElysiumWeapon* Katana = F.ActiveWeapon(F.Fighter))
		{
			TestEqual(TEXT("CWeaponMelee 0x103e9ac0: +0x8b8 is 0"), Katana->RangeWords.MinRange1, 0.f);
			TestEqual(TEXT("CWeaponMelee 0x103e9ac0: +0x8c0 is 50"), Katana->RangeWords.MaxRange1, 50.f);
		}
		const ElysiumWeapons::FRangeWords Tentacle =
			ElysiumWeapons::ConstructorRangeWords(TEXT("item_w_mingxiao_tentacle"), nullptr);
		TestEqual(TEXT("CWeaponMelee_MingXiaoTentacle 0x103ec870: +0x8c0 is 108"), Tentacle.MaxRange1, 108.f);
		const ElysiumWeapons::FRangeWords MingXiao =
			ElysiumWeapons::ConstructorRangeWords(TEXT("item_w_mingxiao_melee"), nullptr);
		TestEqual(TEXT("CWeaponMelee_MingXiaoMelee 0x103ec2b0: +0x8c0 is 500"), MingXiao.MaxRange1, 500.f);
		const ElysiumWeapons::FRangeWords Unarmed =
			ElysiumWeapons::ConstructorRangeWords(TEXT("item_w_unarmed"), nullptr);
		TestEqual(TEXT("CWeaponUnarmed keeps the base 0x10250ac0 +0x8b8 65"), Unarmed.MinRange1, 65.f);
		TestEqual(TEXT("...and +0x8c0 1024"), Unarmed.MaxRange1, 1024.f);
	}

	// --- The four SHOULD_* conditions stay plumbed and unset -------------------------------------
	{
		FCombatFixture F(GKatana);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		F.Fighter->BaseMemory.Enemy = F.Target->Handle;
		// The weapon-sight occlusion is slot 481's `+0x5b98` debounce, which `NPCInit` seeds at its
		// limit of ten (`BaseMemory.EnemyOccludedCheck = 10`); a visible check zeroes it
		// (`0x10270bb1`). This case drives slot 561 alone, so the enemy is stated in sight.
		F.Fighter->BaseMemory.EnemyOccludedCheck = 0;
		// A real notice, delivered: the record is written, and the response policy still raises
		// nothing, because the policy is what is unrecovered.
		TestTrue(TEXT("a swing from 100 cm is inside the recovered 150-unit notice radius"),
			ElysiumNpcCond::NoticeMeleeAttack(*F.Target, F.Fighter->Handle, F.Fighter->Origin, 10.0));
		TestTrue(TEXT("...and the five-second retention holds it"),
			ElysiumNpcCond::HasDetectedAttack(*F.Target, 14.9));
		TestFalse(TEXT("...and expires it"), ElysiumNpcCond::HasDetectedAttack(*F.Target, 15.1));

		F.Target->Cognition.Conditions.Reset();
		ElysiumNpcCond::GatherAttackConditions(*F.Target, 10.5);
		const FElysiumNpcConditions Cond = F.Target->Cognition.Conditions;
		for (const ECond Response : { ECond::ShouldDodge, ECond::ShouldBlock, ECond::ShouldStepback,
			ECond::ShouldKick })
		{
			TestFalse(*FString::Printf(TEXT("%s has no producer and is never set"),
				ElysiumNpcCondName(Response)), Cond.Has(Response));
		}

		// Out of the notice radius and out of sight: refused rather than remembered.
		F.Target->Senses.Memory.DetectedAttackAttacker = FElysiumEntityHandle::Invalid();
		F.Target->Senses.Memory.DetectedAttackTime = -1.0;
		TestFalse(TEXT("a swing from beyond 150 units with no sight route is not noticed"),
			ElysiumNpcCond::NoticeMeleeAttack(*F.Target, F.Fighter->Handle,
				FVector(Cm(900.0), 0.0, 0.0), 11.0));
		TestFalse(TEXT("...and writes no record"),
			ElysiumNpcCond::HasDetectedAttack(*F.Target, 11.0));
	}
	return true;
}


// The two pre-kernel selectors, which are now pass-throughs.
//
// Their CHOSEN, NOT RECOVERED precedence ladders -- dodge, block, the kick/step-back binary draw,
// advance, circle, melee idle on one side; attack, occlusion, distance, back-off on the other --
// are GONE. They existed because the recovered slot bodies answer raw retail numbers and most of
// those numbers named no program this port carried, so the answer had to be folded back to
// something registered. The corpus registers all 691, so the slot body's answer IS the answer, and
// the recovered bodies' own arms are asserted where they belong:
// `Elysium.Arm.NpcKernelSchedule.SelectScheduleMeleeCombat` and
// `Elysium.Arm.NpcKernelCombat10.SelectorAgreement`.
//
// What remains testable here is the composition rule, which is recovered: a selector answering zero
// declines, and `CAI_BaseNPCTroika::SelectSchedule` gets its turn.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatSelectorCompositionTest,
	"Elysium.Arm.NpcCombat.SelectorComposition", GElysiumTestFlags)
bool FElysiumNpcCombatSelectorCompositionTest::RunTest(const FString&)
{
	FCombatFixture F(GPistol);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.CommitToTarget(10.0);

	auto Select = [&F](std::initializer_list<ECond> Conditions)
	{
		F.Fighter->Cognition.Conditions = FElysiumNpcConditions::Of(Conditions);
		return F.Fighter->SelectSchedule();
	};

	// The pre-kernel selector hands the slot body's number through unchanged.
	F.Fighter->Cognition.Conditions = FElysiumNpcConditions::Of({ ECond::CanRangeAttack1 });
	TestEqual(TEXT("the pre-kernel ranged selector is slot 605's answer"),
		ElysiumNpcCombat::SelectRangedSchedule(*F.Fighter, 10.0),
		F.Fighter->SelectScheduleRangedCombat(0));

	// And every number it answers is a program that is actually loaded, which is what retired the
	// fold: an unloaded answer used to be the common case.
	const int32 Ranged = Select({ ECond::CanRangeAttack1 });
	TestTrue(TEXT("...and that number names a loaded program"),
		ElysiumScheduleFor(ElysiumScheduleGlobalId(Ranged)) != nullptr);

	// A zero return falls through to the Troika selector (`0x1038502f JMP 0x10015596`).
	// Story 8 L06 integration (corrected to retail): the fall-through lands in `0x102af660` case 2
	// (COMBAT), not the idle: with no SEE_ENEMY and no ENEMY_OCCLUDED its answer is COMBAT_FACE
	// `0xb` (`0x102afe5d`).
	TestEqual(TEXT("merely waiting on the attack timer falls to the Troika combat face (0x102afe5d)"),
		Select({ ECond::WaitingAttackTime }), 0xb);
	// The damage arm (`0x102afcb5`..`0x102afcdb`) flinches only when `SelectWeightedSequence
	// (ACT_SMALL_FLINCH 0x49)` finds a sequence; this substrate's `SelectWeightedSequenceForActivity`
	// is a seam answering -1 (no sequence index at the kernel tier), so it too faces.
	TestEqual(TEXT("no ACT_SMALL_FLINCH sequence is found"),
		F.Fighter->SelectWeightedSequenceForActivity(0x49), static_cast<int32>(INDEX_NONE));
	TestEqual(TEXT("...so heavy damage also reaches COMBAT_FACE (0x102afcdb JZ -> 0x102afe5d)"),
		Select({ ECond::WaitingAttackTime, ECond::HeavyDamage }), 0xb);
	return true;
}


// The swing program: the notice, the weapon press, the damage that lands through the
// cycle-1 commit, and the recovered empty mask that owns the NPC while it runs.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatSwingTest,
	"Elysium.Arm.NpcCombat.Swing", GElysiumTestFlags)
bool FElysiumNpcCombatSwingTest::RunTest(const FString&)
{
	FCombatFixture F(GKatana);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.CommitToTarget(0.0);
	FElysiumNpcWorldFixture::GatherConditionsAt(*F.Fighter, 0.0);
	// Story 8 wave 2 (the retail pass): the Troika `FCanCheckAttacks` (`0x102953a0`) refuses for a
	// melee-armed body that is not yet `m_bInMelee` (+0x6078), so slot 481 runs slot 560's clear and
	// the full pass raises no CAN_MELEE_ATTACK1; the port's old gather ran the attack conditions
	// unconditionally. The melee ladder takes its approach from SEE_ENEMY regardless (below).
	TestFalse(TEXT("0x102953a0 a melee body not in melee gathers no CAN_MELEE_ATTACK1"),
		F.Fighter->Cognition.Conditions.Has(ECond::CanMeleeAttack1));
	// The recovered slot-604 body's own answer (`CNPC_VHuman::SelectScheduleMeleeCombat
	// 0x10385e40`). Corrected to retail by spec 0002 V11: this pinned `0xe7
	// SCHED_TROIKA_WAIT_FOR_MELEE_ADVANCE`, the arm slot 599 `0x10385ab0` REFUSING takes, which it
	// did only because the attack coordinator was a seam answering false. Every Troika NPC binds
	// "Normal" at Precache (`0x10298ad0` -> slot 608), `0x1025db70` admits a lone NPC to its list,
	// `m_bInMelee` is set, and with no band word gathered yet (above) the same selection answers
	// `0xc7 SCHED_TROIKA_MELEE_IDLE` (line `0x6c1`): one MELEE_IDLE first is retail.
	TestEqual(TEXT("0x10385e40: admitted by 0x1025db70, no band word yet -> MELEE_IDLE 0xc7"),
		F.Fighter->SelectSchedule(), 0xc7);
	TestTrue(TEXT("0x10385ab0: the admission set m_bInMelee"), F.Fighter->bInMelee);

	// The swing's contact is the per-frame swept walk over the clip's own authored records, and
	// retail runs it on the CHARACTER — so an NPC's swing reaches contact through exactly the pass
	// the player's does. Arming the seam before the press is what gives the transaction a clip whose
	// records the walk can read.
	{
		F.Services.bNpcActivitiesResolve = true;
		F.Services.ResolvedNpcActivityLabel = TEXT("swing_long");
		F.Services.ResolvedNpcActivityClip = TEXT("swing_long");
		F.Services.ResolvedNpcActivityOwner = TEXT("cast_bank");
		F.Services.bBodyClipPhaseSet = true;
		F.Services.BodyClipPhase = FElysiumClipPhase();
		F.Services.BodyClipPhase.OwnerStem = TEXT("cast_bank");
		F.Services.BodyClipPhase.Label = TEXT("swing_long");
		F.Services.BodyClipPhase.Length = 1.0f;
		F.Services.BodyClipPhase.PlayId = 1;
		F.Services.BoneFrames.Add(TEXT("bip01 r hand"), FTransform::Identity);
		FElysiumSwingRecord Record;
		Record.Start = 0.30f;
		Record.End = 0.70f;
		Record.Bone = TEXT("Bip01 R Hand");
		Record.BCm = FVector(30.f, 0.f, 0.f);
		F.Services.SwingsByClip.Add(TEXT("swing_long"), { Record });
		F.Services.SwingContacts = { F.Target->Handle };
		F.Services.SeedFixtureActivity(TEXT("ACT_MELEE_ATTACK_KATANA"), 10);
		F.Services.SeedFixtureActivity(TEXT("ACT_MELEE_ATTACK"), 11);
		for (int32 AttackRawIndex : {10, 11})
		{
			auto& AttackClip = F.Services.BodyClipsByRawIndex[AttackRawIndex].Clip;
			AttackClip.LowReachCm = 0.f; AttackClip.ReachCm = 10000.f;
			FElysiumMeleeEnvelope AttackEnvelope;
			AttackEnvelope.Min = FVector(-10000.f, -10000.f, -10000.f);
			AttackEnvelope.Max = FVector(10000.f, 10000.f, 10000.f);
			AttackClip.Envelopes.Add(AttackEnvelope);
		}
	}

	// The whole approach plus its transfer to the terminal swing runs inside one think: face, stop,
	// transfer, announce, attack.
	TestTrue(TEXT("the approach starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, *F.Fighter));
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 0.0,
		&F.Fighter->Cognition.Conditions);

	// Story 8 wave 2: `TASK_ANNOUNCE_ATTACK`'s arm is base `0x10286cd9`, `TaskComplete` alone; the
	// port's op verb that also wrote the victim's detected-attack record was a twin. The record's
	// producer is the swing's opposed staging (the weapon's melee callback), not the announce.
	TestFalse(TEXT("0x10286cd9 TASK_ANNOUNCE_ATTACK writes no detected-attack record"),
		F.Target->Senses.Memory.DetectedAttackAttacker == F.Fighter->Handle);

	FElysiumWeapon* Weapon = F.ActiveWeapon(F.Fighter);
	if (!TestNotNull(TEXT("the fighter holds its katana"), Weapon))
	{
		return false;
	}
	TestTrue(TEXT("TASK_MELEE_ATTACK1 staged a real weapon transaction"), Weapon->Swing.bActive);
	TestEqual(TEXT("...aimed at the committed enemy"), Weapon->Swing.Opponent, F.Target->Handle);
	TestTrue(TEXT("...and held the next-attack deadline"), Weapon->NextPrimaryAttackTime > 0.0);
	// The packet inflictor is the weapon, but retail FOLLOW makes its absolute origin the live
	// owner origin. Put the loose/pickup field somewhere impossible to catch an accidental read.
	Weapon->Origin = FVector(Cm(9000.f), Cm(9000.f), 0.f);

	// Nothing is scheduled: the accept opens no window and the clock alone commits nothing.
	TestEqual(TEXT("no damage lands inside the accepted swing"), F.DamageTaken(F.Target), 0);
	F.Flush(2.0);
	TestEqual(TEXT("...and none from the clock either — melee estimates nothing"),
		F.DamageTaken(F.Target), 0);
	// Two walked frames on the same pass the player's swing takes: the first stages the opposed
	// record and the notice, the second carries the cycle into the authored window.
	F.Services.BodyClipPhase.Cycle = 0.0f;
	ElysiumTestMeleeStep(F.World, F.Services.BodyClipPhase);
	F.Services.BodyClipPhase.Cycle = 0.50f;
	ElysiumTestMeleeStep(F.World, F.Services.BodyClipPhase);
	TestTrue(TEXT("the contact commits damage through the typed health commit"),
		F.DamageTaken(F.Target) > 0);
	if (TestTrue(TEXT("the real weapon contact produced anonymous damage memory"),
		F.Target->EnemyMemory.Num() > 0))
	{
		const FElysiumNpcEnemyMemoryRecord& DamageRecord = F.Target->EnemyMemory.Records()[0];
		TestTrue(TEXT("...as a position-only record for the unknown attacker"),
			DamageRecord.bPositionOnly);
		TestEqual(TEXT("...at the moving owner rather than weapon pickup origin"),
			DamageRecord.LastPosition, F.Fighter->Origin);
	}
	F.Flush(2.5);
	TestEqual(TEXT("...and OnDamaged fires from its real producer"),
		SaveTestCounterValue(F.World.FindByName(TEXT("damagedcount"))), 1.0f);

	// The recovered EMPTY mask: once the terminal task owns the NPC it is not reevaluated.
	const FElysiumScheduleProgram* Swing = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING));
	if (TestNotNull(TEXT("the swing program is loaded"), Swing))
	{
		TestTrue(TEXT("its recovered interrupt mask is empty"), Swing->Interrupts.IsEmpty());
	}
	const FElysiumNpcConditions Storm = FElysiumNpcConditions::Of({
		ECond::NewEnemy, ECond::HeavyDamage, ECond::LightDamage, ECond::EnemyDead });
	const int32 SerialBefore = Weapon->Swing.Serial;

	// The control, so "the mask holds" is a statement about the mask and not about the kernel: with
	// a mask installed the same storm ends the program before its terminal task can run.
	{
		ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING,
			FElysiumNpcConditions::Of({ ECond::NewEnemy }));
		TestTrue(TEXT("the swing program restarts"),
			ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, *F.Fighter));
		F.Fighter->Cognition.Conditions = Storm;
		// Fixture correction: `10281340` invalidates the swing, then `10281b89` selects and starts
		// the replacement in this same loop. The install's `10280e7x` condition clear is the durable
		// proof that the mask fired even when the combat selector returns to an attack immediately.
		TestTrue(TEXT("with a mask installed, NEW_ENEMY reselects in the same loop"),
			ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 2.5,
				&F.Fighter->Cognition.Conditions));
		TestFalse(TEXT("the replacement install clears NEW_ENEMY"),
			F.Fighter->Cognition.Conditions.Has(ECond::NewEnemy));
	}

	// The registered posture: no interrupts, so the same storm cannot stop the swing.
	TestTrue(TEXT("the swing program restarts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, *F.Fighter));
	F.Fighter->Cognition.Conditions = Storm;
	// Read BEFORE the tick: the program ends on this pass -- `TASK_ANNOUNCE_ATTACK`, the swing's
	// first task in retail's own text, has no enemy to announce to in this fixture -- and after it
	// ends the effective mask is the fail route's, not the swing's.
	const FElysiumNpcConditions SwingMask =
		ElysiumSchedule::EffectiveInterrupts(F.Fighter->Schedule, *F.Fighter);
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 2.5,
		&F.Fighter->Cognition.Conditions);
	TestTrue(TEXT("a NEW_ENEMY mid-swing does not abort the terminal attack"),
		Weapon->Swing.Serial > SerialBefore);
	// The mask is empty, so nothing INTERRUPTS. The program still ends on this pass, because
	// `TASK_ANNOUNCE_ATTACK` -- the swing's first task in retail's own text -- has no enemy to
	// announce to in this fixture and fails, and the fail route's install clears the conditions.
	// The claim is about the mask, so it is made of the mask.
	TestTrue(TEXT("...and the empty mask lists nothing the storm could fire"),
		SwingMask.Intersection(Storm).IsEmpty());
	return true;
}


// Interrupts: the chase admits ENEMY_DEAD, and the starvation gate no longer blocks it.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatInterruptTest,
	"Elysium.Arm.NpcCombat.ChaseInterrupt", GElysiumTestFlags)
bool FElysiumNpcCombatInterruptTest::RunTest(const FString&)
{
	FCombatFixture F(GPistol);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	F.Target->Origin = FVector(Cm(GPistolRangeUnits + 2000.0), 0.0, 0.0);
	F.CommitToTarget(10.0);
	FElysiumNpcWorldFixture::GatherConditionsAt(*F.Fighter, 10.0);

	TestTrue(TEXT("the chase starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_CHASE_ENEMY, *F.Fighter));
	TestTrue(TEXT("the chase reaches its movement watch"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0,
			&F.Fighter->Cognition.Conditions));

	// The recovered mask: a chase interrupts on a new, dead, unreachable, occluded or lost enemy and
	// on any newly available attack. "Pathing cannot monopolize an attack-ready NPC."
	if (const FElysiumScheduleProgram* Chase = ElysiumScheduleFor(ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_CHASE_ENEMY)))
	{
		for (const ECond Admitted : { ECond::NewEnemy, ECond::EnemyDead, ECond::EnemyUnreachable,
			ECond::EnemyOccluded, ECond::LostEnemy, ECond::CanMeleeAttack1, ECond::CanRangeAttack1,
			ECond::TooCloseToAttack })
		{
			TestTrue(*FString::Printf(TEXT("the chase admits %s"), ElysiumNpcCondName(Admitted)),
				Chase->Interrupts.Has(Admitted));
		}
	}
	// ...and the enemy transaction's own gate reads that mask, so a dead enemy is not starved:
	// `ChooseEnemy` (`0x10279dd0`) asks `ConditionInterruptsCurrentSchedule` (`0x10269c70`, the mask
	// alone) for ENEMY_DEAD at `0x10279ed9` (the port-only `IsScheduleInterested` is gone).
	TestTrue(TEXT("the starvation gate no longer blocks a dead enemy mid-chase"),
		ElysiumSchedule::MaskHasCondition(F.Fighter->Schedule, *F.Fighter, ECond::EnemyDead));

	F.Fighter->Cognition.Conditions = FElysiumNpcConditions::Of({ ECond::EnemyDead });
	// Fixture correction: `10281340` invalidates before task work, but `10281b89` reselects and
	// `10281be5` installs the answer in the same iteration; the observable boundary is the install's
	// condition clear, not an empty schedule returned to the caller.
	TestTrue(TEXT("ENEMY_DEAD mid-chase reselects in the same loop"),
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.1,
			&F.Fighter->Cognition.Conditions));
	TestTrue(TEXT("selection installed a replacement rather than the fail route"),
		F.Fighter->Schedule.IsRunning());
	TestFalse(TEXT("the replacement install consumed ENEMY_DEAD"),
		F.Fighter->Cognition.Conditions.Has(ECond::EnemyDead));
	return true;
}


// Live acquisition: the disposition idle now admits NEW_ENEMY, so a standing NPC can be
// taken into combat between one think and the next.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatIdleAcquisitionTest,
	"Elysium.Arm.NpcCombat.IdleAcquisition", GElysiumTestFlags)
bool FElysiumNpcCombatIdleAcquisitionTest::RunTest(const FString&)
{
	// The two idle masks, as retail's own texts declare them. This block used to assert a mask
	// CHOSEN off the interrupt census -- `NEW_ENEMY`, the damage pair, `ENEMY_DEAD` and the hear
	// family -- and the census guess was close on the first two and wrong on the rest: neither
	// program admits `COND_ENEMY_DEAD` or any `HEAR_*` condition, and `SCHED_TROIKA_IDLE_DISPOSITION`
	// admits `COND_GIVE_WAY`, `COND_INVESTIGATE_SOUND`, `COND_INVESTIGATE_SIGHT`,
	// `COND_IGNORE_UNKNOWN`, `COND_DETECTED_ATTACK` and `COND_PLAYER_ON_HEAD` instead.
	for (const EId Idle : { ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION,
		ElysiumSched::SCHED_TROIKA_ALERT_LOOK_AROUND_NI })
	{
		const FElysiumScheduleProgram* Program =
			ElysiumScheduleFor(ElysiumScheduleGlobalId(Idle));
		if (!TestNotNull(TEXT("the idle program is loaded"), Program))
		{
			return false;
		}
		const FString Name = ElysiumScheduleName(ElysiumScheduleGlobalId(Idle));
		// The one condition live acquisition turns on, which is why this case exists at all.
		TestTrue(*FString::Printf(TEXT("%s admits NEW_ENEMY"), *Name),
			Program->Interrupts.Has(ECond::NewEnemy));
		TestTrue(TEXT("...and the damage pair"),
			Program->Interrupts.Has(ECond::LightDamage)
				&& Program->Interrupts.Has(ECond::HeavyDamage));
		TestFalse(TEXT("...and NOT ENEMY_DEAD, which the census guess added"),
			Program->Interrupts.Has(ECond::EnemyDead));
		TestFalse(TEXT("...nor the hear family"),
			Program->Interrupts.Has(ECond::HearCombat)
				|| Program->Interrupts.Has(ECond::HearDanger));
		// `COND_PLAYER_ON_HEAD` is one of the 164 condition names the corpus registers and
		// `EElysiumNpcCond` does not spell. It is carried as a global ORDINAL, which is the whole
		// reason the mask has an ordinal-addressed face.
		const int32 OnHead = FElysiumScheduleCorpus::Get()
			.Namespace(EElysiumIdCategory::Condition).Find(TEXT("COND_PLAYER_ON_HEAD"));
		if (TestTrue(TEXT("COND_PLAYER_ON_HEAD is a registered condition"), OnHead != INDEX_NONE))
		{
			TestTrue(TEXT("...and the idle mask declares it, though this runtime cannot spell it"),
				Program->Interrupts.HasOrdinal(OnHead - ElysiumScheduleId::GlobalBase));
		}
	}

	FCombatFixture F(GKatana);
	if (F.Fighter == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();

	// A hostile player standing in front of the idling NPC, seen.
	F.Player->Origin = FVector(Cm(200.0), 0.0, 0.0);
	F.Fighter->Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	F.Fighter->Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Fighter->Senses.Memory.bPlayerVisible = true;
	F.Fighter->Senses.Memory.bPlayerInCone = true;
	F.Fighter->Senses.Memory.bPlayerInRange = true;

	TestTrue(TEXT("the NPC is running its disposition idle"),
		ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, *F.Fighter));

	F.Fighter->Senses.TickSight(*F.Fighter, 20.0);
	FElysiumNpcWorldFixture::GatherConditionsAt(*F.Fighter, 20.0);
	TestTrue(TEXT("the idle program does not starve the acquisition"),
		F.Fighter->BaseMemory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...and the pass raises NEW_ENEMY"),
		F.Fighter->Cognition.Conditions.Has(ECond::NewEnemy));

	// `MaintainSchedule 0x102817c0` is where both halves live, in this order: `IsScheduleValid`
	// (`0x10280ff0`) answers false on the interrupt, THEN `FUN_1026f4d0` dispatches slot 461 and
	// `SetState(m_IdealNPCState)` commits. `m_pSchedule` (`+0x5c38`) is still installed for both,
	// which is why `HasInterruptCondition` (`0x10269d30`) can answer at all.
	TestTrue(TEXT("NEW_ENEMY interrupts the disposition idle"),
		ElysiumSchedule::HasInterruptCondition(F.Fighter->Schedule, *F.Fighter,
			F.Fighter->Cognition.Conditions, ECond::NewEnemy));
	F.Fighter->UpdateIdealState(20.0);
	TestTrue(TEXT("a committed enemy takes the NPC to combat"),
		F.Fighter->GetMind().State() == EElysiumNpcState::Combat);
	TestNotEqual(TEXT("...and combat selects a fight program, not the idle it just left"),
		F.Fighter->SelectSchedule(), ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION);
	return true;
}


// Retaliation: the whole chain from a struck neutral bystander to a pressed swing.
//
// Step 3 of the recovered NPC damage-to-AI transaction is "records the attack position and
// attacker, updates enemy memory" (`docs/vtmb/combat-and-damage.md` -> "NPC damage response and
// stagger boundaries", step 3). The record is CAI_Memory, not a derived relationship; this test
// exercises the ordinary sight admission that follows a visible hit.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatRetaliationTest,
	"Elysium.Arm.NpcCombat.Retaliation", GElysiumTestFlags)
bool FElysiumNpcCombatRetaliationTest::RunTest(const FString&)
{
	// The authored `0` sentinel: nothing equipped, so the fists fallback is what arms the victim.
	// That is the commonest hostile-capable shape in the corpus and the one a bystander carries.
	FCombatFixture F(TEXT("0"));
	if (F.Target == nullptr || F.Fighter == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();

	FElysiumNpc& Victim = *F.Target;
	// The player stands inside the victim's own swing reach and cone. The victim spawned at 100 cm
	// facing +X, so a position further along +X is the faced half-space.
	F.Player->Origin = Victim.Origin + FVector(Cm(20.0), 0.0, 0.0);

	// --- The starting state: an ordinary neutral bystander ---------------------------------------
	TestEqual(TEXT("the fists fallback armed the victim"),
		ElysiumNpcCond::WeaponCapability(Victim), ElysiumNpcCond::ECapability::Melee);
	TestEqual(TEXT("the victim starts neutral toward the player"),
		static_cast<int32>(Victim.Relationships.Resolve(F.Player->Handle, TEXT("player"))),
		static_cast<int32>(EElysiumRelationship::Neutral));
	TestFalse(TEXT("...and holds no enemy"), Victim.BaseMemory.Enemy.IsSet());
	// Damage does not make this relationship hostile. Give the fixture the authored D_HT row a
	// combatant has, then verify the damage path writes its existing actor into CAI_Memory.
	Victim.Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	TestTrue(TEXT("the victim is running its disposition idle"),
		ElysiumSchedule::Start(Victim.Schedule, ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION, Victim));

	// --- The hit, through the one typed health commit --------------------------------------------
	// A second later than the loadout thinks, so the packet is NEW to the pass that follows: the
	// damage condition is edge-triggered against the previous gather's timestamp.
	F.Flush(1.0);
	FElysiumDmg Dmg;
	Dmg.Family = EElysiumDmgFamily::Bashing;
	Dmg.Flags = ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = 10;
	Dmg.Source = F.Player->Handle;
	Victim.TakeDamage(Dmg, F.Player);

	if (!TestTrue(TEXT("the punch committed damage"), F.DamageTaken(&Victim) > 0))
	{
		return false;
	}
	TestTrue(TEXT("the commit recorded the attacker"),
		Victim.BaseMemory.LastDamageAttacker == F.Player->Handle);
	TestEqual(TEXT("...and retains the authored hostile eligibility"),
		static_cast<int32>(Victim.Relationships.Resolve(F.Player->Handle, TEXT("player"))),
		static_cast<int32>(EElysiumRelationship::Hate));
	TestNull(TEXT("...but a cone-visible attacker does not duplicate sight memory"),
		Victim.EnemyMemory.Find(F.Player->Handle));

	// --- One decision pass ------------------------------------------------------------------------
	// Drive the actual sensory producer after moving the player. `GatherConditions` consumes the
	// cached result; it does not itself run the Look cadence.
	Victim.Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	// (Sensing runs inside slot 433: `PerformSensing` `0x1026e4f0` at `0x1026ee04`.)
	FElysiumNpcWorldFixture::GatherConditionsAt(Victim, 1.0);
	TestTrue(TEXT("the pass raises the damage condition"),
		Victim.Cognition.Conditions.Has(ECond::LightDamage));
	TestTrue(TEXT("...commits the attacker as the enemy"),
		Victim.BaseMemory.Enemy == F.Player->Handle);
	TestTrue(TEXT("...raises NEW_ENEMY"), Victim.Cognition.Conditions.Has(ECond::NewEnemy));
	// Corrected to retail: the Troika's slot 564 `FCanCheckAttacks` (`0x102953a0`) refuses a
	// melee-capable body (slot 513 bit `0x8000`) holding an active weapon while `m_bInMelee`
	// (`+0x6078`) is clear, so slot 481 runs `ClearAttackConditions` (`0x102711fa`) instead of the
	// attack gather: melee range alone raises no CAN_MELEE_ATTACK1 before the melee coordinator
	// (slot 600) has admitted the body. (The port's old gather raised it on range and facing.)
	TestFalse(TEXT("...but an armed melee body not yet in melee gathers no attack (0x102953a0)"),
		Victim.Cognition.Conditions.Has(ECond::CanMeleeAttack1));

	// The interrupt stands against the installed mask — `IsScheduleValid 0x10280ff0` answers false
	// — before `MaintainSchedule 0x102817c0` runs slot 461 and commits with `SetState`.
	TestTrue(TEXT("the damage interrupts the disposition idle"),
		ElysiumSchedule::HasInterruptCondition(Victim.Schedule, Victim,
			Victim.Cognition.Conditions, ECond::LightDamage));
	Victim.UpdateIdealState(1.0);
	TestTrue(TEXT("the ideal-state pass takes the struck bystander to combat"),
		Victim.GetMind().State() == EElysiumNpcState::Combat);
	// Story 8 L06 integration (corrected to retail): the pass left NEW_ENEMY standing, and the
	// pre-selector `0x102ae920` answers it first in COMBAT: `0x102aedf0` HasCondition(NEW_ENEMY),
	// not frenzied (`0x102aee01`) -> START_COMBAT `0xea` (`0x102aee03`), ahead of slot 438.
	TestEqual(TEXT("...and combat selection starts the fight (START_COMBAT 0xea, 0x102aee03)"),
		Victim.SelectSchedule(), 0xea);

	// --- The terminal task presses the same weapon transaction the player uses --------------------
	FElysiumWeapon* Fists = F.ActiveWeapon(&Victim);
	if (!TestNotNull(TEXT("the victim holds its fists"), Fists))
	{
		return false;
	}
	// The next decision pass, in the order `MaintainSchedule 0x102817c0` runs it: the packet that started the
	// fight is no longer new, so the approach's own `LIGHT_DAMAGE` interrupt no longer fires and the
	// program reaches its terminal swing. Re-gathering rather than reusing the selection pass's
	// conditions is what makes that sequencing part of the assertion. The first pass ends as RunAI's
	// does: its end-of-pass clear takes LIGHT/HEAVY_DAMAGE (`0x1026f311` / `0x1026f31a`), which is
	// what gives the packet's bits their one-pass life (corrected to retail: the deleted twin rebuilt
	// that edge from a gather timestamp).
	Victim.Cognition.Conditions.Clear(ECond::LightDamage);
	Victim.Cognition.Conditions.Clear(ECond::HeavyDamage);
	FElysiumNpcWorldFixture::GatherConditionsAt(Victim, 1.1);
	TestFalse(TEXT("the damage packet is not gathered twice"),
		Victim.Cognition.Conditions.Has(ECond::LightDamage));
	TestTrue(TEXT("...and the enemy stays committed"),
		Victim.BaseMemory.Enemy == F.Player->Handle);

	const int32 SerialBefore = Fists->Swing.Serial;
	TestTrue(TEXT("the swing program starts"),
		ElysiumSchedule::Start(Victim.Schedule, ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, Victim));
	ElysiumSchedule::Tick(Victim.Schedule, Victim, 1.1, &Victim.Cognition.Conditions);
	TestTrue(TEXT("TASK_MELEE_ATTACK1 staged a real weapon transaction"), Fists->Swing.bActive);
	TestTrue(TEXT("...pressing the controller rather than reporting one"),
		Fists->Swing.Serial > SerialBefore);
	TestTrue(TEXT("...aimed at the player who struck it"),
		Fists->Swing.Opponent == F.Player->Handle);

	// --- The refusal arm: an authored relationship outranks the derived one -----------------------
	// `SetEntity` replaces an existing target only at an equal-or-higher priority, so a character the
	// map authored as friendly stays friendly through a punch. That is an authored decision beating a
	// derived one, and it is the same property the law lane's attack arm carries.
	FElysiumNpc& Friend = *F.Fighter;
	Friend.Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Like, 10);
	FElysiumDmg Second = Dmg;
	Friend.TakeDamage(Second, F.Player);
	TestTrue(TEXT("the authored character still took the damage"), F.DamageTaken(&Friend) > 0);
	TestEqual(TEXT("...but the derived D_HT row was refused"),
		static_cast<int32>(Friend.Relationships.Resolve(F.Player->Handle, TEXT("player"))),
		static_cast<int32>(EElysiumRelationship::Like));
	TestEqual(TEXT("...and a row that can never win was not stored at all"),
		Friend.Relationships.NumDerivedRules(), 0);
	FElysiumNpcWorldFixture::GatherConditionsAt(Friend, 1.0);
	TestFalse(TEXT("...so it acquires no enemy"), Friend.BaseMemory.Enemy.IsSet());

	// --- The arms with no attacker to remember ---------------------------------------------------
	// Three real producers commit positive damage that names no combat character to become hostile
	// toward: `trigger_hurt` and a crushing mover carry their own logic entity as the descriptor's
	// source, the scalar `TakeDamage(float)` compatibility input carries none at all, and a character
	// hurting itself names itself. All three must commit the damage and install nothing — a
	// `trigger_hurt` that turned a room's cast hostile would be the sharpest way for this to go wrong.
	const int32 RulesBefore = Friend.Relationships.NumEntityRules();
	const int32 DamageBefore = F.DamageTaken(&Friend);
	FElysiumDmg FromNowhere = Dmg;
	FromNowhere.Source = FElysiumEntityHandle::Invalid();
	Friend.TakeDamage(FromNowhere, nullptr);
	FElysiumDmg FromSelf = Dmg;
	FromSelf.Source = Friend.Handle;
	Friend.TakeDamage(FromSelf, nullptr);
	Friend.TakeDamage(10.f);
	TestTrue(TEXT("the source-less and self-inflicted hits still committed damage"),
		F.DamageTaken(&Friend) > DamageBefore);
	TestEqual(TEXT("...and none of them installed a relationship row"),
		Friend.Relationships.NumEntityRules(), RulesBefore);
	TestEqual(TEXT("...on either surface"), Friend.Relationships.NumDerivedRules(), 0);
	return true;
}


// Damage updates CAI_Memory, not a temporary relationship. The Troika-specific five-second timer
// is the sight-range override `+0x6604`, a later sensing producer; it is not selection decay.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatRetaliationExpiryTest,
	"Elysium.Arm.NpcCombat.RetaliationExpiry", GElysiumTestFlags)
bool FElysiumNpcCombatRetaliationExpiryTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"));
	if (F.Target == nullptr || F.Player == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();

	FElysiumNpc& Victim = *F.Target;
	F.Player->Origin = Victim.Origin + FVector(Cm(20.0), 0.0, 0.0);
	Victim.Relationships.SetEntity(F.Player->Handle, EElysiumRelationship::Hate, 5);
	// The headless body has no stance clips, so its idle program failed into base `FAIL` (story 25),
	// whose mask withholds `NEW_ENEMY` and so starves `ChooseEnemy` — retail's own gate. This case is
	// about the memory record, so it runs with no program installed.
	Victim.Schedule.Clear();

	FElysiumDmg Dmg;
	Dmg.Family = EElysiumDmgFamily::Bashing;
	Dmg.Flags = ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = 10;
	Dmg.Source = F.Player->Handle;

	F.Flush(1.0);
	Victim.TakeDamage(Dmg, F.Player);
	TestNull(TEXT("the visible attacker waits for the ordinary sight writer"),
		Victim.EnemyMemory.Find(F.Player->Handle));
	TestEqual(TEXT("damage did not fabricate a relationship row"), Victim.Relationships.NumDerivedRules(), 0);
	Victim.Senses.Memory.PlayerLosNextUpdateTime = -1.0;
	// (Sensing runs inside slot 433: `PerformSensing` `0x1026e4f0` at `0x1026ee04`.)
	FElysiumNpcWorldFixture::GatherConditionsAt(Victim, 1.0);
	TestNotNull(TEXT("the subsequent sight pass writes the actor record"),
		Victim.EnemyMemory.Find(F.Player->Handle));
	TestTrue(TEXT("the pass commits the attacker"), Victim.BaseMemory.Enemy == F.Player->Handle);
	FElysiumNpcWorldFixture::GatherConditionsAt(Victim, 1000.0);
	TestNotNull(TEXT("the actor record has no time expiry"), Victim.EnemyMemory.Find(F.Player->Handle));
	TestTrue(TEXT("...and the hostile enemy remains committed"),
		Victim.BaseMemory.Enemy == F.Player->Handle);
	return true;
}


// The derived row is session state: it does not travel in a save, and the rows beside it do.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatRetaliationSaveTest,
	"Elysium.Arm.NpcCombat.RetaliationSave", GElysiumTestFlags)
bool FElysiumNpcCombatRetaliationSaveTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"));
	// The destination world: the catalogue is installed once per process, so the second fixture
	// borrows the first one's. Both stand before the round trip, because since 0019/2 pass C the
	// relationship store's handles are re-stamped by `OnPostRestore` (retail's slot 130) rather
	// than by the leaf blob, and only `Freeze`/`ApplySnapshot` runs it.
	FCombatFixture G(TEXT("0"), /*bWithFists*/ true, /*bInstallCatalogue*/ false);
	if (F.Target == nullptr || F.Fighter == nullptr || F.Player == nullptr
		|| G.Target == nullptr || G.Fighter == nullptr || G.Player == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();

	FElysiumNpc& Victim = *F.Target;
	// An authored/scripted `D_HT` toward another character. Damage no longer inserts a derived
	// relationship, so it is not a second relationship-store row.
	Victim.Relationships.SetEntity(F.Fighter->Handle, EElysiumRelationship::Hate, 5);
	FElysiumDmg Dmg;
	Dmg.Family = EElysiumDmgFamily::Bashing;
	Dmg.Flags = ElysiumDamage::FlagDirectInput;
	Dmg.ExtraInput = 10;
	Dmg.Source = F.Player->Handle;
	F.Flush(1.0);
	Victim.TakeDamage(Dmg, F.Player);
	TestEqual(TEXT("damage leaves the one authored relationship row intact"),
		Victim.Relationships.NumEntityRules() * 10 + Victim.Relationships.NumDerivedRules(), 10);

	// A live stimulus on the DESTINATION store, because clearing one is the direction the load's own
	// reset exists for — an already-empty array would be left alone by a load that did nothing at
	// all. This is the fight the player was in before the slot was loaded, and a restored character
	// still swinging over it is what the reset prevents.
	G.Target->Relationships.SetDerivedEntity(G.Player->Handle, EElysiumRelationship::Hate,
		5, /*ExpiresAt*/ 1000.0);
	TestEqual(TEXT("the destination relationship store holds a transient row before the load"),
		G.Target->Relationships.NumDerivedRules(), 1);

	ElysiumRoundTripSnapshot(F.World, G.World);

	const FElysiumRelationships& Restored = G.Target->Relationships;
	TestEqual(TEXT("the authored hate row survived the round trip"),
		static_cast<int32>(Restored.Resolve(G.Fighter->Handle, TEXT("npc_VHumanCombatant"))),
		static_cast<int32>(EElysiumRelationship::Hate));
	TestEqual(TEXT("...as the one saved row"), Restored.NumEntityRules(), 1);
	TestEqual(TEXT("the damage memory did not, and the destination's own was cleared"),
		Restored.NumDerivedRules(), 0);
	TestEqual(TEXT("...so the restored character is neutral toward the player again"),
		static_cast<int32>(Restored.Resolve(G.Player->Handle, TEXT("player"))),
		static_cast<int32>(EElysiumRelationship::Neutral));
	return true;
}


// Running away: the projected retreat, and its fail path when the world will not have it.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatRunAwayTest,
	"Elysium.Arm.NpcCombat.RunAway", GElysiumTestFlags)
bool FElysiumNpcCombatRunAwayTest::RunTest(const FString&)
{
	// --- The projected retreat ------------------------------------------------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Fighter);
		if (!TestNotNull(TEXT("the fighter owns a recording motor"), Motor))
		{
			return false;
		}
		// 125 units: past slot 365's 100-unit `TOO_CLOSE_FOR_RANGED` edge, inside the firearm's
		// `m_fMinRange1` 150 (`CWeaponRanged 0x10238070`, 0018 story 8).
		F.Target->Origin = FVector(Cm(125.0), 0.0, 0.0);
		F.CommitToTarget(10.0);
		FElysiumNpcWorldFixture::GatherConditionsAt(*F.Fighter, 10.0);
		TestTrue(TEXT("an enemy inside the firearm's minimum range is too close to shoot"),
			F.Fighter->Cognition.Conditions.Has(ECond::TooCloseToAttack));
		// `ShouldDodgeRangedAttack` (`0x102b7f40`) rolls under 75 at `0x102b7f5f` unless
		// COND_STOP_BACKUP (0x2c) stands; the condition pins that arm shut so the answer does not ride
		// the schedule stream's draw (the discipline seam and WEAPON_THROUGH_WALL answer no).
		F.Fighter->Cognition.Conditions.Set(ECond::StopBackup);
		// 0xf0 `SCHED_TROIKA_FORCED_RANGE_ATTACK1` is the recovered slot-605 answer here.
		TestEqual(TEXT("...and the ranged selector answers its recovered program"),
			F.Fighter->SelectSchedule(), 0xf0);

		// Retail's `SCHED_TROIKA_RUN_AWAY_FROM_ENEMY` is not the three-task retreat this port
		// invented. It is `TASK_SET_FAIL_SCHEDULE SCHEDULE:SCHED_TROIKA_STANDOFF`, two
		// `TASK_SET_NPC_FLAG`s, `TASK_STOP_MOVING`, `TASK_SET_TOLERANCE_DISTANCE 24`, then
		// `TASK_STORE_ENEMY_POSITION_IN_SAVEPOSITION` and `TASK_FIND_BACKAWAY_FROM_SAVEPOSITION` --
		// so the enemy stamp the port's SELECTOR poked into `m_vSavePosition` is a TASK in the game,
		// and the retreat itself is a task this runtime has no body for.
		TestTrue(TEXT("the retreat starts"),
			ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_RUN_AWAY_FROM_ENEMY, *F.Fighter));
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0,
			&F.Fighter->Cognition.Conditions);
		// Its first task names its own fail route, which is the rest of the program's story here:
		// `TASK_STORE_ENEMY_POSITION_IN_SAVEPOSITION` and `TASK_FIND_BACKAWAY_FROM_SAVEPOSITION`
		// have no body in this runtime, so the program stops and takes that route.
		TestEqual(TEXT("TASK_SET_FAIL_SCHEDULE named SCHED_TROIKA_STANDOFF"),
			F.Fighter->Schedule.FailScheduleOverride, ElysiumSched::SCHED_TROIKA_STANDOFF);
	}

	// --- The fail path: nowhere navigable to retreat to ------------------------------------------
	{
		FCombatFixture F(GPistol);
		if (F.Fighter == nullptr || F.Target == nullptr)
		{
			return false;
		}
		F.RunAdmissionAndLoadout();
		FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Fighter);
		if (Motor == nullptr)
		{
			return false;
		}
		Motor->bProjectsToNavigable = false;
		F.CommitToTarget(10.0);
		F.Fighter->SavePosition = F.Target->Origin;

		TestTrue(TEXT("the retreat starts"),
			ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_RUN_AWAY_FROM_ENEMY, *F.Fighter));
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.0, nullptr);
		TestFalse(TEXT("the retreat never became a move request"), Motor->bMoving);
		TestTrue(TEXT("...the failure stands as TASK_FAILED for the next pass"),
			F.Fighter->Cognition.Conditions.Has(ECond::TaskFailed));
		// The route runs at the top of the next pass (story 25).
		ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 10.1, &F.Fighter->Cognition.Conditions);
		TestNotEqual(TEXT("...so the program left the retreat through its fail path"),
			F.Fighter->Schedule.Current, ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_RUN_AWAY_FROM_ENEMY));
	}
	return true;
}


// A weaponless NPC fails its terminal attack task BY NAME rather than dealing damage
// out of nothing.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatUnarmedTaskFailureTest,
	"Elysium.Arm.NpcCombat.UnarmedTaskFailure", GElysiumTestFlags)
bool FElysiumNpcCombatUnarmedTaskFailureTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
	if (F.Fighter == nullptr || F.Target == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	TestNull(TEXT("the NPC really is unarmed"), F.ActiveWeapon(F.Fighter));
	F.CommitToTarget(0.0);

	TestTrue(TEXT("the swing program starts"),
		ElysiumSchedule::Start(F.Fighter->Schedule, ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING, *F.Fighter));
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 0.0, nullptr);

	TestEqual(TEXT("no damage is dealt out of nothing"), F.DamageTaken(F.Target), 0);
	TestTrue(TEXT("the failed swing stands as TASK_FAILED for the next pass"),
		F.Fighter->Cognition.Conditions.Has(ECond::TaskFailed));
	// The route runs at the top of the next pass (story 25).
	ElysiumSchedule::Tick(F.Fighter->Schedule, *F.Fighter, 0.1, &F.Fighter->Cognition.Conditions);
	TestNotEqual(TEXT("the failed swing left its own program"), F.Fighter->Schedule.Current,
		ElysiumScheduleGlobalId(ElysiumSched::SCHED_TROIKA_MELEE_ATTACK1_SWING));

	// `TASK_ANNOUNCE_ATTACK` (base `0x10286cd9`) is `TaskComplete` alone: no notice without a swing
	// (story 8 wave 2; the port's op verb that delivered one was a twin).
	TestFalse(TEXT("0x10286cd9 TASK_ANNOUNCE_ATTACK delivers no notice of its own"),
		ElysiumNpcCond::HasDetectedAttack(*F.Target, 0.0));
	return true;
}


// The Reaction-band producer every combat reaction goes through. What is pinned here is the
// blend rule, because it is the one thing the producer decides rather than forwards: a reaction is
// an ideal-activity write and takes the ordinary sequence-blend rules, so a request stating no blend
// takes the resolved clip's OWN authored fade (`docs/vtmb/combat-and-damage.md` § "Block and stagger
// reactions"). Only retail's flinch gesture hard-codes a pair, and it states one.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatReactionProducerTest,
	"Elysium.Arm.NpcCombat.ReactionProducer", GElysiumTestFlags)
bool FElysiumNpcCombatReactionProducerTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
	if (F.Fighter == nullptr)
	{
		return false;
	}
	F.Quiet();
	if (!TestNotNull(TEXT("the fighter carries a body to react with"), F.Fighter->Visual))
	{
		return false;
	}

	auto FirstCall = [&F](const TCHAR* Prefix) -> FString
	{
		for (const FString& Call : F.Services.Calls)
		{
			if (Call.StartsWith(Prefix))
			{
				return Call;
			}
		}
		return FString();
	};

	F.Services.bNpcActivitiesResolve = true;
	F.Services.bNpcOneShotsPlay = true;
	F.Services.ResolvedNpcActivityLabel = TEXT("block_heavy");
	F.Services.ResolvedNpcActivityClip = TEXT("block_heavy");
	F.Services.ResolvedNpcActivityOwner = TEXT("melee_bank");
	// The lying-down and damaged stance idles author 0.45; a reaction clip authoring anything but the
	// common 0.2 is what makes the difference between "read" and "defaulted" visible at all.
	F.Services.ResolvedNpcActivityFadeSeconds = 0.45f;

	// --- No stated blend: the clip's own authored fade -------------------------------------------
	F.Services.Calls.Reset();
	{
		FElysiumReactionPlayRequest Request;
		Request.Activity = TEXT("ACT_BLOCK_HEAVY");
		float Seconds = 0.0f;
		TestTrue(TEXT("the producer plays the resolved reaction"),
			F.Fighter->PlayReactionActivity(Request, &Seconds));
		TestTrue(TEXT("...and reports what the claim holds for"), Seconds > 0.0f);

		const FString Resolve = FirstCall(TEXT("ResolveNpcActivityClip"));
		TestTrue(TEXT("...having asked for the stated activity"),
			Resolve.Contains(TEXT("ACT_BLOCK_HEAVY")));
		// Non-directional: the blocked reaction is the activity the attacker's own sequence descriptor
		// stores, so nothing here derives an angle.
		TestTrue(TEXT("...with no hit yaw, because only the flinch is directional"),
			Resolve.Contains(TEXT("hit=0.0")));

		const FString Played = FirstCall(TEXT("PlayNpcOneShot"));
		TestTrue(TEXT("...and blends it in over the fade its own sequence authored"),
			Played.Contains(TEXT("in=0.45")) && Played.Contains(TEXT("out=0.45")));
		TestTrue(TEXT("...on the reaction route"), Played.Contains(TEXT("route=reaction")));
		TestTrue(TEXT("...in the reaction band"), Played.Contains(TEXT("prio=reaction")));
	}

	// --- A stated blend wins, which is what the flinch's hard-coded gesture pair relies on ---------
	F.Services.Calls.Reset();
	{
		FElysiumReactionPlayRequest Request;
		Request.Activity = TEXT("ACT_BLOCK_HEAVY");
		Request.BlendInSeconds = 0.1f;
		Request.BlendOutSeconds = 0.3f;
		TestTrue(TEXT("the producer plays it again"), F.Fighter->PlayReactionActivity(Request));

		const FString Played = FirstCall(TEXT("PlayNpcOneShot"));
		TestTrue(TEXT("...over the stated pair rather than the authored fade"),
			Played.Contains(TEXT("in=0.10")) && Played.Contains(TEXT("out=0.30")));
	}

	// --- A vocabulary with no such reaction is an ordinary negative, not a failure -----------------
	F.Services.Calls.Reset();
	F.Services.bNpcActivitiesResolve = false;
	{
		FElysiumReactionPlayRequest Request;
		Request.Activity = TEXT("ACT_BLOCK_HEAVY");
		float Seconds = 7.0f;
		TestFalse(TEXT("a body with no such reaction reacts with nothing"),
			F.Fighter->PlayReactionActivity(Request, &Seconds));
		TestEqual(TEXT("...and the duration is left untouched on the miss"), Seconds, 7.0f);
		TestFalse(TEXT("...with nothing handed to the pose layer"),
			F.Services.Saw(TEXT("PlayNpcOneShot")));
	}
	return true;
}


// The death transaction.
//
// 0x102bf340 -> 0x10265ad0 -> 0x1032b9b0 -> 0x1032c0e0: death creates the corpse
// synchronously. Rig capability and solver admission are distinct; no ordinary DIE program.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcCombatDeathTest,
	"Elysium.Arm.NpcCombat.Death", GElysiumTestFlags)
bool FElysiumNpcCombatDeathTest::RunTest(const FString&)
{
	FCombatFixture F(TEXT("0"), /*bWithFists=*/false, /*bInstallCatalogue=*/false);
	if (F.Fighter == nullptr)
	{
		return false;
	}
	F.RunAdmissionAndLoadout();
	if (!TestNotNull(TEXT("the NPC carries a body to die with"), F.Fighter->Visual))
	{
		return false;
	}
	FElysiumRecordingNpcMotor* Motor = F.MotorFor(F.Fighter);
	if (!TestNotNull(TEXT("...and a motor behind it"), Motor))
	{
		return false;
	}
	InstallCorpseSource(F.Fighter->Visual, false); // 0x10090180 typed source refusal, not missing PhysicsAsset
	TestFalse(TEXT("0x10090180 authored no-rig capability"), F.Fighter->HasClientRagdollRig());
	const uint32 NoRigSolidBefore = F.Fighter->RetailSolidFlags;
	const int32 NoRigMoveBefore = F.Fighter->RetailMoveType;
	const int32 NoRigFxBefore = F.Fighter->RenderFxWord;
	const FString NoRigThinkBefore = F.Fighter->ThinkFunctionName;
	TestFalse(TEXT("0x10090180 typed no-rig transaction refuses"),
		F.Fighter->BecomeClientRagdoll(FVector::ZeroVector, INDEX_NONE, false));
	TestEqual(TEXT("0x10090180 refusal keeps solid"), F.Fighter->RetailSolidFlags, NoRigSolidBefore);
	TestEqual(TEXT("0x10090180 refusal keeps move"), F.Fighter->RetailMoveType, NoRigMoveBefore);
	TestEqual(TEXT("0x10090180 refusal keeps FX"), F.Fighter->RenderFxWord, NoRigFxBefore);
	TestEqual(TEXT("0x10090180 refusal keeps think"), F.Fighter->ThinkFunctionName, NoRigThinkBefore);
	// The death sequence resolves and plays, so the transaction is asserted with a real pose on the
	// body rather than through a vocabulary that answers nothing.
	F.Services.bNpcActivitiesResolve = true;
	F.Services.bNpcOneShotsPlay = true;
	F.Services.ResolvedNpcActivityLabel = TEXT("diesimple");
	F.Services.ResolvedNpcActivityClip = TEXT("diesimple");
	F.Services.ResolvedNpcActivityOwner = TEXT("misc");
	F.Services.OneShotSeconds = 2.0f;
	// The player can see where the body falls, so `SUB_PVSRemove` (`0x102696f0`) keeps the corpse.
	F.Services.bNpcMakerInViewCone = true;
	F.Services.bNpcMakerVisible = true;
	F.Services.Calls.Reset();

	// --- The transaction itself -------------------------------------------------------------------
	// Corrected to retail (story 8 wave 2): the kill is slot 144 — Troika `0x102bf340` ->
	// `CAI_BaseNPC::Event_Killed` `0x10265ad0` -> `CBaseCombatCharacter::Event_Killed` `0x1032b9b0`,
	// which ends in slot 301 `CreateCorpse` (`0x1032c0e0`) -> `BecomeClientRagdoll`: the corpse is
	// made INSIDE the transaction, from the current pose, and its think stops. No `DIE` program runs
	// for it (the port's retired transaction started `DIE` and handed over at its end — the named
	// divergence `CompleteDeathHandoff` used to carry).
	FRandomStream& Reaction = ElysiumRng::Stream(EElysiumRngStream::Reaction);
	const int32 ReactionSeedBefore = Reaction.GetCurrentSeed();
	F.Fighter->OnKilled();

	TestEqual(TEXT("0x1032b9b0 m_lifeState = LIFE_DYING"), F.Fighter->LifeState, 1);
	TestFalse(TEXT("...so slot 158 IsAlive answers false"), F.Fighter->IsAlive());
	TestTrue(TEXT("the mind is dead, current (0x10265dba SetState(7)) and ideal (0x10265d06) both"),
		F.Fighter->GetMind().State() == EElysiumNpcState::Dead
			&& F.Fighter->GetMind().IdealState() == EElysiumNpcState::Dead);
	TestFalse(TEXT("0x10090180 no-rig arm adds no frozen-body stand-in"), Motor->bFrozen);
	TestFalse(TEXT("0x10090180 no-rig arm adds no collision switch"), Motor->bIgnoreCharacterCollision);
	// Frozen is NOT hidden: a corpse stays on screen. `SetEnabled(false)` is what would take it off,
	// and death never calls it.
	TestTrue(TEXT("...while staying enabled, because a corpse is visible"), Motor->bEnabled);
	TestFalse(TEXT("nothing disabled the body"),
		F.Services.Saw(TEXT("NpcMotor SetEnabled 0")));
	TestFalse(TEXT("the entity is not killed or hidden by dying"), F.Fighter->IsInert());
	TestFalse(TEXT("0x10090180 no-rig arm never requests physics"), F.Services.Saw(TEXT("StartBodyRagdoll")));
	TestFalse(TEXT("0x10090180 no-rig arm never holds final pose"), F.Services.Saw(TEXT("HoldBodyFinalPose")));
	TestEqual(TEXT("0x10090180 no-rig arm zeroes bounds"), F.Fighter->LastSetSizeMaxsUnits, FVector::ZeroVector);
	TestFalse(TEXT("no death program runs: the corpse's think is gone"), F.Fighter->Schedule.IsRunning());
	// Corrected (L13 wave-2 fixes): `BecomeClientRagdoll` (`0x10090180`) clears the think, and
	// `CreateCorpse`'s tail re-arms it -- `ThinkSet(SUB_PVSRemove)` at `curtime + 10.0`
	// (`0x1032c404..0x1032c423`) for a corpse that does not burn.
	TestEqual(TEXT("0x1032c40f: CreateCorpse's tail installs SUB_PVSRemove"),
		F.Fighter->ThinkFunctionName, FString(TEXT("0x102696f0")));
	TestEqual(TEXT("0x1032c41a..0x1032c423: ...at curtime + 10.0"), F.Fighter->NextThink,
		static_cast<float>(F.World.NowSeconds() + 10.0));
	TestEqual(TEXT("the death draws from the Reaction stream not at all"),
		Reaction.GetCurrentSeed(), ReactionSeedBefore);
	TestFalse(TEXT("...and plays no death clip"), F.Services.Saw(TEXT("PlayNpcClip")));

	// --- No reselection, however hard the world ticks ---------------------------------------------
	F.Services.Calls.Reset();
	for (int32 i = 0; i < 4; ++i)
	{
		// Forcing a think is the strong form of the claim: even asked directly, a corpse selects
		// nothing and hands nothing back to physics twice.
		F.Fighter->NextThink = 0.0f;
		F.World.Tick(10.0 + i);
	}
	TestFalse(TEXT("no schedule is selected after death"),
		F.Fighter->Schedule.IsRunning());
	TestFalse(TEXT("...no stance machine runs"), F.Services.Saw(TEXT("ResolveStanceClips")));
	TestFalse(TEXT("...no activity is resolved"), F.Services.Saw(TEXT("ResolveNpcActivityClip")));
	TestFalse(TEXT("...and the handoff is not repeated"), F.Services.Saw(TEXT("StartBodyRagdoll")));
	TestEqual(TEXT("0x102696f0: a corpse the player sees re-arms at curtime + 10.0"), F.Fighter->NextThink,
		static_cast<float>(13.0 + 10.0));
	TestFalse(TEXT("...and stays in the world"), F.Fighter->IsInert());

	// Selection has a SECOND door, and the corpse has to refuse there too: a script's
	// `ChangeSchedule` and a discipline's `AI_Schedule` channel both arrive through this one, after
	// the death commit and outside any think ordering.
	TestFalse(TEXT("a named schedule pushed at a corpse is refused"),
		F.Fighter->StartNamedSchedule(TEXT("SCHED_CHASE_ENEMY"),
			TEXT("CAI_BaseNPC.ChangeSchedule"), FString()));
	TestFalse(TEXT("...and nothing is running afterwards"), F.Fighter->Schedule.IsRunning());
	TestTrue(TEXT("...with the refusal named, not silent"),
		F.Debug(F.Fighter, TEXT("Mind transition")).Contains(TEXT("this NPC is dead")));

	// --- A second kill is ignored -----------------------------------------------------------------
	F.Services.Calls.Reset();
	F.Fighter->OnKilled();
	TestFalse(TEXT("a duplicate kill runs no second transaction"),
		F.Services.Saw(TEXT("ReleaseBodyAnimClaims")));

	// --- A corpse the feed pair hands back is taken back ------------------------------------------
	// The live producer is `EndFeedVictimRole`: the feed transaction kills a depleted victim and then
	// releases the same body in the same call, which un-freezes it and re-arms its think. Anything
	// that gives a corpse's body back has to lose, so the re-armed think is asserted to take it.
	F.Services.Calls.Reset();
	F.Fighter->SetBodyFrozen(false);
	F.Fighter->NextThink = 0.0f;
	F.World.Tick(20.0);
	TestFalse(TEXT("0x102696f0 removal poll adds no frozen-body write"), Motor->bFrozen);
	TestFalse(TEXT("0x102696f0 removal poll adds no collision switch"), Motor->bIgnoreCharacterCollision);
	TestEqual(TEXT("0x102696f0: ...and SUB_PVSRemove re-arms at curtime + 10.0"), F.Fighter->NextThink,
		static_cast<float>(20.0 + 10.0));
	TestFalse(TEXT("...without handing the body to physics a second time"),
		F.Services.Saw(TEXT("StartBodyRagdoll")));

	// --- Nobody sees it: `UTIL_Remove` ------------------------------------------------------------
	F.Services.bNpcMakerVisible = false;
	F.Fighter->NextThink = 0.0f;
	F.World.Tick(40.0);
	TestTrue(TEXT("0x102696f0: a corpse no player sees is removed (0x101cd940 UTIL_Remove)"),
		F.Fighter->IsDead());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV4dRiggedDeathTest,
	"Elysium.Arm.NpcCombat.Death.RiggedLifetime", GElysiumTestFlags)
bool FElysiumV4dRiggedDeathTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("v4d_rigged_death"), 8413);
	for (const TCHAR* NpcName : {TEXT("rig"), TEXT("failed"), TEXT("missing")})
	{
		FElysiumEntityDef& RigDef = Builder.AddTroikaNpc(NpcName);
		RigDef.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
		RigDef.InternalFactory = []() -> TUniquePtr<FElysiumEntity>
		{ return MakeUnique<FV4dDeathNpc>(); };
	}
	Builder.AddNpcOfClass(TEXT("ped"), FVector(300, 0, 0), TEXT("CNPC_VPedestrian"))
		.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FV4dDeathNpc* const RigNpc = static_cast<FV4dDeathNpc*>(Fixture.Npc(TEXT("rig")));
	FV4dDeathNpc* const FailedNpc = static_cast<FV4dDeathNpc*>(Fixture.Npc(TEXT("failed")));
	FV4dDeathNpc* const MissingNpc = static_cast<FV4dDeathNpc*>(Fixture.Npc(TEXT("missing")));
	FElysiumNpc* const PedNpc = Fixture.Npc(TEXT("ped"));
	if (!RigNpc || !FailedNpc || !MissingNpc || !PedNpc) { return false; }
	FElysiumNpcWorldFixture::Quiet({RigNpc, FailedNpc, MissingNpc, PedNpc});
	Fixture.World.Tick(1.0); // 0x1028d8d0 network-init before the measured death transaction
	for (FElysiumNpc* CorpseNpc : {static_cast<FElysiumNpc*>(RigNpc), static_cast<FElysiumNpc*>(FailedNpc), PedNpc})
	{
		if (!TestNotNull(TEXT("fixture carries a visual"), CorpseNpc->Visual)) { return false; }
		InstallCorpseSource(CorpseNpc->Visual, true);
		CorpseNpc->bHasKindredTemplate = true;
		CorpseNpc->bKindredTemplate = false;
		CorpseNpc->SpawnFlags = 4;
	}
	if (!TestNotNull(TEXT("missing-bone fixture carries a visual"), MissingNpc->Visual)) { return false; }
	InstallCorpseSource(MissingNpc->Visual, true, false);
	MissingNpc->bHasKindredTemplate = true; MissingNpc->bKindredTemplate = false; MissingNpc->SpawnFlags = 4;
	for (int32 SeedRow = 0; SeedRow < 2; ++SeedRow)
	{
		FElysiumNpcClip SeedClip;
		SeedClip.RawIndex = 701 + SeedRow; SeedClip.Owner = TEXT("v4d_seed_bank");
		SeedClip.Activity = TEXT("ACT_DIERAGDOLL"); SeedClip.Weight = 2;
		FElysiumRecordingServices::FRawIndexClip RawSeed;
		RawSeed.Label = FString::Printf(TEXT("v4d_seed%d"), SeedRow); RawSeed.Clip = SeedClip;
		Fixture.Services.BodyClipsByRawIndex.Add(SeedClip.RawIndex, RawSeed);
		RigNpc->DeathPickRows.Add(SeedClip);
	}
	RigNpc->SequenceNumber = 104; RigNpc->SequenceCycle = 0.37f;
	RigNpc->RetailSolidFlags = 0; RigNpc->SetMoveType(4, 0);
	const double DeathNow = Fixture.World.NowSeconds();
	USkeletalMeshComponent* const RigVisual = RigNpc->Visual;
	Fixture.Services.Calls.Reset(); Fixture.Services.bBodiesRagdoll = true;
	FRandomStream& PickStream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	PickStream.Initialize(129); FRandomStream ExpectedPick(129); (void)ExpectedPick.RandRange(0, 3);
	RigNpc->OnKilled(); // 0x1032c29c real fallback bone, same-tick simulation admission
	TestTrue(TEXT("0x10090180 source capability despite no real solver asset in recording fixture"), RigNpc->HasClientRagdollRig());
	TestEqual(TEXT("0x1032c226 native Spine2 lookup, never cop ordinal5"), RigNpc->ObservedForceBone, 2);
	TestEqual(TEXT("0x1009021a one weighted gather"), RigNpc->SeedGathers, 1);
	TestEqual(TEXT("0x10427fc0 ordinary real-bone call still spends its shared pick"), PickStream.GetCurrentSeed(), ExpectedPick.GetCurrentSeed());
	TestEqual(TEXT("0x1009021a real bone retains sequence"), RigNpc->SequenceNumber, 104);
	TestEqual(TEXT("0x1009021a real bone retains cycle"), RigNpc->SequenceCycle, 0.37f);
	TestEqual(TEXT("0x10090180 solid flag4"), RigNpc->RetailSolidFlags, 4u);
	TestEqual(TEXT("0x10090180 renderFX17"), RigNpc->RenderFxWord, 0x17);
	TestEqual(TEXT("0x10090180 move none"), RigNpc->RetailMoveType, 0);
	TestEqual(TEXT("0x10090180 zero bounds"), RigNpc->LastSetSizeMaxsUnits, FVector::ZeroVector);
	TestEqual(TEXT("0x1032c0e0 lifeState1"), RigNpc->LifeState, 1);
	TestEqual(TEXT("0x1032c40f tail PVSRemove"), RigNpc->ThinkFunctionName, FString(TEXT("0x102696f0")));
	TestEqual(TEXT("0x1032c41a tail +10"), RigNpc->NextThink, static_cast<float>(DeathNow + 10.0));
	TestEqual(TEXT("same-tick admission once"), Fixture.Services.Count(TEXT("StartBodyRagdoll")), 1);
	TestEqual(TEXT("lethal death retains the simulating visual"), Fixture.Services.Count(TEXT("ReleaseNpcVisual")), 0);
	TestTrue(TEXT("recording solver retains body"), Fixture.Services.SimulatingNpcVisuals.Contains(RigVisual));
	RigNpc->OnKilled(); RigNpc->CompleteHandoffForTest(); RigNpc->RestoreVisualForTest(); RigNpc->RestoreVisualForTest();
	TestEqual(TEXT("duplicate kill/handoff/restoration never admits twice"), Fixture.Services.Count(TEXT("StartBodyRagdoll")), 1);
	TestEqual(TEXT("restoration never repeats weighted picks"), RigNpc->SeedGathers, 1);
	TestEqual(TEXT("restoration preserves corpse clock"), RigNpc->NextThink, static_cast<float>(DeathNow + 10.0));
	RigNpc->ScriptHide();
	TestEqual(TEXT("ordinary hide retains corpse physics"), Fixture.Services.Count(TEXT("ReleaseNpcVisual")), 0);
	RigNpc->Kill(); RigNpc->Kill();
	TestEqual(TEXT("terminal hidden removal releases once"), Fixture.Services.Count(TEXT("ReleaseNpcVisual sim=1")), 1);
	TestNull(TEXT("terminal removal clears entity Visual"), RigNpc->Visual);
	TestFalse(TEXT("terminal removal clears recording simulation"), Fixture.Services.SimulatingNpcVisuals.Contains(RigVisual));

	Fixture.Services.Calls.Reset(); Fixture.Services.bBodiesRagdoll = false;
	FailedNpc->OnKilled();
	TestTrue(TEXT("missing/failed PhysicsAsset never reclassifies source rig"), FailedNpc->HasClientRagdollRig());
	TestEqual(TEXT("failed admission is attempted once"), Fixture.Services.Count(TEXT("StartBodyRagdoll -> 0")), 1);
	TestTrue(TEXT("failed admission is named"), Fixture.Services.Saw(TEXT("ragdoll bake/handoff failed")));
	TestFalse(TEXT("failed admission never substitutes hold pose"), Fixture.Services.Saw(TEXT("HoldBodyFinalPose")));
	TestEqual(TEXT("failed asset still follows retail rig writes"), FailedNpc->RenderFxWord, 0x17);
	TestEqual(TEXT("failed asset still installs corpse tail"), FailedNpc->ThinkFunctionName, FString(TEXT("0x102696f0")));
	FailedNpc->CompleteHandoffForTest();
	TestEqual(TEXT("failed handoff is not retried by late completion"), Fixture.Services.Count(TEXT("StartBodyRagdoll")), 1);

	Fixture.Services.Calls.Reset(); Fixture.Services.bBodiesRagdoll = true;
	MissingNpc->RetailSolidFlags = 0; MissingNpc->OnKilled();
	TestTrue(TEXT("missing fallback stays source-rigged"), MissingNpc->HasClientRagdollRig());
	TestEqual(TEXT("missing fallback is not fabricated bone-1"), MissingNpc->SeedGathers, 0);
	TestFalse(TEXT("missing fallback does not hand off"), Fixture.Services.Saw(TEXT("StartBodyRagdoll")));
	TestEqual(TEXT("missing fallback does not invent solid writes"), MissingNpc->RetailSolidFlags, 0u);
	TestEqual(TEXT("missing fallback still receives corpse tail"), MissingNpc->ThinkFunctionName, FString(TEXT("0x102696f0")));

	Fixture.Services.Calls.Reset(); PedNpc->OnKilled();
	USkeletalMeshComponent* const PedVisual = PedNpc->Visual;
	TestTrue(TEXT("0x103a38c0 pedestrian body retained"), Fixture.Services.SimulatingNpcVisuals.Contains(PedVisual));
	TestTrue(TEXT("0x103a38c0 pedestrian clears think"), PedNpc->ThinkFunctionName.IsEmpty());
	TestEqual(TEXT("0x103a38c0 pedestrian never-think clock"), PedNpc->NextThink, ELYSIUM_NEVER_THINK);
	TestEqual(TEXT("0x103a38c0 pedestrian SOLID_NONE"), PedNpc->RetailSolidType, 0);
	TestEqual(TEXT("pedestrian lethal death never releases visual"), Fixture.Services.Count(TEXT("ReleaseNpcVisual")), 0);
	PedNpc->Kill();
	TestEqual(TEXT("visible terminal removal also releases simulation"), Fixture.Services.Count(TEXT("ReleaseNpcVisual sim=1")), 1);
	Fixture.Services.Calls.Reset();
	USkeletalMeshComponent* DestructedVisual = nullptr;
	{
		TUniquePtr<FElysiumNpc> DestructedNpc = MakeUnique<FElysiumNpc>();
		DestructedNpc->World = &Fixture.World;
		DestructedNpc->Visual = Fixture.Services.BuildNpcVisual(RigNpc->ModelStem(), FVector::ZeroVector,
			FRotator::ZeroRotator, 1.f, TEXT("Neutral"), 0);
		InstallCorpseSource(DestructedNpc->Visual, true);
		DestructedVisual = DestructedNpc->Visual;
		(void)DestructedNpc->BecomeClientRagdoll(FVector::ZeroVector, 2, false);
	}
	TestEqual(TEXT("destructor releases retained simulation once"), Fixture.Services.Count(TEXT("ReleaseNpcVisual sim=1")), 1);
	TestFalse(TEXT("destructor clears recording physics"), Fixture.Services.SimulatingNpcVisuals.Contains(DestructedVisual));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV4dBurnSoundTest,
	"Elysium.Arm.NpcCombat.Death.BurnSound", GElysiumTestFlags)
bool FElysiumV4dBurnSoundTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("v4d_burn_sound"), 8414);
	Builder.AddTroikaNpc(TEXT("kindred"))
		.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* const BurningNpc = Fixture.Npc(TEXT("kindred"));
	if (!BurningNpc) { return false; }
	FElysiumNpcWorldFixture::Quiet({BurningNpc}); Fixture.World.Tick(1.0);
	BurningNpc->bHasKindredTemplate = true; BurningNpc->bKindredTemplate = true;
	BurningNpc->SpawnFlags = 4; // 0x1032c32f burning corpse has an unconditional +10 clock
	if (!TestNotNull(TEXT("burn fixture carries a visual"), BurningNpc->Visual)) { return false; }
	InstallCorpseSource(BurningNpc->Visual, true);
	Fixture.Services.bBodiesRagdoll = true;
	USkeletalMeshComponent* const BurnVisual = BurningNpc->Visual;
	const FElysiumEntityHandle BurnHandle = BurningNpc->Handle;
	const double BurnNow = Fixture.World.NowSeconds();
	Fixture.Services.Calls.Reset();
	Fixture.Services.BodySounds.Reset(); Fixture.Services.BodySoundOwners.Reset();
	BurningNpc->OnKilled(); BurningNpc->OnKilled();
	const int32 BurnCount = Fixture.Services.BodySounds.FilterByPredicate([](const FElysiumBodySound& Sound)
	{ return Sound.Rel == TEXT("character/vampire burning death.wav"); }).Num();
	TestEqual(TEXT("0x1032c3c1 burn wav exactly once, duplicate kill inert"), BurnCount, 1);
	for (int32 SoundIndex = 0; SoundIndex < Fixture.Services.BodySounds.Num(); ++SoundIndex)
	{
		const FElysiumBodySound& BurnSound = Fixture.Services.BodySounds[SoundIndex];
		if (BurnSound.Rel != TEXT("character/vampire burning death.wav")) { continue; }
		TestEqual(TEXT("0x1032c3c1 volume1"), BurnSound.Volume, 1.f);
		TestEqual(TEXT("0x1032c3c1 attenuation0.8 -> soundlevel75"), BurnSound.SoundLevelDb, 75);
		TestEqual(TEXT("0x1032c3c1 pitch100 -> native1"), BurnSound.Pitch, 1.f);
		TestTrue(TEXT("0x1032c3c1 channel0"), BurnSound.Channel == EElysiumSoundChannel::Auto);
		TestTrue(TEXT("0x1032c3c1 sound belongs to dying entity"), Fixture.Services.BodySoundOwners[SoundIndex] == BurningNpc->Handle);
	}
	TestEqual(TEXT("0x1032c32f burn installs SUB_Remove"), BurningNpc->ThinkFunctionName, FString(TEXT("0x101c0b10")));
	TestEqual(TEXT("0x1032c347 burn deadline +10"), BurningNpc->NextThink, static_cast<float>(BurnNow + 10.0));
	TestEqual(TEXT("burn death retains visual until removal"), Fixture.Services.Count(TEXT("ReleaseNpcVisual")), 0);
	Fixture.World.Tick(BurnNow + 9.0);
	TestTrue(TEXT("burn body retained before +10"), Fixture.Services.SimulatingNpcVisuals.Contains(BurnVisual));
	Fixture.World.Tick(BurnNow + 10.0);
	TestEqual(TEXT("burn removal releases simulation exactly once"), Fixture.Services.Count(TEXT("ReleaseNpcVisual sim=1")), 1);
	TestFalse(TEXT("burn removal releases recording physics"), Fixture.Services.SimulatingNpcVisuals.Contains(BurnVisual));
	const FElysiumEntity* const BurnRemoved = Fixture.World.Resolve(BurnHandle);
	TestTrue(TEXT("burn removal is terminal"), BurnRemoved == nullptr || BurnRemoved->IsInert());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV4cFadeClockTest,
	"Elysium.Arm.NpcCombat.FadeClock", GElysiumTestFlags)
bool FElysiumV4cFadeClockTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("v4c_fade"), 8311);
	Builder.AddTroikaNpc(TEXT("child"));
	Builder.AddNpcOfClass(TEXT("ped"), FVector(200, 0, 0), TEXT("CNPC_VPedestrian"));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Child = Fixture.Npc(TEXT("child"));
	FElysiumNpc* Ped = Fixture.Npc(TEXT("ped"));
	if (Child == nullptr || Ped == nullptr) { return false; }
	FElysiumNpcWorldFixture::Quiet({Child, Ped});
	Fixture.World.Tick(1.0); // 0x1028d8d0 network-init broadcast precedes the measured death clock
	Child->bHasKindredTemplate = true; Child->bKindredTemplate = true;
	Child->SpawnFlags = 0x204; Child->RenderMode = 0; Child->RenderAlphaByte = 255;
	Child->AngularVelocity = FVector(1, 2, 3);
	const float DeathNow = static_cast<float>(Fixture.World.NowSeconds());
	Child->OnKilled(); // 0x1032c0e0 burn remove -> 0x10265d72 later fade install
	TestEqual(TEXT("0x102695d0 mode0 becomes2"), Child->RenderMode, 2);
	TestEqual(TEXT("0x102695d0 alpha starts255"), Child->RenderAlphaByte, uint8(255));
	TestTrue(TEXT("0x102695d0 solid4"), (Child->RetailSolidFlags & 4u) != 0);
	TestEqual(TEXT("0x102695d0 angular zero"), Child->AngularVelocity, FVector::ZeroVector);
	TestEqual(TEXT("0x102695d0 relink once"), Child->FadeRelinkCalls, 1);
	TestEqual(TEXT("0x1026968d first think +10"), Child->NextThink, DeathNow + 10.f);
	TestEqual(TEXT("0x10265d72 fade overrides Kindred SUB_Remove"), Child->ThinkFunctionName, FString(TEXT("0x100152b2")));
	const FElysiumEntityHandle ChildHandle = Child->Handle;
	Fixture.World.Tick(DeathNow + 9.0);
	TestEqual(TEXT("0x102695d0 no early decrement"), Child->RenderAlphaByte, uint8(255));
	for (int32 Decrement = 0; Decrement < 36; ++Decrement)
	{
		const double Due = Child->NextThink;
		Fixture.World.Tick(Due);
		TestEqual(TEXT("0x10269960 subtract7"), int32(Child->RenderAlphaByte), 255 - 7 * (Decrement + 1));
		TestEqual(TEXT("0x10269960 rearm +0.1"), Child->NextThink, static_cast<float>(Due + 0.1));
	}
	Fixture.World.Tick(Child->NextThink);
	TestEqual(TEXT("0x10269960 remainder3 becomes0"), Child->RenderAlphaByte, uint8(0));
	TestEqual(TEXT("0x10269960 install remove +0.2"), Child->ThinkFunctionName, FString(TEXT("0x101c0b10")));
	TestTrue(TEXT("0x10269960 alive entity through zero-alpha think"), Fixture.World.Resolve(ChildHandle) != nullptr);
	const float RemoveDue = Child->NextThink;
	Fixture.World.Tick(RemoveDue);
	const FElysiumEntity* Removed = Fixture.World.Resolve(ChildHandle);
	TestTrue(TEXT("0x101c0b10 removal only on following think"), Removed == nullptr || Removed->IsInert());
	TestTrue(TEXT("0x10269960 approximate death+13.8 deadline"), FMath::Abs(RemoveDue - (DeathNow + 13.8f)) < 0.01f);

	Ped->RenderMode = 4; Ped->RenderAlphaByte = 6; Ped->SpawnFlags = 0x204;
	Ped->OnKilled(); // 0x103a391c clear, later 0x10265d72 fade wins
	TestEqual(TEXT("0x102695d0 nonzero render mode retained"), Ped->RenderMode, 4);
	TestEqual(TEXT("0x102695d0 nonzero mode keeps alpha"), Ped->RenderAlphaByte, uint8(6));
	TestEqual(TEXT("0x10265d72 fade overrides pedestrian clear"), Ped->ThinkFunctionName, FString(TEXT("0x100152b2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV4cCorpseThinkInstallTest,
	"Elysium.Arm.NpcCombat.CorpseThinkInstall", GElysiumTestFlags)
bool FElysiumV4cCorpseThinkInstallTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("v4c_corpse_thinks"), 8314);
	Builder.AddTroikaNpc(TEXT("mortal"));
	Builder.AddTroikaNpc(TEXT("kindred"), FVector(100, 0, 0));
	Builder.AddTroikaNpc(TEXT("static"), FVector(200, 0, 0));
	FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
	FElysiumNpc* Mortal = Fixture.Npc(TEXT("mortal"));
	FElysiumNpc* Kindred = Fixture.Npc(TEXT("kindred"));
	FElysiumNpc* StaticNpc = Fixture.Npc(TEXT("static"));
	if (Mortal == nullptr || Kindred == nullptr || StaticNpc == nullptr) { return false; }
	FElysiumNpcWorldFixture::Quiet({Mortal, Kindred, StaticNpc});
	Mortal->bHasKindredTemplate = true; Mortal->bKindredTemplate = false;
	Kindred->bHasKindredTemplate = true; Kindred->bKindredTemplate = true;
	StaticNpc->MiscFlags |= 0x80000u;
	const float CorpseNow = static_cast<float>(Fixture.World.NowSeconds());
	Mortal->OnKilled(); Kindred->OnKilled(); StaticNpc->OnKilled();
	TestEqual(TEXT("0x1032c40f mortal installs PVSRemove"), Mortal->ThinkFunctionName, FString(TEXT("0x102696f0")));
	TestEqual(TEXT("0x1032c41a mortal +10"), Mortal->NextThink, CorpseNow + 10.f);
	TestEqual(TEXT("0x1032c33c Kindred installs Remove"), Kindred->ThinkFunctionName, FString(TEXT("0x101c0b10")));
	TestEqual(TEXT("0x1032c347 Kindred +10"), Kindred->NextThink, CorpseNow + 10.f);
	TestEqual(TEXT("0x1032c2cb static source installs Remove"), StaticNpc->ThinkFunctionName, FString(TEXT("0x101c0b10")));
	TestEqual(TEXT("0x1032c2cb static source +0.5"), StaticNpc->NextThink, CorpseNow + 0.5f);

	// 0x103a391c explicit NULL is different from a missing restored function identity.
	Mortal->ThinkSet(nullptr, 0.0);
	Mortal->Think();
	TestTrue(TEXT("0x103a391c explicit NULL stays NULL"), Mortal->ThinkFunctionName.IsEmpty());
	TestEqual(TEXT("0x103a391c explicit NULL stays unscheduled"), Mortal->NextThink, ELYSIUM_NEVER_THINK);
	Kindred->ThinkFunctionName.Reset(); Kindred->ThinkSetCalls = 0; Kindred->NextThink = 42.f;
	Kindred->Think();
	TestTrue(TEXT("0x1032c0e0 missing restored function never becomes mortal PVSRemove"),
		Kindred->ThinkFunctionName.IsEmpty());
	TestEqual(TEXT("0x1032c0e0 missing restored clock kept for V6, not inferred"), Kindred->NextThink, 42.f);
	TestFalse(TEXT("0x1032c0e0 missing function never re-enters NPCThink"), Kindred->Schedule.IsRunning());
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV4cNamedTeamDamageTest,
 "Elysium.Arm.NpcCombat.NamedTeamDamageGate", GElysiumTestFlags)
bool FElysiumV4cNamedTeamDamageTest::RunTest(const FString&)
{
 struct FObservedVictim : FElysiumNpc
 {
  int32 AliveDispatches = 0, DyingDispatches = 0, DeadDispatches = 0;
  virtual int32 OnTakeDamage_Alive(void* Packet) override { ++AliveDispatches; return FElysiumNpc::OnTakeDamage_Alive(Packet); }
  virtual int32 OnTakeDamage_Dying(void* Packet) override { ++DyingDispatches; return FElysiumNpc::OnTakeDamage_Dying(Packet); }
  virtual int32 OnTakeDamage_Dead(void* Packet) override { ++DeadDispatches; return FElysiumNpc::OnTakeDamage_Dead(Packet); }
 };
 FElysiumNpcWorldBuilder Builder(TEXT("v4c_team_damage"), 8315);
 Builder.AddTroikaNpc(TEXT("victim")).InternalFactory = []() -> TUniquePtr<FElysiumEntity> { return MakeUnique<FObservedVictim>(); };
 Builder.AddTroikaNpc(TEXT("attacker"), FVector(100, 0, 0));
 Builder.AddEntity(TEXT("prop_base"), TEXT("noncharacter"));
 FElysiumNpcWorldFixture Fixture(MoveTemp(Builder));
 auto* Victim = static_cast<FObservedVictim*>(Fixture.Npc(TEXT("victim")));
 FElysiumNpc* Attacker = Fixture.Npc(TEXT("attacker"));
 if (!Victim || !Attacker) return false;
 FElysiumNpcWorldFixture::Quiet({Victim, Attacker});
 Victim->TakeDamageMode = 2;
 Victim->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100000);
 Victim->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
 Victim->SyncHealthFromSheet();
 Victim->AddToTeam(TEXT("!damage_team")); Attacker->AddToTeam(TEXT("DAMAGE_TEAM"));
 FElysiumNpcBase::FElysiumTakeDamageInfo Info;
 Info.Attacker = Attacker->Handle; Info.Damage = 18.f;
 FElysiumActiveDisciplineEffect Removable;
 Removable.bRemoveOnTakeDamage = true;
 Victim->Disciplines.TargetEffects.Add(Removable);
 TestEqual(TEXT("0x1032ef60 named teammate packet refused"), Victim->OnTakeDamage(&Info), 0);
 TestEqual(TEXT("0x1032ef60 refusal before alive dispatch"), Victim->AliveDispatches, 0);
 TestEqual(TEXT("0x1032ef60 refusal before 0x101e3cf0 notification"), Victim->Disciplines.TargetEffects.Num(), 1);
 TestEqual(TEXT("0x1032ef60 refusal commits no wounds"), Victim->Sheet.GetCurrent(EElysiumTraitContainer::Attributes, ElysiumSlot::Health), 0);
 for (int32 RefusedLife : {1, 2})
 {
  Victim->LifeState = RefusedLife;
  TestEqual(TEXT("0x1032ef60 teammate refusal before dying/dead dispatch"), Victim->OnTakeDamage(&Info), 0);
 }
 TestEqual(TEXT("0x1032ef60 no dying dispatch"), Victim->DyingDispatches, 0);
 TestEqual(TEXT("0x1032ef60 no dead dispatch"), Victim->DeadDispatches, 0);
 Victim->LifeState = 0; Attacker->AddToTeam(TEXT("different_damage_team"));
 TestTrue(TEXT("0x1032ef60 different-team control damages"), Victim->OnTakeDamage(&Info) > 0);
 TestEqual(TEXT("0x101e3cf0 admitted packet removes damage-sensitive effect"), Victim->Disciplines.TargetEffects.Num(), 0);
 Info.Attacker = Victim->Handle;
 TestTrue(TEXT("0x1032ef60 self packet admitted despite same symbol"), Victim->OnTakeDamage(&Info) > 0);
 Info.Attacker = FElysiumEntityHandle::Invalid();
 TestTrue(TEXT("0x1032ef60 null attacker admitted by this gate"), Victim->OnTakeDamage(&Info) > 0);
 Info.Attacker = Fixture.World.FindByName(TEXT("noncharacter"))->Handle;
 TestTrue(TEXT("0x1032ef60 noncharacter attacker admitted by this gate"), Victim->OnTakeDamage(&Info) > 0);
 Victim->AddToTeam(TEXT("")); Attacker->AddToTeam(TEXT("")); Info.Attacker = Attacker->Handle;
 TestTrue(TEXT("0x10323930 two invalid symbols never reject damage"), Victim->OnTakeDamage(&Info) > 0);
 return true;
}

}   // namespace ElysiumNpcCombatTests

#endif   // WITH_DEV_AUTOMATION_TESTS
