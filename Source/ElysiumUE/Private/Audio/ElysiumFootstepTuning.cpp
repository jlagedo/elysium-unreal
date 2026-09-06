#include "ElysiumFootstepTuning.h"

DEFINE_LOG_CATEGORY(LogElysiumFootsteps);

namespace ElysiumFootstep
{

// The declaration, in the order `0x1026d1b0..0x1026d3f0` constructs them plus the two the player's
// own clock reads. Defaults are the strings the DLL pushes, typed exactly as it types them.
static const FCvarDef GFootstepCvars[] =
{
	{ TEXT("footstep_normal_vol"),        TEXT("0.5"),  TEXT("NPC normal-footfall volume when templates are off.") },
	{ TEXT("footstep_normal_dist"),       TEXT("256"),  TEXT("NPC normal-footfall audible distance, units.") },
	{ TEXT("footstep_heavy_vol"),         TEXT("0.85"), TEXT("NPC heavy-footfall volume when templates are off.") },
	{ TEXT("footstep_heavy_dist"),        TEXT("512"),  TEXT("NPC heavy-footfall audible distance, units.") },
	{ TEXT("footstep_npc_use_templates"), TEXT("1"),    TEXT("Read an NPC's footfall volume/distance off its character template.") },
	{ TEXT("footstep_pc_vol"),            TEXT("0.5"),  TEXT("Player footstep volume multiplier.") },
	{ TEXT("sv_footsteps"),               TEXT("1"),    TEXT("Play the player's footsteps at all.") },
};

TArrayView<const FCvarDef> CvarDefs()
{
	return MakeArrayView(GFootstepCvars);
}

} // namespace ElysiumFootstep

void FElysiumFootstepTuning::LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	auto Read = [this, &Lookup](const TCHAR* Name, float& Out)
	{
		const FString Value = Lookup(Name);
		if (Value.IsEmpty())
		{
			return;
		}
		float Parsed = 0.0f;
		if (!LexTryParseString(Parsed, *Value) || !FMath::IsFinite(Parsed))
		{
			const FName Key(Name);
			if (!ReportedMalformed.Contains(Key))
			{
				ReportedMalformed.Add(Key);
				UE_LOG(LogElysiumFootsteps, Warning,
					TEXT("[elysium] %s is set to '%s', which is not a finite number; keeping %g"),
					Name, *Value, Out);
			}
			return;
		}
		ReportedMalformed.Remove(FName(Name));
		Out = Parsed;
	};

	// The two flags read as the console reads every boolean cvar: any non-zero number is on. They
	// go through the same malformed-value report, so `sv_footsteps yes` keeps the default and says
	// so rather than silently muting every step for the session.
	auto ReadFlag = [&Read](const TCHAR* Name, bool& Out)
	{
		float Numeric = Out ? 1.0f : 0.0f;
		Read(Name, Numeric);
		Out = Numeric != 0.0f;
	};

	Read(TEXT("footstep_normal_vol"),  NormalVolume);
	Read(TEXT("footstep_normal_dist"), NormalDistanceUnits);
	Read(TEXT("footstep_heavy_vol"),   HeavyVolume);
	Read(TEXT("footstep_heavy_dist"),  HeavyDistanceUnits);
	Read(TEXT("footstep_pc_vol"),      PlayerVolume);
	ReadFlag(TEXT("footstep_npc_use_templates"), bNpcUseTemplates);
	ReadFlag(TEXT("sv_footsteps"),               bServerFootsteps);
}
