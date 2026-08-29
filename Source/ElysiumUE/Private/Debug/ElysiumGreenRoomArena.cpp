#include "Debug/ElysiumGreenRoomRun.h"

#include "Debug/ElysiumGreenRoomShared.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputRouter.h"   // gr_walk drives the player through its own record/replay door
#include "ElysiumMapActor.h"
#include "ElysiumMovementComponent.h"
#include "ElysiumMoveSolve.h"   // ElysiumMove::WalkSpeed — the arena walk's motor-speed fallback
#include "ElysiumPlayer.h"   // the driven character's inventory, for the gr_status weapon readout
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerController.h"   // gr_walk reaches the driven body's input router through it
#include "ElysiumUserCmd.h"   // the synthesized command stream gr_walk feeds the player through
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"
#include "Visual/ElysiumNpcBody.h"   // gr_walk commands an arena character's IElysiumNpcMotor
#if !UE_BUILD_SHIPPING
// The arena's engine half is debug-only, while its spec is not. Standing and clearing the room
// are the only things here that need a spawner, so the guard is on those calls rather than on
// the harness.
#include "Debug/ElysiumArenaBuilder.h"
#include "Debug/ElysiumArenaCast.h"
#endif

#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

// The arena (the combat playtest room).

bool FElysiumGreenRoomRun::BuildArena(FString& OutError)
{
	DestroyArena();
	UWorld* World = GetWorld();
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(World);
	if (!World || !Refs)
	{
		OutError = TEXT("no player body to build a room for");
		return false;
	}

	Arena = ElysiumArena::Build();
#if !UE_BUILD_SHIPPING
	// The entity world is handed in so the anchors are created as real `intersting_place` entities
	// rather than as markers only this harness understands. A stage world's entity world is empty
	// but live and activated, which is exactly what a runtime spawn needs.
	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* EntityWorld = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	const bool bStood = ElysiumArena::Stand(World, EntityWorld, Arena,
		ElysiumArena::DefaultOrigin(), bGymMeshes, ArenaStanding, OutError);
#else
	const bool bStood = false;
	OutError = TEXT("the arena is not built in Shipping");
#endif
	if (!bStood)
	{
		Arena = ElysiumArena::FSpec();
		return false;
	}
	UE_LOG(LogElysiumGreenRoom, Log,
		TEXT("arena: room standing — %d solid(s), %d anchor(s), %d pad(s)%s"),
		Arena.Solids.Num(), ArenaStanding.Anchors.Num(), Arena.Pads.Num(),
		bGymMeshes ? TEXT("") : TEXT(" (hidden)"));
	return true;
}

void FElysiumGreenRoomRun::DestroyArena()
{
	// A walk targeting a character this teardown is about to clear must not be left pointing at a
	// body that no longer exists.
	ArenaWalkStop();
#if !UE_BUILD_SHIPPING
	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* EntityWorld = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	// The cast goes with the room. A character left standing where a floor used to be falls out of
	// the level and keeps thinking, which reads as an AI bug rather than as a torn-down arena.
	if (EntityWorld != nullptr && ArenaStanding.IsValid())
	{
		const int32 Cleared = ElysiumArenaCast::ClearSpawned(*EntityWorld);
		if (Cleared > 0)
		{
			UE_LOG(LogElysiumGreenRoom, Log,
				TEXT("arena: cleared %d spawned character(s) with the room"), Cleared);
		}
	}
	ElysiumArena::Teardown(EntityWorld, ArenaStanding);
#endif
	ArenaStanding = ElysiumArena::FStanding();
	Arena = ElysiumArena::FSpec();
}

bool FElysiumGreenRoomRun::IsArenaNavigationReady() const
{
#if !UE_BUILD_SHIPPING
	return IsArena() && ArenaStanding.IsValid() && ElysiumArena::IsNavigationReady(GetWorld());
#else
	return false;
#endif
}

bool FElysiumGreenRoomRun::ArenaPadOrigin(const FName& Pad, FVector& OutFeetWorld,
	float& OutYaw) const
{
	const ElysiumArena::FPad* Found = Arena.FindPad(Pad);
	if (Found == nullptr)
	{
		return false;
	}
	OutFeetWorld = ElysiumArena::DefaultOrigin() + Found->FeetOrigin;
	OutYaw = Found->Yaw;
	return true;
}

