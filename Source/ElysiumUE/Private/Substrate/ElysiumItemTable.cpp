#include "Substrate/ElysiumItemTable.h"

#include "Substrate/ElysiumWieldRules.h"

#include "ElysiumCameraSolve.h"
#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumVdataLoad.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::Index;
}

namespace
{
	// In `EElysiumItemType` order, which is `system/items.txt`'s own `ItemTypes` order.
	const TCHAR* const GItemTypeNames[] = {
		TEXT("Weapon_Melee"), TEXT("Weapon_Firearm"), TEXT("Weapon_Thrown"), TEXT("Ammo"),
		TEXT("Armor"), TEXT("Money"), TEXT("Jewelry"), TEXT("Generic"), TEXT("Powerup"),
		TEXT("Bloodpack"), TEXT("Hidden"),
	};
	static_assert(UE_ARRAY_COUNT(GItemTypeNames) == (int32)EElysiumItemType::Count,
		"the item-type mirror must match system/items.txt's ItemTypes block");

	// The same block's own `InventorySection` / `IsWielded` / `IsWorn` columns, one row per type in
	// the same order. Four types file under a section that is not their own name — `Ammo` under the
	// undisplayed `None`, and `Money`, `Jewelry` and `Bloodpack` under `Generic` — so the join is
	// read from here rather than inferred from the type's spelling.
	struct FItemTypeRow
	{
		EElysiumInvSection Section;
		bool bWielded;
		bool bWorn;
	};
	const FItemTypeRow GItemTypeRows[] = {
		/* Weapon_Melee   */ { EElysiumInvSection::WeaponMelee,  true,  false },
		/* Weapon_Firearm */ { EElysiumInvSection::WeaponRanged, true,  false },
		/* Weapon_Thrown  */ { EElysiumInvSection::WeaponThrown, true,  false },
		/* Ammo           */ { EElysiumInvSection::None,         false, false },
		/* Armor          */ { EElysiumInvSection::Armor,        false, true  },
		/* Money          */ { EElysiumInvSection::Generic,      false, false },
		/* Jewelry        */ { EElysiumInvSection::Generic,      false, true  },
		/* Generic        */ { EElysiumInvSection::Generic,      false, false },
		/* Powerup        */ { EElysiumInvSection::Powerups,     false, false },
		/* Bloodpack      */ { EElysiumInvSection::Generic,      false, false },
		/* Hidden         */ { EElysiumInvSection::Hidden,       true,  false },
	};
	static_assert(UE_ARRAY_COUNT(GItemTypeRows) == (int32)EElysiumItemType::Count,
		"the item-type section mirror must match system/items.txt's ItemTypes block");
}

EElysiumInvSection ElysiumSectionForItemType(EElysiumItemType Type)
{
	const int32 Index = (int32)Type;
	return (Index >= 0 && Index < (int32)EElysiumItemType::Count)
		? GItemTypeRows[Index].Section : EElysiumInvSection::None;
}

bool ElysiumItemTypeIsWielded(EElysiumItemType Type)
{
	const int32 Index = (int32)Type;
	return Index >= 0 && Index < (int32)EElysiumItemType::Count && GItemTypeRows[Index].bWielded;
}

bool ElysiumItemTypeIsWorn(EElysiumItemType Type)
{
	const int32 Index = (int32)Type;
	return Index >= 0 && Index < (int32)EElysiumItemType::Count && GItemTypeRows[Index].bWorn;
}


const TCHAR* ElysiumItemTypeName(EElysiumItemType Type)
{
	const int32 Index = (int32)Type;
	return (Index >= 0 && Index < (int32)EElysiumItemType::Count) ? GItemTypeNames[Index] : TEXT("?");
}

bool ElysiumParseItemType(const FString& Raw, EElysiumItemType& OutType, bool& OutHidden)
{
	OutHidden = false;
	TArray<FString> Tokens;
	Raw.ParseIntoArrayWS(Tokens);

	bool bFound = false;
	for (const FString& Token : Tokens)
	{
		bool bMatched = false;
		for (int32 i = 0; i < (int32)EElysiumItemType::Count; ++i)
		{
			if (!Token.Equals(GItemTypeNames[i], ESearchCase::IgnoreCase))
			{
				continue;
			}
			bMatched = true;
			if (!bFound)
			{
				OutType = (EElysiumItemType)i;
				bFound = true;
			}
			else if ((EElysiumItemType)i == EElysiumItemType::Hidden)
			{
				// A trailing `hidden` beside a real type is the file's own visibility flag —
				// `"weapon_firearm hidden"`, and `"hidden hidden"` for an item that is both.
				OutHidden = true;
			}
			break;
		}
		if (!bMatched)
		{
			// An unrecognised token is not an error: the value is authored free-form and the
			// caller keeps whatever type it already resolved.
			continue;
		}
	}
	return bFound;
}

