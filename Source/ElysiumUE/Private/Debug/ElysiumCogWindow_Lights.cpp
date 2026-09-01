#include "Debug/ElysiumCogWindow_Lights.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "Debug/ElysiumPick.h"
#include "ElysiumMapActor.h"
#include "Visual/ElysiumLightRig.h"
#include "Visual/ElysiumMapVisuals.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/LocalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "CogImguiContext.h"
#include "CogImguiHelper.h"
#include "CogSubsystem.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
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
	// (docs/vtmb/light-attribution.md). Exact float equality is the point: batch members come from
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

	// How far a light origin may sit behind the first visual hit and still count as in view.
	// Fixtures and ceiling volumes bury the source a few tens of centimetres; a wall into the
	// next room is farther than this, so those markers stay hidden.
	constexpr double MarkerEmbedSlackCm = 48.0;

	// The baked world and props block ElysiumPick and nothing else — Visibility / Camera ignore
	// them — so this is the same channel the inspector traces. A miss is a clear line of sight;
	// a hit that lands just short of the origin is the fixture the light lives in.
	bool MarkerVisibleFromCamera(UWorld& World, const FVector& CamPos, const FVector& LightPos,
		const FCollisionQueryParams& Params)
	{
		FHitResult Hit;
		if (!World.LineTraceSingleByChannel(Hit, CamPos, LightPos, ELYSIUM_PICK_CHANNEL, Params))
		{
			return true;
		}
		const double Dist = FVector::Dist(CamPos, LightPos);
		const double HitDist = Hit.Distance > 0.0f
			? static_cast<double>(Hit.Distance)
			: FVector::Dist(CamPos, Hit.ImpactPoint);
		return HitDist + MarkerEmbedSlackCm >= Dist;
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
		"Read-only viewer for the real-time light rig (one Unreal light per VtMB WORLDLIGHTS "
		"source). Toggle rig visibility (also elysium.lights / the pawn's L key), select one light "
		"— click its row, or click its marker in the world — to see its resolved values, and browse "
		"the per-source list.\n\n"
		"This window no longer tunes anything (R4.3): the global calibration lives at Project "
		"Settings -> Elysium -> Lighting (UElysiumLightingSettings), and per-light hand-tunes are "
		"the map's own UElysiumLightCalibration data asset, both edited the ordinary Unreal way. "
		"Copy-pasted lights (an identical colour/mag/radius/type/style tuple) form a batch, shown "
		"as a count on the selected light — the same authored-batches finding this project's hand "
		"survey used, documented in docs/vtmb/light-attribution.md.\n\n"
		"Isolate hides every other light, which is the quickest way to find which fixture a row is; "
		"it is lifted automatically while this window is closed and touches no light's values.");
}

void FElysiumCogWindow_Lights::RenderTick(float DeltaTime)
{
	Super::RenderTick(DeltaTime);

	AElysiumMapActor* Map = GetMapActor();
	UElysiumMapVisuals* Visuals = Map ? Map->GetVisuals() : nullptr;
	UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr;
	if (Rig != ActiveRig.Get())
	{
		// A row number only identifies a source inside one rig. Carrying it across travel can select
		// (and, when Isolate is armed, hide everything except) an unrelated light on the next map.
		// Restore the old rig before dropping its transient UI state in case both map actors overlap
		// for a frame during travel.
		if (UElysiumLightRig* OldRig = ActiveRig.Get(); OldRig != nullptr && bIsolateApplied)
		{
			for (int32 Index = 0; Index < OldRig->Sources().Num(); ++Index)
			{
				if (ULightComponent* Light = OldRig->SourceLight(Index))
				{
					Light->SetVisibility(OldRig->ShouldSourceBeLit(Index));
				}
			}
		}

		ActiveRig = Rig;
		SelectedSource = INDEX_NONE;
		HoveredSource = INDEX_NONE;
		bScrollToSelected = false;
		bIsolate = false;
		bIsolateApplied = false;
		bSelectPending = false;
		bClearPending = false;
	}
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

	if (!bDrawMarkers && !bArmed)
	{
		return;
	}

	FCollisionQueryParams OcclusionParams(FName(TEXT("ElysiumLightMarkerVis")), /*bTraceComplex*/ true);
	OcclusionParams.AddIgnoredActor(PC->GetPawn());

	ImDrawList* DrawList = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const ULightComponent* Light = Sources[Index].Light.Get();
		if (Light == nullptr)
		{
			continue;
		}
		const FVector WorldPos = Light->GetComponentLocation();
		ImVec2 P;
		if (!Proj.Project(WorldPos, P))
		{
			continue;
		}
		if (!MarkerVisibleFromCamera(*World, Proj.CamPos, WorldPos, OcclusionParams))
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
			// Gold ring + crosshair, matching the selected-light readout's "this is the committed
			// selection".
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

	// Noted, not applied: CommitPendingPick resolves it once the frame's other widgets are done.
	HoveredSource = Nearest;
	bSelectPending = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && Nearest != INDEX_NONE;
	bClearPending = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
}

