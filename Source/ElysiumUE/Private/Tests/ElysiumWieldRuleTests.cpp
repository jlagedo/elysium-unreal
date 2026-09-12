// Content-free Substrate automation: who may WIELD what.
//
// `CBaseCombatCharacter::Inventory_Can_Wield` (`vampire.dll` 0x10335a70) joins two authored halves —
// the item record's `equip_mask` and the `ExcludedEquipTables` row the character's
// `Excluded_Equipment` stat (sheet slot 31) names — and `Inventory_Wield_Update` (0x10335b80) is
// what enforces the verdict on the hand. The recovery is
// `docs/vtmb/wielded_weapons.md` § "Who may wield what: ExcludedEquipTables and equip_mask".
//
// Both tables here are built in code: the flag values, the row set and the item records below are
// this suite's statement of the retail contract, not a reading of the export.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/ScopeExit.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLoadout.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumWieldRules.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumWieldRuleTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The suite's catalogue. `item_w_claws` and `item_w_unarmed` are spelled EXACTLY: the first is
	// the record whose shipped `equip_mask` is the whole point, and the second is named as a
	// classname by `Inventory_Wield_Update`'s last arm.
	const TCHAR* const GClaws   = TEXT("item_w_claws");
	const TCHAR* const GUnarmed = TEXT("item_w_unarmed");
	const TCHAR* const GKatana  = TEXT("item_w_wield_katana");
	const TCHAR* const GPistol  = TEXT("item_w_wield_pistol");

	FElysiumItemDef MakeWeapon(const TCHAR* Classname, const TCHAR* EquipMask, int32 Weight)
	{
		FElysiumItemDef Def;
		Def.Classname = Classname;
		Def.PrintName = Classname;
		Def.Type = EElysiumItemType::WeaponMelee;
		Def.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Def.Weight = Weight;
		Def.EquipMask = ElysiumEquipFlags::Parse(EquipMask);
		return Def;
	}

	FElysiumItemTable MakeWieldItemTable()
	{
		FElysiumItemTable Table;
		// The shipped `item_w_claws.txt` authors exactly `"equip_mask" "ClawedForm"`.
		Table.Items.Add(MakeWeapon(GClaws, TEXT("ClawedForm"), /*weight*/ 5));
		// A `Normal` melee weapon — the commonest shipped authoring (19 of the 226 files).
		Table.Items.Add(MakeWeapon(GKatana, TEXT("Normal"), /*weight*/ 20));
		// Heavier than the katana, but it carries a magazine and spawns empty, so `CanDeploy`
		// refuses it and the picker has to skip it rather than take the top weight.
		FElysiumItemDef Pistol = MakeWeapon(GPistol, TEXT("Normal"), /*weight*/ 30);
		Pistol.Type = EElysiumItemType::WeaponFirearm;
		Pistol.AmmoType = TEXT("WieldTestRound");
		Pistol.MagazineSize = 6;
		Pistol.DefaultAmmo = 0;
		Table.Items.Add(MoveTemp(Pistol));
		// The authored "holding nothing" record: the shipped `item_w_unarmed.txt` is
		// `item_type "hidden"` with no `Activation` block and no `equip_mask`. Its TYPE is what
		// keeps it out of the gamerules picker — it is not one of the three wielded families — so
		// the sweep can only reach it through its own last arm, which is what retail names it in.
		FElysiumItemDef Unarmed;
		Unarmed.Classname = GUnarmed;
		Unarmed.PrintName = GUnarmed;
		Unarmed.Type = EElysiumItemType::Hidden;
		Unarmed.bHidden = true;
		Unarmed.PlayerModel = TEXT("models/weapons/w_null.mdl");
		Table.Items.Add(MoveTemp(Unarmed));

		Table.Reindex();
		return Table;
	}

	// The shipped rows this suite needs, in the block's own order: `Default` is row 0 because it is
	// authored first, which is what makes an unwritten slot 31 mean `Default`.
	FElysiumExcludedEquipTable MakeExcludedEquipTable()
	{
		FElysiumExcludedEquipTable Table;

		FElysiumExcludedEquipRow Default;
		Default.InternalName = TEXT("Default");
		Default.Excluded = ElysiumEquipFlags::ClawedForm | ElysiumEquipFlags::WolfForm;
		Table.Rows.Add(MoveTemp(Default));

		FElysiumExcludedEquipRow Toreador;
		Toreador.InternalName = TEXT("Toreador");
		Toreador.Excluded = ElysiumEquipFlags::NoBlueBlood;
		Table.Rows.Add(MoveTemp(Toreador));

		FElysiumExcludedEquipRow Clawed;
		Clawed.InternalName = TEXT("Clawed_Form");
		Clawed.Required = ElysiumEquipFlags::ClawedForm;
		Clawed.Excluded = ElysiumEquipFlags::WolfForm;
		Table.Rows.Add(MoveTemp(Clawed));

		Table.Reindex();
		return Table;
	}

	// A world with a player and nothing else. The wield rule is character state, so one character
	// and its inventory is the whole fixture.
	struct FWieldFixture
	{
		FElysiumRecordingServices Services;
		FElysiumItemTable Items;
		FElysiumExcludedEquipTable Rules;
		FElysiumEntityWorld World;
		FElysiumPlayer* Player = nullptr;

		FWieldFixture()
			: Items(MakeWieldItemTable())
			, Rules(MakeExcludedEquipTable())
			, World(nullptr, nullptr, Services.Bundle())
		{
			ElysiumItems::Install(Items);
			// No GameInstance behind this world, so the wield table arrives through the headless
			// binding — the seam `Substrate/ElysiumSheetMath.h` documents.
			ElysiumSheetRules::FBoundTables Bound;
			Bound.ExcludedEquip = &Rules;
			ElysiumSheetRules::BindTables(Bound);

			Services.bHasPlayer = true;
			Services.ItemGroundModelStates.Add(TEXT("models/weapons/w_null.mdl"),
				EElysiumItemGroundModelState::Geometryless);

			FElysiumEntityDefs Defs;
			Defs.MapName = TEXT("__wield_test__");
			World.Load(MoveTemp(Defs));
			World.SpawnPlayer();
			World.Activate(0.0);
			World.Tick(0.0);
			Player = World.FindPlayer();
		}

		~FWieldFixture()
		{
			ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables());
			ElysiumItems::Uninstall(Items);
		}

		FWieldFixture(const FWieldFixture&) = delete;
		FWieldFixture& operator=(const FWieldFixture&) = delete;

		FElysiumItem* Give(const TCHAR* Classname)
		{
			const FElysiumEntityHandle Handle = Player->Inventory.GiveNamedItem(*Player, Classname);
			FElysiumEntity* Entity = World.Resolve(Handle);
			return Entity ? Entity->AsItem() : nullptr;
		}

		// The character's `Excluded_Equipment` row, by name.
		void SetRow(const TCHAR* RowName)
		{
			Player->Sheet.SetBase(EElysiumTraitContainer::Attributes,
				ElysiumSlot::ExcludedEquipment, Rules.RowIndexByName(RowName));
			Player->RecomputeSheet();
		}

		FString ActiveName() const
		{
			const FElysiumItem* Active = Player->Inventory.Active(*Player);
			return Active ? Active->ClassName() : FString(TEXT("(none)"));
		}
	};
}


