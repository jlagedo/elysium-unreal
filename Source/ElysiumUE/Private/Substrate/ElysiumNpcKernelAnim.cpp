#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Anim** — the animation layers, the flex/expression controllers and the
// scene-event queue of `order.md` layers 0–9.
//
// 35 rows, 24 of which fill a Troika-line vtable slot. Four concerns:
//
//   * **The gesture-layer table** (`m_AnimOverlay`, +0x0734) and **the scene-event queue**
//     (`m_SceneEvents`, +0x0a58) are retail's own DATA STRUCTURES. Their bookkeeping is the rule:
//     the strides, the search order, the growth arithmetic and the `memmove` compaction below are
//     reproduced field for field, and every constant in them was read out of the cited body.
//   * **The flex controllers and the pose parameters** are MECHANISMS in an animation instance.
//     This substrate's NPC reaches no studio header, so each goes through a seam that answers
//     nothing and names the retail call it stands for; the bodies around the seam are still
//     retail's, arm for arm, because the arms are what decide which seam is asked.
//   * **The activity commit** — `SetIdealActivity`, `ResolveActivityToSequence`,
//     `ForcePreTranslatedSequenceAndActivity`, `IsActivityFinished`, `ShouldMaintainActivity`,
//     `CanPlaySequence` — is the kernel's own, and lands whole.
//   * **The species branches** of four slots the generator still carries (245, 246, 250, 259).
//
// The walked prose is `docs/vtmb/npc-ai/shape.md`.
//
// One fact governs the flex half and is stated once: `LookupFlexController` (`0x100b5d10`) answers
// **0**, not -1, for a name it cannot find. Every caller in this file therefore writes or reads
// controller zero on a miss rather than dropping the request, and that is retail's behaviour, not
// an oversight in the port.

namespace
{
	// The NPC's `m_NPCState` in RETAIL's ordinals — `{0 NONE, 1 IDLE, 2 COMBAT, 3 ALERT, 4 SCRIPT,
	// 6 PRONE, 7 DEAD}`. `CanPlaySequence` and the base `ShouldMaintainActivity` both compare raw
	// numbers, so the mapping has to happen before the comparison rather than after it.
	int32 AnimRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}

	// `_DAT_104493d0`, the epsilon `AddSceneEvent`, `ProcessGestureSceneEvent` and the Troika loud
	// expression all add before a divide or a deadline. Read out of the pinned image's `.rdata` as a
	// double; every use narrows it to float exactly as retail's `FADD double ptr` then `FSTP float`
	// does.
	constexpr double GAnimTimeEpsilon = 0.0001;

	// The three retail activity numbers this family compares against, named once.
	constexpr int32 GAnimActIdle = 9;                  // the sequence fallback both misses take
	constexpr int32 GAnimActRun = 0x13;                // rewritten to `GAnimActIdle` by the run-to-walk rung
	constexpr int32 GAnimActDisposition = 0xf1;        // ACT_DISPOSITION, the whole-request retry
	constexpr int32 GAnimActScriptCustomMove = 0x18;   // ACT_SCRIPT_CUSTOM_MOVE

	// `0x102b8a10`'s two constants: the activity it probes for and the schedule number it answers.
	constexpr int32 GIdleGateActivity = 0x61;
	constexpr int32 GIdleGateSchedule = 8;

	// The schedule id `CAI_BaseNPCTroika::ShouldMaintainActivity` (`0x102bf510`) refuses for.
	constexpr int32 GScheduleRefusingMaintain = 0xb9;

	// Slot 259's EMPTY override — the two census classes whose `HandleAnimEvent` swallows every
	// animation event. One row per class with the retail address of its own body, so the table can
	// be checked against `docs/vtmb/npc-kernel/slots.md`.
	struct FAnimSwallowRow
	{
		const TCHAR* RetailClass;
		const TCHAR* Body259;
	};

	constexpr FAnimSwallowRow GSwallowAnimEvents[] =
	{
		{ TEXT("CNPC_VCamera"),         TEXT("0x10368ec0") },
		{ TEXT("CNPC_VCameraSecurity"), TEXT("0x10368ec0") },
	};

	// Slots 245/246's species override — the three classes that forward their extra animation models
	// to a possessing player's own model list. `CNPC_VPlayerController` carries the bodies and the
	// other two share them.
	struct FAnimExtraModelRow
	{
		const TCHAR* RetailClass;
		const TCHAR* Body245;
		const TCHAR* Body246;
	};

	constexpr FAnimExtraModelRow GExtraModelForwarders[] =
	{
		{ TEXT("CNPC_VFrenzyShadow"),     TEXT("0x103a49c0"), TEXT("0x103a4a60") },
		{ TEXT("CNPC_VPlayerController"), TEXT("0x103a49c0"), TEXT("0x103a4a60") },
		{ TEXT("CNPC_VWolfMorph"),        TEXT("0x103a49c0"), TEXT("0x103a4a60") },
	};

	// Retail's two `DevWarning` strings on the extra-model pair, quoted so the port's log line is the
	// one a reader of the binary would search for.
	const TCHAR* const GWarnAddExtraModels =
		TEXT("Player Controller NPC adding extra animations, but is not attached to a player. This ")
		TEXT("is probably bad.");
	const TCHAR* const GWarnRemoveExtraModels =
		TEXT("Player Controller NPC removing extra animations, but is not attached to a player. ")
		TEXT("This is probably bad.");
}

// --- The animating-tier seams -------------------------------------------------------------------
//
// Every one of these is a studio-header read this substrate's NPC cannot make. They are here, at the
// top, because the bodies below are written against them and a reader has to be able to see in one
// place exactly how much of this family is standing on nothing.

int32 FElysiumNpc::NumFlexControllers() const
{
	// `CBaseAnimating::GetNumFlexControllers` (`0x10012530`, reached from every body in the flex
	// half). **SEAM**: no studio header at this tier, so the table is empty.
	return 0;
}

FString FElysiumNpc::FlexControllerName(int32 Index) const
{
	// `CBaseAnimating::GetFlexControllerName(int)`. **SEAM**, and unreachable while the count above
	// answers 0.
	(void)Index;
	return FString();
}

bool FElysiumNpc::FlexControllerRange(int32 Index, float& OutMin, float& OutMax) const
{
	// The studio flex-controller descriptor `studiohdr + studiohdr->flexcontrollerindex (+0x164) +
	// index * 0x14`, whose `+0xc` is `min` and `+0x10` is `max` (`0x100b5ba0` writes through it,
	// `0x100b5c50` reads back through it). **SEAM**: `GetModelPtr()` answers null here, which is the
	// arm both slots already have — they return 0 / pass the stored weight through untouched.
	(void)Index;
	OutMin = 0.f;
	OutMax = 0.f;
	return false;
}

int32 FElysiumNpc::LookupSequenceByName(const TCHAR* Name) const
{
	// `CBaseAnimating::LookupSequence(const char*)`. **SEAM**, answering -1 — retail's own "this
	// model authors no such sequence", which every caller here already branches on.
	(void)Name;
	return INDEX_NONE;
}

int32 FElysiumNpc::SequenceFlagsOf(int32 Sequence) const
{
	// `CBaseAnimating::GetSequenceFlags(int)` / `GetSeqDesc(seq)->flags` (studio `+0x8`). **SEAM**,
	// answering 0: no LOOPING bit (so `AddSceneEvent`'s gesture arm raises no vcd warning) and no
	// SNAP bit (so `SetOverlayLayer` keeps retail's 0.2 envelope).
	(void)Sequence;
	return 0;
}

int32 FElysiumNpc::SequenceActivityOf(int32 Sequence) const
{
	// `CBaseAnimating::GetSequenceActivity(int)`. **SEAM**, answering -1.
	(void)Sequence;
	return INDEX_NONE;
}

float FElysiumNpc::SequenceDurationOf(int32 Sequence) const
{
	// `CBaseAnimating::SequenceDuration(int)`. **SEAM**, answering 0.
	(void)Sequence;
	return 0.f;
}

void FElysiumNpc::ResetSequenceInfo()
{
	// `CBaseAnimating::ResetSequenceInfo` (`0x10090950`). **SEAM**: this runtime's clip funnel owns
	// the playback rate and the clip length, so there is nothing to re-read.
}

