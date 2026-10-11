#include "Audio/ElysiumSoundScriptSource.h"
#include "ElysiumKeyValues.h"
#include "ElysiumKeyValuesLoader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"

namespace ElysiumSoundScript
{
	// The selected source is ASCII. Bounds are byte bounds; UTF-8 avoids TCHAR-count truncation.
	static FString Bounded(const FString& Text, int32 Bytes)
	{
		FTCHARToUTF8 Encoded(*Text);
		const int32 Count = FMath::Min(Bytes, Encoded.Length());
		FUTF8ToTCHAR Decoded(Encoded.Get(), Count);
		return FString(Decoded.Length(), Decoded.Get());
	}
	FInterval ParseInterval(const FString& Text)
	{
		// strtok skips empty comma-delimited tokens; atof tolerates trailing text. Ignore token3+.
		TArray<FString> Tokens;
		Bounded(Text, 127).ParseIntoArray(Tokens, TEXT(","), true);
		FInterval Result;
		int32 End = 0;
		if (Tokens.Num() > 0) { Result.Start = static_cast<float>(ElysiumKeyValues::RetailStrtod(Tokens[0], End)); }
		if (Tokens.Num() > 1)
		{
			Result.Range = static_cast<float>(ElysiumKeyValues::RetailStrtod(Tokens[1], End) - static_cast<double>(Result.Start));
		}
		return Result;
	}
	static int32 LevelForAttenuation(double Value)
	{
		return Value == 0.0 ? 0 : static_cast<int32>(FMath::TruncToDouble(20.0 / Value + 50.0));
	}
	static float AttenuationSymbol(const FString& Text, TArray<FString>& Warnings)
	{
		static const TCHAR* Names[] = { TEXT("ATTN_NONE"), TEXT("ATTN_NORM"), TEXT("ATTN_IDLE"),
			TEXT("ATTN_STATIC"), TEXT("ATTN_RICOCHET"), TEXT("ATTN_GUNFIRE") };
		static const float Values[] = { 0.f, .8f, 2.f, 1.25f, 1.5f, .27f };
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			if (Text.Equals(Names[I], ESearchCase::IgnoreCase)) { return Values[I]; }
		}
		Warnings.Add(TEXT("unknown attenuation: ") + Text);
		return .8f;
	}
	static int32 LevelSymbol(const FString& Text, TArray<FString>& Warnings)
	{
		// Installed 30-row table 10598cd0..10598dc0, including aliases and STATIC=66.
		static const TCHAR* Names[] = { TEXT("NONE"), TEXT("20dB"), TEXT("25dB"), TEXT("30dB"),
			TEXT("35dB"), TEXT("40dB"), TEXT("45dB"), TEXT("50dB"), TEXT("55dB"), TEXT("IDLE"),
			TEXT("TALKING"), TEXT("60dB"), TEXT("65dB"), TEXT("STATIC"), TEXT("70dB"), TEXT("NORM"),
			TEXT("75dB"), TEXT("80dB"), TEXT("85dB"), TEXT("90dB"), TEXT("95dB"), TEXT("100dB"),
			TEXT("105dB"), TEXT("110dB"), TEXT("120dB"), TEXT("130dB"), TEXT("GUNFIRE"),
			TEXT("140dB"), TEXT("150dB"), TEXT("180dB") };
		static const int32 Values[] = { 0,20,25,30,35,40,45,50,55,60,80,60,65,66,70,75,75,80,85,90,95,100,105,110,120,130,140,140,150,180 };
		for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
		{
			if (Text.Equals(FString(TEXT("SNDLVL_")) + Names[I], ESearchCase::IgnoreCase)) { return Values[I]; }
		}
		Warnings.Add(TEXT("unknown soundlevel: ") + Text);
		return 75;
	}
	void AppendWave(FTable& Table, FParams& Params, const FString& Wave)
	{
		const int32 GenderAt = Wave.Find(TEXT("$gender"), ESearchCase::IgnoreCase);
		if (GenderAt == INDEX_NONE)
		{
			Params.Waves.Add({ Table.InternWave(*Wave), 0u });
			return;
		}
		// 3990 uses ONE copied-byte counter across the two buffers; suffix gets remaining capacity.
		const FString Prefix = Bounded(Wave.Left(GenderAt), 255);
		const int32 PrefixBytes = FTCHARToUTF8(*Prefix).Length();
		const FString Suffix = Bounded(Wave.Mid(GenderAt + 7), FMath::Max(0, 255 - PrefixBytes));
		Params.Waves.Add({ Table.InternWave(*Bounded(Prefix + TEXT("male") + Suffix, 255)), 1u });
		Params.Waves.Add({ Table.InternWave(*Bounded(Prefix + TEXT("female") + Suffix, 255)), 2u });
	}
	void ParseKeys(FTable& Table, FParams& Params, const ElysiumKeyValues::FKvNode& Root,
		TArray<FString>& Warnings)
	{
		for (const auto& Child : Root.Children)
		{
			const FString Key = Child->Name.ToLower();
			const FString Text = ElysiumKeyValues::GetString(*Child, nullptr, TEXT(""));
			if (Key == TEXT("channel")) { SetChannel(Params, *Text, nullptr); }
			else if (Key == TEXT("volume"))
			{
				Params.Volume = Text.Equals(TEXT("VOL_NORM"), ESearchCase::IgnoreCase) ? FInterval{1.f,0.f} : ParseInterval(Text);
				CopyName(Params.VolumeText, *Bounded(Text, 31));
			}
			else if (Key == TEXT("pitch"))
			{
				if (Text.Equals(TEXT("PITCH_NORM"), ESearchCase::IgnoreCase)) { Params.Pitch = {100.f,0.f}; }
				else if (Text.Equals(TEXT("PITCH_LOW"), ESearchCase::IgnoreCase)) { Params.Pitch = {95.f,0.f}; }
				else if (Text.Equals(TEXT("PITCH_HIGH"), ESearchCase::IgnoreCase)) { Params.Pitch = {120.f,0.f}; }
				else { Params.Pitch = ParseInterval(Text); }
				CopyName(Params.PitchText, *Bounded(Text, 31));
			}
			else if (Key == TEXT("soundlevel"))
			{
				Params.SoundLevel = Text.StartsWith(TEXT("SNDLVL_"), ESearchCase::IgnoreCase)
					? FInterval{static_cast<float>(LevelSymbol(Text, Warnings)),0.f} : ParseInterval(Text);
				CopyName(Params.SoundLevelText, *Bounded(Text, 31));
			}
			else if (Key == TEXT("attenuation"))
			{
				if (Text.StartsWith(TEXT("ATTN_"), ESearchCase::IgnoreCase))
				{
					Params.SoundLevel = {static_cast<float>(LevelForAttenuation(AttenuationSymbol(Text, Warnings))),0.f};
				}
				else
				{
					if (Text.StartsWith(TEXT("SNDLVL_"), ESearchCase::IgnoreCase)) { Warnings.Add(TEXT("soundlevel used as attenuation: ") + Text); }
					const FInterval Interval = ParseInterval(Text);
					const int32 Base = LevelForAttenuation(Interval.Start);
					const int32 Upper = LevelForAttenuation(static_cast<double>(Interval.Start) + Interval.Range);
					Params.SoundLevel = {static_cast<float>(Base),static_cast<float>(Upper - Base)};
				}
			}
			else if (Key == TEXT("wave")) { AppendWave(Table, Params, Text); }
			else if (Key == TEXT("rndwave"))
			{
				for (const auto& Wave : Child->Children)
				{
					AppendWave(Table, Params, ElysiumKeyValues::GetString(*Wave, nullptr, TEXT("")));
				}
			}
			else if (Key == TEXT("play_to_owner_only")) { Params.bPlayToOwnerOnly = ElysiumKeyValues::GetInt(*Child, nullptr, 0) != 0; }
			else if (Key == TEXT("precache")) { Params.bPrecache = ElysiumKeyValues::GetInt(*Child, nullptr, 0) != 0; }
		}
	}
	void FSourceTable::Shutdown()
	{
		Table.ClearSource(); Files.Reset(); Entries.Reset(); Warnings.Reset(); MissingWaves = 0; bLoaded = false;
		SelectedSources.Reset(); Availability.Reset(); bAvailabilityValidated = false;
	}
	bool FSourceTable::Load(ElysiumKeyValuesLoader::IKvFileSystem& Fs, IFileExists& WaveFiles, FString& Error)
	{
		Shutdown(); Error.Reset();
		// 102480f0/102482f0: successful open is success even for empty/malformed text.
		// The process cache owns plain source strings and survives SoundEmitter shutdown/reload.
		ElysiumKeyValuesLoader::FKvLoadArgs Args;
		Args.FileName = TEXT("scripts/game_sounds_manifest.txt");
		Args.Fs = &Fs; Args.PathID = 0; Args.Cache = 1;
		Args.SelectedTextObserver = [this](const FString& Filename, const FString& Text, int32 Size)
		{
			SelectedSources.Add({Filename, Text, Size}); // copies before temporary Owned is released
		};
		const auto Manifest = MakeShared<ElysiumKeyValues::FKvNode>();
		TArray<TSharedPtr<ElysiumKeyValues::FKvNode>> Roots;
		if (!ElysiumKeyValuesLoader::LoadFile(Manifest, Args, Roots))
		{
			Error = TEXT("required scripts/game_sounds_manifest.txt missing; run uv run elysium import sound-script-data");
			return false;
		}
		// BaseInit enumerates only the first root's children, not its sibling roots.
		for (const auto& Declaration : Manifest->Children)
		{
			const bool bPrecache = Declaration->Name.Equals(TEXT("precache_file"), ESearchCase::IgnoreCase);
			if (!bPrecache && !Declaration->Name.Equals(TEXT("declare_file"), ESearchCase::IgnoreCase))
			{
				Warnings.Add(TEXT("unknown manifest key: ") + Declaration->Name); continue;
			}
			const FString Filename = ElysiumKeyValues::GetString(*Declaration, nullptr, TEXT(""));
			const int32 FileIndex = Files.Add({Bounded(Filename, 127), bPrecache, false, Filename});
			Args.FileName = Filename;
			const auto Source = MakeShared<ElysiumKeyValues::FKvNode>();
			TArray<TSharedPtr<ElysiumKeyValues::FKvNode>> SourceRoots;
			if (!ElysiumKeyValuesLoader::LoadFile(Source, Args, SourceRoots))
			{
				Files.RemoveAt(FileIndex); Warnings.Add(TEXT("missing sound script: ") + Filename); continue;
			}
			for (const auto& Sound : SourceRoots)
			{
				if (Sound->Children.IsEmpty()) { continue; }
				Table.InternName(*Sound->Name, nullptr);
				if (Table.Lookup(*Sound->Name) != INDEX_NONE)
				{
					Warnings.Add(TEXT("duplicate sound: ") + Sound->Name); continue;
				}
				FParams Params; Construct(Params, nullptr);
				ParseKeys(Table, Params, *Sound, Warnings);
				Entries.Add({Table.AddEntry(*Sound->Name, MoveTemp(Params)), FileIndex, Sound->Name});
			}
		}
		MissingWaves = Table.MarkMissingWaves(WaveFiles, EMissingReport::Quiet, nullptr);
		bLoaded = true;
		return true;
	}
	TArray<FString> FSourceTable::PrecacheWaves() const
	{
		TArray<FString> Result;
		for (const FPrecacheAttempt& Attempt : PrecacheAttempts()) { Result.Add(Attempt.Wave); }
		return Result;
	}
	TArray<FPrecacheAttempt> FSourceTable::PrecacheAttempts() const
	{
		TArray<FPrecacheAttempt> Result;
		int32 Row = 0;
		for (const FSourceEntry& Entry : Entries)
		{
			const FParams* Params = Table.Record(Entry.Node);
			if (!Params) { continue; }
			for (const FWave& Wave : Params->Waves)
			{
				if (Files[Entry.File].bPrecache && Params->bPrecache)
				{
					Result.Add({Table.WaveString(Wave.Symbol), Availability.IsValidIndex(Row) ? Availability[Row] : EWaveAvailability::Unknown});
				}
				++Row;
			}
		}
		return Result;
	}
	bool CompleteAuthoredAbsence(EWaveAvailability Availability, bool bBakedMissing, FString& Error)
	{
		Error.Reset();
		if (Availability == EWaveAvailability::AuthoredAbsent && bBakedMissing) { return true; }
		if (Availability == EWaveAvailability::Unknown) { Error = TEXT("precache authored availability unknown"); }
		else if (Availability == EWaveAvailability::Nonfile) { Error = TEXT("precache nonfile wave has no native file adapter"); }
		else if (bBakedMissing) { Error = TEXT("precache authored wave present but baked wave missing"); }
		else if (Availability == EWaveAvailability::AuthoredAbsent) { Error = TEXT("precache authored absence contradicts baked wave"); }
		return false;
	}
	static FString MemberKey(FString Filename)
	{
		Filename.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Filename.ToLower();
	}
	bool FSourceTable::ValidateAvailability(const FString& Metadata, FString& Error)
	{
		Availability.Reset(); bAvailabilityValidated = false; Error.Reset();
		const auto Fail = [&Error](const TCHAR* Reason) { Error = Reason; return false; };
		if (!bLoaded) { return Fail(TEXT("SoundEmitter source table not loaded")); }
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Metadata), Root) || !Root.IsValid())
		{ return Fail(TEXT("SoundEmitter availability metadata invalid or missing")); }
		const TArray<TSharedPtr<FJsonValue>>* Bindings = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		const TSharedPtr<FJsonObject>* Coverage = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Issues = nullptr;
		bool bComplete = false;
		if (!Root->TryGetArrayField(TEXT("sourceBindings"), Bindings) || !Root->TryGetArrayField(TEXT("waveAvailability"), Rows)
			|| !Root->TryGetObjectField(TEXT("availabilityCoverage"), Coverage) || !Coverage
			|| !(*Coverage)->TryGetBoolField(TEXT("complete"), bComplete) || !(*Coverage)->TryGetArrayField(TEXT("issues"), Issues)
			|| bComplete != Issues->IsEmpty())
		{ return Fail(TEXT("SoundEmitter availability metadata closure missing")); }
		TMap<FString,FString> SourceHashes;
		for (const auto& Value : *Bindings)
		{
			const TSharedPtr<FJsonObject>* Binding = nullptr;
			FString Path, Encoded, Hash; int32 Size = -1;
			if (!Value->TryGetObject(Binding) || !Binding || !(*Binding)->TryGetStringField(TEXT("path"), Path)
				|| !(*Binding)->TryGetStringField(TEXT("bytesBase64"), Encoded) || !(*Binding)->TryGetStringField(TEXT("sha256"), Hash)
				|| !(*Binding)->TryGetNumberField(TEXT("byteLength"), Size) || SourceHashes.Contains(Path))
			{ return Fail(TEXT("SoundEmitter source binding invalid")); }
			TArray<uint8> Bytes;
			if (!FBase64::Decode(Encoded, Bytes) || Size != Bytes.Num()) { return Fail(TEXT("SoundEmitter source binding size invalid")); }
			const FString BoundText = ElysiumKeyValuesLoader::WidenBytes(Bytes);
			bool bObserved = false;
			for (const FSelectedSource& Selected : SelectedSources)
			{
				if (MemberKey(Selected.Filename) != Path) { continue; }
				bObserved = true;
				if (Size != Selected.Size || BoundText.GetCharArray() != Selected.Text.GetCharArray())
				{ return Fail(TEXT("SoundEmitter metadata does not bind actual cache-selected source")); }
			}
			if (!bObserved) { return Fail(TEXT("SoundEmitter binding not in selected source closure")); }
			SourceHashes.Add(Path, Hash);
		}
		for (const FSelectedSource& Selected : SelectedSources)
		{
			if (!SourceHashes.Contains(MemberKey(Selected.Filename))) { return Fail(TEXT("SoundEmitter selected source binding absent")); }
		}
		int32 Ordinal = 0;
		for (int32 EntryIndex = 0; EntryIndex < Entries.Num(); ++EntryIndex)
		{
			const FSourceEntry& Entry = Entries[EntryIndex];
			const FParams* Params = Table.Record(Entry.Node);
			const FString SourcePath = MemberKey(Files[Entry.File].OriginalFilename);
			for (int32 WaveIndex = 0; WaveIndex < Params->Waves.Num(); ++WaveIndex, ++Ordinal)
			{
				const FWave& Wave = Params->Waves[WaveIndex];
				const FString& Text = Table.WaveString(Wave.Symbol);
				const TSharedPtr<FJsonObject>* Row = nullptr;
				FString Name, Path, Hash, WaveText, Status; int32 EntryNumber = -1, WaveNumber = -1, Gender = -1;
				if (!Rows->IsValidIndex(Ordinal) || !(*Rows)[Ordinal]->TryGetObject(Row) || !Row
					|| !(*Row)->TryGetNumberField(TEXT("entry"), EntryNumber) || EntryNumber != EntryIndex
					|| !(*Row)->TryGetNumberField(TEXT("wave"), WaveNumber) || WaveNumber != WaveIndex
					|| !(*Row)->TryGetNumberField(TEXT("gender"), Gender) || Gender != static_cast<int32>(Wave.Gender)
					|| !(*Row)->TryGetStringField(TEXT("name"), Name) || !Name.Equals(Entry.Name, ESearchCase::CaseSensitive)
					|| !(*Row)->TryGetStringField(TEXT("source"), Path) || Path != SourcePath
					|| !(*Row)->TryGetStringField(TEXT("sourceSha256"), Hash) || Hash != SourceHashes[Path]
					|| !(*Row)->TryGetStringField(TEXT("value"), WaveText) || !WaveText.Equals(Text, ESearchCase::CaseSensitive)
					|| !(*Row)->TryGetStringField(TEXT("availability"), Status))
				{ return Fail(TEXT("SoundEmitter wave metadata differs from selected descriptors")); }
				FString FileText = Text;
				for (int32 Strip = 0; Strip < 2 && !FileText.IsEmpty() && FString(TEXT("*?!#@><^)")).Contains(FileText.Left(1)); ++Strip)
				{ FileText.RightChopInline(1); }
				const bool bNonfile = Text.IsEmpty() || Text[0] == TEXT('!');
				FString ExpectedPath;
				if (!bNonfile) { ExpectedPath = TEXT("sound/") + MemberKey(FileText); }
				FString CapturedPath;
				if ((!bNonfile && (!(*Row)->TryGetStringField(TEXT("filesystemPath"), CapturedPath) || CapturedPath != ExpectedPath))
					|| (bNonfile && !(*Row)->HasTypedField<EJson::Null>(TEXT("filesystemPath"))))
				{ return Fail(TEXT("SoundEmitter wave filesystem path differs from selected descriptor")); }
				EWaveAvailability State = EWaveAvailability::Unknown;
				if (Status == TEXT("present")) { State = EWaveAvailability::Present; }
				else if (Status == TEXT("absent") && bComplete) { State = EWaveAvailability::AuthoredAbsent; }
				else if (Status == TEXT("nonfile") && bNonfile) { State = EWaveAvailability::Nonfile; }
				else if (Status != TEXT("unknown")) { return Fail(TEXT("SoundEmitter availability fact invalid")); }
				if (bNonfile != (State == EWaveAvailability::Nonfile)) { return Fail(TEXT("SoundEmitter nonfile availability inconsistent")); }
				if (State == EWaveAvailability::Present)
				{
					const TSharedPtr<FJsonObject>* Origin = nullptr; FString Kind;
					if (!(*Row)->TryGetObjectField(TEXT("origin"), Origin) || !Origin || !(*Origin)->TryGetStringField(TEXT("kind"), Kind)
						|| (Kind != TEXT("loose") && Kind != TEXT("vpk"))) { return Fail(TEXT("SoundEmitter present origin invalid")); }
				}
				else if ((*Row)->HasField(TEXT("origin"))) { return Fail(TEXT("SoundEmitter non-present wave carries origin")); }
				Availability.Add(State);
			}
		}
		if (Ordinal != Rows->Num()) { return Fail(TEXT("SoundEmitter wave metadata contains unselected rows")); }
		bAvailabilityValidated = true;
		return true;
	}
}
