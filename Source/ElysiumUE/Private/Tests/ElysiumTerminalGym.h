#pragma once

// The terminal gym: the `sp_tutorial_1`
// terminal slice spawned **with bodies** at its authored transforms into the empty stage world the
// movement gym already builds, plus the player pawn. It carries the real monitor, padlock, safe and
// door models and nothing else, so everything that needs a body — the screen cone, the pawn pin,
// the camera shot, the projection — is accepted on a controlled surface rather than on a live map.
//
// Two facts shape the build:
//
//  * `AElysiumMapActor::PreparePropAndWieldModels` needs a game instance and the whole placed-model
//    catalogue, which an `FTestWorldWrapper` world has not got. `HasPlacedModelCatalogue()` is
//    therefore false here and `FElysiumTerminal::Spawn` stands no body of its own. The gym reads the
//    **shipped bake** instead, by canonical address, and registers what it stood.
//  * The `$attachment` transforms live on the model's baked *skeletal* asset even when the placed
//    body is the static reduction, so a hidden `SK_` component is registered as the attachment
//    source and the socket transforms are the asset's own rather than a composition.
//
// A model whose baked asset is absent, or whose socket set lacks `screen` / `screen_axis`, is a
// named line in `Seams` — printed by the test, never a silent pass.

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Debug/ElysiumGymBuilder.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumGymSpec.h"
#include "ElysiumMapActor.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPawn.h"
#include "ElysiumSkeletalBasis.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Misc/PackageName.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumMapSlice.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "UObject/SoftObjectPath.h"
#include "Visual/ElysiumPreparedPropModels.h"

struct FElysiumTerminalGym
{
	FPlayerWorldFixture Host;
	// Every component below is created on `Host.MapActor` and added as one of its instance
	// components, so the actor's own reference keeps them alive; these are non-owning views.
	AActor* Stage = nullptr;
	// The clock every entity think is measured on. Held so a case can step it: the screensaver's
	// schedule is the whole of what slice C asserts on this host.
	UElysiumSessionSubsystem* State = nullptr;
	FElysiumMapSlice Slice;
	TMap<FString, UStaticMeshComponent*> Bodies;
	TMap<FString, USkeletalMeshComponent*> Attachments;
	TArray<FString> Seams;

	// The slice needs the export, and the bodies need the baked plugin mount.
	static bool Available(const TCHAR* Map)
	{
		return FElysiumMapSlice::Available(Map) && FPackageName::DoesPackageExist(
			TEXT("/ElysiumBaked/Models/scenery/furniture/computer/SM_monitor_useable"));
	}

	FElysiumEntityWorld* World() const
	{
		return Host.MapActor ? Host.MapActor->GetEntityWorld() : nullptr;
	}

	FElysiumPropHacking* Terminal(const TCHAR* Name) const
	{
		FElysiumEntityWorld* Entities = World();
		FElysiumEntity* Entity = Entities ? Entities->FindByName(Name) : nullptr;
		FElysiumTerminal* Base = Entity ? Entity->AsTerminal() : nullptr;
		return static_cast<FElysiumPropHacking*>(Base);
	}

