#include "Debug/ElysiumCogWindow_Npc.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumNpcSubsystem.h"
#include "ElysiumPlayerBody.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"

#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

void FElysiumCogWindow_Npc::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_Npc::RenderHelp()
{
	ImGui::Text(
		"glTFRuntime skeletal-path spike (P8 8.2). Pick a VtMB NPC exported to out/npc/<stem>.glb "
		"(mdl_gltf.py: mesh + StudioBone skeleton + one RLE animation, a standard glTF 2.0 file) and "
		"Load it: UElysiumNpcSubsystem runs it through glTFRuntime into a runtime USkeletalMesh + "
		"UAnimSequence and spawns a skeletal-mesh actor in front of the player, playing the clip on "
		"the game's own UElysiumNpcAnimInstance. The table shows what each load produced -- bone count, the "
		"animations present in the glb, the applied clip, load time, spawn location. Per-clip buttons "
		"re-play any animation; Clear destroys the spawned NPCs. Same path the elysium.npc.* verbs drive.\n\n"
		"The Facial tab drives the flex rig on any live rigged body (12.3): 44 flex controllers as "
		"sliders, and beside them the flexdesc weights the 60 RPN rules derive and the morph-target "
		"weights the per-flex ramps derive from those.");
}

// The live `npc_*` / `npc_maker` entities in the loaded map (B3): what stands where and its latch
// state. Distinct from the glTF test harness below — these are the map's own characters, driven by
// the entity substrate, not by the elysium.npc.load spike.
void FElysiumCogWindow_Npc::RenderLiveNpcs()
{
	FElysiumEntityWorld* EW = GetEntityWorld();
	if (EW == nullptr)
	{
		ImGui::TextDisabled("No entity world (load a map).");
		return;
	}

	// Collect npc_*/npc_maker entities off the world's entity list.
	TArray<const FElysiumEntity*> Npcs;
	for (const TUniquePtr<FElysiumEntity>& EntPtr : EW->Entities())
	{
		const FElysiumEntity* E = EntPtr.Get();
		if (E && E->Def && E->Def->Classname.StartsWith(TEXT("npc_")))
		{
			Npcs.Add(E);
		}
	}

	ImGui::Text("%d NPC entities", Npcs.Num());
	if (Npcs.Num() == 0)
	{
		return;
	}

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##LiveNpcs", 3, TableFlags, ImVec2(0, GetDpiScale() * 130.f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Class");
		ImGui::TableSetupColumn("State");
		ImGui::TableHeadersRow();
		for (const FElysiumEntity* E : Npcs)
		{
			// Join the leaf's debug rows into one compact "k=v · k=v" cell — generic over both the
			// character leaf (WillTalk/UseInteresting/In dialog/...) and the maker (Enabled/NPCType/...).
			TArray<TPair<FString, FString>> Rows;
			E->GetDebugState(Rows);
			FString State;
			for (const TPair<FString, FString>& Row : Rows)
			{
				if (!State.IsEmpty()) { State += TEXT("  ·  "); }
				State += FString::Printf(TEXT("%s=%s"), *Row.Key, *Row.Value);
			}

			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextColored(E->IsInert() ? ElysiumCogStyle::ColDim : ElysiumCogStyle::ColName,
				"%s", COG_TCHAR_TO_CHAR(E->TargetName.IsEmpty() ? TEXT("(noname)") : *E->TargetName));
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*E->Def->Classname));
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*State));
		}
		ImGui::EndTable();
	}
}

namespace
{
	const char* StanceName(EElysiumStance Stance)
	{
		switch (Stance)
		{
		case EElysiumStance::Lowering: return "lowering";
		case EElysiumStance::Ducked:   return "ducked";
		case EElysiumStance::Rising:   return "rising";
		default:                       return "standing";
		}
	}

	const char* PhaseName(EElysiumJumpPhase Phase)
	{
		switch (Phase)
		{
		case EElysiumJumpPhase::Ascend:  return "ascend";
		case EElysiumJumpPhase::Descend: return "descend";
		default:                         return "ground";
		}
	}

