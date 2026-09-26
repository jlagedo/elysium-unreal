#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelHintsShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Hints** — the hint nodes and the interesting places of `order.md` layers
// 0–9. 38 rows: the slot-566/567 species halves (`CNPC_Crow`'s two rows carry no port body: no map
// stands that class), the Werewolf's whole hint surface (move, teleport, break, imperative,
// activity, groundpoint, schedule), the two `IsHintCoverValid` forwards, the two `FindHintNode`
// bodies, the Boss's centre-line geometry, the two interest-place bodies and the
// five hint-node activity lookups (those live on `FElysiumNpcScheduleHost`). The walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// THE STANDING FACT OF THIS FAMILY: there is no hint node in this substrate — no `CAI_Hint` entity,
// no global hint list, no AI node graph. `ElysiumNpcKernelHints.inl` declares the seam and says so
// at length. Every rule below is ported over `FHintWords`, the typed view of a hint's own datamap
// words, so the rule is the deliverable and the query is the thing that answers nothing.

namespace
{

	// `_DAT_104454d0` — the OUTOF-phase wait the interest loop adds to `m_flWaitFinished`
	// (`0x102aa210`), the pooled 0.5f.
	constexpr float GHintsInterestOutOfWaitSeconds = ElysiumNpcTunables::Half;

	// `RandomFloat(2.0, 10.0)`, the interest-place activity refresh both interest bodies draw.
	constexpr float GHintsInterestRefreshMin = 2.0f;   // 0x40000000
	constexpr float GHintsInterestRefreshMax = 10.0f;  // 0x41200000

	// `m_flNextActivityTime = -1.0f` (`0xbf800000`), the "no refresh pending" sentinel both interest
	// bodies write and the loop tests for.
	constexpr double GHintsNoActivityRefresh = -1.0;

	constexpr int32 GHintsCondHintIdle = 99;             // `0x102aaa60`'s fallback test

	// The interest-place DevWarning texts, verbatim from `.rdata`.
	const TCHAR* const GHintsWarnInterestActivity = TEXT("Can not find interest activity");
	const TCHAR* const GHintsWarnInterestIntoActivity = TEXT("Can not find interest into activity");
	const TCHAR* const GHintsWarnInterestOutOfActivity =
		TEXT("Can not find interest outof activity");

	// `ACT_IDLE`'s retail id, which is what both interest bodies substitute when the activity name
	// does not resolve (`iVar3 = 1` after the DevWarning).
	constexpr int32 GHintsActivityIdleFallback = 1;
}

// -------------------------------------------------------------------------------------------------
// The hint seam. Every entry answers nothing and names the retail call it stands for.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::HintWords(int32 HintNode, FHintWords& Out) const
{
	// Retail's hint words live on the `CAI_Hint` ENTITY reached through `m_pHintNode` (`+0x5ddc`).
	// This runtime carries a hint reference as that entity's index (0018 story 2), so the words are
	// the live `ai_hint`'s own. An index that is not a live hint answers false and leaves `Out`
	// untouched.
	if (World == nullptr || !World->Entities().IsValidIndex(HintNode))
	{
		return false;
	}
	const FElysiumHint* Hint = FElysiumHint::Cast(World->Entities()[HintNode].Get());
	if (Hint == nullptr || Hint->IsDead())
	{
		return false;
	}
	Out = Hint->ToWords();
	return true;
}

int32 FElysiumNpc::FindHintNear(int32 HintType, uint8 SearchFlags, float RadiusUnits) const
{
	// SEAM for `0x102d1af0`.
	(void)HintType;
	(void)SearchFlags;
	(void)RadiusUnits;
	return INDEX_NONE;
}

