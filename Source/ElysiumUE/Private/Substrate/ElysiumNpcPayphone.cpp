#include "Substrate/ElysiumNpcPayphone.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDialogue.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// `m_bfNPCStateFlags` (`+0x5b64`) bit 2. `FElysiumNpcFlags::NpcStateFlagsForRetailState` gives it
	// to retail states 2 (combat), 7/9/0xa/0xd (dead and its neighbours), 8 (flee) and 0xb/0xe
	// (hunt) — every state in which the body is busy or gone. `CPayphone::CanTalk` and the Troika
	// gate `0x102c21c0` both refuse on it.
	constexpr uint8 GDlgStateFlagBusy = 0x04;
	// The `CPayphone#612` speech sound flags.
	constexpr int32 GChainPayphoneFlagsFinal = 0xa80;
	constexpr int32 GChainPayphoneFlagsNotFinal = 0xe80;
	// `CPayphone#35`'s capability bitmask.
	constexpr int32 GChainPayphoneUseCaps = 0x2f;
	// `CPayphone::vfunc193`'s one bone name, `s_Phone_bone_01_1059537c`.
	const TCHAR* const GPayphoneBoneName = TEXT("Phone_bone_01");
	// `_DAT_1047a3b0` = 85.0f, also a single-reader cell: the payphone's MANHATTAN distance limit,
	// in SOURCE units.
	constexpr float GPayphoneManhattanLimitUnits = 85.0f;
}

