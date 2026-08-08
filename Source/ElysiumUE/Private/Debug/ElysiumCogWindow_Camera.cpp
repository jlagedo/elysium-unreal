#include "Debug/ElysiumCogWindow_Camera.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraModifiers.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerCameraManager.h"

#include "Camera/CameraModifier.h"
#include "GameFramework/PlayerController.h"

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
		"that drive them, the faithful VtMB evaluator's boom beside the modern rig's, the "
		"scripted-shot stack, and the post layers with their alphas. `elysium.ModernCamera` "
		"picks which rig supplies the base view; both solve and record channels every frame "
		"either way, so the delta shown here is the same one the gym records. Read-only.");
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

	// --- the two rigs ---------------------------------------------------------------------------
	ImGui::Separator();
	if (!bFresh)
	{
		ImGui::TextDisabled("The view has not updated this frame (paused, or no possessed body).");
	}

	// Which rig is supplying the base is read from the cvar rather than cached, so this row cannot
	// drift from what the manager actually applied.
	const IConsoleVariable* ModernVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.ModernCamera"));
	const bool bModernBase = ModernVar && ModernVar->GetInt() != 0;

	if (ImGui::BeginTable("rigs", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("");
		ImGui::TableSetupColumn(bModernBase ? "faithful" : "faithful (base)");
		ImGui::TableSetupColumn(bModernBase ? "modern (base)" : "modern");
		ImGui::TableHeadersRow();

		auto Pair = [](const char* Label, const FString& A, const FString& B)
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", Label);
			ImGui::TableNextColumn();
			ImGui::Text("%s", COG_TCHAR_TO_CHAR(*A));
			ImGui::TableNextColumn();
			ImGui::Text("%s", COG_TCHAR_TO_CHAR(*B));
		};

		Pair("boom", FString::Printf(TEXT("%.1f cm"), Sample.BoomLength),
			FString::Printf(TEXT("%.1f cm"), Sample.ModernBoomLength));
		Pair("damper", FString::Printf(TEXT("%.1f cm"), Sample.DamperDistance),
			FString::Printf(TEXT("%.1f cm"), Sample.ModernDamperDistance));
		Pair("pitch", FString::Printf(TEXT("%.2f"), Sample.BoomPitch),
			FString::Printf(TEXT("%.2f"), Sample.ModernBoomPitch));
		Pair("yaw", FString::Printf(TEXT("%.2f"), Sample.BoomYaw),
			FString::Printf(TEXT("%.2f"), Sample.ModernBoomYaw));
		Pair("clipped", Sample.bClipped ? TEXT("yes") : TEXT("no"),
			Sample.bModernClipped ? TEXT("yes") : TEXT("no"));

		ImGui::EndTable();
	}

	// The delta is the co-tune's own number, and the same one the gym records as `cam_*` against
	// `mcam_*` — so what is read here and what a channel diff reports cannot disagree.
	ImGui::TextDisabled("boom delta %+.1f cm   (elysium.ModernCamera %d)",
		Sample.ModernBoomLength - Sample.BoomLength, bModernBase ? 1 : 0);

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
