#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

// The save/restore block framework (L0-r029, `docs/specs/layers/L0-entity/walks/L0-r029.md`): the
// registered-block protocol `CServerGameDLL` runs a map's save and restore through. All vampire.dll.
//
//   CSaveRestoreBlockSet, the static object at 0x1072bae8 (datum 0x10592db0; vftable 0x10477160;
//   ctor FUN_101a47b0 names it "Game"), holds the ISaveRestoreBlockHandler vector (+0x24..+0x34) and the
//   SaveRestoreBlockHeader_t vector (+0x40..+0x50; datamap 0x10592ce8: szName char[32], locHeader +0x20,
//   locBody +0x24, 0x28 bytes a record). Its slots 1..8 run the handlers in registration order; the
//   top-level CServerGameDLL entries (vtable slots 17..21) build a CSave / CRestore adapter over the
//   engine's save-data struct and dispatch into the set:
//     17 0x1011b890  PreSave            -> set slot 1 (a tail JMP)
//     18 0x1011b080  Save               -> set slot 2; returns 1 on every path
//     19 0x1011b8b0  WriteSaveHeaders   -> set slot 3, then set slot 4 (PostSave)
//     20 0x1011b950  ReadRestoreHeaders -> set slot 5 (PreRestore), then set slot 6
//     21 0x1011b300  Restore(p2, p3)    -> set slot 7 (p2, p3 forwarded), then set slot 8 (PostRestore)
//   The engine's order (engine.dll): save vfunc13 0x20096470 = 17, 18, test the return, 19; restore
//   vfunc9 0x200975f0 = 20 then 21; a level transition vfunc10 0x20097d00 = 20 only.
//
// What this header keeps of retail: the handler registration order, the slot call order inside every
// phase, the header/body split with each record's two offsets, the two section lengths (+0x38, +0x3c)
// and the in-place rewrite of the header section's first words, the strcmp lookup of a registered
// block in the read directory, the −1 "skip" arm, the silent miss, the cursor postconditions.
// Named modernization (decisions.md D6, `L0-save_restore`): the BYTE ENCODING of what a handler
// writes is the port's archive, not the datamap writer (`CSave::vfunc2` 0x1019f930 is unrecovered);
// a header record is written as its 0x28 raw bytes so slot 3's in-place overwrite lines up.

struct IElysiumRetailSiteSink;
class FElysiumEntityWorld;

namespace ElysiumSaveRestore
{
	// One ADJACENCY row of the save data (`save+0x1c`, stride 0x50, capacity 0x3c; L0-r030
	// `walks/L0-r030.md` 0x1011b9f0): what `FUN_101c7e00` writes.
	struct FAdjacencyRow
	{
		char MapName[32];                 // +0x00, Q_strncpy(.., 0x20) of the trigger's m_szMapName
		char LandmarkName[32];            // +0x20, the trigger's m_szLandmarkName
		FElysiumEntityHandle Landmark;    // +0x40 pentLandmark: the info_landmark's edict
		FVector LandmarkOrigin = FVector::ZeroVector; // +0x44 vecLandmarkOrigin: the landmark's slot-220 origin
	};
	constexpr int32 AdjacencyCapacity = 0x3c;   // 60 rows; `Error("Too many level transitions ...")` past it

	// One entity-table row (`save+0x1334`, stride 0x30, count `save+0x1330`), as the Entities block's
	// slot 1 `FUN_101a2c30` lays it out and the Save / header slots and `FUN_101a3c40` fill it.
	struct FEntityTableRow
	{
		int32 Id = INDEX_NONE;            // +0x00: the row index (the save id)
		int32 EdictIndex = 0;             // +0x04: the entity's index (slot 131 entindex)
		int32 RestoredEdictIndex = -1;    // +0x0c: := -1 by slot 1; the edict index after a transfer
		FElysiumEntityHandle Handle;      // +0x10: the entity's handle (slot 1), or the created one (FUN_101a3c40 pass 1)
		int32 Location = 0;               // +0x14: body offset of the entity's data
		int32 Size = 0;                   // +0x18: its byte size (0: no data)
		uint32 FlagsLo = 0;               // +0x20: one bit a transition (FUN_101c7ff0)
		uint32 FlagsHi = 0;               // +0x24: FENTTABLE_PLAYER / REMOVED / MOVEABLE / GLOBAL
		FString Classname;                // +0x28: the classname string
	};

