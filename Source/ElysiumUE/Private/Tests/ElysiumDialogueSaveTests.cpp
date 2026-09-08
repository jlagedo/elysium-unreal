// D7 — M-SAVE: no save inside a conversation, no exceptions.
//
// Retail allows saving mid-dialogue; the port refuses every entry while a `DialogueSession` is set.
// The refusal has exactly one gate, and every entry reaches it:
//
//   * pause-menu Save        — `UElysiumMainMenu::Execute` -> `UElysiumGameFlowSubsystem::SaveGame`
//   * quick-save key / console — the `save` command binding -> `SaveGame(Quick|Manual)`
//   * autosave triggers      — `trigger_autosave` -> `SaveGame(FString(), Auto)`
//   * `elysium.save.cansave` — reports `UElysiumSaveSubsystem::CanSave` verbatim
//   * MCP                    — the toolset exposes no save verb (audited 2026-09-06)
//
// All four writers funnel through `UElysiumSaveSubsystem::Save`, whose first act is `CanSave`,
// which asks `FElysiumEntityWorld::ScriptedSessionSaveBlockReason()`. There is no second writer:
// `Save` is the only caller of `BuildPayload` + `ElysiumSave::Write` on the write path. This case
// therefore proves the gate itself, in every state a conversation can be sitting in — the ordinary
// response band, the Auto-Link automatic wait, and the no-audio Continue fallback — because a
// state-dependent hole is the only way the one chokepoint could still let a save through.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumDialogueCamera.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumNpc.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumDialogueSaveTests
{
static constexpr EAutomationTestFlags GSaveTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const TCHAR* const GBlockReason = TEXT("a conversation is open");

	FElysiumEntityDefs MakeSaveTestDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__dialogue_save_test__");
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VPedestrian");
		Npc.TargetName = TEXT("speaker");
		Npc.Origin = FVector(200.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Npc));
		return Defs;
	}

	FElysiumDlgLine& AddLine(FElysiumDlgFile& File, int32 Id, const TCHAR* Text,
		const TCHAR* Link, EElysiumDlgRole Role)
	{
		FElysiumDlgLine& Line = File.Lines.AddDefaulted_GetRef();
		Line.Id = Id;
		Line.TextMale = Text;
		Line.Link = Link;
		Line.Role = Role;
		File.IndexById.Add(Id, File.Lines.Num() - 1);
		return Line;
	}

	// An ordinary NPC turn with two pickable responses.
	TSharedRef<FElysiumDlgConversation> MakeChoiceConversation()
	{
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		AddLine(File.Get(), 1, TEXT("What do you want?"), TEXT("#"), EElysiumDlgRole::NpcLine);
		AddLine(File.Get(), 2, TEXT("Nothing."), TEXT("0"), EElysiumDlgRole::PcChoice);
		AddLine(File.Get(), 3, TEXT("Everything."), TEXT("0"), EElysiumDlgRole::PcChoice);
		TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
			File, true, false, [](const FString&) { return true; }, [](const FString&) {});
		Conversation->Start();
		return Conversation;
	}

	// An NPC turn whose only follower is an `(Auto-Link)` marker: the turn holds, waiting for the
	// voice to end, and shows no response band at all.
	TSharedRef<FElysiumDlgConversation> MakeAutomaticConversation()
	{
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		AddLine(File.Get(), 1, TEXT("Let me think."), TEXT("#"), EElysiumDlgRole::NpcLine);
		AddLine(File.Get(), 2, TEXT("(Auto-Link)"), TEXT("3"), EElysiumDlgRole::PcChoice);
		AddLine(File.Get(), 3, TEXT("There. Done."), TEXT("#"), EElysiumDlgRole::NpcLine);
		AddLine(File.Get(), 4, TEXT("(Auto-End)"), TEXT("0"), EElysiumDlgRole::PcChoice);
		TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
			File, true, false, [](const FString&) { return true; }, [](const FString&) {});
		Conversation->Start();
		return Conversation;
	}

	// A terminal NPC line: no responses and no automatic follower, so presentation shows the
	// no-audio Continue and nothing else. This is the state a content-free world always lands in
	// when the voice cannot start.
	TSharedRef<FElysiumDlgConversation> MakeTerminalConversation()
	{
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		AddLine(File.Get(), 1, TEXT("That is all."), TEXT("#"), EElysiumDlgRole::NpcLine);
		TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
			File, true, false, [](const FString&) { return true; }, [](const FString&) {});
		Conversation->Start();
		return Conversation;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSaveRefusedInDialogueTest,
	"Elysium.Session.SaveRefusedInDialogue", GSaveTestFlags)
bool FElysiumSaveRefusedInDialogueTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MakeSaveTestDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);

	FElysiumEntity* Ent = World.FindByName(TEXT("speaker"));
	if (!TestNotNull(TEXT("the speaker spawned"), Ent))
	{
		return false;
	}

	TestTrue(TEXT("an ordinary world is saveable"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());

	// --- 1. The ordinary response band ------------------------------------------------------
	World.OpenDialog(Ent->Handle, MakeChoiceConversation());
	if (!TestNotNull(TEXT("the conversation opened"), World.GetOpenDialog()))
	{
		return false;
	}
	TestEqual(TEXT("a conversation with responses refuses every save entry"),
		World.ScriptedSessionSaveBlockReason(), FString(GBlockReason));
	World.CloseDialog(/*bSilent=*/true);
	TestTrue(TEXT("closing gives saving back"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());

	// --- 2. The Auto-Link automatic wait ------------------------------------------------------
	World.OpenDialog(Ent->Handle, MakeAutomaticConversation());
	if (FElysiumDlgConversation* Automatic = World.GetOpenDialog())
	{
		TestTrue(TEXT("the turn is waiting on an automatic row"),
			Automatic->IsAwaitingAutomatic());
		TestEqual(TEXT("...and it shows no response band"), Automatic->NumEnabledChoices(), 0);
	}
	TestEqual(TEXT("the automatic wait refuses saving too"),
		World.ScriptedSessionSaveBlockReason(), FString(GBlockReason));
	// Resolving the automatic row moves to the next NPC turn; the block must survive the hop.
	if (FElysiumDlgConversation* Automatic = World.GetOpenDialog())
	{
		Automatic->ResolveAutomatic();
	}
	TestEqual(TEXT("...and after the automatic transition"),
		World.ScriptedSessionSaveBlockReason(), FString(GBlockReason));
	World.CloseDialog(/*bSilent=*/true);
	TestTrue(TEXT("closing gives saving back"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());

	// --- 3. The no-audio Continue state -------------------------------------------------------
	World.OpenDialog(Ent->Handle, MakeTerminalConversation());
	if (FElysiumDlgConversation* Terminal = World.GetOpenDialog())
	{
		TestTrue(TEXT("the turn is the Continue-only terminal line"), Terminal->IsTerminalLine());
	}
	TestEqual(TEXT("the Continue state refuses saving"),
		World.ScriptedSessionSaveBlockReason(), FString(GBlockReason));

	// The Continue itself ends the conversation, which is the one edge that must give saving back
	// without anybody calling CloseDialog explicitly.
	World.PlayerDialogAdvance();
	TestNull(TEXT("the Continue closed the conversation"), World.GetOpenDialog());
	TestTrue(TEXT("...and saving is available again"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());
	return true;
}

}   // namespace ElysiumDialogueSaveTests

#endif   // WITH_DEV_AUTOMATION_TESTS
