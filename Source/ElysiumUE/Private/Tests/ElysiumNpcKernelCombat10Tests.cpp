#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombatSchedules.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Tests/ElysiumNpcTestFixture.h"

// Story 29d, family **Combat10** — the ranged-combat selectors, the fighting-item loadout, the
// health-percent readers, the two weapon drops, the scripted discipline, the ideal-state
// pre-select, the prayer pulse, the knockback velocity and the yaw clearance sweep.
//
// One case per `rule` row, every assertion read off the decompiled C or the listing with the address
// it came from named beside it. The corrections this family's reading made to the checklist's
// one-line walks are each pinned by a case: `0x102b7f40` has THREE arms and not one, slot 385's
// split gate is the item STACK (`+0x8d0`) and not a magazine, slot 460's `SEE_FEAR` arm writes the
// ideal state unconditionally, and `0x102b7cf0`'s convar-gated half sits INSIDE the enemy gate.

static constexpr EAutomationTestFlags GElysiumNpcKernelCombat10Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Prefixed because the module builds adaptive-unity and this anonymous namespace is merged with
	// the other suites'. The two fighting-item classnames are retail's OWN and are deliberately not
	// suite-local: the bodies under test name them by string.
	const TCHAR* const GCombat10Fists = TEXT("item_w_fists");
	const TCHAR* const GCombat10WerewolfAttacks = TEXT("item_w_werewolf_attacks");
	const TCHAR* const GCombat10Katana = TEXT("item_w_katana");
	const TCHAR* const GCombat10BachRifle = TEXT("item_w_rem_m_700_bach");
	// A stackable throwable, for slot 385's stack-split arm.
	const TCHAR* const GCombat10Stackable = TEXT("item_w_combat10_stack");

	// Retail stat `0x0e`, which `ElysiumSlot` spells `faithpoints` at index 14 and names no constant
	// for. Slot 357's prayer pulse is its one reader in this family.
	constexpr int32 GCombat10StatFaithPoints = 0x0e;

	FElysiumWeaponMode Combat10Mode(const TCHAR* Dmg, float Range, int32 AmmoCost)
	{
		FElysiumWeaponMode Mode;
		Mode.Tag = TEXT("Primary");
		Mode.TypeName = TEXT("Attack");
		Mode.Type = EElysiumWeaponModeType::Attack;
		Mode.Dmg = Dmg;
		Mode.BaseLethality = 8;
		Mode.SkillRequirement = 1;
		Mode.AttackRate = 0.5f;
		Mode.Range = Range;
		Mode.AmmoCost = AmmoCost;
		Mode.AmmoFired = 1;
		return Mode;
	}

	FElysiumItemTable MakeCombat10ItemTable()
	{
		FElysiumItemTable Table;

		FElysiumItemDef Fists;
		Fists.Classname = GCombat10Fists;
		Fists.PrintName = TEXT("Fists");
		Fists.Type = EElysiumItemType::WeaponMelee;
		Fists.bHidden = true;
		Fists.Modes.Add(Combat10Mode(TEXT("2 Bashing Close_Combat_Brawl DMG_FIST"), 0.f, 0));
		Table.Items.Add(MoveTemp(Fists));

		FElysiumItemDef Claws;
		Claws.Classname = GCombat10WerewolfAttacks;
		Claws.PrintName = TEXT("Werewolf attacks");
		Claws.Type = EElysiumItemType::WeaponMelee;
		Claws.bHidden = true;
		Claws.Modes.Add(Combat10Mode(TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 0.f, 0));
		Table.Items.Add(MoveTemp(Claws));

		FElysiumItemDef Katana;
		Katana.Classname = GCombat10Katana;
		Katana.PrintName = TEXT("Katana");
		Katana.Type = EElysiumItemType::WeaponMelee;
		Katana.Modes.Add(Combat10Mode(TEXT("3 Lethal Close_Combat_Melee DMG_SLASH"), 0.f, 0));
		Table.Items.Add(MoveTemp(Katana));

		FElysiumItemDef Rifle;
		Rifle.Classname = GCombat10BachRifle;
		Rifle.PrintName = TEXT("Bach's rifle");
		Rifle.Type = EElysiumItemType::WeaponFirearm;
		Rifle.AmmoType = TEXT("Combat10Round");
		Rifle.MagazineSize = 5;
		Rifle.DefaultAmmo = 5;
		Rifle.Modes.Add(Combat10Mode(TEXT("4 Lethal Ranged_Combat DMG_BULLET"), 2000.f, 1));
		Table.Items.Add(MoveTemp(Rifle));

		FElysiumItemDef Stack;
		Stack.Classname = GCombat10Stackable;
		Stack.PrintName = TEXT("Throwables");
		Stack.Type = EElysiumItemType::WeaponFirearm;
		Stack.bStackable = true;
		Stack.StackLimit = 10;
		Stack.AmmoType = TEXT("Combat10Round");
		Stack.MagazineSize = 1;
		Stack.DefaultAmmo = 1;
		Stack.Modes.Add(Combat10Mode(TEXT("3 Lethal Ranged_Combat DMG_BULLET"), 900.f, 1));
		Table.Items.Add(MoveTemp(Stack));

		Table.Reindex();
		return Table;
	}

	struct FCombat10Fixture
	{
		FElysiumItemTable Items;
		bool bInstalled = false;
		FElysiumNpcWorldFixture Fixture;
		FElysiumEntityWorld& World;
		FElysiumNpc* Fighter = nullptr;
		FElysiumNpc* Foe = nullptr;
		FElysiumPlayer* Player = nullptr;

		static FElysiumNpcWorldBuilder BuildWorld()
		{
			FElysiumNpcWorldBuilder Builder(TEXT("__combat10_test__"), 0x434F4D42);
			Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
			Builder.AddNpc(TEXT("fighter"));
			Builder.AddNpc(TEXT("foe"), FVector(200.0, 0.0, 0.0));
			return Builder;
		}

		FCombat10Fixture()
			: Items(MakeCombat10ItemTable())
			, Fixture(BuildWorld(), [this](FElysiumRecordingServices&)
				{
					ElysiumItems::Install(Items);
					bInstalled = true;
				})
			, World(Fixture.World)
		{
			Fighter = Fixture.Npc(TEXT("fighter"));
			Foe = Fixture.Npc(TEXT("foe"));
			Player = Fixture.Player();
			if (Player != nullptr)
			{
				Player->Origin = FVector(0.0, 9000.0 * ElysiumMove::U, 0.0);
			}
			FElysiumNpcWorldFixture::Quiet({ Fighter, Foe });
			for (FElysiumNpc* Npc : { Fighter, Foe })
			{
				if (Npc != nullptr)
				{
					// The NPC loadout resolves on the fixture's own first think and hands an NPC with
					// no authored equipment `item_w_fists` — which is exactly the state slot 304's
					// body exists to reach, so every case here starts EMPTY-HANDED and arms what it
					// wants. Without this the capability word reads Melee on spawn and half this
					// suite would be measuring the loadout rather than the bodies.
					Npc->Inventory.Holster(*Npc);
					while (Npc->Inventory.Num() > 0)
					{
						FElysiumItem* const Carried = Npc->Inventory.At(*Npc, 0);
						if (Carried == nullptr)
						{
							break;
						}
						Npc->Inventory.Detach(*Npc, *Carried);
						Carried->Kill();
					}
					// The sheet IS retail's type-0 `CVStatList_t`; a headless world has no rulebook
					// behind `SeedSheet`, so the pair is stated here.
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 20);
					Npc->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
					Npc->RecomputeSheet();
					Npc->MaxHealth = 100;
				}
			}
		}

		~FCombat10Fixture()
		{
			if (bInstalled)
			{
				ElysiumItems::Uninstall(Items);
			}
		}

		FCombat10Fixture(const FCombat10Fixture&) = delete;
		FCombat10Fixture& operator=(const FCombat10Fixture&) = delete;

		// Hand the fighter a live weapon through the real inventory route — the same one
		// `GiveNamedItem` (`0x1021fe50`) is.
		FElysiumItem* Arm(const TCHAR* Classname)
		{
			if (Fighter == nullptr)
			{
				return nullptr;
			}
			const FElysiumEntityHandle Handle = Fighter->Inventory.GiveNamedItem(*Fighter,
				FString(Classname));
			FElysiumEntity* Entity = World.Resolve(Handle);
			FElysiumItem* Item = Entity != nullptr ? Entity->AsItem() : nullptr;
			if (Item != nullptr)
			{
				Fighter->Inventory.SetActiveWeapon(*Fighter, *Item);
			}
			return Item;
		}
	};
}

