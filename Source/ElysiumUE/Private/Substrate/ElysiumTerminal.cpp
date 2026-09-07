#include "Substrate/ElysiumTerminal.h"

#include "Substrate/ElysiumTerminalCone.h"

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBinds.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumViewState.h"
#include "ElysiumWorldServices.h"
#include "Scripting/ElysiumScriptFS.h"
#include "Substrate/ElysiumMoverSounds.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"

// --- TerminalDefinition -----------------------------------------------------------------------

bool FElysiumTerminalDefinition::ParseText(const FString& Text,
	FElysiumTerminalDefinition& Out, FString& OutError)
{
	Out = FElysiumTerminalDefinition();
	OutError.Reset();
	const TSharedPtr<ElysiumKeyValues::FKvNode> Parsed = ElysiumKeyValues::ParseText(Text);
	const ElysiumKeyValues::FKvNode* Root = Parsed ? Parsed->Child(TEXT("TerminalDefinition")) : nullptr;
	if (!Root)
	{
		OutError = TEXT("missing TerminalDefinition root block");
		return false;
	}

	// `+0x904` is a 64-byte field and the loader fills it with `Q_strncpy` (§4.4), so a longer
	// authored label is TRUNCATED at load, not printed in full — the screensaver think then measures
	// `strlen` of the truncated copy when it places the row.
	Out.ScreenSaver = Root->Str(TEXT("screen saver"), FString()).Left(ElysiumTerminalScreenSaverMax);
	Out.Brackets = Root->Str(TEXT("brackets"), FString());
	Out.EmailPassword = Root->Str(TEXT("email_password"), FString());
	Out.EmailUsername = Root->Str(TEXT("email_username"), FString());

	for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Child : Root->Kids)
	{
		if (Child.Key == TEXT("logonscreen"))
		{
			for (const TPair<FString, FString>& Line : Child.Value->Pairs)
			{
				if (Line.Key.StartsWith(TEXT("line")))
				{
					Out.LogonLines.Add(Line.Value);
				}
			}
			continue;
		}
		if (Child.Key == TEXT("subdir"))
		{
			if (Out.Directories.Num() >= 5)
			{
				OutError = TEXT("TerminalDefinition exceeds the native five-directory limit");
				return false;
			}
			FElysiumTerminalDirectory Directory;
			Directory.Name = Child.Value->Str(TEXT("name"), FString());
			Directory.Password = Child.Value->Str(TEXT("password"), FString());
			Directory.Description = Child.Value->Str(TEXT("description"), FString());
			Directory.Dependency = Child.Value->Str(TEXT("dependency"), FString());
			Directory.Difficulty = Child.Value->Int(TEXT("difficulty"), 0);
			for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Grandchild
				: Child.Value->Kids)
			{
				if (Grandchild.Key != TEXT("function"))
				{
					continue;
				}
				FElysiumTerminalFunction Function;
				Function.Name = Grandchild.Value->Str(TEXT("name"), FString());
				Function.Description = Grandchild.Value->Str(TEXT("description"), FString());
				Function.RunText = Grandchild.Value->Str(TEXT("runtext"), FString());
				Function.Dependency = Grandchild.Value->Str(TEXT("dependency"), FString());
				Function.RunScript = Grandchild.Value->Str(TEXT("runscript"), FString());
				Function.Trigger = Grandchild.Value->Int(TEXT("trigger"), INDEX_NONE);
				if (Function.Trigger < INDEX_NONE || Function.Trigger > 7)
				{
					OutError = FString::Printf(TEXT("Function '%s' has trigger %d outside -1..7"),
						*Function.Name, Function.Trigger);
					return false;
				}
				Directory.Functions.Add(MoveTemp(Function));
			}
			Out.Directories.Add(MoveTemp(Directory));
			continue;
		}
		if (Child.Key == TEXT("email"))
		{
			// `CPropHacking::LoadFromFile` `0x1021cba0`: every `Email` field is a `Q_strncpy` into a
			// fixed record slot, and three of them carry a compiled-in DEFAULT rather than the empty
			// string — so an `Email` block with no `subject` prints retail's stand-in on the glass
			// and not a blank line. Both halves are content-visible and both are reproduced.
			FElysiumTerminalEmail Email;
			Email.Subject = Child.Value->Str(TEXT("subject"),
				ElysiumTerminalEmailCaps::DefaultSubject).Left(ElysiumTerminalEmailCaps::Subject);
			Email.Sender = Child.Value->Str(TEXT("sender"),
				ElysiumTerminalEmailCaps::DefaultSender).Left(ElysiumTerminalEmailCaps::Sender);
			Email.Body = Child.Value->Str(TEXT("body"),
				ElysiumTerminalEmailCaps::DefaultBody).Left(ElysiumTerminalEmailCaps::Body);
			Email.Dependency = Child.Value->Str(TEXT("dependency"), FString())
				.Left(ElysiumTerminalEmailCaps::Dependency);
			Email.RunScript = Child.Value->Str(TEXT("runscript"), FString())
				.Left(ElysiumTerminalEmailCaps::RunScript);
			Email.bAutoDelete = Child.Value->Bool(TEXT("autodelete"), false);
			Out.Emails.Add(MoveTemp(Email));
		}
	}
	return true;
}

bool FElysiumTerminalDefinition::Load(const FString& VirtualPath,
	FElysiumTerminalDefinition& Out, FString& OutError)
{
	FString RealPath;
	if (!FElysiumScriptFS::Resolve(VirtualPath, EElysiumFsAccess::Read, RealPath, OutError))
	{
		return false;
	}
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *RealPath))
	{
		OutError = FString::Printf(TEXT("could not read '%s' (resolved to '%s')"),
			*VirtualPath, *RealPath);
		return false;
	}
	if (!ParseText(Text, Out, OutError))
	{
		OutError = FString::Printf(TEXT("%s: %s"), *VirtualPath, *OutError);
		return false;
	}
	return true;
}

// --- Hacking strings ----------------------------------------------------------------------------

namespace ElysiumHackingStrings
{
	// The 48 compiled-in labels `FUN_10219400` falls back to (§16). Entries 45–47 are the mail
	// list's footer keys; the corpus names them only by index.
	static const TCHAR* const GKeyNames[] =
	{
		TEXT("STRING_PRESS_HACK_KEY"), TEXT("STRING_CHANGE_SUBDIR"), TEXT("STRING_AVAIL_SUBDIR"),
		TEXT("STRING_AVAIL_FUNCS"), TEXT("STRING_NO_FUNCS"), TEXT("STRING_INVALID_FUNC1"),
		TEXT("STRING_INVALID_FUNC2"), TEXT("STRING_INVALID_FUNC3"), TEXT("STRING_HELP1"),
		TEXT("STRING_HELP2"), TEXT("STRING_HELP3"), TEXT("STRING_HELP4"), TEXT("STRING_LOGIN_PROMPT"),
		TEXT("STRING_PASSWORD_NOTIFY"), TEXT("STRING_PASSWORD_PROMPT"), TEXT("STRING_INVALID_PASSWORD"),
		TEXT("STRING_VALID_PASSWORD"), TEXT("STRING_HOME_DIR"), TEXT("STRING_CONTINUE"),
		TEXT("STRING_PASSWORD_ACCEPTED"), TEXT("STRING_HELP_TITLE"), TEXT("STRING_PASSWORD_EXIT"),
		TEXT("STRING_FROM_HEADER"), TEXT("STRING_SUBJECT_HEADER"), TEXT("STRING_NEXT_CMD"),
		TEXT("STRING_PREV_CMD"), TEXT("STRING_DEL_CMD"), TEXT("STRING_MENU_CMD"), TEXT("STRING_QUIT_CMD"),
		TEXT("STRING_EMAIL_TITLEBAR"), TEXT("STRING_EMAIL_DIR"), TEXT("STRING_EMAIL_COUNT"),
		TEXT("STRING_QUIT_MESSAGE"), TEXT("STRING_QUIT"), TEXT("STRING_HELP"), TEXT("STRING_LIST"),
		TEXT("STRING_EMAIL"), TEXT("STRING_DIFFICULTY"), TEXT("STRING_SKILLINSUFFICIENT"),
		TEXT("STRING_EMAIL_PASSWORD_ACCEPTED"), TEXT("STRING_MAKING_HACK_ATTEMPT"),
		TEXT("STRING_CURRENT_SUBDIR"), TEXT("STRING_TYPE_PROMPT"), TEXT("STRING_HOME_MENU"),
		TEXT("STRING_MENU"), TEXT("STRING_EMAIL_FOOTER1"), TEXT("STRING_EMAIL_FOOTER2"),
		TEXT("STRING_EMAIL_FOOTER3"),
	};
	static_assert(UE_ARRAY_COUNT(GKeyNames) == 48, "Hacking_Strings carries 48 compiled-in keys");

	const TCHAR* KeyName(int32 Index)
	{
		return Index >= 0 && Index < 48 ? GKeyNames[Index] : TEXT("Unrecognized Command");
	}

	// A headless world has no rulebook subsystem; the exported table is read once from the corpus.
	static const FElysiumStrings& HeadlessStrings()
	{
		static FElysiumStrings Strings;
		static bool bTried = false;
		if (!bTried)
		{
			bTried = true;
			FString Error;
			if (!Strings.Load(Error))
			{
				UE_LOG(LogElysiumSkill, Display,
					TEXT("terminal strings: %s; the compiled key names stand in"), *Error);
			}
		}
		return Strings;
	}

	// The test-only override, ahead of everything: a case that installs a table is asserting
	// against THAT table, and a tree that happens to carry an export must not change its answers.
	static TUniquePtr<FElysiumStrings> GInstalledForTests;

	void InstallForTests(TArray<FString> Entries)
	{
		GInstalledForTests = MakeUnique<FElysiumStrings>();
		GInstalledForTests->Groups.Add(TEXT("hacking_strings"), MoveTemp(Entries));
	}

	void ResetForTests()
	{
		GInstalledForTests.Reset();
	}

	static const FElysiumStrings& TableFor(const FElysiumEntityWorld* World)
	{
		if (GInstalledForTests)
		{
			return *GInstalledForTests;
		}
		UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
		UElysiumRulebookSubsystem* Rulebook = GameState ? GameState->Rulebook() : nullptr;
		return Rulebook ? Rulebook->Strings() : HeadlessStrings();
	}

	FString Get(const FElysiumEntityWorld* World, int32 Index)
	{
		static const FString Group(TEXT("hacking_strings"));
		const FString Fallback(KeyName(Index));
		return TableFor(World).At(Group, Index, Fallback);
	}

	FString GetOrEmpty(const FElysiumEntityWorld* World, int32 Index)
	{
		// Indices 45/46/47 are the only ones `FUN_10219400` leaves NULL: retail then hands
		// `Q_vsnprintf` a NULL format and the row is whatever the CRT does with that. The port
		// cannot reproduce a crash hazard, so a missing entry prints NOTHING — the named seam is
		// that a `Hacking_Strings` table without those three entries draws three blank footer rows
		// instead of retail's undefined behaviour.
		static const FString Group(TEXT("hacking_strings"));
		return TableFor(World).At(Group, Index, FString());
	}
}

// --- The terminal printer -----------------------------------------------------------------------

FString ElysiumTerminalFormat(const FString& Format, TArrayView<const FElysiumTerminalArg> Args)
{
	// `FUN_10217ac0` = zero the scratch, `Q_vsnprintf`, send as a type-2 print. Only `%s`, `%d`
	// (`%i`), `%c` and `%%` reach it from the terminal bodies; a `%c` fed a NUL ends the string
	// there, exactly as C does, and that is what the empty `brackets` rely on (§8.6).
	FString Out;
	Out.Reserve(Format.Len());
	const int32 Length = Format.Len();
	int32 NextArg = 0;
	for (int32 At = 0; At < Length; ++At)
	{
		const TCHAR Character = Format[At];
		if (Character != TEXT('%'))
		{
			Out.AppendChar(Character);
			continue;
		}
		// The flag / width / precision run C accepts between the `%` and the conversion. No
		// terminal body or shipped string authors one, so it is consumed and not applied; the
		// conversion is the whole behaviour here.
		int32 Spec = At + 1;
		while (Spec < Length)
		{
			const TCHAR Flag = Format[Spec];
			if (FChar::IsDigit(Flag) || Flag == TEXT('-') || Flag == TEXT('+') || Flag == TEXT('.')
				|| Flag == TEXT('#'))
			{
				++Spec;
				continue;
			}
			break;
		}
		if (Spec >= Length)
		{
			// A `%` that runs off the end formats nothing.
			break;
		}
		const TCHAR Conversion = Format[Spec];
		if (Conversion == TEXT('%'))
		{
			Out.AppendChar(TEXT('%'));
			At = Spec;
			continue;
		}
		if (Conversion != TEXT('s') && Conversion != TEXT('d') && Conversion != TEXT('i')
			&& Conversion != TEXT('c'))
		{
			// An unsupported conversion in authored content: retail would read a vararg that was
			// never pushed. Emit the run verbatim instead of inventing one.
			for (int32 Copy = At; Copy <= Spec; ++Copy)
			{
				Out.AppendChar(Format[Copy]);
			}
			At = Spec;
			continue;
		}
		// A conversion with no argument behind it is C's undefined behaviour; contribute nothing.
		const FElysiumTerminalArg* Arg = Args.IsValidIndex(NextArg) ? &Args[NextArg] : nullptr;
		++NextArg;
		At = Spec;
		if (!Arg)
		{
			continue;
		}
		if (Conversion == TEXT('c'))
		{
			const TCHAR Value = Arg->Kind == FElysiumTerminalArg::EKind::Chr
				? Arg->Character : TCHAR(Arg->Number);
			if (Value == 0)
			{
				return Out;   // the NUL that truncates the print
			}
			Out.AppendChar(Value);
			continue;
		}
		// `%s` and `%d`/`%i` differ only in the argument they were given; a mismatch is C's
		// undefined behaviour, so each simply prints what it holds.
		Out += Arg->Kind == FElysiumTerminalArg::EKind::Str
			? Arg->Text : FString::FromInt(Arg->Number);
	}
	return Out;
}

