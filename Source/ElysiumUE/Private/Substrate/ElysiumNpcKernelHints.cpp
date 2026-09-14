#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Hints** — the hint nodes and the interesting places of `order.md` layers
// 0–9. 38 rows: the slot-566/567 species halves, the Werewolf's whole hint surface (move, teleport,
// break, imperative, activity, groundpoint, schedule), the two `IsHintCoverValid` forwards, the two
// `FindHintNode` bodies, the Boss's centre-line geometry, the two interest-place bodies and the
// five hint-node activity lookups (those live on `FElysiumNpcScheduleHost`). The walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// THE STANDING FACT OF THIS FAMILY: there is no hint node in this substrate — no `CAI_Hint` entity,
// no global hint list, no AI node graph. `ElysiumNpcKernelHints.inl` declares the seam and says so
// at length. Every rule below is ported over `FHintWords`, the typed view of a hint's own datamap
// words, so the rule is the deliverable and the query is the thing that answers nothing.

namespace
{
	// `_DAT_104454c4` — the shared 0.0f float constant of `vampire.dll` (1,328 readers, no writer;
	// `0x102961a0` compares a squared length against it to produce a "non-zero length" bool, and
	// `0x1026a910`'s whole body is `FLD [0x104454c4] / RET 4`). It is what `GetHintDelay` answers
	// and the Z scale `DistToHintCenterLine2D_3` multiplies the line direction's Z by — which is why
	// that body is a 2D distance despite carrying a Z term.
	constexpr float GHintsZero = 0.0f;

	// `_DAT_104ada34` — the segment-distance threshold `CheckJumpPathToHintNode` (`0x1036df50`)
	// tests against, in SOURCE UNITS. **Unrecovered**: the corpus holds no reader that pins its
	// value, and it has exactly two referrers, both that one body. Declared as a named constant so
	// the day it is read the change is one line.
	constexpr float GHintsJumpPathClearanceUnits = 0.0f;

	// `_DAT_104454d0` — the OUTOF-phase wait the interest loop adds to `m_flWaitFinished`
	// (`0x102aa210`). **Unrecovered**: same situation.
	constexpr float GHintsInterestOutOfWaitSeconds = 0.0f;

	// `_DAT_104ce8c0` — the squared-length floor `DistToSegment` (`0x103c6b70`) calls degenerate
	// before dividing by it. **Unrecovered**: its WIDTH; it has one reader and nothing pins the
	// value. What IS recovered is the arm's ANSWER — `_DAT_104454c4`, the shared `0.0f` — and that
	// is what the body returns. `UE_SMALL_NUMBER` stands in for the width so the divide stays
	// guarded and a truly zero-length segment reaches the arm; it claims nothing about retail's.
	constexpr float GHintsSegmentEpsilon = UE_SMALL_NUMBER;

	// `vec3_invalid`, `DAT_10713de0/de4/de8`. `staticinit_101371a0` writes `0x7f7fffff` — `FLT_MAX` —
	// into all three, and `CNPC_VWerewolf::GetGroundpoint` (`0x103d6a40`) answers it for a ground
	// trace that did not hit. Recovered by family Motor, which owns `GetGroundpoint`.
	constexpr float GHintsVec3Invalid = MAX_FLT;

	// `RandomFloat(2.0, 10.0)`, the interest-place activity refresh both interest bodies draw.
	constexpr float GHintsInterestRefreshMin = 2.0f;   // 0x40000000
	constexpr float GHintsInterestRefreshMax = 10.0f;  // 0x41200000

	// `m_flNextActivityTime = -1.0f` (`0xbf800000`), the "no refresh pending" sentinel both interest
	// bodies write and the loop tests for.
	constexpr double GHintsNoActivityRefresh = -1.0;

	// Retail's condition numbers, spelled as the registry numbers the bodies push. `EElysiumNpcCond`
	// carries the base table 0x00..0x76; 0x78 is above it and belongs to a Troika-line registrar the
	// census has not decoded, so it is cast rather than named.
	constexpr int32 GHintsCondMoveHintAvailable = 0x78;  // CNPC_VWerewolf's move-hint signal
	constexpr int32 GHintsCondHintIdle = 99;             // `0x102aaa60`'s fallback test

	EElysiumNpcCond HintsCond(int32 RetailCondition)
	{
		return static_cast<EElysiumNpcCond>(RetailCondition);
	}

	// `AngleVectors` (`0x10139550`), forward only — the same body family Facing already recovered
	// (`ElysiumNpcKernelFacing.cpp` § `RetailForward`), repeated here rather than exported because
	// it is a four-line Source identity and a cross-family header would be the larger dependency.
	FVector HintsRetailForward(const FVector& SourceAngles)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		return FVector(FMath::Cos(Pitch) * FMath::Cos(Yaw),
			-(FMath::Cos(Pitch) * FMath::Sin(Yaw)), -FMath::Sin(Pitch));
	}

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
	// SEAM: retail's hint words live on a `CAI_Hint` ENTITY reached through `m_pHintNode`
	// (`+0x5ddc`). This runtime carries hint references as bare indices and stands no hint store,
	// so there is nothing to read `m_nHintType` (`+0x5dc`), `m_iGroupID` (`+0x470`),
	// `m_strTargetName` (`+0x468`) or the four target-range floats off.
	(void)HintNode;
	(void)Out;
	return false;
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
	// SEAM for `CGlobalEntityList::FindEntityByName` (`0x100f7770`) narrowed to `CAI_Hint` by
	// `__RTDynamicCast`. `FElysiumEntityWorld::FindByName` exists and its matcher is already
	// retail's (`NameMatches`), but nothing it can return is a hint, so this refuses rather than
	// handing back an arbitrary entity as if it were one.
	(void)HintName;
	return INDEX_NONE;
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

