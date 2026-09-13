#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Positions** — the node selectors, the teleport clearance rules and the
// Chang brothers' arena. The rest of the family (the trace bodies, slot 563, the Werewolf teleport
// pair) is `Substrate/ElysiumNpcKernelPositions2.cpp`; the declarations and the family's standing
// facts are `Substrate/ElysiumNpcKernelPositions.inl`; the walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address (image base `0x10000000`; `.rdata`'s raw offset equals its RVA in this image, so the
// address IS the file offset), so the numbers are recovered facts and not estimates. Where an
// address is quoted with no number beside it, the datum lives past `.data`'s raw size and is filled
// at runtime — those are named as seams, never guessed.

namespace
{
	// Retail's `.rdata`, one line per constant, in Source units unless noted.
	constexpr float RetailOne = 1.0f;              // _DAT_104454c0
	constexpr float RetailZero = 0.0f;             // _DAT_104454c4
	constexpr float NormalizeEpsilon = 1.1920928955078125e-07f;   // _DAT_1046a51c
	constexpr float DegreesPerTurnRecip = 0.0055555556900799274f; // _DAT_104c6d40, 1/180

	// `CNPC_VChangBros::GetSector` `0x1036e580`.
	constexpr float SectorZThreshold = 200.0f;     // _DAT_104ada6c
	constexpr float SectorOneDistSq = 270.0f;      // _DAT_104ada60 — compared to a SQUARED distance
	constexpr float SectorTwoRadius = 370.0f;      // _DAT_104ada64, squared by staticinit 0x1036e500
	constexpr float SectorThreeRadius = 525.0f;    // _DAT_104ada68, squared by staticinit 0x1036e550

	// `CNPC_VChangBros::GetTeleportPosition` `0x1036d270`.
	constexpr float ChangTeleportPositionMaxAge = 3.0f;   // _DAT_104ada04, seconds

	// The four `PositionClearForTeleport` last-teleport floors, one per species.
	constexpr float AndreiLastTeleportFloor = 100.0f;     // DAT_104a6f7c
	constexpr float ChangLastTeleportFloor = 100.0f;      // DAT_104ad9f4
	constexpr float SheriffLastTeleportFloor = 100.0f;    // DAT_104c6124

	// `CNPC_VAsianVampire::SelectLedgeNode` `0x103615c0`'s clearance.
	constexpr float AsianLedgeClearance = 150.0f;         // DAT_104a9320

	// `CNPC_VSheriffMan::SelectTeleportNode` `0x103b0630`.
	constexpr float SheriffTeleportZScale = 100.0f;       // _DAT_10450564, a DIMENSIONLESS weight
	constexpr float SheriffScoreDistCap = 1000.0f;        // DAT_104c6144
	constexpr float SheriffScoreYawWeight = 0.4f;         // _DAT_1044a2bc
	constexpr float SheriffScoreDistWeight = 0.6f;        // _DAT_104c6d3c

	// `CNPC_VSabbatLeader`'s three node pickers.
	constexpr float ArchwayMinFlatDist = 24.0f;           // _DAT_104c3cbc
	constexpr float DiveMinFlatDist = 40.0f;              // _DAT_104c3cfc
	constexpr float DiveMinSelfDist = 45.0f;              // _DAT_104c3d00
	constexpr float DiveLengthEpsilon = 9.999999747378752e-05f;   // _DAT_104c3ce4

	// `_DAT_104ce8c0`, `DistToSegment`'s degenerate-length floor, is not here: the body collapsed
	// onto family Hints' `FElysiumNpc::DistToSegment`, which owns the constant and records that its
	// WIDTH is unrecovered — the datum has four readers (`0x103c6b70` and
	// `CNPC_VVampireBoss::GetCurrHealthPercent 0x103c6830`, each twice) and no writer, so nothing
	// pins its value. What is recovered is the arm's ANSWER, `_DAT_104454c4` = `0.0f`.

	// `FUN_103d0bf0`'s refresh interval, seconds.
	constexpr float NearestNodeRefreshSeconds = 0.009999999776482582f;  // _DAT_10450aa4

	// Retail's hint-type words. Named here once so the selectors below read as retail reads.
	constexpr int32 HintTeleport17000 = 17000;
	constexpr int32 HintTeleport17001 = 0x4269;
	constexpr int32 HintArchway = 0x3e82;
	constexpr int32 HintDive = 0x3e85;
	constexpr int32 HintChangTeleport = 18000;
	constexpr int32 HintCenter = 0x4651;
	constexpr int32 HintJumpbase = 0x4652;
	constexpr int32 HintLedge = 0x4653;