bool FElysiumItemDef::IsWeaponType() const
{
	// `system/items.txt`'s `IsWeapon` column: the three wielded weapon families, Bloodpack (which
	// the file marks a weapon because feeding is an attack), and Hidden (the intrinsic attacks).
	switch (Type)
	{
	case EElysiumItemType::WeaponMelee:
	case EElysiumItemType::WeaponFirearm:
	case EElysiumItemType::WeaponThrown:
	case EElysiumItemType::Bloodpack:
	case EElysiumItemType::Hidden:
		return true;
	default:
		return false;
	}
}

const TCHAR* ElysiumWeaponModeTypeName(EElysiumWeaponModeType Type)
{
	switch (Type)
	{
	case EElysiumWeaponModeType::Attack:            return TEXT("Attack");
	case EElysiumWeaponModeType::SecondaryAttack:   return TEXT("Secondary_Attack");
	case EElysiumWeaponModeType::TogglePrimaryMode: return TEXT("Toggle_Primary_Mode");
	case EElysiumWeaponModeType::ZoomLoop:          return TEXT("Zoom_Out_Loop");
	case EElysiumWeaponModeType::Other:             return TEXT("Other");
	default:                                        return TEXT("None");
	}
}

const FElysiumWeaponMode* FElysiumItemDef::FindMode(const TCHAR* Tag) const
{
	for (const FElysiumWeaponMode& Mode : Modes)
	{
		if (Mode.Tag.Equals(Tag, ESearchCase::IgnoreCase))
		{
			return &Mode;
		}
	}
	return nullptr;
}

namespace
{
	EElysiumWeaponModeType ParseWeaponModeType(const FString& Raw)
	{
		if (Raw.IsEmpty())                                              { return EElysiumWeaponModeType::None; }
		if (Raw.Equals(TEXT("Attack"), ESearchCase::IgnoreCase))         { return EElysiumWeaponModeType::Attack; }
		if (Raw.Equals(TEXT("Secondary_Attack"), ESearchCase::IgnoreCase)) { return EElysiumWeaponModeType::SecondaryAttack; }
		if (Raw.Equals(TEXT("Toggle_Primary_Mode"), ESearchCase::IgnoreCase)) { return EElysiumWeaponModeType::TogglePrimaryMode; }
		if (Raw.Equals(TEXT("Zoom_Out_Loop"), ESearchCase::IgnoreCase))  { return EElysiumWeaponModeType::ZoomLoop; }
		// A consumable/throw-style mode names its own projectile record (`FragGrenade`,
		// `CrossbowBolt`). It is authored data, not a parse failure.
		return EElysiumWeaponModeType::Other;
	}

	void ParseWeaponMode(const ElysiumKeyValues::FKvNode& Block, FElysiumWeaponMode& Out)
	{
		Out.Tag = Block.Str(TEXT("Tag"), FString());
		Out.TypeName = Block.Str(TEXT("Type"), FString());
		Out.Type = ParseWeaponModeType(Out.TypeName);

		Out.Dmg = Block.Str(TEXT("Dmg"), FString());
		Out.BaseLethality = Block.Int(TEXT("BaseLethality"), 0);
		Out.SkillRequirement = Block.Int(TEXT("SkillRequirement"), 0);

		Out.AttackRate = Block.Flt(TEXT("Attack_Rate"), 0.0f);

		Out.AmmoType = Block.Str(TEXT("Ammo_Type"), FString());
		Out.AmmoCost = Block.Int(TEXT("Ammo_Cost"), 0);
		// An unauthored `Ammo_Fired` is one ray, not zero: every attack mode that omits it fires a
		// single trace, and the M37's eight is why the key exists at all.
		Out.AmmoFired = Block.Int(TEXT("Ammo_Fired"), 1);

		Out.bAllowAutofire = Block.Bool(TEXT("allow_autofire"), false);

		Out.BurstMin = Block.Int(TEXT("BurstMin"), 0);
		Out.BurstMax = Block.Int(TEXT("BurstMax"), 0);
		// The loader's own clamp — retail forces `BurstMin <= BurstMax`.
		Out.BurstMax = FMath::Max(Out.BurstMin, Out.BurstMax);

		Out.Range = Block.Flt(TEXT("Range"), 0.0f);
		Out.BotchTable = Block.Str(TEXT("Botch_Table"), FString());
	}
}