void FElysiumNpc::CommitForcedSequence(int32 Sequence)
{
	// `0x10260a50`, the `SetSequence`-plus-bookkeeping helper
	// `ForcePreTranslatedSequenceAndActivity` hands its forced sequence to. **SEAM**: the number is
	// stored on the kernel's own `m_nSequence` word and nothing downstream reads it yet.
	SequenceNumber = Sequence;
}

int32 FElysiumNpc::LookupPoseParameter(const TCHAR* Name) const
{
	// `CBaseAnimating::LookupPoseParameter(const char*)`. **SEAM**, answering -1. Family Facing's
	// `PoseParameterWrites` is where a pose-parameter write actually lands in this runtime.
	(void)Name;
	return INDEX_NONE;
}

float FElysiumNpc::NormalizePoseParameter(int32 Index, float Value) const
{
	// `0x100c43e0(studiohdr, index, value, &out)` — the studio pose-parameter range normaliser
	// `AddFlinchGesture` runs its authored value through. **SEAM**: with no studio header there is
	// no range, so the identity is what a 0..1 parameter would have given anyway.
	(void)Index;
	return Value;
}

float FElysiumNpc::PlayInstancedScene(const TCHAR* SceneFile)
{
	// `CBaseFlex::PlayScene`'s body (`0x10084b40`): `CreateNoSpawn("instanced_scripted_scene")`, set
	// the scene file and the target handle, `Spawn()` (slot 103) and slot 242, then
	// `LoadSceneFromFile`; on failure `Msg("Unknown scene specified: %s\n")` and 0, else the scene's
	// own play length.
	//
	// **SEAM**: standing that entity needs the world's factory and the scene cache, neither of which
	// the substrate's NPC reaches. -1 is this seam's "could not stand it" and `PlayScene` below turns
	// it into retail's own failure arm.
	(void)SceneFile;
	return -1.f;
}

bool FElysiumNpc::SceneEntityForcesCutsceneLod(const void* SceneEntity) const
{
	// `CSceneEntity + 0x57d`, the byte slots 59/60/61 gate `m_bCutsceneForceLOD` on. **SEAM**: the
	// scene entity arrives as the generated `void*` and this substrate stands no `CSceneEntity`
	// layout, so the answer is retail's own "this scene does not force LOD".
	(void)SceneEntity;
	return false;
}

// --- Slot 250's humanoid branch, and slot 259's camera branch -----------------------------------

float FElysiumNpc::StudioFrameAdvanceHumanoid(float Interval)
{
	// `CAI_BaseHumanoid::vfunc250` `0x1025e4e0`, the whole 27-byte body: clear bits 0 and 1 of
	// +0x5f4c — family Facing's `HumanoidHeadCacheBits`, the "cached eye/head direction is valid"
	// pair — then chain to `CBaseAnimating::StudioFrameAdvance` (`0x10098bb0`, slot 250's own body
	// and still the generator's stub). Clearing first is the point: the cached vectors recompute on
	// the next read rather than lagging the frame the model just advanced.
	HumanoidHeadCacheBits &= ~3u;
	return StudioFrameAdvance(Interval);
}

bool FElysiumNpc::SwallowsAnimEvents() const
{
	// `CNPC_VCamera::HandleAnimEvent` `0x10368ec0` — three bytes, an empty body that ignores its
	// event id. Slot 259 for both camera classes.
	const FElysiumNpcClass* const Cls = RetailClass();
	if (Cls == nullptr)
	{
		return false;
	}
	for (const FAnimSwallowRow& Row : GSwallowAnimEvents)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Row.RetailClass))
		{
			return true;
		}
	}
	return false;
}

// --- Slots 245/246's species branch -------------------------------------------------------------

void FElysiumNpc::AddExtraAnimationModelsPlayerController(FElysiumEntity* ExtraModel,
	const TCHAR* AttachmentA, const TCHAR* AttachmentB, int32 FlagsA, int32 FlagsB)
{
	// `CNPC_VPlayerController::AddExtraAnimationModels` `0x103a49c0`, in retail's order:
	//   1. `CBaseAnimating::AddExtraAnimationModels(...)` — the base's own list (**SEAM**: this
	//      runtime composes extra models through the character catalog, so the request is recorded).
	//   2. slot 97 `GetOwnerEntity()`; when it resolves AND its `+0xa8` (`m_pPlayer`, the
	//      self-downcast cache the `CBasePlayer` constructor fills) is set, forward the same five
	//      arguments to that player's vtable `+0x3d4` — slot 245 again, on the player.
	//   3. otherwise `DevWarning` and stop.
	FExtraAnimationModelRequest Request;
	Request.Model = ExtraModel != nullptr ? ExtraModel->Handle : FElysiumEntityHandle();
	Request.AttachmentA = AttachmentA != nullptr ? FString(AttachmentA) : FString();
	Request.AttachmentB = AttachmentB != nullptr ? FString(AttachmentB) : FString();
	Request.FlagsA = FlagsA;
	Request.FlagsB = FlagsB;

	// `+0xa8` is `m_pPlayer`, the self-downcast cache only `CBasePlayer`'s constructor fills, so the
	// forwarding arm is exactly "the owner IS the player".
	Request.bForwardedToMaster = OwnerIsThePlayer();
	const bool bForwarded = Request.bForwardedToMaster;
	ExtraAnimationModels.Add(MoveTemp(Request));

	if (!bForwarded)
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s"), GWarnAddExtraModels);
	}
}

bool FElysiumNpc::OwnerIsThePlayer() const
{
	// Slot 97 `GetOwnerEntity()` followed by that entity's `+0xa8`. This runtime's player entity is
	// the one `FElysiumEntityWorld::PlayerHandle()` names, so the two-step is one comparison.
	return World != nullptr && OwnerEntity.IsSet() && OwnerEntity == World->PlayerHandle();
}

void FElysiumNpc::RemoveExtraAnimationModelsPlayerController()
{
	// `CNPC_VPlayerController::RemoveExtraAnimationModels` `0x103a4a60` — the exact inverse, read off
	// the listing: clear the base's own list (`0x1008e310`), ask slot 97 for the owner, and TAIL JUMP
	// into the owner's `+0xa8` object's `+0x3d8` (slot 246). With no owner, or an owner that is not a
	// player, the warning is the whole body.
	ExtraAnimationModels.Reset();

	if (!OwnerIsThePlayer())
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s"), GWarnRemoveExtraModels);
	}
}

// --- `CNPC_VMingXiao::BodyGroup` `0x10398800` ---------------------------------------------------

bool FElysiumNpc::BodyGroupCvarIsCommand() const
{
	// `ConVar::IsCommand()` on `DAT_1093bb14`. **SEAM**: the object lives in uninitialised `.data`
	// and no corpus function constructs it, so its NAME and DEFAULT are **unrecovered**. False is
	// "this is a real convar", which is what a registered one answers.
	return false;
}

int32 FElysiumNpc::BodyGroupCvarValue() const
{
	// `ConVar::m_nValue` on the same object. **SEAM**, answering -1 — the negative arm, which is the
	// one that takes the severed-tentacle mask and therefore the shipped behaviour.
	return -1;
}

void FElysiumNpc::BodyGroup()
{
	// `0x10398800`, three arms in retail's order. The cvar is read TWICE, and the second read is not
	// redundant: retail asks `IsCommand()` again rather than caching it.
	if (!BodyGroupCvarIsCommand() && BodyGroupCvarValue() < 0)
	{
		NpcBody = SeveredTentacleMask;
		return;
	}
	if (BodyGroupCvarIsCommand())
	{
		NpcBody = 0;
		return;
	}
	NpcBody = BodyGroupCvarValue();
}

// --- The gesture-layer table (slots 265, 269, 270, 273, 274, 275) -------------------------------
//
// Four records of 0x30 bytes from +0x0734. `GetFirstGestureLayer()` (slot 267) answers 0 for every
// class in the hierarchy, so every scan below starts at 0 and no slot is reserved.