int32 FElysiumNpc::JumpPathSector(const FVector& PositionCm) const
{
	// SEAM for `CNPC_VChangBros::GetSector(pos)`. The Chang fight partitions its arena into numbered
	// sectors; nothing on this substrate carries that partition.
	//
	// It answers 4 — the value `CheckJumpPathToHintNode` tests for, and the one that CLOSES its
	// gate. Both callers of `GetSector` in this band read it the same way: sector 4 refuses.
	// Refusing a jump that cannot be validated is the conservative side, and it agrees with family
	// Motor's `ChangBrosSector`, which answers 4 for `CheckForJumpAttack` for the same reason.
	(void)PositionCm;
	return 4;
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

bool FElysiumNpc::IsTzimisceHintUsable(int32 HintNode, const FElysiumEntity* Anchor) const
{
	// SEAM for `0x103bfc20`.
	(void)HintNode;
	(void)Anchor;
	return false;
}

bool FElysiumNpc::WerewolfHintTrace(const FVector& PositionCm) const
{
	// SEAM for the Werewolf's vtable `+0x9a4` (slot 617) trace. Retail's caller treats a FALSE
	// answer as "the platform is reachable, take the hint"; this answers true, the blocked side,
	// because a trace that was never run must not authorise a teleport.
	(void)PositionCm;
	return true;
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
// Slot 566 `FValidateHintType` — the species table.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FHintTypeSpecies* FElysiumNpc::HintTypeSpeciesRows(int32& OutCount)
{
	using ERule = FElysiumNpc::EHintTypeRule;
	static const FHintTypeSpecies Rows[] = {
		// The five rules, each read from its decompiled body.
		// `CNPC_Crow`: `return *(int *)(param_1 + 0x5dc) == 700;`
		{ TEXT("CNPC_Crow"), TEXT("0x10358c60"), ERule::Equals, 700, 700, 0, false },
		// `CNPC_VDog`: `== 12000`.
		{ TEXT("CNPC_VDog"), TEXT("0x10374aa0"), ERule::Equals, 12000, 12000, 0, false },
		// `CNPC_VSabbatLeader`: `15999 < t && t < 0x3e86` — 16000..16005 inclusive.
		{ TEXT("CNPC_VSabbatLeader"), TEXT("0x103a9340"), ERule::InRange, 16000, 16005, 0, false },
		// `CNPC_VTzimisce`: `param_1 != 0 && 13999 < t && t < 0x36b2` — 14000..14001, and the ONE
		// body in the set that null-checks the hint.
		{ TEXT("CNPC_VTzimisce"), TEXT("0x103ba780"), ERule::InRange, 14000, 14001, 0, true },
		// `CNPC_VWerewolf`: `t != 0x3a9f && 14999 < t && t < 0x3aab` — 15000..15018 except 15007.
		{ TEXT("CNPC_VWerewolf"), TEXT("0x103d7ce0"), ERule::InRangeExcept, 15000, 15018, 15007,
			false },
		// The six constant bodies, whose whole text is `return 1;` or `return 0;`. They are rows
		// rather than omissions because a dispatcher that cannot answer for them would fall through
		// to the base body, which is a DIFFERENT answer.
		{ TEXT("CNPC_VAndreiBlood"), TEXT("0x1035db00"), ERule::AlwaysTrue, 0, 0, 0, false },
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10361470"), ERule::AlwaysTrue, 0, 0, 0, false },
		{ TEXT("CNPC_VChangBros"), TEXT("0x1036c6a0"), ERule::AlwaysTrue, 0, 0, 0, false },
		{ TEXT("CNPC_VSheriffMan"), TEXT("0x103af810"), ERule::AlwaysTrue, 0, 0, 0, false },
		{ TEXT("CNPC_VZombie"), TEXT("0x103e03b0"), ERule::AlwaysFalse, 0, 0, 0, false },
		// Story 29d, family Senses10. `CNPC_VBach::FValidateHintType` (`0x10365800`):
		// `16999 < t && t < 0x426e` — 17000..17005 — accepted OUTRIGHT, and everything else FALLING
		// THROUGH to `CAI_BaseNPCTroika::FValidateHintType` (`0x10295c20`) rather than answering
		// false, which is why it takes its own rule kind. 29c-1 left it out deliberately ("chains
		// into the base body, so it is 29d's"); this is 29d putting it in.
		{ TEXT("CNPC_VBach"), TEXT("0x10365800"), ERule::InRangeOrBase, 17000, 17005, 0, false },
		// `CNPC_VChangBrosBlade` and `CNPC_VChangBrosClaw` fill the slot with the SAME body as
		// `CNPC_VChangBros` (`0x1036c6a0`) and reach it by inheritance, so the base-chain walk in
		// `HintTypeSpeciesOf` is what serves them rather than two duplicate rows. Recorded here so a
		// reader checking `slots.md` does not read their absence as a gap.
	};
	OutCount = UE_ARRAY_COUNT(Rows);
	return Rows;
}

const FElysiumNpc::FHintTypeSpecies* FElysiumNpc::HintTypeSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	int32 Count = 0;
	const FHintTypeSpecies* Rows = HintTypeSpeciesRows(Count);
	// The exact class first, then the base chain — a species with no body of its own at slot 566
	// inherits its base's, exactly as the vtable does.
	for (int32 i = 0; i < Count; ++i)
	{
		if (FCString::Strcmp(Rows[i].RetailClass, InRetailClass) == 0)
		{
			return &Rows[i];
		}
	}
	const FElysiumNpcClass* Cls = ElysiumNpcKernelClass::Find(InRetailClass);
	for (int32 i = 0; i < Count; ++i)
	{
		if (ElysiumNpcKernelClass::DerivesFrom(Cls, Rows[i].RetailClass))
		{
			return &Rows[i];
		}
	}
	return nullptr;
}

bool FElysiumNpc::FValidateHintTypeSpecies(const FHintTypeSpecies* Row, int32 HintType)
{
	if (Row == nullptr)
	{
		// No species override. The base body `0x10295c20` answers, and that is story 29d's.
		return false;
	}
	switch (Row->Rule)
	{
	case EHintTypeRule::AlwaysTrue:
		return true;
	case EHintTypeRule::AlwaysFalse:
		return false;
	case EHintTypeRule::Equals:
		return HintType == Row->Lo;
	case EHintTypeRule::InRange:
		return HintType >= Row->Lo && HintType <= Row->Hi;
	case EHintTypeRule::InRangeExcept:
		return HintType != Row->Except && HintType >= Row->Lo && HintType <= Row->Hi;
	case EHintTypeRule::InRangeOrBase:
		// `10365804`: `16999 < t && t < 0x426e` answers true outright; every other type falls
		// through to the base body, which `HintTypeSpeciesFallsThroughToBase` is how a caller knows.
		return HintType >= Row->Lo && HintType <= Row->Hi;
	}
	return false;
}

bool FElysiumNpc::HintTypeSpeciesFallsThroughToBase(const FHintTypeSpecies* Row)
{
	return Row != nullptr && Row->Rule == EHintTypeRule::InRangeOrBase;
}

bool FElysiumNpc::FValidateHintTypeForSpecies(int32 HintNode) const
{
	const FElysiumNpcClass* Cls = RetailClass();
	const FHintTypeSpecies* Row = HintTypeSpeciesOf(Cls ? Cls->Name : nullptr);
	FHintWords Hint;
	if (!HintWords(HintNode, Hint))
	{
		// Retail's four non-Tzimisce bodies would have dereferenced a null hint here; only
		// `CNPC_VTzimisce` tests it and answers false. The seam cannot hand back a hint at all, so
		// every species takes the refusal — reported, not papered over.
		return false;
	}
	if (FValidateHintTypeSpecies(Row, Hint.HintType))
	{
		return true;
	}
	// `CNPC_VBach`'s row (`0x10365800`) falls through to the base body rather than refusing. Story
	// 29d, family Senses10 wrote this against slot 566's generated stub and passed `nullptr`; family
	// Hints10 landed the real body (`0x10295c20`), whose first act is a null-hint refusal, so the
	// RESOLVED HINT is what retail's fall-through hands it. Named minimal fix, story 29d/Hints10.
	if (HintTypeSpeciesFallsThroughToBase(Row))
	{
		return const_cast<FElysiumNpc*>(this)->FValidateHintType(&Hint);
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// Slot 567 `GetHintActivity` — the one species override.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::CrowHintActivity(int16 HintType, int32 BaseAnswer)
{
	// `0x10358c90`: `if (param_1 == 700) return 0x22;` then a tail-call into the base body
	// `CAI_BaseNPC::GetHintActivity` (`0x1026a8f0`), whose whole text is `return 1`.
	return HintType == 700 ? 0x22 : BaseAnswer;
}

int32 FElysiumNpc::GetHintActivitySpecies(int16 HintType) const
{
	const int32 BaseAnswer = const_cast<FElysiumNpc*>(this)->GetHintActivity(HintType);
	return IsRetailClass(TEXT("CNPC_Crow")) ? CrowHintActivity(HintType, BaseAnswer) : BaseAnswer;
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
	return GHintsZero;
}

// -------------------------------------------------------------------------------------------------
// The Werewolf's move and teleport hints.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::ClearMoveHint()
{
	// `CNPC_VWerewolf::ClearMoveHint` (`0x103d4690`).
	if (MoveHintNode != INDEX_NONE)
	{
		ReleaseHintNode(MoveHintNode, 0.0f);   // `thunk_FUN_102d1420(m_pMoveHint, 0.0)`
	}
	MoveHintNode = INDEX_NONE;
	// `thunk_FUN_10269b50(this, 0x78)` — the clear is UNCONDITIONAL, outside the null test above.
	Cognition.Conditions.Clear(HintsCond(GHintsCondMoveHintAvailable));
}

void FElysiumNpc::SetMoveHint(int32 HintNode, bool bRandom)
{
	// `CNPC_VWerewolf::SetMoveHint` (`0x103d44e0`). Retail's order matters: the existing hint is
	// released first (which CLEARS condition 0x78), then `m_bRandomHint` is written BEFORE
	// `m_pMoveHint`, and only then is 0x78 raised again.
	if (MoveHintNode != INDEX_NONE)
	{
		ClearMoveHint();
	}
	bRandomHint = bRandom;
	MoveHintNode = HintNode;
	// `(**(code **)(*DAT_10924a6c + 4))()` sits between the write and the condition. **Unrecovered**:
	// `DAT_10924a6c` has no other referrer in the corpus and the call takes no visible argument.
	Cognition.Conditions.Set(HintsCond(GHintsCondMoveHintAvailable));
}

void FElysiumNpc::ClearTeleportHint()
{
	// `CNPC_VWerewolf::ClearTeleportHint` (`0x103d4760`). No condition is touched, unlike the move
	// hint's clear.
	if (TeleportHintNode != INDEX_NONE)
	{
		ReleaseHintNode(TeleportHintNode, 0.0f);
	}
	TeleportHintNode = INDEX_NONE;
}

void FElysiumNpc::SetTeleportHint(int32 HintNode)
{
	// `CNPC_VWerewolf::SetTeleportHint` (`0x103d45c0`). `m_bRandomHint` is cleared here and set by
	// `SetMoveHint` — one flag shared by the two hint kinds, which is why setting a teleport hint
	// makes a previously random move hint non-random.
	if (TeleportHintNode != INDEX_NONE)
	{
		ClearTeleportHint();
	}
	bRandomHint = false;
	TeleportHintNode = HintNode;
}

// -------------------------------------------------------------------------------------------------
// The Werewolf's hint rules.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::GetHintTeleportPriority(int32 HintType)
{
	// `CNPC_VWerewolf::GetHintTeleportPriority` (`0x103d3220`), a chain of `==` tests in this order.
	// 0x3a9a and 0x3a9b share priority 3; everything unlisted is 0.
	switch (HintType)
	{
	case 0x3a99:  return 2;   // 15001
	case 0x3a9a:  return 3;   // 15002
	case 0x3a9b:  return 3;   // 15003
	case 15000:   return 1;
	case 0x3a9f:  return 4;   // 15007 — the type `FValidateHintType` excludes
	default:      return 0;
	}
}

bool FElysiumNpc::IsValidBreakHint(const FHintWords& Hint, double Now) const
{
	// `CNPC_VWerewolf::IsValidBreakHint` (`0x103d8550`). Three arms in order: a null hint is false;
	// a hint `0x102d14c0` calls unusable is false; and the type must be exactly 0x3aa3 (15011).
	if (!Hint.bValid)
	{
		return false;
	}
	if (IsHintUnusable(Hint, Now, Hint.HintOwner.IsSet()))
	{
		return false;
	}
	return Hint.HintType == 0x3aa3;
}

int32 FElysiumNpc::SelectScheduleForHint(const FHintWords* Hint, float DistToSavePositionUnits,
	float GoalToleranceUnits)
{
	// `CNPC_VWerewolf::SelectScheduleForHint` (`0x103ce9b0`).
	if (Hint == nullptr || !Hint->bValid)
	{
		return 0x163;
	}
	switch (Hint->HintType)
	{
	case 0x3aa4:  return 0x15f;   // 15012
	case 0x3aa5:  return 0x160;   // 15013
	case 0x3aaa:  return 0x164;   // 15018
	default:      break;
	}
	// The tail: `sqrt(|m_vSavePosition - GetAbsOrigin()|^2) > m_flGoalTolerance`. Retail takes the
	// square root and compares the DISTANCE, not the squared distance, so the threshold is in the
	// same units as `m_flGoalTolerance` — reproduced rather than optimised into a squared compare.
	return DistToSavePositionUnits > GoalToleranceUnits ? 0x15a : 0x15b;
}

int32 FElysiumNpc::SelectScheduleForHint(int32 HintNode) const
{
	FHintWords Hint;
	const bool bResolved = HintWords(HintNode, Hint);
	const float DistUnits =
		static_cast<float>(FVector::Dist(SavePosition, Origin) / ElysiumMove::U);
	const float ToleranceUnits = ScheduleHost.GoalToleranceCm / ElysiumMove::U;
	return SelectScheduleForHint(bResolved ? &Hint : nullptr, DistUnits, ToleranceUnits);
}

int32 FElysiumNpc::HintActivityForType(int32 HintType, bool bPercentRollPassed, bool bCoinFlip)
{
	// `CNPC_VWerewolf::SetHintActivity` (`0x103d6000`), the switch. `bPercentRollPassed` is
	// `RandomInt(1, 100) <= <cvar>` and subtracts ONE from three of the answers; `bCoinFlip` is
	// `RandomInt(0, 1) != 0` and does the same for 0x3aa9 alone.
	const int32 Lower = bPercentRollPassed ? 1 : 0;
	switch (HintType)
	{
	case 15000:   return 0x10c;
	case 0x3a99:  return 0x119;
	case 0x3a9a:  return 0x11a;
	case 0x3a9b:  return 0x11b;
	case 0x3a9c:  return 0x113 - Lower;
	case 0x3a9d:  return 0x115 - Lower;
	case 0x3a9e:  return 0x117 - Lower;
	case 0x3a9f:  return 0x10d;
	case 0x3aa0:  return 0x10f;
	case 0x3aa1:  return 0x110;
	case 0x3aa2:  return 0x10e;
	case 0x3aa3:  return 0x111;
	case 0x3aa6:  return 0x122;
	case 0x3aa7:  return 0x123;
	case 0x3aa8:  return 0x10b;
	case 0x3aa9:  return 0x125 - (bCoinFlip ? 1 : 0);
	case 0x3aaa:  return 0x121;
	default:      return INDEX_NONE;   // retail's `default:` returns false without positioning
	}
}

bool FElysiumNpc::SetHintActivity(const FHintWords& Hint)
{
	// The whole body of `0x103d6000`. Note the ORDER retail draws in: the percentage cvar is read
	// and `RandomInt(1, 100)` is drawn BEFORE the switch, on every call, whether or not the type
	// that lands uses it. The coin flip inside case 0x3aa9 is a second draw.
	if (!Hint.bValid)
	{
		return false;
	}

	// `cVar3 = DAT_1093f85c->vtable[1](); iVar7 = cVar3 ? 0 : DAT_1093f85c[0xb];` — a `ConVar`,
	// `IsCommand()` on slot 1 and the int value at `+0x2c`, used as a percentage.
	// **Unrecovered**: the cvar's NAME. `DAT_1093f85c` has exactly two referrers and both are this
	// body, so nothing in the corpus names it. Zero is the honest stand-in: `RandomInt(1, 100) <= 0`
	// is never true, which is the same answer a default-zero percentage cvar gives.
	constexpr int32 HintActivityVariantPercent = 0;
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	const bool bPercentRollPassed = Stream.RandRange(1, 100) <= HintActivityVariantPercent;
	const bool bCoinFlip = Stream.RandRange(0, 1) != 0;

	const int32 Activity = HintActivityForType(Hint.HintType, bPercentRollPassed, bCoinFlip);
	if (Activity == INDEX_NONE)
	{
		return false;
	}
	PositionAtHint(Hint);
	RestartIdealActivityId(Activity);
	return true;
}

bool FElysiumNpc::IsVec3Invalid(const FVector& Value)
{
	// `vec3_invalid` is `FLT_MAX` in all three components. Testing one is enough — nothing writes a
	// partial one — but all three are tested because the sentinel is defined as the triple.
	return Value.X >= GHintsVec3Invalid && Value.Y >= GHintsVec3Invalid
		&& Value.Z >= GHintsVec3Invalid;
}

FVector FElysiumNpc::GetHintGroundpoint(const FHintWords& Hint) const
{
	// `CNPC_VWerewolf::GetHintGroundpoint` (`0x103d6770`). A linear scan of the authored array at
	// `+0x6714` (count `+0x6720`, stride 0x48, hint pointer at `+0x00`, groundpoint at `+0x08`) for
	// the entry whose hint matches, and on a miss retail's DevWarning plus `GetGroundpoint` of the
	// hint's own origin. SOURCE UNITS throughout, as retail's are.
	for (const FWerewolfHintGroundpoint& Row : WerewolfHintGroundpoints)
	{
		if (Row.HintNode != INDEX_NONE && Row.HintNode == Hint.NodeId)
		{
			return Row.GroundpointUnits;
		}
	}
	// The array is filled by no producer in this runtime, so this arm is the only one reached. It is
	// still retail's arm and not a refusal.
	UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Werewolf did not find the hint %s"), *Hint.Name);
	// Family Motor's `GetGroundpoint` (`0x103d6a40`) takes and answers SOURCE UNITS, and answers
	// `vec3_invalid` when the ground trace does not hit — which, with no trace on this substrate, is
	// always. Retail's caller does not test it either; `PositionAtHint` is where that lands.
	return GetGroundpoint(Hint.OriginCm / ElysiumMove::U);
}

void FElysiumNpc::PositionAtHint(const FHintWords& Hint)
{
	// `CNPC_VWerewolf::PositionAtHint` (`0x103d6280`): the groundpoint into the origin (vtable
	// `+0xf8`), the HINT's own angles into the angles (vtable `+0x368` fed from the hint's `+0x36c`),
	// then `CBaseEntity::Relink`. The port's `SetRuntimeTransform` is the same pair of writes plus
	// the body-follow hook, which is what Relink stands for here.
	if (!Hint.bValid)
	{
		return;   // retail's `if (param_1 != NULL)` guard
	}
	const FVector GroundUnits = GetHintGroundpoint(Hint);
	if (IsVec3Invalid(GroundUnits))
	{
		// NAMED MODERNIZATION, and the only one in this body. Retail does NOT test the groundpoint:
		// a `GetGroundpoint` miss answers `vec3_invalid` (`FLT_MAX` in all three) and retail writes
		// it straight into the origin. This runtime declines the move instead, because `FLT_MAX`
		// centimetres is not a transform the engine can carry — the scene component would take an
		// infinite location and every downstream distance would be a NaN. The recovered fact is
		// recorded here rather than reproduced: retail's Werewolf, handed a hint with no authored
		// groundpoint over no ground, teleports itself out of the world.
		return;
	}
	SetRuntimeTransform(GroundUnits * ElysiumMove::U, Hint.Angles);
}

bool FElysiumNpc::IsImperativeTeleportHint(const FHintWords& Hint) const
{
	// `CNPC_VWerewolf::IsImperativeTeleportHint` (`0x103d3360`). Four gates on the Werewolf's own bit
	// word `+0x66e8`, each admitting one or two authored hint NAMES at one hint type. Retail inlines
	// `FStrEq`-with-trailing-`*` at every compare; `FElysiumEntityWorld::NameMatches` is that exact
	// matcher, already recovered, so it is what the compares go through. `0x103d3a40` — the one
	// compare retail left as a call — is the same matcher against `this->m_iName`.
	const bool bBit0x200 = (WerewolfHintFlags & 0x200) == 0x200;
	const bool bBit0x400 = (WerewolfHintFlags & 0x400) == 0x400;
	const bool bBit0x4 = (WerewolfHintFlags & 0x4) == 0x4;

	if (bBit0x200)
	{
		if (Hint.HintType == 0x3aa9
			&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("cheater_outside_hint_2")))
		{
			return true;
		}
		if (Hint.HintType == 15000
			&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("shard_hint_3")))
		{
			return true;
		}
	}
	if (bBit0x400 && Hint.HintType == 0x3aa9
		&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("cheater_outside_hint_3")))
	{
		return true;
	}
	if (bBit0x4)
	{
		// The door-state split. `m_DoorState` 2 or 3 is one pair of arms, anything else the other.
		if (WerewolfDoorState == 2 || WerewolfDoorState == 3)
		{
			if (Hint.HintType == 0x3aa9
				&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("cheater_outside_hint_1")))
			{
				return true;
			}
			if (Hint.HintType == 0x3aa4
				&& (FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("jump_to_platform_hint_1"))
					|| FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("jump_to_platform_hint_2"))))
			{
				// The one arm with a trace behind it: retail answers TRUE when the trace comes back
				// CLEAR (`cVar2 == '\0'`) and falls through to false otherwise.
				return !WerewolfHintTrace(Hint.OriginCm);
			}
		}
		else
		{
			if (Hint.HintType == 15000
				&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("skylight_2_teleports")))
			{
				return true;
			}
			// `thunk_FUN_103d3a40(param_1, "fulldoor_a_3_breakthrough_f")` — the one compare retail
			// left as a call rather than inlining. Its `this` is `param_1`, the HINT, so it reads
			// the hint's `m_iName` (`+0x26c`) exactly as the inlined compares above do.
			if (Hint.HintType == 0x3a99
				&& FElysiumEntityWorld::NameMatches(Hint.Name, TEXT("fulldoor_a_3_breakthrough_f")))
			{
				return true;
			}
		}
	}
	return false;
}