	// `UTIL_AngleDiff` `0x1013d580` and `VectorAngles` `0x10139970`, the two angle routines every
	// scored selector here runs. Family Facing (`ElysiumNpcKernelFacing.cpp`) carries an identical
	// pair in its own anonymous namespace and neither is exported; the duplication is stated rather
	// than resolved, because factoring a shared header out of another family's file mid-wave is a
	// bigger edit than the two functions are worth.
	float RetailAngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < -180.0f)
			{
				Delta += 360.0f;
			}
		}
		else
		{
			while (Delta > 180.0f)
			{
				Delta -= 360.0f;
			}
		}
		return Delta;
	}

	// Source `[pitch yaw roll]` for a direction in THIS world's axes; `bsp.source_to_unreal` has
	// negated Y, so a yaw derived from a port-space delta is the negated Unreal one.
	FVector RetailVectorAngles(const FVector& PortDir)
	{
		const double Y = -PortDir.Y;
		if (PortDir.X == 0.0 && Y == 0.0)
		{
			return FVector(PortDir.Z > 0.0 ? 270.0 : 90.0, 0.0, 0.0);
		}
		float Yaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Y, PortDir.X)));
		if (Yaw < 0.0f)
		{
			Yaw += 360.0f;
		}
		const double Flat = FMath::Sqrt(PortDir.X * PortDir.X + Y * Y);
		float Pitch = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(-PortDir.Z, Flat)));
		if (Pitch < 0.0f)
		{
			Pitch += 360.0f;
		}
		return FVector(Pitch, Yaw, 0.0);
	}

	// `FUN_10137220` — Source's `VectorNormalize`, which divides by `length + _DAT_1046a51c` rather
	// than by the length, and returns the length in `ST0` (the decompiler prints it `void`; the
	// listing's trailing `FLD`/`FSTP` pattern leaves the length on the stack). The epsilon matters
	// only for a zero vector, which retail therefore leaves at zero rather than faulting.
	float RetailVectorNormalize(FVector& InOutVector)
	{
		const float Length = static_cast<float>(InOutVector.Size());
		const float Scale = RetailOne / (NormalizeEpsilon + Length);
		InOutVector *= Scale;
		return Length;
	}

	// The flat (XY) distance retail computes as `sqrt(dx*dx + dy*dy)` through `FUN_101371d0`.
	float FlatDistance(const FVector& A, const FVector& B)
	{
		const double Dx = A.X - B.X;
		const double Dy = A.Y - B.Y;
		return static_cast<float>(FMath::Sqrt(Dx * Dx + Dy * Dy));
	}

	// The yaw of the flat delta `From - To`, as `VectorAngles(Vector(dx, dy, 0))` answers it.
	float FlatYaw(const FVector& From, const FVector& To)
	{
		const FVector Flat(From.X - To.X, From.Y - To.Y, 0.0);
		return static_cast<float>(RetailVectorAngles(Flat).Y);
	}

	// One Source unit in centimetres, at the point of use so the recovered constant stays visible.
	constexpr float U = ElysiumMove::U;
}

// --- The candidate list -------------------------------------------------------------------------

void FElysiumNpc::GatherHintNodes(TArray<FHintWords>& OutNodes, TArray<int32>& OutNodeIds) const
{
	// `for (node = DAT_10925450; node; node = node->next (+0x5d8))` — the global `CAI_Hint` list.
	// **SEAM**, twice over: family Motor's `NavAllHintNodes` is the list (empty) and family Hints'
	// `HintWords` is one node's words (never valid). Nine selectors walk this and every one of them
	// therefore answers "no node", which is retail's own answer on a map that authors none.
	OutNodes.Reset();
	OutNodeIds.Reset();
	TArray<int32> NodeIds;
	if (!NavAllHintNodes(NodeIds))
	{
		return;
	}
	for (const int32 NodeId : NodeIds)
	{
		FHintWords Words;
		if (HintWords(NodeId, Words) && Words.bValid)
		{
			OutNodes.Add(Words);
			OutNodeIds.Add(NodeId);
		}
	}
}

// --- `CNPC_VSheriffMan::SelectCenterNode` `0x103b0930` ------------------------------------------

int32 FElysiumNpc::SelectCenterNodeRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm)
{
	// The whole body past the closest-player gate: type `0x4651`, the 3-D distance from the node to
	// the PLAYER (rooted, not squared — `PTR_thunk_FUN_101371d0` is `sqrt`), strictly nearest wins,
	// ties keep the FIRST because the compare is `<`.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintCenter)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(PlayerCm, Nodes[Index].OriginCm));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectCenterNode() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;   // retail's `m_hClosestPlayer` handle-validity gate
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectCenterNodeRule(Nodes, Player->Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

// --- `CNPC_VSheriffMan::SelectLedgeNode` `0x103b0ab0` -------------------------------------------

int32 FElysiumNpc::SelectLedgeNodeRule(TArrayView<const FHintWords> Nodes,
	const FVector& MeasureFromCm)
{
	// Type `0x4653`, the 3-D distance from the node to `MeasureFromCm`, nearest wins. Retail's
	// `char` argument selects the reference: `'\0'` reads the PLAYER's `GetAbsOrigin` (through the
	// resolved handle's vtable) and anything else reads its OWN — and the closest-player gate stands
	// in front of both, so a Sheriff with no player answers null even when measuring from himself.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintLedge)
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(MeasureFromCm, Nodes[Index].OriginCm));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectLedgeNode(bool bMeasureFromSelf) const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectLedgeNodeRule(Nodes, bMeasureFromSelf ? Origin : Player->Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

// --- `CNPC_VSabbatLeader::SelectTeleportArchway` `0x103a9540` ------------------------------------

int32 FElysiumNpc::SelectTeleportArchwayRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, float PlayerYaw)
{
	// Type `0x3e82`; the FLAT distance from the node to the player must reach `_DAT_104c3cbc = 24.0`
	// units; the score is `|AngleDiff(playerYaw, VectorAngles(player - node).y)|` alone, so the
	// archway most nearly in front of the player wins regardless of how far away it is.
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintArchway)
		{
			continue;
		}
		const float Flat = FlatDistance(PlayerCm, Nodes[Index].OriginCm);
		if (Flat < ArchwayMinFlatDist * U)
		{
			continue;
		}
		const float Score =
			FMath::Abs(RetailAngleDiff(PlayerYaw, FlatYaw(PlayerCm, Nodes[Index].OriginCm)));
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectTeleportArchway() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	// `FElysiumPlayer::Angles` is Source `[pitch yaw roll]`, exactly what `GetAbsAngles().y` is.
	const int32 Pick = SelectTeleportArchwayRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y));
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