// --- CBaseTerminal ------------------------------------------------------------------------------

void FElysiumTerminal::Spawn()
{
	// `CBaseTerminal::Spawn` `0x10217880` clamps the grid and zeroes `m_HackFlags`, `m_nMaxInput`
	// and `m_bAllowDirKeys` (`+0x824`, the field's ONLY writer in vampire.dll). The colour scheme is
	// replicated in four bits and clamped by the client's rasterizer to `0..3` (`client+0xf08`,
	// §8.3); the port clamps it here so the published view never carries a palette index nobody can
	// draw.
	ColorScheme = FMath::Clamp(ColorScheme, 0, 3);
	Screen.Reset(TextColumns, TextRows);
	FString ContentError;
	if (!OpenContent(ContentError))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s terminal content failed: %s"),
			*DebugString(), *ContentError);
		bStartEnabled = false;
	}

	// `soundgroup` resolves by directory convention under `usable/computers/<group>/` (§14), the
	// way movers resolve theirs; the manifest carries only the shipped subkeys.
	CueRels.Reset();
	const FString Group = SoundGroup.TrimStartAndEnd().ToLower();
	if (!Group.IsEmpty())
	{
		if (const TMap<FString, TMap<FName, FString>>* Groups
			= ElysiumMoverSoundManifest().Find(TEXT("computers")))
		{
			if (const TMap<FName, FString>* Subs = Groups->Find(Group))
			{
				CueRels = *Subs;
			}
		}
	}

	// The body build is conditional; resolving the screen attachments at the end is not, because a
	// bodiless terminal still has to report WHICH part it is missing.
	BuildBody();
	ResolveScreenAttachments();
	ReportMissingAttachments();
}

void FElysiumTerminal::BuildBody()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def || Model.IsEmpty())
	{
		return;
	}
	VisualStem = Model;
	const FQuat StaticRotation = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f)) : Def->ModelQuat;
	const FQuat SkeletalRotation = Def->ModelMesh.IsEmpty()
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)) : Def->ModelQuat;
	if (Embodiment->HasPlacedModelCatalogue())
	{
		FElysiumPlacedModelRequest Request;
		Request.ModelPath = Model;
		Request.StaticStem = VisualStem;
		Request.Location = Origin;
		Request.Rotation = SkeletalRotation;
		Request.UniformScale = Embodiment->BodyScaleFor(*Def);
		Request.PlacementToken = Handle.Index;
		WorldBody = Embodiment->BuildPlacedModelBody(Request).Visual;
	}
	else
	{
		WorldBody = Embodiment->BuildPropVisual(
			VisualStem, Origin, StaticRotation, Embodiment->BodyScaleFor(*Def));
	}
	if (WorldBody)
	{
		// This is also the one site that stands the glass: `AElysiumMapActor::RegisterUseAnchor`
		// registers the projection on the visual it was handed (slice C).
		World->RegisterPropBody(WorldBody, Handle);
		// Dormancy alone: `m_bEnabled` never touched retail's collision box (`InputDisable`).
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
}

void FElysiumTerminal::DestroyBody()
{
	// The anchor record, its `ELYSIUM_USE_CHANNEL` proxy and the projection bound to this body all
	// go with it. Without this a `SetModel` appends a SECOND anchor record (the map actor
	// de-duplicates on the proxy component, which is a fresh object every time) and leaves the glass
	// bound to the component the rebuild is about to replace.
	if (World)
	{
		World->UnregisterUseAnchor(Handle);
	}
	if (WorldBody)
	{
		WorldBody->DestroyComponent();
		WorldBody = nullptr;
	}
	VisualStem.Reset();
	bScreenAttachmentsResolved = false;
}

void FElysiumTerminal::Activate()
{
	FElysiumSkillEntity::Activate();
	bReportedBodilessGlass = false;
}

void FElysiumTerminal::ResolveScreenAttachments()
{
	// `FUN_10218710`'s two `CBaseAnimating::GetAttachment01` calls, done once per body rather than
	// once per gate: the pair only moves when the body does, and the gate runs for every use
	// candidate every frame.
	bScreenAttachmentsResolved = false;
	AttachmentError = nullptr;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		AttachmentError = TEXT("no embodiment");
		return;
	}
	FTransform ScreenFrame;
	FTransform AxisFrame;
	if (!Embodiment->GetBodyAttachment(Handle, FName(TEXT("screen")), ScreenFrame))
	{
		AttachmentError = TEXT("screen");
		return;
	}
	if (!Embodiment->GetBodyAttachment(Handle, FName(TEXT("screen_axis")), AxisFrame))
	{
		AttachmentError = TEXT("screen_axis");
		return;
	}
	ScreenPointCm = ScreenFrame.GetLocation();
	ScreenAxisPointCm = AxisFrame.GetLocation();
	bScreenAttachmentsResolved = true;
}

void FElysiumTerminal::ReportMissingAttachments() const
{
	// The one named content error (`docs/architecture/computer-terminal-architecture.md` §3.3):
	// entity, model, which part is missing. It is raised where the pair is READ, not where a session
	// is refused, because a model that does not carry `screen` / `screen_axis` is an authored/bake
	// defect that exists from spawn — and because the world's own gate refuses the session before
	// the entity's `BeginPlayerUse` ever runs.
	//
	// A terminal with no model at all is not a defect: it is a bodiless fixture, and the port's
	// headless tier is full of them.
	if (AttachmentError && !Model.IsEmpty())
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s cannot be used: model '%s' resolves no '%s' attachment"),
			*DebugString(), *Model, AttachmentError);
	}
}

bool FElysiumTerminal::GetBodyAttachmentPoint(FName Attachment, FVector& OutWorld) const
{
	// The camera shot's `Attachment:` anchors read the SAME two vectors the cone measured with, so
	// the shot cannot frame a screen the gate was not testing.
	if (bScreenAttachmentsResolved)
	{
		if (Attachment == FName(TEXT("screen")))
		{
			OutWorld = ScreenPointCm;
			return true;
		}
		if (Attachment == FName(TEXT("screen_axis")))
		{
			OutWorld = ScreenAxisPointCm;
			return true;
		}
	}
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	FTransform Frame;
	if (Embodiment && Embodiment->GetBodyAttachment(Handle, Attachment, Frame))
	{
		OutWorld = Frame.GetLocation();
		return true;
	}
	return false;
}

bool FElysiumTerminal::FacesScreen(const FElysiumUseContext& Context) const
{
	// `FUN_10218710` (§2): the two attachment reads and `FUN_101d1120`. A model with no `screen` /
	// `screen_axis` fails retail's cone too — `GetAttachment01` leaves the out-vectors alone, the
	// forward is zero-length and `FUN_101d1120` answers 0, which never exceeds 0.7. Failing closed
	// here says WHICH part is missing instead.
	if (!bScreenAttachmentsResolved || !Context.bHasEyeOrigin)
	{
		return false;
	}
	return ElysiumTerminalCone::Faces(ScreenPointCm, ScreenAxisPointCm, Context.EyeOrigin);
}

bool FElysiumTerminal::CanPlayerFocus(const FElysiumUseContext& Context) const
{
	// Use gate, slot 32 `0x102180c0` (§2.3): `m_bEnabled`, a requester with a player component,
	// **either no current user or the requester IS the current user** — re-entry of the same player
	// is allowed, a different actor is refused — and a positive screen-facing test.
	if (!bStartEnabled || IsInert() || !World)
	{
		return false;
	}
	if (CurrentUser.IsSet() && CurrentUser != Context.Activator)
	{
		return false;
	}
	// `param_1[0x2a]` (`+0xa8`, the player component). Only slot 32 asks it.
	const FElysiumEntity* User = World->Resolve(Context.Activator);
	if (!User || !User->AsCombatCharacter())
	{
		return false;
	}
	return FacesScreen(Context);
}

bool FElysiumTerminal::CanBeUsed(const FElysiumUseContext& Context) const
{
	// Availability, slot 34 `0x10218690`: `m_bEnabled`, then reject if `this+0x8c` resolves to ANY
	// live entity — the same player who is already holding it fails here, which is exactly what
	// separates this body from slot 32 — then the cone. No player-component test.
	if (!bStartEnabled || IsInert() || !World || CurrentUser.IsSet())
	{
		return false;
	}
	return FacesScreen(Context);
}

bool FElysiumTerminal::HasUseIconCaps(const FElysiumUseContext& Context) const
{
	// `ObjectCaps`, slot 35 `0x10218660`: `return -((char)cone != 0) & 2`. The cone alone, so a
	// disabled machine — or one somebody else is at — still draws the use icon from in front of the
	// glass. `IsInert()` is the port's dormancy, which retail expresses by removing the entity.
	return !IsInert() && FacesScreen(Context);
}

FElysiumUseBeginResult FElysiumTerminal::BeginPlayerUse(const FElysiumUseContext& Context)
{
	if (!CanPlayerFocus(Context))
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	CurrentUser = Context.Activator;
	++SessionSerial;
	if (SessionSerial == 0)
	{
		++SessionSerial;
	}
	++ViewRevision;

	// `CPropHacking::vfunc39` `0x1021a5b0` calls the base FIRST (`0x1021a5c1`), and
	// `CBaseTerminal::vfunc39` `0x102181a0` is itself ordered:
	//   1 `0x102181a9` `CBaseVampireSkillEntity::vfunc39` — `OnUseBegin` + the skill attach;
	//   2 `0x102181ae` the `access` cue;
	//   3 `0x102181cd` `FUN_1015ef40(player)` — immobilize;
	//   4 `0x102181d6` the `m_iVFlags` bit;
	//   5 `0x102181db` `m_bInUse`;
	//   6 `0x102181e2` the `m_szHackPWD` clear.
	// Only THEN does `CPropHacking` draw (`0x1021a5dd`) and prompt (`0x1021a5e4`), and only after
	// those does it push the camera (`0x1021a5ef`). The hold precedes the first frame the player
	// ever sees of the machine, which is the order this reproduces.
	FElysiumSkillEntity::BeginPlayerUse(Context);   // step 1

	// `FUN_1015ef40(player)` -> `CBasePlayer::m_bIsImmobilized` = 1 (step 3). The port's
	// `AElysiumPlayerController::TickActor` zeroes wish movement off `IsMobile()`, which is retail's
	// `SetupMove` button-mask arm; the pawn is held in place by `TickPlayerUse` below, exactly as
	// retail's slot 43 does. Steps 4 and 5 have no port counterpart: the view-angle lock the port
	// needs is the camera shot, and `m_bInUse` is `CurrentUser` above.
	if (FElysiumPlayer* PlayerEntity = World ? World->FindPlayer() : nullptr)
	{
		PlayerEntity->SetImmobilized(true);
	}

	// Steps 2 and 6 of the base, then `CPropHacking`'s own reset, email load, think cancel, draw and
	// prompt — one body, because the port's content leaf owns the whole of the derived entry.
	BeginContentSession();

	// `FUN_10070470("Hacking", NULL, terminal, terminal, NULL)` (§3.2 step 7): the `Hacking` block
	// of `vdata/camerashots/special-case.txt`, with the terminal bound as its Named slots.
	if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
	{
		// The clamp is the one named Presentation modernization: the glass is the brightest thing in
		// the frame and the auto-exposure has no retail counterpart to reproduce.
		CameraShot = Embodiment->PushCameraShotNamed(TEXT("special-case"), TEXT("Hacking"), Handle,
			EElysiumShotExposure::Clamped);
		if (CameraShot == 0)
		{
			// Retail does NOT refuse. `FUN_10070470` returns NULL when the shot will not load
			// (§3.3 step 3), and step 8 then runs `FUN_1017cef0(player, NULL)` — which CLEARS the
			// camera fields and leaves the client on the player's own eye. The session opens, the
			// player is held, the screen is drawn, and there is simply no cinematic camera
			// (`slice-bc-decompiles.md` §3.2/§3.4; supersedes the plan's refusal line).
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s: the 'Hacking' shot in special-case.txt did not resolve; ")
				TEXT("the session runs cameraless, as retail's NULL camera does"),
				*DebugString());
		}
	}
	return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
}

void FElysiumTerminal::TickPlayerUse(const FElysiumUseContext& Context)
{
	// `CBasePlayer::PlayerUse`'s maintenance arm, `FUN_10167e00` (`slice-bc-decompiles.md` §5.1).
	// The held entity's collision mins/maxs go to world space, the player's eye is clamped onto that
	// box, and `d = |eye.x - clamped.x| + |eye.y - clamped.y|` -- MANHATTAN, XY -- is compared with
	// the entity's slot-37 reach:
	//
	//   d >= reach  -> slot 43, the hull sweep that pulls the pawn in (the FAR arm);
	//   d <  reach  -> slot 41, which snaps the player's eye angles at the terminal's
	//                  `WorldSpaceCenter()` every tick (the NEAR arm, and the common one once the
	//                  sweep has done its work).
	//
	// Both arms then step the cracking/echo printer, which in this port is already the entity's own
	// think, so neither arm carries it here.
	//
	// With no bounds to measure -- a headless world with no body -- the far arm runs, which is what
	// the reach test degenerates to.
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment)
	{
		return;
	}
	FBox BodyBounds(ForceInit);
	const bool bHasBounds = Context.bHasEyeOrigin
		&& Embodiment->GetUseBodyWorldBounds(Handle, BodyBounds);
	if (bHasBounds)
	{
		const FVector Clamped = BodyBounds.GetClosestPointTo(Context.EyeOrigin);
		const double Manhattan = FMath::Abs(Context.EyeOrigin.X - Clamped.X)
			+ FMath::Abs(Context.EyeOrigin.Y - Clamped.Y);
		if (Manhattan < HoldReachCm)
		{
			// Slot 41. Retail's target is slot 192 = `WorldSpaceCenter()`, the collision OBB's own
			// centre -- not an attachment (correction C4).
			Embodiment->SnapPlayerViewTo(BodyBounds.GetCenter());
			return;
		}
	}
	FVector Contact = FVector::ZeroVector;
	Embodiment->SweepPlayerHullToward(Origin, Contact);
}