void FElysiumNpc::SetOverlayLayer(int32 SlotIndex, int32 Activity, int32 Sequence, bool bAutoKill)
{
	// `CBaseAnimatingOverlay::SetLayer` `0x10099020`, field for field and in retail's own write
	// order. The four numbers are `ElysiumOverlay::`'s, which is where the render-side stack reads
	// the same constants from — one spelling, so the two tables cannot drift.
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	FAnimOverlayLayer& Layer = AnimOverlay[SlotIndex];
	Layer.Activity = Activity;                                  // +0x24
	Layer.Cycle = 0.f;                                          // +0x0c
	Layer.PlaybackRate = 1.f;                                   // +0x10
	Layer.Sequence = Sequence;                                  // +0x08
	Layer.BlendIn = ElysiumOverlay::DefaultBlendFraction;       // +0x1c
	Layer.BlendOut = ElysiumOverlay::DefaultBlendFraction;      // +0x20
	Layer.Weight = ElysiumOverlay::SeedWeight;                  // +0x14 — the occupancy marker
	Layer.WeightMax = ElysiumOverlay::WeightMax;                // +0x18
	Layer.bAutoKillWhenFinished = bAutoKill;                    // +0x28
	Layer.SequenceFinished = 0;                                 // +0x04
	Layer.LastEventCheck = 0.f;                                 // +0x2c
	// `m_fFlags` (+0x00) is NOT written: retail leaves it alone, and so does this.

	// The envelope comes from the SEQUENCE, never from the pusher: a SNAP sequence (studio
	// `flags@8 & 0x2`) gets no blend at all and stands at full weight from the frame it is armed.
	if ((SequenceFlagsOf(Sequence) & 0x2) != 0)
	{
		Layer.BlendIn = 0.f;
		Layer.BlendOut = 0.f;
	}
}

