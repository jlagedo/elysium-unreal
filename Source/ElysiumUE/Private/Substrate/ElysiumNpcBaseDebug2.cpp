// `CAI_BaseNPC`'s bodies of the `Debug2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDebug2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcDebug2Shared.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// The `m_debugOverlays` bits this family's bodies gate on, with the listing offset that tests
	// each. Retail tests several of them as a byte compare on `AH`, which is the same bit.
	constexpr int32 GNpcKernelDebug2BitText = 0x1;           // 0x102d160d
	constexpr int32 GNpcKernelDebug2BitCollisionBox = 0x1000; // 0x1027583b `TEST AH,0x10`
	constexpr int32 GNpcKernelDebug2BitNavNode = 0x2000;      // 0x10275951 `TEST AH,0x20`
	constexpr int32 GNpcKernelDebug2BitRoute = 0x4000;        // 0x102757a7 `TEST AH,0x40`
	constexpr int32 GNpcKernelDebug2BitZap = 0x10000;         // 0x1027576a
	constexpr int32 GNpcKernelDebug2BitMemory = 0x20000;      // 0x10275c65
	constexpr int32 GNpcKernelDebug2BitEnemyLines = 0x200000; // 0x1027636a
	constexpr int32 GNpcKernelDebug2BitViewCone = 0x400000;   // 0x10275a12
	// Every box extent `0x10275760` pushes, in SOURCE units, as the listing spells them.
	// `0xc0a00000` / `0x40a00000` = -5.0 / 5.0; `0xc1200000` / `0x41200000` = -10.0 / 10.0;
	// `0x42200000` = 40.0, `0xc2200000` = -40.0, `0x43480000` = 200.0.
	const FVector GNpcKernelDebug2Box5Mins(-5.f, -5.f, -5.f);
	const FVector GNpcKernelDebug2Box5Maxs(5.f, 5.f, 5.f);
	const FVector GNpcKernelDebug2Box10Mins(-10.f, -10.f, -10.f);
	const FVector GNpcKernelDebug2Box10Maxs(10.f, 10.f, 10.f);
	const FVector GNpcKernelDebug2ConeMins(0.f, 0.f, -40.f);
	const FVector GNpcKernelDebug2ConeMaxs(200.f, 0.f, 40.f);
	const FVector GNpcKernelDebug2ConeEyeMins(0.f, 0.f, -10.f);
	const FVector GNpcKernelDebug2ConeEyeMaxs(40.f, 0.f, 10.f);
	// `SCHED_FORCED_GO`, the one program `0x10275760`'s third arm asks for by number
	// (`PUSH 0x39` into `GetSchedule`). `0x102cadd0` registers id 0x39 under the name
	// `"FORCED_GO"` (`0x10608448`) on `CAI_BaseNPC`.
	constexpr int32 GNpcKernelDebug2SchedForcedGo = 0x39;
	// `_DAT_1044e664` = 10.0 — the distance under which the memory arm draws a cross instead of a
	// hull box, and the length it scales the target's forward vector by. Family Squad recovered the
	// same word as the follower-distance overlap.
	constexpr float GNpcKernelDebug2CrossRangeUnits = 10.f;
	// `_DAT_10452dc4` = 2.0 — the Z spread of the cross's two arms.
	constexpr float GNpcKernelDebug2CrossZSpread = 2.f;
	// `_DAT_104454c4` = 0.0.
	constexpr float GNpcKernelDebug2Zero = 0.f;
	const TCHAR* const GNpcKernelDebug2BoxDirection = TEXT("NDebugOverlay::BoxDirection");
	const TCHAR* const GNpcKernelDebug2Line = TEXT("NDebugOverlay::Line");
	const TCHAR* const GNpcKernelDebug2Text = TEXT("NDebugOverlay::Text");
}

// --- Moved from `ElysiumNpcDebug2.cpp` (story 5 step 5) ---

void FElysiumNpcBase::DrawNavigatorRouteOverlay() const
{
	// SEAM for `CAI_Navigator::DrawDebugRouteOverlay(m_pNavigator)` (`0x102f28e0`), the `0x4000`
	// arm's whole body. `m_pNavigator` (+0x5d34) is an `ELYSIUM_NPC_WORD_CHAIN` row onto the motor
	// and no route store exists, so there is no route to draw.
}

