#include "Debug/ElysiumCogWindow_Inspector.h"

#if ENABLE_COG

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDebugSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Debug/ElysiumPick.h"
#include "ElysiumUseIcons.h"

#include "CogImguiContext.h"
#include "CogImguiHelper.h"
#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogSubsystem.h"
#include "CogWidgets.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// File-unique name: a same-named helper (const TCHAR* variant) lives in
	// ElysiumEntityDebugSubsystem.cpp, and both can land in one unity blob.
	const char* InspectorVariantTypeName(EElysiumVariantType T)
	{
		switch (T)
		{
		case EElysiumVariantType::Void:   return "Void";
		case EElysiumVariantType::Bool:   return "Bool";
		case EElysiumVariantType::Int:    return "Int";
		case EElysiumVariantType::Float:  return "Float";
		case EElysiumVariantType::String: return "String";
		case EElysiumVariantType::Vector: return "Vector";
		case EElysiumVariantType::Handle: return "Handle";
		default:                          return "?";
		}
	}

	// Collect a class's chain-resolved input names (derived shadows base), sorted for a stable UI.
	void CollectInputs(const FElysiumClassDesc& Leaf, const FElysiumClassRegistry& Reg, TArray<FName>& Out)
	{
		TSet<FName> Seen;
		for (const FElysiumClassDesc* D = &Leaf; D != nullptr; D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
		{
			for (const TPair<FName, FElysiumInputThunk>& I : D->Inputs)
			{
				Seen.Add(I.Key);
			}
		}
		Out = Seen.Array();
		Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	}

	void CollectFields(const FElysiumClassDesc& Leaf, const FElysiumClassRegistry& Reg, TArray<FName>& Out)
	{
		TSet<FName> Seen;
		for (const FElysiumClassDesc* D = &Leaf; D != nullptr; D = D->BaseName.IsNone() ? nullptr : Reg.Find(D->BaseName))
		{
			for (const TPair<FName, FElysiumFieldAccessor>& F : D->Fields)
			{
				Seen.Add(F.Key);
			}
		}
		Out = Seen.Array();
		Out.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	}

	const char* PickKindName(EElysiumPickKind K)
	{
		switch (K)
		{
		case EElysiumPickKind::Entity:       return "entity";   // the caller distinguishes bodiless
		case EElysiumPickKind::WorldSurface: return "world surface";
		case EElysiumPickKind::PropInstance: return "prop instance";
		default:                             return "nothing";
		}
	}

	// --- selection overlay -------------------------------------------------------------------
	// The highlight is drawn with imgui rather than as scene geometry, which buys three things the
	// project needs: it costs no assets, it reaches things with no renderable mesh at all (trigger
	// volumes), and it keeps drawing if the world is time-scaled to a stop — Cog's render tick is
	// not the game tick.

	// World -> imgui screen, with the near-plane handling the projection helpers do not do.
	struct FProjector
	{
		APlayerController* PC = nullptr;
		ImVec2 Origin = ImVec2(0.0f, 0.0f);   // imgui viewport origin
		FVector CamPos = FVector::ZeroVector;
		FVector CamFwd = FVector::ForwardVector;

		static constexpr double NearEps = 12.0;   // cm in front of the eye

		double Depth(const FVector& P) const { return (P - CamPos) | CamFwd; }

		bool Project(const FVector& P, ImVec2& Out) const
		{
			FVector2D Screen;
			if (!UGameplayStatics::ProjectWorldToScreen(PC, P, Screen, /*bPlayerViewportRelative*/ false))
			{
				return false;
			}
			Out = ImVec2(Origin.x + static_cast<float>(Screen.X), Origin.y + static_cast<float>(Screen.Y));
			return true;
		}

		// Trim a segment to the near plane so an edge running past the camera still draws its
		// visible part instead of vanishing. False when the whole segment is behind.
		bool ClipSegment(FVector& A, FVector& B) const
		{
			const double DA = Depth(A);
			const double DB = Depth(B);
			if (DA < NearEps && DB < NearEps)
			{
				return false;
			}
			if (DA < NearEps)
			{
				A = FMath::Lerp(A, B, (NearEps - DA) / (DB - DA));
			}
			else if (DB < NearEps)
			{
				B = FMath::Lerp(B, A, (NearEps - DB) / (DA - DB));
			}
			return true;
		}
	};

	// One hue per pick kind, so what is outlined is legible without reading the label. Kept inside the
	// window's palette (blood / gold / absinthe) but at full saturation: these are drawn over the
	// world, not over a panel, and they have to survive a dark scene and a bright one.
	void PickColors(EElysiumPickKind Kind, bool bHover, ImU32& OutLine, ImU32& OutFill)
	{
		int32 R = 235, G = 62, B = 60;               // entity: blood
		if (Kind == EElysiumPickKind::WorldSurface) { R = 240; G = 195; B = 110; }  // world: gold
		if (Kind == EElysiumPickKind::PropInstance) { R = 150; G = 220; B = 110; }  // prop: absinthe
		OutLine = IM_COL32(R, G, B, bHover ? 140 : 255);
		OutFill = IM_COL32(R, G, B, bHover ? 22 : 56);
	}

	void DrawPickOverlay(const FProjector& Proj, const FElysiumPickResult& P, bool bHover)
	{
		ImDrawList* DrawList = ImGui::GetBackgroundDrawList(ImGui::GetMainViewport());
		if (DrawList == nullptr)
		{
			return;
		}
		ImU32 LineColor, FillColor;
		PickColors(P.Kind, bHover, LineColor, FillColor);

		// Fill: a triangle with any vertex behind the eye is dropped rather than clipped — the
		// fill is decorative, and the outline (which does clip) carries the shape.
		for (int32 I = 0; I + 2 < P.FillTris.Num(); I += 3)
		{
			ImVec2 A, B, C;
			if (Proj.Depth(P.FillTris[I]) < FProjector::NearEps
				|| Proj.Depth(P.FillTris[I + 1]) < FProjector::NearEps
				|| Proj.Depth(P.FillTris[I + 2]) < FProjector::NearEps)
			{
				continue;
			}
			if (Proj.Project(P.FillTris[I], A) && Proj.Project(P.FillTris[I + 1], B)
				&& Proj.Project(P.FillTris[I + 2], C))
			{
				DrawList->AddTriangleFilled(A, B, C, FillColor);
			}
		}

		const float Thickness = bHover ? 1.5f : 2.5f;
		for (int32 I = 0; I + 1 < P.OutlineSegs.Num(); I += 2)
		{
			FVector S = P.OutlineSegs[I];
			FVector E = P.OutlineSegs[I + 1];
			ImVec2 A, B;
			if (Proj.ClipSegment(S, E) && Proj.Project(S, A) && Proj.Project(E, B))
			{
				DrawList->AddLine(A, B, LineColor, Thickness);
			}
		}

		if (!bHover && !P.Label.IsEmpty() && Proj.Depth(P.Center) >= FProjector::NearEps)
		{
			ImVec2 At;
			if (Proj.Project(P.Center, At))
			{
				FCogWidgets::AddTextWithShadow(DrawList, ImVec2(At.x + 8.0f, At.y - 8.0f),
					LineColor, COG_TCHAR_TO_CHAR(*P.Label));
			}
		}
	}
}

