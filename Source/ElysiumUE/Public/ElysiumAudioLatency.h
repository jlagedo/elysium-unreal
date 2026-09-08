#pragma once

#include "CoreMinimal.h"

// Where the lead a choreographed scene schedules its speech with comes from.
//
// VtMB hands its scenes `snd_mixahead`, 0.1 s by default. That constant is Source's *mixer's*
// lead: the behaviour it buys is "the sample is heard at the authored instant", and the number is
// a property of Source's output path, not of VtMB. Running Unreal's mixer, the same intent
// resolves to a different number — one that varies with the device, the sample rate and the buffer
// configuration — so it is composed here out of what the running device reports plus the one term
// no interface reports (submit → the mixer's first pull), which the owner stamps on
// `UElysiumAudioSettings` from a live reading of `elysium.audio_latency`. Inherited from nothing.

// The output path's latency, broken into the terms it is made of and labelled by how each was
// obtained. Nothing here is a single opaque figure: a caller that wants only the lead reads
// Lead(), and a reader that wants to know *why* reads the terms.
struct FElysiumAudioLatency
{
	// Read off the running device.
	FString PlatformApi;
	FString DeviceName;
	int32 SampleRate = 0;
	// The mixer's render quantum (FMixerDevice::GetNumOutputFrames).
	int32 CallbackFrames = 0;
	// How many of those sit between the mix and the endpoint (FMixerDevice::GetNumOutputBuffers).
	int32 OutputBuffers = 0;
	// The endpoint's own period, asked of the render endpoint directly. 0 when the platform has no
	// way to answer, which is what leaves EndpointFrames at its callback-sized floor.
	int32 DevicePeriodFrames = 0;
	// The endpoint buffer the backend asks the OS for: the callback size rounded up to a whole
	// number of device periods.
	int32 EndpointFrames = 0;

	// The terms, in seconds.
	// Queried: the mixer's own queue between rendering a buffer and submitting it to the endpoint.
	float MixerQueueSeconds = 0.f;
	// Modelled from the two queried frame counts — the steady-state fill of the endpoint buffer.
	// It is not a measurement: no engine interface reports the live endpoint padding.
	float EndpointSeconds = 0.f;
	// Submit → the mixer's first pull: the async asset load, the game-thread realization and the
	// source being picked up by the render thread. AUD1.3 made this a stamped constant
	// (`UElysiumAudioSettings::SubmitToRenderSeconds`) rather than a live measurement: a baked
	// `USoundWave` has no generator of ours to write a render probe, and a primed asset feeds the
	// mixer out of Unreal's stream cache instead of paying a whole-file decode, so the term is a
	// property of the path that the owner measures once with `elysium.audio_latency` rather than
	// something that varies per line. Zero until it is stamped, which leads a cue by the device
	// terms alone.
	float SubmitToRenderSeconds = 0.f;

	// False when there is no audio device to ask — a -nullrhi test tier, a dedicated server, or a
	// device that failed to open. Every term below then carries the fallback instead.
	bool bDeviceQueried = false;

	// What a caller should lead a cue by so its first sample is heard at the authored instant.
	float Lead() const
	{
		return MixerQueueSeconds + EndpointSeconds + SubmitToRenderSeconds;
	}
};

namespace ElysiumAudioLatency
{
	// The lead used when there is no device to ask. Named as a fallback because that is what it
	// is: one 1024-frame mixer callback at 48 kHz, Unreal's own default quantum. Nothing is audible
	// on a path that takes it, so it only has to keep a timeline's shape stable.
	inline constexpr float FallbackLeadSeconds = 1024.f / 48000.f;

	// Fill in everything the running audio device can be asked for. Returns a latency with
	// bDeviceQueried false, and the fallback in its terms, when there is no device.
	FElysiumAudioLatency QueryDevice(const class UWorld* World);
}