bool FElysiumGreenRoomRun::ArenaSeatPlayer(FString& OutError)
{
	if (!IsArena())
	{
		OutError = TEXT("not in the arena");
		return false;
	}
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(GetWorld());
	if (!Refs)
	{
		OutError = TEXT("no player body to seat");
		return false;
	}

	// The gym's own seating, minus the lane: `ResetState` first so the body inherits neither the
	// position nor the *motion* of wherever it was, and `SeatOrigin` is the one conversion between
	// the spec's feet and the pawn's centre.
	Refs.Move->ResetState();
	Refs.Pawn->SetActorLocation(
		ElysiumGym::SeatOrigin(ElysiumArena::DefaultOrigin() + Arena.PlayerFeet,
			Refs.Body->GetBodyHalfHeight()),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	Refs.PC->SetControlRotation(FRotator(0.0f, Arena.PlayerYaw, 0.0f));
	// The boom would otherwise ease across the whole teleport, which reads as the camera falling
	// behind a body that did not move.
	if (UElysiumCameraComponent* Camera = Refs.Body->GetCameraComponent())
	{
		Camera->RequestReseed();
	}
	return true;
}

void FElysiumGreenRoomRun::DrawArenaOverlays() const
{
	UWorld* World = GetWorld();
	if (!World || !LabViewState.bDrawArenaMarkers)
	{
		return;
	}
	const FVector Origin = ElysiumArena::DefaultOrigin();

	for (const ElysiumArena::FPad& Pad : Arena.Pads)
	{
		const FVector Feet = Origin + Pad.FeetOrigin;
		DrawDebugCircle(World, Feet + FVector(0.0f, 0.0f, 2.0f), 40.0f, 24,
			FColor(90, 170, 255), false, -1.0f, 0, 2.0f,
			FVector(1, 0, 0), FVector(0, 1, 0), /*bDrawAxis=*/false);
		// The facing matters as much as the position: a character spawned looking at a wall spends
		// its first seconds turning, which reads as hesitation rather than as a placement choice.
		DrawDebugDirectionalArrow(World, Feet + FVector(0.0f, 0.0f, 6.0f),
			Feet + FRotator(0.0f, Pad.Yaw, 0.0f).RotateVector(FVector(70.0f, 0.0f, 0.0f))
				+ FVector(0.0f, 0.0f, 6.0f),
			18.0f, FColor(90, 170, 255), false, -1.0f, 0, 2.0f);
		DrawDebugString(World, Feet + FVector(0.0f, 0.0f, 30.0f), Pad.Name.ToString(), nullptr,
			FColor(90, 170, 255), 0.0f, /*bDrawShadow=*/true, 1.0f);
	}

	for (const ElysiumArena::FAnchor& Anchor : Arena.Anchors)
	{
		const FVector Feet = Origin + Anchor.FeetOrigin;
		// Two colours, one distinction: an anchor in the cover block's shadow is somewhere a body is
		// actually hidden, and one in a corner is only somewhere to stand. The entity carries no such
		// field — VtMB's does not either — so it is the spec's claim about the geometry, drawn as one.
		const FColor Colour = Anchor.bAgainstCover ? FColor(120, 230, 130) : FColor(200, 190, 110);
		DrawDebugSphere(World, Feet + FVector(0.0f, 0.0f, 12.0f), 16.0f, 10, Colour,
			false, -1.0f, 0, 2.0f);
		DrawDebugDirectionalArrow(World, Feet + FVector(0.0f, 0.0f, 12.0f),
			Feet + FRotator(0.0f, Anchor.Yaw, 0.0f).RotateVector(FVector(50.0f, 0.0f, 0.0f))
				+ FVector(0.0f, 0.0f, 12.0f),
			14.0f, Colour, false, -1.0f, 0, 2.0f);
		DrawDebugString(World, Feet + FVector(0.0f, 0.0f, 44.0f),
			FString::Printf(TEXT("%s (r%d)"), *Anchor.Name.ToString(), Anchor.Rating), nullptr,
			Colour, 0.0f, /*bDrawShadow=*/true, 1.0f);
	}

	// Where the player is put back to. Drawn because `ArenaSeatPlayer` is a button and a button
	// whose destination is invisible is a button nobody presses twice.
	const FVector Start = Origin + Arena.PlayerFeet;
	DrawDebugCircle(World, Start + FVector(0.0f, 0.0f, 2.0f), 46.0f, 24, FColor(230, 120, 120),
		false, -1.0f, 0, 2.5f, FVector(1, 0, 0), FVector(0, 1, 0), /*bDrawAxis=*/false);

	// The hand-dropped `gr_pin` waypoints and, while a walk is running, what it is doing.
	for (const TPair<FName, FVector>& Pin : ArenaPinList)
	{
		DrawDebugSphere(World, Pin.Value + FVector(0.0f, 0.0f, 10.0f), 14.0f, 8,
			FColor(255, 165, 0), false, -1.0f, 0, 2.0f);
		DrawDebugString(World, Pin.Value + FVector(0.0f, 0.0f, 34.0f), Pin.Key.ToString(), nullptr,
			FColor(255, 165, 0), 0.0f, /*bDrawShadow=*/true, 1.0f);
	}
	if (ArenaWalkState.bActive)
	{
		DrawDebugString(World, Origin + FVector(0.0f, 0.0f, 80.0f), ArenaWalkStatus(), nullptr,
			FColor(255, 165, 0), 0.0f, /*bDrawShadow=*/true, 1.2f);
	}
}

// Arena navigation pins.

bool FElysiumGreenRoomRun::ArenaSetPin(const FName& Name, const FVector* FeetWorld, FString& OutError)
{
	if (!IsArena())
	{
		OutError = TEXT("not in the arena");
		return false;
	}
	if (Name.IsNone())
	{
		OutError = TEXT("name the pin");
		return false;
	}

	FVector Position;
	if (FeetWorld != nullptr)
	{
		Position = *FeetWorld;
	}
	else
	{
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(GetWorld());
		if (!Refs)
		{
			OutError = TEXT("no driven body to read a position from — name x y z explicitly");
			return false;
		}
		Position = Refs.Pawn->GetActorLocation()
			- FVector(0.0f, 0.0f, Refs.Body->GetBodyHalfHeight());
	}

	for (TPair<FName, FVector>& Pin : ArenaPinList)
	{
		if (Pin.Key == Name)
		{
			Pin.Value = Position;
			return true;
		}
	}
	ArenaPinList.Emplace(Name, Position);
	return true;
}

const FVector* FElysiumGreenRoomRun::FindArenaPin(const FName& Name) const
{
	for (const TPair<FName, FVector>& Pin : ArenaPinList)
	{
		if (Pin.Key == Name)
		{
			return &Pin.Value;
		}
	}
	return nullptr;
}

FString FElysiumGreenRoomRun::LabWeaponStatus() const
{
	const AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* World = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	if (World == nullptr)
	{
		return FString(TEXT("(no entity world)"));
	}
	FElysiumPlayer* PlayerEnt = World->FindPlayer();
	if (PlayerEnt == nullptr)
	{
		return FString(TEXT("(no driven character)"));
	}

	const FString Buttons = ElysiumInput::DescribeButtons(World->GetPlayerButtons());
	FElysiumItem* Item = PlayerEnt->Inventory.Active(*PlayerEnt);
	FElysiumWeapon* Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		return FString::Printf(TEXT("%s  buttons %s"),
			Item != nullptr ? *Item->ClassName() : TEXT("(empty hand)"), *Buttons);
	}

	// A melee transaction estimates no commit instant — its contact is the swept walk over the clip's
	// own authored records — so the readout names where that walk stands instead.
	const FString SwingText = Weapon->Swing.bActive
		? FString::Printf(TEXT("#%d %s %s"), Weapon->Swing.Serial, *Weapon->Swing.Activity,
			Weapon->Swing.bMelee
				? *FString::Printf(TEXT("walk cycle %.3f"), Weapon->Swing.PrevCycle)
				: *FString::Printf(TEXT("commit %.3f"), Weapon->Swing.CommitTime))
		: FString(TEXT("(idle)"));
	return FString::Printf(
		TEXT("%s  next 1st %.3f / 2nd %.3f  swing %s (%d accepted)  buttons %s"),
		*Weapon->ClassName(), Weapon->NextPrimaryAttackTime, Weapon->NextSecondaryAttackTime,
		*SwingText, Weapon->AcceptedSwingCount(), *Buttons);
}