void FElysiumCogWindow_Inspector::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Inspector::RenderHelp()
{
	ImGui::Text(
		"Click-to-select inspector. Open the F1 menu with this window open and left-click anything in "
		"the world: the click resolves to a brush/logic entity, a world surface, or a single prop "
		"instance, and the selection is highlighted in place (translucent fill + outline + label). "
		"Right-click clears it. The game is not paused - it keeps running under the cursor; stop it "
		"yourself from the Time Scale window if you want it still.\n\n"
		"LMB is only taken while this window is open, so working in World Viz or Lights leaves the "
		"click alone, and F1 disarms it. What is already selected stays selected through both - close "
		"the menu and the highlight is still on it.\n\n"
		"The baked level's geometry is real static meshes, so a surface or prop pick is one physics "
		"trace on the dedicated ElysiumPick channel; the hit face resolves back to a material slot. "
		"Brush bodies and gizmo markers are separate sources - see the tooltips.\n\n"
		"Below: the selected surface (component, mesh, material + textures, section/instance/"
		"triangle) and, when the pick is an entity, its full detail - identity, chain-walked fields, "
		"raw .ents keyvalues, and the 7-field outputs. Fire any input by hand (it goes through the "
		"real event queue, so it shows up in the Event Queue window and is single-steppable); the "
		"In-world debug row toggles the overhead text / bounds box / fading I/O message overlays and "
		"a per-entity breakpoint.");
}

