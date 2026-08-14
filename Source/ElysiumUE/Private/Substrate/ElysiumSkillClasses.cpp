#include "Substrate/ElysiumSkillClasses.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumViewState.h"
#include "ElysiumWorldServices.h"
#include "Scripting/ElysiumScriptFS.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSkill, Log, All);

namespace
{
	const FName GOnSkillAttemptBegin(TEXT("OnSkillAttemptBegin"));
	const FName GOnSkillAttemptCycle(TEXT("OnSkillAttemptCycle"));
	const FName GOnSkillSuccess(TEXT("OnSkillSuccess"));
	const FName GOnSkillFail(TEXT("OnSkillFail"));
	const FName GOnSkillBotch(TEXT("OnSkillBotch"));
	const FName GOnUnlocked(TEXT("OnUnlocked"));
	const FName GOnUseBegin(TEXT("OnUseBegin"));
	const FName GOnUseEnd(TEXT("OnUseEnd"));

	const TCHAR* FeatForSkillType(int32 SkillType)
	{
		return SkillType == 1 ? TEXT("Intrusion") : SkillType == 2 ? TEXT("Hacking") : nullptr;
	}

	template <typename TObject, typename TMember>
	void AddLeafField(FElysiumClassDesc& D, const TCHAR* Name, TMember TObject::* Member)
	{
		FElysiumFieldAccessor Accessor;
		Accessor.ApplyFlags(ElysiumFieldDefault);
		if constexpr (std::is_same_v<TMember, int32>)
		{
			Accessor.Type = EElysiumVariantType::Int;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::Int(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, bool>)
		{
			Accessor.Type = EElysiumVariantType::Bool;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::Bool(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Accessor.Type = EElysiumVariantType::Float;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::Float(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Accessor.Type = EElysiumVariantType::String;
			Accessor.Get = [Member](const FElysiumEntity& E)
				{ return FElysiumVariant::String(static_cast<const TObject&>(E).*Member); };
			Accessor.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V)
				{ static_cast<TObject&>(E).*Member = V.ToString(); };
		}
		D.Fields.Add(FName(Name), MoveTemp(Accessor));
	}
}

void FElysiumSkillEntity::InputResetDifficulty(int32 NewDifficulty)
{
	Difficulty = FMath::Clamp(NewDifficulty, 0, 10);
	SkillAttempts = 0;
}

float FElysiumSkillEntity::AttemptIntervalSeconds(int32 Rating) const
{
	// vampire.dll FUN_1020aea0: (5.0 - rating * 0.25) / m_flSpeedScale. The current player
	// runtime carries the retail default scale of 1; when speed-scale effects land this denominator
	// becomes the shared character value rather than a lock-local setting.
	return FMath::Max(KINDA_SMALL_NUMBER, 5.0f - FMath::Max(0, Rating) * 0.25f);
}

bool FElysiumSkillEntity::StartAttempt(FElysiumCombatCharacter& User)
{
	const TCHAR* Feat = FeatForSkillType(SkillType);
	if (!World || !Feat || AttemptUser.IsSet())
	{
		return false;
	}
	AttemptUser = User.Handle;
	const int32 Rating = User.CalcFeat(Feat);
	if (Rating > LastSkillLevel)
	{
		SkillAttempts = 0;
	}
	LastAttemptSeconds = static_cast<float>(World->NowSeconds());
	FireOutput(GOnSkillAttemptBegin, User.Handle);
	NextThink = static_cast<float>(World->NowSeconds() + AttemptIntervalSeconds(Rating));
	const int32 EffectiveDifficulty = AttemptDifficulty();
	UE_LOG(LogElysiumSkill, Display,
		TEXT("%s: %s attempt started (rating %d, difficulty %d); Intrusion HUD stubbed"),
		*DebugString(), Feat, Rating, EffectiveDifficulty);
	return true;
}

void FElysiumSkillEntity::StopAttempt()
{
	AttemptUser = FElysiumEntityHandle::Invalid();
	NextThink = ELYSIUM_NEVER_THINK;
}

void FElysiumSkillEntity::ResolveAttempt(FElysiumCombatCharacter& User)
{
	const TCHAR* FeatName = FeatForSkillType(SkillType);
	if (!FeatName)
	{
		StopAttempt();
		return;
	}
	const int32 Rating = User.CalcFeat(FeatName);
	const int32 EffectiveDifficulty = AttemptDifficulty();
	// FUN_1020b090 selects the non-roll helper: a normal Intrusion/Hacking attempt can only
	// produce generic result tier 3 (pass) or 1 (fail). `diceroll` is a dead Hammer key here.
	LastRoll = Rating >= EffectiveDifficulty ? 3 : 1;
	LastAttemptSeconds = static_cast<float>(World->NowSeconds());
	FireOutput(GOnSkillAttemptCycle, User.Handle);
	++SkillAttempts;
	UE_LOG(LogElysiumSkill, Display, TEXT("%s: %s attempt resolved — %s (%d >= %d); HUD stubbed"),
		*DebugString(), FeatName, LastRoll > 2 ? TEXT("Success") : TEXT("Failure"),
		Rating, EffectiveDifficulty);

	if (LastRoll > 2)
	{
		FireOutput(GOnSkillSuccess, User.Handle);
		OnSkillSucceeded(User);
		return;
	}
	FireOutput(LastRoll == 0 ? GOnSkillBotch : GOnSkillFail, User.Handle);
	OnSkillFailed(User);
}

void FElysiumSkillEntity::OnSkillFailed(FElysiumCombatCharacter&)
{
	if (World)
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed);
	}
}

