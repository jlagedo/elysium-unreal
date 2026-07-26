#include "ElysiumCogWindow_Lights.h"

#if ENABLE_COG

#include "ElysiumCogStyle.h"
#include "ElysiumContentPaths.h"
#include "ElysiumLightRig.h"
#include "ElysiumMapActor.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "CogImguiContext.h"
#include "CogImguiHelper.h"
#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogSubsystem.h"
#include "CogWidgets.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonWriter.h"
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

	// Level authors copy-paste lights, so an exact raw-attribute tuple identifies one authored
	// decision — on sp_tutorial_1, 83 of 85 such batches were unanimous in the hand survey
	// (docs/light-attribution.md). Exact float equality is the point: batch members come from
	// identical sidecar rows, so their derived values are bit-identical.
	bool SameBatch(const UElysiumLightRig::FLightSource& A, const UElysiumLightRig::FLightSource& B)
	{
		return A.Type == B.Type && A.Style == B.Style && A.Mag == B.Mag
			&& A.RadiusCm == B.RadiusCm && A.Color == B.Color;
	}

	// World -> imgui screen for the per-light markers. Same shape as the inspector's projector, in
	// its simplest form: markers are points, so there is no segment to clip — anything at or behind
	// the near plane is just dropped.
	struct FLightProjector
	{
		APlayerController* PC = nullptr;
		ImVec2 Origin = ImVec2(0.0f, 0.0f);
		FVector CamPos = FVector::ZeroVector;
		FVector CamFwd = FVector::ForwardVector;

		bool Project(const FVector& P, ImVec2& Out) const
		{
			if (((P - CamPos) | CamFwd) < 12.0)
			{
				return false;
			}
			FVector2D Screen;
			if (!UGameplayStatics::ProjectWorldToScreen(PC, P, Screen, /*bPlayerViewportRelative*/ false))
			{
				return false;
			}
			Out = ImVec2(Origin.x + static_cast<float>(Screen.X), Origin.y + static_cast<float>(Screen.Y));
			return true;
		}
	};
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
		"cvar and the rig's editor properties; reload the map to re-read the sidecar from scratch.\n\n"
		"Select one light — click its row, or click its marker in the world — to edit that light on "
		"its own: intensity, colour, reach, cone, shadows, and a 3D gizmo on its transform. An edited "
		"light is marked overridden (*), so the calibration sliders and the lightstyle flicker stop "
		"driving it and leave the hand-set value alone. Isolate hides every other light, which is the "
		"quickest way to find which fixture a row is.\n\n"
		"Enabled switches one light out of the map (x) without touching a single value, so it comes "
		"back exactly as it was — the switch for walking a map deciding which sources are real "
		"fixtures and which are fill. Reviewed records that a light was judged at all: switching one "
		"off marks it reviewed by itself, a kept light is marked by hand, so coverage is data rather "
		"than an inference. Copy-pasted lights (an identical colour/mag/radius/type/style tuple) form "
		"a batch, and the selected light's batch can be switched off or marked reviewed in one go.\n\n"
		"Save writes the verdict out: the switched-off set plus every hand-set attribute, and the "
		"reviewed index list, keyed by .lights line index, to tools/out/_lights/<map>.json. One file "
		"per map, overwritten each save. The save is the map's standing hand-authored light state: "
		"map load auto-applies its disabled + reviewed sets (elysium.LightSurvey 0 turns that off, "
		"loading the full faithful rig), and the Load button is the same pass mid-session. Attribute "
		"overrides are not restored either way.\n\n"
		"Nothing else persists across a reload — attribute edits live in the running rig only, and "
		"the sidecar's calibrated values come back (the saved JSON stays).");
}

void FElysiumCogWindow_Lights::RenderTick(float DeltaTime)
{
	Super::RenderTick(DeltaTime);

	AElysiumMapActor* Map = GetMapActor();
	UElysiumLightRig* Rig = Map ? Map->GetLightRig() : nullptr;
	if (Rig == nullptr)
	{
		return;
	}

	// A selection from a previous map does not survive into this one's (shorter) source list.
	if (!Rig->Sources().IsValidIndex(SelectedSource))
	{
		SelectedSource = INDEX_NONE;
	}

	TickSolo(*Rig);
	TickMarkersAndPick(*Rig);
}

void FElysiumCogWindow_Lights::TickSolo(UElysiumLightRig& Rig)
{
	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig.Sources();

	// Gated on the window being open: RenderTick runs for every window whether or not it is
	// visible, and a map left dark by a hide pass whose UI is closed would be a trap.
	const bool bWant = bIsolate && GetIsVisible() && Sources.IsValidIndex(SelectedSource);
	if (!bWant && !bIsolateApplied)
	{
		return;
	}

	// Both applying and lifting go through the rig's own answer rather than a plain on/off, so
	// neither isolating while the rig is hidden nor soloing a hand-disabled light turns anything
	// back on behind the user.
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		if (ULightComponent* Light = Rig.SourceLight(Index))
		{
			const bool bLit = Rig.ShouldSourceBeLit(Index);
			Light->SetVisibility(bWant ? (bLit && Index == SelectedSource) : bLit);
		}
	}
	bIsolateApplied = bWant;
}