int32 FElysiumNpc::FindGestureLayerByOwner(int32 Activity) const
{
	// `CBaseAnimatingOverlay::FindGestureLayer` `0x100994c0`, slot 271's body. Three terms in
	// retail's own order: the slot is LIVE (`m_flWeight != 0`), its owner is not `ACT_INVALID` (-1),
	// and its owner is the activity asked for. -1 when nothing matches, which is the "found"
	// convention slots 270 and 274 both test against.
	//
	// Stood as a named method beside slot 271's own stub, which is 29c's and answers 0 — the
	// opposite of retail's miss. Every row of this family that searches the table goes through here.
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		const FAnimOverlayLayer& Layer = AnimOverlay[Index];
		if (Layer.Weight != 0.f && Layer.Activity != -1 && Layer.Activity == Activity)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

int32 FElysiumNpc::AllocateGestureLayer() const
{
	// `CBaseAnimatingOverlay::AllocateLayer` `0x10099470`, slot 272's body: the lowest slot whose
	// weight is exactly zero, starting at `GetFirstGestureLayer()` — which answers 0 for every class
	// in the hierarchy, so no slot is reserved. -1 when all four are held; there is no eviction and
	// no priority displacement.
	for (int32 Index = 0; Index < ElysiumOverlay::NumSlots; ++Index)
	{
		if (AnimOverlay[Index].Weight == 0.f)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpc::HasLayer(int32 Activity)
{
	// `0x10099540`, slot 270: the whole 22-byte body is the lookup and a `!= -1`.
	return FindGestureLayerByOwner(Activity) != INDEX_NONE;
}

void FElysiumNpc::RemoveLayer(int32 SlotIndex)
{
	// `0x10099660`, slot 269: two writes, in retail's own order — the WEIGHT first (index
	// `i*0xc + 10` off `m_angPrevSeqAngles`, i.e. +0x0748 on layer 0) and then the SEQUENCE (index
	// `i*0xc + 7`, +0x073c). Nothing else on the record is touched, so the owner activity survives a
	// removal; zeroing the weight is what frees the slot, because occupancy IS the zero-weight test.
	if (SlotIndex < 0 || SlotIndex >= ElysiumOverlay::NumSlots)
	{
		return;
	}
	AnimOverlay[SlotIndex].Weight = 0.f;
	AnimOverlay[SlotIndex].Sequence = 0;
}

void FElysiumNpc::RemoveLayerByOwner(int32 Activity)
{
	// `0x100995e0`, slot 274: the same two writes as slot 269, behind the owner lookup. It is NOT a
	// call to slot 269 — retail inlines the pair — but the effect is identical and the port says so
	// by going through the one spelling.
	const int32 Index = FindGestureLayerByOwner(Activity);
	if (Index != INDEX_NONE)
	{
		AnimOverlay[Index].Weight = 0.f;
		AnimOverlay[Index].Sequence = 0;
	}
}

void FElysiumNpc::RemoveAllGestures()
{
	// `0x10099630`, slot 275: four iterations over the record stride, zeroing `m_nSequence` (+0x00 of
	// the pair) and `m_flWeight` (+0x0c past it). The bound is FOUR because `m_Flinch[0]` begins
	// exactly where a fifth record would.
	for (FAnimOverlayLayer& Layer : AnimOverlay)
	{
		Layer.Weight = 0.f;
		Layer.Sequence = 0;
	}
}

void FElysiumNpc::RestartGesture(int32 Activity, bool bAddIfMissing, bool bAutoKill)
{
	// `0x10099570`, slot 273, read off the listing rather than the decompilation (Ghidra bound the
	// `addifmissing` test to `param_1`; `MOV AL, [ESP+0x10]` says it is `param_2`):
	//   layer = FindLayerByOwner(activity)
	//   if (layer != -1)  m_AnimOverlay[layer].m_flCycle = 0        // rewind, keep the weight
	//   else if (addifmissing) AddGesture(activity, autokill)
	//
	// The rewind writes the CYCLE and nothing else, so a restarted gesture keeps its envelope and its
	// accumulated weight and simply plays again from the head.
	const int32 Index = FindGestureLayerByOwner(Activity);
	if (Index != INDEX_NONE)
	{
		AnimOverlay[Index].Cycle = 0.f;
		return;
	}
	if (!bAddIfMissing)
	{
		return;
	}

	// `CBaseAnimatingOverlay::AddGesture` `0x100991b0`: `HasLayer` first (which cannot answer true
	// here — the lookup above already missed), then `SelectWeightedSequence(activity)`, and a
	// sequence of **1 or less** is the refusal, not just -1. `AllocateLayer` then takes the lowest
	// zero-weight slot, and a full stack refuses outright.
	const int32 Sequence = SelectWeightedSequenceForActivity(Activity);
	if (Sequence < 1)
	{
		// Retail's `DevMsg("CBaseAnimatingOverlay::AddGesture : unable to find %s\n", …)`.
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("CBaseAnimatingOverlay::AddGesture : unable to find activity %d"), Activity);
		return;
	}
	const int32 NewSlot = AllocateGestureLayer();
	if (NewSlot == INDEX_NONE)
	{
		return;
	}
	SetOverlayLayer(NewSlot, Activity, Sequence, bAutoKill);
}

void FElysiumNpc::AddFlinchGesture(int32 Activity, float FadeIn, float FadeOut,
	const TCHAR* PoseParameter, float PoseValue)
{
	// `0x10099690`, slot 265. Two gates, then the victim scan, then the write.
	//
	// `IsAlive()` (slot 158, vtable +0x278) and `m_bNoFlinch` (+0x0730) are BOTH refusals, and their
	// order is retail's: a dead body never flinches whatever the flag says.
	if (!IsAlive() || bNoFlinch)
	{
		return;
	}

	// The earliest-expiring of the three records wins, scanning 1 and 2 against the running best and
	// starting at 0 — so with three equal stamps record 0 is reused, and a strictly-earlier later
	// record displaces it.
	int32 Best = 0;
	for (int32 Candidate = 1; Candidate < NumFlinchRecords; ++Candidate)
	{
		if (Flinch[Candidate].ExpireTime < Flinch[Best].ExpireTime)
		{
			Best = Candidate;
		}
	}

	const int32 Sequence = SelectWeightedSequenceForActivity(Activity);
	if (Sequence < 0)
	{
		// No clip for the flinch activity: the record is left exactly as it was, stamp included.
		return;
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FFlinchRecord& Record = Flinch[Best];
	Record.Sequence = Sequence;                       // +0x00
	Record.FadeOut = FadeOut;                         // +0x0c
	Record.PoseParamIndex = 0x18;                     // +0x10 — seeded, then overwritten below
	Record.FadeIn = FadeIn;                           // +0x08
	Record.Latch = (Record.Latch + 1) & 3;            // +0x04 — retail's own 2-bit rotation
	Record.ExpireTime = FadeIn + static_cast<float>(Now) + FadeOut;   // +0x18

	// The pose parameter is OPTIONAL and is the last thing written; a name that resolves to nothing
	// leaves the seeded 0x18 standing, which is retail's behaviour and not a fallback this port adds.
	if (PoseParameter != nullptr)
	{
		const int32 Index = LookupPoseParameter(PoseParameter);
		if (Index >= 0)
		{
			Record.PoseParamIndex = Index;
			Record.PoseParamValue = NormalizePoseParameter(Index, PoseValue);
		}
	}
}

// --- The flex controllers (slots 280, 281, 282) -------------------------------------------------

int32 FElysiumNpc::LookupFlexController(const TCHAR* Name) const
{
	// `0x100b5d10`, the whole body. The count is re-read every iteration — retail calls
	// `GetNumFlexControllers()` again at the bottom of the loop rather than caching it — and the miss
	// answer is **0**, not -1.
	int32 Index = 0;
	int32 Count = NumFlexControllers();
	if (Count > 0)
	{
		do
		{
			if (FlexControllerName(Index).Equals(Name != nullptr ? Name : TEXT(""),
					ESearchCase::IgnoreCase))
			{
				return Index;
			}
			++Index;
			Count = NumFlexControllers();
		} while (Index < Count);
	}
	return 0;
}

void FElysiumNpc::SetFlexWeight(TCHAR* Name, float Value)
{
	// `0x100b5b60`, slot 280: resolve the name and dispatch slot 279 (vtable +0x45c), which is the
	// INDEX overload and 29c's, not this family's. Two statements, and the vtable hop is retail's —
	// a species that replaced slot 279 would be reached through it.
	SetFlexWeight(LookupFlexController(Name), Value);
}

float FElysiumNpc::GetFlexWeight(TCHAR* Name)
{
	// `0x100b5c20`, slot 282: the mirror of slot 280 — resolve, then dispatch slot 281 (+0x464).
	return GetFlexWeight(LookupFlexController(Name));
}

float FElysiumNpc::GetFlexWeight(int32 Index)
{
	// `0x100b5c50`, slot 281. Read off the listing, because the decompilation is DAMAGED exactly
	// where the answer is: the two `FLD`s at `0x100b5c8d` and `0x100b5ca2` are the two arms.
	//
	//   index < 0                       -> 0
	//   index >= GetNumFlexControllers  -> 0
	//   no studio header                -> 0
	//   max != min                      -> (max - min) * m_flexWeight[index] + min
	//   max == min                      -> m_flexWeight[index]
	//
	// So slot 281 DE-NORMALISES: slot 279 stores `(value - min) / (max - min)` and this maps it back
	// into the controller's authored range. 29c named the port method `FlexControllerRange` for the
	// range read inside it; the range read is the seam above and the body is the slot.
	if (Index < 0 || Index >= NumFlexControllers() || Index >= NumFlexWeightSlots)
	{
		return 0.f;
	}
	float Min = 0.f;
	float Max = 0.f;
	if (!FlexControllerRange(Index, Min, Max))
	{
		return 0.f;
	}
	if (Max != Min)
	{
		return (Max - Min) * FlexWeight[Index] + Min;
	}
	return FlexWeight[Index];
}

// --- The expression writers (slots 288, 289) ----------------------------------------------------

void FElysiumNpc::AddFlexSetting(const TCHAR* Expression, float Scale, void* SettingsHeader,
	void* OverrideHeader, bool bCheckStateChange)
{
	// `0x100b6cf0`, slot 288, over a `flexsettinghdr_t` — a `.vfe` expression file. The body's SHAPE
	// is recovered and reproduced below; its operand BINDING is not, and that is stated rather than
	// guessed.
	//
	// Recovered, in retail's order:
	//   1. linear `__strcmpi` scan of the header's setting table (`hdr+0x8c` count, `hdr+0x90`
	//      offset, 0x18-byte stride) for `Expression`; a miss is the whole refusal.
	//   2. when `bCheckStateChange` and the found setting's `type` is 1, run the preset chain
	//      (`0x100b68d0`).
	//   3. resolve an optional preset/sub-index indirection (`type == 1` -> `index` -> the setting
	//      that index names, `0x100b6f10`), then an optional OVERRIDE header lookup by the same name
	//      (`0x100b6850`), which REPLACES both the setting and the header it is read from.
	//   4. for every `(key, weight)` pair the setting carries, map `key` through the header's index
	//      table (`hdr+0xa8`) to a flex-controller number, read that controller through slot 281 and
	//      write it back through slot 279, blended by `Scale`.
	//
	// **Unrecovered:** the exact blend. Ghidra scrambles the `__thiscall` argument order here — the
	// same `param_1` is used as a `char*` for the name compare and as a `float` in the blend — so
	// whether the write is `current + Scale * weight` or the SDK's
	// `current * (1 - s) + weight * s` cannot be settled from this body, and the corpus holds exactly
	// one caller, which passes constants through a thunk. It is left unported rather than invented.
	//
	// **SEAM**: nothing in this runtime carries a `flexsettinghdr_t` — no `.vfe` is exported and the
	// animating tier stands no expression table — so both headers arrive null and step 1 refuses,
	// which is retail's own answer for a name no setting file holds.
	(void)Expression;
	(void)Scale;
	(void)bCheckStateChange;
	if (SettingsHeader == nullptr && OverrideHeader == nullptr)
	{
		return;
	}
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("AddFlexSetting: a flexsettinghdr_t reached the kernel, which this runtime does not ")
		TEXT("carry (retail 0x100b6cf0)"));
}

void FElysiumNpc::AddFlexAnimation(void* SceneEventInfo)
{
	// `0x100b6960`, slot 289, over one queued scene-event record. Three phases in retail's order.
	FSceneEventRecord* const Record = static_cast<FSceneEventRecord*>(SceneEventInfo);
	if (Record == nullptr || Record->Event == nullptr || Record->Scene == nullptr)
	{
		// `param_1 != 0 && param_1[0] != 0 && param_1[1] != 0` — all three, and the record's own
		// "processed" byte is raised only at the very end.
		return;
	}
	const FElysiumSceneEvent& Event = *Record->Event;

	// Phase 1 — RESOLVE, once per event. Retail latches it on the event
	// (`0x10077710` reads the latch, `0x10077730(event, 1)` raises it); this port latches on the
	// queue record, because the parsed scene is shared immutable data here and a per-event write
	// would be a write into the scene cache.
	//
	// Each flex-animation track names a flex controller. A COMBO track (`0x10074a50`) names two —
	// the same base name prefixed `right_` and `left_` — and retail builds both by `Q_strncpy`ing the
	// prefix into a 512-byte buffer and appending the track name; a plain track resolves its own name
	// once. `LookupFlexController` is the resolver in every case, so a name that matches nothing
	// resolves to controller 0.
	if (!Record->bResolved)
	{
		// **SEAM**: `FElysiumSceneEvent` carries no flex-animation track list — the `.vcd` reader
		// parses the `flexanimations` block's presence but not its tracks — so the track count is
		// zero and the resolve pass has nothing to walk. The prefix rule is recorded here because it
		// is the recovered fact, and it is what the track list will be walked with.
		Record->bResolved = true;
	}

	// Phase 2 — the scene's own clock, and the event's ramp sampled at it. Retail calls
	// `CChoreoScene::GetTime()` (`0x1007dfa0`) and `CChoreoEvent::GetIntensity` (`0x10076280`) and
	// **discards** the intensity (`FSTP ST0`); the number is kept here so the arithmetic is
	// assertable without inventing an effect retail does not have.
	const float SceneTime = Record->StartTime;
	Record->LastIntensity = Event.RampAt(SceneTime - Event.StartTime);

	// Phase 3 — the write pass, once per ACTIVE track (`0x100740d0`), and twice for a combo track.
	// The formula, recovered by shape from `0x100b69xx`'s blend:
	//     SetFlexWeight(j, trackValue * t + (1 - t) * GetFlexWeight(j))
	// a LERP of the controller toward the track's own value by the track's intensity `t`, which is
	// `CFlexAnimationTrack::GetIntensity(sceneTime, side)` (`0x10074610`). A controller index below
	// zero is skipped. With no track list this pass is empty.

	// The record's "expressions applied" byte (+0x08), raised whatever the pass did.
	Record->bResolved = true;
}

// --- The scene-event queue (slots 283, 286, 290, 291) -------------------------------------------

int32 FElysiumNpc::GrowSceneEventCapacity(int32 Current, int32 GrowSize, int32 Needed)
{
	// `CUtlMemory::Grow`, as `0x100b5e60` inlines it. A grow step of -1 is retail's "external memory,
	// do not grow" and the insert is skipped entirely rather than reallocating.
	if (GrowSize == -1 || Current >= Needed)
	{
		return Current;
	}
	int32 Capacity = Current;
	while (Capacity < Needed)
	{
		if (Capacity == 0)
		{
			Capacity = 2;
		}
		else if (GrowSize == 0)
		{
			Capacity = Capacity * 2;
		}
		else
		{
			Capacity = Capacity + GrowSize;
		}
	}
	return Capacity;
}

void FElysiumNpc::AddSceneEventBase(const FElysiumSceneData* Scene, const FElysiumSceneEvent* Event)
{
	// `CBaseFlex::AddSceneEvent` `0x100b5e60` — the base-line body of slot 286, reached from the
	// Troika override's `default` arm.
	if (Scene == nullptr || Event == nullptr)
	{
		// Retail's `Msg("CBaseFlex::AddExpression: scene or event was null!\n")`.
		UE_LOG(LogElysiumNpcEnt, Warning,
			TEXT("CBaseFlex::AddExpression: scene or event was null!"));
		return;
	}

	FSceneEventRecord Record;
	Record.Event = Event;      // +0x00
	Record.Scene = Scene;      // +0x04
	Record.bResolved = false;  // +0x08

	// Two of the nineteen event types arm the record; every other type queues it inert.
	if (Event->Type == EElysiumChoreoEvent::Gesture)
	{
		// Type 6. `LookupSequence(event->GetParameters())`, then — only on a hit — the scene's own
		// "actor is busy" mark (`0x100766d0` / `0x1007a340`), the LOOPING warning, and the cycle the
		// gesture is entered at.
		const int32 Sequence = LookupSequenceByName(*Event->Param);
		Record.Handle = -1;
		Record.Sequence = Sequence;
		if (Sequence >= 0)
		{
			if ((SequenceFlagsOf(Sequence) & 0x1) != 0)
			{
				// Retail: `DevMsg(1, "vcd error: gesture %s of model %s is marked as a loop!\n", …)`.
				UE_LOG(LogElysiumNpcEnt, Verbose,
					TEXT("vcd error: gesture %s is marked as a loop"), *Event->Param);
			}
			// The stamp is the animation clock wound BACK by however far into the event the scene
			// already is, so a gesture queued mid-event enters at the right cycle rather than at 0.
			Record.StartTime =
				AnimTime - (SceneTimeOf(*Scene) - Event->StartTime);
			Record.LastGestureCycle = static_cast<float>(
				((AnimTime - Record.StartTime) + GAnimTimeEpsilon)
					/ FMath::Max(Event->GetDuration(), UE_KINDA_SMALL_NUMBER));
		}
	}
	else if (Event->Type == EElysiumChoreoEvent::Sequence)
	{
		// Type 7. The same lookup, the same busy mark, and the stamp is simply NOW — a sequence event
		// always starts at its own head.
		const int32 Sequence = LookupSequenceByName(*Event->Param);
		Record.Sequence = Sequence;
		Record.Handle = -1;
		if (Sequence >= 0)
		{
			Record.StartTime = AnimTime;
		}
	}

	// The queue itself. `m_Size + 1` against `m_nAllocationCount`, the grow, then an insert AT THE
	// END — retail's inlined `InsertBefore(m_Size)` computes a shift count of exactly zero, so the
	// `memmove` at `0x10430fa0` never moves anything on an add. The mirror at +0x0a68 is written and
	// never read back.
	const int32 OldCount = SceneEvents.Num();
	const int32 Needed = OldCount + 1;
	if (SceneEventsAllocated < Needed && SceneEventsGrowSize != -1)
	{
		SceneEventsAllocated = GrowSceneEventCapacity(SceneEventsAllocated, SceneEventsGrowSize,
			Needed);
		SceneEvents.Reserve(SceneEventsAllocated);
	}
	SceneEvents.Add(MoveTemp(Record));
}

float FElysiumNpc::SceneTimeOf(const FElysiumSceneData& Scene) const
{
	// `CChoreoScene::GetTime()` (`0x1007dfa0`) — one float at scene+0x7c. **SEAM**: `FElysiumSceneData`
	// is the PARSED file and holds no clock; the clock lives on `FElysiumScenePlayer`, which the
	// kernel does not reach. Answers 0, which makes a gesture's wind-back its own start time.
	(void)Scene;
	return 0.f;
}

bool FElysiumNpc::HasLiveDialogPartner() const
{
	// `m_hDialogPartner` (+0x0fe8) resolving to a live entity. This runtime carries the partner as
	// the open dialogue SESSION, which is the reading `ElysiumNpcKernelSounds.cpp` made for
	// `CAI_BaseNPCTroika::IsInDialog` and is repeated rather than re-derived.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return Dialogue.bInDialog || IsTalking(Now);
}

bool FElysiumNpc::DispositionStanceReaction(float& OutThreshold, float& OutChancePercent) const
{
	// `0x100ec5d0` over the disposition record: `+0x108` is the intensity threshold the event's own
	// parameter must EXCEED and `+0x10c` the percentage `RandomInt(1, 100)` is rolled against. The
	// index is clamped to the table's default (`table+0x4`) when it is outside `0..table+0x14`.
	// **SEAM**: `FElysiumDisposition` carries the row's animation, fidget and eye blocks and not this
	// pair, so the Silence arm of slot 286 refuses.
	OutThreshold = 0.f;
	OutChancePercent = 0.f;
	return false;
}

bool FElysiumNpc::DispositionLoudExpression(FString& OutExpression, float& OutFadeIn,
	float& OutFadeOut, float& OutMinLevel, float& OutMaxLevel) const
{
	// `0x100ec450` over the same record: `RandomInt(0, record[+0x21c] - 1)` picks one of the row's
	// 0x40-byte expression names at `record + 0x11c + roll * 0x40`, and the four floats come from
	// `+0x220` (fade in), `+0x224` (fade out), `+0x228` (minimum level) and `+0x22c` (maximum).
	// A fifth out-parameter at `+0x230` is filled and never read by this caller.
	// **SEAM**: the same unbuilt block; the Loud arm refuses.
	OutExpression.Reset();
	OutFadeIn = 0.f;
	OutFadeOut = 0.f;
	OutMinLevel = 0.f;
	OutMaxLevel = 0.f;
	return false;
}

void FElysiumNpc::AddScriptedExpression(const FString& Expression, float FadeIn, float FadeOut,
	float Scale, float Delay, float Duration)
{
	// `CBaseCombatCharacter::AddScriptedExpression`. **SEAM**: the list is
	// `CUtlVector<ScriptedExpression_t>` at +0x1568 and no port member claims it. Recorded so the
	// two arms that raise one are assertable.
	ScriptedExpressions.Add(FScriptedExpressionRequest{ Expression, FadeIn, FadeOut, Scale, Delay,
		Duration });
}

int32 FElysiumNpc::ChangeStanceForReaction()
{
	// `ChangeStance` `0x102c1230` as slot 286's Silence arm consumes it. Retail:
	//     do { n = RandomInt(0, 2); } while (n == m_CurrStance);
	//     seq = g_DispositionTable.TransitionSequence(this, m_CurrStance, n);   // 0x100ecfc0
	//     m_CurrStance = n;  m_flStanceTime = curtime;
	//     return seq;                                     // left in EAX and read by the caller
	//
	// `ElysiumStance::ChangeStance` is that body in this runtime and is cited there; it answers by
	// CLIP NAME, so the transition sequence is resolved back through `LookupSequence`, which is a
	// seam and therefore -1 — the arm on which the whole reaction is dropped.
	if (!EnsureStanceResolved())
	{
		return INDEX_NONE;
	}
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumStanceChoice Choice = ElysiumStance::ChangeStance(StanceClips, Stance, Now,
		Stream);
	if (!Choice.IsSet())
	{
		return INDEX_NONE;
	}
	return LookupSequenceByName(*Choice.Clip);
}

void FElysiumNpc::CallPythonDialogFunction(const FString& FunctionName)
{
	// `CDialogDependency::CallPyDialogFunc(func, partner, this, 0x102, nullptr)`. **SEAM**: the
	// port's Python dialogue surface is reached through the dialogue session rather than from the
	// kernel, so the request is recorded and nothing is called.
	PythonDialogCalls.Add(FunctionName);
}

bool FElysiumNpc::CineAllowsDynamicInteraction() const
{
	// `0x101a8ac0` on the resolved `m_hCine`. **SEAM**: this substrate models no dynamic scripted
	// interaction, so it answers TRUE — the arm that lets the cine stand. Answering false would make
	// every scripted body refuse every sequence, which is the opposite of retail's ordinary case.
	return true;
}

bool FElysiumNpc::ScriptOwnerIsLive() const
{
	// `m_hCine` (+0x5d74), which the shape map binds to `FElysiumEntity::ScriptOwner`.
	return ScriptOwner.IsSet() && World != nullptr && World->Resolve(ScriptOwner) != nullptr;
}

void FElysiumNpc::AddSceneEvent(void* Scene, void* Event)
{
	// `CAI_BaseNPCTroika::AddSceneEvent` `0x102c1680`, slot 286. A five-way dispatch on the choreo
	// event type, and the `default` arm is the base at `0x100b5e60`. The port's own
	// `EElysiumChoreoEvent` carries retail's numbering unchanged, which is why the arms read as names
	// here and as `2 / 7 / 0xd / 0xe / 0xf` in the listing.
	const FElysiumSceneData* const SceneData = static_cast<const FElysiumSceneData*>(Scene);
	const FElysiumSceneEvent* const SceneEvent = static_cast<const FElysiumSceneEvent*>(Event);
	if (SceneEvent == nullptr)
	{
		AddSceneEventBase(SceneData, SceneEvent);
		return;
	}

	switch (SceneEvent->Type)
	{
	case EElysiumChoreoEvent::Expression:
	{
		// Type 2 — `0x102c1a80`, the Troika expression installer. **SEAM**: it walks the disposition
		// table's expression block and `CBaseCombatCharacter::AddScriptedExpression`, neither of which
		// this substrate carries (`docs/vtmb/npc-kernel/layout.md` types +0x1568 as
		// `CUtlVector<ScriptedExpression_t>` and no port member claims it).
		AddScriptedExpression(SceneEvent->Param, 0.f, 0.f, 1.f, 0.f, SceneEvent->GetDuration());
		return;
	}
	case EElysiumChoreoEvent::Sequence:
	{
		// Type 7 — look the named sequence up and, ON A HIT ONLY, force it as both the ideal and the
		// current activity through slot 0x4dc (`ForcePreTranslatedSequenceAndActivity`, slot 311)
		// with the sequence's OWN activity on both arguments, then drop the two stance latches. A
		// miss falls out of the whole body: it does NOT reach the base.
		const int32 Sequence = LookupSequenceByName(*SceneEvent->Param);
		if (Sequence >= 0)
		{
			const int32 Activity = SequenceActivityOf(Sequence);
			ForcePreTranslatedSequenceAndActivity(Activity, Activity, Sequence);
			Stance.bInFidget = false;   // +0x64e0 m_bInDispositionFidget
			Stance.bInChange = false;   // +0x64e1 m_bInStanceChange
		}
		return;
	}
	case EElysiumChoreoEvent::Silence:
	{
		// Type 0xd — the stance-change reaction, gated on a LIVE dialogue partner. The event's
		// parameter string is an intensity, `atof`'d and compared against the disposition row's own
		// threshold (`0x100ec5d0` -> record +0x108); above it, `RandomInt(1, 100)` is rolled against
		// the row's percentage (record +0x10c).
		if (!HasLiveDialogPartner())
		{
			return;
		}
		float Threshold = 0.f;
		float ChancePercent = 0.f;
		if (!DispositionStanceReaction(Threshold, ChancePercent))
		{
			return;
		}
		const float Intensity = FCString::Atof(*SceneEvent->Param);
		if (!(Threshold < Intensity))
		{
			return;
		}
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		if (!(Stream.RandRange(1, 100) < static_cast<int32>(ChancePercent)))
		{
			return;
		}
		// The reaction IS a stance change: retail calls `ChangeStance` (`0x102c1230`) and reads the
		// transition sequence it leaves in `EAX` — the return of the disposition table's own
		// transition lookup. `ElysiumStance::ChangeStance` is that body in this runtime and answers
		// by CLIP NAME, so the sequence index is resolved back through `LookupSequence`.
		const int32 Sequence = ChangeStanceForReaction();
		if (Sequence < 0)
		{
			return;
		}
		SequenceNumber = Sequence;                // +0x06f0
		ResetSequenceInfo();                      // 0x10090950
		SequenceCycle = 0.f;                      // +0x06f8
		AnimTime = static_cast<float>(World != nullptr ? World->NowSeconds() : 0.0);   // +0x0174
		IdealActivityNumber = GAnimActDisposition;    // +0x0ff0
		ActivityNumber = GAnimActDisposition;         // +0x0fec
		Stance.bInFidget = false;                 // +0x64e0
		Stance.bInChange = true;                  // +0x64e1
		return;
	}
	case EElysiumChoreoEvent::Loud:
	{
		// Type 0xe — the loud-line scripted expression, gated on the cooldown `m_flLoudExpressionTime`
		// (+0x6574) having passed. The row (`0x100ec450`) picks ONE of its authored expression names
		// at random (`RandomInt(0, count - 1)` over record +0x21c) and hands back a fade-in
		// (+0x220), a fade-out (+0x224), a minimum (+0x228) and a maximum (+0x22c).
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		if (!(static_cast<double>(LoudExpressionTime) < Now))
		{
			return;
		}
		FString ExpressionName;
		float FadeIn = 0.f;
		float FadeOut = 0.f;
		float MinLevel = 0.f;
		float MaxLevel = 0.f;
		if (!DispositionLoudExpression(ExpressionName, FadeIn, FadeOut, MinLevel, MaxLevel))
		{
			return;
		}
		// The event's own parameter, clamped into [min, max]. Retail re-`atof`s the SAME string on
		// each of the three arms rather than caching it; the value is identical, so one read is
		// faithful.
		const float Authored = FCString::Atof(*SceneEvent->Param);
		const float Level = (Authored <= MaxLevel)
			? ((MinLevel <= Authored) ? Authored : MinLevel)
			: MaxLevel;
		AddScriptedExpression(ExpressionName, FadeIn, FadeOut, 1.f, 0.f, Level);
		// The cooldown covers the whole envelope: the level's own duration plus both fades, from now.
		LoudExpressionTime = Level + Now + FadeIn + FadeOut + GAnimTimeEpsilon;
		return;
	}
	case EElysiumChoreoEvent::Python:
	{
		// Type 0xf — `CDialogDependency::CallPyDialogFunc(func, partner, this, 0x102, nullptr)`. The
		// dialogue partner is resolved first and, when there is none, retail substitutes the object
		// `0x101cd9e0(1)` answers — the player. Both strings go through the interning helper
		// `0x101d3730`; the FIRST is a constant (`DAT_10547e6c`) whose result is discarded, and the
		// function actually called is the event's SECOND parameter (`0x10075ca0`).
		CallPythonDialogFunction(SceneEvent->Param2);
		return;
	}
	default:
		break;
	}

	// Every other type — including the gesture events the base line arms — reaches the base body.
	AddSceneEventBase(SceneData, SceneEvent);
}

void FElysiumNpc::ProcessSceneEvents()
{
	// `0x100b6250`, slot 283. The decompilation carries a DAMAGED banner on its tail jump only; the
	// listing shows the whole body.
	//
	// It does NOT walk the scene-event queue. It zeroes EVERY flex controller through slot 279 —
	// re-reading `GetNumFlexControllers()` each iteration, exactly as `LookupFlexController` does —
	// and then TAIL JUMPS to slot 284 `AddSceneExpressions` (+0x470), which is what rebuilds the
	// frame's expression from the queue. The reset-then-accumulate order is the mechanism: an
	// expression that stopped this frame leaves no residue.
	int32 Index = 0;
	int32 Count = NumFlexControllers();
	if (Count > 0)
	{
		do
		{
			SetFlexWeight(Index, 0.f);
			++Index;
			Count = NumFlexControllers();
		} while (Index < Count);
	}
	AddSceneExpressions();
}

void FElysiumNpc::ProcessSequenceSceneEvent(void* SceneEventInfo)
{
	// `0x100b70e0`, slot 290. Four guards and one call, and the call's result is DISCARDED
	// (`FSTP ST0` on the listing): retail computes the event's ramp intensity at the scene's current
	// time and does nothing with it. Reproduced as retail wrote it — the guards ARE the behaviour,
	// and inventing an effect for the intensity would be a divergence.
	FSceneEventRecord* const Record = static_cast<FSceneEventRecord*>(SceneEventInfo);
	if (Record == nullptr || Record->Event == nullptr || Record->Scene == nullptr
		|| Record->Handle < 0)
	{
		return;
	}
	const float SceneTime = SceneTimeOf(*Record->Scene);
	Record->LastIntensity = Record->Event->RampAt(SceneTime - Record->Event->StartTime);
}

void FElysiumNpc::ProcessGestureSceneEvent(void* SceneEventInfo)
{
	// `0x100b7040`, slot 291. The same four guards, then — read off the listing, because the
	// decompilation binds the divisor to the wrong call:
	//
	//   duration = CChoreoEvent::GetDuration(event)        // 0x10076b30, SAVED
	//   SequenceDuration(record.sequence)                  // 0x10009813, CALLED AND DISCARDED
	//   cycle    = ((m_flAnimTime - record.startTime) + 1e-4) / duration
	//   CChoreoEvent::GetOriginalPercentageFromPlaybackPercentage(event, cycle)   // discarded
	//   intensity = CChoreoEvent::GetIntensity(event, scene->GetTime())           // discarded
	//
	// The divisor is the EVENT's authored length, not the sequence's — `FElysiumSceneEvent::GetDuration`
	// is already this runtime's spelling of `0x10076b30`. `SequenceDuration` is still called, which is
	// why the seam is asked here at all.
	FSceneEventRecord* const Record = static_cast<FSceneEventRecord*>(SceneEventInfo);
	if (Record == nullptr || Record->Event == nullptr || Record->Scene == nullptr
		|| Record->Handle < 0)
	{
		return;
	}
	const FElysiumSceneEvent& Event = *Record->Event;
	const float Duration = Event.GetDuration();
	SequenceDurationOf(Record->Sequence);
	Record->LastGestureCycle = static_cast<float>(
		((AnimTime - Record->StartTime) + GAnimTimeEpsilon)
			/ FMath::Max(Duration, UE_KINDA_SMALL_NUMBER));
	Record->LastIntensity = Event.RampAt(SceneTimeOf(*Record->Scene) - Event.StartTime);
}

// --- The activity commit ------------------------------------------------------------------------

bool FElysiumNpc::IsActivityFinished()
{
	// `0x10272900`, slot 251, the whole 35-byte body. BOTH terms, and the second is what stops a
	// body that is still blending into its ideal from reporting done.
	return bSequenceFinished && SequenceNumber == IdealSequence;
}

void FElysiumNpc::ForcePreTranslatedSequenceAndActivity(int32 Activity, int32 TranslatedAct,
	int32 Sequence)
{
	// `0x10272400`, slot 311. A NEGATIVE sequence is the whole refusal: nothing at all is written.
	if (Sequence < 0)
	{
		return;
	}

	// `OnChangeActivity` (slot 465, vtable +0x744) fires BEFORE the store and only when the activity
	// actually changes, so a re-force of the same activity raises no change notification.
	if (ActivityNumber != Activity)
	{
		OnChangeActivity(Activity);
	}

	ActivityNumber = Activity;                 // +0x0fec m_Activity
	IdealActivityNumber = Activity;            // +0x0ff0 m_IdealActivity
	IdealWeaponActivity = TranslatedAct;       // +0x5cd4
	IdealTranslatedActivity = TranslatedAct;   // +0x5cd0
	TranslatedActivity = TranslatedAct;        // +0x0ff4
	IdealSequence = Sequence;                  // +0x5ccc m_nIdealSequence
	CommitForcedSequence(Sequence);            // 0x10260a50
	SequenceCycle = 0.f;                       // +0x06f8 m_flCycle
	PrevAnimTime = 0.f;                        // +0x0170 m_flPrevAnimTime
}

int32 FElysiumNpc::TranslateActivityNumber(int32 Activity, int32& OutWeaponActivity) const
{
	// `CAI_BaseNPC::TranslateActivity` (`0x10271ff0`). **SEAM**: the recovered per-species and
	// per-weapon translation is `Visual/ElysiumAnimationResolve.cpp`'s and is keyed on activity
	// NAMES, so there is no retail-numbered table at this tier. Answering the activity unchanged is
	// what retail's own EMPTY translation table gives, which is the unarmed body's case.
	OutWeaponActivity = Activity;
	return Activity;
}

void FElysiumNpc::ResolveDispositionActivity(int32& OutSequence, int32& OutTranslatedActivity) const
{
	// `0x10295a80`, the Troika resolver `ACT_DISPOSITION` is handed to when `m_pBaseNPCTroika`
	// (+0x98) is set — which it is on every spawned NPC, so this arm is always the one taken for
	// 0xf1. **SEAM**: the stance machine that answers it here is `ElysiumStance::Select`, which
	// answers by CLIP NAME and has no sequence index to give back. The sequence is left at -1 and the
	// ladder falls through to retail's own `"has no sequence for act ACT_DISPOSITION"` arm.
	OutSequence = INDEX_NONE;
	OutTranslatedActivity = GAnimActDisposition;
}

FString FElysiumNpc::ScriptCustomMoveSequenceName() const
{
	// `m_hCine + 0x5f50`, the scripted sequence's `m_iszCustomMove`. **SEAM**: the port's scripted
	// sequence carries its custom-move label on the `scripted_sequence` entity rather than on a
	// `CCineNPC` the kernel reaches by offset. Empty is retail's own null-string case, which it
	// replaces with `""` before the lookup.
	return FString();
}

void FElysiumNpc::ResolveActivityToSequence(int32 Activity, int32& OutSequence,
	int32& OutTranslatedActivity, int32& OutWeaponActivity) const
{
	// `0x10272130` — the fallback ladder, including its own retry loop. Written as retail wrote it:
	// an outer `do { … } while (true)` whose only exits are a resolved sequence, sequence zero, and
	// the two early returns inside the arms.
	//
	// The rungs, in the order the body reaches them:
	//   ACT_SCRIPT_CUSTOM_MOVE (0x18) with a live cine  -> the cine's own `m_iszCustomMove` by NAME,
	//                                                      else a warning and ACT_IDLE (9)
	//   ACT_DISPOSITION (0xf1) with `m_pBaseNPCTroika`  -> the Troika disposition resolver
	//   anything else                                   -> SelectWeightedSequence(translated)
	//   that missed, translated == ACT_RUN (0x13)       -> 9 instead
	//   still nothing                                   -> retry the WHOLE request as 0xf1
	//   even 0xf1 missed                                -> sequence 0
	LastResolveActivityRung = EResolveActivityRung::None;

	int32 Request = Activity;
	while (true)
	{
		OutSequence = INDEX_NONE;
		OutTranslatedActivity = TranslateActivityNumber(Request, OutWeaponActivity);

		bool bTookCustomMoveTail = false;
		if (Request == GAnimActScriptCustomMove && ScriptOwnerIsLive())
		{
			const FString CustomMove = ScriptCustomMoveSequenceName();
			OutSequence = LookupSequenceByName(*CustomMove);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::ScriptCustomMove;
				return;
			}
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("SCRIPT_CUSTOM_MOVE: %s has no sequence"), *DebugString());
			bTookCustomMoveTail = true;
		}
		else if (Request == GAnimActDisposition)
		{
			// Retail's guard is `m_pBaseNPCTroika != nullptr` (+0x98), the self-downcast cache every
			// spawned `CAI_BaseNPCTroika` fills in its own constructor — so for this runtime's one
			// leaf it is unconditionally true.
			ResolveDispositionActivity(OutSequence, OutTranslatedActivity);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::DispositionTable;
				return;
			}
			UE_LOG(LogElysiumNpcEnt, Verbose,
				TEXT("%s has no sequence for act ACT_DISPOSITION"), *DebugString());
		}
		else
		{
			OutSequence = SelectWeightedSequenceForActivity(OutTranslatedActivity);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::Weighted;
				return;
			}
			// Retail rate-limits this warning on a (this, activity, time) triple held in three
			// globals; the port logs on `Verbose`, which is this runtime's own rate limiter.
			UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s has no sequence for act %d"), *DebugString(),
				OutTranslatedActivity);
			if (OutTranslatedActivity == GAnimActRun)
			{
				OutTranslatedActivity = GAnimActIdle;
				OutSequence = SelectWeightedSequenceForActivity(GAnimActIdle);
				if (OutSequence != INDEX_NONE)
				{
					LastResolveActivityRung = EResolveActivityRung::RunToWalk;
					return;
				}
			}
		}

		if (bTookCustomMoveTail)
		{
			OutSequence = SelectWeightedSequenceForActivity(GAnimActIdle);
			if (OutSequence != INDEX_NONE)
			{
				LastResolveActivityRung = EResolveActivityRung::CustomMoveIdle;
				return;
			}
		}

		if (Request == GAnimActDisposition)
		{
			// The retry has already happened and missed: sequence zero is the floor.
			OutSequence = 0;
			LastResolveActivityRung = EResolveActivityRung::SequenceZero;
			return;
		}
		Request = GAnimActDisposition;
		LastResolveActivityRung = EResolveActivityRung::DispositionRetry;
	}
}