// `ParseEquipFlag` (0x1025b740) — the name table, the `normal` composite and the accumulator rule.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldEquipMaskTest,
	"Elysium.Substrate.Wield.EquipMask", GElysiumTestFlags)
bool FElysiumWieldEquipMaskTest::RunTest(const FString&)
{
	// The bit assignment IS the string table's order (0x105c76f8..0x105c775c).
	TestEqual(TEXT("`never` is bit 0"), (int32)ElysiumEquipFlags::Bit(TEXT("never")), 1);
	TestEqual(TEXT("`blueblood` is bit 1"), (int32)ElysiumEquipFlags::Bit(TEXT("blueblood")), 2);
	TestEqual(TEXT("`no_blueblood` is bit 2"), (int32)ElysiumEquipFlags::Bit(TEXT("No_BlueBlood")), 4);
	TestEqual(TEXT("`wolfform` is bit 3"), (int32)ElysiumEquipFlags::Bit(TEXT("WolfForm")), 8);
	TestEqual(TEXT("`clawedform` is bit 5"), (int32)ElysiumEquipFlags::Bit(TEXT("ClawedForm")), 32);
	TestEqual(TEXT("`no_npc` is bit 7"), (int32)ElysiumEquipFlags::Bit(TEXT("no_npc")), 128);
	TestEqual(TEXT("an unknown name contributes nothing"),
		(int32)ElysiumEquipFlags::Bit(TEXT("not_a_flag")), 0);

	// `normal` is not in the name table at all: the function answers the composite 0x50.
	TestEqual(TEXT("`Normal` is no_wolfform|no_clawedform"),
		(int32)ElysiumEquipFlags::Parse(TEXT("Normal")), 0x50);
	TestEqual(TEXT("...case-insensitively, as strcmpi is"),
		(int32)ElysiumEquipFlags::Parse(TEXT("nOrMaL")), 0x50);
	TestEqual(TEXT("`ClawedForm` is the single bit 5"),
		(int32)ElysiumEquipFlags::Parse(TEXT("ClawedForm")), 32);
	TestEqual(TEXT("`Never` is the single bit 0"), (int32)ElysiumEquipFlags::Parse(TEXT("Never")), 1);
	TestEqual(TEXT("an absent key is mask 0"), (int32)ElysiumEquipFlags::Parse(FString()), 0);

	// The key is a SET, one `ParseEquipFlag` call per whitespace token.
	TestEqual(TEXT("`Normal No_Npc` accumulates both"),
		(int32)ElysiumEquipFlags::Parse(TEXT("Normal No_Npc")), 0xd0);

	// The accumulator's own rule: a value that is neither 0 nor 1 CLEARS bit 0 first, so `never`
	// beside any other flag stops being `never`.
	{
		const uint32 Mask = ElysiumEquipFlags::Parse(TEXT("Never Normal"));
		TestEqual(TEXT("a flag after `Never` clears the never bit"), (int32)Mask, 0x50);
		TestEqual(TEXT("...and the order does not matter"),
			(int32)ElysiumEquipFlags::Parse(TEXT("Normal Never")), 0x51);
	}
	TestEqual(TEXT("an unknown token leaves the accumulator alone"),
		(int32)ElysiumEquipFlags::Parse(TEXT("ClawedForm not_a_flag")), 32);

	// The parse belongs to the record: `equip_mask` is read by the item loader through the same
	// helper (parser 0x10259f80, stored at +0x5eb1c).
	{
		const FString Text = TEXT("WeaponData { \"printname\" \"Claws\" \"item_type\" \"weapon_melee\" ")
			TEXT("\"equip_mask\" \"ClawedForm\" }");
		FElysiumItemDef Def;
		FString Error;
		if (TestTrue(TEXT("a record with an equip_mask parses"),
			FElysiumItemTable::ParseText(TEXT("item_w_claws"), Text, Def, Error)))
		{
			TestEqual(TEXT("...and the record carries the parsed bits"),
				(int32)Def.EquipMask, (int32)ElysiumEquipFlags::ClawedForm);
		}
		FElysiumItemDef Plain;
		if (TestTrue(TEXT("a record with no equip_mask parses"), FElysiumItemTable::ParseText(
			TEXT("item_w_plain"), TEXT("WeaponData { \"item_type\" \"weapon_melee\" }"),
			Plain, Error)))
		{
			TestEqual(TEXT("...and holds mask 0"), Plain.EquipMask, 0);
		}
	}
	return true;
}


