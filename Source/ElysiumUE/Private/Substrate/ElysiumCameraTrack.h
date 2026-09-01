#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

// Plain-value camera_track sampling, kept outside the entity leaf so its timing and interpolation
// are testable without a world, pawn, or renderer.
namespace ElysiumCameraTrack
{
	inline constexpr float SourceUnitCm = 2.54f;

	struct FPoint
	{
		FElysiumEntityHandle Entity;
		FVector Position = FVector::ZeroVector;
		FVector SourceAngles = FVector::ZeroVector;
		float Roll = 0.0f;
		float FocalLength = 0.0f;
		bool bTimeControl = false;
		float MoveSpeed = 64.0f; // Source units/second
		float MoveTime = 0.0f;
		float Pause = 0.0f;
		float RateIn = 1.0f;
		float RateOut = 1.0f;
		bool bCorner = false;
	};

	struct FSample
	{
		FVector Position = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		float Roll = 0.0f;
		float FieldOfView = 0.0f;
		int32 Segment = INDEX_NONE;
		bool bInPause = false;
		bool bFinished = false;
	};

	struct FPath
	{
		TArray<FPoint> Points;
		TArray<float> Arrivals;   // one per point; Arrivals[0] is zero
		TArray<float> Departures; // arrival + that point's authored pause
		float EndTime = 0.0f;

		void RebuildTimes();
		bool Sample(float Elapsed, FSample& Out) const;
	};

	float SegmentSeconds(const FPoint& From, const FPoint& To);

	// `CCameraKeyFrame::Activate`'s short-segment fold, which runs once at spawn and REWRITES the key.
	// A `TimeControl` segment at or below the threshold becomes a true zero-time edit; its authored
	// duration is re-attributed to a neighbouring pause rather than spent in place, and both ends of
	// the segment are forced to corners. sp_theatre's courtroom chain writes 87 of its edits as
	// `MoveTime 0.03` and sm_gallery_1 writes 7 as `0.01`, so the fold is what makes those read as
	// cuts instead of 30-millisecond slews. Threshold via `UElysiumChoreoSettings::CameraCutSeconds`
	// (retail: 0.05).
	float FoldSeconds();
	bool ShouldFold(bool bTimeControl, float MoveTime);
	// Exact-zero only — by sampling time the fold has already happened.
	bool IsHardCut(const FPoint& From);
	// True exactly once when forward playback crosses an authored edit.
	bool CrossesHardCut(const FPath& Path, float PreviousElapsed, float Elapsed);
	float EaseRate(float Alpha, float RateOut, float RateIn);
	float FocalLengthToHorizontalFov(float Millimetres);
}