TArray<FString> FElysiumGreenRoomRun::LabOverlayStatus() const
{
	TArray<FString> Rows;
	const AElysiumMapActor* Map = GetMap();
	if (Map == nullptr)
	{
		Rows.Add(TEXT("(no map actor)"));
		return Rows;
	}
	const FElysiumAnimationSelection& Selection = Map->GetPlayerAnimSelection();
	for (int32 SlotIndex = 0; SlotIndex < ElysiumOverlay::NumSlots; ++SlotIndex)
	{
		const FElysiumOverlaySlotRecord& Row = Selection.Slots[SlotIndex];
		if (!Row.IsValid())
		{
			Rows.Add(FString::Printf(TEXT("%d  (free)"), SlotIndex));
			continue;
		}
		// Weight and cycle together, because either alone misreads: a layer at cycle 0.98 SHOULD be
		// near zero weight on its way out, and one at cycle 0.5 should not.
		Rows.Add(FString::Printf(
			TEXT("%d  %s@%s  %s  w %.3f  cycle %.3f  age %.2fs%s"),
			SlotIndex, *Row.Label,
			Row.OwnerStem.IsEmpty() ? TEXT("?") : *Row.OwnerStem,
			Row.Activity.IsEmpty() ? TEXT("(no activity)") : *Row.Activity,
			Row.Weight, Row.Cycle, Row.AgeSeconds,
			Row.bFinished ? TEXT("  [finished]") : TEXT("")));
	}
	return Rows;
}

