#include "Debug/ElysiumGreenRoomConsole.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaScenario.h"         // gr_scenario: the records
#include "Debug/ElysiumArenaScenarioRunner.h"   // gr_scenario: the run, as the headless suite runs it
#include "Debug/ElysiumArenaSpec.h"
#include "Debug/ElysiumArenaStage.h"            // gr_scenario's staging and gr_los's seat
#include "Debug/ElysiumGreenRoomRun.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumNpc.h"   // gr_hints --validate: the dry run and the debug NPC
#include "Substrate/ElysiumPlaceSet.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGreenRoomCmd, Log, All);

namespace
{
	// Where the lab writes a scenario's trace: beside the headless run's default report, under its own
	// name so the two never read as one run.
	FString LabScenarioDir()
	{
		return FElysiumContentPaths::SavedDebugDir(TEXT("_arena")) / TEXT("lab");
	}

	// Console arguments are text. These read one with a stated default rather than silently taking 0
	// for a typo, because a slider driven to an accidental zero looks like a working control.
	float Arg(const TArray<FString>& Args, int32 Index, float Fallback)
	{
		return Args.IsValidIndex(Index) ? FCString::Atof(*Args[Index]) : Fallback;
	}

	bool ArgBool(const TArray<FString>& Args, int32 Index, bool Fallback)
	{
		if (!Args.IsValidIndex(Index))
		{
			return Fallback;
		}
		const FString& Text = Args[Index];
		return Text == TEXT("1") || Text.StartsWith(TEXT("t")) || Text.StartsWith(TEXT("y"))
			|| Text.StartsWith(TEXT("on"));
	}

	// `f`/`female` selects the female row; anything else is male, which is the shipped default for a
	// caller that does not say.
	bool ArgFemale(const TArray<FString>& Args, int32 Index)
	{
		return Args.IsValidIndex(Index) && Args[Index].StartsWith(TEXT("f"));
	}

	// --- `elysium.gr_hints --validate [npc]` -------------------------------------------------------
	// The tactical cover search's admission, asked of every hint on the list without running the
	// search: `FElysiumNpcBase::DryRunHintByClassMask` puts each hint through `FindHintByClassMask`'s
	// own gates and answers what the walk would return, writing neither the cursor nor a claim.

	// `0x102b7110`'s flags byte (`102b7134 PUSH 0x8`: bit 3, score by distance x rating).
	constexpr uint8 ValidateSearchFlags = 0x8;
	// The mask: `0x102b7110` passes its caller's search type WHOLE and the port records no last
	// selector call, so the cover band (`0x102d2940`'s mask 1: types 100 / 101 / 0x27d8) stands in.
	constexpr int32 ValidateDefaultMask = 1;

	const TCHAR* AdmissionGateName(FElysiumNpcBase::EHintAdmissionGate Gate)
	{
		switch (Gate)
		{
		case FElysiumNpcBase::EHintAdmissionGate::Admitted:         return TEXT("admitted");
		case FElysiumNpcBase::EHintAdmissionGate::NotLive:          return TEXT("not live");
		case FElysiumNpcBase::EHintAdmissionGate::Unusable:         return TEXT("unusable (0x102d14c0)");
		case FElysiumNpcBase::EHintAdmissionGate::ClassMask:        return TEXT("class mask");
		case FElysiumNpcBase::EHintAdmissionGate::Distance:         return TEXT("distance");
		case FElysiumNpcBase::EHintAdmissionGate::ValidateHintType: return TEXT("slot 566 FValidateHintType");
		case FElysiumNpcBase::EHintAdmissionGate::Trace:            return TEXT("eye trace");
		default:                                                    return TEXT("?");
		}
	}

	// Why the row stopped where it did, in the words the gate reads.
	FString AdmissionGateReason(const FElysiumEntityWorld& EW, const FElysiumNpc& Npc,
		const FElysiumNpcBase::FHintAdmissionRow& Row, const FElysiumNpcBase::FHintWords& Words,
		int32 Mask, float RadiusUnits)
	{
		const FElysiumAiDebugHintProbe& Probe = EW.AiDebugHintProbe();
		switch (Row.Gate)
		{
		case FElysiumNpcBase::EHintAdmissionGate::NotLive:
			return TEXT("the list entry names no live ai_hint");
		case FElysiumNpcBase::EHintAdmissionGate::Unusable:
			if (Words.Disabled != 0)
			{
				return TEXT("m_iDisabled");
			}
			if (static_cast<float>(EW.NowSeconds()) < Words.NextUseTime)
			{
				return FString::Printf(TEXT("m_flNextUseTime %.2f > curtime %.2f"), Words.NextUseTime,
					EW.NowSeconds());
			}
			return FString::Printf(TEXT("owned by %s"), *EW.DescribeHandle(Words.HintOwner));
		case FElysiumNpcBase::EHintAdmissionGate::ClassMask:
			return FString::Printf(TEXT("mask %d & hint class %d == 0"), Mask, Words.ClassMask);
		case FElysiumNpcBase::EHintAdmissionGate::Distance:
			return FString::Printf(TEXT("d %.0f >= radius %.0f"), Row.DistanceUnits, RadiusUnits);
		case FElysiumNpcBase::EHintAdmissionGate::ValidateHintType:
			if (Probe.HintIndex == Row.HintIndex && !Probe.Reason.IsEmpty())
			{
				return FString::Printf(TEXT("%s: %s"), Probe.Validator != nullptr ? Probe.Validator : TEXT("?"),
					*Probe.Reason);
			}
			if ((static_cast<uint32>(Words.GroupMask) & Npc.ScheduleHost.HintGroupMask) == 0)
			{
				return FString::Printf(TEXT("hint group %d vs m_iHintGroups 0x%08x"), Words.GroupMask,
					Npc.ScheduleHost.HintGroupMask);
			}
			return FString::Printf(TEXT("type %d refused, no reason string"), Words.HintType);
		case FElysiumNpcBase::EHintAdmissionGate::Trace:
			return TEXT("eye trace blocked");
		default:
			return FString();
		}
	}