// -------------------------------------------------------------------------------------------------
// The Chang brothers' jump path and the Boss's centre-line geometry.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::AddHintToStoredJumpPositions(const FHintWords& Hint)
{
	// `CNPC_VAsianVampire::AddHintToStoredJumpPositions` (`0x10361990`). A two-slot ring: write the
	// hint's origin at the index, advance, and wrap when the advanced index is greater than 1. The
	// buffer size 2 is baked into the wrap test, not read from anywhere.
	if (!Hint.bValid)
	{
		return;
	}
	// The ring lives on family Motor's `LastJumpPosition` pair, in SOURCE UNITS.
	LastJumpPosition[LastJumpPositionIdx] = Hint.OriginCm / ElysiumMove::U;
	++LastJumpPositionIdx;
	if (LastJumpPositionIdx > 1)
	{
		LastJumpPositionIdx = 0;
	}
}

float FElysiumNpc::DistToSegment(const FVector& A, const FVector& B, const FVector& P)
{
	// `CNPC_VVampireBoss::DistToSegment` `0x103c6b70`, arm for arm:
	//
	//     d = B - A;  lenSq = |d|^2;
	//     if (lenSq < _DAT_104ce8c0) return _DAT_104454c4;      // 0.0f, NOT |P-A|
	//     ap = P - A;  t = dot(ap, d) / lenSq;
	//     if (t > 0.0f) { distSq = (t < 1.0f) ? |P - (A + d*t)|^2 : |P - B|^2; }
	//     else          { distSq = |ap|^2; }
	//     return sqrt(distSq);
	//
	// THE DEGENERATE ARM IS `0.0`. A zero-length segment answers "no distance at all", not the
	// distance to the point — which for `CheckJumpPathToHintNode`, whose test is `dist < threshold`,
	// means a hint sitting exactly on the NPC always BLOCKS the jump. Story 29c-1's first pass here
	// wrote `|P-A|` and was wrong; family Positions found the divergence.
	//
	// Ghidra spells the sign test `(t < 0.0) == (t == 0.0)`, which is true only when both are false,
	// i.e. `t > 0`. `t == 0` therefore takes the `|ap|` arm — the same value the projection arm
	// would give at `t == 0`, so the split is not observable, but it is retail's split and is kept.
	//
	// 3D throughout: `CheckJumpPathToHintNode` zeroes the Z of the two ENDPOINTS before calling and
	// leaves the player's Z alone, so the Z difference does reach the answer.
	const FVector D = B - A;
	const float LenSq = static_cast<float>(D.SizeSquared());
	if (LenSq < GHintsSegmentEpsilon)
	{
		return GHintsZero;
	}
	const FVector AP = P - A;
	const float T = static_cast<float>(FVector::DotProduct(AP, D)) / LenSq;
	float DistSq;
	if (T > GHintsZero)
	{
		DistSq = T < 1.0f
			? static_cast<float>((P - (A + D * T)).SizeSquared())
			: static_cast<float>((P - B).SizeSquared());
	}
	else
	{
		DistSq = static_cast<float>(AP.SizeSquared());
	}
	return FMath::Sqrt(DistSq);
}