AElysiumNpcBody* FElysiumGreenRoomRun::FindArenaCastBody(FElysiumEntityWorld& World,
	const FString& TargetName) const
{
	// The same targetname resolution `elysium_entity_fire`/`ent_fire` use, narrowed to a live
	// record that actually has a skeletal body riding an `AElysiumNpcBody` motor — a brush, a
	// prop, or an inert record names nothing this feature can walk.
	for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
	{
		if (!Entity || Entity->IsDead())
		{
			continue;
		}
		if (!Entity->TargetName.Equals(TargetName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (USkeletalMeshComponent* Skeletal = Entity->GetSkeletalBody())
		{
			if (AElysiumNpcBody* Body = Cast<AElysiumNpcBody>(Skeletal->GetAttachParentActor()))
			{
				return Body;
			}
		}
	}
	return nullptr;
}

bool FElysiumGreenRoomRun::ArenaWalkStart(const FString& Target, const TArray<FName>& PinNames,
	bool bLoop, FString& OutError)
{
	if (!IsArena())
	{
		OutError = TEXT("not in the arena");
		return false;
	}
	if (PinNames.Num() == 0)
	{
		OutError = TEXT("name at least one pin");
		return false;
	}

	TArray<FVector> Route;
	Route.Reserve(PinNames.Num());
	for (const FName& PinName : PinNames)
	{
		const FVector* Found = FindArenaPin(PinName);
		if (Found == nullptr)
		{
			OutError = FString::Printf(
				TEXT("no pin named '%s' — drop it first with elysium.gr_pin"), *PinName.ToString());
			return false;
		}
		Route.Add(*Found);
	}

	// Whatever the previous order was is superseded, not stacked — a second `gr_walk` names a new
	// whole order, the way a fresh MoveTo replaces the one before it.
	ArenaWalkStop();

	ArenaWalkState.TargetName = Target;
	ArenaWalkState.Route = Route;
	ArenaWalkState.bLoop = bLoop;
	ArenaWalkState.LegIndex = 0;

	if (Target.Equals(TEXT("player"), ESearchCase::IgnoreCase))
	{
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(GetWorld());
		AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
		if (!Refs || PC == nullptr || PC->GetInputRouter() == nullptr)
		{
			OutError = TEXT("no driven player body to walk");
			ArenaWalkState = FArenaWalkState();
			return false;
		}
		ArenaWalkState.bTargetIsPlayer = true;
		ArenaWalkState.bActive = true;
		// The first leg is issued by the next TickArenaWalkPlayer tick, which reads the body's live
		// position rather than one taken here — there is nothing to precompute.
		return true;
	}

	AElysiumMapActor* Map = GetMap();
	FElysiumEntityWorld* World = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	if (World == nullptr)
	{
		OutError = TEXT("no entity world loaded");
		ArenaWalkState = FArenaWalkState();
		return false;
	}
	AElysiumNpcBody* Body = FindArenaCastBody(*World, Target);
	if (Body == nullptr)
	{
		OutError = FString::Printf(
			TEXT("no arena character named '%s' — spawn one from the AI window first"), *Target);
		ArenaWalkState = FArenaWalkState();
		return false;
	}
	ArenaWalkState.NpcBody = Body;
	ArenaWalkState.bActive = true;
	IssueNextArenaWalkLeg();
	return true;
}

void FElysiumGreenRoomRun::ArenaWalkStop()
{
	if (!ArenaWalkState.bActive)
	{
		return;
	}
	if (!ArenaWalkState.bTargetIsPlayer)
	{
		if (AElysiumNpcBody* Body = ArenaWalkState.NpcBody.Get())
		{
			Body->Stop();
		}
	}
	else
	{
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(GetWorld());
		AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
		if (UElysiumInputRouter* Router = PC != nullptr ? PC->GetInputRouter() : nullptr)
		{
			if (Router->IsReplaying())
			{
				Router->StopReplay();
			}
		}
	}
	ArenaWalkState = FArenaWalkState();
}

FString FElysiumGreenRoomRun::ArenaWalkStatus() const
{
	if (!ArenaWalkState.bActive)
	{
		return TEXT("(not walking)");
	}
	if (!ArenaWalkState.bTargetIsPlayer)
	{
		return FString::Printf(TEXT("%s -> pin %d/%d%s"), *ArenaWalkState.TargetName,
			ArenaWalkState.LegIndex + 1, ArenaWalkState.Route.Num(),
			ArenaWalkState.bLoop ? TEXT(" (loop)") : TEXT(""));
	}
	return FString::Printf(TEXT("player walking %d pin(s)%s"), ArenaWalkState.Route.Num(),
		ArenaWalkState.bLoop ? TEXT(" (loop)") : TEXT(""));
}

void FElysiumGreenRoomRun::IssueNextArenaWalkLeg()
{
	AElysiumNpcBody* Body = ArenaWalkState.NpcBody.Get();
	if (Body == nullptr || !ArenaWalkState.Route.IsValidIndex(ArenaWalkState.LegIndex))
	{
		return;
	}
	// Named rather than left to the fallback: a body that resolves no walk fan still gets a
	// non-zero order, the way `ElysiumNpcGait::TravelSpeed` covers the same gap for a real order.
	const float Speed = Body->GaitSpeed(EElysiumNpcGaitKind::Walk, 0.0f);
	Body->MoveTo(ArenaWalkState.Route[ArenaWalkState.LegIndex], /*AcceptanceRadiusCm=*/32.0f,
		Speed > 0.0f ? Speed : ElysiumMove::WalkSpeed, /*bAllowPartialPath=*/true,
		EElysiumNpcGaitKind::Walk);
	ArenaWalkState.bLegArmed = false;
}

void FElysiumGreenRoomRun::TickArenaWalkNpc(AElysiumNpcBody& Body)
{
	FVector Feet = FVector::ZeroVector;
	float Yaw = 0.0f;
	const EElysiumNpcMoveStatus Status = Body.Sample(Feet, Yaw);

	if (Status == EElysiumNpcMoveStatus::Moving)
	{
		ArenaWalkState.bLegArmed = true;
		return;
	}
	if (Status == EElysiumNpcMoveStatus::Failed)
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("gr_walk: '%s' failed to reach pin %d of %d — stopping"),
			*ArenaWalkState.TargetName, ArenaWalkState.LegIndex + 1, ArenaWalkState.Route.Num());
		ArenaWalkStop();
		return;
	}
	// Reached is the ordinary completion; an Idle sampled only after this leg was actually seen
	// Moving is the same thing read off a status that does not distinguish "arrived" from "never
	// started" on its own.
	if (Status == EElysiumNpcMoveStatus::Reached
		|| (Status == EElysiumNpcMoveStatus::Idle && ArenaWalkState.bLegArmed))
	{
		++ArenaWalkState.LegIndex;
		if (!ArenaWalkState.Route.IsValidIndex(ArenaWalkState.LegIndex))
		{
			if (!ArenaWalkState.bLoop)
			{
				UE_LOG(LogElysiumGreenRoom, Log, TEXT("gr_walk: '%s' reached the last pin"),
					*ArenaWalkState.TargetName);
				ArenaWalkStop();
				return;
			}
			ArenaWalkState.LegIndex = 0;
		}
		IssueNextArenaWalkLeg();
	}
}

