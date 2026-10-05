// `CAI_BaseNPC`'s bodies of the `Motor` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseMotor.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumMover.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumNpcClips.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `CheckOnGround` `0x1026e5e0`.
	constexpr float GCheckOnGroundInterval = ElysiumNpcTunables::Half;
	constexpr double GCheckOnGroundSlack = ElysiumNpcTunables::MinusThousandthDouble;
	constexpr float GCheckOnGroundDown = static_cast<float>(ElysiumNpcTunables::CheckOnGroundReach);   // `_DAT_10449148`, f64
	constexpr float GTraceClearFraction = static_cast<float>(ElysiumNpcTunables::OneDouble);
	constexpr int32 GCoverTraceMask = 0x2804091;    // `ValidateNavGoal`'s
	// The retail contents bit the character half of the trace filters reads (`StandardFilterRules
	// 0x101d3080` at `101d30f2`), and the one mask `CTraceFilterSimple` skips slot 68 under.
	constexpr int32 GTraceMaskMonster = 0x2000000;
	constexpr int32 GTraceMaskNoIgnoreCollision = 0x46004003;
	// Conditions this family touches that `EElysiumNpcCond` does not name. Retail's `CAI_BaseNPC`
	// registrar is one dense namespace 0x00..0x76 and 0x73 sits in the unnamed tail of it; 0x7b is
	// above the base band entirely, so it is a species registration. Both are carried as retail's
	// own number.
	constexpr EElysiumNpcCond GCondOnGround = static_cast<EElysiumNpcCond>(0x73);
	constexpr EElysiumNpcCond GCondNavGoalInvalid = static_cast<EElysiumNpcCond>(0x39);
	// `TaskFail`'s reason on the `ValidateNavGoal` failure (`0x10280360`, `vtable+0x700` slot 448).
	constexpr int32 GFailNoCover = 0x1b;
	constexpr float GJumpApexScale = static_cast<float>(ElysiumNpcTunables::JumpApexScale);

	// `CAI_Navigator::Move` `0x102eff40` and the pass it dispatches (R3). The failure codes are the
	// table `0x1060fcc4` = `{0, 0x0d, 0x0c, 0x0e, 0x0f}`.
	constexpr float GMoveStepMaxInterval = 1.0f;                  // 0x10449280 (double 1.0)
	constexpr int32 GMoveStepFailNoGoal = 0x0d;                   // table[1], 0x102f0081
	constexpr int32 GMoveStepFailNoRoute = 0x0c;                  // table[2], 0x102f00bc / 0x102f0180
	constexpr int32 GMoveStepFailDoor = 0x0e;                     // table[3], 0x102f08e7
	constexpr int32 GMoveStepClimbNavType = 3;                    // nav+0x18 == 3, 0x102f0198
	constexpr int32 GMoveStepMaxPasses = 0x10;                    // `INC EBP; CMP EBP,0x10; JG`
	constexpr float GMoveStepStaleSeconds = 4.0f;                 // `PUSH 0x40800000`, 0x102f016e
	// Navigator slot 16's waypoint arrival radius, 0.0625 units (`0x10451f78`); 0.25 (`0x10449260`) under
	// ConVar `npc_vphysics`, whose shipped value `"0"` the port keeps without reading.
	constexpr float GMoveStepArrivalUnits = 0.0625f;
	constexpr float GMoveStepGoalSlackUnits = 0.1f;               // 0x104491b4, 0x102ef760
	constexpr double GMoveStepTimeSlack = ElysiumNpcTunables::MinusThousandthDouble; // 0x10497530
	// S4 `0x102ef0e0`: `distClear < 1.0` (`FCOMP double [0x10449280]; TEST AH,5; JP`, strictly; NaN
	// fails) is the blocked probe that keeps the trace status (`-3`). The port's post-hold probe is the
	// re-issued leg: a `Blocked` end that closed less than this on the destination is that probe's `-3`.
	constexpr float GMoveStepProbeClearUnits = 1.0f;
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 5) ---

int32 FElysiumNpcBase::NavGetType() const
{
	// `FUN_1027d990`: `return m_pNavigator->field_0x18;` — one word, 29 direct callers, the widest
	// read of this family.
	return Navigator.NavType;
}

void FElysiumNpcBase::NavSetType(int32 Type)
{
	// `FUN_1027d9b0` → `0x102eeba0`: `m_pNavigator->field_0x18 = value`.
	Navigator.NavType = Type;
	// The port's navigator IS `IElysiumNpcMotor`, and it carries the same four-value vocabulary
	// (`EElysiumNpcNavType`: Ground 0, Jump 1, Fly 2, Climb 3), so the stored word is pushed on.
	// A value outside the four has no counterpart and is stored only — retail stores anything.
	if (Motor != nullptr && Type >= 0 && Type <= 3)
	{
		Motor->SetNavigationType(static_cast<EElysiumNpcNavType>(Type));
	}
}

void FElysiumNpcBase::NavSnapshotOwnerPointers(int32 Argument)
{
	// `CAI_Navigator::vfunc3` `0x102ecb50`:
	//     this->+0x20 = owner->+0x5d44;   // m_pMotor
	//     this->+0x24 = owner->+0x5d40;   // m_pMoveProbe
	//     this->+0x28 = owner->+0x5d38;   // m_pLocalNavigator
	//     this->+0x2c = param_1;
	// All three owner words are `ELYSIUM_NPC_WORD_CHAIN` rows onto the one motor, so there are no
	// three pointers to snapshot. What survives is that the snapshot happened and what it was taken
	// with.
	Navigator.bSnapshotTaken = true;
	Navigator.SnapshotArgument = Argument;
}

bool FElysiumNpcBase::NavIsGoalActive() const
{
	// `IsGoalActive` `0x102ee6a0` (`nav+0x30 != 0 && path+0x24 != 0`, a head waypoint exists) -- NOT
	// `0x102ee680`, which is `IsGoalSet` (`path+0x5c != 0`, `NavigatorIsGoalSet`). The head is the
	// navigator's word: set when a leg is accepted, popped by `AdvancePath` / the arrival's
	// `ClearGoal`, and left standing by `OnNavFailed` (the path is not cleared), which is what the
	// kernel's readers must see after a failure.
	return Navigator.IsGoalActive();
}

bool FElysiumNpcBase::NavigatorIsGoalSet() const
{
	return Navigator.IsGoalSet();                                        // 0x102ee680 path+0x5c != 0
}

bool FElysiumNpcBase::NavigatorIsPaused() const
{
	return Navigator.IsPaused();                                         // 0x102ee2e0 path+0x10 m_bPaused
}

void FElysiumNpcBase::NavStopMoving()
{
	// `thunk_FUN_102ee2c0(m_pNavigator)` -> `0x1030bea0`: `path+0x10 m_bPaused := 0` (R1 §3). The body
	// is not stopped: the move step's paused gate (`0x102effab`) is what this lifts.
	Navigator.bPaused = false;
}

int32 FElysiumNpcBase::NavGoalState() const
{
	// `thunk_FUN_102ee620(m_pNavigator)` -- `GetGoalType()`, `path+0x5c`: 0 none, 1 target, 2 enemy,
	// 3 path corner, 4 location, 6 cover, 7 best-unknown, 8 pedestrian place, 9 animal place.
	// `ValidateNavGoal` requires exactly 6. No goal: 0 (the path constructor's and the reset's).
	return Navigator.GetGoalType();
}

bool FElysiumNpcBase::NavLinkActivity(int32& OutActivity) const
{
	// The pair `FUN_1027a6c0` reads off `m_pNavigator` (+0x5d34): `0x102ee6a0` is
	// `CAI_Navigator::IsGoalActive` (`m_pPath` +0x30 and its current waypoint +0x24 both set,
	// `Navigator.IsGoalActive()`), and `0x102ee510` answers the path's movement activity
	// (`0x1030b520(m_pPath)`), which this runtime carries as the navigator's
	// `MovementActivity` (written by `SetGoal`'s activity word, `0x102ee250`; 1 after a reset). The
	// head stands at `TaskMovementComplete` (its `SetIdealActivity` runs before `ClearGoal`) and after
	// `OnNavFailed`, so both write the path's movement activity, as retail's do.
	if (!Navigator.IsGoalActive())                                       // 0x102ee6a0
	{
		return false;
	}
	OutActivity = Navigator.GetMovementActivity();                       // 0x102ee510 path+0x2c
	return true;
}

bool FElysiumNpcBase::AnimIntervalMovement(float Interval, FVector& OutDeltaUnits,
	float& OutYawDelta, bool* OutIntervalFinished) const
{
	OutDeltaUnits = Origin / ElysiumMove::U;                         // 0x10094b70 slot 220
	OutYawDelta = Angles.Y;                                         // 0x10094b70 slot 221
	const FElysiumNpc* const Troika = AsNpc();                       // 0x10094b70 GetModelPtr
	if (Visual == nullptr || Troika == nullptr) return false;        // 0x10094b70 no model
	float CycleTo = SequenceCycle + SequenceCycleRate * Troika->SequencePlaybackRate * Interval; // 0x10094b70
	const bool bIntervalFinished = !bSequenceLoopedOnce && CycleTo > 1.f; // 0x10094b70
	if (OutIntervalFinished != nullptr) *OutIntervalFinished = bIntervalFinished; // 0x10094b70 local output byte
	if (bIntervalFinished) CycleTo = 1.f;                            // 0x10094b70
	// 0x100c5d10 -> 0x100c5400: pose blending is a named seam; the baked path is animation 0.
	const int32 RawIndex = SequenceNumber == 0 ? 0
		: (Troika->SequenceRows.IsValidIndex(SequenceNumber)
			? Troika->SequenceRows[SequenceNumber].RawIndex : INDEX_NONE); // 0x100c5d10 current sequence
	// 0x100c5d10: an unnumbered bridge row has no studio input; no activity re-pick or guessed index.
	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr; // 0x10094b70
	FVector LocalDeltaCm = FVector::ZeroVector;                      // 0x100c5d10 nummovements=0
	float LocalYawDegrees = 0.f;                                    // 0x100c5d10 dAng.y
	const bool bMovement = Embodiment != nullptr && RawIndex >= 0
		&& Embodiment->GetBodySequenceIntervalMovement(Visual, ModelStem(), RawIndex,
			SequenceCycle, CycleTo, LocalDeltaCm, LocalYawDegrees);    // 0x100c5d10 -> 0x100c6020
	OutDeltaUnits += FRotator(0.f, -Angles.Y, 0.f).RotateVector(LocalDeltaCm) / ElysiumMove::U; // 0x1013a7e0
	OutYawDelta -= LocalYawDegrees;                                 // 0x10094b70 Source yaw
	return bMovement;                                              // 0x10094b70
}

bool FElysiumNpcBase::KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits,
	const FVector& HullMins, const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const
{
	// `UTIL_TraceHull 0x1026e940(start, end, mins, maxs, mask, filter, &tr, 1)` and the engine
	// `TraceRay` behind it, answered by `IElysiumEmbodiment::TraceRetail` (0018 story 6). The seam
	// answers the WORLD (brushes, movers, props) on a channel no character answers and LISTS the
	// characters the same box met; which of them retail's filter keeps is decided here.
	//
	// A clear trace ends where it was aimed; `endpos` is the one word of the clear answer a caller
	// can read back (`CNPC_VWerewolf::CheckStuck`'s `SetAbsOrigin(tr.endpos)`).
	OutTrace.EndPosUnits = EndUnits;
	++MotorSeams.HullTraces;
	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return false;
	}
	// Source units in the port's axes -> centimetres. The box is retail's (Source axes, Y up the
	// other way), so its Y pair is mirrored: mins.y' = -maxs.y, maxs.y' = -mins.y. A hull row is
	// symmetric in Y, so only an asymmetric box sees the difference.
	const double U = ElysiumMove::U;
	FElysiumRetailTrace Trace;
	Trace.StartCm = StartUnits * U;
	Trace.EndCm = EndUnits * U;
	Trace.MinsCm = FVector(HullMins.X, -HullMaxs.Y, HullMins.Z) * U;
	Trace.MaxsCm = FVector(HullMaxs.X, -HullMins.Y, HullMaxs.Z) * U;
	Trace.RetailMask = Mask;
	// The filter's pass entity (`PassServerEntityFilter 0x101d2fc0`): the tester itself.
	Trace.Ignore.Add(Handle);
	FElysiumRetailTraceResult Result;
	if (!Embodiment->TraceRetail(Trace, Result))
	{
		return false;
	}
	OutTrace.Fraction = Result.Fraction;
	OutTrace.HitEntity = Result.HitEntity;
	OutTrace.PlaneNormal = Result.Normal;
	OutTrace.bAllSolid = Result.bAllSolid;
	OutTrace.bStartSolid = Result.bStartSolid;
	OutTrace.EndPosUnits = Result.EndPosCm / U;

	// The characters, nearest first: the first one the filter KEEPS is the only one that can stop
	// the trace. It is folded in when it is nearer than the world hit, or when it starts solid and
	// the world answer does not (a start-solid character is a hit at fraction 0: `CheckStandPosition`
	// hands it to `CanStandOn`, `IsAreaClear` / `IsValidCover` refuse on it). The world's own
	// `allsolid` stands: the seam reports no per-character `allsolid`.
	for (const FElysiumRetailTraceCharacter& Character : Result.Characters)
	{
		if (!KernelTraceKeepsCharacter(Character.Entity, Mask))
		{
			continue;
		}
		const bool bNearer = Character.Fraction < OutTrace.Fraction;
		const bool bSolidFirst = Character.bStartSolid && !Result.bStartSolid;
		if (bNearer || bSolidFirst)
		{
			const float Fraction = Character.bStartSolid ? 0.f : Character.Fraction;
			OutTrace.Fraction = Fraction;
			OutTrace.HitEntity = Character.Entity;
			OutTrace.PlaneNormal = FVector::ZeroVector;   // no per-character normal at the seam
			OutTrace.bStartSolid = OutTrace.bStartSolid || Character.bStartSolid;
			OutTrace.EndPosUnits = StartUnits + (EndUnits - StartUnits) * Fraction;
		}
		break;
	}
	return true;
}

