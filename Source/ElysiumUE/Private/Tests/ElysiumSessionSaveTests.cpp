#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumSessionSubsystem.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumPlayer.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Tests/ElysiumSaveTestHelpers.h"
#include "Tests/ElysiumTestServices.h"

// Only the resource-ready wiring is supplied by the fixture. Globals, inputs, capture,
// command dispatch, native asynchronous writing, reading and queue restore are production code.
struct FElysiumSessionSaveFixture
{
	FElysiumRecordingServices Services;
	FPlayerWorldFixture Host; // destroyed before the service implementations its entities reference
	UElysiumSessionSubsystem* Session = nullptr;
	UElysiumGameFlowSubsystem* Flow = nullptr;
	FElysiumEntityWorld* World = nullptr;
	TArray<FString> CreatedSlots;
	FString Named = TEXT("sg01-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TArray<EElysiumSaveOperationState> Phases;
	double Deadline = FPlatformTime::Seconds() + 60.0;

	bool Start(FAutomationTestBase& Test)
	{
		if (!Host.TestWorld.CreateTestWorld(EWorldType::Game)) return false;
		Host.World = Host.TestWorld.GetTestWorld();
		// A save fixture supplies a running map; booting the application's front-end game mode
		// here would schedule an unrelated OpenLevel before the fixture can be wired.
		Host.World->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
		if (!Host.TestWorld.BeginPlayInTestWorld() || !Host.SpawnMapActorDeferred()) return false;
		UGameInstance* GI = Host.World->GetGameInstance();
		Session = GI->GetSubsystem<UElysiumSessionSubsystem>();
		Flow = GI->GetSubsystem<UElysiumGameFlowSubsystem>();
		if (!Session || !Flow) return false;
		GI->GetSubsystem<UElysiumMapSubsystem>()->CurrentMap = Host.MapActor;
		Flow->State = EElysiumAppState::Playing;
		Services.bHasPlayer = true;
		auto Entities = MakePimpl<FElysiumEntityWorld>(nullptr, Session, Services.Bundle());
		World = Entities.Get();
		World->Load(ElysiumSaveTestHelpers::MakeSaveTestDefs());
		World->SpawnPlayer();
		World->Activate(0.0);
		Host.MapActor->AdoptEntityWorldForTests(MoveTemp(Entities));
		Session->OnSaveResult().AddLambda([this](const FElysiumSaveResult& Result) { Phases.Add(Result.State); });
		return true;
	}
	~FElysiumSessionSaveFixture()
	{
		if (Session) Session->OnSaveResult().Clear();
		if (World) World->Detach();
		// The deferred map actor never finishes spawning, so it gets no EndPlay and its adopted
		// entity world would otherwise be torn down by the next garbage collection, after the
		// recording services it reaches through the embodiment seam are gone. Release it here,
		// while Services is still alive.
		if (Host.MapActor) Host.MapActor->AdoptEntityWorldForTests(TPimplPtr<FElysiumEntityWorld>());
		World = nullptr;
		for (const FString& Slot : CreatedSlots) UGameplayStatics::DeleteGameInSlot(Slot, 0);
	}
};

class FElysiumAwaitSave final : public IAutomationLatentCommand
{
public:
	FElysiumAwaitSave(FAutomationTestBase* InTest, TSharedRef<FElysiumSessionSaveFixture> InFixture)
		: Test(InTest), Fixture(InFixture) {}
	bool Update() override
	{
		auto& F = Fixture.Get();
		const FElysiumSaveResult Result = F.Session->LastSaveResult();
		if (Result.State == EElysiumSaveOperationState::Writing)
		{
			if (FPlatformTime::Seconds() < F.Deadline) return false;
			Test->AddError(TEXT("native save completion timed out")); return true;
		}
		if (!Test->TestTrue(TEXT("native write completed successfully"), Result.State == EElysiumSaveOperationState::Written)) return true;
		FString Error, Slot;
		FElysiumSavePayload Saved;
		if (!Test->TestTrue(TEXT("native reader accepts completed envelope with case-insensitive name"),
			F.Session->ReadSlotPayload(Result.Slot.ToUpper(), Saved, Error))) { Test->AddError(Error); return true; }
		if (Phase == 0)
		{
			const auto* Flag = Saved.Session.Globals.FindByPredicate([](const auto& Pair) { return Pair.Key == TEXT("SG01_Witness"); });
			Test->TestTrue(TEXT("saved global retains pre-acceptance value"), Flag && Flag->Value.ToInt() == 17);
			Test->TestEqual(TEXT("live global changed independently"), F.Session->GetGlobalInt(TEXT("SG01_Witness")), 99);
			Test->TestEqual(TEXT("capture/writing/completion each published once"), F.Phases.Num(), 3);
			if (F.Phases.Num() == 3)
			{
				Test->TestTrue(TEXT("capturing precedes writing"), F.Phases[0] == EElysiumSaveOperationState::Capturing && F.Phases[1] == EElysiumSaveOperationState::Writing);
			}
			FElysiumEntityWorld Restored(nullptr, nullptr);
			Restored.Load(ElysiumSaveTestHelpers::MakeSaveTestDefs());
			Restored.SpawnPlayer();
			Restored.Activate(0.0);
			const FElysiumMapSnapshot* Map = Saved.Maps.Find(TEXT("__save_test__"));
			if (!Test->TestNotNull(TEXT("native payload contains current map"), Map)) return true;
			Test->TestEqual(TEXT("every captured entity record applied"), Restored.ApplySnapshot(*Map), Map->Entities.Num());
			auto Value = [&]() { return ElysiumSaveTestHelpers::SaveTestCounterValue(Restored.FindByName(TEXT("counter1"))); };
			Test->TestEqual(TEXT("registered counter field retains captured value"), Value(), 5.0f);
			Restored.Tick(9.0);
			Test->TestEqual(TEXT("saved output not dispatched early"), Value(), 5.0f);
			Restored.Tick(10.0);
			Test->TestEqual(TEXT("next saved delivery applies once at deadline"), Value(), 8.0f);
			Restored.Tick(10.1);
			Test->TestEqual(TEXT("next delivery does not repeat"), Value(), 8.0f);
			Test->TestEqual(TEXT("save did not transfer maps into run cache"), F.Session->MapSnapshots().Num(), 0);
			FElysiumSaveStorage Storage;
			Test->TestTrue(TEXT("bare save can resolve unused manual slot"), Storage.ResolveSlotName(EElysiumSaveKind::Manual, TEXT(""), Slot, Error));
			Test->TestFalse(TEXT("selected manual slot unused before acceptance"), UGameplayStatics::DoesSaveGameExist(Slot, 0));
			const FString ExpectedSlot = Slot;
			if (!Test->TestTrue(TEXT("bare save accepted by owner"), F.Session->ExecuteSaveCommand({}, Slot, Error))) return true;
			Test->TestEqual(TEXT("bare save uses selected unused slot"), Slot, ExpectedSlot);
			F.CreatedSlots.Add(Slot);
			Phase = 1;
			return false;
		}
		if (Phase == 1)
		{
			const FString UiSlot = F.Named; // replace only this fixture's own named slot
			if (!Test->TestTrue(TEXT("UI/manual forwarding route accepts save"), F.Flow->SaveGame(UiSlot, EElysiumSaveKind::Manual))) return true;
			Test->TestEqual(TEXT("UI request publishes through same session owner"), F.Session->LastSaveResult().OperationId, Result.OperationId + 1);
			F.CreatedSlots.AddUnique(UiSlot);
			Phase = 2;
			return false;
		}
		const auto* Replaced = Saved.Session.Globals.FindByPredicate([](const auto& Pair) { return Pair.Key == TEXT("SG01_Witness"); });
		Test->TestTrue(TEXT("named replacement contains the later state"), Replaced && Replaced->Value.ToInt() == 99);
		// A world with no player refuses every caller before writing reserved user slots.
		F.World->ForgetPlayer();
		const uint64 Before = F.Session->LastSaveResult().OperationId;
		Test->TestFalse(TEXT("quick client reaches common admission"), F.Flow->SaveGame(TEXT(""), EElysiumSaveKind::Quick));
		Test->TestEqual(TEXT("quick request observed by owner"), F.Session->LastSaveResult().OperationId, Before + 1);
		Test->TestFalse(TEXT("auto client reaches common admission"), F.Flow->SaveGame(TEXT(""), EElysiumSaveKind::Auto));
		Test->TestEqual(TEXT("auto request observed by owner"), F.Session->LastSaveResult().OperationId, Before + 2);
		return true;
	}
private:
	FAutomationTestBase* Test;
	TSharedRef<FElysiumSessionSaveFixture> Fixture;
	int32 Phase = 0;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSessionNamedSaveTest, "Elysium.Session.SaveNamedSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumSessionNamedSaveTest::RunTest(const FString&)
{
	auto F = MakeShared<FElysiumSessionSaveFixture>();
	if (!TestTrue(TEXT("running fixture created"), F->Start(*this))) return false;
	FString Slot, Error;
	for (const FString& Invalid : {FString(TEXT("../escape")), FString(TEXT("C:\\save")), FString(TEXT("bad.sav")),
		FString(TEXT("two slots")), FString(TEXT("Quick")), FString(TEXT("Auto2")), FString::ChrN(65, 'a')})
	{
		TestFalse(TEXT("invalid/manual-reserved slot rejected"), F->Session->ExecuteSaveCommand({Invalid}, Slot, Error));
		TestFalse(TEXT("rejection explains reason"), Error.IsEmpty());
	}
	TestFalse(TEXT("extra console arguments rejected"), F->Session->ExecuteSaveCommand({TEXT("one"), TEXT("two")}, Slot, Error));
	F->Phases.Reset();
	F->Session->SetGlobalInt(TEXT("SG01_Witness"), 17);
	F->World->EnqueueInput(TEXT("counter1"), TEXT("Add"), FElysiumVariant::Int(5), 0.0, {}, {});
	F->World->Tick(0.0);
	F->World->EnqueueInput(TEXT("counter1"), TEXT("Add"), FElysiumVariant::Int(3), 10.0, {}, {});
	IConsoleObject* Object = IConsoleManager::Get().FindConsoleObject(TEXT("elysium.save"));
	IConsoleCommand* Command = Object ? Object->AsCommand() : nullptr;
	if (!TestNotNull(TEXT("exact native console command registered"), Command)) return false;
	Command->Execute({F->Named}, F->Host.World, *GLog);
	if (!TestTrue(TEXT("console request accepted for writing"), F->Session->LastSaveResult().State == EElysiumSaveOperationState::Writing)) return false;
	F->CreatedSlots.Add(F->Named);
	FElysiumSavePayload Busy;
	TestFalse(TEXT("read during accepted write refuses stale slot"), F->Session->ReadSlotPayload(F->Named, Busy, Error));
	TestFalse(TEXT("admission diagnostic also reports write in progress"), F->Session->CanSave(Error));
	TestFalse(TEXT("second save cannot race first"), F->Session->RequestSave({EElysiumSaveKind::Manual, F->Named}, Slot, Error));
	F->Session->SetGlobalInt(TEXT("SG01_Witness"), 99);
	F->World->EnqueueInput(TEXT("counter1"), TEXT("Add"), FElysiumVariant::Int(20), 0.0, {}, {});
	F->World->Tick(0.0);
	TestEqual(TEXT("live field changes after snapshot acceptance"), ElysiumSaveTestHelpers::SaveTestCounterValue(F->World->FindByName(TEXT("counter1"))), 25.0f);
	ADD_LATENT_AUTOMATION_COMMAND(FElysiumAwaitSave(this, F));
	return true;
}
#endif