// =================================================================================================
// `0x1032fe60` — `CBaseCombatCharacter::HealthToPercent`, slot 348.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10HealthToPercentTest,
	"Elysium.Substrate.NpcKernelCombat10.HealthToPercent", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10HealthToPercentTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}

	// `1032ff24` / `1032ff8f` / `1032ffa8`: `((stat0x11 - stat0xf) * m_iMaxHealth) / stat0x11`.
	// Stat `0x11` is the CAP and stat `0xf` the accumulated WOUND counter — the sense the checklist
	// and one reviewer both had to recover, and which `SyncHealthFromSheet` already relies on.
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 20);
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
	F.Fighter->RecomputeSheet();
	F.Fighter->MaxHealth = 100;
	TestEqual(TEXT("0x1032fe60 an unwounded body reads 100%"), F.Fighter->HealthToPercent(), 100);

	// `RecomputeSheet` re-derives the engine-space `m_iMaxHealth` from the sheet
	// (`SyncHealthFromSheet`), so the engine-space ceiling is re-stated after every write — it is a
	// DIFFERENT number from the stat-0x11 cap and this body multiplies by it.
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 5);
	F.Fighter->RecomputeSheet();
	F.Fighter->MaxHealth = 100;
	TestEqual(TEXT("...a quarter of the cap in wounds reads 75%"),
		F.Fighter->HealthToPercent(), 75);

	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 20);
	F.Fighter->RecomputeSheet();
	F.Fighter->MaxHealth = 100;
	TestEqual(TEXT("...wounds at the cap read 0%"), F.Fighter->HealthToPercent(), 0);

	// The INTEGER divide is retail's: 7 wounds of 20 is (13 * 100) / 20 = 65, not 65.0 rounded.
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 7);
	F.Fighter->RecomputeSheet();
	F.Fighter->MaxHealth = 100;
	TestEqual(TEXT("...and the divide is integral (1032ffa8)"), F.Fighter->HealthToPercent(), 65);

	// The typed join: type 0 IS the sheet, types 2 and 3 are the empty lazily-built global.
	TestTrue(TEXT("the type-0 CVStatList_t is this runtime's sheet"),
		F.Fighter->HasTypedStatList(0));
	TestFalse(TEXT("...the type-2 buff list has no port container"), F.Fighter->HasTypedStatList(2));
	TestFalse(TEXT("...nor the type-3 scripted list"), F.Fighter->HasTypedStatList(3));
	TestEqual(TEXT("...and an absent list reads DAT_109f0b40's zero"),
		F.Fighter->TypedStatValue(3, ElysiumSlot::MaxHealth), 0);
	return true;
}

// =================================================================================================
// `0x103970d0` — `CNPC_VMingXiao::vfunc348`, slot 348's one species arm.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10MingXiaoHealthToPercentTest,
	"Elysium.Substrate.NpcKernelCombat10.MingXiaoHealthToPercent", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10MingXiaoHealthToPercentTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 20);
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 5);
	F.Fighter->RecomputeSheet();
	F.Fighter->MaxHealth = 100;

	// The named case that proves the dispatch: a plain `npc_VCop` has NO retail class (its census
	// classname list is null, story 29c-1's cleanup), so slot 348 takes the Troika-line body.
	TestNull(TEXT("a spawned npc_VCop resolves to no retail class"),
		ElysiumNpcKernelClass::OfClassname(TEXT("npc_VCop")));
	F.Fighter->SetRetailClassForTests(nullptr);
	TestEqual(TEXT("0x1032fe60 the Troika body answers for a class with no override"),
		F.Fighter->HealthToPercent(), 75);

	// `CNPC_VMingXiao#348` is the one override at this slot.
	TestNotNull(TEXT("CNPC_VMingXiao overrides slot 348"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VMingXiao")), 348));
	F.Fighter->SetRetailClassForTests(TEXT("CNPC_VMingXiao"));
	// `10397250`: the limb loop folds one extra contribution in per ATTACHED limb. This runtime
	// stands no severable limbs (`0x10398000` answers false for all six), so the arm equals the base
	// — retail's own answer for an intact boss.
	TestEqual(TEXT("0x103970d0 with every limb absent the arm equals the base"),
		F.Fighter->HealthToPercent(), 75);
	TestFalse(TEXT("0x10398000 stands no limb"), F.Fighter->MingXiaoLimbPresent(0));
	F.Fighter->SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `0x103c6830` — `CNPC_VVampireBoss::GetCurrHealthPercent`, no slot.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10CurrHealthPercentTest,
	"Elysium.Substrate.NpcKernelCombat10.GetCurrHealthPercent", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10CurrHealthPercentTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 20);
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 5);
	F.Fighter->RecomputeSheet();

	// `103c68db` reads stat `0x0f` FIRST (the numerator) and `103c694d` stat `0x11` SECOND (the
	// divisor), so the answer is the DAMAGE fraction — the complement of `HealthToPercent`.
	TestEqual(TEXT("0x103c6830 answers stat0xf / stat0x11, the damage fraction"),
		F.Fighter->GetCurrHealthPercent(), 0.25f, 1e-5f);

	// `103c6964`: the guard is `ABS(cap) > 1e-05` on the DIVISOR — the decompiler's
	// `(a < eps) == (a == eps)` idiom — and the refusal answer is `_DAT_104454c4` = 0.0.
	F.Fighter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 0);
	F.Fighter->RecomputeSheet();
	TestEqual(TEXT("...a zero cap takes the divide-by-zero guard and answers _DAT_104454c4"),
		F.Fighter->GetCurrHealthPercent(), 0.0f);
	return true;
}