bool FElysiumNpcBase::KernelTraceKeepsCharacter(const FElysiumEntityHandle& Character, int32 Mask) const
{
	// `StandardFilterRules 0x101d3080` (`101d30f2`): a non-brush entity is rejected unless the mask
	// carries MONSTER. `0x202400b` does; `0x2400b` / `0x2000b` do not, and the seam lists no
	// character then either.
	if ((Mask & GTraceMaskMonster) == 0 || World == nullptr)
	{
		return false;
	}
	FElysiumEntity* const Other = World->Resolve(Character);
	if (Other == nullptr || Other == static_cast<const FElysiumEntity*>(this))
	{
		return false;
	}
	// `CTraceFilterSimple::ShouldHitEntity 0x101d31c0` (`101d3284`): a combat character (`+0x9c`)
	// with `m_bIsBCCTargetable (+0x1480) == 0` or `m_bScriptHidden (+0xf4)` is skipped. The
	// script-hidden byte is `FElysiumEntity::IsHidden` (ScriptHide / StartHidden).
	if (Other->IsHidden() || !IsBccTargetable(*Other))
	{
		return false;
	}
	// Arm (a), the nav-ignore set `CNavPropertyDatabase` (`0x102eb170`, the RB-tree at `0x10934014`):
	// its insert `0x102eaaf0` and remove `0x102eacf0` have no reference anywhere in the image
	// (`navigation-jump-links.md` § "What the probe ignores"), so the set is always empty and the
	// arm never drops anything. Nothing to port.
	//
	// Arm (c), the candidate's own slot 91 `ShouldCollide(group, mask)` (`0x100b4de0`, the chain
	// body `FElysiumEntity::ShouldCollide`); the filter's group is the tester's `m_CollisionGroup`
	// (`+0x368`), which that body ignores. Arm (d), `g_pGameRules->ShouldCollide(group, group)`:
	// no game-rules object stands in the port (`RetailGameRulesShouldCollide` answers "collide").
	if (!Other->ShouldCollide(CollisionGroup, Mask)
		|| !RetailGameRulesShouldCollide(CollisionGroup, Other->CollisionGroup))
	{
		return false;
	}
	// Then, unless the mask is `0x46004003`, slot 68 `ShouldIgnoreCollision` in both directions: the
	// candidate's own body asked about this NPC, and this NPC's asked about the candidate. The nav
	// filters (`0x102e32d0` arm (e)) run the tester's direction first; the answer is the same OR.
	// This NPC's slot 68 is the one `m_bForceNPCCheck` (`+0x63da`) reaches: with the bracket up
	// (`CanStandAt 0x102a0ed0`, `IsAreaClear 0x102a0fb0`) its NPC/player/sleeping arm is skipped
	// and other NPCs are solid. Neither slot-68 body writes anything.
	//
	// Slot 68 is dispatched only on a Troika-line body (`FElysiumNpc` and its species), where it is
	// ported. The player's (`CBaseCombatCharacter 0x10340650`) and a base-line NPC's are still
	// generated stubs answering false; that answer is taken without firing the stub tally on every
	// trace.
	if (Mask != GTraceMaskNoIgnoreCollision)
	{
		FElysiumNpc* const SelfTroika = const_cast<FElysiumNpcBase*>(this)->AsNpc();
		if (SelfTroika != nullptr && SelfTroika->ShouldIgnoreCollision(Other))
		{
			return false;
		}
		FElysiumNpc* const OtherTroika = Other->AsNpc();
		if (OtherTroika != nullptr && OtherTroika->ShouldIgnoreCollision(const_cast<FElysiumNpcBase*>(this)))
		{
			return false;
		}
	}
	return true;
}

bool FElysiumNpcBase::RetailGameRulesShouldCollide(int32 GroupA, int32 GroupB)
{
	// **SEAM**: `CGameRules::ShouldCollide` (VtMB's body unrecovered). No game-rules object stands in
	// the port; answers "collide", which drops nothing.
	(void)GroupA;
	(void)GroupB;
	return true;
}

bool FElysiumNpcBase::RetailHullExtents(int32 Hull, EElysiumHullExtents Which, FVector& OutMinsUnits,
	FVector& OutMaxsUnits) const
{
	// The shared hull table, replayed from the image's own static initialisers. A hull id outside
	// the table keeps the zero box and the false return every caller's failure arm was written
	// against, so an unrecovered id still refuses rather than boxing a point.
	const ElysiumRetailHulls::FRow* Row = ElysiumRetailHulls::Find(Hull);
	if (Row == nullptr)
	{
		OutMinsUnits = FVector::ZeroVector;
		OutMaxsUnits = FVector::ZeroVector;
		return false;
	}
	const bool bSmall = Which == EElysiumHullExtents::Small;
	OutMinsUnits = bSmall ? Row->SmallMins : Row->Mins;
	OutMaxsUnits = bSmall ? Row->SmallMaxs : Row->Maxs;
	return true;
}

bool FElysiumNpcBase::RetailCollisionExtents(const FElysiumEntity& Entity, FVector& OutMinsUnits,
	FVector& OutMaxsUnits)
{
	// `m_Collision` (+0x270) slots 1 / 2 — `OBBMins()` / `OBBMaxs()` (`0x100dc810` / `0x100dc830`).
	// An NPC's box is whatever `UTIL_SetSize` last wrote, and only the two hull bodies write it:
	// `SetHullSizeNormal 0x10273070` the FULL row of `m_eHull` (`+0x1568`), `SetHullSizeSmall
	// 0x10273180` the SMALL row, with `m_fIsUsingSmallHull` (`+0x5f2d`) saying which one stands.
	// `+0x1568` is the box word; `+0x156c` (`PathingHullKind`) only picks the nav agent
	// (`navigation-jump-links.md` § "The two hull words").
	const FElysiumNpcBase* const Npc = Entity.AsNpcBase();
	if (Npc != nullptr)
	{
  if (Npc->SetSizeCalls > 0)
  { OutMinsUnits = Npc->LastSetSizeMinsUnits; OutMaxsUnits = Npc->LastSetSizeMaxsUnits; return true; } // 0x101cf390 latest UTIL_SetSize
		return Npc->RetailHullExtents(Npc->HullKind,
			Npc->bIsUsingSmallHull ? EElysiumHullExtents::Small : EElysiumHullExtents::Full,
			OutMinsUnits, OutMaxsUnits);
	}
	// The player: `CBasePlayer::Spawn` sizes the box from the mover's hulls, the `CGameMovement`
	// constructor's literals (`0x1011e0d0`; `GetPlayerMins 0x1011e310` / `GetPlayerMaxs 0x1011e350`,
	// `docs/vtmb/source_movement.md` § "The hulls and the view offsets"): standing
	// `(-16,-16,0)..(16,16,72)`, ducked `(-16,-16,0)..(16,16,36)`, selected by `m_bDucked`
	// (`player+0x1edd`), which this runtime answers through `IElysiumEmbodiment::IsPlayerDucking`.
	const FElysiumPlayer* const Player = Entity.World != nullptr ? Entity.World->FindPlayer() : nullptr;
	if (Player != nullptr && static_cast<const FElysiumEntity*>(Player) == &Entity)
	{
		const IElysiumEmbodiment* const Embodiment = Entity.World->Embodiment();
		const bool bDucked = Embodiment != nullptr && Embodiment->IsPlayerDucking();
		const double HalfWidthUnits = ElysiumMove::HullHalfWidth / ElysiumMove::U;
		const double HeightUnits = (bDucked ? ElysiumMove::DuckHeight : ElysiumMove::StandHeight) / ElysiumMove::U;
		OutMinsUnits = FVector(-HalfWidthUnits, -HalfWidthUnits, 0.0);
		OutMaxsUnits = FVector(HalfWidthUnits, HalfWidthUnits, HeightUnits);
		return true;
	}
	// **SEAM** for every other entity: a prop's or a brush entity's `+0x274` / `+0x280` have no
	// source in this runtime. The bodies below refuse rather than box a point.
	OutMinsUnits = FVector::ZeroVector;
	OutMaxsUnits = FVector::ZeroVector;
	return false;
}

uint32 FElysiumNpcBase::ActiveWeaponCapabilityWord() const
{
	// The active weapon's vtable +0x5a0 (slot 360, retail body `0x1014f930`). **SEAM**: no such
	// word on `FElysiumWeapon`; answering 0 closes `ShouldMoveAndShoot`'s Troika gate.
	return 0;
}

float FElysiumNpcBase::MotorMinStoppingDistanceUnits() const
{
	// `CAI_Motor#16`, which lives on `IElysiumNpcMotor` itself
	// (`MinStoppingDistanceUnits`). With no motor at all the interface's own floor is the answer.
	return Motor != nullptr ? Motor->MinStoppingDistanceUnits() : 10.0f;
}

bool FElysiumNpcBase::IsIgnoreCollisionEntityTail(const FElysiumEntity* Other) const
{
	// `CBaseAnimating::IsIgnoreCollisionEntity` `0x1008be20`: resolve `m_hIgnoreCollisionEntity`
	// (+0x055c) and compare it against the candidate. Nothing writes the handle in this runtime yet,
	// so the tail answers "not that entity" for everything.
	if (Other == nullptr || !IgnoreCollisionEntity.IsSet() || World == nullptr)
	{
		return false;
	}
	return World->Resolve(IgnoreCollisionEntity) == Other;
}

float FElysiumNpcBase::StepHeight() const
{
	// slot 522. `CAI_BaseNPC::StepHeight` `0x101a6b40` returns `_DAT_10453b94` = 18.0 and IS the
	// body slot 522 carries on the Troika line. `CAI_TestHull::StepHeight` (40.0) and
	// three species override it on their own classes.
	return NpcKernelMotorShared::GStepHeightBase;
}

float FElysiumNpcBase::GetStepDownHeight() const
{
	// slot 523. `CAI_BaseNPC::GetStepDownHeight` `0x101a6b60` returns `_DAT_10453b94` = 18.0 — the
	// SAME cell slot 522's `0x101a6b40` reads. The Troika (`0x101aa670`), `CAI_TestHull`,
	// `CNPC_VMingXiao` (`0x10391050`), `CNPC_VMingXiaoTentacle` (`0x1039b070`) and
	// `CNPC_VTzimisce` (`0x103b6df0`) override it on their own classes. Until story 5 fold A1 this
	// was a generated stub answering 0.
	//
	// **The SDK name is wrong** (R1 §5): slot 523 is the STEP-DOWN height -- the drop
	// `CheckStandPosition 0x102e7270` traces below the feet (`102e72b9`, `JMP [EAX+0x82c]`) and the
	// step record's down-step in `TestGroundMove 0x102e4f50`. The name is the generated slot table's
	// (`docs/vtmb/npc-kernel/signatures.tsv` row 523), which this body does not own.
	return ElysiumNpcTunables::StepHeightBase;
}

bool FElysiumNpcBase::IsJumpLegal(FVector& StartUnits, FVector& ApexUnits, FVector& EndUnits) const
{
	// slot 521. `CAI_BaseNPC::IsJumpLegal` `0x10280880` forwards to the geometry helper with
	// 80.0 / 250.0 / 160.0; `CAI_TestHull::IsJumpLegal` is its own class's override
	// with 1024 / 1024 / 1024 (`FElysiumNpcTestHull`).
	return IsJumpLegalGeometry(StartUnits, ApexUnits, EndUnits, NpcKernelMotorShared::GJumpLegalRise,
		NpcKernelMotorShared::GJumpLegalDrop, NpcKernelMotorShared::GJumpLegalDistance);
}

float FElysiumNpcBase::MaxYawSpeed()
{
	// slot 516, `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` -- one constant, `_DAT_1049949c` = 45.0, the
	// number every ladder of the family falls through to (0019/6: restored as data; the rate is retail's,
	// the turn is the mover's).
	return NpcKernelMotorShared::GYawDefault;
}

