// `CBaseFlex`'s hand-written slot bodies and the helpers they reach (story 5 step 6),
// moved up the chain from `FElysiumNpcBase`. Declarations are generated in
// `ElysiumFlexSlots.inl` (a slot body) or in `ElysiumFlexSlotBodies.inl`.

#include "ElysiumFlex.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAnimShared.h"
#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumNpcBaseEntityChainShared.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEntityChainShared.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"

// slot 82 `datamap_t* GetDataDescMap()` -- `CBaseFlex::vfunc82` 0x100b57b0
void* FElysiumFlex::GetDataDescMap()
{
	// `MOV EAX,0x10559360; RET`: `&datamap_CBaseFlex`, `baseMap` -> `datamap_CBaseAnimatingOverlay`
	// 0x105509f8. The port's descriptor chain answers the leaf's descriptor through the base body
	// (L0-r017, `ElysiumEntityKeyValue.cpp`).
	return FElysiumEntity::GetDataDescMap();
}

// --- Moved from `ElysiumNpcBaseAnim.cpp` (story 5 step 6) ---

float FElysiumFlex::SequenceDurationOf(int32 Sequence) const
{
	// `CBaseAnimating::SequenceDuration(int)`. **SEAM**, answering 0.
	(void)Sequence;
	return 0.f;
}

float FElysiumFlex::SceneTimeOf(const FElysiumSceneData& Scene) const
{
	// `CChoreoScene::GetTime()` (`0x1007dfa0`) — one float at scene+0x7c. **SEAM**: `FElysiumSceneData`
	// is the PARSED file and holds no clock; the clock lives on `FElysiumScenePlayer`, which the
	// kernel does not reach. Answers 0, which makes a gesture's wind-back its own start time.
	(void)Scene;
	return 0.f;
}

int32 FElysiumFlex::NumFlexControllers() const
{
	// `CBaseAnimating::GetNumFlexControllers` (`0x10012530`, reached from every body in the flex
	// half). **SEAM**: no studio header at this tier, so the table is empty.
	return 0;
}

// --- Moved from `ElysiumNpcBaseFacing.cpp` (story 5 step 6) ---

void FElysiumFlex::SetViewtarget(const FVector& NewViewtarget)
{
	// `0x100b5b00`, the whole body: three floats into `m_viewtarget` (+0x0848). Retail networks the
	// word; nothing in this substrate reads it yet.
	Viewtarget = NewViewtarget;
}
