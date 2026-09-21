#pragma once

#include "CoreMinimal.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumPlayer.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumVariant.h"

// One character-sheet slot registered as a datamap field on `CBaseCombatCharacter`.
//
// Retail holds the sheet as four fixed int arrays on `CBaseCombatCharacter` and gives every ELEMENT
// its own `typedescription_t` row — `m_iVAttributesCurrent[ v_attribute_strength ]` is the row named
// `strength`, and the parallel base array's is `base_strength` (`ElysiumSheetSlots.h`). So a sheet
// trait is reached by exactly the same R2 walk as any other keyfield, and the 148 rows are data the
// datamap replay carries rather than a second namespace.
//
// The port stores the sheet as `FElysiumSheet`, addressed by (container, slot) instead of by
// offset; this is the seam between the two. The names come from the replay through
// `gen_kernel_bindings`, never from the port's own slot table — retail's spelling is what a map and
// a level script can author, typos included.
inline void ElysiumAddSheetField(FElysiumClassDesc& D, const TCHAR* Name,
	EElysiumTraitContainer Container, int32 Slot, bool bBase,
	EElysiumField Flags = ElysiumFieldDefault)
{
	FElysiumFieldAccessor Acc;
	Acc.ApplyFlags(Flags);
	Acc.Type = EElysiumVariantType::Int;
	Acc.Get = [Container, Slot, bBase](const FElysiumEntity& E)
	{
		const FElysiumSheet& S = static_cast<const FElysiumCombatCharacter&>(E).Sheet;
		return FElysiumVariant::Int(bBase ? S.GetBase(Container, Slot) : S.GetCurrent(Container, Slot));
	};
	Acc.Set = [Container, Slot](FElysiumEntity& E, const FElysiumVariant& V)
	{
		// A write lands on the base either way: a keyvalue and a script assignment both set the
		// character sheet, and the current value is derived from it.
		static_cast<FElysiumCombatCharacter&>(E).Sheet.SetBase(Container, Slot, V.ToInt());
	};
	D.Fields.Add(FName(Name), MoveTemp(Acc));
}
