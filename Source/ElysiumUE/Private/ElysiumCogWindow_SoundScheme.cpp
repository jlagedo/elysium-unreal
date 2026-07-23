#include "ElysiumCogWindow_SoundScheme.h"

#if ENABLE_COG

#include "ElysiumAudioSubsystem.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumSoundScheme.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "HAL/IConsoleManager.h"
#include "imgui.h"

void FElysiumCogWindow_SoundScheme::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_SoundScheme::RenderHelp()
{
	ImGui::Text(
		"P6.3 SoundScheme runtime. Shows the active scheme (ambient bed + music stems + polar random "
		"one-shots) driven by this map's ambient_soundscheme anchors, and drives the music state "
		"machine (Explore/Combat/Alert). VtMB crossfades schemes with Source I/O (FadeIn/FadeOut); the "
		"buttons below fire the same path. Combat scoring will own the music state in P9 — for now it "
		"is the elysium.MusicState debug echo set here.");
}

void FElysiumCogWindow_SoundScheme::SetMusicStateCvar(int32 State)
{
	// The manager samples elysium.MusicState each tick and applies changes (single source of truth),
	// so the window sets the cvar rather than poking the manager directly (which the tick would revert).
	if (IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.MusicState")))
	{
		CVar->Set(State);
	}
}

void FElysiumCogWindow_SoundScheme::RenderContent()
{
	Super::RenderContent();

	AElysiumMapActor* Map = GetMapActor();
	FElysiumSoundSchemeManager* Mgr = Map ? Map->GetSchemeManager() : nullptr;
	UElysiumAudioSubsystem* Audio = GetAudioSubsystem();
	if (Mgr == nullptr)
	{
		ImGui::TextDisabled("No SoundScheme manager (no map / no .ents loaded).");
		return;
	}

	// --- Music state machine ----------------------------------------------------------------
	ImGui::SeparatorText("Music state");
	const EElysiumMusicState State = Mgr->MusicState();
	const char* StateNames[] = { "Explore (safe)", "Combat", "Alert" };
	ImGui::Text("Current: %s", StateNames[(int32)State]);
	auto StateButton = [&](const char* Label, EElysiumMusicState Target, int32 Cv)
	{
		const bool bOn = State == Target;
		if (bOn) { ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.25f, 1.f)); }
		if (ImGui::Button(Label)) { SetMusicStateCvar(Cv); }
		if (bOn) { ImGui::PopStyleColor(); }
	};
	StateButton("Explore", EElysiumMusicState::Explore, 0); ImGui::SameLine();
	StateButton("Combat", EElysiumMusicState::Combat, 1);   ImGui::SameLine();
	StateButton("Alert", EElysiumMusicState::Alert, 2);

	// --- Active scheme ----------------------------------------------------------------------
	ImGui::SeparatorText("Active scheme");
	if (!Mgr->HasActiveScheme())
	{
		const FString& Rel = Mgr->ActiveSchemeRel();
		if (Rel.IsEmpty())
		{
			ImGui::TextDisabled("No scheme active.");
		}
		else
		{
			ImGui::TextColored(ImVec4(1.f, 0.6f, 0.35f, 1.f), "%s", COG_TCHAR_TO_CHAR(*Rel));
			ImGui::TextDisabled("scheme file missing/unparsed — run PL5a (UE_extract_sounds.py) to mirror it.");
		}
	}
	else
	{
		const FElysiumSoundScheme& S = Mgr->ActiveScheme();
		ImGui::Text("%s", COG_TCHAR_TO_CHAR(*Mgr->ActiveSchemeRel()));
		const FVector A = Mgr->ActiveAnchor();
		ImGui::Text("anchor (%.0f, %.0f, %.0f)  ·  RandomSoundCount %d  ·  RoomDSP %d  ·  active randoms %d",
			A.X, A.Y, A.Z, S.RandomSoundCount, S.RoomDSP, Mgr->ActiveRandomVoiceCount());

		auto SoundRow = [](const char* Label, const FElysiumSchemeSound& Snd)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(Label);
			ImGui::TableNextColumn();
			if (Snd.IsSet()) { ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Snd.Filename)); }
			else { ImGui::TextDisabled("(none)"); }
			ImGui::TableNextColumn(); ImGui::Text("%.2f", Snd.Volume);
			ImGui::TableNextColumn(); ImGui::TextUnformatted(Snd.bDry ? "dry" : "wet");
		};
		const ImGuiTableFlags TF = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##Stems", 4, TF))
		{
			ImGui::TableSetupColumn("Block", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 64.0f);
			ImGui::TableSetupColumn("Filename");
			ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 40.0f);
			ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 36.0f);
			ImGui::TableHeadersRow();
			SoundRow("Ambient", S.Ambient);
			SoundRow("Music", S.Music);
			SoundRow("Combat", S.Combat);
			SoundRow("Alert", S.Alert);
			ImGui::EndTable();
		}

		if (S.RandomSounds.Num() > 0 && ImGui::TreeNode("RandomSounds", "RandomSounds (%d)", S.RandomSounds.Num()))
		{
			if (ImGui::BeginTable("##Rnd", 5, TF, ImVec2(0, GetDpiScale() * 130.0f)))
			{
				ImGui::TableSetupColumn("Filename");
				ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 40.0f);
				ImGui::TableSetupColumn("Vol", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 40.0f);
				ImGui::TableSetupColumn("Dist", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 80.0f);
				ImGui::TableSetupColumn("Pitch", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 68.0f);
				ImGui::TableHeadersRow();
				for (const FElysiumRandomSound& R : S.RandomSounds)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*R.Filename));
					ImGui::TableNextColumn(); ImGui::Text("%d", R.Frequency);
					ImGui::TableNextColumn(); ImGui::Text("%.2f", R.Volume);
					ImGui::TableNextColumn(); ImGui::Text("%.0f-%.0f", R.DistMin, R.DistMax);
					ImGui::TableNextColumn(); ImGui::Text("%d-%d", R.PitchMin, R.PitchMax);
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		}
	}

	// --- This map's ambient_soundscheme anchors ---------------------------------------------
	ImGui::SeparatorText("ambient_soundscheme anchors");
	const FElysiumEntityWorld* World = GetEntityWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No entity world.");
		return;
	}
	int32 Shown = 0;
	for (const TUniquePtr<FElysiumEntity>& EPtr : World->Entities())
	{
		const FElysiumEntity* E = EPtr.Get();
		if (E == nullptr || E->Def == nullptr ||
			!E->Def->Classname.Equals(TEXT("ambient_soundscheme"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		++Shown;
		const FString Rel = E->Def->Keys.FindRef(TEXT("scheme_file")).Replace(TEXT("\\"), TEXT("/"));
		const FVector Anchor = E->Def->Origin;
		const bool bActive = Mgr->ActiveSchemeRel() == Rel;

		ImGui::PushID(E->Handle.Index);
		if (ImGui::SmallButton("FadeIn"))
		{
			Mgr->FadeInScheme(Audio, Rel, Anchor, 2.f);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("FadeOut"))
		{
			Mgr->FadeOutScheme(Audio, Rel, 2.f);
		}
		ImGui::SameLine();
		if (bActive) { ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.f), "%s", COG_TCHAR_TO_CHAR(*Rel)); }
		else         { ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rel)); }
		if (!E->TargetName.IsEmpty())
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", COG_TCHAR_TO_CHAR(*E->TargetName));
		}
		ImGui::PopID();
	}
	if (Shown == 0)
	{
		ImGui::TextDisabled("No ambient_soundscheme entities on this map.");
	}
}

#endif // ENABLE_COG