	void ValidateHints(FElysiumEntityWorld& EW, const FString& NpcArg)
	{
		FElysiumEntity* Entity = EW.FindByName(NpcArg);
		int32 Index = INDEX_NONE;
		if (Entity == nullptr && LexTryParseString(Index, *NpcArg) && EW.Entities().IsValidIndex(Index))
		{
			Entity = EW.Entities()[Index].Get();
		}
		FElysiumNpc* Npc = Entity != nullptr ? Entity->AsNpc() : nullptr;
		if (Npc == nullptr)
		{
			UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_hints --validate: no NPC named or indexed '%s'"),
				*NpcArg);
			return;
		}
		const float RadiusUnits = Npc->CoverRadius();   // slot 550, `102b7125 CALL [EAX+0x898]`
		FElysiumEntity* Enemy = Npc->GetEnemy();
		UE_LOG(LogElysiumGreenRoomCmd, Display,
			TEXT("gr_hints --validate: %s  flags 0x%x (0x102b7110)  mask %d (last selector mask not ")
			TEXT("recorded; 1 = the cover band)  radius %.0f (CoverRadius)  curtime %.2f"),
			*Npc->DebugString(), ValidateSearchFlags, ValidateDefaultMask, RadiusUnits, EW.NowSeconds());
		UE_LOG(LogElysiumGreenRoomCmd, Display,
			TEXT("  enemy %s  m_hHintCoverObject %s  bStayEntrenched %d  m_pHintNode %d  cursor %d"),
			Enemy != nullptr ? *Enemy->DebugString() : TEXT("none"),
			*EW.DescribeHandle(Npc->ScheduleHost.HintCoverObject), Npc->bStayEntrenched ? 1 : 0,
			Npc->BaseScheduleHost.HintNode, EW.HintCursor());

		// For the run only: this NPC is the `ai_debug_npc` (so the validators format and record), and
		// `m_bForceCoverLOSCheck` stands as `0x102b7110` raises it around its first search. Slot 566's
		// refusal counter is a port test tally, restored too. All three are put back afterwards.
		const FElysiumEntityHandle SavedDebugNpc = EW.AiDebugNpc();
		const bool bSavedForceLos = Npc->ScheduleHost.bForceCoverLosCheck;
		const int32 SavedGroupRefusals = Npc->HintGroupRefusals;
		EW.SetAiDebugNpc(Npc->Handle);
		Npc->ScheduleHost.bForceCoverLosCheck = true;
		EW.AiDebugHintProbe().Reset();

		const FElysiumNpc& ConstNpc = *Npc;
		const int32 Winner = ConstNpc.DryRunHintByClassMask(ValidateSearchFlags, ValidateDefaultMask,
			RadiusUnits, [&EW, &ConstNpc, RadiusUnits](const FElysiumNpcBase::FHintAdmissionRow& Row)
		{
			FElysiumNpcBase::FHintWords Words;
			const bool bLive = ConstNpc.HintWords(Row.HintIndex, Words);
			const FString Verdict = Row.Gate == FElysiumNpcBase::EHintAdmissionGate::Admitted
				? FString(TEXT("admitted"))
				: FString::Printf(TEXT("refused at %s (%s)"), AdmissionGateName(Row.Gate),
					*AdmissionGateReason(EW, ConstNpc, Row, Words, ValidateDefaultMask, RadiusUnits));
			const FString ScoreText = Row.bScored ? FString::Printf(TEXT("  score %.2f"), Row.Score) : FString();
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  #%d %-16s type %d  mask %d  node %d  -> %s%s"),
				Row.HintIndex, !bLive ? TEXT("-") : Words.Name.IsEmpty() ? TEXT("(unnamed)") : *Words.Name,
				Words.HintType, Words.ClassMask, Words.NodeId, *Verdict, *ScoreText);
			// What the validator computed, for authoring the arena's geometry from it.
			const FElysiumAiDebugHintProbe& Probe = EW.AiDebugHintProbe();
			if (Probe.HintIndex == Row.HintIndex && Probe.Validator != nullptr)
			{
				FString Numbers = FString::Printf(TEXT("     [%s]"), Probe.Validator);
				if (Probe.bDistance)
				{
					Numbers += FString::Printf(TEXT(" dist %.1f"), Probe.DistanceUnits);
				}
				if (Probe.bEnemyProjection)
				{
					Numbers += FString::Printf(TEXT("  projection %.2f (>= 0.2)"), Probe.EnemyProjection);
				}
				if (Probe.bFacing)
				{
					Numbers += FString::Printf(TEXT("  facing %.2f (> good %.2f"), Probe.FacingProjection,
						Probe.GoodRange);
					Numbers += Probe.bBadRange ? FString::Printf(TEXT(", < bad %.2f)"), Probe.BadRange)
						: FString(TEXT(")"));
				}
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("%s"), *Numbers);
			}
			EW.AiDebugHintProbe().Reset();
		});

		EW.SetAiDebugNpc(SavedDebugNpc);
		Npc->ScheduleHost.bForceCoverLosCheck = bSavedForceLos;
		Npc->HintGroupRefusals = SavedGroupRefusals;
		EW.AiDebugHintProbe().Reset();
		const FString WinnerText = Winner != INDEX_NONE ? FString::Printf(TEXT("#%d"), Winner) : FString(TEXT("none"));
		UE_LOG(LogElysiumGreenRoomCmd, Display,
			TEXT("  -> the walk would return %s (cursor not written, nothing claimed)"), *WinnerText);
	}
}

