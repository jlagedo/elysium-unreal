#include "UI/ElysiumCharacterStage.h"

#include "UI/ElysiumUiArt.h"
#include "Visual/ElysiumAnimSubsystem.h"
#include "Visual/ElysiumNpcVisual.h"
#include "ElysiumFog.h"                     // ElysiumLightStyle::StampUnstyled -- CPD slot 6 neutral

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCharStage, Log, All);

namespace
{
	// The rig, in centimetres, in the stage's own little pocket of the world. The vantage is
	// authored rather than derived: the body stands at the origin facing +X and the camera looks
	// back at it from chest height, which is the framing the retail screen uses — head just under
	// the tab strip, feet just above the footer.
	namespace Stage
	{
		inline constexpr float BodyYaw      = 180.0f;   // face the camera
		inline constexpr float CameraBack   = 190.0f;
		inline constexpr float CameraHeight = 96.0f;
		inline constexpr float CameraFov    = 32.0f;
		// The backdrop hangs behind the body, wide enough to fill the frame at that FOV.
		inline constexpr float BackdropBack = 420.0f;
		inline constexpr float BackdropSize = 340.0f;
		// Far enough from anything a map builds that the rig can never intersect the level.
		inline const FVector Origin(0.0f, 0.0f, 100000.0f);
	}

	const TCHAR* GBackdropArt = TEXT("interface/charactermaintenance/background");
}

FElysiumCharacterStage::~FElysiumCharacterStage()
{
	Teardown();
}

void FElysiumCharacterStage::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Camera);
	Collector.AddReferencedObject(Backdrop);
	Collector.AddReferencedObject(Body);
}

void FElysiumCharacterStage::Raise(UWorld* World, APlayerController* PC)
{
	if (World == nullptr || PC == nullptr || Camera != nullptr)
	{
		return;
	}
	WeakWorld = World;
	WeakPC = PC;
	// Remembered so the in-game screen gives the player's camera back on close. Chargen has none,
	// and a null here is what says so.
	PrevViewTarget = PC->GetViewTarget();

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;

	const FVector CamLoc = Stage::Origin + FVector(-Stage::CameraBack, 0.0f, Stage::CameraHeight);
	Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CamLoc,
		FRotator::ZeroRotator, Params);
	if (Camera == nullptr)
	{
		UE_LOG(LogElysiumCharStage, Warning, TEXT("could not spawn the stage camera"));
		return;
	}
	if (UCameraComponent* Cam = Camera->GetCameraComponent())
	{
		Cam->SetFieldOfView(Stage::CameraFov);
	}
	PC->SetViewTarget(Camera);

	BuildBackdrop();
}

void FElysiumCharacterStage::BuildBackdrop()
{
	UWorld* World = WeakWorld.Get();
	if (World == nullptr || Backdrop != nullptr)
	{
		return;
	}
	// The scene behind the body is a **fixed wallpaper**, not a rendered set: one unlit quad
	// carrying the sheet's own painted street. Without the art there is no quad, and the screen's
	// own scrim is the ground — which is what the panels already assume.
	UTexture2D* Art = ElysiumUI::ArtTexture(GBackdropArt);
	if (Art == nullptr)
	{
		return;
	}

	UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (Plane == nullptr)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Backdrop = World->SpawnActor<AActor>(AActor::StaticClass(),
		Stage::Origin + FVector(Stage::BackdropBack, 0.0f, Stage::CameraHeight),
		// The engine plane faces +Z, so stand it up and turn it to face the camera down -X.
		FRotator(90.0f, 0.0f, 0.0f), Params);
	if (Backdrop == nullptr)
	{
		return;
	}

	UStaticMeshComponent* Quad = NewObject<UStaticMeshComponent>(Backdrop);
	Quad->SetMobility(EComponentMobility::Movable);
	Quad->SetStaticMesh(Plane);
	Backdrop->SetRootComponent(Quad);
	Quad->RegisterComponent();
	Quad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Quad->SetWorldScale3D(FVector(Stage::BackdropSize / 100.0f, Stage::BackdropSize / 100.0f, 1.0f));

	// Unlit and emissive: the wallpaper must read the same whatever the map's lighting is, because
	// the stage borrows whichever world happens to be loaded.
	if (UMaterial* Unlit = LoadObject<UMaterial>(nullptr,
		TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial")))
	{
		if (UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Unlit, Backdrop))
		{
			Mid->SetTextureParameterValue(TEXT("Texture"), Art);
			Quad->SetMaterial(0, Mid);
		}
	}
}

void FElysiumCharacterStage::SetBody(const FString& InStem)
{
	if (InStem == Stem)
	{
		return;
	}
	UWorld* World = WeakWorld.Get();
	if (World == nullptr)
	{
		return;
	}

	if (Body)
	{
		Body->Destroy();
		Body = nullptr;
	}
	Stem = InStem;
	if (Stem.IsEmpty())
	{
		return;
	}

	// The same mount the game's own NPC bodies come off, so what the screen stands is the body a
	// map would stand.
	FString Error;
	USkeletalMesh* Mesh = ElysiumNpcVisual::LoadMesh(Stem, Error);
	if (Mesh == nullptr)
	{
		UE_LOG(LogElysiumCharStage, Warning, TEXT("no body for '%s': %s"), *Stem, *Error);
		return;
	}

	// The standing idle the same policy picks for an NPC — a body frozen in its bind pose reads as
	// broken, and the screen is the one place the player looks at it for a whole minute.
	UAnimSequence* Anim = nullptr;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		if (UElysiumAnimSubsystem* Anims = GI->GetSubsystem<UElysiumAnimSubsystem>())
		{
			EElysiumIdleTier Tier = EElysiumIdleTier::None;
			const FString Clip = Anims->PickIdleClip(Stem, FString(), Tier);
			if (!Clip.IsEmpty())
			{
				FString AnimError;
				Anim = Anims->ResolveClip(Stem, Clip, Mesh, AnimError);
			}
		}
	}

	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Body = World->SpawnActor<AActor>(AActor::StaticClass(), Stage::Origin,
		FRotator(0.0f, Stage::BodyYaw, 0.0f), Params);
	if (Body == nullptr)
	{
		return;
	}

	USkeletalMeshComponent* Component = NewObject<USkeletalMeshComponent>(Body);
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetSkeletalMeshAsset(Mesh);
	ElysiumLightStyle::StampUnstyled(Component);   // R7.4 (G6): slot 6 neutral or it renders black
	Body->SetRootComponent(Component);
	Component->RegisterComponent();
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Anim)
	{
		Component->PlayAnimation(Anim, /*bLooping*/ true);
	}
}

void FElysiumCharacterStage::Teardown()
{
	// The player's own camera comes back before the rig goes, so there is never a frame with no
	// view target. Chargen has nothing to restore, and the null is what says so.
	if (APlayerController* PC = WeakPC.Get())
	{
		if (AActor* Prev = PrevViewTarget.Get())
		{
			PC->SetViewTarget(Prev);
		}
	}
	for (TObjectPtr<AActor>* Slot : { &Backdrop, &Body })
	{
		if (*Slot)
		{
			(*Slot)->Destroy();
			*Slot = nullptr;
		}
	}
	if (Camera)
	{
		Camera->Destroy();
		Camera = nullptr;
	}
	Stem.Reset();
	PrevViewTarget.Reset();
	WeakPC.Reset();
	WeakWorld.Reset();
}
