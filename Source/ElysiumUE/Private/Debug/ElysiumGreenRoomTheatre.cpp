#include "Debug/ElysiumGreenRoomRun.h"

#include "Debug/ElysiumGreenRoomShared.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapEntities.h"
#include "ElysiumMapActor.h"
#include "Substrate/ElysiumCameraTrack.h"

bool FElysiumGreenRoomRun::PrepareTheatreCase()
{
	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	FElysiumEntity* Scene = World ? World->FindByName(TEXT("embrace_o_matic")) : nullptr;
	FElysiumEntity* PositionRoot = World ? World->FindByName(TEXT("embrace_camera")) : nullptr;
	FElysiumEntity* TargetRoot = World ? World->FindByName(TEXT("embrace_target")) : nullptr;
	if (!Scene || !PositionRoot || !TargetRoot)
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("embrace room requires embrace_o_matic, embrace_camera, and embrace_target"));
		return false;
	}

	TheatreSceneOrigin = Scene->Origin;
	TheatreSceneAngles = Scene->Angles;
	TheatrePositionOwner = PositionRoot->Handle;
	TheatreTargetOwner = TargetRoot->Handle;
	World->SelectTrackCameraRole(false, TheatrePositionOwner);
	World->SelectTrackCameraRole(true, TheatreTargetOwner);

	// sp_theatre's own entity table, read for the embrace camera track alone: the green room stands
	// its case in whatever map is loaded, so the track's authored points come off the theatre's
	// defs rather than the live world. Through the transport resolver (baked asset first,
	// `.ents` second) like every other def read.
	FElysiumEntityDefs Defs;
	if (ElysiumEntityDefSource::Load(TEXT("sp_theatre"), Defs) == EElysiumEntityDefSource::None)
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("embrace room found no sp_theatre entity table (no baked asset, no .ents)"));
		return false;
	}
	TMap<FString, const FElysiumEntityDef*> Named;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (!Def.TargetName.IsEmpty())
		{
			Named.FindOrAdd(Def.TargetName.ToLower(), &Def);
		}
	}

	auto FloatKey = [](const FElysiumEntityDef& Def, const TCHAR* Name, float Default)
	{
		const FString* Value = Def.Keys.Find(Name);
		return Value && !Value->IsEmpty() ? FCString::Atof(**Value) : Default;
	};
	auto BoolKey = [](const FElysiumEntityDef& Def, const TCHAR* Name, bool Default)
	{
		const FString* Value = Def.Keys.Find(Name);
		return Value && !Value->IsEmpty() ? FCString::Atoi(**Value) != 0 : Default;
	};
	auto VectorKey = [](const FElysiumEntityDef& Def, const TCHAR* Name)
	{
		const FString* Value = Def.Keys.Find(Name);
		if (!Value)
		{
			return FVector::ZeroVector;
		}
		TArray<FString> Parts;
		Value->ParseIntoArrayWS(Parts);
		return Parts.Num() >= 3
			? FVector(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]), FCString::Atof(*Parts[2]))
			: FVector::ZeroVector;
	};
	auto BuildPath = [&Named, &FloatKey, &BoolKey, &VectorKey](
		const TCHAR* RootName, ElysiumCameraTrack::FPath& Out,
		TArray<const FElysiumEntityDef*>& OutDefs)
	{
		Out = ElysiumCameraTrack::FPath();
		OutDefs.Reset();
		const FElysiumEntityDef* const* Root = Named.Find(FString(RootName).ToLower());
		const FElysiumEntityDef* Cursor = Root ? *Root : nullptr;
		TSet<FString> Seen;
		for (int32 Guard = 0; Cursor && Guard < 1024; ++Guard)
		{
			const FString Current = Cursor->TargetName.ToLower();
			if (Seen.Contains(Current))
			{
				return false;
			}
			Seen.Add(Current);

			ElysiumCameraTrack::FPoint Point;
			Point.Position = Cursor->Origin;
			Point.SourceAngles = VectorKey(*Cursor, TEXT("angles"));
			Point.Roll = FloatKey(*Cursor, TEXT("Roll"), 0.0f);
			Point.FocalLength = FloatKey(*Cursor, TEXT("FocalLength"), 0.0f);
			Point.bTimeControl = BoolKey(*Cursor, TEXT("TimeControl"), false);
			Point.MoveSpeed = FloatKey(*Cursor, TEXT("MoveSpeed"), 64.0f);
			Point.MoveTime = FloatKey(*Cursor, TEXT("MoveTime"), 0.0f);
			Point.Pause = FloatKey(*Cursor, TEXT("Pause"), 0.0f);
			Point.RateIn = FloatKey(*Cursor, TEXT("RateIn"), 1.0f);
			Point.RateOut = FloatKey(*Cursor, TEXT("RateOut"), 1.0f);
			Point.bCorner = BoolKey(*Cursor, TEXT("Corner"), false);
			Out.Points.Add(Point);
			OutDefs.Add(Cursor);

			const FString Next = Cursor->Keys.FindRef(TEXT("NextKey"));
			if (Next.IsEmpty())
			{
				break;
			}
			const FElysiumEntityDef* const* Found = Named.Find(Next.ToLower());
			if (!Found)
			{
				return false;
			}
			Cursor = *Found;
		}
		Out.RebuildTimes();
		return Out.Points.Num() > 0;
	};

	TheatrePositionPath = MakeUnique<ElysiumCameraTrack::FPath>();
	TheatreTargetPath = MakeUnique<ElysiumCameraTrack::FPath>();
	TArray<const FElysiumEntityDef*> PositionDefs;
	TArray<const FElysiumEntityDef*> TargetDefs;
	if (!BuildPath(TEXT("embrace_camera"), *TheatrePositionPath, PositionDefs)
		|| !BuildPath(TEXT("embrace_target"), *TheatreTargetPath, TargetDefs))
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("embrace room could not build both camera paths"));
		return false;
	}
	TheatreDuration = FMath::Max(TheatrePositionPath->EndTime, TheatreTargetPath->EndTime);
	if (TheatreDuration <= 0.0f
		|| !FMath::IsNearlyEqual(TheatrePositionPath->EndTime, TheatreTargetPath->EndTime, 0.001f))
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("embrace camera clocks disagree: position %.3f target %.3f"),
			TheatrePositionPath->EndTime, TheatreTargetPath->EndTime);
		return false;
	}

	TheatreFades.Reset();
	for (int32 Index = 0; Index < PositionDefs.Num(); ++Index)
	{
		const FElysiumEntityDef& Point = *PositionDefs[Index];
		for (const FElysiumOutputDef& Output : Point.Outputs)
		{
			const bool bReached = Output.Name.Equals(TEXT("OnReachedKeyframe"), ESearchCase::IgnoreCase);
			const bool bLeaving = Output.Name.Equals(TEXT("OnLeavingKeyframe"), ESearchCase::IgnoreCase);
			if ((!bReached && !bLeaving)
				|| !Output.Input.Equals(TEXT("Fade"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FElysiumEntityDef* const* Found = Named.Find(Output.Target.ToLower());
			const FElysiumEntityDef* Fade = Found ? *Found : nullptr;
			if (!Fade || !Fade->Classname.Equals(TEXT("env_fade"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			FFadeWindow Window;
			Window.StartTime = (bReached
				? TheatrePositionPath->Arrivals[Index]
				: TheatrePositionPath->Departures[Index]) + FMath::Max(0.0f, Output.Delay);
			Window.Duration = FMath::Max(0.0f, FloatKey(*Fade, TEXT("duration"), 0.0f));
			Window.HoldTime = FMath::Max(0.0f, FloatKey(*Fade, TEXT("holdtime"), 0.0f));
			Window.Opacity = FMath::Clamp(FloatKey(*Fade, TEXT("renderamt"), 255.0f) / 255.0f, 0.0f, 1.0f);
			Window.bAutoReverse = (FMath::RoundToInt(FloatKey(*Fade, TEXT("spawnflags"), 0.0f)) & 8) != 0;
			TheatreFades.Add(Window);
		}
	}
	TheatreFades.Sort([](const FFadeWindow& A, const FFadeWindow& B)
	{
		return A.StartTime < B.StartTime;
	});

	// Sample every authored edit from both independently timed streams. Positive moves get a
	// midpoint; true zero-time cuts get frames immediately before and after the boundary.
	TArray<float> Times;
	auto AddTime = [&Times, this](float Time)
	{
		const float Clamped = FMath::Clamp(Time, 0.0f, TheatreDuration);
		if (!Times.ContainsByPredicate([Clamped](float Existing)
		{
			return FMath::IsNearlyEqual(Existing, Clamped, 0.002f);
		}))
		{
			Times.Add(Clamped);
		}
	};
	auto AddPathEdits = [&AddTime](const ElysiumCameraTrack::FPath& Path)
	{
		for (int32 Index = 0; Index + 1 < Path.Points.Num(); ++Index)
		{
			const float Departure = Path.Departures[Index];
			const float Arrival = Path.Arrivals[Index + 1];
			if (ElysiumCameraTrack::IsHardCut(Path.Points[Index]))
			{
				AddTime(Arrival - 0.01f);
				AddTime(Arrival + 0.01f);
			}
			else
			{
				AddTime(Departure + (Arrival - Departure) * 0.5f);
			}
		}
	};
	AddTime(0.0f);
	AddPathEdits(*TheatrePositionPath);
	AddPathEdits(*TheatreTargetPath);
	for (const FFadeWindow& Window : TheatreFades)
	{
		AddTime(Window.StartTime - 0.01f);
		AddTime(Window.StartTime + 0.01f);
		AddTime(Window.StartTime + Window.Duration + Window.HoldTime - 0.01f);
		AddTime(Window.StartTime + Window.Duration + Window.HoldTime + Window.Duration + 0.01f);
	}
	AddTime(TheatreDuration - 0.01f);
	Times.Sort();
	Fractions.Reset();
	for (const float Time : Times)
	{
		Fractions.Add(Time / TheatreDuration);
	}
	UE_LOG(LogElysiumGreenRoom, Log,
		TEXT("embrace room ready: origin=%s yaw=%.1f camera=%.3fs samples=%d fades=%d"),
		*TheatreSceneOrigin.ToCompactString(), TheatreSceneAngles.Y, TheatreDuration,
		Fractions.Num(), TheatreFades.Num());
	return true;
}

void FElysiumGreenRoomRun::PublishTheatreCamera()
{
	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* World = Map ? Map->GetEntityWorld() : nullptr;
	if (!World || !TheatrePositionPath || !TheatreTargetPath)
	{
		bAnyFailure = true;
		return;
	}
	ElysiumCameraTrack::FSample Position;
	ElysiumCameraTrack::FSample Target;
	const float SceneTime = CurrentSceneTime();
	if (!TheatrePositionPath->Sample(SceneTime, Position)
		|| !TheatreTargetPath->Sample(SceneTime, Target))
	{
		UE_LOG(LogElysiumGreenRoom, Warning, TEXT("embrace camera sample failed at %.3fs"), SceneTime);
		bAnyFailure = true;
		return;
	}

	// Mirror the authored output order, then let the position publish compose the complete value
	// shot. This is the production world seam, including independent stream ownership and cuts.
	World->PublishTrackCamera(true, TheatreTargetOwner, Target.Position, Target.Rotation,
		Target.Roll, Target.FieldOfView, 0.0f);
	World->PublishTrackCamera(false, TheatrePositionOwner, Position.Position, Position.Rotation,
		Position.Roll, Position.FieldOfView, 0.0f);

	CurrentCamera.bValid = true;
	CurrentCamera.SceneTime = SceneTime;
	CurrentCamera.PositionSegment = Position.Segment;
	CurrentCamera.TargetSegment = Target.Segment;
	CurrentCamera.Position = Position.Position;
	CurrentCamera.Target = Target.Position;
	CurrentCamera.Rotation = (Target.Position - Position.Position).Rotation();
	CurrentCamera.Rotation.Roll = Position.Roll;
	CurrentCamera.Roll = Position.Roll;
	CurrentCamera.FieldOfView = Position.FieldOfView;
	CurrentCamera.FadeAlpha = TheatreFadeAlpha(SceneTime);
	CameraLocation = Position.Position;
	CameraRotation = CurrentCamera.Rotation;
}

float FElysiumGreenRoomRun::CurrentSceneTime() const
{
	return bTheatreCamera && Fractions.IsValidIndex(FractionIndex)
		? Fractions[FractionIndex] * TheatreDuration
		: 0.0f;
}

float FElysiumGreenRoomRun::TheatreFadeAlpha(float SceneTime) const
{
	float Alpha = 0.0f;
	for (const FFadeWindow& Window : TheatreFades)
	{
		const float Local = SceneTime - Window.StartTime;
		if (Local < 0.0f)
		{
			continue;
		}
		float WindowAlpha = Window.Opacity;
		if (Window.Duration > KINDA_SMALL_NUMBER && Local < Window.Duration)
		{
			WindowAlpha *= Local / Window.Duration;
		}
		else if (Local <= Window.Duration + Window.HoldTime)
		{
			WindowAlpha = Window.Opacity;
		}
		else if (Window.bAutoReverse
			&& Local < 2.0f * Window.Duration + Window.HoldTime)
		{
			WindowAlpha *= 1.0f - (Local - Window.Duration - Window.HoldTime)
				/ FMath::Max(Window.Duration, KINDA_SMALL_NUMBER);
		}
		else
		{
			WindowAlpha = Window.bAutoReverse ? 0.0f : Window.Opacity;
		}
		Alpha = FMath::Max(Alpha, FMath::Clamp(WindowAlpha, 0.0f, 1.0f));
	}
	return Alpha;
}