	// The engine's save-data struct the adapters wrap (CSave::ctor FUN_1019f870 / CRestore::ctor
	// FUN_101a12a0 store it at A+0x18). Dwords as the walk reads them: [0] base, [1] cursor,
	// [2] offset, [3] size; [4]/[5] the token table (unused here: the datamap writer's, not ported).
	struct FSaveRestoreData
	{
		TArray<uint8> Bytes;           // [0] base: the buffer; the cursor [1] is base + Offset
		int32 Offset = 0;              // [2]
		int32 Size = 0;                // [3]: the allocation; `Seek` is valid below it
		// The level-transition words (L0-r030): `+0x18` connectionCount and the `+0x1c` ADJACENCY rows
		// (BuildAdjacentMapList 0x1011b9f0 writes them), and the entity table `+0x1330` / `+0x1334`
		// the Entities block's slot 1 (FUN_101a2c30) allocates and FUN_101c7ff0 / FUN_101a3c40 flag.
		int32 ConnectionCount = 0;
		TArray<FAdjacencyRow> Adjacency;
		TArray<FEntityTableRow> EntityTable;
		// FUN_1011a580's list (DAT_1070b0d0): the handles of the entities a transition transferred,
		// appended per row by FUN_101a3c40; its reader is unrecovered.
		TArray<FElysiumEntityHandle> TransferredEntities;
		// The server's entity list (`DAT_106eb5d8`) the transition walks scan: engine-side, the one
		// world; here the world this save data is about (null: no entities, as a bare buffer has).
		FElysiumEntityWorld* World = nullptr;
		// Engine-side buffer policy: the engine allocates a fixed buffer (its size is engine.dll's,
		// unrecovered). With no pinned capacity the port grows the buffer on write; a pinned capacity
		// reproduces `CSave::vfunc12`'s overflow arm (Warning, offset := size, nothing written).
		bool bGrowable = true;
		bool bReadPastEnd = false;     // port diagnostic: a read asked for bytes the buffer does not hold
		// The `retail_site` tap (null outside a recorded run) and the port's way for the static
		// handlers to reach their world (retail's handlers read globals -- gEntList, g_EventQueue).
		IElysiumRetailSiteSink* Sites = nullptr;
		void* Context = nullptr;

		void Reset(int32 Capacity = 0);
		int32 Remaining() const { return Size - Offset; }
	};

	// ISave (vftable 0x104625f4) as the block set uses it, over `CSave` (0x10476fbc).
	struct FSave
	{
		explicit FSave(FSaveRestoreData* InData) : Data(InData) {}
		int32 Tell() const;                             // slot 0 0x1019fa10: struct[2]
		void Seek(int32 Pos);                           // slot 1 0x1019fa30: 0 <= pos < size, else a silent no-op
		void WriteInt(const int32* Values, int32 Count); // slot 10 0x1019fa80: vfunc12(ptr, 4n)
		void WriteData(const void* Ptr, int32 Bytes);   // slot 12 0x101a07b0: memcpy at the cursor, or the overflow arm
		FSaveRestoreData* Data;
	};

	// IRestore (vftable 0x104626bc) as the block set uses it, over `CRestore` (0x10477084).
	struct FRestore
	{
		explicit FRestore(FSaveRestoreData* InData) : Data(InData) {}
		int32 Tell() const;                             // slot 0 0x101a1490
		void Seek(int32 Pos);                           // slot 1 0x101a14b0
		int32 ReadInt();                                // slot 14 0x101a1ea0: 4 bytes
		int32 ReadInt(int32* Out, int32 Count, int32 ByteLimit); // slot 15 0x101a2090: min(4n, limit) bytes, skip the rest, returns bytes/4
		void ReadData(void* Out, int32 Bytes);          // the raw read FUN_101a1f90 slot 15 calls (the port's row bodies)
		FSaveRestoreData* Data;
	};

	// SaveRestoreBlockHeader_t (datamap 0x10592ce8, builder 0x101a4760): 0x28 bytes, three SAVE fields.
	struct FBlockHeader
	{
		char szName[32];       // +0x00, `Q_strncpy(rec, GetBlockName(), 0x20)` in set slot 1
		int32 locHeader = -1;  // +0x20, written by set slot 3
		int32 locBody = -1;    // +0x24, written by set slot 2
	};
	static_assert(sizeof(FBlockHeader) == 0x28, "SaveRestoreBlockHeader_t is 0x28 bytes");

	// ISaveRestoreBlockHandler: slots 0..8 (4·k from the handler vtable). Retail's shared no-op bodies
	// FUN_10043b20 (1), FUN_10043b80 (4), FUN_10043ba0 (5), FUN_10043c00 (8) are the defaults here.
	class IBlockHandler
	{
	public:
		virtual ~IBlockHandler() = default;
		virtual const char* GetBlockName() const = 0;               // slot 0
		virtual void PreSave(FSaveRestoreData* /*Arg*/) {}          // slot 1
		virtual void Save(FSave& /*S*/) {}                          // slot 2
		virtual void WriteSaveHeaders(FSave& /*S*/) {}              // slot 3
		virtual void PostSave() {}                                  // slot 4
		virtual void PreRestore() {}                                // slot 5
		virtual void ReadRestoreHeaders(FRestore& /*R*/) {}         // slot 6
		virtual void Restore(FRestore& /*R*/, int32 /*P2*/, int32 /*P3*/) {} // slot 7
		virtual void PostRestore() {}                               // slot 8
	};

	// CSaveRestoreBlockSet (RTTI 0x10593270, vftable 0x10477160). Offsets name the retail fields.
	class FBlockSet
	{
	public:
		explicit FBlockSet(const char* InName = "Game");   // ctor FUN_101a47b0: `Q_strncpy(this+0x04, "Game", 0x20)`