void FElysiumTerminal::EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason)
{
	if (!CurrentUser.IsSet())
	{
		return;
	}
	if (Context.Activator.IsSet() && Context.Activator != CurrentUser)
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s refused terminal teardown from non-owner %s (owner %s)"),
			*DebugString(), *Context.Activator.ToString(), *CurrentUser.ToString());
		return;
	}
	// The one release body, in `CBaseTerminal::vfunc42` `0x10218220` +
	// `CPropHacking::vfunc42` `0x1021a6c0` order (`slice-bc-decompiles.md` §4). Retail's
	// `FUN_10167fd0` dispatches slot 42 **before** it clears `entity+0x8c`, which is what lets the
	// hint hide still resolve its recipient -- so `CurrentUser` stays bound until the end here.
	//
	// 1. hide the `InfoCtrl` hint (type 2, value 0), user still bound;
	SetHudHint(2, 0);
	// 2. `FUN_1015ef60(player)` -> `m_bIsImmobilized` = 0. Unconditional and idempotent, and this is
	//    the ONE place every exit converges on -- directory `quit`, the failed per-tick gate, the
	//    second `+use` press, `InputDisable`, dormancy, damage, dialogue, teleport, removal and
	//    world teardown all reach `EndActiveUse`, which calls this.
	if (FElysiumPlayer* PlayerEntity = World ? World->FindPlayer() : nullptr)
	{
		PlayerEntity->SetImmobilized(false);
	}
	// 3-5. the two `m_iVFlags` bits and `m_bInUse` have no port counterpart yet: the view-angle lock
	//    the port needs is the camera shot, and `m_bInUse` is `CurrentUser` below.
	// 6. `10218251`-`10218278`: `sound->slot5( engine->IndexOfEdict(this+0x2e0), 0 )` on
	//    `IEngineSoundServer003` (`DAT_1070b248`). Decompiled for slice F:
	//    `CEngineSoundServer::vfunc5` (engine.dll `0x20002050`, `RET 8`) calls
	//    `FUN_200ef880(filter, entIndex, /*channel*/0, /*flag*/arg2)`, and its neighbour
	//    `vfunc6` (`0x20001fc0`) calls the same body as
	//    `FUN_200ef880(filter, entIndex, /*channel*/arg2, /*flag*/1)`. That body writes net
	//    message 6 — the same `svc_sounds` id `SV_StartSound` (`0x200ef320`) writes — with a
	//    sub-type of **2** instead of 1, then 11 bits of entity, 3 bits of channel and 1 bit of
	//    flag, and NO sample name. So the pair is stop-by-entity: `vfunc6` stops one channel and
	//    `vfunc5(ent, 0)` clears the flag, which stops **every** sound that entity is playing.
	//    The port's equivalent is stop-by-owner, and the terminal's cues already carry that owner.
	if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
	{
		FElysiumAudioOwner StopOwner;
		StopOwner.Kind = EElysiumAudioOwnerKind::MapEntity;
		StopOwner.StableId = CueOwnerId();
		Audio->CancelAudioOwner(StopOwner, /*FadeSeconds*/ 0.0f);
	}
	// 7. `0x1021827e` `CBaseVampireSkillEntity::vfunc42`: `OnUseEnd` + skill detach.
	FElysiumSkillEntity::EndPlayerUse(Context, Reason);
	// Then `CPropHacking`'s own tail: the idle title box and the directory/pending reset
	// (`EndContentSession`), with the screensaver re-arm at `ss_start + now` left to slice C.
	EndContentSession();
	// Last: `FUN_1017cef0(player, NULL)` drops the `camera_cinematic` outright. Nothing was saved
	// and nothing is restored -- the client simply falls back to the player's own eye -- and there
	// is **no ease-out** (correction C8, C19), so the pop is immediate.
	if (CameraShot != 0)
	{
		if (IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr)
		{
			Embodiment->PopCameraShot(CameraShot, /*BlendOutSeconds*/ 0.0f);
		}
		CameraShot = 0;
	}
	CurrentUser = FElysiumEntityHandle::Invalid();
	++ViewRevision;
}

void FElysiumTerminal::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumSkillEntity::Serialize(Ar);
}

void FElysiumTerminal::InputEnable()
{
	bStartEnabled = true;
}

void FElysiumTerminal::InputDisable()
{
	// `m_bEnabled` is an arm of slots 32 and 34 and NOTHING else: retail's terminal keeps its
	// `SOLID_BBOX` collision box whether it is enabled or not, so a disabled machine is still
	// swept into by the pin, still occludes what is behind it, and still draws the use icon through
	// slot 35 (`slice-bc-decompiles.md` §2.3). The use anchor is this port's stand-in for that box,
	// so it tracks dormancy alone — see `OnDormancyChanged`.
	bStartEnabled = false;
	if (World && CurrentUser.IsSet())
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
	}
}

bool FElysiumTerminal::Submit(uint32 ExpectedSerial, const FString& Command)
{
	if (!CurrentUser.IsSet() || ExpectedSerial != SessionSerial)
	{
		return false;
	}
	const bool bAccepted = SubmitContent(Command);
	if (bAccepted)
	{
		++ViewRevision;
	}
	return bAccepted;
}

bool FElysiumTerminal::BeginHack(uint32 ExpectedSerial)
{
	if (!CurrentUser.IsSet() || ExpectedSerial != SessionSerial)
	{
		return false;
	}
	const bool bAccepted = BeginContentHack();
	if (bAccepted)
	{
		++ViewRevision;
	}
	return bAccepted;
}

EElysiumTerminalInputMode FElysiumTerminal::ContentInputMode() const
{
	if (HackFlags & FlagAcknowledge)
	{
		return EElysiumTerminalInputMode::Acknowledge;
	}
	if (HackFlags & FlagRawCharacter)
	{
		return EElysiumTerminalInputMode::Raw;
	}
	return EElysiumTerminalInputMode::Line;
}

EElysiumTerminalInputMode FElysiumTerminal::InputMode() const
{
	return ContentInputMode();
}

void FElysiumTerminal::SetHudHint(int32 Type, int32 Value)
{
	// Types 0 and 2 hide; 1 carries a raw string no terminal body sends; 3/4/5/6 select a
	// `Hacking_Strings` line (§8.4).
	if (Type == 0 || Type == 2)
	{
		HudHintType = 0;
		HudHintValue = 0;
		return;
	}
	HudHintType = Type;
	HudHintValue = Value;
}

FString FElysiumTerminal::HudHintLine() const
{
	// The client's `InfoCtrl` handler `FUN_10055d30` (§8.4): the type selects a `Hacking_Strings`
	// line and 4/5/6 append the integer the usermessage carried. Type 1 (a raw `CGameText` string)
	// has no terminal producer, and 0/2 hide.
	using namespace ElysiumHackingStrings;
	switch (HudHintType)
	{
	case 3: return Get(World, PressHackKey);
	case 4: return Get(World, DifficultyLabel) + FString::FromInt(HudHintValue);
	case 5: return Get(World, MakingHackAttempt) + FString::FromInt(HudHintValue);
	case 6: return Get(World, SkillInsufficient) + FString::FromInt(HudHintValue);
	default: return FString();
	}
}

FString FElysiumTerminal::CueOwnerId() const
{
	// One place, because the exit's stop-by-owner (`vfunc42` step 6) has to name exactly what
	// `PlayCue` submitted under.
	return FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
}

void FElysiumTerminal::PlayCue(const TCHAR* Cue)
{
	const FString* Rel = CueRels.Find(FName(Cue));
	if (!Rel || Rel->IsEmpty() || !World)
	{
		return;
	}
	IElysiumAudio* Audio = World->Audio();
	if (!Audio)
	{
		return;
	}
	// The same spatialized entity voice a mover's `soundgroup` cue takes.
	constexpr float CueRadiusCm = 2500.f;
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(*Rel);
	Request.Owner.Kind = EElysiumAudioOwnerKind::MapEntity;
	Request.Owner.StableId = CueOwnerId();
	Request.Category = EElysiumAudioCategory::Sfx;
	Request.Placement.bSpatialized = true;
	Request.AttenuationRadiusCm = CueRadiusCm;
	Request.Placement.AttachTo = WorldBody;
	Request.Placement.Location = WorldBody ? WorldBody->GetComponentLocation() : Origin;
	Audio->Submit(MoveTemp(Request));
}

void FElysiumTerminal::RuleRow()
{
	// `FUN_1021b2b0`: `columns - 2` characters from the current column — `+`, dashes, `+`.
	const int32 Width = FMath::Max(2, Screen.Columns() - 2);
	FString Row = FString::ChrN(Width, TEXT('-'));
	Row[0] = TEXT('+');
	Row[Width - 1] = TEXT('+');
	// Retail hands the assembled row to `FUN_10217ac0` as the format string, then prints a
	// separate newline message: that newline is what returns the cursor to the left margin.
	ScreenPrint(ElysiumTerminalFormat(Row));
	ScreenPrint(TEXT("\n"));
}

void FElysiumTerminal::FramedRow(const FString& Text, int32 Margin)
{
	// `FUN_1021b330`: a `columns - 2` row of spaces with `|` at both ends; the text is blitted at
	// `Margin` over whatever is there (a long title eats the right edge), clipped at the 40-byte
	// buffer the retail body builds in.
	const int32 Width = FMath::Max(2, Screen.Columns() - 2);
	FString Row = FString::ChrN(Width, TEXT(' '));
	Row[0] = TEXT('|');
	Row[Width - 1] = TEXT('|');
	const int32 At = FMath::Clamp(Margin, 0, Width);
	const int32 Cap = FMath::Min(Text.Len(), 40 - At);
	for (int32 Offset = 0; Offset < Cap; ++Offset)
	{
		if (At + Offset < Width)
		{
			Row[At + Offset] = Text[Offset];
		}
		else
		{
			Row.AppendChar(Text[Offset]);
		}
	}
	// The row carries authored text (a directory description, a typed line) and retail prints
	// it as the format string, so a percent sign in content is a retail hazard, reproduced.
	ScreenPrint(ElysiumTerminalFormat(Row));
	ScreenPrint(TEXT("\n"));
}

void FElysiumTerminal::TitleBox(const FString* Title, const TArray<FString>& LogonLines)
{
	// `FUN_1021b140`: margins (1, 1), clear, rule, empty framed row, the title line(s), empty
	// framed row, rule. The centring margin floors at 1 only on the LogonScreen branch.
	ScreenSetMargins(1, 1);
	ScreenClear();
	const int32 Columns = Screen.Columns();
	if (Title == nullptr)
	{
		int32 Longest = 0;
		for (const FString& Line : LogonLines)
		{
			Longest = FMath::Max(Longest, Line.Len());
		}
		int32 Margin = Columns - Longest - 2;
		Margin = Margin <= 2 ? 1 : Margin / 2;
		RuleRow();
		FramedRow(FString(), Margin);
		for (const FString& Line : LogonLines)
		{
			FramedRow(Line, Margin);
		}
		FramedRow(FString(), Margin);
		RuleRow();
		return;
	}
	const int32 Margin = (Columns - Title->Len() - 2) / 2;
	RuleRow();
	FramedRow(FString(), Margin);
	FramedRow(*Title, Margin);
	FramedRow(FString(), Margin);
	RuleRow();
}

void FElysiumTerminal::EnterLineEdit()
{
	// `FUN_10219120`, the server half: `m_HackFlags &= 0xfffffffa` clears acknowledge (0x1) and raw
	// (0x4), and the same body sends entity message type 3.
	HackFlags &= ~(FlagAcknowledge | FlagRawCharacter);

	// `FUN_100c82e0`, the client half — the whole body, guards included.
	//
	//   `if (-1 < cursorCol && cursorCol < columns - rightMargin - 1 &&
	//       -1 < cursorRow && cursorRow < rows && *(char*)(this+0xe88) == 0)`
	//
	// so a message that arrives with the cursor already at (or past) the last usable column, or
	// while an edit is still open, does NOTHING: no origin, no cleared line, no flag. Inside the
	// guard it saves the cursor's cell row into `+0xe90`, records the cursor column as the edit
	// origin `+0xe8c`, clears the local line `+0xed8` and sets `+0xe88`.
	//
	// The saved row has no counterpart here: retail saves it because the client re-renders the
	// typed line INTO its own grid destructively, while the port composes the draft over an
	// untouched copy of the authority's grid each publish (`ElysiumTerminalCells::ComposeDraft`),
	// which is the same picture with nothing to restore.
	if (IsLineEditActive())
	{
		return;
	}
	const int32 Column = Screen.CursorColumn();
	const int32 Row = Screen.CursorRow();
	if (Column < 0 || Column >= Screen.Columns() - Screen.RightMargin() - 1)
	{
		return;
	}
	if (Row < 0 || Row >= Screen.Rows())
	{
		return;
	}
	LineEditOriginColumn = Column;
	LineEditOriginRow = Row;
	LineEditBreakMark = Screen.LineEditBreaks();
	bLineEditArmed = true;
	// The cleared line `+0xed8`. The widget holds it, so the epoch is what reaches it.
	++LineEditEpoch;
}