void FElysiumNpc::SetIdealActivity(int32 Activity)
{
	// `0x10272650`, read off the listing. Activity 0 (`ACT_INVALID`) TAIL JUMPS to slot 310
	// `SetActivity(0)` — it does NOT store the word — and everything else stores `m_IdealActivity`
	// and re-resolves the ideal triple beside it WITHOUT committing it.
	if (Activity == 0)
	{
		SetActivity(0);
		return;
	}
	// Family Facing's `SetIdealActivityNumber` is the store half of this body; it is called rather
	// than respelt so the two cannot drift.
	SetIdealActivityNumber(Activity);
	ResolveActivityToSequence(Activity, IdealSequence, IdealTranslatedActivity, IdealWeaponActivity);
}

bool FElysiumNpc::ShouldMaintainActivity()
{
	// `0x102bf510`, slot 466. Three arms, and the first one's answer is FALSE — retail returns
	// `EAX & 0xffffff00`, whose low byte is zero.
	if (Schedule.IsRunning()
		&& GetLocalScheduleId(Schedule.Current) == GScheduleRefusingMaintain)
	{
		return false;
	}
	if (bForceMaintainActivity)   // +0x65fa
	{
		return true;
	}
	// `CAI_BaseNPC::ShouldMaintainActivity` `0x10272790`: `GetState()` (slot 464) against 4
	// (`NPC_STATE_SCRIPT`) with `m_Activity != 2`. Everything else maintains.
	return !(AnimRetailNpcState(Mind.State()) == 4 && ActivityNumber != 2);
}

