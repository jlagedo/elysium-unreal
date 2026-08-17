#include "Debug/ElysiumCogWindow_Environment.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameClock.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumLightRig.h"
#include "Visual/ElysiumMapVisuals.h"

#include "CogLocalizationConfig.h"
#include "Engine/TextureCube.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstance.h"
#include "UObject/UObjectIterator.h"
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

	int32 ReadInt(const TCHAR* Name, int32 Fallback)
	{
		const IConsoleVariable* Variable = FindCVar(Name);
		return Variable ? Variable->GetInt() : Fallback;
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

	void WriteInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = FindCVar(Name))
		{
			Variable->Set(Value);
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

	struct FSourceBindingSummary
	{
		int32 Responding = 0;
		int32 Missing = 0;
		FString Cube;
		bool bMultipleCubes = false;
	};

	FSourceBindingSummary InspectSourceBindings(const AElysiumMapActor& Map)
	{
		FSourceBindingSummary Summary;
		const FString Prefix = FString::Printf(
			TEXT("/ElysiumBaked/%s/Materials/"), *Map.MapName);
		for (TObjectIterator<UMaterialInstance> It; It; ++It)
		{
			UMaterialInstance* Instance = *It;
			if (!IsValid(Instance) || !Instance->GetPathName().StartsWith(Prefix))
			{
				continue;
			}
			float WetnessDriven = 0.0f;
			if (!Instance->GetScalarParameterValue(TEXT("WetnessDriven"), WetnessDriven)
				|| WetnessDriven < 0.5f)
			{
				continue;
			}
			UTexture* Texture = nullptr;
			if (!Instance->GetTextureParameterValue(TEXT("SourceCube"), Texture)
				|| !Texture || !Texture->IsA<UTextureCube>())
			{
				++Summary.Missing;
				continue;
			}
			++Summary.Responding;
			const FString CubePath = Texture->GetPathName();
			if (Summary.Cube.IsEmpty())
			{
				Summary.Cube = CubePath;
			}
			else if (Summary.Cube != CubePath)
			{
				Summary.bMultipleCubes = true;
			}
		}
		return Summary;
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
		"Live control of the one weather path. Authored sm_hub_1 timers keep updating wetness and "
		"env_particle rate; Override substitutes only the visible material input. Rain is the "
		"follow volume those two rain_follow_emitter entities share (NS_ElysiumRain). Force rain "
		"turns that volume on without waiting for the dry timer. Material Look tunes the shared "
		"wetness graph. Local-light specular is the same live light-rig value as Look > Lighting.");
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
	UElysiumMapVisuals* Visuals = Map->GetVisuals();
	UElysiumLightRig* LightRig = Visuals ? Visuals->GetLightRig() : nullptr;
	if (!ImGui::BeginTabBar("##EnvironmentViews"))
	{
		return;
	}

	if (ImGui::BeginTabItem("Wetness"))
	{
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

	ImGui::SeparatorText("Authored events");
	if (ImGui::Button("Start rain timer"))
	{
		Map->FireWeatherTimer(true);
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop rain timer"))
	{
		Map->FireWeatherTimer(false);
	}
	ImGui::TextDisabled("Uses the map's existing timer path, including its authored 10 second delay.");
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Rain"))
	{
		const bool bFollow = Map->IsFollowRainActive();
		if (bFollow)
		{
			ImGui::TextColored(ElysiumCogStyle::ColOk, "RAIN");
		}
		else
		{
			ImGui::TextDisabled("DRY");
		}
		ImGui::SameLine();
		const bool bForced = ReadBool(TEXT("elysium.RainForce"), false);
		ImGui::TextDisabled("%s", bForced ? "forced" : "authored emitters");
		if (bFollow)
		{
			const FString Spawn = Map->GetFollowRainLocation().ToCompactString();
			ImGui::TextDisabled("spawn %s", COG_TCHAR_TO_CHAR(*Spawn));
		}

		ImGui::SeparatorText("Presentation");
		bool bForce = bForced;
		if (ImGui::Checkbox("Force follow rain", &bForce))
		{
			WriteBool(TEXT("elysium.RainForce"), bForce);
		}
		ImGui::TextDisabled(
			"Ignores env_particle rate. Authored wetness, audio and the timer graph keep running.");
		SliderCVar("Rate scale", TEXT("elysium.RainRateScale"), 0.0f, 4.0f, "%.2fx");
		SliderCVar("Streak width", TEXT("elysium.RainStreakWidth"), 0.2f, 8.0f, "%.2f cm");
		SliderCVar("Streak length", TEXT("elysium.RainStreakLength"), 4.0f, 160.0f, "%.1f cm");
		SliderCVar("Streak alpha", TEXT("elysium.RainStreakAlpha"), 0.0f, 1.0f, "%.2f");
		ImGui::TextDisabled(
			"Width, length and alpha are pushed as Niagara user values. The generated system still "
			"bakes 1.2 x 55 cm at 0.18; those three sliders wait on a spawn-expression bind.");

		ImGui::SeparatorText("Authored cycle");
		if (ImGui::Button("Start rain timer"))
		{
			Map->FireWeatherTimer(true);
		}
		ImGui::SameLine();
		if (ImGui::Button("Stop rain timer"))
		{
			Map->FireWeatherTimer(false);
		}
		ImGui::TextDisabled("Same rain_on_timer / rain_off_timer I/O as the Wetness tab.");

		ImGui::SeparatorText("env_particle");
		const TMap<int32, FElysiumWeatherEmitterState>& Emitters = Map->GetWeatherEmitters();
		if (Emitters.IsEmpty())
		{
			ImGui::TextDisabled("No published emitters on this map.");
		}
		else if (ImGui::BeginTable("##RainEmitters", 6,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Entity");
			ImGui::TableSetupColumn("Definition");
			ImGui::TableSetupColumn("Rate", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 90.0f);
			ImGui::TableSetupColumn("Bounds", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 60.0f);
			ImGui::TableSetupColumn("Attach", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 50.0f);
			ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 55.0f);
			ImGui::TableHeadersRow();
			FElysiumEntityWorld* World = GetEntityWorld();
			for (const TPair<int32, FElysiumWeatherEmitterState>& Pair : Emitters)
			{
				const FElysiumWeatherEmitterState& Emitter = Pair.Value;
				const FElysiumEntity* Entity = World ? World->Resolve(Emitter.Entity) : nullptr;
				const bool bFollowDef = Emitter.ParticleDefinition.Equals(
					TEXT("rain_follow_emitter"), ESearchCase::IgnoreCase);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				const FString EntityLabel = Entity && !Entity->TargetName.IsEmpty()
					? Entity->TargetName
					: FString::Printf(TEXT("#%d"), Emitter.Entity.Index);
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*EntityLabel));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Emitter.ParticleDefinition));
				ImGui::TableNextColumn();
				ImGui::Text("%.2f -> %.2f", Emitter.RateScale, Emitter.RampTargetScale);
				ImGui::TableNextColumn();
				ImGui::Text("%.0f", Emitter.BoundsCm);
				ImGui::TableNextColumn();
				ImGui::Text("%d", Emitter.AttachType);
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(bFollowDef ? "follow" : "other");
			}
			ImGui::EndTable();
		}
		ImGui::TextDisabled(
			"func_particle rain boxes are still stubs. Only env_particle publishes through this seam.");
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Material look"))
	{
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

	if (ImGui::CollapsingHeader("Source cube diagnostics"))
	{
	const FSourceBindingSummary Bindings = InspectSourceBindings(*Map);
	if (Bindings.Cube.IsEmpty())
	{
		ImGui::TextDisabled("Bound cube: none loaded");
	}
	else
	{
		ImGui::TextWrapped("Bound cube: %s%s", TCHAR_TO_UTF8(*Bindings.Cube),
			Bindings.bMultipleCubes ? " (multiple)" : "");
	}
	ImGui::Text("Responding wet materials: %d", Bindings.Responding);
	if (Bindings.Missing > 0)
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "Missing SourceCube bindings: %d", Bindings.Missing);
	}
	else
	{
		ImGui::TextColored(ElysiumCogStyle::ColOk, "Missing SourceCube bindings: 0");
	}
	SliderCVar("Source retain", TEXT("elysium.RainSourceRetain"), 0.0f, 1.0f, "%.2f");
	SliderCVar("Wet specular", TEXT("elysium.RainWetSpecular"), 0.0f, 1.0f, "%.2f");
	static const char* DebugModes[] = {
		"Final", "Raw Mask", "Coarse Mask", "Wet Factor", "Cube Sample",
		"Source Contribution", "Enhanced Coverage"
	};
	int32 DebugMode = FMath::Clamp(ReadInt(TEXT("elysium.RainReflectionDebug"), 0), 0, 6);
	if (ImGui::Combo("Debug view", &DebugMode, DebugModes, UE_ARRAY_COUNT(DebugModes)))
	{
		WriteInt(TEXT("elysium.RainReflectionDebug"), DebugMode);
	}
	}

	ImGui::SeparatorText("Presentation enhancement");
	SliderCVar("Enhancement", TEXT("elysium.RainEnhancement"), 0.0f, 1.0f, "%.2f");
	SliderCVar("Full-wet darken", TEXT("elysium.RainWetDarken"), 0.0f, 0.25f, "%.3f");
	SliderCVar("Roughness reduction", TEXT("elysium.RainWetRoughness"), 0.0f, 0.50f, "%.3f");
	SliderCVar("Rain light response", TEXT("elysium.RainLightResponse"), 0.0f, 1.0f, "%.2f");
	ImGui::TextDisabled(
		"Writes the MPC and a Niagara user value. The unlit follow material does not sample it.");
	if (ImGui::Button("Source reference"))
	{
		WriteFloat(TEXT("elysium.RainEnhancement"), 0.0f);
		WriteInt(TEXT("elysium.RainReflectionDebug"), 0);
		if (LightRig)
		{
			LightRig->SpecularScale = 0.0f;
			LightRig->ApplyLiveTuning();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Enhanced defaults"))
	{
		WriteFloat(TEXT("elysium.RainEnhancement"), 1.0f);
		WriteFloat(TEXT("elysium.RainWetDarken"), 0.06f);
		WriteFloat(TEXT("elysium.RainWetRoughness"), 0.10f);
		WriteFloat(TEXT("elysium.RainLightResponse"), 0.25f);
		WriteFloat(TEXT("elysium.RainSourceRetain"), 1.0f);
		WriteFloat(TEXT("elysium.RainWetSpecular"), 0.50f);
		WriteInt(TEXT("elysium.RainReflectionDebug"), 0);
		WriteFloat(TEXT("elysium.EnvironmentWetnessScale"), 1.0f);
	}

	ImGui::SeparatorText("Reflection energy");
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

		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

#endif // ENABLE_COG