void FElysiumCogWindow_Inspector::PreBegin(ImGuiWindowFlags& WindowFlags)
{
	Super::PreBegin(WindowFlags);

	// Cap the window to the viewport. Without this a window sized (or restored from the ImGui ini)
	// taller than the screen simply runs off the bottom edge, with no way to reach the controls at
	// the end of the content. Constrained, the detail region scrolls instead.
	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	if (Viewport == nullptr)
	{
		return;
	}
	const ImVec2 MinSize(GetDpiScale() * 300.0f, GetDpiScale() * 180.0f);
	const ImVec2 MaxSize(Viewport->WorkSize.x * 0.95f, Viewport->WorkSize.y * 0.85f);
	ImGui::SetNextWindowSizeConstraints(MinSize, MaxSize);
}

void FElysiumCogWindow_Inspector::RenderTick(float DeltaTime)
{
	Super::RenderTick(DeltaTime);

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	if (PC == nullptr || Viewport == nullptr)
	{
		return;
	}

	// Drop a pick the previous map took with it: either its component was destroyed, or — for a
	// bodiless entity, which has no component to test — its handle no longer resolves against the
	// current epoch.
	if (GetPick().IsStale())
	{
		ClearPick();
	}
	else if (GetPick().Entity.IsSet() && !GetPick().bHasComponent)
	{
		FElysiumEntityWorld* EW = GetEntityWorld();
		if (EW == nullptr || EW->Resolve(GetPick().Entity) == nullptr)
		{
			ClearPick();
		}
	}

	FProjector Proj;
	Proj.PC = PC;
	Proj.Origin = Viewport->Pos;
	FRotator ViewRot;
	PC->GetPlayerViewPoint(Proj.CamPos, ViewRot);
	Proj.CamFwd = ViewRot.Vector();

	// Armed only while this window is open, Cog owns the mouse, and the cursor is over the world
	// rather than over an imgui window. The window gate is what makes the click-pick an Inspector
	// tool rather than a mode: opening World Viz or Lights to read values leaves LMB alone, and
	// closing the menu with F1 disarms it. The last-in gate keeps a click on a Cog button from
	// also picking the world behind it.
	//
	// The committed selection outlives all three — it is dropped by RMB, by the next pick, or by
	// the map that owned it going away, never by a window or the menu closing — so the highlight
	// below keeps drawing on a closed menu.
	const UCogSubsystem* Cog = GetOwner();
	const bool bArmed = bClickToSelect && GetIsVisible() && Cog != nullptr
		&& Cog->GetContext().GetEnableInput() && !ImGui::GetIO().WantCaptureMouse;

	if (bArmed)
	{
		if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			ClearPick();
		}

		// Do not use ImGui::GetMousePos(): it is invalid over NetImgui (as Cog's own selection
		// window notes), and the context's copy is the one the deproject agrees with.
		const ImVec2 MousePos = Cog->GetContext().GetImguiMousePos();
		FVector RayOrigin, RayDir;
		if (UGameplayStatics::DeprojectScreenToWorld(PC,
			FCogImguiHelper::ToFVector2D(MousePos - Viewport->Pos), RayOrigin, RayDir))
		{
			// The face flood costs an edge map over the whole section, so it runs on commit only;
			// the hover preview shows the single triangle under the cursor.
			const bool bCommit = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			FElysiumPickResult Hit;
			if (bCommit || bHoverPreview)
			{
				// Committing passes the current selection so a second click into a cluster of
				// gizmos steps to the next marker behind it; the hover preview passes nothing, so
				// it always shows the nearest and does not flicker through the stack.
				const FElysiumEntityHandle CycleAfter = bCommit
					? GetPick().Entity : FElysiumEntityHandle::Invalid();
				if (ElysiumPick::Trace(World, RayOrigin, RayDir, Hit, /*bBuildFaceOutline*/ bCommit,
					CycleAfter))
				{
					if (bCommit)
					{
						SetPick(Hit);
						if (Hit.Entity.IsSet())
						{
							SetSelection(Hit.Entity);
						}
					}
					else if (bDrawHighlight)
					{
						DrawPickOverlay(Proj, Hit, /*bHover*/ true);
					}
				}
				else if (bCommit)
				{
					ClearPick();   // clicked empty space
				}
			}
		}
	}

	if (bDrawHighlight && GetPick().IsSet())
	{
		DrawPickOverlay(Proj, GetPick(), /*bHover*/ false);
	}
}

