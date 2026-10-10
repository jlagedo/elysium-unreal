// The entity data-object registry and `CBaseEntity`'s six data-object helpers (story
// L0.entity_core.data-object-registry; `docs/specs/layers/L0-entity/walks/L0-r019.md`).
//
// Every retail body here brackets itself with a diagnostic scope-trace frame (a 12-byte record of
// `"CBaseEntity::<fn>"`, `m_iName` and the empty string on the stack at `*[0x109f3620]`, depth
// `*[0x109f3680]`). The frame writes no entity state and is not reproduced; the `retail_site` taps
// stand where the measured event happens.

#include "Substrate/ElysiumDataObjects.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"

namespace
{
	// `CEntityDataInstantiator<T>` (the derived accessor, 0x28 bytes): `{vtable, hash container at
	// +4}`. The base ctor `FUN_10041c80(this+4, 0x40, 0, 0, compare, hash)` builds 0x40 buckets of
	// 0x14 bytes holding 8-byte `{key, value}` entries; for type 1 `compare` (0x100083a0 -> 0x10041b40)
	// is dword equality of the two keys and `hash` (0x10009de5 -> 0x10041b70) returns the key, so the
	// bucket is `key & 0x3f`. The key is the `CBaseEntity*`. A hash map keyed by the entity pointer is
	// the same contract (find-or-insert, the stored block returned, erase on destroy) with Unreal's
	// own container: a named modernization of the container, not of its behaviour.
	template <typename TData, uint32 RetailVtbl, uint32 RetailGet, uint32 RetailCreate, uint32 RetailDestroy>
	class TElysiumEntityDataInstantiator final : public IElysiumEntityDataInstantiator
	{
	public:
		// `GetDataObject` (vtable slot 1, type 1: 0x10040880): bucket scan; the stored block, or 0
		// when the key is absent. Reads only.
		virtual void* GetDataObject(const FElysiumEntity* Entity) override
		{
			const TUniquePtr<TData>* Found = Blocks.Find(Entity);
			return Found != nullptr ? Found->Get() : nullptr;
		}
		// `CreateDataObject` (vtable slot 2, type 1: 0x10040970): bucket scan; when the key is absent
		// insert it (`FUN_10041d20` / `FUN_10042200`) and store a new `operator_new` block (type 0 0xC,
		// type 1 0x14, type 2 0x6C, type 3 0x10 bytes) zero-filled dword by dword; an existing entry is
		// returned untouched. Either way the stored block is the result.
		virtual void* CreateDataObject(const FElysiumEntity* Entity) override
		{
			TUniquePtr<TData>& Slot = Blocks.FindOrAdd(Entity);
			if (!Slot)
			{
				Slot = MakeUnique<TData>();   // value-initialised: every word 0
			}
			return Slot.Get();
		}
		// `DestroyDataObject` (vtable slot 3, type 1: 0x10040b20): bucket scan; when found, `_free`
		// (`FUN_10430964`) the block and erase the entry (shift the bucket down, count - 1). An absent
		// key is a no-op. No result.
		virtual void DestroyDataObject(const FElysiumEntity* Entity) override
		{
			Blocks.Remove(Entity);
		}
		virtual uint32 RetailVtable() const override { return RetailVtbl; }
		virtual int32 Num() const override { return Blocks.Num(); }

	private:
		TMap<const FElysiumEntity*, TUniquePtr<TData>> Blocks;
	};

	using FGroundLinkInstantiator = TElysiumEntityDataInstantiator<FElysiumGroundLink,
		0x10449378u, 0x10040c60u, 0x10040d50u, 0x10040f00u>;
	using FTouchLinkInstantiator = TElysiumEntityDataInstantiator<FElysiumTouchLink,
		0x10449390u, 0x10040880u, 0x10040970u, 0x10040b20u>;
	using FStepSimulationInstantiator = TElysiumEntityDataInstantiator<FElysiumStepSimulationData,
		0x10449360u, 0x10041040u, 0x10041130u, 0x100412c0u>;
	using FModelWidthScaleInstantiator = TElysiumEntityDataInstantiator<FElysiumModelWidthScale,
		0x10449348u, 0x10041400u, 0x100414f0u, 0x10041680u>;
}

