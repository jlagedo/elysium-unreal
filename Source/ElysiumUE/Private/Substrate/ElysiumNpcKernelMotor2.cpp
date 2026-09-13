#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"

// Story 29c-1, family **Motor**, second half — the species jump chain and the two position probes.
//
// `ElysiumNpcKernelMotor.cpp` carries the seams, the slot fills and the yaw ladder; this file
// carries the bodies that hang off them: `CNPC_VChangBros`'s jump-attack gate and super jump,
// `CNPC_VSabbatLeader`'s stuck push, no-jump zone and lead-jump velocity, `CNPC_VAsianVampire`'s
// jump ring / stationary watchdog / jumpbase search, `CNPC_VSheriffMan`'s jump setup and
// `CNPC_VWerewolf`'s ground snap. Split because the family passed ~1,500 lines, per the brief.
//
// The same two conventions hold as in the first half: every body works in SOURCE units, and every
// missing input is a named seam that answers nothing. The constants are re-declared here rather than
// exported, because a constant is only useful beside the body that cites its address.

namespace
{
	constexpr float GChangJumpCooldown = 16.0f;     // _DAT_104ada00
	constexpr float GSuperJumpNearRise = 50.0f;     // _DAT_104ada1c
	constexpr float GSuperJumpFarRise = 150.0f;     // _DAT_104ada20
	constexpr float GSuperJumpSplit = 40.0f;        // _DAT_104ada3c
	constexpr float GMotorTailAsianJumpRise = 100.0f;        // _DAT_104a9310
	constexpr float GMotorTailSheriffJumpRise = 400.0f;      // _DAT_104c614c
	constexpr float GAsianJumpScheduleDrop = 40.0f; // _DAT_104a9314
	constexpr float GAsianJumpNear = 30.0f;         // _DAT_104a9308
	constexpr float GAsianStationaryTime = 3.0f;    // _DAT_104a9318
	constexpr float GAsianMovedEpsilon = 40.0f;     // _DAT_104a931c
	constexpr float GJumpbaseClearance = 150.0f;    // DAT_104a9320
	constexpr float GSabbatLeadScale = 25.0f;       // DAT_104c3cb8
	constexpr float GNoJumpZoneDistance = 100.0f;   // DAT_104c3ccc
	constexpr float GGroundpointDrop = 1000.0f;     // _DAT_10447ee0 / -_DAT_104d00ac
	// `vec3_invalid` — `DAT_10713de0/de4/de8`, which `staticinit_101371a0` fills with `0x7f7fffff`.
	// `GetGroundpoint`'s no-hit answer, and `CNPC_VWerewolf::GetForwardYawForHint`'s sentinel.
	constexpr float GVecInvalid = 3.4028234663852886e+38f;
	constexpr float GMotorTailTraceClearFraction = 1.0f;     // _DAT_10449280
	constexpr int32 GMotorTailGroundTraceMask = 0x202400b;
	constexpr int32 GJumpbaseHintType = 18000;
	constexpr int32 GNoJumpHintType = 0x3e84;
	constexpr float GStuckDegenerate = 9.999999974752427e-07f;  // _DAT_104c3d14
	constexpr float GStuckHalf = 0.5f;                          // _DAT_104454d0
	constexpr float GStuckOne = 1.0f;                           // _DAT_104454c0
	constexpr float GStuckLift = 5.0f;                          // _DAT_10454110
	constexpr int32 GActSuperJump = 0x7b;
	constexpr int32 GSchedJumpDown = 0x15b;
	constexpr int32 GSchedJumpAcross = 0x15a;

	// Retail's `sqrt`, `PTR_thunk_FUN_101371d0`.
	float Length2D(const FVector& A)
	{
		return FMath::Sqrt(static_cast<float>(A.X * A.X + A.Y * A.Y));
	}

	float Length3D(const FVector& A)
	{
		return FMath::Sqrt(static_cast<float>(A.X * A.X + A.Y * A.Y + A.Z * A.Z));
	}

