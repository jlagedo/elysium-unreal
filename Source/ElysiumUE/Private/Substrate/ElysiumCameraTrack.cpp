#include "Substrate/ElysiumCameraTrack.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumClassFields.h"

#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCameraTrack, Log, All);

// A/B for the short-segment fold. 0 disables it, which leaves sp_theatre's courtroom edits as
// 30-millisecond camera moves.
static TAutoConsoleVariable<float> CVarCameraCutSeconds(
	TEXT("elysium.CameraCutSeconds"),
	0.05f,
	TEXT("camera_keyframe: a TimeControl segment at or below this many seconds is folded to a hard "
	     "cut at spawn, the way CCameraKeyFrame::Activate does (retail's threshold is 0.05). "
	     "0 disables the fold and leaves short segments as camera movement."),
	ECVF_Default);

namespace ElysiumCameraTrack
{
	namespace
	{
		FVector Catmull(const FVector& P0, const FVector& P1, const FVector& P2,
			const FVector& P3, float T)
		{
			const float T2 = T * T;
			const float T3 = T2 * T;
			return 0.5f * ((2.0f * P1)
				+ (-P0 + P2) * T
				+ (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2
				+ (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * T3);
		}

		float Catmull(float P0, float P1, float P2, float P3, float T)
		{
			const float T2 = T * T;
			const float T3 = T2 * T;
			return 0.5f * ((2.0f * P1) + (-P0 + P2) * T
				+ (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * T2
				+ (-P0 + 3.0f * P1 - 3.0f * P2 + P3) * T3);
		}

		FRotator CameraRotation(const FVector& SourceAngles)
		{
			// Source pitch/yaw both change sign under the Source -> Unreal handedness reflection.
			return FRotator(-SourceAngles.X, -SourceAngles.Y, 0.0f);
		}

		void FillPoint(const FPoint& Point, FSample& Out)
		{
			Out.Position = Point.Position;
			Out.Rotation = CameraRotation(Point.SourceAngles);
			Out.Roll = FRotator::NormalizeAxis(Point.Roll);
			Out.FieldOfView = FocalLengthToHorizontalFov(Point.FocalLength);
			Out.bInPause = true;
		}
	}

	float SegmentSeconds(const FPoint& From, const FPoint& To)
	{
		if (From.bTimeControl)
		{
			return FMath::Max(0.0f, From.MoveTime);
		}
		const float CurrentSpeed = FMath::Max(0.0f, From.MoveSpeed);
		const float NextSpeed = FMath::Max(0.0f, To.MoveSpeed);
		const float SourceSpeed = To.bCorner
			? CurrentSpeed
			: 0.5f * (CurrentSpeed + NextSpeed);
		const float SpeedCm = SourceSpeed * SourceUnitCm;
		return SpeedCm > SMALL_NUMBER
			? FVector::Distance(From.Position, To.Position) / SpeedCm
			: 0.0f;
	}

	float FoldSeconds()
	{
		return FMath::Max(0.0f, CVarCameraCutSeconds.GetValueOnAnyThread());
	}

	bool ShouldFold(bool bTimeControl, float MoveTime)
	{
		return bTimeControl && MoveTime >= 0.0f && MoveTime <= FoldSeconds();
	}

	bool IsHardCut(const FPoint& From)
	{
		// Exact zero only. A short segment never reaches the sampler as a short segment: FoldShortEdit
		// has already zeroed its MoveTime at spawn, which is where retail settles it.
		return From.bTimeControl
			&& From.MoveTime >= 0.0f
			&& From.MoveTime <= KINDA_SMALL_NUMBER;
	}

	bool CrossesHardCut(const FPath& Path, float PreviousElapsed, float Elapsed)
	{
		if (Elapsed < PreviousElapsed || Path.Arrivals.Num() != Path.Points.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index + 1 < Path.Points.Num(); ++Index)
		{
			const float CutTime = Path.Arrivals[Index + 1];
			if (IsHardCut(Path.Points[Index])
				&& PreviousElapsed < CutTime && Elapsed >= CutTime)
			{
				return true;
			}
		}
		return false;
	}

	float EaseRate(float Alpha, float RateOut, float RateIn)
	{
		const float T = FMath::Clamp(Alpha, 0.0f, 1.0f);
		if (T <= 0.0f || T >= 1.0f)
		{
			return T;
		}
		// CCameraTrack's recovered Hermite time remap. RateOut is the derivative at the source,
		// RateIn the derivative at the destination; the polynomial keeps E(0)=0 and E(1)=1.
		const float T2 = T * T;
		const float T3 = T2 * T;
		return RateOut * T
			+ (3.0f - RateIn - 2.0f * RateOut) * T2
			+ (RateIn + RateOut - 2.0f) * T3;
	}

	float FocalLengthToHorizontalFov(float Millimetres)
	{
		if (Millimetres <= SMALL_NUMBER)
		{
			return 0.0f; // zero means retain the player's FOV on the shot channel
		}
		return FMath::RadiansToDegrees(2.0f * FMath::Atan(18.0f / Millimetres));
	}

	void FPath::RebuildTimes()
	{
		Arrivals.Reset();
		Departures.Reset();
		EndTime = 0.0f;
		if (Points.Num() == 0)
		{
			return;
		}
		Arrivals.SetNumZeroed(Points.Num());
		Departures.SetNumZeroed(Points.Num());
		// Retail reaches the root immediately, fires its OnReached output, then holds that sample for
		// the root's authored Pause before leaving for the first segment.
		Departures[0] = FMath::Max(0.0f, Points[0].Pause);
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			Arrivals[Index + 1] = Departures[Index] + SegmentSeconds(Points[Index], Points[Index + 1]);
			Departures[Index + 1] = Arrivals[Index + 1]
				+ FMath::Max(0.0f, Points[Index + 1].Pause);
		}
		EndTime = Departures.Last();
	}

	bool FPath::Sample(float Elapsed, FSample& Out) const
	{
		Out = FSample();
		if (Points.Num() == 0 || Arrivals.Num() != Points.Num() || Departures.Num() != Points.Num())
		{
			return false;
		}
		const float Time = FMath::Max(0.0f, Elapsed);
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			if (Time < Departures[Index])
			{
				FillPoint(Points[Index], Out);
				Out.Segment = Index;
				return true;
			}
			const float Seconds = Arrivals[Index + 1] - Departures[Index];
			if (Seconds > SMALL_NUMBER && Time <= Arrivals[Index + 1])
			{
				if (IsHardCut(Points[Index]))
				{
					// An edit holds the source key for the segment's authored length and switches
					// exactly at the arrival, so the chain's clock is untouched and only the
					// interior interpolation is skipped.
					FillPoint(Time < Arrivals[Index + 1] ? Points[Index] : Points[Index + 1], Out);
					Out.Segment = Index;
					Out.bInPause = false;
					return true;
				}
				const float Raw = Seconds > SMALL_NUMBER
					? (Time - Departures[Index]) / Seconds
					: 1.0f;
				const float T = EaseRate(Raw, Points[Index].RateOut, Points[Index + 1].RateIn);
				const int32 I0 = (Index == 0 || Points[Index].bCorner) ? Index : Index - 1;
				const int32 I3 = (Index + 2 >= Points.Num() || Points[Index + 1].bCorner)
					? Index + 1 : Index + 2;
				Out.Position = Catmull(Points[I0].Position, Points[Index].Position,
					Points[Index + 1].Position, Points[I3].Position, T);

				const FRotator R1 = CameraRotation(Points[Index].SourceAngles);
				const FRotator R2 = CameraRotation(Points[Index + 1].SourceAngles);
				Out.Rotation.Pitch = R1.Pitch + FMath::FindDeltaAngleDegrees(R1.Pitch, R2.Pitch) * T;
				Out.Rotation.Yaw = R1.Yaw + FMath::FindDeltaAngleDegrees(R1.Yaw, R2.Yaw) * T;
				Out.Rotation.Roll = 0.0f;

				// Retail packs roll and FOV into one vector and runs the SAME Catmull over both:
				// the focal length is converted to a field of view BEFORE interpolating rather than
				// after, and roll takes no shortest-path unwrapping — the keys are normalised once
				// at spawn and interpolated as plain values.
				Out.Roll = Catmull(Points[I0].Roll, Points[Index].Roll,
					Points[Index + 1].Roll, Points[I3].Roll, T);
				Out.FieldOfView = Catmull(
					FocalLengthToHorizontalFov(Points[I0].FocalLength),
					FocalLengthToHorizontalFov(Points[Index].FocalLength),
					FocalLengthToHorizontalFov(Points[Index + 1].FocalLength),
					FocalLengthToHorizontalFov(Points[I3].FocalLength), T);
				Out.Segment = Index;
				Out.bInPause = false;
				return true;
			}
		}

		FillPoint(Points.Last(), Out);
		Out.Segment = FMath::Max(0, Points.Num() - 1);
		Out.bFinished = Time >= EndTime;
		return true;
	}
}

namespace
{
	class FElysiumCameraKeyframe : public FElysiumEntity
	{
	public:
		FString NextKey;
		float Roll = 0.0f;
		float FocalLength = 0.0f;
		bool bTimeControl = false;
		float MoveSpeed = 64.0f;
		float MoveTime = 0.0f;
		float Pause = 0.0f;
		float RateIn = 1.0f;
		float RateOut = 1.0f;
		bool bCorner = false;
		int32 PositionInterpolator = 0; // authored and inspectable, but dead in CCameraKeyFrame

