#include "ElysiumLineService.h"

#include "ElysiumDlg.h"
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
	EElysiumAudioCategory Category, float StartOffsetSeconds, float Gain)
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
	Request.Gain = FMath::Max(Gain, 0.f);
	Request.Priority = 100;
	Request.ConcurrencyKey = TEXT("dialogue.line");
	const FElysiumVoiceHandle Handle = Audio->Submit(MoveTemp(Request));
	if (Handle.IsValid())
	{
		Active.Add(SessionId, Handle);
	}
	return Handle;
}

bool FElysiumLineService::IsLetterlessText(const FString& RawText)
{
	// `FUN_100df0b0`: count the bytes in 0x41..0x5a, 0x61..0x7a or >= 0xc0 and answer "letterless"
	// when the count is zero. The high range is the Latin-1 accented block the shipped text uses;
	// on our UTF-16 strings every code point above 0xbf answers the same question.
	for (const TCHAR C : RawText)
	{
		const uint32 Code = static_cast<uint32>(C);
		if ((Code >= 0x41 && Code <= 0x5a) || (Code >= 0x61 && Code <= 0x7a) || Code >= 0xc0)
		{
			return false;
		}
	}
	return true;
}

TCHAR FElysiumLineService::TakeLetterFor(const FElysiumDlgLine& Line, bool bMale, int32 ClanOffset)
{
	// `generate_speech_filename` tests `line[1]` — raw col-1 — before it calls the column chooser
	// (`0x100e15c0`), so the ellipses route does not depend on which column would be displayed.
	if (IsLetterlessText(Line.TextMale))
	{
		return EllipsesTake;
	}
	return ElysiumDlgText::ChosenTakeLetter(Line, bMale, ClanOffset);
}

float FElysiumLineService::SpeechVolumeFor(const FElysiumEntityHandle&)
{
	// Seam only — see the header's TODO(dialogue-plan) for `m_flSpeechVol`.
	return 1.f;
}

FString FElysiumLineService::DialogueLineSource(const FString& DlgSourcePath, int32 LineId,
	TCHAR TakeLetter)
{
	if (TakeLetter == EllipsesTake)
	{
		// `s_sound_character_dlg_ellipses__s` @ `10562314`, with the owned `sound/` prefix stripped.
		// One shared take for every wordless row; it has its own `.lip` and no `.vcd`.
		return TEXT("character/dlg/ellipses");
	}
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
	// Lower-cased because the corpus tree is deployed lower-case and this stem is compared as a
	// string by the lip/scene caches as well as resolved as a path.
	return FString::Printf(TEXT("character/dlg/%s/%s/line%d_col_%c"),
		*Dir, *Stem, LineId, TakeLetter).Replace(TEXT("//"), TEXT("/")).ToLower();
}

FElysiumVoiceHandle FElysiumLineService::PlayDialogueTurn(const FElysiumEntityHandle& Owner,
	const FString& DlgSourcePath, int32 LineId, const FVector& Origin, USceneComponent* AttachTo,
	TCHAR TakeLetter, float SpeechVolume)
{
	// The extension is deliberately absent: UElysiumAudioSubsystem::ResolveSourcePath owns the
	// authored mp3-then-wav probe (`LookupSpeechFile` `0x100e1880`). Retail probes a third extension
	// after those two; the string behind `DAT_10562364` is unread in the corpus, so nothing here
	// probes it — TODO(dialogue-plan): recover the third extension and add it to the resolver.
	return PlayDirect(DialogueSessionId(Owner), DialogueLineSource(DlgSourcePath, LineId, TakeLetter),
		Origin, AttachTo, EElysiumAudioCategory::Dialogue, /*StartOffsetSeconds=*/0.f, SpeechVolume);
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