// --- CDataObjectAccessSystem ------------------------------------------------------------------

FElysiumDataObjectAccessSystem::FElysiumDataObjectAccessSystem()
{
	// 0x1003c5a0 (static init `staticinit_1003c580`): the 32 slots zeroed; `TUniquePtr`s are null.
}

FElysiumDataObjectAccessSystem::~FElysiumDataObjectAccessSystem()
{
	// 0x1003cae0: the dtor only removes the system from the auto-system array (`thunk_FUN_1042c2c0`);
	// the accessors are freed by `Shutdown`, which the owner calls. A system destroyed with slots
	// still set frees them here (the port's owner has no separate DLL unload).
}

bool FElysiumDataObjectAccessSystem::Init()
{
	// 0x1003c660, in retail's registration order. Each block allocates the 0x28-byte accessor, sets
	// the base vtable 0x104493a8, runs the base ctor `(0x40, 0, 0, compare, hash)` and installs the
	// derived vtable; the store is inline, "only if the slot is empty" (1003c6c0.. `if (this+0xC == 0)`).
	// Touch-link (vtable 0x10449390; compare 0x100083a0, hash 0x10009de5) into slot 1 (`+0xC`).
	if (!Slots[TouchLink])
	{
		Slots[TouchLink] = MakeUnique<FTouchLinkInstantiator>();
	}
	// Ground-link (vtable 0x10449378; compare 0x10007342, hash 0x1000148d) into slot 0 (`+8`).
	if (!Slots[GroundLink])
	{
		Slots[GroundLink] = MakeUnique<FGroundLinkInstantiator>();
	}
	// Step-simulation (vtable 0x10449360; compare 0x10014696, hash 0x1000dda0) into slot 2 (`+0x10`).
	if (!Slots[StepSimulation])
	{
		Slots[StepSimulation] = MakeUnique<FStepSimulationInstantiator>();
	}
	// Model-width-scale (vtable 0x10449348; compare 0x1000e282, hash 0x1000dffd) through
	// `AddDataAccessor(this, 3, obj)` (thunk 1000369d -> 0x1003c9f0), the one caller of that function.
	AddDataAccessor(ModelWidthScale, MakeUnique<FModelWidthScaleInstantiator>());
	return true;   // `return 1`
}

void FElysiumDataObjectAccessSystem::Shutdown()
{
	// 0x1003c950: `this += 8; for 0x20 slots: if (slot) slot->vtbl[0](1); slot = 0`.
	for (int32 Type = 0; Type < MaxTypes; ++Type)
	{
		Slots[Type].Reset();
	}
}

void FElysiumDataObjectAccessSystem::AddDataAccessor(int32 Type,
	TUniquePtr<IElysiumEntityDataInstantiator> Accessor)
{
	// 0x1003c9f0: `if (-1 < t && t < 0x20 && slot[t] == 0) slot[t] = obj`.
	if (IsValidType(Type) && !Slots[Type])
	{
		Slots[Type] = MoveTemp(Accessor);
	}
	// Otherwise retail leaks the allocated accessor; `Accessor` frees here.
}

IElysiumEntityDataInstantiator* FElysiumDataObjectAccessSystem::Accessor(int32 Type) const
{
	return IsValidType(Type) ? Slots[Type].Get() : nullptr;
}

int32 FElysiumDataObjectAccessSystem::NumLive() const
{
	int32 Total = 0;
	for (int32 Type = 0; Type < MaxTypes; ++Type)
	{
		Total += Slots[Type] ? Slots[Type]->Num() : 0;
	}
	return Total;
}

