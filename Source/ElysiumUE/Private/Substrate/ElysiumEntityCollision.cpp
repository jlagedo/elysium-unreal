// `CBaseEntity`'s collision property and its untouch contract (`walks/L0-r015.md`): the
// `CCollisionProperty` initializer and setters (`FUN_100dc190`, `FUN_100dc300`, `FUN_100dc480`,
// `FUN_100dc580`, `FUN_100dda20`, `FUN_100ddd20`, `FUN_100ddc40`, `FUN_100dc430`), the flag-word
// accessors (slots 83 / 84), `SetCheckUntouch` (slot 6), `IsCurrentlyTouching` (slot 207),
// `PhysicsCheckForEntityUntouch`, and the physical words `ScriptHide` / `ScriptUnhide` save and write.

#include "ElysiumEntity.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

namespace
{
	// `DAT_1070d1b0 / b4 / b8`: retail's zero-vector global (three dwords written 0 by
	// `staticinit_101370b0`, 369 readers), the source of the two surrounding vectors in `FUN_100dc300`.
	const FVector GRetailZeroVector(0.0, 0.0, 0.0);

	// `m_Solid != 0 && !(m_usSolidFlags & FSOLID_NOT_SOLID)`: the "solid" boolean `FUN_100dc480`
	// computes before and after its store (`100dc493-100dc4a2`, `100dc502-100dc516`), and the
	// negation of `FUN_100dc430`'s first test.
	bool RetailIsSolid(int32 SolidType, uint32 SolidFlags)
	{
		return SolidType != 0 && (SolidFlags & 0x4u) == 0;
	}
}

// --- The collision property -------------------------------------------------------------------

void FElysiumEntity::ConstructCollisionProperty()
{
	// `FUN_100dc190` 0x100dc190: `*this = &vftable_CCollisionProperty` (no port word), the partition
	// handle `coll+0x46 = 0xffff`, then `FUN_100dc300(this, 0)`.
	PartitionHandle = 0xffff;
	InitCollisionProperty(nullptr);
}

void FElysiumEntity::InitCollisionProperty(FElysiumEntity* Owner)
{
	// `FUN_100dc300` 0x100dc300, straight-line, in retail's store order (asm `100dc300-100dc373`).
	CollisionOwner = Owner;                                   // 1. [+0x3c] = owner
	CollMins = FVector::ZeroVector;                      // 2. +0x04..+0x0c m_vecMins = 0
	CollMaxs = FVector::ZeroVector;                      //    +0x10..+0x18 m_vecMaxs = 0
	CollisionRadius = 0.f;                                    // 3. +0x48 m_flRadius = 0
	TriggerBloat = 0.f;                                       //    +0x20 m_flTriggerBloat = 0
	RetailSolidFlags = 0u;                                    //    +0x44 m_usSolidFlags = 0 (16-bit store)
	RetailSolidType = 0;                                      //    +0x40 m_Solid = SOLID_NONE
	SurroundType = 0;                                         //    +0x1c m_nSurroundType = 0
	SurroundingMins = GRetailZeroVector;                      // 4. +0x4c..+0x54 <- DAT_1070d1b0/b4/b8
	SurroundingMaxs = GRetailZeroVector;                      //    +0x58..+0x60 <- DAT_1070d1b0/b4/b8
	SpecifiedSurroundingMins = FVector::ZeroVector;           // 5. +0x24..+0x2c = 0
	SpecifiedSurroundingMaxs = FVector::ZeroVector;           //    +0x30..+0x38 = 0
	// Not touched: +0x00 (the vftable) and +0x46 (the partition handle).
}

