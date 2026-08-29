#include "Debug/ElysiumCogLocomotionRow.h"

#if ENABLE_COG

#include "Debug/ElysiumCogStyle.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumGraphState.h"
#include "ElysiumLocomotionSample.h"

#include "CogLocalizationConfig.h"   // COG_TCHAR_TO_CHAR
#include "imgui.h"

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

	// An outcome is a verdict, so it is coloured like one: a clean resolve reads plain, a fallback
	// reads as a warning, and the four that mean the catalog is wrong read as errors.
	ImVec4 OutcomeColor(EElysiumAnimOutcome Outcome)
	{
		switch (Outcome)
		{
		case EElysiumAnimOutcome::Resolved:
			return ElysiumCogStyle::ColOk;
		case EElysiumAnimOutcome::MissingSequence:
		case EElysiumAnimOutcome::MaskedRejected:
		case EElysiumAnimOutcome::LayerMaskRejected:
		case EElysiumAnimOutcome::GridStateRefused:
			return ElysiumCogStyle::ColError;
		default:
			return ElysiumCogStyle::ColWarn;
		}
	}
}

void ElysiumCogLocomotion::SetupColumns()
{
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
	ImGui::TableSetupColumn("Activity");
	ImGui::TableSetupColumn("State");
	ImGui::TableSetupColumn("Label");
	ImGui::TableSetupColumn("Owner bank");
	ImGui::TableSetupColumn("Asset");
	ImGui::TableSetupColumn("Outcome");
	ImGui::TableSetupColumn("Base hold");
}

// The six selection columns, and `Owner bank` is the one that carries the acceptance
// visually: the player's row reads its PC-only bank while every cast row reads the shared one, side
// by side, out of one function. `State` is read off the record rather than projected here, so a row
// that disagrees with the pose on screen is the resolver's answer being wrong and never this
// window's arithmetic.
void ElysiumCogLocomotion::Row(const char* Producer, const char* Name,
	const FElysiumLocomotionSample& S, const FElysiumAnimationSelection* Sel)
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

	if (Sel == nullptr)
	{
		for (int32 Column = 0; Column < 7; ++Column)
		{
			ImGui::TableNextColumn(); ImGui::TextDisabled("--");
		}
		return;
	}

	ImGui::TableNextColumn();
	if (Sel->ResolvedActivity.IsEmpty()) { ImGui::TextDisabled("--"); }
	else { ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Sel->ResolvedActivity)); }
	// The translation hops the resolver walked, on the column where the final answer
	// already reads: which rung fired is the difference between "the weapon named this pose" and
	// "the body fell back to what it had", and the hover is where a reader asks.
	if (!Sel->RequestedActivity.IsEmpty() && ImGui::IsItemHovered())
	{
		const FString Chain = FString::Printf(
			TEXT("requested %s\npre-translation %s\nclass %s\nweapon %s (ladder rung %d)\n")
			TEXT("resolved %s (availability rung %d)"),
			*Sel->RequestedActivity,
			Sel->PreTranslationActivity.IsEmpty() ? TEXT("--") : *Sel->PreTranslationActivity,
			Sel->ClassActivity.IsEmpty() ? TEXT("--") : *Sel->ClassActivity,
			Sel->WeaponActivity.IsEmpty() ? TEXT("--") : *Sel->WeaponActivity,
			Sel->WeaponRung,
			*Sel->ResolvedActivity, Sel->AvailabilityRung);
		ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*Chain));
	}

	ImGui::TableNextColumn();
	ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(ElysiumAnimGraph::StateName(Sel->GraphState)));

	ImGui::TableNextColumn();
	if (Sel->SequenceLabel.IsEmpty()) { ImGui::TextDisabled("--"); }
	else { ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(*Sel->SequenceLabel)); }

	ImGui::TableNextColumn();
	if (Sel->OwnerStem.IsEmpty()) { ImGui::TextDisabled("--"); }
	else { ImGui::TextColored(ElysiumCogStyle::ColName, "%s", COG_TCHAR_TO_CHAR(*Sel->OwnerStem)); }

	ImGui::TableNextColumn();
	ImGui::TextUnformatted(COG_TCHAR_TO_CHAR(ElysiumAnimIntent::AssetKindName(Sel->AssetKind)));
	// The pair a fan actually evaluates, where the reader is already looking at what shape
	// the asset is. A row naming one cell reads as a snap, and a snap is a different pose from the
	// two-cell mix the graph strikes.
	if (!Sel->AnimationName.IsEmpty() && ImGui::IsItemHovered())
	{
		const FString Pair = Sel->NextAnimationName.IsEmpty()
			? FString::Printf(TEXT("%s"), *Sel->AnimationName)
			: FString::Printf(TEXT("%s -> %s (%.0f%%)"), *Sel->AnimationName,
				*Sel->NextAnimationName, Sel->AxisFraction[0] * 100.0f);
		ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*Pair));
	}

	ImGui::TableNextColumn();
	ImGui::TextColored(OutcomeColor(Sel->Outcome), "%s",
		COG_TCHAR_TO_CHAR(ElysiumAnimIntent::OutcomeName(Sel->Outcome)));
	// The one line naming what missed, where a reader is already looking at the verdict.
	if (!Sel->Detail.IsEmpty() && ImGui::IsItemHovered())
	{
		ImGui::SetTooltip("%s", COG_TCHAR_TO_CHAR(*Sel->Detail));
	}

	// The base-channel arbitration verdict. A held frame is correct behaviour that looks
	// exactly like a stuck pose, so the row has to name the holder rather than leave a reader to
	// guess which one is on screen.
	ImGui::TableNextColumn();
	if (Sel->bBasePoseOwned) { ImGui::TextDisabled("--"); }
	else
	{
		// The age rides beside the holder: a scene mid-performance reads its own running time, a
		// leaked claim only grows — which is the difference between correct and stuck.
		ImGui::TextColored(ElysiumCogStyle::ColName, "%s %.1fs",
			COG_TCHAR_TO_CHAR(*Sel->BaseHold), Sel->BaseHoldSeconds);
	}
}

#endif // ENABLE_COG
