#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcKernelShape.h"

// The census read by name, for TESTS ONLY (0019 story 5 commit B).
//
// The runtime has no string-keyed class dispatch: a class's behaviour is its C++ overrides and
// retail's `__RTDynamicCast` is `FElysiumNpcBase::AsSpecies<T>()`. What remains is a test's need to
// pin a recovered fact against the census -- "the census says `CNPC_VCamera` fills slot 259 with
// `0x10368ec0`", "`npc_VCop`'s factory builds `CNPC_VCop`" -- so that a body's cited address and
// the ledger agree. These readers answer those questions from `ElysiumNpcKernelShape` alone and
// nothing in `Source/ElysiumUE/Private/Substrate` may include this header.
namespace ElysiumNpcTestCensus
{
	// The census row of a retail class by name, or null.
	inline const FElysiumNpcClass* Find(const TCHAR* RetailClass)
	{
		return ElysiumNpcKernelShape::ClassNamed(RetailClass);
	}

	// The class whose retail factory builds `Classname`, or null: the census's classname column.
	inline const FElysiumNpcClass* OfClassname(const FString& Classname)
	{
		for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
		{
			for (int32 Index = 0; Index < Row.ClassnameCount; ++Index)
			{
				if (Classname.Equals(Row.Classnames[Index], ESearchCase::CaseSensitive))
				{
					return &Row;
				}
			}
		}
		return nullptr;
	}

	// Whether census row `Cls` is `Ancestor` or has it on its base chain.
	inline bool DerivesFrom(const FElysiumNpcClass* Cls, const TCHAR* Ancestor)
	{
		for (const FElysiumNpcClass* Walk = Cls; Walk != nullptr && Ancestor != nullptr;
			Walk = Find(Walk->Base))
		{
			if (FCString::Strcmp(Walk->Name, Ancestor) == 0)
			{
				return true;
			}
		}
		return false;
	}

	// The census's own-body row of `Slot` nearest `Cls` on its base chain, or null when the chain
	// carries only the Troika line's body there.
	inline const FElysiumNpcClassSlot* OverrideOf(const FElysiumNpcClass* Cls, int32 Slot)
	{
		for (const FElysiumNpcClass* Walk = Cls; Walk != nullptr; Walk = Find(Walk->Base))
		{
			for (const FElysiumNpcClassSlot& Row : ElysiumNpcKernelShape::Overrides())
			{
				if (Row.Slot == Slot && FCString::Strcmp(Row.Class, Walk->Name) == 0)
				{
					return &Row;
				}
			}
		}
		return nullptr;
	}

	// The Troika-line census row of a slot, or null.
	inline const FElysiumNpcSlot* SlotRow(int32 Slot)
	{
		for (const FElysiumNpcSlot& Row : ElysiumNpcKernelShape::Slots())
		{
			if (Row.Slot == Slot && (Row.Class == nullptr || Row.Class[0] == TEXT('\0')))
			{
				return &Row;
			}
		}
		return nullptr;
	}

	// The retail address that fills `Slot` for `Cls`: the nearest own-body row's, else the Troika
	// line's. Empty where neither carries one.
	inline const TCHAR* BodyOf(const FElysiumNpcClass* Cls, int32 Slot)
	{
		if (const FElysiumNpcClassSlot* Row = OverrideOf(Cls, Slot))
		{
			return Row->Address;
		}
		if (const FElysiumNpcSlot* Row = SlotRow(Slot))
		{
			return Row->Address;
		}
		return TEXT("");
	}
}