		// `CCameraKeyFrame::Activate`'s spawn-time rewrite. Runs once, mutates the authored keys, and
		// is what turns a short authored segment into a real edit — so by the time anything samples the
		// chain there are no sub-frame segments left to interpolate.
		//
		// The re-attribution is the part that is easy to get wrong: the folded MoveTime is NOT spent
		// where it was authored. It moves to the NEXT key's pause when that key has one, otherwise to
		// this key's own pause when it has one, and otherwise it is dropped outright. So the cut lands
		// at the start of the pause rather than the end of it, and a fold between two pauseless keys
		// shortens the chain.
		//
		// Order-sensitive, exactly as retail is: a key whose pause was just raised from zero by its
		// predecessor's fold becomes a legal re-attribution target for its own.
		void FoldShortEdit()
		{
			Roll = FRotator::NormalizeAxis(Roll);   // Activate normalises the authored roll once
			if (!ElysiumCameraTrack::ShouldFold(bTimeControl, MoveTime))
			{
				return;
			}
			FElysiumCameraKeyframe* Next = NextKeyframe();
			if (MoveTime > 0.0f)
			{
				if (Next != nullptr && Next->Pause > 0.0f)
				{
					Next->Pause += MoveTime;
				}
				else if (Pause > 0.0f)
				{
					Pause += MoveTime;
				}
				// else: dropped. The chain is genuinely shorter by that much.
			}
			MoveTime = 0.0f;
			bCorner = true;
			if (Next != nullptr)
			{
				// Forced on BOTH ends. This is why a fold changes more than timing: a speed-driven
				// segment arriving at a corner takes the source key's own speed instead of the endpoint
				// average, and the Catmull endpoint selection collapses to the segment itself.
				Next->bCorner = true;
			}
		}

