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
		ElysiumAddClassField(D, TEXT("active"), &FElysiumEnvParticle::bActive, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("particle_definition"), &FElysiumEnvParticle::ParticleDefinition, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("attach_type"), &FElysiumEnvParticle::AttachType, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("bone"), &FElysiumEnvParticle::AttachBone, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("attach_point"), &FElysiumEnvParticle::AttachPoint, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("spawnbounds"), &FElysiumEnvParticle::SpawnBounds, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("ramp_scale"), &FElysiumEnvParticle::RampScale, EElysiumField::MapKey);
		ElysiumAddClassField(D, TEXT("ramp_time"), &FElysiumEnvParticle::RampTime, EElysiumField::MapKey);
		// The `DT_EnvParticle` words the datamap leaves unnamed (`walks/L0-r011.md`), registered under
		// their SendTable names so a record reads them by retail name: `m_nParticle` (+0x454),
		// `m_flActivationTime` (+0x488), `m_nRampFrame` (+0x494). Not keyable (no map authors them).
		ElysiumAddClassField(D, TEXT("m_nParticle"), &FElysiumEnvParticle::ParticleIndex, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_flActivationTime"), &FElysiumEnvParticle::ActivationTime, EElysiumField::None);
		ElysiumAddClassField(D, TEXT("m_nRampFrame"), &FElysiumEnvParticle::RampFrame, EElysiumField::None);
		// `m_fRateScaleTarget` (+0x48c) is the `ramp_scale` key's word in retail; this port's `ramp_scale`
		// seeds its CURRENT rate and Spawn copies it to the target, so the target is readable here too.
		ElysiumAddClassField(D, TEXT("m_fRateScaleTarget"), &FElysiumEnvParticle::RampTargetScale, EElysiumField::None);
	});
