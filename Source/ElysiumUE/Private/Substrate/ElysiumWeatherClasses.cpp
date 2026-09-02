// env_particle's registration. The class body is `Substrate/ElysiumEnvParticle.h` so that
// `func_particle` (`ElysiumFuncParticle.cpp`) can derive from it.

#include "Substrate/ElysiumEnvParticle.h"

#include "ElysiumClassRegistry.h"
#include "Substrate/ElysiumClassFields.h"

static TUniquePtr<FElysiumEntity> MakeEnvParticle() { return MakeUnique<FElysiumEnvParticle>(); }

static FElysiumClassRegistrar GRegEnvParticle(
	TEXT("env_particle"), ElysiumBaseClassName(), &MakeEnvParticle,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("TurnOn"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvParticle&>(E).InputTurnOn(); });
		D.Input(TEXT("TurnOff"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvParticle&>(E).InputTurnOff(); });
		D.Input(TEXT("SetRateScale"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvParticle&>(E).InputSetRateScale(A.Param); });
		D.Input(TEXT("SetRampTime"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvParticle&>(E).InputSetRampTime(A.Param); });
		D.Input(TEXT("SetAttachType"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumEnvParticle&>(E).InputSetAttachType(A.Param); });
		D.Input(TEXT("JetLength"), [](FElysiumEntity& E, const FElysiumInputArgs&) { static_cast<FElysiumEnvParticle&>(E).InputJetLength(); });
		// None: spawn-time keyvalue application ignores bKeyable, and these fields are neither
		// runtime-writable nor save-enumerated.
		ElysiumAddClassField(D, TEXT("active"), &FElysiumEnvParticle::bActive, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("particle_definition"), &FElysiumEnvParticle::ParticleDefinition, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("attach_type"), &FElysiumEnvParticle::AttachType, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("bone"), &FElysiumEnvParticle::AttachBone, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("attach_point"), &FElysiumEnvParticle::AttachPoint, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("spawnbounds"), &FElysiumEnvParticle::SpawnBounds, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("ramp_scale"), &FElysiumEnvParticle::RampScale, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("ramp_time"), &FElysiumEnvParticle::RampTime, EElysiumField::None);
	});
