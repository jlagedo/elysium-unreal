#include "Substrate/ElysiumNpcKernelClassLookup.h"

#include "Containers/Map.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumNpc.h"

namespace
{
	// The census is `constexpr` data with a stable address and a fixed order, so every index built
	// here is built once. Unit-prefixed names because the module builds adaptive-unity.
	struct FElysiumNpcKernelClassIndex
	{
		TMap<FString, const FElysiumNpcClass*> ByName;
		// classname -> the class its retail factory builds.
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
					// The census classnames are retail's factories (story 5 step 2): each names the
					// one class it builds, so a second claimant is a generator defect.
					const FString Classname(Row.Classnames[Index]);
					checkf(!ByClassname.Contains(Classname), TEXT("census classname %s has two claimants"),
						*Classname);
					ByClassname.Add(Classname, &Row);
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
	// The C++ class is the answer: the classname's factory built it (story 5 step 2), so nothing
	// here reads the classname. The test latch stands only for the enumerated deferred classes.
	return bRetailClassForTests ? RetailClassForTests : OwnRetailClass();
}

const FElysiumNpcClass* FElysiumNpc::OwnRetailClass() const
{
	return nullptr;
}

bool FElysiumNpc::IsRetailClass(const TCHAR* RetailClassName) const
{
	return ElysiumNpcKernelClass::DerivesFrom(RetailClass(), RetailClassName);
}

bool FElysiumNpc::OwnRetailClassDerivesFrom(const TCHAR* RetailClassName) const
{
	return ElysiumNpcKernelClass::DerivesFrom(OwnRetailClass(), RetailClassName);
}

// --- The two hull words -------------------------------------------------------------------------

void FElysiumNpc::ApplyRetailHulls()
{
	// Retail writes both words in a CONSTRUCTOR, so a class with no store of its own holds
	// whatever the nearest ancestor's constructor left -- which for everything under
	// `CAI_BaseNPC` is 0, that constructor zeroing both before any derived one runs. The table is
	// ordered most-derived first, so the first row this body's chain claims IS that nearest
	// ancestor (`navigation-jump-links.md` § "The two hull words").
	//
	// `CNPC_VRat` is the case that needs the ordering: it has no constructor of its own, and its
	// factory runs `CNPC_VScurrying`'s and then re-vtables, so 19 reaches it by inheritance.
	for (int32 Index = 0; Index < ElysiumRetailHulls::ClassHullCount; ++Index)
	{
		const ElysiumRetailHulls::FClassHulls& Row = ElysiumRetailHulls::ClassHulls[Index];
		if (IsRetailClass(Row.RetailClass))
		{
			// `CBaseCombatCharacter`'s own 23 is one past the table and never survives retail's
			// construction either. Refusing it here keeps an unassigned value from reaching the
			// extent accessors, which index raw and bounds-check nothing.
			const bool bUsable = ElysiumRetailHulls::Find(Row.Standing) != nullptr
				&& ElysiumRetailHulls::Find(Row.Pathing) != nullptr;
			if (bUsable)
			{
				HullKind = Row.Standing;
				PathingHullKind = Row.Pathing;
				return;
			}
			break;
		}
	}
	HullKind = ElysiumRetailHulls::DefaultHull;
	PathingHullKind = ElysiumRetailHulls::DefaultHull;
}