FElysiumGreenRoomConsole::FElysiumGreenRoomConsole(UElysiumMapSubsystem* InOwner)
	: Owner(InOwner)
{
	// The body.

	Register(TEXT("elysium.gr_stand"),
		TEXT("Stand a body: `elysium.gr_stand malkavian_female_armor_0 katana_idle`. "
		     "With no clip the model's own idle policy picks one."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_stand: name a model stem."));
				return;
			}
			FString Error;
			if (Run.LabSetBody(Args[0], Args.Num() > 1 ? Args[1] : FString(), Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_stand: %s on %s"),
					*Run.LabStem(), *Run.LabClip());
			}
			else
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_stand failed: %s"), *Error);
			}
		});

	Register(TEXT("elysium.gr_restand"),
		TEXT("Rebuild the standing body, discarding the map's cached meshes and clips first. "
		     "This is the door a re-export needs."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>&)
		{
			FString Error;
			if (Run.LabRestand(Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_restand: %s on %s"),
					*Run.LabStem(), *Run.LabClip());
			}
			else
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_restand failed: %s"), *Error);
			}
		});

	// The clip vocabulary.

	Register(TEXT("elysium.gr_clips"),
		TEXT("List the standing body's clip labels, optionally filtered: `elysium.gr_clips katana`. "
		     "A well-connected body resolves over a thousand, so an unfiltered list is capped."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			const UGameInstance* GI = Run.LabBody() != nullptr && Run.LabBody()->GetWorld() != nullptr
				? Run.LabBody()->GetWorld()->GetGameInstance() : nullptr;
			UElysiumAnimSubsystem* Anims = GI != nullptr
				? GI->GetSubsystem<UElysiumAnimSubsystem>() : nullptr;
			const FElysiumNpcClipSet* Set = Anims != nullptr
				? Anims->GetClipSet(Run.LabStem()) : nullptr;
			if (Set == nullptr)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_clips: '%s' resolves no clip vocabulary."), *Run.LabStem());
				return;
			}
			const FString Filter = Args.Num() > 0 ? Args[0] : FString();
			TArray<FString> Hits;
			Set->Clips.ForEachClip([&Hits, &Filter](const FString& Label, const FElysiumNpcClip& Clip)
			{
				// The label is the KEY — a clip carries its owner and activity, not its own name —
				// and several banks may declare one label, so the owner is printed beside it.
				if (Filter.IsEmpty() || Label.Contains(Filter))
				{
					Hits.Add(FString::Printf(TEXT("%s  [%s]"), *Label, *Clip.Owner));
				}
			});
			Hits.Sort();
			// Capped rather than truncated silently: the count says what was withheld, so a filter
			// that matched more than it showed reads as a filter to tighten, not as the whole answer.
			const int32 Cap = 60;
			for (int32 Index = 0; Index < FMath::Min(Hits.Num(), Cap); ++Index)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  %s"), *Hits[Index]);
			}
			UE_LOG(LogElysiumGreenRoomCmd, Display,
				TEXT("gr_clips: %d match%s%s (of %d) — showing %d"),
				Hits.Num(), Hits.Num() == 1 ? TEXT("") : TEXT("es"),
				Filter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" for '%s'"), *Filter),
				Set->Clips.Num(), FMath::Min(Hits.Num(), Cap));
		});

	// Layers, grids and aim.

	Register(TEXT("elysium.gr_layer"),
		TEXT("Lay an autolayer over the standing body: `elysium.gr_layer glock_aim_layer 1.0`. "
		     "With no argument, clears every layer."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				Run.LabClearLayers();
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_layer: cleared."));
				return;
			}
			FString Error;
			if (Run.LabSetLayer(Args[0], Arg(Args, 1, 1.0f), Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_layer: %s riding at %.2f (%s)"),
					*Args[0], Arg(Args, 1, 1.0f), *Run.LabLayerArmed());
			}
			else
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_layer failed: %s (%s)"),
					*Error, *Run.LabLayerArmed());
			}
		});

	Register(TEXT("elysium.gr_aim"),
		TEXT("Steer an armed aim grid, in the pose parameters' own degrees: "
		     "`elysium.gr_aim <yaw> <pitch>`."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			Run.LabSetAimFollowsLook(false);
			Run.LabSetLayerAim(Arg(Args, 0, 0.0f), Arg(Args, 1, 0.0f));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_aim: yaw %.1f pitch %.1f"),
				Run.LabLayerAimYaw(), Run.LabLayerAimPitch());
		});

	Register(TEXT("elysium.gr_grid"),
		TEXT("Stand the body on a label's whole blend grid: `elysium.gr_grid walk`. "
		     "Optional second argument is the graph state (`Idle`, `Crouch`, `Leap`, `Falling`, "
		     "`Land`, `Walk`, `Run`, `Sneak`); default Walk. With no argument, clears it."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				Run.LabClearGrid();
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_grid: cleared."));
				return;
			}
			EElysiumGraphState State = EElysiumGraphState::Walk;
			if (Args.Num() >= 2 && !ElysiumAnimGraph::TryParseState(Args[1], State))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_grid: unknown state '%s' (Idle Walk Run Sneak Crouch Leap Falling Land)"),
					*Args[1]);
				return;
			}
			FString Error;
			if (Run.LabSetGrid(Args[0], Error, State))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_grid: %s in %s (%s)"),
					*Args[0], ElysiumAnimGraph::StateName(State), *Run.LabGridArmed());
			}
			else
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_grid failed: %s (%s)"),
					*Error, *Run.LabGridArmed());
			}
		});

	Register(TEXT("elysium.gr_gridat"),
		TEXT("Move the blend grid's sample point: `elysium.gr_gridat <axis0> <axis1>`."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			Run.LabSetGridPosition(Arg(Args, 0, 0.0f), Arg(Args, 1, 0.0f));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_gridat: %.2f, %.2f"),
				Run.LabGridAxis(0), Run.LabGridAxis(1));
		});

	// Playback.

	Register(TEXT("elysium.gr_time"),
		TEXT("Seek the standing clip to an absolute time in seconds: `elysium.gr_time 0.5`."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			Run.LabSetTime(Arg(Args, 0, 0.0f));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_time: %.3f / %.3f s"),
				Run.LabTime(), Run.LabDuration());
		});

	Register(TEXT("elysium.gr_pause"),
		TEXT("Pause or resume playback: `elysium.gr_pause 1`. With no argument, toggles."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			FElysiumGreenRoomRun::FLabView& View = Run.LabView();
			View.bPaused = Args.Num() > 0 ? ArgBool(Args, 0, true) : !View.bPaused;
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_pause: %s"),
				View.bPaused ? TEXT("paused") : TEXT("running"));
		});

	Register(TEXT("elysium.gr_speed"),
		TEXT("Playback rate multiplier: `elysium.gr_speed 0.25`."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			Run.LabView().Speed = FMath::Max(0.0f, Arg(Args, 0, 1.0f));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_speed: %.2fx"), Run.LabView().Speed);
		});

	// The view.
	//
	// The one control a screenshot-driven caller cannot do without. A weapon in a hand is a few
	// centimetres of a body-sized frame, and whether it is held or merely near the hand is a question
	// the default orbit distance cannot answer at all.

	Register(TEXT("elysium.gr_view"),
		TEXT("Orbit the stage camera: `elysium.gr_view <yaw> <pitch> [distance] [lookheight 0..1]`. "
		     "Distance is a multiple of the automatic fit — 0.3 is a close-up."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			FElysiumGreenRoomRun::FLabView& View = Run.LabView();
			View.OrbitYaw = Arg(Args, 0, View.OrbitYaw);
			View.OrbitPitch = Arg(Args, 1, View.OrbitPitch);
			View.DistanceScale = FMath::Max(0.05f, Arg(Args, 2, View.DistanceScale));
			View.LookHeight = FMath::Clamp(Arg(Args, 3, View.LookHeight), 0.0f, 1.0f);
			UE_LOG(LogElysiumGreenRoomCmd, Display,
				TEXT("gr_view: yaw %.1f pitch %.1f dist %.2f look %.2f"),
				View.OrbitYaw, View.OrbitPitch, View.DistanceScale, View.LookHeight);
		});

	Register(TEXT("elysium.gr_light"),
		TEXT("Stage lighting: `elysium.gr_light studio|interior [scale]`."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			FElysiumGreenRoomRun::FLabView& View = Run.LabView();
			if (Args.Num() > 0)
			{
				View.Lighting = Args[0].StartsWith(TEXT("i"))
					? FElysiumGreenRoomRun::ELabLighting::Interior
					: FElysiumGreenRoomRun::ELabLighting::Studio;
			}
			View.LightScale = FMath::Max(0.0f, Arg(Args, 1, View.LightScale));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_light: %s at %.2f"),
				View.Lighting == FElysiumGreenRoomRun::ELabLighting::Interior
					? TEXT("interior") : TEXT("studio"),
				View.LightScale);
		});

	Register(TEXT("elysium.gr_skeleton"),
		TEXT("Draw the standing body's skeleton: `elysium.gr_skeleton 1`. "
		     "The bone a weapon claims to ride is otherwise invisible."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			Run.LabView().bDrawSkeleton = Args.Num() > 0 ? ArgBool(Args, 0, true)
				: !Run.LabView().bDrawSkeleton;
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_skeleton: %s"),
				Run.LabView().bDrawSkeleton ? TEXT("on") : TEXT("off"));
		});

	Register(TEXT("elysium.gr_bones"),
		TEXT("Dump the standing body's evaluated local pose against its mesh bind. With no "
		     "argument, the torso-to-head chain; otherwise every bone whose name contains an "
		     "argument: `elysium.gr_bones \"L UpperArm\" \"L Hand\"`. The proportion a retarget "
		     "delivers — and the local rotation a clip actually lands — is otherwise invisible."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			USkeletalMeshComponent* Body = Run.LabBody();
			const USkeletalMesh* Mesh = Body ? Body->GetSkeletalMeshAsset() : nullptr;
			if (Mesh == nullptr)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_bones: no standing body."));
				return;
			}
			const TArray<FTransform>& Locals = Body->GetBoneSpaceTransforms();
			if (Locals.Num() == 0)
			{
				// Distinguished from "not on this body" below: an empty array means the component has
				// not evaluated a pose yet, not that the chain's bones are absent from the skeleton.
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_bones: %s has no evaluated pose yet -- BoneSpaceTransforms is empty."),
					*Run.LabStem());
				return;
			}
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			// The bones to report: the default torso-to-head chain, or — with arguments — every
			// bone whose name contains one, so an arm or finger chain can be read without a
			// recompile.
			TArray<int32> Indices;
			if (Args.Num() == 0)
			{
				static const FName Chain[] =
				{
					TEXT("Bip01 Spine1"), TEXT("Bip01 Spine2"), TEXT("Bip01 Neck"),
					TEXT("Bip01 Head")
				};
				for (const FName BoneName : Chain)
				{
					const int32 Index = Ref.FindBoneIndex(BoneName);
					if (Index == INDEX_NONE)
					{
						UE_LOG(LogElysiumGreenRoomCmd, Display,
							TEXT("gr_bones: %s — not on this body"), *BoneName.ToString());
						continue;
					}
					Indices.Add(Index);
				}
			}
			else
			{
				for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
				{
					const FString Name = Ref.GetBoneName(Index).ToString();
					for (const FString& Filter : Args)
					{
						if (Name.Contains(Filter))
						{
							Indices.Add(Index);
							break;
						}
					}
				}
				if (Indices.Num() == 0)
				{
					UE_LOG(LogElysiumGreenRoomCmd, Warning,
						TEXT("gr_bones: no bone on %s matches the filter."), *Run.LabStem());
					return;
				}
			}
			for (const int32 Index : Indices)
			{
				if (!Locals.IsValidIndex(Index))
				{
					UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_bones: %s — not on this body"),
						*Ref.GetBoneName(Index).ToString());
					continue;
				}
				const FVector Posed = Locals[Index].GetTranslation();
				const FTransform& BindLocal = Ref.GetRefBonePose()[Index];
				const FVector Bind = BindLocal.GetTranslation();
				// The rotation column is the discriminating number for a clip defect: how far the
				// evaluated LOCAL rotation sits from the mesh bind's local, in degrees.
				const float RotDeg = FMath::RadiansToDegrees(
					Locals[Index].GetRotation().AngularDistance(BindLocal.GetRotation()));
				UE_LOG(LogElysiumGreenRoomCmd, Display,
					TEXT("gr_bones: %-16s posed %7.3f cm (%.2f, %.2f, %.2f)  bind %7.3f cm  "
					     "rot-vs-bind %7.2f deg"),
					*Ref.GetBoneName(Index).ToString(), Posed.Size(), Posed.X, Posed.Y, Posed.Z,
					Bind.Size(), RotDeg);
			}
		});

	// The wielded weapon.

	Register(TEXT("elysium.gr_wield"),
		TEXT("Put an item's wield model in the standing body's hand: "
		     "`elysium.gr_wield item_w_katana f`. With no argument, empties the hands."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				Run.LabClearWield();
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_wield: hands emptied."));
				return;
			}
			FString Detail;
			const EElysiumWieldResult Answer = Run.LabSetWield(Args[0], ArgFemale(Args, 1), Detail);
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_wield: %s"), *Detail);
			if (ElysiumWieldFailed(Answer))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_wield failed: %s"), *Detail);
				return;
			}
			FString Check;
			if (Run.LabWieldCheck(Check))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_wield: %s"), *Check);
			}
		});

	Register(TEXT("elysium.gr_wield_check"),
		TEXT("Verify the held weapon rides the wearer's hand across an animated base: "
		     "`elysium.gr_wield_check [seconds] [tolerance_cm]`. Samples the rendered mount "
		     "against the hand every frame; the verdict logs when the window closes, failing "
		     "with the worst sample and distance."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Run.LabWieldTrackRunning())
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_wield_check: a window is already sampling."));
				return;
			}
			FString Error;
			if (!Run.LabWieldTrackStart(
					Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.0f,
					Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f, Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_wield_check failed: %s"),
					*Error);
			}
		});

	// The mode.

	Register(TEXT("elysium.gr_mode"),
		TEXT("Switch the lab: `elysium.gr_mode review|drive|arena`."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_mode: name review, drive or arena."));
				return;
			}
			FElysiumGreenRoomRun::ELabMode Mode = FElysiumGreenRoomRun::ELabMode::Review;
			if (Args[0].StartsWith(TEXT("a")))
			{
				Mode = FElysiumGreenRoomRun::ELabMode::Arena;
			}
			else if (Args[0].StartsWith(TEXT("d")))
			{
				Mode = FElysiumGreenRoomRun::ELabMode::Drive;
			}
			else if (!Args[0].StartsWith(TEXT("r")))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_mode: '%s' is not review, drive or arena."), *Args[0]);
				return;
			}
			FString Error;
			if (!Run.LabSetMode(Mode, Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_mode failed: %s"), *Error);
				return;
			}
			const TCHAR* Name = Run.IsArena() ? TEXT("arena")
				: Run.IsDriving() ? TEXT("drive") : TEXT("review");
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_mode: %s"), Name);
		});

	// Arena navigation pins.

	Register(TEXT("elysium.gr_pin"),
		TEXT("Drop or update a named arena waypoint: `elysium.gr_pin corner [x y z]`. With no "
		     "coordinates, uses the driven body's own current position. With no arguments at all, "
		     "lists every pin."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				const TArray<TPair<FName, FVector>>& Pins = Run.ArenaPins();
				if (Pins.Num() == 0)
				{
					UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_pin: no pins."));
					return;
				}
				for (const TPair<FName, FVector>& Pin : Pins)
				{
					UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  %-16s (%.1f, %.1f, %.1f)"),
						*Pin.Key.ToString(), Pin.Value.X, Pin.Value.Y, Pin.Value.Z);
				}
				return;
			}

			const FName Name(*Args[0]);
			const FVector Explicit(Arg(Args, 1, 0.0f), Arg(Args, 2, 0.0f), Arg(Args, 3, 0.0f));
			const bool bExplicit = Args.Num() >= 4;
			FString Error;
			if (Run.ArenaSetPin(Name, bExplicit ? &Explicit : nullptr, Error))
			{
				const FVector* Placed = Run.FindArenaPin(Name);
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_pin: %s (%.1f, %.1f, %.1f)"),
					*Args[0], Placed ? Placed->X : 0.0, Placed ? Placed->Y : 0.0,
					Placed ? Placed->Z : 0.0);
			}
			else
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_pin failed: %s"), *Error);
			}
		});

	Register(TEXT("elysium.gr_walk"),
		TEXT("Walk a body between pins through the real motor: `elysium.gr_walk player pin1 pin2 "
		     "[loop]`, or name an arena character's targetname in place of `player`. "
		     "`elysium.gr_walk stop` ends it."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_walk: name a target (player, or an arena character) and at least one ")
					TEXT("pin, or 'stop'."));
				return;
			}
			if (Args[0].Equals(TEXT("stop"), ESearchCase::IgnoreCase))
			{
				Run.ArenaWalkStop();
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_walk: stopped."));
				return;
			}
			if (Args.Num() < 2)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_walk: name at least one pin."));
				return;
			}

			const FString Target = Args[0];
			TArray<FName> Pins;
			bool bLoop = false;
			for (int32 i = 1; i < Args.Num(); ++i)
			{
				if (Args[i].Equals(TEXT("loop"), ESearchCase::IgnoreCase))
				{
					bLoop = true;
					continue;
				}
				Pins.Add(FName(*Args[i]));
			}

			FString Error;
			if (Run.ArenaWalkStart(Target, Pins, bLoop, Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_walk: %s walking %d pin(s)%s"),
					*Target, Pins.Num(), bLoop ? TEXT(" (looping)") : TEXT(""));
			}
			else
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_walk failed: %s"), *Error);
			}
		});

	// Arena scenarios: the `Arena/scenarios/` records (`Debug/ElysiumArenaScenario.h`), staged and run
	// in the lab -- rendered, in real time, driveable over MCP `elysium_console_exec` -- through the same
	// staging (`ElysiumArenaStage::Stage`) and the same runner the headless `-ElysiumArena` suite uses,
	// so what the lab shows is what the suite judged. Each expectation is logged as it is met or missed.

	Register(TEXT("elysium.gr_scenario"),
		TEXT("Run an arena scenario record in the lab: `elysium.gr_scenario <name>` enters arena mode, ")
		TEXT("rebuilds the stage's entity world from the record (Arena/scenarios/**/*.json) through the map ")
		TEXT("path, runs its script and logs each expectation as it is met or missed; its trace lands in ")
		TEXT("Saved/Elysium/_arena/lab/. With no name, lists the records. `--stop` ends a running one. A ")
		TEXT("second call replaces everything."),
		[this](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			// Read fresh on every call: a record is tuned without a build.
			TArray<FElysiumArenaScenario> Records;
			TArray<FString> Refused;
			ElysiumArenaScenario::LoadAll(Records, Refused);
			for (const FString& Reason : Refused)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario: record refused: %s"), *Reason);
			}
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_scenario: %d record(s) under %s"),
					Records.Num(), *ElysiumArenaScenario::ScenarioRoot());
				for (const FElysiumArenaScenario& Record : Records)
				{
					UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  %-24s %-24s %5.1f s  %s%s"), *Record.Name,
						*Record.Stage, Record.Duration, *Record.About.Left(100),
						Record.KnownRed.IsEmpty() ? TEXT("")
							: *FString::Printf(TEXT("  [known red %s]"), *Record.KnownRed));
				}
				if (LabScenario.IsValid())
				{
					UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  running: %s"),
						*LabScenario->GetRecord().Name);
				}
				return;
			}
			if (Args[0].Equals(TEXT("--stop"), ESearchCase::IgnoreCase))
			{
				StopLabScenario(TEXT("stopped by `elysium.gr_scenario --stop`"));
				return;
			}
			const FElysiumArenaScenario* Record = Records.FindByPredicate([&Args](const FElysiumArenaScenario& R)
				{ return R.Name.Equals(Args[0], ESearchCase::IgnoreCase); });
			if (Record == nullptr)
			{
				TArray<FString> Names;
				for (const FElysiumArenaScenario& R : Records)
				{
					Names.Add(R.Name);
				}
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario: no record named '%s' (%s)."), *Args[0],
					*FString::Join(Names, TEXT(", ")));
				return;
			}
			if (!Record->IsArenaStage())
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_scenario: '%s' runs on %s's own entities, not in the lab: `uv run elysium arena %s`."),
					*Record->Name, *Record->StageMap, *Record->Name);
				return;
			}

			FString Error;
			if (!Run.IsArena())
			{
				if (!Run.LabSetMode(FElysiumGreenRoomRun::ELabMode::Arena, Error))
				{
					UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario failed: %s"), *Error);
					return;
				}
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_scenario: entered arena mode"));
			}
			if (EntityWorldFor(TEXT("gr_scenario")) == nullptr)
			{
				return;
			}
			// Refused before anything is touched while the Recast graph is still building: a path request
			// before it answers fails by name, which would read as an AI defect.
			if (!Run.IsArenaNavigationReady())
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_scenario: the arena's navigation is still building -- nothing was staged; ")
					TEXT("run `elysium.gr_scenario %s` again once it is ready."), *Record->Name);
				return;
			}
			UElysiumMapSubsystem* Maps = Owner.Get();
			AElysiumMapActor* Map = Maps != nullptr ? Maps->GetCurrentMap() : nullptr;
			if (Map == nullptr)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario: no map actor to load into."));
				return;
			}
			// A walk commanding a body being replaced must not outlive it, and neither may a run
			// recording the world being replaced.
			Run.ArenaWalkStop();
			StopLabScenario(TEXT("replaced by another gr_scenario"));

			ElysiumArenaStage::FHost Host;
			Host.World = World();
			Host.Map = Map;
			Host.bArena = true;
			Host.Spec = Run.ArenaSpec();
			Host.Origin = ElysiumArena::DefaultOrigin();
			FString Summary;
			ElysiumArenaStage::ConfigureHost(Host); // 0x1011a620: retain lab checkpoint provenance before Stage
			if (!ElysiumArenaStage::Stage(*Record, Host, Summary, Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario failed: %s"), *Error);
				return;
			}
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_scenario: %s"), *Summary);

			// After the rebuild: the sink goes on the world the rebuild just made.
			LabScenario = MakeUnique<FElysiumArenaScenarioRunner>(*Record, Host);
			LabScenario->SetLogProgress(true);
			if (!LabScenario->Start(FElysiumArenaScenarioRunner::EZero::StageActivation, Error))
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario: could not start the run: %s"), *Error);
				LabScenario.Reset();
				return;
			}
			if (!LabScenarioTicker.IsValid())
			{
				LabScenarioTicker = FTSTicker::GetCoreTicker().AddTicker(
					FTickerDelegate::CreateRaw(this, &FElysiumGreenRoomConsole::TickLabScenario));
			}
			UE_LOG(LogElysiumGreenRoomCmd, Display,
				TEXT("gr_scenario: running '%s' for up to %.1f s from the stage's Activate%s"), *Record->Name,
				Record->Duration, Record->KnownRed.IsEmpty() ? TEXT("")
					: *FString::Printf(TEXT(" (known red %s)"), *Record->KnownRed));
		});

	Register(TEXT("elysium.gr_hints"),
		TEXT("List the world's hint list, head first: index, name, type, class mask, node id, "
		     "disabled, owner, next use, rating and origin. `--validate [npc]` (default arena_gunman) "
		     "runs the tactical cover search's admission on every hint without searching: the first "
		     "gate each fails, the validators' reason strings (ai_debug_npc set for the run) and the "
		     "numbers they computed, and the hint the walk would return."),
		[this](FElysiumGreenRoomRun&, const TArray<FString>& Args)
		{
			FElysiumEntityWorld* EntityWorld = EntityWorldFor(TEXT("gr_hints"));
			if (EntityWorld == nullptr)
			{
				return;
			}
			if (Args.Num() > 0 && Args[0].Equals(TEXT("--validate"), ESearchCase::IgnoreCase))
			{
				ValidateHints(*EntityWorld,
					Args.IsValidIndex(1) ? Args[1] : FString(ElysiumArena::CoverGunmanName));
				return;
			}
			const TArray<int32>& List = EntityWorld->HintList();
			const TArray<TUniquePtr<FElysiumEntity>>& All = EntityWorld->Entities();
			UE_LOG(LogElysiumGreenRoomCmd, Display,
				TEXT("gr_hints: %d hint(s) listed, AI network %d node(s), curtime %.2f"),
				List.Num(), EntityWorld->Places().NumNodes(), EntityWorld->NowSeconds());
			for (const int32 Index : List)
			{
				const FElysiumEntity* Entity = All.IsValidIndex(Index) ? All[Index].Get() : nullptr;
				const FElysiumHint* Hint = FElysiumHint::Cast(Entity);
				if (Hint == nullptr)
				{
					UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  #%d  (not a live hint)"), Index);
					continue;
				}
				// The claim word as `0x102d14c0` reads it: a LIVE owner, not merely a set handle.
				const FElysiumEntity* OwnerEntity = EntityWorld->Resolve(Hint->HintOwner);
				const FString OwnerText = OwnerEntity != nullptr
					? FString::Printf(TEXT("#%d %s"), OwnerEntity->Handle.Index, *OwnerEntity->TargetName)
					: FString(TEXT("none"));
				UE_LOG(LogElysiumGreenRoomCmd, Display,
					TEXT("  #%d %-16s type %d  mask %d  node %d  disabled %d  owner %s  next use %.2f  ")
					TEXT("rating=%.2f  origin (%.0f, %.0f, %.0f)%s"),
					Index, Hint->TargetName.IsEmpty() ? TEXT("(unnamed)") : *Hint->TargetName,
					Hint->HintType, Hint->ClassMask, Hint->NodeId, Hint->Disabled, *OwnerText,
					Hint->NextUseTime,
					Hint->HintRating, Hint->Origin.X, Hint->Origin.Y, Hint->Origin.Z,
					Hint->IsDead() ? TEXT("  [dead]") : TEXT(""));
			}
		});

	Register(TEXT("elysium.gr_los"),
		TEXT("Break or restore the sight line to the far_ne pad: `elysium.gr_los on` puts the player "
		     "behind the cover block as seen from far_ne; `off` puts them back at the cover seat."),
		[this](FElysiumGreenRoomRun& Run, const TArray<FString>& Args)
		{
			if (!Run.IsArena())
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning,
					TEXT("gr_los: not in the arena — run `elysium.gr_scenario cover` first."));
				return;
			}
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_los: name on or off."));
				return;
			}
			const bool bBehindCover = ArgBool(Args, 0, false);
			const ElysiumArena::FSpec& Spec = Run.ArenaSpec();
			const FVector Origin = ElysiumArena::DefaultOrigin();
			// `on`: behind the block as seen from `far_ne`; `off`: the scenario's open seat (brief S4).
			const FVector Feet = Origin + (bBehindCover ? Spec.CoverBehindFeet : Spec.CoverSeatFeet);
			const float Yaw = bBehindCover ? Spec.CoverBehindYaw : Spec.CoverSeatYaw;
			FString Error;
			const bool bSeated = ElysiumArenaStage::SeatPlayerAt(World(), Feet, Yaw, Error);
			if (!bSeated)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_los failed: %s"), *Error);
				return;
			}
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_los: %s — player at %s facing %.0f"),
				bBehindCover ? TEXT("on (behind the cover block)") : TEXT("off (cover seat)"),
				*Feet.ToCompactString(), Yaw);
		});

	// The readout.

	Register(TEXT("elysium.gr_status"),
		TEXT("What the lab is currently doing: mode, body, clip, layers, grid, held weapon and the ")
		TEXT("driven character's live combat state."),
		[](FElysiumGreenRoomRun& Run, const TArray<FString>&)
		{
			const TCHAR* Mode = Run.IsArena() ? TEXT("arena")
				: Run.IsDriving() ? TEXT("drive") : TEXT("review");
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_status: mode %s"), Mode);
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  body   %s"),
				Run.LabStem().IsEmpty() ? TEXT("(none standing)") : *Run.LabStem());
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  clip   %s  %.3f / %.3f s%s"),
				Run.LabClip().IsEmpty() ? TEXT("(none)") : *Run.LabClip(),
				Run.LabTime(), Run.LabDuration(),
				Run.LabView().bPaused ? TEXT("  [paused]") : TEXT(""));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  layers %s"),
				Run.LabLayers().IsEmpty() ? TEXT("(none)")
					: *FString::Join(Run.LabLayers(), TEXT(", ")));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  view   yaw %.1f pitch %.1f dist %.2f"),
				Run.LabView().OrbitYaw, Run.LabView().OrbitPitch, Run.LabView().DistanceScale);
			FString Check;
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  weapon %s"),
				Run.LabWieldCheck(Check) ? *Check : TEXT("(none held)"));
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  track  %s"),
				Run.LabWieldTrackRunning() ? TEXT("(sampling)")
				: Run.LabWieldTrackVerdict().IsEmpty() ? TEXT("(not run)")
				: *Run.LabWieldTrackVerdict());
			// The entity side of the same hand: what the driven character is actually holding and
			// what its weapon controller is doing with the buttons the world is draining.
			UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  combat %s"), *Run.LabWeaponStatus());
			// The overlay stack, every slot, in composition order. `layers` above is the BASE clip's
			// declared closure — a different mechanism — so neither line stands in for the other.
			const TArray<FString> Overlay = Run.LabOverlayStatus();
			for (int32 Index = 0; Index < Overlay.Num(); ++Index)
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  %s %s"),
					Index == 0 ? TEXT("slots ") : TEXT("       "), *Overlay[Index]);
			}
			if (Run.IsArena())
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  pins   %d"), Run.ArenaPins().Num());
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  walk   %s"), *Run.ArenaWalkStatus());
			}
		});
}