		virtual void PostSpawn() override
		{
			FoldShortEdit();
		}

		// The next key in the chain, or null when this is the tail or the name does not resolve to a
		// keyframe. Deliberately non-const: the fold above writes through it.
		FElysiumCameraKeyframe* NextKeyframe() const
		{
			FElysiumEntity* Next = (World && !NextKey.IsEmpty()) ? World->FindByName(NextKey) : nullptr;
			const FName ClassName = Next && Next->Class ? Next->Class->ClassName : NAME_None;
			if (ClassName != FName(TEXT("camera_keyframe")) && ClassName != FName(TEXT("camera_track")))
			{
				return nullptr;
			}
			return static_cast<FElysiumCameraKeyframe*>(Next);
		}

		ElysiumCameraTrack::FPoint Point() const
		{
			ElysiumCameraTrack::FPoint P;
			P.Entity = Handle;
			P.Position = Origin;
			P.SourceAngles = Angles;
			P.Roll = Roll;
			P.FocalLength = FocalLength;
			P.bTimeControl = bTimeControl;
			P.MoveSpeed = MoveSpeed;
			P.MoveTime = MoveTime;
			P.Pause = Pause;
			P.RateIn = RateIn;
			P.RateOut = RateOut;
			P.bCorner = bCorner;
			return P;
		}
	};

	class FElysiumCameraTrack final : public FElysiumCameraKeyframe
	{
	public:
		bool bHoldAtEnd = false;
		float FromPlayerTime = 0.0f;
		float ToPlayerTime = 0.0f;