// =================================================================================================
// `0x102b5b20` / `0x102b5b70` — slots 304 and 305, the Troika fighting-item pair.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10GiveBaseFightingItemsTest,
	"Elysium.Substrate.NpcKernelCombat10.GiveBaseFightingItems", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10GiveBaseFightingItemsTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	F.Fighter->SetRetailClassForTests(nullptr);
	F.Fighter->MiscFlags = 0;

	// `102b5b47`: with neither slot 307 nor slot 308 answering, `item_w_fists` is granted and misc
	// flag `0x10` is set.
	F.Fighter->GiveBaseFightingItems();
	TestTrue(TEXT("0x102b5b20 an unarmed body is given item_w_fists"),
		F.Fighter->InventoryFindByClassname(GCombat10Fists));
	TestTrue(TEXT("...and misc flag 0x10 is set (1033c6b0)"),
		ElysiumMiscFlags::Has(F.Fighter->MiscFlags,
			FElysiumNpc::MiscFlagBaseFightingItems));

	// `102b5b25` / `102b5b34`: an ARMED body is refused at the first gate. The fists are already the
	// active weapon, so the capability word reads Melee and the body declines a second grant.
	const int32 Before = F.Fighter->Inventory.Num();
	F.Fighter->GiveBaseFightingItems();
	TestEqual(TEXT("...and an armed body is refused, so nothing is granted twice"),
		F.Fighter->Inventory.Num(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10RemoveBaseFightingItemsTest,
	"Elysium.Substrate.NpcKernelCombat10.RemoveBaseFightingItems", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10RemoveBaseFightingItemsTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	F.Fighter->SetRetailClassForTests(nullptr);
	F.Fighter->MiscFlags = 0;

	// `102b5b75`: with the flag CLEAR the body does nothing at all — not even a lookup.
	F.Arm(GCombat10Fists);
	F.Fighter->RemoveBaseFightingItems();
	TestTrue(TEXT("0x102b5b70 with misc flag 0x10 clear nothing is removed"),
		F.Fighter->InventoryFindByClassname(GCombat10Fists));

	// `102b5b85`: with the flag set the item goes and the flag is cleared.
	ElysiumMiscFlags::Set(F.Fighter->MiscFlags, FElysiumNpc::MiscFlagBaseFightingItems);
	F.Fighter->RemoveBaseFightingItems();
	TestFalse(TEXT("...with it set item_w_fists is removed (1021fee0)"),
		F.Fighter->InventoryFindByClassname(GCombat10Fists));
	TestFalse(TEXT("...and the flag is cleared, mirroring the grant exactly"),
		ElysiumMiscFlags::Has(F.Fighter->MiscFlags, FElysiumNpc::MiscFlagBaseFightingItems));
	return true;
}

// =================================================================================================
// `0x103cc9b0` / `0x103cca80` — `CNPC_VWerewolf`'s slot 304 / 305 pair.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10WerewolfGiveBaseFightingItemsTest,
	"Elysium.Substrate.NpcKernelCombat10.WerewolfGiveBaseFightingItems",
	GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10WerewolfGiveBaseFightingItemsTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	// UNREACHABLE IN PLAY: `CNPC_VWerewolf` carries no entity classname in the census, so no spawned
	// NPC's `RetailClass()` can be it. The arm is ported and is reached the only way it can be.
	TestNull(TEXT("no classname resolves to CNPC_VWerewolf"),
		ElysiumNpcKernelClass::OfClassname(TEXT("npc_VWerewolf")));
	TestNotNull(TEXT("...but the census carries the class and its slot-304 override"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VWerewolf")), 304));

	F.Fighter->MiscFlags = 0;
	F.Fighter->SetRetailClassForTests(TEXT("CNPC_VWerewolf"));
	// `103cca32`: the arm consults NEITHER base gate. An ARMED werewolf still gets its claws, which
	// is the whole difference from the base.
	F.Arm(GCombat10Katana);
	F.Fighter->GiveBaseFightingItems();
	TestTrue(TEXT("0x103cc9b0 the werewolf is given item_w_werewolf_attacks"),
		F.Fighter->InventoryFindByClassname(GCombat10WerewolfAttacks));
	TestFalse(TEXT("...and never item_w_fists"),
		F.Fighter->InventoryFindByClassname(GCombat10Fists));
	TestTrue(TEXT("...and misc flag 0x10 is set"),
		ElysiumMiscFlags::Has(F.Fighter->MiscFlags, FElysiumNpc::MiscFlagBaseFightingItems));

	// `103cca32` again: a body that ALREADY carries the item is refused.
	const int32 Before = F.Fighter->Inventory.Num();
	F.Fighter->GiveBaseFightingItems();
	TestEqual(TEXT("...Inventory_Find refuses a second grant"), F.Fighter->Inventory.Num(), Before);

	// The named Troika case: with no retail class the base body runs and grants fists instead.
	F.Fighter->SetRetailClassForTests(nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10WerewolfRemoveBaseFightingItemsTest,
	"Elysium.Substrate.NpcKernelCombat10.WerewolfRemoveBaseFightingItems",
	GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10WerewolfRemoveBaseFightingItemsTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	F.Fighter->MiscFlags = 0;
	F.Fighter->SetRetailClassForTests(TEXT("CNPC_VWerewolf"));
	F.Fighter->GiveBaseFightingItems();
	TestTrue(TEXT("the claws are carried"),
		F.Fighter->InventoryFindByClassname(GCombat10WerewolfAttacks));

	// `103ccb32`: the same `0x10` gate as the base, with the werewolf's own classname.
	F.Fighter->RemoveBaseFightingItems();
	TestFalse(TEXT("0x103cca80 the werewolf's claws are removed"),
		F.Fighter->InventoryFindByClassname(GCombat10WerewolfAttacks));
	TestFalse(TEXT("...and the flag is cleared"),
		ElysiumMiscFlags::Has(F.Fighter->MiscFlags, FElysiumNpc::MiscFlagBaseFightingItems));

	// With the flag clear it is a no-op, exactly as the base is.
	F.Fighter->GiveBaseFightingItems();
	ElysiumMiscFlags::Clear(F.Fighter->MiscFlags, FElysiumNpc::MiscFlagBaseFightingItems);
	F.Fighter->RemoveBaseFightingItems();
	TestTrue(TEXT("...and with the flag clear nothing is removed"),
		F.Fighter->InventoryFindByClassname(GCombat10WerewolfAttacks));
	F.Fighter->SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `0x1032ce40` — slot 386 `Weapon_Drop()`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10WeaponDropActiveTest,
	"Elysium.Substrate.NpcKernelCombat10.WeaponDropActive", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10WeaponDropActiveTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (!TestNotNull(TEXT("the rifle is carried and active"), Rifle))
	{
		return false;
	}
	TestEqual(TEXT("GetActiveWeapon answers the inventory's active slot (0x10007e19)"),
		F.Fighter->ActiveWeaponEntity(), static_cast<FElysiumEntity*>(Rifle));

	// `1032cec2`: `m_bCantDropWeapons` (`+0x1589`) is the FIRST gate, and it refuses everything.
	F.Fighter->bCantDropWeapons = true;
	F.Fighter->Weapon_Drop();
	TestEqual(TEXT("0x1032ce40 m_bCantDropWeapons refuses the drop"),
		F.Fighter->WeaponDropNotifies, 0);
	F.Fighter->bCantDropWeapons = false;

	// `1032cefa`: an `FL_NPC` body REROLLS the ammo before dropping — two entries, `i = 0` and `1`
	// (`iVar9` steps `0x1093c` while below `0x21278`, which is exactly twice).
	F.Fighter->Inventory.PreviousWeapon = Rifle->Handle;
	F.Fighter->LastMeleeWeapon = Rifle->Handle;
	F.Fighter->Weapon_Drop();
	if (TestEqual(TEXT("...the ammo reroll ran once"), F.Fighter->WeaponAmmoRerolls.Num(), 1))
	{
		TestEqual(TEXT("...over exactly two entries (0x21278 / 0x1093c)"),
			static_cast<int32>(UE_ARRAY_COUNT(F.Fighter->WeaponAmmoRerolls[0].Entry)), 2);
		// `1032cf4a`: the `+0x450` cap is a seam answering 0, which is below 1, so retail writes a
		// FLAT ZERO rather than rolling.
		TestEqual(TEXT("...and a cap below 1 writes a flat zero, it does not roll"),
			F.Fighter->WeaponAmmoRerolls[0].Entry[0], 0);
	}
	// `1032cfa8` / `1032cfeb`: both handles reset, because both resolved to THIS weapon.
	TestFalse(TEXT("...m_hLastWeapon is reset to 0xffffffff"),
		F.Fighter->Inventory.PreviousWeapon.IsSet());
	TestFalse(TEXT("...and m_hLastMeleeWeapon with it"), F.Fighter->LastMeleeWeapon.IsSet());
	// `1032d02c`: the weapon's `+0x4b0`, `Inventory_Remove`, then `Weapon_Detach` under `FL_NPC`.
	TestEqual(TEXT("...the drop notify fired once"), F.Fighter->WeaponDropNotifies, 1);
	TestEqual(TEXT("...and Weapon_Detach after it"), F.Fighter->WeaponDetaches, 1);
	TestFalse(TEXT("...and the weapon is no longer carried"),
		F.Fighter->InventoryFindByClassname(GCombat10BachRifle));
	return true;
}

// =================================================================================================
// `0x1032d0c0` — slot 385 `Weapon_Drop(weapon, pos, force)`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10WeaponDropNamedTest,
	"Elysium.Substrate.NpcKernelCombat10.WeaponDropNamed", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10WeaponDropNamedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}

	// `1032d129`: a null weapon does nothing at all.
	F.Fighter->Weapon_Drop(nullptr, nullptr, false);
	TestEqual(TEXT("0x1032d0c0 a null weapon does nothing"), F.Fighter->WeaponDropNotifies, 0);

	// `1032d13d`: the ACTIVE weapon takes the holster path — `this->vtable +0x608`, which is slot
	// 386 (`0x608 / 4 == 386`), this class's own no-argument `Weapon_Drop`.
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (!TestNotNull(TEXT("the rifle is carried"), Rifle))
	{
		return false;
	}
	F.Fighter->Weapon_Drop(Rifle, nullptr, false);
	TestEqual(TEXT("...the active weapon is routed through slot 386"),
		F.Fighter->WeaponDropNotifies, 1);
	TestEqual(TEXT("...which is the arm that rerolls the ammo"),
		F.Fighter->WeaponAmmoRerolls.Num(), 1);

	// **CORRECTION**: `param_1[0x234]` is BYTE offset `0x8d0` — `m_iItemCount`, the STACK — and the
	// `+0x5cc` gate beside it is stackability, not a magazine test. `1032d27a`: a stack of two or
	// more DECREMENTS and spawns one loose copy, and it RETURNS without removing the original.
	FElysiumItem* const Stack = F.Arm(GCombat10Stackable);
	if (!TestNotNull(TEXT("the stackable weapon is carried"), Stack))
	{
		return false;
	}
	Stack->ItemCount = 3;
	// It is the active weapon, so hand slot 385 something else first to reach the split: park it.
	F.Fighter->Inventory.Holster(*F.Fighter);
	const int32 NotifiesBefore = F.Fighter->WeaponDropNotifies;
	F.Fighter->Weapon_Drop(Stack, nullptr, false);
	TestEqual(TEXT("0x1032d16d the stack is decremented, not removed"), Stack->ItemCount, 2);
	TestTrue(TEXT("...and the original is still carried"),
		F.Fighter->InventoryFindByClassname(GCombat10Stackable));
	TestEqual(TEXT("...while the spawned copy takes the drop notify"),
		F.Fighter->WeaponDropNotifies, NotifiesBefore + 1);

	// `1032d158`: a non-droppable weapon with `bForce` false is refused outright.
	Stack->ItemCount = 1;
	const int32 Notifies = F.Fighter->WeaponDropNotifies;
	TestTrue(TEXT("the catalogue's default is droppable"),
		FElysiumNpc::WeaponIsDroppable(Stack));
	F.Fighter->Weapon_Drop(Stack, nullptr, false);
	TestEqual(TEXT("...a single droppable unit takes the ordinary removal"),
		F.Fighter->WeaponDropNotifies, Notifies + 1);
	TestFalse(TEXT("...and leaves the inventory"),
		F.Fighter->InventoryFindByClassname(GCombat10Stackable));
	return true;
}

// =================================================================================================
// `0x102c2ec0` — slot 589 `SetScriptedDiscipline`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10SetScriptedDisciplineTest,
	"Elysium.Substrate.NpcKernelCombat10.SetScriptedDiscipline", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10SetScriptedDisciplineTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	constexpr int32 Stat = 6;   // any discipline slot; the body is stat-agnostic

	// The type-3 list has no port container, so `GetBase` reads the empty global's zero and every
	// write lands in the ledger — which is where the ORDER of retail's two-step pairs is observable.
	F.Fighter->TypedStatWrites.Reset();

	// `102c2f9c`–`102c30e6`: a value ABOVE the base does `Set(list2)`, `AddBase(list3)`, `Set(list3)`.
	F.Fighter->SetScriptedDiscipline(Stat, 4);
	if (TestEqual(TEXT("0x102c2ec0 a rise writes three times"),
		F.Fighter->TypedStatWrites.Num(), 3))
	{
		TestEqual(TEXT("...the type-2 buff list first (102c3002)"),
			F.Fighter->TypedStatWrites[0].ListType, 2);
		TestEqual(TEXT("...Set"), FString(F.Fighter->TypedStatWrites[0].Op), FString(TEXT("Set")));
		TestEqual(TEXT("...then AddBase on the type-3 scripted list (102c3081)"),
			FString(F.Fighter->TypedStatWrites[1].Op), FString(TEXT("AddBase")));
		TestEqual(TEXT("...by value - base"), F.Fighter->TypedStatWrites[1].Value, 4);
		TestEqual(TEXT("...and only THEN Set on it (102c30e6)"),
			FString(F.Fighter->TypedStatWrites[2].Op), FString(TEXT("Set")));
		TestEqual(TEXT("...both intermediate states are observable, which is why the order matters"),
			F.Fighter->TypedStatWrites[2].ListType, 3);
	}

	// `102c3169` / `102c31c3`: a value BELOW the base does `Set(list3)` FIRST, then `SubBase(list3)`.
	// With the type-3 list absent its base still reads 0, so the fall is driven with a negative.
	F.Fighter->TypedStatWrites.Reset();
	F.Fighter->SetScriptedDiscipline(Stat, -2);
	if (TestEqual(TEXT("a fall writes twice"), F.Fighter->TypedStatWrites.Num(), 2))
	{
		TestEqual(TEXT("...Set first (102c3169)"),
			FString(F.Fighter->TypedStatWrites[0].Op), FString(TEXT("Set")));
		TestEqual(TEXT("...then SubBase by base - value (102c31c3)"),
			FString(F.Fighter->TypedStatWrites[1].Op), FString(TEXT("SubBase")));
		TestEqual(TEXT("...which is 0 - (-2)"), F.Fighter->TypedStatWrites[1].Value, 2);
	}

	// Equal values write NOTHING at all — retail falls out of both branches.
	F.Fighter->TypedStatWrites.Reset();
	F.Fighter->SetScriptedDiscipline(Stat, 0);
	TestEqual(TEXT("...and an equal value writes nothing at all"),
		F.Fighter->TypedStatWrites.Num(), 0);
	return true;
}