FElysiumGreenRoomConsole::~FElysiumGreenRoomConsole()
{
	if (LabScenarioTicker.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(LabScenarioTicker);
		LabScenarioTicker.Reset();
	}
	// Clears its sink on the world it recorded, if that world still stands.
	LabScenario.Reset();
	for (IConsoleObject* Command : Commands)
	{
		if (Command != nullptr)
		{
			IConsoleManager::Get().UnregisterConsoleObject(Command);
		}
	}
	Commands.Reset();
}

FElysiumGreenRoomRun* FElysiumGreenRoomConsole::Lab(const TCHAR* Verb) const
{
	UElysiumMapSubsystem* const Maps = Owner.Get();
	FElysiumGreenRoomRun* const Run = Maps != nullptr ? Maps->GetGreenRoom() : nullptr;
	if (Run == nullptr || !Run->IsLabReady())
	{
		UE_LOG(LogElysiumGreenRoomCmd, Warning,
			TEXT("%s: no green room is standing -- run `elysium.gr` first."), Verb);
		return nullptr;
	}
	return Run;
}

void FElysiumGreenRoomConsole::Register(const TCHAR* Name, const TCHAR* Help,
	TFunction<void(FElysiumGreenRoomRun&, const TArray<FString>&)> Body)
{
	// The lab guard lives here rather than in each verb, so every one of them is only its own
	// operation and none can forget the check.
	Commands.Add(IConsoleManager::Get().RegisterConsoleCommand(Name, Help,
		FConsoleCommandWithArgsDelegate::CreateLambda(
			[this, Name, Body = MoveTemp(Body)](const TArray<FString>& Args)
		{
			if (FElysiumGreenRoomRun* Run = Lab(Name))
			{
				Body(*Run, Args);
			}
		}),
		ECVF_Default));
}

