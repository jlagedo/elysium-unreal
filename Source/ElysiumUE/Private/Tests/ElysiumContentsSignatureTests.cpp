#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "ElysiumCollisionChannels.h"
#include "ElysiumUseIcons.h"
#include "ElysiumContentsSignature.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/ElysiumPlayerWorldFixture.h"

// 0018 story 3's acceptance, stated as retail states it: a brush answers four masks and the
// answers differ. These build one body per shipped signature and ask it all four questions, which
// is the only way to prove the profiles say what the contents word meant -- the table in
// ElysiumContentsSignature.h decides the name, DefaultEngine.ini decides the responses, and
// neither can be checked by reading the other.
//
// The retail words, for the four questions: does this brush block the PLAYER (0x1400b), an NPC
// (0x2400b), SIGHT (0x804091), and is it a pedestrian volume (0x2000).

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** A box wearing one signature's profile, at the origin, 100 cm on a side. */
	UBoxComponent* MakeSignatureBox(AActor* Owner, EElysiumContentsSignature Signature)
	{
		UBoxComponent* Box = NewObject<UBoxComponent>(Owner);
		Box->InitBoxExtent(FVector(50.0));
		Box->SetCollisionProfileName(ElysiumContents::ProfileName(Signature));
		Box->RegisterComponent();
		return Box;
	}

	/** Does a trace on `Channel` from one side of the box to the other hit anything? */
	bool BlocksChannel(UWorld* World, ECollisionChannel Channel)
	{
		FHitResult Hit;
		return World->LineTraceSingleByChannel(Hit, FVector(-200.0, 0.0, 0.0),
			FVector(200.0, 0.0, 0.0), Channel,
			FCollisionQueryParams(FName(TEXT("ElysiumSignatureProbe")), false));
	}

	/** Would a pawn of this object type be stopped by the box? */
	bool BlocksPawn(UWorld* World, ECollisionChannel PawnChannel)
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FHitResult Hit;
		// A sweep of the pawn's own shape, asking the world whether it would be blocked. The
		// response that matters is the BODY's response to the pawn channel, which is what a
		// capsule sweep consults, so the query is by channel rather than by object type.
		return World->SweepSingleByChannel(Hit, FVector(-200.0, 0.0, 0.0), FVector(200.0, 0.0, 0.0),
			FQuat::Identity, PawnChannel, FCollisionShape::MakeSphere(10.0),
			FCollisionQueryParams(FName(TEXT("ElysiumSignaturePawn")), false));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSignatureProfilesAnswerEachMask,
	"Elysium.Content.ContentsSignature.ProfilesAnswerEachMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumSignatureProfilesAnswerEachMask::RunTest(const FString&)
{
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this))
	{
		return false;
	}
	UWorld* World = Fixture.World;

	using ESig = EElysiumContentsSignature;
	struct FCase
	{
		const TCHAR* Name;
		ESig Signature;
		bool bBlocksPlayer;
		bool bBlocksNpc;
		bool bBlocksSight;
	};
	// The five signatures the two witness maps carry between them, plus the two the rest of the
	// corpus adds. Every one is a real shipped contents word, not a constructed case.
	const FCase Cases[] = {
		// `0x1` solid: the ordinary wall.
		{ TEXT("PNS-"), ESig::Player | ESig::Npc | ESig::Sight, true, true, true },
		// `0x10000002` a window, `0x8030000` both clip bits: stops bodies, not sight.
		{ TEXT("PN--"), ESig::Player | ESig::Npc, true, true, false },
		// `0x8032000` both clips over a pedestrian volume.
		{ TEXT("PN-p"), ESig::Player | ESig::Npc | ESig::Pedestrian, true, true, false },
		// `0x8000080` OPAQUE without SOLID -- a light blocker that stops sight and nothing else.
		{ TEXT("--S-"), ESig::Sight, false, false, true },
		// `0x8020000` MONSTERCLIP alone: the 5 brushes on the tutorial an NPC cannot pass.
		{ TEXT("-N--"), ESig::Npc, false, true, false },
		// `0x8022000` NPC clip over a pedestrian volume: 31 on the hub.
		{ TEXT("-N-p"), ESig::Npc | ESig::Pedestrian, false, true, false },
		// `0x8002000` the roadway slab: priced for a pedestrian route, solid to nobody.
		{ TEXT("---p"), ESig::Pedestrian, false, false, false },
	};

	for (const FCase& Case : Cases)
	{
		AActor* Owner = World->SpawnActor<AActor>();
		Owner->SetRootComponent(NewObject<USceneComponent>(Owner));
		Owner->GetRootComponent()->RegisterComponent();
		UBoxComponent* Box = MakeSignatureBox(Owner, Case.Signature);

		TestEqual(*FString::Printf(TEXT("%s blocks the player"), Case.Name),
			BlocksPawn(World, ElysiumCollision::PlayerChannel), Case.bBlocksPlayer);
		TestEqual(*FString::Printf(TEXT("%s blocks an NPC"), Case.Name),
			BlocksPawn(World, ECC_Pawn), Case.bBlocksNpc);
		TestEqual(*FString::Printf(TEXT("%s blocks sight"), Case.Name),
			BlocksChannel(World, ElysiumCollision::SightChannel), Case.bBlocksSight);
		// Never the world's: the +use ray and the debug pick belong to entities and to the baked
		// render geometry, so no signature body may take either.
		TestFalse(*FString::Printf(TEXT("%s does not take the +use ray"), Case.Name),
			BlocksChannel(World, ELYSIUM_USE_CHANNEL));

		// Navigation relevance is the NPC answer and nothing else. This is the identity the whole
		// design rests on: `UPrimitiveComponent::IsNavigationRelevant` returns true exactly when a
		// body blocks ECC_Pawn (or ECC_Vehicle, which no signature ever sets).
		TestEqual(*FString::Printf(TEXT("%s cuts the NavMesh iff it blocks an NPC"), Case.Name),
			Box->IsNavigationRelevant(),
			ElysiumContents::AffectsNavigation(Case.Signature) && Case.bBlocksNpc);

		Owner->Destroy();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSignatureSightIsNotSolidity,
	"Elysium.Content.ContentsSignature.SightIsNotSolidity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumSignatureSightIsNotSolidity::RunTest(const FString&)
{
	// The two cases that make the whole exercise worth doing, from the real contents words: a
	// window stops both pawns and no sight trace, and an unsolid OPAQUE brush does the reverse.
	// Before story 3 both were the same anonymous BlockAll body, and sight was traced on a channel
	// the world collider ignored outright.
	const uint32 Window = 0x10000002;     // WINDOW | DETAIL
	const uint32 ToolsShadow = 0x08000080;   // OPAQUE | DETAIL, no SOLID

	const EElysiumContentsSignature WindowSignature = ElysiumContents::SignatureOf(Window);
	const EElysiumContentsSignature ShadowSignature = ElysiumContents::SignatureOf(ToolsShadow);

	TestEqual(TEXT("a window reads PN--"), ElysiumContents::Spell(WindowSignature),
		FString(TEXT("PN--")));
	TestEqual(TEXT("a tools_shadow brush reads --S-"), ElysiumContents::Spell(ShadowSignature),
		FString(TEXT("--S-")));

	TestTrue(TEXT("the window cuts the NavMesh"),
		ElysiumContents::AffectsNavigation(WindowSignature));
	TestFalse(TEXT("the shadow brush does not"),
		ElysiumContents::AffectsNavigation(ShadowSignature));

	// Neither WINDOW nor GRATE is in the sight mask, which is why an NPC sees through glass.
	TestEqual(TEXT("glass does not block sight"),
		EnumHasAnyFlags(WindowSignature, EElysiumContentsSignature::Sight), false);
	TestEqual(TEXT("a grate does not either"),
		EnumHasAnyFlags(ElysiumContents::SignatureOf(0x10000008),
			EElysiumContentsSignature::Sight), false);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