// --- CBaseEntity's six helpers -----------------------------------------------------------------

namespace
{
	// `1 << (type & 31)`: `MOV EAX,1; SHL EAX,CL`. x86 masks CL to five bits, so type 32 aliases to bit
	// 0, 33 to bit 1 and -1 to bit 31 for every mask write; the range checks below are on the signed type.
	FORCEINLINE uint32 DataObjectTypeBit(int32 Type) { return 1u << (static_cast<uint32>(Type) & 31u); }

	// The system this entity dispatches through: retail's global `DAT_106bd938`, here the owning
	// world's. An entity outside a world (a probe) finds no accessor and takes the null-slot arm.
	FORCEINLINE IElysiumEntityDataInstantiator* DataObjectAccessor(const FElysiumEntity& Entity, int32 Type)
	{
		return Entity.World != nullptr ? Entity.World->DataObjects().Accessor(Type) : nullptr;
	}

	FORCEINLINE void DataObjectSite(const FElysiumEntity& Entity, const TCHAR* Tag, const TCHAR* Fn,
		uint32 Va, const TCHAR* Phase, const FString& Payload)
	{
		if (Entity.World != nullptr)
		{
			Entity.World->EmitRetailSite(Entity, Tag, Fn, Va, Phase, Payload);
		}
	}
}

void FElysiumEntity::AddDataObjectType(int32 Type)
{
	// `CBaseEntity::AddDataObjectType` 0x1003cbc0 (143 B): after the scope frame, one write --
	// `OR ESI,EAX; MOV [EDX+0x444],ESI` (1003cc2f-1003cc3e). No range check, no null check. Sole
	// caller `CreateDataObject` (thunk 0x10009674).
	const uint32 MaskBefore = DataObjectTypes;
	DataObjectTypes |= DataObjectTypeBit(Type);
	DataObjectSite(*this, TEXT("dobj_add"), TEXT("CBaseEntity::AddDataObjectType"), 0x1003cbc0u, TEXT("write"),
		FString::Printf(TEXT("type=%d mask_before=0x%x mask_after=0x%x"), Type, MaskBefore, DataObjectTypes));
}

bool FElysiumEntity::HasDataObjectType(int32 Type) const
{
	// `CBaseEntity::HasDataObjectType` 0x1003cb00 (144 B): `TEST EAX,ESI; SETNZ AL`
	// (1003cb7a-1003cb86). Only AL is the result (the upper bytes of EAX keep the shifted bit), and
	// every caller tests AL: 1003cdc6 (Get), 1003d1a6 (Destroy), 1003d375 (DestroyAll), 1003d43d
	// (`IsCurrentlyTouching` 0x1003d3d0, slot 207, which is `HasDataObjectType(this, 1)`).
	const bool bHas = (DataObjectTypes & DataObjectTypeBit(Type)) != 0u;
	DataObjectSite(*this, TEXT("dobj_has"), TEXT("CBaseEntity::HasDataObjectType"), 0x1003cb00u, TEXT("return"),
		FString::Printf(TEXT("type=%d mask=0x%x result=%d"), Type, DataObjectTypes, bHas ? 1 : 0));
	return bHas;
}

void FElysiumEntity::RemoveDataObjectType(int32 Type)
{
	// `CBaseEntity::RemoveDataObjectType` 0x1003cc80 (145 B): `MOV EAX,1; SHL EAX,CL; NOT EAX; AND
	// ESI,EAX; MOV [EDX+0x444],ESI` (1003ccf5-1003cd00). Sole caller `DestroyDataObject` (thunk 0x1000c829).
	const uint32 MaskBefore = DataObjectTypes;
	DataObjectTypes &= ~DataObjectTypeBit(Type);
	DataObjectSite(*this, TEXT("dobj_remove"), TEXT("CBaseEntity::RemoveDataObjectType"), 0x1003cc80u, TEXT("write"),
		FString::Printf(TEXT("type=%d mask_before=0x%x mask_after=0x%x"), Type, MaskBefore, DataObjectTypes));
}