void FElysiumSkillEntity::Think()
{
	FElysiumEntity* UserEntity = World ? World->Resolve(AttemptUser) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User || User->IsInert())
	{
		StopAttempt();
		return;
	}
	ResolveAttempt(*User);
}

void FElysiumSkillEntity::Serialize(FElysiumSaveArchive& Ar)
{
	Ar << LastRoll;
	Ar << LastAttemptSeconds;
	Ar << SkillAttempts;
	Ar << LastSkillLevel;
}

const TCHAR* FElysiumSkillEntity::SaveBlockReason() const
{
	return AttemptUser.IsSet() ? TEXT("a skill attempt is in progress") : nullptr;
}

void FElysiumSkillEntity::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	Out.Emplace(TEXT("Skill"), FeatForSkillType(SkillType)
		? FString(FeatForSkillType(SkillType)) : TEXT("(none)"));
	Out.Emplace(TEXT("Difficulty"), FString::FromInt(Difficulty));
	Out.Emplace(TEXT("Last roll / lock"), FString::FromInt(LastRoll));
	Out.Emplace(TEXT("Attempts"), FString::FromInt(SkillAttempts));
	Out.Emplace(TEXT("Last skill level"), FString::FromInt(LastSkillLevel));
	Out.Emplace(TEXT("Attempt user"), AttemptUser.IsSet() ? AttemptUser.ToString() : TEXT("(none)"));
}

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

	Out.ScreenSaver = Root->Str(TEXT("screen saver"), FString());
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
			FElysiumTerminalEmail Email;
			Email.Subject = Child.Value->Str(TEXT("subject"), FString());
			Email.Sender = Child.Value->Str(TEXT("sender"), FString());
			Email.Body = Child.Value->Str(TEXT("body"), FString());
			Email.Dependency = Child.Value->Str(TEXT("dependency"), FString());
			Email.RunScript = Child.Value->Str(TEXT("runscript"), FString());
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

void FElysiumTerminal::Spawn()
{
	FString ContentError;
	if (!OpenContent(ContentError))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s terminal content failed: %s"),
			*DebugString(), *ContentError);
		bStartEnabled = false;
	}

	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def)
	{
		return;
	}
	VisualStem = Def->ModelMesh.IsEmpty()
		? FElysiumContentPaths::PropModelStem(Model) : Def->ModelMesh;
	if (VisualStem.IsEmpty())
	{
		return;
	}
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
		World->RegisterPropBody(WorldBody, Handle);
		World->SetUseAnchorEnabled(Handle, bStartEnabled && !IsInert());
	}
}

bool FElysiumTerminal::CanPlayerFocus(const FElysiumUseContext& Context) const
{
	if (!bStartEnabled || IsInert() || CurrentUser.IsSet() || !World)
	{
		return false;
	}
	const FElysiumEntity* User = World->Resolve(Context.Activator);
	return User && User->AsCombatCharacter();
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
	BeginContentSession();
	return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
}

void FElysiumTerminal::EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason)
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
	StopAttempt();
	EndContentSession();
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
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
}

void FElysiumTerminal::InputDisable()
{
	bStartEnabled = false;
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, false);
		if (CurrentUser.IsSet())
		{
			World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
		}
	}
}