// The rows and the evaluator (0x10220460).


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldRowTest,
	"Elysium.Substrate.Wield.Rows", GElysiumTestFlags)
bool FElysiumWieldRowTest::RunTest(const FString&)
{
	const FElysiumExcludedEquipTable Table = MakeExcludedEquipTable();

	// Block order IS the row id, and it is what a template's authored name resolves to.
	TestEqual(TEXT("`Default` is row 0"), Table.RowIndexByName(TEXT("Default")), 0);
	TestEqual(TEXT("...case-insensitively"), Table.RowIndexByName(TEXT("default")), 0);
	TestEqual(TEXT("`Clawed_Form` is row 2"), Table.RowIndexByName(TEXT("Clawed_Form")), 2);
	TestEqual(TEXT("a name the block does not carry resolves to nothing"),
		Table.RowIndexByName(TEXT("Not_A_Row")), (int32)INDEX_NONE);

	const uint32 Claws = ElysiumEquipFlags::Parse(TEXT("ClawedForm"));
	const uint32 Normal = ElysiumEquipFlags::Parse(TEXT("Normal"));
	const uint32 Never = ElysiumEquipFlags::Parse(TEXT("Never"));

	// Jack's row. This is the consequence the recovery is stated by: `Default` excludes
	// `ClawedForm`, so a `Default` character can never wield `item_w_claws`.
	TestFalse(TEXT("Default may not wield the claws"), Table.CanWield(0, Claws));
	TestTrue(TEXT("Default may wield a Normal weapon"), Table.CanWield(0, Normal));

	// The Protean row is the mirror image: it REQUIRES the claw bit, so every ordinary weapon fails.
	TestTrue(TEXT("Clawed_Form may wield the claws"), Table.CanWield(2, Claws));
	TestFalse(TEXT("Clawed_Form may not wield a Normal weapon"), Table.CanWield(2, Normal));

	// Arm 1 is row-independent.
	TestFalse(TEXT("`never` refuses under Default"), Table.CanWield(0, Never));
	TestFalse(TEXT("`never` refuses under Clawed_Form"), Table.CanWield(2, Never));

	// A mask of 0 passes every arm: an excluded flag it does not carry, and a required flag is only
	// tested when the row states one.
	TestTrue(TEXT("an unauthored mask is wieldable under Default"), Table.CanWield(0, 0));
	TestFalse(TEXT("...but not under a row that requires a flag"), Table.CanWield(2, 0));

	// A row id the table does not carry is unrestricted, which is what keeps a headless run armed.
	TestTrue(TEXT("an unknown row restricts nothing"), Table.CanWield(INDEX_NONE, Normal));
	TestFalse(TEXT("...except `never`, which is not the row's business"),
		Table.CanWield(INDEX_NONE, Never));

	// The parse and the row load agree on the shipped `Toreador` row's one flag.
	if (const FElysiumExcludedEquipRow* Toreador = Table.Find(TEXT("Toreador")))
	{
		TestEqual(TEXT("Toreador excludes No_BlueBlood"),
			(int32)Toreador->Excluded, (int32)ElysiumEquipFlags::NoBlueBlood);
		TestEqual(TEXT("...and requires nothing"), Toreador->Required, 0);
		TestEqual(TEXT("...at row 1"), Toreador->Index, 1);
	}
	else
	{
		AddError(TEXT("the hand-built table lost its Toreador row"));
	}
	return true;
}