int32 FElysiumEntity::EdictIndex() const
{
	// `IndexOfEdict(this->+0x2e0)` (`VEngineServer014` slot 35, engine `0x20109110`): 0 for a NULL
	// edict and for edict 0 (the world), else the edict's index. The same rule `FElysiumDecal::EdictIndex`
	// applies: there are no edicts here, so the world is the one entity without one.
	if (Def != nullptr && Def->Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
	{
		return 0;
	}
	return Handle.IsSet() ? Handle.Index : 0;
}

void FElysiumEntity::MarkCollisionBoundsDirty()
{
	// `FUN_100dda20` 0x100dda20: `owner->m_iEFlags |= 0x14000` (`100dda23`), then `JMP` into
	// `FUN_100ddd20` (`100dda2d` -> thunk `0x1000c2e3`).
	EFlags |= 0x14000u;
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("bounds_dirty"), TEXT("FUN_100dda20"), 0x100dda23u, TEXT("write"),
			FString::Printf(TEXT("m_iEFlags=0x%x"), EFlagsWord()));
	}
	MarkPartitionHandleDirty();
}

void FElysiumEntity::MarkPartitionHandleDirty()
{
	// `FUN_100ddd20` 0x100ddd20: `idx = IndexOfEdict(owner->+0x2e0)`; `idx != 0` and
	// `(owner->m_iEFlags >> 15) & 1 == 0` -> `|= 0x8000` and `FUN_100dbc60(&DAT_106e8144, owner)` (a
	// `CUtlVector` AddToTail onto the dirty-partition list). The list is the engine partition's relink
	// input (`FUN_100dbac0`, its consumer) -- engine-replaced here (Unreal's overlap queries), as the
	// partition itself is (`audit.tsv` 0x100ddd20 / 0x100ddc40); the bit is retail's and is carried.
	const int32 Eidx = EdictIndex();
	const TCHAR* Arm = TEXT("mark");
	if (Eidx == 0)
	{
		Arm = TEXT("world");
	}
	else if ((EFlags & 0x8000u) != 0)
	{
		Arm = TEXT("already_dirty");
	}
	else
	{
		EFlags |= 0x8000u;
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("bounds_dirty"), TEXT("FUN_100ddd20"), 0x100ddd20u, TEXT("branch"),
			FString::Printf(TEXT("eidx=%d arm=%s m_iEFlags=0x%x"), Eidx, Arm, EFlagsWord()));
	}
}

void FElysiumEntity::UpdatePartitionMembership()
{
	// `FUN_100ddc40` 0x100ddc40 (`SpatialPartition001` = `DAT_1070b24c`; slot 5 `Remove(handle)`, slot 4
	// `Insert(mask, handle)`), arms in retail order. Engine-replaced: this port never holds a handle
	// (`PartitionHandle` stays `0xffff`), so every call takes the first exit; the later arms are
	// written out so the function is whole.
	const uint16 Handle16 = PartitionHandle;
	if (Handle16 == 0xffff)                                                       // 1. h == -1 -> exit
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("partition_relink"), TEXT("FUN_100ddc40"), 0x100ddc40u, TEXT("branch"),
				TEXT("handle=0xffff arm=no_handle"));
		}
		return;
	}
	// 2. slot 5 `Remove(h)`: list mask := 0 -- the engine's; nothing here.
	// 3. `e = owner->+0x2e0`; `e == 0` -> exit; `IndexOfEdict(e) == 0` -> exit.
	const int32 Eidx = EdictIndex();
	if (Eidx == 0)
	{
		return;
	}
	// 4. `P = (m_Solid == 0 || flags & 4) && !(flags & 8)`; P and `m_iEFlags & 0x40000 == 0` -> exit.
	const bool bNotSolidNotTrigger = (RetailSolidType == 0 || (RetailSolidFlags & 0x4u) != 0) && (RetailSolidFlags & 0x8u) == 0;
	if (bNotSolidNotTrigger && (EFlags & 0x40000u) == 0)
	{
		return;
	}
	// 5. slot 4 `Insert(0x10, h)`. 6. not P: `type = (flags & 4 ? 0 : 1) | (flags & 8 ? 2 : 0)`;
	//    `Insert(type, h)`. Both engine-side.
	uint32 InsertMask = 0x10u;
	if (!bNotSolidNotTrigger)
	{
		InsertMask |= ((RetailSolidFlags & 0x4u) != 0 ? 0u : 1u) | ((RetailSolidFlags & 0x8u) != 0 ? 2u : 0u);
	}
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("partition_relink"), TEXT("FUN_100ddc40"), 0x100ddc40u, TEXT("branch"),
			FString::Printf(TEXT("handle=0x%x arm=insert mask=0x%x"), Handle16, InsertMask));
	}
}