void FElysiumCogWindow_Inspector::RenderPickDetails(const FElysiumPickResult& InPick)
{
	ImGui::SeparatorText("Selection");
	if (!InPick.IsSet())
	{
		// The entity below can outlive a pick, and can exist without one at all — the Entities
		// browser sets it directly. Say which, so an empty pick over a populated Entity section
		// reads as deliberate rather than broken.
		FElysiumEntityWorld* EW = GetEntityWorld();
		const bool bHaveEntity = EW != nullptr && EW->Resolve(GetSelection()) != nullptr;
		ImGui::TextDisabled(bHaveEntity
			? "Nothing picked - the entity below is the last selection (kept until you pick another)."
			: "Nothing picked. Open F1 and left-click something in the world.");
		return;
	}

	const UPrimitiveComponent* Comp = InPick.Component.Get();
	const char* Kind = InPick.Kind == EElysiumPickKind::Entity
		? (InPick.bHasComponent ? "entity (brush body)" : "entity (bodiless)")
		: PickKindName(InPick.Kind);

	// A real value column rather than spaces baked into the format string — the font is proportional,
	// so "kind       " and "hit point  " do not end at the same X.
	const float ValueColumn = GetDpiScale() * 84.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ElysiumCogStyle::LabelValue(Label, COG_TCHAR_TO_CHAR(*Value), ValueColumn);
	};

	Row("kind", FString::Printf(TEXT("%hs%s"), Kind, InPick.bViaGizmo ? TEXT("  via gizmo marker") : TEXT("")));
	Row("what", InPick.Label);
	Row("component", Comp
		? FString::Printf(TEXT("%s (%s)"), *Comp->GetName(), *Comp->GetClass()->GetName())
		: FString(TEXT("(none)")));
	Row("distance", FString::Printf(TEXT("%.2f m"), InPick.Distance / 100.0));
	Row("hit point", InPick.HitPoint.ToCompactString());

	// Both geometry kinds resolve to a material slot of a baked static mesh, so they report the
	// same pair — the slot index and the name the bake gave it (the OBJ group key).
	switch (InPick.Kind)
	{
	case EElysiumPickKind::WorldSurface:
		Row("section", FString::Printf(TEXT("%d  (obj group '%s')"), InPick.Section, *InPick.MaterialName));
		Row("normal", InPick.HitNormal.ToCompactString());
		break;

	case EElysiumPickKind::PropInstance:
		Row("model", InPick.ModelName);
		Row("section", FString::Printf(TEXT("%d  (slot '%s')"), InPick.Section, *InPick.MaterialName));
		Row("normal", InPick.HitNormal.ToCompactString());
		break;

	default:
		break;
	}

	// The material the pick actually landed on (the section's MID, not the component's slot 0)
	// and the textures bound into it — "what am I looking at".
	if (UMaterialInterface* Mat = InPick.Material.Get())
	{
		Row("material", Mat->GetName());
		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Ids;
		Mat->GetAllTextureParameterInfo(Infos, Ids);
		for (const FMaterialParameterInfo& Info : Infos)
		{
			UTexture* Tex = nullptr;
			if (Mat->GetTextureParameterValue(Info, Tex) && Tex != nullptr)
			{
				ImGui::Text("    %s: %s", COG_TCHAR_TO_CHAR(*Info.Name.ToString()),
					COG_TCHAR_TO_CHAR(*Tex->GetName()));
			}
		}
	}
	else if (InPick.Kind == EElysiumPickKind::Entity)
	{
		ImGui::TextDisabled(Comp != nullptr
			? "    (brush body - collision only, nothing rendered)"
			: "    (bodiless entity - the gizmo marker is its only representation)");
	}
}