int32 FElysiumNpc::FindHintOfTypeNear(const FElysiumEntity* Near, int32 HintType, uint8 SearchFlags,
	float RadiusUnits) const
{
	// SEAM for `0x102d24b0`. The recovered walk, for whoever stands the store: start at the rotating
	// cursor `DAT_10925454`'s successor (else the head `DAT_10925450`), follow `+0x5d8`, wrap to the
	// head, and stop when the cursor comes round again; admit a node when `IsHintUnusable` is false,
	// `HintType == 0 || node->m_nHintType == HintType`, the squared distance to `Near` is under
	// `RadiusUnits * RadiusUnits`, and slot 566 `FValidateHintType` accepts it; with bit 0 of the
	// flags additionally require a clear trace. Bit 2 diverts the whole call to `0x102d1760`.
	(void)Near;
	(void)HintType;
	(void)SearchFlags;
	(void)RadiusUnits;
	return INDEX_NONE;
}

int32 FElysiumNpc::FindHintByName(const FString& HintName) const
{
	// `CGlobalEntityList::FindEntityByName` (`0x100f7770`) narrowed to `CAI_Hint` by
	// `__RTDynamicCast`: the FIRST entity the name matches, and a miss when that one is not a hint —
	// the cast fails, the search does not continue to a later match.
	if (World == nullptr)
	{
		return INDEX_NONE;
	}
	const FElysiumEntity* First = World->FindByName(HintName);
	const FElysiumHint* Hint = FElysiumHint::Cast(First);
	return Hint != nullptr ? Hint->Handle.Index : INDEX_NONE;
}

void FElysiumNpc::ReleaseHintNode(int32 HintNode, float ReuseDelaySeconds)
{
	// `0x102d1420`, exactly: `hint->m_hHintOwner (+0x5e0) = -1` and
	// `hint->m_flNextUseTime (+0x5ec) = ReuseDelaySeconds + gpGlobals->curtime`.
	//
	// SEAM on the write side only. `FElysiumNpcScheduleHost` carries `HintReusableAt` and
	// `bOwnsHint` — the NPC's half of the same claim — but the fields retail writes are the HINT's,
	// and there is no hint to write them into.
	(void)HintNode;
	(void)ReuseDelaySeconds;
}

bool FElysiumNpc::IsHintUnusable(const FHintWords& Hint, double Now, bool bOwnerAlive)
{
	// `0x102d14c0`, arm for arm and in retail's order.
	if (Hint.Disabled != 0)                    // `m_iDisabled != 0`
	{
		return true;
	}
	if (Now < Hint.NextUseTime)                // `curtime < m_flNextUseTime`
	{
		return true;
	}
	// `m_hHintOwner` resolves to a live entity — retail's `EHANDLE` serial check plus a non-null
	// entity pointer, which is what `bOwnerAlive` stands for.
	return bOwnerAlive;
}

bool FElysiumNpc::IsHintUnusable(int32 HintNode, double Now) const
{
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		// The seam could not resolve it. A hint that is not there is not usable.
		return true;
	}
	return IsHintUnusable(Hint, Now, Hint.HintOwner.IsSet());
}

bool FElysiumNpc::ValidateHintCoverRange(const FHintWords& Hint, const FElysiumEntity* CoverObject,
	float AngleRangeDot, float BadRangeLimit) const
{
	// SEAM for `0x10296c40`, the shared range/LOS/cover validator. It is `order.md` layer 11 and
	// belongs to story 29d; reproducing it here would be a second copy that 29d then has to
	// reconcile. Its recovered shape, for the record: a null or disabled hint fails; no active
	// weapon fails; a height difference over `_DAT_1049ae28` fails; the enemy distance must lie in
	// `[m_flTargetDistMin, min(weapon range, m_flTargetDistMax)]`; a hint that is not already
	// `m_pHintNode` additionally needs a forward-projection of at least `_DAT_10451ab4`; then the
	// normalised enemy direction is dotted with the hint's facing and must be `>= AngleRangeDot` and
	// `< BadRangeLimit`; and with `m_bForceCoverLOSCheck` set, `0x102968f0` must pass.
	(void)Hint;
	(void)CoverObject;
	(void)AngleRangeDot;
	(void)BadRangeLimit;
	return false;
}