bool FElysiumNpcBase::NavigatorNearestNodePositionUnits(FVector& OutUnits) const
{
	// The `0x2000` arm's three-step:
	//
	//     m_pNavigator->+0x08 = m_pNavigator->+0x04->+0x156c;   // the NPC's own +0x156c, stamped in
	//     m_pNavigator->+0x0c = gpGlobals->+0x04;               // the frame counter
	//     idx = CAI_Network::NearestNodeToNPC(m_pNavigator->+0x2c, this, GetOrigin());  0x102f3c10
	//     if (idx != -1) node = network->+0x04[idx];
	//     CAI_Node::GetPosition(node, out, m_eHull);            // 0x102fb0d0
	//
	// The two scratch writes land on navigator words the port's mover does not keep (the pathing
	// hull is read straight off the NPC wherever retail reads `nav+8`). The final position is the
	// STANDING hull's, `m_eHull`, not the pathing one the search measured with.
	const int32 Node = NavNearestNodeToNpc(Origin);                            // slot 220 GetOrigin
	FVector NodeCm = FVector::ZeroVector;
	if (Node == INDEX_NONE || World == nullptr || !World->Places().GetPositionCm(Node, HullKind, NodeCm))
	{
		return false;
	}
	OutUnits = NodeCm / ElysiumMove::U;
	return true;
}

namespace
{
	// `0x102f3c10`'s box: `local_28/24/20` = 800, 800, 200 units, or `_DAT_1046bacc` (2048) on X/Y
	// and 2048.0 on Z when the capabilities carry `4`.
	constexpr float GNearestNpcBoxXYUnits = 800.0f;
	constexpr float GNearestNpcBoxZUnits = 200.0f;
	constexpr float GNearestNpcFlyBoxUnits = 2048.0f;
	constexpr int32 GNearestNpcCapFly = 0x4;       // bits_CAP_MOVE_FLY: type-3 (air) nodes
	constexpr int32 GNearestNpcCapGround = 0x1;    // bits_CAP_MOVE_GROUND: type-2 nodes
	constexpr int32 GNearestNpcNodeGround = 2;
	constexpr int32 GNearestNpcNodeAir = 3;
	constexpr int32 GNearestNpcNodeClimb = 4;
	// `ListNodesInBox`'s cap at both call sites (`PUSH 10`).
	constexpr int32 GNearestNodeListCount = 10;
	// `0x102f1900`: a climb node is probed only with one of the info bits `0x1d`.
	constexpr int32 GCanFitClimbBits = 0x1d;
	// The mask `0x102f3c10` hands `CanFitAtNode` (`PUSH 0x2400b`).
	constexpr int32 GCanFitMask = 0x2400b;
	// `0x102f1a20`'s trace end: the point raised by `_DAT_1044e658` (the double 0.01).
	constexpr double GCanFitRiseUnits = 0.01;
}

