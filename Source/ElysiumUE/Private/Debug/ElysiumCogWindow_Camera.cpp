#include "Debug/ElysiumCogWindow_Camera.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraModifiers.h"
#include "ElysiumCameraService.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerCameraManager.h"

#include "Camera/CameraModifier.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "imgui.h"

void FElysiumCogWindow_Camera::Initialize()
{
	Super::Initialize();

	// Read-only: the rig is driven by the cvar surface and the verbs, not from here.
	bHasMenu = false;
}

void FElysiumCogWindow_Camera::RenderHelp()
{
	ImGui::Text(
		"Live camera service state: the third-person / scripted / feed weights and the latches "
		"that drive them, the rig's boom, the scripted-shot stack, and the post layers with their "
		"alphas. The boom shown here is the one the gym records as `cam_*`. Read-only.");
}

void FElysiumCogWindow_Camera::RenderContent()
{
	Super::RenderContent();

	const APlayerController* PC = GetLocalPlayerController();
	const AElysiumPlayerCameraManager* Manager = PC
		? Cast<AElysiumPlayerCameraManager>(PC->PlayerCameraManager) : nullptr;
	if (Manager == nullptr)
	{
		ImGui::TextDisabled("No Elysium camera manager on the local player.");
		return;
	}

	IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(PC->GetPawn());
	const UElysiumCameraComponent* Camera = Body ? Body->GetCameraComponent() : nullptr;

	const float ValueColumn = GetDpiScale() * 130.0f;
	auto Row = [ValueColumn](const char* Label, const FString& Value)
	{
		ElysiumCogStyle::LabelValue(Label, COG_TCHAR_TO_CHAR(*Value), ValueColumn);
	};

	const FElysiumCameraSample& Sample = Manager->GetCameraSample();
	const bool bFresh = Sample.Frame == GFrameCounter || Sample.Frame + 1 == GFrameCounter;

	// --- the mode -------------------------------------------------------------------------------
	if (Camera)
	{
		const FElysiumCameraWeights& W = Camera->GetWeights();
		ImGui::TextColored(W.IsThirdPerson() ? ElysiumCogStyle::ColName : ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
			"%s", W.IsThirdPerson() ? "third person" : "first person");
		ImGui::SameLine();
		ImGui::TextDisabled("driver: %s", COG_TCHAR_TO_CHAR(W.Driver()));
		ImGui::Separator();

		Row("Third", FString::Printf(TEXT("%.3f  (eased %.3f)"), W.Third, W.ThirdBlend()));
		Row("Scripted", FString::Printf(TEXT("%.3f"), W.Scripted));
		Row("Secondary", FString::Printf(TEXT("%.3f"), W.Secondary));
		Row("Feed", FString::Printf(TEXT("%.3f"), W.Feed));
		Row("Latches", FString::Printf(TEXT("user %s · forced3 %s · forced1 %s · feed %s"),
			W.bUserThird ? TEXT("on") : TEXT("off"),
			W.bForcedThird ? TEXT("on") : TEXT("off"),
			W.bForcedFirst ? TEXT("on") : TEXT("off"),
			W.bFeed ? TEXT("on") : TEXT("off")));
		Row("Model alpha", FString::Printf(TEXT("%.3f"), Camera->ModelAlpha()));
	}
	else
	{
		ImGui::TextDisabled("The view target is not a player body.");
	}

	// --- the rig --------------------------------------------------------------------------------
	ImGui::Separator();
	if (!bFresh)
	{
		ImGui::TextDisabled("The view has not updated this frame (paused, or no possessed body).");
	}

	if (ImGui::BeginTable("rig", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn("boom");
		ImGui::TableHeadersRow();

		auto BoomRow = [](const char* Label, const FString& Value)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", Label);
			ImGui::TableNextColumn();
			ImGui::Text("%s", COG_TCHAR_TO_CHAR(*Value));
		};

		BoomRow("boom", FString::Printf(TEXT("%.1f cm"), Sample.BoomLength));
		BoomRow("damper", FString::Printf(TEXT("%.1f cm"), Sample.DamperDistance));
		BoomRow("pitch", FString::Printf(TEXT("%.2f"), Sample.BoomPitch));
		BoomRow("yaw", FString::Printf(TEXT("%.2f"), Sample.BoomYaw));
		BoomRow("clipped", Sample.bClipped ? TEXT("yes") : TEXT("no"));

		ImGui::EndTable();
	}

	// --- the scripted stack ---------------------------------------------------------------------
	if (Camera && ImGui::CollapsingHeader("Scripted shots"))
	{
		const FElysiumCameraShotStack& Shots = Camera->GetShots();
		if (Shots.Num() == 0)
		{
			ImGui::TextDisabled("%s", Shots.GetWeight() > 0.0f
				? "no shot up; the channel is ramping out" : "no shot up.");
		}
		Row("Weight", FString::Printf(TEXT("%.3f"), Shots.GetWeight()));
		Row("Depth", FString::Printf(TEXT("%d"), Shots.Num()));
		if (const FElysiumCameraShot* Top = Shots.Top())
		{
			Row("Top", Top->DebugName.IsEmpty() ? TEXT("(unnamed)") : Top->DebugName);
			Row("Blend", FString::Printf(TEXT("%.2f s%s"), Top->BlendSeconds,
				Top->bCameraCut ? TEXT("  (cut)") : TEXT("")));
			Row("FOV", Top->FieldOfView > 0.0f
				? FString::Printf(TEXT("%.1f"), Top->FieldOfView) : FString(TEXT("(player's)")));
		}
	}

	// --- scoped director requests ---------------------------------------------------------------
	if (ImGui::CollapsingHeader("Director requests", ImGuiTreeNodeFlags_DefaultOpen))
	{
		const ULocalPlayer* LocalPlayer = PC->GetLocalPlayer();
		const UElysiumCameraService* Service = LocalPlayer
			? LocalPlayer->GetSubsystem<UElysiumCameraService>() : nullptr;
		if (!Service)
		{
			ImGui::TextDisabled("No local-player camera service.");
		}
		else
		{
			const FElysiumResolvedCameraState& Resolved = Service->ResolvedCamera();
			Row("Epoch", FString::Printf(TEXT("%llu"), Service->CurrentEpoch()));
			Row("Winner", Resolved.Request.DebugName.IsEmpty()
				? TEXT("player view") : Resolved.Request.DebugName);
			Row("Weight", FString::Printf(TEXT("%.3f"), Resolved.Weight));
			Row("Source shot", Resolved.Request.SourceShot.IsEmpty()
				? TEXT("(none)") : Resolved.Request.SourceShot);
			Row("Profile", Resolved.Request.SelectedProfile.IsEmpty()
				? TEXT("(none)") : Resolved.Request.SelectedProfile);
			Row("Fallback", Resolved.Request.FallbackReason.IsEmpty()
				? TEXT("(none)") : Resolved.Request.FallbackReason);
			Row("Control", FString::FromInt(static_cast<int32>(Resolved.Request.Control)));
			Row("Pose", FString::Printf(TEXT("%s  %s  fov %.1f"),
				*Resolved.Location.ToCompactString(), *Resolved.Rotation.ToCompactString(),
				Resolved.FieldOfView));
			TArray<FString> Requests;
			Service->DescribeRequests(Requests);
			for (const FString& Request : Requests)
			{
				ImGui::BulletText("%s", COG_TCHAR_TO_CHAR(*Request));
			}
		}
	}

	if (ImGui::CollapsingHeader("Dialogue director", ImGuiTreeNodeFlags_DefaultOpen))
	{
		if (const FElysiumEntityWorld* EntityWorld = GetEntityWorld())
		{
			TArray<TPair<FString, FString>> DialogueRows;
			EntityWorld->GetDialogueDebugState(DialogueRows);
			for (const TPair<FString, FString>& DialogueRow : DialogueRows)
			{
				Row(COG_TCHAR_TO_CHAR(*DialogueRow.Key), DialogueRow.Value);
			}
		}
		else
		{
			ImGui::TextDisabled("No entity world.");
		}
	}

	// --- the post layers ------------------------------------------------------------------------
	if (ImGui::CollapsingHeader("Post layers"))
	{
		// The same list `showdebug camera` walks, in application order — priority 0 first.
		for (const UCameraModifier* Modifier : Manager->GetModifiers())
		{
			if (Modifier == nullptr)
			{
				continue;
			}
			const bool bElysium = Modifier->IsA<UElysiumCameraModifier>();
			ImGui::TextColored(Modifier->IsDisabled()
					? ImVec4(0.58f, 0.58f, 0.58f, 1.0f)
					: (bElysium ? ElysiumCogStyle::ColName : ImVec4(0.8f, 0.8f, 0.8f, 1.0f)),
				"%s", COG_TCHAR_TO_CHAR(*Modifier->GetName()));
			ImGui::SameLine();
			ImGui::TextDisabled("priority %d%s", Modifier->Priority,
				Modifier->IsDisabled() ? "  (disabled)" : "");
		}
	}
}

#endif // ENABLE_COG
