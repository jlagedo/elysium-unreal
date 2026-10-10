// `CBaseEntity`'s collision property and its untouch contract (`walks/L0-r015.md`): the
// `CCollisionProperty` initializer and setters (`FUN_100dc190`, `FUN_100dc300`, `FUN_100dc480`,
// `FUN_100dc580`, `FUN_100dda20`, `FUN_100ddd20`, `FUN_100ddc40`, `FUN_100dc430`), the flag-word
// accessors (slots 83 / 84), `SetCheckUntouch` (slot 6), `IsCurrentlyTouching` (slot 207),
// `PhysicsCheckForEntityUntouch`, and the physical words `ScriptHide` / `ScriptUnhide` save and write.

#include "ElysiumEntity.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"   // ElysiumMove::U, the Source inch in cm
#include "Substrate/ElysiumDataObjects.h"   // type 1, the touch-link head slot 207 asks for (L0-r019)

DEFINE_LOG_CATEGORY_STATIC(LogElysiumEntityCollision, Log, All);

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
	// `IndexOfEdict(this->+0x2e0)` (`VEngineServer014` slot 35, engine `0x20109110`: `if (edict ==
	// NULL) return 0; return (edict - sv.edicts) / 0x78`): 0 for a NULL edict and for edict 0 (the
	// world), else the edict's index. `+0x2e0` is NULL for the whole base constructor (`FUN_101ab590`
	// zeroes it at `1009da5e` and `1009db12`; `CreateEntityByName` attaches the edict after the
	// constructor returns and before the keyvalues) -- `bEdictAttached`. The same rule
	// `FElysiumDecal::EdictIndex` applies: there are no edicts here, so the world is the one entity
	// without one.
	if (!bEdictAttached)
	{
		return 0;
	}
	if (Def != nullptr && Def->Classname.Equals(TEXT("worldspawn"), ESearchCase::IgnoreCase))
	{
		return 0;
	}
	return Handle.IsSet() ? Handle.Index : 0;
}

FString FElysiumEntity::RetailVectorText(const FVector& V)
{
	return FString::Printf(TEXT("%g,%g,%g"), static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z));
}

void FElysiumEntity::SetCollisionBounds(const FVector& MinsUnits, const FVector& MaxsUnits)
{
	// `CBaseEntity::SetCollisionBounds` 0x1009edc0: the scope-trace frame (`"CBaseEntity::
	// SetCollisionBounds"` 0x10555610, `m_iName` / `""` / `"NULL ENTITY"`) around
	// `thunk_FUN_100dc770(&m_Collision, mins, maxs)`; no game state of its own.
	//
	// `FUN_100dc770` 0x100dc770 (116 B, one path, `RET 8`), in retail's store order:
	// 1. `cp+0x04..0x0c <- mins` (`100dc779-100dc785`); 2. `cp+0x10..0x18 <- maxs` (`100dc78f-100dc79b`).
	//    The f32 words, as the entity's `m_vecMins` / `m_vecMaxs`.
	const FVector Mins(static_cast<float>(MinsUnits.X), static_cast<float>(MinsUnits.Y), static_cast<float>(MinsUnits.Z));
	const FVector Maxs(static_cast<float>(MaxsUnits.X), static_cast<float>(MaxsUnits.Y), static_cast<float>(MaxsUnits.Z));
	CollMins = Mins;
	CollMaxs = Maxs;
	// 3. `d = maxs - mins` per axis in x87 (`100dc79e-100dc7ab`), then `S = (dz*dz + dx*dx) + dy*dy` in
	//    extended precision (`100dc7ae-100dc7bc`; the asm grouping, not the decompiler's).
	const double Dx = static_cast<double>(static_cast<float>(Maxs.X)) - static_cast<double>(static_cast<float>(Mins.X));
	const double Dy = static_cast<double>(static_cast<float>(Maxs.Y)) - static_cast<double>(static_cast<float>(Mins.Y));
	const double Dz = static_cast<double>(static_cast<float>(Maxs.Z)) - static_cast<double>(static_cast<float>(Mins.Z));
	// 4. `FSTP float [ESP]` (`100dc7be`): S rounded to f32 as the argument of `[0x10579660]` ->
	//    `FUN_101371d0` = `FLD; FSQRT; RET` (`float10 sqrt(float)`).
	const float S32 = static_cast<float>((Dz * Dz + Dx * Dx) + Dy * Dy);
	// 5. `FMUL float [0x104454d0]` (0.5f, `100dc7cd`); `FSTP f32 -> cp+0x48` (`100dc7d8`): `m_flRadius`.
	//    UNRECOVERED: the x87 precision-control word at run time (the 53-bit default is assumed); it
	//    decides the last bit only.
	CollisionRadius = static_cast<float>(0.5 * FMath::Sqrt(static_cast<double>(S32)));
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("entity_bounds"), TEXT("FUN_100dc770"), 0x100dc7d8u, TEXT("write"),
			FString::Printf(TEXT("mins=%s maxs=%s m_flRadius=%.8g edict=%d"), *RetailVectorText(CollMins),
				*RetailVectorText(CollMaxs), CollisionRadius, EdictIndex()));
	}
	// 6. `CALL thunk_FUN_100dda20` (`100dc7db`), ECX = cp: the `0x14000` OR and `FUN_100ddd20`'s edict gate.
	MarkCollisionBoundsDirty();
	// `m_vecSize` (`+0x38c`) is NOT written here: only `UTIL_SetSize`'s slot-213 call does that.
}