void FElysiumTerminal::BuildView(FElysiumTerminalView& Out) const
{
	Out = FElysiumTerminalView();
	if (!CurrentUser.IsSet())
	{
		return;
	}
	FillView(Out, SessionSerial);
}

void FElysiumTerminal::BuildIdleView(FElysiumTerminalView& Out) const
{
	// Serial 0 is what says "no session": the same grid, the same revision, no user. The retail
	// screen is the entity's own state whether or not anyone is standing at it — the screensaver
	// think writes into the very buffer a session would — so idle and live differ only in the serial.
	Out = FElysiumTerminalView();
	FillView(Out, /*Serial*/ 0);
}

const FString& FElysiumTerminal::ScreenSaverLabel() const
{
	static const FString None;
	return None;
}

void FElysiumTerminal::FillView(FElysiumTerminalView& Out, uint32 Serial) const
{
	// --- the GLASS half: what the monitor draws whether or not anyone is at it ---------------
	Out.Owner = Handle;
	Out.SessionSerial = Serial;
	Out.Revision = ViewRevision;
	Out.ScreenSaverLabel = ScreenSaverLabel();
	Out.Columns = Screen.Columns();
	Out.Rows = Screen.Rows();
	// Replicated glass state, not session state: `m_nColorScheme` selects the rasterizer's palette
	// whether or not anyone is standing at the machine.
	Out.ColorScheme = ColorScheme;
	Out.Cells.Reserve(Out.Columns * Out.Rows);
	Out.ScreenRows.Reserve(Out.Rows);
	for (int32 Row = 0; Row < Out.Rows; ++Row)
	{
		for (int32 Column = 0; Column < Out.Columns; ++Column)
		{
			Out.Cells.Add(Screen.Cell(Column, Row));
		}
		Out.ScreenRows.Add(Screen.RowText(Row));
	}
	Out.CursorRow = Screen.CursorRow();
	Out.CursorColumn = Screen.CursorColumn();
	Out.RightMargin = Screen.RightMargin();
	Out.CellStyle = Screen.Style();
	if (Serial == 0)
	{
		// --- and nothing else. An idle machine publishes no session state and, above all, runs no
		// dependency gate: `BuildContentView` evaluates every directory's and function's authored
		// `dependency` through the script host, and doing that for every terminal on the map every
		// frame is work retail's idle machine does not do — its think draws the screensaver and
		// stops. The action list is a live session's affordance and has no idle reader.
		return;
	}
	Out.InputMode = static_cast<uint8>(InputMode());
	// The client line editor's activation (§8.1.1). Session state, not glass state: an idle
	// monitor has no client holding a line, and retail's `+0xe88` only matters to the body that
	// eats keys.
	Out.bLineEditActive = IsLineEditActive();
	Out.EditEpoch = LineEditEpoch;
	Out.EditOriginColumn = LineEditOriginColumn;
	Out.EditOriginRow = LineEditOriginRow;
	Out.MaxInput = MaxInput;
	Out.bDigitsOnly = (HackFlags & FlagDigitsOnly) != 0;
	// `m_bAllowDirKeys` is zeroed at Spawn and no shipped body raises it, so the local line editor
	// never moves its cursor. Published rather than assumed, because the widget reads it as data.
	Out.bAcceptsDirectoryKeys = false;
	Out.HudHintType = HudHintType;
	Out.HudHintValue = HudHintValue;
	Out.HudHintText = HudHintLine();
	BuildContentView(Out);
}

void FElysiumTerminal::ReportBodilessGlass() const
{
	if (bReportedBodilessGlass)
	{
		return;
	}
	bReportedBodilessGlass = true;
	UE_LOG(LogElysiumSkill, Warning,
		TEXT("%s is live but has no body: its screen has nowhere to draw"), *DebugString());
}

void FElysiumTerminal::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	if (WorldBody)
	{
		WorldBody->SetVisibility(!IsInert());
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
		if (IsInert() && CurrentUser.IsSet())
		{
			World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
		}
	}
	// The glass is the body's, so it goes with the body. An inert terminal — hidden, killed or
	// reaped — hands its `screen` slot back its authored material and drops the 1024x768 target;
	// coming back live re-registers and re-binds it. Without this a machine destroyed mid-map keeps
	// a render target pinned until the map epoch retires.
	if (World && WorldBody)
	{
		if (IsInert())
		{
			World->UnregisterUseAnchor(Handle);
		}
		else
		{
			World->RegisterUseAnchor(WorldBody, Handle);
		}
	}
}

void FElysiumTerminal::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (WorldBody)
	{
		WorldBody->SetWorldLocationAndRotation(Origin,
			FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)));
	}
	// The attachments are world points, so a moved body moves the cone and the camera with it.
	ResolveScreenAttachments();
}

void FElysiumTerminal::OnRuntimeModelChanged()
{
	FElysiumEntity::OnRuntimeModelChanged();
	// The same rebuild `FElysiumProp::OnRuntimeModelChanged` does, in the same order: drop the old
	// body (and with it the anchor record, the use proxy and the glass), stand the new one, and let
	// the registration re-bind the projection to the component that now carries the `screen` slot.
	DestroyBody();
	BuildBody();
	ResolveScreenAttachments();
	ReportMissingAttachments();
}

UPrimitiveComponent* FElysiumTerminal::GetAttachBody() const
{
	return WorldBody;
}

const TCHAR* FElysiumTerminal::SaveBlockReason() const
{
	if (CurrentUser.IsSet())
	{
		return TEXT("a computer terminal session is active");
	}
	return FElysiumSkillEntity::SaveBlockReason();
}

void FElysiumTerminal::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumSkillEntity::GetDebugState(Out);
	// The owner is the machine itself. It reads redundant beside an inspector that already named
	// the entity, and it is not: the terminal diagnostic is copied out of `elysium_entity_get` into
	// bug reports, and a row set with no owner in it cannot be told from another terminal's.
	Out.Emplace(TEXT("Terminal owner"), Handle.ToString());
	Out.Emplace(TEXT("Terminal enabled"), bStartEnabled ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Terminal user"), CurrentUser.IsSet() ? CurrentUser.ToString() : TEXT("(none)"));
	Out.Emplace(TEXT("Session serial"), FString::Printf(TEXT("%u"), SessionSerial));
	Out.Emplace(TEXT("View revision"), FString::Printf(TEXT("%u"), ViewRevision));
	Out.Emplace(TEXT("Grid"), FString::Printf(TEXT("%dx%d"), Screen.Columns(), Screen.Rows()));
	Out.Emplace(TEXT("Colour scheme"), FString::FromInt(ColorScheme));
	Out.Emplace(TEXT("Hack flags"), FString::Printf(TEXT("0x%x (mode %d, maxinput %d)"),
		HackFlags, static_cast<int32>(InputMode()), MaxInput));
	Out.Emplace(TEXT("Cursor"), FString::Printf(TEXT("%d,%d"), Screen.CursorColumn(), Screen.CursorRow()));
	// `+0xe88` and its origin: the client's line editor is the difference between a key that
	// inserts and a key that is silently eaten, and nothing else on the screen shows it.
	Out.Emplace(TEXT("Line editor"), IsLineEditActive()
		? FString::Printf(TEXT("open at %d,%d (epoch %u)"), LineEditOriginColumn, LineEditOriginRow,
			LineEditEpoch)
		: FString::Printf(TEXT("closed (armed %s, epoch %u)"),
			bLineEditArmed ? TEXT("stale") : TEXT("no"), LineEditEpoch));
	// The timed skill attempt this terminal is running, if any: who, at what difficulty, and when
	// the last cycle resolved. The base's rows carry the roll and the count; this names the window.
	Out.Emplace(TEXT("Hack attempt"), AttemptUser.IsSet()
		? FString::Printf(TEXT("%s at difficulty %d, last cycle %.2fs"), *AttemptUser.ToString(),
			AttemptDifficulty(), LastAttemptSeconds)
		: TEXT("(none)"));
	Out.Emplace(TEXT("HUD hint"), FString::Printf(TEXT("%d (%d) \"%s\""), HudHintType, HudHintValue,
		*HudHintLine()));
	Out.Emplace(TEXT("Cues"), FString::FromInt(CueRels.Num()));
	Out.Emplace(TEXT("Screen attachments"), bScreenAttachmentsResolved
		? FString::Printf(TEXT("%s / %s"), *ScreenPointCm.ToCompactString(),
			*ScreenAxisPointCm.ToCompactString())
		: FString::Printf(TEXT("(missing %s)"),
			AttachmentError ? AttachmentError : TEXT("unknown")));
	Out.Emplace(TEXT("Camera shot"), FString::FromInt(CameraShot));
}

// --- CPropHacking -------------------------------------------------------------------------------

namespace
{
	// `Q_strnicmp(table, line, count) == 0`: a case-insensitive compare over the first `count`
	// characters, including the terminators — both strings must agree over `min(len, count)`.
	bool TokenEquals(const FString& Table, const FString& Line, int32 Count)
	{
		return Table.Left(Count).Equals(Line.Left(Count), ESearchCase::IgnoreCase);
	}

	// `brackets[0]` / `brackets[1]` (`+0x984`), handed to the format as `%c`. The rdata default is
	// `"[]"`; the tutorial patch authors the key empty, so both characters are NUL and every format
	// that takes one truncates there (`ElysiumTerminalFormat`, §8.6).
	FElysiumTerminalArg BracketAt(const FString& Brackets, int32 Index)
	{
		return FElysiumTerminalArg::Char(Brackets.Len() > Index ? Brackets[Index] : TCHAR(0));
	}

	// The player's bound `+use` key name (`CBasePlayer+0x2078`), which the prompt prefix prints
	// as `[E@home] `. The default bind table stands in for the live rebind until the input
	// subsystem publishes it; no shipped `brackets` value renders it (they are all empty or `[]`
	// on `home`, and the rebind seam is the only difference).
	// `FUN_10217200(bDigitsOnly)`: the cracking filler's character source. Its exact alphabet is
	// unrecovered (the body was not decompiled), so this draws printable ASCII, or a digit when
	// the `m_HackFlags 0x2` bit is set — the one property the listing does establish.
	TCHAR RandomHackCharacter(bool bDigitsOnly)
	{
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Terminal);
		return bDigitsOnly ? TCHAR('0' + Rng.RandRange(0, 9)) : TCHAR(Rng.RandRange(0x21, 0x7e));
	}

	FString UseKeyName()
	{
		for (const FElysiumDefaultBind& Bind : ElysiumBinds::Defaults())
		{
			if (FCString::Stricmp(Bind.Command, TEXT("+use")) == 0)
			{
				// `Q_strncpy(key, player+0x2078, 8)` — seven characters and the terminator, copied
				// verbatim: retail does not case-fold the key name.
				return FString(Bind.VtmbKey).Left(7);
			}
		}
		return FString();
	}

	// C's `atoi`, which is what the mail router runs on the WHOLE line (`0x1021b9c0`): leading
	// whitespace, an optional sign, then digits, stopping at the first byte that is not one — so
	// `12abc` is 12 and `nonsense` is 0. Overflow is undefined in C and wraps on MSVC; the
	// accumulator here is 64-bit and truncates, which lands in the same place. The range test in
	// the caller, not this, is what catches an out-of-range number.
	int32 TerminalAtoi(const FString& Line)
	{
		int32 At = 0;
		while (At < Line.Len() && FChar::IsWhitespace(Line[At]))
		{
			++At;
		}
		int64 Sign = 1;
		if (At < Line.Len() && (Line[At] == TEXT('-') || Line[At] == TEXT('+')))
		{
			Sign = Line[At] == TEXT('-') ? -1 : 1;
			++At;
		}
		int64 Value = 0;
		for (; At < Line.Len() && FChar::IsDigit(Line[At]); ++At)
		{
			Value = Value * 10 + (Line[At] - TEXT('0'));
		}
		return static_cast<int32>(Sign * Value);
	}

	// `Q_strnicmp(line, FUN_10219400(i) + 1, 1)`: ONE character, case-insensitive, against the
	// **second** byte of the localized word. The shipped words are bracketed (`"[n]ext"`), so
	// `word[1]` is the hotkey letter. A word shorter than two characters has no letter to offer and
	// matches nothing — which is also what the empty-table case degenerates to.
	bool MatchesHotkey(const FString& Word, const FString& Line)
	{
		return Word.Len() > 1 && !Line.IsEmpty()
			&& FChar::ToLower(Line[0]) == FChar::ToLower(Word[1]);
	}

	FString FirstUpper(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return Name;
		}
		FString Out = Name;
		Out[0] = FChar::ToUpper(Out[0]);
		return Out;
	}
}

bool FElysiumPropHacking::SameToken(const FString& Table, const FString& Line, int32 Count)
{
	return TokenEquals(Table, Line, Count);
}

void FElysiumPropHacking::InstallDefinition(FElysiumTerminalDefinition InDefinition)
{
	Definition = MoveTemp(InDefinition);
	DirectoryUnlocked.SetNumZeroed(Definition.Directories.Num());
	DirectoryAttempts.SetNumZeroed(Definition.Directories.Num());
}