bool FElysiumItemTable::ParseText(const FString& Classname, const FString& Text,
	FElysiumItemDef& Out, FString& OutError)
{
	TSharedPtr<FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	const FKvNode* Data = Root.IsValid() ? Root->Child(TEXT("WeaponData")) : nullptr;
	if (Data == nullptr)
	{
		OutError = FString::Printf(TEXT("no WeaponData block in %s"), *Classname);
		return false;
	}

	Out = FElysiumItemDef();
	Out.Classname = Classname;
	Out.PrintName = Data->Str(TEXT("printname"), FString());
	Out.Description = Data->Str(TEXT("description"), FString());

	ElysiumParseItemType(Data->Str(TEXT("item_type"), FString()), Out.Type, Out.bHidden);

	Out.bStackable = Data->Bool(TEXT("is_stackable"), false);
	// `stack_limit` only. One patch file authors `stacklimit` without the underscore, which the
	// engine's own `GetInt("stack_limit")` never reads either — a defect reproduced, not repaired.
	Out.StackLimit = Data->Int(TEXT("stack_limit"), 0);
	Out.bDroppable = Data->Bool(TEXT("is_droppable"), true);
	Out.bPermanentInventory = Data->Bool(TEXT("permanent_inventory"), false);
	Out.bWieldable = Data->Bool(TEXT("is_wieldable"), false);
	Out.bVisibleInHud = Data->Bool(TEXT("is_visible_in_hud"), true);

	Out.Bucket = Data->Int(TEXT("bucket"), 0);
	Out.BucketPosition = Data->Int(TEXT("bucket_position"), 0);

	// The wield rule's weapon half (`docs/vtmb/wielded_weapons.md` § "Who may wield what").
	Out.EquipMask = ElysiumEquipFlags::Parse(Data->Str(TEXT("equip_mask"), FString()));

	Out.Worth = Data->Int(TEXT("item_worth"), 0);
	Out.PlayerSell = Data->Int(TEXT("player_sell"), 0);
	Out.Weight = Data->Int(TEXT("weight"), 0);
	Out.ItemFlags = Data->Int(TEXT("item_flags"), 0);

	Out.PlayerModel = Data->Str(TEXT("playermodel"), FString());
	Out.ViewModel = Data->Str(TEXT("viewmodel"), FString());
	Out.InfoModel = Data->Str(TEXT("infomodel"), FString());
	Out.WieldModelM = Data->Str(TEXT("wieldmodel_m"), FString());
	Out.WieldModelF = Data->Str(TEXT("wieldmodel_f"), FString());
	Out.AnimPrefix = Data->Str(TEXT("anim_prefix"), FString());

	Out.CameraClass = ElysiumCam::ParseCameraClass(Data->Str(TEXT("camera_class"), FString()));

	Out.bReloadSingle = Data->Bool(TEXT("reload_single"), false);
	Out.bDisallowFirearmsToBashing = Data->Bool(TEXT("Disallow_FirearmsToBashing"), false);

	// The `Activation` blocks, in file order — a record may author several and the order is what
	// `PrimaryMode2`'s toggle cycles through.
	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Data->Kids)
	{
		if (Kid.Key != TEXT("activation") || !Kid.Value.IsValid())
		{
			continue;
		}
		FElysiumWeaponMode Mode;
		ParseWeaponMode(*Kid.Value, Mode);
		Out.Modes.Add(MoveTemp(Mode));
	}

	if (const FKvNode* Magazine = Data->Child(TEXT("Magazine")))
	{
		Out.AmmoType = Magazine->Str(TEXT("Type"), FString());
		Out.MagazineSize = Magazine->Int(TEXT("Size"), 0);
		// An unauthored `Default_Size` falls back to the magazine's capacity: the file states the
		// two separately only where they differ.
		Out.DefaultAmmo = Magazine->Int(TEXT("Default_Size"), Out.MagazineSize);
		Out.DroppedAmmo = Magazine->Int(TEXT("Dropped_Ammo"), 0);
		Out.ReloadTime = Magazine->Flt(TEXT("ReloadTime"), 0.0f);
	}

	OutError.Reset();
	return true;
}

bool FElysiumItemTable::Load(FString& OutError)
{
	Items.Reset();
	ByName.Reset();

	const FString Dir = FElysiumContentPaths::VdataDir() / TEXT("items");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir / TEXT("*.txt")), true, false);
	Files.Sort();

	for (const FString& Leaf : Files)
	{
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *(Dir / Leaf)))
		{
			OutError = FString::Printf(TEXT("unreadable: %s"), *(Dir / Leaf));
			continue;   // one unreadable file must not cost the other 243
		}
		FElysiumItemDef Def;
		FString FileError;
		if (!ParseText(FPaths::GetBaseFilename(Leaf), Raw, Def, FileError))
		{
			OutError = FileError;
			continue;
		}
		Items.Add(MoveTemp(Def));
	}

	if (Items.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no item definitions in %s"), *Dir);
		return false;
	}
	Reindex();
	OutError.Reset();
	return true;
}

void FElysiumItemTable::Reindex()
{
	ByName.Reset();
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		Index(ByName, Items[i].Classname, i);
	}
}

const FElysiumItemDef* FElysiumItemTable::Find(const FString& Classname) const
{
	const int32* Idx = ByName.Find(ElysiumFold(Classname));
	return Idx ? &Items[*Idx] : nullptr;
}

const FElysiumItemDef* FElysiumItemTable::At(int32 InIndex) const
{
	return Items.IsValidIndex(InIndex) ? &Items[InIndex] : nullptr;
}

int32 FElysiumItemTable::CountOfType(EElysiumItemType Type) const
{
	int32 N = 0;
	for (const FElysiumItemDef& Def : Items)
	{
		if (Def.Type == Type) { ++N; }
	}
	return N;
}