int32 FElysiumNpc::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	// The vtable dispatch first. Five classes carry their own slot-482 body (`CNPC_VAnimal`
	// `0x1035fd40`, `CNPC_VTzimisce` `0x103bd270` and three inheritors, family **Species**) and every
	// one of them is BYTE-IDENTICAL to the body below — so the species arm CALLS this one and the
	// answers coincide. The row is wired anyway because the dispatch is the recovered fact: the day a
	// sixth class' copy turns out to differ, the prologue is already where it has to be.
	int32 SpeciesAnswer = 0;
	if (SpeciesCanPlaySequence(bDisregardState, InterruptLevel, SpeciesAnswer))
	{
		return SpeciesAnswer;
	}

	// `0x10278090`, slot 482. The return is retail's `CanPlaySequence_t`: 0 refuses, 1 is the plain
	// yes, and **2 is the yes of a body that already holds a cine** — the two are not the same
	// answer, which is why this returns an int rather than a bool.
	int32 Result = 1;
	if (ScriptOwnerIsLive())
	{
		if (!CineAllowsDynamicInteraction())
		{
			return 0;
		}
		Result = 2;
	}
	if (!IsAlive())   // slot 158, vtable +0x278
	{
		return 0;
	}
	// The state gate, refusing when EVERY term holds: the caller did not disregard state, the body is
	// neither NONE (0) nor IDLE (1), its ideal is not IDLE, and it is not an ALERT (3) body whose
	// caller asked with an interrupt level of at least 1.
	const int32 State = AnimRetailNpcState(Mind.State());
	const int32 IdealState = AnimRetailNpcState(Mind.IdealState());
	if (!bDisregardState && State != 0 && State != 1 && IdealState != 1
		&& (State != 3 || InterruptLevel < 1))
	{
		return 0;
	}
	return Result;
}