// --- `CNPC_VSabbatLeader::SelectDiveOutPoint` `0x103a9ad0` --------------------------------------

int32 FElysiumNpc::SelectDiveOutPointRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, float PlayerYaw)
{
	// Type `0x3e85`; the flat distance must reach `_DAT_104c3cfc = 40.0` units; the score ADDS the
	// flat distance to the yaw delta, so a degree of misalignment and a unit of distance cost the
	// same — retail's own weighting, and the one line that separates this from the archway pick.
	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintDive)
		{
			continue;
		}
		const float Flat = FlatDistance(PlayerCm, Nodes[Index].OriginCm);
		if (Flat < DiveMinFlatDist * U)
		{
			continue;
		}
		const float Score =
			FMath::Abs(RetailAngleDiff(PlayerYaw, FlatYaw(PlayerCm, Nodes[Index].OriginCm))) + Flat;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectDiveOutPoint() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectDiveOutPointRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y));
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

// --- `CNPC_VSabbatLeader::SelectDiveInPoint` `0x103a9760` ---------------------------------------

int32 FElysiumNpc::SelectDiveInPointRule(TArrayView<const FHintWords> Nodes, const FVector& PlayerCm,
	float PlayerYaw, const FVector& SelfCm)
{
	// `SelectDiveOutPoint` plus two gates.
	//
	// The first is on the LEADER: the flat delta `player - self` is normalized in place
	// (`FUN_10137220`) and the length it returns must reach `_DAT_104c3d00 = 45.0` units, or the
	// body returns null without walking the list at all. The normalize matters because the SAME
	// three floats become the unit direction the second gate dots against.
	//
	// The second is per node: the delta `node - self` is normalized, its length must exceed
	// `_DAT_104c3ce4 = 1e-4`, and the dot of the two unit vectors must be BELOW `_DAT_104454c4 = 0`
	// — so the dive point has to lie in the hemisphere AWAY from the player. The leader dives out of
	// the player's reach, not toward him.
	FVector ToPlayerFlat(PlayerCm.X - SelfCm.X, PlayerCm.Y - SelfCm.Y, 0.0);
	const float ToPlayerLength = RetailVectorNormalize(ToPlayerFlat);
	if (ToPlayerLength < DiveMinSelfDist * U)
	{
		return INDEX_NONE;
	}

	int32 Best = INDEX_NONE;
	float BestScore = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintDive)
		{
			continue;
		}
		const float Flat = FlatDistance(PlayerCm, Nodes[Index].OriginCm);
		if (Flat < DiveMinFlatDist * U)
		{
			continue;
		}
		FVector ToNode = Nodes[Index].OriginCm - SelfCm;
		const float ToNodeLength = RetailVectorNormalize(ToNode);
		if (ToNodeLength <= DiveLengthEpsilon * U)
		{
			continue;
		}
		if (static_cast<float>(FVector::DotProduct(ToPlayerFlat, ToNode)) >= RetailZero)
		{
			continue;
		}
		const float Score =
			FMath::Abs(RetailAngleDiff(PlayerYaw, FlatYaw(PlayerCm, Nodes[Index].OriginCm))) + Flat;
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectDiveInPoint() const
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectDiveInPointRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y), Origin);
	return Pick == INDEX_NONE ? INDEX_NONE : NodeIds[Pick];
}

// --- `CNPC_VSheriffMan::SelectTeleportNode` `0x103b0630` ----------------------------------------

FElysiumNpc::FTeleportNodePick FElysiumNpc::SelectTeleportNodeSheriffRule(
	TArrayView<const FHintWords> Nodes, const FVector& PlayerCm, float PlayerYaw,
	TFunctionRef<bool(const FVector&, float)> Clear)
{
	// Types 17000, `0x4653` and `0x4652`. The distance is NOT euclidean: the Z component is
	// multiplied by `_DAT_10450564 = 100.0` before the root, so a node one unit above the player
	// reads as a hundred away and the Sheriff will not teleport off his own floor.
	//
	//     d  = player - node;  dz *= 100;  dist = sqrt(dx*dx + dy*dy + dz*dz)
	//     if (dist < DAT_104c6124) skip;                    // 100.0 units
	//     if (!PositionClearForTeleport(node, DAT_104c6124)) skip;
	//     yaw   = AngleDiff( player.yaw, VectorAngles(dx, dy, 0).y );
	//     score = |yaw| * (1/180) * 0.4  +  (min(dist, 1000) / 1000) * 0.6
	//
	// Both terms are normalized into `[0,1]` before the weights, which is why the two weights sum
	// to one. The clearance the gate uses is the SAME constant as the distance floor.
	FTeleportNodePick Pick;
	Pick.Score = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Type = Nodes[Index].HintType;
		if (Type != HintTeleport17000 && Type != HintLedge && Type != HintJumpbase)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		const double Dx = PlayerCm.X - NodeCm.X;
		const double Dy = PlayerCm.Y - NodeCm.Y;
		const double Dz = (PlayerCm.Z - NodeCm.Z) * SheriffTeleportZScale;
		float Distance = static_cast<float>(FMath::Sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
		if (Distance < SheriffLastTeleportFloor * U)
		{
			continue;
		}
		if (!Clear(NodeCm, SheriffLastTeleportFloor * U))
		{
			continue;
		}
		const float YawDelta = RetailAngleDiff(PlayerYaw, FlatYaw(PlayerCm, NodeCm));
		if (Distance > SheriffScoreDistCap * U)
		{
			Distance = SheriffScoreDistCap * U;
		}
		const float Score = FMath::Abs(YawDelta) * DegreesPerTurnRecip * SheriffScoreYawWeight
			+ (Distance / (SheriffScoreDistCap * U)) * SheriffScoreDistWeight;
		if (Score < Pick.Score)
		{
			Pick.Score = Score;
			Pick.Index = Index;
		}
	}
	return Pick;
}