int32 FElysiumNpcBase::NavNearestNodeToNpc(const FVector& PositionCm) const
{
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || Places->NumNodes() == 0)                          // 0x102f3c3a *this == 0
	{
		return INDEX_NONE;
	}
	// The cache `0x102f4520` (20 recent answers, keyed by point and hull, reused inside a short
	// window) and its write-back `0x102f45f0` are engine machinery -- a lookup shortcut in front of
	// this body -- and are not ported: the search runs every call.
	FElysiumNpcBase* Self = const_cast<FElysiumNpcBase*>(this);
	const int32 Caps = Self->CapabilitiesGet();                                // slot 513, local_48
	const int32 Hull = RetailPathingHull();                                    // +0x156c, local_44
	const FVector P = PositionCm / ElysiumMove::U;
	const FVector Half = (Caps & GNearestNpcCapFly) != 0
		? FVector(GNearestNpcFlyBoxUnits, GNearestNpcFlyBoxUnits, GNearestNpcFlyBoxUnits)
		: FVector(GNearestNpcBoxXYUnits, GNearestNpcBoxXYUnits, GNearestNpcBoxZUnits);
	// `CNodeNPCFilter::vfunc0` (`0x102f40f0`): the node type against the capabilities, then slot 527.
	auto IsValid = [Places, Caps, Self](int32 Node) -> bool
	{
		const int32 Type = Places->Row(Node).Type;
		if (Type == GNearestNpcNodeAir && (Caps & GNearestNpcCapFly) == 0)
		{
			return false;
		}
		if (Type == GNearestNpcNodeGround && (Caps & GNearestNpcCapGround) == 0)
		{
			return false;
		}
		return !Self->IsUnusableNode(const_cast<FElysiumPlaceRow*>(&Places->Row(Node)));   // +0x83c
	};
	// `CNodeNPCFilter::vfunc1` (`0x102f4140`): the squared distance to the node at the pathing hull.
	auto DistanceSqr = [Places, Hull, &P](int32 Node) -> float
	{
		FVector At = Places->Row(Node).OriginCm;
		Places->GetPositionCm(Node, Hull, At);
		return static_cast<float>(FVector::DistSquared(At / ElysiumMove::U, P));
	};
	const TArray<int32> Order = Places->ListNodesInBox(GNearestNodeListCount, P - Half, P + Half,
		IsValid, DistanceSqr);                                                 // 0x102f32f0
	const FVector ViewOffsetCm = EyePosition() - Origin;                       // npc +0x184..+0x18c
	int32 Fallback = INDEX_NONE;                                               // local_10
	for (const int32 Node : Order)
	{
		if (!NavCanFitAtNode(Node, Hull, GCanFitMask) || IsUnusableNodeIndex(Node))   // 0x102f1900, 0x1027db30
		{
			continue;
		}
		FVector NodeCm = Places->Row(Node).OriginCm;
		Places->GetPositionCm(Node, Hull, NodeCm);                             // param_1[0x55b]
		const ENavNodeTrace Trace = NavNearestNodeTrace(PositionCm, NodeCm + ViewOffsetCm, Handle);   // 0x102f3900
		if (Trace == ENavNodeTrace::Clear)
		{
			return Node;
		}
		if (Trace == ENavNodeTrace::ClearPastFlagged && Fallback == INDEX_NONE)
		{
			Fallback = Node;
		}
	}
	return Fallback;
}

bool FElysiumNpcBase::NavCanFitAtNode(int32 Node, int32 Hull, int32 Mask) const
{
	// `0x102f1900`: the node through the network (an id outside it bumps `DAT_106c994c` and reads a
	// NULL node -- the one caller hands only listed ids), `GetPosition(node, nav+8)`, then the two
	// geometry queries below.
	FVector NodeCm = FVector::ZeroVector;
	if (World == nullptr || !World->Places().GetPositionCm(Node, Hull, NodeCm))
	{
		return false;
	}
	const FElysiumPlaceRow& Row = World->Places().Row(Node);
	const bool bProbe = Row.Type == GNearestNpcNodeGround
		|| (Row.Type == GNearestNpcNodeClimb && (Row.Flags & GCanFitClimbBits) != 0);
	const FVector PointUnits = NodeCm / ElysiumMove::U;
	if (bProbe)
	{
		// `0x102e7270(m_pMoveProbe, pos, mask, 0, 0, 0, 0)` -- `CheckStandPosition` (R1 §4), with NO
		// `m_bForceNPCCheck` bracket (that is `CanStandAt`'s): the foot box of `m_Collision`'s OBB
		// dropped slot 523 under the node, standable iff it hit and slot 166 agrees. A base-only NPC
		// has no Troika move probe here and refuses.
		const FElysiumNpc* Troika = AsNpc();
		if (Troika == nullptr || !Troika->MoveProbeCheckStandPosition(PointUnits, Mask))
		{
			return false;
		}
	}
	// `0x102f1a20` (R1 §4): a trace from the point to the point raised by 0.01 (`0x1044e658`) on the
	// FULL row of the navigator's hull (`nav+8`: `0x102d6100` / `0x102d6120`; the small pair is
	// `+0x20` / `+0x2c`), `CTraceFilterSimple(npc, 0)`, the caller's mask; fits iff the trace does
	// NOT start solid (`[ESP+0x8b]`, `102f1c44`) -- the fraction is ignored. Family Motor's
	// `KernelHullTrace`; a world with no collision answers clear, which fits.
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailHullExtents(Hull, EElysiumHullExtents::Full, Mins, Maxs);
	FKernelHullTrace Trace;
	KernelHullTrace(PointUnits, PointUnits + FVector(0.0, 0.0, GCanFitRiseUnits), Mins, Maxs, Mask, Trace);
	return !Trace.bStartSolid;
}

