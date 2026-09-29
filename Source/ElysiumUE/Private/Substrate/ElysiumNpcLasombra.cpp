#include "Substrate/ElysiumNpcLasombra.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// Slot 592: `0x103893c0`, whose miss calls the Troika body `0x102953e0` directly.
/** `CNPC_VLasombra::vfunc592` (`0x103893c0`) — the body of `FElysiumNpcLasombra::CanSeekCover`. */
bool FElysiumNpcLasombra::CanSeekCover()
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// `CNPC_VLasombra::vfunc592` (`0x103893c0`), the body of `FElysiumNpcLasombra::CanSeekCover`:
	//     if (curtime < m_flCoverDisableOverride (+0x6664)) return true;   // NOT false
	//     return CAI_BaseNPCTroika::CanSeekCover();
	//
	// **29c's walk has the polarity backwards** ("returns false while curtime is before
	// m_flCoverDisableOverride"). The body returns the FPU flag word for `curtime < override` on
	// that arm, i.e. TRUE — `CONCAT22(..., (curtime<ovr)<<8 | ...)` puts the comparison result in
	// AL. The field name reads as a disable, but the arm it guards is the permissive one: while the
	// override stands, a Lasombra may always seek cover without consulting the Troika rule.
	// Corrected here and in the walked paragraph.
	if (Now < static_cast<double>(LasombraCoverDisableOverride))
	{
		return true;
	}
	return FElysiumNpc::CanSeekCover();   // `0x102953e0`, direct
}

// --- Moved from `ElysiumNpcMisc.cpp` (story 5 step 4) ---