bool FElysiumNpcBase::IsMoving()
{
	// slot 153. `CAI_BaseNPC::FUN_10280300` `0x10280300` is a one-line forward to
	// `thunk_FUN_102ee680(m_pNavigator)`, which is `IsGoalSet` (goal type != 0), not `IsGoalActive`.
	// The goal type stands after `OnNavFailed` until the schedule clears it, so a failed walker still
	// answers "moving" here, as retail's does.
	return NavigatorIsGoalSet();
}

bool FElysiumNpcBase::OverrideMove(float Interval)
{
	// slot 525. The census holds three species bodies and this leaf dispatches none of them:
	//   * `CNPC_VManBat` `0x1038b120` (`rule`) — the flight step at navigator state 2. UNPORTED.
	//   * `CNPC_VVampireBoss` `0x103c5fe0` and its seven heirs (`present`) — `m_bJumping != 0`
	//     (`+0x6498`, `bJumping`). The word is carried; this arm does NOT read it yet, so a jumping
	//     boss's move is not suppressed here. A named divergence, not a step-5 change.
	//   * `CNPC_Crow` `0x10357ba0` — on a class no map stands; no port arm (0019 story 5 step 1).
	(void)Interval;
	// `CAI_BaseNPC::OverrideMove` `0x1027da90` — a scope-trace push/pop around an unconditional
	// false. The base DECLINES, and that is what every species override is measured against.
	return false;
}

bool FElysiumNpcBase::ValidateNavGoal()
{
	// slot 528. `CAI_BaseNPC::FUN_10280360` `0x10280360` (R2 §6, from the listing) — retail's
	// is-this-still-cover check on the goal the navigator is holding:
	//
	//     if (GetNavigator()->GetGoalType() != 6) return true;           // 0x102ee620
	//     if (!GetEnemy()) return true;                                   // slot 167
	//     Vector p = GetNavigator()->GetGoalPos();
	//     p.z = 0x102f99d0(p, 384.0);                                     // the floor probe
	//     p += EyeOffset(GetCoverActivity(m_pHintNode));                  // slot 533 of slot 569
	//     UTIL_TraceLine(p, GetEnemy()->EyePosition() /* slot 193 */, 0x2804091,
	//                    CTraceFilterSimple(this, 0), &tr);               // mask at 102804f1
	//     if (tr.fraction == 1.0f) {                   // NOTHING blocks -> this is not cover
	//         if (!ConditionInterruptsCurrentSchedule(0x39)) {
	//             m_failText/-Line = "…AI_BaseNPC…", 0xbc;
	//             TaskFail(0x1b);                      // slot 448
	//             return false;
	//         }
	//         SetCondition(0x39);
	//     }
	//     return true;                                 // blocked -> the cover hides
	//
	// `NavGoalState()` is the navigator's goal type (`path+0x5c`), so the gate opens for a cover
	// goal (`SetGoal` type 6) and answers true for every other type, as retail does.
	if (NavGoalState() != 6)
	{
		return true;
	}
	FElysiumEntity* Enemy = World != nullptr && BaseMemory.Enemy.IsSet()
		? World->Resolve(BaseMemory.Enemy)
		: nullptr;
	if (Enemy == nullptr)
	{
		return true;
	}
	FVector GoalUnits = FVector::ZeroVector;
	if (!NavGoalPosition(GoalUnits))
	{
		return true;
	}
	// `NavGoalPosition` answers Source units in the port's axes, which is `KernelHullTrace`'s frame.
	//
	// The floor probe `0x102f99d0(goal, 384)` (R2 §6): two LINES from the goal straight down 384
	// units, filter `(NULL, 0)`, masks `0x2400b` then `0x202400b` (`102f9b58`); the second's z wins
	// only when it hit EARLIER than the first on an entity carrying flag `0x1000000`. That flag's name
	// and its word are unrecovered and no port entity carries it, so the second trace can never win:
	// it is still run, and its entity test answers "no flag" (a SEAM, named here). The z taken is the
	// first trace's `endpos` -- R2 calls it the floor z; a probe that finds no floor answers its own
	// end, 384 under the goal, which is what a clear trace's `endpos` is. Filter divergence: the
	// kernel trace always passes this NPC as the pass entity, where retail's floor probe has none.
	constexpr double GFloorProbeUnits = 384.0;
	constexpr int32 GFloorProbeMaskWorld = 0x2400b;
	constexpr int32 GFloorProbeMaskNpcSolid = 0x202400b;
	const FVector FloorEndUnits(GoalUnits.X, GoalUnits.Y, GoalUnits.Z - GFloorProbeUnits);
	FKernelHullTrace FloorWorld;
	KernelHullTrace(GoalUnits, FloorEndUnits, FVector::ZeroVector, FVector::ZeroVector,
		GFloorProbeMaskWorld, FloorWorld);
	FKernelHullTrace FloorSolid;
	KernelHullTrace(GoalUnits, FloorEndUnits, FVector::ZeroVector, FVector::ZeroVector,
		GFloorProbeMaskNpcSolid, FloorSolid);
	// SEAM: flag `0x1000000`'s word is unrecovered, so no hit entity carries it.
	auto CarriesFlag0x1000000 = [](const FElysiumEntityHandle& /*HitEntity*/) { return false; };
	const bool bSecondWins = FloorSolid.Fraction < FloorWorld.Fraction
		&& CarriesFlag0x1000000(FloorSolid.HitEntity);
	const double FloorZUnits = bSecondWins ? FloorSolid.EndPosUnits.Z : FloorWorld.EndPosUnits.Z;

	// Slot 533 `EyeOffset` of slot 569 `GetCoverActivity(m_pHintNode)`: both are dispatched (the
	// Troika line's bodies are `0x102b4ab0` / `0x10297560`); the hint argument is the non-null marker
	// both bodies read the held hint through. `EyeOffset` answers centimetres.
	void* const HeldHint = BaseScheduleHost.HintNode != INDEX_NONE
		? static_cast<void*>(&BaseScheduleHost.HintNode) : nullptr;
	const int32 CoverActivity = GetCoverActivity(HeldHint);
	const FVector EyeOffsetUnits = EyeOffset(CoverActivity, CoverActivity) / ElysiumMove::U;
	const FVector StartUnits(GoalUnits.X + EyeOffsetUnits.X, GoalUnits.Y + EyeOffsetUnits.Y,
		FloorZUnits + EyeOffsetUnits.Z);
	// The enemy's slot 193 eye. `CTraceFilterSimple(this, 0)` passes only this NPC: the ENEMY is not
	// ignored and NPCs are not transparent to it, so under `0x2804091`'s MONSTER half a line that ends
	// inside the enemy's own box meets that box first and reads BLOCKED -- the cover is kept. That is
	// retail's filter, reproduced; the character rule is the kernel trace's.
	const FVector EnemyEyeUnits = Enemy->EyePosition() / ElysiumMove::U;
	FKernelHullTrace Trace;
	if (!KernelHullTrace(StartUnits, EnemyEyeUnits, FVector::ZeroVector, FVector::ZeroVector,
		GCoverTraceMask, Trace))
	{
		// No collision world: nothing can be asked, and the goal stands (the answer before 0018/6).
		return true;
	}
	if (Trace.Fraction == GTraceClearFraction)
	{
		if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, GCondNavGoalInvalid))
		{
			TaskFail(GFailNoCover);
			return false;
		}
		Cognition.Conditions.Set(GCondNavGoalInvalid);
	}
	return true;
}

bool FElysiumNpcBase::AutoMovement()
{
	StudioFrameAdvance(0.f);                                       // 0x10280a50 slot 250 first
	FVector EndUnits = Origin / ElysiumMove::U;                       // 0x10094b70
	float EndYaw = Angles.Y;                                        // 0x10094b70
	AnimIntervalMovement(AnimTime - PrevAnimTime, EndUnits, EndYaw); // 0x10280a50 just-advanced cycle
	if (Visual == nullptr || GetMoveType() != 4 || (Flags & 0x400) != 0) return false;      // 0x10280a50
	FMotorMoveTrace MoveTrace;                                     // 0x102e0bd0
	// 0x102e0c48: flags=5 (ground seam's existing NavMesh contract); pct=100, mask=0x202400b.
	MotorMoveTraceSweep(0, Origin / ElysiumMove::U, EndUnits,
		0x202400b, 100.f, nullptr, MoveTrace);                       // 0x102e0c57 MoveLimit
	// 0x102e0c57: the landed ground seam supplies the floor; the hull supplies solid obstructions.
	FVector AutoHullMins, AutoHullMaxs; // 0x102e0bd0
	RetailCollisionExtents(*this, AutoHullMins, AutoHullMaxs); // 0x102e0bd0
	FKernelHullTrace AutoHullTrace; // 0x102e0bd0
	if (KernelHullTrace(Origin / ElysiumMove::U, EndUnits, AutoHullMins, AutoHullMaxs,
		0x202400b, AutoHullTrace) && (AutoHullTrace.Fraction < 1.f
			|| AutoHullTrace.bStartSolid || AutoHullTrace.bAllSolid)) // 0x102e0bd0 MoveLimit status
	{
		MoveTrace.EndPositionUnits = AutoHullTrace.EndPosUnits; // 0x102e0bd0
		MoveTrace.ObstructionHandle = AutoHullTrace.HitEntity; // 0x102e0bd0
		MoveTrace.Obstruction = World != nullptr ? World->Resolve(AutoHullTrace.HitEntity) : nullptr; // 0x102e0bd0
		MoveTrace.Status = MoveTrace.Obstruction == nullptr ? -2
			: (MoveTrace.Obstruction->AsNpcBase() != nullptr ? -3 : -1); // 0x102e2d70
	}
	FElysiumEntity* const ExpectedTarget = GetNavTargetEntity();     // 0x102729d0 goals 2/1/7 only
	const bool bStoppedAtTarget = ExpectedTarget != nullptr
		&& (MoveTrace.Obstruction == ExpectedTarget
			|| MoveTrace.ObstructionHandle == ExpectedTarget->Handle); // 0x102e0cb5..0x102e0cc9
	if (MoveTrace.Status < 0 && !bStoppedAtTarget) return false;     // 0x102e0cd8..0x102e0cee
	SetOrigin(MoveTrace.EndPositionUnits * ElysiumMove::U);           // 0x102e0cfc; carries motor capsule
	if (EndYaw != -1.f) SetRuntimeAngles(FVector(Angles.X, EndYaw, Angles.Z)); // 0x102e0d7e..0x102e0dc2
	return !bStoppedAtTarget && MoveTrace.Status >= 0;               // 0x102e0dc8: only result 1
}

float FElysiumNpcBase::PostRun()
{
	// `CAI_BaseNPC::PostRun` `0x1026c7c0`. Everything but three lines is VProf scaffolding:
	//     float dt = RunAnimation();                  // `0x1026c8c4 CALL 0x1000ed63` -> 0x1026c540
	//     vtable[0x408/4 = 258](dt, this);            // DispatchAnimEvents, `0x1026c8d8`
	//     CBaseCombatCharacter::Weapon_FrameUpdate(dt);   // `0x1026c8e0`, with the SAME number
	// and the interval is the answer the think hands to `PerformMovement`.
	const float Interval = RunAnimation();                                      // 0x1026c8c4
	DispatchAnimEvents(Interval, this);                                          // 0x1026c8d8 slot 258
	MotorSeams.PostRunInterval = Interval;
	++MotorSeams.PostRunWeaponUpdates;                                           // 0x1026c8e0 Weapon_FrameUpdate
	FElysiumEntity* const HeldEntity = ActiveWeaponEntity(); // 0x1032aa40
	if (FElysiumWeapon* const ActiveWeapon = HeldEntity && HeldEntity->AsItem() ? HeldEntity->AsItem()->AsWeapon() : nullptr)
	{
		ActiveWeapon->WeaponFrameUpdate(*this);                                  // 0x1032aa40 -> 0x1024efa0
	}
	return Interval;
}