	// This world's centimetres into retail's Source units, and back. Stated once per file, for the
	// reason given at the top of `ElysiumNpcKernelMotor.cpp`.
	FVector MotorTailSourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}

	FVector PortOf(const FVector& Units)
	{
		return FVector(Units.X * ElysiumMove::U, -Units.Y * ElysiumMove::U,
			Units.Z * ElysiumMove::U);
	}
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VChangBros` — the jump-attack gate and the super jump.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::CheckForJumpAttack()
{
	// `CNPC_VChangBros::CheckForJumpAttack` `0x1036c8d0`, arm for arm:
	//
	//     if (m_ChangType != 0) return false;
	//     CBaseEntity* p = m_hClosestPlayer;  if (!p) return false;
	//     if (GetSector(p->GetAbsOrigin()) == 4) return false;
	//     if (GetSector(GetAbsOrigin())  == 4) return false;
	//     if (curtime - m_fLastJumpTime < 16.0) return false;             // _DAT_104ada00
	//     if (m_iSquadDisconnected > 0 || !m_pSquad) return true;
	//     for (i = 0; i < squad->NumMembers(); ++i) {
	//         CNPC_VChangBros* b = dynamic_cast<…>(squad->GetMember(i));
	//         if (b && b != this && curtime - b->m_fLastJumpTime < 16.0) return false;
	//     }
	//     return true;
	//
	// Note the shape of the sector test: BOTH the player and this NPC must be out of sector 4, and
	// the second read is of this NPC's own origin, not the player's.
	if (ChangType != 0)
	{
		return false;
	}
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return false;
	}
	if (ChangBrosSector(MotorTailSourceOf(Player->Origin)) == 4)
	{
		return false;
	}
	if (ChangBrosSector(MotorTailSourceOf(Origin)) == 4)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now - LastJumpTime < GChangJumpCooldown)
	{
		return false;
	}
	// The squad walk. `ConnectedSquad()` is the Squad family's seam and answers null, so a
	// disconnected or squadless brother is the arm every ChangBros takes today — which is the arm
	// that ALLOWS the jump, not the one that refuses it.
	if (ScheduleHost.SquadDisconnected > 0 || ConnectedSquad() == nullptr)
	{
		return true;
	}
	const int32 Count = SquadMemberCount(ConnectedSquad());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FElysiumEntity* Member = SquadMember(ConnectedSquad(), Index);
		if (Member == nullptr)
		{
			continue;
		}
		FElysiumNpc* Brother = Member->AsNpc();
		// `___RTDynamicCast(member, 0, CNPC_VChangBros)` — a sibling of the same retail class only.
		if (Brother == nullptr || Brother == this || !Brother->IsRetailClass(TEXT("CNPC_VChangBros")))
		{
			continue;
		}
		if (Now - Brother->LastJumpTime < GChangJumpCooldown)
		{
			return false;
		}
	}
	return true;
}

void FElysiumNpc::SetupSuperJump(float Enabled)
{
	// `CNPC_VChangBros::SetupSuperJump` `0x1036e160`:
	//
	//     if (param_1 == 0.0) return;
	//     m_vJumpOrigin = GetAbsOrigin();
	//     m_vJumpTarget = m_pHintNode->GetAbsOrigin();
	//     float top = m_pHintNode->GetAbsOrigin().z;
	//     if (top < GetAbsOrigin().z) top = GetAbsOrigin().z;      // the HIGHER of the two
	//     float dz = top - GetAbsOrigin().z;                       // >= 0 by construction
	//     m_fJumpHeight = (ABS(dz) < 40.0 ? 50.0 : 150.0) + dz;    // _DAT_104ada3c/1c/20
	//     ClearCondition(0x7b);                                    // 0x10269b50
	//     thunk_FUN_102c4e80(this);
	//
	// The ledger's one-line walk reads `thunk_FUN_10269b50` as "plays activity 0x7b"; it is
	// `ClearCondition`, and 0x7b is a species-registered condition number, not an activity.
	if (Enabled == 0.0f)
	{
		return;
	}
	const FVector SelfUnits = MotorTailSourceOf(Origin);
	FVector HintUnits = FVector::ZeroVector;
	if (!NavHintNodeOrigin(ScheduleHost.HintNode, HintUnits))
	{
		// **SEAM**: the hint store carries no origins, so retail's two reads of
		// `m_pHintNode->GetAbsOrigin()` cannot be made. Retail would crash on a null hint here — it
		// dereferences `m_pHintNode` without a check — so the port refuses instead, and says so.
		return;
	}
	JumpOrigin = SelfUnits;
	JumpTarget = HintUnits;
	float Top = static_cast<float>(HintUnits.Z);
	if (Top < static_cast<float>(SelfUnits.Z))
	{
		Top = static_cast<float>(SelfUnits.Z);
	}
	const float DeltaZ = Top - static_cast<float>(SelfUnits.Z);
	const float Rise = FMath::Abs(DeltaZ) < GSuperJumpSplit ? GSuperJumpNearRise : GSuperJumpFarRise;
	JumpHeight = Rise + DeltaZ;
	Cognition.Conditions.Clear(static_cast<EElysiumNpcCond>(GActSuperJump));
	CommitSetupJump();
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VAsianVampire` / `CNPC_VSheriffMan` — the ordinary jump setup and its bookkeeping.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetupJump(float Enabled)
{
	// `CNPC_VAsianVampire::SetupJump` `0x10361a70` and `CNPC_VSheriffMan::SetupJump` `0x103b1300`
	// are the same 284 bytes apart from their rise constant (`_DAT_104a9310` = 100.0 and
	// `_DAT_104c614c` = 400.0), so this is one body plus a two-row species table:
	//
	//     if (param_1 == 0.0) return;
	//     m_vJumpOrigin = GetAbsOrigin();
	//     m_vJumpTarget = m_pHintNode->GetAbsOrigin();
	//     float top = max(m_pHintNode->GetAbsOrigin().z, GetAbsOrigin().z);
	//     m_fJumpHeight = rise + (top - GetAbsOrigin().z);
	//     thunk_FUN_102c4e80(this);
	//
	// Unlike `SetupSuperJump` there is no `ABS`/split arm and no condition clear.
	if (Enabled == 0.0f)
	{
		return;
	}
	float Rise = GMotorTailAsianJumpRise;
	if (IsRetailClass(TEXT("CNPC_VSheriffMan")))
	{
		const FSetupJumpSpecies* Row = SetupJumpSpeciesOf(TEXT("CNPC_VSheriffMan"));
		Rise = Row != nullptr ? Row->Rise : GMotorTailSheriffJumpRise;
	}
	const FVector SelfUnits = MotorTailSourceOf(Origin);
	FVector HintUnits = FVector::ZeroVector;
	if (!NavHintNodeOrigin(ScheduleHost.HintNode, HintUnits))
	{
		// **SEAM**, as in `SetupSuperJump`: no hint origins.
		return;
	}
	JumpOrigin = SelfUnits;
	JumpTarget = HintUnits;
	float Top = static_cast<float>(HintUnits.Z);
	if (Top < static_cast<float>(SelfUnits.Z))
	{
		Top = static_cast<float>(SelfUnits.Z);
	}
	JumpHeight = Rise + (Top - static_cast<float>(SelfUnits.Z));
	CommitSetupJump();
}