FElysiumNpcBase::ENavNodeTrace FElysiumNpcBase::NavNearestNodeTrace(const FVector& StartCm,
	const FVector& EndCm, const FElysiumEntityHandle& Ignore) const
{
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return ENavNodeTrace::Clear;                                           // no collision world
	}
	float Fraction = 1.0f;
	bool bStartSolid = false;
	Embodiment->TraceCameraHull(StartCm, EndCm, FVector::ZeroVector, Ignore, Fraction, bStartSolid);
	// `0x102f3a5b`: clear at `fraction == 1.0` exactly (`_DAT_10449280`); startsolid is not read.
	return Fraction == 1.0f ? ENavNodeTrace::Clear : ENavNodeTrace::Blocked;
}

void FElysiumNpcBase::DrawPathfinderDebugOverlays(int32 DebugOverlayBits) const
{
	// SEAM for `CAI_Pathfinder::DrawDebugGeometryOverlays(m_pPathfinder, m_debugOverlays)`
	// (`0x103061e0`), the first of slot 123's two tail calls. `m_pPathfinder` (+0x5d3c) is a chain
	// row; the bits are passed through unchanged, which is the whole of the call.
	(void)DebugOverlayBits;
}

void FElysiumNpcBase::EntityDrawDebugGeometryOverlays() const
{
	// SEAM for `CBaseEntity::DrawDebugGeometryOverlays()`, slot 123's other tail call and the last
	// statement of `0x10275760`.
}

int32 FElysiumNpcBase::EntityDrawDebugTextOverlays() const
{
	// SEAM for `CBaseEntity::DrawDebugTextOverlays()`, the FIRST statement of both slot-124 bodies.
	// Its answer is the next free text-overlay LINE, which every override adds its own lines above.
	// Nothing stands the base lines here, so the first free line is 0.
	return 0;
}

bool FElysiumNpcBase::CollisionObbExtentsUnits(FVector& OutMinsUnits, FVector& OutMaxsUnits) const
{
	// SEAM for `m_Collision`'s vtable `+0x4` `OBBMins()` and `+0x8` `OBBMaxs()` (`+0x0270`), as the
	// DEBUG overlays read it. Answering false takes the arm retail takes for an entity whose OBB mins
	// and maxs are EQUAL on all three axes — the default ±5 box. Family Motor's
	// `RetailCollisionExtents` answers the same words for an NPC since 0018 story 6 (its `m_eHull`
	// row); this developer-overlay copy is left on the degenerate arm, and folding it onto that body
	// is the overlays' own change (their cases, `NpcKernelDebug` / `NpcKernelDebug10`, pin this arm).
	(void)OutMinsUnits;
	(void)OutMaxsUnits;
	return false;
}