int32 FElysiumNpc::IdleSequenceGate() const
{
	// `0x102b8a10`. Two gates and one answer:
	//   HasCondition(0x58)                       — COND_ENEMY_DEAD
	//   SelectWeightedSequence(0x61) != -1       — the body authors a clip for the gate's activity
	//   -> schedule 8, after stamping the selector trace (+0x1b30 the source file, +0x1b34 line
	//      0x5f20). The shape map calls that word ABSENT: this runtime records selections in the
	//      mind's transition trace instead of carrying retail's file/line pair.
	// Retail schedule number 8 has no row in `int32` yet, so the NUMBER is the answer;
	// 0 is `SCHED_NONE`.
	if (!Cognition.Conditions.Has(EElysiumNpcCond::EnemyDead))
	{
		return 0;
	}
	if (SelectWeightedSequenceForActivity(GIdleGateActivity) == INDEX_NONE)
	{
		return 0;
	}
	return GIdleGateSchedule;
}

// --- The pose parameter (slot 345) --------------------------------------------------------------

float FElysiumNpc::SetPoseParameter(const TCHAR* Name, float Value, bool bUpdatePoseControls)
{
	// `0x1032fb80`, slot 345. Retail's body is a VPROF scope push, `LookupPoseParameter(name)`, a
	// dispatch through vtable +0x568 — slot 346, the INDEX overload — and the scope pop. The scope
	// is a diagnostic this runtime replaces with its own channels; the lookup and the dispatch are
	// the behaviour.
	//
	// The write is ALSO recorded on family Facing's `PoseParameterWrites`, which is this runtime's
	// one pose-parameter surface: slot 346 is still 29c's stub, so without this the write would
	// vanish and a reader asking "what did this body aim" would see nothing.
	SetPoseParameterByName(Name, Value);
	return SetPoseParameter(LookupPoseParameter(Name), Value, bUpdatePoseControls);
}