		void InputPlay(bool bTarget, const FElysiumInputArgs& Args)
		{
			FPlayback& Playback = bTarget ? TargetPlayback : PositionPlayback;
			StopPlayback(bTarget, Playback, /*BlendOutSeconds*/ 0.0f);
			Playback = FPlayback();
			if (!BuildPath(Playback.Path))
			{
				return;
			}
			Playback.bActive = true;
			Playback.StartTime = World ? World->NowSeconds() : 0.0;
			Playback.LastElapsed = -KINDA_SMALL_NUMBER;
			Playback.ActivatorIndex = Args.Activator.Index;
			const bool bRoleChanged = World && World->SelectTrackCameraRole(bTarget, Handle);
			TickPlayback(bTarget, Playback, 0.0f, bRoleChanged);
			ScheduleNextThink();
		}

		void InputRestore(const FElysiumInputArgs& Args)
		{
			const FString Param = Args.Param.ToString();
			const float Blend = Args.Param.IsVoid() || Param.IsEmpty()
				? ToPlayerTime
				: FMath::Max(0.0f, Args.Param.ToFloat());
			// Retail's input is a player-camera restore, not a request to stop this entity's two
			// clocks. It is accepted only while this entity remains the player's position track and
			// releases the complete position/target pair. The clocks may continue to fire authored
			// outputs, but the cleared world leases prevent them from reclaiming the view.
			if (World && World->TrackCameraOwner(/*bTargetRole*/ false) == Handle)
			{
				World->ClearTrackCamera(Blend);
			}
		}

		virtual void Think() override
		{
			const double Now = World ? World->NowSeconds() : 0.0;
			if (PositionPlayback.bActive)
			{
				TickPlayback(false, PositionPlayback,
					static_cast<float>(Now - PositionPlayback.StartTime));
			}
			if (TargetPlayback.bActive)
			{
				TickPlayback(true, TargetPlayback,
					static_cast<float>(Now - TargetPlayback.StartTime));
			}
			ScheduleNextThink();
		}

		virtual void OnDormancyChanged() override
		{
			if (IsInert())
			{
				StopPlayback(false, PositionPlayback, ToPlayerTime);
				StopPlayback(true, TargetPlayback, ToPlayerTime);
			}
			FElysiumEntity::OnDormancyChanged();
		}

		virtual void Serialize(FElysiumSaveArchive& Ar) override
		{
			SerializePlayback(Ar, false, PositionPlayback);
			SerializePlayback(Ar, true, TargetPlayback);
			ScheduleNextThink();
		}

		virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
		{
			Out.Emplace(TEXT("Position"), PlaybackState(PositionPlayback));
			Out.Emplace(TEXT("Target"), PlaybackState(TargetPlayback));
			Out.Emplace(TEXT("Hold at end"), bHoldAtEnd ? TEXT("yes") : TEXT("no"));
			Out.Emplace(TEXT("PositionInterpolator"), FString::Printf(TEXT("%d (dead key)"), PositionInterpolator));
		}

	private:
		struct FPlayback
		{
			bool bActive = false;
			bool bHeld = false;
			double StartTime = 0.0;
			float LastElapsed = 0.0f;
			int32 ActivatorIndex = INDEX_NONE;
			ElysiumCameraTrack::FPath Path;
		};

		static FString PlaybackState(const FPlayback& P)
		{
			if (P.bHeld) { return TEXT("held"); }
			if (P.bActive) { return FString::Printf(TEXT("playing %.2f / %.2f"), P.LastElapsed, P.Path.EndTime); }
			return TEXT("idle");
		}

		bool BuildPath(ElysiumCameraTrack::FPath& Out) const
		{
			Out = ElysiumCameraTrack::FPath();
			const FElysiumCameraKeyframe* Cursor = this;
			TSet<int32> Seen;
			for (int32 Guard = 0; Cursor && Guard < 1024; ++Guard)
			{
				if (Seen.Contains(Cursor->Handle.Index))
				{
					UE_LOG(LogElysiumCameraTrack, Warning, TEXT("%s camera chain cycles at %s"),
						*DebugString(), *Cursor->TargetName);
					break;
				}
				Seen.Add(Cursor->Handle.Index);
				Out.Points.Add(Cursor->Point());
				if (Cursor->NextKey.IsEmpty())
				{
					break;
				}
				FElysiumEntity* Next = World ? World->FindByName(Cursor->NextKey) : nullptr;
				const FName ClassName = Next && Next->Class ? Next->Class->ClassName : NAME_None;
				if (!Next || (ClassName != FName(TEXT("camera_keyframe"))
					&& ClassName != FName(TEXT("camera_track"))))
				{
					UE_LOG(LogElysiumCameraTrack, Warning, TEXT("%s camera chain cannot resolve '%s'"),
						*DebugString(), *Cursor->NextKey);
					break;
				}
				Cursor = static_cast<const FElysiumCameraKeyframe*>(Next);
			}
			Out.RebuildTimes();
			return Out.Points.Num() > 0;
		}

