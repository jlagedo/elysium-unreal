#include "ElysiumLineService.h"

#include "ElysiumWorldServices.h"
#include "Misc/Paths.h"

namespace
{
	FString DialogueSessionId(const FElysiumEntityHandle& Owner)
	{
		return FString::Printf(TEXT("dialogue:%u:%d"), Owner.Epoch, Owner.Index);
	}
}

FElysiumVoiceHandle FElysiumLineService::PlayDirect(const FString& SessionId,
	const FString& AuthoredPath, const FVector& Origin, USceneComponent* AttachTo,
	EElysiumAudioCategory Category, float StartOffsetSeconds)
{
	if (!Audio || AuthoredPath.IsEmpty())
	{
		return FElysiumVoiceHandle::Invalid();
	}
	CancelSession(SessionId);

	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(AuthoredPath);
	Request.Owner.Kind = EElysiumAudioOwnerKind::DialogueSession;
	Request.Owner.StableId = SessionId;
	Request.Category = Category;
	Request.Placement.bSpatialized = true;
	Request.Placement.Location = Origin;
	Request.Placement.AttachTo = AttachTo;
	Request.StartOffsetSeconds = FMath::Max(StartOffsetSeconds, 0.f);
	Request.Priority = 100;
	Request.ConcurrencyKey = TEXT("dialogue.line");
	const FElysiumVoiceHandle Handle = Audio->Submit(MoveTemp(Request));
	if (Handle.IsValid())
	{
		Active.Add(SessionId, Handle);
	}
	return Handle;
}

FString FElysiumLineService::DialogueLineSource(const FString& DlgSourcePath, int32 LineId)
{
	FString Rel = DlgSourcePath;
	Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
	const int32 DlgAt = Rel.Find(TEXT("dlg/"), ESearchCase::IgnoreCase);
	if (DlgAt != INDEX_NONE)
	{
		Rel = Rel.Mid(DlgAt + 4);
	}
	Rel = FPaths::ChangeExtension(Rel, TEXT(""));
	while (Rel.EndsWith(TEXT(".")))
	{
		Rel.LeftChopInline(1);
	}
	const FString Dir = FPaths::GetPath(Rel);
	const FString Stem = FPaths::GetBaseFilename(Rel);
	return FString::Printf(TEXT("character/dlg/%s/%s/line%d_col_e"),
		*Dir, *Stem, LineId).Replace(TEXT("//"), TEXT("/"));
}

FElysiumVoiceHandle FElysiumLineService::PlayDialogueTurn(const FElysiumEntityHandle& Owner,
	const FString& DlgSourcePath, int32 LineId, const FVector& Origin, USceneComponent* AttachTo)
{
	return PlayDirect(DialogueSessionId(Owner), DialogueLineSource(DlgSourcePath, LineId),
		Origin, AttachTo, EElysiumAudioCategory::Dialogue);
}

void FElysiumLineService::CancelSession(const FString& SessionId, float FadeSeconds)
{
	if (FElysiumVoiceHandle* Handle = Active.Find(SessionId))
	{
		if (Audio)
		{
			Audio->StopVoice(*Handle, FadeSeconds);
		}
		Active.Remove(SessionId);
	}
}

void FElysiumLineService::CancelDialogue(const FElysiumEntityHandle& Owner, float FadeSeconds)
{
	CancelSession(DialogueSessionId(Owner), FadeSeconds);
}

void FElysiumLineService::Shutdown()
{
	if (Audio)
	{
		for (const TPair<FString, FElysiumVoiceHandle>& Pair : Active)
		{
			Audio->StopVoice(Pair.Value, 0.f);
		}
	}
	Active.Reset();
}
