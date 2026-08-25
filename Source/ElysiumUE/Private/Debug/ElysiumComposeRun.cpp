#include "Debug/ElysiumComposeRun.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumGymBuilder.h"

#include "ElysiumContentPaths.h"
#include "ElysiumGymSpec.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputRouter.h"
#include "ElysiumInputScope.h"
#include "ElysiumInputSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayerController.h"
#include "ElysiumMovementComponent.h"
#include "Substrate/ElysiumItemClasses.h"

#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCompose, Log, All);

namespace
{
	// The default is a pump shotgun because it is the family whose attack layer moves both arms
	// against each other — the shape a composition defect is visible in at all.
	const TCHAR* const GDefaultWeapon = TEXT("item_w_ithaca_m_37");

	// Frames of input-free settling before the stream is armed, for the reason the movement harness
	// settles: a body that has just been placed is not grounded yet, and a gait resolved off an
	// airborne frame is not the gait under test.
	constexpr int32 GSettleFrames = 10;

	// The scripted intent, in frames at the run's own rate. Each phase states what it is FOR,
	// because a stream is only as good as the composition it provokes:
	//
	//  - `Relaxed` runs the body sideways with nothing held and no attack behind it. The stance clock
	//              has never been stamped, so this is the RELAXED gait — `m37_relaxed_run` and its
	//              own move layer — and it is the control the aggressive phase is read against.
	//  - `Fire`    holds the trigger while still strafing. That is an attack layer over a moving
	//              gait, and it also stamps the combat-stance clock.
	//  - `Carry`   keeps strafing with NOTHING held, inside the five seconds the stamp buys. This is
	//              the case the owner reports: the aggressive gait (`m37_aggressive_run`) composing
	//              its own two autolayers — a masked aim pose and an additive bobble — over a moving
	//              body. It is the phase the whole run exists for, so it is the longest.
	//  - the same two again to the left, because a strafe's gait cell is chosen by `move_yaw` and one
	//    direction cannot show a cell picked from the wrong side of the fan.
	// How many consecutive frames of open gameplay input the run wants before it arms.
	constexpr int32 GUngatedFramesRequired = 30;

	constexpr int32 GRelaxedFrames = 60;
	constexpr int32 GFireFrames = 60;
	constexpr int32 GCarryFrames = 150;

	FString OutputDir()
	{
		return FPaths::Combine(FElysiumContentPaths::Root(), TEXT("_compose"));
	}

	// One place that resolves what the run drives, so a null anywhere reads the same.
	struct FComposeRefs
	{
		AElysiumPlayerController* PC = nullptr;
		UElysiumInputRouter* Router = nullptr;
		AElysiumMapActor* Map = nullptr;
		USkeletalMeshComponent* Visual = nullptr;
		// The body itself and the thing that moves it. Both are recorded per frame, because the
		// composition under test is the one a MOVING body produces: a run that records a gait
		// selection while the body stands still records the wrong thing convincingly, and only the
		// travelled distance says which happened.
		AElysiumPawn* Pawn = nullptr;
		UElysiumMovementComponent* Move = nullptr;

		explicit operator bool() const { return PC && Router && Map && Visual && Pawn && Move; }
	};

	FComposeRefs Resolve(UElysiumMapSubsystem* Sub)
	{
		FComposeRefs R;
		const UGameInstance* GI = Sub != nullptr ? Sub->GetGameInstance() : nullptr;
		const UWorld* World = GI != nullptr ? GI->GetWorld() : nullptr;
		if (World == nullptr)
		{
			return R;
		}
		R.PC = Cast<AElysiumPlayerController>(World->GetFirstPlayerController());
		if (R.PC != nullptr)
		{
			R.Router = R.PC->GetInputRouter();
			R.Pawn = Cast<AElysiumPawn>(R.PC->GetPawn());
			if (R.Pawn != nullptr)
			{
				R.Visual = R.Pawn->GetPlayerVisual();
				R.Move = Cast<UElysiumMovementComponent>(R.Pawn->GetMovementComponent());
			}
		}
		R.Map = Sub != nullptr ? Sub->GetCurrentMap() : nullptr;
		return R;
	}