int32 FElysiumNpc::SelectTeleportNodeSheriff()
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const FTeleportNodePick Pick = SelectTeleportNodeSheriffRule(Nodes, Player->Origin,
		static_cast<float>(Player->Angles.Y),
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportSheriff(PositionCm, ClearanceCm);
		});
	if (Pick.Index == INDEX_NONE)
	{
		// **Named divergence.** Retail dereferences the winning node UNCONDITIONALLY —
		// `(**(code **)(*local_2c + 0x364))()` with `local_2c` still null — so a Sheriff on a map
		// with no reachable teleport node faults. That is a shipped crash and not a behaviour a
		// caller can observe; the port leaves the cache untouched and answers null instead.
		return INDEX_NONE;
	}
	SheriffLastTeleportPosition = Nodes[Pick.Index].OriginCm;
	SheriffLastTeleportTime = World != nullptr ? World->NowSeconds() : 0.0;
	return NodeIds[Pick.Index];
}

// --- `CNPC_VAndreiBlood::SelectTeleportNode` `0x1035ddd0` ---------------------------------------

int32 FElysiumNpc::SelectTeleportNodeAndreiRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, bool bPickFarthest, TFunctionRef<bool(const FVector&, float)> Clear)
{
	// Types 17000 and `0x4269`, the plain 3-D distance to the player, the clearance gate at
	// `DAT_104a6f7c = 100.0`, and then a COIN FLIP that is drawn ONCE before the walk and decides
	// whether the nearest or the FARTHEST candidate wins. The seed of the running best follows the
	// flip — `FLT_MAX` for nearest, `0.0` for farthest — so the farthest arm accepts any candidate
	// at a positive distance.
	int32 Best = INDEX_NONE;
	float BestDistance = bPickFarthest ? 0.f : TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Type = Nodes[Index].HintType;
		if (Type != HintTeleport17000 && Type != HintTeleport17001)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		const float Distance = static_cast<float>(FVector::Dist(PlayerCm, NodeCm));
		if (Distance < AndreiLastTeleportFloor * U)
		{
			continue;
		}
		if (!Clear(NodeCm, AndreiLastTeleportFloor * U))
		{
			continue;
		}
		if (bPickFarthest ? BestDistance < Distance : Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectTeleportNodeAndrei()
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;   // the ONE arm that returns before the draw, so no stream position moves
	}
	// `(**(code **)(*DAT_1070b244 + 8))(0, 1)` — `VEngineRandom001::RandomInt(0, 1)`. Named
	// decision: the draw goes on `EElysiumRngStream::NpcSchedule`, this runtime's NPC decision
	// stream, because it is one decision per selection and belongs beside the schedule picks.
	const bool bPickFarthest =
		ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 1) != 0;
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectTeleportNodeAndreiRule(Nodes, Player->Origin, bPickFarthest,
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportAndrei(PositionCm, ClearanceCm);
		});
	if (Pick == INDEX_NONE)
	{
		// Retail faults here for the same reason the Sheriff's does, and takes the same named
		// divergence. Note that Andrei stamps NO clock beside the position — unlike the Sheriff and
		// the Chang brothers, he has no `m_fLastTeleportTime` at all.
		return INDEX_NONE;
	}
	AndreiLastTeleportPosition = Nodes[Pick].OriginCm;
	return NodeIds[Pick];
}

// --- `CNPC_VChangBros::SelectTeleportNode` `0x1036cce0` -----------------------------------------

int32 FElysiumNpc::SelectTeleportNodeChangRule(TArrayView<const FHintWords> Nodes,
	const FVector& PlayerCm, TFunctionRef<bool(const FVector&, float)> Clear,
	TFunctionRef<int32(const FVector&)> Sector)
{
	// Types 18000, `0x4653` and `0x4652`; the plain 3-D distance to the player; the clearance gate
	// at `DAT_104ad9f4 = 100.0`. TWO bests are kept — the nearest of all candidates, and the nearest
	// whose sector matches the PLAYER's (retail takes the reference sector from
	// `GetSector(player->GetAbsOrigin())`, not from its own origin) — and the sector-matched one
	// REPLACES the plain one when it exists. A brother teleports to the player's half of the arena
	// when he can and to the nearest node when he cannot.
	int32 BestPlain = INDEX_NONE;
	int32 BestSector = INDEX_NONE;
	float BestPlainDistance = TNumericLimits<float>::Max();
	float BestSectorDistance = TNumericLimits<float>::Max();
	const int32 PlayerSector = Sector(PlayerCm);
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		const int32 Type = Nodes[Index].HintType;
		if (Type != HintChangTeleport && Type != HintLedge && Type != HintJumpbase)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		const float Distance = static_cast<float>(FVector::Dist(PlayerCm, NodeCm));
		if (Distance < ChangLastTeleportFloor * U)
		{
			continue;
		}
		if (!Clear(NodeCm, ChangLastTeleportFloor * U))
		{
			continue;
		}
		if (Distance < BestPlainDistance)
		{
			BestPlainDistance = Distance;
			BestPlain = Index;
		}
		if (PlayerSector == Sector(NodeCm) && Distance < BestSectorDistance)
		{
			BestSectorDistance = Distance;
			BestSector = Index;
		}
	}
	return BestSector != INDEX_NONE ? BestSector : BestPlain;
}