// =================================================================================================
// `0x102ad340` — slot 460 `PreSelectIdealState`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10PreSelectIdealStateTest,
	"Elysium.Substrate.NpcKernelCombat10.PreSelectIdealState", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10PreSelectIdealStateTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;

	// The literal `0x3cf445af` decodes to **2** through `0x1042fe90` — the bound the criminal arm
	// compares against, and the one number in the body that is not written out.
	TestEqual(TEXT("0x1042fe90(0x3cf445af) == 2"),
		static_cast<int32>(FElysiumNpc::SecureUnhashLevel(0x3cf445afu)), 2);

	// `102ad459`: no condition at all answers 0 WITHOUT touching the ideal state.
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("0x102ad340 with no condition the body answers 0"),
		N.PreSelectIdealStateRetail(), 0);

	// `102ad34f`: a non-zero `m_eForcedState` (`+0x65cc`) wins outright and is CLEARED.
	// **CORRECTION**: `0x102ae840` stamps a raw `NPC_STATE` there, not the order id
	// `ElysiumAiScriptedSchedule.h` described.
	N.ScriptedScheduleOrder.RetailOrderId = 3;
	TestEqual(TEXT("102ad34f the forced state is returned"), N.PreSelectIdealStateRetail(), 3);
	TestEqual(TEXT("...and cleared"), N.ForcedNpcState(), 0);

	// `102ad3ef`: `COND_SEE_FEAR` as an INTERRUPT condition. **CORRECTION**: the flag write is
	// nested — armed only while the state is not already 8 — and the ideal state is then written
	// UNCONDITIONALLY, where the checklist's walk reads it as an alternative.
	N.NpcFlags.Clear(EElysiumNpcFlag::INITIAL_FLEE);
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::SeeFear);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		// The program is installed FIRST: `SetSchedule`'s own tail (slot 435 `OnScheduleChange`)
		// runs on the way in, and the condition is raised after it so the install cannot consume it.
		TestTrue(TEXT("the interrupt carrier installs"),
			ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N));
		N.Cognition.Conditions.Set(EElysiumNpcCond::SeeFear);
		TestEqual(TEXT("102ad3ef SEE_FEAR answers retail state 8"), N.PreSelectIdealStateRetail(), 8);
		TestTrue(TEXT("...and INITIAL_FLEE (0x100) is armed on the way in"),
			N.NpcFlags.Has(EElysiumNpcFlag::INITIAL_FLEE));
		// The generated virtual returns this runtime's typed state, which has no member for retail 8;
		// the RAW id is the deliverable and it is kept beside the typed answer.
		N.PreSelectIdealState();
		TestEqual(TEXT("...and the typed virtual records the raw id beside its own answer"),
			N.LastPreSelectIdealStateRetail, 8);
	}
	N.Cognition.Conditions.Clear(EElysiumNpcCond::SeeFear);
	N.Schedule.Clear();

	// `102ad429`: `COND_SUPERNATURAL_ATTACK_LEVEL` answers 2.
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::SupernaturalAttackLevel);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
		N.Cognition.Conditions.Set(EElysiumNpcCond::SupernaturalAttackLevel);
		TestEqual(TEXT("102ad429 SUPERNATURAL_ATTACK_LEVEL answers 2"),
			N.PreSelectIdealStateRetail(), 2);
	}
	N.Cognition.Conditions.Clear(EElysiumNpcCond::SupernaturalAttackLevel);
	N.Schedule.Clear();

	// `102ad4d9` / `102ad578`: the criminal arm. A level at or under the bound with suspicion allowed
	// and no hated offender writes `m_iSubState = 0` and answers retail state `0xe`.
	N.SubState = 7;
	N.Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Level = 1;
	N.Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Offender =
		FElysiumEntityHandle::Invalid();
	N.Witness.bAllowCriminalSuspicion = true;
	{
		FElysiumNpcConditions Mask;
		Mask.Set(EElysiumNpcCond::CriminalAttackLevel);
		const ElysiumSchedule::FInterruptMaskScope Scope(ElysiumSched::IDLE_STAND, Mask);
		ElysiumSchedule::Start(N.Schedule, ElysiumSched::IDLE_STAND, N);
		N.Cognition.Conditions.Set(EElysiumNpcCond::CriminalAttackLevel);
		TestEqual(TEXT("102ad578 the suspicion window answers retail state 0xe"),
			N.PreSelectIdealStateRetail(), 0xe);
		TestEqual(TEXT("...and zeroes m_iSubState (+0x63f8)"), N.SubState, 0);

		// `102ad58a`: with suspicion refused the shared tail writes combat instead.
		N.Witness.bAllowCriminalSuspicion = false;
		TestEqual(TEXT("102ad58a without it the shared tail answers 2"),
			N.PreSelectIdealStateRetail(), 2);

		// `102ad4f4`: a level ABOVE the bound skips the whole window and lands on the same tail.
		N.Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).Level = 3;
		N.Witness.bAllowCriminalSuspicion = true;
		TestEqual(TEXT("102ad4f4 a level above 2 skips the window"),
			N.PreSelectIdealStateRetail(), 2);
	}
	N.Schedule.Clear();
	return true;
}