void FElysiumCogWindow_Lights::CommitPendingPick()
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
	bSelectPending = false;
	bClearPending = false;
}

void FElysiumCogWindow_Lights::RenderContent()
{
	Super::RenderContent();

	AElysiumMapActor* Map = GetMapActor();
	UElysiumMapVisuals* Visuals = Map ? Map->GetVisuals() : nullptr;
	UElysiumLightRig* Rig = Visuals ? Visuals->GetLightRig() : nullptr;
	if (Rig == nullptr)
	{
		ImGui::TextDisabled("No light rig on this map.");
		return;
	}

	const TArray<UElysiumLightRig::FLightSource>& Sources = Rig->Sources();

	// Visibility + counts.
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
	ImGui::SetItemTooltip("LMB over the world selects the nearest visible light marker; RMB clears. "
		"Armed only while this window is open and the Cog menu owns the mouse. Lights have no "
		"collision, so this picks by screen distance to the marker, not by a trace. Walls occlude.");
	ImGui::SameLine();
	ImGui::Checkbox("Markers", &bDrawMarkers);
	ImGui::SetItemTooltip("Draw a dot per light the camera can see, in that light's own colour. "
		"Walls occlude. Hollow = hidden.");

	// Above the source list, not below it: this is what the window is opened to look at.
	ImGui::SeparatorText("Selected light");
	if (Sources.IsValidIndex(SelectedSource))
	{
		RenderSelectedSource(*Rig, SelectedSource);
	}
	else
	{
		ImGui::TextDisabled("Click a row below, or a marker in the world.");
	}

	// Per-source list.
	ImGui::SeparatorText("Sources");
	if (Sources.Num() == 0)
	{
		ImGui::TextDisabled("No lights loaded.");
	}
	else if (ImGui::BeginTable("##Lights", 5,
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
				// The row owns more than its selectable: every colour swatch also has the hidden
				// label "##c". Keep the row on the ID stack until every interactive item is drawn.
				ImGui::PushID(Row);

				ImGui::TableNextColumn();
				// The whole row is the hit target (SpanAllColumns), so the index cell doubles as
				// the selectable. The index carries both edit states, so the list shows at a
				// glance which lights are off the global calibration and which are switched off.
				if (ImGui::Selectable("##row", Row == SelectedSource,
					ImGuiSelectableFlags_SpanAllColumns))
				{
					SelectedSource = (Row == SelectedSource) ? INDEX_NONE : Row;
				}
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
					// overridden light's radius is whatever the calibration asset set it to.
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
				ImGui::PopID();
			}
		}
		ImGui::EndTable();
	}
	bScrollToSelected = false;
	ImGui::TextDisabled("* overridden (calibration asset or a stale hand edit) · x switched off");

	// After every other widget, so a click already consumed elsewhere (a table row selectable, for
	// instance) is not also read as a fresh world pick.
	CommitPendingPick();
}

