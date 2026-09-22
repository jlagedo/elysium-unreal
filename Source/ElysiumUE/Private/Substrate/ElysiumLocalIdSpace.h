// `CAI_LocalIdSpace` -- one class's range inside one global namespace, and the parent it falls
// through to.
//
// Retail's record is six words, and this port carries the same six under their own names:
//
//   +0x00  m_globalBase      taken from the namespace's next-free counter at Init
//   +0x04  m_localBase       9999 = "this space holds no ids"
//   +0x08  m_localTop        the highest LOCAL id registered
//   +0x0c  m_translatedTop   the highest GLOBAL id registered
//   +0x10  m_pParent         the parent space, or null at a root
//   +0x14  m_pNamespace      the global namespace it registers into
//
// `m_translatedTop` is the one the port had been missing, and missing it is not cosmetic:
// `GlobalToLocal 0x102ea280` bounds a GLOBAL id against it, and a port that bounds against
// `m_localTop` instead is comparing a global number with a local one. That was invisible only
// because every id-space row in this runtime was the empty sentinel and every translation answered
// -1; it becomes a live range bug the moment real ranges land.

#pragma once

#include "CoreMinimal.h"
#include "Substrate/ElysiumIdNamespace.h"
#include "Substrate/ElysiumScheduleId.h"

/**
 * One class's id range in one namespace.
 *
 * Every body here is `docs/vtmb/npc-ai/schedule-kernel.md` § "The schedule owners and their
 * registrations" plus the four retail bodies it names, ported arm for arm.
 */
struct FElysiumLocalIdSpace
{
	int32 GlobalBase = INDEX_NONE;                                 // +0x00
	int32 LocalBase = ElysiumScheduleId::EmptyLocalBase;           // +0x04
	int32 LocalTop = INDEX_NONE;                                   // +0x08
	int32 TranslatedTop = INDEX_NONE;                              // +0x0c
	const FElysiumLocalIdSpace* Parent = nullptr;                  // +0x10
	FElysiumIdNamespace* Namespace = nullptr;                      // +0x14

	/** `CAI_LocalIdSpace::Init 0x102ea0e0`.
	 *
	 *  Takes the space's global base FROM the namespace rather than being given one, and seeds
	 *  `m_localBase` to `9999` when there is a parent and `0` when there is not -- so a root space
	 *  is open for business and a parented one stays empty until its first `Register` widens it.
	 *  The re-init guard is retail's own: a space whose translated top is already `-1` keeps its
	 *  range rather than having it cleared again. */
	bool Init(FElysiumIdNamespace& InNamespace, const FElysiumLocalIdSpace* InParent);

	/** `CAI_LocalIdSpace::Register 0x102ea130`: widen the range to hold `LocalId`, then insert
	 *  `Name` into the namespace under its translated global id.
	 *
	 *  False on any of retail's three refusals, each of which is a `Msg` there and a log line here:
	 *  an uninitialised space, a first local id the parent already covers, and an id below the
	 *  base. The caller reports which name it was; retail's own message names the category and the
	 *  class, which is why both are arguments. */
	bool Register(const FString& Name, int32 LocalId, const TCHAR* Category, const TCHAR* ClassName);

	/** `CAI_LocalIdSpace::LocalToGlobal 0x102ea2d0`: walk the chain and answer the first space
	 *  whose LOCAL range holds the id, else `-1`. `-1` stays `-1`. */
	int32 LocalToGlobal(int32 LocalId) const;

	/** `CAI_LocalIdSpace::GlobalToLocal 0x102ea280`: walk the chain and answer the first space
	 *  whose range holds the id, bounded below by `m_globalBase` and above by `m_translatedTop`. */
	int32 GlobalToLocal(int32 GlobalId) const;

	/** Whether this space has taken any id at all. */
	bool IsEmpty() const { return LocalBase == ElysiumScheduleId::EmptyLocalBase; }

	/** Back to the state a fresh static would hold. */
	void Reset();

private:
	/** `0x102ea240`, retail's "the first local id decides the base".
	 *
	 *  It refuses when the PARENT already holds ids at or above this one, which is what stops a
	 *  child from claiming a range its parent is using -- the only structural rule the id spaces
	 *  have, and the reason a species' first schedule id sits above its base class's last. */
	bool SetFirstLocal(int32 LocalId);
};
