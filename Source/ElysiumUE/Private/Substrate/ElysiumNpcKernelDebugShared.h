#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelDebug.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelDebugShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumSchedule.h"

namespace NpcKernelDebugShared
{
	// The open capture, or empty. Game-thread only, like the rest of the substrate.
	inline TArray<FElysiumNpc::FDebugLine> GNpcKernelDebugCapture;
	inline bool GNpcKernelDebugCapturing = false;
	inline const TCHAR* const GNpcKernelDebugChannelOverlay = TEXT("Overlay");
	inline void GNpcKernelDebugRecord(const TCHAR* Channel, const TCHAR* Retail, FString&& Text,
		int32 Line)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("[%s] %s"), Channel, *Text);
		if (GNpcKernelDebugCapturing)
		{
			FElysiumNpc::FDebugLine Row;
			Row.Channel = Channel;
			Row.Retail = Retail;
			Row.Text = MoveTemp(Text);
			Row.Line = Line;
			GNpcKernelDebugCapture.Add(MoveTemp(Row));
		}
	}
}
