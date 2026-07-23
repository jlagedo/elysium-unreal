// P6.3 — ambient_generic: VtMB's point sound (66 on the tutorial, 1,631 across 95 maps;
// audio_pipeline.md §7). A plain-C++ FElysiumEntity leaf (R1) that plays a WAV/MP3 at its origin
// through the GI audio subsystem's voice pool, honouring the Source spawnflags (everywhere / start-
// silent / not-looped) and the I/O input surface the maps wire (PlaySound 903, StopSound 248,
// Volume 36, FadeIn/FadeOut 2 each — entity_io.md). Registration follows ElysiumStarterClasses.cpp.
//
// Provenance for the field semantics is the decompiled ambient_generic (vampire.dll @0x1000e8a9) +
// audio_pipeline.md §7: `message` = wav path, `health` = VOLUME on a 0–10 scale (NOT hit points),
// `radius` = falloff in Source units, `pitch` = 100-based, `SourceEntityName` = parent to a mover.

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAmbient, Log, All);

namespace
{
	// Source ambient_generic spawnflags (audio_pipeline.md §7 / stock Source SF layout).
	constexpr int32 SF_AMBIENT_EVERYWHERE  = 0x01;   // omni: non-spatialized, no distance falloff
	constexpr int32 SF_AMBIENT_START_SILENT = 0x10;  // do not play on spawn; wait for PlaySound
	constexpr int32 SF_AMBIENT_NOT_LOOPED  = 0x20;   // one-shot (else the sound loops)

	// Source keyvalue `radius`/`pitch` are raw Source units; geometry (origins) is already cm (the
	// UE_ convention). Radius converts inches→cm; pitch is a ratio, unitless.
	constexpr float AmbientInchToCm = 2.54f;

	// A raw keyvalue read (the value stays on the def when the key isn't a mapped base field).
	// File-unique names (Ambient*) so this TU can share a unity blob with the other class files'
	// identical helpers (KeyFloat in ElysiumLogicClasses, the mover's inch-to-cm, ...).
	float AmbientKeyFloat(const FElysiumEntityDef* Def, const TCHAR* Key, float Default)
	{
		if (Def)
		{
			if (const FString* V = Def->Keys.Find(Key))
			{
				return FCString::Atof(**V);
			}
		}
		return Default;
	}
	bool AmbientKeyBool(const FElysiumEntityDef* Def, const TCHAR* Key)
	{
		const FString* V = Def ? Def->Keys.Find(Key) : nullptr;
		return V && FCString::Atoi(**V) != 0;
	}
}

// ============================================================================================
// ambient_generic
// ============================================================================================

class FElysiumAmbientGeneric final : public FElysiumEntity
{
public:
	// --- Inputs (registered below; reach here through the class-chain thunk) ---------------
	void InputPlaySound()  { bDesiredPlaying = true;  StartVoice(0.f); }
	void InputStopSound()  { bDesiredPlaying = false; StopActiveVoice(0.f); }
	void InputToggleSound()
	{
		if (bDesiredPlaying) { InputStopSound(); } else { InputPlaySound(); }
	}
	// VtMB `Volume` input param is 0–10 (Source convention); scale to the 0..1 linear multiplier.
	void InputVolume(const FElysiumVariant& Param)
	{
		Volume = FMath::Clamp(Param.ToFloat() / 10.f, 0.f, 1.f);
		if (UElysiumAudioSubsystem* Audio = World ? World->AudioSubsystem() : nullptr)
		{
			Audio->SetVoiceVolume(VoiceHandle, Volume);   // no-op if no voice is running
		}
	}
	void InputFadeIn(const FElysiumVariant& Param)  { bDesiredPlaying = true;  StartVoice(FMath::Max(Param.ToFloat(), 0.f)); }
	void InputFadeOut(const FElysiumVariant& Param) { bDesiredPlaying = false; StopActiveVoice(FMath::Max(Param.ToFloat(), 0.f)); }

	virtual void Spawn() override
	{
		SoundRel = Def ? Def->Keys.FindRef(TEXT("message")).Replace(TEXT("\\"), TEXT("/")) : FString();
		// `health` is repurposed as VOLUME 0–10 (§7). Missing → full (Source default 10).
		Volume = FMath::Clamp(AmbientKeyFloat(Def, TEXT("health"), 10.f) / 10.f, 0.f, 1.f);
		RadiusCm = FMath::Max(AmbientKeyFloat(Def, TEXT("radius"), 1250.f) * AmbientInchToCm, 1.f);
		Pitch = FMath::Max(AmbientKeyFloat(Def, TEXT("pitch"), 100.f) / 100.f, 0.01f);
		SourceEntityName = Def ? Def->Keys.FindRef(TEXT("SourceEntityName")) : FString();

		bEverywhere = (SpawnFlags & SF_AMBIENT_EVERYWHERE) != 0;
		// Loops unless explicitly NOT_LOOPED, or forced by the VtMB `flag_force_looping` key.
		bLoop = AmbientKeyBool(Def, TEXT("flag_force_looping")) || (SpawnFlags & SF_AMBIENT_NOT_LOOPED) == 0;

		// Play on spawn unless Start Silent (§7) or born hidden (StartHidden). A start-silent sound
		// waits for a PlaySound input; a hidden one plays when ScriptUnhide reveals it (if desired).
		const bool bStartSilent = (SpawnFlags & SF_AMBIENT_START_SILENT) != 0;
		bDesiredPlaying = !bStartSilent;
		// Defer the initial play to the first think: brush bodies are built after the spawn pass, so a
		// SourceEntityName parent (a mover) does not exist yet in Spawn(). One think later it does.
		if (bDesiredPlaying && !IsInert() && !SoundRel.IsEmpty())
		{
			NextThink = 0.f;
		}
	}

