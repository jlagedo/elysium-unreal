// Story 0019/8 (29e under the strict verdict), family **Misc19** -- `CAI_BaseNPCTroika`'s helper
// declarations.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`; the definitions are in
// `ElysiumNpcMisc19.cpp`, or generated in the slot files for a slot body.
//
// Owns (Misc19's `rule` rows): 0x10279a50 SetEnemy, 0x102b4f60 CAI_BaseNPCTroika::FUN_102b4f60,
// 0x102b4fe0 CAI_BaseNPCTroika::FUN_102b4fe0, 0x10365a90 FUN_10365a90, 0x102b4cc0
// CAI_BaseNPCTroika::FUN_102b4cc0, 0x10395ce0 FUN_10395ce0, 0x1039ea60 FUN_1039ea60, 0x102b5c00
// CAI_BaseNPCTroika::EnterGrappleState, 0x1017f4a0 PlayerSupernaturalIncident, 0x1029b290
// CAI_BaseNPCTroika::HandleAnimEvent.

/** SEAM for `UTIL_EntitiesInBox(list, 0x20, mins, maxs, 0x40, 0)` (`0x101ccc80`), slot 595's
 *  candidate source: every live entity whose origin lies in `[MinsCm, MaxsCm]`, in entity-list
 *  order, at most `MaxCount`. See the definition for what of the retail enumeration is unrecovered. */
TArray<FElysiumEntity*> AcquireTargetBoxQuery(const FVector& MinsCm, const FVector& MaxsCm,
	int32 MaxCount) const;

/** SEAM for `CBaseCombatCharacter::SetExpression(name, delay, fadeIn, duration, fadeOut, scale)`
 *  (`0x10106580`), the call slot 259's `0x80c` / `0x80d` arms make: `FadeoutExpressions`, the
 *  dialog-partner scale, then `AddScriptedExpression`. Only the last step has a port recorder. */
void SetExpressionMisc19(const FString& Name, float Delay, float FadeIn, float Duration, float FadeOut,
	float Scale);
int32 SetExpressionMisc19Calls = 0;

// `CAI_BaseNPC::HandleAnimEvent` (`0x10274e30`, `FElysiumNpcBase::HandleAnimEvent`) calls the
// footstep chain `0x1026d460` for 0x802..0x805; this runtime keeps that chain as the Troika's
// protected `NpcStep`, so the base body is granted access to it rather than a second copy.
friend class FElysiumNpcBase;

/** SEAM for `UTIL_ScreenShake(center, amplitude, frequency, duration, radius, command, bAirShake)`
 *  (`0x101cdba0`) as the species `HandleAnimEvent` bodies of this family raise it (footfalls, the
 *  Ming Xiao and Werewolf slams). Nothing in this substrate shakes the view from the kernel; each
 *  request is recorded, in order. SOURCE units. The ManBat's cone keeps its own recorder
 *  (`ScreenShakeCalls`), which has no air-shake field. */
struct FAnimEventShakeCall
{
	FVector CentreUnits = FVector::ZeroVector;
	float Amplitude = 0.f;
	float Frequency = 0.f;
	float Duration = 0.f;
	float Radius = 0.f;
	bool bAirShake = false;
};
TArray<FAnimEventShakeCall> AnimEventShakeCalls;
void RecordAnimEventShake(const FVector& CentreUnits, float Amplitude, float Frequency, float Duration,
	float Radius, bool bAirShake);

/** SEAMS for species anim-event callees with no port body, each counted:
 *  - `CNPC_VDog`'s bite `0x10374f40` (the enemy trace at the enemy's height with `MASK_NPCSOLID`
 *    and the active weapon's slot 270 hit application — no `trace_t`, no slot-270 body);
 *  - `CNPC_VMingXiao`'s tentacle grab `0x10398db0` (the `phys_animlink` on
 *    `Bip01_[RL]_ThrowingTenticle5`) and vomit emitter `0x102c42a0("Ming_xiao_vomit_emitter", …,
 *    "Bip01 MouthRoot")`;
 *  - `CNPC_VTzimisce`'s expression `0x103b9f90(2, x)` (table entry 2's name unrecovered);
 *  - `CNPC_VWerewolf`'s activity voice `0x103d8df0` and the hint `OnAnimEvent<n>` fire
 *    `0x102d09b0` (no hint-to-entity path). */
int32 DogBiteCalls = 0;
int32 MingXiaoGrabCalls = 0;
int32 MingXiaoVomitEmitterCalls = 0;
int32 TzimisceExpressionTwoCalls = 0;
int32 WerewolfActivityVoiceCalls = 0;
int32 WerewolfHintAnimEventCalls = 0;
int32 WerewolfHintAnimEventIndex = 0;