int32 FElysiumNpc::SelectTeleportNodeChang()
{
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return INDEX_NONE;
	}
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectTeleportNodeChangRule(Nodes, Player->Origin,
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportChang(PositionCm, ClearanceCm);
		},
		[this](const FVector& PositionCm) { return GetSector(PositionCm); });
	if (Pick == INDEX_NONE)
	{
		// The same named divergence: retail dereferences the null winner.
		return INDEX_NONE;
	}
	ChangLastTeleportPosition = Nodes[Pick].OriginCm;
	ChangLastTeleportTime = World != nullptr ? World->NowSeconds() : 0.0;
	return NodeIds[Pick];
}

// --- `CNPC_VAsianVampire::SelectLedgeNode` `0x103615c0` -----------------------------------------

int32 FElysiumNpc::SelectLedgeNodeAsianRule(TArrayView<const FHintWords> Nodes,
	const FVector& SelfCm, TFunctionRef<bool(const FVector&, float)> Clear)
{
	// The one selector of the eight that never asks for a player: type `0x4653`, the clearance gate
	// FIRST (at `DAT_104a9320 = 150.0`, and the clearance is checked before the distance is even
	// computed), then the 3-D distance to this NPC's OWN origin, nearest wins.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != HintLedge)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		if (!Clear(NodeCm, AsianLedgeClearance * U))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(SelfCm, NodeCm));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpc::SelectLedgeNodeAsian()
{
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectLedgeNodeAsianRule(Nodes, Origin,
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportAsian(PositionCm, ClearanceCm);
		});
	if (Pick == INDEX_NONE)
	{
		return INDEX_NONE;   // retail's own `if (piVar5 != NULL)` guard before the store
	}
	// `AddHintToStoredJumpPositions(winner)` — family Hints' body (`0x10361990`), which pushes the
	// hint's origin into the two-slot ring and advances the index. Calling it is the whole of the
	// tail.
	AddHintToStoredJumpPositions(Nodes[Pick]);
	return NodeIds[Pick];
}

// --- `PositionClearForTeleport`, four species ---------------------------------------------------

bool FElysiumNpc::PositionClearForTeleportSpecies(const FVector& PositionCm, float ClearanceCm) const
{
	// The dispatcher. Retail stands NO base `PositionClearForTeleport` — the name exists on exactly
	// these four classes and on nothing else — so a class the chain walk does not match has no body
	// at all, and the honest answer is a refusal rather than a guess. Every recovered caller is one
	// of the four's own selectors, so the refusal is unreachable in practice.
	if (IsRetailClass(TEXT("CNPC_VAndreiBlood")))
	{
		return PositionClearForTeleportAndrei(PositionCm, ClearanceCm);
	}
	if (IsRetailClass(TEXT("CNPC_VAsianVampire")))
	{
		return PositionClearForTeleportAsian(PositionCm, ClearanceCm);
	}
	if (IsRetailClass(TEXT("CNPC_VChangBros")))
	{
		// The chain walk covers `CNPC_VChangBrosBlade` and `CNPC_VChangBrosClaw`, which inherit it.
		return PositionClearForTeleportChang(PositionCm, ClearanceCm);
	}
	if (IsRetailClass(TEXT("CNPC_VSheriffMan")))
	{
		return PositionClearForTeleportSheriff(PositionCm, ClearanceCm);
	}
	return false;
}

bool FElysiumNpc::PositionClearForTeleportAndrei(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x1035e030`, the whole body:
	//     if (|GetAbsOrigin() - pos| <  clearance)      return false;
	//     if (|m_vLastTeleportPosition - pos| < 100.0)  return false;   // DAT_104a6f7c
	//     return true;
	// Both distances are 3-D and rooted. There is no player term and no store term: Andrei only
	// asks that the candidate be far enough from where he is and from where he last went.
	if (static_cast<float>(FVector::Dist(Origin, PositionCm)) < ClearanceCm)
	{
		return false;
	}
	if (static_cast<float>(FVector::Dist(AndreiLastTeleportPosition, PositionCm))
		< AndreiLastTeleportFloor * U)
	{
		return false;
	}
	return true;
}

bool FElysiumNpc::PositionClearForTeleportAsian(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x103629d0`, the whole body:
	//     if (IsPosNearStoredJumpPositions(pos))           return false;
	//     if (player resolves && |player.xy - pos.xy| <= clearance) return false;
	//     if (|GetAbsOrigin().xy - pos.xy| < clearance)    return false;
	//     return true;
	// Both distance terms are FLAT — the Asian vampire's ledges are stacked, so a candidate directly
	// above him is "clear" — and the player term's compare is `<=` where his own is `<`. The
	// listing's `(a < b) != (a == b)` is that `<=`; the asymmetry is retail's and is reproduced.
	if (IsPosNearStoredJumpPositions(PositionCm))
	{
		return false;
	}
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr && Player->Handle == Senses.Memory.ClosestPlayer)
	{
		if (FlatDistance(Player->Origin, PositionCm) <= ClearanceCm)
		{
			return false;
		}
	}
	if (FlatDistance(Origin, PositionCm) < ClearanceCm)
	{
		return false;
	}
	return true;
}