void FElysiumCogWindow_Lights::TickMarkersAndPick(UElysiumLightRig& Rig)
{
	HoveredSource = INDEX_NONE;
	bSelectPending = false;
	bClearPending = false;

	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig.Sources();
	if (Sources.Num() == 0 || !GetIsVisible())
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	if (PC == nullptr || PC->PlayerCameraManager == nullptr || Viewport == nullptr)
	{
		return;
	}

	FLightProjector Proj;
	Proj.PC = PC;
	Proj.Origin = Viewport->Pos;
	Proj.CamPos = PC->PlayerCameraManager->GetCameraLocation();
	Proj.CamFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();

	// Armed on the same terms as the inspector's click-pick: this window open, the Cog menu holding
	// the mouse, and the cursor not over an imgui window (so clicking a slider does not also pick).
	const UCogSubsystem* Cog = GetOwner();
	const bool bArmed = bClickToSelect && Cog != nullptr
		&& Cog->GetContext().GetEnableInput() && !ImGui::GetIO().WantCaptureMouse;

	// Cog's own copy of the cursor, not ImGui::GetMousePos(): the latter is invalid over NetImgui,
	// and this is the one the projection agrees with.
	ImVec2 Cursor(-FLT_MAX, -FLT_MAX);
	if (bArmed)
	{
		Cursor = Cog->GetContext().GetImguiMousePos();
	}

	const float Scale = GetDpiScale();
	const float PickRadius = Scale * 22.0f;
	int32 Nearest = INDEX_NONE;
	float NearestDistSq = PickRadius * PickRadius;

	ImDrawList* DrawList = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const ULightComponent* Light = Sources[Index].Light.Get();
		if (Light == nullptr)
		{
			continue;
		}
		ImVec2 P;
		if (!Proj.Project(Light->GetComponentLocation(), P))
		{
			continue;
		}

		if (bArmed)
		{
			const float DX = P.x - Cursor.x;
			const float DY = P.y - Cursor.y;
			const float DistSq = DX * DX + DY * DY;
			if (DistSq < NearestDistSq)
			{
				NearestDistSq = DistSq;
				Nearest = Index;
			}
		}

		if (!bDrawMarkers)
		{
			continue;
		}

		// The marker wears the light's own colour, so the overlay reads as the lighting it stands
		// for. A hidden light draws hollow — under isolate that is most of them.
		const FLinearColor C = Light->GetLightColor();
		const bool bLit = Light->GetVisibleFlag();
		const ImU32 Col = IM_COL32(
			static_cast<int>(FMath::Clamp(C.R, 0.f, 1.f) * 255.f),
			static_cast<int>(FMath::Clamp(C.G, 0.f, 1.f) * 255.f),
			static_cast<int>(FMath::Clamp(C.B, 0.f, 1.f) * 255.f),
			bLit ? 235 : 90);
		const bool bSelected = (Index == SelectedSource);
		const float R = Scale * (bSelected ? 7.0f : 4.0f);

		if (bLit)
		{
			DrawList->AddCircleFilled(P, R, Col);
		}
		DrawList->AddCircle(P, R, IM_COL32(20, 18, 22, 200), 0, Scale * 1.5f);

		if (bSelected)
		{
			// Gold ring + crosshair, matching the inspector's "this is the committed selection".
			const ImU32 Ring = IM_COL32(240, 195, 110, 255);
			const float Outer = Scale * 13.0f;
			DrawList->AddCircle(P, Outer, Ring, 0, Scale * 2.0f);
			DrawList->AddLine(ImVec2(P.x - Outer * 1.7f, P.y), ImVec2(P.x - Outer, P.y), Ring, Scale * 1.5f);
			DrawList->AddLine(ImVec2(P.x + Outer, P.y), ImVec2(P.x + Outer * 1.7f, P.y), Ring, Scale * 1.5f);
			DrawList->AddLine(ImVec2(P.x, P.y - Outer * 1.7f), ImVec2(P.x, P.y - Outer), Ring, Scale * 1.5f);
			DrawList->AddLine(ImVec2(P.x, P.y + Outer), ImVec2(P.x, P.y + Outer * 1.7f), Ring, Scale * 1.5f);
		}
	}

	if (!bArmed)
	{
		return;
	}

	// Hover: ring whatever a click would take, so a cluster is legible before committing.
	if (bDrawMarkers && Nearest != INDEX_NONE && Nearest != SelectedSource)
	{
		if (const ULightComponent* Light = Sources[Nearest].Light.Get())
		{
			ImVec2 P;
			if (Proj.Project(Light->GetComponentLocation(), P))
			{
				DrawList->AddCircle(P, Scale * 10.0f, IM_COL32(240, 195, 110, 140), 0, Scale * 1.5f);
			}
		}
	}

	// Noted, not applied: CommitPendingPick resolves it once the gizmo has had the click.
	HoveredSource = Nearest;
	bSelectPending = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Nearest != INDEX_NONE;
	bClearPending = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