void* FElysiumEntity::GetDataObject(int32 Type)
{
	// `CBaseEntity::GetDataObject` 0x1003cd50 (385 B), arms in retail order.
	// Arm 1 (1003cdc1-1003cdcf): `if (!HasDataObjectType(type)) return 0` -- no validation, no dispatch.
	if (!HasDataObjectType(Type))
	{
		DataObjectSite(*this, TEXT("dobj_get"), TEXT("CBaseEntity::GetDataObject"), 0x1003cd50u, TEXT("branch"),
			FString::Printf(TEXT("arm=nobit type=%d result=0"), Type));
		return nullptr;
	}
	// Arm 2: the `CDataObjectAccessSystem::GetDataObject` and `::IsValidType` scope labels (no call).
	// Arm 3 (1003ce67-1003ce70): `if (type < 0 || type >= 32) return 0`, signed.
	if (!FElysiumDataObjectAccessSystem::IsValidType(Type))
	{
		DataObjectSite(*this, TEXT("dobj_get"), TEXT("CBaseEntity::GetDataObject"), 0x1003cd50u, TEXT("branch"),
			FString::Printf(TEXT("arm=range type=%d result=0"), Type));
		return nullptr;
	}
	// Arm 4 (1003ce72-1003ce82): `acc = DAT_106bd938[type]; if (acc == 0) return 0`.
	IElysiumEntityDataInstantiator* Accessor = DataObjectAccessor(*this, Type);
	if (Accessor == nullptr)
	{
		DataObjectSite(*this, TEXT("dobj_get"), TEXT("CBaseEntity::GetDataObject"), 0x1003cd50u, TEXT("branch"),
			FString::Printf(TEXT("arm=nullslot type=%d result=0"), Type));
		return nullptr;
	}
	// Arm 5 (1003ce84-1003ce91): `return acc->vtbl[1](this)` (`CALL [EAX+4]`, callee pops), EAX as is.
	void* Block = Accessor->GetDataObject(this);
	DataObjectSite(*this, TEXT("dobj_get"), TEXT("CBaseEntity::GetDataObject"), 0x1003cd50u, TEXT("branch"),
		FString::Printf(TEXT("arm=dispatch slot=1 acc=0x%08x type=%d result=%s"), Accessor->RetailVtable(), Type,
			Block != nullptr ? TEXT("block") : TEXT("0")));
	return Block;
}

void* FElysiumEntity::CreateDataObject(int32 Type)
{
	// `CBaseEntity::CreateDataObject` 0x1003cf50 (371 B), arms in retail order.
	// Arm 1 (1003cfbf-1003cfc1): `AddDataObjectType(this, type)` through thunk 0x10009674 --
	// UNCONDITIONAL, before any validation: an invalid type or one with no accessor still gets its bit.
	AddDataObjectType(Type);
	DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("write"),
		FString::Printf(TEXT("type=%d mask_after=0x%x"), Type, DataObjectTypes));
	// Arm 2: the `CDataObjectAccessSystem::CreateDataObject` / `::IsValidType` scope labels (no call).
	// Arm 3 (1003d056-1003d05f): the signed range check; invalid -> `return 0`.
	if (!FElysiumDataObjectAccessSystem::IsValidType(Type))
	{
		DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("branch"),
			FString::Printf(TEXT("arm=range type=%d"), Type));
		DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("return"),
			FString::Printf(TEXT("type=%d result=0"), Type));
		return nullptr;
	}
	// Arm 4 (1003d061-1003d06a): `acc = DAT_106bd938[type]; if (acc == 0) return 0`.
	IElysiumEntityDataInstantiator* Accessor = DataObjectAccessor(*this, Type);
	if (Accessor == nullptr)
	{
		DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("branch"),
			FString::Printf(TEXT("arm=nullslot type=%d"), Type));
		DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("return"),
			FString::Printf(TEXT("type=%d result=0"), Type));
		return nullptr;
	}
	// Arm 5 (1003d080): `return acc->vtbl[2](this)` (`CALL [EAX+8]`). The entity side has no existence
	// check, so a repeat create reaches the accessor again; the accessor is find-or-insert and hands
	// back the existing block untouched.
	DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("branch"),
		FString::Printf(TEXT("arm=dispatch slot=2 acc=0x%08x type=%d"), Accessor->RetailVtable(), Type));
	void* Block = Accessor->CreateDataObject(this);
	DataObjectSite(*this, TEXT("dobj_create"), TEXT("CBaseEntity::CreateDataObject"), 0x1003cf50u, TEXT("return"),
		FString::Printf(TEXT("type=%d result=%s"), Type, Block != nullptr ? TEXT("block") : TEXT("0")));
	return Block;
}