bool FElysiumNpc::CheckJumpPathToHintNode(const FHintWords& Hint) const
{
	// `CNPC_VChangBros::CheckJumpPathToHintNode` (`0x1036df50`).
	//
	// The segment is (my origin, the hint's origin) with the Z of BOTH endpoints forced to 0 — the
	// decompiler shows `fStack_4 = 0.0` overwriting the origin's Z it had just read, and
	// `uStack_10 = 0` doing the same to the hint end.
	if (!Hint.bValid)
	{
		return false;
	}
	// `m_hClosestPlayer` (`+0x628c`), the sense pass's cache — `FElysiumNpcMemory::ClosestPlayer`.
	// Dead or unset and retail falls straight to the blocked return without asking anything else.
	const FElysiumNpcMemory& Mem = Senses.Memory;
	FElysiumEntity* ClosestPlayer = Mem.ClosestPlayer.IsSet() && World
		? World->Resolve(Mem.ClosestPlayer) : nullptr;
	if (ClosestPlayer == nullptr || ClosestPlayer->IsInert())
	{
		return false;
	}
	FVector From = Origin;
	From.Z = 0.0;
	FVector To = Hint.OriginCm;
	To.Z = 0.0;

	if (DistToSegment(From, To, ClosestPlayer->Origin) < GHintsJumpPathClearanceUnits)
	{
		// The player stands on the jump line. Blocked.
		return false;
	}
	// Only when the player is clear does retail ask about the other brother, and a brother ON the
	// line blocks. `GetOtherBrother` is family Squad's ported body (`0x1036e2f0`).
	if (const FElysiumNpc* Brother = GetOtherBrother())
	{
		if (DistToSegment(From, To, Brother->Origin) < GHintsJumpPathClearanceUnits)
		{
			return false;
		}
	}
	// Both endpoints must be outside sector 4. Retail tests MY sector first and only asks about the
	// hint's when mine is not 4, so a body already in sector 4 is blocked without a second query.
	if (JumpPathSector(Origin) == 4)
	{
		return false;
	}
	return JumpPathSector(Hint.OriginCm) != 4;
}

