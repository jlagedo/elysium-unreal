#pragma once

#include "CoreMinimal.h"
#include "ElysiumSaveRestoreBlocks.h"

// Map-transition save state (L0-r030, `docs/specs/layers/L0-entity/walks/L0-r030.md`): the three
// retail pieces a level change moves through, all vampire.dll unless an engine.dll address says so.
//
//   1. The global-entity table `DAT_106be530` (Source's CGlobalState): a prepend-linked list of
//      0x68-byte nodes -- name[64] +0x00, levelName[32] +0x40, state +0x60, next +0x64 -- with a
//      count word at table +0x04. Written by `FUN_10057740` (AddGlobal), searched by `FUN_100576a0`
//      (FindGlobal, case-insensitive), cleared by `FUN_100579f0` -> `FUN_10057680`. Serialized as the
//      "GLOBAL" group (one field, `m_listCount`) and one "GENT" record a node by `FUN_10057850`;
//      rebuilt from the same bytes by `FUN_100578e0`. CServerGameDLL slots 15 / 16 (`0x1011b040` /
//      `0x1011b060`) wrap them in a CSave / CRestore over the engine's save data (`FUN_10057a30` /
//      `FUN_10057ac0`); the engine calls slot 15 after the GameHeader group of a save
//      (`CSaveRestore::vfunc12` 0x20095980) and slot 16 after reading it (0x20095f14).
//      `DispatchSpawn` 0x101d1280 is the table's spawn-time reader: a `globalname` entity is added
//      on its first spawn, removed when its record is GLOBAL_DEAD, made dormant when another map
//      owns it.
//   2. The adjacency table on the save data (`save+0x18` connectionCount, `save+0x1c` sixty 0x50-byte
//      rows: mapName[32], landmarkName[32], pentLandmark +0x40, vecLandmarkOrigin +0x44), built by
//      CServerGameDLL slot 23 `BuildAdjacentMapList` 0x1011b9f0 -> `FUN_101c7ff0` from every
//      `trigger_changelevel` and its `info_landmark`, de-duplicated on (landmark, map) by
//      `FUN_101c7e00`; the same walk marks the entity-table rows an entity may cross with (the
//      FENTTABLE_MOVEABLE / FENTTABLE_GLOBAL words and one bit a transition).
//   3. CServerGameDLL slot 22 `CreateEntityTransitionList` 0x1011b590 -> `FUN_101a3c40`: the rows of a
//      saved map selected by the engine's transition mask become the entities that arrive with the
//      player; the four non-entity blocks' slot 7 run when any row moved, their slot 8 always.
//
// Everything below is cited at its line. What the port keeps of retail: every arm in order, the
// constants, the counts, the flag words and the tombstone the engine writes back (`.HL3`, the
// snapshot's `AbsentEntities`). Named modernizations are stated where they sit.

class FElysiumEntityWorld;
class FElysiumEntity;
struct FElysiumMapSnapshot;
struct IElysiumRetailSiteSink;

namespace ElysiumTransitionState
{
	using ElysiumSaveRestore::FSave;
	using ElysiumSaveRestore::FRestore;
	using ElysiumSaveRestore::FSaveRestoreData;

	// --- the global-entity table (DAT_106be530) -----------------------------------------------------

	// The retail state words `FUN_10057740` stores and `FUN_101a3c40` / `DispatchSpawn` test
	// (Source's GLOBAL_OFF / GLOBAL_ON / GLOBAL_DEAD).
	constexpr int32 GlobalStateOff = 0;
	constexpr int32 GlobalStateOn = 1;
	constexpr int32 GlobalStateDead = 2;

	// One node's saved prefix: the "GENT" typedescription 0x1053fc80 (3 fields, flags 0x2, stride
	// 0x2c): `name` CHARACTER[64] +0x00, `levelName` CHARACTER[32] +0x40, `state` INTEGER +0x60. The
	// chain link +0x64 is not saved.
	struct FGlobalRecord
	{
		char Name[64];
		char LevelName[32];
		int32 State = GlobalStateOff;
	};
	static_assert(sizeof(FGlobalRecord) == 0x64, "a GENT record is 0x64 bytes");

	class FGlobalState
	{
	public:
		// FUN_10057740 (AddGlobal): calloc a node, prepend it, Q_strncpy the two strings (NULL -> ""),
		// store the state, count += 1. No duplicate check, no return value.
		void Add(const char* Name, const char* LevelName, int32 State, IElysiumRetailSiteSink* Sites = nullptr);
		// FUN_100576a0 (FindGlobal): NULL -> NULL; else the first node whose name `__strcmpi`s equal.
		const FGlobalRecord* Find(const char* Name) const;
		FGlobalRecord* Find(const char* Name);
		// FUN_100579f0: free every node along +0x64, then FUN_10057680: head := 0, count := 0.
		void Clear();
		// FUN_10057850: the "GLOBAL" header (`m_listCount`) and `min(count, chain)` "GENT" records.
		int32 Save(FSave& S, IElysiumRetailSiteSink* Sites) const;
		// FUN_100578e0: clear, the header, count := 0, then one AddGlobal per record read.
		int32 Restore(FRestore& R, IElysiumRetailSiteSink* Sites);

		int32 Count() const { return ListCount; }                 // table +0x04
		const TArray<FGlobalRecord>& Nodes() const { return Chain; } // head first (index 0 = table +0x00)

