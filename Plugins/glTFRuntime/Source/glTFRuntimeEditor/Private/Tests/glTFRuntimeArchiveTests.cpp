// Copyright 2026 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeEditor.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Archive_Tar_Dummy, "glTFRuntime.UnitTests.Archive.Tar.Dummy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Archive_Tar_Dummy::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Dummy.tar.gz");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAsBlob = true;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	TestTrue("Asset->IsArchive()", Asset->IsArchive());

	TArray<FString> Items = Asset->GetArchiveItems();
	Items.Sort();

	TestEqual("", Items, { "zip000/", "zip000/000", "zip000/zip001/", "zip000/zip001/001", "zip000/zip001/zip002/", "zip000/zip001/zip002/002" });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Archive_Tar_DummyNoRoot, "glTFRuntime.UnitTests.Archive.Tar.DummyNoRoot", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Archive_Tar_DummyNoRoot::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("DummyNoRoot.tar.gz");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAsBlob = true;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	TestTrue("Asset->IsArchive()", Asset->IsArchive());

	TArray<FString> Items = Asset->GetArchiveItems();
	Items.Sort();

	TestEqual("", Items, { "000", "zip001/001", "zip001/zip002/", "zip001/zip002/002" });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Archive_Tar_DummyUriRewrite, "glTFRuntime.UnitTests.Archive.Tar.DummyUriRewrite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Archive_Tar_DummyUriRewrite::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Dummy.tar.gz");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAsBlob = true;
	LoaderConfig.ArchiveUriRewriterHook.NativeUriRewriter.BindLambda([](const FString& Key, UObject* Context) {
		return "test_" + Key;
		});
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	TestTrue("Asset->IsArchive()", Asset->IsArchive());

	TArray<FString> Items = Asset->GetArchiveItems();
	Items.Sort();

	TestEqual("", Items, { "test_zip000/", "test_zip000/000", "test_zip000/zip001/", "test_zip000/zip001/001", "test_zip000/zip001/zip002/", "test_zip000/zip001/zip002/002" });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Archive_Zip_Dummy, "glTFRuntime.UnitTests.Archive.Zip.Dummy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Archive_Zip_Dummy::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Dummy.zip");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAsBlob = true;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	TestTrue("Asset->IsArchive()", Asset->IsArchive());

	TArray<FString> Items = Asset->GetArchiveItems();
	Items.Sort();

	TestEqual("", Items, { "zip000/", "zip000/000", "zip000/zip001/", "zip000/zip001/001", "zip000/zip001/zip002/", "zip000/zip001/zip002/002" });

	return true;
}

#endif