	bool Build(FAutomationTestBase& Test, const TCHAR* Map, const TArray<FString>& Roots,
		const FVector& PawnFeetCm, float PawnYawDegrees)
	{
		if (!Host.CreateWorld(Test))
		{
			return false;
		}
		// The movement gym's empty stage: boxes, no meshes, so it stands under `-nullrhi`.
		const FElysiumMoveTuning Tuning;
		Stage = ElysiumGym::Spawn(Host.World, ElysiumGym::Build(Tuning), ElysiumGym::DefaultOrigin());

		Host.SpawnPlayerControllerAndPawn(PawnFeetCm, FRotator(0.0f, PawnYawDegrees, 0.0f));
		if (!Host.PlayerController || !Host.Pawn)
		{
			Test.AddError(TEXT("the terminal gym could not stand its player"));
			return false;
		}
		Host.PlayerController->Possess(Host.Pawn);
		Host.PlayerController->SetControlRotation(FRotator(0.0f, PawnYawDegrees, 0.0f));
		if (Host.PlayerController->PlayerCameraManager)
		{
			Host.PlayerController->PlayerCameraManager->UpdateCamera(0.0f);
		}

		// Deferred and never finished: the gym wants the embodiment seam, not the map lifecycle.
		if (!Host.SpawnMapActorDeferred())
		{
			Test.AddError(TEXT("the terminal gym could not stand its map actor"));
			return false;
		}

		FString Error;
		if (!FElysiumMapSlice::Build(Map, Roots, Slice, Error))
		{
			Test.AddError(Error);
			return false;
		}
		Seams.Append(Slice.Seams);

		State = NewObject<UElysiumSessionSubsystem>(
			NewObject<UGameInstance>(GetTransientPackage()));
		FElysiumWorldServices Services;
		Services.Embodiment = Host.MapActor;
		Services.Audio = Host.MapActor;
		Services.Travel = Host.MapActor;
		Services.Weather = Host.MapActor;
		TPimplPtr<FElysiumEntityWorld> Entities =
			MakePimpl<FElysiumEntityWorld>(Host.MapActor, State, Services);
		FElysiumEntityWorld* Raw = Entities.Get();
		Host.MapActor->AdoptEntityWorldForTests(MoveTemp(Entities));

		Raw->Load(MoveTemp(Slice.Defs));
		Raw->SpawnPlayer();
		Raw->Activate(0.0);

		// The bodies come after the spawn pass, because `FElysiumTerminal::Spawn` stands none of its
		// own without the placed-model catalogue. Placement is the entity's live transform and the
		// same rotation rule the production build uses.
		for (const TUniquePtr<FElysiumEntity>& Entity : Raw->Entities())
		{
			if (!Entity || Entity->TargetName.IsEmpty() || !Entity->Model.EndsWith(TEXT(".mdl")))
			{
				continue;
			}
			const bool bDecoded = Entity->Def && !Entity->Def->ModelMesh.IsEmpty();
			const FQuat Rotation = bDecoded
				? Entity->Def->ModelQuat : FQuat(FRotator(0.0f, -Entity->Angles.Y, 0.0f));
			StandBody(*Entity, Entity->Model, Entity->Origin, Rotation);
		}

		// Every terminal re-reads its pair now that a body carries one.
		for (const TUniquePtr<FElysiumEntity>& Entity : Raw->Entities())
		{
			if (FElysiumTerminal* AsTerminal = Entity ? Entity->AsTerminal() : nullptr)
			{
				AsTerminal->ResolveScreenAttachments();
				if (!AsTerminal->bScreenAttachmentsResolved)
				{
					Seams.Add(FString::Printf(TEXT("%s: no '%s' attachment on its baked model"),
						*AsTerminal->TargetName,
						AsTerminal->AttachmentError ? AsTerminal->AttachmentError : TEXT("?")));
				}
			}
		}
		return true;
	}

	// Seat the pawn's box at a feet origin, as `FPlayerWorldFixture::SpawnPawn` does.
	void PlacePawnFeet(const FVector& FeetCm, float YawDegrees)
	{
		if (!Host.Pawn)
		{
			return;
		}
		Host.Pawn->SetActorLocation(FeetCm + FVector(0.0f, 0.0f, ElysiumMove::StandHeight * 0.5f),
			false, nullptr, ETeleportType::TeleportPhysics);
		if (Host.PlayerController)
		{
			Host.PlayerController->SetControlRotation(FRotator(0.0f, YawDegrees, 0.0f));
			if (Host.PlayerController->PlayerCameraManager)
			{
				Host.PlayerController->PlayerCameraManager->UpdateCamera(0.0f);
			}
		}
	}

	// One game frame's worth of the map actor's own order: the entity think pass, then the
	// interaction pass that re-gates and maintains a held session.
	void Frame(double Now)
	{
		if (FElysiumEntityWorld* Entities = World())
		{
			Entities->Tick(Now);
			Entities->UpdatePlayerInteraction();
		}
	}

	double Now() const { return State ? State->GameClock().GetNow() : 0.0; }