void FElysiumNpc::RestartIdealActivityId(int32 RetailActivityId)
{
	// SEAM for `RestartIdealActivity` (`0x10289ee0`). This runtime resolves activities by NAME
	// (`FElysiumClipIdentity`) and carries no retail `Activity` enum table — the same reason 29b
	// left `m_IdealTranslatedActivity` (`+0x5cd0`) an opaque `int32`. Every body in this file
	// DECIDES the id retail would have passed and hands it here; the decision is what the tests
	// assert, and the play is what this seam does not do.
	(void)RetailActivityId;
}

FElysiumNpc::FHintRestoreResult FElysiumNpc::HintOnRestore(const FHintWords& Hint)
{
	// `CAI_Hint::OnRestore` — slot 130, `0x102d3ec0`, handed over by family Sounds.
	//
	// SLOT 130 IS `OnRestore`, not a sound handler: `vtmb_slot 130` shows `CAI_BaseNPC::OnRestore`
	// (`0x1027bf50`) and `CAI_BaseNPCTroika::OnRestore` (`0x102998c0`) filling it, and the
	// `CAISound::FUN_100aa5a0` this body opens with is `CBaseEntity::OnRestore`, whose whole body
	// tail-calls slot 6. `docs/vtmb/npc-ai/shape.md` § "Speech and the looping-sound stop" reads it
	// as a reaction to an AI sound; the section this family added supersedes that identification.
	//
	// The body, verbatim:
	//   1. the base `CBaseEntity::OnRestore`;
	//   2. resolve the hint's own AI-network node — `0x102d3e60` bounds-checks `m_nNodeID`
	//      (`+0x5e4`) against `(*DAT_1093407c)` and indexes `DAT_1093407c[1]`, bumping an error
	//      counter for an out-of-range id;
	//   3. NO NODE: `DevMsg("Warning: AI hint has incorrect origin")` and return;
	//   4. a node: `Teleport(&node->origin /*+0x08..+0x10*/, NULL, NULL)` through vtable `+0x2d4`,
	//      then claim the node by writing `this` into its `CAI_Hint*` slot at `+0xa0`.
	//
	// THIS RUNTIME HAS NO AI NETWORK, so step 2 never finds a node and step 3 is the arm every call
	// takes — which is a recovered arm, not a refusal. Ported as such.
	(void)Hint;
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Warning: AI hint has incorrect origin"));
	return FHintRestoreResult{};
}

int32 FElysiumNpc::ActivityIdForName(const FString& ActivityName) const
{
	// SEAM for `ActivityNameToId` (`0x10412520`). -1 is retail's own "not in the table" answer.
	(void)ActivityName;
	return INDEX_NONE;
}

bool FElysiumNpc::IsHintSequenceFinished() const
{
	// SEAM for `m_bSequenceFinished` (`+0x65c`).
	return false;
}

bool FElysiumNpc::DoesHintSequenceLoop() const
{
	// SEAM for `m_bSequenceLoops` (`+0x65d`).
	return false;
}

FElysiumNpc* FElysiumNpc::InterestingPlaceMarkerOccupant(const FElysiumInterestingPlace* Place) const
{
	// SEAM for `0x102db760` (the place's occupied marker record) and `0x102dcc20` (the NPC that
	// holds it). `FElysiumInterestingPlace` carries claimants as a set of entity indices and no
	// per-marker record, so there is no marker to ask.
	(void)Place;
	return nullptr;
}

