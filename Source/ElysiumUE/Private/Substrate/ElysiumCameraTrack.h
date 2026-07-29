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
	bool IsHardCut(const FPoint& From);
	// True exactly once when forward playback crosses an authored zero-time edit.
	bool CrossesHardCut(const FPath& Path, float PreviousElapsed, float Elapsed);
	float EaseRate(float Alpha, float RateOut, float RateIn);
	float FocalLengthToHorizontalFov(float Millimetres);
}
