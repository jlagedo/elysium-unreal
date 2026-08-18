#include "Debug/ElysiumCogWindow_GreenRoom.h"

#if ENABLE_COG

#include "Debug/ElysiumCogLocomotionRow.h"
#include "Debug/ElysiumCogStyle.h"
#include "Debug/ElysiumGreenRoomRun.h"
#include "ElysiumContentPaths.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumNpcSubsystem.h"
#include "ElysiumPlayerBody.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "ChaosClothAsset/ClothAsset.h"
#include "ChaosClothAsset/ClothAssetInteractor.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Visual/ElysiumPoseDeviation.h"

#include "Algo/Unique.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Engine/SkinnedAsset.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

#include "Debug/ElysiumClothDebug.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"
#include "imgui_internal.h"          // the dock builder, and the viewport dockspace's host window

namespace
{
	/** One bone's pose at one time, off the sequence's raw data. False when the bone is absent. */
	bool SampleClipBone(const UAnimSequence* Sequence, const FName Bone, const double Time,
		FTransform& OutTransform)
	{
		const USkeleton* Skeleton = Sequence != nullptr ? Sequence->GetSkeleton() : nullptr;
		const int32 BoneIndex = Skeleton != nullptr
			? Skeleton->GetReferenceSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
		if (BoneIndex == INDEX_NONE)
		{
			return false;
		}
		Sequence->GetBoneTransform(OutTransform, FSkeletonPoseBoneIndex(BoneIndex),
			FAnimExtractContext(Time), /*bUseRawData=*/true);
		return true;
	}
}

void FElysiumCogWindow_GreenRoom::Initialize()
{
	Super::Initialize();
	bHasMenu = false;
}

void FElysiumCogWindow_GreenRoom::RenderHelp()
{
	ImGui::Text(
		"The green room, driven by hand. Stand any exported character on the neutral stage, play "
		"any clip it owns, orbit it, and tune the garment simulation while it moves.\n\n"
		"Launch it with `uv run elysium gr [model] [clip]`, or type `elysium.gr` in a running "
		"session. Either way the stage stands in an empty world of its own with no VtMB map loaded, "
		"so the room looks the same however it was entered -- entering from a session leaves that "
		"map, and `elysium.map <name>` goes back.\n\n"
		"Source: the body comes off the /ElysiumBaked mount, which is the only build of a character "
		"-- a stem the character bake has not covered cannot stand at all. The line under Restand "
		"names the asset that is standing and the rig family whose skeleton it was built against, "
		"and Restand is what picks up a re-export.\n\n"
		"Cloth: retail carries renderer-side particle cloth after skeletal skinning, independently "
		"from its hair/body bone-chain solver. The current offline spike does not decode that cloth "
		"payload: it appends a substitute bone lattice to a copy of the "
		"mesh and hangs an AnimDynamics chain down each panel; `npc/cloth/<stem>.json` is the "
		"solver setup it derived from the model's own measurements. These sliders edit the live "
		"simulation, never the file, until Save bakes them into it. Revert goes back to the "
		"sidecar. The baked-character path currently does not select the approximation mesh, so "
		"these controls remain a research surface until that route is reconnected.\n\n"
		"Gravity, the two component scales, the iteration counts, the cone ramp and the collider "
		"radii are re-read every frame, so those answer while you drag. Damping and the body "
		"extents are baked into the rigid bodies when a chain initialises, so moving either "
		"re-seats the chain and the garment drops from the pose again -- that is the settle, not a "
		"glitch.");
}

FElysiumGreenRoomRun* FElysiumCogWindow_GreenRoom::GetLab() const
{
	const UElysiumMapSubsystem* Maps = GetMapSubsystem();
	return Maps ? Maps->GetGreenRoom() : nullptr;
}

UElysiumBodyAnimInstance* FElysiumCogWindow_GreenRoom::GetBodyInstance() const
{
	FElysiumGreenRoomRun* Lab = GetLab();
	USkeletalMeshComponent* Body = Lab ? Lab->LabBody() : nullptr;
	return Body ? Cast<UElysiumBodyAnimInstance>(Body->GetAnimInstance()) : nullptr;
}

UChaosClothComponent* FElysiumCogWindow_GreenRoom::FindGarment() const
{
	FElysiumGreenRoomRun* Lab = GetLab();
	USkeletalMeshComponent* Body = Lab ? Lab->LabBody() : nullptr;
	if (Body == nullptr)
	{
		return nullptr;
	}
	for (USceneComponent* Child : Body->GetAttachChildren())
	{
		if (UChaosClothComponent* Cloth = Cast<UChaosClothComponent>(Child))
		{
			return Cloth;
		}
	}
	return nullptr;
}

UElysiumBipedAnimInstance* FElysiumCogWindow_GreenRoom::GetBipedInstance() const
{
	// The rows that read a clip or a layer rather than a rig. A component that is not one of our
	// hosts at all — a preview driven straight through `PlayAnimation` — answers null and its rows
	// read empty, which is the honest report rather than a hidden failure.
	return Cast<UElysiumBipedAnimInstance>(GetBodyInstance());
}

void FElysiumCogWindow_GreenRoom::OpenLab()
{
	SetIsVisible(true);
	LastError.Reset();
	// The green room is a place, not an overlay: this leaves whatever map is loaded and builds the
	// stage in an empty world, so the room looks the same however it was entered. `elysium.map <name>`
	// is the way back.
	const UWorld* World = GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (UElysiumGameFlowSubsystem* Flow = GI ? GI->GetSubsystem<UElysiumGameFlowSubsystem>() : nullptr)
	{
		Flow->EnterGreenRoom(LastError);
	}
	else
	{
		LastError = TEXT("no game flow subsystem");
	}
}

void FElysiumCogWindow_GreenRoom::DockToSide(const bool bLeft)
{
	PendingDock = bLeft ? EPendingDock::Left : EPendingDock::Right;
}

void FElysiumCogWindow_GreenRoom::PreBegin(ImGuiWindowFlags& WindowFlags)
{
	Super::PreBegin(WindowFlags);
	if (PendingDock == EPendingDock::None)
	{
		return;
	}

	// Cog submits one dockspace over the viewport and keeps the id to itself, so the only way to
	// reach the node is to re-derive it the way ImGui's own default does: `GetID("DockSpace")` inside
	// a host window named after the viewport.
	const ImGuiViewport* Viewport = ImGui::GetMainViewport();
	ANSICHAR HostName[64];
	ImFormatString(HostName, IM_ARRAYSIZE(HostName), "WindowOverViewport_%08X", Viewport->ID);
	ImGuiWindow* Host = ImGui::FindWindowByName(HostName);
	const ImGuiID DockspaceId = Host != nullptr ? Host->GetID("DockSpace") : 0;
	// Nothing submitted yet — stay pending and try again next frame rather than dropping the request.
	if (DockspaceId == 0 || ImGui::DockBuilderGetNode(DockspaceId) == nullptr)
	{
		return;
	}

	ImGuiID SideId = 0;
	ImGui::DockBuilderSplitNode(DockspaceId,
		PendingDock == EPendingDock::Left ? ImGuiDir_Left : ImGuiDir_Right,
		0.34f, &SideId, nullptr);
	// The docked name is the one Begin() is given, which Cog builds as `<title>##<short name>`.
	const FString WindowTitle = GetTitle() + TEXT("##") + GetName();
	ImGui::DockBuilderDockWindow(COG_TCHAR_TO_CHAR(*WindowTitle), SideId);
	ImGui::DockBuilderFinish(DockspaceId);
	PendingDock = EPendingDock::None;
}

void FElysiumCogWindow_GreenRoom::Stand(FElysiumGreenRoomRun& Lab, const FString& Stem,
	const FString& Clip)
{
	LastError.Reset();
	LastNotice.Reset();
	bUserPicked = true;
	if (Lab.LabSetBody(Stem, Clip, LastError))
	{
		PendingStem = Lab.LabStem();
		PendingClip = Lab.LabClip();
		// The new body's rig is a different rig; whatever was being tuned no longer applies.
	}
}

void FElysiumCogWindow_GreenRoom::Pick(FElysiumGreenRoomRun& Lab, int32 Index)
{
	if (!Clips.IsValidIndex(Index))
	{
		return;
	}
	bUserPicked = true;
	ClipCursor = Index;
	const bool bLayerRow = (ClipAdditive.IsValidIndex(Index) && ClipAdditive[Index])
		|| (ClipOverlay.IsValidIndex(Index) && ClipOverlay[Index]);
	if (!bLayerRow)
	{
		PendingClip = Clips[Index];
		// A grid row stands the whole fan when there is a baked blend space for it, and the single
		// resolved cell when there is not. The cell is a fallback rather than an error path: a label
		// with no baked fan must still stand something.
		const bool bGridRow = ClipCells.IsValidIndex(Index) && !ClipCells[Index].IsEmpty();
		LastError.Reset();
		LastNotice.Reset();
		if (bGridRow)
		{
			FString GridError;
			if (Lab.LabSetGrid(Clips[Index], GridError))
			{
				const FElysiumResolvedGrid& Grid = Lab.LabGrid();
				LastNotice = FString::Printf(TEXT("blending %s across %d cells on %s (%s)"),
					*Clips[Index], Grid.Space->GetBlendSamples().Num(), *Grid.AxisName[0],
					*Lab.LabGridArmed());
				return;
			}
			// Only a label that does not name a grid falls through to the resolved cell. Every
			// other refusal — no graph, masked layer, not on the mount — is the answer, not
			// "no blend space".
			if (!GridError.Contains(TEXT("does not name a blend grid")))
			{
				LastError = GridError;
				return;
			}
		}
		Stand(Lab, PendingStem, Clips[Index]);
		if (bGridRow && LastError.IsEmpty())
		{
			LastNotice = FString::Printf(TEXT("%s: playing the resolved cell '%s' — no blend space"),
				*Clips[Index], *ClipCells[Index]);
		}
		return;
	}
	// A layer row leaves PendingClip alone: the standing clip is still the standing clip, and the
	// layer rides over it. Restanding the body would drop the layer, so the two must not share
	// the field that Restand reads.
	LastError.Reset();
	LastNotice.Reset();
	if (!Lab.LabSetLayer(Clips[Index], LayerWeight, LastError))
	{
		return;
	}
	LastNotice = FString::Printf(TEXT("layering %s over %s (%s)"),
		*Clips[Index], *Lab.LabClip(), *Lab.LabLayerArmed());
}

