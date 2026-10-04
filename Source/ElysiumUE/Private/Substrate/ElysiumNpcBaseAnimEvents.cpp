// `CAI_BaseNPC`'s slot 258, the animation-event dispatch of the NPC chain (spec 0002 V4a). The
// declaration is in `ElysiumNpcBase.h`; the shared dispatcher bodies (`0x10091880`, `0x10098cd0`)
// are `ElysiumAnimEvents::DispatchBase` / `DispatchLayer` (`Substrate/ElysiumAnimEvents.h`).

#include "Substrate/ElysiumNpcBase.h"

void FElysiumNpcBase::DispatchAnimEvents(float Interval, FElysiumEntity* Handler)
{
	// `CBaseAnimatingOverlay::DispatchAnimEvents 0x10098c80` on the NPC chain; filled by V4a lane A1.
	// Until then the seam forwards to today's body, the overlay's counting stub, so `PostRun
	// 0x1026c7c0`'s call does exactly what it did.
	FElysiumAnimatingOverlay::DispatchAnimEvents(Interval, Handler);
}