void FElysiumEntity::CheckForUntouchOnSolidChange()
{
	// `FUN_100dc430` 0x100dc430, arms in retail order (asm `100dc430-100dc464`).
	// 1. `m_Solid == 0` -> arm 2; else `flags & 4 == 0` -> return at `100dc43e` (solid).
	if (RetailIsSolid(RetailSolidType, RetailSolidFlags))
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("untouch_gate"), TEXT("FUN_100dc430"), 0x100dc43eu, TEXT("branch"),
				FString::Printf(TEXT("arm=solid m_Solid=%d m_usSolidFlags=0x%x"), RetailSolidType, RetailSolidFlags & 0xffffu));
		}
		return;
	}
	// 2. `(flags >> 3) & 1` -> return at `100dc448` (a trigger).
	if ((RetailSolidFlags & 0x8u) != 0)
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("untouch_gate"), TEXT("FUN_100dc430"), 0x100dc448u, TEXT("branch"),
				FString::Printf(TEXT("arm=trigger m_Solid=%d m_usSolidFlags=0x%x"), RetailSolidType, RetailSolidFlags & 0xffffu));
		}
		return;
	}
	// 3. `owner->vslot207()` (`IsCurrentlyTouching`); `AL == 0` -> return at `100dc457`.
	FElysiumEntity* Owner = CollisionOwner != nullptr ? CollisionOwner : this;
	if (!Owner->IsCurrentlyTouching())
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("untouch_gate"), TEXT("FUN_100dc430"), 0x100dc457u, TEXT("branch"),
				FString::Printf(TEXT("arm=not_touching m_Solid=%d m_usSolidFlags=0x%x"), RetailSolidType, RetailSolidFlags & 0xffffu));
		}
		return;
	}
	// 4. `PUSH 1; CALL [vtbl+0x18]` (`100dc460`): `owner->SetCheckUntouch(true)`.
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("untouch_gate"), TEXT("FUN_100dc430"), 0x100dc460u, TEXT("call"),
			FString::Printf(TEXT("arm=request fn=CBaseEntity::SetCheckUntouch on=1 m_Solid=%d m_usSolidFlags=0x%x"),
				RetailSolidType, RetailSolidFlags & 0xffffu));
	}
	Owner->SetCheckUntouch(true);
}

void FElysiumEntity::SetSolid(int32 Type)
{
	// `FUN_100dc480` 0x100dc480 (`"CBaseEntity::SetSolid"` 0x1053dafc is the wrapper's scope-trace
	// label), arms in retail order (`walks/L0-r005.md:217`, `L0-r015.md` Open question 6).
	const int32 Old = RetailSolidType;
	if (Old == Type)                                                               // 100dc486: no change -> return
	{
		return;
	}
	// Port-only: `IsRetailNotSolid` reads "a SetSolid landed" (`RetailSolidSets > 0 && m_Solid == 0`)
	// to tell an explicit `SOLID_NONE` from a never-set word, because the NPC Spawn bodies write
	// `SOLID_BBOX` without this setter. Counted past the no-change exit, so the base constructor's
	// `SetSolid(0)` on the zeroed word (`1009dbe3`) does not read every entity as non-solid.
	++RetailSolidSets;
	const bool bWasSolid = RetailIsSolid(Old, RetailSolidFlags);                  // 100dc493-100dc4a2
	MarkCollisionBoundsDirty();                                                    // 100dc4a4 -> 0x100dda20
	// `SOLID_BSP` (1) under a LIVE move parent (`+0x25c` resolves) becomes `SOLID_VPHYSICS` (6).
	int32 Stored = Type;
	if (Type == 1 && World != nullptr && World->Resolve(MoveParent) != nullptr)   // 100dc4ac-100dc4e5
	{
		Stored = 6;
	}
	RetailSolidType = Stored;                                                      // 100dc4e7: the store
	// `owner->m_pPhysicsObject` (`+0x36c`) non-NULL -> its slot 25 (`[vtbl+0x64]`). There is no vphysics
	// object here (`vphysics.dll` is engine-replaced; the slot's name is UNRECOVERED), so the NULL arm.
	UpdatePartitionMembership();                                                   // 100dc4fd -> 0x100ddc40
	const bool bIsSolid = RetailIsSolid(RetailSolidType, RetailSolidFlags);       // 100dc502-100dc516
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("set_solid"), TEXT("FUN_100dc480"), 0x100dc4e7u, TEXT("write"),
			FString::Printf(TEXT("old=%d new=%d m_Solid=%d was_solid=%d is_solid=%d"), Old, Type, Stored,
				bWasSolid ? 1 : 0, bIsSolid ? 1 : 0));
	}
	if (bWasSolid != bIsSolid)                                                     // 100dc518-100dc51f: the flip
	{
		CheckForUntouchOnSolidChange();                                            // -> 0x100dc430
	}
}

