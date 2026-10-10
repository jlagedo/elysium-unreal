#pragma once

#include "CoreMinimal.h"
#include "ElysiumSaveRestoreBlocks.h"

// The "Game" block set's five handlers over the port's map snapshot (L0-r029). Retail registers
// them from DLLInit (CServerGameDLL::vfunc1 0x1011a0c0, asm 0x1011a20b–0x1011a279) in this order:
//
//   | # | object      | class (vftable)                              | GetBlockName            |
//   | 0 | 0x1072bb44  | CEntitySaveRestoreBlockHandler (0x10477130)  | "Entities"   0x1059317c |
//   | 1 | 0x106e70a8  | CEQ_SaveRestoreBlockHandler (0x10454060)     | "EventQueue" 0x1055e5bc |
//   | 2 | 0x106bda60  | CPhysSaveRestoreBlockHandler (0x1044943c)    | "Physics"    0x10539b54 |
//   | 3 | 0x10936b5c  | CAI_SaveRestoreBlockHandler (0x1049dfb0)     | "AI"         0x106129e0 |
//   | 4 | 0x1072b354  | CPython_SaveRestoreBlockHandler (0x10476ac8) | "Python"     0x1055e570 |
//
// The handlers' own slot bodies are NOT walked by L0-r029 (its Open 10). What each port handler
// writes is the port's existing capture of that block -- the entity rows, the queue -- in the
// port's archive encoding (decisions.md D6, a named modernization); `Physics` has no port body
// (D6: the physics serializer is an owner decision; the base slot answers false); `AI` and
// `Python` are the L4 / L5 upward hooks of a lower layer's registration: registered in retail's
// order with empty bodies, so the dispatch order they will fill is already the retail one.

struct FElysiumMapSnapshot;
class FElysiumEntityWorld;
struct IElysiumRetailSiteSink;

namespace ElysiumSaveRestore
{
	// What the five static handlers reach their world through. Retail's handlers read globals
	// (gEntList, g_EventQueue); the port hands them this through the save-data struct's `Context`.
	struct FGameContext
	{
		// Save: the arrays the Entities and EventQueue bodies encode.
		const FElysiumMapSnapshot* Source = nullptr;
		// Restore: where the decoded rows land, and the world they apply to (null: decode only).
		FElysiumMapSnapshot* Decoded = nullptr;
		FElysiumEntityWorld* World = nullptr;
		double RestoreBase = 0.0;
		int32 AppliedRows = INDEX_NONE;   // what `ApplyRestoredEntities` answered; INDEX_NONE until it ran
		bool bRowDecodeFailed = false;    // a row's bytes did not read back
	};

	// Freeze's tail: the engine's save order (vfunc13 0x20096470: slots 17, 18, 19) over the Game set,
	// encoding the snapshot's arrays into `Snapshot.BlockStream` / `BlockHeaderStart`.
	void EncodeMapBlocks(FElysiumMapSnapshot& Snapshot, IElysiumRetailSiteSink* Sites);

	// The file reader's decode: the engine's restore order (vfunc9 0x200975f0: slots 20, 21) with no
	// world, so the rows land in the arrays without applying. False when the stream did not read back.
	bool DecodeMapBlocks(FElysiumMapSnapshot& Snapshot);

	// ApplySnapshot's chain: slot 20, then slot 21 (an ordinary load; the handlers' Restore bodies
	// apply onto `World`) or the engine-side transition path (vfunc10 0x20097d00: slot 20 only, the
	// engine's own entity loop -- unrecovered -- standing in for the Entities body). `Decoded` receives
	// the rows; the answer is the applied row count or INDEX_NONE.
	int32 RestoreMapBlocks(const FElysiumMapSnapshot& Snapshot, FElysiumEntityWorld& World, FElysiumMapSnapshot& Decoded,
		double RestoreBase, bool bLevelTransition, IElysiumRetailSiteSink* Sites);
}