// =================================================================================================
// `0x1033b5f0` — slot 357, the prayer pulse.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10PrayerPulseTest,
	"Elysium.Substrate.NpcKernelCombat10.PrayerPulse", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10PrayerPulseTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;

	// `1033b5f0`: the activity gate. Family Hints' `CurrentRetailActivityId()` is the seam for
	// `m_Activity` (`+0xfec`) and answers `-1`, which is not `0x132` — retail's own answer for a body
	// that is not praying, so the slot refuses and nothing pulses.
	TestNotEqual(TEXT("0x1033b5f0 m_Activity is a seam and is never the prayer activity"),
		N.CurrentRetailActivityId(), FElysiumNpc::PrayerActivityId);
	N.FeedState.NextPulse = 0.f;
	N.FeedState.Interval = 1.f;
	N.Slot357();
	TestEqual(TEXT("...so the slot refuses and the cadence is untouched"),
		N.FeedState.Interval, 1.f);

	// The recovered half below the gate. `1033b673`: stat `0x0e` (FaithPoints) below its authored
	// maximum takes `IncBase`; at or above it, slot 358 `PrayerEnd`.
	N.TypedStatWrites.Reset();
	N.Sheet.SetBase(EElysiumTraitContainer::Attributes, GCombat10StatFaithPoints, 0);
	N.RecomputeSheet();
	N.FeedState.Interval = 1.0f;
	N.RunPrayerPulse(10.0);
	// `FaithPointsMaximum` reads the authored `Max`, and a headless world has no `stats.txt`, so it
	// answers 0 — which makes `GetValue >= max` true and takes the `PrayerEnd` arm. That is the
	// stated fallback, and it is what the ledger shows: no `IncBase`.
	TestEqual(TEXT("0x10200370 with no rulebook the authored maximum is 0"),
		N.FaithPointsMaximum(), 0);
	TestEqual(TEXT("...so the pulse takes the PrayerEnd arm rather than incrementing for ever"),
		N.TypedStatWrites.Num(), 0);

	// `1033b712`: the cadence tail runs on EVERY pulse, whichever arm was taken.
	TestEqual(TEXT("1033b712 m_flNextFeedPulse = curtime + m_flNextFeedDuration"),
		N.FeedState.NextPulse, 11.0f, 1e-4f);
	// `_DAT_1049e890` is a DOUBLE reading 0.15; the floor `_DAT_1047b868` is 0.3.
	TestEqual(TEXT("...and the interval accelerates by _DAT_1049e890 (0.15)"),
		N.FeedState.Interval, 0.85f, 1e-4f);

	// The floor holds: once at or below 0.3 the interval stops shrinking.
	N.FeedState.Interval = 0.3f;
	N.RunPrayerPulse(20.0);
	TestEqual(TEXT("..._DAT_1047b868 (0.3) is a floor, not a target"), N.FeedState.Interval, 0.3f);
	return true;
}

// =================================================================================================
// `0x102a0290` — the knockback velocity builder.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10KnockbackVelocityTest,
	"Elysium.Substrate.NpcKernelCombat10.KnockbackVelocity", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10KnockbackVelocityTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter) || F.Foe == nullptr)
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;

	// `102a02a7`: with a NULL source the body takes the angles' forward and negates it; `102a0395`
	// then zeroes Z and normalises, and the blend parameter is the bare `_DAT_104454c0` = 1.0 — so
	// the factors are the HIGH ends, 400 and 310. The `5.0` initialiser is DEAD on this path.
	N.Angles = FVector::ZeroVector;
	N.ComputeKnockbackVelocity(nullptr);
	TestEqual(TEXT("0x102a0290 a null source pushes backwards along the body's own forward"),
		N.KnockbackVelocity.X, -400.0, 1e-3);
	TestEqual(TEXT("...with Y at zero for a zero yaw"), N.KnockbackVelocity.Y, 0.0, 1e-3);
	// `102a03f3`: Z is multiplied by the XY factor and then REPLACED outright by the Z factor.
	TestEqual(TEXT("...and Z REPLACED by _DAT_1049a1e4 (310), not scaled"),
		N.KnockbackVelocity.Z, 310.0, 1e-3);

	// `102a02ee` / `102a0348`: with a source the vector is THIS body's origin minus the source's,
	// and the blend parameter is `GetRawAttackValue * _DAT_104491b4` (0.1). The raw value is a seam
	// answering 0.0, so `t` is 0 and the factors are the LOW ends, 220 and 200.
	N.Origin = FVector(0.0, 0.0, 0.0);
	F.Foe->Origin = FVector(-100.0 * ElysiumMove::U, 0.0, 0.0);
	TestEqual(TEXT("GetRawAttackValue is a seam answering 0.0"),
		N.SourceRawAttackValue(F.Foe), 0.0f);
	N.ComputeKnockbackVelocity(F.Foe);
	TestEqual(TEXT("102a0348 the push is away from the source"),
		N.KnockbackVelocity.X, 220.0, 1e-3);
	TestEqual(TEXT("...at _DAT_1049a1d8 (220) because t is 0"),
		N.KnockbackVelocity.Size2D(), 220.0, 1e-3);
	TestEqual(TEXT("...and Z at _DAT_1049a1e0 (200)"), N.KnockbackVelocity.Z, 200.0, 1e-3);

	// `102a0307`: the 139..147 weapon-id family is a seam answering false, so the source's own
	// velocity is never copied and the origin subtraction is the arm taken.
	TestFalse(TEXT("0x10344da0 stands no retail weapon id"),
		N.SourceWeaponIdIsKnockbackFamily(F.Foe));
	return true;
}

// =================================================================================================
// `0x102a1650` — the yaw clearance sweep.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10YawClearanceTest,
	"Elysium.Substrate.NpcKernelCombat10.YawClearance", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10YawClearanceTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	N.Origin = FVector::ZeroVector;
	N.Angles = FVector::ZeroVector;

	// `102a1656`–`102a17b5`: at yaw 0 the sweep is the FORWARD leg alone — `_DAT_10462950` = **40**
	// Source units times the reach — because sin(0) cancels the right leg. Both cells were read out
	// of the pinned image; the checklist named neither.
	TestTrue(TEXT("0x102a1650 the hull seam reports a clear sweep, which admits the task"),
		N.TraceMoveClearanceAtYaw(0, 0.f, 1.f));
	TestEqual(TEXT("...the start is GetAbsOrigin (slot 217)"),
		N.LastYawClearanceSweep.StartUnits.X, 0.0, 1e-3);
	TestEqual(TEXT("..._DAT_10462950 is 40.0, so a reach of 1 sweeps 40 units forward"),
		N.LastYawClearanceSweep.EndUnits.X, 40.0, 1e-3);

	// At yaw 90 the cosine cancels the forward leg and `_DAT_10451acc` = **64** carries the right
	// leg. `ElysiumNpcKernelSchedule.cpp` still reads the same cell as UNRECOVERED 0.0.
	N.TraceMoveClearanceAtYaw(0, 90.f, 1.f);
	TestEqual(TEXT("..._DAT_10451acc is 64.0, the right leg"),
		N.LastYawClearanceSweep.EndUnits.Y, 64.0, 1e-3);

	// `102a17fa`: the mask is `0x202400b`, the same mask the rest of the kernel traces with.
	TestEqual(TEXT("...and the mask is 0x202400b"), N.LastYawClearanceSweep.Mask, 0x202400b);

	// The reach scales both legs; `102a1910` calls this with `m_flWaitFinishedDelta`.
	N.TraceMoveClearanceAtYaw(0, 180.f, 2.f);
	TestEqual(TEXT("...the reach scales the leg (a literal 180.0 is one of the four call sites)"),
		N.LastYawClearanceSweep.EndUnits.X, -80.0, 1e-3);
	return true;
}