void FElysiumNpcBase::CheckOnGround()
{
	// `CAI_BaseNPC::CheckOnGround` `0x1026e5e0`, arm for arm.
	//
	//     if (HasCondition(0x73)) {
	//         if (!(GetFlags() & 1) && GetNavType() == 0) return;   // still airborne on the ground
	//         ClearCondition(0x73);                                  // 0x10269b50
	//         return;
	//     }
	//     if (GetNavType() != 0) return;                             // FUN_1027d990
	//     if (GetMoveType() == 7) return;
	//     if (curtime - m_flCheckOnGroundTime <= -0.001) return;     // _DAT_10497530
	//     m_flCheckOnGroundTime = curtime + 0.5;                     // _DAT_104454d0
	//     start = GetAbsOrigin() + (0,0,0.1);  end = GetAbsOrigin() - (0,0,4.0);
	//     TraceHull(start, end, OBBMins, OBBMaxs, 0x202400b, filter, &tr);
	//     if (tr.fraction == 1.0) { SetCondition(0x73); SetGroundEntity(NULL); return; }
	//     if (tr.m_pEnt && tr.m_pEnt != GetGroundEntity()) SetGroundEntity(tr.m_pEnt);
	//
	// `m_flCheckOnGroundTime` is the ONE bound word of this body (`FElysiumNpcBase::CheckOnGroundTime`);
	// the deadline is stamped before the trace, exactly as retail stamps it. The trace (R1 §6): the
	// move probe's `CTraceFilterNavGround`, the FULL collision box (no foot box), cylinder flag 1, no
	// `CanStandOn` and no `m_bForceNPCCheck` bracket. A world with no collision answers nothing and
	// neither ground write runs.
	if (Cognition.Conditions.Has(GCondOnGround))
	{
		if ((Flags & 1) == 0 && NavGetType() == 0)
		{
			return;
		}
		Cognition.Conditions.Clear(GCondOnGround);
		return;
	}
	if (NavGetType() != 0)
	{
		return;
	}
	if (GetMoveType() == 7)
	{
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now - CheckOnGroundTime <= GCheckOnGroundSlack)
	{
		return;
	}
	CheckOnGroundTime = Now + GCheckOnGroundInterval;

	// The measurement is the floor facts seam (`IElysiumNpcMotor::SampleFloor`, the character
	// movement component's floor sweep under the capsule); the rule above and the two writes below
	// stay retail's. Retail's trace runs from `origin + 0.1` to `origin - 4.0` (`_DAT_10449148`), so
	// "fraction 1.0" is no floor within 4.0 units of the feet: the probe reaches about the step
	// height, which is further, so the reach is tested here against 4.0. A motor with no movement
	// component answers nothing and neither write runs, as a world with no collision did.
	FElysiumNpcFloorFacts Floor;
	if (Motor == nullptr || !Motor->SampleFloor(Floor))
	{
		return;
	}
	if (!Floor.bOnGround || Floor.FloorDistanceCm > GCheckOnGroundDown * ElysiumMove::U)
	{
		Cognition.Conditions.Set(GCondOnGround);
		SetGroundEntity(nullptr);
		return;
	}
	// `tr.m_pEnt`: an unset handle is the static world, which retail's `GetGroundEntity` compare
	// sees as worldspawn; the port writes only a named entity, as before.
	if (Floor.GroundEntityHandle.IsSet() && World != nullptr)
	{
		FElysiumEntity* Hit = World->Resolve(Floor.GroundEntityHandle);
		if (Hit != nullptr && Hit != GetGroundEntity())
		{
			SetGroundEntity(Hit);
		}
	}
}

bool FElysiumNpcBase::OnObstructingDoorBase(float& InOutMoveGoalMaxDistance, int32 DoorState,
	float DistClear, EObstructingDoorResult& OutResult) const
{
	// `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80`, the BASE branch of slot 531
	// `OnObstructingDoor(AILocalMoveGoal_t*, CBaseDoor*, float distClear, AIMoveResult_t*)`:
	//     if (moveGoal->+0x28 < distClear) return false;      // the door is further than the goal
	//     int state = door->+0x4f8;
	//     if (state != 1 && state != 3) return false;         // only opening / closing obstruct
	//     if (distClear < 0.1) { *result = -1; return true; } // _DAT_104493d0
	//     moveGoal->+0x28 = distClear;
	//     *result = 0;
	//     return true;
	if (InOutMoveGoalMaxDistance < DistClear)
	{
		return false;
	}
	if (DoorState != 1 && DoorState != 3)
	{
		return false;
	}
	if (DistClear < NpcKernelMotorShared::GJumpLegalSlack)
	{
		OutResult = EObstructingDoorResult::Illegal;
		return true;
	}
	InOutMoveGoalMaxDistance = DistClear;
	OutResult = EObstructingDoorResult::Ok;
	return true;
}

int32 FElysiumNpcBase::ResolveLinkActivity() const
{
	// `FUN_1027a6c0` `0x1027a6c0`:
	//     if (thunk_FUN_102ee6a0(m_pNavigator)) {
	//         int a = thunk_FUN_102ee510(m_pNavigator);
	//         if (a != -1) return a;
	//     }
	//     return 1;                                  // ACT_IDLE
	int32 Activity = INDEX_NONE;
	if (NavLinkActivity(Activity) && Activity != INDEX_NONE)
	{
		return Activity;
	}
	return NpcKernelMotorShared::GActIdle;
}

void FElysiumNpcBase::NavOnNavFailed(int32 FailReason)
{
	// `CAI_Navigator::OnNavFailed` `0x102eeae0` (`CAI_Navigator#10`, and `CAI_Navigator#9`
	// `0x102eeb50` is a tail-jump into it):
	//     thunk_FUN_102eeb70(this);                             // the navigator's own reset
	//     owner->+0x1b44 = "E:\Vampire\main\dlls\ai_navigato…";  owner->+0x1b48 = 0x406;
	//     owner->vtable[0x700/4 = 448](reason);                 // TaskFail
	//     SetIdealActivity(owner, FUN_1027a6c0(owner));         // 0x10272650
	//     this->+0x1c = 1;
	//
	// The file/line pair is `ELYSIUM_NPC_WORD_ABSENT(0x1b44)` — the named failure reason and the
	// schedule trace rows carry that account here. `SetIdealActivity` is the Facing family's
	// `SetIdealActivityNumber`, reused rather than duplicated. The path is NOT cleared: the head
	// waypoint and the goal type stand for the schedule's own reaction to the failure.
	if (IsAiTraced())
	{
		EmitAiTrace(TEXT("move"), FString::Printf(TEXT("fail %d"), FailReason));   // the AI trace (debug only)
	}
	NavResetBlockerMemory();                                                 // 0x102eeb70
	TaskFail(FailReason);                                                    // slot 448 (+0x700)
	SetIdealActivityNumber(ResolveLinkActivity());                           // 0x100097d2(npc, 0x1000b285(npc))
	Navigator.bNavFailed = true;                                             // +0x1c = 1
	Navigator.LastOutcome = FElysiumNpcNavOutcome();
	Navigator.LastOutcome.Kind = FailReason == GMoveStepFailDoor
		? EElysiumNpcNavOutcomeKind::Door : EElysiumNpcNavOutcomeKind::Failed;
	Navigator.LastOutcome.FailCode = FailReason;
}

void FElysiumNpcBase::NavResetBlockerMemory()
{
	// `0x102eeb70`: `nav+0x54 = -1`, `nav+0x58 = nav+0x60 = -1.0f`, then `0x1000b550` (the local
	// navigator's reset; the port's steering is the crowd's, so the call is recorded). `nav+0x51`
	// is not this reset's word.
	Navigator.BlockerEntity = FElysiumEntityHandle::Invalid();
	Navigator.BlockerHoldUntil = -1.0;
	Navigator.BlockerForgetAt = -1.0;
	Navigator.bBlockerHoldStanding = false;                                  // port: the hold's probe words
	Navigator.bBlockerHoldReissued = false;
	Navigator.BlockerProbeAt = -1.0;
	Navigator.BlockerProbeRemainingUnits = 0.f;
	++NavMoveStep.LocalNavResets;                                            // 0x1000b550, SEAM
}

void FElysiumNpcBase::NavOnNavComplete()
{
	// `CAI_Navigator::OnNavComplete` (slot 8): the reset `0x102eeb70`, the owner's
	// `TaskMovementComplete` through `0x102eccc0`, `nav+0x1c = 1`.
	if (IsAiTraced())
	{
		EmitAiTrace(TEXT("move"), TEXT("arrived"));                          // the AI trace (debug only)
	}
	NavResetBlockerMemory();                                                 // 0x102eeb70
	// `TaskMovementComplete` advances the goal waypoint (`0x102f0400`, the last corner's `InPass`) and
	// ends with `ClearGoal` (`0x10273f46` -> `0x102ee270`), so the goal type (`path+0x5c`) and the head
	// waypoint never outlive an arrival.
	TaskMovementComplete();                                                  // 0x102eccc0 -> 0x10273ec0
	Navigator.bNavFailed = true;                                             // +0x1c = 1
	Navigator.LastOutcome = FElysiumNpcNavOutcome();
	Navigator.LastOutcome.Kind = EElysiumNpcNavOutcomeKind::Arrived;
}

FElysiumEntityHandle FElysiumNpcBase::NavMoveTarget() const
{
	// `0x102ecc40`, the move goal's `+0x34` (slot 17's word `[0xd]`, the body `FUN_102eee40` also
	// transcribes): goal type 2 / 1 / 7 -> `GetNavTargetEntity` (`0x102729d0`), else `path+0x30`.
	const int32 GoalType = Navigator.GetGoalType();
	const FElysiumEntity* TargetEntity = nullptr;
	if (GoalType == 2 || GoalType == 1 || GoalType == 7)
	{
		TargetEntity = GetNavTargetEntity();
	}
	else if (World != nullptr)
	{
		TargetEntity = static_cast<const FElysiumEntityWorld*>(World)->Resolve(Navigator.GetTarget());
	}
	return TargetEntity != nullptr ? TargetEntity->Handle : FElysiumEntityHandle::Invalid();
}

bool FElysiumNpcBase::NavIsNpcBlocker(const FElysiumEntityHandle& Blocker) const
{
	// `0x102e2d70`: `-3` iff `blocker[+0x94]` (the cached NPC pointer) is non-null; else `-1` for an
	// ordinary entity, `-2` for the world (R2 §5). The body names only NPC obstructions, so anything
	// else it reports reaches the pass unnamed.
	if (!Blocker.IsSet() || World == nullptr)
	{
		return false;
	}
	const FElysiumEntity* Entity = static_cast<const FElysiumEntityWorld*>(World)->Resolve(Blocker);
	return Entity != nullptr && Entity->AsNpcBase() != nullptr;
}

bool FElysiumNpcBase::NavBlockerHold(const FElysiumEntityHandle& Blocker, double CurTime)
{
	// `0x102ef3e0(nav, trace)` (thunk `0x1000856c`), instruction by instruction (R3):
	//     blocker = trace+0x1c -> +0x94;  none -> return nav+0x51;
	//     if (resolve(nav+0x54) != blocker || curtime - nav+0x60 > -0.001) goto arm;
	//     if (curtime - nav+0x58 <= -0.001) { nav+0x51 = 1; return true; }       // hold
	//     return nav+0x51;
	//   arm (0x102ef49a):
	//     nav+0x51 = 1; nav+0x54 = blocker; nav+0x58 = curtime + nav+0x5c; nav+0x60 = curtime + nav+0x64;
	//     return true;
	if (!NavIsNpcBlocker(Blocker))
	{
		return Navigator.bBlockerHold;
	}
	const FElysiumEntityWorld* ConstWorld = World;
	// `nav+0x54` is resolved through the entity list (`>> 0xd` serial, `& 0x1fff` index): a handle that
	// no longer names a live entity is "not the blocker".
	const FElysiumEntity* Remembered = ConstWorld->Resolve(Navigator.BlockerEntity);
	const FElysiumEntity* Current = ConstWorld->Resolve(Blocker);
	if (Remembered != Current || CurTime - Navigator.BlockerForgetAt > GMoveStepTimeSlack)
	{
		Navigator.bBlockerHold = true;                                       // nav+0x51
		Navigator.BlockerEntity = Blocker;                                   // nav+0x54
		Navigator.BlockerHoldUntil = CurTime + Navigator.BlockerHoldSeconds; // nav+0x58 = curtime + nav+0x5c
		Navigator.BlockerForgetAt = CurTime + Navigator.BlockerWindowSeconds; // nav+0x60 = curtime + nav+0x64
		++NavMoveStep.BlockerHoldArms;
		return true;
	}
	if (CurTime - Navigator.BlockerHoldUntil <= GMoveStepTimeSlack)
	{
		Navigator.bBlockerHold = true;
		return true;
	}
	return Navigator.bBlockerHold;
}

