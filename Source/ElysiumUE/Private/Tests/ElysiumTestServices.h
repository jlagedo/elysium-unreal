#pragma once

// The recording world-services stub (11.2). Implements all four FElysiumWorldServices interfaces
// and writes one line per call into `Calls`, so a Substrate-tier test can assert what a map's logic
// *did* — stood this body, played that voice, faded the screen, asked to travel — with no RHI, no
// actors and no `tools/out`. That is the tier the back-pointers used to make impossible.
//
// It lives in the module (like the tests themselves — the substrate carries no ELYSIUMUE_API
// exports, so a same-module test links its symbols directly) and compiles only where the automation
// framework does.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumSignData.h"
#include "ElysiumWorldServices.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

struct FElysiumRecordingServices final
	: public IElysiumEmbodiment
	, public IElysiumAudio
	, public IElysiumTravel
	, public IElysiumPresenter
{
	// One line per service call, in the order they happened: "PlayVoice ambient/x.wav".
	// Mutable so the const interface methods can record too.
	mutable TArray<FString> Calls;

	// The bundle to hand FElysiumEntityWorld. All four members point at this object.
	FElysiumWorldServices Bundle()
	{
		FElysiumWorldServices S;
		S.Embodiment = this;
		S.Audio      = this;
		S.Travel     = this;
		S.Presenter  = this;
		return S;
	}

	// Did any call start with this prefix, and how many. Prefix-matched so a test can assert the
	// verb ("PlayNpcClip") or the verb and its argument ("PlayNpcClip jack cower_idle").
	int32 Count(const FString& Prefix) const
	{
		int32 N = 0;
		for (const FString& C : Calls)
		{
			N += C.StartsWith(Prefix) ? 1 : 0;
		}
		return N;
	}
	bool Saw(const FString& Prefix) const { return Count(Prefix) > 0; }
	FString Log() const { return FString::Join(Calls, TEXT(" | ")); }

	// What the player's body reports. A test sets these to stand a player somewhere; left as-is,
	// bHasPlayer false is the "menu backdrop / headless" case every call site must survive.
	bool     bHasPlayer = false;
	FVector  PlayerLocation = FVector::ZeroVector;
	FRotator PlayerRotation = FRotator::ZeroRotator;
	FElysiumCameraShot LastCameraShot;
	// What the next TraceUseCursor returns (Invalid = the ray hit nothing usable).
	FElysiumEntityHandle UseCursorHit;
	// Damage accumulated by DamagePlayer, so a trigger_hurt cadence is assertable as a number.
	float DamageTaken = 0.f;

	// --- IElysiumEmbodiment ----------------------------------------------------------------
	virtual float BodyScaleFor(const FElysiumEntityDef& Def) const override { return Def.bSky ? 16.f : 1.f; }

	virtual USkeletalMeshComponent* BuildNpcVisual(const FString& Stem, const FVector& Location,
		const FRotator& Rotation, float UniformScale, const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("BuildNpcVisual %s %s scale=%.2f disp=%s var=%d"),
			*Stem, *Location.ToString(), UniformScale, *Disposition, IdleVariant));
		// A real component (transient, never registered — no RHI is touched) rather than null, so
		// the leaf classes take their body-carrying path: they register it for teardown, gate it on
		// dormancy, and route SetAnimation/SetDisposition through it.
		return NewComponent<USkeletalMeshComponent>();
	}
	virtual bool RefreshNpcIdle(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("RefreshNpcIdle %s disp=%s var=%d"), *Stem, *Disposition, IdleVariant));
		return Body != nullptr;
	}
	virtual bool PlayNpcClip(USkeletalMeshComponent* Body, const FString& Stem, const FString& ClipName,
		bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayNpcClip %s %s loop=%d"), *Stem, *ClipName, bLoop ? 1 : 0));
		if (OutSeconds)
		{
			*OutSeconds = ClipSeconds;   // a beat's OnEndSequence schedules off this
		}
		return Body != nullptr;
	}
	virtual bool PlayCinematicClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& AnimSetModel, const FString& BoneRoot, const FString& ClipName,
		bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayCinematicClip %s %s %s %s"), *Stem, *AnimSetModel,
			*BoneRoot, *ClipName));
		if (OutSeconds)
		{
			*OutSeconds = ClipSeconds;
		}
		return bCinematicClipsResolve && Body != nullptr;
	}
	virtual bool SeekCinematicClip(USkeletalMeshComponent* Body, float PositionSeconds) override
	{
		Record(FString::Printf(TEXT("SeekCinematicClip %.3f"), PositionSeconds));
		return Body != nullptr;
	}
	virtual void StopCinematicClip(USkeletalMeshComponent*) override { Record(TEXT("StopCinematicClip")); }
	// Test-controlled model path -> v4 animated-prop stem.
	TMap<FString, FString> AnimatedPropModels;
	virtual FString AnimatedPropStemForModel(const FString& ModelPath) const override
	{
		if (const FString* Stem = AnimatedPropModels.Find(ModelPath))
		{
			return *Stem;
		}
		return FString();
	}
	virtual USkeletalMeshComponent* BuildAnimatedPropVisual(const FString& Stem,
		const FVector& Location, const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildAnimatedPropVisual %s %s scale=%.2f"),
			*Stem, *Location.ToString(), UniformScale));
		return NewComponent<USkeletalMeshComponent>();
	}
	virtual bool PlayAnimatedPropClip(USkeletalMeshComponent* Body, const FString& Stem,
		const FString& ClipName, bool bLoop, float* OutSeconds) override
	{
		Record(FString::Printf(TEXT("PlayAnimatedPropClip %s %s loop=%d"),
			*Stem, *ClipName, bLoop ? 1 : 0));
		if (OutSeconds) { *OutSeconds = ClipSeconds; }
		return Body != nullptr;
	}
	virtual void ApplyAnimatedPropSkin(USkeletalMeshComponent*, const FString& StaticStem,
		int32 Family) override
	{
		Record(FString::Printf(TEXT("ApplyAnimatedPropSkin %s family=%d"), *StaticStem, Family));
	}

	// The authored length every stub clip reports. A scripted_sequence's OnEndSequence lands here.
	float ClipSeconds = 1.0f;
	// Whether a cinematic anim set resolves. Default false, which is the state of the world until
	// PL16's banks are exported — a scene must run its timeline and outputs either way.
	bool bCinematicClipsResolve = false;

	virtual UStaticMeshComponent* BuildPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildPropVisual %s %s scale=%.2f"), *Stem, *Location.ToString(), UniformScale));
		return NewComponent<UStaticMeshComponent>();
	}
	virtual UStaticMeshComponent* BuildPhysPropVisual(const FString& Stem, const FVector& Location,
		const FQuat& Rotation, float UniformScale) override
	{
		Record(FString::Printf(TEXT("BuildPhysPropVisual %s %s scale=%.2f"), *Stem, *Location.ToString(), UniformScale));
		return NewComponent<UStaticMeshComponent>();
	}
	virtual void ApplyPropSkin(UStaticMeshComponent* Comp, const FString& Stem, int32 Family) override
	{
		Record(FString::Printf(TEXT("ApplyPropSkin %s family=%d"), *Stem, Family));
	}
	virtual USkeletalMeshComponent* BuildPlayerVisual(const FString& Stem,
		const FString& Disposition, int32 IdleVariant) override
	{
		Record(FString::Printf(TEXT("BuildPlayerVisual %s disp=%s var=%d"),
			*Stem, *Disposition, IdleVariant));
		return Stem.IsEmpty() ? nullptr : NewComponent<USkeletalMeshComponent>();
	}
	virtual void ClearPlayerVisual() override
	{
		Record(TEXT("ClearPlayerVisual"));
	}

	virtual bool GetPlayerViewPoint(FVector& OutLocation, FRotator& OutRotation) const override
	{
		if (!bHasPlayer)
		{
			return false;
		}
		OutLocation = PlayerLocation;
		OutRotation = PlayerRotation;
		return true;
	}
	virtual bool GetPlayerOrigin(FVector& OutLocation, float& OutYaw) const override
	{
		if (!bHasPlayer)
		{
			return false;
		}
		OutLocation = PlayerLocation;
		OutYaw = PlayerRotation.Yaw;
		return true;
	}
	virtual void TeleportPlayer(const FVector& FeetOrigin, float Yaw) override
	{
		Record(FString::Printf(TEXT("TeleportPlayer %s yaw=%.1f"), *FeetOrigin.ToString(), Yaw));
		bHasPlayer = true;
		PlayerLocation = FeetOrigin;
		PlayerRotation = FRotator(0.f, Yaw, 0.f);
	}
	virtual void DamagePlayer(float Amount) override
	{
		Record(FString::Printf(TEXT("DamagePlayer %.1f"), Amount));
		DamageTaken += Amount;
	}
	virtual FElysiumEntityHandle TraceUseCursor(const FVector& Start, const FVector& End) const override
	{
		return UseCursorHit;
	}
	virtual int32 PushCameraShot(const FString& ShotFile, const FElysiumEntityHandle& Subject) override
	{
		Record(FString::Printf(TEXT("PushCameraShot %s"), *ShotFile));
		return ++NextCameraShotId;
	}
	virtual int32 PushCameraShotValue(const FElysiumCameraShot& Shot) override
	{
		Record(FString::Printf(TEXT("PushCameraShotValue %s"), *Shot.DebugName));
		LastCameraShot = Shot;
		return ++NextCameraShotId;
	}
	virtual bool UpdateCameraShotValue(int32 ShotId, const FElysiumCameraShot& Shot) override
	{
		Record(FString::Printf(TEXT("UpdateCameraShotValue %d %s"), ShotId, *Shot.DebugName));
		LastCameraShot = Shot;
		return ShotId > 0;
	}
	virtual bool PopCameraShot(int32 ShotId, float BlendOutSeconds = -1.0f) override
	{
		Record(FString::Printf(TEXT("PopCameraShot %d blend=%.2f"), ShotId, BlendOutSeconds));
		return ShotId > 0;
	}

	// --- IElysiumAudio ---------------------------------------------------------------------
	virtual FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params) override
	{
		Record(FString::Printf(TEXT("PlayVoice %s vol=%.2f loop=%d offset=%.3f"), *Rel, Params.Volume,
			Params.bLooping ? 1 : 0, Params.StartTimeSeconds));
		FElysiumAudioVoiceHandle H;
		H.Id = ++NextVoiceId;
		LiveVoices.Add(H.Id);
		return H;
	}
	virtual void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds) override
	{
		Record(FString::Printf(TEXT("StopVoice %d fade=%.2f"), Handle.Id, FadeSeconds));
		LiveVoices.Remove(Handle.Id);
	}
	virtual void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume) override
	{
		Record(FString::Printf(TEXT("SetVoiceVolume %d %.2f"), Handle.Id, Volume));
	}
	virtual bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const override
	{
		return LiveVoices.Contains(Handle.Id);
	}
	virtual void FadeInScheme(const FString& SchemeRel, const FVector& Anchor, float FadeSeconds) override
	{
		Record(FString::Printf(TEXT("FadeInScheme %s fade=%.2f"), *SchemeRel, FadeSeconds));
		ActiveScheme = SchemeRel;
	}
	virtual void FadeOutScheme(const FString& SchemeRel, float FadeSeconds) override
	{
		Record(FString::Printf(TEXT("FadeOutScheme %s fade=%.2f"), *SchemeRel, FadeSeconds));
		if (ActiveScheme == SchemeRel)
		{
			ActiveScheme.Reset();
		}
	}
	virtual FString ActiveSchemeRel() const override { return ActiveScheme; }

	// --- IElysiumTravel --------------------------------------------------------------------
	virtual void RequestLandmarkTravel(const FString& Map, const FString& Landmark,
		const FVector& Offset, float Yaw) override
	{
		Record(FString::Printf(TEXT("RequestLandmarkTravel %s@%s off=%s yaw=%.1f"),
			*Map, *Landmark, *Offset.ToString(), Yaw));
	}
	virtual void ChangeMap(const FString& Map) override
	{
		Record(FString::Printf(TEXT("ChangeMap %s"), *Map));
	}

	// --- IElysiumPresenter -----------------------------------------------------------------
	virtual void StartFade(const FLinearColor& Color, float Duration, float HoldTime, float MaxAlpha,
		bool bFadeIn, bool bAutoReverse) override
	{
		Record(FString::Printf(TEXT("StartFade dur=%.2f hold=%.2f alpha=%.2f in=%d rev=%d"),
			Duration, HoldTime, MaxAlpha, bFadeIn ? 1 : 0, bAutoReverse ? 1 : 0));
	}
	virtual void OpenSign(const FElysiumEntityHandle& Owner, const TSharedPtr<const FElysiumSignData>& Data,
		float FadeInSeconds) override
	{
		Record(FString::Printf(TEXT("OpenSign #%d fade=%.2f"), Owner.Index, FadeInSeconds));
		OpenSignOwner = Owner;
	}
	virtual void CloseSign() override
	{
		Record(TEXT("CloseSign"));
		OpenSignOwner = FElysiumEntityHandle::Invalid();
	}
	virtual void OpenDialog(const FElysiumEntityHandle& Owner, FElysiumDlgConversation& Conversation) override
	{
		Record(FString::Printf(TEXT("OpenDialog #%d"), Owner.Index));
		OpenDialogOwner = Owner;
	}
	virtual void CloseDialog() override
	{
		Record(TEXT("CloseDialog"));
		OpenDialogOwner = FElysiumEntityHandle::Invalid();
	}

	FElysiumEntityHandle OpenSignOwner;
	FElysiumEntityHandle OpenDialogOwner;
	FString ActiveScheme;

private:
	void Record(FString&& Line) const { Calls.Add(MoveTemp(Line)); }

	// Transient components handed back by the Build* calls, kept rooted for the test's lifetime so
	// GC cannot reclaim one while an entity still holds it.
	template <typename T>
	T* NewComponent()
	{
		T* Comp = NewObject<T>(GetTransientPackage());
		Spawned.Emplace(Comp);
		return Comp;
	}
	TArray<TStrongObjectPtr<UActorComponent>> Spawned;

	int32 NextVoiceId = 0;
	int32 NextCameraShotId = 0;
	TSet<int32> LiveVoices;
};

#endif   // WITH_DEV_AUTOMATION_TESTS