// =================================================================================================
// `0x102b7cf0` — the taunt-and-cover prologue.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10CombatReactionTest,
	"Elysium.Substrate.NpcKernelCombat10.CombatReaction", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10CombatReactionTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter) || F.Foe == nullptr)
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;

	// `102b7cf6`: `COND_LOST_ENEMY` is the ONE arm outside the enemy gate, and it answers 0x10.
	N.Cognition.Conditions.Reset();
	N.Senses.Memory.Enemy = FElysiumEntityHandle::Invalid();
	N.Cognition.Conditions.Set(EElysiumNpcCond::LostEnemy);
	TestEqual(TEXT("0x102b7cf0 COND_LOST_ENEMY answers 0x10 with no enemy at all"),
		N.SelectCombatReactionSchedule(), 0x10);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::LostEnemy);

	// `102b7d2a`: **CORRECTION** — everything else is INSIDE `GetEnemy() && CanSeekCover()`. A body
	// with no enemy answers 0 whatever its flags say, including the `0x800` taunt bit.
	N.NpcFlags.Set(EElysiumNpcFlag::DODGING);
	TestEqual(TEXT("102b7d2a with no enemy the whole convar-gated half is unreachable"),
		N.SelectCombatReactionSchedule(), 0);
	TestTrue(TEXT("...so the 0x800 bit is NOT consumed"),
		N.NpcFlags.Has(EElysiumNpcFlag::DODGING));

	// With an enemy and cover seeking allowed the taunt arm runs and CONSUMES the bit. Both answers
	// (`0x8e` and `0x8f`) are the roll's; whichever lands, the bit is gone and one of the two
	// observable writes happened.
	N.Senses.Memory.Enemy = F.Foe->Handle;
	const double BeforeDodgeTime = N.NextDodgeTime;
	const int32 Answer = N.SelectCombatReactionSchedule();
	if (Answer != 0)
	{
		TestTrue(TEXT("102b7d8a the taunt arm answers 0x8e or 0x8f"),
			Answer == 0x8e || Answer == 0x8f);
		TestFalse(TEXT("...and CONSUMES the 0x800 bit"),
			N.NpcFlags.Has(EElysiumNpcFlag::DODGING));
		if (Answer == 0x8f)
		{
			// `102b7dcb`: `+0x65a4 += _DAT_10463584`, which is **15.0** out of the pinned image, and
			// it is an ADD rather than a re-base on curtime.
			TestEqual(TEXT("102b7dcb 0x8f advances m_flNextDodgeTime by _DAT_10463584 (15.0)"),
				N.NextDodgeTime, BeforeDodgeTime + 15.0, 1e-4);
		}
		else
		{
			// `102b7df0`: `0x8e` sets `FORCED_OCCLUDE` instead.
			TestTrue(TEXT("102b7df0 0x8e sets FORCED_OCCLUDE (0x10000000)"),
				N.NpcFlags.Has(EElysiumNpcFlag::FORCED_OCCLUDE));
		}
	}
	else
	{
		AddInfo(TEXT("slot 592 CanSeekCover refused, which is its own recovered gate"));
	}
	return true;
}

// =================================================================================================
// `0x102b7fc0` — slot 605, the Troika-line ranged selector, and the two helpers it offers.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10TroikaRangedTest,
	"Elysium.Substrate.NpcKernelCombat10.TroikaRanged", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10TroikaRangedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	N.SetRetailClassForTests(nullptr);   // the Troika line, and what a plain `npc_VCop` reaches
	N.Cognition.Conditions.Reset();

	// `102b7fc6`: arm 1 wins over everything, including the pre-pass.
	N.bInMelee = true;
	TestEqual(TEXT("0x102b7fc0 m_bInMelee answers 0xe3 first"),
		N.SelectScheduleRangedCombat(0), 0xe3);
	N.bInMelee = false;

	// `102b8027`: arm 3, `COND_WEAPON_THROUGH_WALL`, answers **0xb8** on the Troika line — which is
	// the one place `CNPC_VAsianVampire` diverges, and this is the base half of that pair.
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestEqual(TEXT("102b8027 COND 0x3c answers 0xb8 on the Troika line"),
		N.SelectScheduleRangedCombat(0), 0xb8);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::WeaponThroughWall);

	// `102b88b8`: with no usable weapon at all the pre-pass answers 0x98 and the selector returns it
	// — arm 4 wins whenever the pre-pass does.
	TestEqual(TEXT("0x102b8620 an unarmed body answers 0x98"), N.RangedWeaponPrePass(), 0x98);
	TestEqual(TEXT("...and arm 4 hands that straight back"),
		N.SelectScheduleRangedCombat(0), 0x98);

	// `102b87b3`: a body with a usable RANGED weapon and an empty magazine draws it — 0xe9 — and
	// slot 601 runs, which clears `m_bInMelee`.
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
		// `102b86ba`: a NON-EMPTY magazine declines the pre-pass outright.
		TestEqual(TEXT("102b86ba a loaded weapon declines the pre-pass"),
			N.RangedWeaponPrePass(), 0);
		Rifle->MagazineCount = 0;
		N.bInMelee = true;
		TestEqual(TEXT("102b87b3 an empty ranged weapon answers 0xe9"),
			N.RangedWeaponPrePass(), 0xe9);
		TestFalse(TEXT("...and slot 601 cleared m_bInMelee on the way"), N.bInMelee);
	}

	// `102b8056`: the split. With the pre-pass declining, neither COND `0x5f` nor COND `0x8`, and the
	// discipline gate false, the slot-606 branch runs and `COND_TOO_FAR_TO_ATTACK` answers 0xb1.
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	TestFalse(TEXT("0x101e3f50 is a seam answering false, which ADMITS the slot-606 branch"),
		N.RangedDisciplineGate(&N));
	TestEqual(TEXT("102b80bd COND_TOO_FAR_TO_ATTACK answers 0xb1"),
		N.SelectScheduleRangedCombat(0), 0xb1);

	// `102b80a3`: `COND_EXTENDED_BLOCKED_BY_FRIEND` is tried FIRST of the two — the order is the
	// behaviour.
	N.Cognition.Conditions.Set(EElysiumNpcCond::ExtendedBlockedByFriend);
	TestEqual(TEXT("102b80a3 COND 0x2e is tried before COND 0x60"),
		N.SelectScheduleRangedCombat(0), 0xbd);
	N.Cognition.Conditions.Reset();

	// `102b80cf`: with neither condition the branch DECLINES, which is the composition rule.
	TestEqual(TEXT("102b80cf and with neither the selector declines"),
		N.SelectScheduleRangedCombat(0), 0);
	return true;
}

// =================================================================================================
// `0x102b7f40` — the dodge test, and the three arms the checklist's walk had as one.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10DodgeTestTest,
	"Elysium.Substrate.NpcKernelCombat10.DodgeTest", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10DodgeTestTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	N.Cognition.Conditions.Reset();

	// `102b7f46`: retail tests the weighted sequence `!= 0`, NOT `!= -1`. Family Facing's seam
	// answers `-1`, which is non-zero, so the gate PASSES and the body below is reachable at all.
	TestEqual(TEXT("SelectWeightedSequence's seam answers -1"),
		N.SelectWeightedSequenceForActivity(0x10), -1);

	// `102b7f96`: **CORRECTION** — the checklist's walk has only the roll arm. `COND_WEAPON_THROUGH_
	// WALL` is a THIRD arm and it answers true even when `COND_STOP_BACKUP` blocks the roll.
	N.Cognition.Conditions.Set(EElysiumNpcCond::StopBackup);
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestTrue(TEXT("102b7f96 COND 0x3c answers true past a standing COND_STOP_BACKUP"),
		N.ShouldDodgeRangedAttack());

	// `102b7f5f` / `102b7f85` / `102b7f96`: with `COND_STOP_BACKUP` up, the discipline gate false and
	// `COND 0x3c` clear, every arm declines.
	N.Cognition.Conditions.Clear(EElysiumNpcCond::WeaponThroughWall);
	TestFalse(TEXT("102b7fa6 and with all three refusing the answer is false"),
		N.ShouldDodgeRangedAttack());
	return true;
}