void FElysiumNpc::MoveToBoneOriginAngles(const TCHAR* BoneName, bool bMoveOrigin, bool bMoveAngles)
{
	// SEAM for `CBaseCombatCharacter::MoveToBoneOriginAngles`, which the interest loop calls with
	// `("Bip01", false, true)` when the OUTOF phase ends — retail's way of committing the animation's
	// accumulated root motion back onto the entity. Nothing here reads bone transforms on the
	// substrate side.
	(void)BoneName;
	(void)bMoveOrigin;
	(void)bMoveAngles;
}

void FElysiumNpc::SetMotorHintYaw(float Yaw)
{
	// SEAM for `0x102e0a80` / `0x102e2020` — the motor's yaw write (`CAI_Motor+0x34`), with retail's
	// `+0x1c == 180.0f` sentinel choosing between an immediate assign and a clamped step.
	(void)Yaw;
}

void FElysiumNpc::ReleaseMotorHintYaw()
{
	// SEAM for `0x102e1e20(motor, -1)` — the yaw-hold release the interest bodies end with, and
	// `0x102e0b40(motor)` which the claim opens with.
}

FVector FElysiumNpc::HintComparePosition(const FElysiumEntity* Entity) const
{
	// SEAM for the vtable `+0x370` accessor (slot 220). `0x103bfa50` uses it for BOTH sides of its
	// squared-distance compare, so answering the origin keeps the comparison self-consistent even
	// though the accessor itself is unidentified. **Unrecovered:** what slot 220 returns.
	return Entity ? Entity->Origin : FVector::ZeroVector;
}

int32 FElysiumNpc::CurrentRetailActivityId() const
{
	// SEAM for `m_Activity` (`+0xfec`).
	return INDEX_NONE;
}

bool FElysiumNpc::HintIdleActivityGate() const
{
	// SEAM for `0x102b5de0`, the gate `0x102aaa60` puts in front of each of its three hint types.
	// Recovered shape: with a live `m_hShootAtTarget` (`+0x5ba8`), trace from the eye to its origin
	// and answer "not blocked"; with none, `HasCondition(0x48 ENEMY_OCCLUDED)` fails it, a distance
	// under `_DAT_10449258` fails it, and otherwise trace to the enemy's shoot position and accept
	// unless the hit entity's relationship is 3 or 4.
	//
	// Answers TRUE — retail's "the gate passed" answer, which is the arm that restarts the hint
	// activity. The alternative would make every hint-idle body silently do nothing, which is not
	// the recovered behaviour of a body whose whole point is the activity.
	return true;
}

bool FElysiumNpc::PatrolNodeInterestRecordName(int32 PatrolNode, FString& OutName) const
{
	// SEAM for `0x1029f730` — the patrol node's interest record. There is no patrol-node graph on
	// this substrate (patrol is a point list, `FElysiumNpc::PatrolPoints`), so there is no record
	// and no `+0x468` name on it.
	(void)PatrolNode;
	(void)OutName;
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 568 `GetHintDelay` — the Troika-line body, filled by 77 classes with no override.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::GetHintDelay(int16)
{
	// `0x1026a910`. The whole function is two instructions:
	//     1026a910  FLD float ptr [0x104454c4]
	//     1026a916  RET 0x4
	// `_DAT_104454c4` is the image's shared 0.0f. The `short` argument is not read.
	return NpcKernelHintsShared::GHintsZero;
}

// -------------------------------------------------------------------------------------------------
// Finding and installing a hint node.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::FindHintNode(int32 HintType, uint8 SearchFlags)
{
	// `0x10365780` — the task-side hint install.
	const int32 Node = FindHintNear(HintType, SearchFlags, 5000.0f);
	ScheduleHost.HintNode = Node;   // `m_pHintNode +0x5ddc` is written on BOTH paths
	if (Node != INDEX_NONE)
	{
		// `thunk_FUN_10273e80(this, '\0')` — `TaskComplete(false)`.
		Schedule.TaskStatus = EElysiumTaskStatus::Complete;
		return true;
	}
	// The miss arm writes retail's assert file and line into `+0x1b44` / `+0x1b48`
	// (`"E:\Vampire\main\dlls\hl2_dll\NPC_..."`, line 0x48a = 1162) before failing. The shape map
	// records `+0x1b44` ABSENT — this runtime carries no assert file/line pair — so only the fail
	// lands, and that is stated rather than faked.
	TaskFail(4);
	return false;
}

// -------------------------------------------------------------------------------------------------
// Hint cover.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::IsHintCoverValid(int32 HintNode) const
{
	// `0x10297430`. The handle is checked TWICE in retail — once for "the slot holds this serial and
	// a non-null entity", then again before dereferencing — and a failed first check returns false
	// without calling the validator at all.
	if (!ScheduleHost.HintCoverObject.IsSet())
	{
		return false;
	}
	FElysiumEntity* Cover = World ? World->Resolve(ScheduleHost.HintCoverObject) : nullptr;
	if (Cover == nullptr)
	{
		return false;
	}
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return false;
	}
	return ValidateHintCoverRange(Hint, Cover, Hint.TargetAngleRangeDot, 0.731f);
}

