#include "Substrate/ElysiumNpcKernelClassLookup.h"

#include "Containers/Map.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpc.h"

namespace
{
	// The census is `constexpr` data with a stable address and a fixed order, so every index built
	// here is built once. Unit-prefixed names because the module builds adaptive-unity.
	struct FElysiumNpcKernelClassIndex
	{
		TMap<FString, const FElysiumNpcClass*> ByName;
		// classname -> the most derived class claiming it.
		TMap<FString, const FElysiumNpcClass*> ByClassname;
		// (class, slot) -> the override row.
		TMap<TPair<FString, int32>, const FElysiumNpcClassSlot*> Overrides;
		TMap<int32, const FElysiumNpcSlot*> SlotRows;

		FElysiumNpcKernelClassIndex()
		{
			for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
			{
				ByName.Add(FString(Row.Name), &Row);
			}
			for (const FElysiumNpcClass& Row : ElysiumNpcKernelShape::Classes())
			{
				for (int32 Index = 0; Index < Row.ClassnameCount; ++Index)
				{
					const FString Classname(Row.Classnames[Index]);
					const FElysiumNpcClass** Existing = ByClassname.Find(Classname);
					// The most derived claimant wins: a claimant that descends from the one already
					// recorded replaces it, and one the recorded class descends from is ignored.
					if (Existing == nullptr || IsDescendant(&Row, *Existing))
					{
						ByClassname.Add(Classname, &Row);
					}
				}
			}
			for (const FElysiumNpcClassSlot& Row : ElysiumNpcKernelShape::Overrides())
			{
				Overrides.Add(TPair<FString, int32>(FString(Row.Class), Row.Slot), &Row);
			}
			for (const FElysiumNpcSlot& Row : ElysiumNpcKernelShape::Slots())
			{
				// The Troika line only; a per-branch virtual past 583/617 names its own class and is
				// not a slot of the line every body dispatches through.
				if (Row.Class == nullptr || Row.Class[0] == TEXT('\0'))
				{
					SlotRows.Add(Row.Slot, &Row);
				}
			}
		}

		// Whether `Candidate` derives from `Ancestor` (strictly or as the same class). Used during
		// construction, so it walks `ByName` rather than the finished index.
		bool IsDescendant(const FElysiumNpcClass* Candidate, const FElysiumNpcClass* Ancestor) const
		{
			for (const FElysiumNpcClass* Walk = Candidate; Walk != nullptr; )
			{
				if (Walk == Ancestor)
				{
					return true;
				}
				const FElysiumNpcClass* const* Base = ByName.Find(FString(Walk->Base));
				Walk = Base != nullptr ? *Base : nullptr;
			}
			return false;
		}
	};

	const FElysiumNpcKernelClassIndex& KernelClassIndex()
	{
		static const FElysiumNpcKernelClassIndex Index;
		return Index;
	}
}

namespace ElysiumNpcKernelClass
{
	const FElysiumNpcClass* Find(const TCHAR* RetailClass)
	{
		if (RetailClass == nullptr)
		{
			return nullptr;
		}
		const FElysiumNpcClass* const* Row = KernelClassIndex().ByName.Find(FString(RetailClass));
		return Row != nullptr ? *Row : nullptr;
	}

	const FElysiumNpcClass* OfClassname(const FString& Classname)
	{
		const FElysiumNpcClass* const* Row = KernelClassIndex().ByClassname.Find(Classname);
		return Row != nullptr ? *Row : nullptr;
	}

	bool DerivesFrom(const FElysiumNpcClass* Cls, const TCHAR* Ancestor)
	{
		if (Cls == nullptr || Ancestor == nullptr)
		{
			return false;
		}
		const FString Wanted(Ancestor);
		for (const FElysiumNpcClass* Walk = Cls; Walk != nullptr; Walk = Find(Walk->Base))
		{
			if (Wanted.Equals(Walk->Name, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	const FElysiumNpcClassSlot* OverrideOf(const FElysiumNpcClass* Cls, int32 Slot)
	{
		const FElysiumNpcKernelClassIndex& Index = KernelClassIndex();
		for (const FElysiumNpcClass* Walk = Cls; Walk != nullptr; Walk = Find(Walk->Base))
		{
			const FElysiumNpcClassSlot* const* Row =
				Index.Overrides.Find(TPair<FString, int32>(FString(Walk->Name), Slot));
			if (Row != nullptr)
			{
				return *Row;
			}
		}
		return nullptr;
	}

	const TCHAR* BodyOf(const FElysiumNpcClass* Cls, int32 Slot)
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

	const FElysiumNpcSlot* SlotRow(int32 Slot)
	{
		const FElysiumNpcSlot* const* Row = KernelClassIndex().SlotRows.Find(Slot);
		return Row != nullptr ? *Row : nullptr;
	}
}

// --- The leaf's own answer ----------------------------------------------------------------------

const FElysiumNpcClass* FElysiumNpc::RetailClass() const
{
	if (!bRetailClassResolved)
	{
		bRetailClassResolved = true;
		RetailClassRow = Def != nullptr ? ElysiumNpcKernelClass::OfClassname(Def->Classname)
			: nullptr;
	}
	return RetailClassRow;
}

bool FElysiumNpc::IsRetailClass(const TCHAR* RetailClassName) const
{
	return ElysiumNpcKernelClass::DerivesFrom(RetailClass(), RetailClassName);
}