// =================================================================================================
// `0x10386560` — `CNPC_VHuman`, the shared human arm of slot 605.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10HumanRangedTest,
	"Elysium.Substrate.NpcKernelCombat10.HumanRanged", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10HumanRangedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;

	// The named dispatch case: `CNPC_VHuman` overrides slot 605 and a plain `npc_VCop` does not.
	TestNotNull(TEXT("CNPC_VHuman overrides slot 605"),
		ElysiumNpcKernelClass::OverrideOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_VHuman")), 605));
	TestNull(TEXT("a spawned npc_VCop resolves to no retail class, so it takes the Troika body"),
		ElysiumNpcKernelClass::OfClassname(TEXT("npc_VCop")));

	N.SetRetailClassForTests(TEXT("CNPC_VHuman"));
	N.Cognition.Conditions.Reset();

	// `10386566`: arm 1.
	N.bInMelee = true;
	TestEqual(TEXT("0x10386560 m_bInMelee answers 0xe3"), N.SelectScheduleRangedCombat(0), 0xe3);
	N.bInMelee = false;

	// `10386588`: arm 2 — the cover hint. Family Hints' store resolves nothing, so `+0x6444` stays
	// empty and the whole arm is SKIPPED, which is retail's own answer for a map with no hints.
	N.ScheduleHost.ShootAtHintNode = 0;
	// Retail's `+0x6444` is a pointer whose `0` means "no hint"; the port's word is an INDEX and
	// family Hints' search answers `INDEX_NONE`, which this arm folds onto `0` so a miss does not
	// read as a resolved hint.
	TestEqual(TEXT("10386588 the hint store resolves nothing"),
		N.FindShootAtHintNode(false), static_cast<int32>(INDEX_NONE));
	N.SelectScheduleRangedCombat(0);
	TestEqual(TEXT("...so +0x6444 stays empty and the cover arm is skipped"),
		N.ScheduleHost.ShootAtHintNode, 0);

	// `1038661d`: arm 4 answers **0xb8** on the human line, where `CNPC_VAsianVampire` answers 0xf0.
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestEqual(TEXT("1038661d COND 0x3c answers 0xb8 on the human line"),
		N.SelectScheduleRangedCombat(0), 0xb8);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::WeaponThroughWall);

	// `1038663d`: the three helpers in order, first non-zero wins. Unarmed, the pre-pass answers
	// 0x98 and nothing below it is reached.
	TestEqual(TEXT("1038663d the weapon pre-pass is offered FIRST of the three"),
		N.SelectScheduleRangedCombat(0), 0x98);

	// `10386675`: the split, with the discipline gate on the ENEMY rather than on this body — the
	// one difference from the Troika base's gate.
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}
	N.Cognition.Conditions.Set(EElysiumNpcCond::ExtendedBlockedByFriend);
	TestEqual(TEXT("10386675 COND 0x2e answers 0xbd"), N.SelectScheduleRangedCombat(0), 0xbd);
	N.Cognition.Conditions.Reset();

	// `103866f2`: the dodge/spacing branch. With COND `0x8` up and the pre-pass declining, the arm
	// that is reached is the `0x2f`/`0x63` split; with neither of those up, the dodge test decides,
	// and `COND_STOP_BACKUP` plus no other arm makes it refuse, so the answer is 0xf0.
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooCloseForRanged);
	N.Cognition.Conditions.Set(EElysiumNpcCond::StopBackup);
	TestEqual(TEXT("103866f2 a refused dodge with no melee weapon answers 0xf0"),
		N.SelectScheduleRangedCombat(0), 0xf0);

	// `10386720`: with `COND_WAITING_ATTACK_TIME` up the SAME pair answers 0xb9 instead of 0xf0.
	N.Cognition.Conditions.Set(EElysiumNpcCond::WaitingAttackTime);
	TestEqual(TEXT("10386720 COND 0x2f moves the tail to 0xb9"),
		N.SelectScheduleRangedCombat(0), 0xb9);
	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `0x103620d0` — `CNPC_VAsianVampire`'s slot 605.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10AsianVampireRangedTest,
	"Elysium.Substrate.NpcKernelCombat10.AsianVampireRanged", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10AsianVampireRangedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	TestNotNull(TEXT("CNPC_VAsianVampire overrides slot 605"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VAsianVampire")), 605));
	N.SetRetailClassForTests(TEXT("CNPC_VAsianVampire"));
	N.Cognition.Conditions.Reset();

	// `103621c2`: the ONE divergence from the Troika base — COND `0x3c` answers **0xf0**, not 0xb8.
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestEqual(TEXT("0x103620d0 COND 0x3c answers 0xf0 where the base answers 0xb8"),
		N.SelectScheduleRangedCombat(0), 0xf0);
	N.SetRetailClassForTests(nullptr);
	TestEqual(TEXT("...and the Troika body under the same condition answers 0xb8"),
		N.SelectScheduleRangedCombat(0), 0xb8);
	N.SetRetailClassForTests(TEXT("CNPC_VAsianVampire"));
	N.Cognition.Conditions.Reset();

	// `10362210`: COND `0x5f` with neither `0x2f` nor `0x63` answers 0xf0 — and this body offers
	// NEITHER the door helper NOR the combat-reaction prologue, so the pre-pass is the only helper
	// in front of it. Arm the body so the pre-pass declines.
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	TestEqual(TEXT("10362210 COND 0x5f with neither 0x2f nor 0x63 answers 0xf0"),
		N.SelectScheduleRangedCombat(0), 0xf0);

	// `10362253`: `COND_SEE_ENEMY` and not `0x48` and not `0x60` answers 0xf0 too — a DIFFERENT arm,
	// reached when the one above declines.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	TestEqual(TEXT("10362253 SEE_ENEMY unoccluded and in range answers 0xf0"),
		N.SelectScheduleRangedCombat(0), 0xf0);

	// `103622b1`: the tail — `COND_ENEMY_UNREACHABLE` takes `GetJumpSchedule` (a seam answering 0),
	// everything else 0xe8.
	N.Cognition.Conditions.Reset();
	TestEqual(TEXT("103622d2 the tail answers 0xe8"), N.SelectScheduleRangedCombat(0), 0xe8);
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("103622ce COND_ENEMY_UNREACHABLE takes GetJumpSchedule, a seam answering 0"),
		N.SelectScheduleRangedCombat(0), 0);
	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `0x103642f0` — `CNPC_VBach`'s slot 605, the one arm that CHAINS.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10BachRangedTest,
	"Elysium.Substrate.NpcKernelCombat10.BachRanged", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10BachRangedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	TestNotNull(TEXT("CNPC_VBach overrides slot 605"),
		ElysiumNpcKernelClass::OverrideOf(ElysiumNpcKernelClass::Find(TEXT("CNPC_VBach")), 605));
	N.SetRetailClassForTests(TEXT("CNPC_VBach"));
	N.Cognition.Conditions.Reset();

	// `103642f6`: COND `0x7b` — a Bach-line condition above the base registrar's `0x76`, carried by
	// NUMBER because no recovered table names it. It stamps `+0x6690` and answers 0x15a.
	N.BachRepositionTimer = 0.0;
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7b));
	TestEqual(TEXT("0x103642f0 COND 0x7b answers 0x15a"), N.SelectScheduleRangedCombat(0), 0x15a);
	// `curtime` is the fixture's clock, which stands at the NPC's first think: `NPCInit`
	// (`0x1029a0b0`) arms that think a tenth of a second after Activate rather than at Activate.
	TestEqual(TEXT("...and stamps +0x6690 with curtime + _DAT_10463584 (15.0)"),
		N.BachRepositionTimer, FElysiumNpcWorldFixture::FirstThinkSeconds + 15.0, 1e-4);
	N.Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x7b));

	// `10364355`: with NO weapon, COND `0x7a` answers 0x158 and COND `0x79` answers 0x159.
	TestNull(TEXT("the body carries no weapon"), N.ActiveWeaponEntity());
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7a));
	TestEqual(TEXT("10364355 with no weapon COND 0x7a answers 0x158"),
		N.SelectScheduleRangedCombat(0), 0x158);
	N.Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x7a));
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x79));
	TestEqual(TEXT("...and COND 0x79 answers 0x159"), N.SelectScheduleRangedCombat(0), 0x159);
	N.Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x79));

	// `1036439b`: WITH a weapon and COND `0x7a`, a classname that is not the katana answers 0x158;
	// the katana itself dispatches slot 604 and returns ITS answer.
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x7a));
	TestEqual(TEXT("1036439b a non-katana under COND 0x7a answers 0x158"),
		N.SelectScheduleRangedCombat(0), 0x158);
	N.Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(0x7a));

	// `103643f7`: COND `0x79` with a classname that is not Bach's rifle answers 0x159; the matching
	// rifle falls THROUGH to the human body.
	FElysiumItem* const Katana = F.Arm(GCombat10Katana);
	N.Cognition.Conditions.Set(static_cast<EElysiumNpcCond>(0x79));
	TestEqual(TEXT("103643f7 a non-rifle under COND 0x79 answers 0x159"),
		N.SelectScheduleRangedCombat(0), 0x159);
	if (Katana != nullptr && Rifle != nullptr)
	{
		N.Inventory.SetActiveWeapon(N, *Rifle);
		// `1036447a`: the human body declines (the rifle is loaded, so the pre-pass declines too),
		// and a 0 answer with `COND_SEE_ENEMY` up is REWRITTEN to 0x15f.
		N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
		N.ScheduleHost.ShootAtHintNode = 0;
		const int32 Answer = N.SelectScheduleRangedCombat(0);
		TestTrue(TEXT("1036447a the matching rifle chains into CNPC_VHuman's body"),
			Answer != 0x159);
	}
	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `0x103967d0` — `CNPC_VMingXiao`'s slot 605.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10MingXiaoRangedTest,
	"Elysium.Substrate.NpcKernelCombat10.MingXiaoRanged", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10MingXiaoRangedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	TestNotNull(TEXT("CNPC_VMingXiao overrides slot 605"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VMingXiao")), 605));
	N.SetRetailClassForTests(TEXT("CNPC_VMingXiao"));
	N.Cognition.Conditions.Reset();
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}

	// `103967d6`: arm 1 is the human's.
	N.bInMelee = true;
	TestEqual(TEXT("0x103967d0 m_bInMelee answers 0xe3"), N.SelectScheduleRangedCombat(0), 0xe3);
	N.bInMelee = false;

	// **There is NO `COND 0x3c` arm.** With the condition up and nothing else standing, this body
	// falls through to the slot-606 branch and DECLINES, where the human answers 0xb8.
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestEqual(TEXT("103967d0 drops the COND 0x3c arm entirely"),
		N.SelectScheduleRangedCombat(0), 0);
	N.SetRetailClassForTests(TEXT("CNPC_VHuman"));
	TestEqual(TEXT("...where the shared human arm answers 0xb8"),
		N.SelectScheduleRangedCombat(0), 0xb8);
	N.SetRetailClassForTests(TEXT("CNPC_VMingXiao"));
	N.Cognition.Conditions.Reset();

	// `103968c3`: **there is NO discipline gate** either, so the slot-606 branch is entered on the
	// two conditions alone and `COND_TOO_FAR_TO_ATTACK` answers 0xb1.
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	TestEqual(TEXT("103968c3 COND_TOO_FAR_TO_ATTACK answers 0xb1 with no discipline gate in front"),
		N.SelectScheduleRangedCombat(0), 0xb1);

	// `10396920`: the INLINED dodge — the first arm of `0x102b7f40` only, at the same `0x4b`. With
	// `COND_STOP_BACKUP` up it refuses, and the shared helper's `COND 0x3c` arm is NOT there to
	// rescue it, so the tail answers 0xf0.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	N.Cognition.Conditions.Set(EElysiumNpcCond::StopBackup);
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestEqual(TEXT("10396920 the inlined dodge has only the roll arm, so COND 0x3c does not save it"),
		N.SelectScheduleRangedCombat(0), 0xf0);
	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// `0x103afdb0` — `CNPC_VSheriffMan`'s slot 605.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10SheriffManRangedTest,
	"Elysium.Substrate.NpcKernelCombat10.SheriffManRanged", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10SheriffManRangedTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	TestNotNull(TEXT("CNPC_VSheriffMan overrides slot 605"),
		ElysiumNpcKernelClass::OverrideOf(
			ElysiumNpcKernelClass::Find(TEXT("CNPC_VSheriffMan")), 605));
	N.SetRetailClassForTests(TEXT("CNPC_VSheriffMan"));
	N.Cognition.Conditions.Reset();
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}

	// `103afea2`: arm 3 answers 0xb8, as the Troika base does.
	N.Cognition.Conditions.Set(EElysiumNpcCond::WeaponThroughWall);
	TestEqual(TEXT("0x103afdb0 COND 0x3c answers 0xb8"), N.SelectScheduleRangedCombat(0), 0xb8);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::WeaponThroughWall);

	// `103aff56`: this body has NO slot-606 arm at all. With neither COND `0x5f` nor COND `0x8`,
	// `COND_SEE_ENEMY` unoccluded and in range DECLINES outright.
	N.Cognition.Conditions.Set(EElysiumNpcCond::SeeEnemy);
	TestEqual(TEXT("103aff56 SEE_ENEMY unoccluded and in range declines"),
		N.SelectScheduleRangedCombat(0), 0);

	// `103affa6`: otherwise `COND_ENEMY_UNREACHABLE` answers **0x15a** — the sheriff's own number,
	// which no other slot-605 body names — and everything else 0xe8.
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	N.Cognition.Conditions.Set(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("103affa6 COND_ENEMY_UNREACHABLE answers 0x15a"),
		N.SelectScheduleRangedCombat(0), 0x15a);
	N.Cognition.Conditions.Clear(EElysiumNpcCond::EnemyUnreachable);
	TestEqual(TEXT("103affbe and everything else 0xe8"), N.SelectScheduleRangedCombat(0), 0xe8);

	// `103b0026`: the dodge/spacing branch, with the inlined dodge refused by `COND_STOP_BACKUP`.
	N.Cognition.Conditions.Reset();
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooCloseToAttack);
	N.Cognition.Conditions.Set(EElysiumNpcCond::StopBackup);
	TestEqual(TEXT("103b0026 a refused dodge with no melee weapon answers 0xf0"),
		N.SelectScheduleRangedCombat(0), 0xf0);
	N.SetRetailClassForTests(nullptr);
	return true;
}

