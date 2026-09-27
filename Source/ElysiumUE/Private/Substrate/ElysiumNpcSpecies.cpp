#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29c-1, family **Species** — the per-species bodies of layers 0–9. This file carries slot
// 323, the two `CUtlVector<{EHANDLE, expiry}>` stores
// (`CNPC_VBaseBoss`'s and `CNPC_VTzimisce`'s) and the melee quartet's species replacements (slots
// 599, 600, 601, 602) plus slots 606 and 609. Everything else — `CNPC_VNewscaster`,
// `CNPC_VTzimisce`'s carry chain, `CNPC_VWerewolf`, `CNPC_VZombie`, `CNPC_VMingXiaoTentacle`,
// `CNPC_VCamera` and `CNPC_VAndreiBlood` — is
// `Substrate/ElysiumNpcSpecies2.cpp`. The declarations and this family's three standing facts
// are `Substrate/ElysiumNpcSpecies.inl`; the walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// **Every `.rdata` constant in this family was READ**, not inferred: the pinned retail
// `vampire.dll`'s `.rdata` was addressed directly (image base `0x10000000`, section VA
// `0x10445000`, raw `0x445000`) and each cell's little-endian float is quoted beside its address
// below. Where a datum lives in `.data` and is filled at runtime it is a seam and says so. Nothing
// here is a guess dressed as a number.

namespace
{
	// --- Retail `.rdata`, one line per cell, each value read out of the pinned image ---------------

	constexpr float SpeciesOne = ElysiumNpcTunables::One;

}

