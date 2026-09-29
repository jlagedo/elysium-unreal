#pragma once

#include "Substrate/ElysiumNpc.h"

// `CPayphone` (primary vtable `0x10479814`), built by `npc_payphone` factory `0x101aa550`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcPayphone : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CPayphone", FElysiumNpc)

	virtual void NPCInit() override;
	virtual FVector EyePosition() const override;
	virtual bool CanTalk(FElysiumEntity* Activator) override;
	virtual void Think() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcEntityChain.inl`.
	/** `CPayphone::vfunc286` (`0x101aad90`) — the payphone's override of slot 286 `AddSceneEvent`, whose
	 *  whole body is `return;`. Species-only (only `CPayphone#286` fills it), so `default:void` does not
	 *  formally apply and it lands as a body: a payphone swallows every choreo scene event. */
	void PayphoneAddSceneEvent(const void* Scene, const void* Event);
	/** `0x101aadb0` — `CPayphone#612`, the speech sound FLAGS the emitter (`0x102c0520`) passes to
	 *  `EmitSound` (`signatures.md` slot 612). `0xa80` when `bDialogQueIsFinal` (+0x654c) is SET,
	 *  `0xe80` when it is clear — note the inversion against the Troika line's own body, which adds
	 *  `0x400` when the flag is CLEAR. */
	int32 PayphoneSpeechSoundFlags() const;
	/** `CPayphone::vfunc35` (`0x101aa950`) — `return CanTalk(other) ? 0x2f : 0;`, the capability bitmask
	 *  a payphone publishes. Slot 35's own base is generic ObjectCaps-style across the shared vtable;
	 *  this is the payphone's, and it is the whole mask gated on one virtual. */
	int32 PayphoneUseCaps(FElysiumEntity* Other);

	// From `ElysiumNpcSensesBodies.inl`.
	/** `0x101aaf80`, `CPayphone#45` — the payphone's `PassesFindEntityFOVTrace`. **No cone and no
	 *  trace**, whatever 29c's walk says: the MANHATTAN distance between the two `EyePosition()`s
	 *  against `_DAT_1047a3b0` = **85.0** Source units, and under it a six-term AABB overlap of the two
	 *  entities' OBBs (`0x10240250`). Both arguments of the slot's `Vector, Vector, int` tail are
	 *  ignored by the body. */
	bool PayphonePassesFindEntityFovTrace(const FElysiumEntity& Other) const;

	// From `ElysiumNpcSpeciesLifecycle10.inl`.
	/** `m_hDialogPartner` (`+0x0fe8`), as an ENTITY HANDLE.
	 *
	 *  **SEAM, and it answers nothing.** This runtime carries the dialogue partner as the open session
	 *  (`FElysiumNpcDialogue::bInDialog` plus the talk-end stamp), which is the reading family Anim's
	 *  `HasLiveDialogPartner()` and family Sounds' `IsInDialog` both already made — a boolean, not a
	 *  handle. `CPayphone::NPCThink` is the first body in the kernel that needs the partner as an
	 *  entity, because it reads two of the partner's own animation words off it, so the handle is
	 *  declared here at its retail offset with no writer. Nothing in this runtime assigns it, so a
	 *  payphone takes retail's own no-partner arm — which is the admitting arm: a payphone standing
	 *  alone idles and re-thinks on the 0.25 s clock, exactly as retail's does. */
	FElysiumEntityHandle DialogPartner;   // +0x0fe8 m_hDialogPartner (SEAM: no writer)
	/** The partner resolved live, or null. Retail's test is the three-part EHANDLE validity check
	 *  (`index & 0x1fff`, serial `>> 0xd`, non-null record) at `101aac0b`..`101aac2c`. */
	FElysiumEntity* ResolveDialogPartner() const;
	/** SEAM for `FUN_102c1400` (`0x102c1400`), the dialogue upkeep tick both payphone arms run. It is a
	 *  237-instruction body of its own — the scene-entity release, `FinishTalking` when the talk
	 *  finished, the queued-line pump, the disposition switch to schedule `0xf1` and
	 *  `CDialog::ShowPlayerChoices` — and it is **not one of this family's rows**. Counted here so the
	 *  payphone's call ORDER (tick before the activity mirror, tick before the idle) is assertable, and
	 *  named so the day it is walked the call site is already correct. */
	int32 DialogUpkeepTicks = 0;
	void DialogUpkeepTick();
	/** How many payphone passes took the mirror arm and how many took the idle arm. */
	int32 PayphoneMirrorPasses = 0;
	int32 PayphoneIdlePasses = 0;
	/** `CPayphone::vfunc431` (`0x101aabf0`), slot 431 — the WHOLE think for a payphone. It does not call
	 *  `CAI_BaseNPCTroika::NPCThink` at any point, so a payphone runs no schedule, no senses and no
	 *  motor: it is an NPC whose entire behaviour is to copy its dialogue partner's pose.
	 *
	 *  Retail, in the listing's order:
	 *    1. slot 250 `StudioFrameAdvance(0.0)`, its float return discarded (`101aabf8` then
	 *       `101aac07 FSTP ST0`).
	 *    2. If `m_hDialogPartner` (`+0x0fe8`) resolves live:
	 *         a. the dialogue upkeep tick `0x102c1400`, UNCONDITIONALLY;
	 *         b. if the partner's `m_IdealActivity` (`+0xff0`) differs from mine:
	 *              `SetIdealActivity(theirs)`; `m_nSequence` (`+0x6f0`) =
	 *              `SelectWeightedSequence(theirs, -1)`; `ResetSequenceInfo()` (`0x10090950`);
	 *         c. ALWAYS `m_flCycle` (`+0x6f8`) = the partner's `m_flCycle`;
	 *         d. `m_flNextThink` = curtime + `_DAT_10450aa4` (**0.01 s**).
	 *       and RETURNS — nothing below runs.
	 *    3. Otherwise: `if (IsInDialog())` the same tick; then `SetIdealActivity(1)` (`ACT_IDLE`)
	 *       UNCONDITIONALLY; then `m_flNextThink` = curtime + `_DAT_1044bef8` (**0.25 s**).
	 *
	 *  Note (b) versus (c): the sequence is re-selected only on an activity CHANGE, but the cycle is
	 *  copied on every pass. That is what makes the mirror frame-accurate, and it is why the arm needs
	 *  a 0.01 s clock.
	 *
	 *  Always answers true: the payphone body owns the whole pass. */
	bool PayphoneThink();
	/** `_DAT_10450aa4` — **0.01f**, read at file offset `0x450aa4` of the pinned `vampire.dll`. */
	static constexpr double PayphoneMirrorThinkSeconds = 0.009999999776482582;
	/** `_DAT_1044bef8` — **0.25f**, read at file offset `0x44bef8`. */
	static constexpr double PayphoneIdleThinkSeconds = 0.25;
	/** `101aac9c PUSH 0x1` — `ACT_IDLE`. */
	static constexpr int32 PayphoneIdleActivity = 1;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual bool EnterGrappleState(const FElysiumEntityHandle& Partner, EElysiumGrappleRole Role, EElysiumGrappleType Type, int32 Position = INDEX_NONE, bool bHolster = true) override;
};
