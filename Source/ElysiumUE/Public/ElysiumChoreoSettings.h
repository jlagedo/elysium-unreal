#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumChoreoSettings.generated.h"

/**
 * Choreography-track taste knobs, Project Settings -> Elysium -> Choreo, tracked at
 * `Config/DefaultElysium.ini`.
 * Every value here used to be a hardcoded `TAutoConsoleVariable` default with no editor home;
 * moving them here changes where the numbers live, not what they are.
 *
 * Read directly off the CDO at evaluation time (`GetDefault<UElysiumChoreoSettings>()`), the same
 * way `UElysiumModelSettings`'s LOD knobs are bake-time inputs rather than a live-pushed scalar:
 * a jaw/camera evaluation reads the current value every tick, so there is nothing to push.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Choreo"))
class ELYSIUMUE_API UElysiumChoreoSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumChoreoSettings();

	/**
	 * The jaw's resting level while a line is being spoken and no `silence`/`loud` marker claims the
	 * instant. Measured, not chosen: decoding each line's audio and taking the RMS envelope
	 * normalised to the line's own 99th percentile, over 110 lines of the dialogue corpus and 35 of
	 * the courtroom cast's own, a `silence` span averages 0.037, a `loud` span 0.640, and the
	 * uncovered time between them 0.312. Pinning `loud` at a fully open jaw puts the between level at
	 * 0.312 / 0.640 = 0.49 (`ElysiumChoreoScene.cpp`).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Jaw", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JawSpeechLevel = 0.49f;

	/**
	 * Time constant (seconds) the jaw lags its target by; 0 steps at the span boundary. Measured:
	 * across 187 lines the wav's own envelope takes a median 100 ms to rise 10->90% into a `loud`
	 * span and 150 ms to fall 90->10% into a `silence` span; a first-order lag covers 10->90% in
	 * ln(9) = 2.2 time constants (tau = 45 ms / 68 ms), and one constant of 50 ms sits between them.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Jaw", meta = (ClampMin = "0.0"))
	float JawSmoothing = 0.05f;

	/**
	 * `camera_keyframe`: a TimeControl segment at or below this many seconds is folded to a hard cut
	 * at spawn, the way `CCameraKeyFrame::Activate` does (retail's threshold is 0.05). 0 disables the
	 * fold and leaves short segments as camera movement (`ElysiumCameraTrack.cpp`).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Camera", meta = (ClampMin = "0.0"))
	float CameraCutSeconds = 0.05f;
};
