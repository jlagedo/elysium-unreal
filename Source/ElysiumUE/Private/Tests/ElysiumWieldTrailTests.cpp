// The melee weapon-trail VFX's `TrailTip` socket is a synthetic bake-side attachment
// (`wield_corpus.trail_tip_from_geometry`), not a `.mdl`-authored one, so it needs its own
// coverage rather than riding `Elysium.Content.BakedAttachmentSockets`' repeat-name fixture.
//
// Self-skipping: both the wield manifest and the baked mount are gitignored and regenerable.
// The manifest itself is the enumeration source of truth for which stems carry a socket, so this
// discovers them from `items/wield_models.json` rather than naming stems by hand.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumSkeletalSource.h"

#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumWieldTrailTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldTrailSocketTest,
	"Elysium.Content.WieldTrailSockets", GElysiumWieldTrailTestFlags)
bool FElysiumWieldTrailSocketTest::RunTest(const FString&)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FElysiumContentPaths::WieldManifest()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no wield manifest "
			"(run: uv run elysium export bundle items)"));
		return true;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!TestTrue(TEXT("the wield manifest parses"),
		FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return true;
	}
	const TSharedPtr<FJsonObject>* Models = nullptr;
	if (!TestTrue(TEXT("...carrying a models table"), Root->TryGetObjectField(TEXT("models"), Models)))
	{
		return true;
	}

	int32 SocketProp = 0;
	int32 Checked = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Models)->Values)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(Row))
		{
			continue;
		}
		// Absent on every non-`socket_prop` model (`null` in the JSON), which is the ordinary
		// answer for a firearm or a non-socket binding, not a failure.
		const TSharedPtr<FJsonObject>* TrailTip = nullptr;
		if (!(*Row)->TryGetObjectField(TEXT("trail_tip"), TrailTip) || !TrailTip->IsValid())
		{
			continue;
		}
		++SocketProp;
		const FString Stem = Entry.Key;

		USkeletalMesh* const Mesh = LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedWieldMesh(Stem), nullptr, LOAD_NoWarn | LOAD_Quiet);
		FElysiumSkeletalSource Source;
		FString Error;
		if (Mesh == nullptr
			|| !FElysiumSkeletalSource::Load(FElysiumContentPaths::WieldSource(Stem), Source, Error))
		{
			continue;
		}
		++Checked;

		const FElysiumSourceAttachment* const Attachment = Source.Attachments.FindByPredicate(
			[](const FElysiumSourceAttachment& A) { return A.Name == FName(TEXT("TrailTip")); });
		if (!TestNotNull(*FString::Printf(TEXT("%s's .eskm carries a TrailTip attachment"), *Stem),
			Attachment))
		{
			continue;
		}

		const USkeletalMeshSocket* const Socket = Mesh->FindSocket(TEXT("TrailTip"));
		if (!TestNotNull(*FString::Printf(TEXT("%s bakes a TrailTip socket"), *Stem), Socket))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s TrailTip socket binds the mount bone"), *Stem),
			Socket->BoneName, Source.Bones[Attachment->Bone].Name);
		const FTransform Baked(Socket->RelativeRotation, Socket->RelativeLocation,
			Socket->RelativeScale);
		TestTrue(*FString::Printf(
			TEXT("%s TrailTip socket carries the manifest's transform (%s against %s)"),
			*Stem, *Baked.ToString(), *Attachment->Local.ToString()),
			Baked.Equals(Attachment->Local));
	}

	if (SocketProp == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the wield manifest names no socket_prop model"));
		return true;
	}
	if (Checked == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no socket_prop wield model is baked; "
			"run: uv run elysium export wield"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d/%d socket_prop wield model(s) checked"), Checked, SocketProp));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
