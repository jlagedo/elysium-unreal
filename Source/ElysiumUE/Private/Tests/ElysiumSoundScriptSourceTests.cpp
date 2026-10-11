#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
#include "Audio/ElysiumSoundScriptSource.h"
#include "ElysiumKeyValues.h"
#include "ElysiumKeyValuesLoader.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/Base64.h"
#if WITH_EDITOR
#include "ElysiumAudioSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#endif

namespace
{
	using namespace ElysiumSoundScript;
	// Exercise the existing process cache, restoring production contents after each fixture.
	struct FScopedScriptCache
	{
		ElysiumKeyValuesLoader::FKvFileTextCache Saved;
		FScopedScriptCache() : Saved(MoveTemp(ElysiumKeyValuesLoader::FileTextCache()))
		{ ElysiumKeyValuesLoader::FileTextCache() = {}; }
		~FScopedScriptCache() { ElysiumKeyValuesLoader::FileTextCache() = MoveTemp(Saved); }
	};
	struct FScriptFixtureFiles final : ElysiumKeyValuesLoader::IKvFileSystem
	{
		TMap<FString,FString> Inputs;
		TArray<FString> Opens;
		ElysiumKeyValuesLoader::FKvFileHandle Open(const FString& Name, const ANSICHAR*, int32) override
		{
			Opens.Add(Name);
			const FString* Text = Inputs.Find(Name);
			if (!Text) { return nullptr; }
			auto* Bytes = new TArray<uint8>;
			for (const TCHAR C : *Text) { Bytes->Add(static_cast<uint8>(C)); }
			return reinterpret_cast<ElysiumKeyValuesLoader::FKvFileHandle>(Bytes);
		}
		void Close(ElysiumKeyValuesLoader::FKvFileHandle Handle) override
		{ delete reinterpret_cast<TArray<uint8>*>(Handle); }
		int32 Read(uint8* Out, int32 Count, ElysiumKeyValuesLoader::FKvFileHandle Handle) override
		{
			const auto* Bytes = reinterpret_cast<TArray<uint8>*>(Handle);
			const int32 N = FMath::Min(Count, Bytes->Num());
			FMemory::Memcpy(Out, Bytes->GetData(), N); return N;
		}
		int32 Size(ElysiumKeyValuesLoader::FKvFileHandle Handle) override
		{ return reinterpret_cast<TArray<uint8>*>(Handle)->Num(); }
	};
	struct FAllWaves final : IFileExists
	{
		int32 Calls = 0;
		bool FileExists(const TCHAR*) override { ++Calls; return true; }
	};
	static constexpr auto SoundSourceTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	FString ScriptBindingFixture(const FSourceTable& Source, const FString& Status)
	{
		const auto Root = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Bindings, Rows;
		TSet<FString> Bound;
		for (const auto& Selected : Source.SelectedSources)
		{
			FString Path = Selected.Filename.Replace(TEXT("\\"), TEXT("/")).ToLower();
			if (Bound.Contains(Path)) { continue; }
			Bound.Add(Path);
			TArray<uint8> Bytes;
			for (const TCHAR Character : Selected.Text) { Bytes.Add(static_cast<uint8>(Character)); }
			const auto Binding = MakeShared<FJsonObject>();
			Binding->SetStringField(TEXT("path"), Path);
			Binding->SetStringField(TEXT("sha256"), FString::ChrN(64, TEXT('a')));
			Binding->SetNumberField(TEXT("byteLength"), Selected.Size);
			Binding->SetStringField(TEXT("bytesBase64"), FBase64::Encode(Bytes));
			Bindings.Add(MakeShared<FJsonValueObject>(Binding));
		}
		for (int32 EntryIndex = 0; EntryIndex < Source.Entries.Num(); ++EntryIndex)
		{
			const auto& Entry = Source.Entries[EntryIndex];
			const auto* Params = Source.Table.Record(Entry.Node);
			for (int32 WaveIndex = 0; WaveIndex < Params->Waves.Num(); ++WaveIndex)
			{
				const auto& Wave = Params->Waves[WaveIndex];
				const FString Text = Source.Table.WaveString(Wave.Symbol);
				const auto Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("entry"), EntryIndex); Row->SetNumberField(TEXT("wave"), WaveIndex);
				Row->SetNumberField(TEXT("gender"), Wave.Gender); Row->SetStringField(TEXT("name"), Entry.Name);
				Row->SetStringField(TEXT("value"), Text);
				Row->SetStringField(TEXT("source"), Source.Files[Entry.File].OriginalFilename.Replace(TEXT("\\"), TEXT("/")).ToLower());
				Row->SetStringField(TEXT("sourceSha256"), FString::ChrN(64, TEXT('a')));
				Row->SetStringField(TEXT("filesystemPath"), TEXT("sound/") + Text.ToLower());
				Row->SetStringField(TEXT("availability"), Status);
				if (Status == TEXT("present"))
				{
					const auto Origin = MakeShared<FJsonObject>();
					Origin->SetStringField(TEXT("kind"), TEXT("loose")); Origin->SetStringField(TEXT("root"), TEXT("Vampire"));
					Row->SetObjectField(TEXT("origin"), Origin);
				}
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}
		}
		Root->SetArrayField(TEXT("sourceBindings"), Bindings); Root->SetArrayField(TEXT("waveAvailability"), Rows);
		const auto Coverage = MakeShared<FJsonObject>(); Coverage->SetBoolField(TEXT("complete"), true);
		Coverage->SetArrayField(TEXT("issues"), TArray<TSharedPtr<FJsonValue>>{});
		Root->SetObjectField(TEXT("availabilityCoverage"), Coverage);
		FString Metadata; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Metadata));
		return Metadata;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptOrderedSource, "Elysium.Arm.SoundScriptSource.OrderedPopulationReload", SoundSourceTestFlags)