void FElysiumEntity::SetSolidFlags(uint16 Word)
{
	// `FUN_100dc580` 0x100dc580 (`"CBaseEntity::SetSolidFlags"` 0x10548fb4 is the wrapper's scope-trace
	// label), arms in retail order (asm `100dc580-100dc5e7`).
	const uint32 Old = RetailSolidFlags & 0xffffu;                                // 0. ESI = old (zero-extended)
	RetailSolidFlags = Word;                                                       //    100dc58e: the 16-bit store
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("solid_flags"), TEXT("FUN_100dc580"), 0x100dc58eu, TEXT("write"),
			FString::Printf(TEXT("old=0x%x new=0x%x"), Old, static_cast<uint32>(Word)));
	}
	if (Old == Word)                                                               // 1. 100dc599 JZ: nothing else runs
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("solid_flags"), TEXT("FUN_100dc580"), 0x100dc599u, TEXT("branch"),
				TEXT("arm=unchanged"));
		}
		return;
	}
	const uint32 Changed = Word ^ Old;
	if ((Changed & 0x180u) != 0)                                                   // 2. 100dc5a2 / 100dc5a4
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("solid_flags"), TEXT("FUN_100dc580"), 0x100dc5a4u, TEXT("call"),
				FString::Printf(TEXT("arm=bounds_dirty fn=FUN_100dda20 changed=0x%x"), Changed & 0x180u));
		}
		MarkCollisionBoundsDirty();
	}
	if ((Changed & 0x4u) != 0)                                                     // 3. 100dc5b6 / 100dc5c3 / 100dc5c7
	{
		// `[coll+0x3c]->m_pPhysicsObject` (`+0x36c`) NULL -> `JZ 100dc5c3`; else `CALL [vtbl+0x64]` (slot 25,
		// no args, result unused). No vphysics object exists here (engine-replaced; the slot's name is
		// UNRECOVERED, `vphysics.dll` is not in the corpus), so the NULL arm.
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("solid_flags"), TEXT("FUN_100dc580"), 0x100dc5c3u, TEXT("branch"),
				TEXT("arm=no_physics_object"));
		}
	}
	if ((Changed & 0xcu) != 0)                                                     // 4. 100dc5d5 / 100dc5d9 / 100dc5e0
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("solid_flags"), TEXT("FUN_100dc580"), 0x100dc5d9u, TEXT("call"),
				FString::Printf(TEXT("arm=relink_and_gate fn=FUN_100ddc40 then=FUN_100dc430 changed=0x%x"), Changed & 0xcu));
		}
		UpdatePartitionMembership();                                               // 100dc5d9 -> 0x100ddc40
		CheckForUntouchOnSolidChange();                                            // 100dc5e0 -> 0x100dc430
	}
}

// --- The flag word: slots 83 / 84 --------------------------------------------------------------

// slot 83 0x100b4ef0 `int GetEFlags()`: `MOV EAX,[ECX+0x268]; RET` -- the whole word. The two bits this
// port carries beside the word (`EFL_DORMANT` 0x2 in `bEflDormant`, `EFL_KILLME` 0x1 in `bDead`) are
// folded in so the answer is the complete `m_iEFlags`.
int32 FElysiumEntity::GetEFlags()
{
	const uint32 Word = EFlagsWord();
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("eflags_word"), TEXT("CBaseEntity::GetEFlags"), 0x100b4ef0u, TEXT("return"),
			FString::Printf(TEXT("m_iEFlags=0x%x"), Word));
	}
	return static_cast<int32>(Word);
}