	// One row of the shared record. Producer-agnostic on purpose: if the player's row and an NPC's
	// row ever need different columns, the contract has already split and this is where it shows.
	void LocomotionRow(const char* Producer, const char* Name, const FElysiumLocomotionSample& S)
	{
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextColored(ElysiumCogStyle::ColName, "%s", Producer);
		ImGui::TableNextColumn(); ImGui::TextUnformatted(Name);
		ImGui::TableNextColumn(); ImGui::Text("%.1f", S.Speed2D());
		ImGui::TableNextColumn(); ImGui::Text("%.1f / %.1f",
			S.LocalVelocity.X, S.LocalVelocity.Y);
		ImGui::TableNextColumn(); ImGui::Text("%.1f", S.FacingYaw);
		ImGui::TableNextColumn();
		// The wish yaw is only a measurement while something is being asked for; at rest it is a
		// placeholder, and the scale is what says so.
		if (S.WishScale > 0.0f) { ImGui::Text("%.1f (x%.2f)", S.MoveYawWish, S.WishScale); }
		else { ImGui::TextDisabled("--"); }
		ImGui::TableNextColumn(); ImGui::Text("%.1f", S.MoveYawVelocity);
		ImGui::TableNextColumn(); ImGui::TextUnformatted(S.bOnGround ? "yes" : "no");
		ImGui::TableNextColumn(); ImGui::TextUnformatted(StanceName(S.Stance));
		ImGui::TableNextColumn(); ImGui::TextUnformatted(PhaseName(S.JumpPhase()));
	}
}

// The body sample, from both producers at once (CCC1). The player's mover published its row at its
// own tick tail; each NPC row is pulled from its motor here. What the view is for is the claim the
// contract makes — that these are the same record — so they are drawn by one function over one
// struct rather than by two panels that happen to look alike.
void FElysiumCogWindow_Npc::RenderLocomotion()
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		ImGui::TextDisabled("No world.");
		return;
	}

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (!ImGui::BeginTable("##Locomotion", 10, TableFlags, ImVec2(0, GetDpiScale() * 200.f)))
	{
		return;
	}

	ImGui::TableSetupScrollFreeze(0, 1);
	ImGui::TableSetupColumn("Producer");
	ImGui::TableSetupColumn("Body");
	ImGui::TableSetupColumn("Speed2D");
	ImGui::TableSetupColumn("Fwd / Side");
	ImGui::TableSetupColumn("Facing");
	ImGui::TableSetupColumn("Yaw wish");
	ImGui::TableSetupColumn("Yaw vel");
	ImGui::TableSetupColumn("Ground");
	ImGui::TableSetupColumn("Stance");
	ImGui::TableSetupColumn("Jump");
	ImGui::TableHeadersRow();

	const APlayerController* PC = World->GetFirstPlayerController();
	if (const IElysiumPlayerBody* Body = PC ? Cast<IElysiumPlayerBody>(PC->GetPawn()) : nullptr)
	{
		LocomotionRow("player", COG_TCHAR_TO_CHAR(*PC->GetPawn()->GetName()),
			Body->GetLocomotionSample());
	}

	for (TActorIterator<AElysiumNpcBody> It(const_cast<UWorld*>(World)); It; ++It)
	{
		const AElysiumNpcBody* Npc = *It;
		if (Npc)
		{
			LocomotionRow("npc", COG_TCHAR_TO_CHAR(*Npc->GetName()), Npc->SampleLocomotion());
		}
	}

	ImGui::EndTable();
}