// =================================================================================================
// The pre-kernel selector and the slot body now carry ONE answer.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelCombat10SelectorAgreementTest,
	"Elysium.Substrate.NpcKernelCombat10.SelectorAgreement", GElysiumNpcKernelCombat10Flags)
bool FElysiumNpcKernelCombat10SelectorAgreementTest::RunTest(const FString&)
{
	FCombat10Fixture F;
	if (!TestNotNull(TEXT("the fighter leaf constructs"), F.Fighter))
	{
		return false;
	}
	FElysiumNpc& N = *F.Fighter;
	N.SetRetailClassForTests(nullptr);
	N.Cognition.Conditions.Reset();
	FElysiumItem* const Rifle = F.Arm(GCombat10BachRifle);
	if (Rifle != nullptr)
	{
		Rifle->MagazineCount = 3;
	}

	// `0xb1` is `SCHED_TROIKA_CHASE_ENEMY`, and the slot body's answer IS the pre-kernel selector's
	// answer: the number is handed straight through.
	TestEqual(TEXT("retail 0xb1 is SCHED_TROIKA_CHASE_ENEMY"),
		0xb1, ElysiumSched::SCHED_TROIKA_CHASE_ENEMY);
	N.Cognition.Conditions.Set(EElysiumNpcCond::TooFarToAttack);
	TestEqual(TEXT("slot 605 answers 0xb1"), N.SelectScheduleRangedCombat(0), 0xb1);
	TestEqual(TEXT("...and the pre-kernel selector answers the SAME program"),
		ElysiumNpcCombat::SelectRangedSchedule(N, 0.0), ElysiumSched::SCHED_TROIKA_CHASE_ENEMY);

	// `0x98` used to name no registered program, and the fold that discovered that is gone with the
	// 29 typed identities it folded onto. Every number the slot bodies answer is a loaded program
	// now -- `0x98` included -- which is what retired `KernelAnswerFirst`.
	TestTrue(TEXT("retail 0x98 is a loaded program"),
		ElysiumScheduleFor(ElysiumScheduleGlobalId(0x98)) != nullptr);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