bool FElysiumPropHacking::OpenContent(FString& OutError)
{
	if (HackFile.IsEmpty())
	{
		OutError = TEXT("hack_file is empty");
		return false;
	}
	FElysiumTerminalDefinition Parsed;
	UElysiumGameStateSubsystem* GameState = World ? World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rulebook = GameState ? GameState->Rulebook() : nullptr;
	if (Rulebook)
	{
		const FElysiumTerminalDefinition* Cached = Rulebook->TerminalDefinition(HackFile, OutError);
		if (!Cached)
		{
			return false;
		}
		Parsed = *Cached;
	}
	else if (!FElysiumTerminalDefinition::Load(HackFile, Parsed, OutError))
	{
		return false;
	}
	InstallDefinition(MoveTemp(Parsed));
	return true;
}

FString FElysiumPropHacking::HackingString(int32 Index) const
{
	return ElysiumHackingStrings::Get(World, Index);
}

bool FElysiumPropHacking::DependencyPasses(const FString& Source) const
{
	// Retail's terminal dependency gate (CPropHacking::TestDependency -> the shared
	// CDialogDependency::CallPyDialogFunction helper) short-circuits an empty string to TRUE, then
	// evaluates the expression with Py_eval_input and converts the result to bool with the exact
	// logic_pythoncheck rule: TRUE only for a non-zero Python integer, non-integer/null/error FALSE
	// (docs/vtmb, RE C073/C079). That is FElysiumVariant::IsPythonCheckTrue, not the generic
	// ToBool — a truthy non-integer (a string, the float 1.0) reads FALSE here. A raised eval is
	// already logged by the host and lands as an ordinary Void -> FALSE.
	return Source.IsEmpty()
		|| (World && World->EvalCondition(Source, Handle, CurrentUser).IsPythonCheckTrue());
}

// --- the mail state (§12) ---

bool FElysiumPropHacking::IsEmailRead(int32 Index) const
{
	// `FUN_1021a4b0`: `if (0 <= i && i < 0x80) return (flags[i] & 1) == 1; return false;` — out of
	// range answers FALSE (`XOR AL,AL` at `0x1021a4d3`), it does not clamp.
	return EmailFlags.IsValidIndex(Index) && (EmailFlags[Index] & EmailFlagRead) != 0;
}

bool FElysiumPropHacking::IsEmailDeleted(int32 Index) const
{
	// `FUN_1021a4f0`, the same shape on bit `0x2`.
	return EmailFlags.IsValidIndex(Index) && (EmailFlags[Index] & EmailFlagDeleted) != 0;
}

void FElysiumPropHacking::SetEmailRead(int32 Index)
{
	// `FUN_1021a530`. Set-only: there is no clear accessor in the module.
	if (EmailFlags.IsValidIndex(Index))
	{
		EmailFlags[Index] |= EmailFlagRead;
	}
}

void FElysiumPropHacking::SetEmailDeleted(int32 Index)
{
	// `FUN_1021a560`.
	if (EmailFlags.IsValidIndex(Index))
	{
		EmailFlags[Index] |= EmailFlagDeleted;
	}
}

void FElysiumPropHacking::RebuildMailIndex()
{
	// `FUN_1021bd80`: purge the vector, then append every record that is neither deleted nor
	// dependency-hidden, in record order. It is called from the list draw AND from the directory
	// draw (for the unread count), so the numbering is rebuilt whenever either is drawn — a
	// dependency that flips while the terminal is open renumbers the inbox under the player.
	//
	// It never re-clamps the page (`+0xa0c`): deleting the last row of the last page leaves the
	// player on an empty page until they press `[p]rev`. Retail off-by-one, ported as-is.
	MailVisible.Reset();
	for (int32 Index = 0; Index < Definition.Emails.Num(); ++Index)
	{
		if (IsEmailDeleted(Index))
		{
			continue;
		}
		// `FUN_1021bec0`: an empty dependency passes; otherwise `CallPyDialogFunc(dep, ..., 0x102)`
		// with the session's player as the activator — the same gate a directory's takes.
		if (!DependencyPasses(Definition.Emails[Index].Dependency))
		{
			continue;
		}
		MailVisible.Add(Index);
	}
}

int32 FElysiumPropHacking::UnreadEmailCount() const
{
	// `0x1021ace9`: unread among the freshly rebuilt VISIBLE table, not among the raw records.
	int32 Unread = 0;
	for (const int32 Real : MailVisible)
	{
		if (!IsEmailRead(Real))
		{
			++Unread;
		}
	}
	return Unread;
}

void FElysiumPropHacking::MailListDraw()
{
	// `FUN_1021c040`.
	using namespace ElysiumHackingStrings;
	// `0x1021c049`: leaving the open-message state is the FIRST thing the list draw does, which is
	// why every router miss lands back on the inbox.
	MailOpenIndex = INDEX_NONE;
	RebuildMailIndex();
	const int32 Count = MailVisible.Num();

	// `UTIL_VarArgs("%s %s", string 29, email_username)` -> the framed title box.
	const FString Title = ElysiumTerminalFormat(TEXT("%s %s"),
		{ HackingString(EmailTitleBar), Definition.EmailUsername });
	TitleBox(&Title, Definition.LogonLines);

	const int32 First = MailPage * 10;
	int32 Drawn = 0;
	for (int32 At = First; At < Count && Drawn < 10; ++At, ++Drawn)
	{
		const int32 Real = MailVisible[At];
		// `"[%d]"` (`0x105b06ac`) with the ONE-BASED on-screen row number.
		const FString Number = ElysiumTerminalFormat(TEXT("[%d]"),
			{ FElysiumTerminalArg::Num(At + 1) });
		if (!IsEmailRead(Real))
		{
			// Entity message type 6 (client style byte `0x00`) before the bracketed number and type
			// 5 (`0x80`) after. The rasterizer XORs the glyph on bit 7, so **bit 7 clear is reverse
			// video**: an unread row's `[N]` is inverted and the subject after it is not.
			ScreenStyleAlternate();
			ScreenPrint(Number);
			ScreenStyleDefault();
		}
		else
		{
			ScreenPrint(Number);
		}
		// `"%s\n"` (`0x1053e080`) with `record + 0x00`, the subject.
		ScreenPrint(ElysiumTerminalFormat(TEXT("%s\n"), { Definition.Emails[Real].Subject }));
	}

	// The three footer rows, pinned to `rows-3 / rows-2 / rows-1` at column 0. Their formats are
	// the three `Hacking_Strings` entries with no compiled-in fallback, so they resolve through
	// `GetOrEmpty` and an absent entry draws a blank row.
	ScreenSetCursor(0, Screen.Rows() - 3);
	ScreenPrint(ElysiumTerminalFormat(GetOrEmpty(World, MailListCount),
		{ FElysiumTerminalArg::Num(Count), FElysiumTerminalArg::Num(First + 1),
		  FElysiumTerminalArg::Num(Drawn + First) }));
	ScreenSetCursor(0, Screen.Rows() - 2);
	ScreenPrint(ElysiumTerminalFormat(GetOrEmpty(World, MailListMore),
		{ HackingString(NextCmd), HackingString(PrevCmd) }));
	ScreenSetCursor(0, Screen.Rows() - 1);
	ScreenPrint(ElysiumTerminalFormat(GetOrEmpty(World, MailListExit),
		{ HackingString(QuitCmd) }));
	// `FUN_10219120`: type 3 and `m_HackFlags &= ~(1|4)` — back to the line editor, which is what
	// makes the list a typed prompt while an open message is one key per press.
	EnterLineEdit();
}

void FElysiumPropHacking::MailRender()
{
	// `FUN_1021c260`.
	using namespace ElysiumHackingStrings;
	if (!Definition.Emails.IsValidIndex(MailOpenIndex))
	{
		return;
	}
	const FElysiumTerminalEmail& Record = Definition.Emails[MailOpenIndex];
	// `FUN_10218db0` is the type-4 clear and nothing else: no title box and no type 7, so the (1,1)
	// margins the last title box set stand for the whole message.
	ScreenClear();
	// `"\n\n(%s) %s\n"` (`0x105b06e0`) then `"(%s) %s\n\n\n"` (`0x105b06d0`) — the parentheses are
	// the format's, not the data's, so retail's `Subject:` header renders as `(Subject:) <text>`.
	ScreenPrint(ElysiumTerminalFormat(TEXT("\n\n(%s) %s\n"),
		{ HackingString(SubjectHeader), Record.Subject }));
	ScreenPrint(ElysiumTerminalFormat(TEXT("(%s) %s\n\n\n"),
		{ HackingString(FromHeader), Record.Sender }));
	// `0x1021c2e2` prints the body as ONE type-2 message with the body itself **as the format
	// string**, so authored newlines pass through and an authored `%` is a retail formatting
	// hazard — the same contract a Function's `runtext` has. The wrap is the screen buffer's.
	ScreenPrint(ElysiumTerminalFormat(Record.Body));
	ScreenSetCursor(0, Screen.Rows() - 2);
	// `"%s, %s, %s, %s, %s: "` (`0x105b06b4`) — `[n]ext, [p]rev, [d]elete, [m]enu, [q]uit: `.
	ScreenPrint(ElysiumTerminalFormat(TEXT("%s, %s, %s, %s, %s: "),
		{ HackingString(NextCmd), HackingString(PrevCmd), HackingString(DelCmd),
		  HackingString(MenuCmd), HackingString(QuitCmd) }));
	// The tail is `FUN_10219240`, NOT the acknowledge helper: an open message runs in single-key
	// mode (`m_HackFlags |= 4`), so the client sends `hackcmd %c` per keypress.
	EnterRawCharacter();
}

void FElysiumPropHacking::MailOpen(int32 VisibleRow)
{
	// `FUN_1021bc90`.
	if (MailVisible.Num() == 0)
	{
		return;
	}
	MailSelectedRow = FMath::Clamp(VisibleRow, 0, MailVisible.Num() - 1);
	MailOpenIndex = MailVisible[MailSelectedRow];
	MailRender();
	// The `runscript` fires BEFORE the read bit is set and only when it was clear — exactly once
	// per email per saved state — and AFTER the render, so a script that flips a dependency does
	// not change the body already on the glass. Mode `0x100` is `Py_file_input`: a statement body,
	// the same seam a Function's `runscript` takes.
	if (!IsEmailRead(MailOpenIndex))
	{
		const FString& RunScript = Definition.Emails[MailOpenIndex].RunScript;
		if (!RunScript.IsEmpty() && World)
		{
			World->EvalCondition(RunScript, Handle, CurrentUser);
		}
	}
	SetEmailRead(MailOpenIndex);
}

void FElysiumPropHacking::MailDelete(int32 RealIndex)
{
	// `FUN_1021bbf0`, the only deletion path in the module — `autodelete` is read by nothing.
	// Bounded by the RECORD count here and by 128 inside the accessor.
	if (RealIndex >= 0 && RealIndex < Definition.Emails.Num())
	{
		SetEmailDeleted(RealIndex);
	}
}

void FElysiumPropHacking::MailNextPage()
{
	// `FUN_1021c000`: `page = min(page + 1, count / 10)`. Integer division, and the first of the
	// two retail off-by-ones — with a visible count that is an exact multiple of ten the last page
	// starts at index `count` and draws ZERO rows. Ported, not corrected.
	MailPage = FMath::Min(MailPage + 1, MailVisible.Num() / 10);
	MailListDraw();
}

void FElysiumPropHacking::MailPrevPage()
{
	// `FUN_1021bfd0`: `page = max(page - 1, 0)`.
	MailPage = FMath::Max(MailPage - 1, 0);
	MailListDraw();
}

void FElysiumPropHacking::MailNextMessage()
{
	// `FUN_1021bc20`: the guard is `+0x9f0 < count + 1`, one too lax. `MailOpen` re-clamps and
	// writes the row back, so it self-corrects to "stay on the last message" — the off-by-one is
	// real but invisible, and it is reproduced rather than tightened.
	if (MailSelectedRow < MailVisible.Num() + 1)
	{
		++MailSelectedRow;
	}
	MailOpen(MailSelectedRow);
}

void FElysiumPropHacking::MailPrevMessage()
{
	// `FUN_1021bc60`.
	if (MailSelectedRow > 0)
	{
		--MailSelectedRow;
	}
	MailOpen(MailSelectedRow);
}

bool FElysiumPropHacking::MailHotkeys(const FString& Line)
{
	// `FUN_1021b9c0`. Returns true when the line was consumed; a miss makes `AcceptCmd` redraw the
	// list, which is also how the empty line after the entry acknowledgement reaches the inbox.
	using namespace ElysiumHackingStrings;
	if (Line.IsEmpty())
	{
		return false;
	}
	auto Hotkey = [this, &Line](int32 Index)
	{
		return MatchesHotkey(HackingString(Index), Line);
	};
	if (MailOpenIndex == INDEX_NONE)
	{
		// The LIST state. There is **no length check** here: `atoi` runs on the whole line and the
		// letter compares look at one character, so `next`, `n` and `nonsense` all page forward and
		// `12abc` opens row 12. `[d]elete` and `[m]enu` are not matched in this state at all.
		const int32 Count = MailVisible.Num();
		const int32 Chosen = TerminalAtoi(Line);
		if (Chosen > 0 && Chosen < Count + 1)
		{
			// One-based over the VISIBLE table, then clamped.
			MailOpen(FMath::Clamp(Chosen - 1, 0, Count - 1));
			return true;
		}
		if (Hotkey(NextCmd))
		{
			MailNextPage();
			return true;
		}
		if (Hotkey(PrevCmd))
		{
			MailPrevPage();
			return true;
		}
		if (Hotkey(QuitCmd))
		{
			EnterDirectory(INDEX_NONE);
			return true;
		}
		return false;
	}
	// The OPEN state, where the client is in single-key mode and sends one `hackcmd %c` per press.
	// The length test is exactly 1, so `ESC` — which the client sends as the four-byte
	// `hackcmd quit` — misses here and lands on the LIST, not at the root directory.
	if (Line.Len() != 1)
	{
		return false;
	}
	if (Hotkey(NextCmd))
	{
		MailNextMessage();
		return true;
	}
	if (Hotkey(PrevCmd))
	{
		MailPrevMessage();
		return true;
	}
	if (Hotkey(DelCmd))
	{
		MailDelete(MailOpenIndex);
		MailListDraw();
		return true;
	}
	if (Hotkey(MenuCmd))
	{
		MailListDraw();
		return true;
	}
	if (Hotkey(QuitCmd))
	{
		EnterDirectory(INDEX_NONE);
		return true;
	}
	// Numeric selection does not exist in the open state — the `atoi` branch is inside the list arm
	// only, so any other single key is a miss and the list is redrawn.
	return false;
}

