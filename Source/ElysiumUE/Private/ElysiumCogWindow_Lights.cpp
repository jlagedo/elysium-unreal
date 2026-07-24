#include "ElysiumCogWindow_Lights.h"

#if ENABLE_COG

#include "ElysiumLightRig.h"
#include "ElysiumMapActor.h"

#include "Components/LightComponent.h"
#include "CogWidgets.h"
#include "imgui.h"

namespace
{
	const char* TypeLabel(int32 Type)
	{
		switch (Type)
		{
		case 0:  return "tex";
		case 1:  return "point";
		case 2:  return "spot";
		case 3:  return "sun";
		default: return "?";
		}
	}
}

void FElysiumCogWindow_Lights::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Lights::RenderHelp()
{
	ImGui::Text(
		"Real-time light rig (one Unreal light per VtMB WORLDLIGHTS source). Toggle rig visibility, "
		"tune the calibration live (the sliders re-derive every light's intensity/reach with no map "
		"reload), and browse the per-source list. These are the same knobs as the elysium.LightScale "
		"cvar and the rig's editor properties; reload the map to re-read the sidecar from scratch.");
}

void FElysiumCogWindow_Lights::RenderContent()
{
	Super::RenderContent();

	AElysiumMapActor* Map = GetMapActor();
	UElysiumLightRig* Rig = Map ? Map->GetLightRig() : nullptr;
	if (Rig == nullptr)
	{
		ImGui::TextDisabled("No light rig on this map.");
		return;
	}

	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig->Sources();

	// --- Visibility + counts -------------------------------------------------------------------
	bool bVisible = Rig->AreLightsVisible();
	if (ImGui::Checkbox("Rig visible", &bVisible))
	{
		Rig->SetLightsVisible(bVisible);   // also elysium.lights / the pawn's L key
	}

	int32 NumPoint = 0, NumSpot = 0, NumTex = 0, NumSun = 0, NumAnimated = 0;
	for (const UElysiumLightRig::FLightSource& S : Sources)
	{
		switch (S.Type)
		{
		case 0: ++NumTex; break;
		case 1: ++NumPoint; break;
		case 2: ++NumSpot; break;
		case 3: ++NumSun; break;
		default: break;
		}
		NumAnimated += (S.Style >= 1) ? 1 : 0;
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(%d lights)", Sources.Num());
	ImGui::Text("%d point · %d spot · %d tex · %d sun · %d animated%s",
		NumPoint, NumSpot, NumTex, NumSun, NumAnimated,
		Rig->bHasSkyAmbient ? " · +skyambient" : "");

	// --- Live calibration ----------------------------------------------------------------------
	ImGui::SeparatorText("Calibration (live)");

	// Snapshot before the sliders; any change re-tunes the running rig in place.
	const float PrevPointSpot = Rig->PointSpotScale;
	const float PrevMaxBright = Rig->MaxBrightness;
	const float PrevFalloff = Rig->FalloffExponent;
	const float PrevRadius = Rig->RadiusScale;
	const float PrevSpecular = Rig->SpecularScale;
	const float PrevSunLux = Rig->SunScaleLux;

	// The sliders stretch with the window, minus a gutter sized to the longest label — an ImGui
	// slider draws its label to its right, so a fixed slider width pushes that label off the edge of
	// a narrow window. The floor keeps the track grabbable when the window is dragged very narrow.
	const float LabelGutter = ImGui::CalcTextSize("Point/spot scale").x + ImGui::GetStyle().ItemInnerSpacing.x;
	const float SliderWidth = FMath::Max(GetDpiScale() * 110.0f,
		ImGui::GetContentRegionAvail().x - LabelGutter);

	ImGui::SetNextItemWidth(SliderWidth);
	FCogWidgets::SliderWithReset("Point/spot scale", &Rig->PointSpotScale, 0.0001f, 0.02f, 0.003f, "%.4f");
	ImGui::SetNextItemWidth(SliderWidth);
	FCogWidgets::SliderWithReset("Max brightness", &Rig->MaxBrightness, 0.5f, 20.0f, 8.0f, "%.1f");
	ImGui::SetNextItemWidth(SliderWidth);
	FCogWidgets::SliderWithReset("Falloff exponent", &Rig->FalloffExponent, 0.2f, 8.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	FCogWidgets::SliderWithReset("Reach scale", &Rig->RadiusScale, 0.25f, 4.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	FCogWidgets::SliderWithReset("Specular scale", &Rig->SpecularScale, 0.0f, 1.0f, 0.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	FCogWidgets::SliderWithReset("Sun lux scale", &Rig->SunScaleLux, 0.5f, 30.0f, 8.0f, "%.1f");
	ImGui::TextDisabled("right-click a slider to reset it");

	const bool bChanged =
		PrevPointSpot != Rig->PointSpotScale || PrevMaxBright != Rig->MaxBrightness ||
		PrevFalloff != Rig->FalloffExponent || PrevRadius != Rig->RadiusScale ||
		PrevSpecular != Rig->SpecularScale || PrevSunLux != Rig->SunScaleLux;
	if (bChanged)
	{
		Rig->ApplyLiveTuning();
	}

	// --- Per-source list -----------------------------------------------------------------------
	ImGui::SeparatorText("Sources");
	if (Sources.Num() == 0)
	{
		ImGui::TextDisabled("No lights loaded.");
		return;
	}

	if (ImGui::BeginTable("##Lights", 5,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit,
		ImVec2(0.0f, GetDpiScale() * 220.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 34.0f);
		ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		// Swatch + the raw WORLDLIGHTS magnitude the swatch was derived from, so the header names both.
		ImGui::TableSetupColumn("colour/mag", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 92.0f);
		ImGui::TableSetupColumn("intensity", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("style", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		ImGui::TableHeadersRow();

		ImGuiListClipper Clipper;
		Clipper.Begin(Sources.Num());
		while (Clipper.Step())
		{
			for (int32 Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
			{
				const UElysiumLightRig::FLightSource& S = Sources[Row];
				const ULightComponent* Light = S.Light.Get();
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%d", Row);

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(TypeLabel(S.Type));

				ImGui::TableNextColumn();
				const FLinearColor Col = Light ? Light->GetLightColor() : FLinearColor::Black;
				// Sized to the text line so the swatch and the magnitude beside it share a baseline
				// at any font scale.
				const float Swatch = ImGui::GetTextLineHeight();
				ImGui::ColorButton("##c", ImVec4(Col.R, Col.G, Col.B, 1.0f),
					ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker, ImVec2(Swatch, Swatch));
				ImGui::SameLine();
				ImGui::TextDisabled("%.0f", S.Mag);

				ImGui::TableNextColumn();
				if (Light == nullptr)
				{
					ImGui::TextDisabled("(gone)");
				}
				else if (S.Type == 3)
				{
					ImGui::Text("%.2f lux", S.BaseIntensity);
				}
				else
				{
					ImGui::Text("%.2f · reach %.0f m", S.BaseIntensity,
						(S.RadiusCm > 1.f ? S.RadiusCm : Rig->FallbackRadiusCm) * Rig->RadiusScale / 100.f);
				}

				ImGui::TableNextColumn();
				if (S.Style >= 1)
				{
					ImGui::Text("%d", S.Style);
				}
				else
				{
					ImGui::TextDisabled("-");
				}
			}
		}
		ImGui::EndTable();
	}
}

#endif // ENABLE_COG
