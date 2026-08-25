#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumUserCmd.h"

#if !UE_BUILD_SHIPPING

class UElysiumMapSubsystem;

// Headless composed-pose harness (`-ElysiumCompose`), beside `-ElysiumProfile`, `-ElysiumShots`,
// `-ElysiumProbe`, `-ElysiumMove` and `-ElysiumCast`.
//
// **What it exists to record is the pose the animation graph actually produced**, on a real map,
// from real input, with nothing driving it but the game. `Elysium.Content.RigCompose` scores the
// baked assets — it evaluates a sequence directly and never stands a graph up — and
// `Elysium.Content.RigPose` scores one clip on a frame nothing layered onto. Neither can see a
// defect that lives in the composition: a slot published at the wrong weight, a bone mask that
// resolved against the wrong skeleton, a chain whose branches compose in the wrong order. That
// half only exists in a built world, which is what this run stands up and writes down.
//
// It replays a fixed `FElysiumUserCmdStream` through the **real input router**, so the weapon, the
// activity commit and the overlay producers are all reached the way a player reaches them. Per
// frame it writes the player body's own component-space bone transforms — the anim graph's output
// after every layer has composed — beside the four overlay slot rows and the selection record that
// produced them.
//
// The output is written in the **same schema as `pose_oracle.json`**, deliberately: one comparator
// then scores a retail capture and one of these runs the same way, and the question "does our
// composition reach the pose retail drew" is asked of one file pair rather than of two formats.
// Distances are in centimetres and Unreal-native; the comparison is isometry-invariant, which is
// what lets the two sit in one schema without either being converted.
//
// One map, one weapon, one body, one launch — the same shape `-ElysiumProbe` takes, for the same
// reason: the run seats a body and drives it, and a second scenario in the same process would
// inherit the first one's motion. `-ComposeBody=<stem>` is what makes the body a statement of the
// harness rather than an inheritance from whatever record the map loaded, and it is why the report
// is named per body as well as per weapon: two bodies through one map and one weapon are two runs,
// and a shared filename would leave the second silently standing for both.
class FElysiumComposeRun
{
public:
	static bool IsRequested();

	explicit FElysiumComposeRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumComposeRun();

private:
	bool Tick(float DeltaSeconds);

	// Give the body its weapon and arm the command stream. False when the world is not ready to be
	// driven yet, which the tick retries.
	bool Begin();
	// One recorded frame: the drawn pose, the stack, the record.
	void Sample();
	// Write the run and ask for exit.
	void Finish();

	// The scripted intent, built once. Frames of: settle, strafe with the trigger held (which is
	// what puts an attack layer over a moving gait), a forced reload, then a strafe the other way.
	void BuildStream();

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	FElysiumUserCmdStream Stream;
	// Every sampled frame, as the JSON object the report carries. Held rather than streamed because
	// the schema is one array and a run is bounded by its own stream.
	TArray<TSharedPtr<class FJsonValue>> Frames;

	FString WeaponClass;
	// The body the run stands, before it is stood. `Stem` below is what the driver PUBLISHED once it
	// was standing, and the two are compared: an override that did not take records the wrong body
	// under the right name, which is the one failure a pose report cannot show on its own.
	FString BodyStem;
	// The item the run granted. Held because `Begin` is retried while the world settles, and a grant
	// per attempt would stack weapons in the inventory.
	FElysiumEntityHandle GrantedWeapon;
	FString Stem;
	int32 Hz = 60;
	float StepSeconds = 1.0f / 60.0f;
	int32 SettleRemaining = 0;
	int32 FrameIndex = 0;
	bool bStarted = false;
	bool bDone = false;
	// How many frames wanted a pose and could not read one. A run that records nothing has to say
	// why rather than writing an empty report that reads as "the body posed nothing".
	int32 NoPose = 0;
	int32 NoBody = 0;
	// Where the body stood when the stream was armed, and the farthest it ever got from there. A
	// strafe that never happened is the one failure this harness cannot see in its own pose data —
	// every frame still carries a gait selection and a full skeleton — so the distance is recorded
	// per frame and the run refuses to end quietly on a body that stayed put.
	FVector StartLocation = FVector::ZeroVector;
	double Farthest = 0.0;
	// Consecutive frames the resolved input scope has allowed gameplay commands. A replay armed
	// under a screen that holds input is a replay every frame of which is gated to nothing.
	int32 UngatedFrames = 0;
	bool bGymBuilt = false;
	// The body is built once, before the weapon is granted, so the wield lands on the body under
	// test rather than on one a later rebuild throws away.
	bool bBodyBuilt = false;
	// A body that would not build is terminal rather than retried: every remaining attempt would
	// stand the map's own body and record it under the requested name.
	bool bBodyFailed = false;
};

#endif // !UE_BUILD_SHIPPING
