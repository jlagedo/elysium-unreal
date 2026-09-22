#include "Substrate/ElysiumLocalIdSpace.h"

#include "Substrate/ElysiumNpcLog.h"   // the one `npc_*` category a refused registration reports on

bool FElysiumLocalIdSpace::Init(
	FElysiumIdNamespace& InNamespace, const FElysiumLocalIdSpace* InParent)
{
	// `0x102ea0e0`, arm for arm:
	//
	//   if (this[+0x0c] != -1) {
	//     this[+0x08] = -1;                       // m_localTop
	//     this[+0x0c] = -1;                       // m_translatedTop
	//     this[+0x04] = (parent != 0) ? 9999 : 0; // m_localBase
	//   }
	//   this[+0x10] = parent;
	//   this[+0x14] = namespace;
	//   this[+0x00] = namespace[+0x04];           // m_globalBase <- the next-free counter
	//   return true;
	//
	// The guard reads oddly and is retail's: a space whose translated top is ALREADY -1 (a fresh
	// static) skips the clear, and one that has been used has its range thrown away. Either way
	// the parent, the namespace and the global base are re-taken.
	if (TranslatedTop != INDEX_NONE)
	{
		LocalTop = INDEX_NONE;
		TranslatedTop = INDEX_NONE;
		LocalBase = (InParent != nullptr) ? ElysiumScheduleId::EmptyLocalBase : 0;
	}
	else if (LocalBase == ElysiumScheduleId::EmptyLocalBase && InParent == nullptr)
	{
		// A fresh static that is being stood as a ROOT still needs its base opened; retail reaches
		// the same state because its statics are zero-initialised rather than 9999-initialised.
		LocalBase = 0;
	}
	Parent = InParent;
	Namespace = &InNamespace;
	GlobalBase = InNamespace.NextFree();
	return true;
}

bool FElysiumLocalIdSpace::SetFirstLocal(int32 LocalId)
{
	// `0x102ea240`. The first id registered decides the base -- but only if the parent is not
	// already using that number or a higher one. This is the one structural rule the spaces have,
	// and it is why `CAI_BaseNPCTroika`'s schedules start at 68: `CAI_BaseNPC` registered through
	// 0x43 and Troika's first id has to clear it.
	if (LocalBase != ElysiumScheduleId::EmptyLocalBase)
	{
		return false;
	}
	LocalBase = LocalId;
	if (Parent == nullptr || Parent->LocalBase == ElysiumScheduleId::EmptyLocalBase
		|| Parent->LocalBase < LocalId)
	{
		return true;
	}
	return false;
}

bool FElysiumLocalIdSpace::Register(
	const FString& Name, int32 LocalId, const TCHAR* Category, const TCHAR* ClassName)
{
	// `0x102ea130`, including its three refusals. Retail reports each through `Msg`; the port logs
	// the same three facts, because a corpus that mis-numbers a class is a load error and not a
	// silent range.
	if (Namespace == nullptr)
	{
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("Adding symbol to uninitialized id space: %s (%s)"), ClassName, Category);
		return false;
	}

	if (LocalBase == ElysiumScheduleId::EmptyLocalBase)
	{
		if (!SetFirstLocal(LocalId))
		{
			UE_LOG(LogElysiumNpcEnt, Error,
				TEXT("Bad %s LOCALID for %s: %d is not above its parent's base"),
				Category, ClassName, LocalId);
			return false;
		}
	}

	const int32 Base = LocalBase;
	if (LocalId < Base)
	{
		UE_LOG(LogElysiumNpcEnt, Error,
			TEXT("%s: first added %s must be the lowest; %d is below the base %d"),
			ClassName, Category, LocalId, Base);
		return false;
	}

	if (LocalTop == INDEX_NONE)
	{
		LocalTop = Base;
		TranslatedTop = GlobalBase;
	}
	else if (LocalTop < LocalId)
	{
		LocalTop = LocalId;
		TranslatedTop = (GlobalBase - Base) + LocalId;
	}

	Namespace->Insert(Name, LocalToGlobal(LocalId));
	return true;
}

int32 FElysiumLocalIdSpace::LocalToGlobal(int32 LocalId) const
{
	// `0x102ea2d0`:
	//
	//   if (id == -1) return -1;
	//   do {
	//     if (localBase != 9999 && localBase <= id && id <= localTop)
	//       return (globalBase - localBase) + id;
	//     space = space->parent;
	//   } while (space);
	//   return -1;
	if (LocalId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	for (const FElysiumLocalIdSpace* Space = this; Space != nullptr; Space = Space->Parent)
	{
		if (Space->LocalBase != ElysiumScheduleId::EmptyLocalBase
			&& Space->LocalBase <= LocalId && LocalId <= Space->LocalTop)
		{
			return (Space->GlobalBase - Space->LocalBase) + LocalId;
		}
	}
	return INDEX_NONE;
}

int32 FElysiumLocalIdSpace::GlobalToLocal(int32 GlobalId) const
{
	// `0x102ea280`. The upper bound is `m_translatedTop` (+0x0c) and NOT `m_localTop`: the id being
	// tested is global, so the bound it is weighed against has to be global too.
	if (GlobalId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	for (const FElysiumLocalIdSpace* Space = this; Space != nullptr; Space = Space->Parent)
	{
		if (Space->LocalBase != ElysiumScheduleId::EmptyLocalBase
			&& Space->GlobalBase <= GlobalId && GlobalId <= Space->TranslatedTop)
		{
			return (Space->LocalBase - Space->GlobalBase) + GlobalId;
		}
	}
	return INDEX_NONE;
}

void FElysiumLocalIdSpace::Reset()
{
	GlobalBase = INDEX_NONE;
	LocalBase = ElysiumScheduleId::EmptyLocalBase;
	LocalTop = INDEX_NONE;
	TranslatedTop = INDEX_NONE;
	Parent = nullptr;
	Namespace = nullptr;
}