bool FElysiumNpc::IsHintCoverValidLoose(int32 HintNode) const
{
	// `0x102974f0`. The same forward with `1.1` in place of `0.731` and NO pre-check: an invalid
	// handle resolves to null and is handed to the validator as null, which is a different arm of
	// `0x10296c40` than "never called". The decompiler types this body `void` because it leaves the
	// callee's `EAX` alone; the base `FValidateHintType` (`0x10295c20`) returns that `EAX` for hint
	// types 100 and 101, so the answer IS the validator's.
	FElysiumEntity* Cover = World && ScheduleHost.HintCoverObject.IsSet()
		? World->Resolve(ScheduleHost.HintCoverObject) : nullptr;
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		return false;
	}
	return ValidateHintCoverRange(Hint, Cover, Hint.TargetAngleRangeDot, 1.1f);
}

// -------------------------------------------------------------------------------------------------
// The hint group key and the hint-idle activity.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetHintGroup(const FString& NewHintGroup)
{
	// `0x102781e0`. Retail reads the old `m_strHintGroup` (`+0x5db0`), assigns the new one, and
	// dispatches slot 551 `OnChangeHintGroup(old, new)` — vtable `+0x89c`, `0x89c / 4 == 551` —
	// ONLY when the two differ. Both arguments are the string_t values, old first.
	const FString Old = ScheduleHost.HintGroup;
	ScheduleHost.HintGroup = NewHintGroup;
	if (!Old.Equals(NewHintGroup, ESearchCase::CaseSensitive))
	{
		// NAMED MODERNIZATION, one word wide: retail compares the two `string_t` POINTERS, so two
		// separately allocated strings with the same text would dispatch. VtMB interns keyfield
		// strings through `AllocPooledString`, which makes pointer equality text equality for every
		// authored path; the port compares the text.
		OnChangeHintGroup(FName(*Old), FName(*NewHintGroup));
	}
}

bool FElysiumNpc::PlayHintIdleActivity(double Now)
{
	// `0x102aaa60`. The timestamp at `param_1[0x1767]` is `+0x5d9c` — `m_flLastAttackTime`, which
	// this body stamps with `curtime` UNCONDITIONALLY and before anything else, whatever arm it then
	// takes. The hint node it reads is `param_1[0x1777]`, `+0x5ddc`.
	LastAttackTime = Now;

	FHintWords Hint;
	const bool bResolved = HintWords(ScheduleHost.HintNode, Hint);
	if (bResolved)
	{
		// Three typed arms, each gated on `0x102b5de0`. A gate that FAILS returns the gate's own
		// false and restarts nothing — it does NOT fall through to the condition test below.
		switch (Hint.HintType)
		{
		case 100:
			if (HintIdleActivityGate())
			{
				RestartIdealActivityId(0x1114);
				return true;
			}
			return false;
		case 0x65:
			if (HintIdleActivityGate())
			{
				RestartIdealActivityId(0x110f);
				return true;
			}
			return false;
		case 0x27d8:
			if (HintIdleActivityGate())
			{
				// `m_bLeaningLeft` (`+0x63fd`) picks 0x111f over 0x1120.
				RestartIdealActivityId(bLeaningLeft ? 0x111f : 0x1120);
				return true;
			}
			return false;
		default:
			break;
		}
	}
	// No hint node, or a type outside the three: the fallback is `HasCondition(99)`, and only its
	// ABSENCE restarts an activity.
	if (Cognition.Conditions.Has(NpcKernelHintsShared::HintsCond(GHintsCondHintIdle)))
	{
		return false;
	}
	RestartIdealActivityId(0x19);
	return true;
}

