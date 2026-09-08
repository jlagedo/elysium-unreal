#pragma once

#include "CoreMinimal.h"
#include "ElysiumUserCmd.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

#include "ElysiumInputRouter.generated.h"

class APlayerController;
class UInputComponent;
class UElysiumInputActionSet;
struct FInputActionValue;
struct FElysiumInputState;

// The one thing that turns keys into verbs and verbs into intent.
// It sits on `AElysiumPlayerController` and does three jobs,
// in this order, every frame:
//
//   1. **binds** — every key in the default bind table fires its VtMB console line through
//      `ElysiumCommandBus`, so a key bound to a compiled verb and a key bound to a user alias are
//      indistinguishable, which is what the patch's whole vocabulary requires;
//   2. **latches** — a `+cmd` / `-cmd` pair lands in the user-command builder, because a button is
//      the verb's own property (`FElysiumCommands` writes it, no handler involved);
//   3. **publishes** — one `FElysiumUserCmd` per frame: the look delta is applied to the control
//      rotation and the whole command handed to the body. **Nothing polls a key**, so headless play
//      and deterministic replay are the same mechanism as playing.
//
// Enhanced Input mapping contexts, the remapping screen and the `config.cfg` projection sit in
// front of this layer, not in place of it — the command bus and the user command stay exactly as
// they are.
UCLASS()
class UElysiumInputRouter : public UObject
{
	GENERATED_BODY()

public:
	// Install the default binds onto the controller's input component and become the registry's
	// user-command sink.
	void Setup(APlayerController* Controller, UInputComponent* Input);
	void Shutdown();

	// Build the frame's command and hand it to the body. Called from
	// `AElysiumPlayerController::ProcessPlayerInput`, after the bindings for the frame have run
	// (step 1). The look delta is published on the command; the controller feeds it to the engine's
	// rotation input, and `UpdateRotation` is what integrates it.
	void SampleFrame(float DeltaSeconds);

	const FElysiumUserCmd& CurrentCmd() const { return Current; }
	FElysiumUserCmdBuilder& Builder() { return CmdBuilder; }

	// Press a `+`/`-` button verb and release it after the frame that samples it — the key-up a typed
	// console line has no way to produce. **This is the debug surface, not a verb**: `+attack` through
	// the bus still latches until `-attack`, which is what retail does, and this is how a QA driver
	// asks for the click a key would have made. Returns false — and says so — when the line does not
	// name a declared button pair.
	//
	// The release is deferred rather than issued immediately because the two edges would otherwise
	// cancel inside the builder and no frame would ever carry the bit.
	bool TapCommand(const FString& Line);

	// Apply a resolved scope to the command seam. Every transition clears held input; a scope without
	// player contexts also replaces the body's retained command immediately, because UI-only mode may
	// stop controller sampling before another frame can publish a neutral command.
	void ApplyScopeState(const FElysiumInputState& State);

	// Record / replay.
	// The acceptance for S5: a recorded stream replays identically. Recording captures the command
	// the router built; replay feeds recorded commands in instead of building them, so movement,
	// the camera and the bus cannot tell the difference.
	void StartRecording();
	void StopRecording();
	bool IsRecording() const { return bRecording; }
	const FElysiumUserCmdStream& Recorded() const { return Record; }

	// Replay the given stream from its start. Ends by itself when the stream runs out.
	void StartReplay(const FElysiumUserCmdStream& Stream);
	void StopReplay();
	bool IsReplaying() const { return bReplaying; }
	// What replay actually produced, for the identity check against the recording.
	const FElysiumUserCmdStream& Replayed() const { return ReplayLog; }

private:
	// Bind one console line to one key edge.
	void BindLine(UInputComponent* Input, const struct FInputChord& Chord, enum EInputEvent Event,
		const FString& Line, bool bEngineCommand);
	// Bind one row of the default table. A leading `+` binds the release to the matching `-cmd`.
	void BindDefault(UInputComponent* Input, const struct FElysiumDefaultBind& Bind);
	// The dev chords the reserved-key rule keeps off bare keys.
	void BindDebugChords(UInputComponent* Input);
	void BindEnhancedActions(UInputComponent* Input);

	void FireCommand(FString Line);
	void FireEngineCommand(FString Line);
	void OnMouseLook(const FInputActionValue& Value);
	void OnAnalogMove(const FInputActionValue& Value);
	void OnAnalogLook(const FInputActionValue& Value);
	void OnCommandDown(FName Command);
	void OnCommandUp(FName Command);

	// Degrees per mouse count: `sensitivity` x `m_yaw` / `m_pitch`, read off the VtMB console store
	// so the options slider and the cvar are one settings truth.
	float MouseYawScale() const;
	float MousePitchScale() const;

	// Re-read the mouse scale and the response curve from the console store, and hand them to the
	// builder. Once per frame, at the head of `SampleFrame`.
	void RefreshLookTuning();
	void PublishCurrentToBody();
	// Fire the `-cmd` half of every tap whose press has now been sampled into `Current`.
	void ReleaseTaps();

	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<UInputComponent> BoundInput;
	UPROPERTY(Transient)
	TObjectPtr<UElysiumInputActionSet> EnhancedActions;

	FElysiumUserCmdBuilder CmdBuilder;
	FElysiumUserCmd Current;
	ElysiumInput::FElysiumLookTuning LookTuning;
	bool bWarnedLookCurve = false;

	// The `-cmd` lines owed by taps pressed since the last sample. More than one can stand at once —
	// a driver taps attack and reload in the same statement — and each is released exactly once.
	TArray<FString> PendingTapReleases;

	bool bRecording = false;
	bool bReplaying = false;
	FElysiumUserCmdStream Record;
	FElysiumUserCmdStream Replay;
	FElysiumUserCmdStream ReplayLog;
};