bool FElysiumNpc::PositionClearForTeleportChang(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x1036d350`, the whole body:
	//     if (|GetAbsOrigin() - pos| < clearance)        return false;
	//     if (|m_vLastTeleportPosition - pos| < 100.0)   return false;   // DAT_104ad9f4
	//     if (ConnectedSquad()) {
	//         if (thunk_FUN_10315a80(squad, pos, clearance)) return false;
	//         for (i = 0; i < NumSquadMembers(squad); ++i) {
	//             CNPC_VChangBros* m = dynamic_cast<CNPC_VChangBros*>( SquadMember(squad, i) );
	//             if (m && m != this && m->GetTeleportPosition(&p) && |p - pos| < clearance)
	//                 return false;
	//         }
	//     }
	//     return true;
	// The member count is re-read every iteration, which is how retail leaves it. The two brothers
	// therefore never claim the same spot inside the cooldown `GetTeleportPosition` enforces.
	if (static_cast<float>(FVector::Dist(Origin, PositionCm)) < ClearanceCm)
	{
		return false;
	}
	if (static_cast<float>(FVector::Dist(ChangLastTeleportPosition, PositionCm))
		< ChangLastTeleportFloor * U)
	{
		return false;
	}
	if (ConnectedSquad() != nullptr)
	{
		if (SquadPositionTaken(PositionCm, ClearanceCm))
		{
			return false;
		}
		TArray<FElysiumNpc*> Members;
		SquadMembers(Members);
		for (FElysiumNpc* Member : Members)
		{
			if (Member == nullptr || Member == this)
			{
				continue;
			}
			// `___RTDynamicCast(member, 0, …, CNPC_VChangBros, 0)` — a brother, not any squadmate.
			if (!Member->IsRetailClass(TEXT("CNPC_VChangBros")))
			{
				continue;
			}
			FVector Claimed;
			if (Member->GetTeleportPosition(Claimed)
				&& static_cast<float>(FVector::Dist(Claimed, PositionCm)) < ClearanceCm)
			{
				return false;
			}
		}
	}
	return true;
}

bool FElysiumNpc::PositionClearForTeleportSheriff(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x103b0c70`, the whole body:
	//     if (|GetAbsOrigin() - pos| < clearance)       return false;
	//     if (|m_vLastTeleportPosition - pos| < 100.0)  return false;   // DAT_104c6124
	//     if (player resolves && |player - pos| < clearance) return false;
	//     return true;
	// All three are 3-D, unlike the Asian vampire's flat pair.
	if (static_cast<float>(FVector::Dist(Origin, PositionCm)) < ClearanceCm)
	{
		return false;
	}
	if (static_cast<float>(FVector::Dist(SheriffLastTeleportPosition, PositionCm))
		< SheriffLastTeleportFloor * U)
	{
		return false;
	}
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr && Player->Handle == Senses.Memory.ClosestPlayer)
	{
		if (static_cast<float>(FVector::Dist(Player->Origin, PositionCm)) < ClearanceCm)
		{
			return false;
		}
	}
	return true;
}

bool FElysiumNpc::SquadPositionTaken(const FVector& PositionCm, float ClearanceCm) const
{
	// `thunk_FUN_10315a80(m_pSquad, pos, clearance)`. **SEAM**: this substrate has no squad object,
	// so `ConnectedSquad()` answers null and the caller never gets here. Answering false is the arm
	// that lets the candidate through, which is what an empty squad answers in retail.
	(void)PositionCm;
	(void)ClearanceCm;
	return false;
}

void FElysiumNpc::SquadMembers(TArray<FElysiumNpc*>& OutMembers) const
{
	// `thunk_FUN_103160a0(squad)` (the count) and `thunk_FUN_103160c0(squad, i)` (the Nth member).
	// **SEAM**: no squad object, so the list is empty.
	OutMembers.Reset();
}

// --- The Chang brothers' arena ------------------------------------------------------------------

int32 FElysiumNpc::GetSector(const FVector& PositionCm) const
{
	// `0x1036e580`, read from the listing because the decompiler turned both FPU compares into
	// `(a < b) != (a == b)`:
	//
	//     if (!m_bCenterStored) return 0;                              // +0x66e8
	//     float d2 = (p.x - c.x)^2 + (p.y - c.y)^2;                    // XY ONLY
	//     if (p.z - c.z >= 200.0)          return d2 <= 525^2 ? 3 : 4; // _DAT_104ada6c, _DAT_104ada68
	//     if (d2 <= 270.0)                 return 1;                   // _DAT_104ada60
	//     if (d2 <= 370^2)                 return 2;                   // _DAT_104ada64
	//     return 4;
	//
	// The sector-1 threshold is the odd one and is reproduced as it stands: 270 is compared against
	// a SQUARED distance while 370 and 525 are squared first by their own static initializers
	// (`0x1036e500` and `0x1036e550`). Sector 1 is therefore a circle of radius ~16.4 units around
	// the arena centre, not 270 — a shipped inconsistency the two bodies that read the sector
	// (`SelectTeleportNode` and `IsUnreachable`) were tuned against.
	if (!bChangCenterStored)
	{
		return 0;
	}
	const double Dx = PositionCm.X - ChangArenaCenter.X;
	const double Dy = PositionCm.Y - ChangArenaCenter.Y;
	const float DistSq = static_cast<float>(Dx * Dx + Dy * Dy);
	if (PositionCm.Z - ChangArenaCenter.Z >= SectorZThreshold * U)
	{
		if (DistSq <= SectorThreeRadius * SectorThreeRadius * U * U)
		{
			return 3;
		}
		return 4;
	}
	if (DistSq <= SectorOneDistSq * U * U)
	{
		return 1;
	}
	if (DistSq <= SectorTwoRadius * SectorTwoRadius * U * U)
	{
		return 2;
	}
	return 4;
}