		FElysiumEntityHandle Activator(const FPlayback& P) const
		{
			return (World && P.ActivatorIndex != INDEX_NONE)
				? FElysiumEntityHandle(P.ActivatorIndex, World->GetEpoch())
				: FElysiumEntityHandle::Invalid();
		}

		void FireCrossedOutputs(FPlayback& P, float Elapsed)
		{
			const FElysiumEntityHandle A = Activator(P);
			for (int32 Index = 0; Index < P.Path.Points.Num(); ++Index)
			{
				const float Arrival = P.Path.Arrivals[Index];
				if (P.LastElapsed < Arrival && Elapsed >= Arrival)
				{
					if (FElysiumEntity* PointEnt = World ? World->Resolve(P.Path.Points[Index].Entity) : nullptr)
					{
						PointEnt->FireOutput(FName(TEXT("OnReachedKeyframe")), A);
					}
				}
				const float Departure = P.Path.Departures[Index];
				if (Index + 1 < P.Path.Points.Num()
					&& P.LastElapsed < Departure && Elapsed >= Departure)
				{
					if (FElysiumEntity* PointEnt = World ? World->Resolve(P.Path.Points[Index].Entity) : nullptr)
					{
						PointEnt->FireOutput(FName(TEXT("OnLeavingKeyframe")), A);
					}
				}
			}
		}

		void TickPlayback(bool bTarget, FPlayback& P, float Elapsed, bool bRoleChanged = false)
		{
			ElysiumCameraTrack::FSample Sample;
			if (!P.Path.Sample(Elapsed, Sample))
			{
				StopPlayback(bTarget, P, ToPlayerTime);
				return;
			}
			const bool bHardCut = ElysiumCameraTrack::CrossesHardCut(P.Path, P.LastElapsed, Elapsed);
			const bool bCameraCut = bHardCut
				|| (bRoleChanged && FromPlayerTime <= KINDA_SMALL_NUMBER);
			if (bHardCut)
			{
				UE_LOG(LogElysiumCameraTrack, Log, TEXT("hard cut crossed: %.6f -> %.6f"),
					P.LastElapsed, Elapsed);
			}
			FireCrossedOutputs(P, Elapsed);
			P.LastElapsed = Elapsed;
			if (World)
			{
				World->PublishTrackCamera(bTarget, Handle, Sample.Position, Sample.Rotation,
					Sample.Roll, Sample.FieldOfView, FromPlayerTime, bCameraCut);
			}
			if (Sample.bFinished)
			{
				P.bActive = false;
				P.bHeld = bHoldAtEnd;
				FireOutput(FName(TEXT("OnAnimationCompleted")), Activator(P));
				if (!bHoldAtEnd && World && World->TrackCameraOwner(bTarget) == Handle)
				{
					// A selected non-held stream returning to player control releases the complete
					// camera session. This matters when the paired stream has already completed with
					// HoldAtEnd, as sp_tutorial_1's lockpick focus shot deliberately does.
					World->ClearTrackCamera(ToPlayerTime);
				}
			}
		}

		void StopPlayback(bool bTarget, FPlayback& P, float Blend)
		{
			if (!P.bActive && !P.bHeld)
			{
				return;
			}
			if (World)
			{
				World->RestoreTrackCamera(bTarget, Handle, Blend);
			}
			P = FPlayback();
		}

		void ScheduleNextThink()
		{
			NextThink = (PositionPlayback.bActive || TargetPlayback.bActive)
				? static_cast<float>(World ? World->NowSeconds() : 0.0)
				: ELYSIUM_NEVER_THINK;
		}

