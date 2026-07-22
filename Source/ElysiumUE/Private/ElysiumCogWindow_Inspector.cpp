#include "ElysiumCogWindow_Inspector.h"

#if ENABLE_COG

#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDebugSubsystem.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const char* VariantTypeName(EElysiumVariantType T)
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

	// The camera-ray hit under the crosshair (Visibility, complex, ignoring the pawn). This is what
	// makes the window inspect *any* surface — walls, props, brush bodies — not just entities.
	bool TraceCrosshair(UWorld* W, FHitResult& OutHit)
	{
		APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
		if (PC == nullptr)
		{
			return false;
		}
		FVector Loc; FRotator Rot;
		PC->GetPlayerViewPoint(Loc, Rot);
		const FVector End = Loc + Rot.Vector() * 100000.0;
		FCollisionQueryParams Q(FName(TEXT("ElysiumInspectAim")), /*bTraceComplex*/ true, PC->GetPawn());
		return W->LineTraceSingleByChannel(OutHit, Loc, End, ECC_Visibility, Q);
	}

	// A thin crosshair on the imgui foreground, so aiming works without the debug HUD. Cog renders the
	// foreground list every frame a window is visible — including while the F1 menu is closed.
	void DrawReticle()
	{
		const ImGuiViewport* VP = ImGui::GetMainViewport();
		if (VP == nullptr)
		{
			return;
		}
		const ImVec2 C(VP->Pos.x + VP->Size.x * 0.5f, VP->Pos.y + VP->Size.y * 0.5f);
		ImDrawList* DL = ImGui::GetForegroundDrawList();
		const ImU32 Col = IM_COL32(255, 255, 255, 180);
		DL->AddLine(ImVec2(C.x - 8, C.y), ImVec2(C.x + 8, C.y), Col, 1.5f);
		DL->AddLine(ImVec2(C.x, C.y - 8), ImVec2(C.x, C.y + 8), Col, 1.5f);
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
		"Live crosshair inspector. Leave this window open and it keeps updating while you play (Cog "
		"renders open windows even with the F1 menu closed), so whatever you aim at appears here — the "
		"surface under the crosshair (actor, component, mesh, material + textures) and, if it is a "
		"brush/logic entity, its full detail: identity, chain-walked fields, raw .ents keyvalues, and "
		"the 7-field outputs. Fire any input by hand (goes through the real event queue, visible in the "
		"Event Queue window and single-steppable); the In-world debug row toggles the overhead text / "
		"bounds box / fading I/O message overlays and a per-entity breakpoint. Open F1 to click these "
		"controls (that also freezes your aim on the current entity).");
}

