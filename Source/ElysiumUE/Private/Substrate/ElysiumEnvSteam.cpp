// env_steam -- Valve's CSteamJet (`docs/vtmb/effects.md` §3.1):
// the clinic's leftover jet. `lifetime = JetLength / Speed`, the square spread, the raw-seconds
// size ramp, the hardcoded +-8 deg/s roll and the `sin(pi)` alpha law are the actor's on
// `NS_ElysiumSteam`; the leaf owns the keys, `InitialState` and TurnOn / TurnOff / Toggle, and
// publishes through `IElysiumWeather::ApplySteam`.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

class FElysiumEnvSteam final : public FElysiumEntity
{
public:
	int32 Type = 0;            // 0 normal, 1 heatwave (0 on all 11 rows)
	bool bInitialState = true;
	float SpreadSpeed = 15.f;  // Source inches / s
	float Speed = 120.f;
	float StartSize = 10.f;    // inches
	float EndSize = 25.f;
	float Rate = 26.f;         // particles / s
	float JetLength = 80.f;    // inches
	FString RenderColor = TEXT("255 255 255");
	float RenderAmt = 255.f;

	virtual void Spawn() override
	{
		bOn = bInitialState;
		Publish();
	}

	void TurnOn()  { bOn = true;  Publish(); }
	void TurnOff() { bOn = false; Publish(); }
	void Toggle()  { bOn = !bOn;  Publish(); }

	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		Publish();
	}

	virtual void Serialize(FElysiumSaveArchive& Ar) override
	{
		Ar << bOn;
		if (Ar.IsLoading())
		{
			Publish();
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Steam"), bOn ? TEXT("on") : TEXT("off"));
		Out.Emplace(TEXT("Jet"), FString::Printf(TEXT("%.0f in at %.0f in/s = %.2f s"),
			JetLength, Speed, Speed > 0.f ? JetLength / Speed : 0.f));
	}

private:
	void Publish() const
	{
		IElysiumWeather* Service = World ? World->Weather() : nullptr;
		if (!Service)
		{
			return;
		}
		FElysiumSteamState State;
		State.Entity = Handle;
		State.bActive = bOn && !IsInert();
		State.Type = Type;
		State.SpreadSpeedCm = SpreadSpeed * 2.54f;
		State.SpeedCm = Speed * 2.54f;
		State.StartSizeCm = StartSize * 2.54f;
		State.EndSizeCm = EndSize * 2.54f;
		State.Rate = FMath::Max(0.f, Rate);
		State.JetLengthCm = JetLength * 2.54f;
		State.Lifetime = Speed > 0.f ? JetLength / Speed : 0.f;
		TArray<FString> Parts;
		RenderColor.ParseIntoArrayWS(Parts);
		State.Color = FLinearColor(1.f, 1.f, 1.f, FMath::Clamp(RenderAmt / 255.f, 0.f, 1.f));
		if (Parts.Num() >= 3)
		{
			State.Color.R = FCString::Atof(*Parts[0]) / 255.f;
			State.Color.G = FCString::Atof(*Parts[1]) / 255.f;
			State.Color.B = FCString::Atof(*Parts[2]) / 255.f;
		}
		Service->ApplySteam(State);
	}

	bool bOn = true;
};

static TUniquePtr<FElysiumEntity> MakeEnvSteam() { return MakeUnique<FElysiumEnvSteam>(); }

static FElysiumClassRegistrar GRegEnvSteam(
	TEXT("env_steam"), ElysiumBaseClassName(), &MakeEnvSteam,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSteam&>(E).TurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSteam&>(E).TurnOff(); });
		D.Input(TEXT("Toggle"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvSteam&>(E).Toggle(); });
		ElysiumAddClassField(D, TEXT("type"), &FElysiumEnvSteam::Type, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("InitialState"), &FElysiumEnvSteam::bInitialState, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("SpreadSpeed"), &FElysiumEnvSteam::SpreadSpeed, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("Speed"), &FElysiumEnvSteam::Speed, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("StartSize"), &FElysiumEnvSteam::StartSize, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("EndSize"), &FElysiumEnvSteam::EndSize, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("Rate"), &FElysiumEnvSteam::Rate, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("JetLength"), &FElysiumEnvSteam::JetLength, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("rendercolor"), &FElysiumEnvSteam::RenderColor, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("renderamt"), &FElysiumEnvSteam::RenderAmt, EElysiumField::None);
	});
