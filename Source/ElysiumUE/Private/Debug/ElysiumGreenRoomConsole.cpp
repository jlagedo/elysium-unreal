#include "Debug/ElysiumGreenRoomConsole.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumGreenRoomRun.h"
#include "ElysiumMapSubsystem.h"
#include "Visual/ElysiumAnimGraph.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumGreenRoomCmd, Log, All);

namespace
{
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
}

FElysiumGreenRoomConsole::FElysiumGreenRoomConsole(UElysiumMapSubsystem* InOwner)
	: Owner(InOwner)
{
	// --- the body ---------------------------------------------------------------------------------

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

	// --- the clip vocabulary ----------------------------------------------------------------------

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
			for (const TPair<FString, FElysiumNpcClip>& Pair : Set->Clips)
			{
				// The label is the KEY — a clip carries its owner and activity, not its own name.
				if (Filter.IsEmpty() || Pair.Key.Contains(Filter))
				{
					Hits.Add(Pair.Key);
				}
			}
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

	// --- layers, grids and aim --------------------------------------------------------------------

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

	// --- playback ---------------------------------------------------------------------------------

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

	// --- the view ---------------------------------------------------------------------------------
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

	// --- the wielded weapon (CCC10.2) -------------------------------------------------------------

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

	// --- the mode ---------------------------------------------------------------------------------

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

	// --- arena navigation pins ----------------------------------------------------------------------

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

	// --- the readout ------------------------------------------------------------------------------

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
			if (Run.IsArena())
			{
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  pins   %d"), Run.ArenaPins().Num());
				UE_LOG(LogElysiumGreenRoomCmd, Display, TEXT("  walk   %s"), *Run.ArenaWalkStatus());
			}
		});
}

FElysiumGreenRoomConsole::~FElysiumGreenRoomConsole()
{
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

#endif // !UE_BUILD_SHIPPING