void FElysiumNpcBase::DrawDebugGeometryOverlays()
{
	// --- Arm 1, `0x10000` (SDK's `OVERLAY_NPC_ZAP_BIT`) --------------------------------------------
	//
	//     VacateSquadSlot();                                   // 0x1028ae60 — family Squad's body
	//     Weapon_Drop(GetActiveWeapon());                      // slot 385
	//     ThinkSet(SUB_Remove 0x10015b68, 0.0, NULL);          // remove on the next think
	//
	// This is NOT a draw, and the bit is NOT cleared: an NPC with `0x10000` set drops its weapon and
	// re-arms its own removal on every call. SDK 2013 clears the bit here; Troika's build does not,
	// and the squad-slot release is Troika's own addition. Both are reproduced.
	if ((DebugOverlays & GNpcKernelDebug2BitZap) != 0)
	{
		VacateSquadSlot();
		Weapon_Drop(ActiveWeaponEntity(), nullptr, false);
		// `SUB_Remove` at a zero think delay. `Kill()` is this runtime's terminal removal.
		Kill();
	}

	// --- Arm 2, `0x4000` — the navigator's route ---------------------------------------------------
	if ((DebugOverlays & GNpcKernelDebug2BitRoute) != 0)
	{
		DrawNavigatorRouteOverlay();
	}

	// --- Arm 3, the save-position box --------------------------------------------------------------
	//
	//     if (!(DAT_1092053c & 1) && m_pSchedule && m_pSchedule == GetSchedule(SCHED_FORCED_GO))
	//         NDebugOverlay::Box(m_vSavePosition, -5, +5, 255, 0, 255, 0, 0);
	//
	// Note this arm is NOT gated on `m_debugOverlays` at all: it is gated on a global latch and on
	// the NPC running `SCHED_FORCED_GO` (0x39). `DAT_1092053c & 1` is a one-shot initialisation
	// latch with no port counterpart — nothing in this runtime sets it, which is the arm-ENABLING
	// state, so the schedule test is what decides. The colour is magenta at alpha 0 (wireframe).
	if (Schedule.IsRunning()
		&& GetLocalScheduleId(Schedule.Current) == GNpcKernelDebug2SchedForcedGo)
	{
		EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, LastPosition / ElysiumMove::U,
			GNpcKernelDebug2Box5Mins, GNpcKernelDebug2Box5Maxs, 255, 0, 255, 0);
	}

	// --- Arm 4, `0x1000` — the collision box -------------------------------------------------------
	//
	//     GetVectors(&forward, &right, &up);                    // computed and DROPPED
	//     if (OBBMins().x == OBBMaxs().x && .y == .y && .z == .z)
	//         NDebugOverlay::Box(GetAbsOrigin(), -5, +5, 255, 128, 0, 20, 0);   // degenerate
	//     else
	//         NDebugOverlay::Box(GetAbsOrigin(), OBBMins(), OBBMaxs(), 255, 0, 0, 20, 0);
	//
	// The three basis vectors are computed before the test and never read: an artefact of the SDK
	// body this was cut down from, reproduced only as this comment.
	if ((DebugOverlays & GNpcKernelDebug2BitCollisionBox) != 0)
	{
		FVector ObbMins = FVector::ZeroVector;
		FVector ObbMaxs = FVector::ZeroVector;
		if (!CollisionObbExtentsUnits(ObbMins, ObbMaxs))
		{
			EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, Origin / ElysiumMove::U,
				GNpcKernelDebug2Box5Mins, GNpcKernelDebug2Box5Maxs, 255, 128, 0, 20);
		}
		else
		{
			EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, Origin / ElysiumMove::U, ObbMins, ObbMaxs,
				255, 0, 0, 20);
		}
	}

	// --- Arm 5, `0x2000` — the nearest nav node ----------------------------------------------------
	//
	//     NDebugOverlay::Box(nodePos, -10, +10, 255, 255, 255, 0, 0)
	//
	// A white wireframe box at the node the pathfinder says is nearest, sized by `m_eHull`.
	if ((DebugOverlays & GNpcKernelDebug2BitNavNode) != 0)
	{
		FVector NodeUnits = FVector::ZeroVector;
		if (NavigatorNearestNodePositionUnits(NodeUnits))
		{
			EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, NodeUnits, GNpcKernelDebug2Box10Mins,
				GNpcKernelDebug2Box10Maxs, 255, 255, 255, 0);
		}
	}

	// --- Arm 6, `0x400000` — the view cone ---------------------------------------------------------
	//
	//     if ((m_debugOverlays & 0x400000) && m_pBaseNPCTroika == 0) { … }
	//
	// **The second gate is the whole story of this arm.** `+0x0098` is `m_pBaseNPCTroika`, one of
	// `CBaseEntity`'s cached downcast pointers (`+0x0094 m_pBaseNPC`, `+0x0098 m_pBaseNPCTroika`,
	// `+0x009c m_pCombatCharacter`). It is non-null for EVERY `CAI_BaseNPCTroika`, and every
	// classname this runtime stands is on the Troika line — so the view-cone arm is DEAD for every
	// NPC in this port, exactly as it is dead for every NPC in retail that reaches this body through
	// the Troika chain. That is the recovered answer, not a gap.
	//
	// What the arm would do, from the listing:
	//     range = acos(m_flFieldOfView);                        // 0x10432520, +0x1574
	//     eye   = EyeDirection2D();                             // slot 372
	//     left  = ( cos(range)*eye.x - sin(range)*eye.y,  sin(range)*eye.x + cos(range)*eye.y, 0 )
	//     right = the same with -range
	//     BoxDirection(EyePosition(), (0,0,-40), (200,0,40), left,  255, 0, 0, 50, 0)
	//     BoxDirection(EyePosition(), (0,0,-40), (200,0,40), right, 255, 0, 0, 50, 0)
	//     BoxDirection(EyePosition(), (0,0,-40), (200,0,40), eye,     0, 255, 0, 50, 0)
	//     BoxDirection(EyePosition(), (0,0,-10), (40,0,10),  eye,     0, 255, 0, 50, 0)
	// The fourth call is a BoxDirection in Troika's build where SDK 2013 has a plain `Box`.
	if ((DebugOverlays & GNpcKernelDebug2BitViewCone) != 0 && !IsBaseNpcTroika())
	{
		const float Range = FMath::Acos(ElysiumNpcSense::DefaultViewConeDot);
		const FVector Eye = EyeDirection2D();
		const FVector EyeUnits = EyePosition() / ElysiumMove::U;
		const FVector Left(FMath::Cos(Range) * Eye.X - FMath::Sin(Range) * Eye.Y,
			FMath::Sin(Range) * Eye.X + FMath::Cos(Range) * Eye.Y, 0.f);
		const FVector RightDir(FMath::Cos(-Range) * Eye.X - FMath::Sin(-Range) * Eye.Y,
			FMath::Sin(-Range) * Eye.X + FMath::Cos(-Range) * Eye.Y, 0.f);
		EmitOverlayBoxDirection(GNpcKernelDebug2BoxDirection, EyeUnits, GNpcKernelDebug2ConeMins,
			GNpcKernelDebug2ConeMaxs, Left, 255, 0, 0, 50);
		EmitOverlayBoxDirection(GNpcKernelDebug2BoxDirection, EyeUnits, GNpcKernelDebug2ConeMins,
			GNpcKernelDebug2ConeMaxs, RightDir, 255, 0, 0, 50);
		EmitOverlayBoxDirection(GNpcKernelDebug2BoxDirection, EyeUnits, GNpcKernelDebug2ConeMins,
			GNpcKernelDebug2ConeMaxs, Eye, 0, 255, 0, 50);
		EmitOverlayBoxDirection(GNpcKernelDebug2BoxDirection, EyeUnits,
			GNpcKernelDebug2ConeEyeMins, GNpcKernelDebug2ConeEyeMaxs, Eye, 0, 255, 0, 50);
	}

	// --- Arm 7, `0x20000` — the enemy-memory labels ------------------------------------------------
	if ((DebugOverlays & GNpcKernelDebug2BitMemory) != 0)
	{
		DrawEnemyMemoryOverlays();
	}

	// --- Arm 8, `0x200000` — the enemy and target lines --------------------------------------------
	//
	//     if (GetEnemy())      Line(EyePosition(), GetEnemy()->EyePosition(),  255, 0, 0, true, 0)
	//     if (m_hTargetEnt)    Line(EyePosition(), target->EyePosition(),      0, 0, 255, true, 0)
	//
	// Both lines are `noDepthTest = true`. The enemy test re-dispatches `GetEnemy()` a second time
	// for the draw, which is why a body that changes enemies mid-call would draw to the new one.
	if ((DebugOverlays & GNpcKernelDebug2BitEnemyLines) != 0)
	{
		const FVector EyeUnits = EyePosition() / ElysiumMove::U;
		if (GetEnemy() != nullptr)
		{
			const FElysiumEntity* Enemy = GetEnemy();
			EmitOverlayLine(GNpcKernelDebug2Line, EyeUnits, Enemy->EyePosition() / ElysiumMove::U,
				255, 0, 0, true);
		}
		const FElysiumEntity* TargetEntity =
			World != nullptr ? World->Resolve(GetTarget()) : nullptr;
		if (TargetEntity != nullptr)
		{
			EmitOverlayLine(GNpcKernelDebug2Line, EyeUnits, TargetEntity->EyePosition() / ElysiumMove::U,
				0, 0, 255, true);
		}
	}

	// --- The two tail calls, in this order ---------------------------------------------------------
	DrawPathfinderDebugOverlays(DebugOverlays);
	EntityDrawDebugGeometryOverlays();
}