bool FElysiumTerminal::Submit(uint32 ExpectedSerial, const FString& Command)
{
	if (!CurrentUser.IsSet() || ExpectedSerial != SessionSerial || AttemptUser.IsSet())
	{
		return false;
	}
	const bool bAccepted = SubmitContent(Command.Left(16));
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

void FElysiumTerminal::BuildView(FElysiumTerminalView& Out) const
{
	Out = FElysiumTerminalView();
	if (!CurrentUser.IsSet())
	{
		return;
	}
	Out.Owner = Handle;
	Out.SessionSerial = SessionSerial;
	Out.Revision = ViewRevision;
	Out.Columns = FMath::Max(1, TextColumns);
	Out.Rows = FMath::Max(1, TextRows);
	BuildContentView(Out);
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
		World->SetUseAnchorEnabled(Handle, bStartEnabled && !IsInert());
		if (IsInert() && CurrentUser.IsSet())
		{
			World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
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
	Out.Emplace(TEXT("Terminal enabled"), bStartEnabled ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Terminal user"), CurrentUser.IsSet() ? CurrentUser.ToString() : TEXT("(none)"));
	Out.Emplace(TEXT("Session serial"), FString::Printf(TEXT("%u"), SessionSerial));
	Out.Emplace(TEXT("View revision"), FString::Printf(TEXT("%u"), ViewRevision));
	Out.Emplace(TEXT("Grid"), FString::Printf(TEXT("%dx%d"), TextColumns, TextRows));
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

void FElysiumPropHacking::BeginContentSession()
{
	CurrentDirectory = INDEX_NONE;
	PendingDirectory = INDEX_NONE;
	InputMode = EElysiumTerminalInputMode::Line;
	LastCommand.Reset();
	LastRejection.Reset();
	RenderRoot();
}

void FElysiumPropHacking::EndContentSession()
{
	CurrentDirectory = INDEX_NONE;
	PendingDirectory = INDEX_NONE;
	InputMode = EElysiumTerminalInputMode::Line;
	ScreenLines.Reset();
	VisibleDirectories.Reset();
	VisibleFunctions.Reset();
}

bool FElysiumPropHacking::DependencyPasses(const FString& Source) const
{
	return Source.IsEmpty() || (World && World->EvalCondition(Source, Handle, CurrentUser).ToBool());
}

void FElysiumPropHacking::AppendLine(const FString& Line)
{
	TArray<FString> Lines;
	Line.ParseIntoArrayLines(Lines, false);
	if (Lines.IsEmpty())
	{
		Lines.Add(FString());
	}
	for (FString& Row : Lines)
	{
		ScreenLines.Add(Row.Left(FMath::Max(1, TextColumns)));
	}
	const int32 Overflow = ScreenLines.Num() - FMath::Max(1, TextRows);
	if (Overflow > 0)
	{
		ScreenLines.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
}

void FElysiumPropHacking::RenderRoot()
{
	ScreenLines.Reset();
	VisibleDirectories.Reset();
	VisibleFunctions.Reset();
	for (const FString& Line : Definition.LogonLines)
	{
		AppendLine(Line);
	}
	for (int32 Index = 0; Index < Definition.Directories.Num(); ++Index)
	{
		const FElysiumTerminalDirectory& Directory = Definition.Directories[Index];
		if (DependencyPasses(Directory.Dependency))
		{
			VisibleDirectories.Add(Index);
			AppendLine(Directory.Name + (Directory.Description.IsEmpty()
				? FString() : TEXT(" - ") + Directory.Description));
		}
	}
}

void FElysiumPropHacking::RenderDirectory()
{
	ScreenLines.Reset();
	VisibleDirectories.Reset();
	VisibleFunctions.Reset();
	if (!Definition.Directories.IsValidIndex(CurrentDirectory))
	{
		RenderRoot();
		return;
	}
	const FElysiumTerminalDirectory& Directory = Definition.Directories[CurrentDirectory];
	AppendLine(Directory.Description.IsEmpty() ? Directory.Name : Directory.Description);
	for (int32 Index = 0; Index < Directory.Functions.Num(); ++Index)
	{
		const FElysiumTerminalFunction& Function = Directory.Functions[Index];
		if (DependencyPasses(Function.Dependency))
		{
			VisibleFunctions.Add(Index);
			AppendLine(Function.Name + (Function.Description.IsEmpty()
				? FString() : TEXT(" - ") + Function.Description));
		}
	}
}

void FElysiumPropHacking::EnterDirectory(int32 Index)
{
	if (!Definition.Directories.IsValidIndex(Index))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot enter terminal directory %d"),
			*DebugString(), Index);
		ReturnToRoot();
		return;
	}
	CurrentDirectory = Index;
	PendingDirectory = INDEX_NONE;
	InputMode = EElysiumTerminalInputMode::Line;
	DirectoryUnlocked[Index] = 1;
	RenderDirectory();
}

void FElysiumPropHacking::ReturnToRoot()
{
	CurrentDirectory = INDEX_NONE;
	PendingDirectory = INDEX_NONE;
	InputMode = EElysiumTerminalInputMode::Line;
	RenderRoot();
}

bool FElysiumPropHacking::RoutePassword(const FString& Command)
{
	if (!Definition.Directories.IsValidIndex(PendingDirectory))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s has invalid pending directory %d"),
			*DebugString(), PendingDirectory);
		ReturnToRoot();
		return false;
	}
	if (Command.Equals(TEXT("break"), ESearchCase::IgnoreCase))
	{
		return BeginContentHack();
	}
	return AcceptPassword(Command);
}

bool FElysiumPropHacking::AcceptPassword(const FString& Command)
{
	if (!Definition.Directories.IsValidIndex(PendingDirectory))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot accept a password for pending directory %d"),
			*DebugString(), PendingDirectory);
		ReturnToRoot();
		return false;
	}
	const FElysiumTerminalDirectory& Directory = Definition.Directories[PendingDirectory];
	if (Command.Equals(Directory.Password, ESearchCase::IgnoreCase))
	{
		EnterDirectory(PendingDirectory);
		return true;
	}
	++DirectoryAttempts[PendingDirectory];
	LastRejection = Command;
	AppendLine(TEXT("Invalid password"));
	return true;
}