// The facial flex rig (12.3). Everything below a flex controller is arithmetic, so this tab is the
// whole system in one view: the 44 controllers as sliders, and beside them the 65 flexdesc weights
// the RPN rules derive and the morph-target weights the per-flex ramps derive from those. Sliding
// `blink` moves four flexdescs and four morph targets and closes both pairs of lids.
void FElysiumCogWindow_Npc::RenderFacial()
{
	UElysiumNpcSubsystem* Npc = GetNpcSubsystem();
	if (Npc == nullptr)
	{
		ImGui::TextDisabled("NPC subsystem unavailable.");
		return;
	}

	ImGui::SetNextItemWidth(GetDpiScale() * 160.f);
	FCogWidgets::InputTextWithHint("##FacialFilter", "(every rigged body)", FacialFilter);
	ImGui::SameLine();
	ImGui::Checkbox("Non-zero only", &bFacialNonZeroOnly);

	const TArray<UElysiumNpcAnimInstance*> Bodies = Npc->FacialBodies(FacialFilter);
	if (Bodies.IsEmpty())
	{
		ImGui::TextDisabled("No live body carries a facial flex rig.");
		ImGui::TextDisabled("Load a speaking character from the Preview lab, or stand in a map with rigged NPCs.");
		ImGui::TextDisabled("Animals, dancers, crowd bodies and every player body carry no flex data at all.");
		return;
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset all"))
	{
		for (UElysiumNpcAnimInstance* Inst : Bodies)
		{
			Inst->ResetFlexControllers();
		}
	}

	for (int32 BodyIndex = 0; BodyIndex < Bodies.Num(); ++BodyIndex)
	{
		UElysiumNpcAnimInstance* Inst = Bodies[BodyIndex];
		const FElysiumFacialRig& Rig = *Inst->GetFacialRig();
		const TArray<float>& Values = Inst->GetFlexControllerValues();
		const TArray<float>& Flexes = Inst->GetFlexWeights();
		const TArray<float>& Morphs = Inst->GetMorphWeights();

		ImGui::PushID(BodyIndex);
		const FString Header = FString::Printf(TEXT("%s   %d controllers · %d rules · %d morphs · %d lid(s)"),
			*Rig.Stem, Rig.Controllers.Num(), Rig.Rules.Num(), Rig.Morphs.Num(), Rig.Lids.Num());
		// Only the first body opens by default: a crowded map stands dozens, and 44 sliders each
		// would bury the one being driven.
		if (ImGui::CollapsingHeader(COG_TCHAR_TO_CHAR(*Header),
			BodyIndex == 0 ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None))
		{
			// The controllers arrive grouped by family — eyelid, brow, nose, mouth, phoneme — so a
			// family break is just a change of the type string.
			FString CurrentType;
			for (int32 i = 0; i < Rig.Controllers.Num() && i < Values.Num(); ++i)
			{
				const FElysiumFlexController& Controller = Rig.Controllers[i];
				if (!Controller.Type.Equals(CurrentType))
				{
					CurrentType = Controller.Type;
					ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*CurrentType));
				}
				float Value = Values[i];
				ImGui::PushID(i);
				if (ImGui::SliderFloat(COG_TCHAR_TO_CHAR(*Controller.Name), &Value,
					Controller.Min, Controller.Max))
				{
					Inst->SetFlexControllerByIndex(i, Value);
				}
				ImGui::PopID();
			}

			ImGui::SeparatorText("Derived");
			const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
				ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
			const ImVec2 TableSize(0, GetDpiScale() * 120.f);
			if (ImGui::BeginTable("##Derived", 2, ImGuiTableFlags_SizingStretchSame))
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::TextDisabled("flexdesc weights (the rules)");
				if (ImGui::BeginTable("##Flexes", 2, TableFlags, TableSize))
				{
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Flexdesc");
					ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 56.f);
					ImGui::TableHeadersRow();
					for (int32 i = 0; i < Flexes.Num() && i < Rig.FlexDescs.Num(); ++i)
					{
						if (bFacialNonZeroOnly && FMath::IsNearlyZero(Flexes[i]))
						{
							continue;
						}
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rig.FlexDescs[i]));
						ImGui::TableNextColumn(); ImGui::Text("%.3f", Flexes[i]);
					}
					ImGui::EndTable();
				}

				ImGui::TableNextColumn();
				ImGui::TextDisabled("morph weights (the target ramps)");
				if (ImGui::BeginTable("##Morphs", 2, TableFlags, TableSize))
				{
					ImGui::TableSetupScrollFreeze(0, 1);
					ImGui::TableSetupColumn("Morph target");
					ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 56.f);
					ImGui::TableHeadersRow();
					for (int32 i = 0; i < Morphs.Num() && i < Rig.Morphs.Num(); ++i)
					{
						if (bFacialNonZeroOnly && FMath::IsNearlyZero(Morphs[i]))
						{
							continue;
						}
						ImGui::TableNextRow();
						ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Rig.Morphs[i].Name));
						ImGui::TableNextColumn(); ImGui::Text("%.3f", Morphs[i]);
					}
					ImGui::EndTable();
				}
				ImGui::EndTable();
			}
		}
		ImGui::PopID();
	}
}