void FElysiumCogWindow_Inspector::RenderContent()
{
	Super::RenderContent();

	UWorld* GameWorld = GetWorld();
	UElysiumEntityDebugSubsystem* Dbg = GameWorld ? GameWorld->GetSubsystem<UElysiumEntityDebugSubsystem>() : nullptr;

	// The window is the crosshair: draw a reticle (works while the F1 menu is closed and the game
	// runs, because Cog keeps rendering visible windows) and live-inspect whatever the ray hits.
	DrawReticle();

	// --- Under crosshair: any surface the ray hits — works with or without an .ents substrate. ------
	ImGui::SeparatorText("Under crosshair");
	FElysiumEntityHandle HitEntity;
	FHitResult Hit;
	if (TraceCrosshair(GameWorld, Hit))
	{
		const UPrimitiveComponent* Comp = Hit.GetComponent();
		const AActor* HitActor = Hit.GetActor();
		ImGui::Text("actor      %s", HitActor ? COG_TCHAR_TO_CHAR(*HitActor->GetName()) : "?");
		ImGui::Text("component  %s (%s)",
			Comp ? COG_TCHAR_TO_CHAR(*Comp->GetName()) : "?",
			Comp ? COG_TCHAR_TO_CHAR(*Comp->GetClass()->GetName()) : "?");
		ImGui::Text("distance   %.2f m", Hit.Distance / 100.0f);

		if (const UStaticMeshComponent* SMC = Cast<UStaticMeshComponent>(Comp))
		{
			if (const UStaticMesh* SM = SMC->GetStaticMesh())
			{
				ImGui::Text("mesh       %s", COG_TCHAR_TO_CHAR(*SM->GetName()));
			}
		}

		// Material(s) on the hit surface and each one's bound textures — "what am I looking at".
		if (Comp != nullptr)
		{
			const int32 NumMat = Comp->GetNumMaterials();
			for (int32 m = 0; m < NumMat; ++m)
			{
				UMaterialInterface* Mat = Comp->GetMaterial(m);
				if (Mat == nullptr)
				{
					continue;
				}
				ImGui::Text("material%-2d %s", m, COG_TCHAR_TO_CHAR(*Mat->GetName()));
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
			if (NumMat == 0)
			{
				ImGui::TextDisabled("    (collision-only, no material)");
			}
		}

		if (const UElysiumBrushComponent* Body = Cast<UElysiumBrushComponent>(Comp))
		{
			HitEntity = Body->GetOwningEntity();
		}
	}
	else
	{
		ImGui::TextDisabled("nothing under the crosshair");
	}

	// Entity half: the hit brush entity, else the nearest bodiless logic ent under the aim. Sticky —
	// aiming at a plain surface keeps the last entity selected so you can still work on it.
	FElysiumEntityWorld* World = GetEntityWorld();
	if (World != nullptr)
	{
		const FElysiumEntityHandle Pick = HitEntity.IsSet()
			? HitEntity : (Dbg ? Dbg->PickSelection() : FElysiumEntityHandle::Invalid());
		if (Pick.IsSet())
		{
			SetSelection(Pick);
		}
	}

	ImGui::SeparatorText("Entity");
	if (World == nullptr)
	{
		ImGui::TextDisabled("No .ents substrate on this map (surface inspect only).");
		return;
	}

	FElysiumEntity* Ent = World->Resolve(GetSelection());
	if (Ent == nullptr)
	{
		ImGui::TextDisabled("No entity under the crosshair. Aim at one, or pick from the Entities window.");
		return;
	}

	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const float ValueColumn = GetDpiScale() * 110.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ImGui::TextUnformatted(Label);
		ImGui::SameLine(ValueColumn);
		ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Value));
	};

	// --- Identity --------------------------------------------------------------------------
	ImGui::SeparatorText("Identity");
	Row("Entity", Ent->DebugString());
	Row("Targetname", Ent->TargetName.IsEmpty() ? TEXT("(none)") : Ent->TargetName);
	Row("Class", FString::Printf(TEXT("%s%s"), *Ent->Def->Classname,
		Ent->IsRecordOnly() ? TEXT("  (inert record)") : TEXT("")));
	Row("State", Ent->IsDead() ? TEXT("dead") : Ent->IsHidden() ? TEXT("hidden") : TEXT("live"));
	Row("Body", Ent->Body ? TEXT("brush body") : TEXT("(none)"));
	Row("Next think", Ent->NextThink == ELYSIUM_NEVER_THINK
		? FString(TEXT("never")) : FString::Printf(TEXT("%.2f s"), Ent->NextThink));
	Row("Origin", Ent->Def->Origin.ToString());

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
	if (Ent->Class != nullptr && ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen))
	{
		TArray<FName> FieldNames;
		CollectFields(*Ent->Class, Reg, FieldNames);

		const ImGuiTableFlags Flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
			ImGuiTableFlags_SizingStretchProp;
		if (ImGui::BeginTable("##Fields", 4, Flags))
		{
			ImGui::TableSetupColumn("Field");
			ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 52.0f);
			ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 34.0f);
			ImGui::TableSetupColumn("Value");
			ImGui::TableHeadersRow();

			for (const FName& N : FieldNames)
			{
				const FElysiumFieldAccessor* Acc = Reg.FindField(*Ent->Class, N);
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*N.ToString()));
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Acc ? VariantTypeName(Acc->Type) : "?");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Acc && Acc->bKeyable ? "yes" : "");
				ImGui::TableNextColumn();
				ImGui::TextUnformatted(Acc ? COG_TCHAR_TO_CHAR(*Acc->Get(*Ent).ToString()) : "");
			}
			ImGui::EndTable();
		}
	}

	// --- Raw keyvalues ---------------------------------------------------------------------
	if (ImGui::CollapsingHeader("Keyvalues"))
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
	if (ImGui::CollapsingHeader("Outputs", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const TArray<FElysiumOutputDef>& Outputs = Ent->Def->Outputs;
		if (Outputs.Num() == 0)
		{
			ImGui::TextDisabled("No outputs.");
		}
		else if (ImGui::BeginTable("##Outputs", 7,
			ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
			ImGuiTableFlags_SizingFixedFit))
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

	// --- Fire input (through the real queue, targeting this entity) ------------------------
	ImGui::SeparatorText("Fire input");
	ImGui::SetNextItemWidth(GetDpiScale() * 160.0f);
	FCogWidgets::InputTextWithHint("##Param", "param", PendingParam);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(GetDpiScale() * 90.0f);
	ImGui::InputFloat("delay", &PendingDelay, 0.0f, 0.0f, "%.2f");
	PendingDelay = FMath::Max(0.0f, PendingDelay);

	TArray<FName> InputNames;
	if (Ent->Class != nullptr)
	{
		CollectInputs(*Ent->Class, Reg, InputNames);
	}
	if (InputNames.Num() == 0)
	{
		ImGui::TextDisabled("This class registers no inputs.");
	}
	else
	{
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