bool FElysiumPropHacking::ExecuteFunction(const FElysiumTerminalFunction& Function)
{
	if (!DependencyPasses(Function.Dependency))
	{
		return false;
	}
	AppendLine(Function.RunText);
	if (Function.Trigger != INDEX_NONE)
	{
		FireOutput(FName(*FString::Printf(TEXT("OnTrigger%d"), Function.Trigger)), CurrentUser);
	}
	if (!Function.RunScript.IsEmpty() && World)
	{
		// The bridge accepts statement bodies on this seam. Any output raised by the statement joins
		// the same queue after the trigger already enqueued above.
		World->EvalCondition(Function.RunScript, Handle, CurrentUser);
	}
	return true;
}

bool FElysiumPropHacking::RouteNormalCommand(const FString& Command)
{
	if (Command.IsEmpty() || Command.Equals(TEXT("list"), ESearchCase::IgnoreCase))
	{
		CurrentDirectory == INDEX_NONE ? RenderRoot() : RenderDirectory();
		return true;
	}
	if (Command.Equals(TEXT("quit"), ESearchCase::IgnoreCase))
	{
		return World && World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed);
	}
	if (Command.Equals(TEXT("home"), ESearchCase::IgnoreCase))
	{
		ReturnToRoot();
		return true;
	}
	if (Command.Equals(TEXT("help"), ESearchCase::IgnoreCase))
	{
		AppendLine(TEXT("help  list  home  email  quit"));
		return true;
	}
	if (Command.Equals(TEXT("email"), ESearchCase::IgnoreCase))
	{
		LastRejection = Command;
		AppendLine(TEXT("Email is not available in this terminal slice"));
		return true;
	}

	if (CurrentDirectory == INDEX_NONE)
	{
		for (int32 Index = 0; Index < Definition.Directories.Num(); ++Index)
		{
			const FElysiumTerminalDirectory& Directory = Definition.Directories[Index];
			if (!Command.Equals(Directory.Name, ESearchCase::IgnoreCase)
				|| !DependencyPasses(Directory.Dependency))
			{
				continue;
			}
			if (Directory.Password.IsEmpty() || DirectoryUnlocked[Index] != 0)
			{
				EnterDirectory(Index);
			}
			else
			{
				PendingDirectory = Index;
				InputMode = EElysiumTerminalInputMode::Password;
				AppendLine(FString::Printf(TEXT("Password required for %s"), *Directory.Name));
			}
			return true;
		}
	}
	else if (Definition.Directories.IsValidIndex(CurrentDirectory))
	{
		for (const FElysiumTerminalFunction& Function
			: Definition.Directories[CurrentDirectory].Functions)
		{
			if (Command.Equals(Function.Name, ESearchCase::IgnoreCase)
				&& ExecuteFunction(Function))
			{
				return true;
			}
		}
	}

	LastRejection = Command;
	AppendLine(FString::Printf(TEXT("Invalid command: %s"), *Command));
	return true;
}

bool FElysiumPropHacking::SubmitContent(const FString& Command)
{
	LastCommand = Command;
	return InputMode == EElysiumTerminalInputMode::Password
		? RoutePassword(Command) : RouteNormalCommand(Command);
}

bool FElysiumPropHacking::BeginContentHack()
{
	if (InputMode != EElysiumTerminalInputMode::Password
		|| !Definition.Directories.IsValidIndex(PendingDirectory) || !World)
	{
		return false;
	}
	FElysiumEntity* UserEntity = World->Resolve(CurrentUser);
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User)
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot start terminal hack: user %s is invalid"),
			*DebugString(), *CurrentUser.ToString());
		return false;
	}
	return StartAttempt(*User);
}

int32 FElysiumPropHacking::AttemptDifficulty() const
{
	if (!Definition.Directories.IsValidIndex(PendingDirectory))
	{
		return Difficulty;
	}
	const int32 DirectoryDifficulty = Definition.Directories[PendingDirectory].Difficulty;
	return DirectoryDifficulty > 0 ? DirectoryDifficulty : Difficulty;
}

void FElysiumPropHacking::OnSkillSucceeded(FElysiumCombatCharacter&)
{
	StopAttempt();
	if (Definition.Directories.IsValidIndex(PendingDirectory))
	{
		// Retail fills the real password and rejoins the ordinary comparison callback. Keep that
		// convergence explicit so typed and skill-mediated acceptance cannot drift apart.
		AcceptPassword(Definition.Directories[PendingDirectory].Password);
		++ViewRevision;
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s terminal hack succeeded without a valid pending directory"), *DebugString());
		ReturnToRoot();
	}
}

void FElysiumPropHacking::OnSkillFailed(FElysiumCombatCharacter&)
{
	StopAttempt();
	if (Definition.Directories.IsValidIndex(PendingDirectory))
	{
		// The native failure path submits randomized non-password text through the same comparison,
		// then returns to root. An empty value is guaranteed to differ because only password-bearing
		// directories can enter this state.
		AcceptPassword(FString());
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s terminal hack failed without a valid pending directory"), *DebugString());
	}
	LastRejection = TEXT("break");
	ReturnToRoot();
	AppendLine(TEXT("Hacking skill insufficient"));
	++ViewRevision;
}

