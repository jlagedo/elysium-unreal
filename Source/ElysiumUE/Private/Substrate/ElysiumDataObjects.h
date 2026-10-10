// The entity data-object registry (`CDataObjectAccessSystem`, the global at 0x106bd930; story
// L0.entity_core.data-object-registry, `docs/specs/layers/L0-entity/walks/L0-r019.md`).
//
// Retail keeps per-entity side data outside the entity: a 32-slot table of accessors
// (`DAT_106bd938`, one per data-object type) and, on `CBaseEntity`, one dword mask `+0x444`
// (`m_fDataObjectTypes`, the ledger's name) saying which types this entity holds. Four accessors
// are registered once, at DLL init (`CDataObjectAccessSystem::Init` 0x1003c660 through
// `ServerGameDLL002` slot 1); nothing else writes the table. Every decompiled caller uses type 1,
// the touch-link list head.
//
// Port shape: the system is owned by `FElysiumEntityWorld` (the DLL-lifetime analogue this port
// has: `Init` in the world's constructor, `Shutdown` 0x1003c950 at its teardown) and is keyed, as
// retail's hash containers are, by the entity pointer. The six `CBaseEntity` helpers that read the
// mask and dispatch through the table are `FElysiumEntity::{Add,Has,Remove}DataObjectType` and
// `{Get,Create,Destroy}DataObject` (`ElysiumDataObjects.cpp`).
#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntity;

// `IEntityDataInstantiator` (base vtable 0x104493a8): slot 0 the deleting destructor (what
// `Shutdown` calls with 1), slot 1 `GetDataObject`, slot 2 `CreateDataObject`, slot 3
// `DestroyDataObject`, each taking the `CBaseEntity*` as the hash key.
struct IElysiumEntityDataInstantiator
{
	virtual ~IElysiumEntityDataInstantiator() = default;
	virtual void* GetDataObject(const FElysiumEntity* Entity) = 0;      // vtable slot 1
	virtual void* CreateDataObject(const FElysiumEntity* Entity) = 0;   // vtable slot 2
	virtual void DestroyDataObject(const FElysiumEntity* Entity) = 0;   // vtable slot 3
	// The derived vtable retail installs after the base ctor (0x10449378 / 0x10449390 / 0x10449360 /
	// 0x10449348): what a `retail_site` payload names as `acc=`.
	virtual uint32 RetailVtable() const = 0;
	// Live entries (port-only: the tests and the teardown audit read it).
	virtual int32 Num() const = 0;
};

// Type 1, `touchlink_t` (0x14 bytes, zero-filled by the accessor's create 0x10040970). The block
// `CreateDataObject(E, 1)` hands back IS the circular list's sentinel: `PhysicsMarkEntityAsTouched`
// 0x1003dc70 writes `H[+8] = H[+0xC] = H` straight after the create. A node is the same shape:
// `[0]` the other entity's handle (`EHANDLE`, -1 none), `[1]` the owner's `m_touchStamp` (+0x1ac)
// at link time (`0xffffffff` marks an event-driven link that never expires), `[2]` / `[3]` the
// links, `[4]` flags (`|= 1` when the begin was dispatched: the bit `PhysicsRemoveToucher`
// 0x1003d770 tests before `EndTouch`).
struct FElysiumTouchLink
{
	FElysiumEntityHandle EntityTouched = FElysiumEntityHandle::Invalid();   // +0x00
	int32 TouchStamp = 0;                                                 // +0x04
	FElysiumTouchLink* NextLink = nullptr;                                // +0x08
	FElysiumTouchLink* PrevLink = nullptr;                                // +0x0C
	int32 Flags = 0;                                                      // +0x10
};

// Type 0, `groundlink_t` (0xC bytes): the same list shape without stamp and flags. Registered by
// `Init` (accessor 0x10040c60 / 0x10040d50 / 0x10040f00); no decompiled vampire.dll path creates one.
struct FElysiumGroundLink
{
	FElysiumEntityHandle Entity = FElysiumEntityHandle::Invalid();   // +0x00
	FElysiumGroundLink* NextLink = nullptr;                        // +0x04
	FElysiumGroundLink* PrevLink = nullptr;                        // +0x08
};

// Type 2, `StepSimulationData` (0x6C bytes) and type 3, `ModelWidthScale` (0x10 bytes): registered
// by `Init` (0x10041040 / 0x10041130 / 0x100412c0 and 0x10041400 / 0x100414f0 / 0x10041680); no
// decompiled path creates either, so their layout is carried as the zero-filled block of retail's size.
struct FElysiumStepSimulationData { uint8 Bytes[0x6C] = {}; };
struct FElysiumModelWidthScale { uint8 Bytes[0x10] = {}; };

// `CDataObjectAccessSystem` 0x106bd930: `+8` the 32 accessor slots (`DAT_106bd938`), zeroed by the
// ctor 0x1003c5a0.
class FElysiumDataObjectAccessSystem
{
public:
	static constexpr int32 MaxTypes = 32;   // `CMP ESI,0x20` in every range check
	enum EType : int32
	{
		GroundLink = 0,        // `Ugroundlink_t____CEntityDataInstantiator`
		TouchLink = 1,         // `Utouchlink_t____CEntityDataInstantiator`
		StepSimulation = 2,    // `UStepSimulationData____CEntityDataInstantiator`
		ModelWidthScale = 3,   // `UModelWidthScale____CEntityDataInstantiator`
	};

	FElysiumDataObjectAccessSystem();   // 0x1003c5a0: the 32 slots zeroed
	~FElysiumDataObjectAccessSystem();  // 0x1003cae0 only unlinks the auto-system node; `Shutdown` is explicit

	// `CDataObjectAccessSystem::Init` 0x1003c660: touch-link into slot 1 (`+0xC`, only if empty),
	// ground-link into slot 0 (`+8`), step-simulation into slot 2 (`+0x10`), model-width-scale through
	// `AddDataAccessor(3, ...)`; returns 1. Reached from `ServerGameDLL002` slot 1 (1011a0c0) through
	// the auto-system walker `FUN_1042c4e0`.
	bool Init();
	// `CDataObjectAccessSystem::Shutdown` 0x1003c950: for each of the 32 slots, the accessor's vtable
	// slot 0 with 1 (the deleting destructor) when set, then the slot zeroed. Reached from
	// `ServerGameDLL002` slot 8 (1011a330) through `FUN_1042c5b0`'s reverse walk.
	void Shutdown();
	// `CDataObjectAccessSystem::AddDataAccessor` 0x1003c9f0: `if 0 <= t < 32 && slot[t] == 0 ->
	// slot[t] = obj`; otherwise nothing (retail leaks the object; the port frees it).
	void AddDataAccessor(int32 Type, TUniquePtr<IElysiumEntityDataInstantiator> Accessor);
	// `DAT_106bd938[type]`: the registered accessor, or null. The caller range-checks (every retail
	// caller does, inline, under its `IsValidType` scope label); an out-of-range type answers null here.
	IElysiumEntityDataInstantiator* Accessor(int32 Type) const;
	// `CDataObjectAccessSystem::IsValidType`'s inline test: `TEST ESI,ESI; JL` / `CMP ESI,0x20; JGE`.
	static bool IsValidType(int32 Type) { return Type >= 0 && Type < MaxTypes; }
	// Live entries across every accessor (port-only, for the teardown audit and the tests).
	int32 NumLive() const;

private:
	TUniquePtr<IElysiumEntityDataInstantiator> Slots[MaxTypes];
};