void FElysiumPropHacking::LoadGlobalEmailState()
{
	// `CPropHacking::LoadGlobalEmailState` `0x1021a2f0`, called from OnUseBegin BEFORE any draw.
	// Only the 128 flags cross: `m_bEmailUnlocked` and `m_nEmailAttempts` stay per-entity, which is
	// why the haven PC's password is re-prompted in every haven map even though the read/deleted
	// state follows the player.
	if (!bGlobalEmail)
	{
		return;
	}
	if (FElysiumPlayer* PlayerEntity = World ? World->FindPlayer() : nullptr)
	{
		// The key is the entity's `targetname`, `""` when unnamed — the four shipped
		// `global_email 1` entities are all named `haven_pc`.
		PlayerEntity->RetrieveGlobalEmailFlags(TargetName, EmailFlags);
		EmailFlags.SetNumZeroed(EmailFlagCount);
	}
}

void FElysiumPropHacking::SaveGlobalEmailState()
{
	// `CPropHacking::SaveGlobalEmailState` `0x1021a3d0`, the first statement of OnUseEnd.
	if (!bGlobalEmail)
	{
		return;
	}
	if (FElysiumPlayer* PlayerEntity = World ? World->FindPlayer() : nullptr)
	{
		PlayerEntity->StoreGlobalEmailFlags(TargetName, EmailFlags);
	}
}

void FElysiumPropHacking::BeginContentSession()
{
	// `CPropHacking` entry 0x1021a5b0: `FUN_1021a1c0` (flags, directory -1, pending -1, clear
	// buffer), `access`, the player hold (slice B), `m_bInUse`, then the directory draw and prompt.
	HackFlags = FlagKeystrokeClick;
	MaxInput = 0;
	CurrentDirectory = INDEX_NONE;
	PendingDirectory = INDEX_NONE;
	CrackBuffer.Reset();
	bReprintPrompt = false;
	bSkillSubmit = false;
	LastCommand.Reset();
	PlayCue(TEXT("access"));
	// `0x1021a5cb`: `LoadGlobalEmailState(this, player)` sits between the base's entry and the
	// think cancel, so a `global_email` terminal has the player's copy of the flags in hand before
	// the directory draw counts unread mail off them.
	LoadGlobalEmailState();
	// Entry step 4, `0x1021a5d6`: `CBaseEntity::ThinkSet(NULL, 0.0f, NULL)`. The screensaver think
	// carries no in-use guard of its own (correction C16), so cancelling the schedule is the whole
	// of what keeps the label off a screen someone is typing on.
	CancelScreenSaver();
	// No `InfoCtrl` here: `thunk_FUN_10218820` has exactly six callers (BeginInput, the typed
	// submit, `vfunc42`, `AcceptCmd`, the password prompt, the password failure) and entry is not
	// one of them — the hint is already hidden because nothing raised it.
	DirectoryDraw();
	Prompt();
}

void FElysiumPropHacking::EndContentSession()
{
	// `CPropHacking` exit 0x1021a6c0, in its listing order: re-arm the screensaver, redraw the idle
	// title, reset the two indices. It touches neither `m_HackFlags` nor `m_szHackPWD` — the ONLY
	// writer of the crack buffer on the entry/exit path is `FUN_1021a1c0` at entry (§3.2 step 1),
	// and the next entry rewrites the whole flags word as well.
	//
	// `SaveGlobalEmailState(this, player)` is `CPropHacking::vfunc42`'s FIRST statement, ahead of
	// the base's own teardown. The port reaches this body after the base has run, which changes
	// nothing the store can see: `CurrentUser` is still bound (the base clears it last) and the
	// flags are entity state neither half touches.
	SaveGlobalEmailState();
	// `m_flNextThink = m_flSS_Start + gpGlobals->curtime` — **`ss_start`, never `ss_delay`, and with
	// no floor**: the 2.0 floor is Activate-only (correction C15).
	ArmScreenSaver(ScreenSaverStart);
	TitleBox(nullptr, Definition.LogonLines);
	CurrentDirectory = INDEX_NONE;
	PendingDirectory = INDEX_NONE;
	bReprintPrompt = false;
	// Exit resets neither the selected row, the open index, the page nor `m_bEmailUnlocked`: none
	// of the four is in retail's teardown, and the first three are re-derived by the next list draw
	// anyway. The unlock deliberately persists for the entity's lifetime.
}

EElysiumTerminalInputMode FElysiumPropHacking::ContentInputMode() const
{
	if (PendingDirectory != INDEX_NONE)
	{
		return EElysiumTerminalInputMode::Password;
	}
	return FElysiumTerminal::ContentInputMode();
}

// --- draw bodies ---

void FElysiumPropHacking::DirectoryDraw()
{
	// `FUN_1021aca0`.
	using namespace ElysiumHackingStrings;
	const bool bHasMail = Definition.Emails.Num() > 0;
	// `0x1021acce`: when the entity holds any email records at all, the directory draw REBUILDS the
	// visible table before it draws anything, and both numbers on the email row come off that
	// table — the first is the visible count (dependency-filtered, deleted excluded), NOT the raw
	// record count, and the second is the unread count among those.
	int32 VisibleMail = 0;
	int32 UnreadMail = 0;
	if (bHasMail)
	{
		RebuildMailIndex();
		VisibleMail = MailVisible.Num();
		UnreadMail = UnreadEmailCount();
	}
	const bool bInDirectory = Definition.Directories.IsValidIndex(CurrentDirectory);
	const FString* Title = bInDirectory ? &Definition.Directories[CurrentDirectory].Description : nullptr;
	TitleBox(Title, Definition.LogonLines);
	if (bHasMail)
	{
		// String 31 "You have %d emails, %d are unread." is itself the format (`FUN_101d3730`).
		ScreenPrint(ElysiumTerminalFormat(HackingString(EmailCount),
			{ FElysiumTerminalArg::Num(VisibleMail), FElysiumTerminalArg::Num(UnreadMail) }));
		ScreenPrint(TEXT("\n\n"));
	}
	if (!bInDirectory)
	{
		ScreenPrint(HackingString(HomeMenu) + TEXT("\n\n"));
	}
	else
	{
		const FString Name = FirstUpper(LoweredName(Definition.Directories[CurrentDirectory].Name).Left(16));
		ScreenPrint(Name + TEXT(" ") + HackingString(Menu) + TEXT("\n\n"));
	}
	ScreenPrint(HackingString(AvailableMenus) + TEXT(":\n"));
	if (bHasMail)
	{
		ScreenPrint(TEXT("   ") + HackingString(EmailDir) + TEXT("\n"));
	}
	if (CurrentDirectory != INDEX_NONE)
	{
		ScreenPrint(TEXT("   ") + HackingString(HomeDir) + TEXT("\n"));
	}
	for (int32 Index = 0; Index < Definition.Directories.Num(); ++Index)
	{
		if (Index != CurrentDirectory && DependencyPasses(Definition.Directories[Index].Dependency))
		{
			ScreenPrint(TEXT("   ") + LoweredName(Definition.Directories[Index].Name) + TEXT("\n"));
		}
	}
	ScreenPrint(TEXT("\n") + HackingString(AvailableCommands) + TEXT(":\n"));
	if (bInDirectory)
	{
		for (const FElysiumTerminalFunction& Function : Definition.Directories[CurrentDirectory].Functions)
		{
			if (DependencyPasses(Function.Dependency))
			{
				ScreenPrint(TEXT("   ") + LoweredName(Function.Name) + TEXT("\n"));
			}
		}
	}
	if (CurrentDirectory == INDEX_NONE)
	{
		ScreenPrint(TEXT("   ") + HackingString(Help) + TEXT("\n"));
		ScreenPrint(TEXT("   ") + HackingString(Quit) + TEXT("\n"));
	}
	bReprintPrompt = true;
}

void FElysiumPropHacking::Prompt()
{
	// `FUN_1021b410`: "Type menu or command: " on `rows - 2`, the `%c%s@%s%c ` prefix on
	// `rows - 1`, digits-only off, line edit on.
	using namespace ElysiumHackingStrings;
	if (!CurrentUser.IsSet())
	{
		return;
	}
	const FString Key = UseKeyName();
	ScreenSetCursor(0, Screen.Rows() - 2);
	ScreenPrint(HackingString(TypePrompt));
	const FString Host = Definition.Directories.IsValidIndex(CurrentDirectory)
		? LoweredName(Definition.Directories[CurrentDirectory].Name) : HackingString(HomeDir);
	ScreenSetCursor(0, Screen.Rows() - 1);
	// `"%c%s@%s%c "` (`0x105b064c`): a `user@host` prompt, `[E@home] `. The tutorial's empty
	// `brackets` make the first `%c` a NUL, so the whole prefix vanishes and the input row is bare.
	ScreenPrint(ElysiumTerminalFormat(TEXT("%c%s@%s%c "),
		{ BracketAt(Definition.Brackets, 0), Key, Host, BracketAt(Definition.Brackets, 1) }));
	HackFlags &= ~FlagDigitsOnly;
	EnterLineEdit();
}

void FElysiumPropHacking::PasswordPrompt(int32 PendingTarget, bool bRetry)
{
	// `FUN_1021c390` stores the pending target; `FUN_1021b5e0(this, retry)` draws.
	using namespace ElysiumHackingStrings;
	PendingDirectory = PendingTarget;
	if (!bRetry)
	{
		bSkillSubmit = false;
	}
	const FString Title = HackingString(bRetry ? InvalidPassword : PasswordRequiredTitle);
	TitleBox(&Title, Definition.LogonLines);
	if (PendingTarget >= 0 && Definition.Directories.IsValidIndex(PendingTarget))
	{
		// `"\n%s %c%s%c\n\n"` (`0x105b065c`) — with the tutorial's empty `brackets` the first `%c`
		// is a NUL, so the print stops after the notify sentence and its trailing space, and the
		// directory name and the two newlines never reach the screen (`04-password-failed.png`).
		ScreenPrint(ElysiumTerminalFormat(TEXT("\n%s %c%s%c\n\n"),
			{ HackingString(PasswordNotify), BracketAt(Definition.Brackets, 0),
			  LoweredName(Definition.Directories[PendingTarget].Name),
			  BracketAt(Definition.Brackets, 1) }));
	}
	if (bRetry)
	{
		ScreenPrint(HackingString(PasswordExit));
	}
	HackFlags &= ~FlagDigitsOnly;
	ScreenSetCursor(0, Screen.Rows() - 1);
	ScreenPrint(HackingString(LoginPrompt));
	EnterLineEdit();
	PlayCue(TEXT("error"));
	SetHudHint(3, 0);
}

void FElysiumPropHacking::HelpDraw()
{
	// `FUN_1021b030`.
	using namespace ElysiumHackingStrings;
	const FString Title = HackingString(HelpTitle);
	TitleBox(&Title, Definition.LogonLines);
	for (int32 Index = Help1; Index <= Help4; ++Index)
	{
		const FString Line = HackingString(Index);
		if (!Line.IsEmpty())
		{
			ScreenPrint(TEXT("\n") + Line + TEXT("\n"));
		}
	}
	bReprintPrompt = true;
}

void FElysiumPropHacking::InvalidCommand(const FString& Line)
{
	// `FUN_1021c3c0`: `error` first, then the title, the two hints, the ack line.
	using namespace ElysiumHackingStrings;
	PlayCue(TEXT("error"));
	// `"%s: %s"` (`0x105b06f0`) of string 5 and the typed line; bare string 5 for an empty line.
	const FString Title = Line.IsEmpty() ? HackingString(InvalidCommandTitle)
		: ElysiumTerminalFormat(TEXT("%s: %s"), { HackingString(InvalidCommandTitle), Line });
	TitleBox(&Title, Definition.LogonLines);
	ScreenPrint(TEXT("\n") + HackingString(InvalidCommandHint1) + TEXT("\n"));
	ScreenPrint(HackingString(InvalidCommandHint2) + TEXT("\n"));
	ScreenSetCursor(0, Screen.Rows() - 1);
	ScreenPrint(HackingString(Continue));
	EnterAcknowledge();
}