bool FElysiumNpc::SectorIsInPit(int32 Sector)
{
	// `0x1036b6b0`: `return 0 < sector && sector < 3;` — sectors 1 and 2 are the pit, and the retail
	// `ScopeTrace` push/pop around it carries no gameplay data. Sector 0 ("no centre stored") and
	// sectors 3 and 4 are not the pit, which is what makes `IsUnreachable`'s pit-versus-3 pair
	// meaningful.
	return 0 < Sector && Sector < 3;
}

bool FElysiumNpc::GetTeleportPosition(FVector& OutPositionCm) const
{
	// `0x1036d270`:
	//     if (3.0 < gpGlobals->curtime - m_fLastTeleportTime) return false;   // _DAT_104ada04
	//     *out = m_vLastTeleportPosition;
	//     return true;
	// A claim expires three seconds after it was made, which is the window inside which the other
	// brother's `PositionClearForTeleport` will refuse the same spot.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (ChangTeleportPositionMaxAge < static_cast<float>(Now - ChangLastTeleportTime))
	{
		return false;
	}
	OutPositionCm = ChangLastTeleportPosition;
	return true;
}

// --- The Sheriff's floor heights ----------------------------------------------------------------

void FElysiumNpc::CacheFloorHeights()
{
	// `0x103b1510`. Two nested gates and three writes, and the nesting is the point: the LEDGE
	// height is only cached when a centre node was found first, so a map that authors ledges but no
	// centre leaves `m_bLedgeStored` false and `CategorizeHeight` comparing against a zero.
	//
	//     node = SelectCenterNode();       if (!node) return;
	//     m_flCenterZ = node->GetAbsOrigin().z;                 // +0x66e8
	//     node = SelectLedgeNode('\0');    if (!node) return;   // measured from the PLAYER
	//     m_bLedgeStored = 1;                                   // +0x66e7
	//     m_flLedgeZ = node->GetAbsOrigin().z;                  // +0x66ec
	const int32 Center = SelectCenterNode();
	if (Center == INDEX_NONE)
	{
		return;
	}
	FHintWords CenterWords;
	if (HintWords(Center, CenterWords) && CenterWords.bValid)
	{
		SheriffCenterFloorZ = static_cast<float>(CenterWords.OriginCm.Z);
	}
	const int32 Ledge = SelectLedgeNode(/*bMeasureFromSelf*/ false);
	if (Ledge == INDEX_NONE)
	{
		return;
	}
	FHintWords LedgeWords;
	if (HintWords(Ledge, LedgeWords) && LedgeWords.bValid)
	{
		bSheriffLedgeHeightStored = true;
		SheriffLedgeFloorZ = static_cast<float>(LedgeWords.OriginCm.Z);
	}
}

void FElysiumNpc::CategorizeHeight(float Zcm, int32& OutCategory) const
{
	// `0x103b1790`, the whole body: `*out = |z - m_flCenterZ| < |z - m_flLedgeZ| ? 0 : 1;`
	// Strictly nearer wins for the centre; a tie goes to the LEDGE. No gate on
	// `m_bLedgeStored` — the byte `CacheFloorHeights` sets has no reader here, which is why a map
	// with no ledge node categorises everything against a zero height.
	OutCategory = FMath::Abs(Zcm - SheriffCenterFloorZ) < FMath::Abs(Zcm - SheriffLedgeFloorZ)
		? 0 : 1;
}

void FElysiumNpc::CategorizeHeights(int32& OutPlayer, int32& OutSelf) const
{
	// `0x103b1680`: both out-params are zeroed FIRST, then the closest player is resolved and both
	// are filled — the player's height, then this body's. A Sheriff with no player therefore
	// answers `0, 0`, which is the centre category for both, not an untouched pair.
	OutPlayer = 0;
	OutSelf = 0;
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr || Player->Handle != Senses.Memory.ClosestPlayer)
	{
		return;
	}
	CategorizeHeight(static_cast<float>(Player->Origin.Z), OutPlayer);
	CategorizeHeight(static_cast<float>(Origin.Z), OutSelf);
}

// --- `GetNearestNodeToPlayer` `0x103d0bf0` ------------------------------------------------------

int32 FElysiumNpc::NavNearestNodeTo(const FVector& PositionCm) const
{
	// `thunk_FUN_102f41b0(m_pNavigator->GetNetwork() (+0x2c), &pos)`. **SEAM**: family Motor's
	// standing fact — there is no `CAI_Node` array here — so this answers retail's own miss value.
	(void)PositionCm;
	return -1;
}

