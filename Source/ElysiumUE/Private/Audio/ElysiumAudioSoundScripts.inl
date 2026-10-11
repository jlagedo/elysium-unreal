// Included by ElysiumAudioSubsystem.cpp: host types and the existing typed service are shared here.
void UElysiumAudioSubsystem::ReleaseSoundScripts()
{
	check(IsInGameThread());
	++ScriptGeneration; // fences asset and chunk callbacks before any UObject access
	if (ScriptHost)
	{
		for (const auto& Load : ScriptHost->Loads) { if (Load) { Load->CancelHandle(); } }
		ScriptHost->Loads.Reset();
		ScriptHost->Native.Reset(); // releases public chunk handles
		ScriptHost->Device = FAudioDeviceHandle();
	}
	ScriptRetainedWaves.Reset();
}

bool UElysiumAudioSubsystem::ReloadSoundScripts(FString& Error)
{
	ReleaseSoundScripts();
	if (!ScriptHost) { ScriptHost = MakeShared<FElysiumScriptNativeHost>(); }
	ScriptHost->Source.Table.UseProcessMissingCache();
	ScriptHost->Error.Reset();
	ScriptHost->PendingAssets = 0;
	ScriptHost->bLoadStarted = false;
	ScriptHost->AttemptOutcomes.Reset();
	ElysiumSoundScript::FGameSoundFiles Files;
	ElysiumKeyValuesLoader::FKvDiskFileSystem ScriptFiles([](const FString& Filename)
	{
		return FElysiumContentPaths::AudioScriptFile(Filename);
	});
	bool bLoaded = ScriptHost->Source.Load(ScriptFiles, Files, Error);
	if (bLoaded)
	{
		FString Metadata;
		if (!FFileHelper::LoadFileToString(Metadata, *FElysiumContentPaths::AudioScriptFile(TEXT("scripts/_source_provenance.json"))))
		{ Error = TEXT("SoundEmitter source availability metadata missing; import sound-script-data"); bLoaded = false; }
		else { bLoaded = ScriptHost->Source.ValidateAvailability(Metadata, Error); }
	}
	ScriptHost->Error = Error;
	ScriptHost->Eligible = ScriptHost->Source.PrecacheAttempts();
	for (const FString& Warning : ScriptHost->Source.Warnings)
	{
		UE_LOG(LogElysiumAudio, Warning, TEXT("SoundEmitter: %s"), *Warning);
	}
	for (const auto& Entry : ScriptHost->Source.Entries)
	{
		const auto* Params = ScriptHost->Source.Table.Record(Entry.Node);
		if (ScriptHost->Source.Files[Entry.File].bPrecache && Params->bPrecache && Params->Waves.IsEmpty())
		{
			UE_LOG(LogElysiumAudio, Warning, TEXT("SoundEmitter: precache sound has no waves (node %d)"), Entry.Node);
		}
	}
	return bLoaded;
}