void FElysiumPropHacking::OnAttemptStopped(FElysiumCombatCharacter&, EElysiumUseEndReason)
{
	StopAttempt();
}

void FElysiumPropHacking::BuildContentView(FElysiumTerminalView& Out) const
{
	Out.ScreenSaverLabel = Definition.ScreenSaver;
	Out.InputMode = static_cast<uint8>(InputMode);
	Out.MaxInput = 16;
	Out.bAcceptsDirectoryKeys = true;
	Out.ScreenRows.Reserve(Out.Rows);
	for (int32 Row = 0; Row < Out.Rows; ++Row)
	{
		const FString Source = ScreenLines.IsValidIndex(Row) ? ScreenLines[Row] : FString();
		Out.ScreenRows.Add(Source.Left(Out.Columns).RightPad(Out.Columns));
	}
	Out.CursorRow = FMath::Clamp(ScreenLines.Num() - 1, 0, Out.Rows - 1);
	Out.CursorColumn = ScreenLines.IsEmpty() ? 0
		: FMath::Min(ScreenLines.Last().Len(), Out.Columns - 1);

	auto AddAction = [&Out](const FString& Id, const FString& Label, const FString& Command)
	{
		FElysiumTerminalActionView Action;
		Action.Id = Id;
		Action.Label = Label;
		Action.Command = Command;
		Out.Actions.Add(MoveTemp(Action));
	};
	if (InputMode == EElysiumTerminalInputMode::Password)
	{
		AddAction(TEXT("hack"), TEXT("Bypass password"), TEXT("break"));
		return;
	}
	AddAction(TEXT("list"), TEXT("List"), TEXT("list"));
	AddAction(TEXT("help"), TEXT("Help"), TEXT("help"));
	if (CurrentDirectory != INDEX_NONE)
	{
		AddAction(TEXT("home"), TEXT("Home"), TEXT("home"));
	}
	AddAction(TEXT("quit"), TEXT("Quit"), TEXT("quit"));
	if (CurrentDirectory == INDEX_NONE)
	{
		for (const int32 Index : VisibleDirectories)
		{
			if (!Definition.Directories.IsValidIndex(Index))
			{
				continue;
			}
			const FElysiumTerminalDirectory& Directory = Definition.Directories[Index];
			AddAction(FString::Printf(TEXT("dir:%d"), Index), Directory.Name, Directory.Name);
		}
	}
	else if (Definition.Directories.IsValidIndex(CurrentDirectory))
	{
		const FElysiumTerminalDirectory& Directory = Definition.Directories[CurrentDirectory];
		for (const int32 Index : VisibleFunctions)
		{
			if (!Directory.Functions.IsValidIndex(Index))
			{
				continue;
			}
			const FElysiumTerminalFunction& Function = Directory.Functions[Index];
			AddAction(FString::Printf(TEXT("func:%d:%d"), CurrentDirectory, Index),
				Function.Name, Function.Name);
		}
	}
}

void FElysiumPropHacking::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumTerminal::Serialize(Ar);
	Ar << DirectoryUnlocked;
	Ar << DirectoryAttempts;
	if (Ar.IsLoading())
	{
		DirectoryUnlocked.SetNum(Definition.Directories.Num());
		DirectoryAttempts.SetNum(Definition.Directories.Num());
	}
}

void FElysiumPropHacking::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumTerminal::GetDebugState(Out);
	Out.Emplace(TEXT("Hack file"), HackFile);
	Out.Emplace(TEXT("Current directory"), FString::FromInt(CurrentDirectory));
	Out.Emplace(TEXT("Pending directory"), FString::FromInt(PendingDirectory));
	Out.Emplace(TEXT("Input mode"), FString::FromInt(static_cast<int32>(InputMode)));
	Out.Emplace(TEXT("Last command"), LastCommand);
	Out.Emplace(TEXT("Last rejection"), LastRejection);
}

void FElysiumLockableEntity::Spawn()
{
	LastRoll = Difficulty != 0 ? 1 : 3;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def)
	{
		return;
	}
	VisualStem = Def->ModelMesh.IsEmpty()
		? FElysiumContentPaths::PropModelStem(Model) : Def->ModelMesh;
	if (VisualStem.IsEmpty())
	{
		return;
	}
	const FQuat StaticRotation = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f)) : Def->ModelQuat;
	const FQuat SkeletalRotation = Def->ModelMesh.IsEmpty()
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)) : Def->ModelQuat;
	FElysiumPlacedModelRequest Request;
	Request.ModelPath = Model;
	Request.StaticStem = VisualStem;
	Request.Location = Origin;
	Request.Rotation = SkeletalRotation;
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	if (Embodiment->HasPlacedModelCatalogue())
	{
		const FElysiumPlacedModelBody Placed = Embodiment->BuildPlacedModelBody(Request);
		WorldBody = Placed.Visual;
		AnimatedStem = Placed.Stem;
	}
	else
	{
		AnimatedStem = Embodiment->AnimatedPropStemForModel(Model);
		if (!AnimatedStem.IsEmpty())
		{
			WorldBody = Embodiment->BuildAnimatedPropVisual(AnimatedStem, Origin, SkeletalRotation,
				Embodiment->BodyScaleFor(*Def), Handle.Index);
		}
		else
		{
			WorldBody = Embodiment->BuildPropVisual(
				VisualStem, Origin, StaticRotation, Embodiment->BodyScaleFor(*Def));
		}
	}
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody, Handle);
	}
	OnLockPresentationChanged();
}

