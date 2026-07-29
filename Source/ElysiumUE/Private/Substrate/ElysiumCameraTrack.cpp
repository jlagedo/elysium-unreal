#include "Substrate/ElysiumCameraTrack.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCameraTrack, Log, All);

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

	bool IsHardCut(const FPoint& From)
	{
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
		// Retail begins the first segment immediately. Pause belongs to a destination after it is
		// reached; the root key's authored Pause is not a pre-roll dwell.
		Departures[0] = 0.0f;
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
					// A true zero-time edit switches exactly at the arrival. Positive authored
					// MoveTime values remain camera movement, even when they are very short.
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

				const float Roll1 = Points[Index].Roll;
				const float Roll0 = Roll1 - FMath::FindDeltaAngleDegrees(Points[I0].Roll, Roll1);
				const float Roll2 = Roll1 + FMath::FindDeltaAngleDegrees(Roll1, Points[Index + 1].Roll);
				const float Roll3 = Roll2 + FMath::FindDeltaAngleDegrees(Points[Index + 1].Roll, Points[I3].Roll);
				Out.Roll = FRotator::NormalizeAxis(Catmull(Roll0, Roll1, Roll2, Roll3, T));

				const float Focal = Catmull(Points[I0].FocalLength, Points[Index].FocalLength,
					Points[Index + 1].FocalLength, Points[I3].FocalLength, T);
				Out.FieldOfView = FocalLengthToHorizontalFov(Focal);
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
	template <typename TClass, typename TMember>
	void AddCameraField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>);
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

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
			TickPlayback(bTarget, Playback, 0.0f);
			ScheduleNextThink();
		}

		void InputRestore(const FElysiumInputArgs& Args)
		{
			const FString Param = Args.Param.ToString();
			const float Blend = Args.Param.IsVoid() || Param.IsEmpty()
				? ToPlayerTime
				: FMath::Max(0.0f, Args.Param.ToFloat());
			StopPlayback(false, PositionPlayback, Blend);
			StopPlayback(true, TargetPlayback, Blend);
			ScheduleNextThink();
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

		void TickPlayback(bool bTarget, FPlayback& P, float Elapsed)
		{
			ElysiumCameraTrack::FSample Sample;
			if (!P.Path.Sample(Elapsed, Sample))
			{
				StopPlayback(bTarget, P, ToPlayerTime);
				return;
			}
			const bool bCameraCut = ElysiumCameraTrack::CrossesHardCut(P.Path, P.LastElapsed, Elapsed);
			if (bCameraCut)
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
				if (!bHoldAtEnd && World)
				{
					World->RestoreTrackCamera(bTarget, Handle, ToPlayerTime);
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
						World->PublishTrackCamera(bTarget, Handle, Sample.Position, Sample.Rotation,
							Sample.Roll, Sample.FieldOfView, FromPlayerTime);
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
		AddCameraField(D, TEXT("NextKey"), &FElysiumCameraKeyframe::NextKey);
		AddCameraField(D, TEXT("Roll"), &FElysiumCameraKeyframe::Roll);
		AddCameraField(D, TEXT("FocalLength"), &FElysiumCameraKeyframe::FocalLength);
		AddCameraField(D, TEXT("TimeControl"), &FElysiumCameraKeyframe::bTimeControl);
		AddCameraField(D, TEXT("MoveSpeed"), &FElysiumCameraKeyframe::MoveSpeed);
		AddCameraField(D, TEXT("MoveTime"), &FElysiumCameraKeyframe::MoveTime);
		AddCameraField(D, TEXT("Pause"), &FElysiumCameraKeyframe::Pause);
		AddCameraField(D, TEXT("RateIn"), &FElysiumCameraKeyframe::RateIn);
		AddCameraField(D, TEXT("RateOut"), &FElysiumCameraKeyframe::RateOut);
		AddCameraField(D, TEXT("Corner"), &FElysiumCameraKeyframe::bCorner);
		AddCameraField(D, TEXT("PositionInterpolator"), &FElysiumCameraKeyframe::PositionInterpolator);
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
		AddCameraField(D, TEXT("HoldAtEnd"), &FElysiumCameraTrack::bHoldAtEnd);
		AddCameraField(D, TEXT("FromPlayerTime"), &FElysiumCameraTrack::FromPlayerTime);
		AddCameraField(D, TEXT("ToPlayerTime"), &FElysiumCameraTrack::ToPlayerTime);
	}

	FElysiumClassRegistrar GRegCameraKeyframe(
		TEXT("camera_keyframe"), ElysiumBaseClassName(), &MakeCameraKeyframe, &BuildCameraKeyframeClass);
	FElysiumClassRegistrar GRegCameraTrack(
		TEXT("camera_track"), FName(TEXT("camera_keyframe")), &MakeCameraTrack, &BuildCameraTrackClass);
}
