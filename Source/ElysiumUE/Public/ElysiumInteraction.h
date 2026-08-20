#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

// One semantic edge from FElysiumUserCmd. Edges are queued before movement and consumed only
// after the frame's interaction target has settled.
enum class EElysiumUseEdge : uint8
{
	Pressed,
	Released,
};

enum class EElysiumUseSelection : uint8
{
	None,
	Exact,
	Assisted,
};

enum class EElysiumUseOutcome : uint8
{
	NoTarget,
	Unavailable,
	OutOfRange,
	Occluded,
	Locked,
	Completed,
	SessionStarted,
	Busy,
	Cancelled,
};

enum class EElysiumUseSessionKind : uint8
{
	None,
	WhileHeld,
	Explicit,
};

enum class EElysiumUseEndReason : uint8
{
	Completed,
	Released,
	Cancelled,
	TargetInvalid,
	WorldTeardown,
};

struct FElysiumUseContext
{
	FElysiumEntityHandle Activator;
	FElysiumEntityHandle Owner;
	FVector AnchorPoint = FVector::ZeroVector;
	EElysiumUseSelection Selection = EElysiumUseSelection::None;
	double TimeSeconds = 0.0;
};

struct FElysiumUseBeginResult
{
	EElysiumUseOutcome Outcome = EElysiumUseOutcome::Completed;
	EElysiumUseSessionKind SessionKind = EElysiumUseSessionKind::None;

	static FElysiumUseBeginResult Completed()
	{
		return FElysiumUseBeginResult();
	}

	static FElysiumUseBeginResult Started(EElysiumUseSessionKind Kind)
	{
		FElysiumUseBeginResult Result;
		Result.Outcome = EElysiumUseOutcome::SessionStarted;
		Result.SessionKind = Kind;
		return Result;
	}

	static FElysiumUseBeginResult Refused(EElysiumUseOutcome InOutcome)
	{
		FElysiumUseBeginResult Result;
		Result.Outcome = InOutcome;
		return Result;
	}
};

// One geometrically valid embodiment candidate. Entity eligibility remains the substrate's call.
struct FElysiumUseCandidate
{
	FElysiumEntityHandle Owner;
	FVector AnchorPoint = FVector::ZeroVector;
	EElysiumUseSelection Selection = EElysiumUseSelection::None;
	float BodyDistance = 0.0f;
	float CameraDistance = 0.0f;
	float CameraDepth = 0.0f;
	float AimError = 0.0f;
	bool bHysteresis = false;
};

struct FElysiumUseQueryResult
{
	TArray<FElysiumUseCandidate> Candidates;
	EElysiumUseOutcome MissOutcome = EElysiumUseOutcome::NoTarget;
};

namespace ElysiumInteraction
{
	// The total candidate order is shared by the embodiment and pure tests. Exact intent is
	// absolute. Hysteresis changes admission to the assisted tier, never its scoring order.
	inline bool CandidateLess(const FElysiumUseCandidate& A, const FElysiumUseCandidate& B)
	{
		if (A.Selection != B.Selection)
		{
			return A.Selection == EElysiumUseSelection::Exact;
		}
		if (!FMath::IsNearlyEqual(A.AimError, B.AimError))
		{
			return A.AimError < B.AimError;
		}
		if (!FMath::IsNearlyEqual(A.CameraDepth, B.CameraDepth))
		{
			return A.CameraDepth < B.CameraDepth;
		}
		if (A.Owner.Index != B.Owner.Index)
		{
			return A.Owner.Index < B.Owner.Index;
		}
		return A.Owner.Epoch < B.Owner.Epoch;
	}

	inline void SortCandidates(TArray<FElysiumUseCandidate>& Candidates)
	{
		Candidates.Sort(CandidateLess);
	}

	inline FText UseBindingText(bool bGamepad)
	{
		return FText::FromString(bGamepad ? TEXT("RT") : TEXT("E"));
	}
}

// Presentation projection of the world-owned focus. A fading-out prompt can remain visible while
// bActionable is false, so input can never act on a retained visual.
struct FElysiumInteractionView
{
	bool bVisible = false;
	bool bActionable = false;
	bool bLocked = false;
	int32 Icon = 0;
	float PromptAlpha = 0.0f;
	FName Action = FName(TEXT("Use"));
};
