#pragma once

#include "CoreMinimal.h"
#include "ElysiumUserCmd.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"

#include "ElysiumInputRouter.generated.h"

class APlayerController;
class UInputComponent;

// The one thing that turns keys into verbs and verbs into intent (roadmap 11.6,
// `runtime-architecture.md` §8.2–8.3). It sits on `AElysiumPlayerController` and does three jobs,
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
// Enhanced Input mapping contexts, the remapping screen and the `config.cfg` projection are
// **10.6**'s; this is the layer they replace the *front* of, not the whole path — the command bus
// and the user command stay exactly as they are.
UCLASS()
class UElysiumInputRouter : public UObject
{
	GENERATED_BODY()

public:
	// Install the default binds onto the controller's input component and become the registry's
	// user-command sink.
	void Setup(APlayerController* Controller, UInputComponent* Input);
	void Shutdown();

	// Build the frame's command, apply its look delta, and hand it to the body. Called from
	// `AElysiumPlayerController::PlayerTick`, after the bindings for the frame have run (step 1).
	void SampleFrame(float DeltaSeconds);

	const FElysiumUserCmd& CurrentCmd() const { return Current; }
	FElysiumUserCmdBuilder& Builder() { return CmdBuilder; }

	// Drop every held button without producing a command — what an input-scope change means for
	// intent (a screen opening must not leave the player walking).
	void ClearHeldButtons();

	// --- Record / replay -------------------------------------------------------------------
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
	// Mouse look, and the dev chords the reserved-key rule keeps off bare keys.
	void BindLookAxes(UInputComponent* Input);
	void BindDebugChords(UInputComponent* Input);

	void FireCommand(FString Line);
	void FireEngineCommand(FString Line);
	void OnMouseX(float Value);
	void OnMouseY(float Value);
	void ApplyLook(const FElysiumUserCmd& Cmd);

	// Degrees per mouse count: `sensitivity` x `m_yaw` / `m_pitch`, read off the VtMB console store
	// so the options slider and the cvar are one settings truth (`input-architecture.md`).
	float MouseYawScale() const;
	float MousePitchScale() const;

	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<UInputComponent> BoundInput;

	FElysiumUserCmdBuilder CmdBuilder;
	FElysiumUserCmd Current;

	bool bRecording = false;
	bool bReplaying = false;
	FElysiumUserCmdStream Record;
	FElysiumUserCmdStream Replay;
	FElysiumUserCmdStream ReplayLog;
};