void FElysiumCogWindow_Npc::RenderContent()
{
	Super::RenderContent();
	if (!ImGui::BeginTabBar("##NpcViews"))
	{
		return;
	}

	if (ImGui::BeginTabItem("Live map NPCs"))
	{
		RenderLiveNpcs();
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Facial"))
	{
		RenderFacial();
		ImGui::EndTabItem();
	}

	// Ahead of "Preview lab" deliberately: that tab's body returns early out of the whole tab bar on
	// a null subsystem, so anything declared after it can be skipped entirely.
	if (ImGui::BeginTabItem("Locomotion"))
	{
		RenderLocomotion();
		ImGui::EndTabItem();
	}

	if (ImGui::BeginTabItem("Preview lab"))
	{

	UElysiumNpcSubsystem* Npc = GetNpcSubsystem();
	if (Npc == nullptr)
	{
		ImGui::TextDisabled("NPC subsystem unavailable.");
		ImGui::EndTabItem();
		ImGui::EndTabBar();
		return;
	}

	if (bStemsDirty)
	{
		Stems = UElysiumNpcSubsystem::AvailableGlbStems();
		bStemsDirty = false;
		if (PendingStem.IsEmpty() && Stems.Num() > 0)
		{
			PendingStem = Stems[0];
		}
	}

	// --- Load an NPC ------------------------------------------------------------------------
	// Both boxes stretch to the window, minus the Rescan button beside the first, so the two fields
	// line up at the same right edge instead of at ImGui's default 65%-of-window item width.
	ImGui::TextDisabled("Load an exported character beside the player to inspect its runtime mesh and clips.");
	const float RescanWidth = ImGui::CalcTextSize("Rescan").x + ImGui::GetStyle().FramePadding.x * 2.0f
		+ ImGui::GetStyle().ItemSpacing.x;
	const float FieldWidth = FMath::Max(GetDpiScale() * 120.0f,
		ImGui::GetContentRegionAvail().x - RescanWidth);

	ImGui::SetNextItemWidth(FieldWidth);
	FCogWidgets::InputTextWithHint("##Stem", "gangmember_male_2", PendingStem);
	ImGui::SameLine();
	if (ImGui::SmallButton("Rescan"))
	{
		bStemsDirty = true;
	}
	ImGui::SetNextItemWidth(FieldWidth);
	FCogWidgets::InputTextWithHint("##Anim", "(first animation)", PendingAnim);

	ImGui::BeginDisabled(PendingStem.IsEmpty());
	if (ImGui::Button("Load"))
	{
		LastError.Reset();
		if (Npc->LoadTestNpc(PendingStem, PendingAnim, LastError) != nullptr)
		{
			LastError.Reset();
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Clear all"))
	{
		Npc->ClearNpcs();
	}
	if (!LastError.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}

	// --- Available assets (one-click Load) --------------------------------------------------
	ImGui::SeparatorText("Available");
	if (Stems.Num() == 0)
	{
		ImGui::TextDisabled("No .glb under out/npc. Export one:");
		ImGui::TextDisabled("  uv run elysium export bundle npc");
	}
	else
	{
		// EndChild pairs with BeginChild unconditionally: BeginChild returns false when the region is
		// fully clipped (scrolled out of view), and skipping EndChild on that frame trips ImGui's
		// begin/end balance assert.
		ImGui::BeginChild("##Stems", ImVec2(0, GetDpiScale() * 90.f), ImGuiChildFlags_Borders);
		for (int32 i = 0; i < Stems.Num(); ++i)
		{
			ImGui::PushID(i);
			if (ImGui::SmallButton("Load"))
			{
				PendingStem = Stems[i];
				LastError.Reset();
				Npc->LoadTestNpc(Stems[i], PendingAnim, LastError);
			}
			ImGui::SameLine();
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Stems[i]), PendingStem == Stems[i]))
			{
				PendingStem = Stems[i];
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
	}

	// --- Loaded (what glTFRuntime produced) -------------------------------------------------
	const TArray<FElysiumLoadedNpc>& Live = Npc->GetLoaded();
	ImGui::SeparatorText("Loaded");
	ImGui::Text("%d spawned", Live.Num());
	if (Live.Num() == 0)
	{
		ImGui::EndTabItem();
		ImGui::EndTabBar();
		return;
	}

	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##Npcs", 6, TableFlags, ImVec2(0, GetDpiScale() * 130.f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableSetupColumn("Stem");
		ImGui::TableSetupColumn("Bones", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 46.f);
		ImGui::TableSetupColumn("Clip");
		ImGui::TableSetupColumn("Anims", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 44.f);
		ImGui::TableSetupColumn("Load ms", ImGuiTableColumnFlags_WidthFixed, GetDpiScale() * 58.f);
		ImGui::TableSetupColumn("Location");
		ImGui::TableHeadersRow();
		for (const FElysiumLoadedNpc& Record : Live)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Record.Stem));
			ImGui::TableNextColumn(); ImGui::Text("%d", Record.NumBones);
			ImGui::TableNextColumn();
			ImGui::TextUnformatted(Record.AnimName.IsEmpty() ? "(ref pose)" : COG_TCHAR_TO_CHAR(*Record.AnimName));
			ImGui::TableNextColumn(); ImGui::Text("%d", Record.NumAnims);
			ImGui::TableNextColumn(); ImGui::Text("%.1f", Record.LoadMilliseconds);
			ImGui::TableNextColumn(); ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Record.Location.ToCompactString()));
		}
		ImGui::EndTable();
	}

	// Per-clip re-play for the most-recent load: audition any animation the glb carries.
	const FElysiumLoadedNpc& Last = Live.Last();
	if (Last.AnimNames.Num() > 0)
	{
		ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("Clips in %s  (re-load with clip)"), *Last.Stem)));
		// Wrap on the measured width rather than a fixed count per row: clip names run from "idle" to
		// "combat_knife_attack2", so four-per-row overflows for some models and wastes a third of the
		// window for others.
		const float ClipsRight = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
		for (int32 i = 0; i < Last.AnimNames.Num(); ++i)
		{
			if (i > 0)
			{
				const float NextWidth = ImGui::CalcTextSize(COG_TCHAR_TO_CHAR(*Last.AnimNames[i])).x
					+ ImGui::GetStyle().FramePadding.x * 2.0f;
				if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + NextWidth < ClipsRight)
				{
					ImGui::SameLine();
				}
			}
			ImGui::PushID(i);
			if (ImGui::SmallButton(COG_TCHAR_TO_CHAR(*Last.AnimNames[i])))
			{
				PendingAnim = Last.AnimNames[i];
				LastError.Reset();
				Npc->LoadTestNpc(Last.Stem, Last.AnimNames[i], LastError);
			}
			ImGui::PopID();
		}
	}
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

#endif // ENABLE_COG
