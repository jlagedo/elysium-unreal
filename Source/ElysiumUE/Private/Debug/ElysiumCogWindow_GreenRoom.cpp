#include "Debug/ElysiumCogWindow_GreenRoom.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "Debug/ElysiumGreenRoomRun.h"
#include "ElysiumContentPaths.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumNpcSubsystem.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Algo/Unique.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "CogWidgets.h"
#include "imgui.h"

namespace
{
	IConsoleVariable* ClothCVar()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.Cloth"));
	}

	IConsoleVariable* BakedCVar()
	{
		return IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.BakedCharacters"));
	}

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
		"Source: `elysium.BakedCharacters` picks which build of the body stands here -- the assets "
		"on the /ElysiumBaked mount, or the one glTFRuntime builds from the .glb at load. The "
		"choice is made when the body is built, so flip it and press Restand. Only the models the "
		"character bake has run over are on the mount; anything else silently falls back to the "
		"loader, which is why the line under the checkbox names the path the standing body "
		"actually came from rather than the one the cvar asked for.\n\n"
		"Cloth: VtMB simulates no garment at all -- a skirt or coat is skinned rigidly to the "
		"pelvis and swings as one shell. The offline spike appends a bone lattice to a copy of the "
		"mesh and hangs an AnimDynamics chain down each panel; `npc/cloth/<stem>.json` is the "
		"solver setup it derived from the model's own measurements. These sliders edit the live "
		"simulation, never the file, until Save bakes them into it. Revert goes back to the "
		"sidecar, and `elysium.Cloth 0` puts the faithful rigid mesh back on.\n\n"
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

UElysiumNpcAnimInstance* FElysiumCogWindow_GreenRoom::GetBodyInstance() const
{
	FElysiumGreenRoomRun* Lab = GetLab();
	USkeletalMeshComponent* Body = Lab ? Lab->LabBody() : nullptr;
	return Body ? Cast<UElysiumNpcAnimInstance>(Body->GetAnimInstance()) : nullptr;
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
		TunedStem.Reset();
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
		Stand(Lab, PendingStem, Clips[Index]);
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
	LastNotice = FString::Printf(TEXT("layering %s over %s"), *Clips[Index], *Lab.LabClip());
}

void FElysiumCogWindow_GreenRoom::RenderSource(FElysiumGreenRoomRun& Lab)
{
	IConsoleVariable* Baked = BakedCVar();
	if (Baked == nullptr)
	{
		return;
	}

	// Which build of the body to use is decided when the body is built, so the toggle takes effect
	// on the next Restand -- and Restand has to be the cache-clearing one, or it silently reuses
	// whichever mesh answered first (`UElysiumEntityBodies::ForgetNpcVisuals`).
	bool bEnabled = Baked->GetInt() != 0;
	if (ImGui::Checkbox("elysium.BakedCharacters - stand the baked asset", &bEnabled))
	{
		Baked->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
		// Restand immediately. A toggle whose only visible effect is to arm a second button reads
		// as broken, and this one had a whole debugging session spent on it.
		if (!Lab.LabStem().IsEmpty())
		{
			LastError.Reset();
			Lab.LabRestand(LastError);
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(Lab.LabStem().IsEmpty());
	if (ImGui::Button("Restand##source"))
	{
		LastError.Reset();
		Lab.LabRestand(LastError);
	}
	ImGui::EndDisabled();

	// Which path the body ACTUALLY came from, read off the asset that is standing rather than off
	// the cvar. Only part of the cast is baked, and an unbaked model falls back to the loader
	// without saying so -- which would otherwise make an A/B look like a null result.
	const UElysiumNpcAnimInstance* Inst = GetBodyInstance();
	const USkeletalMeshComponent* Comp = Inst != nullptr ? Inst->GetSkelMeshComponent() : nullptr;
	const USkeletalMesh* Mesh = Comp != nullptr ? Comp->GetSkeletalMeshAsset() : nullptr;
	if (Mesh == nullptr)
	{
		ImGui::TextDisabled("Nothing is standing on the stage.");
		return;
	}
	const FString Path = Mesh->GetPathName();
	if (Path.StartsWith(FElysiumContentPaths::BakedMount()))
	{
		const USkeleton* Skeleton = Mesh->GetSkeleton();
		const FString Family = Skeleton != nullptr
			? FElysiumContentPaths::BakedCharacterFamily(Skeleton->GetName()) : FString();
		ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f),
			"baked: %s (rig family '%s')", COG_TCHAR_TO_CHAR(*FPackageName::GetShortName(Path)),
			COG_TCHAR_TO_CHAR(*Family));
	}
	else if (!bEnabled)
	{
		ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f), "glTFRuntime: built at load");
	}
	else if (ElysiumNpcVisual::IsStemBaked(Lab.LabStem()))
	{
		// The asset IS there and this body did not come from it, so the toggle was off when this
		// body was built. Said outright rather than guessed at: the old wording blamed the export
		// for a stale cache, which sends the reader off to re-bake a model that was already baked.
		ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f),
			"glTFRuntime: built at load - the baked asset EXISTS; this body predates the toggle");
	}
	else
	{
		ImGui::TextColored(ImVec4(0.85f, 0.75f, 0.35f, 1.0f),
			"glTFRuntime: built at load - this model is not on the baked mount");
	}
}

