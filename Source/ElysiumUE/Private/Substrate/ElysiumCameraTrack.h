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

	// The longest `TimeControl` segment that counts as an authored edit rather than camera movement.
	// VtMB's server frame IS the render frame (`docs/vtmb/game_runtime.md`), so at the ~30 fps its
	// cutscenes were cut on, a segment shorter than one frame was stepped over whole and its interior
	// never sampled. sp_theatre's courtroom chain writes 87 of its edits as `MoveTime 0.03` and
	// sm_gallery_1 writes 7 as `0.01`; sampling those at 120 fps turns each into a visible slew.
	// The authored duration is still spent either way, so a chain's timing against its scene audio is
	// unchanged — only the interior interpolation goes away. `elysium.CameraCutSeconds`.
	float HardCutSeconds();
	bool IsHardCut(const FPoint& From);
	// True exactly once when forward playback crosses an authored edit.
	bool CrossesHardCut(const FPath& Path, float PreviousElapsed, float Elapsed);
	float EaseRate(float Alpha, float RateOut, float RateIn);
	float FocalLengthToHorizontalFov(float Millimetres);
}
