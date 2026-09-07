#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumEntityHandle.h"

class IElysiumAudio;
class USceneComponent;
struct FElysiumDlgLine;

// Map-owned story/audio join. It knows dialogue and scene ownership, while the GI subsystem alone
// resolves media, decodes it, schedules it and owns the rendering component.
class FElysiumLineService
{
public:
	explicit FElysiumLineService(IElysiumAudio* InAudio) : Audio(InAudio) {}

	FElysiumVoiceHandle PlayDirect(const FString& SessionId, const FString& AuthoredPath,
		const FVector& Origin, USceneComponent* AttachTo = nullptr,
		EElysiumAudioCategory Category = EElysiumAudioCategory::Auto,
		float StartOffsetSeconds = 0.f, float Gain = 1.f);

	FElysiumVoiceHandle PlayDialogueTurn(const FElysiumEntityHandle& Owner,
		const FString& DlgSourcePath, int32 LineId, const FVector& Origin,
		USceneComponent* AttachTo = nullptr, TCHAR TakeLetter = DefaultTake,
		float SpeechVolume = 1.f);

	void CancelSession(const FString& SessionId, float FadeSeconds = 0.f);
	void CancelDialogue(const FElysiumEntityHandle& Owner, float FadeSeconds = 0.f);
	void Shutdown();

	// The `e` column: the male/default text take, and the letter every caller that does not know the
	// conversation's (sex, clan) pair must ask for.
	static constexpr TCHAR DefaultTake = TEXT('e');
	// Retail's letterless sentinel. `generate_speech_filename` (`0x100e1680`) counts the letter bytes
	// of the row's raw col-1 through `FUN_100df0b0` and, when there are none, returns
	// `sound/character/dlg/ellipses.<ext>` instead of a per-line stem. Passing this letter asks for
	// that path. NUL is not a column, so it cannot collide with a real take.
	static constexpr TCHAR EllipsesTake = TCHAR(0);

	// `sound/character/<dlgpath>/line<id>_col_<C>` with the `sound/` prefix stripped (the audio
	// subsystem's own normalisation owns that prefix and the extension probe). The SAME stem feeds
	// the voice, the line's `.vcd` and its `.lip`, so a female take moves the female mouth.
	static FString DialogueLineSource(const FString& DlgSourcePath, int32 LineId,
		TCHAR TakeLetter = DefaultTake);

	// `FUN_100df0b0` — true when the raw text carries no A-Z, a-z or >= 0xC0 byte at all, which is
	// what makes a line "letterless" (a lone "...", a bare ellipsis stage cue).
	static bool IsLetterlessText(const FString& RawText);

	// The take letter this line resolves to for this player: `EllipsesTake` when col-1 is letterless,
	// otherwise `ElysiumDlgText::ChosenTakeLetter` (`0x100e15c0`). Retail tests col-1 specifically,
	// before the column chooser runs, so a female/clan variant of a letterless row is still ellipses.
	static TCHAR TakeLetterFor(const FElysiumDlgLine& Line, bool bMale, int32 ClanOffset);

	// TODO(dialogue-plan): `m_flSpeechVol` (datadesc field name at `105d6284`) is the CHAN_STREAM
	// volume retail plays a non-VCD dialogue line at. No exported map authors a `speechvol`-like
	// keyvalue (grep over `exports/*/*.ents`, `npc/`, `vdata/`, 2026-09-06) and no reader of the
	// field is recovered, so the seam answers the unattenuated 1.0 for every NPC. Wire it to the
	// NPC keyvalue store the moment a source for it exists.
	static float SpeechVolumeFor(const FElysiumEntityHandle& Speaker);

private:
	IElysiumAudio* Audio = nullptr;
	TMap<FString, FElysiumVoiceHandle> Active;
};