	// Step the game clock in bounded frames (`ElysiumFrame::ClampFrameDelta`) and run one gym frame
	// per step, exactly as the map actor's gameplay tick does. A single jump would stamp delayed
	// rows on a clock that had not moved.
	void Advance(double To)
	{
		if (!State)
		{
			return;
		}
		for (int32 Guard = 0; Guard < 8192 && State->GameClock().GetNow() < To; ++Guard)
		{
			State->TimeControl().AdvanceFrame(FMath::Min(0.05, To - State->GameClock().GetNow()));
			Frame(State->GameClock().GetNow());
		}
		Frame(State->GameClock().GetNow());
	}

	FString Report() const
	{
		FString Text = FString::Printf(TEXT("terminal gym: %d bodies, %d attachment sources"),
			Bodies.Num(), Attachments.Num());
		for (const FString& Seam : Seams)
		{
			Text += TEXT("\n  seam: ") + Seam;
		}
		return Text;
	}

private:
	// The shipped bake, by canonical address: `vtmb:model:<key>` -> `SM_`/`SK_` under
	// `/ElysiumBaked/Models/`. This is the same asset pair `FElysiumCataloguePlacedModel` makes
	// resident at runtime, so the gym reads the production data through a different door, not
	// different data.
	void StandBody(FElysiumEntity& Entity, const FString& ModelPath, const FVector& OriginCm,
		const FQuat& Rotation)
	{
		const FString& Name = Entity.TargetName;
		const FString Id = ElysiumPreparedProps::ModelId(ModelPath);
		const FString StaticPath = FElysiumContentPaths::BakedUnit(Id, TEXT("SM"));
		const FString SkeletalPath = FElysiumContentPaths::BakedUnit(Id, TEXT("SK"));

		UStaticMesh* StaticMesh = Cast<UStaticMesh>(FSoftObjectPath(StaticPath).TryLoad());
		if (!StaticMesh)
		{
			Seams.Add(FString::Printf(TEXT("%s: no baked static mesh at %s"), *Name, *StaticPath));
			return;
		}
		UStaticMeshComponent* Body =
			NewObject<UStaticMeshComponent>(Host.MapActor, FName(*(Name + TEXT("_Body"))));
		Body->SetStaticMesh(StaticMesh);
		Body->SetupAttachment(Host.MapActor->GetRootComponent());
		Body->SetWorldLocationAndRotation(OriginCm, Rotation);
		// Solid to the pawn's own sweep AND to `+use`: retail raises `SOLID_BBOX` on the terminal in
		// its Spawn, which is what stops the pin's `MASK_PLAYERSOLID` trace at the machine.
		Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Body->SetCollisionProfileName(TEXT("BlockAll"));
		Body->SetGenerateOverlapEvents(false);
		Body->RegisterComponent();
		Host.MapActor->AddInstanceComponent(Body);
		Bodies.Add(Name, Body);
		Host.MapActor->RegisterUseAnchor(Body, Entity.Handle);

		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(FSoftObjectPath(SkeletalPath).TryLoad());
		if (!SkeletalMesh)
		{
			Seams.Add(FString::Printf(TEXT("%s: no baked skeletal mesh at %s (no attachments)"),
				*Name, *SkeletalPath));
			return;
		}
		USkeletalMeshComponent* Source =
			NewObject<USkeletalMeshComponent>(Host.MapActor, FName(*(Name + TEXT("_Attach"))));
		Source->SetSkeletalMesh(SkeletalMesh);
		Source->SetupAttachment(Host.MapActor->GetRootComponent());
		Source->SetWorldLocationAndRotation(OriginCm, Rotation);
		// Sockets only: it is never drawn, never collided, and never animated.
		Source->SetVisibility(false);
		Source->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Source->SetComponentTickEnabled(false);
		Source->RegisterComponent();
		Host.MapActor->AddInstanceComponent(Source);
		Attachments.Add(Name, Source);
		Host.MapActor->RegisterAttachmentSource(Entity.Handle, Source);
	}
};

#endif   // WITH_DEV_AUTOMATION_TESTS