const FElysiumNpcClass* FElysiumNpcPayphone::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x101aab90`.
// `0x101aab90`
void FElysiumNpcPayphone::NPCInit()
{
	TroikaNPCInit();                                                     // 101aab9x
	bIsBccTargetable = true;                                             // +0x1480
	bInvincible = true;                                                  // +0x63d8
	bNpcIsAlive = false;                                                 // +0x1481
	Senses.bCanPerformSenses = false;                                    // senses+0x80
}

// Slot 370: `0x101aa7f0`, a tail call through slot 368 — the head aim IS the body direction. Slot 368
// (`FElysiumCombatCharacter::BodyDirection2D`) has no override on any port class, so the direct call
// resolves exactly as retail's virtual one does.
FVector FElysiumNpcPayphone::HeadDirection2D()
{
	return BodyDirection2D();
}

// Slot 371: `0x101aa820`, a tail call through slot 369.
FVector FElysiumNpcPayphone::HeadDirection3D()
{
	return BodyDirection3D();
}

// Slot 193: `0x101aae60`, whose miss calls the Troika-line body `0x100b4b40` directly.
/** `CPayphone::vfunc193` (`0x101aae60`) — the body of `FElysiumNpcPayphone::EyePosition`. */
FVector FElysiumNpcPayphone::EyePosition() const
{
	// `CPayphone::vfunc193` `0x101aae60`, 95 bytes, the body of `FElysiumNpcPayphone::EyePosition`:
	//
	//     bone = LookupBone("Phone_bone_01");
	//     if (bone == -1) { return CAISound::FUN_100b4b40(this, out); }
	//     GetBonePosition02(bone, &pos, &ang);
	//     return pos;
	//
	// The bone's own world position, with NO view offset added and the angles thrown away. The
	// payphone's "eye" is its handset, which is what a dialogue camera and a `LookAtEntityEye` aim
	// at. `npc_payphone` is both a census classname and a registered spawn leaf, so this body is
	// reachable from a map.
	FVector BoneCm = FVector::ZeroVector;
	if (BoneWorldPosition(GPayphoneBoneName, BoneCm))
	{
		return BoneCm;
	}
	// `LookupBone` answered -1 — retail's own direct call into the base body `0x100b4b40`.
	return FElysiumCombatCharacter::EyePosition();
}

// Slot 295: `0x101aaee0`, a replacement that does not chain.
/** `CPayphone::CanTalk` (`0x101aaee0`, 119 bytes) — slot 295's `CPayphone` override, seven arms. */
bool FElysiumNpcPayphone::CanTalk(FElysiumEntity* Activator)
{
	// 0x101aaee0 — `CPayphone::CanTalk`, 119 bytes, slot 295's `CPayphone` override. Seven arms,
	// every one a refusal, in this order:
	//
	//     1. activator == NULL
	//     2. m_iDialog   (+0x0128) == 0            — no authored `dialogname`
	//     3. m_bScriptHidden (+0x00f4)             — `0x100b5190`, a 7-byte getter of that byte
	//     4. m_bWillTalk (+0x1088) == 0
	//     5. m_bfNPCStateFlags (+0x5b64) & 0x4     — the per-state busy bit
	//     6. m_bfAINPCFlags (+0x14b8) & 0x80000    — NO_DIALOG
	//     7. IsInDialog() (0x102c1170)             — the four-term session gate
	//     ...and the answer IS arm 7's negation, so a phone with no session admits.
	//
	// What the payphone does NOT test, against the Troika-line body `0x102c21c0`: the two `IsAlive`
	// dispatches, `IsUnconscious`, the player-side `0x10175180` and `0x10146b20`,
	// `IsBusyWithDiscipline`, `NO_DIALOG_PERSISTENT` (word two `0x10000000`), the menu global's
	// `+0x4ac` and slot 406 (`+0x650`). Seven arms against fourteen: a payphone is a simpler gate
	// than a person, and the difference is the recovered fact.
	//
	// 29c's walk calls arm 3 "the alive test 0x100b5190"; `docs/vtmb/npc-kernel/fields.md` names
	// `+0x00f4` `m_bScriptHidden`, whose writers are `ScriptHide`/`ScriptUnhide`. It is the
	// SCRIPT-HIDDEN test, which this runtime spells `FElysiumEntity::IsHidden()`.
	if (Activator == nullptr)
	{
		return false;
	}
	if (DialogName.IsEmpty())
	{
		return false;
	}
	if (IsHidden())
	{
		return false;
	}
	if (!bWillTalk)
	{
		return false;
	}
	if ((NpcStateFlags() & GDlgStateFlagBusy) != 0)
	{
		return false;
	}
	// Arm 6 is NO_DIALOG ALONE — word one's `0x80000`. `HasDialogSuppressFlag()` is the Troika
	// line's reading and ORs in `NO_DIALOG_PERSISTENT`, which this override does NOT test, so the
	// bit is read by name here rather than through that helper.
	if (NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG))
	{
		return false;
	}
	// Arm 7 — `CAI_BaseNPCTroika::IsInDialog` (`0x102c1170`). This runtime carries one session bit
	// plus a talk-end stamp for its four terms, the reading `ElysiumNpcThinkCadence.cpp` and family
	// Sounds both already took; repeated here rather than re-derived.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return !(Dialogue.bInDialog || IsTalking(Now));
}

// Slot 431: `0x101aabf0` replaces the Troika line's `NPCThink` (`0x10292de0`), which the port's
// `Think` carries; it never calls it.
/** `FElysiumNpcPayphone::Think`'s body: the inert gate, then `PayphoneThink` (story 5 step 3). */
void FElysiumNpcPayphone::Think()
{
	// `FElysiumNpcPayphone::Think`'s body (story 5 step 3): the port's `Think` IS the Troika line's
	// `NPCThink` (`0x10292de0`), and `CPayphone::vfunc431` REPLACES it outright. It sits under
	// `IsInert()` because retail expresses "this entity is gone" by having no think function at all.
	if (IsInert())
	{
		return;
	}
	PayphoneThink();
}

// --- Moved from `ElysiumNpcEntityChain.cpp` (story 5 step 4) ---

void FElysiumNpcPayphone::PayphoneAddSceneEvent(const void* Scene, const void* Event)
{
	// 0x101aad90, `CPayphone#286` — the body is `return;` with both parameters ignored. Only
	// `CPayphone` fills slot 286 with it, so `default:void` does not formally apply and it lands as
	// a body: a payphone swallows every choreographed-scene event instead of queueing it the way
	// `CBaseFlex::AddSceneEvent` (family Anim's `AddSceneEventBase`) would.
	(void)Scene;
	(void)Event;
}

int32 FElysiumNpcPayphone::PayphoneSpeechSoundFlags() const
{
	// 0x101aadb0, `CPayphone#612`. Slot 612 is the speech sound FLAGS the line emitter
	// (`0x102c0520`) passes to `EmitSound` (`docs/vtmb/npc-kernel/signatures.md` slot 612).
	//
	//   bDialogQueIsFinal set   -> 0xa80
	//   bDialogQueIsFinal clear -> 0xe80
	//
	// Note the sign against the Troika line's own body (`0x102c04b0`), which returns `0x680` when
	// the flag is CLEAR and the partner is live: the payphone's two answers differ by `0x400` in the
	// opposite direction, so a payphone's LAST line is the quiet one and the Troika line's is not.
	return Dialogue.bDialogQueIsFinal ? GChainPayphoneFlagsFinal : GChainPayphoneFlagsNotFinal;
}