// `Inventory_Wield_Update` (0x10335b80) on a live character.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldUpdateTest,
	"Elysium.Substrate.Wield.Update", GElysiumTestFlags)
bool FElysiumWieldUpdateTest::RunTest(const FString&)
{
	FWieldFixture F;
	if (!TestNotNull(TEXT("the player exists"), F.Player))
	{
		return false;
	}
	FElysiumItem* Claws = F.Give(GClaws);
	FElysiumItem* Katana = F.Give(GKatana);
	FElysiumItem* Pistol = F.Give(GPistol);
	if (!TestNotNull(TEXT("the claws are carried"), Claws)
		|| !TestNotNull(TEXT("the katana is carried"), Katana)
		|| !TestNotNull(TEXT("the pistol is carried"), Pistol))
	{
		return false;
	}

	// --- The rule itself, read through the character ------------------------------------------
	// Slot 31 is 0 with nothing written on it, and row 0 is `Default`.
	TestEqual(TEXT("an unwritten slot 31 is row 0"),
		F.Player->Sheet.GetCurrent(EElysiumTraitContainer::Attributes,
			ElysiumSlot::ExcludedEquipment), 0);
	TestFalse(TEXT("a Default character may not wield the claws"),
		F.Player->Inventory.CanWield(*F.Player, *Claws));
	TestTrue(TEXT("...but may wield a Normal weapon"),
		F.Player->Inventory.CanWield(*F.Player, *Katana));

	// --- The sweep drops what may not be held and takes the next best --------------------------
	// Nothing has been held yet, so `m_hLastWeapon` is empty and the sweep goes straight to the
	// gamerules picker.
	F.Player->Inventory.SetActiveWeapon(*F.Player, *Claws);
	TestEqual(TEXT("the ungated switch still arms the claws"), F.ActiveName(), FString(GClaws));
	F.Player->Inventory.WieldUpdate(*F.Player);
	// The pistol outweighs the katana (30 vs 20) but spawned with an empty magazine and no reserve,
	// so `CanDeploy` refuses it — the picker's second condition, not an ordering accident.
	TestEqual(TEXT("the wield update drops the claws for the best deployable weapon"),
		F.ActiveName(), FString(GKatana));

	// --- The last-weapon arm comes FIRST ------------------------------------------------------
	// The katana is now `m_hLastWeapon`, and the sweep returns to it without ever asking the
	// picker — even though the pistol is about to become the better answer.
	F.Player->Inventory.AddReserve(TEXT("WieldTestRound"), 12);
	F.Player->Inventory.SetActiveWeapon(*F.Player, *Claws);
	F.Player->Inventory.WieldUpdate(*F.Player);
	TestEqual(TEXT("the sweep prefers the last weapon over the picker"),
		F.ActiveName(), FString(GKatana));

	// With the katana gone the last weapon resolves to nothing and the picker speaks again — and
	// now the loaded pistol outweighs everything, which is what proves the ammunition arm is live.
	F.Player->Inventory.ScriptRemove(*F.Player, GKatana);
	F.Player->Inventory.SetActiveWeapon(*F.Player, *Claws);
	F.Player->Inventory.WieldUpdate(*F.Player);
	TestEqual(TEXT("a heavier weapon wins once it has ammunition to deploy with"),
		F.ActiveName(), FString(GPistol));

	// --- The Protean row inverts every answer --------------------------------------------------
	F.SetRow(TEXT("Clawed_Form"));
	TestTrue(TEXT("a Clawed_Form character may wield the claws"),
		F.Player->Inventory.CanWield(*F.Player, *Claws));
	TestFalse(TEXT("...and may no longer wield a Normal weapon"),
		F.Player->Inventory.CanWield(*F.Player, *Pistol));
	F.Player->Inventory.WieldUpdate(*F.Player);
	TestEqual(TEXT("the sweep takes the Normal weapon away and arms the claws"),
		F.ActiveName(), FString(GClaws));

	// --- The `item_w_unarmed` arm, and then nothing at all -------------------------------------
	F.SetRow(TEXT("Default"));
	// Take the last ordinary weapon away so the picker finds nothing wieldable at all.
	F.Player->Inventory.ScriptRemove(*F.Player, GPistol);
	FElysiumItem* Unarmed = F.Give(GUnarmed);
	if (TestNotNull(TEXT("the unarmed record is carried"), Unarmed))
	{
		F.Player->Inventory.SetActiveWeapon(*F.Player, *Claws);
		F.Player->Inventory.WieldUpdate(*F.Player);
		TestEqual(TEXT("with nothing else wieldable the hand falls to `item_w_unarmed`"),
			F.ActiveName(), FString(GUnarmed));
	}

	F.Player->Inventory.ScriptRemove(*F.Player, GUnarmed);
	F.Player->Inventory.SetActiveWeapon(*F.Player, *Claws);
	F.Player->Inventory.WieldUpdate(*F.Player);
	TestEqual(TEXT("with no fallback at all the character holds nothing"),
		F.ActiveName(), FString(TEXT("(none)")));
	TestTrue(TEXT("...and the refused weapon is still CARRIED, not destroyed"),
		F.Player->Inventory.Has(*F.Player, GClaws));
	return true;
}