bool FElysiumPropHacking::EnterDirectory(int32 TargetIndex)
{
	// `FUN_1021c890`.
	using namespace ElysiumHackingStrings;
	if (TargetIndex > Definition.Directories.Num())
	{
		return false;
	}
	if (TargetIndex == Definition.Directories.Num())
	{
		// Retail's guard is `> count`, so an index equal to the count reads past the five unlock
		// bytes. No router arm can produce it (names match only real entries); refuse it here
		// rather than read past the array.
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s refused terminal directory %d (count %d)"),
			*DebugString(), TargetIndex, Definition.Directories.Num());
		return false;
	}
	CurrentDirectory = TargetIndex;
	PendingDirectory = INDEX_NONE;
	if (TargetIndex == INDEX_NONE)
	{
		DirectoryDraw();
		Prompt();
	}
	else if (TargetIndex == MailArea)
	{
		const FString Title = HackingString(ValidPassword);
		TitleBox(&Title, Definition.LogonLines);
		// String 39 takes the `email_password` as its `%s` (`0x1021c8ed`): retail echoes the
		// accepted password back at the player.
		ScreenPrint(ElysiumTerminalFormat(HackingString(EmailPasswordAccepted),
			{ Definition.EmailPassword }));
		ScreenSetCursor(0, Screen.Rows() - 1);
		ScreenPrint(HackingString(Continue));
		EnterAcknowledge();
		bEmailUnlocked = true;
	}
	else if (DirectoryUnlocked[TargetIndex] == 0)
	{
		DirectoryUnlocked[TargetIndex] = 1;
		const FString Title = HackingString(ValidPassword);
		TitleBox(&Title, Definition.LogonLines);
		// String 19 takes the directory's own password as its `%s` (`0x1021c98c`), and the result
		// is then the `%s` of `"\n%s %c%s%c"` (`0x105b06f8`) — the empty `brackets` truncate it.
		const FString Accepted = ElysiumTerminalFormat(HackingString(PasswordAccepted),
			{ Definition.Directories[TargetIndex].Password });
		ScreenPrint(ElysiumTerminalFormat(TEXT("\n%s %c%s%c"),
			{ Accepted, BracketAt(Definition.Brackets, 0),
			  LoweredName(Definition.Directories[TargetIndex].Name),
			  BracketAt(Definition.Brackets, 1) }));
		ScreenSetCursor(0, Screen.Rows() - 1);
		ScreenPrint(HackingString(Continue));
		EnterAcknowledge();
	}
	else
	{
		DirectoryDraw();
		Prompt();
	}
	PlayCue(TEXT("accept"));
	return true;
}

bool FElysiumPropHacking::ExecuteFunction(int32 DirectoryIndex, int32 FunctionIndex)
{
	// `FUN_1021c6d0`: the current directory's description, a newline, the runtext, the numbered
	// output, the runscript, the ack line. Silent.
	using namespace ElysiumHackingStrings;
	if (!Definition.Directories.IsValidIndex(DirectoryIndex)
		|| !Definition.Directories[DirectoryIndex].Functions.IsValidIndex(FunctionIndex))
	{
		return false;
	}
	const FElysiumTerminalDirectory& Directory = Definition.Directories[DirectoryIndex];
	const FElysiumTerminalFunction& Function = Directory.Functions[FunctionIndex];
	TitleBox(&Directory.Description, Definition.LogonLines);
	ScreenPrint(TEXT("\n"));
	// The authored `runtext` (record `+0x30`) is handed to the printer **as the format string**;
	// its own newlines make the rows and the client word-wraps the rest.
	ScreenPrint(ElysiumTerminalFormat(Function.RunText));
	if (Function.Trigger >= 0 && Function.Trigger < 8)
	{
		FireOutput(FName(*FString::Printf(TEXT("OnTrigger%d"), Function.Trigger)), CurrentUser);
	}
	if (!Function.RunScript.IsEmpty() && World)
	{
		// `CallPyDialogFunc` with `Py_file_input`, synchronous, result discarded. The bridge accepts
		// statement bodies on this seam; any output the statement raises joins the same queue
		// behind the trigger already enqueued above.
		World->EvalCondition(Function.RunScript, Handle, CurrentUser);
	}
	ScreenSetCursor(0, Screen.Rows() - 1);
	ScreenPrint(HackingString(Continue));
	EnterAcknowledge();
	return true;
}

void FElysiumPropHacking::CrackingRow(const FString& Shown)
{
	// `CPropHacking::vfunc278` 0x1021d610: the last row, `"%s%s     "` (`0x105b0888`) with string
	// 12. It overwrites and never clears; the five spaces are the erase.
	ScreenSetCursor(0, Screen.Rows() - 1);
	ScreenPrint(ElysiumTerminalFormat(TEXT("%s%s     "),
		{ HackingString(ElysiumHackingStrings::LoginPrompt), Shown }));
}

void FElysiumPropHacking::CrackingStep()
{
	// `FUN_10217d60`.
	if (!World)
	{
		return;
	}
	if (!World->Resolve(CurrentUser))
	{
		CrackBuffer.Reset();
		StopAttempt();
		return;
	}
	const double Now = World->NowSeconds();
	const float Remaining = FMath::Max(0.0f,
		LastAttemptSeconds + CrackTotalSeconds - static_cast<float>(Now));
	++ViewRevision;
	if (Remaining <= 0.0f)
	{
		PlayCue(TEXT("typing"));
		for (int32 Index = 0; Index < CrackBuffer.Len(); ++Index)
		{
			ScreenEcho(TCHAR(0x7f));
		}
		// Retail prints the revealed buffer as the format string too (`FUN_10217d60`).
		ScreenPrint(ElysiumTerminalFormat(CrackBuffer));
		const FString Submitted = CrackBuffer;
		CrackBuffer.Reset();
		StopAttempt();
		TypedSubmit(true, Submitted);
		return;
	}
	const float Fraction = CrackTotalSeconds > 0.0f ? Remaining / CrackTotalSeconds : 0.0f;
	const int32 Shown = FMath::Clamp(static_cast<int32>((1.0f - Fraction) * CrackBuffer.Len()),
		0, CrackBuffer.Len());
	FString Row = CrackBuffer.Left(Shown);
	const bool bDigits = (HackFlags & FlagDigitsOnly) != 0;
	for (int32 Index = Shown; Index < CrackBuffer.Len(); ++Index)
	{
		Row.AppendChar(RandomHackCharacter(bDigits));
	}
	CrackingRow(Row);
	NextThink = static_cast<float>(Now);   // per frame while the buffer is live
}

void FElysiumPropHacking::Think()
{
	if (!CrackBuffer.IsEmpty())
	{
		CrackingStep();
		return;
	}
	if (AttemptUser.IsSet())
	{
		// The roll already resolved at `BeginInput`; a live attempt with no buffer is a stale think.
		StopAttempt();
	}
	// Retail runs two different think functions off one `m_flNextThink`: the cracking stepper while a
	// session is printing, and `CPropHackingSS_Think` while the machine is idle. The port has one
	// `Think()`, so the buffer decides first and the screensaver takes the tick that is left. The
	// `CurrentUser` test is this dispatch, not a guard inside the think — entry's cancel is what
	// keeps the label off a live screen, and it is why a stale schedule cannot clear one either.
	if (!CurrentUser.IsSet())
	{
		ScreenSaverThink();
	}
}

void FElysiumPropHacking::Activate()
{
	FElysiumTerminal::Activate();
	// `0x1021a27c`: `FUN_1021b140(this, NULL)` — the idle `LogonScreen` box is on the glass from map
	// load, before anyone has touched the machine.
	TitleBox(nullptr, Definition.LogonLines);
	// `0x1021a298`: `RandomFloat(0.0f, 1.0f) + curtime`. Every terminal on a map arms inside the
	// same second but at its own offset, so a room full of monitors does not blink in lockstep.
	ArmScreenSaver(ElysiumRng::Stream(EElysiumRngStream::Terminal).GetFraction());
	// `0x1021a2af`: the floor, applied **after** that first schedule and nowhere else.
	if (ScreenSaverDelay < ScreenSaverDelayFloor)
	{
		ScreenSaverDelay = ScreenSaverDelayFloor;
	}
	++ViewRevision;
}

void FElysiumPropHacking::ArmScreenSaver(float DelaySeconds)
{
	NextThink = static_cast<float>(World ? World->NowSeconds() : 0.0) + DelaySeconds;
}

void FElysiumPropHacking::ScreenSaverThink()
{
	// `CPropHackingSS_Think` `0x1021a740`, step for step (`docs/vtmb/computer-terminals.md` §13).
	FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::Terminal);

	// 1. type 7 with margins **(0, 0)** — the screensaver RESETS the (1,1) the title box left, and
	//    type 7 clears nothing on its own; 2. the type-4 clear does.
	ScreenSetMargins(0, 0);
	ScreenClear();

	// 3-5. the label's placement. `maxCol` is computed before the draws, and both draws are
	//      inclusive on **both** bounds (`RandomInt`), so the label never sits on row 0 or column 0.
	const FString& Label = Definition.ScreenSaver;
	const int32 MaxColumn = FMath::Max(0, Screen.Columns() - Label.Len());
	const int32 Row = Rng.RandRange(1, Screen.Rows() - 1);
	const int32 Column = Rng.RandRange(1, MaxColumn);

	// 6. type-1 cursor, **column first**, margin-relative — and the margin is 0 here, so absolute.
	ScreenSetCursor(Column, Row);
	// 7. `RandomInt(0, 1)`: 0 -> style 5 (`FUN_10218ca0`, the default), 1 -> style 6
	//    (`FUN_10218b90`, the alternate block).
	if (Rng.RandRange(0, 1) != 0)
	{
		ScreenStyleAlternate();
	}
	else
	{
		ScreenStyleDefault();
	}
	// 8. the type-2 print takes the label as the **format string** (`FUN_10217ac0` runs it through
	//    `Q_vsnprintf`), so an authored `%` in a `screen saver` line is interpreted, not printed.
	ScreenPrint(ElysiumTerminalFormat(Label));
	// 9. style 5 again, so whatever draws next starts from the default.
	ScreenStyleDefault();
	++ViewRevision;
	// 10. `m_flNextThink = m_flSS_Delay + curtime`, with `ss_delay` already floored at Activate.
	ArmScreenSaver(ScreenSaverDelay);
}

// --- the router ---

bool FElysiumPropHacking::SubmitContent(const FString& Command)
{
	// `CPropHacking::AcceptCmd` 0x1021a830.
	bReprintPrompt = false;
	if (!CrackBuffer.IsEmpty())
	{
		return false;   // a live cracking buffer owns the session; the line is dropped
	}
	// `if (strlen(line) > 16) line[16] = 0`, applied in place; no whitespace trim on any path.
	const FString Line = Command.Left(16);
	// `AcceptCmd` never tests `m_HackFlags 0x1`: acknowledge mode is a *client* restriction (the
	// widget forwards only Enter, §8.1), so a line that reaches the authority in ack mode routes
	// like any other. The mode stays observable through `InputMode()` for the widget to obey.
	LastCommand = Line;
	if (CurrentDirectory == MailArea)
	{
		// `0x1021a909`: hide the hint, run the hotkey router, and on a MISS redraw the whole list —
		// which also clears the open-message index, so any unrecognized key leaves an open message.
		SetHudHint(2, 0);
		if (!MailHotkeys(Line))
		{
			MailListDraw();
		}
	}
	else if (PendingDirectory == INDEX_NONE)
	{
		SetHudHint(2, 0);
		if (!Builtins(Line) && CurrentUser.IsSet() && !MatchNames(Line))
		{
			InvalidCommand(Line);
		}
	}
	else if (SameToken(TEXT("break"), Line))
	{
		BeginInput();
	}
	else
	{
		TypedSubmit(false, Line);
	}
	if (bReprintPrompt && CurrentUser.IsSet() && PendingDirectory == INDEX_NONE)
	{
		Prompt();
	}
	return true;
}

bool FElysiumPropHacking::Builtins(const FString& Line)
{
	// `FUN_1021aaa0`: empty, quit, help, list, email, home — in that order, `Q_strnicmp` 16.
	using namespace ElysiumHackingStrings;
	if (Line.IsEmpty())
	{
		DirectoryDraw();
		bReprintPrompt = true;
		return true;
	}
	if (SameToken(HackingString(Quit), Line))
	{
		// `FUN_10167fd0`: the player release, the same path as the exit key.
		if (World)
		{
			World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed);
		}
		return true;
	}
	if (SameToken(HackingString(Help), Line))
	{
		HelpDraw();
		return true;
	}
	if (SameToken(HackingString(List), Line))
	{
		DirectoryDraw();
		return true;
	}
	if (SameToken(HackingString(Email), Line) && Definition.Emails.Num() > 0)
	{
		if (!Definition.EmailPassword.IsEmpty() && !bEmailUnlocked)
		{
			PasswordPrompt(MailArea, false);
		}
		else
		{
			EnterDirectory(MailArea);
		}
		return true;
	}
	if (SameToken(HackingString(HomeDir), Line))
	{
		EnterDirectory(INDEX_NONE);
		return true;
	}
	return false;
}

bool FElysiumPropHacking::MatchNames(const FString& Line)
{
	// `FUN_1021b750`: every directory by name from anywhere, then the current directory's
	// functions. The loop condition is `strnicmp(name, line, 16) == 0 && dependency(dep)`, so a
	// name that matches but fails its dependency does not stop the scan — the walk continues
	// through the remaining directories and then into the function list, and only a walk that
	// reaches the end ends as an invalid command.
	for (int32 Index = 0; Index < Definition.Directories.Num(); ++Index)
	{
		const FElysiumTerminalDirectory& Directory = Definition.Directories[Index];
		if (!SameToken(LoweredName(Directory.Name), Line)
			|| !DependencyPasses(Directory.Dependency))
		{
			continue;
		}
		if (!Directory.Password.IsEmpty() && DirectoryUnlocked[Index] == 0)
		{
			PasswordPrompt(Index, false);
		}
		else
		{
			DirectoryUnlocked[Index] = 1;
			EnterDirectory(Index);
		}
		return true;
	}
	if (Definition.Directories.IsValidIndex(CurrentDirectory))
	{
		const FElysiumTerminalDirectory& Directory = Definition.Directories[CurrentDirectory];
		for (int32 Index = 0; Index < Directory.Functions.Num(); ++Index)
		{
			if (!SameToken(LoweredName(Directory.Functions[Index].Name), Line)
				|| !DependencyPasses(Directory.Functions[Index].Dependency))
			{
				continue;
			}
			return ExecuteFunction(CurrentDirectory, Index);
		}
	}
	return false;
}