FElysiumEntityWorld* FElysiumGreenRoomConsole::EntityWorldFor(const TCHAR* Verb) const
{
	const UElysiumMapSubsystem* Maps = Owner.Get();
	const AElysiumMapActor* Map = Maps != nullptr ? Maps->GetCurrentMap() : nullptr;
	FElysiumEntityWorld* EntityWorld = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	if (EntityWorld == nullptr)
	{
		UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("%s: no entity world is loaded."), Verb);
	}
	return EntityWorld;
}

bool FElysiumGreenRoomConsole::TickLabScenario(float /*DeltaSeconds*/)
{
	if (!LabScenario.IsValid())
	{
		LabScenarioTicker.Reset();
		return false;
	}
	if (LabScenario->Tick())
	{
		return true;
	}
	FinishLabScenario();
	LabScenarioTicker.Reset();
	return false;
}

void FElysiumGreenRoomConsole::StopLabScenario(const TCHAR* Reason)
{
	if (!LabScenario.IsValid())
	{
		return;
	}
	LabScenario->Abort(Reason);
	FinishLabScenario();
}

void FElysiumGreenRoomConsole::FinishLabScenario()
{
	if (!LabScenario.IsValid())
	{
		return;
	}
	const FString Dir = LabScenarioDir();
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	FString Error;
	const FString Path = Dir / LabScenario->GetResult().Trace;
	if (!LabScenario->WriteTrace(Path, Error))
	{
		UE_LOG(LogElysiumGreenRoomCmd, Warning, TEXT("gr_scenario: %s"), *Error);
	}
	const FElysiumArenaScenarioResult& Result = LabScenario->GetResult();
	UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("gr_scenario: %s; trace %s"), *Result.Summary(), *Path);
	LabScenario.Reset();
}

UWorld* FElysiumGreenRoomConsole::World() const
{
	// The lab's own resolution (`FElysiumGreenRoomRun::GetWorld`): the owning game instance's world.
	const UElysiumMapSubsystem* Maps = Owner.Get();
	const UGameInstance* GI = Maps != nullptr ? Maps->GetGameInstance() : nullptr;
	return GI != nullptr ? GI->GetWorld() : nullptr;
}

#endif // !UE_BUILD_SHIPPING