bool FElysiumNpcBase::NavNpcBlockerStep(const FNavStepFacts& Step)
{
	// Retail, per think (0.1 s in view): S3 `0x102ef350` / S7 on motor code 2 ask `0x102ef3e0` with a
	// FRESH probe of the step. The first contact arms a 0.25 s hold (stand at the clearance, no fail);
	// the first think past it re-probes; a probe still blocked by the same NPC inside the 3.0 s window
	// gets `false`, S4 `0x102ef0e0` finds `distClear < 1.0` and the `-3` reaches `Move` -> `0x0c`.
	//
	// NAMED MODERNIZATION, of the probe only (the words and edges are retail's): Unreal's path follower
	// gives the request up on its own block detection (~5 s) BEFORE the substrate sees the blocker, and
	// the ended request is re-read by every think until a leg is issued again -- at any think cadence
	// (the Normal law `0x10290b60` spaces an out-of-PVS NPC's moves up to 16 s apart). So:
	//  - the end the hold was armed on is ONE contact, not one per think: re-reading it asks only the
	//    hold test (`curtime - nav+0x58 <= -0.001`);
	//  - once the hold has run, retail's re-probe is the recorded leg handed back (`NavReissueHeadLeg`:
	//    no new route, no new pedestrian draw), dated at this think's `curtime`. A think arriving after
	//    the window has lapsed meets the probe with a fresh window, in retail's words for a contact
	//    then (`nav+0x60 = curtime + nav+0x64`); the hold it stood on the ended request is served;
	//  - that leg's `Blocked` end is the probe's verdict when the body closed less than S4's 1.0 unit on
	//    the destination: it is judged at the probe's `curtime` (retail's probe answers inside the think
	//    that sends it), so a re-probe still blocked by the same NPC is the exhausted hold -> `-3`,
	//    whatever the cadence. A leg that walked on and met the NPC again is a new contact at `curtime`.
	const FElysiumEntityWorld* ConstWorld = World;
	const double Now = ConstWorld->NowSeconds();
	const bool bSameBlocker = ConstWorld->Resolve(Navigator.BlockerEntity) == ConstWorld->Resolve(Step.Blocker);
	if (Navigator.bBlockerHoldStanding && bSameBlocker)
	{
		if (Now - Navigator.BlockerHoldUntil <= GMoveStepTimeSlack)          // 0x102ef3e0 the hold test
		{
			Navigator.bBlockerHold = true;                                   // nav+0x51 = 1
			return true;
		}
		Navigator.bBlockerHoldStanding = false;
		if (Now - Navigator.BlockerForgetAt > GMoveStepTimeSlack)            // curtime - nav+0x60 > -0.001
		{
			Navigator.BlockerForgetAt = Now + Navigator.BlockerWindowSeconds; // nav+0x60 = curtime + nav+0x64
		}
		if (NavReissueHeadLeg())
		{
			Navigator.bBlockerHoldReissued = true;
			Navigator.BlockerProbeAt = Now;
			Navigator.BlockerProbeRemainingUnits = Step.RemainingUnits;
			++NavMoveStep.BlockerHoldReissues;
			NavLogMoveStep(TEXT("NPC-blocker hold over, head leg re-issued"));
			return true;
		}
		// A body that refuses the leg cannot probe: the blocked status stands (S4).
		return NavFollowSameDirectionMover(Step.Blocker);
	}
	// A new end: the post-hold probe's verdict, or a fresh contact.
	const bool bProbeVerdict = Navigator.bBlockerHoldReissued && bSameBlocker
		&& Navigator.BlockerProbeRemainingUnits - Step.RemainingUnits < GMoveStepProbeClearUnits;
	Navigator.bBlockerHoldReissued = false;
	const int32 ArmsBefore = NavMoveStep.BlockerHoldArms;
	if (NavBlockerHold(Step.Blocker, bProbeVerdict ? Navigator.BlockerProbeAt : Now))
	{
		Navigator.bBlockerHoldStanding = true;
		if (NavMoveStep.BlockerHoldArms != ArmsBefore)
		{
			NavLogMoveStep(TEXT("NPC-blocker hold start"));
		}
		return true;
	}
	// S4 `0x102ef0e0`: follow a same-direction mover (0), else the NPC status stands.
	return NavFollowSameDirectionMover(Step.Blocker);
}

bool FElysiumNpcBase::NavFollowSameDirectionMover(const FElysiumEntityHandle& Blocker)
{
	// `0x102efde0`, reached from S4 `0x102ef0e0` when motor slot 16 exceeds the
	// clearance: a moving NPC going the same way is followed (result 0, `maxDist = distClear`, flag 2).
	// SEAM answering no: its constants and the S4 gate distance are unrecovered (R3), so the blocked
	// result stands, which is S4's own `distClear < 1.0` arm.
	(void)Blocker;
	++NavMoveStep.MoverFollowTests;
	return false;
}

bool FElysiumNpcBase::NavBlockedStepCompletes()
{
	// `0x102ef760` (sink slot 5, `OnMoveBlocked`, `this = nav+0x10`). The owner's movement sink
	// (`[npc+0x19b0]`, `CAI_DefMovementSink`) is asked first through its slot 5 (`+0x14`): a true
	// answer returns with the result as the sink left it. `CAI_DefMovementSink`'s slot 5 is
	// `0x101a63c0`, `XOR AL,AL; RET 4`, and no port class replaces that secondary table
	// (`CNPC_VZombie 0x103de330` is unread), so the sink answers false.
	constexpr bool bMovementSinkHandled = false;                             // 0x102ef777 CALL [EAX+0x14]
	if (bMovementSinkHandled)                                                // 0x102ef77a / 0x102ef77c JZ
	{
		return false;
	}
	// The stopped activity, unconditionally: `SetIdealActivity(0x1027a6c0())`.
	SetIdealActivity(ResolveLinkActivity());                                 // 0x102ef78a / 0x102ef793
	// `dist(GetOrigin() (slot 220), 0x1030ba30 raw goal)` -- 2-D on ground nav, 3-D otherwise --
	// against `0x102ee1a0` `path+0x28` + 0.1, strictly (`FCOMPP; TEST AH,5; JP`: equal or NaN fails).
	const float U = ElysiumMove::U;
	const FVector Delta = (Navigator.GoalPosCm - Origin) / U;
	const double DistUnits = Navigator.GetNavType() == 0 ? Delta.Size2D() : Delta.Size();
	const double ToleranceUnits = Navigator.GetGoalTolerance() / U + GMoveStepGoalSlackUnits;
	if (DistUnits < ToleranceUnits)
	{
		NavLogMoveStep(TEXT("arrived (blocked inside the goal tolerance, 0x102ef760)"));
		NavOnNavComplete();                                                  // nav slot 8, *result = 0
		return true;
	}
	return false;
}

void FElysiumNpcBase::NavMarkStaleLink(float Seconds)
{
	// `0x102f1fa0(nav, Seconds, NULL)` from `Move`'s failure tail (4.0 s, no blocker): routed to the
	// link the NPC's current path segment stands on (0018/7). Counted for the move-step suites.
	++NavMoveStep.StaleMarkCalls;
	NavMarkLinkStale(static_cast<double>(Seconds), nullptr);
}

void FElysiumNpcBase::NavMarkLinkStale(double Seconds, const FElysiumEntity* Blocker)
{
	// `0x102f1fa0(nav, Seconds, Blocker)`, gate by gate:
	//   `nav+0x50 m_fRememberStaleNodes` (the constructor's 1), a path with a head (`path+0x24`),
	//   `path+0x44` (the last node passed) and the head's node `wp+0x10` both set -- the link
	//   between them is the one marked (`thunk_FUN_102f9400(node, headNode)`).
	if (!bNavRememberStaleNodes || World == nullptr || !NavIsGoalActive())
	{
		return;
	}
	// The port's link: ONLY the door smart link the body is held at -- the doorway it stands in is
	// the segment between the last node passed and the head's node. Any other segment marks
	// nothing, named: the navmesh keeps no per-segment word (the NAMED MODERNIZATION of a NavMesh
	// route standing for AIN links), and a door link merely AHEAD is not the segment being walked.
	// A spliced door waypoint (the stand point) is no candidate either: its `wp+0x10` is -1, which
	// fails retail's own gate.
	if (!bNavLastFactsValid)
	{
		return;
	}
	const FElysiumEntityHandle LinkDoor = NavLastFacts.DoorLinkEntity;
	FElysiumEntity* const LinkEntity = World->Resolve(LinkDoor);
	FElysiumDoorBase* const Link = LinkEntity != nullptr ? LinkEntity->AsDoorBase() : nullptr;
	if (Link == nullptr)
	{
		return;
	}
	// `link+0x64 |= 1`, `link+0x68 = curtime + Seconds`, `link+0 = blocker handle or -1`.
	Link->MarkLinkStale(World->NowSeconds(), Seconds,
		Blocker != nullptr ? Blocker->Handle : FElysiumEntityHandle::Invalid());
}

bool FElysiumNpcBase::NavCheckStaleRoute(const FVector& StartCm, const FVector& EndCm)
{
	// `CAI_Pathfinder::CheckStaleRoute` `0x10304a40(start, end, motionBits)`: by motion bit in the
	// order 1 (ground), 4 (fly), 2 (jump), 8 (climb). A door smart link is a ground link, so the
	// ground arm is the one asked: `0x103048d0(this, navType 0, start, end)`:
	//   (a) `MoveLimit(0, start, end, 0x202400b, pct 100)`; `status >= 0` -> clear.
	//   (b) blocked -> `0x103059d0`, a local route around the obstruction (`dist - flDistObstructed`
	//       budget); found -> clear. SEAM answering "not found": the port has no local-route
	//       builder over the probe's geometry (counted, `NavStaleRouteLocalRouteAsks`).
	//   (c) the obstruction (`trace+0x1c`) an NPC (`+0x94`) -> `MoveLimit` again under `0x2400b`
	//       (the same mask without `CONTENTS_MONSTER`): `status >= 0` -> clear.
	//   Otherwise still stale.
	//
	// NAMED DIVERGENCE, the probe's geometry: `MoveLimit`'s ground arm in the port is the NavMesh
	// raycast (`MotorMoveTraceSweep`), and every door is cut out of the mesh (0018/7), so a mesh ray
	// across a door link would be refused whatever the door does. The probe is therefore this NPC's
	// hull swept node to node through the collision world (`KernelHullTrace`, the characters folded
	// by the nav filter) -- the geometry `TestGroundMove` sweeps, without its 16-unit stepping.
	constexpr int32 MaskNpcSolid = 0x202400b;
	constexpr int32 MaskNpcSolidNoMonster = 0x2400b;
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	const FVector StartUnits = StartCm / ElysiumMove::U;
	const FVector EndUnits = EndCm / ElysiumMove::U;
	auto Clear = [](const FKernelHullTrace& Trace)
	{
		return Trace.Fraction == 1.0f && !Trace.bStartSolid && !Trace.bAllSolid;
	};
	FKernelHullTrace Probe;
	KernelHullTrace(StartUnits, EndUnits, MinsUnits, MaxsUnits, MaskNpcSolid, Probe);   // (a)
	if (Clear(Probe))
	{
		return true;
	}
	++NavStaleRouteLocalRouteAsks;                                                // (b) 0x103059d0, SEAM
	const FElysiumEntity* const Obstruction = World != nullptr ? static_cast<const FElysiumEntityWorld*>(World)->Resolve(Probe.HitEntity) : nullptr;
	if (Obstruction != nullptr && Obstruction->AsNpcBase() != nullptr)      // (c) +0x94
	{
		FKernelHullTrace Retry;
		KernelHullTrace(StartUnits, EndUnits, MinsUnits, MaxsUnits, MaskNpcSolidNoMonster, Retry);
		return Clear(Retry);
	}
	return false;
}

bool FElysiumNpcBase::DoorLinkPathfindingAllowed(FElysiumEntity& Door, const FVector& StartCm,
	const FVector& EndCm)
{
	FElysiumDoorBase* const DoorBase = Door.AsDoorBase();
	if (DoorBase == nullptr || World == nullptr)
	{
		return true;
	}
	FElysiumDoorLinkWords& Link = DoorBase->LinkWords;
	// `0x102fce80` (answers TRUE for "stale"):
	//   (1) `link+0x64` bit 1 clear -> usable.
	if (!Link.bStale)
	{
		return true;
	}
	//   (2) `curtime > link+0x68` (strict: `fVar1 >= fVar2 && !(fVar1 == fVar2)`) -> clear, usable.
	const double Now = World->NowSeconds();
	if (Now > Link.StaleUntil)
	{
		Link.bStale = false;
		return true;
	}
	//   (3) At most once per `curtime` per pathfinder (`pf+0x14`): re-probe the link's segment
	//       (`CheckStaleRoute 0x10304a40`). A clear probe clears the bit.
	if (PathfinderLinkProbeTime != Now)
	{
		PathfinderLinkProbeTime = Now;
		if (NavCheckStaleRoute(StartCm, EndCm))
		{
			Link.bStale = false;
			return true;
		}
	}
	//   (4) Still stale (a second ask this `curtime` answers so without probing). `0x102ff960`'s
	//       tail: a live `link+0` door gets the door-blocked notice, and the link is refused.
	if (FElysiumEntity* const Blocker = World->Resolve(Link.StaleDoor))
	{
		OnDoorBlocked(*Blocker);                                             // 0x1027de00
	}
	return false;
}

float FElysiumNpcBase::NavStepDistClearUnits(const FNavStepFacts& Step) const
{
	if (bNavLastFactsValid && Step.Blocker.IsSet() && NavLastFacts.DoorLinkEntity == Step.Blocker)
	{
		return static_cast<float>(FVector::Dist2D(Origin, NavLastFacts.DoorLinkPointCm) / ElysiumMove::U);
	}
	return 0.f;
}