bool FElysiumNpcBase::IsBaseNpcTroika() const
{
	// `CBaseEntity+0x0098 m_pBaseNPCTroika`, the cached downcast the view-cone arm tests. This
	// runtime stands ONE leaf and it IS the Troika line — `GetSchedulingErrorName` answers
	// `"CAI_BaseNPCTroika"` for every classname — so the pointer is never null and the answer is
	// always true. Declared as a question rather than folded into the caller so the recovered gate
	// is visible and a test can assert it.
	return true;
}

void FElysiumNpcBase::DrawEnemyMemoryOverlays()
{
	// `0x10275760`'s `0x20000` arm, the longest in the body. It walks `GetEnemies()`'s record list
	// (`+0x0c` head, `+0x38` next) and for each record:
	//
	//   1. resolve `record->+0x24` (the remembered entity); skip an unresolved handle;
	//   2. `npc = entity->+0x9c`  (`m_pCombatCharacter`); skip a record whose entity is not one;
	//   3. build a label, by three exclusive tests in THIS order:
	//        npc == GetEnemy()                 -> "Current Enemy"
	//        npc == resolve(m_hTargetEnt)      -> "Current Target"
	//        otherwise                         -> "Other Memory"
	//   4. append, independently and in this order:
	//        IsUnreachable(npc)                -> " (Unreachable)"
	//        record->+0x35 (eluded)            -> " (Eluded)"
	//   5. pick the colour, by a SECOND ladder that is NOT the label's — `IsUnreachable` is asked a
	//      second time and wins outright, then eluded, then the same enemy/target/other three:
	//        unreachable -> (  0, 255,   0)      eluded -> (  0,   0, 255)
	//        enemy       -> (255,   0,   0)      target -> (255,   0, 255)
	//        other       -> (255, 100, 100)
	//   6. `NDebugOverlay::Text(record->+0x0c, label, false, 0)` at the remembered position;
	//   7. then ONE of two shapes:
	//        the remembered entity is the PLAYER (`npc->+0xa8 m_pPlayer`) AND the remembered
	//        position is within 10.0 units of its current origin  -> a two-line cross;
	//        otherwise -> the hull box at the remembered position, drawn TWICE with identical
	//        arguments. The duplication is retail's and is reproduced.
	//
	// This runtime's `FElysiumNpcEnemyMemory` IS `CAI_Memory`'s record list, so the walk is real.
	// `LastPosition` is the record's `+0x0c` and `bEluded` its `+0x35`.
	const FElysiumEntity* Enemy = GetEnemy();
	const FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(GetTarget()) : nullptr;
	const FElysiumEntity* Player =
		World != nullptr ? static_cast<const FElysiumEntity*>(World->FindPlayer()) : nullptr;

	for (const FElysiumNpcEnemyMemoryRecord& Record : EnemyMemory.Records())
	{
		FElysiumEntity* Remembered = World != nullptr ? World->Resolve(Record.Handle) : nullptr;
		if (Remembered == nullptr || Remembered->AsCombatCharacter() == nullptr)
		{
			continue;
		}

		FString Label;
		const bool bIsEnemy = Remembered == Enemy;
		const bool bIsTarget = !bIsEnemy && Remembered == TargetEntity;
		if (bIsEnemy)
		{
			Label = TEXT("Current Enemy");
		}
		else if (bIsTarget)
		{
			Label = TEXT("Current Target");
		}
		else
		{
			Label = TEXT("Other Memory");
		}

		const bool bUnreachable = IsUnreachable(Remembered);
		if (bUnreachable)
		{
			Label += TEXT(" (Unreachable)");
		}
		if (Record.bEluded)
		{
			Label += TEXT(" (Eluded)");
		}

		int32 R = 255;
		int32 G = 100;
		int32 B = 100;
		if (bUnreachable)
		{
			R = 0;
			G = 255;
			B = 0;
		}
		else if (Record.bEluded)
		{
			R = 0;
			G = 0;
			B = 255;
		}
		else if (bIsEnemy)
		{
			R = 255;
			G = 0;
			B = 0;
		}
		else if (bIsTarget)
		{
			R = 255;
			G = 0;
			B = 255;
		}

		const FVector RememberedUnits = Record.LastPosition / ElysiumMove::U;
		EmitOverlayText(GNpcKernelDebug2Text, RememberedUnits, Label);

		const bool bIsPlayer = Player != nullptr && Remembered == Player;
		const float DistUnits = static_cast<float>(
			(Record.LastPosition - Remembered->Origin).Size() / ElysiumMove::U);
		if (bIsPlayer && DistUnits < GNpcKernelDebug2CrossRangeUnits)
		{
			// The cross: the remembered entity's basis (slot 368 `GetVectors`) scaled by 10.0 and
			// added to its `EyePosition()`, then two lines through that point whose arms are
			// `(2*up.z - right.z*0.0, right.z*0.0 - 2*up.x, up.x*0.0 - up.z*0.0)` — the listing's
			// own expression, in which `_DAT_104454c4` is 0.0 and therefore zeroes two of the three
			// terms outright. The `_DAT_10452dc4` = 2.0 is added to and subtracted from the Z of the
			// two endpoints. **SEAM**: no basis is available for a remembered entity here, so the
			// arms collapse to the Z spread alone and the cross degenerates to a vertical tick —
			// which is what a zero basis gives retail's own arithmetic.
			const FVector Centre = Remembered->EyePosition() / ElysiumMove::U;
			EmitOverlayLine(GNpcKernelDebug2Line,
				Centre - FVector(0.f, 0.f, GNpcKernelDebug2CrossZSpread),
				Centre + FVector(0.f, 0.f, GNpcKernelDebug2CrossZSpread), R, G, B, false);
			EmitOverlayLine(GNpcKernelDebug2Line,
				Centre - FVector(0.f, 0.f, GNpcKernelDebug2CrossZSpread),
				Centre + FVector(0.f, 0.f, GNpcKernelDebug2CrossZSpread), R, G, B, false);
			EmitOverlayText(GNpcKernelDebug2Text, Centre, Label);
			continue;
		}

		// The hull box, twice. `NAI_Hull::Mins(m_eHull)` / `Maxs(m_eHull)` read off the REMEMBERED
		// NPC's hull (`+0x1568`), not this one's.
		FVector HullMins = FVector::ZeroVector;
		FVector HullMaxs = FVector::ZeroVector;
		const FElysiumNpc* RememberedNpc = Remembered->AsNpc();
		RetailHullExtents(RememberedNpc != nullptr ? RememberedNpc->HullKind : 0, EElysiumHullExtents::Full, HullMins,
			HullMaxs);
		EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, RememberedUnits, HullMins, HullMaxs, R, G, B, 0);
		EmitOverlayBox(NpcKernelDebug2Shared::GNpcKernelDebug2Box, RememberedUnits, HullMins, HullMaxs, R, G, B, 0);
	}
}

