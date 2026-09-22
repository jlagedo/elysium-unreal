#include "Substrate/ElysiumIdNamespace.h"

void FElysiumIdNamespace::Insert(const FString& Name, int32 GlobalId)
{
	// `0x102e9fe0`:
	//
	//   if (id != -1) {
	//     table.Insert(name, id);
	//     if (nextFree < id + 1) nextFree = id + 1;
	//   }
	//
	// The counter is a HIGH-WATER MARK, not a running count: it moves only when an id lands above
	// it, which is why registering a class's ids out of order still leaves the next class's space
	// starting past all of them.
	if (GlobalId == INDEX_NONE)
	{
		return;
	}
	Symbols.Add(Name.ToLower(), GlobalId);
	if (NextGlobalId < GlobalId + 1)
	{
		NextGlobalId = GlobalId + 1;
	}
}

int32 FElysiumIdNamespace::Find(const FString& Name) const
{
	// `0x102ea050`. The stored id is returned verbatim; nothing is added or subtracted here, which
	// is why a schedule text's `Schedule:` operand still has to be translated into the reading
	// class's local space afterwards and an interrupt's condition does NOT.
	if (const int32* Found = Symbols.Find(Name.ToLower()))
	{
		return *Found;
	}
	return INDEX_NONE;
}

void FElysiumIdNamespace::Reset()
{
	Symbols.Reset();
	NextGlobalId = ElysiumScheduleId::GlobalBase;
}