void FElysiumLockableEntity::PostSpawn()
{
	FElysiumEntity::PostSpawn();
	FElysiumEntity* Parent = World && !ParentName.IsEmpty() ? World->FindByName(ParentName) : nullptr;
	if (!Parent || !AttachToParent(*Parent))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s attached to invalid owner: %s"),
			*DebugString(), *ParentName);
		Kill();
	}
}

void FElysiumLockableEntity::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumSkillEntity::Serialize(Ar);
	if (!Ar.IsLoading())
	{
		return;
	}
	OnLockPresentationChanged();
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			Container->NotifyLockState(Handle, IsUseLocked());
		}
	}
}

bool FElysiumLockableEntity::AttachToParent(FElysiumEntity& Parent)
{
	FElysiumDoorBase* Door = Parent.AsDoorBase();
	if (!Door)
	{
		return false;
	}
	AttachedOwner = Door->Handle;
	Door->RegisterDoorknob(*this);
	return !IsDead();
}

void FElysiumLockableEntity::InputLock()
{
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumDoorBase* Door = Parent->AsDoorBase())
		{
			Door->InputLock();
			return;
		}
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			ApplyDoorLockState(true);
			Container->NotifyLockState(Handle, true);
			return;
		}
	}
	ApplyDoorLockState(true);
}

void FElysiumLockableEntity::InputUnlock(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUnlocked, Activator);
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumDoorBase* Door = Parent->AsDoorBase())
		{
			Door->InputUnlock();
		}
		else if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			ApplyDoorLockState(false);
			Container->NotifyLockState(Handle, false);
		}
		else
		{
			ApplyDoorLockState(false);
		}
	}
	else
	{
		ApplyDoorLockState(false);
	}
	OnUnlocked(Activator);
}

void FElysiumLockableEntity::ApplyDoorLockState(bool bLocked)
{
	LastRoll = bLocked ? 1 : 3;
	OnLockPresentationChanged();
}

int32 FElysiumLockableEntity::ResolveUseIcon(const FElysiumEntityHandle& Activator) const
{
	if (IsUseLocked() && KeyIcon != 0 && !KeyName.IsEmpty() && World)
	{
		const FElysiumEntity* Entity = World->Resolve(Activator);
		const FElysiumCombatCharacter* User = Entity ? Entity->AsCombatCharacter() : nullptr;
		if (User && User->Inventory.Has(*User, KeyName))
		{
			return KeyIcon;
		}
	}
	return GetUseIcon();
}

FElysiumUseBeginResult FElysiumLockableEntity::BeginPlayerUse(const FElysiumUseContext& Context)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User)
	{
		return FElysiumUseBeginResult::Completed();
	}
	if (!IsUseLocked())
	{
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (!KeyName.IsEmpty() && User->Inventory.Has(*User, KeyName))
	{
		if (bDeleteKey)
		{
			User->Inventory.ScriptRemove(*User, KeyName);
		}
		InputUnlock(Context.Activator);
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (bRequiresKey || !User->Inventory.Has(*User, TEXT("item_g_lockpick")))
	{
		UE_LOG(LogElysiumSkill, Display, TEXT("%s: locked use refused — %s"), *DebugString(),
			bRequiresKey ? TEXT("requires key") : TEXT("no item_g_lockpick"));
		return FElysiumUseBeginResult::Completed();
	}
	FireOutput(GOnUseBegin, Context.Activator);
	bUseOutputsOpen = true;
	if (StartAttempt(*User))
	{
		return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
	}
	FinishUseOutputs(Context.Activator);
	return FElysiumUseBeginResult::Completed();
}

void FElysiumLockableEntity::EndPlayerUse(const FElysiumUseContext& Context,
	EElysiumUseEndReason Reason)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (User)
	{
		if (const TCHAR* Feat = FeatForSkillType(SkillType))
		{
			LastSkillLevel = User->CalcFeat(Feat);
		}
		OnAttemptStopped(*User, Reason);
	}
	else
	{
		StopAttempt();
	}
	FinishUseOutputs(Context.Activator);
}

void FElysiumLockableEntity::Use(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUseBegin, Activator);
	ForwardUse(Activator);
	FireOutput(GOnUseEnd, Activator);
}

void FElysiumLockableEntity::OnSkillSucceeded(FElysiumCombatCharacter& User)
{
	InputUnlock(User.Handle);
	if (World && !World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed))
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s skill success could not close its captured +use session"), *DebugString());
	}
	ForwardUse(User.Handle);
}

