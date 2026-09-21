#include "Visual/ElysiumRopeActor.h"

#include "Components/BillboardComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

AElysiumRopeActor::AElysiumRopeActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);

	// A plain scene root, as the infrastructure actors have: the billboard is editor-only, and a
	// cooked actor still needs a root for the location `BuildRopes` reads back as endpoint A.
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SceneRoot->SetMobility(EComponentMobility::Static);
	RootComponent = SceneRoot;

#if WITH_EDITORONLY_DATA
	Billboard = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Billboard != nullptr)
	{
		static ConstructorHelpers::FObjectFinderOptional<UTexture2D> NoteSprite(
			TEXT("/Engine/EditorResources/S_Note"));
		Billboard->Sprite = NoteSprite.Get();
		Billboard->SetupAttachment(SceneRoot);
		Billboard->bIsScreenSizeScaled = true;
	}
#endif
}

void AElysiumRopeActor::ConfigureRope(int32 InSourceIndex, const FString& MaterialId,
	FVector A, FVector B, float WidthCm, float RestCm, int32 Nodes, float TexScale, int32 Flags)
{
	SourceIndex = InSourceIndex;
	Rope.MaterialId = MaterialId;
	Rope.A = A;
	Rope.B = B;
	Rope.WidthCm = WidthCm;
	Rope.RestCm = RestCm;
	Rope.Nodes = Nodes;
	Rope.TexScale = TexScale;
	// The producer's flag word is four bits (`Dangling`/`Collide`/`Barbed`/`Breakable`); the
	// parameter is `int32` only because Python has no narrower integer to hand over.
	Rope.Flags = static_cast<uint8>(Flags & 0xFF);
}
