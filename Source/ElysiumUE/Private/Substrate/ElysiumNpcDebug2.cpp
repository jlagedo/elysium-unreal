#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebug2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Debug**, the geometry half — slots 123, 124 and 620 plus the two
// `CNPC_VWerewolf` draws. `ElysiumNpcDebug.cpp` carries the seam, the tables and the text
// bodies and states this family's standing fact; every emission below goes through the same
// `FDebugLine`.
//
// The whole of `0x10275760` was recovered from the LISTING, not from the decompiled C: the
// decompiler folded eight `NDebugOverlay` argument lists into the 508-byte stack frame and lost
// every colour byte and every box extent. What is ported is the eight `m_debugOverlays` gates in
// retail's order, the literals the listing pushes, and the two tail calls.

namespace
{
	// Unit-prefixed: adaptive unity merges anonymous namespaces.

}

// -------------------------------------------------------------------------------------------------
// The geometry seams.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::EntityDrawBBoxOverlay() const
{
	// SEAM for `CBaseEntity::DrawBBoxOverlay()`, the arm `CNPC_VWerewolf#620` falls through to.
}

// -------------------------------------------------------------------------------------------------
// Slot 123's base body — `CAI_BaseNPC::DrawDebugGeometryOverlays` `0x10275760`.
// -------------------------------------------------------------------------------------------------

