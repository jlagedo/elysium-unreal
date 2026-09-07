#include "Substrate/ElysiumWieldRules.h"

#include "ElysiumKeyValues.h"
#include "Substrate/ElysiumItemClasses.h"   // LogElysiumItem — the item family's own category
#include "Substrate/ElysiumRulebook.h"      // ElysiumFold
#include "Substrate/ElysiumVdataLoad.h"

namespace
{
	using ElysiumKeyValues::FKvNode;
	using ElysiumVdata::ReadVdata;
	using ElysiumVdata::RootBlock;

	// The name table `ParseEquipFlag` (0x1025b740) walks, index = bit. Order is the string table's
	// own (0x105c76f8..0x105c775c); the ninth entry is unrecovered and has no shipped author, so the
	// walk simply ends one short of the bit reserved for it.
	const TCHAR* const GEquipFlagNames[] =
	{
		TEXT("never"),
		TEXT("blueblood"),
		TEXT("no_blueblood"),
		TEXT("wolfform"),
		TEXT("no_wolfform"),
		TEXT("clawedform"),
		TEXT("no_clawedform"),
		TEXT("no_npc"),
	};
}

uint32 ElysiumEquipFlags::Bit(const FString& Token)
{
	// `normal` is the one literal the function answers with a COMPOSITE rather than a single bit.
	if (Token.Equals(TEXT("normal"), ESearchCase::IgnoreCase))
	{
		return Normal;
	}
	for (int32 i = 0; i < UE_ARRAY_COUNT(GEquipFlagNames); ++i)
	{
		if (Token.Equals(GEquipFlagNames[i], ESearchCase::IgnoreCase))
		{
			return 1u << i;
		}
	}
	return 0;   // `ParseEquipFlag`'s own answer for a name the table does not carry
}

uint32 ElysiumEquipFlags::Parse(const FString& Authored)
{
	uint32 Mask = 0;
	TArray<FString> Tokens;
	// One `ParseEquipFlag` call per whitespace-separated token: `"Normal No_Npc"` is one shipped
	// author (`item_w_fists`'s family), so the key is a SET and not a single word.
	Authored.ParseIntoArrayWS(Tokens);
	for (const FString& Token : Tokens)
	{
		const uint32 Value = Bit(Token);
		if (Value == 0)
		{
			UE_LOG(LogElysiumItem, Warning,
				TEXT("equip flag '%s' names no ParseEquipFlag entry and contributes nothing"),
				*Token);
		}
		// The accumulator's own rule: anything that is not the `never` bit itself CLEARS `never`
		// before it is OR'd in, so a later flag beside `Never` un-says it.
		if (Value != 0 && Value != Never)
		{
			Mask &= ~(uint32)Never;
		}
		Mask |= Value;
	}
	return Mask;
}

FString ElysiumEquipFlags::Describe(uint32 Mask)
{
	if (Mask == 0)
	{
		return TEXT("(none)");
	}
	TArray<FString> Names;
	for (int32 i = 0; i < UE_ARRAY_COUNT(GEquipFlagNames); ++i)
	{
		if ((Mask & (1u << i)) != 0)
		{
			Names.Add(GEquipFlagNames[i]);
		}
	}
	if ((Mask & Unrecovered8) != 0)
	{
		Names.Add(TEXT("<unrecovered bit 8>"));
	}
	return FString::Join(Names, TEXT(" "));
}

bool FElysiumExcludedEquipTable::Load(FString& OutError)
{
	Rows.Reset();
	ByName.Reset();

	static const TCHAR* Rel = TEXT("system/items.txt");
	TSharedPtr<FKvNode> Root;
	if (!ReadVdata(Rel, Root, OutError))
	{
		return false;
	}
	const FKvNode* Data = RootBlock(Root, TEXT("ItemTypeData"), Rel, OutError);
	if (Data == nullptr)
	{
		return false;
	}
	const FKvNode* Tables = Data->Child(TEXT("ExcludedEquipTables"));
	if (Tables == nullptr)
	{
		OutError = FString::Printf(TEXT("no ExcludedEquipTables block in %s"), Rel);
		return false;
	}

	for (const TPair<FString, TSharedPtr<FKvNode>>& Kid : Tables->Kids)
	{
		if (Kid.Key != TEXT("excludedequip") || !Kid.Value.IsValid())
		{
			continue;
		}
		FElysiumExcludedEquipRow Row;
		Row.InternalName = Kid.Value->Str(TEXT("InternalName"), FString());
		Row.Name = Kid.Value->Str(TEXT("Name"), FString());

		// BOTH keys repeat inside a row (`Clawed_Form_Toreador` authors two `ExcludedFlag`s and one
		// `RequiredFlag`), so they are read through `ValuesFor` and accumulated by the same rule one
		// authored string is.
		TArray<FString> Authored;
		Kid.Value->ValuesFor(TEXT("ExcludedFlag"), Authored);
		for (const FString& Value : Authored)
		{
			Row.Excluded |= ElysiumEquipFlags::Parse(Value);
		}
		Authored.Reset();
		Kid.Value->ValuesFor(TEXT("RequiredFlag"), Authored);
		for (const FString& Value : Authored)
		{
			Row.Required |= ElysiumEquipFlags::Parse(Value);
		}
		Rows.Add(MoveTemp(Row));
	}

	if (Rows.IsEmpty())
	{
		OutError = FString::Printf(TEXT("no ExcludedEquip rows in %s"), Rel);
		return false;
	}
	Reindex();
	OutError.Reset();
	return true;
}

void FElysiumExcludedEquipTable::Reindex()
{
	ByName.Reset();
	for (int32 i = 0; i < Rows.Num(); ++i)
	{
		Rows[i].Index = i;
		ElysiumVdata::Index(ByName, Rows[i].InternalName, i);
	}
}

const FElysiumExcludedEquipRow* FElysiumExcludedEquipTable::At(int32 Index) const
{
	return Rows.IsValidIndex(Index) ? &Rows[Index] : nullptr;
}

const FElysiumExcludedEquipRow* FElysiumExcludedEquipTable::Find(const FString& InternalName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InternalName));
	return Idx ? &Rows[*Idx] : nullptr;
}

int32 FElysiumExcludedEquipTable::RowIndexByName(const FString& InternalName) const
{
	const int32* Idx = ByName.Find(ElysiumFold(InternalName));
	return Idx ? *Idx : INDEX_NONE;
}

bool FElysiumExcludedEquipTable::CanWield(int32 RowIndex, uint32 EquipMask) const
{
	// Arm 1 is the row-independent one: `never` refuses everything, including a character whose
	// stat names no row at all.
	if ((EquipMask & (uint32)ElysiumEquipFlags::Never) != 0)
	{
		return false;
	}
	const FElysiumExcludedEquipRow* Row = At(RowIndex);
	if (Row == nullptr)
	{
		return true;
	}
	if ((Row->Excluded & EquipMask) != 0)
	{
		return false;
	}
	if (Row->Required != 0 && (EquipMask & Row->Required) == 0)
	{
		return false;
	}
	return true;
}
