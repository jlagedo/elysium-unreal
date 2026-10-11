#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumAudioSubsystem.h"
#include "Audio/ElysiumSoundScriptSource.h"
#include "ElysiumContentPaths.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumUserSettings.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "PlatformCryptoContextIncludes.h"
#include "Misc/SecureHash.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundAttenuation.h"

namespace
{
	class FSoundScriptNativeBranch final : public IAutomationLatentCommand
	{
	public:
		explicit FSoundScriptNativeBranch(FAutomationTestBase& InTest) : Test(InTest) {}
		~FSoundScriptNativeBranch()
		{
			if (Game)
			{
				Game->Shutdown();
				if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
				Game->RemoveFromRoot();
			}
		}
		bool Update() override
		{
			if (!Game)
			{
				if (FParse::Param(FCommandLine::Get(), TEXT("nosound")))
				{
					Test.AddError(TEXT("native branch requires public test --native-audio")); return true;
				}
				for (const auto& Source : {TPair<FString,FString>(TEXT("scripts/game_sounds_manifest.txt"), TEXT("ea09cd8ce3bfb9a5d5ab31be40ee78e31854de15e91d7bf1ed9f01ce5b236f02")),
					TPair<FString,FString>(TEXT("scripts/game_sounds_surfaceproperties.txt"), TEXT("e5de11ef6a648b45cb2196af15aaa47bd3d3a989b14f1bb7986430f692019eb6"))})
				{
					TArray<uint8> Bytes, Digest;
					FEncryptionContext Crypto;
					if (!FFileHelper::LoadFileToArray(Bytes, *FElysiumContentPaths::AudioScriptFile(Source.Key))
						|| !Crypto.CalcSHA256(MakeArrayView(Bytes), Digest) || Digest.Num() != 32)
					{
						Test.AddError(TEXT("missing/unhashable deployed source: ") + Source.Key); return true;
					}
					const FString DigestHex = BytesToHex(Digest.GetData(), Digest.Num()).ToLower();
					Test.TestEqual(TEXT("actual deployed shipped source SHA"), DigestHex, Source.Value);
					Test.AddInfo(FString::Printf(TEXT("deployed path=%s bytes=%d sha256=%s"), *Source.Key, Bytes.Num(), *DigestHex));
				}
				FString Provenance;
				if (!FFileHelper::LoadFileToString(Provenance, *FElysiumContentPaths::AudioScriptFile(TEXT("scripts/_source_provenance.json"))))
				{
					Test.AddError(TEXT("deployed source provenance absent")); return true;
				}
				Test.AddInfo(TEXT("deployed origin: ") + Provenance);
				Game = NewObject<UGameInstance>(GEngine); Game->AddToRoot();
				Game->InitializeStandalone(FName(TEXT("SoundScriptNativeBranch")));
				World = Game->GetWorld();
				Audio = Game->GetSubsystem<UElysiumAudioSubsystem>();
				if (!World || !Audio) { Test.AddError(TEXT("fixture world/audio host absent")); return true; }
				World->bAllowAudioPlayback = true;
				if (!World->GetAudioDevice().IsValid()) { Test.AddError(TEXT("actual native audio device absent")); return true; }
				Audio->RefreshOutputLatency();
				const auto& Latency = Audio->OutputLatency();
				Test.AddInfo(FString::Printf(TEXT("world=%s device=%s api=%s sampleRate=%d"), *World->GetName(), *Latency.DeviceName, *Latency.PlatformApi, Latency.SampleRate));
				if (!Latency.bDeviceQueried || Latency.DeviceName.Contains(TEXT("null"), ESearchCase::IgnoreCase)
					|| Latency.PlatformApi.Contains(TEXT("null"), ESearchCase::IgnoreCase))
				{
					Test.AddError(TEXT("fixture cannot bless a null/nonusable native output")); return true;
				}
				Started = FPlatformTime::Seconds();
			}
			Audio->TickAudio(0.f);
			if (!Submitted)
			{
				if (!Audio->IsReadyForMapActivation())
				{
					if (FPlatformTime::Seconds() - Started < 60) { return false; }
					for (const auto& Row : Audio->ScriptReadinessEvidence()) { Test.AddInfo(Row); }
					Test.AddError(TEXT("native selected precache did not become ready")); return true;
				}
				// FIRST actual classification evidence; no guessed streaming/chunk-specific literals.
				for (const auto& Row : Audio->ScriptReadinessEvidence()) { Test.AddInfo(Row); }
				const auto* Source = Audio->SoundScriptTable();
				Test.TestTrue(TEXT("actual cached source availability binding validated"), Source->bAvailabilityValidated);
				Test.TestEqual(TEXT("shipped selected accepted entries"), Source->Table.Count(), 48);
				FString Error;
				if (!Audio->ResolveSoundScript(TEXT("concrete.Impact"), Params, Error))
				{
					Test.AddError(TEXT("shipped resolve failed: ") + Error); return true;
				}
				Test.AddInfo(FString::Printf(TEXT("production script=concrete.Impact wave=%s volume=%g pitch=%d level=%d channel=%d ownerOnly=%d GT=%d"),
					*Params.SoundName, Params.Volume, Params.Pitch, Params.SoundLevel, Params.Channel, Params.bPlayToOwnerOnly, IsInGameThread()));
				FElysiumAudioRequest Request;
				Request.Owner = {EElysiumAudioOwnerKind::GameplaySystem, TEXT("native-branch"), 730001};
				Request.Placement.Location = FVector(0,0,0); // already Unreal centimetres, fixed placement
				Handle = Audio->SubmitResolvedScript(Params, Request, Error);
				if (!Handle.IsValid()) { Test.AddError(TEXT("typed script submit failed: ") + Error); return true; }
				Submitted = true;
			}
			for (const auto& Voice : Audio->ActiveVoices())
			{
				if (Voice.Handle != Handle || !Voice.Comp) { continue; }
				Test.AddInfo(FString::Printf(TEXT("native voice class=%s wave=%s state=%d playing=%d gain=%g pitch=%g fixed=%s GT=%d"),
					*Voice.Comp->GetClass()->GetName(), *Voice.Wave->GetPathName(), static_cast<int32>(Voice.Event.State), Voice.Comp->IsPlaying(),
					Voice.Request.Gain, Voice.Comp->PitchMultiplier, *Voice.Comp->GetComponentLocation().ToString(), IsInGameThread()));
				Test.TestTrue(TEXT("native component actually playing"), Voice.Comp->IsPlaying());
				Test.TestEqual(TEXT("typed value gain"), Voice.Request.Gain, Params.Volume);
				Test.TestEqual(TEXT("percentage converted once"), Voice.Comp->PitchMultiplier, static_cast<float>(Params.Pitch)/100.f);
				const UElysiumUserSettings* Settings = Cast<UElysiumUserSettings>(GEngine->GetGameUserSettings());
				const float CategoryGain = Settings ? Settings->AudioVolume(Voice.Request.Category) : 1.f;
				Test.TestEqual(TEXT("native component gain follows existing mixer policy"), Voice.Comp->VolumeMultiplier,
					Params.Volume * Audio->MasterGain() * CategoryGain);
				Test.TestEqual(TEXT("fixed point location"), Voice.Comp->GetComponentLocation(), FVector::ZeroVector);
				Test.TestNotNull(TEXT("authored level attenuation applied"), Voice.Comp->GetAttenuationSettingsToApply());
				if (const auto* Attenuation = Voice.Comp->GetAttenuationSettingsToApply())
				{
					Test.TestEqual(TEXT("native authored level curve reach"), Attenuation->FalloffDistance,
						ElysiumSoundLevel::MakeAttenuation(Params.SoundLevel).FalloffDistance);
				}
				Audio->RetireMapEpoch(730001);
				Test.TestTrue(TEXT("native voice retired"), Audio->ActiveVoices().IsEmpty());
				Test.TestTrue(TEXT("table/resources survive map teardown"), Audio->IsReadyForMapActivation());
				return true;
			}
			if (FPlatformTime::Seconds() - Started > 60) { Test.AddError(TEXT("actual native component never realized")); return true; }
			return false;
		}
	private:
		FAutomationTestBase& Test;
		UGameInstance* Game = nullptr;
		UWorld* World = nullptr;
		UElysiumAudioSubsystem* Audio = nullptr;
		double Started = 0;
		bool Submitted = false;
		ElysiumSoundScript::FSoundParameters Params;
		FElysiumVoiceHandle Handle;
	};