int32 FElysiumNpc::GetJumpSchedule() const
{
	// `CNPC_VAsianVampire::GetJumpSchedule` `0x10362430`:
	//     if (m_hClosestPlayer resolves
	//         && player->GetAbsOrigin().z - GetAbsOrigin().z < -40.0)   // -_DAT_104a9314
	//         return 0x15b;
	//     return 0x15a;
	// The threshold is the NEGATED constant: the player has to be more than 40 units BELOW this NPC.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player != nullptr && !Player->IsInert()
		&& Player->Handle == Senses.Memory.ClosestPlayer)
	{
		const float DeltaZ = static_cast<float>(MotorTailSourceOf(Player->Origin).Z - MotorTailSourceOf(Origin).Z);
		if (DeltaZ < -GAsianJumpScheduleDrop)
		{
			return GSchedJumpDown;
		}
	}
	return GSchedJumpAcross;
}

bool FElysiumNpc::IsPosNearStoredJumpPositions(const FVector& PositionUnits) const
{
	// `CNPC_VAsianVampire::IsPosNearStoredJumpPositions` `0x103618a0`: a fixed two-iteration loop
	// over `m_vLastJumpPosition` (+0x66b8), each entry tested by 2-D distance against
	// `_DAT_104a9308` = 30.0. The comparison is `dist < 30.0`, not `<=`.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Delta(LastJumpPosition[Index].X - PositionUnits.X,
			LastJumpPosition[Index].Y - PositionUnits.Y, 0.0);
		if (Length2D(Delta) < GAsianJumpNear)
		{
			return true;
		}
	}
	return false;
}