void UElysiumAudioSubsystem::PrepareSoundScripts()
{
	check(IsInGameThread());
	// The ordinary public automation boot explicitly disables output. Populate the authored table
	// there, but never initiate/claim native readiness or grant script playback permission.
	if (FParse::Param(FCommandLine::Get(), TEXT("nosound"))) { return; }
	if (!ScriptHost || !ScriptHost->Source.bLoaded || !ScriptHost->Source.bAvailabilityValidated) { return; }
	UWorld* World = GetWorld();
	if (!World) { return; } // population may precede world creation; activation remains gated
	if (!ScriptHost->Device.IsValid()) { ScriptHost->Device = World->GetAudioDevice(); }
	if (!ScriptHost->Device.IsValid()) { return; } // explicit pending device, never ready-empty
	const uint64 Generation = ScriptGeneration;
	const TWeakObjectPtr<UElysiumAudioSubsystem> WeakThis(this);
	if (!ScriptHost->bLoadStarted)
	{
		ScriptHost->bLoadStarted = true;
		TSet<FString> Seen;
		for (const auto& Attempt : ScriptHost->Eligible)
		{
			const FString& Authored = Attempt.Wave;
			FElysiumSoundAssetRow& Row = RowFor(NormalizeSourcePath(Authored));
			FString Failure;
			if (ElysiumSoundScript::CompleteAuthoredAbsence(Attempt.Availability, Row.bMissing, Failure))
			{
				ScriptHost->AttemptOutcomes.Add(TEXT("AuthoredAbsent"));
				UE_LOG(LogElysiumAudio, Warning, TEXT("SoundEmitter precache attempt=%d wave=%s outcome=AuthoredAbsent (verified source-bound install absence)"),
					ScriptHost->AttemptOutcomes.Num()-1, *Authored);
				continue;
			}
			if (!Failure.IsEmpty())
			{
				ScriptHost->Error = Failure + TEXT(": ") + Authored;
				ScriptHost->AttemptOutcomes.Add(ScriptHost->Error);
				UE_LOG(LogElysiumAudio, Error, TEXT("SoundEmitter: %s"), *ScriptHost->Error);
				continue;
			}
			ScriptHost->AttemptOutcomes.Add(TEXT("NativeQueued"));
			UE_LOG(LogElysiumAudio, Log, TEXT("SoundEmitter precache attempt=%d wave=%s outcome=NativeQueued"), ScriptHost->AttemptOutcomes.Num()-1, *Authored);
			TArray<FString> Objects{Row.ObjectPath};
			if (!Row.LoopObjectPath.IsEmpty()) { Objects.Add(Row.LoopObjectPath); }
			for (const FString& Object : Objects)
			{
				if (Seen.Contains(Object)) { continue; }
				Seen.Add(Object);
				const int32 Index = ScriptHost->Native.AddDefaulted();
				const FSoftObjectPath Path(Object);
				ScriptHost->Native[Index].Path = Path;
				++ScriptHost->PendingAssets;
				ScriptHost->Loads.Add(UAssetManager::GetStreamableManager().RequestAsyncLoad(Path,
					FStreamableDelegate::CreateLambda([WeakThis, Generation, Index, Path]()
					{
						UElysiumAudioSubsystem* Self = WeakThis.Get();
						if (!Self || Self->ScriptGeneration != Generation || !Self->ScriptHost) { return; }
						check(IsInGameThread());
						--Self->ScriptHost->PendingAssets;
						auto& Native = Self->ScriptHost->Native[Index];
						Native.Wave = Cast<USoundWave>(Path.ResolveObject());
						if (!Native.Wave)
						{
							Native.Error = TEXT("precache asset failed or not SoundWave: ") + Path.ToString();
							UE_LOG(LogElysiumAudio, Error, TEXT("SoundEmitter: %s"), *Native.Error);
							return;
						}
						Self->ScriptRetainedWaves.Add(Native.Wave);
						Self->PrepareSoundScripts();
					})));
			}
		}
	}
	for (int32 Index = 0; Index < ScriptHost->Native.Num(); ++Index)
	{
		auto& Native = ScriptHost->Native[Index];
		if (!Native.Wave || Native.bSetupStarted || !Native.Error.IsEmpty()) { continue; }
		Native.bSetupStarted = true;
		ScriptHost->Device->Precache(Native.Wave, false, false, false);
		if (Native.Wave->IsStreaming())
		{
			Native.bZerothReady = Native.Wave->LoadZerothChunk();
			if (!Native.bZerothReady)
			{
				Native.Error = TEXT("streaming zeroth chunk failed: ") + Native.Path.ToString();
				UE_LOG(LogElysiumAudio, Error, TEXT("SoundEmitter: %s"), *Native.Error);
				continue;
			}
			if (Native.Wave->GetNumChunks() > 1)
			{
				Native.bChunkPending = true;
				Native.Wave->GetHandleForChunkOfAudio([WeakThis, Generation, Index](FAudioChunkHandle&& Handle)
				{
					UElysiumAudioSubsystem* Self = WeakThis.Get();
					if (!Self || Self->ScriptGeneration != Generation || !Self->ScriptHost) { return; }
					check(IsInGameThread());
					auto& Completed = Self->ScriptHost->Native[Index];
					Completed.bChunkPending = false;
					Completed.Chunk = MoveTemp(Handle);
					if (!Completed.Chunk.IsValid())
					{
						Completed.Error = TEXT("streaming chunk1 failed: ") + Completed.Path.ToString();
						UE_LOG(LogElysiumAudio, Error, TEXT("SoundEmitter: %s"), *Completed.Error);
					}
				}, false, 1, ENamedThreads::GameThread);
			}
		}
	}
}