bool FElysiumNpcBase::NavObstructionPreSink(const FNavStepFacts& Step, float DistClearUnits,
	ENavMoveResult& OutResult)
{
	// S1 `0x102eefb0(goal, distClear, &result)`, `this = nav+0x10`:
	//   goal flags (`+0x38`): `AILMG_TARGET_IS_GOAL 1` -> the tolerance is `path+0x28`; else
	//   `AILMG_TARGET_IS_TRANSITION 4` -> straight to the sink; else the tolerance is `path+0x40`
	//   (`m_waypointTolerance`) and a reached waypoint advances. The port's legs are all plain ground
	//   legs (no transition waypoint), and the goal's `+0x28` is the leg's remaining distance.
	const bool bTargetIsGoal = Navigator.CurWaypointIsGoal();
	const float ToleranceUnits = (bTargetIsGoal ? Navigator.GetGoalTolerance() : Navigator.WaypointToleranceCm)
		/ ElysiumMove::U;
	const float MaxDistUnits = Step.RemainingUnits;
	//   `maxDist < distClear`: the obstruction lies past the step -> `*result = 0`, decided.
	if (MaxDistUnits < DistClearUnits)
	{
		OutResult = ENavMoveResult::Ok;
		return true;
	}
	//   `maxDist < tolerance`: the target is (all but) reached.
	if (MaxDistUnits < ToleranceUnits)
	{
		// The goal, blocked by an NPC (`goal+0x44 == -3`) that is itself moving (slot 153 on
		// `goal+0x60`) -> the sink after all.
		FElysiumEntity* const Blocker = World != nullptr ? World->Resolve(Step.Blocker) : nullptr;
		const bool bNpcBlocker = NavIsNpcBlocker(Step.Blocker);
		if (bTargetIsGoal && bNpcBlocker && Blocker != nullptr && Blocker->IsMoving())
		{
			return false;
		}
		OutResult = ENavMoveResult::Ok;                                      // maxDist = distClear; *result = 0
		if (!bTargetIsGoal)
		{
			NavLogMoveStep(TEXT("obstructed inside the waypoint tolerance: waypoint passed (S1)"));
			NavAdvancePath();                                                // 0x102f0400
			return true;
		}
		//   `distClear < 0.01` (`_DAT_1044e658`, a double) -> `*result = goal+0x44`, the probe's own
		//   status: -3 for an NPC, -1 for any other entity (the world names no blocker here).
		if (DistClearUnits < 0.01f)
		{
			OutResult = bNpcBlocker ? ENavMoveResult::BlockedNpc : ENavMoveResult::BlockedEntity;
		}
		return true;
	}
	return false;                                                            // the sink, slot 1
}

bool FElysiumNpcBase::NavMoveSinkDoorStep(const FNavStepFacts& Step)
{
	// `0x1027dc10` (the base sink's slot 1): `goal+0x60` (the obstruction) -> its `+0xa4` door ->
	// NPC slot 531 `OnObstructingDoor(goal, door, distClear, &result)`; true = handled.
	if (World == nullptr || !Step.Blocker.IsSet())
	{
		return false;
	}
	FElysiumEntity* const Blocker = World->Resolve(Step.Blocker);
	FElysiumDoorBase* const Door = Blocker != nullptr ? Blocker->AsDoorBase() : nullptr;
	if (Door == nullptr)
	{
		return false;
	}
	// The goal the step carries: `maxDist` the leg's remaining distance; `distClear` how far the
	// body stands from the obstruction -- where the door link holds it, or 0 for an obstruction the
	// body names by contact.
	FLocalMoveGoal Goal;
	Goal.MaxDistanceUnits = Step.RemainingUnits;
	Goal.ExpectedBlocker = Blocker;
	const float DistClearUnits = NavStepDistClearUnits(Step);
	int32 Result = 0;
	if (!OnObstructingDoor(&Goal, Door, DistClearUnits, &Result))            // slot 531 (+0x84c)
	{
		return false;
	}
	MoveSinkResult = static_cast<ENavMoveResult>(Result);
	NavLogMoveStep(Result == 0 ? TEXT("door in the way, slot 531 handled it")
		: TEXT("door in the way, slot 531 blocked"));
	return true;
}

FElysiumNpcMoveRequest FElysiumNpcBase::PrepareMoveRequest(const FElysiumNpcMoveRequest& Request)
{
	FElysiumNpcMoveRequest Out = Request;
	if (Out.YawSpeedDegPerS <= 0.f)
	{
		// The stored `+0x38`, as `UpdateYaw(-1)` `0x102e1e20` reads it -- not a fresh slot 516: a
		// task-stated speed (`0x102e1ca8`, Andrei's `AndreiFacePlayerYawSpeed`) stands until a writer
		// replaces it. `MoveFacing` (motor slot 18 `0x102e19e0`) IS such a writer: every move step it
		// issues `0x102e1c10(yaw, -1.0)` (`102e1a6a`, `102e1b2d`), whose `-1.0` re-reads slot 516
		// `MaxYawSpeed` into `+0x38` (`0x102e1cf0`) -- `FElysiumNpc::MotorMoveReissueYaw`, spec 0002
		// V4b; `MotorThinkUpkeep` hands the changed word to the travelling body.
		Out.YawSpeedDegPerS = MotorYawRateDegPerS(MotorYawSpeedWord);
	}
	bKernelMoveLive = true;
	MotorYawRateHanded = Out.YawSpeedDegPerS;
	RegisterMoveIgnores();
	return Out;
}

void FElysiumNpcBase::MotorThinkUpkeep()
{
	if (bKernelMoveLive && Motor != nullptr)
	{
		RegisterMoveIgnores();                                              // slot 69, per think
		const float Rate = MotorYawRateDegPerS(MotorYawSpeedWord);           // `UpdateYaw(-1)`'s read of +0x38
		if (Rate > 0.f && Rate != MotorYawRateHanded)
		{
			Motor->SetYawSpeed(Rate);
			MotorYawRateHanded = Rate;
		}
	}
	MotorHandFacingTarget();                                                 // slot 15 `0x102e2180`
}

void FElysiumNpcBase::RegisterMoveIgnores()
{
	if (Motor == nullptr || World == nullptr)
	{
		ClearMoveIgnores();
		return;
	}
	TArray<FElysiumEntityHandle> Answered;
	for (const TUniquePtr<FElysiumEntity>& Entry : World->Entities())
	{
		FElysiumEntity* const Other = Entry.Get();
		// A dead record is skipped; a HIDDEN one is still asked (a hidden solid still collides, and
		// retail's filter sees every contact).
		if (Other == nullptr || Other == this || Other->IsDead() || Other->Handle == IgnoreCollisionEntity)
		{
			continue;
		}
		if (NavIgnoreCollision(Other))                                       // slot 69
		{
			Answered.Add(Other->Handle);
		}
	}
	for (const FElysiumEntityHandle& Held : MoveIgnoreRegistered)
	{
		if (!Answered.Contains(Held))
		{
			Motor->SetMoveIgnore(Held, false);
		}
	}
	for (const FElysiumEntityHandle& Fresh : Answered)
	{
		if (!MoveIgnoreRegistered.Contains(Fresh))
		{
			Motor->SetMoveIgnore(Fresh, true);
		}
	}
	MoveIgnoreRegistered = MoveTemp(Answered);
}

void FElysiumNpcBase::ClearMoveIgnores()
{
	if (Motor != nullptr)
	{
		for (const FElysiumEntityHandle& Ignored : MoveIgnoreRegistered)
		{
			Motor->SetMoveIgnore(Ignored, false);
		}
	}
	MoveIgnoreRegistered.Reset();
	bKernelMoveLive = false;
	MotorYawRateHanded = 0.f;
}

bool FElysiumNpcBase::NavIssueLeg(const FElysiumNpcMoveRequest& InRequest)
{
	// Port-only: the issue of one leg (`DoFindPath 0x102f2330`'s product, walked by the body) and the
	// record of what was issued, which retail keeps in the path's waypoints themselves.
	const FElysiumNpcMoveRequest Request = PrepareMoveRequest(InRequest);
	if (Motor == nullptr || !Motor->MoveTo(Request))
	{
		ClearMoveIgnores();
		Navigator.HeadLegRequest = FElysiumNpcMoveRequest();
		Navigator.bHeadLegRequestSet = false;
		return false;
	}
	Navigator.HeadLegRequest = Request;
	Navigator.bHeadLegRequestSet = true;
	// A new leg is a new probe: an end still standing under the hold, or a post-hold probe out, is
	// superseded (the retail words `nav+0x54..+0x60` stand, as retail's do across a waypoint advance).
	Navigator.bBlockerHoldStanding = false;
	Navigator.bBlockerHoldReissued = false;
	return true;
}

bool FElysiumNpcBase::NavReissueHeadLeg()
{
	// The recorded request, unchanged: same destination, arrival radius, gait and speed, and the
	// pedestrian multiplier as drawn at the route build (`0x102fe9f0` draws once per build).
	if (Motor == nullptr || !Navigator.bHeadLegRequestSet)
	{
		return false;
	}
	// The move's per-think upkeep reopens: the rate the leg carried is what the body holds, and the
	// next think re-reads `+0x38` against it (`MotorThinkUpkeep`).
	bKernelMoveLive = true;
	MotorYawRateHanded = Navigator.HeadLegRequest.YawSpeedDegPerS;
	RegisterMoveIgnores();
	bMoveIssued = Motor->MoveTo(Navigator.HeadLegRequest);
	if (!bMoveIssued)
	{
		ClearMoveIgnores();
	}
	return bMoveIssued;
}

void FElysiumNpcBase::NavLogMoveStep(const TCHAR* Outcome, int32 FailCode) const
{
	if (!UE_LOG_ACTIVE(LogElysiumNpcEnt, Verbose))
	{
		return;
	}
	const FVector DestCm = Navigator.bHeadLegRequestSet
		? Navigator.HeadLegRequest.DestinationCm : Navigator.GetGoalPos();
	const double Left2DCm = FVector::Dist2D(Origin, DestCm);
	if (FailCode != 0)
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Move: %s 0x%02x, dest (%.1f, %.1f, %.1f), %.1f cm 2-D left"),
			*DebugString(), Outcome, FailCode, DestCm.X, DestCm.Y, DestCm.Z, Left2DCm);
	}
	else
	{
		UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("%s Move: %s, dest (%.1f, %.1f, %.1f), %.1f cm 2-D left"),
			*DebugString(), Outcome, DestCm.X, DestCm.Y, DestCm.Z, Left2DCm);
	}
}

bool FElysiumNpcBase::NavSimplifyPathDoorRefused()
{
	// `SimplifyPath(nav, 0)` `0x102f13d0` from the `MoveNormal` gate `0x102efd50`, on every pass.
	return NavSimplifyPath(/*bForce*/ false);
}

bool FElysiumNpcBase::NavSimplifyPath(bool bForce)
{
	// `0x102f13d0`. `SetGoal 0x102ecd20` calls it forced (argument 1); the `MoveNormal` gate unforced.
	// SEAM, the forced call: the port's pass reads the body's path facts (the next door link, its
	// far end), which do not exist until the body holds the route `SetGoal` just requested -- a
	// forced pass there would probe the PREVIOUS route's facts. The port's `SetGoal`
	// (`StartTaskSetGoal`) therefore does not call it; the first `MoveNormal` pass on the new route
	// runs the far scan when `nav+0x38` is due. Named, not wired.
	++NavMoveStep.SimplifyPasses;
	// Nav type 0 or 2 only.
	const int32 NavTypeNow = Navigator.GetNavType();
	if (NavTypeNow != 0 && NavTypeNow != 2)
	{
		return false;
	}
	// `102f13fc`-`102f1429`: a head waypoint, NOT the goal (it has a next waypoint, `wp+0x30`, of
	// the head's own nav type), and head `flags & 0x2a == 0` (no `bits_WP_TO_PATHCORNER 0x2`,
	// `bits_WP_TO_DOOR`'s neighbour `0x8` goal bit, `bits_WP_DONT_SIMPLIFY 0x20`). The port's head
	// words are the Troika leg list's head (`PedestrianLegs`); a route with no list has one leg, the
	// goal, which the gate refuses. The port's legs are all ground, so "same nav type" holds.
	if (!NavIsGoalActive() || World == nullptr || Navigator.CurWaypointIsGoal())
	{
		return false;
	}
	constexpr int32 SimplifyRefusedHeadFlags = 0x2a;
	const FElysiumNpc* const HeadOwner = AsNpc();
	const int32 HeadFlags = HeadOwner != nullptr && HeadOwner->PedestrianLegs.Num() > 0
		? HeadOwner->PedestrianLegs[0].Flags : 0;
	if ((HeadFlags & SimplifyRefusedHeadFlags) != 0)
	{
		return false;
	}
	const double Now = World->NowSeconds();
	// The two scans' radii, Source units: the far scan `0x102f0e80` `{384, 144, 36, 4}`
	// (`0x1060fcd8`) and the quick pass `0x102f13a0` `{143.9, 144, 6, 1}`. A point more than
	// radius + 0.1 away is skipped.
	constexpr float FarScanRadiusUnits = 384.0f;
	constexpr float QuickPassRadiusUnits = 143.9f;
	// `nav+0x38`: the far scan's clock, `curtime + float [0x1049d988]` -- 0.5, read from the image.
	constexpr double FarScanIntervalSeconds = 0.5;

	// The one point a pass can probe in the port: the far end of the next door smart link on the
	// path (the NAMED MODERNIZATION -- Detour shortcuts the rest of the path itself).
	const bool bDoorAhead = bNavLastFactsValid && NavLastFacts.UpcomingDoorLinkEntity.IsSet();
	auto Pass = [this, bDoorAhead](float RadiusUnits, bool& bDoorSeen) -> bool
	{
		if (!bDoorAhead || bDoorSeen)
		{
			return false;
		}
		const double LimitCm = static_cast<double>(RadiusUnits + 0.1f) * ElysiumMove::U;   // radius + 0.1
		if (NavLastFacts.UpcomingDoorLinkDistanceCm > LimitCm)
		{
			return false;
		}
		return NavDoorProbe(NavLastFacts.UpcomingDoorLinkEndCm, bDoorSeen);
	};

	bool bDoorSeen = false;
	bool bRefused = false;
	if (bForce || NavSimplifyNextTime <= Now)
	{
		NavSimplifyNextTime = Now + FarScanIntervalSeconds;
		bRefused |= Pass(FarScanRadiusUnits, bDoorSeen);                     // 0x102f0e80
		// `0x102f0fe0` (the second far pass) is not ported: its body is unrecovered here and it
		// probes no point the port's path carries beyond the one above.
	}
	// ALWAYS the quick pass -- unless a door has already been seen this tick.
	bRefused |= Pass(QuickPassRadiusUnits, bDoorSeen);                       // 0x102f13a0
	return bRefused;
}