void FElysiumLockableEntity::OnAttemptStopped(FElysiumCombatCharacter&, EElysiumUseEndReason)
{
	StopAttempt();
}

void FElysiumLockableEntity::FinishUseOutputs(const FElysiumEntityHandle& Activator)
{
	if (bUseOutputsOpen)
	{
		bUseOutputsOpen = false;
		FireOutput(GOnUseEnd, Activator);
	}
}

void FElysiumLockableEntity::ForwardUse(const FElysiumEntityHandle&)
{
	UE_LOG(LogElysiumSkill, Warning,
		TEXT("%s accepted lockable use but has no specialized attachment forwarder"), *DebugString());
}

void FElysiumLockableEntity::OnLockPresentationChanged() {}

FElysiumPropDoorknob::FElysiumPropDoorknob()
{
	UseIcon = 10;
	LockedIcon = 3;
	KeyIcon = 4;
}

void FElysiumPropDoorknob::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached door %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

void FElysiumPropDoorknob::OnLockPresentationChanged()
{
	USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(WorldBody);
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Animated || !Embodiment || AnimatedStem.IsEmpty())
	{
		if (Embodiment && Embodiment->HasPlacedModelCatalogue() && WorldBody)
		{
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s cannot present doorknob lock state: animated body/stem did not resolve"),
				*DebugString());
		}
		return;
	}
	const FString Clip = IsUseLocked() ? TEXT("handle_locked") : TEXT("handle_unlocked");
	bool bLoops = false;
	if (Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops))
	{
		if (!Embodiment->PlayAnimatedPropClip(
			Animated, AnimatedStem, Clip, /*bLoop*/ false, nullptr))
		{
			UE_LOG(LogElysiumSkill, Warning, TEXT("%s failed to play doorknob sequence '%s' on %s"),
				*DebugString(), *Clip, *AnimatedStem);
		}
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s has no doorknob sequence '%s' on %s"),
			*DebugString(), *Clip, *AnimatedStem);
	}
}

FElysiumElectronicDoorknob::FElysiumElectronicDoorknob()
{
	UseIcon = 53;
	LockedIcon = 54;
	KeyIcon = 5;
}

void FElysiumElectronicDoorknob::Spawn()
{
	bRequiresKey = true;
	FElysiumPropDoorknob::Spawn();
}

void FElysiumElectronicDoorknob::OnLockPresentationChanged()
{
	FElysiumPropDoorknob::OnLockPresentationChanged();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!WorldBody || !Embodiment || VisualStem.IsEmpty())
	{
		return;
	}
	const int32 Family = IsUseLocked() ? 0 : 1;
	if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(WorldBody))
	{
		Embodiment->ApplyAnimatedPropSkin(Skeletal, VisualStem, Family);
	}
	else if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(WorldBody))
	{
		Embodiment->ApplyPropSkin(Static, VisualStem, Family);
	}
}

FElysiumContainerLock::FElysiumContainerLock()
{
	UseIcon = 10;
	LockedIcon = 3;
}

bool FElysiumContainerLock::AttachToParent(FElysiumEntity& Parent)
{
	FElysiumItemContainer* Container = Parent.AsItemContainer();
	if (!Container || !Container->RegisterLock(*this))
	{
		return false;
	}
	AttachedOwner = Container->Handle;
	return true;
}

void FElysiumContainerLock::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumItemContainer* Container = Parent ? Parent->AsItemContainer() : nullptr)
	{
		const FElysiumUseBeginResult Result = World->BeginPlayerUseSession(
			Container->Handle, Activator);
		if (Result.Outcome != EElysiumUseOutcome::SessionStarted)
		{
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s unlocked but failed to forward +use to %s (outcome=%d)"),
				*DebugString(), *Container->DebugString(), static_cast<int32>(Result.Outcome));
		}
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached container %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

FElysiumPadlock::FElysiumPadlock()
{
	UseIcon = 10;
	LockedIcon = 3;
	KeyIcon = 4;
}

void FElysiumPadlock::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached door %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

void FElysiumPadlock::OnUnlocked(const FElysiumEntityHandle&)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->UnregisterDoorknob(Handle);
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s unlocked without a live attached door %s"),
			*DebugString(), *AttachedOwner.ToString());
	}
	Kill();
}

void FElysiumLockableEntity::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	if (WorldBody)
	{
		WorldBody->SetVisibility(!IsInert());
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
}

void FElysiumLockableEntity::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (!WorldBody)
	{
		return;
	}
	const FQuat Rotation = Cast<USkeletalMeshComponent>(WorldBody)
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles))
		: FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
	WorldBody->SetWorldLocationAndRotation(Origin, Rotation);
}

UPrimitiveComponent* FElysiumLockableEntity::GetAttachBody() const
{
	return WorldBody;
}

void FElysiumLockableEntity::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumSkillEntity::GetDebugState(Out);
	Out.Emplace(TEXT("Locked"), IsUseLocked() ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Attached owner"), AttachedOwner.IsSet() ? AttachedOwner.ToString() : TEXT("(none)"));
}