bool UElysiumAudioSubsystem::IsReadyForMapActivation()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("nosound")))
	{
		return PendingPrefetches == 0 && ScriptHost && ScriptHost->Source.bLoaded && ScriptHost->Error.IsEmpty();
	}
	return PendingPrefetches == 0 && IsSoundScriptNativeReady();
}

bool UElysiumAudioSubsystem::IsSoundScriptNativeReady()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("nosound"))) { return false; }
	PrepareSoundScripts();
	if (!ScriptHost || !ScriptHost->Source.bLoaded || !ScriptHost->Error.IsEmpty()
		|| !ScriptHost->Device.IsValid() || !ScriptHost->bLoadStarted || ScriptHost->PendingAssets != 0) { return false; }
	if (ScriptHost->AttemptOutcomes.Num() != ScriptHost->Eligible.Num()) { return false; }
	for (const auto& Native : ScriptHost->Native)
	{
		if (!Native.Error.IsEmpty() || !Native.Wave || !Native.bSetupStarted || Native.bChunkPending
			|| Native.Wave->GetPrecacheState() != ESoundWavePrecacheState::Done) { return false; }
		if (Native.Wave->IsStreaming() && (!Native.bZerothReady
			|| (Native.Wave->GetNumChunks() > 1 && !Native.Chunk.IsValid()))) { return false; }
	}
	return true;
}

FString UElysiumAudioSubsystem::SoundScriptReadinessError() const
{
	if (FParse::Param(FCommandLine::Get(), TEXT("nosound"))) { return TEXT("SoundEmitter native output explicitly disabled (-nosound)"); }
	if (!ScriptHost) { return TEXT("SoundEmitter source host absent"); }
	if (!ScriptHost->Error.IsEmpty()) { return ScriptHost->Error; }
	for (const auto& Native : ScriptHost->Native)
	{
		if (!Native.Error.IsEmpty()) { return Native.Error; }
	}
	return TEXT("SoundEmitter native precache not ready (world/device/assets/chunks)");
}

bool UElysiumAudioSubsystem::ResolveSoundScript(const TCHAR* Name,
	ElysiumSoundScript::FSoundParameters& Out, FString& Error)
{
	Error.Reset();
	if (!IsSoundScriptNativeReady())
	{
		Error = SoundScriptReadinessError();
		return false;
	}
	ElysiumSoundScript::FSessionRandom Random;
	ElysiumSoundScript::FGameSoundFiles Files;
	return ScriptHost->Source.Table.GetParametersForSound(Name, Out, Random, Files, nullptr);
}

FElysiumVoiceHandle UElysiumAudioSubsystem::SubmitResolvedScript(
	const ElysiumSoundScript::FSoundParameters& Params, const FElysiumAudioRequest& RequestTemplate, FString& Error)
{
	Error.Reset();
	if (!IsSoundScriptNativeReady())
	{
		Error = SoundScriptReadinessError(); return FElysiumVoiceHandle::Invalid();
	}
	FElysiumAudioRequest Request = RequestTemplate;
	Request.Source = FElysiumAudioSource::Path(Params.SoundName);
	Request.Gain *= Params.Volume;
	Request.Pitch *= static_cast<float>(Params.Pitch) / 100.f;
	Request.AttenuationOverride = MakeShared<FSoundAttenuationSettings>(ElysiumSoundLevel::MakeAttenuation(Params.SoundLevel));
	return Submit(Request); // copies immutable params/path; no borrowed descriptor crosses async load
}