void FElysiumCogWindow_Lights::CommitPendingPick()
{
	// The gizmo owns the click whenever it is mid-drag — including the frame it grabbed on, since
	// it submits before this runs.
	const bool bGizmoBusy = Gizmo.DraggedElementType != ECogDebug_GizmoElementType::MAX;
	if (!bGizmoBusy)
	{
		if (bSelectPending)
		{
			SelectedSource = HoveredSource;
			bScrollToSelected = true;
		}
		else if (bClearPending)
		{
			SelectedSource = INDEX_NONE;
		}
	}
	bSelectPending = false;
	bClearPending = false;
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
		bIsolate = false;                  // the master switch outranks an isolate in progress
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

	ImGui::Checkbox("Click to select", &bClickToSelect);
	ImGui::SetItemTooltip("LMB over the world selects the nearest light marker; RMB clears. Armed "
		"only while this window is open and the Cog menu owns the mouse. Lights have no collision, "
		"so this picks by screen distance to the marker, not by a trace.");
	ImGui::SameLine();
	ImGui::Checkbox("Markers", &bDrawMarkers);
	ImGui::SetItemTooltip("Draw a dot per light over the world, in that light's own colour. "
		"Hollow = hidden.");

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

	// --- Ambience ------------------------------------------------------------------------------
	// The sky light and height fog are actors baked into the level, adopted by the map actor. They
	// belong here because they are the other half of the same calibration: with a real cubemap on
	// the sky light Lumen occludes it properly, so how much the sky contributes and how much the
	// per-source rig has to carry are one decision, not two.
	ImGui::SeparatorText("Ambience (live)");

	if (USkyLightComponent* Sky = Map->GetSkyLight())
	{
		float Intensity = Sky->Intensity;
		ImGui::SetNextItemWidth(SliderWidth);
		FCogWidgets::SliderWithReset("Sky intensity", &Intensity, 0.0f, 4.0f, 1.0f, "%.2f");
		if (Intensity != Sky->Intensity)
		{
			Sky->SetIntensity(Intensity);
		}

		FLinearColor Color = Sky->GetLightColor();
		ImGui::SetNextItemWidth(SliderWidth);
		if (ImGui::ColorEdit3("Sky colour", &Color.R))
		{
			Sky->SetLightColor(Color);
		}

		// Cubemap off falls back to a flat constant ambient of the light colour — what the sky
		// light did before it was given the real sky. Kept togglable because it is the A/B that
		// shows what sky occlusion is actually buying while the map is being recalibrated.
		bool bUseCube = Sky->Cubemap != nullptr;
		const bool bCanToggle = bUseCube || SkyCubemap.IsValid();
		if (bCanToggle && ImGui::Checkbox("Sky cubemap (occluded IBL)", &bUseCube))
		{
			if (bUseCube)
			{
				Sky->Cubemap = SkyCubemap.Get();
			}
			else
			{
				SkyCubemap = Sky->Cubemap;
				Sky->Cubemap = nullptr;
			}
			Sky->RecaptureSky();
		}

		bool bLowerBlack = Sky->bLowerHemisphereIsBlack;
		if (ImGui::Checkbox("Lower hemisphere black", &bLowerBlack))
		{
			Sky->bLowerHemisphereIsBlack = bLowerBlack;
			Sky->RecaptureSky();
		}
	}
	else
	{
		ImGui::TextDisabled("No sky light in this level.");
	}

	if (UExponentialHeightFogComponent* Fog = Map->GetHeightFog())
	{
		float Density = Fog->FogDensity;
		ImGui::SetNextItemWidth(SliderWidth);
		FCogWidgets::SliderWithReset("Fog density", &Density, 0.0f, 0.05f, 0.002f, "%.4f");
		if (Density != Fog->FogDensity)
		{
			Fog->SetFogDensity(Density);
		}

		float Start = Fog->StartDistance;
		ImGui::SetNextItemWidth(SliderWidth);
		FCogWidgets::SliderWithReset("Fog start (cm)", &Start, 0.0f, 20000.0f, 0.0f, "%.0f");
		if (Start != Fog->StartDistance)
		{
			Fog->SetStartDistance(Start);
		}

		bool bVolumetric = Fog->bEnableVolumetricFog;
		if (ImGui::Checkbox("Volumetric fog", &bVolumetric))
		{
			Fog->SetVolumetricFog(bVolumetric);
		}
		if (bVolumetric)
		{
			float Extinction = Fog->VolumetricFogExtinctionScale;
			ImGui::SetNextItemWidth(SliderWidth);
			FCogWidgets::SliderWithReset("Volumetric extinction", &Extinction, 0.0f, 10.0f, 1.0f, "%.2f");
			if (Extinction != Fog->VolumetricFogExtinctionScale)
			{
				Fog->SetVolumetricFogExtinctionScale(Extinction);
			}
		}
	}
	else
	{
		ImGui::TextDisabled("No height fog in this level (the map's .env has fog off).");
	}

	// --- The selected light --------------------------------------------------------------------
	// Above the source list, not below it: the list is a picker you visit once, while the editor is
	// what the map is actually being worked through, so it sits with the other live controls.
	ImGui::SeparatorText("Selected light");
	if (Sources.IsValidIndex(SelectedSource))
	{
		RenderSelectedSource(*Rig, SelectedSource);
	}
	else
	{
		ImGui::TextDisabled("Click a row below, or a marker in the world.");
	}
	RenderEditActions(*Rig, Map->MapName);

	// --- Per-source list -----------------------------------------------------------------------
	ImGui::SeparatorText("Sources");
	if (Sources.Num() == 0)
	{
		ImGui::TextDisabled("No lights loaded.");
		CommitPendingPick();
		return;
	}

	if (ImGui::BeginTable("##Lights", 5,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit,
		ImVec2(0.0f, GetDpiScale() * 220.0f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 48.0f);
		ImGui::TableSetupColumn("type", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		// Swatch + the raw WORLDLIGHTS magnitude the swatch was derived from, so the header names both.
		ImGui::TableSetupColumn("colour/mag", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 92.0f);
		ImGui::TableSetupColumn("intensity", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("style", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.0f);
		ImGui::TableHeadersRow();

		ImGuiListClipper Clipper;
		Clipper.Begin(Sources.Num());
		// A world pick can land on a row the clipper would skip, so force it to be laid out —
		// SetScrollHereY below only works on a row that actually submits.
		if (bScrollToSelected && Sources.IsValidIndex(SelectedSource))
		{
			Clipper.IncludeItemByIndex(SelectedSource);
		}
		while (Clipper.Step())
		{
			for (int32 Row = Clipper.DisplayStart; Row < Clipper.DisplayEnd; ++Row)
			{
				const UElysiumLightRig::FLightSource& S = Sources[Row];
				const ULightComponent* Light = S.Light.Get();
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				// The whole row is the hit target (SpanAllColumns), so the index cell doubles as
				// the selectable. The index carries both edit states, so the list shows at a
				// glance which lights are off the global calibration and which are switched off.
				ImGui::PushID(Row);
				if (ImGui::Selectable("##row", Row == SelectedSource,
					ImGuiSelectableFlags_SpanAllColumns))
				{
					SelectedSource = (Row == SelectedSource) ? INDEX_NONE : Row;
				}
				ImGui::PopID();
				if (bScrollToSelected && Row == SelectedSource)
				{
					ImGui::SetScrollHereY(0.5f);
				}
				ImGui::SameLine();
				if (S.bDisabled)
				{
					ImGui::TextDisabled("%dx", Row);
				}
				else if (S.bOverridden)
				{
					ImGui::TextColored(ElysiumCogStyle::ColWarn, "%d*", Row);
				}
				else if (S.bReviewed)
				{
					ImGui::TextColored(ElysiumCogStyle::ColOk, "%d", Row);
				}
				else
				{
					ImGui::Text("%d", Row);
				}

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
					// Reach off the live component, not re-derived from the sidecar row: an
					// overridden light's radius is whatever the inspector set it to.
					const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light);
					ImGui::Text("%.2f · reach %.0f m", S.BaseIntensity,
						(Local ? Local->AttenuationRadius : 0.f) / 100.f);
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
	bScrollToSelected = false;
	ImGui::TextDisabled("* overridden · x switched off · green = reviewed & kept");

	// After the gizmo, so a click that grabbed a handle is not also read as a new selection.
	CommitPendingPick();
}

void FElysiumCogWindow_Lights::RenderEditActions(UElysiumLightRig& Rig, const FString& MapName)
{
	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig.Sources();
	int32 NumDisabled = 0, NumOverridden = 0, NumReviewed = 0;
	for (const UElysiumLightRig::FLightSource& S : Sources)
	{
		NumDisabled += S.bDisabled ? 1 : 0;
		NumOverridden += S.bOverridden ? 1 : 0;
		NumReviewed += S.bReviewed ? 1 : 0;
	}

	if (ImGui::Button("Save edits"))
	{
		FString Message;
		bSaveFailed = !SaveEdits(Rig, MapName, Message);
		SaveStatus = Message;
	}
	ImGui::SetItemTooltip("Write the switched-off set, every hand-set attribute, and the reviewed "
		"index list to tools/out/_lights/<map>.json, keyed by .lights line index. One file per map, "
		"overwritten each save.");

	ImGui::SameLine();
	if (ImGui::Button("Load"))
	{
		FString Message;
		bSaveFailed = !Rig.LoadSurvey(Message);
		SaveStatus = Message;
	}
	ImGui::SetItemTooltip("Re-apply the saved disabled + reviewed sets. The same pass runs by "
		"itself at map load when a save exists (elysium.LightSurvey 0 turns that off), so this is "
		"for re-applying mid-session. Additive — nothing currently off or reviewed is cleared. "
		"Attribute overrides are not restored.");

	ImGui::SameLine();
	ImGui::BeginDisabled(NumOverridden == 0);
	if (ImGui::Button("Revert all"))
	{
		Rig.RevertAllSources();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Drop every per-light attribute override in the map. Leaves the "
		"switched-off set alone.");

	ImGui::SameLine();
	ImGui::BeginDisabled(NumDisabled == 0);
	if (ImGui::Button("Enable all"))
	{
		Rig.EnableAllSources();
	}
	ImGui::EndDisabled();
	ImGui::SetItemTooltip("Switch every hand-disabled light back on. Reviewed marks stay.");

	ImGui::SameLine();
	if (ImGui::Button("Next unreviewed"))
	{
		// The closest light not yet judged — the walking order for finishing a survey.
		FVector Cam = FVector::ZeroVector;
		if (const APlayerController* PC = GetLocalPlayerController())
		{
			if (PC->PlayerCameraManager != nullptr)
			{
				Cam = PC->PlayerCameraManager->GetCameraLocation();
			}
		}
		int32 Best = INDEX_NONE;
		double BestDistSq = TNumericLimits<double>::Max();
		for (int32 I = 0; I < Sources.Num(); ++I)
		{
			const ULightComponent* Light = Sources[I].Light.Get();
			if (Sources[I].bReviewed || Light == nullptr)
			{
				continue;
			}
			const double DistSq = FVector::DistSquared(Cam, Light->GetComponentLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = I;
			}
		}
		if (Best != INDEX_NONE)
		{
			SelectedSource = Best;
			bScrollToSelected = true;
		}
	}
	ImGui::SetItemTooltip("Select the nearest light with no reviewed mark yet.");

	ImGui::SameLine();
	ImGui::TextColored(NumDisabled + NumOverridden > 0 ? ElysiumCogStyle::ColWarn : ElysiumCogStyle::ColDim,
		"%d off · %d overridden", NumDisabled, NumOverridden);
	ImGui::SameLine();
	ImGui::TextColored(NumReviewed == Sources.Num() && Sources.Num() > 0
		? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColDim,
		"· %d/%d reviewed", NumReviewed, Sources.Num());

	if (!SaveStatus.IsEmpty())
	{
		ImGui::TextColored(bSaveFailed ? ElysiumCogStyle::ColError : ElysiumCogStyle::ColOk,
			"%s", COG_TCHAR_TO_CHAR(*SaveStatus));
	}
}

bool FElysiumCogWindow_Lights::SaveEdits(UElysiumLightRig& Rig, const FString& MapName,
	FString& OutMessage)
{
	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig.Sources();

	int32 NumDisabled = 0, NumOverridden = 0, NumReviewed = 0;
	for (const UElysiumLightRig::FLightSource& S : Sources)
	{
		NumDisabled += S.bDisabled ? 1 : 0;
		NumOverridden += S.bOverridden ? 1 : 0;
		NumReviewed += S.bReviewed ? 1 : 0;
	}

	FString Json;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> W =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Json);

	W->WriteObjectStart();
	W->WriteValue(TEXT("map"), MapName);
	W->WriteValue(TEXT("saved_utc"), FDateTime::UtcNow().ToIso8601());

	// The denominator, so "how many were left on" is answerable from the file alone.
	W->WriteObjectStart(TEXT("counts"));
	W->WriteValue(TEXT("sources"), Sources.Num());
	W->WriteValue(TEXT("disabled"), NumDisabled);
	W->WriteValue(TEXT("overridden"), NumOverridden);
	W->WriteValue(TEXT("reviewed"), NumReviewed);
	W->WriteObjectEnd();

	// The calibration the survey was made under: which lights read as redundant depends on how
	// bright the rig was driving all of them, so the judgement is only interpretable with it.
	W->WriteObjectStart(TEXT("calibration"));
	W->WriteValue(TEXT("point_spot_scale"), Rig.PointSpotScale);
	W->WriteValue(TEXT("max_brightness"), Rig.MaxBrightness);
	W->WriteValue(TEXT("falloff_exponent"), Rig.FalloffExponent);
	W->WriteValue(TEXT("radius_scale"), Rig.RadiusScale);
	W->WriteValue(TEXT("specular_scale"), Rig.SpecularScale);
	W->WriteValue(TEXT("sun_lux_scale"), Rig.SunScaleLux);
	W->WriteObjectEnd();

	// Only the edited sources. `index` is the source's `<map>.lights` line, so the untouched
	// complement joins back from the sidecar rather than being carried in every save. `row` is the
	// position in the window's list, which is what the UI numbers rows by — the two differ (the
	// skyambient row is skipped and adoption order is the baked level's), so both are written
	// rather than leaving a reader to guess which one a bare index meant.
	W->WriteArrayStart(TEXT("edits"));
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const UElysiumLightRig::FLightSource& S = Sources[Index];
		if (!S.bDisabled && !S.bOverridden)
		{
			continue;
		}
		const ULightComponent* Light = S.Light.Get();

		W->WriteObjectStart();
		W->WriteValue(TEXT("index"), S.SourceIndex);
		W->WriteValue(TEXT("row"), Index);
		W->WriteValue(TEXT("type"), FString(ANSI_TO_TCHAR(TypeLabel(S.Type))));
		W->WriteValue(TEXT("disabled"), S.bDisabled);
		W->WriteValue(TEXT("overridden"), S.bOverridden);
		W->WriteValue(TEXT("reviewed"), S.bReviewed);
		// Raw sidecar row, so a light is identifiable without the join.
		W->WriteValue(TEXT("mag"), S.Mag);
		W->WriteValue(TEXT("radius_cm"), S.RadiusCm);
		W->WriteValue(TEXT("style"), S.Style);
		W->WriteValue(TEXT("intensity"), S.BaseIntensity);
		if (Light != nullptr)
		{
			const FVector P = Light->GetComponentLocation();
			W->WriteArrayStart(TEXT("pos"));
			W->WriteValue(P.X); W->WriteValue(P.Y); W->WriteValue(P.Z);
			W->WriteArrayEnd();

			const FLinearColor C = Light->GetLightColor();
			W->WriteArrayStart(TEXT("color"));
			W->WriteValue(C.R); W->WriteValue(C.G); W->WriteValue(C.B);
			W->WriteArrayEnd();

			if (const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
			{
				W->WriteValue(TEXT("reach_cm"), Local->AttenuationRadius);
			}
		}
		W->WriteObjectEnd();
	}
	W->WriteArrayEnd();

	// Every reviewed source, disabled ones included, as a flat `.lights`-line index list. This is
	// the coverage record: a source absent here was never judged, so scoring restricts to it
	// instead of inferring coverage from where the disabled lights happen to sit.
	W->WriteArrayStart(TEXT("reviewed"));
	for (const UElysiumLightRig::FLightSource& S : Sources)
	{
		if (S.bReviewed)
		{
			W->WriteValue(S.SourceIndex);
		}
	}
	W->WriteArrayEnd();
	W->WriteObjectEnd();
	W->Close();

	const FString Path = FElysiumContentPaths::LightEdits(MapName);
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::LightEditsDir(), /*Tree*/ true);
	if (!FFileHelper::SaveStringToFile(Json, *Path))
	{
		OutMessage = FString::Printf(TEXT("save failed: %s"), *Path);
		return false;
	}
	OutMessage = FString::Printf(TEXT("saved %d edit%s · %d reviewed to %s"),
		NumDisabled + NumOverridden, NumDisabled + NumOverridden == 1 ? TEXT("") : TEXT("s"),
		NumReviewed, *Path);
	return true;
}

void FElysiumCogWindow_Lights::RenderSelectedSource(UElysiumLightRig& Rig, int32 Index)
{
	const UElysiumLightRig::FLightSource& S = Rig.Sources()[Index];
	ULightComponent* Light = Rig.SourceLight(Index);
	if (Light == nullptr)
	{
		ImGui::TextDisabled("Light %d is gone (map reloaded?).", Index);
		return;
	}

	// Both numbers, because the list row and the sidecar line are not the same and the saved JSON
	// keys on the latter.
	ImGui::Text("#%d · src %d · %s%s", Index, S.SourceIndex, TypeLabel(S.Type),
		S.Style >= 1 ? " · styled" : "");
	ImGui::SetItemTooltip("#row in this list · the .lights line it was built from (what a save "
		"records).");
	ImGui::SameLine();
	if (S.bOverridden)
	{
		ImGui::TextColored(ImVec4(0.94f, 0.76f, 0.43f, 1.0f), "· overridden");
		ImGui::SetItemTooltip("This light is off the global calibration and the lightstyle "
			"animation until it is reverted.");
	}
	else
	{
		ImGui::TextDisabled("· following the calibration");
	}

	// The switch the survey is driven from: it changes no value, so an enabled light comes back
	// exactly as it was and the disabled set stays a clean list of "this one contributes nothing".
	bool bEnabled = !S.bDisabled;
	if (ImGui::Checkbox("Enabled", &bEnabled))
	{
		Rig.SetSourceDisabled(Index, !bEnabled);
	}
	ImGui::SetItemTooltip("Take this one light out of the map without touching its values. Held "
		"against the rig's master toggle and against Isolate, and written to the save file. "
		"Switching a light off also marks it reviewed.");
	ImGui::SameLine();
	bool bReviewed = S.bReviewed;
	if (ImGui::Checkbox("Reviewed", &bReviewed))
	{
		Rig.SetSourceReviewed(Index, bReviewed);
	}
	ImGui::SetItemTooltip("This light was judged — kept or killed — so survey coverage is recorded "
		"rather than inferred. A kill checks it by itself; a keep is checked by hand (or per batch, "
		"below). Written to the save file.");
	ImGui::SameLine();
	if (ImGui::Button("Revert"))
	{
		Rig.RevertSource(Index);
	}
	ImGui::SetItemTooltip("Re-derive this light from its sidecar row and the current calibration "
		"sliders, and hand it back to them. Leaves the Enabled switch alone.");
	ImGui::SameLine();
	ImGui::Checkbox("Isolate", &bIsolate);
	ImGui::SetItemTooltip("Hide every other light, to see what this one alone is doing. Lifted "
		"automatically while this window is closed.");

	// --- The authored batch ----------------------------------------------------------------------
	// The survey's real unit: copy-pasted lights share one authored decision, so the verdict is
	// offered per batch. Recomputed per frame — a few hundred tuple compares.
	{
		const TArray<UElysiumLightRig::FLightSource>& All = Rig.Sources();
		TArray<int32> Batch;
		for (int32 I = 0; I < All.Num(); ++I)
		{
			if (SameBatch(All[I], S))
			{
				Batch.Add(I);
			}
		}
		if (Batch.Num() > 1)
		{
			int32 NumOff = 0, NumRev = 0;
			for (const int32 I : Batch)
			{
				NumOff += All[I].bDisabled ? 1 : 0;
				NumRev += All[I].bReviewed ? 1 : 0;
			}
			ImGui::Text("batch x%d", Batch.Num());
			ImGui::SetItemTooltip("Lights whose raw colour/mag/radius/type/style tuple is identical "
				"to this one's — a copy-pasted authored batch. Verdicts are near-unanimous within a "
				"batch, so it is offered as the unit of the survey.");
			ImGui::SameLine();
			ImGui::TextDisabled("(%d off · %d reviewed)", NumOff, NumRev);
			ImGui::SameLine();
			if (ImGui::Button("Next in batch"))
			{
				const int32 At = Batch.IndexOfByKey(Index);
				SelectedSource = Batch[(At + 1) % Batch.Num()];
				bScrollToSelected = true;
			}
			ImGui::SetItemTooltip("Step the selection through the batch's members, to eyeball each "
				"placement before judging them together.");
			ImGui::SameLine();
			if (ImGui::Button("Batch off"))
			{
				for (const int32 I : Batch)
				{
					Rig.SetSourceDisabled(I, true);
				}
			}
			ImGui::SetItemTooltip("Switch the whole batch off (which also marks it reviewed).");
			ImGui::SameLine();
			if (ImGui::Button("Batch on"))
			{
				for (const int32 I : Batch)
				{
					Rig.SetSourceDisabled(I, false);
				}
			}
			ImGui::SetItemTooltip("Switch the whole batch back on. Reviewed marks stay.");
			ImGui::SameLine();
			if (ImGui::Button("Batch reviewed"))
			{
				for (const int32 I : Batch)
				{
					Rig.SetSourceReviewed(I, true);
				}
			}
			ImGui::SetItemTooltip("Mark the whole batch judged-and-kept in one go.");
		}
		else
		{
			ImGui::TextDisabled("no batch — this tuple is unique on the map");
		}
	}

	const float LabelGutter = ImGui::CalcTextSize("Volumetric scatter").x + ImGui::GetStyle().ItemInnerSpacing.x;
	const float Width = FMath::Max(GetDpiScale() * 110.0f, ImGui::GetContentRegionAvail().x - LabelGutter);

	// Every edit below marks the source overridden through the rig, which is what stops
	// ApplyLiveTuning and the style tick from writing back over it on the next frame.
	float Intensity = S.BaseIntensity;
	ImGui::SetNextItemWidth(Width);
	if (ImGui::DragFloat("Intensity", &Intensity, S.Type == 3 ? 0.05f : 0.01f, 0.0f, 0.0f,
		S.Type == 3 ? "%.2f lux" : "%.3f"))
	{
		Rig.SetSourceIntensity(Index, FMath::Max(Intensity, 0.0f));
	}

	FLinearColor Color = Light->GetLightColor();
	ImGui::SetNextItemWidth(Width);
	if (ImGui::ColorEdit3("Colour", &Color.R))
	{
		Light->SetLightColor(Color);
		Rig.SetSourceOverridden(Index, true);
	}

	if (ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light))
	{
		float Reach = Local->AttenuationRadius;
		ImGui::SetNextItemWidth(Width);
		if (ImGui::DragFloat("Reach (cm)", &Reach, 5.0f, 1.0f, 100000.0f, "%.0f"))
		{
			Local->SetAttenuationRadius(Reach);
			Rig.SetSourceOverridden(Index, true);
		}
	}

	if (UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
	{
		float Falloff = Point->LightFalloffExponent;
		ImGui::SetNextItemWidth(Width);
		if (ImGui::DragFloat("Falloff", &Falloff, 0.02f, 0.2f, 8.0f, "%.2f"))
		{
			Point->SetLightFalloffExponent(Falloff);
			Rig.SetSourceOverridden(Index, true);
		}

		float SourceRadius = Point->SourceRadius;
		ImGui::SetNextItemWidth(Width);
		if (ImGui::DragFloat("Source radius", &SourceRadius, 0.5f, 0.0f, 500.0f, "%.1f"))
		{
			Point->SetSourceRadius(SourceRadius);
			Rig.SetSourceOverridden(Index, true);
		}
		ImGui::SetItemTooltip("Softens the shadow edge and widens the specular highlight. "
			"Not part of the sidecar calibration, so Revert leaves it where you put it.");
	}

	if (USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
	{
		float Inner = Spot->InnerConeAngle;
		float Outer = Spot->OuterConeAngle;
		ImGui::SetNextItemWidth(Width);
		if (ImGui::DragFloat("Inner cone", &Inner, 0.25f, 0.0f, 89.0f, "%.1f deg"))
		{
			Spot->SetInnerConeAngle(FMath::Min(Inner, Outer));
			Rig.SetSourceOverridden(Index, true);
		}
		ImGui::SetNextItemWidth(Width);
		if (ImGui::DragFloat("Outer cone", &Outer, 0.25f, 1.0f, 89.0f, "%.1f deg"))
		{
			Spot->SetOuterConeAngle(Outer);
			Spot->SetInnerConeAngle(FMath::Min(Spot->InnerConeAngle, Outer));
			Rig.SetSourceOverridden(Index, true);
		}
	}

	float Scatter = Light->VolumetricScatteringIntensity;
	ImGui::SetNextItemWidth(Width);
	if (ImGui::DragFloat("Volumetric scatter", &Scatter, 0.02f, 0.0f, 20.0f, "%.2f"))
	{
		Light->SetVolumetricScatteringIntensity(Scatter);
		Rig.SetSourceOverridden(Index, true);
	}
	ImGui::SetItemTooltip("How much this light shows up as a shaft in the height fog. Needs "
		"volumetric fog on, above.");

	float Specular = Light->SpecularScale;
	ImGui::SetNextItemWidth(Width);
	if (ImGui::DragFloat("Specular", &Specular, 0.01f, 0.0f, 1.0f, "%.2f"))
	{
		Light->SpecularScale = Specular;
		Light->MarkRenderStateDirty();
		Rig.SetSourceOverridden(Index, true);
	}

	bool bShadows = Light->CastShadows;
	if (ImGui::Checkbox("Cast shadows", &bShadows))
	{
		Light->SetCastShadows(bShadows);
		Rig.SetSourceOverridden(Index, true);
	}

	// --- Transform -----------------------------------------------------------------------------
	// The 3D gizmo. Scale means nothing to a light, and neither does the orientation of a point
	// light, so both are taken off the handle rather than offered and ignored.
	const FVector Pos = Light->GetComponentLocation();
	ImGui::Text("pos %.0f %.0f %.0f", Pos.X, Pos.Y, Pos.Z);

	if (const APlayerController* PC = GetLocalPlayerController())
	{
		if (PC->PlayerCameraManager != nullptr)
		{
			ImGui::SameLine();
			ImGui::TextDisabled("(%.1f m away)",
				FVector::Dist(Pos, PC->PlayerCameraManager->GetCameraLocation()) / 100.f);
		}

		ECogDebug_GizmoFlags Flags = ECogDebug_GizmoFlags::NoScale;
		if (S.Type == 0 || S.Type == 1)
		{
			Flags |= ECogDebug_GizmoFlags::NoRotation;
		}
		Gizmo.Draw("ElysiumLight", *PC, *Light, Flags);
	}
}

#endif // ENABLE_COG