float FElysiumNpc::DistToHintCenterLine2D_3(const FVector& LineStart, const FVector& LineDir,
	const FVector& Point)
{
	// `CNPC_VVampireBoss::DistToHintCenterLine2D_3` (`0x103c6680`), transcribed from the listing
	// (`103c6716`–`103c6781`) because the decompiler drops the final `sqrt` call's result.
	//
	//   t  = (P.x - S.x) * D.x + (P.y - S.y) * D.y + D.z * K
	//   dx = (t * D.x + S.x) - P.x
	//   dy = (t * D.y + S.y) - P.y
	//   r  = sqrt(dx*dx + dy*dy + (t * D.z) * (t * D.z))
	//
	// `K` is `_DAT_104454c4`, the image's shared 0.0f, so the `D.z * K` term contributes nothing —
	// and `DistToHintCenterLine2D` zeroes `D.z` before calling in, which kills the third term too.
	// That is why this is a 2D distance. Both terms are kept because retail's arithmetic is the
	// deliverable, not the algebra it simplifies to.
	const float Sx = static_cast<float>(LineStart.X);
	const float Sy = static_cast<float>(LineStart.Y);
	const float Dx = static_cast<float>(LineDir.X);
	const float Dy = static_cast<float>(LineDir.Y);
	const float Dz = static_cast<float>(LineDir.Z);
	const float Px = static_cast<float>(Point.X);
	const float Py = static_cast<float>(Point.Y);

	const float T = (Px - Sx) * Dx + (Py - Sy) * Dy + Dz * GHintsZero;
	const float OffX = (T * Dx + Sx) - Px;
	const float OffY = (T * Dy + Sy) - Py;
	return FMath::Sqrt(OffX * OffX + OffY * OffY + (T * Dz) * (T * Dz));
}