// --- The scene (slot 540) -----------------------------------------------------------------------

float FElysiumNpc::PlayScene(const TCHAR* SceneFile)
{
	// `0x10279060`, slot 540: nineteen bytes, a tail call into `CBaseFlex::PlayScene(this, name, 0)`
	// (`0x10084b40`) with a NULL out-handle. That body stands an `instanced_scripted_scene`, points
	// it at this NPC, loads the named scene and answers its play length; an unknown scene gets
	// `Msg("Unknown scene specified: %s\n")` and 0, with -1 written to the out-handle the NPC does
	// not pass.
	const float Length = PlayInstancedScene(SceneFile);
	if (Length < 0.f)
	{
		UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Unknown scene specified: %s"),
			SceneFile != nullptr ? SceneFile : TEXT(""));
		return 0.f;
	}
	return Length;
}

// --- The choreo-scene latches (slots 59, 60, 61) ------------------------------------------------

void FElysiumNpc::Slot59(void* SceneEntity)
{
	// `CAI_BaseNPCTroika::vfunc59` `0x102b51e0` — the scene ENTERS. `m_bInChoreoScene` (+0x5bc4) is
	// raised unconditionally; `m_bCutsceneForceLOD` (+0x1590) only when the scene entity's own
	// `+0x57d` byte is set.
	bInChoreoScene = true;
	if (SceneEntityForcesCutsceneLod(SceneEntity))
	{
		bCutsceneForceLOD = true;
	}
}

void FElysiumNpc::Slot60(void* SceneEntity)
{
	// `0x102b5220` — the scene LEAVES. The exact inverse, gated on the same byte, so a scene that
	// never forced LOD cannot clear a flag another scene raised.
	bInChoreoScene = false;
	if (SceneEntityForcesCutsceneLod(SceneEntity))
	{
		bCutsceneForceLOD = false;
	}
}

void FElysiumNpc::Slot61(void* SceneEntity)
{
	// `0x102b5260` — byte for byte the same body as slot 60. Two slots, one behaviour: retail
	// declares two exits (the ordinary one and the cancel) and gives them the same implementation.
	bInChoreoScene = false;
	if (SceneEntityForcesCutsceneLod(SceneEntity))
	{
		bCutsceneForceLOD = false;
	}
}