void FElysiumCogWindow_GreenRoom::RenderSource(FElysiumGreenRoomRun& Lab)
{
	// There is one build of a character, so this reports rather than chooses. It still reads the
	// asset that is STANDING rather than a path re-derived from the stem: a body outlives the
	// export that built it, and "the mount has it now" is a different question from "this body
	// came off it".
	const UElysiumBodyAnimInstance* Inst = GetBodyInstance();
	const USkeletalMeshComponent* Comp = Inst != nullptr ? Inst->GetSkelMeshComponent() : nullptr;
	const USkeletalMesh* Mesh = Comp != nullptr ? Comp->GetSkeletalMeshAsset() : nullptr;

	ImGui::BeginDisabled(Lab.LabStem().IsEmpty());
	if (ImGui::Button("Restand##source"))
	{
		LastError.Reset();
		Lab.LabRestand(LastError);
	}
	ImGui::EndDisabled();

	if (Mesh == nullptr)
	{
		if (!Lab.LabStem().IsEmpty() && !ElysiumNpcVisual::IsStemBaked(Lab.LabStem()))
		{
			ImGui::TextColored(ImVec4(0.9f, 0.45f, 0.45f, 1.0f),
				"'%s' is not on the baked mount - run `uv run elysium export characters`",
				COG_TCHAR_TO_CHAR(*Lab.LabStem()));
			return;
		}
		ImGui::TextDisabled("Nothing is standing on the stage.");
		return;
	}

	const FString Path = Mesh->GetPathName();
	const USkeleton* Skeleton = Mesh->GetSkeleton();
	const FString Family = Skeleton != nullptr
		? FElysiumContentPaths::BakedCharacterFamily(Skeleton->GetName()) : FString();
	ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
		"baked: %s (rig family '%s')", COG_TCHAR_TO_CHAR(*FPackageName::GetShortName(Path)),
		COG_TCHAR_TO_CHAR(*Family));
}

void FElysiumCogWindow_GreenRoom::ScanRootMotion(FElysiumGreenRoomRun& Lab)
{
	ClipRootMotion.Reset();
	ScannedStem.Reset();

	const UGameInstance* GI = GetMapSubsystem() ? GetMapSubsystem()->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
	USkeletalMeshComponent* Body = Lab.LabBody();
	USkeletalMesh* Mesh = Body != nullptr ? Body->GetSkeletalMeshAsset() : nullptr;
	if (Anims == nullptr || Mesh == nullptr)
	{
		LastError = TEXT("stand a body first - a clip resolves against the mesh it plays on");
		return;
	}

	static const FName RootBone(TEXT("Bip01"));
	ClipRootMotion.SetNumZeroed(Clips.Num());
	int32 Moving = 0;
	for (int32 Index = 0; Index < Clips.Num(); ++Index)
	{
		FString Error;
		// A baked sequence is addressed by owner and animation name off the standing mesh's own rig
		// family.
		const UAnimSequence* Sequence = Anims->ResolveClip(PendingStem, Clips[Index], Mesh, Error);
		FTransform Start;
		FTransform End;
		const float Length = Sequence != nullptr ? Sequence->GetPlayLength() : 0.0f;
		if (Sequence == nullptr || !SampleClipBone(Sequence, RootBone, 0.0, Start)
			|| !SampleClipBone(Sequence, RootBone, Length, End))
		{
			continue;
		}
		const bool bMoves = FVector::Dist(Start.GetTranslation(), End.GetTranslation()) > 0.5
			|| FMath::RadiansToDegrees(Start.GetRotation().AngularDistance(End.GetRotation())) > 0.5;
		ClipRootMotion[Index] = bMoves ? 1 : 2;
		Moving += bMoves ? 1 : 0;
	}
	ScannedStem = PendingStem;
	LastNotice = FString::Printf(TEXT("%d of %d clips carry the root"), Moving, Clips.Num());
}

void FElysiumCogWindow_GreenRoom::RenderModel(FElysiumGreenRoomRun& Lab)
{
	RenderSource(Lab);
	ImGui::Separator();

	if (bStemsDirty)
	{
		Stems = UElysiumNpcSubsystem::AvailableGlbStems();
		StemHasCloth.Reset();
		StemHasCloth.Reserve(Stems.Num());
		for (const FString& Stem : Stems)
		{
			StemHasCloth.Add(IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGarment(Stem)));
		}
		bStemsDirty = false;
	}

	// Until something is picked here, the fields answer for the body actually on the stage. Mirrored
	// every frame rather than seeded once, because `gr <model> <clip>` stands its body up from the
	// lab's own tick, which can land after this window has already drawn a frame — and a field
	// naming the first row of a directory listing while someone else is standing is a field that
	// lies about the thing this window exists to control.
	if (!bUserPicked)
	{
		if (!Lab.LabStem().IsEmpty())
		{
			PendingStem = Lab.LabStem();
			PendingClip = Lab.LabClip();
		}
		else if (PendingStem.IsEmpty() && !Stems.IsEmpty())
		{
			PendingStem = Stems[0];
		}
	}

	const float ButtonWidth = ImGui::CalcTextSize("Rescan").x
		+ ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
	const float FieldWidth = FMath::Max(GetDpiScale() * 120.0f,
		ImGui::GetContentRegionAvail().x - ButtonWidth);

	ImGui::SetNextItemWidth(FieldWidth);
	bUserPicked |= FCogWidgets::InputTextWithHint("##Stem", "tremere_female_armor_0", PendingStem);
	ImGui::SameLine();
	if (ImGui::SmallButton("Rescan"))
	{
		bStemsDirty = true;
	}
	ImGui::SetNextItemWidth(FieldWidth);
	bUserPicked |= FCogWidgets::InputTextWithHint("##Clip", "(the model's own idle)", PendingClip);

	ImGui::BeginDisabled(PendingStem.IsEmpty());
	if (ImGui::Button("Stand it up"))
	{
		Stand(Lab, PendingStem, PendingClip);
	}
	ImGui::EndDisabled();
	if (!LastError.IsEmpty())
	{
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}

	// --- models -------------------------------------------------------------------------------
	ImGui::SeparatorText("Models");
	ImGui::SetNextItemWidth(FieldWidth);
	FCogWidgets::InputTextWithHint("##StemFilter", "(filter)", StemFilter);
	if (Stems.IsEmpty())
	{
		ImGui::TextDisabled("No .glb under npc/. Export one: uv run elysium export bundle npc");
	}
	else
	{
		ImGui::BeginChild("##Stems", ImVec2(0, GetDpiScale() * 140.f), ImGuiChildFlags_Borders);
		for (int32 Index = 0; Index < Stems.Num(); ++Index)
		{
			if (!StemFilter.IsEmpty() && !Stems[Index].Contains(StemFilter))
			{
				continue;
			}
			ImGui::PushID(Index);
			// A model the spike built a garment for is the reason this window exists; the rest are
			// here because a stage that only accepts two models is not a green room.
			const bool bCloth = StemHasCloth.IsValidIndex(Index) && StemHasCloth[Index];
			if (bCloth)
			{
				ImGui::TextColored(ElysiumCogStyle::ColName, "~");
			}
			else
			{
				ImGui::TextDisabled(" ");
			}
			ImGui::SameLine();
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Stems[Index]), PendingStem == Stems[Index]))
			{
				bUserPicked = true;
				PendingStem = Stems[Index];
				PendingClip.Reset();
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				Stand(Lab, Stems[Index], FString());
			}
			ImGui::PopID();
		}
		ImGui::EndChild();
		ImGui::TextDisabled("~ = the cloth spike built a simulated garment.  Double-click to stand.");
	}

}

