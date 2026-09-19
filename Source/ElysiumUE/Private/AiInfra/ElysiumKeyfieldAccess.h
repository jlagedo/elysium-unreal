#pragma once

#include "CoreMinimal.h"

class FProperty;
class UScriptStruct;

// Reads and writes one generated keyfield struct (`AiInfra/ElysiumInfraKeyfields.h`) by the
// keyvalue a map authors. The struct's reflected property name IS the retail external name, and
// `FindProperty` folds case exactly as the entity registry's `FName` lookup does, so a key resolves
// here if and only if it resolves to the same datamap row at spawn.
//
// Parsing is the runtime's own, not Unreal's text import: `Atoi` for int and bool (`!= 0`), `Atof`
// for float, `ElysiumParseVec3` for a vector, the raw string for a string — the rules
// `FElysiumEntity::Construct` applies, so a typed value here is the value the live entity gets.
namespace ElysiumKeyfieldAccess
{
	// One struct instance, viewed through its reflection. Non-owning.
	struct FView
	{
		const UScriptStruct* Struct = nullptr;
		void* Data = nullptr;
	};

	// The struct's property for `Key` (case-insensitive), or null when the key is not one of its
	// rows or the row's type is not one this reader handles.
	const FProperty* FindProperty(const UScriptStruct* Struct, const FString& Key);

	// The first view in `Views` whose struct has `Key`; its property through `OutProperty`.
	const FView* FindOwner(TConstArrayView<FView> Views, const FString& Key, const FProperty*& OutProperty);

	// Write the parse of `Raw` into the property.
	void Apply(const FProperty* Property, void* Data, const FString& Raw);

	// Whether the property currently holds exactly what `Raw` parses to.
	bool Matches(const FProperty* Property, const void* Data, const FString& Raw);

	// The property's value as a keyvalue string that parses back to the same value.
	FString Format(const FProperty* Property, const void* Data);

	// Whether the property holds its zero value (0, 0.0, false, empty, the zero vector).
	bool IsZero(const FProperty* Property, const void* Data);
}
