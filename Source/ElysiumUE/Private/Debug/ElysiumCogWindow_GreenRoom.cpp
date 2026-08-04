#include "Debug/ElysiumCogWindow_GreenRoom.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "Debug/ElysiumGreenRoomRun.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumNpcSubsystem.h"
#include "Visual/ElysiumNpcAnimInstance.h"
#include "Visual/ElysiumNpcAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

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
		"session to stand a stage up wherever you are.\n\n"
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
	if (UElysiumMapSubsystem* Maps = GetMapSubsystem())
	{
		Maps->EnsureGreenRoomLab(LastError);
	}
	else
	{
		LastError = TEXT("no map subsystem — load a map first");
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

void FElysiumCogWindow_GreenRoom::RenderModel(FElysiumGreenRoomRun& Lab)
{
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
			for (const FString& Label : Clips)
			{
				const FString Cell = Anims->ResolveClipAnimName(PendingStem, Label);
				ClipCells.Add(Cell.Equals(Label, ESearchCase::IgnoreCase) ? FString() : Cell);
				const FElysiumNpcClip* Clip = Set->Find(Label);
				ClipAdditive.Add(Clip != nullptr && Clip->IsAdditive());
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
	ImGui::BeginChild("##Clips", ImVec2(0, GetDpiScale() * 140.f), ImGuiChildFlags_Borders);
	for (int32 Index = 0; Index < Clips.Num(); ++Index)
	{
		if (!ClipFilter.IsEmpty() && !Clips[Index].Contains(ClipFilter))
		{
			continue;
		}
		const bool bGrid = ClipCells.IsValidIndex(Index) && !ClipCells[Index].IsEmpty();
		const bool bAdditive = ClipAdditive.IsValidIndex(Index) && ClipAdditive[Index];
		ImGui::PushID(Index);
		FString Row = bGrid
			? FString::Printf(TEXT("%s  -> %s"), *Clips[Index], *ClipCells[Index])
			: Clips[Index];
		if (bAdditive)
		{
			// Named on the row, not hidden from the list: standing one of these is the fastest way
			// to see what an additive layer actually contains, and the only thing worth preventing
			// is mistaking the result for a broken model.
			Row += TEXT("   [additive layer]");
			ImGui::PushStyleColor(ImGuiCol_Text, ElysiumCogStyle::ColError);
		}
		if (ImGui::Selectable(COG_TCHAR_TO_CHAR(*Row), PendingClip == Clips[Index]))
		{
			bUserPicked = true;
			PendingClip = Clips[Index];
			Stand(Lab, PendingStem, Clips[Index]);
		}
		if (bAdditive)
		{
			ImGui::PopStyleColor();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();
	ImGui::TextDisabled("-> = a blend grid, showing the cell the pose parameters select.");
	ImGui::TextDisabled("[additive layer] = a delta on top of a base pose, not a pose. Standing one");
	ImGui::TextDisabled("alone shows the difference itself, which folds the skeleton up.");
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

void FElysiumCogWindow_GreenRoom::RenderCamera(FElysiumGreenRoomRun& Lab)
{
	FElysiumGreenRoomRun::FLabView& View = Lab.LabView();
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

	ImGui::SeparatorText("Overlays");
	ImGui::Checkbox("Lattice", &View.bDrawLattice);
	ImGui::SameLine();
	ImGui::Checkbox("Colliders", &View.bDrawColliders);
	ImGui::SameLine();
	ImGui::Checkbox("Skeleton", &View.bDrawSkeleton);
	ImGui::TextDisabled("Anchor row in blue, simulated rows in amber, leg spheres in red.");
	ImGui::TextDisabled("Skeleton is the model's own bones, lattice excluded; the bones a collider");
	ImGui::TextDisabled("hangs off are named, so a sphere sitting off its limb reads at a glance.");
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
		ImGui::TextDisabled("Builds a neutral stage and moves the camera onto it. `elysium.gr` does");
		ImGui::TextDisabled("the same from the console; `uv run elysium gr` launches straight into it.");
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
	if (ImGui::BeginTabItem("Camera"))
	{
		RenderCamera(*Lab);
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
