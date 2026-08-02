#include "Debug/ElysiumCogWindow_Environment.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumGameClock.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumLightRig.h"
#include "Visual/ElysiumMapVisuals.h"

#include "HAL/IConsoleManager.h"
#include "imgui.h"

namespace
{
	IConsoleVariable* FindCVar(const TCHAR* Name)
	{
		return IConsoleManager::Get().FindConsoleVariable(Name);
	}

	float ReadFloat(const TCHAR* Name, float Fallback)
	{
		const IConsoleVariable* Variable = FindCVar(Name);
		return Variable ? Variable->GetFloat() : Fallback;
	}

	bool ReadBool(const TCHAR* Name, bool Fallback)
	{
		const IConsoleVariable* Variable = FindCVar(Name);
		return Variable ? Variable->GetInt() != 0 : Fallback;
	}

	void WriteFloat(const TCHAR* Name, float Value)
	{
		if (IConsoleVariable* Variable = FindCVar(Name))
		{
			Variable->Set(Value);
		}
	}

	void WriteBool(const TCHAR* Name, bool bValue)
	{
		if (IConsoleVariable* Variable = FindCVar(Name))
		{
			Variable->Set(bValue ? 1 : 0);
		}
	}

	bool SliderCVar(const char* Label, const TCHAR* Name, float Minimum, float Maximum,
		const char* Format)
	{
		float Value = ReadFloat(Name, Minimum);
		if (!ImGui::SliderFloat(Label, &Value, Minimum, Maximum, Format))
		{
			return false;
		}
		WriteFloat(Name, Value);
		return true;
	}

	float MaterialResponse(float Wetness, float OutputScale, float AuthoredScale)
	{
		return FMath::Clamp(Wetness * OutputScale * AuthoredScale, 0.0f, 1.0f);
	}
}

void FElysiumCogWindow_Environment::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Environment::RenderHelp()
{
	ImGui::Text(
		"Live control of MPC_ElysiumEnvironment. Authored sm_hub_1 timers continue updating the "
		"engine-neutral wetness state at all times. Override substitutes only the visible material "
		"input; Follow authored reconnects to the state already in progress. The output-scale and "
		"enhancement controls tune the one shared material graph. Local-light specular is the same "
		"live light-rig value exposed by Elysium.Lights. Niagara remains disconnected.");
}