	virtual void Think() override
	{
		NextThink = ELYSIUM_NEVER_THINK;   // one-shot: the deferred initial play only
		if (bDesiredPlaying && !IsInert())
		{
			StartVoice(0.f);
		}
	}

	// Mirror dormancy onto the voice: a hidden/killed sound goes silent; on un-hide it resumes if it
	// was meant to be playing. Base first (gates the body — though point sounds have none).
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		if (IsInert())
		{
			StopActiveVoice(0.f);
		}
		else if (bDesiredPlaying)
		{
			StartVoice(0.f);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Sound"), SoundRel.IsEmpty() ? TEXT("(none)") : SoundRel);
		Out.Emplace(TEXT("Playing"), IsPlaying() ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Desired"), bDesiredPlaying ? TEXT("play") : TEXT("stopped"));
		Out.Emplace(TEXT("Volume"), FString::Printf(TEXT("%.2f (health %.0f/10)"), Volume, Volume * 10.f));
		Out.Emplace(TEXT("Pitch"), FString::Printf(TEXT("%.2f"), Pitch));
		Out.Emplace(TEXT("Placement"), bEverywhere ? TEXT("everywhere (2D)")
			: FString::Printf(TEXT("3D · radius %.0f cm%s"), RadiusCm,
				SourceEntityName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" · parent '%s'"), *SourceEntityName)));
		Out.Emplace(TEXT("Looping"), bLoop ? TEXT("yes") : TEXT("no (one-shot)"));
	}

private:
	bool IsPlaying() const
	{
		UElysiumAudioSubsystem* Audio = World ? World->AudioSubsystem() : nullptr;
		return Audio && Audio->IsVoicePlaying(VoiceHandle);
	}

	// (Re)start the voice — stop any prior one first so a repeated PlaySound restarts cleanly.
	void StartVoice(float FadeInSeconds)
	{
		UElysiumAudioSubsystem* Audio = World ? World->AudioSubsystem() : nullptr;
		if (!Audio || SoundRel.IsEmpty())
		{
			return;
		}
		Audio->StopVoice(VoiceHandle, 0.f);

		FElysiumPlayParams P;
		P.Volume = Volume;
		P.Pitch = Pitch;
		P.bLooping = bLoop;
		P.b3D = !bEverywhere;
		P.AttenuationRadiusCm = RadiusCm;
		P.FadeInSeconds = FadeInSeconds;
		P.Location = Def ? Def->Origin : FVector::ZeroVector;
		P.AttachTo = ResolveParentComponent();   // SourceEntityName's body, if any

		VoiceHandle = Audio->PlayVoice(SoundRel, P);
		if (!VoiceHandle.IsValid())
		{
			UE_LOG(LogElysiumAmbient, Verbose, TEXT("%s: PlaySound '%s' failed (missing/undecodable)"),
				*DebugString(), *SoundRel);
		}
	}

	void StopActiveVoice(float FadeOutSeconds)
	{
		if (UElysiumAudioSubsystem* Audio = World ? World->AudioSubsystem() : nullptr)
		{
			Audio->StopVoice(VoiceHandle, FadeOutSeconds);
		}
		VoiceHandle = FElysiumAudioVoiceHandle::Invalid();
	}

	// SourceEntityName parents the sound to a moving entity (§7, 154 uses). Resolve the named entity
	// and attach to its brush body so the sound tracks it. NPCs have no body yet (P8) → null → the
	// sound falls back to a world-static play at the def origin, which is acceptable.
	USceneComponent* ResolveParentComponent()
	{
		if (SourceEntityName.IsEmpty() || !World)
		{
			return nullptr;
		}
		if (const FElysiumEntity* Src = World->FindByName(SourceEntityName))
		{
			return Src->Body;   // UElysiumBrushComponent : UPrimitiveComponent : USceneComponent
		}
		return nullptr;
	}

	FString SoundRel;
	FString SourceEntityName;
	float   Volume = 1.f;        // 0..1 linear
	float   RadiusCm = 1250.f * AmbientInchToCm;
	float   Pitch = 1.f;
	bool    bEverywhere = false;
	bool    bLoop = true;
	bool    bDesiredPlaying = false;   // the intent (PlaySound/StopSound); the actual voice mirrors it

	FElysiumAudioVoiceHandle VoiceHandle;
};

// ============================================================================================
// Registration
// ============================================================================================

static TUniquePtr<FElysiumEntity> MakeAmbientGeneric() { return MakeUnique<FElysiumAmbientGeneric>(); }

static FElysiumClassRegistrar GRegAmbientGeneric(
	TEXT("ambient_generic"), ElysiumBaseClassName(), &MakeAmbientGeneric,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("PlaySound"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientGeneric&>(E).InputPlaySound(); });
		D.Input(TEXT("StopSound"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientGeneric&>(E).InputStopSound(); });
		D.Input(TEXT("ToggleSound"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientGeneric&>(E).InputToggleSound(); });
		D.Input(TEXT("Volume"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputVolume(A.Param); });
		D.Input(TEXT("FadeIn"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputFadeIn(A.Param); });
		D.Input(TEXT("FadeOut"),     [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputFadeOut(A.Param); });
	});