void FElysiumGreenRoomRun::TickArenaWalkPlayer()
{
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(GetWorld());
	AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
	UElysiumInputRouter* Router = PC != nullptr ? PC->GetInputRouter() : nullptr;
	if (!Refs || Router == nullptr)
	{
		ArenaWalkStop();
		return;
	}
	if (!ArenaWalkState.Route.IsValidIndex(ArenaWalkState.LegIndex))
	{
		ArenaWalkStop();
		return;
	}

	const FVector Feet = Refs.Pawn->GetActorLocation()
		- FVector(0.0f, 0.0f, Refs.Body->GetBodyHalfHeight());
	FVector Delta = ArenaWalkState.Route[ArenaWalkState.LegIndex] - Feet;
	Delta.Z = 0.0f;
	const float Distance = static_cast<float>(Delta.Size());

	// The same radius IssueNextArenaWalkLeg hands the motor for the NPC case, so the two cases
	// call a pin "reached" by the same standard.
	constexpr float AcceptanceRadiusCm = 32.0f;
	if (Distance <= AcceptanceRadiusCm)
	{
		++ArenaWalkState.LegIndex;
		if (!ArenaWalkState.Route.IsValidIndex(ArenaWalkState.LegIndex))
		{
			if (!ArenaWalkState.bLoop)
			{
				UE_LOG(LogElysiumGreenRoom, Log, TEXT("gr_walk: player reached the last pin"));
				ArenaWalkStop();
				return;
			}
			ArenaWalkState.LegIndex = 0;
		}
		// The next leg's command is this same function's job, one tick from now, over the fresh
		// position it will read then — nothing to issue on the arrival tick itself.
		return;
	}

	// Turn and walk in the same command, recomputed fresh every tick from the live position: the
	// turn is idempotent (a body already facing the pin gets a ~0 delta) and self-correcting, so
	// there is no separate "aim, then walk straight" phase to fall out of step with reality.
	const float TargetYaw = static_cast<float>(Delta.Rotation().Yaw);
	const float CurrentYaw = static_cast<float>(Refs.PC->GetControlRotation().Yaw);

	FElysiumUserCmd Cmd;
	Cmd.LookDelta = FVector2D(FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw), 0.0f);
	Cmd.Move = FVector2D(1.0f, 0.0f);
	Cmd.Buttons = static_cast<uint64>(EElysiumButton::Forward);

	// One command, replayed once: `UElysiumInputRouter::SampleFrame` re-times whatever it reads
	// off `Replay` against the real frame's own delta and consumes exactly one entry per call, so
	// a single-entry stream reissued every tick is "drive this frame" — never a fixed span of
	// simulated time, which is what let the first version of this drift with the frame rate.
	FElysiumUserCmdStream OneShot;
	OneShot.Record(Cmd);
	Router->StartReplay(OneShot);
}

void FElysiumGreenRoomRun::TickArenaWalk(float DeltaSeconds)
{
	if (!ArenaWalkState.bActive)
	{
		return;
	}
	if (ArenaWalkState.bTargetIsPlayer)
	{
		TickArenaWalkPlayer();
		return;
	}
	// An NPC case whose body no longer resolves — killed, or the room torn down under it — has
	// nothing left to walk. This is not the player case falling through: bTargetIsPlayer above is
	// what tells the two apart, so a dead character stops here rather than silently starting to
	// drive the driven body instead.
	AElysiumNpcBody* Body = ArenaWalkState.NpcBody.Get();
	if (Body == nullptr)
	{
		UE_LOG(LogElysiumGreenRoom, Warning,
			TEXT("gr_walk: '%s' no longer exists — stopping"), *ArenaWalkState.TargetName);
		ArenaWalkStop();
		return;
	}
	TickArenaWalkNpc(*Body);
}