void FElysiumCogWindow_Inspector::RenderContent()
{
	Super::RenderContent();

	// --- Pick controls + what the last click resolved to --------------------------------------
	ImGui::Checkbox("Click to select", &bClickToSelect);
	ImGui::SetItemTooltip("LMB over the world picks; RMB clears. Armed only while this window is "
		"open and the Cog menu owns the mouse. The selection itself survives closing either.");
	ImGui::SameLine();
	ImGui::Checkbox("Highlight", &bDrawHighlight);
	ImGui::SetItemTooltip("Draw the translucent fill + outline + label on the selection.");
	ImGui::SameLine();
	ImGui::Checkbox("Hover", &bHoverPreview);
	ImGui::SetItemTooltip("Outline whatever the cursor is over before you commit. Costs one CPU "
		"ray-cast per frame while the menu is open.");

	// Everything below the control row lives in a scrolling region, so the window's height is
	// whatever the user dragged it to rather than however tall this entity's data happens to be —
	// a door with 21 keyvalues and 7 outputs would otherwise run off the bottom of the screen.
	if (!ImGui::BeginChild("##Detail", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::EndChild();
		return;
	}

	RenderPickDetails(GetPick());

	// The entity half is sticky: picking a wall or a prop leaves the last entity selected, so a
	// half-finished test harness survives a stray click.
	FElysiumEntityWorld* World = GetEntityWorld();

	ImGui::SeparatorText("Entity");
	FElysiumEntity* Ent = World ? World->Resolve(GetSelection()) : nullptr;
	if (World == nullptr)
	{
		ImGui::TextDisabled("No .ents substrate on this map (surface inspect only).");
	}
	else if (Ent == nullptr)
	{
		ImGui::TextDisabled("No entity selected. Click one in the world (F1 open), or pick from the Entities window.");
	}
	else
	{
		const bool bSelectionChanged = !(LastDetailSelection == Ent->Handle);
		LastDetailSelection = Ent->Handle;
		RenderEntityDetails(*Ent, *World, bSelectionChanged);
	}

	ImGui::EndChild();
}

void FElysiumCogWindow_Inspector::RenderEntityDetails(FElysiumEntity& EntRef, FElysiumEntityWorld& WorldRef,
	bool bSelectionChanged)
{
	FElysiumEntity* Ent = &EntRef;
	FElysiumEntityWorld* World = &WorldRef;
	UWorld* GameWorld = GetWorld();
	UElysiumEntityDebugSubsystem* Dbg = GameWorld ? GameWorld->GetSubsystem<UElysiumEntityDebugSubsystem>() : nullptr;

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	// Wide enough for "locked_icon" / "Reticle now" / "Next think", and overlap-safe for whatever
	// key a leaf class returns from GetDebugState.
	const float ValueColumn = GetDpiScale() * 110.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ElysiumCogStyle::LabelValue(Label, COG_TCHAR_TO_CHAR(*Value), ValueColumn);
	};

	// --- Identity --------------------------------------------------------------------------
	ImGui::SeparatorText("Identity");
	Row("Entity", Ent->DebugString());
	Row("Targetname", Ent->TargetName.IsEmpty() ? TEXT("(none)") : Ent->TargetName);
	Row("Class", FString::Printf(TEXT("%s%s"), *Ent->Def->Classname,
		Ent->IsRecordOnly() ? TEXT("  (inert record)") : TEXT("")));
	// Same colour key as the Entities browser's State column: dead / hidden / live.
	const ImVec4& StateColor = Ent->IsDead() ? ElysiumCogStyle::ColError
		: Ent->IsHidden() ? ElysiumCogStyle::ColWarn : ElysiumCogStyle::ColOk;
	ElysiumCogStyle::LabelValue("State",
		Ent->IsDead() ? "dead" : Ent->IsHidden() ? "hidden" : "live", ValueColumn, &StateColor);
	Row("Body", Ent->Body ? TEXT("brush body") : TEXT("(none)"));
	Row("Next think", Ent->NextThink == ELYSIUM_NEVER_THINK
		? FString(TEXT("never")) : FString::Printf(TEXT("%.2f s"), Ent->NextThink));
	// The live origin (same shape as the pick's "hit point"). An NPC a `scripted_sequence` placed on
	// its mark stands somewhere else than it spawned, so the def's origin is a separate row and only
	// appears once they differ.
	Row("Origin", Ent->Origin.ToCompactString());
	if (!Ent->Origin.Equals(Ent->Def->Origin, 0.01))
	{
		Row("Spawned at", Ent->Def->Origin.ToCompactString());
	}

	// --- +use (P4.4) — the context-icon reticle state. Shown for anything the player can look-and-use
	// or that carries an icon: whether the +use trace is armed on it, the use_icon/locked_icon it
	// names, its live lock, and the icon GetUseIcon() resolves to (= exactly what the HUD draws).
	if (Ent->IsUsable() || Ent->UseIcon != 0 || Ent->LockedIcon != 0)
	{
		ImGui::SeparatorText("+use");
		auto IconLabel = [](int32 N) -> FString
		{
			return N == 0 ? FString(TEXT("(none)"))
				: FString::Printf(TEXT("%d (%s)"), N, ElysiumUseIconName(N));
		};
		Row("Usable", Ent->IsUsable() ? TEXT("yes (+use armed)") : TEXT("no"));
		Row("Locked", Ent->IsUseLocked() ? TEXT("yes") : TEXT("no"));
		Row("use_icon", IconLabel(Ent->UseIcon));
		Row("locked_icon", IconLabel(Ent->LockedIcon));
		Row("Reticle now", IconLabel(Ent->GetUseIcon()));
		const bool bAimed = World->GetAimedUsable() == Ent->Handle;
		Row("Look-cursor", bAimed ? TEXT("ON THIS (aimed)") : TEXT("not aimed"));
	}

	// --- Live state (P4.3) — runtime, non-keyfield state a leaf surfaces (mover toggle-state, current
	// move, resolved links, spawnflag decode). Empty for classes that don't override GetDebugState.
	TArray<TPair<FString, FString>> DebugState;
	Ent->GetDebugState(DebugState);
	if (DebugState.Num() > 0)
	{
		ImGui::SeparatorText("Live state");
		for (const TPair<FString, FString>& KV : DebugState)
		{
			Row(COG_TCHAR_TO_CHAR(*KV.Key), KV.Value);
		}
	}

	// --- In-world debug (drives the same overlays/breakpoint as the ent_* verbs) ------------
	if (Dbg != nullptr)
	{
		ImGui::SeparatorText("In-world debug");
		const int32 Idx = Ent->Handle.Index;

		bool bText = Dbg->IsOverlayOn(Idx, UElysiumEntityDebugSubsystem::Overlay_Text);
		bool bBox  = Dbg->IsOverlayOn(Idx, UElysiumEntityDebugSubsystem::Overlay_BBox);
		bool bMsg  = Dbg->IsOverlayOn(Idx, UElysiumEntityDebugSubsystem::Overlay_Messages);
		if (ImGui::Checkbox("Text", &bText))     { Dbg->SetOverlay(Idx, UElysiumEntityDebugSubsystem::Overlay_Text, bText); }
		ImGui::SameLine();
		if (ImGui::Checkbox("Box", &bBox))       { Dbg->SetOverlay(Idx, UElysiumEntityDebugSubsystem::Overlay_BBox, bBox); }
		ImGui::SameLine();
		if (ImGui::Checkbox("Messages", &bMsg))  { Dbg->SetOverlay(Idx, UElysiumEntityDebugSubsystem::Overlay_Messages, bMsg); }

		bool bBreak = Dbg->IsBreakArmedOn(Ent->Handle);
		if (ImGui::Checkbox("Break when this entity receives an input", &bBreak))
		{
			if (bBreak) { Dbg->ArmBreakOn(Ent->Handle); } else { Dbg->ClearBreak(); }
		}
	}

	// --- Live fields (chain-resolved) ------------------------------------------------------
	// Every record inherits the CBaseEntity chain whether or not its class uses it, so an inert
	// record like info_node (3 keyvalues on disk) shows ~20 fields, all at their default. The
	// values are collected first so the header can carry the set/total count and the table can
	// drop the untouched ones — otherwise the useful rows are buried in zeros.
	struct FFieldRow
	{
		FName Name;
		const FElysiumFieldAccessor* Acc = nullptr;
		FString Value;
		bool bSet = false;
	};
	TArray<FFieldRow> FieldRows;
	int32 FieldsSet = 0;
	if (Ent->Class != nullptr)
	{
		TArray<FName> FieldNames;
		CollectFields(*Ent->Class, Reg, FieldNames);
		FieldRows.Reserve(FieldNames.Num());
		for (const FName& N : FieldNames)
		{
			FFieldRow FR;
			FR.Name = N;
			FR.Acc = Reg.FindField(*Ent->Class, N);
			if (FR.Acc != nullptr)
			{
				const FElysiumVariant V = FR.Acc->Get(*Ent);
				FR.Value = V.ToString();
				// Python truthiness is exactly the "has this been touched" test: 0, empty string,
				// zero vector, unbound handle and Void all read false.
				FR.bSet = V.ToBool();
			}
			FieldsSet += FR.bSet ? 1 : 0;
			FieldRows.Add(MoveTemp(FR));
		}
	}

	// A real class leads with its fields; an inert record leads with its keyvalues, which are the
	// only data it has. Re-seated only when the selection changes, so toggling a section by hand
	// sticks while you work on one entity.
	if (bSelectionChanged)
	{
		ImGui::SetNextItemOpen(!Ent->IsRecordOnly() && FieldRows.Num() > 0, ImGuiCond_Always);
	}
	// The "###Fields" suffix pins the ImGui ID so the changing count in the label does not reset
	// the section's open state.
	const FString FieldsLabel = FString::Printf(TEXT("Fields  (%d of %d set)###Fields"),
		FieldsSet, FieldRows.Num());
	if (FieldRows.Num() > 0 && ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*FieldsLabel)))
	{
		ImGui::Checkbox("Hide unset", &bHideDefaultFields);
		ImGui::SetItemTooltip("Drop fields still at their zero / empty default. Most records inherit "
			"the whole CBaseEntity chain and use almost none of it.");

		const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
			ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##Fields", 4, Flags))
		{
			ImGui::TableSetupColumn("Field");
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 34.0f);
			ImGui::TableSetupColumn("Value");
			ImGui::TableHeadersRow();

			for (const FFieldRow& FR : FieldRows)
			{
				if (bHideDefaultFields && !FR.bSet)
				{
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*FR.Name.ToString()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FR.Acc ? InspectorVariantTypeName(FR.Acc->Type) : "?");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(FR.Acc && FR.Acc->bKeyable ? "yes" : "");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*FR.Value));
			}
			ImGui::EndTable();
		}
		if (bHideDefaultFields && FieldsSet == 0)
		{
			ImGui::TextDisabled("Every inherited field is at its default - this record's data is "
				"in Keyvalues below.");
		}
	}

	// --- Raw keyvalues ---------------------------------------------------------------------
	// The verbatim `.ents` record. For the inert majority (light, info_node, infodecal, env_sprite)
	// this is the whole of what the map author wrote, so it opens by default for them.
	if (bSelectionChanged)
	{
		ImGui::SetNextItemOpen(Ent->IsRecordOnly() || FieldsSet == 0, ImGuiCond_Always);
	}
	const FString KeysLabel = FString::Printf(TEXT("Keyvalues  (%d)###Keyvalues"),
		Ent->Def->Keys.Num());
	if (ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*KeysLabel)))
	{
		const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
			ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##Keys", 2, Flags))
		{
			ImGui::TableSetupColumn("Key");
			ImGui::TableSetupColumn("Value");
			ImGui::TableHeadersRow();

			// Sort keys for a stable listing (TMap iteration order is unspecified).
			TArray<FString> Keys;
			Ent->Def->Keys.GetKeys(Keys);
			Keys.Sort();
			for (const FString& K : Keys)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*K));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Ent->Def->Keys[K]));
			}
			ImGui::EndTable();
		}
	}

	// --- Outputs (7 fields: name, target, input, param, delay, times, python) --------------
	// Most records wire nothing, so the section only opens for the ones that do.
	const TArray<FElysiumOutputDef>& Outputs = Ent->Def->Outputs;
	if (bSelectionChanged)
	{
		ImGui::SetNextItemOpen(Outputs.Num() > 0, ImGuiCond_Always);
	}
	const FString OutputsLabel = FString::Printf(TEXT("Outputs  (%d)###Outputs"), Outputs.Num());
	if (ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*OutputsLabel)))
	{
		if (Outputs.Num() == 0)
		{
			ImGui::TextDisabled("No outputs.");
		}
		else
		{
			// ScrollX makes this a scrolling region, and an outer height of 0 on one of those means
			// "take the host's remaining height" - which, inside the detail child that is itself
			// scrolling, is nothing: the table collapsed to its own horizontal scrollbar. Size it to
			// its rows instead, exact up to the cap and scrolling past it.
			const ImGuiStyle& Style = ImGui::GetStyle();
			const float RowHeight = ImGui::GetTextLineHeight() + Style.CellPadding.y * 2.0f;
			const float TableHeight = RowHeight * (FMath::Min(Outputs.Num(), 12) + 1)   // + header row
				+ Style.ScrollbarSize + Style.CellPadding.y * 2.0f;

			if (ImGui::BeginTable("##Outputs", 7,
				ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
				ImGuiTableFlags_SizingFixedFit, ImVec2(0.0f, TableHeight)))
			{
				ImGui::TableSetupColumn("Output");
				ImGui::TableSetupColumn("Target");
				ImGui::TableSetupColumn("Input");
				ImGui::TableSetupColumn("Param");
				ImGui::TableSetupColumn("Delay");
				ImGui::TableSetupColumn("Times");
				ImGui::TableSetupColumn("Python");
				ImGui::TableHeadersRow();

				for (int32 i = 0; i < Outputs.Num(); ++i)
				{
					const FElysiumOutputDef& O = Outputs[i];
					const int32 Remaining = Ent->OutputTimesRemaining.IsValidIndex(i)
						? Ent->OutputTimesRemaining[i] : O.Times;

					ImGui::TableNextRow();
					ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*O.Name));
					ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*O.Target));
					ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*O.Input));
					ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*O.Param));
					ImGui::TableNextColumn(); ImGui::Text("%.2f", O.Delay);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(O.Times < 0
						? "inf" : COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("%d/%d"), Remaining, O.Times)));
					ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*O.Python));
				}
				ImGui::EndTable();
			}
		}
	}

	// --- Fire input (through the real queue, targeting this entity) ------------------------
	// An inert record answers no inputs at all, which is the common case; skip the param/delay
	// widgets entirely for it rather than showing controls that drive nothing.
	TArray<FName> InputNames;
	if (Ent->Class != nullptr)
	{
		CollectInputs(*Ent->Class, Reg, InputNames);
	}
	ImGui::SeparatorText("Fire input");
	if (InputNames.Num() == 0)
	{
		ImGui::TextDisabled(Ent->IsRecordOnly()
			? "Inert record - no class is registered for this classname, so it answers no inputs."
			: "This class registers no inputs.");
	}
	else
	{
		ImGui::SetNextItemWidth(GetDpiScale() * 160.0f);
		FCogWidgets::InputTextWithHint("##Param", "param", PendingParam);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(GetDpiScale() * 90.0f);
		ImGui::InputFloat("delay", &PendingDelay, 0.0f, 0.0f, "%.2f");
		PendingDelay = FMath::Max(0.0f, PendingDelay);

		// Fire "!self" with Caller = this entity, so the input reaches exactly the inspected
		// record regardless of targetname sharing. Activator is unset (hand-fired, no activator).
		const FElysiumEntityHandle Handle = Ent->Handle;
		const float BtnW = GetDpiScale() * 150.0f;
		// Right edge of the content region in screen space (captured before the first button, so
		// it is the stable window edge); buttons wrap when the next one would cross it.
		const float AvailRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		const float Spacing = ImGui::GetStyle().ItemSpacing.x;
		for (int32 i = 0; i < InputNames.Num(); ++i)
		{
			if (i > 0)
			{
				const float NextRight = ImGui::GetItemRectMax().x + Spacing + BtnW;
				if (NextRight < AvailRight)
				{
					ImGui::SameLine();
				}
			}
			if (ImGui::Button(COG_TCHAR_TO_CHAR(*InputNames[i].ToString()), ImVec2(BtnW, 0)))
			{
				World->EnqueueInput(TEXT("!self"), InputNames[i],
					FElysiumVariant::String(PendingParam), PendingDelay,
					FElysiumEntityHandle::Invalid(), Handle);
			}
		}
	}
}

#endif // ENABLE_COG