// The NPC spawn path: the grant stays ungated and the sweep is what disarms.


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldNpcLoadoutTest,
	"Elysium.Substrate.Wield.NpcLoadout", GElysiumTestFlags)
bool FElysiumWieldNpcLoadoutTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.bProvideNpcMotor = true;
	Services.ItemGroundModelStates.Add(TEXT("models/weapons/w_null.mdl"),
		EElysiumItemGroundModelState::Geometryless);

	const FElysiumItemTable Items = MakeWieldItemTable();
	const FElysiumExcludedEquipTable Rules = MakeExcludedEquipTable();
	ElysiumItems::Install(Items);
	ElysiumSheetRules::FBoundTables Bound;
	Bound.ExcludedEquip = &Rules;
	ElysiumSheetRules::BindTables(Bound);
	ON_SCOPE_EXIT
	{
		ElysiumSheetRules::BindTables(ElysiumSheetRules::FBoundTables());
		ElysiumItems::Uninstall(Items);
	};

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__wield_npc_test__");
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VHumanCombatant");
		Npc.TargetName = TEXT("clawed_guard");
		Npc.Keys.Add(TEXT("model"),
			TEXT("models/character/npc/common/blueblood/male/Blueblood_Male.mdl"));
		// Jack's own authoring: the loadout hands him a weapon his `Excluded_Equipment` row forbids.
		Npc.Keys.Add(TEXT("additionalequipment"), GClaws);
		Defs.Defs.Add(MoveTemp(Npc));
		World.Load(MoveTemp(Defs));
	}
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* Entity = World.FindByName(TEXT("clawed_guard"));
	FElysiumNpc* Guard = Entity ? Entity->AsNpc() : nullptr;
	if (!TestNotNull(TEXT("the guard spawned"), Guard))
	{
		return false;
	}
	TestEqual(TEXT("the keyfield parsed"), Guard->AdditionalEquipment, FString(GClaws));
	// Nothing wrote slot 31, and row 0 is `Default` — which is what the shipped templates author.
	TestEqual(TEXT("the guard is on the Default row"),
		Guard->Sheet.GetCurrent(EElysiumTraitContainer::Attributes,
			ElysiumSlot::ExcludedEquipment), 0);

	// Two ordinary thinks: the first crosses the admission barrier, the second resolves the
	// loadout — the real production path, as `Elysium.Substrate.NpcCombat.Loadout` drives it.
	for (int32 i = 0; i < 2; ++i)
	{
		FElysiumNpcWorldFixture::Wake({ Guard }, 0.0);
		World.Tick(0.0);
	}

	TestTrue(TEXT("the loadout ran"), Guard->bLoadoutResolved);
	TestTrue(TEXT("the authored weapon is CARRIED — the spawn equip stays ungated, as retail's is"),
		Guard->Inventory.Has(*Guard, GClaws));
	TestNull(TEXT("...but the wield update took it out of the hand"),
		Guard->Inventory.Active(*Guard));
	return true;
}

}   // namespace ElysiumWieldRuleTests

#endif   // WITH_DEV_AUTOMATION_TESTS