	private:
		TArray<FGlobalRecord> Chain;   // the linked list, head at index 0 (prepend = insert at 0)
		int32 ListCount = 0;           // `m_listCount`, written by AddGlobal, FUN_10057680 and the restore
	};

	// The static table 0x106be530.
	FGlobalState& GlobalStateTable();

	// CServerGameDLL slot 15 `vfunc15` 0x1011b040 -> FUN_10057a30: CSave over `Data`, FUN_10057850
	// (result dropped), teardown. `Data` null is the `save == 0` shape (the adapter wraps nothing).
	void SaveGlobalState(FSaveRestoreData* Data);
	// CServerGameDLL slot 16 `vfunc16` 0x1011b060 -> FUN_10057ac0: CRestore over `Data`, FUN_100578e0
	// (result dropped), teardown.
	void RestoreGlobalState(FSaveRestoreData* Data);
	// CServerGameDLL slot 7 `vfunc7` 0x10057b50, its first arm: FUN_100579f0 on the table. The byte
	// 0x10580ae8 := 1 and the tail call through 0x101bdb50's slot 0 are unrecovered (walk Open 10).
	void ResetGlobalState();

	// The `globalname` arm of `DispatchSpawn` 0x101d1280, after PostSpawn: with `m_iGlobalname` set,
	// FindGlobal; absent -> AddGlobal(name, current map, GLOBAL_ON); present and GLOBAL_DEAD -> the
	// caller removes the entity (DispatchSpawn returns -1); present and another map's -> MakeDormant.
	// Returns -1 for the removal arm, 0 otherwise.
	int32 DispatchSpawnGlobalArm(FElysiumEntityWorld& World, FElysiumEntity& Entity);

	// --- the adjacency table and the transition flags ----------------------------------------------

	// Entity-table flag words (row +0x20 low, +0x24 high; `docs/vtmb/savegame_format.md`).
	constexpr uint32 EntTablePlayer = 0x80000000u;    // FENTTABLE_PLAYER
	constexpr uint32 EntTableRemoved = 0x40000000u;   // FENTTABLE_REMOVED: transferred out (the `.HL3` rows)
	constexpr uint32 EntTableMoveable = 0x20000000u;  // FENTTABLE_MOVEABLE: may cross a transition
	constexpr uint32 EntTableGlobal = 0x10000000u;    // FENTTABLE_GLOBAL: a `globalname` entity, merged

	// CServerGameDLL slot 23 `BuildAdjacentMapList(oldLevel, landmarkName)` 0x1011b9f0: when the save
	// data exists, `save+0x18 := FUN_101c7ff0(save+0x1c, 0x3c, oldLevel, landmarkName)`. Both strings
	// may be NULL (the save path, engine 0x200964ae); the transition path (engine 0x20097d73) passes
	// the level being left and the landmark crossed. Scans `Data.World`.
	void BuildAdjacentMapList(FSaveRestoreData& Data, const char* OldLevel, const char* LandmarkName);

	// --- the transition list -----------------------------------------------------------------------

	// CServerGameDLL slot 22 `CreateEntityTransitionList(save, maskLo, maskHi)` 0x1011b590:
	// `rc = FUN_101a3c40(save, lo, hi)`; when rc != 0 a CRestore over `save` and slot 7 of the
	// EventQueue, Physics, AI and Python blocks with (adapter, 0, 0); then slot 8 of the four, always.
	// Returns rc. `Destination` is the world the rows arrive in (retail: the one server).
	int32 CreateEntityTransitionList(FSaveRestoreData& Data, uint32 MaskLo, uint32 MaskHi, FElysiumEntityWorld& Destination);

	// The engine's EntityPatchWrite (engine.dll 0x200973c0, reached from CSaveRestore::vfunc10 when
	// slot 22 answered non-zero): the `.HL3` list -- every row carrying FENTTABLE_REMOVED -- written
	// beside the saved map. The port's `.HL3` is the snapshot's `AbsentEntities`.
	int32 EntityPatchWrite(const FSaveRestoreData& Data, FElysiumMapSnapshot& Snapshot);
	// Its read half (EntityPatchRead): the listed rows are marked FENTTABLE_REMOVED before the mask
	// test, so an entity that already left is not moved twice.
	void EntityPatchRead(FSaveRestoreData& Data, const FElysiumMapSnapshot& Snapshot);

	// The saved maps a transition can draw from (the engine reads `.HL1` files; the port keeps the
	// visited maps' snapshots in the session).
	struct ISnapshotStore
	{
		virtual ~ISnapshotStore() = default;
		virtual FElysiumMapSnapshot* FindMutable(const FString& Map) = 0;
	};

	// The engine's level-transition load (CSaveRestore::vfunc10 0x20097d00, its shape Source's
	// `LoadAdjacentEnts`): slot 23 on the current level with (oldLevel, landmarkName); then for each
	// adjacency row's map, once, with a saved snapshot: slot 20 over that save, the mask of ITS
	// adjacency rows whose map is the current level (`1 << (j & 31)` sign-extended, engine
	// 0x20098130), slot 22 when the mask is non-zero, EntityPatchWrite when rows moved. The engine's
	// own steps between the slot calls are unrecovered beyond this shape. Returns the rows moved.
	int32 EngineLoadAdjacentEnts(FElysiumEntityWorld& World, ISnapshotStore& Store, const FString& OldLevel,
		const FString& LandmarkName, IElysiumRetailSiteSink* Sites);
}