// slot 84 0x100b4f10 `void SetEFlags(int)`: `MOV EAX,[ESP+4]; MOV [ECX+0x268],EAX; RET 4` -- a whole-dword
// replace, no mask, no OR, and no enqueue even with `0x1000000` in the argument. `EFL_DORMANT` lands in
// `bEflDormant`. **Divergence (named):** `EFL_KILLME` is not written: `bDead` is the world's pending
// kill (`UTIL_Remove 0x101cd940`'s port), and a bare bit cannot make the world reap the slot; no
// entity-side caller in the corpus (`CServerNetworkProperty::vfunc3`, `FUN_10140330`, `FUN_10183470`)
// passes a word with bit 0 set on a live entity.
void FElysiumEntity::SetEFlags(int32 Word)
{
	const uint32 Unsigned = static_cast<uint32>(Word);
	EFlags = Unsigned & ~0x3u;
	bEflDormant = (Unsigned & 0x2u) != 0;
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("eflags_word"), TEXT("CBaseEntity::SetEFlags"), 0x100b4f14u, TEXT("write"),
			FString::Printf(TEXT("m_iEFlags=0x%x"), Unsigned));
	}
}

// --- The untouch contract: slots 6 / 207 and the per-entity check -------------------------------

// slot 6 0x100b11d0 `void SetCheckUntouch(bool)`, arms in retail order (asm `100b11d0-100b128e`). The
// scope-trace frame (`"CBaseEntity::SetCheckUntouch"` 0x105589e0 / `"NULL ENTITY"` 0x105387dc /
// `m_iName`) is diagnostic and writes no game state.
void FElysiumEntity::SetCheckUntouch(bool bOn)
{
	if (!bOn)
	{
		// 1. `100b127d`: `m_iEFlags &= 0xfeffffff`. The stamp is not touched.
		EFlags &= ~0x1000000u;
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("check_untouch"), TEXT("CBaseEntity::SetCheckUntouch"), 0x100b127du, TEXT("write"),
				FString::Printf(TEXT("arm=clear m_iEFlags=0x%x m_touchStamp=%d"), EFlagsWord(), TouchStamp));
		}
		return;
	}
	// 2a. `100b1251`: `m_touchStamp += 1`, always, before the pending test (the pending byte was read
	//     at `100b1242`, before the increment; the two are independent).
	++TouchStamp;
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("check_untouch"), TEXT("CBaseEntity::SetCheckUntouch"), 0x100b1251u, TEXT("write"),
			FString::Printf(TEXT("m_touchStamp=%d"), TouchStamp));
	}
	// 2b. `100b1257 JNZ`: byte `+0x26b & 1` (bit `0x1000000`) set -> exit at `100b1287`, no enqueue.
	if ((EFlags & 0x1000000u) != 0)
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("check_untouch"), TEXT("CBaseEntity::SetCheckUntouch"), 0x100b1287u, TEXT("branch"),
				FString::Printf(TEXT("arm=pending m_iEFlags=0x%x m_touchStamp=%d"), EFlagsWord(), TouchStamp));
		}
		return;
	}
	// 2c. `100b1259`: `m_iEFlags |= 0x1000000`; `100b126b`: `FUN_100f8e20(this)` (thunk `0x10011a9a`).
	EFlags |= 0x1000000u;
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("check_untouch"), TEXT("CBaseEntity::SetCheckUntouch"), 0x100b126bu, TEXT("call"),
			FString::Printf(TEXT("arm=enqueue fn=FUN_100f8e20 m_iEFlags=0x%x m_touchStamp=%d"), EFlagsWord(), TouchStamp));
		World->EnqueueUntouchCheck(*this);
	}
}