void FElysiumCogWindow_GreenRoom::RenderClips(FElysiumGreenRoomRun& Lab)
{
	// --- clips --------------------------------------------------------------------------------
	if (PendingStem.IsEmpty())
	{
		ImGui::TextDisabled("Select a model first on the Model tab.");
		return;
	}
	if (ClipsStem != PendingStem)
	{
		Clips.Reset();
		ClipCells.Reset();
		ClipAdditive.Reset();
		ClipOverlay.Reset();
		ClipOwner.Reset();
		ClipOwners.Reset();
		ClipRootMotion.Reset();
		ScannedStem.Reset();
		OwnerFilter.Reset();
		ClipCursor = INDEX_NONE;
		ClipsStem = PendingStem;
		const UGameInstance* GI = GetMapSubsystem() ? GetMapSubsystem()->GetGameInstance() : nullptr;
		UElysiumAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
		if (const FElysiumNpcClipSet* Set = Anims ? Anims->GetClipSet(PendingStem) : nullptr)
		{
			Set->Clips.GetKeys(Clips);
			Clips.Sort();
			// Which of these labels is really a fan of animations. A grid collapsed to a cell is the
			// one thing in this window that changes what plays without changing what was asked for,
			// so it is named on screen rather than left to the log.
			ClipCells.Reserve(Clips.Num());
			ClipAdditive.Reserve(Clips.Num());
			ClipOverlay.Reserve(Clips.Num());
			ClipOwner.Reserve(Clips.Num());
			for (const FString& Label : Clips)
			{
				const FString Cell = Anims->ResolveClipAnimName(PendingStem, Label);
				ClipCells.Add(Cell.Equals(Label, ESearchCase::IgnoreCase) ? FString() : Cell);
				const FElysiumNpcClip* Clip = Set->Find(Label);
				const bool bAdditiveClip = Clip != nullptr && Clip->IsAdditive();
				ClipAdditive.Add(bAdditiveClip);
				ClipOverlay.Add(!bAdditiveClip && Label.EndsWith(TEXT("_layer")));
				ClipOwner.Add(Clip != nullptr && !Clip->IsOwnedBy(PendingStem)
					? Clip->Owner : FString());
			}
			ClipOwners = ClipOwner;
			ClipOwners.Sort();
			ClipOwners.SetNum(Algo::Unique(ClipOwners));
			// The body's own file sorts as the empty string; name it, so the list reads as a set of
			// sources rather than as one blank row among thirty banks.
			if (!ClipOwners.IsEmpty() && ClipOwners[0].IsEmpty())
			{
				ClipOwners[0] = PendingStem;
			}
		}
	}

	ImGui::SeparatorText(COG_TCHAR_TO_CHAR(
		*FString::Printf(TEXT("Clips in %s (%d)"), *PendingStem, Clips.Num())));
	const float ButtonWidth = ImGui::CalcTextSize("Rescan").x
		+ ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemSpacing.x;
	const float FieldWidth = FMath::Max(GetDpiScale() * 120.0f,
		ImGui::GetContentRegionAvail().x - ButtonWidth);

	ImGui::SetNextItemWidth(FieldWidth);
	FCogWidgets::InputTextWithHint("##ClipFilter", "(filter - try 'walk' or 'Stance')", ClipFilter);
	if (Clips.IsEmpty())
	{
		ImGui::TextDisabled("No clip vocabulary - npc/clips/%s.json is missing.",
			COG_TCHAR_TO_CHAR(*PendingStem));
		return;
	}
	// --- filters ------------------------------------------------------------------------------
	// A substring box alone is not a filter over 1,500 clips. Source, kind and root motion are the
	// three axes that actually partition a vocabulary, and they are ANDed with the text.
	const float ThirdWidth = (ImGui::GetContentRegionAvail().x
		- ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

	ImGui::SetNextItemWidth(ThirdWidth);
	if (ImGui::BeginCombo("##Owner",
		OwnerFilter.IsEmpty() ? "all sources" : COG_TCHAR_TO_CHAR(*OwnerFilter)))
	{
		if (ImGui::Selectable("all sources", OwnerFilter.IsEmpty()))
		{
			OwnerFilter.Reset();
		}
		for (const FString& Source : ClipOwners)
		{
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Source), OwnerFilter == Source))
			{
				OwnerFilter = Source;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ThirdWidth);
	ImGui::Combo("##Kind", &KindFilter, "all kinds\0poses only\0layers only\0");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(ThirdWidth);
	ImGui::BeginDisabled(ScannedStem != PendingStem);
	ImGui::Combo("##Motion", &MotionFilter, "all motion\0carries the root\0root held\0");
	ImGui::EndDisabled();

	if (ScannedStem != PendingStem)
	{
		MotionFilter = 0;
		if (ImGui::SmallButton("Scan root motion"))
		{
			ScanRootMotion(Lab);
		}
		ImGui::SameLine();
		ImGui::TextDisabled("resolves every clip once - takes a moment on a big vocabulary");
	}

	// The rows the filters leave, in order. Built before the list is drawn because the arrow keys
	// have to step through what is on screen -- stepping through Clips itself would jump over
	// filtered-out rows and stand something the list is not showing.
	TArray<int32> Visible;
	Visible.Reserve(Clips.Num());
	for (int32 Index = 0; Index < Clips.Num(); ++Index)
	{
		if (!ClipFilter.IsEmpty() && !Clips[Index].Contains(ClipFilter))
		{
			continue;
		}
		if (!OwnerFilter.IsEmpty())
		{
			const FString& Source = ClipOwner.IsValidIndex(Index) ? ClipOwner[Index] : FString();
			// The body's own clips carry an empty owner and are listed under the model's name.
			const bool bMine = Source.IsEmpty() && OwnerFilter == PendingStem;
			if (!bMine && Source != OwnerFilter)
			{
				continue;
			}
		}
		if (KindFilter != 0)
		{
			// Both kinds of autolayer sit on the same side of this filter: what it separates is
			// "picking this stands the body" from "picking this composes over the body".
			const bool bLayer = (ClipAdditive.IsValidIndex(Index) && ClipAdditive[Index])
				|| (ClipOverlay.IsValidIndex(Index) && ClipOverlay[Index]);
			if (bLayer != (KindFilter == 2))
			{
				continue;
			}
		}
		if (MotionFilter != 0)
		{
			const uint8 Motion = ClipRootMotion.IsValidIndex(Index) ? ClipRootMotion[Index] : 0;
			if (Motion != (MotionFilter == 1 ? 1 : 2))
			{
				continue;
			}
		}
		Visible.Add(Index);
	}
	if (!Visible.Contains(ClipCursor))
	{
		// The cursor follows the standing clip, and falls to the first visible row when the filter
		// moves out from under it.
		const int32 Standing = Clips.IndexOfByKey(PendingClip);
		ClipCursor = Visible.Contains(Standing) ? Standing
			: (Visible.IsEmpty() ? INDEX_NONE : Visible[0]);
	}

	// Up/Down walk the list and stand what they land on. Guarded on the window having focus and on
	// no text field being active, so typing in a filter box still moves the caret.
	bClipCursorMoved = false;
	if (!Visible.IsEmpty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)
		&& !ImGui::IsAnyItemActive())
	{
		const int32 At = FMath::Max(Visible.IndexOfByKey(ClipCursor), 0);
		int32 Step = 0;
		if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, /*bRepeat=*/true))
		{
			Step = 1;
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, /*bRepeat=*/true))
		{
			Step = -1;
		}
		if (Step != 0)
		{
			bClipCursorMoved = true;
			Pick(Lab, Visible[FMath::Clamp(At + Step, 0, Visible.Num() - 1)]);
		}
	}

	ImGui::BeginChild("##Clips", ImVec2(0, GetDpiScale() * 140.f), ImGuiChildFlags_Borders);
	for (const int32 Index : Visible)
	{
		const bool bGrid = ClipCells.IsValidIndex(Index) && !ClipCells[Index].IsEmpty();
		const bool bAdditive = ClipAdditive.IsValidIndex(Index) && ClipAdditive[Index];
		const bool bOverlay = ClipOverlay.IsValidIndex(Index) && ClipOverlay[Index];
		const FString Bank = ClipOwner.IsValidIndex(Index) ? ClipOwner[Index] : FString();
		ImGui::PushID(Index);
		FString Row = bGrid
			? FString::Printf(TEXT("%s  -> %s"), *Clips[Index], *ClipCells[Index])
			: Clips[Index];
		if (bAdditive || bOverlay)
		{
			// Named on the row, not hidden from the list: picking one composes it over the body
			// rather than standing it, and saying which rows do that is what stops the layer from
			// reading as a clip that did nothing. The two kinds are named apart because they fail
			// apart — a delta over the wrong base is anatomical nonsense, an overlay under no mask
			// is a body with no legs.
			Row += bAdditive ? TEXT("   [additive layer]") : TEXT("   [overlay layer]");
			ImGui::PushStyleColor(ImGuiCol_Text, ElysiumCogStyle::ColName);
		}
		if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Row),
			ClipCursor == Index || PendingClip == Clips[Index]))
		{
			Pick(Lab, Index);
		}
		if (bAdditive || bOverlay)
		{
			ImGui::PopStyleColor();
		}
		if (!Bank.IsEmpty())
		{
			// Right-aligned so the labels stay readable down the left edge; a body's own clips are
			// left blank rather than repeating the model's name on hundreds of rows.
			const float Width = ImGui::CalcTextSize(COG_TCHAR_TO_CHAR(*Bank)).x;
			ImGui::SameLine(ImGui::GetContentRegionAvail().x - Width);
			ImGui::TextDisabled("%s", COG_TCHAR_TO_CHAR(*Bank));
		}
		if (bClipCursorMoved && ClipCursor == Index)
		{
			ImGui::SetScrollHereY(0.5f);
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::TextDisabled("Up/Down walk the list and stand each clip. Right column = the bank that");
	ImGui::TextDisabled("owns it; blank means this model's own file.");
	if (!LastNotice.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(*LastNotice));
	}
	ImGui::TextDisabled("-> = a blend grid, showing the cell the pose parameters select.");
	ImGui::TextDisabled("[additive layer] = a delta, not a pose. Picking one composes it OVER the");
	ImGui::TextDisabled("standing clip at the weight below; the standing clip does not change.");
	ImGui::TextDisabled("[overlay layer] = a partial-body pose. It REPLACES the bones its mask owns");
	ImGui::TextDisabled("(the spine up and both arms, for an aim layer) and leaves the legs alone.");

	// The grid controls, drawn only while a grid is standing — unlike the layer weight there is
	// nothing to pre-set, because the axes and their ranges are the grid's own and are not known
	// until one is picked.
	if (const FElysiumResolvedGrid& Grid = Lab.LabGrid(); Grid.IsValid())
	{
		ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*FString::Printf(
			TEXT("Blend grid: %s (%d cells)"), *Grid.Label, Grid.Space->GetBlendSamples().Num())));
		bool bMoved = false;
		float At[2] = { Lab.LabGridAxis(0), Lab.LabGridAxis(1) };
		for (int32 Axis = 0; Axis < Grid.Axes; ++Axis)
		{
			ImGui::PushID(Axis);
			ImGui::SetNextItemWidth(GetDpiScale() * 220.f);
			bMoved |= ImGui::SliderFloat(COG_TCHAR_TO_CHAR(*Grid.AxisName[Axis]), &At[Axis],
				Grid.AxisMin[Axis], Grid.AxisMax[Axis], "%.0f deg");
			ImGui::PopID();
		}
		if (bMoved)
		{
			// Every frame the slider is dragged. Moving the sample point does not restart the
			// animations underneath it, which is the whole difference from the per-cell clip pick
			// this replaces — that one had to swap the sequence to change direction.
			Lab.LabSetGridPosition(At[0], At[1]);
		}
		if (ImGui::Button("Back to one clip"))
		{
			Lab.LabClearGrid();
			LastNotice.Reset();
		}
		ImGui::TextDisabled("0 deg is straight ahead: the fan runs -180..180 and its END CELLS SHARE");
		ImGui::TextDisabled("one clip, which is how VtMB authors a wrapping axis. So `walk` itself is");
		ImGui::TextDisabled("the BACKWARD walk, and the middle of this slider is the forward one.");
	}

}