// -------------------------------------------------------------------------------------------------
// The interesting places.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::ResolvePatrolInterestPlace(int32 PatrolNode)
{
	// `0x1029f780`. A cache at `+0x6300` (`FElysiumNpcScheduleHost::Unknown6300`, the placeholder
	// 29b already reserved for exactly this offset): resolve once, reuse forever. Retail never
	// invalidates it.
	if (ScheduleHost.Unknown6300 != 0)
	{
		return static_cast<int32>(ScheduleHost.Unknown6300);
	}
	FString RecordName;
	if (PatrolNodeInterestRecordName(PatrolNode, RecordName) && World != nullptr)
	{
		// `thunk_FUN_100f7770(&DAT_106eb5d8, NULL, name, 0, 0)` — the by-name lookup, whose matcher
		// is `FElysiumEntityWorld::NameMatches`. A record with no name string is retail's empty
		// string, which matches nothing.
		if (FElysiumEntity* Found = World->FindByName(RecordName))
		{
			// `*(int *)(this + 0x6300) = piVar3[0x2c];` — the found entity's own `+0xb0`, not the
			// entity. **Unrecovered**: what `+0xb0` on a `CAI_InterestingPlace` is; the datamap ends
			// well below it and no other body in the closure reads that offset.
			ScheduleHost.Unknown6300 = static_cast<uint32>(Found->Handle.Index);
		}
	}
	return static_cast<int32>(ScheduleHost.Unknown6300);
}