	// A second fixture keeps the first production-branch witness unchanged. All cancellation
	// stimuli are real outstanding loads or live native components, never a replaced callback.
	class FSoundScriptLifecycle final : public IAutomationLatentCommand
	{
	public:
		explicit FSoundScriptLifecycle(FAutomationTestBase& InTest) : Test(InTest) {}
		~FSoundScriptLifecycle() { DestroyHost(); }
		bool Update() override
		{
			if (Phase == 0)
			{
				if (!CreateHost()) { return true; }
				EventHandle = Audio->OnVoiceEvent().AddLambda([this](const FElysiumVoiceEvent& Event) {
					Events.Add(Event);
					Test.AddInfo(FString::Printf(TEXT("observed voice event handle=%u/%u state=%d completion=%d path=%s"),
						Event.Handle.Slot, Event.Handle.Generation, static_cast<int32>(Event.State), static_cast<int32>(Event.Completion), *Event.ResolvedPath));
				});
				const auto* Source = Audio->SoundScriptTable();
				if (!Source || !Source->bAvailabilityValidated) { Test.AddError(TEXT("lifecycle deployed source is not bound")); return true; }
				NameIndex = Source->Table.Lookup(TEXT("concrete.Impact"));
				NameCount = Source->Table.NameCount();
				Audio->TickAudio(0.f);
				const auto Before = RecordEvidence(TEXT("pending before reload"));
				if (!HasPending(Before)) { Test.AddError(TEXT("INVALID lifecycle stimulus: reload has no observed pending native work")); return true; }
				FString Error;
				if (!Audio->ReloadSoundScripts(Error)) { Test.AddError(TEXT("pending reload failed: ") + Error); return true; }
				const auto After = RecordEvidence(TEXT("after pending reload"));
				Test.TestTrue(TEXT("pending reload advances generation"), Generation(Before) != Generation(After));
				Test.TestTrue(TEXT("pending reload releases native adapter references"), IsReleased(After));
				Test.TestEqual(TEXT("reload retains existing name index"), Audio->SoundScriptTable()->Table.Lookup(TEXT("CONCRETE.IMPACT")), NameIndex);
				Test.TestEqual(TEXT("reload retains name symbols"), Audio->SoundScriptTable()->Table.NameCount(), NameCount);
				Phase = 1; return false;
			}
			if (Phase == 1 || Phase == 2)
			{
				Audio->TickAudio(0.f);
				if (!Audio->IsSoundScriptNativeReady()) { return TimedOut(TEXT("lifecycle reload never reached native readiness")); }
				const auto ReadyRows = RecordEvidence(Phase == 1 ? TEXT("ready after pending reload") : TEXT("ready for copied submit"));
				Test.TestEqual(TEXT("new generation owns measured selected native assets"), AssetCount(ReadyRows), 121);
				Test.TestEqual(TEXT("new generation owns valid streaming chunk handles"), ValidChunkCount(ReadyRows), 121);
				Test.TestTrue(TEXT("new generation converges without stale pending work"), ReadyRows[1].Contains(TEXT(" pending=0 retained=121 error= device=1")));
				Test.TestTrue(TEXT("cached source binding survives reload"), Audio->SoundScriptTable()->bAvailabilityValidated);
				FString Error;
				if (Phase == 1)
				{
					if (!Audio->ResolveSoundScript(TEXT("concrete.Impact"), CopiedParams, Error)) { Test.AddError(TEXT("copy resolve failed: ") + Error); return true; }
					Test.AddInfo(FString::Printf(TEXT("copied before reload wave=%s gain=%g pitch=%d level=%d"),
						*CopiedParams.SoundName, CopiedParams.Volume, CopiedParams.Pitch, CopiedParams.SoundLevel));
					if (!Audio->ReloadSoundScripts(Error)) { Test.AddError(TEXT("ready reload failed: ") + Error); return true; }
					const auto Released = RecordEvidence(TEXT("ready chunk ownership released by reload"));
					Test.TestTrue(TEXT("ready reload releases owned chunk and wave references"), IsReleased(Released));
					Phase = 2; return false;
				}
				// General prefetch composes the map barrier, but cannot suppress a ready script
				// during gameplay. This is real public async work, observed before its next tick.
				Audio->Prefetch(FElysiumAudioSource::Path(TEXT("surfaces/scrape1.wav")));
				if (Audio->PendingLoadCount() == 0) { Test.AddError(TEXT("INVALID prefetch stimulus: no observed pending public load")); return true; }
				Test.TestFalse(TEXT("pending general prefetch still gates map activation"), Audio->IsReadyForMapActivation());
				ElysiumSoundScript::FSoundParameters DuringPrefetch;
				Test.TestTrue(TEXT("ready selected resolve is independent of general prefetch"), Audio->ResolveSoundScript(TEXT("concrete.Impact"), DuringPrefetch, Error));
				for (int32 OwnerIndex = 0; OwnerIndex < 3; ++OwnerIndex)
				{
					FElysiumAudioRequest Request;
					Request.Owner = {EElysiumAudioOwnerKind::GameplaySystem, FString::Printf(TEXT("sound-lifecycle-%d"), OwnerIndex), 740001u + OwnerIndex};
					Request.Gain = 0.75f; Request.Pitch = 1.1f;
					Request.Placement.Location = FVector(OwnerIndex * 100.f, 0.f, 0.f);
					Owners.Add(Request.Owner);
					const auto SubmittedHandle = Audio->SubmitResolvedScript(CopiedParams, Request, Error);
					if (!SubmittedHandle.IsValid()) { Test.AddError(TEXT("copied submit failed: ") + Error); return true; }
					Handles.Add(SubmittedHandle);
				}
				Phase = 3; return false;
			}
			if (Phase == 3)
			{
				Audio->TickAudio(0.f);
				for (int32 OwnerIndex = 0; OwnerIndex < Handles.Num(); ++OwnerIndex)
				{
					const auto* Voice = FindVoice(Handles[OwnerIndex]);
					if (!Voice || HasCompletion(Handles[OwnerIndex]))
					{ Test.AddError(TEXT("INVALID cancellation stimulus: selected one-shot completed before all live controls")); return true; }
					if (!Voice->Comp || !Voice->Comp->IsPlaying()) { return TimedOut(TEXT("lifecycle controls never all became native playing voices")); }
				}
				// The three live components are established before any operation; no frame advances here.
				for (int32 OwnerIndex = 0; OwnerIndex < Handles.Num(); ++OwnerIndex)
				{
					const auto* Voice = FindVoice(Handles[OwnerIndex]);
					Test.AddInfo(FString::Printf(TEXT("live owner=%s epoch=%llu handle=%u/%u playing=%d wave=%s gain=%g pitch=%g"),
						*Voice->Request.Owner.StableId, Voice->Request.Owner.MapEpoch, Voice->Handle.Slot, Voice->Handle.Generation,
						Voice->Comp->IsPlaying(), *Voice->Request.Source.LogicalPath, Voice->Request.Gain, Voice->Comp->PitchMultiplier));
					Test.TestEqual(TEXT("copied wave path survives descriptor reload"), Voice->Request.Source.LogicalPath, CopiedParams.SoundName);
					Test.TestEqual(TEXT("copied authored volume combines with caller gain"), Voice->Request.Gain, CopiedParams.Volume * 0.75f);
					Test.TestEqual(TEXT("copied pitch converts once and combines with caller pitch"), Voice->Comp->PitchMultiplier, static_cast<float>(CopiedParams.Pitch) / 100.f * 1.1f);
					Test.TestTrue(TEXT("caller ownership retained"), Voice->Request.Owner == Owners[OwnerIndex]);
					Test.TestEqual(TEXT("caller fixed placement retained"), Voice->Comp->GetComponentLocation(), FVector(OwnerIndex * 100.f, 0.f, 0.f));
					const auto* Attenuation = Voice->Comp->GetAttenuationSettingsToApply();
					Test.TestNotNull(TEXT("copied soundlevel attenuation retained"), Attenuation);
					if (Attenuation) { Test.TestEqual(TEXT("copied level curve reach"), Attenuation->FalloffDistance, ElysiumSoundLevel::MakeAttenuation(CopiedParams.SoundLevel).FalloffDistance); }
				}
				Audio->CancelOwner(Owners[0]);
				Test.TestTrue(TEXT("matching owner literal completion"), CompletedAs(Handles[0], EElysiumVoiceCompletion::OwnerCanceled));
				Test.TestTrue(TEXT("owner cancellation removes matching native handle"), FindVoice(Handles[0]) == nullptr);
				Test.TestTrue(TEXT("other epoch remains live after owner cancellation"), IsNativePlaying(Handles[1]));
				Test.TestTrue(TEXT("unrelated owner remains live after owner cancellation"), IsNativePlaying(Handles[2]));
				Audio->RetireMapEpoch(Owners[1].MapEpoch);
				Test.TestTrue(TEXT("matching epoch literal completion"), CompletedAs(Handles[1], EElysiumVoiceCompletion::MapEpochRetired));
				Test.TestTrue(TEXT("epoch retirement removes matching native handle"), FindVoice(Handles[1]) == nullptr);
				Test.TestTrue(TEXT("unrelated epoch retains live native component"), IsNativePlaying(Handles[2]));
				Test.TestFalse(TEXT("unrelated owner has no completion yet"), HasCompletion(Handles[2]));
				Test.TestTrue(TEXT("selected source/native ownership survives epoch teardown"), Audio->IsSoundScriptNativeReady());
				Audio->CancelOwner(Owners[2]);
				Test.TestTrue(TEXT("remaining owner literal completion"), CompletedAs(Handles[2], EElysiumVoiceCompletion::OwnerCanceled));
				Test.TestTrue(TEXT("all controlled voices retired"), Audio->ActiveVoices().IsEmpty());
				RecordEvidence(TEXT("ready ownership before shutdown"));
				ReadyShutdown = Audio;
				DestroyHost();
				ObserveShutdown(ReadyShutdown, TEXT("ready host after shutdown"));
				if (!CreateHost()) { return true; }
				Audio->TickAudio(0.f);
				const auto PendingRows = RecordEvidence(TEXT("pending before shutdown"));
				if (!HasPending(PendingRows)) { Test.AddError(TEXT("INVALID lifecycle stimulus: shutdown has no observed pending native work")); return true; }
				PendingShutdown = Audio;
				DestroyHost();
				ObserveShutdown(PendingShutdown, TEXT("pending host after shutdown"));
				Phase = 4; return false;
			}
			// Public automation ticks dispatch any scheduled engine work. Observe only adapter state,
			// not whether canceled delegates ran or whether the global stream cache evicted data.
			ObserveShutdown(ReadyShutdown, TEXT("ready host after callback frame"));
			ObserveShutdown(PendingShutdown, TEXT("pending host after callback frame"));
			return ++CallbackFrames >= 3;
		}
	private:
		bool CreateHost()
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("nosound"))) { Test.AddError(TEXT("native lifecycle requires --native-audio")); return false; }
			Game = NewObject<UGameInstance>(GEngine); Game->AddToRoot();
			Game->InitializeStandalone(FName(TEXT("SoundScriptLifecycle")));
			World = Game->GetWorld(); Audio = Game->GetSubsystem<UElysiumAudioSubsystem>();
			if (!World || !Audio || !World->GetAudioDevice().IsValid()) { Test.AddError(TEXT("lifecycle world/native device absent")); return false; }
			World->bAllowAudioPlayback = true; Audio->RefreshOutputLatency();
			const auto& Latency = Audio->OutputLatency();
			Test.AddInfo(FString::Printf(TEXT("lifecycle device=%s api=%s sampleRate=%d"), *Latency.DeviceName, *Latency.PlatformApi, Latency.SampleRate));
			if (!Latency.bDeviceQueried || Latency.DeviceName.Contains(TEXT("null"), ESearchCase::IgnoreCase) || Latency.PlatformApi.Contains(TEXT("null"), ESearchCase::IgnoreCase))
			{ Test.AddError(TEXT("lifecycle requires physically queried usable output")); return false; }
			Started = FPlatformTime::Seconds(); return true;
		}
		void DestroyHost()
		{
			if (!Game) { return; }
			if (Audio && EventHandle.IsValid()) { Audio->OnVoiceEvent().Remove(EventHandle); EventHandle.Reset(); }
			Game->Shutdown();
			if (World) { GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
			Game->RemoveFromRoot(); Game = nullptr; World = nullptr; Audio = nullptr;
		}
		TArray<FString> RecordEvidence(const TCHAR* Label)
		{
			const auto Rows = Audio->ScriptReadinessEvidence();
			for (const auto& Row : Rows) { Test.AddInfo(FString(Label) + TEXT(": ") + Row); }
			return Rows;
		}
		static bool HasPending(const TArray<FString>& Rows)
		{
			return Rows.ContainsByPredicate([](const FString& Row) {
				return (Row.StartsWith(TEXT("generation=")) && !Row.Contains(TEXT(" pending=0 ")))
					|| (Row.StartsWith(TEXT("asset=")) && Row.Contains(TEXT(" pending=1 ")));
			});
		}
		static FString Generation(const TArray<FString>& Rows)
		{
			for (const auto& Row : Rows) { if (Row.StartsWith(TEXT("generation="))) { return Row.Left(Row.Find(TEXT(" "))); } }
			return {};
		}
		static int32 AssetCount(const TArray<FString>& Rows)
		{ return Rows.FilterByPredicate([](const FString& Row) { return Row.StartsWith(TEXT("asset=")); }).Num(); }
		static int32 ValidChunkCount(const TArray<FString>& Rows)
		{ return Rows.FilterByPredicate([](const FString& Row) { return Row.StartsWith(TEXT("asset=")) && Row.Contains(TEXT(" chunk1=1 ")); }).Num(); }
		static bool IsReleased(const TArray<FString>& Rows)
		{
			return AssetCount(Rows) == 0 && Rows.ContainsByPredicate([](const FString& Row) {
				return Row.StartsWith(TEXT("generation=")) && Row.Contains(TEXT(" pending=0 retained=0 "));
			});
		}
		void ObserveShutdown(TWeakObjectPtr<UElysiumAudioSubsystem> WeakAudio, const TCHAR* Label)
		{
			if (auto* RetiredAudio = WeakAudio.Get())
			{
				const auto Rows = RetiredAudio->ScriptReadinessEvidence();
				for (const auto& Row : Rows) { Test.AddInfo(FString(Label) + TEXT(": ") + Row); }
				Test.TestTrue(TEXT("shutdown host releases native handle ownership"), Rows.Contains(TEXT("host absent")) && AssetCount(Rows) == 0);
				Test.TestTrue(TEXT("shutdown removes descriptor host"), RetiredAudio->SoundScriptTable() == nullptr);
				Test.TestTrue(TEXT("shutdown has no late voice"), RetiredAudio->ActiveVoices().IsEmpty());
			}
			else { Test.AddInfo(FString(Label) + TEXT(": subsystem UObject no longer live")); }
		}
		const FElysiumAudioVoice* FindVoice(FElysiumVoiceHandle InHandle) const
		{ return Audio->ActiveVoices().FindByPredicate([InHandle](const auto& Voice) { return Voice.Handle == InHandle; }); }
		bool IsNativePlaying(FElysiumVoiceHandle InHandle) const
		{ const auto* Voice = FindVoice(InHandle); return Voice && Voice->Comp && Voice->Comp->IsPlaying(); }
		bool HasCompletion(FElysiumVoiceHandle InHandle) const
		{ return Events.ContainsByPredicate([InHandle](const auto& Event) { return Event.Handle == InHandle && Event.Completion != EElysiumVoiceCompletion::None; }); }
		bool CompletedAs(FElysiumVoiceHandle InHandle, EElysiumVoiceCompletion Completion) const
		{ return Events.ContainsByPredicate([InHandle, Completion](const auto& Event) { return Event.Handle == InHandle && Event.Completion == Completion; }); }
		bool TimedOut(const TCHAR* Message)
		{ if (FPlatformTime::Seconds() - Started < 60) { return false; } RecordEvidence(TEXT("timeout")); Test.AddError(Message); return true; }
		FAutomationTestBase& Test;
		UGameInstance* Game = nullptr;
		UWorld* World = nullptr;
		UElysiumAudioSubsystem* Audio = nullptr;
		TWeakObjectPtr<UElysiumAudioSubsystem> ReadyShutdown, PendingShutdown;
		FDelegateHandle EventHandle;
		TArray<FElysiumVoiceEvent> Events;
		TArray<FElysiumAudioOwner> Owners;
		TArray<FElysiumVoiceHandle> Handles;
		ElysiumSoundScript::FSoundParameters CopiedParams;
		int32 Phase = 0, CallbackFrames = 0, NameIndex = -1, NameCount = 0;
		double Started = 0;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptInitialNative,
	"Elysium.Content.SoundScriptProducer.InitialNativeBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSoundScriptInitialNative::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoundScriptNativeBranch(*this));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptNativeLifecycle,
	"Elysium.Content.SoundScriptProducer.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSoundScriptNativeLifecycle::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoundScriptLifecycle(*this));
	return true;
}
#endif
