#include "Debug/ElysiumCogWindow_Audio.h"

#if ENABLE_COG

#include "ElysiumAudioSubsystem.h"
#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "Substrate/ElysiumMoverSounds.h"
#include "ElysiumSoundCache.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

void FElysiumCogWindow_Audio::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Audio::RenderHelp()
{
	ImGui::Text(
		"Mute is the non-persistent global debug gate (cvar elysium.Mute) and defaults to OFF. "
		"Muting does not stop any voice, so unmuting rejoins the ambience mid-stream.\n\n"
		"Runtime audio decoder test harness (P6.1 WAV / P6.2 MP3). Type a path under out/sound/ (or "
		"click one of this map's ambient_generic references) and Play it 2D, or Info to decode without "
		"playing. dr_wav decodes VtMB's Microsoft ADPCM, IMA ADPCM and PCM WAVs; dr_mp3 decodes the loose "
		"dialogue/music/radio MP3s (.mp3) - both into a procedural sound wave (Unreal has no runtime path "
		"for loose MP3s). The table lists every decode this session with its codec/on-disk format, "
		"channels, sample rate, bit depth, frame count, duration and decode time - the same registry the "
		"elysium.playsound / elysium.sound_info verbs write.");
}

TArray<FString> FElysiumCogWindow_Audio::CollectMapAudioRefs() const
{
	TArray<FString> Refs;
	const FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		return Refs;
	}
	for (const TUniquePtr<FElysiumEntity>& EPtr : World->Entities())
	{
		const FElysiumEntity* E = EPtr.Get();
		if (E == nullptr || E->Def == nullptr)
		{
			continue;
		}
		if (!E->Def->Classname.Equals(TEXT("ambient_generic"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		const FString Msg = E->Def->Keys.FindRef(TEXT("message"));   // FString map keys fold case
		if (!Msg.IsEmpty() && (Msg.EndsWith(TEXT(".wav")) || Msg.EndsWith(TEXT(".mp3"))))
		{
			Refs.AddUnique(Msg.Replace(TEXT("\\"), TEXT("/")));
		}
	}
	Refs.Sort();
	return Refs;
}

void FElysiumCogWindow_Audio::RenderContent()
{
	Super::RenderContent();

	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	if (Audio == nullptr)
	{
		ImGui::TextDisabled("Audio subsystem unavailable.");
		return;
	}

	// --- Global mute --------------------------------------------------------------------------
	// The non-persistent debug gate over every voice the subsystem owns, previews included.
	ImGui::SeparatorText("Output");
	bool bMuted = Audio->IsMuted();
	if (ImGui::Checkbox("Mute all audio", &bMuted))
	{
		Audio->SetMuted(bMuted);
	}
	ImGui::SameLine();
	if (bMuted)
	{
		ImGui::TextColored(ElysiumCogStyle::ColWarn, "muted - voices keep running at zero gain");
	}
	else
	{
		ImGui::TextColored(ElysiumCogStyle::ColOk, "audible");
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(elysium.Mute)");

	// --- Play a clip by path ---------------------------------------------------------------
	ImGui::SeparatorText("Play a clip  (WAV/MP3 path under out/sound/)");
	ImGui::SetNextItemWidth(-FLT_MIN);   // full width: these are long relative paths
	FCogWidgets::InputTextWithHint("##Path", "Environmental/Fire/Fire_Roaring.wav", PendingPath);
	ImGui::BeginDisabled(PendingPath.IsEmpty());
	if (ImGui::Button("Play"))
	{
		Audio->PreviewSound2D(PendingPath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Info (decode only)"))
	{
		Audio->Probe(PendingPath);
	}
	ImGui::EndDisabled();

	// --- This map's ambient_generic references (one-click test material) --------------------
	const AElysiumMapActor* Map = GetMapActor();
	const FString CurrentMap = Map ? Map->LoadedMap : FString();
	if (bRefsDirty || CurrentMap != LastMap)
	{
		MapRefs = CollectMapAudioRefs();
		LastMap = CurrentMap;
		bRefsDirty = false;
	}

	ImGui::SeparatorText("ambient_generic references");
	if (MapRefs.Num() == 0)
	{
		ImGui::TextDisabled("No ambient_generic audio references on this map.");
	}
	else
	{
		FCogWidgets::SearchBar("##RefFilter", RefFilter, GetDpiScale() * 180.0f);
		if (ImGui::BeginChild("##Refs", ImVec2(0, GetDpiScale() * 130.0f), ImGuiChildFlags_Borders))
		{
			for (int32 i = 0; i < MapRefs.Num(); ++i)
			{
				const FString& Ref = MapRefs[i];
				if (!RefFilter.PassFilter(COG_TCHAR_TO_CHAR(*Ref)))
				{
					continue;
				}
				ImGui::PushID(i);
				if (ImGui::SmallButton("Play"))
				{
					PendingPath = Ref;
					Audio->PreviewSound2D(Ref);
				}
				ImGui::SameLine();
				if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Ref)))
				{
					PendingPath = Ref;
				}
				ImGui::PopID();
			}
		}
		ImGui::EndChild();
	}

	// --- Mover soundgroups (P6.4: door/button `soundgroup` → usable/<cat>/<group>/<sub>.wav) ---
	// The offline manifest resolves a token to its subkey WAVs by directory convention (no VtMB data
	// file). Browse it and Play any subkey 2D to audition what a door/button will emit.
	const TMap<FString, TMap<FString, TMap<FName, FString>>>& Manifest = ElysiumMoverSoundManifest();
	ImGui::SeparatorText("Mover soundgroups");
	if (Manifest.Num() == 0)
	{
		ImGui::TextDisabled("No soundgroups.json manifest (run UE_extract_sounds.py).");
	}
	else
	{
		int32 GroupCount = 0;
		for (const auto& CatPair : Manifest) { GroupCount += CatPair.Value.Num(); }
		ImGui::Text("%d group(s) across %d categor(y/ies)", GroupCount, Manifest.Num());
		if (ImGui::BeginChild("##Soundgroups", ImVec2(0, GetDpiScale() * 150.0f), ImGuiChildFlags_Borders))
		{
			// Stable category order (openable/switches/computers), each a collapsing tree of groups.
			TArray<FString> Cats;
			Manifest.GetKeys(Cats);
			Cats.Sort();
			int32 Id = 0;
			for (const FString& Cat : Cats)
			{
				const TMap<FString, TMap<FName, FString>>& Groups = Manifest[Cat];
				if (!ImGui::TreeNode(COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("%s  (%d)"), *Cat, Groups.Num()))))
				{
					continue;
				}
				TArray<FString> GroupNames;
				Groups.GetKeys(GroupNames);
				GroupNames.Sort();
				for (const FString& Group : GroupNames)
				{
					ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Group));
					const TMap<FName, FString>& Subs = Groups[Group];
					TArray<FName> SubKeys;
					Subs.GetKeys(SubKeys);
					SubKeys.Sort(FNameLexicalLess());
					for (const FName& Sub : SubKeys)
					{
						ImGui::SameLine();
						ImGui::PushID(Id++);
						if (ImGui::SmallButton(COG_TCHAR_TO_CHAR(*Sub.ToString())))
						{
							PendingPath = Subs[Sub];
							Audio->PreviewSound2D(PendingPath);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*Subs[Sub]));
						}
						ImGui::PopID();
					}
				}
				ImGui::TreePop();
			}
		}
		ImGui::EndChild();
	}

	// --- Live voices (runtime playback: ambient_generic + scheme bed/music/random) ----------
	const TArray<FElysiumAudioVoice>& Live = Audio->ActiveVoices();
	ImGui::SeparatorText("Live voices");
	ImGui::Text("%d playing", Live.Num());
	ImGui::SameLine();
	if (ImGui::SmallButton("Stop all"))
	{
		Audio->StopAllVoices();
	}
	if (Live.Num() > 0)
	{
		const ImGuiTableFlags VoiceFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
			ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##Voices", 6, VoiceFlags, ImVec2(0, GetDpiScale() * 120.0f)))
		{
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableSetupColumn("Path");
			ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 74.0f);
			ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 40.0f);
			ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
			ImGui::TableSetupColumn("Playing", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
			ImGui::TableSetupColumn("##stop", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
			ImGui::TableHeadersRow();
			for (const FElysiumAudioVoice& V : Live)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*V.Event.ResolvedPath));
				ImGui::TableNextColumn(); ImGui::TextUnformatted(
					V.Request.bLooping
						? (V.Request.Placement.bSpatialized ? "loop 3D" : "loop 2D")
						: (V.Request.Placement.bSpatialized ? "1shot 3D" : "1shot 2D"));
				ImGui::TableNextColumn(); ImGui::Text("%.2f", V.Request.Gain);
				ImGui::TableNextColumn(); ImGui::Text("%.2f", V.Request.Pitch);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Audio->IsVoicePlaying(V.Handle) ? "yes" : "-");
				ImGui::TableNextColumn();
				ImGui::PushID(static_cast<int>(V.Handle.Slot));
				if (ImGui::SmallButton("Stop"))
				{
					Audio->StopVoice(V.Handle);
				}
				ImGui::PopID();
			}
			ImGui::EndTable();
		}
	}

	// --- Decode summary + results table -----------------------------------------------------
	const TMap<FString, FElysiumSoundInfo>& Results = Audio->Results();

	int32 NumMsAdpcm = 0, NumImaAdpcm = 0, NumPcm = 0, NumMp3 = 0, NumOther = 0, NumFailed = 0;
	double TotalDecodeMs = 0.0;
	for (const TPair<FString, FElysiumSoundInfo>& Pair : Results)
	{
		const FElysiumSoundInfo& Info = Pair.Value;
		TotalDecodeMs += Info.DecodeMilliseconds;
		if (!Info.Error.IsEmpty()) { ++NumFailed; continue; }
		if (Info.Codec == EElysiumAudioCodec::Mp3) { ++NumMp3; continue; }
		switch (Info.FormatTag)
		{
		case 0x0002: ++NumMsAdpcm; break;   // DR_WAVE_FORMAT_ADPCM
		case 0x0011: ++NumImaAdpcm; break;  // DR_WAVE_FORMAT_DVI_ADPCM
		case 0x0001: ++NumPcm; break;       // DR_WAVE_FORMAT_PCM
		default:     ++NumOther; break;
		}
	}

	// Two lines: the totals, then the codec mix. One line ran to ~110 characters and was clipped at
	// every window width this thing is actually used at.
	ImGui::SeparatorText("Decoded this session");
	ImGui::Text("%d decoded  ·  %.1f ms total", Results.Num(), TotalDecodeMs);
	if (NumFailed > 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColError, "·  %d failed", NumFailed);
	}
	ImGui::TextDisabled("MS-ADPCM %d · IMA %d · PCM %d · MP3 %d · other %d",
		NumMsAdpcm, NumImaAdpcm, NumPcm, NumMp3, NumOther);

	// Its own filter: this table is populated by every decode this session, the reference list above
	// only by this map's ambient_generic keys. One shared filter left the table silently narrowed by
	// a search box that is not even drawn on a map with no ambient_generic references.
	FCogWidgets::SearchBar("##DecodeFilter", DecodeFilter, GetDpiScale() * 180.0f);

	TArray<FString> Keys;
	Results.GetKeys(Keys);
	Keys.Sort();

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##Decodes", 8, TableFlags))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Path");
		ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 78.0f);
		ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 30.0f);
		ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
		ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 38.0f);
		ImGui::TableSetupColumn("Frames", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 66.0f);
		ImGui::TableSetupColumn("Dur", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
		ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 48.0f);
		ImGui::TableHeadersRow();

		for (const FString& Key : Keys)
		{
			if (!DecodeFilter.PassFilter(COG_TCHAR_TO_CHAR(*Key)))
			{
				continue;
			}
			const FElysiumSoundInfo& Info = Results[Key];
			const bool bError = !Info.Error.IsEmpty();

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Key), false, ImGuiSelectableFlags_SpanAllColumns))
			{
				PendingPath = Key;
			}

			ImGui::TableNextColumn();
			if (bError)
			{
				ImGui::TextColored(ElysiumCogStyle::ColError, "error");
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*Info.Error));
				}
			}
			else
			{
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Info.FormatName()));
			}

			ImGui::TableNextColumn(); ImGui::Text("%d", Info.Channels);
			ImGui::TableNextColumn(); ImGui::Text("%d", Info.SampleRate);
			ImGui::TableNextColumn(); ImGui::Text("%d", Info.BitsPerSample);
			ImGui::TableNextColumn(); ImGui::Text("%lld", Info.FrameCount);
			ImGui::TableNextColumn(); ImGui::Text("%.2fs", Info.DurationSeconds);
			ImGui::TableNextColumn(); ImGui::Text("%.2f", Info.DecodeMilliseconds);
		}
		ImGui::EndTable();
	}
}

#endif // ENABLE_COG