void FElysiumNpc::ClaimInterestingPlace(FElysiumInterestingPlace* Place, bool bClaimSecondary,
	double Now)
{
	// `0x102a9f40` — "the wait" `TASK_DO_INTEREST_ACTIVITY`, `TASK_DO_INTEREST_LOITER` and
	// `TASK_DO_INTEREST_INTERACT` share (`docs/vtmb/npc-ai/programs.md`).
	if (Place == nullptr)
	{
		return;
	}
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);

	// 1. `thunk_FUN_102da7c0(place, this, param_3, '\x01')` — claim the marker.
	(void)bClaimSecondary;   // retail's third argument, a marker-selection byte; see the seam note
	Place->Claim(Handle);

	// 2. `m_OnInterestingPlaceArrived` (`+0x5f74`), fired with the PLACE as activator and this NPC
	//    as caller, zero delay.
	FireOutput(FName(TEXT("OnInterestingPlaceArrived")), Place->Handle);

	// 3. `m_bInterestingPlaceArrived` (`+0x62e8`).
	bAmbientArrived = true;

	// 4. The INTO arm or the idle arm, chosen on whether the place's type carries an INTO activity
	//    name at all (`thunk_FUN_102dae70` returns the string; retail tests its FIRST BYTE).
	const FElysiumInterestingPlaceType* TypeRow = AmbientType(Place);
	const FString IntoName = TypeRow && !TypeRow->IntoActivities.IsEmpty()
		? TypeRow->IntoActivities[0].Name : FString();
	if (!IntoName.IsEmpty())
	{
		int32 Activity = ActivityIdForName(IntoName);
		if (Activity == INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: %s"), GHintsWarnInterestIntoActivity,
				*IntoName);
			Activity = GHintsActivityIdleFallback;
		}
		NpcFlags.Set(EElysiumNpcFlag::INTERESTING_INTO);   // `m_bfAINPCFlags |= 0x20000000`
		AmbientPhase = EAmbientPhase::Into;                // `+0x6304 = 1`
		RestartIdealActivityId(Activity);
		AmbientNextActivityAt = GHintsNoActivityRefresh;   // `+0x63d4 = -1.0f`
	}
	else
	{
		const FString IdleName = TypeRow && !TypeRow->Activities.IsEmpty()
			? TypeRow->Activities[0].Name : FString();
		int32 Activity = ActivityIdForName(IdleName);
		if (Activity == INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: %s"), GHintsWarnInterestActivity, *IdleName);
			Activity = GHintsActivityIdleFallback;
		}
		AmbientPhase = EAmbientPhase::Dwelling;            // `+0x6304 = 2`
		RestartIdealActivityId(Activity);
		AmbientNextActivityAt = Now + Stream.FRandRange(GHintsInterestRefreshMin,
			GHintsInterestRefreshMax);
	}

	// 5. `m_flWaitFinished` (`+0x5db4`), UNCONDITIONALLY and after both arms: the place's own
	//    `m_fMinStayTime` / `m_fMaxStayTime` (`+0x568` / `+0x56c`).
	ScheduleHost.WaitFinished = Now + Stream.FRandRange(Place->MinTime, Place->MaxTime);

	// 6. `thunk_FUN_102e0b40(m_pMotor)` — the motor's yaw hold.
	ReleaseMotorHintYaw();

	// 7. `m_bMatchOrientation` (`+0x570`): face the place. Retail has two sources for the yaw and
	//    the decompiler cannot tell them apart — the branch keys on a register (`unaff_retaddr`)
	//    that its prologue never loads. **Unrecovered:** which of the two yaw sources a given
	//    caller reaches, and the `_DAT_1044c3a8` half-turn constant the flip adds or subtracts. What
	//    IS recovered is that a yaw is computed and handed to the motor, so that is what lands.
	if (Place->bMatchOrientation)
	{
		SetMotorHintYaw(static_cast<float>(Place->Angles.Y));
	}
}

