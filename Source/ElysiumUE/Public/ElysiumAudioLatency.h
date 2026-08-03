#pragma once

#include "CoreMinimal.h"

#include <atomic>

// 12.2b — where the lead a choreographed scene schedules its speech with comes from.
//
// VtMB hands its scenes `snd_mixahead`, 0.1 s by default. That constant is Source's *mixer's*
// lead: the behaviour it buys is "the sample is heard at the authored instant", and the number is
// a property of Source's output path, not of VtMB. Running Unreal's mixer, the same intent
// resolves to a different number — one that varies with the device, the sample rate and the buffer
// configuration — so it is composed here out of what the running device reports plus what the
// render path is measured doing, rather than inherited.

// One playing voice's view of the mixer's render head.
//
// FElysiumPcmGenerator writes this from the audio render thread; the game thread only reads. Each
// field is stamped independently, so a read is a snapshot of each rather than a consistent tuple —
// which is all a latency reading needs, and the alternative would be a lock on the render callback.
struct FElysiumVoiceRenderProbe
{
	// Frames this generator has handed the mixer. The render head, not what is audible.
	std::atomic<int64>  FramesRendered{ 0 };
	// The media frame the generator opened at (a mid-line seek starts above zero).
	std::atomic<int64>  StartFrame{ 0 };
	std::atomic<int32>  SampleRate{ 0 };
	// FPlatformTime::Seconds() stamped on the game thread at Submit, before the decode is queued.
	std::atomic<double> SubmitSeconds{ -1.0 };
	// FPlatformTime::Seconds() stamped on the render thread at the first and latest pull.
	std::atomic<double> FirstRenderSeconds{ -1.0 };
	std::atomic<double> LastRenderSeconds{ -1.0 };

	bool HasRendered() const
	{
		return FirstRenderSeconds.load(std::memory_order_relaxed) >= 0.0;
	}

	// True media position of the mixer's render head, in seconds into the file. This is the read a
	// scene clock is compared against: `Voice.Event.ScheduledAudioClock` is stamped at submit, ahead
	// of the async decode, so it says when the line was *asked for* and not where it actually is.
	double RenderHeadSeconds() const
	{
		const int32 Rate = SampleRate.load(std::memory_order_relaxed);
		if (Rate <= 0)
		{
			return 0.0;
		}
		const int64 Frames = StartFrame.load(std::memory_order_relaxed)
			+ FramesRendered.load(std::memory_order_relaxed);
		return static_cast<double>(Frames) / static_cast<double>(Rate);
	}

	// Submit → the mixer's first pull: the file read, the mp3 decode, the game-thread realization
	// and the source being picked up by the render thread. Negative until that first pull, which is
	// the term `ScheduledAudioClock` alone cannot see.
	double SubmitToRenderSeconds() const
	{
		const double Submit = SubmitSeconds.load(std::memory_order_relaxed);
		const double First = FirstRenderSeconds.load(std::memory_order_relaxed);
		return (Submit >= 0.0 && First >= 0.0) ? First - Submit : -1.0;
	}
};

using FElysiumVoiceRenderProbePtr = TSharedPtr<FElysiumVoiceRenderProbe, ESPMode::ThreadSafe>;

// The output path's latency, broken into the terms it is made of and labelled by how each was
// obtained. Nothing here is a single opaque figure: a caller that wants only the lead reads
// Lead(), and a reader that wants to know *why* reads the terms.
struct FElysiumAudioLatency
{
	// --- read off the running device --------------------------------------------------------
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

	// --- the terms, in seconds --------------------------------------------------------------
	// Queried: the mixer's own queue between rendering a buffer and submitting it to the endpoint.
	float MixerQueueSeconds = 0.f;
	// Modelled from the two queried frame counts — the steady-state fill of the endpoint buffer.
	// It is not a measurement: no engine interface reports the live endpoint padding.
	float EndpointSeconds = 0.f;
	// Measured on the live render path — the mean and worst submit → first-pull, over DIALOGUE
	// voices only. The lead exists to place speech, and a map-load music stream says nothing about
	// a line: on sp_theatre the ambience and score decode in 50–890 ms while a spoken line takes
	// 37–62 ms, so a mean over both would lead every line by a number no line ever costs. Zero
	// until one line has played, which is why a session's first line is led by the device alone.
	float SubmitToRenderSeconds = 0.f;
	float SubmitToRenderPeakSeconds = 0.f;
	int32 SubmitToRenderSamples = 0;
	// The part of the above the decoder itself timed, mean over the same voices. Reported rather
	// than led by separately: it is content-dependent, not a property of the path, and it is why
	// the lead has a spread at all. Driving it toward zero is a prefetch, not a constant.
	float DecodeSeconds = 0.f;

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