bool FElysiumNpcBase::NavDoorProbe(const FVector& PointCm, bool& bOutDoorSeen)
{
	// `0x102f06e0`, arm by arm.
	IElysiumEmbodiment* const Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return false;
	}
	//   (1) The ray: `0x2400b`, or `0x2600b` on a pedestrian path (`path+1`), from slot 193
	//       (`WorldSpaceCenter`: the origin raised to the middle of the collision box) to the point,
	//       `CTraceFilterSimple(this)`.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailCollisionExtents(*this, MinsUnits, MaxsUnits);
	FElysiumRetailTrace Ray;
	Ray.StartCm = Origin + FVector(0.0, 0.0, (MinsUnits.Z + MaxsUnits.Z) * 0.5 * ElysiumMove::U);
	Ray.EndCm = PointCm;
	Ray.RetailMask = Navigator.bPedestrian ? 0x2600b : 0x2400b;
	Ray.Ignore.Add(Handle);
	FElysiumRetailTraceResult Hit;
	if (!Embodiment->TraceRetail(Ray, Hit))
	{
		return false;                                                        // no collision world: clear
	}
	//   (2) `fraction == 1.0` (`_DAT_10449280`) -> `MoveLimit` toward the point (the shortcut's own
	//       test); no door arm. The shortcut itself is the follower's.
	if (Hit.Fraction == 1.0f)
	{
		return false;
	}
	//   (3) `hit+0x4c -> +0xa4`: the door the ray met.
	FElysiumEntity* const HitEntity = World->Resolve(Hit.HitEntity);
	FElysiumDoorBase* const Door = HitEntity != nullptr ? HitEntity->AsDoorBase() : nullptr;
	if (Door == nullptr)
	{
		return false;
	}
	//       The lock test `0x100eec70(door, npc)`: REFUSED -> false, nothing written.
	if (Door->IsUseRefused(Handle))
	{
		return false;
	}
	//       ACCEPTED -> a zeroed goal (14 + 31 dwords) with `+0x28 = dist + 10.0` (`_DAT_1044e664`),
	//       `dist` the ray's length to where it stopped; slot 531 with `distClear = dist`.
	const float DistUnits = static_cast<float>(FVector::Dist(Ray.StartCm, Hit.EndPosCm) / ElysiumMove::U);
	FLocalMoveGoal Goal;
	Goal.MaxDistanceUnits = DistUnits + 10.0f;
	int32 Result = 0;
	if (!OnObstructingDoor(&Goal, Door, DistUnits, &Result))                 // slot 531 (+0x84c)
	{
		return false;
	}
	//   (4) Handled: the out byte (`*puStack_1c = 1`, "a door was seen"); a non-zero result raises
	//       `OnNavFailed(0x0e, 1)` (nav vtable `+0x28`, `0x102f08e7`) then `0x1027de00`.
	bOutDoorSeen = true;
	if (Result == 0)
	{
		return false;
	}
	NavLogMoveStep(TEXT("door refused, failed"), GMoveStepFailDoor);
	NavOnNavFailed(GMoveStepFailDoor);
	OnDoorBlocked(*Door);
	//   (5) Every door arm answers false to its own caller (no shortcut); the port's answer is
	//       whether the `0x0e` failure was raised.
	return true;
}

FElysiumNpcBase::FNavStepFacts FElysiumNpcBase::NavSampleStep()
{
	FNavStepFacts Step;
	// The facts first: `Sample` below consumes a terminal status (it stops the body), and the facts
	// are what survive that.
	FElysiumNpcMoveFacts Facts;
	const bool bFacts = Motor->SampleMoveFacts(Facts);
	// Kept for the door arms (0018/7): the look-ahead reads the next door link, the move-step sink
	// and the stale mark the link the body is held at.
	NavLastFacts = bFacts ? Facts : FElysiumNpcMoveFacts();
	bNavLastFactsValid = bFacts;
	// `GetOrigin` (slot 220) is the entity record; the body is its source.
	const EElysiumNpcMoveStatus Status = SampleMotorIntoEntity();
	// The move's end drops the ignores registered for it (`RegisterMoveIgnores`).
	if ((bFacts && Facts.bRequestEnded) || Status == EElysiumNpcMoveStatus::Reached
		|| Status == EElysiumNpcMoveStatus::Failed || Status == EElysiumNpcMoveStatus::Unavailable)
	{
		ClearMoveIgnores();
	}
	if (bFacts && (Facts.bRequestAlive || Facts.bRequestEnded))
	{
		// Navigator slot 16: the head waypoint against the constant radius, 2-D when `nav+0x18 == 0`,
		// 3-D otherwise; not reached iff `tol < dist` (NaN: not reached). The body's request goes to
		// the head waypoint, so its remaining distance is that test's. NAMED MODERNIZATION: the
		// follower's own Success end (its arrival floor, `AlreadyAtGoal`) also counts as reached --
		// Unreal's path follower lands a body where retail's clamped step did.
		const float U = ElysiumMove::U;
		const double Dist2D = Facts.RemainingDistance2DCm / U;
		const double DistUnits = Navigator.GetNavType() == 0
			? Dist2D
			: FMath::Sqrt(Dist2D * Dist2D + FMath::Square(Facts.RemainingDzCm / U));
		const bool bSucceeded = Facts.bRequestEnded && Facts.ResultCode == EElysiumNpcMoveResultCode::Success;
		Step.bWaypointReached = DistUnits <= GMoveStepArrivalUnits || bSucceeded;
		Step.bGaveUp = Facts.bRequestEnded && !bSucceeded;
		Step.Blocker = Facts.BlockingEntity;
		// A body its door smart link holds at the doorway is blocked by that door: retail's move
		// probe meets the closed leaf and `goal+0x60` is the door (`0x1027dc10`).
		if (!Step.Blocker.IsSet() && Facts.bRequestAlive && Facts.DoorLinkEntity.IsSet())
		{
			Step.Blocker = Facts.DoorLinkEntity;
		}
		Step.RemainingUnits = static_cast<float>(DistUnits);
		return Step;
	}
	// A motor that reports no facts (a double, a headless world): its own verdict, read as the facts
	// it stands for -- arrived, or given up with no obstruction named.
	Step.bWaypointReached = Status == EElysiumNpcMoveStatus::Reached;
	Step.bGaveUp = Status == EElysiumNpcMoveStatus::Failed || Status == EElysiumNpcMoveStatus::Unavailable;
	return Step;
}

FElysiumNpcBase::ENavMoveResult FElysiumNpcBase::NavMoveNormalPass(const FNavStepFacts& Step)
{
	// `MoveNormal`. Route types 0 and 2 dispatch here; the port's waypoint move types
	// (jump 1, climb 3) are the body's own traversal, so every pass is this one.
	//
	// The gate `0x102efd50`: the route-type / nav-type checks read the head waypoint's move type
	// (`+0x2c`), which the follower does not report, so they are not ported; then `SimplifyPath(nav,
	// 0)` (result ignored) and `nav+0x51 = 0`. Quirk kept: a door refusal inside the simplify pass
	// raises `OnNavFailed(0x0e)` (inside `NavDoorProbe`, ahead of `OnDoorBlocked`, as `0x102f06e0`
	// orders them) and the pass goes on (`MoveNormal` does not re-test `nav+0x1c`).
	(void)NavSimplifyPathDoorRefused();
	Navigator.bBlockerHold = false;                                          // nav+0x51 = 0

	// Navigator slot 16, before any step is built (`102efb2f`).
	if (Step.bWaypointReached)
	{
		if (Navigator.CurWaypointIsGoal())                                   // 0x102ee660 -> 0x1030bd50
		{
			NavLogMoveStep(TEXT("arrived"));
			NavOnNavComplete();                                              // *result = 0
			return ENavMoveResult::Ok;
		}
		// `0x102f0400`, then `*result = 1` whatever it did. A failure it raised (`nav+0x1c` set) ends
		// the loop at its top; it must not be turned into a completion below.
		NavLogMoveStep(TEXT("waypoint passed"));
		const bool bHeadStands = NavAdvancePath();
		if (bHeadStands || Navigator.bNavFailed)
		{
			return ENavMoveResult::ChangeType;
		}
		// No head stands after the advance. Retail's pop (`0x1030ba90`) never empties the list: with
		// no next waypoint it prints "ERROR: Force end of route without goal" and flags the last one
		// as the goal, and the loop's next pass completes on it -- the same position, so the same
		// arrival. The port reaches that completion here.
		NavLogMoveStep(TEXT("arrived (forced end of route without goal)"));
		NavOnNavComplete();
		return ENavMoveResult::Ok;
	}

	// Not reached (`MoveNormal 0x102efaa0`, past slot 16): `102efb44` owner slot 248 `GetIdealSpeed`
	// is saved, then `m_Activity` (`+0xfec`), `m_nSequence` (`+0x6f0`) and `GetAbsOrigin`; `102efb80`
	// reads the path's movement activity (`0x102ee3f0`, `path+0x2c`) and `102efb88` pushes it through
	// owner slot 310 `SetActivity` -- the COMMIT (`m_Activity`, the sequence, `ResetSequenceInfo`), not
	// `SetIdealActivity`: the kernel plays the WALK/RUN row from the first move step, so its events
	// and its `+0x654` are the walk's.
	SetActivity(Navigator.GetMovementActivity());                            // 102efb80 / 102efb88
	// `102efb93..102efbb0`: `GetIdealSpeed() <= 0.0` (`0x104454c4`, `<=`) with `m_Activity == 2`
	// (`ACT_TRANSITION`, which slot 310 refuses to leave) -> `*result = 0`, no step this pass.
	if (GetIdealSpeed() <= 0.f && ActivityNumber == 2)
	{
		return ENavMoveResult::Ok;
	}
	// NOT PORTED, named: the restore after an `AIMR_OK` step (`102efc11`..): when the ideal speed
	// saved BEFORE the `SetActivity` is under 0.01 (double `0x1044e658`) and the body moved under 0.01
	// units in the step, `m_nSequence` is written back and the saved activity re-issued through slot
	// 310. The step is the body's own tick here (K1), so the distance moved inside this call has no
	// source.

	// Motor code 4 (`0x102e0bd0`, checked first): the obstruction is the move goal's own target
	// (`goal+0x34`) -> S7 `0x102ef6d0` runs `OnNavComplete`, `*result = 0`. The probe's own
	// `0x102e5d80` already clears a block by that target.
	const FElysiumEntityHandle MoveTargetHandle = NavMoveTarget();
	if (Step.Blocker.IsSet() && MoveTargetHandle.IsSet() && Step.Blocker == MoveTargetHandle)
	{
		NavLogMoveStep(TEXT("arrived (blocked by the move target, motor code 4)"));
		NavOnNavComplete();
		return ENavMoveResult::Ok;
	}

	// S1 dispatches the movement sink's slot 1 on the obstruction before any steering: the Troika
	// sink `0x10298340` (the crosswalk arms, then the base door arm `0x1027dc10`), or the base sink
	// itself on a base-only NPC. Handled -> the sink's result is the step's status (a door's -2
	// reaches the failure tail: the 4.0 s stale mark, then `0x0c`).
	// S1's own arms (`NavObstructionPreSink`) decide first and hand the step to the sink only where
	// retail's reach it.
	MoveSinkResult = ENavMoveResult::Ok;
	if (Step.Blocker.IsSet())
	{
		ENavMoveResult PreSinkResult = ENavMoveResult::Ok;
		if (NavObstructionPreSink(Step, NavStepDistClearUnits(Step), PreSinkResult))
		{
			NavFailArm = TEXT("S1 0x102eefb0, the obstruction's own arms ahead of the sink");
			return PreSinkResult;
		}
	}
	if (FElysiumNpc* Troika = AsNpc(); Troika && Step.Blocker.IsSet() && Troika->MovementSinkObstructed(Step))
	{
		NavFailArm = TEXT("the Troika movement sink's slot 1 0x10298340");
		return MoveSinkResult;
	}
	if (AsNpc() == nullptr && Step.Blocker.IsSet() && NavMoveSinkDoorStep(Step))
	{
		NavFailArm = TEXT("the base movement sink's door arm 0x1027dc10");
		return MoveSinkResult;
	}

	if (!Step.bGaveUp)
	{
		// The step walked (`MoveEnact` -> the motor, `motor+0x30` spent). An NPC named in the way
		// while the request still stands is being steered round: NAMED MODERNIZATION -- Unreal's crowd
		// avoidance stands for the local navigator's steer (`localnav` slot 6 `0x102de110`) and S2's
		// `PrependLocalAvoidance 0x102ede30` (result 1, the detour walked). Retail's order is kept
		// (steer and detour first; the 0.25 s hold only once they fail, i.e. once the follower gives
		// the request up) and so is the outcome: keep walking, nothing fails, no hold is armed.
		//
		// The step's facing: `CAI_HumanoidMotor` vfunc 19 `0x10264680` (`MoveGroundExecute`) calls
		// motor slot 18 `MoveFacing 0x102e19e0` on its copy of the move (`1026482f`), which is where
		// retail writes the motor's ideal yaw (`+0x34`, `SetIdealYawAndUpdate 0x102e1c10`) and
		// `m_flDesiredMoveYaw` (`+0x63ec`) during a move, then re-writes `+0x654` (`0x10264841/46`).
		// It replaces the old copy of the body's yaw into the ideal yaw (a named divergence, gone).
		// Only the Troika line carries the sequence bridge and `+0x63ec`; a base-only NPC (the hull
		// probe) has no row to read and its ideal yaw is left as its tasks wrote it.
		if (FElysiumNpc* const Troika = AsNpc())
		{
			Troika->MotorMoveGroundExecuteFacing();
		}
		return ENavMoveResult::Ok;
	}

	// The body gave the request up. The obstruction's class decides the status (`0x102e2d70`).
	ENavMoveResult Result = ENavMoveResult::BlockedWorld;                    // motor code 3: -1/-2/-4 -> -2
	if (NavIsNpcBlocker(Step.Blocker))
	{
		// S3 `0x102ef350` / S7 on motor code 2: the hold (`0x102ef3e0`). True -> walk up to the
		// clearance, spend the interval (`goal+0x38 |= 2`), `*result = 0`: nothing fails this pass.
		if (NavNpcBlockerStep(Step))
		{
			return ENavMoveResult::Ok;
		}
		Result = ENavMoveResult::BlockedNpc;                                 // motor code 2: -3
	}
	// Every negative result leaving `MoveEnact` passes `0x102ef760` first.
	if (NavBlockedStepCompletes())
	{
		return ENavMoveResult::Ok;
	}
	NavFailArm = Result == ENavMoveResult::BlockedNpc
		? TEXT("the body gave the request up on an NPC (motor code 2), the hold 0x102ef3e0 refused, "
			"outside 0x102ef760's goal tolerance")
		: TEXT("the body gave the request up on the world or an unnamed blocker (motor code 3), "
			"outside 0x102ef760's goal tolerance");
	return Result;
}