int32 FElysiumNpcPayphone::PayphoneUseCaps(FElysiumEntity* Other)
{
	// 0x101aa950, `CPayphone#35` — `return CanTalk(other) ? 0x2f : 0;`. The whole mask behind one
	// virtual: slot 295 (`+0x49c`), which is family 29d's `CanTalk`. `-(c != 0) & 0x2f` is the
	// compiler's branchless spelling of that conditional and nothing more.
	return CanTalk(Other) ? GChainPayphoneUseCaps : 0;
}

// --- Moved from `ElysiumNpcSensesBodies.cpp` (story 5 step 4) ---

bool FElysiumNpcPayphone::PayphonePassesFindEntityFovTrace(const FElysiumEntity& Other) const
{
	// `0x101aaf80`, `CPayphone#45`, 165 bytes. **There is no cone and no trace.**
	//
	//     d = |other.EyePosition() - EyePosition()| summed per component   (MANHATTAN, not Euclidean)
	//     if (d >= 85.0) return false;                                     (_DAT_1047a3b0)
	//     return AABBOverlap(myOBB, otherOBB);                             (0x10240250)
	//
	// `0x10240250` is six comparisons — `otherMax >= myMin && otherMin <= myMax` per axis — which
	// is a box intersection, not a field-of-view test. 29c's walk calls it "the actual FOV cone
	// test"; the body is `FLD/FCOMP` pairs on the two OBBs and nothing else.
	//
	// The slot's `Vector, Vector, int` tail is IGNORED by this body: only the entity argument is
	// read. The payphone is an NPC that answers "is someone standing at me", which is what the
	// classname is for.
	const float DistanceUnits = static_cast<float>(
		(FMath::Abs(EyePosition().X - Other.EyePosition().X)
			+ FMath::Abs(EyePosition().Y - Other.EyePosition().Y)
			+ FMath::Abs(EyePosition().Z - Other.EyePosition().Z)) / ElysiumMove::U);
	if (!(DistanceUnits < GPayphoneManhattanLimitUnits))
	{
		return false;
	}
	// `m_Collision` slots +4 / +8 on both entities. Family **Motor**'s `RetailCollisionExtents` is
	// the seam for them and answers false with both boxes zeroed; retail's six comparisons over two
	// zero boxes anchored at the same place would answer TRUE, so refusing on the seam is this
	// port's choice and is stated: an unmeasurable box does not overlap.
	FVector MyMins = FVector::ZeroVector;
	FVector MyMaxs = FVector::ZeroVector;
	FVector OtherMins = FVector::ZeroVector;
	FVector OtherMaxs = FVector::ZeroVector;
	if (!RetailCollisionExtents(*this, MyMins, MyMaxs)
		|| !RetailCollisionExtents(Other, OtherMins, OtherMaxs))
	{
		return false;
	}
	const FVector MyOriginUnits = Origin / ElysiumMove::U;
	const FVector OtherOriginUnits = Other.Origin / ElysiumMove::U;
	const FVector A0 = MyOriginUnits + MyMins;
	const FVector A1 = MyOriginUnits + MyMaxs;
	const FVector B0 = OtherOriginUnits + OtherMins;
	const FVector B1 = OtherOriginUnits + OtherMaxs;
	return B1.X >= A0.X && B0.X <= A1.X && B1.Y >= A0.Y && B0.Y <= A1.Y
		&& B1.Z >= A0.Z && B0.Z <= A1.Z;
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

FElysiumEntity* FElysiumNpcPayphone::ResolveDialogPartner() const
{
	// `101aac0b`..`101aac2c`: the EHANDLE validity triple — index `& 0x1fff`, serial `>> 0xd` against
	// the slot's, and a non-null record. `FElysiumEntityWorld::Resolve` is that triple.
	if (!DialogPartner.IsSet() || World == nullptr)
	{
		return nullptr;
	}
	return const_cast<FElysiumEntityWorld*>(World)->Resolve(DialogPartner);
}

void FElysiumNpcPayphone::DialogUpkeepTick()
{
	// SEAM for `FUN_102c1400`. Counted; see the `.inl` for what the real body does and why it is not
	// one of this family's rows.
	++DialogUpkeepTicks;
}

bool FElysiumNpcPayphone::PayphoneThink()
{
	// `CPayphone::vfunc431` `0x101aabf0`. The whole think for a payphone; the Troika body never runs.

	// **What this body replaces is `NPCThink`, and NOT `NPCInit`.** Retail runs admission, the
	// combat loadout and a director's parked order at SPAWN, inside `NPCInit`; this runtime cannot
	// (creating an item entity inside the world's spawn pass invalidates the array being iterated,
	// and admission would wipe a forced state a director applied ahead of it), so it defers all
	// three onto the first normal-due think — see `FElysiumNpc::Think`, which calls them "the
	// port's own one-shot lifecycle". They are bookkeeping this runtime owes every NPC, not
	// statements of `0x10292de0`, so a species body that replaces the think must still run them or
	// the entity is never admitted and can never own its own body.
	//
	// It is not theoretical: without this, `FElysiumNpcMind::IsAcquisitionAllowed` refuses every
	// claim on an unadmitted mind, so `BeginDialogueBodySession` refuses and **a payphone cannot be
	// talked to at all** — which is what `Elysium.Substrate.DialogueCamera.RetailChain` caught.
	// All three are guarded one-shots, so calling them on every payphone pass is free.
	RunAdmissionBarrier();
	ResolveLoadout();
	ReplayDeferredScriptedOrder();

	// 1. `101aabf4 PUSH 0x0` / `101aabf8 CALL dword ptr [EAX + 0x3e8]` — slot 250
	//    `StudioFrameAdvance(0.0)`, and `101aac07 FSTP ST0` throws the returned interval away.
	(void)StudioFrameAdvance(0.0f);

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// 2. `101aabfe MOV EAX,dword ptr [ESI + 0xfe8]` — the partner arm.
	if (const FElysiumEntity* PartnerEntity = ResolveDialogPartner())
	{
		// `101aac31 CALL 0x100125bc` — the dialogue upkeep tick, unconditional on this arm.
		DialogUpkeepTick();

		// `101aac36 MOV EDI,[EBX + 0xff0]` — the PARTNER's `m_IdealActivity`. Only an NPC carries
		// the kernel's activity words in this runtime; a partner that is any other leaf answers the
		// activity it has, which is zero, and zero is exactly what retail reads out of a
		// `CBaseEntity` whose `+0xff0` was never written.
		const FElysiumNpc* PartnerNpc = PartnerEntity->AsNpc();
		const int32 PartnerIdealActivity = PartnerNpc != nullptr ? PartnerNpc->IdealActivityNumber : 0;

		// `101aac42 CMP EDI,EAX / JZ` — the sequence is re-selected ONLY on an activity change.
		if (PartnerIdealActivity != IdealActivityNumber)
		{
			SetIdealActivity(PartnerIdealActivity);                                 // 0x10272650
			SequenceNumber = SelectWeightedSequenceForActivity(PartnerIdealActivity);  // (act, -1)
			ResetSequenceInfo();                                                    // 0x10090950
		}

		// `101aac65 MOV EAX,[EBX + 0x6f8] / 101aac6c MOV [ESI + 0x6f8],EAX` — the partner's
		// `m_flCycle`, copied on EVERY pass. This is the frame-accurate half of the mirror.
		SequenceCycle = PartnerNpc != nullptr ? PartnerNpc->SequenceCycle : 0.f;

		// `101aac7b FADD float ptr [0x10450aa4]` — 0.01 s.
		NextThink = static_cast<float>(Now + PayphoneMirrorThinkSeconds);
		++PayphoneMirrorPasses;
		return true;
	}

	// 3. `101aac8a` — the no-partner arm. `IsInDialog()` gates only the tick.
	if (IsInDialog())
	{
		DialogUpkeepTick();
	}
	// `101aac9c PUSH 0x1` — `ACT_IDLE`, UNCONDITIONALLY, outside the `IsInDialog` test.
	SetIdealActivity(PayphoneIdleActivity);
	// `101aacae FADD float ptr [0x1044bef8]` — 0.25 s.
	NextThink = static_cast<float>(Now + PayphoneIdleThinkSeconds);
	++PayphoneIdlePasses;
	return true;
}

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// `CPayphone::NPCThink` — slot 431, `0x101aabf0`.
// -------------------------------------------------------------------------------------------------
