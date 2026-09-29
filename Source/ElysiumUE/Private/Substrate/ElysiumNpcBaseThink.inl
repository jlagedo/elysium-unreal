// Story 0019/8 (29e under the strict verdict), family **Think19** -- `CAI_BaseNPC`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpcBase` by `Substrate/ElysiumNpcBase.h`; the definitions are in
// `ElysiumNpcBaseThink.cpp`, or generated in the slot files for a slot body.
//
// Owns (Think19's `rule` rows): 0x1026ca80 CAI_BaseNPC::NPCThink.

/** `0x1026c3d0` (retail name unrecovered) -- the AI console gate every `NPCThink` asks (the base
 *  `0x1026ca80`, the Troika `0x10292de0`, `CNPC_VCamera` `0x10369120`). Reads `g_AIDisabled`
 *  (`DAT_1092053c`: the world's `IsAiEnabled` for bit 0, `IsAiStepMode` for bit 1) and the node-graph
 *  byte `DAT_1093408c`. True runs the AI; false has already dispatched slot 310 `SetActivity(1)` on
 *  the disabled and graphless arms, or written `m_flPlaybackRate` on the `ai_step` arm. */
bool Think19AiConsoleGate();

/** `DAT_1093408c`, the loader byte the console gate `0x1026c3d0` (`0x1026c3e3`) and the Troika think's
 *  refused arm read: the node graph is loaded or built. Set by the `.ain` loader `0x102f5bd0` and by the
 *  rebuild step `0x102f6610`; here false only while a map's place set is pending adoption, true otherwise
 *  (`FElysiumEntityWorld::IsNodeGraphLoaded`). False with no world. Every graph-false arm is a refusal
 *  (the throttled "A.I. Disabled..." overlay, the refused think's `m_flNextThink = curtime + 0.1` re-arm). */
bool Think19NodeGraphBuilt() const;

/** `_DAT_104491a8` (a double, 0.8): the delay `0x102f6690` arms the network manager's first think at
 *  (`m_flNextThink = curtime + 0.8`, at the end of `CWorld::Precache`'s network build). */
static constexpr double AiNetworkFirstThinkDelay = ElysiumNpcTunables::EightTenthsDouble;

/** `g_pAINetworkManager` (`DAT_10934088`) non-null AND its `+0x658` byte, the base think's gate
 *  `0x1026cb2a`/`0x1026cb36`. The byte has one writer, the manager's think `0x102f6a50`, and one reader,
 *  `CAI_BaseNPC::NPCThink` `0x1026ca80`; here, `AiNetworkFirstThinkDelay` seconds past the world's build
 *  stamp (`FElysiumEntityWorld::BuildStampSeconds`, re-stamped on a restore). The Troika body never asks. */
bool Think19AiNetworkReady() const;

/** SEAM for `CAI_BaseNPC::CacheInterruptConditions` (`0x1026a0f0`) on a body that is NOT on the Troika
 *  line. The port's one body is `FElysiumNpc::CacheInterruptConditionsForMaintenance` (it reads the
 *  Troika's own schedule); the base-only classes (`CCineNPC` line, `CAI_TestHull`) carry no program
 *  host that body could read, so this counts the call and caches nothing. */
void Think19BaseCacheInterruptConditions();
int32 Think19BaseCacheInterruptCalls = 0;

/** `0x1026c3d0`'s graphless arm: the "A.I. Disabled...\n" (`0x105cbd9c`) overlay through
 *  `0x101ce3d0`, throttled on the process-wide stamp `_DAT_10920550` (+5.0 s, `0x10454110`). A debug
 *  overlay: the port counts it (reached while `Think19NodeGraphBuilt` answers false). */
int32 Think19AiDisabledOverlayCalls = 0;