	// A bone's component-space transform written the way `pose_oracle.json` writes retail's: three
	// rows of four, a basis row then that row's translation. The comparator reads the translation
	// column and never the basis, so the row order is the schema rather than a claim about the
	// frame — and the numbers here are Unreal-native centimetres, which is stated in the report's
	// own `units` field rather than converted.
	TArray<TSharedPtr<FJsonValue>> MatrixRow(const FTransform& Transform)
	{
		const FMatrix M = Transform.ToMatrixWithScale();
		TArray<TSharedPtr<FJsonValue>> Row;
		Row.Reserve(12);
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Row.Add(MakeShared<FJsonValueNumber>(M.M[0][Column]));
			Row.Add(MakeShared<FJsonValueNumber>(M.M[1][Column]));
			Row.Add(MakeShared<FJsonValueNumber>(M.M[2][Column]));
			Row.Add(MakeShared<FJsonValueNumber>(M.M[3][Column]));
		}
		return Row;
	}
}

bool FElysiumComposeRun::IsRequested()
{
	return FParse::Param(FCommandLine::Get(), TEXT("ElysiumCompose"));
}

FElysiumComposeRun::FElysiumComposeRun(UElysiumMapSubsystem* InSubsystem)
	: Subsystem(InSubsystem)
{
	FParse::Value(FCommandLine::Get(), TEXT("ComposeHz="), Hz);
	Hz = FMath::Clamp(Hz, 10, 1000);
	StepSeconds = 1.0f / static_cast<float>(Hz);

	if (!FParse::Value(FCommandLine::Get(), TEXT("ComposeWeapon="), WeaponClass)
		|| WeaponClass.IsEmpty())
	{
		WeaponClass = GDefaultWeapon;
	}

	BuildStream();
	UE_LOG(LogElysiumCompose, Log,
		TEXT("headless composed-pose run armed: weapon '%s', %d Hz, %d scripted frames."),
		*WeaponClass, Hz, Stream.Num());

	TickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FElysiumComposeRun::Tick));
}

FElysiumComposeRun::~FElysiumComposeRun()
{
	if (TickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	}
}

void FElysiumComposeRun::BuildStream()
{
	Stream.Reset();
	uint32 Seq = 0;
	const auto Push = [this, &Seq](const FVector2D& Move, uint64 Buttons)
	{
		FElysiumUserCmd Cmd;
		Cmd.Seq = ++Seq;
		Cmd.DeltaSeconds = StepSeconds;
		Cmd.Move = Move;
		Cmd.Buttons = Buttons;
		Stream.Record(Cmd);
	};

	const FVector2D Right(0.0f, 1.0f);
	const FVector2D Left(0.0f, -1.0f);
	const uint64 MoveRight = static_cast<uint64>(EElysiumButton::MoveRight);
	const uint64 MoveLeft = static_cast<uint64>(EElysiumButton::MoveLeft);
	const uint64 Attack = static_cast<uint64>(EElysiumButton::Attack);

	for (int32 Frame = 0; Frame < GRelaxedFrames; ++Frame)
	{
		Push(Right, MoveRight);
	}
	for (int32 Frame = 0; Frame < GFireFrames; ++Frame)
	{
		Push(Right, MoveRight | Attack);
	}
	for (int32 Frame = 0; Frame < GCarryFrames; ++Frame)
	{
		Push(Right, MoveRight);
	}
	for (int32 Frame = 0; Frame < GFireFrames; ++Frame)
	{
		Push(Left, MoveLeft | Attack);
	}
	for (int32 Frame = 0; Frame < GCarryFrames; ++Frame)
	{
		Push(Left, MoveLeft);
	}
}