bool FElysiumNpc::RunInterestingPlaceLoop(FElysiumInterestingPlace* Place, double Now)
{
	// `0x102aa210` — the INTO → IDLE → OUTOF loop. The return is the byte retail leaves in `AL`:
	// "this task is finished".
	if (Place == nullptr)
	{
		return false;
	}
	bool bFinished = false;
	bool bRefreshActivity = false;

	// The refresh decision. Two mutually exclusive gates, and note which field each reads:
	//   * with NO refresh pending (`+0x63d4 == -1.0f`) or a non-looping sequence, the decision is
	//     "has the sequence finished" and the PHASE advances;
	//   * otherwise it is the timer plus `m_bSequenceFinished`.
	if (AmbientNextActivityAt == GHintsNoActivityRefresh || !DoesHintSequenceLoop())
	{
		if (!IsHintSequenceFinished())
		{
			// Still playing. No refresh, no phase advance.
		}
		else if (AmbientPhase == EAmbientPhase::Into)
		{
			AmbientPhase = EAmbientPhase::Dwelling;   // `+0x6304: 1 -> 2`
			bRefreshActivity = true;
		}
		else if (AmbientPhase == EAmbientPhase::Out)
		{
			// The OUTOF animation has finished: release the INTO flag, commit the root motion, end
			// the wait immediately, and consume `INTERESTING_LOST` if it is standing.
			NpcFlags.Clear(EElysiumNpcFlag::INTERESTING_INTO);
			MoveToBoneOriginAngles(TEXT("Bip01"), /*bMoveOrigin=*/false, /*bMoveAngles=*/true);
			ScheduleHost.WaitFinished = Now;
			if (NpcFlags.Has(EElysiumNpcFlag2::INTERESTING_LOST))
			{
				bFinished = true;
				NpcFlags.Clear(EElysiumNpcFlag2::INTERESTING_LOST);
			}
			ReleaseMotorHintYaw();
			return bFinished;
		}
		else
		{
			bRefreshActivity = true;
		}
	}
	else if (Now >= AmbientNextActivityAt && IsHintSequenceFinished())
	{
		bRefreshActivity = true;
	}

	// The marker block. Retail additionally forces a refresh when the activity currently playing is
	// not the place's idle activity (a case-insensitive prefix compare of the two NAMES), and aligns
	// with whoever else stands on the marker.
	if (FElysiumNpc* Occupant = InterestingPlaceMarkerOccupant(Place))
	{
		if (Occupant != this)
		{
			// `thunk_FUN_10279cc0(this, other)` then, with the marker's `+0x514` byte set, a motor
			// yaw toward the other body. Both are seams; the ordering is retail's.
			SetMotorHintYaw(static_cast<float>(
				(Occupant->Origin - Origin).Rotation().Yaw));
		}
	}

	const FElysiumInterestingPlaceType* TypeRow = AmbientType(Place);
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);

	if (bRefreshActivity)
	{
		const FString IdleName = TypeRow && !TypeRow->Activities.IsEmpty()
			? TypeRow->Activities[0].Name : FString();
		int32 Activity = ActivityIdForName(IdleName);
		if (Activity == INDEX_NONE)
		{
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: %s"), GHintsWarnInterestActivity, *IdleName);
			Activity = GHintsActivityIdleFallback;
		}
		// Retail restarts only when the id differs from `m_Activity` (`+0xfec`) OR the current
		// sequence does not loop — a looping idle already playing is left alone.
		if (Activity != CurrentRetailActivityId() || !DoesHintSequenceLoop())
		{
			RestartIdealActivityId(Activity);
		}
		AmbientNextActivityAt = Now + Stream.FRandRange(GHintsInterestRefreshMin,
			GHintsInterestRefreshMax);
	}

	// The wait's end. `INTERESTING_LOST` short-circuits the deadline.
	if (Now >= ScheduleHost.WaitFinished || NpcFlags.Has(EElysiumNpcFlag2::INTERESTING_LOST))
	{
		if (NpcFlags.Has(EElysiumNpcFlag::INTERESTING_INTO))
		{
			// Enter the OUTOF phase — unless one is already running or the INTO has not played yet.
			// Retail's guard reads `phase != 1 && (phase == 2 || phase != 3)`, which is exactly
			// "neither 1 nor 3".
			if (AmbientPhase != EAmbientPhase::Into && AmbientPhase != EAmbientPhase::Out)
			{
				const FString OutName = TypeRow && !TypeRow->OutOfActivities.IsEmpty()
					? TypeRow->OutOfActivities[0].Name : FString();
				int32 Activity = ActivityIdForName(OutName);
				if (Activity == INDEX_NONE)
				{
					UE_LOG(LogElysiumNpcEnt, Warning, TEXT("%s: %s"),
						GHintsWarnInterestOutOfActivity, *OutName);
					Activity = GHintsActivityIdleFallback;
				}
				AmbientPhase = EAmbientPhase::Out;                 // `+0x6304 = 3`
				RestartIdealActivityId(Activity);
				AmbientNextActivityAt = GHintsNoActivityRefresh;
				ScheduleHost.WaitFinished = Now + GHintsInterestOutOfWaitSeconds;
			}
		}
		else
		{
			// No INTO was ever played, so there is nothing to play out of: the task is done.
			bFinished = true;
		}
	}

	ReleaseMotorHintYaw();   // `thunk_FUN_102e1e20(m_pMotor, -1)`
	return bFinished;
}