void FElysiumCogWindow_GreenRoom::RenderLayers(FElysiumGreenRoomRun& Lab)
{
	if (PendingStem.IsEmpty())
	{
		ImGui::TextDisabled("Select a model first on the Model tab.");
		return;
	}

	// CCC10's acceptance, one click per claim. Ahead of the hand controls because this is the door
	// the owner should come through: a body carries ~1,500 clips and each claim needs one specific
	// layer, so a case that has to be hunted for is a case that does not get run.
	ImGui::SeparatorText("Layer test cases (CCC10)");
	for (int32 Case = 0; Case < FElysiumGreenRoomRun::LayerCaseCount(); ++Case)
	{
		if (Case > 0)
		{
			ImGui::SameLine();
		}
		if (ImGui::Button(COG_TCHAR_TO_CHAR(FElysiumGreenRoomRun::LayerCaseName(Case))))
		{
			LastError.Reset();
			LastNotice.Reset();
			FString Summary;
			if (Lab.LabLoadLayerCase(Case, Summary, LastError))
			{
				LastNotice = Summary;
				// The sliders follow what the case set, or the next drag would snap the aim back to
				// wherever they happened to be sitting.
				LayerAimYaw = Lab.LabLayerAimYaw();
				LayerAimPitch = Lab.LabLayerAimPitch();
				LayerWeight = 1.f;
			}
		}
	}
	if (!LastNotice.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(*LastNotice));
	}

	RenderAutoLayers(Lab);

	// The layer controls, drawn whether or not one is running: the weight has to be settable before
	// the pick, because a layer picked at 1.0 and then dialled back reads as a different clip.
	ImGui::SetNextItemWidth(GetDpiScale() * 160.f);
	if (ImGui::SliderFloat("Layer weight", &LayerWeight, 0.f, 1.f, "%.2f")
		&& !Lab.LabLayer().IsEmpty())
	{
		// Re-asking for the running layer only re-weights it, so dragging does not restart the
		// delta under the slider.
		FString Ignored;
		Lab.LabSetLayer(Lab.LabLayer(), LayerWeight, Ignored);
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(Lab.LabLayer().IsEmpty());
	if (ImGui::Button("Clear layers"))
	{
		Lab.LabClearLayers();
		LastNotice.Reset();
	}
	ImGui::EndDisabled();

	// Read once: the proxy accessor behind it blocks on any in-flight parallel evaluation, which is
	// not a thing to do twice a frame to draw one label.
	const UElysiumBipedAnimInstance* LayerInst = GetBipedInstance();
	const int32 Composing = LayerInst != nullptr ? LayerInst->GetActiveLayers() : 0;
	if (Composing > 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColName, "%d composing", Composing);
	}

	// The aim pair, beside the weight and for the same reason: **drawn whether or not a host declares
	// a layer**. It cannot live with the declared-autolayer list above, which returns early for a host
	// that declares none — and a walking body is exactly such a host, so the one control the aim-grid
	// acceptance needs would vanish precisely when the layer is being judged over a moving host.
	//
	// The range is the grids' own: every shipped aim grid is 3x3 spanning -45..45 on both axes, so a
	// wider slider would spend half its travel clamped against a cell that does not exist.
	const bool bFollowsLook = Lab.LabAimFollowsLook();
	ImGui::SetNextItemWidth(GetDpiScale() * 160.f);
	if (ImGui::SliderFloat("aim_yaw", &LayerAimYaw, -45.f, 45.f, "%.0f deg"))
	{
		Lab.LabSetLayerAim(LayerAimYaw, LayerAimPitch);
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("level"))
	{
		LayerAimYaw = 0.f;
		LayerAimPitch = 0.f;
		Lab.LabSetLayerAim(LayerAimYaw, LayerAimPitch);
	}

	// While the view drives the pitch, the slider REPORTS rather than sets it: two writers on one
	// value is how a control comes to disagree with the body it is supposed to describe.
	if (bFollowsLook)
	{
		LayerAimPitch = Lab.LabLayerAimPitch();
	}
	ImGui::BeginDisabled(bFollowsLook);
	ImGui::SetNextItemWidth(GetDpiScale() * 160.f);
	if (ImGui::SliderFloat("aim_pitch", &LayerAimPitch, -45.f, 45.f, "%.0f deg"))
	{
		Lab.LabSetLayerAim(LayerAimYaw, LayerAimPitch);
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	bool bFollow = bFollowsLook;
	if (ImGui::Checkbox("follows look", &bFollow))
	{
		Lab.LabSetAimFollowsLook(bFollow);
	}

	// **Retail's player selector writes `aim_yaw` as a literal 0**, taking pitch from a separate
	// field (`docs/vtmb/animation_and_movers.md`), so the yaw axis is the NPC-side parameter and
	// pitch is the half a player can steer by looking. Saying so keeps a swept yaw here from being
	// read as evidence that the player's own grid sweeps.
	ImGui::TextDisabled("Look up/down drives aim_pitch (retail's own shape). aim_yaw is NPC-side:");
	ImGui::TextDisabled("retail's player selector writes it as a literal 0, so it stays on the slider.");
}