bool FElysiumComposeRun::Begin()
{
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FComposeRefs Refs = Resolve(Sub);
	if (!Refs)
	{
		return false;
	}
	FElysiumEntityWorld* World = Refs.Map->GetEntityWorld();
	FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	if (Player == nullptr)
	{
		return false;
	}

	// The weapon, through the player's own grant service rather than by constructing an item: the
	// activity ladder this run exists to exercise is keyed on what the inventory says is active,
	// and a hand-built item would be a second way for one to become active.
	//
	// **Granted once, and the handle is kept.** `Begin` is retried while the world settles, and a
	// grant per attempt would hand the body a stack of shotguns before the first one is in its hand.
	if (!GrantedWeapon.IsSet())
	{
		GrantedWeapon = Player->Inventory.GiveNamedItem(*Player, WeaponClass);
		if (!GrantedWeapon.IsSet())
		{
			UE_LOG(LogElysiumCompose, Error,
				TEXT("compose run cannot give '%s' — no such item class, or the grant was refused; "
					 "nothing is recorded"), *WeaponClass);
			return false;
		}
	}

	// A grant adds; it does not necessarily wield. `SetActiveWeapon` is the switch the equip route
	// and the NPC loadout both use, and it is what puts the class in the hand the activity ladder
	// reads — without it the run would record a whole stream of the UNARMED ladder, which looks
	// exactly like a working recording of the wrong thing.
	FElysiumItem* Held = Player->Inventory.Active(*Player);
	if (Held == nullptr || !Held->ClassName().Equals(WeaponClass, ESearchCase::IgnoreCase))
	{
		FElysiumEntity* Entity = World->Resolve(GrantedWeapon);
		FElysiumItem* Granted = Entity != nullptr ? Entity->AsItem() : nullptr;
		if (Granted == nullptr || !Player->Inventory.SetActiveWeapon(*Player, *Granted))
		{
			// Retried rather than failed: the grant may still be spawning its entity this frame.
			return false;
		}
		Held = Player->Inventory.Active(*Player);
	}
	if (Held == nullptr)
	{
		UE_LOG(LogElysiumCompose, Error,
			TEXT("compose run granted '%s' but no item is active; nothing is recorded"),
			*WeaponClass);
		return false;
	}
	UE_LOG(LogElysiumCompose, Log, TEXT("compose run holds '%s'"), *Held->ClassName());

	// **The body must pose while nothing is looking at it.** Every harness tier runs `-nullrhi`, so
	// the component is never rendered and the default visibility-based tick would skip the pose
	// evaluation entirely — leaving `GetComponentSpaceTransforms` holding the reference pose while
	// the run records frame after frame of it as if that were the answer.
	Refs.Visual->VisibilityBasedAnimTickOption =
		EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Refs.Visual->bEnableUpdateRateOptimizations = false;

	// **A screen holding input turns every replayed command into a zero.** `SampleFrame` gates each
	// command through `GateGameplayCommand`, which strips the move vector and every button whenever
	// the resolved scope has dropped the player mapping contexts — and the grant this run just made
	// raises a pickup notification that does exactly that for its ~2.9 s. Arming through it would
	// hand the stream to a body that cannot act on it: the report would still name a gait, still
	// carry four slot rows and still hold a full skeleton, and every frame of it would be a standing
	// body. So the run waits for gameplay input, which is what the retry loop is for.
	//
	// Consecutive frames rather than one, because the scope is pushed by a widget activating: the
	// grant above returns before its notification exists, so a single open reading is the frame
	// BEFORE the screen and arming on it is arming into the gate.
	const UElysiumInputSubsystem* InputSubsystem =
		UElysiumInputSubsystem::Get(Refs.PC->GetGameInstance());
	if (InputSubsystem == nullptr
		|| !ElysiumInput::AllowsGameplayCommands(InputSubsystem->State()))
	{
		UngatedFrames = 0;
		return false;
	}
	if (++UngatedFrames < GUngatedFramesRequired)
	{
		return false;
	}

	// **The stage, so the map's walls cannot end the strafe.** Every phase of this stream runs the
	// body sideways for seconds at a time, and a spawn point in a corridor stops it against a wall
	// inside a metre — which records a standing body under a gait selection, the one failure a pose
	// report cannot show on its own. The gym is the movement harness's own flat lane, generated from
	// the mover's live tuning and stood up at the world origin, far from anything a map placed.
	if (!bGymBuilt)
	{
		bGymBuilt = true;
		const ElysiumGym::FSpec Spec = ElysiumGym::Build(Refs.Move->GetTuning());
		const ElysiumGym::FLane* Lane = Spec.FindLane(TEXT("flat"));
		if (Lane == nullptr || ElysiumGym::Spawn(Refs.PC->GetWorld(), Spec,
			ElysiumGym::DefaultOrigin()) == nullptr)
		{
			UE_LOG(LogElysiumCompose, Error,
				TEXT("compose run could not stand the gym up; the body would strafe into whatever "
					 "the map put beside it"));
			return false;
		}
		Refs.Move->ResetState();
		Refs.Pawn->SetActorLocation(
			ElysiumGym::SeatOrigin(ElysiumGym::DefaultOrigin() + Lane->FeetOrigin,
				Refs.Pawn->GetBodyHalfHeight()),
			/*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
		// Faced across the lane, so `+moveright` -- which every phase of the stream holds -- runs
		// ALONG it. A body facing down the lane would strafe off the 96-unit half-width in under a
		// second and the run would measure a fall.
		Refs.PC->SetControlRotation(FRotator(0.0f, Lane->Yaw - 90.0f, 0.0f));
	}

	// **A frozen body records a gait it never walked.** The map layer freezes a body that arrives
	// somewhere with no floor under it, and every phase of this stream is a strafe — so the freeze is
	// released here, by the harness that is about to drive it, exactly as the movement harness does.
	Refs.Pawn->SetMovementFrozen(false);

	StartLocation = Refs.Pawn->GetActorLocation();

	Stem = Refs.Map->GetPlayerAnimSelection().Stem;
	Refs.Router->StartReplay(Stream);
	bStarted = true;
	UE_LOG(LogElysiumCompose, Log, TEXT("compose run begins on '%s'"),
		Stem.IsEmpty() ? TEXT("(no stem yet)") : *Stem);
	return true;
}

void FElysiumComposeRun::Sample()
{
	const FComposeRefs Refs = Resolve(Subsystem.Get());
	if (!Refs)
	{
		++NoBody;
		return;
	}

	const USkeletalMesh* Mesh = Refs.Visual->GetSkeletalMeshAsset();
	const TArray<FTransform>& Component = Refs.Visual->GetComponentSpaceTransforms();
	if (Mesh == nullptr || Component.IsEmpty())
	{
		++NoPose;
		return;
	}
	const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
	USkeleton* Skeleton = Refs.Visual->GetSkeletalMeshAsset()->GetSkeleton();

	TSharedRef<FJsonObject> Frame = MakeShared<FJsonObject>();
	Frame->SetNumberField(TEXT("frame"), FrameIndex);
	Frame->SetNumberField(TEXT("curtime"), FrameIndex * static_cast<double>(StepSeconds));
	Frame->SetBoolField(TEXT("is_player"), true);
	Frame->SetStringField(TEXT("stem"), Stem);
	Frame->SetStringField(TEXT("root_bone"),
		Ref.GetNum() > 0 ? Ref.GetBoneName(0).ToString() : FString());
	Frame->SetNumberField(TEXT("bone_count"), Component.Num());

	TSharedRef<FJsonObject> Bones = MakeShared<FJsonObject>();
	for (int32 Index = 0; Index < Component.Num() && Index < Ref.GetNum(); ++Index)
	{
		Bones->SetArrayField(Ref.GetBoneName(Index).ToString(), MatrixRow(Component[Index]));
	}
	Frame->SetObjectField(TEXT("bones"), Bones);

	// The record that produced the pose, so a divergence names the selection it came from rather
	// than only the geometry. Read rather than re-derived: the driver published it this frame.
	const FElysiumAnimationSelection& Selection = Refs.Map->GetPlayerAnimSelection();
	TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
	State->SetStringField(TEXT("label"), Selection.SequenceLabel);
	State->SetStringField(TEXT("owner"), Selection.OwnerStem);
	State->SetStringField(TEXT("activity"), Selection.ResolvedActivity);
	State->SetNumberField(TEXT("raw_index"), Selection.RawSequenceIndex);
	TArray<TSharedPtr<FJsonValue>> Layers;
	for (const FString& Layer : Selection.LayerLabels)
	{
		Layers.Add(MakeShared<FJsonValueString>(Layer));
	}
	State->SetArrayField(TEXT("layers"), Layers);
	// The three pose parameters the fans are sampled on. `move_yaw` is what picks the gait cell a
	// strafe plays, so a run whose cells look wrong can be told apart from a run whose steering was
	// wrong without re-deriving either from the geometry.
	State->SetNumberField(TEXT("move_yaw"), Selection.MoveYaw);
	State->SetNumberField(TEXT("aim_yaw"), Selection.AimYaw);
	State->SetNumberField(TEXT("aim_pitch"), Selection.AimPitch);
	Frame->SetObjectField(TEXT("selection"), State);

	// **Where the body is and how fast it is going.** Without these a recording of a stationary body
	// is indistinguishable from a recording of a moving one: the selection would still name a gait,
	// the slots would still carry layers, and every pose in the file would be the wrong case
	// answered confidently. The comparator gates its moving cohort on `speed`, and `travelled` is
	// what a reader checks first when a whole run turns out to have gone nowhere.
	const FVector Here = Refs.Pawn->GetActorLocation();
	const FVector Velocity = Refs.Move->Velocity;
	TSharedRef<FJsonObject> Motion = MakeShared<FJsonObject>();
	Motion->SetNumberField(TEXT("x"), Here.X);
	Motion->SetNumberField(TEXT("y"), Here.Y);
	Motion->SetNumberField(TEXT("z"), Here.Z);
	Motion->SetNumberField(TEXT("vx"), Velocity.X);
	Motion->SetNumberField(TEXT("vy"), Velocity.Y);
	Motion->SetNumberField(TEXT("vz"), Velocity.Z);
	Motion->SetNumberField(TEXT("speed"), FVector2D(Velocity.X, Velocity.Y).Size());
	const double Travelled = FVector::Dist2D(Here, StartLocation);
	Farthest = FMath::Max(Farthest, Travelled);
	Motion->SetNumberField(TEXT("travelled"), Travelled);
	Motion->SetNumberField(TEXT("facing_yaw"), Refs.PC->GetControlRotation().Yaw);
	const UElysiumInputSubsystem* Gate = UElysiumInputSubsystem::Get(Refs.PC->GetGameInstance());
	Motion->SetBoolField(TEXT("gated"),
		Gate == nullptr || !ElysiumInput::AllowsGameplayCommands(Gate->State()));
	Frame->SetObjectField(TEXT("motion"), Motion);

	// The overlay stack, every slot including the free ones — the same rows `gr_status` and
	// `elysium_player_get` print, so one reading of the stack serves all three.
	TArray<TSharedPtr<FJsonValue>> Slots;
	for (int32 SlotIndex = 0; SlotIndex < ElysiumOverlay::NumSlots; ++SlotIndex)
	{
		const FElysiumOverlaySlotRecord& Row = Selection.Slots[SlotIndex];
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetNumberField(TEXT("slot"), SlotIndex);
		Object->SetStringField(TEXT("label"), Row.Label);
		Object->SetStringField(TEXT("owner_stem"), Row.OwnerStem);
		Object->SetStringField(TEXT("activity"), Row.Activity);
		Object->SetNumberField(TEXT("weight"), Row.Weight);
		Object->SetNumberField(TEXT("cycle"), Row.Cycle);
		Object->SetNumberField(TEXT("age_seconds"), Row.AgeSeconds);
		Object->SetBoolField(TEXT("finished"), Row.bFinished);
		Object->SetStringField(TEXT("mask"), Row.MaskName.ToString());
		// **The bones the mask owns, by name, resolved against the PLAYING skeleton** — which is the
		// same skeleton the blend resolves them against, and the reason the set is written per frame
		// rather than assumed from the profile's name. Without it a comparator has only the bone
		// names both sides happen to share, which includes a lower body the layer never touches and
		// whose gait is free to differ; scoring that reports the legs as a composition defect.
		TArray<TSharedPtr<FJsonValue>> Masked;
		if (const UBlendProfile* Profile = Skeleton != nullptr && !Row.MaskName.IsNone()
			? Skeleton->GetBlendProfile(Row.MaskName) : nullptr)
		{
			for (int32 Entry = 0; Entry < Profile->GetNumBlendEntries(); ++Entry)
			{
				const FBlendProfileBoneEntry& Bone = Profile->GetEntry(Entry);
				// In `BlendMask` mode the scale IS the per-bone weight, so a zero entry is a bone the
				// mask does not own however it got into the table.
				if (Bone.BlendScale > 0.0f && !Bone.BoneReference.BoneName.IsNone())
				{
					Masked.Add(MakeShared<FJsonValueString>(Bone.BoneReference.BoneName.ToString()));
				}
			}
		}
		Object->SetArrayField(TEXT("masked_bones"), Masked);
		Slots.Add(MakeShared<FJsonValueObject>(Object));
	}
	Frame->SetArrayField(TEXT("overlay_slots"), Slots);

	Frames.Add(MakeShared<FJsonValueObject>(Frame));
	++FrameIndex;
}

void FElysiumComposeRun::Finish()
{
	bDone = true;

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("schema"), TEXT("elysium.compose.pose.v1"));
	Root->SetStringField(TEXT("harness"), TEXT("compose"));
	Root->SetStringField(TEXT("weapon"), WeaponClass);
	Root->SetStringField(TEXT("stem"), Stem);
	// Stated rather than converted: the comparison against a retail capture is isometry-invariant,
	// so the two schemas differ only in the unit their numbers are already in.
	Root->SetStringField(TEXT("units"), TEXT("cm"));
	Root->SetStringField(TEXT("space"), TEXT("unreal-native component space"));
	Root->SetNumberField(TEXT("hz"), Hz);
	Root->SetNumberField(TEXT("frames_recorded"), Frames.Num());
	Root->SetNumberField(TEXT("frames_without_a_pose"), NoPose);
	Root->SetNumberField(TEXT("frames_without_a_body"), NoBody);
	Root->SetNumberField(TEXT("farthest_travelled_cm"), Farthest);
	Root->SetArrayField(TEXT("frames"), Frames);

	UElysiumMapSubsystem* Sub = Subsystem.Get();
	const FString MapName = Sub != nullptr ? Sub->GetCurrentMapName() : FString(TEXT("nomap"));
	const FString Path = FPaths::Combine(OutputDir(),
		FString::Printf(TEXT("%s-%s.json"), *MapName, *WeaponClass));

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!FJsonSerializer::Serialize(Root, Writer) || !FFileHelper::SaveStringToFile(Text, *Path))
	{
		UE_LOG(LogElysiumCompose, Error, TEXT("compose run could not write %s"), *Path);
	}
	else
	{
		UE_LOG(LogElysiumCompose, Log,
			TEXT("compose run wrote %d frames to %s (%d without a pose, %d without a body)"),
			Frames.Num(), *Path, NoPose, NoBody);
	}
	if (Farthest < 25.0)
	{
		// A quarter of a metre over the whole stream is not a strafe. The gait cohort of this file is
		// worthless and a comparator would happily score it, so the run names the failure here.
		UE_LOG(LogElysiumCompose, Error,
			TEXT("compose run travelled only %.1f cm — the body never strafed, so every gait frame ")
			TEXT("in this report is a standing body"), Farthest);
	}
	if (Frames.IsEmpty())
	{
		// An empty report is indistinguishable from a body that posed nothing, so the run says which
		// it was rather than leaving a reader to guess from a zero.
		UE_LOG(LogElysiumCompose, Error,
			TEXT("compose run recorded no frame at all — the body never posed"));
	}
	FPlatformMisc::RequestExit(false);
}

bool FElysiumComposeRun::Tick(float /*DeltaSeconds*/)
{
	if (bDone)
	{
		return false;
	}
	UElysiumMapSubsystem* Sub = Subsystem.Get();
	if (Sub == nullptr)
	{
		Finish();
		return false;
	}
	AElysiumMapActor* MapActor = Sub->GetCurrentMap();
	if (MapActor == nullptr || !MapActor->IsSpawnDone())
	{
		return true;   // still loading; nothing to drive yet
	}

	if (!bStarted)
	{
		if (SettleRemaining < GSettleFrames)
		{
			++SettleRemaining;
			return true;
		}
		if (!Begin())
		{
			// One retry window rather than an indefinite wait: a world that is ready and still
			// cannot be driven is a fault, and a harness that spins forever on it reports nothing.
			// Long enough to outlast a pickup notification's hold, which is the ordinary reason the
			// first several hundred attempts refuse.
			if (++SettleRemaining > GSettleFrames * 120)
			{
				UE_LOG(LogElysiumCompose, Error,
					TEXT("compose run could not arm on a ready map; nothing is recorded"));
				Finish();
				return false;
			}
			return true;
		}
		return true;
	}

	Sample();
	if (FrameIndex >= Stream.Num())
	{
		Finish();
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