void FElysiumNpcBase::NavigatorMoveStep()
{
	// `CAI_Navigator::Move` `0x102eff40` (R3 "Entry gate and order of tests"). The interval is the
	// one `PerformMovement` (`0x1026c120`) was handed; `102eff57`: clamped to 1.0.
	float Interval = MotorSeams.PerformMovementInterval;
	if (Interval > GMoveStepMaxInterval)
	{
		Interval = GMoveStepMaxInterval;
	}
	// `102effab`: `path+0x10 m_bPaused` -> return. No stop, no fail, `nav+0x1c` untouched. Retail's
	// body translates only inside `Move`, so a paused path stands still while `MotorUpdateYaw`
	// (`0x102e1e20`, the tasks' facing -- `FACE_NEXT_NODE` under schedule `0x102`, the door wait's
	// alternate AI via `0x102bf770` from slot 464) keeps turning it. NAMED MODERNIZATION: the
	// port's body walks its leg on its own tick, so the arm parks it (`FElysiumNpc::NavParkBody`:
	// the request stopped, the head leg kept; a turn-in-place still runs) and the first pass past
	// the pause that may move (`m_bShouldMove`, the gate below) re-issues that leg
	// (`NavUnparkBody`). A base-only NPC has no park and simply returns.
	FElysiumNpc* const PauseTroika = AsNpc();
	if (Navigator.IsPaused())                                                // 0x102ee2e0
	{
		if (PauseTroika != nullptr)
		{
			PauseTroika->NavParkBody();
		}
		return;
	}
	if (PauseTroika != nullptr && BaseScheduleHost.bShouldMove)
	{
		PauseTroika->NavUnparkBody();
	}
	// `102effc2`: NPC slot 525 `OverrideMove(interval)` true -> return. The base `0x1027da90` declines;
	// the ManBat's flight (`0x1038b120`) is the species body that answers true.
	if (OverrideMove(Interval))
	{
		return;
	}
	// `102effd3` / `102f0198`: `m_bShouldMove == 0` -> no move this think and no failure.
	if (!BaseScheduleHost.bShouldMove)                                       // npc+0x1a40
	{
		if (Navigator.GetNavType() == GMoveStepClimbNavType)
		{
			++NavMoveStep.ClimbMotorResets;
			NavSetType(0);                                                   // 0x102eeba0
		}
		else
		{
			// `nav+0x18 != -1` -> motor slot 10, the velocity zeroed for this think with
			// the route kept. SEAM: the body integrates on its own tick and `IElysiumNpcMotor` has no
			// velocity stop that keeps the request (`Stop` drops it), so the call is recorded.
			++NavMoveStep.VelocityStops;
		}
		return;
	}
	// `102effe1..102f0069`: the hull / frame stamps on five components and `0x1000f240(path)` with its
	// result dropped -- words the port's navigator does not keep. `102f007b`: `motor+0x30 = interval`,
	// the budget the pass loop below stands for.
	if (!Navigator.IsGoalSet())                                              // 102f0081 path+0x5c == 0
	{
		++NavMoveStep.NoRouteWarnings;
		NavLogMoveStep(TEXT("no goal type, failed"), GMoveStepFailNoGoal);
		NavOnNavFailed(GMoveStepFailNoGoal);
		return;
	}
	if (!Navigator.IsGoalActive())                                           // 102f00bc head waypoint == 0
	{
		NavLogMoveStep(TEXT("no head waypoint, failed"), GMoveStepFailNoRoute);
		NavOnNavFailed(GMoveStepFailNoRoute);                                // no warning, no stale mark
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < BaseScheduleHost.MoveWaitFinished)                             // 102f00cd 0x10280a20 npc+0x5cf0
	{
		return;
	}
	if (Motor == nullptr)
	{
		return;                                                              // port: no body to sample
	}

	// The loop (`102f00e9..102f0165`): result seeded 1, `nav+0x1c = 0`, pass counter 0.
	Navigator.bNavFailed = false;
	NavMoveStep.Passes = 0;
	NavFailArm = TEXT("none");
	ENavMoveResult Result = ENavMoveResult::ChangeType;
	FElysiumEntityHandle LastBlocker;
	FNavStepFacts LastStep;
	bool bBudgetSpent = false;
	for (;;)
	{
		// Loop top: the latch exits (a failure result goes on to the tail); a spent budget exits.
		if (Navigator.bNavFailed || bBudgetSpent)
		{
			if (static_cast<int32>(Result) >= 0)
			{
				return;
			}
			break;
		}
		const FNavStepFacts Step = NavSampleStep();
		LastBlocker = Step.Blocker;
		LastStep = Step;
		Result = NavMoveNormalPass(Step);
		// `INC EBP; CMP EBP,0x10; JG`: the 17th dispatch fails whatever it answered.
		if (++NavMoveStep.Passes > GMoveStepMaxPasses)
		{
			NavMarkStaleLink(GMoveStepStaleSeconds);                         // 102f016e
			NavLogMoveStep(TEXT("17th pass, failed"), GMoveStepFailNoRoute);
			NavOnNavFailed(GMoveStepFailNoRoute);                            // 102f0180
			return;
		}
		if (static_cast<int32>(Result) < 0)
		{
			break;
		}
		// 0 spends the think's budget (the motor zeroes `motor+0x30`, or the step walked it); 1 re-enters
		// with what is left.
		bBudgetSpent = Result == ENavMoveResult::Ok;
	}
	// The failure tail `102f0169`: `CMP EAX,-3; JZ` skips the stale mark.
	if (Result != ENavMoveResult::BlockedNpc)
	{
		NavMarkStaleLink(GMoveStepStaleSeconds);                             // 0x102f1fa0(nav, 4.0, NULL)
	}
	NavLogMoveStep(Result == ENavMoveResult::BlockedNpc ? TEXT("blocked by an NPC (-3), failed")
		: TEXT("blocked, failed"), GMoveStepFailNoRoute);
	// Which arm of the pass answered the negative result this tail turns into `0x0c` (packet S11
	// item 1.4): the arm, the step's facts and the goal tolerance the blocked-step arms compare
	// (`path+0x28`, `0x102eefb0` / `0x102ef760`).
	if (UE_LOG_ACTIVE(LogElysiumNpcEnt, Verbose))
	{
		const FElysiumEntity* const BlockerEntity =
			(World != nullptr && LastStep.Blocker.IsSet()) ? World->Resolve(LastStep.Blocker) : nullptr;
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s Move: 0x0c raised by %s (result %d): blocker %s, body gave up %d, %.2f units "
				"left, goal tolerance %.2f cm"),
			*DebugString(), NavFailArm, static_cast<int32>(Result),
			BlockerEntity != nullptr ? *BlockerEntity->DebugString()
				: (LastStep.Blocker.IsSet() ? TEXT("an unresolved handle") : TEXT("none (the world)")),
			LastStep.bGaveUp ? 1 : 0, LastStep.RemainingUnits, Navigator.GetGoalTolerance());
	}
	NavOnNavFailed(GMoveStepFailNoRoute);                                    // 102f0180 nav slot 10
	if (Result == ENavMoveResult::BlockedNpc)
	{
		Navigator.LastOutcome.Kind = EElysiumNpcNavOutcomeKind::NpcBlocked;
		Navigator.LastOutcome.Blocker = LastBlocker;
	}
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::NavGoalPosition(FVector& OutGoalUnits) const
{
	// `thunk_FUN_102ee140(m_pNavigator)` -- `ActualGoalPosition`: `path+0x4c` minus `path+0x34`. Retail
	// has no "none" answer (the reset leaves `(0,0,0)`), so this always writes and answers true; the
	// bool is the port's old seam shape, kept for the callers that seed a fallback. Source units, in
	// the port's axes (no Y reflection): the frame `Origin / U` is in.
	OutGoalUnits = Navigator.GetGoalPos() / ElysiumMove::U;
	return true;
}

bool FElysiumNpcBase::IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance)
{
	// `FUN_10280790(start, apex, end, maxRise, maxDrop, maxDistance)`, arm for arm. Every threshold
	// carries the same `_DAT_104493d0 = 0.1` slack except the apex, which is scaled by
	// `_DAT_10460020 = 1.25` instead.
	const float Rise = static_cast<float>(EndUnits.Z - StartUnits.Z);
	if (MaxRise + NpcKernelMotorShared::GJumpLegalSlack < Rise)
	{
		return false;
	}
	const float Drop = static_cast<float>(StartUnits.Z - EndUnits.Z);
	if (MaxDrop + NpcKernelMotorShared::GJumpLegalSlack < Drop)
	{
		return false;
	}
	const float ApexRise = static_cast<float>(ApexUnits.Z - StartUnits.Z);
	if (MaxRise * GJumpApexScale < ApexRise)
	{
		return false;
	}
	// The distance reuses `Drop` for the Z term — squared, so the sign does not matter, and the
	// listing computes it exactly this way.
	const float Distance = FMath::Sqrt(
		static_cast<float>((StartUnits.Y - EndUnits.Y) * (StartUnits.Y - EndUnits.Y))
		+ Drop * Drop
		+ static_cast<float>((StartUnits.X - EndUnits.X) * (StartUnits.X - EndUnits.X)));
	return !(MaxDistance + NpcKernelMotorShared::GJumpLegalSlack < Distance);
}

bool NpcKernelMotorShared::IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance)
{
	return FElysiumNpcBase::IsJumpLegalGeometry(StartUnits, ApexUnits, EndUnits, MaxRise, MaxDrop,
		MaxDistance);
}