// -------------------------------------------------------------------------------------------------
// `CAI_Hint::DrawDebugTextOverlays` — `0x102d1600`, slot 124 on `CAI_Hint`.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpcBase::HintDrawDebugTextOverlays(int32 EntityTextLine, int32 DebugOverlayBits,
	int32 HintType, double NextUseTime, double Now)
{
	// `0x102d1600`. `CAI_Hint` is not this leaf and family Hints records that no hint-node store
	// carries hint types, so the hint's own two words are parameters rather than reads — the body
	// is the recovered thing, and it is exercised by handing it the two words.
	//
	// Two lines, both white and opaque:
	//   1. `Q_snprintf(buf, 512, "%i", m_nHintType)` — the FORMAT IS BARE `"%i"` (`0x1057ae88`),
	//      with no label at all. SDK 2013 prints `GetHintTypeDescription(HintType())` here; Troika's
	//      build prints the number.
	//   2. `delay = MAX(m_flNextUseTime - curtime, 0.0)` then `"delay %f"` (`0x1060a508`). The floor
	//      is `_DAT_104454c4` = 0.0 and the compare is `<`, so an already-expired hint reads
	//      `delay 0.000000`.
	//
	// Returns the base line plus two when the `0x1` bit is set, and the base unchanged otherwise.
	if ((DebugOverlayBits & GNpcKernelDebug2BitText) == 0)
	{
		return EntityTextLine;
	}
	EmitEntityText(EntityTextLine, TEXT("%i"), FString::Printf(TEXT("%i"), HintType));
	const double Delay = FMath::Max(NextUseTime - Now, static_cast<double>(GNpcKernelDebug2Zero));
	EmitEntityText(EntityTextLine + 1, TEXT("delay %f"),
		FString::Printf(TEXT("delay %f"), Delay));
	return EntityTextLine + 2;
}