void FElysiumCogWindow_GreenRoom::ScanRootMotion(FElysiumGreenRoomRun& Lab)
{
	ClipRootMotion.Reset();
	ScannedStem.Reset();

	const UGameInstance* GI = GetMapSubsystem() ? GetMapSubsystem()->GetGameInstance() : nullptr;
	UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
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
		// Null own-asset: on the baked path there is none, and on the loader path the lab's own
		// body already parsed it, so this resolves through the same cache either way.
		const UAnimSequence* Sequence = Anims->ResolveClip(PendingStem, Clips[Index], Mesh,
			nullptr, Error);
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
			StemHasCloth.Add(IFileManager::Get().FileExists(*FElysiumContentPaths::NpcClothGlb(Stem)));
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

	// --- clips --------------------------------------------------------------------------------
	if (PendingStem.IsEmpty())
	{
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
		UElysiumNpcAnimSubsystem* Anims = GI ? GI->GetSubsystem<UElysiumNpcAnimSubsystem>() : nullptr;
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
	const UElysiumNpcAnimInstance* LayerInst = GetBodyInstance();
	const int32 Composing = LayerInst != nullptr ? LayerInst->GetActiveLayers() : 0;
	if (Composing > 0)
	{
		ImGui::SameLine();
		ImGui::TextColored(ElysiumCogStyle::ColName, "%d composing", Composing);
	}
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
	const UElysiumNpcAnimInstance* Inst = GetBodyInstance();
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

	// The overlays belong to what is being LOOKED at, not to the garment. The skeleton in particular
	// is the model's own rig and reads on any body, including every model the cloth spike never
	// touched — behind the Cloth tab it was unreachable on exactly those, because that tab returns
	// early when no garment rig is installed.
	ImGui::SeparatorText("Overlays");
	const UElysiumNpcAnimInstance* Inst = GetBodyInstance();
	const bool bHasRig = Inst != nullptr && Inst->GetClothRig() != nullptr;

	ImGui::Checkbox("Skeleton", &View.bDrawSkeleton);
	ImGui::TextDisabled("The model's own bones, garment lattice excluded. The bones a collider hangs");
	ImGui::TextDisabled("off are named, so a sphere sitting off its limb reads at a glance.");

	ImGui::BeginDisabled(!bHasRig);
	ImGui::Checkbox("Lattice", &View.bDrawLattice);
	ImGui::SameLine();
	ImGui::Checkbox("Colliders", &View.bDrawColliders);
	ImGui::EndDisabled();
	if (bHasRig)
	{
		ImGui::TextDisabled("Anchor row in blue, simulated rows in amber, leg spheres in red.");
	}
	else
	{
		ImGui::TextDisabled("Both need a garment rig on the standing body - see the Cloth tab.");
	}
}

void FElysiumCogWindow_GreenRoom::RenderCloth(FElysiumGreenRoomRun& Lab)
{
	FElysiumGreenRoomRun::FLabView& View = Lab.LabView();

	// The cvar first: it decides which of the two meshes a body is built from, so it is the one
	// control here that needs the body rebuilt rather than re-tuned.
	if (IConsoleVariable* Cloth = ClothCVar())
	{
		bool bEnabled = Cloth->GetInt() != 0;
		if (ImGui::Checkbox("elysium.Cloth - wear the simulated mesh", &bEnabled))
		{
			Cloth->Set(bEnabled ? 1 : 0, ECVF_SetByConsole);
		}
		ImGui::SameLine();
		ImGui::BeginDisabled(Lab.LabStem().IsEmpty());
		if (ImGui::Button("Restand"))
		{
			// The mesh is chosen when the body is built, so flipping the cvar changes nothing until
			// one is built again. This is that, without going back to the model list.
			Stand(Lab, Lab.LabStem(), Lab.LabClip());
		}
		ImGui::EndDisabled();
	}

	UElysiumNpcAnimInstance* Inst = GetBodyInstance();
	const FElysiumClothRig* Rig = Inst ? Inst->GetClothRig() : nullptr;
	if (Inst == nullptr)
	{
		ImGui::TextDisabled("Nothing is standing on the stage.");
		return;
	}
	if (Rig == nullptr)
	{
		ImGui::TextDisabled("%s wears its faithful rigid mesh - no garment rig is installed.",
			COG_TCHAR_TO_CHAR(*Lab.LabStem()));
		ImGui::TextDisabled("Either the cloth spike never built this model, or elysium.Cloth is 0.");
		ImGui::TextDisabled("  uv run python -m elysium_pipeline.enhancement.cloth_spike");
		return;
	}

	const int32 Chains = Inst->GetResolvedClothChains();
	ImGui::Text("%d chains resolved - %d rows x %d columns - %d colliders",
		Chains, Rig->Rows, Rig->Columns, Rig->Colliders.Num());
	if (Chains == 0)
	{
		// A rig that loaded but resolved nothing is the signature of the enhanced sidecar sitting
		// beside the faithful mesh: the lattice bones the chains name are not on this skeleton.
		ImGui::TextColored(ElysiumCogStyle::ColError,
			"The rig loaded but no chain resolved - this body's skeleton has no lattice.");
	}

	// The tuning follows the body. A different stem means a different rig and a different baseline.
	if (TunedStem != Lab.LabStem())
	{
		TunedStem = Lab.LabStem();
		Tuning = Inst->GetClothTuning();
		Baseline = FElysiumClothTuning::FromRig(*Rig);
	}

	bool bChanged = false;
	const float SliderWidth = -GetDpiScale() * 130.f;

	ImGui::SeparatorText("Solver");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Gravity", &Tuning.Solver.GravityScale, 0.0f, 3.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Follow (velocity)",
		&Tuning.Solver.ComponentLinearVelScale, 0.0f, 2.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Follow (acceleration)",
		&Tuning.Solver.ComponentLinearAccScale, 0.0f, 2.0f, "%.2f");
	ImGui::TextDisabled("Both follow scales at 0 and the garment ignores the character walking.");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderInt("Iterations (pre)", &Tuning.Solver.IterationsPre, 1, 16);
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderInt("Iterations (post)", &Tuning.Solver.IterationsPost, 0, 8);

	ImGui::SeparatorText("Shape");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Cone x", &Tuning.ConeScale, 0.1f, 2.0f, "%.2f");
	ImGui::TextDisabled("Scales the authored ramp - how far a panel may swing off vertical.");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Leg clearance x", &Tuning.ColliderRadiusScale, 0.4f, 2.0f, "%.2f");

	ImGui::SeparatorText("Re-seats the chain");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Linear damping", &Tuning.Solver.LinearDamping, 0.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Angular damping", &Tuning.Solver.AngularDamping, 0.0f, 1.0f, "%.2f");
	ImGui::SetNextItemWidth(SliderWidth);
	bChanged |= ImGui::SliderFloat("Body extent x", &Tuning.BoxExtentScale, 0.25f, 3.0f, "%.2f");
	ImGui::TextDisabled("These three are baked in when a chain initialises, so moving one drops the");
	ImGui::TextDisabled("garment from the pose and lets it settle again. That is the cost, not a bug.");

	if (bChanged)
	{
		Inst->SetClothTuning(Tuning);
		LastNotice.Reset();
	}

	ImGui::SeparatorText("Sidecar");
	const bool bModified = !Tuning.EqualsTuning(Baseline);
	ImGui::BeginDisabled(!bModified);
	if (ImGui::Button("Save to sidecar"))
	{
		FString Error;
		if (ElysiumClothRig::SaveTuning(Lab.LabStem(), Tuning, Error))
		{
			// Deliberately not resetting the scales afterwards. The file now holds the authored
			// values multiplied through, but the rig in memory still holds the originals — zeroing
			// the scales here would snap the garment back to where it started, which is the exact
			// opposite of what saving means.
			Baseline = Tuning;
			LastNotice = FString::Printf(TEXT("wrote %s — a fresh load reproduces this"),
				*FElysiumContentPaths::NpcClothRig(Lab.LabStem()));
			LastError.Reset();
		}
		else
		{
			LastError = Error;
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!bModified);
	if (ImGui::Button("Revert"))
	{
		Tuning = Baseline;
		Inst->SetClothTuning(Tuning);
		LastNotice.Reset();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (bModified)
	{
		ImGui::TextColored(ElysiumCogStyle::ColName, "modified");
	}
	else
	{
		ImGui::TextDisabled("matches the sidecar");
	}
	if (!LastNotice.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(*LastNotice));
	}
	if (!LastError.IsEmpty())
	{
		ImGui::TextColored(ElysiumCogStyle::ColError, "%s", COG_TCHAR_TO_CHAR(*LastError));
	}

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

	if (!ImGui::BeginTabBar("##GreenRoom"))
	{
		return;
	}
	if (ImGui::BeginTabItem("Model"))
	{
		RenderModel(*Lab);
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("Playback"))
	{
		RenderPlayback(*Lab);
		ImGui::EndTabItem();
	}
	if (ImGui::BeginTabItem("View"))
	{
		RenderView(*Lab);
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