float FElysiumNpc::DistToHintCenterLine2D(const FHintWords& Hint, const FVector& PointCm)
{
	// `CNPC_VVampireBoss::DistToHintCenterLine2D_2` (`0x103c6570`): take the hint's origin with Z
	// forced to 0 as the line start, `AngleVectors(hint->GetAbsAngles())`'s forward with Z forced to
	// 0 and re-normalised as the direction, then forward to `_3`.
	FVector Start = Hint.OriginCm;
	Start.Z = 0.0;

	// `AngleVectors(hint->GetAbsAngles(), &forward)` — pitch AND yaw, Source's own formula.
	FVector Forward = HintsRetailForward(Hint.Angles);
	Forward.Z = 0.0;
	Forward.Normalize();

	return DistToHintCenterLine2D_3(Start, Forward, PointCm);
}

// -------------------------------------------------------------------------------------------------
// Finding and installing a hint node.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::FindHintEndEntity(const FHintWords& Hint) const
{
	// `CNPC_VWerewolf::FindHintEndEntity` (`0x103d6520`). Two hops, and they are NOT symmetric:
	//   1. if the hint carries `m_strTargetName` (`+0x468`), look it up by name and RTTI-cast the
	//      result to `CAI_Hint`; a failed cast leaves null;
	//   2. null falls back to the hint itself;
	//   3. if THAT hint carries a target name, look it up again — with NO cast this time, so retail
	//      can hand back an entity that is not a hint at all;
	//   4. a null second hop returns the result of step 2.
	int32 First = INDEX_NONE;
	if (!Hint.TargetName.IsEmpty())
	{
		First = FindHintByName(Hint.TargetName);
	}
	if (First == INDEX_NONE)
	{
		First = Hint.NodeId;
	}
	FHintWords FirstWords;
	if (HintWords(First, FirstWords) && !FirstWords.TargetName.IsEmpty())
	{
		const int32 Second = FindHintByName(FirstWords.TargetName);
		if (Second != INDEX_NONE)
		{
			return Second;
		}
	}
	return First;
}