namespace
{
	TUniquePtr<FElysiumEntity> MakeSkillEntity() { return MakeUnique<FElysiumSkillEntity>(); }
	TUniquePtr<FElysiumEntity> MakeTerminal() { return MakeUnique<FElysiumTerminal>(); }
	TUniquePtr<FElysiumEntity> MakePropHacking() { return MakeUnique<FElysiumPropHacking>(); }
	TUniquePtr<FElysiumEntity> MakeLockableEntity() { return MakeUnique<FElysiumLockableEntity>(); }
	TUniquePtr<FElysiumEntity> MakeDoorknob() { return MakeUnique<FElysiumPropDoorknob>(); }
	TUniquePtr<FElysiumEntity> MakeElectronicDoorknob() { return MakeUnique<FElysiumElectronicDoorknob>(); }
	TUniquePtr<FElysiumEntity> MakeContainerLock() { return MakeUnique<FElysiumContainerLock>(); }
	TUniquePtr<FElysiumEntity> MakePadlock() { return MakeUnique<FElysiumPadlock>(); }

	struct FElysiumSkillRegistrar
	{
		FElysiumSkillRegistrar()
		{
			FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
			FElysiumClassDesc& Skill = Registry.Register(ElysiumSkillEntityClassName(),
				ElysiumBaseClassName(), &MakeSkillEntity);
			Skill.Input(TEXT("ResetDifficulty"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumSkillEntity&>(E).InputResetDifficulty(A.Param.ToInt()); });
			AddLeafField(Skill, TEXT("difficulty"), &FElysiumSkillEntity::Difficulty);
			AddLeafField(Skill, TEXT("skilltype"), &FElysiumSkillEntity::SkillType);

			FElysiumClassDesc& Terminal = Registry.Register(ElysiumTerminalClassName(),
				ElysiumSkillEntityClassName(), &MakeTerminal);
			Terminal.Input(TEXT("Enable"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumTerminal&>(E).InputEnable(); });
			Terminal.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumTerminal&>(E).InputDisable(); });
			AddLeafField(Terminal, TEXT("start_enabled"), &FElysiumTerminal::bStartEnabled);
			AddLeafField(Terminal, TEXT("textcolumns"), &FElysiumTerminal::TextColumns);
			AddLeafField(Terminal, TEXT("textrows"), &FElysiumTerminal::TextRows);
			AddLeafField(Terminal, TEXT("colorscheme"), &FElysiumTerminal::ColorScheme);
			AddLeafField(Terminal, TEXT("soundgroup"), &FElysiumTerminal::SoundGroup);

			FElysiumClassDesc& Hacking = Registry.Register(FName(TEXT("prop_hacking")),
				ElysiumTerminalClassName(), &MakePropHacking);
			AddLeafField(Hacking, TEXT("hack_file"), &FElysiumPropHacking::HackFile);
			AddLeafField(Hacking, TEXT("global_email"), &FElysiumPropHacking::bGlobalEmail);
			AddLeafField(Hacking, TEXT("ss_delay"), &FElysiumPropHacking::ScreenSaverDelay);
			AddLeafField(Hacking, TEXT("ss_start"), &FElysiumPropHacking::ScreenSaverStart);

			FElysiumClassDesc& Lockable = Registry.Register(ElysiumLockableEntityClassName(),
				ElysiumSkillEntityClassName(), &MakeLockableEntity);
			Lockable.Input(TEXT("Lock"), [](FElysiumEntity& E, const FElysiumInputArgs&)
				{ static_cast<FElysiumLockableEntity&>(E).InputLock(); });
			Lockable.Input(TEXT("Unlock"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumLockableEntity&>(E).InputUnlock(A.Activator); });
			Lockable.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
				{ static_cast<FElysiumLockableEntity&>(E).Use(A.Activator); });
			AddLeafField(Lockable, TEXT("key_name"), &FElysiumLockableEntity::KeyName);
			AddLeafField(Lockable, TEXT("delete_key"), &FElysiumLockableEntity::bDeleteKey);
			AddLeafField(Lockable, TEXT("requires_key"), &FElysiumLockableEntity::bRequiresKey);
			AddLeafField(Lockable, TEXT("key_icon"), &FElysiumLockableEntity::KeyIcon);

			Registry.Register(FName(TEXT("prop_doorknob")),
				ElysiumLockableEntityClassName(), &MakeDoorknob);
			Registry.Register(FName(TEXT("prop_doorknob-wesp")),
				ElysiumLockableEntityClassName(), &MakeDoorknob);
			Registry.Register(FName(TEXT("prop_doorknob_electronic")),
				ElysiumLockableEntityClassName(), &MakeElectronicDoorknob);
			Registry.Register(FName(TEXT("item_container_lock")),
				ElysiumLockableEntityClassName(), &MakeContainerLock);
			Registry.Register(FName(TEXT("prop_padlock")),
				ElysiumLockableEntityClassName(), &MakePadlock);
		}
	};

	const FElysiumSkillRegistrar GSkillRegistrar;
}