void FElysiumEntity::DestroyDataObject(int32 Type)
{
	// `CBaseEntity::DestroyDataObject` 0x1003d130 (361 B), arms in retail order.
	// Arm 1 (1003d1a6-1003d1ad): `if (!HasDataObjectType(type)) goto epilogue` -- no validation, no
	// call, and NO clear.
	if (!HasDataObjectType(Type))
	{
		DataObjectSite(*this, TEXT("dobj_destroy"), TEXT("CBaseEntity::DestroyDataObject"), 0x1003d130u, TEXT("branch"),
			FString::Printf(TEXT("arm=nobit type=%d"), Type));
		return;
	}
	// Arm 2: the `CDataObjectAccessSystem::DestroyDataObject` / `::IsValidType` scope labels (no call).
	// Arm 3 (1003d242-1003d247 -> 1003d28d): out of range -> skip the call, fall to arm 6.
	if (!FElysiumDataObjectAccessSystem::IsValidType(Type))
	{
		DataObjectSite(*this, TEXT("dobj_destroy"), TEXT("CBaseEntity::DestroyDataObject"), 0x1003d130u, TEXT("branch"),
			FString::Printf(TEXT("arm=range type=%d"), Type));
	}
	else if (IElysiumEntityDataInstantiator* Accessor = DataObjectAccessor(*this, Type); Accessor == nullptr)
	{
		// Arm 4 (1003d249-1003d259 -> 1003d294): no accessor -> skip the call, fall to arm 6.
		DataObjectSite(*this, TEXT("dobj_destroy"), TEXT("CBaseEntity::DestroyDataObject"), 0x1003d130u, TEXT("branch"),
			FString::Printf(TEXT("arm=nullslot type=%d"), Type));
	}
	else
	{
		// Arm 5 (1003d268): `acc->vtbl[3](this)` (`CALL [EAX+0xc]`), result discarded. The type bit is
		// STILL SET during the call: a callback that asks `HasDataObjectType` reads 1.
		DataObjectSite(*this, TEXT("dobj_destroy"), TEXT("CBaseEntity::DestroyDataObject"), 0x1003d130u, TEXT("branch"),
			FString::Printf(TEXT("arm=dispatch slot=3 acc=0x%08x type=%d"), Accessor->RetailVtable(), Type));
		Accessor->DestroyDataObject(this);
	}
	// Arm 6 (1003d26b-1003d278): `RemoveDataObjectType(this, type)` through thunk 0x1000c829 -- ALWAYS,
	// after the dispatch or after either skip: a validation failure does not prevent the clear, and a
	// callback that re-created the type is undone by it.
	RemoveDataObjectType(Type);
	DataObjectSite(*this, TEXT("dobj_destroy"), TEXT("CBaseEntity::DestroyDataObject"), 0x1003d130u, TEXT("write"),
		FString::Printf(TEXT("type=%d mask_after=0x%x"), Type, DataObjectTypes));
}