bool FElysiumNpc::FindHintNode(int32 HintType, uint8 SearchFlags)
{
	// `0x10365780` — the task-side hint install.
	const int32 Node = FindHintNear(HintType, SearchFlags, 5000.0f);
	ScheduleHost.HintNode = Node;   // `m_pHintNode +0x5ddc` is written on BOTH paths
	if (Node != INDEX_NONE)
	{
		// `thunk_FUN_10273e80(this, '\0')` — `TaskComplete(false)`.
		Schedule.bTaskCompletedExternally = true;
		return true;
	}
	// The miss arm writes retail's assert file and line into `+0x1b44` / `+0x1b48`
	// (`"E:\Vampire\main\dlls\hl2_dll\NPC_..."`, line 0x48a = 1162) before failing. The shape map
	// records `+0x1b44` ABSENT — this runtime carries no assert file/line pair — so only the fail
	// lands, and that is stated rather than faked.
	TaskFail(4);
	return false;
}

int32 FElysiumNpc::SelectTzimisceHintNode(const FElysiumEntity* Anchor)
{
	// `0x103bfa50`. Two competing hint groups within 200 units of `Target`, the nearer wins, the
	// loser is released with a 0.5 s reuse delay, and the answer is a schedule id chosen by whether
	// the winner is usable.
	if (Anchor == nullptr)
	{
		ScheduleHost.HintNode = INDEX_NONE;   // retail zeroes `m_pHintNode`
		return 0;
	}

	const int32 A = FindHintOfTypeNear(Anchor, 14000, 0, 200.0f);
	const int32 B = FindHintOfTypeNear(Anchor, 0x36b1, 0, 200.0f);

	auto Install = [this, Anchor](int32 Node, int32 BaseSchedule) -> int32
	{
		ScheduleHost.HintNode = Node;
		// `(-(uint)usable & 0xfffffff6) + Base` — usable subtracts 10 from the base id.
		return IsTzimisceHintUsable(Node, Anchor) ? BaseSchedule - 10 : BaseSchedule;
	};

	if (A != INDEX_NONE)
	{
		if (B == INDEX_NONE)
		{
			return Install(A, 0x36ba);
		}
		// The compare is on the vtable `+0x370` position accessor of the target and of each hint,
		// squared, with no tie-break: an exact tie takes the B branch.
		const FVector AnchorPos = HintComparePosition(Anchor);
		FHintWords WordsA;
		FHintWords WordsB;
		HintWords(A, WordsA);
		HintWords(B, WordsB);
		const double DistA = FVector::DistSquared(WordsA.OriginCm, AnchorPos);
		const double DistB = FVector::DistSquared(WordsB.OriginCm, AnchorPos);
		if (DistA < DistB)
		{
			ReleaseHintNode(B, 0.5f);
			return Install(A, 0x36ba);
		}
		ReleaseHintNode(A, 0.5f);
	}
	if (B != INDEX_NONE)
	{
		return Install(B, 0x36bb);
	}
	// Both searches missed. Retail leaves `m_pHintNode` untouched on this path — only the null
	// `Anchor` arm zeroes it — and returns 0.
	return 0;
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
	if (Cognition.Conditions.Has(HintsCond(GHintsCondHintIdle)))
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