void FElysiumCogWindow_GreenRoom::RenderPlayback(FElysiumGreenRoomRun& Lab)
{
	FElysiumGreenRoomRun::FLabView& View = Lab.LabView();
	const float Duration = Lab.LabDuration();
	if (Lab.LabBody() == nullptr)
	{
		ImGui::TextDisabled("Nothing is standing on the stage.");
		return;
	}

	if (ImGui::Button(View.bPaused ? "Play" : "Pause"))
	{
		View.bPaused = !View.bPaused;
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart"))
	{
		Lab.LabSetTime(0.0f);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%s  -  %.3fs", COG_TCHAR_TO_CHAR(*Lab.LabClip()), Duration);

	// What the standing clip does to the root. A VtMB bank clip either carries the body across the
	// floor on Bip01 or animates only the limbs above a root that never moves, and which of the two
	// it is decides whether a wrong-looking result is the clip, the rig, or the placement. Measured
	// off the sequence rather than declared, so it answers for whichever path built the body.
	const UElysiumBipedAnimInstance* Inst = GetBipedInstance();
	const UAnimSequence* Playing = Inst != nullptr ? Inst->GetPlayingClip() : nullptr;
	if (Playing != nullptr)
	{
		static const FName RootBone(TEXT("Bip01"));
		FTransform Start;
		FTransform End;
		if (SampleClipBone(Playing, RootBone, 0.0, Start)
			&& SampleClipBone(Playing, RootBone, Duration, End))
		{
			const double Travel = FVector::Dist(Start.GetTranslation(), End.GetTranslation());
			const double Turn = FMath::RadiansToDegrees(
				Start.GetRotation().AngularDistance(End.GetRotation()));
			if (Travel > 0.5 || Turn > 0.5)
			{
				ImGui::TextColored(ElysiumCogStyle::ColName,
					"Bip01 moves: %.1f cm, %.1f deg over the clip", Travel, Turn);
			}
			else
			{
				ImGui::TextDisabled("Bip01 is held - the motion is all above the root.");
			}
		}
		else
		{
			ImGui::TextDisabled("No Bip01 on this skeleton.");
		}
	}

	float Time = Lab.LabTime();
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	if (ImGui::SliderFloat("Time", &Time, 0.0f, FMath::Max(Duration, KINDA_SMALL_NUMBER), "%.3fs"))
	{
		// Scrubbing is an explicit request for a pose, so it pauses: a slider that snaps back the
		// instant it is released cannot be used to look at one frame.
		View.bPaused = true;
		Lab.LabSetTime(Time);
	}
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Speed", &View.Speed, 0.02f, 2.0f, "%.2fx");
	ImGui::SameLine();
	if (ImGui::SmallButton("1x"))
	{
		View.Speed = 1.0f;
	}
	// The simulation always integrates over real frame time, whatever the clip is doing. That is
	// what makes slow motion useful here: the pose crawls, the garment keeps its own timing, and
	// the lag between them is exactly the thing being judged.
	ImGui::TextDisabled("Slowing the clip does not slow the cloth - the sim runs in real time.");
}

void FElysiumCogWindow_GreenRoom::RenderView(FElysiumGreenRoomRun& Lab)
{
	FElysiumGreenRoomRun::FLabView& View = Lab.LabView();
	ImGui::SeparatorText("Orbit");
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Yaw", &View.OrbitYaw, -180.0f, 180.0f, "%.0f deg");
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Pitch", &View.OrbitPitch, -85.0f, 85.0f, "%.0f deg");
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Distance", &View.DistanceScale, 0.15f, 3.0f, "%.2fx");
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Look height", &View.LookHeight, 0.0f, 1.0f, "%.2f");

	// The four yaws the contact sheet captures, so a live view can be put on the same footing as a
	// still someone is holding up beside it.
	ImGui::TextDisabled("Capture views:");
	const float Yaws[] = { 0.0f, 45.0f, 90.0f, 180.0f };
	for (const float Yaw : Yaws)
	{
		ImGui::SameLine();
		if (ImGui::SmallButton(COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("%.0f°"), Yaw))))
		{
			View.OrbitYaw = Yaw;
		}
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Reset"))
	{
		const FElysiumGreenRoomRun::FLabView Default;
		View.OrbitYaw = Default.OrbitYaw;
		View.OrbitPitch = Default.OrbitPitch;
		View.DistanceScale = Default.DistanceScale;
		View.LookHeight = Default.LookHeight;
	}
	ImGui::TextDisabled("Hip height is 0.50 - where a skirt hangs. 0.15 frames the hem.");

	ImGui::SeparatorText("Stage");
	// The lab hides the game HUD, reticle included, because it is not showing the player anything.
	// The switch is here rather than assumed permanent: a HUD element that only misbehaves against
	// a moving body is worth being able to put back over one.
	ImGui::Checkbox("Game HUD", &View.bShowHud);
	ImGui::TextDisabled("Off by default - the reticle sits where the hem is.");

	ImGui::SeparatorText("Lighting");
	int32 Mood = static_cast<int32>(View.Lighting);
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	if (ImGui::Combo("Mood", &Mood, "studio (capture rig)\0warm interior\0"))
	{
		View.Lighting = static_cast<FElysiumGreenRoomRun::ELabLighting>(Mood);
	}
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Brightness", &View.LightScale, 0.1f, 2.0f, "%.2fx");
	ImGui::SameLine();
	if (ImGui::SmallButton("1x##light"))
	{
		View.LightScale = 1.0f;
	}
	ImGui::TextDisabled("Studio is the even two-point rig the contact sheets are captured under -");
	ImGui::TextDisabled("bright and cold, so one still compares to another. It also clips a skin");
	ImGui::TextDisabled("albedo to flat white, which is where a tone or a normal stops reading.");
	ImGui::TextDisabled("Warm interior is one tungsten key at a third the level over a dim warm");
	ImGui::TextDisabled("bounce. Lab only: the capture path always shoots studio.");

	// The overlays belong to what is being LOOKED at, not to the garment. The skeleton in
	// particular is the model's own rig and reads on any body, including every model that authors
	// no cloth at all — behind the Cloth tab it would be unreachable on exactly those.
	ImGui::SeparatorText("Overlays");

	ImGui::Checkbox("Skeleton", &View.bDrawSkeleton);
	ImGui::TextDisabled("The model's own bones. The bones a garment collider hangs off are named,");
	ImGui::TextDisabled("so one sitting off its limb reads at a glance.");

	const bool bHasGarment = FindGarment() != nullptr;
	ImGui::BeginDisabled(!bHasGarment);
	ImGui::Checkbox("Bounds", &View.bDrawLattice);
	ImGui::SameLine();
	ImGui::Checkbox("Colliders", &View.bDrawColliders);
	ImGui::EndDisabled();
	if (bHasGarment)
	{
		ImGui::TextDisabled("Garment bounds in amber, its capsules and spheres in red.");
	}
	else
	{
		ImGui::TextDisabled("Both need a generated garment on the standing body - see the Cloth tab.");
	}
}

void FElysiumCogWindow_GreenRoom::RenderAutoLayers(FElysiumGreenRoomRun& Lab)
{
	// What the MODEL declares rides over the standing clip, in the order it declares them. The lab
	// does not choose this list — it displays it and lets it be armed, so a disagreement between
	// what was declared and what is riding is visible rather than inferred.
	//
	// **While driving there is no lab clip**: the graph is standing whatever the gait resolved, and
	// that is precisely the host the owner wants a layer over. So the host is read off the live
	// selection record rather than off what the lab last stood, which is what makes the declared
	// list follow a walking body instead of emptying the moment drive mode starts.
	FString Standing = Lab.LabClip();
	if (Lab.IsDriving())
	{
		const UElysiumBipedAnimInstance* Driven = GetBipedInstance();
		Standing = Driven != nullptr ? Driven->GetAppliedSelection().SequenceLabel : FString();
	}
	if (Standing.IsEmpty())
	{
		return;
	}
	const UGameInstance* GI = GetMapSubsystem() ? GetMapSubsystem()->GetGameInstance() : nullptr;
	UElysiumAnimSubsystem* Anims = GI != nullptr ? GI->GetSubsystem<UElysiumAnimSubsystem>()
		: nullptr;
	const FElysiumNpcClipSet* Set = Anims != nullptr ? Anims->GetClipSet(Lab.LabStem()) : nullptr;
	const FElysiumNpcClip* Host = Set != nullptr ? Set->Find(Standing) : nullptr;
	if (Host == nullptr)
	{
		return;
	}
	// The binding lives on the model that OWNS the clip, which for every shipped host is a shared
	// locomotion bank rather than the body standing on the stage.
	const TSharedPtr<const FElysiumBlendTable> Table = Anims->GetBlendTable(Host->Owner);
	const FElysiumAutoLayerBinding* Binding = Table.IsValid()
		? Table->FindAutoLayers(Standing) : nullptr;
	if (Binding == nullptr || Binding->Clips.IsEmpty())
	{
		return;
	}

	ImGui::SeparatorText(COG_TCHAR_TO_CHAR(*FString::Printf(
		TEXT("Autolayers declared by %s (%d)"), *Standing, Binding->Clips.Num())));

	const TArray<FString>& Armed = Lab.LabLayers();
	for (int32 Entry = 0; Entry < Binding->Clips.Num(); ++Entry)
	{
		const FString& Layer = Binding->Clips[Entry];
		const FElysiumNpcClip* Target = Set->Find(Layer);
		const bool bAdditive = Target != nullptr && Target->IsAdditive();
		const bool bRiding = Armed.Contains(Layer);

		ImGui::PushID(Entry);
		if (ImGui::SmallButton(bRiding ? "re-arm" : "arm"))
		{
			Lab.LabSetLayer(Layer, LayerWeight, LastError);
		}
		ImGui::SameLine();
		// The index is on the row because it is the payload: the engine walks this array in order,
		// and an overlay armed after an additive overwrites it on every bone its mask owns.
		ImGui::TextColored(bRiding ? ElysiumCogStyle::ColName : ElysiumCogStyle::ColDim,
			"%d  %s   [%s]%s", Entry, COG_TCHAR_TO_CHAR(*Layer),
			Target == nullptr ? "unresolved" : (bAdditive ? "additive" : "overlay"),
			bRiding ? "  riding" : "");
		ImGui::PopID();
	}

	if (ImGui::Button("Arm as declared"))
	{
		// In declaration order, and without clearing first: re-asking for a layer already riding
		// only re-weights it, so this is idempotent and does not restart what is already correct.
		LastError.Reset();
		for (const FString& Layer : Binding->Clips)
		{
			Lab.LabSetLayer(Layer, LayerWeight, LastError);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Reverse order"))
	{
		// The A/B for the composition order. Both contributions survive in exactly one order, so
		// the wrong one is visible rather than argued: the overlay's bones lose the additive.
		LastError.Reset();
		Lab.LabClearLayers();
		for (int32 Entry = Binding->Clips.Num() - 1; Entry >= 0; --Entry)
		{
			Lab.LabSetLayer(Binding->Clips[Entry], LayerWeight, LastError);
		}
	}

	// The weight is the one number here that is not the model's. Until a capture measures what
	// retail's dispatcher passes, the slider below IS the value, and saying so on screen is what
	// keeps a stand-in from being read as a measurement.
	ImGui::TextDisabled("Selected by: the model's own table.  Weight: the slider (a STAND-IN --");
	ImGui::TextDisabled("the 4-byte record carries no weight, so retail's lives in the game DLL).");
	if (!LastError.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}
}

void FElysiumCogWindow_GreenRoom::RenderWield(FElysiumGreenRoomRun& Lab)
{
	// Sex selects the manifest row, and the two rows are different models: `w_m_katana` and
	// `w_f_katana` each agree with the bodies of their own sex and disagree with the other's by a
	// fixed per-family offset. So this is a property of the request, not of the stage.
	bool bFemale = Lab.LabWieldFemale();
	if (ImGui::RadioButton("Male wielder", !bFemale)) { bFemale = false; }
	ImGui::SameLine();
	if (ImGui::RadioButton("Female wielder", bFemale)) { bFemale = true; }
	if (bFemale != bWieldRowsFemale)
	{
		bWieldRowsFemale = bFemale;
		bWieldRowsDirty = true;
	}
	if (bWieldRowsDirty)
	{
		WieldRows = FElysiumGreenRoomRun::LabWieldClassnames(bWieldRowsFemale);
		bWieldRowsDirty = false;
	}

	ImGui::SameLine();
	if (ImGui::SmallButton("Rescan##Wield"))
	{
		bWieldRowsDirty = true;
	}

	// What is in the hand right now, and what the table said to put there. The mount bone is the
	// payload: a name the wearer declares means the weapon rides that bone and the wearer's own
	// attack clips swing it; a name it does not means the weapon rides the hand off its frame-0
	// reference pose. Both are one mechanism, and which one happened is only readable here.
	const FString& Held = Lab.LabWield();
	if (Held.IsEmpty())
	{
		ImGui::TextDisabled("Empty-handed.");
	}
	else
	{
		ImGui::TextColored(ElysiumCogStyle::ColName, "holding %s", COG_TCHAR_TO_CHAR(*Held));
		ImGui::SameLine();
		if (ImGui::SmallButton("Holster"))
		{
			Lab.LabClearWield();
			LastNotice.Reset();
		}
		// The composition, described: which mount, whether the wearer declares it, where the weapon
		// sits relative to the hand right now. The line carries no verdict — whether the weapon
		// RIDES the hand is only provable over a window of a moving base, which is the button below.
		FString Check;
		if (Lab.LabWieldCheck(Check))
		{
			ImGui::TextWrapped("%s", COG_TCHAR_TO_CHAR(*Check));
		}
		if (Lab.LabWieldTrackRunning())
		{
			ImGui::TextDisabled("tracking check: sampling...");
		}
		else
		{
			if (ImGui::SmallButton("Check tracking"))
			{
				FString Error;
				if (!Lab.LabWieldTrackStart(0.0f, 0.0f, Error))
				{
					LastError = Error;
				}
			}
			if (!Lab.LabWieldTrackVerdict().IsEmpty())
			{
				ImGui::SameLine();
				ImGui::TextColored(
					Lab.LabWieldTrackPassed() ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColError,
					Lab.LabWieldTrackPassed() ? "ok" : "failed");
				ImGui::TextWrapped("%s", COG_TCHAR_TO_CHAR(*Lab.LabWieldTrackVerdict()));
			}
		}
	}
	if (!LastNotice.IsEmpty())
	{
		ImGui::TextWrapped("%s", COG_TCHAR_TO_CHAR(*LastNotice));
	}

	ImGui::SeparatorText("Items that carry a wield model");
	if (WieldRows.IsEmpty())
	{
		// The one outcome that is a failure rather than an authored answer, so it names its fix.
		ImGui::TextDisabled(
			"No wield rows. Is the corpus baked? uv run elysium export wield");
		return;
	}

	ImGui::SetNextItemWidth(FMath::Max(GetDpiScale() * 120.0f,
		ImGui::GetContentRegionAvail().x - GetDpiScale() * 80.0f));
	FCogWidgets::InputTextWithHint("##WieldFilter", "(filter)", WieldFilter);

	ImGui::BeginChild("##WieldRows", ImVec2(0, GetDpiScale() * 180.f), ImGuiChildFlags_Borders);
	for (int32 Index = 0; Index < WieldRows.Num(); ++Index)
	{
		const FString& Classname = WieldRows[Index];
		if (!WieldFilter.IsEmpty() && !Classname.Contains(WieldFilter))
		{
			continue;
		}
		ImGui::PushID(Index);
		if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Classname), Held == Classname))
		{
			FString Detail;
			const EElysiumWieldResult Answer = Lab.LabSetWield(Classname, bFemale, Detail);
			// Only an outcome that means something is wrong reaches the error line. The rest are
			// notices — a weapon that legitimately carries no geometry must never read here as a
			// missing asset, which is most of the corpus.
			if (ElysiumWieldFailed(Answer))
			{
				LastError = Detail;
				LastNotice.Reset();
			}
			else
			{
				LastNotice = Detail;
				LastError.Reset();
			}
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::TextDisabled("%d rows carry geometry for this sex.", WieldRows.Num());
}

void FElysiumCogWindow_GreenRoom::RenderCloth(FElysiumGreenRoomRun& Lab)
{
	// A garment is attached when the body is BUILT, so a regenerated asset reaches the stage only
	// on the next build -- attaching a simulating component to a body already posed this frame
	// would drop the garment out of the bind pose in view. Restand is that rebuild, without going
	// back to the model list.
	ImGui::BeginDisabled(Lab.LabStem().IsEmpty());
	if (ImGui::Button("Restand"))
	{
		Stand(Lab, Lab.LabStem(), Lab.LabClip());
	}
	ImGui::EndDisabled();

	UChaosClothComponent* Cloth = FindGarment();
	if (Cloth == nullptr)
	{
		ImGui::TextDisabled("No garment component on the stage.");
		// Three different causes read identically in the viewport, so they are separated here:
		// the model authored no cloth at all, the export never ran, or the asset was never
		// generated from an export that did.
		const FString Stem = Lab.LabStem();
		if (!Stem.IsEmpty())
		{
			const bool bSidecar = IFileManager::Get().FileExists(
				*FElysiumContentPaths::NpcGarment(Stem));
			if (!bSidecar)
			{
				ImGui::TextDisabled("%s exported no garment payload - either its model authors "
					"none, or the character export has not run.", COG_TCHAR_TO_CHAR(*Stem));
			}
			else
			{
				ImGui::TextDisabled("%s exported a payload but no asset exists.",
					COG_TCHAR_TO_CHAR(*Stem));
				ImGui::TextDisabled("  uv run elysium build content   (make_cloth_assets.py)");
			}
		}
		return;
	}

	const UChaosClothAsset* Asset = Cast<UChaosClothAsset>(Cloth->GetAsset());
	ImGui::Text("%s", COG_TCHAR_TO_CHAR(*GetNameSafe(Asset)));
	ImGui::Text("leader pose: %s",
		Cloth->LeaderPoseComponent.IsValid() ? "bound" : "NONE - the garment will not follow");

	ImGui::SeparatorText("Simulation");
	bool bSuspended = Cloth->IsSimulationSuspended();
	if (ImGui::Checkbox("Suspended", &bSuspended))
	{
		if (bSuspended)
		{
			Cloth->SuspendSimulation();
		}
		else
		{
			Cloth->ResumeSimulation();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Teleport"))
	{
		// What a body moved by anything other than its own motion needs: without it the garment
		// solves the jump as one enormous frame of velocity and flails.
		Cloth->ForceNextUpdateTeleportAndReset();
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset config"))
	{
		Cloth->ResetConfigProperties();
	}

	// Live tuning is the asset's OWN property set rather than a fixed list of sliders: a Chaos
	// cloth config is data, and enumerating it means this panel does not go stale when the
	// generator starts authoring a property it did not before.
	if (UChaosClothAssetInteractor* Interactor =
			Cast<UChaosClothAssetInteractor>(Cloth->GetClothOutfitInteractor()))
	{
		ImGui::SeparatorText("Properties");
		const TArray<FName> Names = Interactor->GetAllPropertyNames();
		for (const FName& Property : Names)
		{
			float Value = Interactor->GetFloatPropertyValue(Property, 0);
			ImGui::SetNextItemWidth(-GetDpiScale() * 130.f);
			if (ImGui::DragFloat(COG_TCHAR_TO_CHAR(*Property.ToString()), &Value, 0.01f))
			{
				// LOD -1 writes every LOD, which is what a live edit means here: the garment has
				// one authored config and tuning it per LOD would diverge as the body walks away.
				Interactor->SetFloatPropertyValue(Property, -1, Value);
			}
		}
		if (Names.IsEmpty())
		{
			ImGui::TextDisabled("The asset exposes no float properties.");
		}
	}

	RenderClothDebugDraw();
}

// Chaos's own cloth overlays, as toggles over the shared table in `Debug/ElysiumClothDebug.h`.
// They are the instrument this vertical is debugged with, and a console command nobody can recall
// is not an instrument.
void FElysiumCogWindow_GreenRoom::RenderClothDebugDraw()
{
	ImGui::SeparatorText("Debug draw");

	const int32 Active = ElysiumClothDebug::NumActiveDraws();
	ImGui::BeginDisabled(Active == 0);
	if (ImGui::Button("All off"))
	{
		ElysiumClothDebug::ClearDraws();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("%d on  -  also elysium.garment", Active);

	// Two columns: the list is long enough that one column pushes the properties above it off the
	// top of the window at any sane panel height.
	if (ImGui::BeginTable("##ClothDebugDraw", 2, ImGuiTableFlags_SizingStretchSame))
	{
		for (const ElysiumClothDebug::FDrawToggle& Toggle : ElysiumClothDebug::DrawToggles())
		{
			ImGui::TableNextColumn();
			IConsoleVariable* const CVar = ElysiumClothDebug::DrawCVar(Toggle.Suffix);
			if (CVar == nullptr)
			{
				// Unavailable rather than off: these are registered by the ChaosCloth module and
				// compiled out with CHAOS_DEBUG_DRAW, so a missing one is worth seeing as missing.
				ImGui::TextDisabled("%s", Toggle.Label);
				continue;
			}
			bool bOn = CVar->GetBool();
			if (ImGui::Checkbox(Toggle.Label, &bOn))
			{
				CVar->Set(bOn, ECVF_SetByConsole);
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s\n\np.ChaosCloth.DebugDraw%s", Toggle.Help,
					COG_TCHAR_TO_CHAR(Toggle.Suffix));
			}
		}
		ImGui::EndTable();
	}
}

// Drive mode's readout. Everything here is READ: the sample the mover published, the record the
// resolver produced, the parameters the graph is posing from, and the bones that came out. Nothing
// is re-derived, because a readout that computes its own answer is a second implementation of the
// rule that decided the pose, and the two disagreeing is exactly the confusion this exists to end.
UElysiumEntityBodies* FElysiumCogWindow_GreenRoom::GetBodies() const
{
	const UElysiumMapSubsystem* Maps = GetMapSubsystem();
	AElysiumMapActor* Map = Maps ? Maps->GetCurrentMap() : nullptr;
	return Map ? Map->GetBodies() : nullptr;
}

void FElysiumCogWindow_GreenRoom::RenderEyes(FElysiumGreenRoomRun& Lab)
{
	UElysiumEntityBodies* Bodies = GetBodies();
	USkeletalMeshComponent* Body = Lab.LabBody();
	if (Bodies == nullptr || Body == nullptr)
	{
		ImGui::TextDisabled("Nothing is standing on the stage.");
		return;
	}

	// --- what the pass actually bound -------------------------------------------------------------
	//
	// First, because every control below is meaningless on a body that bound nothing, and because
	// this is the one eye failure that looks like a working eye: an unjoined section still draws the
	// eye master, whose default iris is a texture.
	ImGui::SeparatorText("Binding");
	UElysiumEntityBodies::FElysiumEyeReadout Eyes;
	const bool bBound = Bodies->DescribeEyes(Body, Eyes);
	if (!bBound)
	{
		ImGui::TextColored(ElysiumCogStyle::ColWarn, "%s carries no eye sections.",
			COG_TCHAR_TO_CHAR(*Lab.LabStem()));
		ImGui::TextDisabled("Either the model authors no `StudioEyeball` record, or npc/eyes/<stem>.json");
		ImGui::TextDisabled("was not exported. Animals and most crowd bodies are the normal case.");
		return;
	}
	const bool bHealthy = Eyes.BoundCount > 0 && Eyes.BoundCount == Eyes.EyeSlotCount;
	ImGui::TextColored(bHealthy ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColError,
		"%d of %d eye sections bound, over %d record%s",
		Eyes.BoundCount, Eyes.EyeSlotCount, Eyes.RecordCount, Eyes.RecordCount == 1 ? "" : "s");
	for (const TPair<FString, int32>& Join : Eyes.Slots)
	{
		const bool bJoined = Join.Value != INDEX_NONE;
		ImGui::TextColored(bJoined ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColError,
			"  %s -> %s", COG_TCHAR_TO_CHAR(*Join.Key),
			bJoined ? COG_TCHAR_TO_CHAR(*FString::Printf(TEXT("eyeball %d"), Join.Value))
				: "no record");
	}
	if (!bHealthy)
	{
		ImGui::TextDisabled("An unjoined section still draws M_Eyes, so it has a round iris that never");
		ImGui::TextDisabled("aims and never blinks. The join is the slot's material name against the");
		ImGui::TextDisabled("sidecar's `material` field, case-insensitive and exact.");
	}
	ImGui::BeginDisabled(Eyes.BoundCount == 0);

	// --- where it looks ---------------------------------------------------------------------------
	UElysiumEntityBodies::FElysiumEyeDebug& Debug = Bodies->EyeDebug();
	ImGui::SeparatorText("Aim");
	typedef UElysiumEntityBodies::FElysiumEyeDebug::EGaze EGaze;
	const auto GazeButton = [&Debug](const char* Label, EGaze Mode, const char* Tip)
	{
		if (ImGui::RadioButton(Label, Debug.Gaze == Mode)) { Debug.Gaze = Mode; }
		if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", Tip); }
	};
	GazeButton("Shipping", EGaze::Off,
		"The priority the game runs: elysium.EyeTrackPlayer, then the substrate's gaze, then rest. "
		"On the stage there is no character behind the body, so this rests.");
	ImGui::SameLine();
	GazeButton("Rest", EGaze::Rest,
		"bEyeMove off - the record's own authored resting aim. A real retail configuration, and NOT "
		"guaranteed to point out of the face: it is whatever the model's QC authored.");
	ImGui::SameLine();
	GazeButton("Camera", EGaze::Camera,
		"The orbit camera. The cheapest unambiguous check that the basis math is right: if both "
		"irises converge on the lens as you orbit, the record, the import, the solve and the plane "
		"parameters are all correct.");
	ImGui::SameLine();
	GazeButton("Point", EGaze::Point, "A marker you steer, below.");

	if (Debug.Gaze == EGaze::Point)
	{
		ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
		ImGui::SliderFloat("Yaw##eye", &EyeAimYaw, -180.f, 180.f, "%.0f deg");
		ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
		ImGui::SliderFloat("Pitch##eye", &EyeAimPitch, -80.f, 80.f, "%.0f deg");
		ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
		ImGui::SliderFloat("Distance##eye", &EyeAimDistance, 20.f, 800.f, "%.0f cm");
		ImGui::Checkbox("Draw the marker", &bEyeAimDraw);
		ImGui::SameLine();
		if (ImGui::SmallButton("Centre")) { EyeAimYaw = 0.f; EyeAimPitch = 0.f; }
		ImGui::TextDisabled("Degrees off the body's facing, about the head. Sweep the yaw past the");
		ImGui::TextDisabled("shoulder: retail's eyes have no clamp, so the iris rolls to the corner.");
	}
	// Resolved every frame regardless of the mode, so switching to Point does not aim at where the
	// body stood when the tab was opened.
	{
		const int32 Head = Body->GetBoneIndex(TEXT("Bip01 Head"));
		const FTransform HeadWorld = Head != INDEX_NONE
			? Body->GetBoneTransform(Head) : Body->GetComponentTransform();
		const FRotator Facing = Body->GetComponentRotation();
		const FVector Dir = FRotator(EyeAimPitch, Facing.Yaw + EyeAimYaw, 0.f).Vector();
		Debug.Target = HeadWorld.GetLocation() + Dir * EyeAimDistance;
		if (Debug.Gaze == EGaze::Point && bEyeAimDraw)
		{
			if (const UWorld* World = Body->GetWorld())
			{
				DrawDebugSphere(World, Debug.Target, 4.f, 12, FColor::Cyan, /*bPersistent=*/false,
					/*LifeTime=*/-1.f, /*DepthPriority=*/0, /*Thickness=*/0.6f);
			}
		}
	}
	ImGui::TextColored(Eyes.bAiming ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColWarn,
		Eyes.bAiming ? "aiming" : "resting on the authored aim");

	// --- the lids ---------------------------------------------------------------------------------
	ImGui::SeparatorText("Blink");
	ImGui::Checkbox("Hold open", &Debug.bHoldBlink);
	ImGui::SameLine();
	// Disabled rather than made to win: the hold re-opens the lid on the frame after the envelope
	// starts, so a press under it would fire a blink one frame long and read as a broken button.
	ImGui::BeginDisabled(Debug.bHoldBlink);
	if (ImGui::Button("Blink now")) { Debug.bBlinkNow = true; }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("300 ms, and asymmetric: shut in 48, open over the rest.");
	ImGui::ProgressBar(Eyes.Blink, ImVec2(-GetDpiScale() * 90.f, 0.f));
	ImGui::SameLine();
	ImGui::Text("lid");
	const UElysiumBodyAnimInstance* Inst = GetBodyInstance();
	const bool bHasLids = Eyes.bHasSet && Inst != nullptr;
	if (!bHasLids)
	{
		ImGui::TextDisabled("The envelope drives the material either way; a morph needs a facial rig.");
	}

	// --- the renderer's own knobs -----------------------------------------------------------------
	ImGui::SeparatorText("Tuning");
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	ImGui::SliderFloat("Iris size", &Debug.Tuning.EyeSize, -1.f, 2.f, "%.2f");
	ImGui::TextDisabled("Retail's eyeball_size: enters as 1/(1/iris_scale + this), so it widens the");
	ImGui::TextDisabled("iris upward and narrows it downward. 0 is the shipped config.");
	float Shift[3] = {
		static_cast<float>(Debug.Tuning.EyeShift.X),
		static_cast<float>(Debug.Tuning.EyeShift.Y),
		static_cast<float>(Debug.Tuning.EyeShift.Z) };
	ImGui::SetNextItemWidth(-GetDpiScale() * 90.f);
	if (ImGui::SliderFloat3("Eye shift", Shift, -2.f, 2.f, "%.2f cm"))
	{
		Debug.Tuning.EyeShift = FVector(Shift[0], Shift[1], Shift[2]);
	}
	ImGui::TextDisabled("Applied by the sign of each component, so a mirrored pair moves apart. It");
	ImGui::TextDisabled("shifts the iris planes but deliberately not the shading origin.");
	if (ImGui::Button("Reset tuning"))
	{
		Debug.Tuning = FElysiumEyeTuning();
	}
	ImGui::EndDisabled();
}

void FElysiumCogWindow_GreenRoom::RenderDrive(FElysiumGreenRoomRun& Lab)
{
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const IElysiumPlayerBody* Body = PC ? Cast<IElysiumPlayerBody>(PC->GetPawn()) : nullptr;
	const AElysiumMapActor* Map = GetMapActor();
	if (!Body || !Map)
	{
		ImGui::TextDisabled("No player body in this session.");
		return;
	}

	ImGui::TextDisabled("WASD move  |  Shift gait  |  Ctrl crouch  |  Space jump  |  mouse look");
	ImGui::TextDisabled("F1 hands the keyboard to this window, and hands it back.");
	ImGui::Separator();

	// --- what the mover published, and what the resolver made of it ---------------------------
	const ImGuiTableFlags TableFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders |
		ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
	if (ImGui::BeginTable("##DriveLocomotion", ElysiumCogLocomotion::NumColumns, TableFlags,
		ImVec2(0, GetDpiScale() * 56.f)))
	{
		ImGui::TableSetupScrollFreeze(0, 1);
		ElysiumCogLocomotion::SetupColumns();
		ImGui::TableHeadersRow();
		// The driver's published pair, not a fresh sample beside a settled record: the mover's
		// getter recomputes from live component state, and the two halves then describe different
		// frames.
		ElysiumCogLocomotion::Row("player", COG_TCHAR_TO_CHAR(*PC->GetPawn()->GetName()),
			Map->GetPlayerAnimSample(), &Map->GetPlayerAnimSelection());
		ImGui::EndTable();
	}

	USkeletalMeshComponent* Visual = Body->GetPlayerVisual();
	if (Visual == nullptr)
	{
		ImGui::TextColored(ElysiumCogStyle::ColWarn,
			"No model on the pawn -- the mover is driving and nothing is drawn.");
		ImGui::TextDisabled("Name one on the Model tab, or relaunch with `uv run elysium gr <stem> --drive`.");
		return;
	}
	UElysiumBipedAnimInstance* Graph = Cast<UElysiumBipedAnimInstance>(Visual->GetAnimInstance());
	if (Graph == nullptr)
	{
		// A cast body, or a player body whose graph package is missing. Every row below would
		// honestly read blank on the native host, so say so rather than drawing an empty panel.
		ImGui::TextColored(ElysiumCogStyle::ColWarn,
			"The body is on the cast's native host, not ABP_ElysiumBiped.");
		ImGui::TextDisabled("The graph rows below need a player body posed from the graph.");
		return;
	}

	// --- what the graph is posing from ---------------------------------------------------------
	ImGui::SeparatorText("Graph");
	ImGui::Text("state %s%s", ElysiumAnimGraph::StateName(Graph->RequestedState),
		Graph->bStateChanged ? "  (changing)" : "");
	ImGui::Text("move_yaw %.1f    speed %.1f    axis0 %.1f    %s",
		Graph->MoveYaw, Graph->Speed, Graph->GridAxis0,
		Graph->bHasBlendSpace ? "blend space" : "one clip");

	// --- held, playing, or nothing published yet ------------------------------------------------
	//
	// Three states rather than two, because a body that has never been handed a selection poses the
	// bind pose BY CONSTRUCTION — it has nothing to hold — and that is a correct frame that looks
	// exactly like the defect below it.
	const FElysiumAnimationSelection& Applied = Graph->GetAppliedSelection();
	if (Applied.AnimationName.IsEmpty() && !Graph->IsHoldingPose())
	{
		ImGui::TextDisabled("no selection yet -- the body is in its bind pose because it has none");
	}
	else if (Graph->IsHoldingPose())
	{
		// A warning, not an error. This is retail's own behaviour: a failed selection never restarts
		// the sequence, so the body goes on playing what it had.
		ImGui::TextColored(ElysiumCogStyle::ColWarn, "held: still playing '%s'",
			COG_TCHAR_TO_CHAR(*Applied.AnimationName));
		const FElysiumAnimationSelection& Asked = Map->GetPlayerAnimSelection();
		ImGui::SameLine();
		ImGui::TextDisabled("(asked for %s)", Asked.ResolvedActivity.IsEmpty()
			? "nothing" : COG_TCHAR_TO_CHAR(*Asked.ResolvedActivity));
	}
	else
	{
		ImGui::TextColored(ElysiumCogStyle::ColOk, "playing '%s'",
			COG_TCHAR_TO_CHAR(*Applied.AnimationName));
	}

	// --- the one-shot report the jump chain rides on --------------------------------------------
	const FElysiumOneShotReport& OneShot = Graph->GetOneShotReport();
	const FElysiumAnimationSelection& Current = Map->GetPlayerAnimSelection();
	ImGui::Text("one-shot: %s", OneShot.bInOneShotState ? "in state" : "--");
	ImGui::SameLine();
	// A negative remaining is "cannot say", never a duration: the engine answers MAX_flt when it
	// finds no relevant asset player, and rendering that as a number is how it read as "playing".
	if (OneShot.RemainingSeconds < 0.0f) { ImGui::TextDisabled("remaining: cannot say"); }
	else { ImGui::Text("remaining %.2fs%s", OneShot.RemainingSeconds,
		OneShot.bComplete ? " (complete)" : ""); }
	ImGui::SameLine();
	// The gate the driver reads. A mismatch means the report describes a request that has already
	// been replaced, which is the state it must not end.
	const bool bGenerationMatches = OneShot.Generation == Current.Generation;
	ImGui::TextColored(bGenerationMatches ? ElysiumCogStyle::ColDim : ElysiumCogStyle::ColWarn,
		"gen %u/%u", OneShot.Generation, Current.Generation);

	// --- the T-pose observable -------------------------------------------------------------------
	//
	// A dead pin, a null asset, a miss projected anyway: all of them evaluate a player node with
	// nothing to play, and all of them answer the bind pose. The bones are the only thing that says
	// so, and this is the same measure `Elysium.Content.PlayerGraphInstance` asserts offline.
	ImGui::SeparatorText("Pose");
	const USkinnedAsset* Asset = Visual->GetSkinnedAsset();
	if (Asset != DeviationAsset.Get())
	{
		DeviationAsset = Asset;
		DeviationRefPose.Reset();
		if (Asset != nullptr)
		{
			ElysiumPose::FillRefPoseComponentSpace(Asset->GetRefSkeleton(), DeviationRefPose);
		}
	}
	const TArray<FTransform>& Pose = Visual->GetComponentSpaceTransforms();
	if (DeviationRefPose.IsEmpty() || Pose.IsEmpty())
	{
		ImGui::TextDisabled("no evaluated pose yet");
	}
	else
	{
		const ElysiumPose::FDeviation Dev = ElysiumPose::Measure(DeviationRefPose, Pose);
		const int32 Posed = FMath::Min(DeviationRefPose.Num(), Pose.Num()) - 1;
		const bool bBind = Dev.MovedBones <= Posed / 4;
		ImGui::TextColored(bBind ? ElysiumCogStyle::ColError : ElysiumCogStyle::ColOk,
			"%d of %d bones off the bind pose (max %.1f deg)", Dev.MovedBones, Posed, Dev.MaxDegrees);
		if (bBind)
		{
			ImGui::TextDisabled("that is a T-pose unless the row above says no selection yet.");
		}
	}

	// --- where the body stands -------------------------------------------------------------------
	// The arena has no lanes: it is one flat plate rather than a bracket ladder, so the only
	// placement question it has is "put me back at the start", which the mode row above answers.
	// Drawing an empty lane combo here would read as a gym that failed to build.
	if (Lab.IsArena())
	{
		ImGui::SeparatorText("Arena");
		const ElysiumArena::FSpec& Room = Lab.ArenaSpec();
		bool bArenaVisible = Lab.ArenaVisible();
		if (ImGui::Checkbox("Draw the room", &bArenaVisible))
		{
			FString Error;
			if (!Lab.LabSetGymVisible(bArenaVisible, Error)) { LastError = Error; }
			else { LastError.Reset(); }
		}
		ImGui::TextDisabled("%d solids, %d pads, %d anchors. The boxes collide either way,",
			Room.Solids.Num(), Room.Pads.Num(), Room.Anchors.Num());
		ImGui::TextDisabled("and they are the only thing in this runtime that breaks an eye line.");
		return;
	}

	ImGui::SeparatorText("Gym");
	const ElysiumGym::FSpec& Spec = Lab.DriveGym();
	const FName Seated = Lab.DriveLane();
	if (ImGui::BeginCombo("Lane", COG_TCHAR_TO_CHAR(*Seated.ToString())))
	{
		for (const ElysiumGym::FLane& Lane : Spec.Lanes)
		{
			const bool bSelected = Lane.Name == Seated;
			if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Lane.Name.ToString()), bSelected))
			{
				FString Error;
				if (!Lab.LabSeatOnLane(Lane.Name, Error)) { LastError = Error; }
				else { LastError.Reset(); }
			}
			if (ImGui::IsItemHovered())
			{
				// The bracket is what the lane is named for, and its unit is the family's.
				ImGui::SetTooltip("bracket %.1f%s", Lane.BracketUnits,
					Lane.bSpeedDependent ? " (speed-dependent)" : "");
			}
		}
		ImGui::EndCombo();
	}
	if (ImGui::Button("Reseat"))
	{
		FString Error;
		if (!Lab.LabSeatOnLane(Seated, Error)) { LastError = Error; }
		else { LastError.Reset(); }
	}
	ImGui::SameLine();
	bool bVisible = Lab.DriveGymVisible();
	if (ImGui::Checkbox("Draw the gym", &bVisible))
	{
		FString Error;
		if (!Lab.LabSetGymVisible(bVisible, Error)) { LastError = Error; }
		else { LastError.Reset(); }
	}
	ImGui::TextDisabled("%d lanes, %d solids. The boxes collide either way.",
		Spec.Lanes.Num(), Spec.Placements.Num());
}

void FElysiumCogWindow_GreenRoom::RenderContent()
{
	Super::RenderContent();

	FElysiumGreenRoomRun* Lab = GetLab();
	if (Lab == nullptr)
	{
		ImGui::TextDisabled("No green room is armed in this session.");
		if (ImGui::Button("Open the green room"))
		{
			OpenLab();
		}
		ImGui::TextDisabled("Leaves the current map for an empty world and stands a neutral stage in");
		ImGui::TextDisabled("it. `elysium.gr` does the same from the console; `uv run elysium gr`");
		ImGui::TextDisabled("launches straight into it. `elysium.map <name>` goes back.");
		if (!LastError.IsEmpty())
		{
			ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
		}
		return;
	}
	if (!Lab->IsLab())
	{
		ImGui::TextDisabled("A green-room capture run owns the stage in this session.");
		ImGui::TextDisabled("It writes its stills and exits; the lab cannot share it.");
		return;
	}
	if (!Lab->IsLabReady())
	{
		ImGui::TextDisabled("Building the stage...");
		return;
	}

	// The mode selector, above the tabs because it changes what they mean. Review is the animation
	// programme's stage — one body, one clip, the orbit; Drive is the shipping path with the
	// movement gym under it (CCC6); Arena is the same shipping path on a navigable room, which is
	// what a cast needs to path at all.
	const bool bDriving = Lab->IsDriving();
	const bool bArena = Lab->IsArena();
	const auto ModeButton = [Lab, this](const char* Label, FElysiumGreenRoomRun::ELabMode Mode,
		bool bActive)
	{
		if (bActive) { ImGui::BeginDisabled(); }
		if (ImGui::Button(Label))
		{
			FString Error;
			if (!Lab->LabSetMode(Mode, Error)) { LastError = Error; }
			else { LastError.Reset(); }
		}
		if (bActive) { ImGui::EndDisabled(); }
	};
	ModeButton("Review", FElysiumGreenRoomRun::ELabMode::Review, !bDriving);
	ImGui::SameLine();
	ModeButton("Drive", FElysiumGreenRoomRun::ELabMode::Drive, bDriving && !bArena);
	ImGui::SameLine();
	ModeButton("Arena", FElysiumGreenRoomRun::ELabMode::Arena, bArena);
	ImGui::SameLine();
	ImGui::TextDisabled(bArena
		? "a navigable room — the Cast & AI window stands the opposition up"
		: bDriving
			? "the pawn on the gym, posed by the shipping path"
			: "one body on the stage, one clip, the orbit");
	if (bArena)
	{
		// Recast builds asynchronously and a room whose graph has not landed produces characters
		// that acquire, select a chase, and fail every path request by name. Saying so here is the
		// difference between a two-minute puzzle and a two-hour one.
		const bool bNavReady = Lab->IsArenaNavigationReady();
		ImGui::TextColored(bNavReady ? ElysiumCogStyle::ColOk : ElysiumCogStyle::ColWarn,
			bNavReady ? "navigation ready" : "navigation building...");
		ImGui::SameLine();
		if (ImGui::SmallButton("Reseat me"))
		{
			FString Error;
			if (!Lab->ArenaSeatPlayer(Error)) { LastError = Error; }
		}
		ImGui::SameLine();
		bool bMarkers = Lab->LabView().bDrawArenaMarkers;
		if (ImGui::Checkbox("Pads & anchors", &bMarkers))
		{
			Lab->LabView().bDrawArenaMarkers = bMarkers;
		}
	}
	if (!LastError.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}

	if (!ImGui::BeginTabBar("##GreenRoom"))
	{
		return;
	}
	if (ImGui::BeginTabItem("Model"))
	{
		RenderModel(*Lab);
		ImGui::EndTabItem();
	}
	if (!bDriving && ImGui::BeginTabItem("Clips"))
	{
		RenderClips(*Lab);
		ImGui::EndTabItem();
	}
	if (!bDriving && ImGui::BeginTabItem("Layers"))
	{
		RenderLayers(*Lab);
		ImGui::EndTabItem();
	}
	if (bDriving && ImGui::BeginTabItem("Drive"))
	{
		RenderDrive(*Lab);
		ImGui::EndTabItem();
	}
	// The scrubber drives a clip seek, which has no meaning on a body the graph is posing.
	if (!bDriving && ImGui::BeginTabItem("Playback"))
	{
		RenderPlayback(*Lab);
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("View"))
	{
		RenderView(*Lab);
		ImGui::EndTabItem();
	}
	// Available while driving as well: a weapon that tracks the hand through a walk is exactly what
	// drive mode is for, and the attachment is the same one either way.
	if (ImGui::BeginTabItem("Weapon"))
	{
		RenderWield(*Lab);
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Eyes"))
	{
		RenderEyes(*Lab);
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Cloth"))
	{
		RenderCloth(*Lab);
		ImGui::EndTabItem();
	}
	ImGui::EndTabBar();
}

#endif // ENABLE_COG