bool FElysiumNpc::StationaryForTooLong() const
{
	// `CNPC_VAsianVampire::StationaryForTooLong` `0x10362670`:
	//     return 3.0 <= curtime - m_fMovedTimeStamp;     // _DAT_104a9318, inclusive
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return GAsianStationaryTime <= Now - MovedTimeStamp;
}

void FElysiumNpc::UpdateMovedTimeStamp()
{
	// `CNPC_VAsianVampire::UpdateMovedTimeStamp` `0x10362540`:
	//     if (40.0 < Length(GetAbsOrigin() - m_vMovedPosition)) {    // _DAT_104a931c, exclusive
	//         m_fMovedTimeStamp = curtime;
	//         m_vMovedPosition  = GetAbsOrigin();
	//     }
	// The distance is 3-D, and the write order is stamp first, position second — which matters
	// because retail re-reads `GetAbsOrigin()` for the second write.
	const FVector SelfUnits = MotorTailSourceOf(Origin);
	const FVector Delta = SelfUnits - MovedPosition;
	if (GAsianMovedEpsilon < Length3D(Delta))
	{
		MovedTimeStamp = World != nullptr ? World->NowSeconds() : 0.0;
		MovedPosition = SelfUnits;
	}
}

int32 FElysiumNpc::SelectJumpbaseNode()
{
	// `CNPC_VAsianVampire::SelectJumpbaseNode` `0x10361730`:
	//     best = NULL;  bestDist = FLT_MAX;
	//     for (hint = g_pHintList; hint; hint = hint->+0x5d8)
	//         if (hint->m_nHintType == 18000
	//             && PositionClearForTeleport(hint->GetAbsOrigin(), 150.0))      // DAT_104a9320
	//         {
	//             float d = Length(GetAbsOrigin() - hint->GetAbsOrigin());        // 3-D
	//             if (d < bestDist) { bestDist = d; best = hint; }
	//         }
	//     if (best) AddHintToStoredJumpPositions(best);
	//     return best;
	//
	// Identical in shape to `SelectLedgeNode`, which filters hint type 0x4653 instead.
	// **SEAM**: the global hint list answers empty, so the search finds nothing — which is retail's
	// own answer for a map with no `jumpbase` hints.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	TArray<int32> Hints;
	NavAllHintNodes(Hints);
	const FVector SelfUnits = MotorTailSourceOf(Origin);
	for (int32 Hint : Hints)
	{
		int32 Type = 0;
		if (!NavHintNodeType(Hint, Type) || Type != GJumpbaseHintType)
		{
			continue;
		}
		FVector HintUnits = FVector::ZeroVector;
		if (!NavHintNodeOrigin(Hint, HintUnits))
		{
			continue;
		}
		if (!PositionClearForTeleport(HintUnits, GJumpbaseClearance))
		{
			continue;
		}
		const float Distance = Length3D(SelfUnits - HintUnits);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Hint;
		}
	}
	if (Best != INDEX_NONE)
	{
		AddHintToStoredJumpPositions(Best);
	}
	return Best;
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VSabbatLeader` — the lead jump, the no-jump zone and the stuck push.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::SetJumpVelocityTowardPlayer()
{
	// `CNPC_VSabbatLeader::SetJumpVelocityTowardPlayer` `0x103aad40`:
	//
	//     if (!m_hClosestPlayer resolves) return;
	//     Vector v = GetAbsVelocity();                              // vtable +0x318
	//     Vector lead;
	//     lead.x = 25.0 * (player.x - self.x);                      // DAT_104c3cb8
	//     lead.y = (player.y - self.y) * 25.0;
	//     lead.z = 25.0 * 0.0;                                      // the Z term is scaled from ZERO
	//     Vector to = player->GetAbsOrigin() - lead;
	//     thunk_FUN_102c4cc0(this, &v, GetAbsOrigin(), &to);        // the jump-arc solver
	//     SetAbsVelocity(v);
	//
	// Two facts the summary loses and that are kept here: the lead is built from the POSITION delta
	// between the two bodies (not from the player's velocity), and its Z term is a constant times
	// zero, so the lead is flat. The solver is a seam, so the velocity write is what it was.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	const FVector SelfUnits = MotorTailSourceOf(Origin);
	const FVector PlayerUnits = MotorTailSourceOf(Player->Origin);
	const FVector Lead(GSabbatLeadScale * (PlayerUnits.X - SelfUnits.X),
		(PlayerUnits.Y - SelfUnits.Y) * GSabbatLeadScale, GSabbatLeadScale * 0.0);
	const FVector ToUnits = PlayerUnits - Lead;
	FVector VelocityUnits = MotorTailSourceOf(Velocity);
	if (!SolveJumpArc(SelfUnits, ToUnits, VelocityUnits))
	{
		// **SEAM**: with no solver retail's out-parameter is untouched, and it then assigns that
		// untouched value — the body's CURRENT velocity — straight back. Reproduced exactly: the
		// assignment happens and changes nothing.
		return;
	}
	Velocity = PortOf(VelocityUnits);
}

bool FElysiumNpc::PlayerInNoJumpZone() const
{
	// `CNPC_VSabbatLeader::PlayerInNoJumpZone` `0x103a9e70`:
	//     if (!m_hClosestPlayer resolves) return false;
	//     for (hint = g_pHintList; hint; hint = hint->+0x5d8) {
	//         if (hint->m_nHintType != 0x3e84) continue;
	//         if (DistToHintCenterLine2D_2(hint, player->GetAbsOrigin()) < 100.0      // DAT_104c3ccc
	//          && DistToHintCenterLine2D_2(hint, GetAbsOrigin())         < 100.0)
	//             return true;
	//     }
	//     return false;
	// BOTH bodies have to be inside the SAME hint's line, which is why the second test is nested.
	// **SEAM**: the hint list answers empty, so nobody is ever in a no-jump zone.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return false;
	}
	TArray<int32> Hints;
	NavAllHintNodes(Hints);
	const FVector PlayerUnits = MotorTailSourceOf(Player->Origin);
	const FVector SelfUnits = MotorTailSourceOf(Origin);
	for (int32 Hint : Hints)
	{
		int32 Type = 0;
		if (!NavHintNodeType(Hint, Type) || Type != GNoJumpHintType)
		{
			continue;
		}
		float PlayerDistance = 0.f;
		if (!DistToHintCenterLine2DSqr(Hint, PlayerUnits, PlayerDistance)
			|| !(PlayerDistance < GNoJumpZoneDistance))
		{
			continue;
		}
		float SelfDistance = 0.f;
		if (DistToHintCenterLine2DSqr(Hint, SelfUnits, SelfDistance)
			&& SelfDistance < GNoJumpZoneDistance)
		{
			return true;
		}
	}
	return false;
}

void FElysiumNpc::CheckStuck()
{
	// `CNPC_VSabbatLeader::CheckStuck` `0x103ab580`, read from the listing because the decompiler
	// aliased the two entities' collision extents onto one pair of registers.
	//
	//     CBaseEntity* p = m_hClosestPlayer;  if (!p) return;
	//     Vector pMins = p->m_Collision.OBBMins(), pMaxs = p->m_Collision.OBBMaxs();
	//     float pRadius = Length2D(pMaxs - pMins) * 0.5;                 // _DAT_104454d0
	//     float pTop    = p->GetAbsOrigin().z + pMaxs.z;
	//     float pBottom = p->GetAbsOrigin().z + pMins.z;
	//     Vector pCentre = p->GetAbsOrigin() + (pMins + pMaxs) * 0.5;
	//     Vector sMins = m_Collision.OBBMins(), sMaxs = m_Collision.OBBMaxs();
	//     float sRadius = Length2D(sMaxs - sMins) * 0.5;
	//     float sTop    = GetAbsOrigin().z + pMaxs.z;      // <- the PLAYER's maxs (see below)
	//     float sBottom = GetAbsOrigin().z + pMins.z;      // <- the PLAYER's mins
	//     Vector sCentre = GetAbsOrigin() + (sMins + sMaxs) * 0.5;
	//     if (!(sBottom <= pTop && pBottom <= sTop)) return;             // the Z-span overlap
	//     Vector d = sCentre - pCentre;  d.z = 0;
	//     float len = Length(d);
	//     float sum = sRadius + pRadius;
	//     if (!(len < sum)) return;
	//     if (len < 1e-6) d.x = 1.0;                                     // _DAT_104c3d14
	//     float scale = sum + 1.0;                                       // _DAT_104454c0
	//     Vector push = d * scale;
	//     Vector out = pCentre + push;
	//     out.z = pCentre.z + push.z + 5.0;                              // _DAT_10454110
	//     if (out.z < sBottom) out.z = sBottom;
	//     SetAbsOrigin(out);                                             // vtable +0x360
	//
	// **RETAIL BUG, reproduced.** `sTop` and `sBottom` are built from SELF's origin and the PLAYER's
	// box extents — the listing loads `[EBP+8]` and `[EBX+8]`, which still hold the player's maxs and
	// mins, after self's own pair has been read into `EDI` / `[ESP+0x14]`. A shipped program was
	// tuned against that, so it is kept; the two Z spans agree only when the two bodies use the same
	// hull. Note also that `push` is NOT normalised: its magnitude is `len * (sum + 1)`.
	//
	// **SEAM**: `RetailCollisionExtents` answers nothing, so the body refuses before it boxes
	// anything. The whole computation is written out so it stands the day an extent source exists.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player == nullptr || Player->IsInert() || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	FVector PlayerMins = FVector::ZeroVector;
	FVector PlayerMaxs = FVector::ZeroVector;
	if (!RetailCollisionExtents(*Player, PlayerMins, PlayerMaxs))
	{
		return;
	}
	FVector SelfMins = FVector::ZeroVector;
	FVector SelfMaxs = FVector::ZeroVector;
	if (!RetailCollisionExtents(*this, SelfMins, SelfMaxs))
	{
		return;
	}
	const FVector PlayerUnits = MotorTailSourceOf(Player->Origin);
	const FVector SelfUnits = MotorTailSourceOf(Origin);

	const float PlayerRadius = Length2D(PlayerMaxs - PlayerMins) * GStuckHalf;
	const float PlayerTop = static_cast<float>(PlayerUnits.Z + PlayerMaxs.Z);
	const float PlayerBottom = static_cast<float>(PlayerUnits.Z + PlayerMins.Z);
	const FVector PlayerCentre = PlayerUnits + (PlayerMins + PlayerMaxs) * GStuckHalf;

	const float SelfRadius = Length2D(SelfMaxs - SelfMins) * GStuckHalf;
	// The two lines the bug lives on — the PLAYER's extents against SELF's origin.
	const float SelfTop = static_cast<float>(SelfUnits.Z + PlayerMaxs.Z);
	const float SelfBottom = static_cast<float>(SelfUnits.Z + PlayerMins.Z);
	const FVector SelfCentre = SelfUnits + (SelfMins + SelfMaxs) * GStuckHalf;

	if (!(SelfBottom <= PlayerTop && PlayerBottom <= SelfTop))
	{
		return;
	}
	FVector Delta = SelfCentre - PlayerCentre;
	Delta.Z = 0.0;
	const float Separation = Length3D(Delta);
	const float SumRadius = SelfRadius + PlayerRadius;
	if (!(Separation < SumRadius))
	{
		return;
	}
	if (Separation < GStuckDegenerate)
	{
		Delta.X = 1.0;
	}
	const float Scale = SumRadius + GStuckOne;
	const FVector Push = Delta * Scale;
	FVector Out = PlayerCentre + Push;
	Out.Z = PlayerCentre.Z + Push.Z + GStuckLift;
	if (static_cast<float>(Out.Z) < SelfBottom)
	{
		Out.Z = SelfBottom;
	}
	Origin = PortOf(Out);
}

// -------------------------------------------------------------------------------------------------
// `CNPC_VWerewolf` — the ground snap.
// -------------------------------------------------------------------------------------------------

FVector FElysiumNpc::GetGroundpoint(const FVector& PointUnits) const
{
	// `CNPC_VWerewolf::GetGroundpoint` `0x103d6a40`:
	//
	//     Vector mins = HullMins(m_eHull), maxs = HullMaxs(m_eHull);
	//     Vector start = point, end = Vector(point.x, point.y, point.z - 1000.0);   // _DAT_10447ee0
	//     Ray_t ray(start, end, mins, maxs);
	//     TraceRay(ray, 0x202400b, filter(this, 7), &tr);
	//     if (tr.fraction < 1.0)                                                    // _DAT_10449280
	//         return Vector(start.x + tr.fraction * 0.0,                            // _DAT_104454c4
	//                       start.y + tr.fraction * 0.0,
	//                       start.z + tr.fraction * -1000.0);                       // _DAT_104d00ac
	//     return Vector(DAT_10713de0, DAT_10713de4, DAT_10713de8);
	//
	// The two zero terms are the trace delta's X and Y, which the compiler folded to constants
	// because the ray is straight down; the Z term is the drop, negated.
	//
	// **The fallback triple is `vec3_invalid`, not `vec3_origin`.** `staticinit_101371a0` writes
	// `0x7f7fffff` — `FLT_MAX` — into all three, and its only other reader is
	// `CNPC_VWerewolf::GetForwardYawForHint` (`0x103d7210`), which uses it as a sentinel. A no-hit
	// ground snap therefore answers "there is no ground point", and a caller must test for it.
	//
	// **SEAM**: no hull table and no trace, so the body lands on that no-hit arm every time. That is
	// a REAL answer of retail's, not a port refusal, and the caller must treat it the way retail's
	// caller does.
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, Mins, Maxs);
	const FVector EndUnits(PointUnits.X, PointUnits.Y, PointUnits.Z - GGroundpointDrop);
	FKernelHullTrace Trace;
	if (KernelHullTrace(PointUnits, EndUnits, Mins, Maxs, GMotorTailGroundTraceMask, Trace)
		&& Trace.Fraction < GMotorTailTraceClearFraction)
	{
		return FVector(PointUnits.X, PointUnits.Y,
			PointUnits.Z + Trace.Fraction * -GGroundpointDrop);
	}
	return FVector(GVecInvalid, GVecInvalid, GVecInvalid);
}