void FElysiumCogWindow_Environment::RenderContent()
{
	Super::RenderContent();

	AElysiumMapActor* Map = GetMapActor();
	if (Map == nullptr)
	{
		ImGui::TextDisabled("No map loaded.");
		return;
	}

	const FElysiumWeatherTransition& Authored = Map->GetWetnessTransition();
	const float Presented = Map->GetPresentedWetness();
	const float OutputScale = Map->GetPresentedWetnessScale();
	const bool bOverrideApplied = Map->IsEnvironmentWetnessOverridden();

	ImGui::SeparatorText("Live wetness");
	if (Presented > KINDA_SMALL_NUMBER)
	{
		ImGui::TextColored(ElysiumCogStyle::ColOk, "WET  %.3f", Presented);
	}
	else
	{
		ImGui::TextDisabled("DRY  0.000");
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s", bOverrideApplied ? "manual override" : "authored events");

	char WetnessLabel[32];
	FCStringAnsi::Snprintf(WetnessLabel, UE_ARRAY_COUNT(WetnessLabel), "%.3f", Presented);
	ImGui::ProgressBar(Presented, ImVec2(-1.0f, 0.0f), WetnessLabel);
	ImGui::Text("Authored current %.3f  target %.3f", Authored.CurrentWetness, Authored.TargetWetness);
	if (Authored.Duration > 0.0f)
	{
		double Now = Authored.StartTime;
		if (const UElysiumGameStateSubsystem* State = GetGameState())
		{
			Now = State->GameClock().GetNow();
		}
		const double Remaining = FMath::Max(0.0, Authored.StartTime + Authored.Duration - Now);
		ImGui::TextDisabled("authored transition %.1f s total, %.1f s remaining",
			Authored.Duration, Remaining);
	}
	else
	{
		ImGui::TextDisabled("authored transition idle");
	}

	ImGui::SeparatorText("Override authored events");
	bool bOverride = ReadBool(TEXT("elysium.EnvironmentWetnessOverride"), false);
	if (ImGui::Checkbox("Manual presentation override", &bOverride))
	{
		WriteBool(TEXT("elysium.EnvironmentWetnessOverride"), bOverride);
	}
	if (ImGui::Button("Dry"))
	{
		WriteFloat(TEXT("elysium.EnvironmentWetness"), 0.0f);
		WriteBool(TEXT("elysium.EnvironmentWetnessOverride"), true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Half"))
	{
		WriteFloat(TEXT("elysium.EnvironmentWetness"), 0.5f);
		WriteBool(TEXT("elysium.EnvironmentWetnessOverride"), true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Wet"))
	{
		WriteFloat(TEXT("elysium.EnvironmentWetness"), 1.0f);
		WriteBool(TEXT("elysium.EnvironmentWetnessOverride"), true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Follow authored"))
	{
		WriteBool(TEXT("elysium.EnvironmentWetnessOverride"), false);
	}
	float Manual = ReadFloat(TEXT("elysium.EnvironmentWetness"), 1.0f);
	if (ImGui::SliderFloat("Manual wetness", &Manual, 0.0f, 1.0f, "%.3f"))
	{
		WriteFloat(TEXT("elysium.EnvironmentWetness"), Manual);
		WriteBool(TEXT("elysium.EnvironmentWetnessOverride"), true);
	}
	ImGui::TextDisabled("The timer graph and save state continue underneath an override.");

	ImGui::SeparatorText("Material response");
	SliderCVar("Output scale", TEXT("elysium.EnvironmentWetnessScale"), 0.0f, 4.0f, "%.2fx");
	ImGui::TextDisabled("Multiplies, then saturates, each patch-authored material scale.");
	if (ImGui::BeginTable("##WetnessScales", 3,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Patch group");
		ImGui::TableSetupColumn("Authored", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 65.0f);
		ImGui::TableSetupColumn("Live", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 65.0f);
		ImGui::TableHeadersRow();
		auto Row = [&](const char* Label, float Scale)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(Label);
			ImGui::TableNextColumn(); ImGui::Text("%.2f", Scale);
			ImGui::TableNextColumn(); ImGui::Text("%.3f", MaterialResponse(Presented, OutputScale, Scale));
		};
		Row("Asphalt", 0.56f);
		Row("Six streets", 0.60f);
		Row("Seven surfaces", 1.00f);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Presentation enhancement");
	SliderCVar("Enhancement", TEXT("elysium.RainEnhancement"), 0.0f, 1.0f, "%.2f");
	SliderCVar("Full-wet darken", TEXT("elysium.RainWetDarken"), 0.0f, 0.25f, "%.3f");
	SliderCVar("Roughness reduction", TEXT("elysium.RainWetRoughness"), 0.0f, 0.50f, "%.3f");
	SliderCVar("Rain light response", TEXT("elysium.RainLightResponse"), 0.0f, 1.0f, "%.2f");
	ImGui::TextDisabled("Light response is retained for the future particle slice; it has no visible wetness effect.");
	if (ImGui::Button("Authored reference"))
	{
		WriteFloat(TEXT("elysium.RainEnhancement"), 0.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Enhanced defaults"))
	{
		WriteFloat(TEXT("elysium.RainEnhancement"), 1.0f);
		WriteFloat(TEXT("elysium.RainWetDarken"), 0.06f);
		WriteFloat(TEXT("elysium.RainWetRoughness"), 0.10f);
		WriteFloat(TEXT("elysium.RainLightResponse"), 0.25f);
		WriteFloat(TEXT("elysium.EnvironmentWetnessScale"), 1.0f);
	}

	ImGui::SeparatorText("Reflection energy");
	UElysiumMapVisuals* Visuals = Map->GetVisuals();
	UElysiumLightRig* LightRig = Visuals ? Visuals->GetLightRig() : nullptr;
	if (LightRig)
	{
		float Specular = LightRig->SpecularScale;
		if (ImGui::SliderFloat("Local-light specular", &Specular, 0.0f, 1.0f, "%.2f"))
		{
			LightRig->SpecularScale = Specular;
			LightRig->ApplyLiveTuning();
		}
		if (ImGui::Button("Source light baseline (0)"))
		{
			LightRig->SpecularScale = 0.0f;
			LightRig->ApplyLiveTuning();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reflection test (1)"))
		{
			LightRig->SpecularScale = 1.0f;
			LightRig->ApplyLiveTuning();
		}
		ImGui::TextDisabled(
			"Shared with Elysium.Lights. Non-zero affects every non-overridden source and is a "
			"presentation test, not authored wetness.");
	}
	else
	{
		ImGui::TextDisabled("The map light rig is not available yet.");
	}

	ImGui::SeparatorText("Authored I/O");
	if (ImGui::Button("Fire rain_on_timer"))
	{
		Map->FireWeatherTimer(true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Fire rain_off_timer"))
	{
		Map->FireWeatherTimer(false);
	}
	ImGui::TextDisabled("These buttons fire the existing map timers. FadeGlobalWetness arrives after its authored 10 s delay.");
}

#endif // ENABLE_COG