const ElysiumSoundScript::FSourceTable* UElysiumAudioSubsystem::SoundScriptTable() const
{
	return ScriptHost ? &ScriptHost->Source : nullptr;
}

TArray<FString> UElysiumAudioSubsystem::ScriptReadinessEvidence() const
{
	TArray<FString> Rows;
	const bool bOutputDisabled = FParse::Param(FCommandLine::Get(), TEXT("nosound"));
	bool bNativeReady = !bOutputDisabled && ScriptHost && ScriptHost->Source.bLoaded && ScriptHost->Error.IsEmpty()
		&& ScriptHost->Source.bAvailabilityValidated && ScriptHost->Device.IsValid() && ScriptHost->bLoadStarted
		&& ScriptHost->PendingAssets == 0 && ScriptHost->AttemptOutcomes.Num() == ScriptHost->Eligible.Num();
	if (ScriptHost)
	{
		for (const auto& Native : ScriptHost->Native)
		{
			bNativeReady = bNativeReady && Native.Error.IsEmpty() && Native.Wave && Native.bSetupStarted
				&& !Native.bChunkPending && Native.Wave->GetPrecacheState() == ESoundWavePrecacheState::Done
				&& (!Native.Wave->IsStreaming() || (Native.bZerothReady
					&& (Native.Wave->GetNumChunks() <= 1 || Native.Chunk.IsValid())));
		}
	}
	Rows.Add(FString::Printf(TEXT("outputDisabled=%d nativeWaveReady=%d nativePermission=%d"),
		bOutputDisabled, bNativeReady, bNativeReady));
	if (!ScriptHost) { Rows.Add(TEXT("host absent")); return Rows; }
	Rows.Add(FString::Printf(TEXT("generation=%llu table=%d entries=%d eligible=%d pending=%d retained=%d error=%s device=%d"),
		ScriptGeneration, ScriptHost->Source.bLoaded, ScriptHost->Source.Table.Count(), ScriptHost->Eligible.Num(),
		ScriptHost->PendingAssets, ScriptRetainedWaves.Num(), *ScriptHost->Error, ScriptHost->Device.IsValid()));
	for (int32 AttemptIndex = 0; AttemptIndex < ScriptHost->Eligible.Num(); ++AttemptIndex)
	{
		Rows.Add(FString::Printf(TEXT("attempt=%d wave=%s availability=%d outcome=%s"), AttemptIndex,
			*ScriptHost->Eligible[AttemptIndex].Wave, static_cast<int32>(ScriptHost->Eligible[AttemptIndex].Availability),
			ScriptHost->AttemptOutcomes.IsValidIndex(AttemptIndex) ? *ScriptHost->AttemptOutcomes[AttemptIndex] : TEXT("pending")));
	}
	for (const auto& Native : ScriptHost->Native)
	{
		const USoundWave* Wave = Native.Wave;
		Rows.Add(FString::Printf(TEXT("asset=%s class=%s compression=%d streaming=%d chunks=%u precache=%d zeroth=%d chunk1=%d pending=%d error=%s"),
			*Native.Path.ToString(), Wave ? *Wave->GetClass()->GetName() : TEXT("pending"),
			Wave ? static_cast<int32>(Wave->GetSoundAssetCompressionType()) : -1,
			Wave ? Wave->IsStreaming() : false, Wave ? Wave->GetNumChunks() : 0,
			Wave ? static_cast<int32>(Wave->GetPrecacheState()) : -1, Native.bZerothReady,
			Native.Chunk.IsValid(), Native.bChunkPending, *Native.Error));
	}
	return Rows;
}
