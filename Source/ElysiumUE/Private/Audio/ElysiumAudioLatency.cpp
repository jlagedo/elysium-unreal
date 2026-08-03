#include "ElysiumAudioLatency.h"

#include "AudioMixerDevice.h"
#include "Engine/World.h"

#if PLATFORM_WINDOWS
#include "Microsoft/COMPointer.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
#if PLATFORM_WINDOWS
	// The endpoint's period, in frames at the mixer's sample rate. WASAPI reports it as a
	// reference time (100 ns units), which is sample-rate agnostic; the backend converts it the
	// same way. Zero means the endpoint could not be asked — a device that failed to open, or a
	// thread with no COM apartment — and the caller falls back to a callback-sized endpoint.
	int32 QueryDevicePeriodFrames(int32 SampleRate)
	{
		if (SampleRate <= 0)
		{
			return 0;
		}
		TComPtr<IMMDeviceEnumerator> Enumerator;
		if (FAILED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
			__uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&Enumerator))) || !Enumerator)
		{
			return 0;   // no COM apartment on this thread, or no endpoint at all
		}
		TComPtr<IMMDevice> Device;
		if (FAILED(Enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &Device)) || !Device)
		{
			return 0;
		}
		TComPtr<IAudioClient> Client;
		if (FAILED(Device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
			reinterpret_cast<void**>(&Client))) || !Client)
		{
			return 0;
		}
		REFERENCE_TIME PeriodRefTime = 0;
		// The second parameter is exclusive-mode only; shared mode reports its period in the first.
		if (FAILED(Client->GetDevicePeriod(&PeriodRefTime, nullptr)) || PeriodRefTime <= 0)
		{
			return 0;
		}
		// A REFERENCE_TIME is 100 ns units, so frames = period_seconds * rate.
		const double PeriodSeconds = static_cast<double>(PeriodRefTime) * 1.0e-7;
		return FMath::Max(1, FMath::RoundToInt(PeriodSeconds * SampleRate));
	}
#endif

	// What the backend asks the OS to allocate: the mixer's callback size rounded up to a whole
	// number of device periods, because a non-integral buffer would phase against the period and
	// starve. This mirrors FAudioMixerWasapiRenderStream::InitializeHardware; with no period to
	// round against, the endpoint can only be assumed callback-sized.
	int32 EndpointFramesFor(int32 CallbackFrames, int32 DevicePeriodFrames)
	{
		if (CallbackFrames <= 0)
		{
			return 0;
		}
		if (DevicePeriodFrames <= 0)
		{
			return CallbackFrames;
		}
		if (CallbackFrames % DevicePeriodFrames == 0)
		{
			return CallbackFrames;
		}
		if (CallbackFrames < DevicePeriodFrames && DevicePeriodFrames % CallbackFrames == 0)
		{
			return DevicePeriodFrames;
		}
		const int32 Periods = FMath::DivideAndRoundUp(
			CallbackFrames + DevicePeriodFrames - 1, DevicePeriodFrames);
		return DevicePeriodFrames * Periods;
	}
}

FElysiumAudioLatency ElysiumAudioLatency::QueryDevice(const UWorld* World)
{
	FElysiumAudioLatency Out;

	const FAudioDevice* Base = World ? World->GetAudioDeviceRaw() : nullptr;
	const Audio::FMixerDevice* Mixer = static_cast<const Audio::FMixerDevice*>(Base);
	if (Mixer == nullptr || Mixer->GetSampleRate() <= 0.f)
	{
		// No device: everything is the fallback, and bDeviceQueried says so.
		Out.MixerQueueSeconds = FallbackLeadSeconds;
		return Out;
	}

	Out.bDeviceQueried = true;
	Out.SampleRate = FMath::RoundToInt(Mixer->GetSampleRate());
	Out.CallbackFrames = Mixer->GetNumOutputFrames();
	Out.OutputBuffers = FMath::Max(1, Mixer->GetNumOutputBuffers());
	Out.DeviceName = Mixer->GetPlatformDeviceInfo().Name;
	if (const Audio::IAudioMixerPlatformInterface* Platform = Mixer->GetAudioMixerPlatform())
	{
		Out.PlatformApi = Platform->GetPlatformApi();
	}

#if PLATFORM_WINDOWS
	Out.DevicePeriodFrames = QueryDevicePeriodFrames(Out.SampleRate);
#endif
	Out.EndpointFrames = EndpointFramesFor(Out.CallbackFrames, Out.DevicePeriodFrames);

	const float Rate = static_cast<float>(Out.SampleRate);
	Out.MixerQueueSeconds = Out.CallbackFrames > 0 ? (Out.OutputBuffers * Out.CallbackFrames) / Rate : 0.f;

	// The endpoint drains one device period per callback and the backend tops it up whenever a
	// whole mixer callback fits, so a sample written into it waits behind somewhere between
	// EndpointFrames - CallbackFrames and EndpointFrames of already-queued audio. The midpoint is
	// the steady-state expectation; the spread is one callback, and it is real jitter rather than
	// error, which is why a residual of zero would not be believable.
	const float EndpointFill = FMath::Max(0.f,
		Out.EndpointFrames - 0.5f * Out.CallbackFrames);
	Out.EndpointSeconds = EndpointFill / Rate;

	return Out;
}
