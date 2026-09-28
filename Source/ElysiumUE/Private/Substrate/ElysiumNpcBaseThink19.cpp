// Story 0019/8 (29e under the strict verdict), family **Think19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseThink19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (Think19's `rule` rows): 0x1026ca80 CAI_BaseNPC::NPCThink. Also the AI console gate
// `0x1026c3d0` the three `NPCThink` bodies share. Walked prose: `docs/vtmb/npc-ai/story8/Think19.md`.

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

bool FElysiumNpcBase::Think19NodeGraphBuilt()
{
	// SEAM: `DAT_1093408c` -- no node-graph build step in this runtime; the admitting value.
	return true;
}

bool FElysiumNpcBase::Think19AiNetworkReady()
{
	// SEAM: `g_pAINetworkManager && g_pAINetworkManager->+0x658` -- the admitting value.
	return true;
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
			if (!NavIsGoalActive())                                         // 0x102ee6a0(+0x5d34)
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
	// 0x1026caf1..0x1026cb03: `m_bDumpDebugBuffer` (+0x5b55) set -> clear it and dump the ring
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
	if (!Think19AiConsoleGate())                                            // 0x1026cb8b / 0x1026cb92
	{
		return;
	}
	// `m_GrapplePartner` (+0x1538) resolving AND `m_GrappleRole` (+0x153c) == 1 skips slot 432.
	const FElysiumEntity* Partner = World != nullptr ? World->Resolve(Grapple.Partner) : nullptr;  // 0x1026cbf2..0x1026cc1a
	if (Partner == nullptr || Grapple.Role != EElysiumGrappleRole::Victim)  // 0x1026cc1c / 0x1026cc23
	{
		RunAI(false);                                                       // 0x1026cc2a slot 432
	}
	PostRun();                                                              // 0x1026cc32 0x1026c7c0
	// `PerformMovement(RunAnimation(), 0)`: `0x1026c540` advances the sequence clock and re-picks a
	// finished idle; its interval is this runtime's 0.0.
	PerformMovement(RunAnimation(), 0);                                     // 0x1026cc3b 0x1026c540 / 0x1026cc43 0x1026c120
}