// slot 207 0x1003d3d0 `bool IsCurrentlyTouching() const`: the scope-trace frame
// (`"CBaseEntity::IsCurrentlyTouching"` 0x1053bbb0), then `HasDataObjectType(this, 1)` -- true iff
// the entity owns a touchlink list, the same type-1 data object `PhysicsCheckForEntityUntouch`
// fetches with `GetDataObject(this, 1)`.
bool FElysiumEntity::IsCurrentlyTouching() const
{
	return World != nullptr && World->EntityHasTouchLinks(Handle);
}

void FElysiumEntity::PhysicsCheckForEntityUntouch()
{
	// `CBaseEntity::PhysicsCheckForEntityUntouch` 0x1003d490 (scope-trace label 0x1053bbd8). The link
	// walk is the world's (`ExpireStaleTouchLinks`: a link whose stamp is not `m_touchStamp` ends,
	// `1003d4f0`); an emptied list is destroyed (`DestroyDataObject(this, 1)`, `1003d5b9`) -- here the
	// pairs' absence IS that; then `PUSH 0; CALL [vtbl+0x18]` (`1003d5c9`): `SetCheckUntouch(false)`.
	if (World == nullptr)
	{
		return;
	}
	const int32 Expired = World->ExpireStaleTouchLinks(*this);
	World->EmitRetailSite(*this, TEXT("untouch_check"), TEXT("CBaseEntity::PhysicsCheckForEntityUntouch"), 0x1003d5c9u,
		TEXT("call"), FString::Printf(TEXT("fn=CBaseEntity::SetCheckUntouch on=0 expired=%d touching=%d m_touchStamp=%d"),
			Expired, IsCurrentlyTouching() ? 1 : 0, TouchStamp));
	SetCheckUntouch(false);
}

// --- The physical words `ScriptHide` / `ScriptUnhide` save and write ---------------------------

bool FElysiumEntity::ReadScriptPhysicalWords(int32& OutSolid, int32& OutMoveType, int32& OutMoveCollide,
	int32& OutSolidFlags, int32& OutEffects) const
{
	// `CBaseEntity::ScriptHide` 0x100a8710: `m_ScriptSavedSolid = GetSolid()` (slot 92, `[vtbl+0x170]`),
	// `m_ScriptSavedMoveCollide = m_MoveCollide`, `m_ScriptSavedMoveType = m_MoveType`,
	// `m_ScriptSavedSolidFlags = GetSolidFlags()` (slot 211, `[vtbl+0x34c]`), `m_fScriptSavedEffects =
	// m_fEffects`. The base carries no `m_fEffects` word (the NPC kernel's `EffectsWord` does; its
	// override answers it), so that one reads 0 here.
	OutSolid = RetailSolidType;
	OutMoveType = GetMoveType();
	OutMoveCollide = RetailMoveCollide;
	OutSolidFlags = static_cast<int32>(RetailSolidFlags & 0xffffu);
	OutEffects = 0;
	return true;
}

void FElysiumEntity::WriteScriptPhysicalWords(int32 InSolid, int32 InMoveType, int32 InMoveCollide,
	int32 InSolidFlags, int32 InEffects)
{
	// `ScriptHide` 0x100a8710: `SetMoveType(0, 0)` (slot 93), `SetSolid(0)` (`FUN_100dc480`),
	// `SetSolidFlags(4)` (`FUN_100dc580`), `m_fEffects = 0xe0`. `ScriptUnhide` 0x100a8990:
	// `SetSolid(saved)`, `SetMoveType(saved, saved)`, `SetSolidFlags(saved)`, `m_fEffects = saved`.
	// The two collision setters run their change tails (the `0x100dc430` untouch gate among them);
	// `SetMoveType` touches neither word, so its place in the order has no observable here.
	(void)InEffects;   // no base `m_fEffects` word (above)
	SetSolid(InSolid);                                                    // 0x100dc480
	RetailMoveType = InMoveType;                                          // slot 93's +0x158 word
	RetailMoveCollide = InMoveCollide;                                    // +0x159
	SetSolidFlags(static_cast<uint16>(static_cast<uint32>(InSolidFlags) & 0xffffu));   // 0x100dc580
}
