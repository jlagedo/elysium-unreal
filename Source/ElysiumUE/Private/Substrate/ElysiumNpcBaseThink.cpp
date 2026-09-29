// Story 0019/8 (29e under the strict verdict), family **Think19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseThink.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (Think19's `rule` rows): 0x1026ca80 CAI_BaseNPC::NPCThink. Also the AI console gate
// `0x1026c3d0` the three `NPCThink` bodies share. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "Story 8, family Think19".

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelTunables.h"

namespace
{
	// `DAT_105c9798`, the `ai_step` debug-index floor `0x1026c3d0` compares `+0x5f40` against; -1 in
	// the pinned image (family Maintain19 reads the same cell, `FElysiumNpc::FreezeForAiStep`).
	constexpr int32 GThink19AiStepIndexFloor = -1;

	// `_DAT_10920550`: the process-wide throttle stamp of the "A.I. Disabled..." overlay.
	double GThink19AiDisabledOverlayAt = 0.0;
}

bool FElysiumNpcBase::Think19NodeGraphBuilt() const
{
	// `DAT_1093408c` != 0: loaded or rebuilt (`0x102f5bd0` / `0x102f6610`); false only while the map's
	// place set is pending adoption (0018 story 4).
	return World != nullptr && World->IsNodeGraphLoaded();
}

bool FElysiumNpcBase::Think19AiNetworkReady() const
{
	// `g_pAINetworkManager && g_pAINetworkManager->+0x658`: the manager's think `0x102f6a50` sets the
	// byte, armed at `curtime + 0.8` by `0x102f6690` inside `CWorld::Precache`.
	return World != nullptr && World->NowSeconds() >= World->BuildStampSeconds() + AiNetworkFirstThinkDelay;
}

void FElysiumNpcBase::Think19BaseCacheInterruptConditions()
{
	// SEAM: `0x1026a0f0` on a non-Troika body (see the declaration).
	++Think19BaseCacheInterruptCalls;
}

// `0x1026c3d0`, 283 bytes (`__fastcall`, the NPC in ECX).
bool FElysiumNpcBase::Think19AiConsoleGate()
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const bool bAiDisabled = World != nullptr && !World->IsAiEnabled();     // DAT_1092053c & 1
	bool bOverlay = false;
	if (!bAiDisabled)                                                       // 0x1026c3e3
	{
		if (Think19NodeGraphBuilt())                                        // DAT_1093408c != 0
		{
			if (World == nullptr || !World->IsAiStepMode())                 // DAT_1092053c & 2
			{
				return true;                                                // LAB_1026c429
			}
			// The `ai_step` arm. `+0x5f40` (the maintenance debug task index) and `+0x6f4`
			// (`m_flPlaybackRate`) live on the Troika line in this runtime (`MaintainDebugTaskIndex`,
			// `SequencePlaybackRate`); a base-only body has no home for either and reads index 0.
			FElysiumNpc* Troika = AsNpc();
			const int32 StepIndex = Troika != nullptr ? Troika->MaintainDebugTaskIndex : 0;  // param_1[0x17d0]
			if (StepIndex < GThink19AiStepIndexFloor)                       // < DAT_105c9798
			{
				if (Troika != nullptr)
				{
					Troika->SequencePlaybackRate = ElysiumNpcTunables::One; // +0x6f4 = 1.0
				}
				return true;                                                // LAB_1026c429
			}
			if (!NavigatorGoalIsActive())                                   // 0x102ee6a0(+0x5d34)
			{
				if (Troika != nullptr)
				{
					Troika->SequencePlaybackRate = 0.f;                     // +0x6f4 = 0
				}
				return false;
			}
			return false;                                                   // LAB_1026c4e8: low byte cleared
		}
		bOverlay = true;                                                    // LAB_1026c43d
	}
	else if (!Think19NodeGraphBuilt())
	{
		bOverlay = true;                                                    // LAB_1026c43d
	}
	if (bOverlay && GThink19AiDisabledOverlayAt <= Now)                     // _DAT_10920550 <= curtime
	{
		GThink19AiDisabledOverlayAt = Now + ElysiumNpcTunables::Five;       // + _DAT_10454110
		++Think19AiDisabledOverlayCalls;                                    // 0x101ce3d0 "A.I. Disabled...\n"
	}
	SetActivity(1);                                                         // slot 310 (vt+0x4d8), ACT_IDLE
	return false;                                                           // LAB_1026c4e8
}

// Slot 431: `0x1026ca80`, 643 bytes. `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`,
// `CAI_TestHull`, the four Cine classes, `CGenericNPC`, `CGenericSabbat_NPC`. No Troika body reaches it.
void FElysiumNpcBase::NPCThink()
{
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	// 0x1026ca8c / 0x1026ca96: the debug name-stack push (`this` null -> "NULL ENTITY", else the
	// classname at +0x26c or ""), popped at 0x1026cbec / 0x1026ccfd: instrumentation, absent.
	// 0x1026caf1..0x1026cb03 (0x1026caf9): `m_bDumpDebugBuffer` (+0x5b55) set -> clear it and dump the ring
	// `0x1027efb0`. The byte is an ABSENT word (story 29b; the shape map's ABSENT row), so the arm
	// cannot fire here; the dump body is `DumpDebugLogRing`.
	Think19BaseCacheInterruptConditions();                                  // 0x1026cb0a 0x1026a0f0
	// `m_flNextThink = curtime + 0.1` (the DOUBLE at 0x104493d0), BEFORE every gate.
	NextThink = static_cast<float>(Now + ElysiumNpcTunables::TenthDouble); // 0x1026cb14..0x1026cb1d
	if (!Think19AiNetworkReady())                                           // 0x1026cb2a / 0x1026cb36
	{
		return;
	}
	// VProf "CAI_BaseNPC_NPCThink" enter/exit (0x1026cb3c..0x1026cb89, 0x1026cc48..): absent.
	// Enter: 0x1026cb48 / 0x1026cb50 (profiler off), 0x1026cb5e (node already current), 0x1026cb6b
	// (GetSubNode), 0x1026cb7d (EnterScope).
	if (!Think19AiConsoleGate())                                            // 0x1026cb8b / 0x1026cb92
	{
		// VProf exit on this return (absent): 0x1026cba0 / 0x1026cba8, 0x1026cbb4 (ExitScope),
		// 0x1026cbbc (pop to parent node).
		return;
	}
	// `m_GrapplePartner` (+0x1538) resolving AND `m_GrappleRole` (+0x153c) == 1 skips slot 432.
	// The handle resolve: 0x1026cbfb (handle == -1), 0x1026cc16 (serial mismatch), 0x1026cc1a (slot empty).
	const FElysiumEntity* Partner = World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;  // 0x1026cbf2..0x1026cc1a
	if (Partner == nullptr || Grapple.Role != EElysiumGrappleRole::Victim)  // 0x1026cc1c / 0x1026cc23
	{
		RunAI(false);                                                       // 0x1026cc2a slot 432
	}
	// `PerformMovement(PostRun(), 0)`: `PostRun` runs `RunAnimation` (the sequence clock and the idle
	// re-pick) and the anim events, and answers the interval (`0x1026cc37 FSTP`).
	const float Interval = PostRun();                                       // 0x1026cc32 0x1026c7c0
	PerformMovement(Interval, 0);                                           // 0x1026cc43 0x1026c120
	// VProf exit (absent): 0x1026cc54 / 0x1026cc5c (profiler off), 0x1026cc6f / 0x1026cc74 (outermost
	// scope with a timer: RDTSC accumulate), 0x1026ccc4 (the node's exit call), 0x1026cccd (pop).
}