		const char* GetName() const { return Name; }        // slot 0 0x101a4830: this+0x04
		void PreSave(FSaveRestoreData* Arg);                // slot 1 0x101a4850
		void Save(FSave& S);                                // slot 2 0x101a48e0
		void WriteSaveHeaders(FSave& S);                    // slot 3 0x101a4970
		void PostSave();                                    // slot 4 0x101a4bf0
		void PreRestore();                                  // slot 5 0x101a4c70
		void ReadRestoreHeaders(FRestore& R);               // slot 6 0x101a4cb0
		void Restore(FRestore& R, int32 P2, int32 P3);      // slot 7 0x101a4e70
		void PostRestore();                                 // slot 8 0x101a4fa0
		void AddBlockHandler(IBlockHandler* Handler);       // slot 9 0x101a5020
		void RemoveBlockHandler(IBlockHandler* Handler);    // slot 10 0x101a5100

		int32 HandlerCount() const { return Handlers.Num(); }   // +0x30
		int32 HeaderCount() const { return Headers.Num(); }     // +0x4c
		int32 HeaderSectionLength() const { return HeaderLength; } // +0x38
		int32 DataSectionLength() const { return DataLength; }     // +0x3c
		const TArray<FBlockHeader>& HeaderRecords() const { return Headers; } // +0x40, by raw offset
		const TArray<IBlockHandler*>& HandlerList() const { return Handlers; }

		// The tap for this call (port-only; the top-level entries copy it from the save-data struct).
		IElysiumRetailSiteSink* Sites = nullptr;

	private:
		void PurgeHeaders(const TCHAR* Tag, const TCHAR* Fn, uint32 Va); // the slot-4 / slot-8 tail
		int32 FindRecord(const char* BlockName) const;                  // the strcmp loop of slots 6 / 7
		void Site(const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload) const;

		char Name[32];                    // +0x04
		TArray<IBlockHandler*> Handlers;  // +0x24 memory, +0x30 size
		int32 HeaderLength = 0;           // +0x38: header-section length, `Tell − t0` at the end of slot 3
		int32 DataLength = 0;             // +0x3c: data-section length, `Tell − t0` at the end of slot 2
		TArray<FBlockHeader> Headers;     // +0x40 memory, +0x4c size; the grow marker +0x48 is 0 on the
		                                  // retail object (ctor), never −1: the vector owns its memory
	};

	// The vector ops (CUtlVectorDataOps, vftable 0x1047719c) over the header vector.
	void WriteHeaderVector(FSave& S, const TArray<FBlockHeader>& Vector); // vfunc0 0x101a5330: dword N, then N elements
	void ReadHeaderVector(FRestore& R, TArray<FBlockHeader>& Vector);     // vfunc1 0x101a5440: count, size := 0, reserve, read each

	// CServerGameDLL slots 17..21.
	void PreSave(FBlockSet& Set, FSaveRestoreData* Data);                       // slot 17 0x1011b890
	int32 Save(FBlockSet& Set, FSaveRestoreData* Data);                         // slot 18 0x1011b080, returns 1
	void WriteSaveHeaders(FBlockSet& Set, FSaveRestoreData* Data);              // slot 19 0x1011b8b0
	void ReadRestoreHeaders(FBlockSet& Set, FSaveRestoreData* Data);            // slot 20 0x1011b950
	void Restore(FBlockSet& Set, FSaveRestoreData* Data, int32 P2, int32 P3);   // slot 21 0x1011b300

	// The engine's orchestration, as engine.dll calls the slots (the engine's own work between the
	// calls -- BuildAdjacentMapList slot 23, FUN_200962c0, FUN_20097070/20097280/20097520 -- is
	// unrecovered and not here). `OutHeaderStart` is where slot 3 began: the engine moves the two
	// sections into the file's header and data areas from it (`docs/vtmb/savegame_format.md`).
	// vfunc13 also calls slot 23 `BuildAdjacentMapList(NULL, NULL)` between slots 17 and 18 (0x200964ae;
	// L0-r030), over `Data.World`.
	bool EngineSaveGameState(FBlockSet& Set, FSaveRestoreData& Data, int32& OutHeaderStart);          // vfunc13 0x20096470
	void EngineLoadGameState(FBlockSet& Set, FSaveRestoreData& Data, int32 HeaderStart, int32 DataStart, int32 P2, int32 P3); // vfunc9 0x200975f0
	// vfunc10's two slot calls over one save: slot 23 with the two transition strings (0x20097d73), then
	// slot 20 (0x20097e25). The adjacent-map loop and slot 22 around them are
	// `ElysiumTransitionState::EngineLoadAdjacentEnts`.
	void EngineLevelTransition(FBlockSet& Set, FSaveRestoreData& Data, int32 HeaderStart,
		const char* OldLevel = nullptr, const char* LandmarkName = nullptr);                            // vfunc10 0x20097d00

	// The static set 0x1072bae8 with retail's five handlers appended in DLLInit order
	// (CServerGameDLL::vfunc1 0x1011a0c0, asm 0x1011a20b–0x1011a279): Entities, EventQueue, Physics,
	// AI, Python. Built on first use.
	FBlockSet& GameBlockSet();
}