bool FSoundScriptOrderedSource::RunTest(const FString&)
{
	FSourceTable Source;
	FAllWaves WaveFiles;
	FString Error;
	FScopedScriptCache CacheScope;
	FScriptFixtureFiles Fs;
	auto& Inputs = Fs.Inputs;
	Inputs.Add(TEXT("scripts/game_sounds_manifest.txt"), TEXT("m { PreCache_File scripts/a.txt declare_file scripts/missing.txt declare_file scripts/b.txt unknown ignored precache_file scripts/a.txt } ignored { precache_file scripts/ignored.txt }"));
	Inputs.Add(TEXT("scripts/a.txt"), TEXT("Empty {} A { channel chan_voice volume VOL_NORM volume \".6,.4\" pitch PITCH_LOW soundlevel SNDLVL_STATIC attenuation \"1,2\" wave first rndwave { any second x third } play_to_owner_only -1 precache 0 } a { wave discarded }"));
	Inputs.Add(TEXT("scripts/b.txt"), TEXT("A { wave acrossfile } B { wave b } C { wave c }"));
	Inputs.Add(TEXT("scripts/ignored.txt"), TEXT("Ignored { wave never }"));
	TestTrue(TEXT("whole source loads"), Source.Load(Fs, WaveFiles, Error));
	TestFalse(TEXT("manifest sibling not selected"), Fs.Opens.Contains(TEXT("scripts/ignored.txt")));
	TestEqual(TEXT("missing file removed, repeated file retained"), Source.Files.Num(), 3);
	TestEqual(TEXT("roots with children and first definition only"), Source.Table.Count(), 3);
	TestEqual(TEXT("duplicate waves never parsed"), Source.Table.WaveCount(), 5);
	const FParams* A = Source.Table.Record(Source.Table.Lookup(TEXT("A")));
	if (!TestNotNull(TEXT("A accepted"), A)) { return false; }
	TestEqual(TEXT("channel shared parser"), A->Channel, 2);
	TestEqual(TEXT("last volume wins"), A->Volume.Start, .6f);
	TestEqual(TEXT("descending volume preserved"), A->Volume.Range, static_cast<float>(.4 - static_cast<double>(.6f)));
	TestEqual(TEXT("pitch symbol"), A->Pitch.Start, 95.f);
	TestEqual(TEXT("attenuation base"), A->SoundLevel.Start, 70.f);
	TestEqual(TEXT("attenuation descending span"), A->SoundLevel.Range, -10.f);
	TestEqual(TEXT("attenuation retains soundlevel text"), FString(A->SoundLevelText), FString(TEXT("SNDLVL_STATIC")));
	TestEqual(TEXT("rndwave every child, append order"), Source.Table.WaveString(A->Waves[2].Symbol), FString(TEXT("third")));
	TestEqual(TEXT("owner atoi"), static_cast<int32>(A->bPlayToOwnerOnly), 1);
	TestEqual(TEXT("only declared entries and disabled A: no precache"), Source.PrecacheWaves().Num(), 0);
	TestEqual(TEXT("quiet missing pass once over accepted waves"), WaveFiles.Calls, 5);
	TestTrue(TEXT("SecondList no writer"), A->SecondList.IsEmpty());
	const uint16 Unknown = Source.Table.InternName(TEXT("Unknown"), nullptr);
	const uint16 Name = Source.Table.InternName(TEXT("a"), nullptr);
	Inputs[TEXT("scripts/a.txt")] = TEXT("New { wave fresh } A { wave replaced }");
	Fs.Opens.Reset();
	TestTrue(TEXT("reload"), Source.Load(Fs, WaveFiles, Error));
	TestFalse(TEXT("same filename uses process cached text without open"), Fs.Opens.Contains(TEXT("scripts/a.txt")));
	TestEqual(TEXT("cached old text remains source"), Source.Table.Lookup(TEXT("New")), INDEX_NONE);
	TestEqual(TEXT("known symbol retained"), Source.Table.InternName(TEXT("A"), nullptr), Name);
	TestEqual(TEXT("unknown symbol retained"), Source.Table.InternName(TEXT("unknown"), nullptr), Unknown);
	TestEqual(TEXT("wave symbols refilled from cache"), Source.Table.WaveString(0), FString(TEXT("first")));
	Inputs.Add(TEXT("scripts/missing.txt"), TEXT("New { wave fresh }") );
	TestTrue(TEXT("new filename can now open"), Source.Load(Fs, WaveFiles, Error));
	TestEqual(TEXT("new filename descriptor appears"), Source.Table.Count(), 4);
	TestEqual(TEXT("wave symbols rebuilt in accepted file order"), Source.Table.WaveString(3), FString(TEXT("fresh")));
	TestEqual(TEXT("file record restored on successful open"), Source.Files.Num(), 4);
	Source.Shutdown();
	TestEqual(TEXT("shutdown descriptors cleared"), Source.Table.Count(), 0);
	TestTrue(TEXT("shutdown clears waves"), Source.Table.WaveString(0).IsEmpty());
	TestEqual(TEXT("shutdown names retained"), Source.Table.InternName(TEXT("A"), nullptr), Name);
	Inputs.Remove(TEXT("scripts/game_sounds_manifest.txt"));
	TestTrue(TEXT("cached manifest survives disk removal"), Source.Load(Fs, WaveFiles, Error));
	// A separate cold fixture supplies the genuinely failed-open branch; production never resets cache.
	ElysiumKeyValuesLoader::FileTextCache() = {};
	TestFalse(TEXT("required manifest explicit failure"), Source.Load(Fs, WaveFiles, Error));
	TestFalse(TEXT("missing table not ready empty"), Source.bLoaded);
	TestFalse(TEXT("failure reason"), Error.IsEmpty());
	for (const FString Text : {FString(), FString(TEXT("// comment")), FString(TEXT("dropped peek later { precache_file scripts/b.txt }"))})
	{
		ElysiumKeyValuesLoader::FileTextCache() = {};
		Inputs.Add(TEXT("scripts/game_sounds_manifest.txt"), Text);
		TestTrue(TEXT("successful empty/comment/malformed open stays success"), Source.Load(Fs, WaveFiles, Error));
		TestTrue(TEXT("opened manifest marks populated"), Source.bLoaded);
		TestEqual(TEXT("first root no children ignores later siblings"), Source.Table.Count(), 0);
	}
	ElysiumKeyValuesLoader::FileTextCache() = {};
	Inputs[TEXT("scripts/game_sounds_manifest.txt")] = TEXT("m { declare_file scripts/empty.txt declare_file scripts/comment.txt declare_file scripts/malformed.txt }");
	Inputs.Add(TEXT("scripts/empty.txt"), TEXT("")); Inputs.Add(TEXT("scripts/comment.txt"), TEXT("// comment"));
	Inputs.Add(TEXT("scripts/malformed.txt"), TEXT("orphan peek Kept { wave retained"));
	TestTrue(TEXT("permissive selected file roots"), Source.Load(Fs, WaveFiles, Error));
	TestEqual(TEXT("successful files retain records including empty"), Source.Files.Num(), 3);
	TestEqual(TEXT("selected file siblings and unterminated block consumed"), Source.Table.Count(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptScalarWave, "Elysium.Arm.SoundScriptSource.ScalarGenderBounds", SoundSourceTestFlags)
bool FSoundScriptScalarWave::RunTest(const FString&)
{
	TestEqual(TEXT("strtok empty tokens"), ParseInterval(TEXT(",,1,,2,99")).Range, 1.f);
	TestEqual(TEXT("missing tokens"), ParseInterval(TEXT(",,,")).Start, 0.f);
	TestEqual(TEXT("atof trailing text"), ParseInterval(TEXT("1.5tail")).Start, 1.5f);
	TestEqual(TEXT("VC atof D exponent"), ParseInterval(TEXT("1D2")).Start, 100.f);
	TestEqual(TEXT("127-byte scalar input"), ParseInterval(FString::ChrN(127, TEXT(' ')) + TEXT("9")).Start, 0.f);
	FTable Table; FParams Params; Construct(Params, nullptr);
	AppendWave(Table, Params, TEXT("pre/$GeNdEr/after/$gender.wav"));
	TestEqual(TEXT("male first"), Table.WaveString(Params.Waves[0].Symbol), FString(TEXT("pre/male/after/$gender.wav")));
	TestEqual(TEXT("female second"), Table.WaveString(Params.Waves[1].Symbol), FString(TEXT("pre/female/after/$gender.wav")));
	TestEqual(TEXT("male tag"), Params.Waves[0].Gender, 1u);
	TestEqual(TEXT("female tag"), Params.Waves[1].Gender, 2u);
	AppendWave(Table, Params, FString::ChrN(250, TEXT('p')) + TEXT("$gender") + FString::ChrN(30, TEXT('s')));
	TestEqual(TEXT("bounded formatter 255 bytes"), Table.WaveString(Params.Waves[2].Symbol).Len(), 255);
	TestTrue(TEXT("prefix consumed suffix capacity; male plus one suffix byte"), Table.WaveString(Params.Waves[2].Symbol).EndsWith(TEXT("males")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptSymbols, "Elysium.Arm.SoundScriptSource.SymbolsFlagsAndOrdering", SoundSourceTestFlags)
bool FSoundScriptSymbols::RunTest(const FString&)
{
	FTable Table;
	FParams Params;
	TArray<FString> Warnings;
	const auto Replay = [&](const FString& Keys)
	{
		const auto Root = MakeShared<ElysiumKeyValues::FKvNode>();
		const FString Text = TEXT("A { ") + Keys + TEXT(" }");
		ElysiumKeyValues::FKvReader Reader(Text);
		TArray<TSharedPtr<ElysiumKeyValues::FKvNode>> Roots;
		ElysiumKeyValuesLoader::ParseRoots(Root, Reader, Roots);
		Construct(Params, nullptr);
		ParseKeys(Table, Params, *Root, Warnings);
	};
	// The 30 installed symbols, with aliases retained rather than inferred numeric suffixes.
	const TCHAR* Names[] = { TEXT("NONE"),TEXT("20dB"),TEXT("25dB"),TEXT("30dB"),TEXT("35dB"),TEXT("40dB"),TEXT("45dB"),TEXT("50dB"),TEXT("55dB"),TEXT("IDLE"),TEXT("TALKING"),TEXT("60dB"),TEXT("65dB"),TEXT("STATIC"),TEXT("70dB"),TEXT("NORM"),TEXT("75dB"),TEXT("80dB"),TEXT("85dB"),TEXT("90dB"),TEXT("95dB"),TEXT("100dB"),TEXT("105dB"),TEXT("110dB"),TEXT("120dB"),TEXT("130dB"),TEXT("GUNFIRE"),TEXT("140dB"),TEXT("150dB"),TEXT("180dB") };
	const int32 Values[] = {0,20,25,30,35,40,45,50,55,60,80,60,65,66,70,75,75,80,85,90,95,100,105,110,120,130,140,140,150,180};
	for (int32 I = 0; I < UE_ARRAY_COUNT(Names); ++I)
	{
		Replay(FString(TEXT("soundlevel sndlvl_")) + Names[I]);
		TestEqual(Names[I], Params.SoundLevel.Start, static_cast<float>(Values[I]));
	}
	Replay(TEXT("pitch pitch_high volume vol_norm soundlevel unknown channel 4 precache 1 play_to_owner_only 0"));
	TestEqual(TEXT("pitch high"), Params.Pitch.Start, 120.f);
	TestEqual(TEXT("numeric invalid soundlevel atof"), Params.SoundLevel.Start, 0.f);
	Replay(TEXT("soundlevel SNDLVL_UNKNOWN"));
	TestEqual(TEXT("unknown symbolic level defaults75"), Params.SoundLevel.Start, 75.f);
	TestFalse(TEXT("unknown symbolic warns"), Warnings.IsEmpty());
	// Root-approved ST0/double model: no invented intermediate float store. Runtime x87 control
	// is unrecovered; 74 is a model assertion, not a measured retail runtime result.
	Replay(TEXT("attenuation ATTN_NORM"));
	TestEqual(TEXT("ATTN_NORM under stated double/ST0 model"), Params.SoundLevel.Start, 74.f);
	Replay(TEXT("attenuation ATTN_NONE")); TestEqual(TEXT("ATTN_NONE"), Params.SoundLevel.Start, 0.f);
	Replay(TEXT("attenuation ATTN_IDLE")); TestEqual(TEXT("ATTN_IDLE"), Params.SoundLevel.Start, 60.f);
	Replay(TEXT("attenuation ATTN_STATIC")); TestEqual(TEXT("ATTN_STATIC"), Params.SoundLevel.Start, 66.f);
	Replay(TEXT("attenuation ATTN_RICOCHET")); TestEqual(TEXT("ATTN_RICOCHET"), Params.SoundLevel.Start, 63.f);
	Replay(TEXT("attenuation ATTN_GUNFIRE")); TestEqual(TEXT("ATTN_GUNFIRE"), Params.SoundLevel.Start, 124.f);
	Replay(TEXT("attenuation ATTN_UNKNOWN")); TestEqual(TEXT("unknown attenuation NORM model"), Params.SoundLevel.Start, 74.f);
	Replay(TEXT("attenuation SNDLVL_NORM")); TestEqual(TEXT("misused symbol warns but numeric zero"), Params.SoundLevel.Start, 0.f);
	Replay(TEXT("attenuation \"1,2\" soundlevel SNDLVL_TALKING precache 0 precache -2 play_to_owner_only 1 play_to_owner_only 0"));
	TestEqual(TEXT("later soundlevel overwrites attenuation"), Params.SoundLevel.Start, 80.f);
	TestEqual(TEXT("later precache truthy"), static_cast<int32>(Params.bPrecache), 1);
	TestEqual(TEXT("later owner false"), static_cast<int32>(Params.bPlayToOwnerOnly), 0);
	const FString Long = FString::ChrN(40,TEXT('x'));
	Replay(TEXT("volume ") + Long);
	TestEqual(TEXT("text is bounded31"), FString(Params.VolumeText).Len(), 31);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptSelectedTextObserver, "Elysium.Arm.SoundScriptSource.SelectedTextObserver", SoundSourceTestFlags)
bool FSoundScriptSelectedTextObserver::RunTest(const FString&)
{
	FScopedScriptCache CacheScope;
	FScriptFixtureFiles Fs; ElysiumKeyValuesLoader::FKvLoadArgs Args;
	Args.Fs = &Fs; Args.FileName = TEXT("observer.txt");
	Fs.Inputs.Add(Args.FileName, TEXT("A { wave old }"));
	TArray<TSharedPtr<ElysiumKeyValues::FKvNode>> Roots;
	TestFalse(TEXT("observer default empty"), static_cast<bool>(Args.SelectedTextObserver));
	TestTrue(TEXT("unobserved load unchanged"), ElysiumKeyValuesLoader::LoadFile(nullptr, Args, Roots));
	TArray<FSelectedSource> Observed;
	Args.SelectedTextObserver = [&Observed](const FString& Filename, const FString& Text, int32 Size)
	{ Observed.Add({Filename, Text, Size}); };
	Fs.Inputs[Args.FileName] = TEXT("B { wave new }");
	TestTrue(TEXT("cache hit observed"), ElysiumKeyValuesLoader::LoadFile(nullptr, Args, Roots));
	TestEqual(TEXT("actual chosen cached text"), Observed.Last().Text, FString(TEXT("A { wave old }")));
	TestEqual(TEXT("cached raw Size"), Observed.Last().Size, 14);
	Args.FileName = TEXT("owned.kv"); Fs.Inputs.Add(Args.FileName, TEXT("Owned {}"));
	TestTrue(TEXT("no txt temporary source observed"), ElysiumKeyValuesLoader::LoadFile(nullptr, Args, Roots));
	TestEqual(TEXT("owned text copied past LoadFile"), Observed.Last().Text, FString(TEXT("Owned {}")));
	ElysiumKeyValuesLoader::FileTextCache().Remaining = 0;
	Args.FileName = TEXT("fallback.txt"); Fs.Inputs.Add(Args.FileName, TEXT("Fallback {}"));
	TestTrue(TEXT("arena exhaustion falls back"), ElysiumKeyValuesLoader::LoadFile(nullptr, Args, Roots));
	TestEqual(TEXT("fallback text copied"), Observed.Last().Text, FString(TEXT("Fallback {}")));
	const int32 Before = Observed.Num(); Args.FileName = TEXT("missing.kv");
	TestFalse(TEXT("failed open"), ElysiumKeyValuesLoader::LoadFile(nullptr, Args, Roots));
	TestEqual(TEXT("failed open not observed"), Observed.Num(), Before);
	FString Embedded(TEXT("A {}")); Embedded.AppendChar(TEXT('\0')); Embedded += TEXT("B {}");
	Args.FileName = TEXT("nul.kv"); Fs.Inputs.Add(Args.FileName, Embedded);
	TestTrue(TEXT("embedded nul observed"), ElysiumKeyValuesLoader::LoadFile(nullptr, Args, Roots));
	TestEqual(TEXT("raw size includes bytes beyond parser nul"), Observed.Last().Size, Embedded.Len());
	TestTrue(TEXT("observer preserves full source characters"), Observed.Last().Text.GetCharArray() == Embedded.GetCharArray());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptAvailabilityBinding, "Elysium.Arm.SoundScriptSource.AvailabilityBindingAndOutcomes", SoundSourceTestFlags)
bool FSoundScriptAvailabilityBinding::RunTest(const FString&)
{
	FScopedScriptCache CacheScope; FScriptFixtureFiles Fs; FAllWaves Files; FSourceTable Source; FString Error;
	Fs.Inputs.Add(TEXT("scripts/game_sounds_manifest.txt"), TEXT("m { precache_file scripts/a.txt declare_file scripts/declared.txt precache_file scripts/later.txt }"));
	Fs.Inputs.Add(TEXT("scripts/a.txt"), TEXT("A { wave first wave second }"));
	Fs.Inputs.Add(TEXT("scripts/declared.txt"), TEXT("D { wave declared }"));
	TestTrue(TEXT("source loads"), Source.Load(Fs, Files, Error));
	const FString Metadata = ScriptBindingFixture(Source, TEXT("absent"));
	TestTrue(TEXT("source binding accepts exact selected bytes"), Source.ValidateAvailability(Metadata, Error));
	const auto Attempts = Source.PrecacheAttempts();
	TestEqual(TEXT("declared waves not precache eligibility"), Attempts.Num(), 2);
	TestEqual(TEXT("first ordered attempt"), Attempts[0].Wave, FString(TEXT("first")));
	TestEqual(TEXT("second ordered attempt"), Attempts[1].Wave, FString(TEXT("second")));
	TestTrue(TEXT("verified absence terminal only for missing bake"), CompleteAuthoredAbsence(Attempts[0].Availability, true, Error));
	TestTrue(TEXT("terminal absence is warning not fatal"), Error.IsEmpty());
	TestFalse(TEXT("present unbaked not terminal"), CompleteAuthoredAbsence(EWaveAvailability::Present, true, Error));
	TestFalse(TEXT("present unbaked explicit error"), Error.IsEmpty());
	TestFalse(TEXT("unknown never terminal even baked"), CompleteAuthoredAbsence(EWaveAvailability::Unknown, false, Error));
	TestFalse(TEXT("unknown error"), Error.IsEmpty());
	TestFalse(TEXT("absence contradicted by bake blocked"), CompleteAuthoredAbsence(EWaveAvailability::AuthoredAbsent, false, Error));
	TestFalse(TEXT("contradiction error"), Error.IsEmpty());
	TestFalse(TEXT("missing metadata rejected"), Source.ValidateAvailability(TEXT("{}"), Error));
	TestFalse(TEXT("failed binding not usable"), Source.bAvailabilityValidated);
	Fs.Inputs[TEXT("scripts/a.txt")] = TEXT("A { wave changed }");
	TestTrue(TEXT("same filename reload"), Source.Load(Fs, Files, Error));
	TestTrue(TEXT("matching cached metadata stays valid"), Source.ValidateAvailability(Metadata, Error));
	FSourceTable NewHost; TestTrue(TEXT("new host reads process cached source"), NewHost.Load(Fs, Files, Error));
	TestTrue(TEXT("new host binds same cached source"), NewHost.ValidateAvailability(Metadata, Error));
	// A coherently parsed fresh capsule cannot be paired with the process-cached old file.
	FString Unrelated;
	{
		FScopedScriptCache FreshCacheScope;
		FSourceTable FreshSource;
		TestTrue(TEXT("cold independent loader sees changed disk text"), FreshSource.Load(Fs, Files, Error));
		Unrelated = ScriptBindingFixture(FreshSource, TEXT("absent"));
		TestTrue(TEXT("new metadata independently matches its changed source"), FreshSource.ValidateAvailability(Unrelated, Error));
	}
	// The nested fixture restores the real old process cache before testing cached-reimport safety.
	TestFalse(TEXT("new metadata with cached old text rejected"), Source.ValidateAvailability(Unrelated, Error));
	TestTrue(TEXT("binding error explicit"), Error.Contains(TEXT("cache-selected")));
	Fs.Inputs.Add(TEXT("scripts/later.txt"), TEXT("Later { wave newfile }"));
	TestTrue(TEXT("uncached differing filename supplies new source"), Source.Load(Fs, Files, Error));
	TestFalse(TEXT("old member closure cannot mix new source"), Source.ValidateAvailability(Metadata, Error));
	TestTrue(TEXT("new filename metadata binds actual source"), Source.ValidateAvailability(ScriptBindingFixture(Source, TEXT("present")), Error));
	TestEqual(TEXT("new wave eligible after actual load"), Source.PrecacheAttempts().Last().Wave, FString(TEXT("newfile")));
	Source.Shutdown();
	TestTrue(TEXT("shutdown clears source snapshots"), Source.SelectedSources.IsEmpty());
	TestFalse(TEXT("shutdown invalidates availability"), Source.bAvailabilityValidated);
	return true;
}
#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSoundScriptOutputDisabled,
	"Elysium.Arm.SoundScriptSource.OutputDisabledHost", SoundSourceTestFlags)
bool FSoundScriptOutputDisabled::RunTest(const FString&)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("nosound")))
	{
		AddError(TEXT("output-disabled arm requires the normal public test boot (-nosound)")); return false;
	}
	UGameInstance* DisabledGame = NewObject<UGameInstance>(GEngine);
	DisabledGame->AddToRoot();
	DisabledGame->InitializeStandalone(FName(TEXT("SoundScriptOutputDisabled")));
	UWorld* DisabledWorld = DisabledGame->GetWorld();
	UElysiumAudioSubsystem* DisabledAudio = DisabledGame->GetSubsystem<UElysiumAudioSubsystem>();
	if (!DisabledWorld || !DisabledAudio)
	{
		AddError(TEXT("output-disabled fixture world/audio host absent"));
	}
	else
	{
		DisabledAudio->TickAudio(0.f);
		const auto* Source = DisabledAudio->SoundScriptTable();
		TestNotNull(TEXT("output-disabled host has a real selected table"), Source);
		if (Source)
		{
			TestTrue(TEXT("output-disabled selected table loaded"), Source->bLoaded);
			TestTrue(TEXT("output-disabled cached source binding valid"), Source->bAvailabilityValidated);
			TestEqual(TEXT("output-disabled actual shipped entries"), Source->Table.Count(), 48);
		}
		TestTrue(TEXT("explicit output-disabled mode permits populated map simulation"), DisabledAudio->IsReadyForMapActivation());
		TestFalse(TEXT("disabled output never reports native readiness"), DisabledAudio->IsSoundScriptNativeReady());
		ElysiumSoundScript::FSoundParameters Params;
		Params.SoundName = TEXT("untouched-output");
		FString Error;
		TestFalse(TEXT("disabled output forbids script resolve permission"), DisabledAudio->ResolveSoundScript(TEXT("concrete.Impact"), Params, Error));
		TestEqual(TEXT("disabled resolve preserves outputs"), Params.SoundName, FString(TEXT("untouched-output")));
		TestTrue(TEXT("disabled resolve has explicit mode error"), Error.Contains(TEXT("explicitly disabled (-nosound)")));
		const FElysiumAudioRequest Template;
		TestFalse(TEXT("disabled output forbids typed native submission"), DisabledAudio->SubmitResolvedScript(Params, Template, Error).IsValid());
		TestTrue(TEXT("disabled submit has explicit mode error"), Error.Contains(TEXT("explicitly disabled (-nosound)")));
		TestTrue(TEXT("disabled output creates no native voice"), DisabledAudio->ActiveVoices().IsEmpty());
		for (const auto& Row : DisabledAudio->ScriptReadinessEvidence()) { AddInfo(Row); }
	}
	DisabledGame->Shutdown();
	if (DisabledWorld) { GEngine->DestroyWorldContext(DisabledWorld); DisabledWorld->DestroyWorld(false); }
	DisabledGame->RemoveFromRoot();
	return true;
}
#endif
#endif