		void SerializePlayback(FElysiumSaveArchive& Ar, bool bTarget, FPlayback& P)
		{
			float Elapsed = P.bActive
				? static_cast<float>((World ? World->NowSeconds() : P.StartTime) - P.StartTime)
				: P.LastElapsed;
			Ar << P.bActive << P.bHeld << Elapsed << P.LastElapsed << P.ActivatorIndex;
			if (Ar.IsLoading())
			{
				if ((P.bActive || P.bHeld) && BuildPath(P.Path))
				{
					const double Now = World ? World->NowSeconds() : 0.0;
					P.StartTime = Now - Elapsed;
					P.LastElapsed = Elapsed;
					ElysiumCameraTrack::FSample Sample;
					if (P.Path.Sample(Elapsed, Sample) && World)
					{
						const bool bRoleChanged = World->SelectTrackCameraRole(bTarget, Handle);
						World->PublishTrackCamera(bTarget, Handle, Sample.Position, Sample.Rotation,
							Sample.Roll, Sample.FieldOfView, FromPlayerTime,
							bRoleChanged && FromPlayerTime <= KINDA_SMALL_NUMBER);
					}
				}
				else
				{
					P = FPlayback();
				}
			}
		}

		FPlayback PositionPlayback;
		FPlayback TargetPlayback;
	};

	TUniquePtr<FElysiumEntity> MakeCameraKeyframe() { return MakeUnique<FElysiumCameraKeyframe>(); }
	TUniquePtr<FElysiumEntity> MakeCameraTrack() { return MakeUnique<FElysiumCameraTrack>(); }

	void BuildCameraKeyframeClass(FElysiumClassDesc& D)
	{
		ElysiumAddClassField(D, TEXT("NextKey"), &FElysiumCameraKeyframe::NextKey);
		ElysiumAddClassField(D, TEXT("Roll"), &FElysiumCameraKeyframe::Roll);
		ElysiumAddClassField(D, TEXT("FocalLength"), &FElysiumCameraKeyframe::FocalLength);
		ElysiumAddClassField(D, TEXT("TimeControl"), &FElysiumCameraKeyframe::bTimeControl);
		ElysiumAddClassField(D, TEXT("MoveSpeed"), &FElysiumCameraKeyframe::MoveSpeed);
		ElysiumAddClassField(D, TEXT("MoveTime"), &FElysiumCameraKeyframe::MoveTime);
		ElysiumAddClassField(D, TEXT("Pause"), &FElysiumCameraKeyframe::Pause);
		ElysiumAddClassField(D, TEXT("RateIn"), &FElysiumCameraKeyframe::RateIn);
		ElysiumAddClassField(D, TEXT("RateOut"), &FElysiumCameraKeyframe::RateOut);
		ElysiumAddClassField(D, TEXT("Corner"), &FElysiumCameraKeyframe::bCorner);
		ElysiumAddClassField(D, TEXT("PositionInterpolator"), &FElysiumCameraKeyframe::PositionInterpolator);
	}

	void BuildCameraTrackClass(FElysiumClassDesc& D)
	{
		D.Input(TEXT("PlayAsCameraPosition"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumCameraTrack&>(E).InputPlay(false, A); });
		D.Input(TEXT("PlayAsCameraTarget"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumCameraTrack&>(E).InputPlay(true, A); });
		D.Input(TEXT("RestoreCameraToPlayerControl"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumCameraTrack&>(E).InputRestore(A); });
		// Script-facing compact alias; authored map outputs use the full retail input above.
		D.Input(TEXT("Restore"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumCameraTrack&>(E).InputRestore(A); });
		ElysiumAddClassField(D, TEXT("HoldAtEnd"), &FElysiumCameraTrack::bHoldAtEnd);
		ElysiumAddClassField(D, TEXT("FromPlayerTime"), &FElysiumCameraTrack::FromPlayerTime);
		ElysiumAddClassField(D, TEXT("ToPlayerTime"), &FElysiumCameraTrack::ToPlayerTime);
	}

	FElysiumClassRegistrar GRegCameraKeyframe(
		TEXT("camera_keyframe"), ElysiumBaseClassName(), &MakeCameraKeyframe, &BuildCameraKeyframeClass);
	FElysiumClassRegistrar GRegCameraTrack(
		TEXT("camera_track"), FName(TEXT("camera_keyframe")), &MakeCameraTrack, &BuildCameraTrackClass);
}
