#include "Debug/ElysiumCogWindow_Audio.h"

#if ENABLE_COG

#include "ElysiumAudioSubsystem.h"
#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumSoundAssets.h"
#include "Substrate/ElysiumMoverSounds.h"

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
		"Audio request harness over the baked sound family (AUD1.2). Type a logical path under "
		"sound/ (or click one of this map's ambient_generic references) and Play it 2D, or Inspect "
		"to resolve and load it without playing. Every sound unit is a USoundWave under "
		"/ElysiumBaked/Sounds/**/SW_<name>: the resolver folds the key, probes the mp3 asset before "
		"the wav, and the engine's stream cache is the decoder. The table lists every key this "
		"session resolved with the asset it named and whether that asset is loaded, retained by a "
		"prefetch, still loading or missing from the bake - the same registry the "
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

	// The non-persistent debug gate over every voice the subsystem owns, previews included.
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
	if (!ImGui::BeginTabBar("##AudioViews"))
	{
		return;
	}

	if (ImGui::BeginTabItem("Preview"))
	{
	ImGui::TextDisabled("Logical path under sound/ (the baked asset is resolved from it)");
	ImGui::SetNextItemWidth(-FLT_MIN);   // full width: these are long relative paths
	FCogWidgets::InputTextWithHint("##Path", "Environmental/Fire/Fire_Roaring.wav", PendingPath);
	ImGui::BeginDisabled(PendingPath.IsEmpty());
	if (ImGui::Button("Play"))
	{
		Audio->PreviewSound2D(PendingPath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Inspect (resolve + load)"))
	{
		Audio->Probe(PendingPath);
	}
	ImGui::EndDisabled();

	// This map's ambient_generic references (one-click test material).
	const AElysiumMapActor* Map = GetMapActor();
	const FString CurrentMap = Map ? Map->LoadedMap : FString();
	if (bRefsDirty || CurrentMap != LastMap)
	{
		MapRefs = CollectMapAudioRefs();
		LastMap = CurrentMap;
		bRefsDirty = false;
	}

	ImGui::SeparatorText("Sounds referenced by this map");
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

	// Mover soundgroups: door/button/computer `soundgroup` -> usable/<cat>/<group>/<sub>.wav.
	// There is no manifest: retail walks the shipped directories (ElysiumMoverSounds.h), and so does
	// the resolver. Browse it and Play any subkey 2D to audition what a door/button will emit.
	ImGui::SeparatorText("Door, switch and computer sound groups");
	{
		if (ImGui::BeginChild("##Soundgroups", ImVec2(0, GetDpiScale() * 150.0f), ImGuiChildFlags_Borders))
		{
			int32 Id = 0;
			for (const FString& Cat : ElysiumSoundGroups::Categories())
			{
				const TArray<FString> GroupNames = ElysiumSoundGroups::EnumerateGroups(Cat);
				if (!ImGui::TreeNode(COG_TCHAR_TO_CHAR(
					*FString::Printf(TEXT("%s  (%d)"), *Cat, GroupNames.Num()))))
				{
					continue;
				}
				for (const FString& Group : GroupNames)
				{
					ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Group));
					const TMap<FName, FString>& Subs = ElysiumSoundGroups::Resolve(Cat, Group);
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
		ImGui::EndTabItem();
	}

	const TArray<FElysiumAudioVoice>& Live = Audio->ActiveVoices();
	const FString LiveLabel = FString::Printf(TEXT("Live voices  %d###LiveVoices"), Live.Num());
	if (ImGui::BeginTabItem(COG_TCHAR_TO_CHAR(*LiveLabel)))
	{
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
		else
		{
			ImGui::TextDisabled("No voices are playing.");
		}
		ImGui::EndTabItem();
	}

	// The asset ledger: every key this session resolved and what the engine did with it. There is
	// no decode log any more — Unreal's stream cache is the decoder (AUD1.2) — so what is worth
	// showing is what the resolver made of a key and whether the wave is loaded, retained or
	// missing.
	const TMap<FString, FElysiumSoundAssetRow>& Rows = Audio->AssetRows();

	int32 NumLoaded = 0, NumRetained = 0, NumMissing = 0;
	for (const TPair<FString, FElysiumSoundAssetRow>& Pair : Rows)
	{
		if (Pair.Value.bMissing)  { ++NumMissing; }
		if (Pair.Value.bLoaded)   { ++NumLoaded; }
		if (Pair.Value.bRetained) { ++NumRetained; }
	}

	const FString AssetLabel = FString::Printf(TEXT("Assets  %d###AssetLog"), Rows.Num());
	if (ImGui::BeginTabItem(COG_TCHAR_TO_CHAR(*AssetLabel)))
	{
	ImGui::Text("%d resolved  ·  %d loaded  ·  %d retained  ·  %d in flight",
		Rows.Num(), NumLoaded, NumRetained, Audio->PendingLoadCount());
	if (NumMissing > 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColError, "·  %d missing", NumMissing);
	}
	ImGui::TextDisabled("%d baked sound assets indexed under /ElysiumBaked/Sounds",
		ElysiumSoundAssets::Count());

	// Its own filter: this table is populated by every key this session resolved, the reference
	// list above only by this map's ambient_generic keys. One shared filter left the table silently
	// narrowed by a search box that is not even drawn on a map with no ambient_generic references.
	FCogWidgets::SearchBar("##AssetFilter", DecodeFilter, GetDpiScale() * 180.0f);

	TArray<FString> Keys;
	Rows.GetKeys(Keys);
	Keys.Sort();

	const ImGuiTableFlags TableFlags =
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
		ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

	if (ImGui::BeginTable("##Assets", 7, TableFlags))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Key");
		ImGui::TableSetupColumn("Asset");
		ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 78.0f);
		ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 30.0f);
		ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
		ImGui::TableSetupColumn("Dur", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
		ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		ImGui::TableHeadersRow();

		for (const FString& Key : Keys)
		{
			if (!DecodeFilter.PassFilter(COG_TCHAR_TO_CHAR(*Key)))
			{
				continue;
			}
			const FElysiumSoundAssetRow& Row = Rows[Key];

			ImGui::TableNextRow();

			ImGui::TableNextColumn();
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Key), false, ImGuiSelectableFlags_SpanAllColumns))
			{
				PendingPath = Key;
			}

			ImGui::TableNextColumn();
			ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Row.ObjectPath));

			ImGui::TableNextColumn();
			if (Row.bMissing)
			{
				ImGui::TextColored(ElysiumCogStyle::ColError, "missing");
				if (ImGui::IsItemHovered())
				{
					ImGui::SetTooltip("the bake carries no asset for this key");
				}
			}
			else if (Row.bPending)
			{
				ImGui::TextColored(ElysiumCogStyle::ColWarn, "loading");
			}
			else if (Row.bRetained)
			{
				ImGui::TextColored(ElysiumCogStyle::ColOk, "retained");
			}
			else if (Row.bLoaded)
			{
				ImGui::TextColored(ElysiumCogStyle::ColOk, "loaded");
			}
			else
			{
				ImGui::TextDisabled("resolved");
			}

			ImGui::TableNextColumn(); ImGui::Text("%d", Row.Channels);
			ImGui::TableNextColumn(); ImGui::Text("%d", Row.SampleRate);
			ImGui::TableNextColumn(); ImGui::Text("%.2fs", Row.DurationSeconds);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(Row.LoopObjectPath.IsEmpty() ? "-" : "intro+body");
		}
		ImGui::EndTable();
	}
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

#endif // ENABLE_COG