void FElysiumEntity::UtilSetSize(const FVector& MinsUnits, const FVector& MaxsUnits)
{
	// `UTIL_SetSize` `FUN_101cf3c0` 0x101cf3c0 (`void __cdecl (CBaseEntity*, const float* mins, const
	// float* maxs, int unused)`; `FUN_101cf390` 0x101cf390 is the 3-argument wrapper passing 0,
	// `UTIL_SetModel` 0x101cf4a0 passes 1; the fourth argument is never read), arms in retail order.
	// 1. Per axis x, y, z: `if (maxs[i] < mins[i]) Error("backwards mins/maxs")` (`101cf3dd-101cf405`;
	//    the call is `[0x109f366c]`, the engine's fatal `Error`). Retail dies here; this port logs the
	//    fault, reports it and refuses the call -- the one divergence, on a path no shipped program
	//    survives.
	const float M[3] = { static_cast<float>(MinsUnits.X), static_cast<float>(MinsUnits.Y), static_cast<float>(MinsUnits.Z) };
	const float X[3] = { static_cast<float>(MaxsUnits.X), static_cast<float>(MaxsUnits.Y), static_cast<float>(MaxsUnits.Z) };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (X[Axis] < M[Axis])
		{
			UE_LOG(LogElysiumEntityCollision, Error, TEXT("%s: backwards mins/maxs (axis %d: mins %g maxs %g) -- "
				"retail Error() 0x101cf3f6"), *DebugString(), Axis, M[Axis], X[Axis]);
			if (World != nullptr)
			{
				World->EmitRetailSite(*this, TEXT("util_setsize"), TEXT("FUN_101cf3c0"), 0x101cf3f6u, TEXT("branch"),
					FString::Printf(TEXT("arm=backwards_mins_maxs axis=%d mins=%s maxs=%s fn=Error"), Axis,
						*RetailVectorText(FVector(M[0], M[1], M[2])), *RetailVectorText(FVector(X[0], X[1], X[2]))));
			}
			return;
		}
	}
	// 2. `SetCollisionBounds(ent, mins, maxs)` (`101cf40f`, call 0x100157c1 -> 0x1009edc0).
	SetCollisionBounds(FVector(M[0], M[1], M[2]), FVector(X[0], X[1], X[2]));
	// 3. `size = maxs - mins` (three x87 subtracts stored as f32, `101cf41a-101cf449`), then
	//    `ent->vtable[0x354/4 = 213](&size)` (`101cf459`): slot 213 `SetSize` through the dispatch.
	const FVector Size(X[0] - M[0], X[1] - M[1], X[2] - M[2]);
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("util_setsize"), TEXT("FUN_101cf3c0"), 0x101cf459u, TEXT("call"),
			FString::Printf(TEXT("fn=CBaseEntity::SetSize size=%s mins=%s maxs=%s"), *RetailVectorText(Size),
				*RetailVectorText(FVector(M[0], M[1], M[2])), *RetailVectorText(FVector(X[0], X[1], X[2]))));
	}
	SetSize(Size);
}