void FElysiumPropHacking::TypedSubmit(bool bSkill, const FString& Line)
{
	// `FUN_10217f50(this, skillFlag, line)`.
	SetHudHint(2, 0);
	bSkillSubmit = bSkill;
	if (Line.IsEmpty() || (Line.Len() >= 4 && SameToken(TEXT("quit"), Line, 4)))
	{
		// Slot 277: cancel — pending cleared, the directory redrawn. `quit` is never a guess.
		PendingDirectory = INDEX_NONE;
		DirectoryDraw();
		Prompt();
		CrackBuffer.Reset();
		return;
	}
	const FString Password = PendingDirectory == MailArea ? Definition.EmailPassword
		: Definition.Directories.IsValidIndex(PendingDirectory)
			? Definition.Directories[PendingDirectory].Password : FString();
	if (SameToken(Password, Line))
	{
		PasswordSucceeded();
	}
	else
	{
		PasswordFailed();
	}
	CrackBuffer.Reset();
}

void FElysiumPropHacking::PasswordSucceeded()
{
	// 0x1021c4d0.
	EnterDirectory(PendingDirectory);
}

void FElysiumPropHacking::PasswordFailed()
{
	// 0x1021c560.
	if (PendingDirectory == MailArea)
	{
		++EmailAttempts;
	}
	else if (DirectoryAttempts.IsValidIndex(PendingDirectory))
	{
		++DirectoryAttempts[PendingDirectory];
	}
	// The hint is hidden on both arms, before the branch.
	SetHudHint(2, 0);
	if (!bSkillSubmit)
	{
		// The typed arm: the retry prompt, which raises the hint again and replays `error`.
		PasswordPrompt(PendingDirectory, true);
		return;
	}
	// The skill arm: `OnSkillFail` already ran inside the attempt. Show the difficulty and return
	// to root without leaving the terminal. Retail clears nothing here — the root draw's title box
	// is what wipes the screen.
	const int32 FailedDifficulty = AttemptDifficulty();
	SetHudHint(6, FailedDifficulty);
	EnterDirectory(INDEX_NONE);
}

void FElysiumPropHacking::BeginInput()
{
	// `BeginInput` 0x10217b30: `OnSkillAttemptBegin`, the shared attempt (roll now), the buffer —
	// random characters below tier 3, the real password at 3 — `typing`, and the hint.
	if (!World || PendingDirectory == INDEX_NONE)
	{
		return;
	}
	FElysiumEntity* UserEntity = World->Resolve(CurrentUser);
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User)
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot start terminal hack: user %s is invalid"),
			*DebugString(), *CurrentUser.ToString());
		return;
	}
	if (!StartAttempt(*User))
	{
		return;
	}
	const TCHAR* Feat = FeatForSkillType(SkillType);
	CrackRating = Feat ? User->CalcFeat(Feat) : 0;
	// `(5.0 - rating * 0.25) / m_flSpeedScale`; the player's speed scale (`CBasePlayer+0x1488`)
	// is 1 until a discipline writes it — the seam is the divisor.
	CrackTotalSeconds = AttemptIntervalSeconds(CrackRating);
	ResolveAttempt(*User);   // OnSkillSucceeded / OnSkillFailed fill the buffer
	if (!AttemptUser.IsSet())
	{
		return;   // the user vanished during the outputs
	}
	PlayCue(TEXT("typing"));
	SetHudHint(5, CrackRating);
	NextThink = static_cast<float>(World->NowSeconds());
}

bool FElysiumPropHacking::BeginContentHack()
{
	if (PendingDirectory == INDEX_NONE || !CrackBuffer.IsEmpty())
	{
		return false;
	}
	BeginInput();
	return !CrackBuffer.IsEmpty();
}

int32 FElysiumPropHacking::AttemptDifficulty() const
{
	// 0x1021cae0: the mail area and a directory with difficulty < 1 fall back to the entity's.
	if (!Definition.Directories.IsValidIndex(PendingDirectory))
	{
		return Difficulty;
	}
	const int32 DirectoryDifficulty = Definition.Directories[PendingDirectory].Difficulty;
	return DirectoryDifficulty > 0 ? DirectoryDifficulty : Difficulty;
}

void FElysiumPropHacking::OnSkillSucceeded(FElysiumCombatCharacter&)
{
	// Tier 3: the buffer is the real password, revealed over the interval and then typed.
	CrackBuffer = PendingDirectory == MailArea ? Definition.EmailPassword
		: Definition.Directories.IsValidIndex(PendingDirectory)
			? Definition.Directories[PendingDirectory].Password : FString();
	if (CrackBuffer.IsEmpty())
	{
		CrackBuffer = TEXT(" ");   // a directory with no password never reaches a prompt
	}
}

void FElysiumPropHacking::OnSkillFailed(FElysiumCombatCharacter&)
{
	// Below tier 3, `BeginInput` 0x10217b30 walks the password once and does two things per
	// character: it stores one `FUN_10217200(digitsOnly)` character into `m_szHackPWD`, then draws
	// a **second** one and sends it as a type-9 echo. The stored string is what the typed submit
	// compares (and rejects); the echoed characters are what the player watches appear. Retail
	// fills once — it never re-rolls a buffer that happens to equal the password.
	const FString Password = PendingDirectory == MailArea ? Definition.EmailPassword
		: Definition.Directories.IsValidIndex(PendingDirectory)
			? Definition.Directories[PendingDirectory].Password : FString();
	const bool bDigits = (HackFlags & FlagDigitsOnly) != 0;
	CrackBuffer.Reset();
	for (int32 Index = 0; Index < Password.Len(); ++Index)
	{
		CrackBuffer.AppendChar(RandomHackCharacter(bDigits));
		// Type 9 (`FUN_102192a0`): put-char, then the client drops its left margin to 0 — which is
		// why the cracking row that follows is drawn from column 0.
		ScreenEcho(RandomHackCharacter(bDigits));
	}
}

void FElysiumPropHacking::OnAttemptStopped(FElysiumCombatCharacter&, EElysiumUseEndReason)
{
	CrackBuffer.Reset();
	StopAttempt();
}

void FElysiumPropHacking::BuildContentView(FElysiumTerminalView& Out) const
{
	// The label is glass state and `FillView` has already published it; this body is the SESSION's
	// affordances only. `bAcceptsDirectoryKeys` is the base's to publish, and it is always false.
	auto AddAction = [&Out](const FString& Id, const FString& Label, const FString& Command)
	{
		FElysiumTerminalActionView Action;
		Action.Id = Id;
		Action.Label = Label;
		Action.Command = Command;
		Out.Actions.Add(MoveTemp(Action));
	};
	if (PendingDirectory != INDEX_NONE)
	{
		AddAction(TEXT("hack"), TEXT("Bypass password"), TEXT("break"));
		return;
	}
	if (HackFlags & FlagAcknowledge)
	{
		return;
	}
	if (CurrentDirectory == MailArea)
	{
		// The mail area answers a completely different grammar (`FUN_1021b9c0`), so the directory's
		// verbs are not offered here — `list` and `help` do not route in mail mode at all. The
		// commands are the localized words themselves, whose first character is what the router
		// actually matches, so what the affordance sends is what a keypress would.
		using namespace ElysiumHackingStrings;
		auto Hotkey = [this, &AddAction](const TCHAR* Id, int32 Index)
		{
			const FString Word = HackingString(Index);
			if (Word.Len() > 1)
			{
				AddAction(Id, Word, FString::Chr(Word[1]));
			}
		};
		Hotkey(TEXT("mail:next"), NextCmd);
		Hotkey(TEXT("mail:prev"), PrevCmd);
		if (MailOpenIndex != INDEX_NONE)
		{
			Hotkey(TEXT("mail:delete"), DelCmd);
			Hotkey(TEXT("mail:menu"), MenuCmd);
		}
		Hotkey(TEXT("mail:quit"), QuitCmd);
		return;
	}
	AddAction(TEXT("list"), TEXT("List"), TEXT("list"));
	AddAction(TEXT("help"), TEXT("Help"), TEXT("help"));
	if (CurrentDirectory != INDEX_NONE)
	{
		AddAction(TEXT("home"), TEXT("Home"), TEXT("home"));
	}
	AddAction(TEXT("quit"), TEXT("Quit"), TEXT("quit"));
	for (int32 Index = 0; Index < Definition.Directories.Num(); ++Index)
	{
		const FElysiumTerminalDirectory& Directory = Definition.Directories[Index];
		if (Index != CurrentDirectory && DependencyPasses(Directory.Dependency))
		{
			AddAction(FString::Printf(TEXT("dir:%d"), Index), LoweredName(Directory.Name),
				LoweredName(Directory.Name));
		}
	}
	if (Definition.Directories.IsValidIndex(CurrentDirectory))
	{
		const FElysiumTerminalDirectory& Directory = Definition.Directories[CurrentDirectory];
		for (int32 Index = 0; Index < Directory.Functions.Num(); ++Index)
		{
			const FElysiumTerminalFunction& Function = Directory.Functions[Index];
			if (DependencyPasses(Function.Dependency))
			{
				AddAction(FString::Printf(TEXT("func:%d:%d"), CurrentDirectory, Index),
					LoweredName(Function.Name), LoweredName(Function.Name));
			}
		}
	}
}

void FElysiumPropHacking::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumTerminal::Serialize(Ar);
	Ar << DirectoryUnlocked;
	Ar << DirectoryAttempts;
	Ar << bEmailUnlocked;
	Ar << EmailAttempts;
	// `DEFINE_ARRAY(m_EmailFlags, FIELD_INTEGER, 128)`. Appended behind its own version at the end
	// of the leaf's block, so a payload written before the mail state existed restores a terminal
	// with every message unread and none deleted — which is the state it was actually saved in.
	// The page, the selected row and the open index are deliberately NOT here: retail's datamap
	// does not carry them either, and OnUseEnd has already forced the directory back to root.
	if (Ar.Version() >= FElysiumSaveVersion::TerminalEmail)
	{
		Ar << EmailFlags;
	}
	if (Ar.IsLoading())
	{
		DirectoryUnlocked.SetNum(Definition.Directories.Num());
		DirectoryAttempts.SetNum(Definition.Directories.Num());
		EmailFlags.SetNumZeroed(EmailFlagCount);
	}
}

void FElysiumPropHacking::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumTerminal::GetDebugState(Out);
	Out.Emplace(TEXT("Hack file"), HackFile.IsEmpty() ? TEXT("(none)") : *HackFile);
	// `-1` is the root and `-2` the mail area, which is the whole reason these are printed as their
	// authored name and not only as an index.
	auto DirectoryLabel = [this](int32 Index)
	{
		if (Index == INDEX_NONE) { return FString(TEXT("-1 root")); }
		if (Index == MailArea) { return FString(TEXT("-2 mail")); }
		return FString::Printf(TEXT("%d %s"), Index, Definition.Directories.IsValidIndex(Index)
			? *Definition.Directories[Index].Name : TEXT("(out of range)"));
	};
	Out.Emplace(TEXT("Current directory"), DirectoryLabel(CurrentDirectory));
	Out.Emplace(TEXT("Pending directory"), DirectoryLabel(PendingDirectory));
	Out.Emplace(TEXT("Input mode"), FString::FromInt(static_cast<int32>(InputMode())));
	// Per-directory `m_SubDirAttempts`, which is what the difficulty escalation reads.
	{
		TArray<FString> Attempts;
		for (int32 Index = 0; Index < Definition.Directories.Num(); ++Index)
		{
			Attempts.Add(FString::Printf(TEXT("%s=%d/%s"), *Definition.Directories[Index].Name,
				DirectoryAttempts.IsValidIndex(Index) ? DirectoryAttempts[Index] : -1,
				(DirectoryUnlocked.IsValidIndex(Index) && DirectoryUnlocked[Index] != 0)
					? TEXT("unlocked") : TEXT("locked")));
		}
		Out.Emplace(TEXT("Directories"), Attempts.Num()
			? FString::Join(Attempts, TEXT(", ")) : TEXT("(none)"));
	}
	Out.Emplace(TEXT("Crack buffer"), CrackBuffer.IsEmpty() ? TEXT("(none)") : *CrackBuffer);
	Out.Emplace(TEXT("Reprint prompt"), bReprintPrompt ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Email unlocked"), bEmailUnlocked ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Email attempts"), FString::FromInt(EmailAttempts));
	int32 ReadCount = 0;
	int32 DeletedCount = 0;
	for (int32 Index = 0; Index < Definition.Emails.Num(); ++Index)
	{
		ReadCount += IsEmailRead(Index) ? 1 : 0;
		DeletedCount += IsEmailDeleted(Index) ? 1 : 0;
	}
	Out.Emplace(TEXT("Email"), FString::Printf(
		TEXT("%d records, %d visible, %d read, %d deleted; page %d, row %d, open %d"),
		Definition.Emails.Num(), MailVisible.Num(), ReadCount, DeletedCount, MailPage,
		MailSelectedRow, MailOpenIndex));
	Out.Emplace(TEXT("Global email"), bGlobalEmail
		? FString::Printf(TEXT("keyed on '%s'"), *TargetName) : TEXT("no"));
	Out.Emplace(TEXT("Last command"), LastCommand);
}