void FElysiumCogWindow_Lights::RenderSelectedSource(UElysiumLightRig& Rig, int32 Index)
{
	const UElysiumLightRig::FLightSource& S = Rig.Sources()[Index];
	const ULightComponent* Light = Rig.SourceLight(Index);
	if (Light == nullptr)
	{
		ImGui::TextDisabled("Light %d is gone (map reloaded?).", Index);
		return;
	}

	// Both numbers, because the list row and the sidecar line are not the same: `SourceIndex` is
	// the `.lights` line a `UElysiumLightCalibration` row would key on.
	ImGui::Text("#%d · src %d · %s%s", Index, S.SourceIndex, TypeLabel(S.Type),
		S.Style >= 1 ? " · styled" : "");
	ImGui::SetItemTooltip("#row in this list · the .lights line it was built from (what a "
		"UElysiumLightCalibration row keys on).");
	ImGui::SameLine();
	if (S.bOverridden)
	{
		ImGui::TextColored(ElysiumCogStyle::ColWarn, "· overridden");
		ImGui::SetItemTooltip("A UElysiumLightCalibration row (or a stale in-session hand edit) has "
			"taken this light off the global calibration and the lightstyle animation.");
	}
	else
	{
		ImGui::TextDisabled("· following the calibration");
	}
	ImGui::SameLine();
	ImGui::Text(S.bDisabled ? "· switched off" : "· enabled");
	ImGui::SameLine();
	ImGui::Checkbox("Isolate", &bIsolate);
	ImGui::SetItemTooltip("Hide every other light, to see what this one alone is doing. Lifted "
		"automatically while this window is closed; touches no light's values.");

	// The authored batch, read-only: how many other sources share this one's exact raw tuple.
	{
		const TArray<UElysiumLightRig::FLightSource>& All = Rig.Sources();
		int32 BatchCount = 0, BatchOff = 0;
		for (const UElysiumLightRig::FLightSource& Other : All)
		{
			if (SameBatch(Other, S))
			{
				++BatchCount;
				BatchOff += Other.bDisabled ? 1 : 0;
			}
		}
		if (BatchCount > 1)
		{
			ImGui::Text("batch x%d (%d off)", BatchCount, BatchOff);
			ImGui::SetItemTooltip("Lights whose raw colour/mag/radius/type/style tuple is identical "
				"to this one's — a copy-pasted authored batch (docs/vtmb/light-attribution.md).");
		}
		else
		{
			ImGui::TextDisabled("no batch — this tuple is unique on the map");
		}
	}

	// Resolved values, read-only.
	ImGui::SeparatorText("Resolved");
	ImGui::Text("colour %.2f %.2f %.2f · mag %.0f", S.Color.R, S.Color.G, S.Color.B, S.Mag);
	if (S.Type == 3)
	{
		ImGui::Text("intensity %.2f lux", S.BaseIntensity);
	}
	else
	{
		const ULocalLightComponent* Local = Cast<ULocalLightComponent>(Light);
		ImGui::Text("intensity %.3f · reach %.0f cm", S.BaseIntensity,
			Local ? Local->AttenuationRadius : 0.f);
	}
	if (const UPointLightComponent* Point = Cast<UPointLightComponent>(Light))
	{
		ImGui::Text("falloff %.2f · source radius %.0f cm", Point->LightFalloffExponent,
			Point->SourceRadius);
	}
	if (const USpotLightComponent* Spot = Cast<USpotLightComponent>(Light))
	{
		ImGui::Text("cone %.1f / %.1f deg (inner/outer)", Spot->InnerConeAngle, Spot->OuterConeAngle);
	}
	if (const UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Light))
	{
		ImGui::Text("sun angle %.3f deg · soft %.3f deg", Sun->LightSourceAngle,
			Sun->LightSourceSoftAngle);
	}
	ImGui::Text("shadows %s · fog shadow %s", Light->CastShadows ? "on" : "off",
		Light->bCastVolumetricShadow ? "on" : "off");

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
	}
}

#endif // ENABLE_COG