void FElysiumEntity::UtilSetModel(const FString& Name)
{
	// `UTIL_SetModel` `FUN_101cf4a0` 0x101cf4a0 (asm `101cf4a0-101cf577`), arms in retail order.
	// 1. `name == NULL || *name == 0` -> `RET` (`101cf4ab` / `101cf4b4`): the entity is not touched.
	if (Name.IsEmpty())
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("util_setmodel"), TEXT("FUN_101cf4a0"), 0x101cf4b4u, TEXT("branch"),
				TEXT("arm=empty_name"));
		}
		return;
	}
	// 2. `VModelInfoServer001` slot 12 (`+0x30`): the model index; negative -> `Error("no precache: %s")`.
	//    3. the "passed a va string" check (`0x10002383`). 4. `SetModelIndex(idx)` (slot 10, `101cf505`)
	//    and `SetModelName(name)` (slot 212, `101cf515`). The index is the engine's precache slot (no
	//    port word, UNBOUND); the name is `Model`, already the authored key.
	// 5. `modelinfo->GetModel(idx)` (slot 2, `101cf524`): non-NULL -> `GetModelBounds(model, mins, maxs)`
	//    (slot 3, `101cf53f`) and `UTIL_SetSize(this, mins, maxs, 1)` (`101cf54f`); NULL ->
	//    `UTIL_SetSize(this, vec3_origin, vec3_origin, 1)` (`101cf56a`). The model table here is the
	//    def's baked hulls: their AABB in cm on the Unreal axes, turned into Source units with the Y
	//    reflection (as `KernelHullTrace` and the NPC motor turn a hull the other way).
	FBox LocalCm(ForceInit);
	if (Def != nullptr)
	{
		for (const FElysiumConvexHull& Hull : Def->Hulls)
		{
			for (const FVector& V : Hull.Vertices)
			{
				LocalCm += V;
			}
		}
	}
	if (!LocalCm.IsValid)
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("util_setmodel"), TEXT("FUN_101cf4a0"), 0x101cf56au, TEXT("call"),
				FString::Printf(TEXT("arm=null_model model=%s fn=FUN_101cf3c0 mins=0,0,0 maxs=0,0,0"), *Name));
		}
		UtilSetSize(FVector::ZeroVector, FVector::ZeroVector);                       // 101cf56a: DAT_1070d1b0 twice
		return;
	}
	const FVector MinsUnits(LocalCm.Min.X / ElysiumMove::U, -LocalCm.Max.Y / ElysiumMove::U, LocalCm.Min.Z / ElysiumMove::U);
	const FVector MaxsUnits(LocalCm.Max.X / ElysiumMove::U, -LocalCm.Min.Y / ElysiumMove::U, LocalCm.Max.Z / ElysiumMove::U);
	if (World != nullptr)
	{
		World->EmitRetailSite(*this, TEXT("util_setmodel"), TEXT("FUN_101cf4a0"), 0x101cf54fu, TEXT("call"),
			FString::Printf(TEXT("arm=model_bounds model=%s fn=FUN_101cf3c0 mins=%s maxs=%s"), *Name,
				*RetailVectorText(MinsUnits), *RetailVectorText(MaxsUnits)));
	}
	UtilSetSize(MinsUnits, MaxsUnits);                                                // 101cf54f
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
		// `IndexOfEdict` answered 0: a NULL edict (the base constructor's `SetCollisionBounds`, before
		// the edict is attached; engine `0x20109110` returns 0 for NULL) or edict 0, the world.
		Arm = bEdictAttached ? TEXT("world") : TEXT("null_edict");
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
// (`"CBaseEntity::IsCurrentlyTouching"` 0x1053bbb0), then `HasDataObjectType(this, 1)` (thunk 0x10003b02,
// `TEST AL,AL` at 1003d43d) -- true iff the entity owns a type-1 data object, the touchlink list head
// `PhysicsMarkEntityAsTouched` 0x1003dc70 creates and `PhysicsCheckForEntityUntouch` 0x1003d490 /
// `PhysicsNotifyOtherOfUntouch` 0x1003d640 / `PhysicsRemoveTouchedList` 0x1003d8f0 destroy when the last
// node goes (`walks/L0-r015.md`, `walks/L0-r019.md`; the registry is `Substrate/ElysiumDataObjects.cpp`).
bool FElysiumEntity::IsCurrentlyTouching() const
{
	return HasDataObjectType(FElysiumDataObjectAccessSystem::TouchLink);
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