int32 FElysiumNpc::GetNearestNodeToPlayer()
{
	// `FUN_103d0bf0`, in retail's order:
	//
	//     if (m_flNodeRefreshed + 0.01 < curtime) m_iNearestNode = 0;   // _DAT_10450aa4, +0x6700/+0x6704
	//     if (m_iNearestNode == 0) {
	//         m_iNearestNode = 0;
	//         if (m_hClosestPlayer resolves) {
	//             Vector p = player->GetLocalOrigin();                  // vtable +0x370, slot 220
	//             nav->field_8 = owner->m_nSomething (+0x156c);         // the two navigator scratch
	//             nav->field_c = gpGlobals->frametime;                  //   words the query reads
	//             int node = FindNearestNode( nav->GetNetwork(), p );
	//             if (node == -1) DevWarning("GetNearestNodeToPlayer failed\n");
	//             else if (node < 0 || node >= network->count) { ++g_counter; m_iNearestNode = 0; }
	//             else m_iNearestNode = network->nodes[node];
	//         }
	//         m_flNodeRefreshed = curtime;
	//     }
	//     return m_iNearestNode;
	//
	// Note what the cache is keyed on: **zero**, not `-1`. A map whose node 0 is the nearest one
	// re-queries every frame, and a query that fails leaves the stamp advanced so the next attempt
	// waits the full interval. Both are reproduced.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (NearestNodeToPlayerRefreshedAt + NearestNodeRefreshSeconds < Now)
	{
		NearestNodeToPlayer = 0;
	}
	if (NearestNodeToPlayer != 0)
	{
		return NearestNodeToPlayer;
	}
	NearestNodeToPlayer = 0;
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr && Player->Handle == Senses.Memory.ClosestPlayer)
	{
		const int32 Node = NavNearestNodeTo(Player->Origin);
		if (Node == -1)
		{
			// `DevWarning(s_GetNearestNodeToPlayer_failed_1066249c)` — the string is
			// "GetNearestNodeToPlayer failed". This is the arm the seam always takes.
			UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("GetNearestNodeToPlayer failed"));
		}
		else
		{
			NearestNodeToPlayer = Node;
		}
	}
	NearestNodeToPlayerRefreshedAt = Now;
	return NearestNodeToPlayer;
}

// --- What is left of `FUN_102c5570` -------------------------------------------------------------

bool FElysiumNpc::EnemyLastKnownPosition(FVector& OutPositionCm) const
{
	// `GetEnemies()` (slot 541) then `thunk_FUN_102dfed0(memory, &out, pEnemy)`. This runtime's
	// `FElysiumNpcEnemyMemory` is the same store, so the fact is carried; the record's position is
	// what retail's helper copies out.
	const FElysiumEntity* Enemy =
		World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr)
	{
		return false;
	}
	OutPositionCm = Enemy->Origin;
	return true;
}

bool FElysiumNpc::ShootTargetDelta(FVector& OutDeltaCm) const
{
	// The middle of `FUN_102c5570`, which is what the listing settles:
	//
	//     if (m_hShootTargetOverride (+0x5ba8) resolves)
	//         delta = override->GetAbsOrigin() - GetAbsOrigin();
	//     else if (GetEnemy() (slot 167) != NULL)
	//         delta = GetEnemies()->GetLastKnownPosition(GetEnemy()) - GetAbsOrigin();
	//     else
	//         <no target: the caller skips the divide entirely>
	//
	// The override wins outright — the enemy is not even asked for — which is why a schedule that
	// pins a shoot target keeps aiming at it after the enemy moves.
	const FElysiumEntity* Override =
		World != nullptr ? World->Resolve(ShootTargetOverride) : nullptr;
	if (Override != nullptr)
	{
		OutDeltaCm = Override->Origin - Origin;
		return true;
	}
	FVector LastKnown;
	if (!EnemyLastKnownPosition(LastKnown))
	{
		return false;
	}
	OutDeltaCm = LastKnown - Origin;
	return true;
}

float FElysiumNpc::ShootTargetFalloff(float RangeBase, float RangeDivisor, float Value) const
{
	// `FUN_102c5570`'s whole numeric shape, read from the listing (`0x102c5570`..`0x102c56bf`):
	//
	//     float scale = 1.0f;                                 // _DAT_104454c0
	//     if (RangeDivisor > 0.0f) {                          // _DAT_104454c4
	//         Vector d;
	//         if (ShootTargetDelta(d)) {
	//             float dist = d.Length();
	//             scale = (dist > 0.0f) ? sqrt(dist / RangeDivisor) : dist;
	//         } else {
	//             scale = sqrt(1.0f / RangeDivisor);
	//         }
	//     }
	//     return (Value - RangeBase) * scale;
	//
	// `RangeBase` is the pointer argument's `+0x260` and `RangeDivisor` its `+0x26c`.
	//
	// **UNRECOVERED, and stated rather than papered over**: the class of that pointer. Those two
	// offsets are `m_pMoveChild` (an `EHANDLE`) and `m_iName` (a `string_t`) on a `CBaseEntity`, so
	// it is NOT an entity, and the function has ZERO callers anywhere in the image — nothing states
	// its type. 29c's overlay names this row `FElysiumNpc::IsNearShootTarget`; that name is 29c's
	// own inference and the body is not a predicate at all, so it is ported under a name that
	// describes what it computes. See `docs/vtmb/npc-ai/shape.md`.
	float Scale = RetailOne;
	if (RangeDivisor > RetailZero)
	{
		FVector Delta;
		if (ShootTargetDelta(Delta))
		{
			const float Distance = static_cast<float>(Delta.Size());
			Scale = Distance > RetailZero ? FMath::Sqrt(Distance / RangeDivisor) : Distance;
		}
		else
		{
			Scale = FMath::Sqrt(RetailOne / RangeDivisor);
		}
	}
	return (Value - RangeBase) * Scale;
}
