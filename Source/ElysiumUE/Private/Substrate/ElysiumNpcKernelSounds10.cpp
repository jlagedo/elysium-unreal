// Story 29d, family **Sounds10** — the middle band of the NPC's sound surface, layers 10–18.
//
// Twenty-eight recovered bodies:
//
//   * the **seventeen sound hooks** of slots 488–507 on the Troika line (`0x10293ec0` …
//     `0x10294f40`) — nineteen slots, of which 497 is another family's and 496/500 are reached only
//     through the vtable;
//   * the **three `KeyValue` overloads**, slots 108 (`0x1004fbb0`), 109 (`0x1004fbf0`) and 110
//     (`0x101c1480`), with the two `CBaseEntity` formatters they forward through (`0x1009eca0`,
//     `0x1009ebb0`);
//   * slot 185 **`FireBullets`** (`0x10268900`), the shared bullet pass;
//   * the **species arms** `CNPC_VWerewolf#500` (`0x103d8660`), `CNPC_VWerewolf#491`
//     (`0x103d87a0`) and `CNPC_Crow#511` (`0x10357800`).
//
// The walked prose is `docs/vtmb/npc-ai/senses.md` § "Story 29d, family Sounds10 — …".
//
// **What this family emits through.** Every hook here ends in the VSound play entry `0x101f5950`,
// which resolves a wav out of the per-entity VSound table and hands it to `IEngineSound::EmitSound`.
// This runtime parses no VSound table and no concept list — `PrecacheSoundTable` (slot 71) is still
// a generated stub and `m_iVSoundTableIdx` (`+0x00bc`) has no writer — so the concept lookup answers
// retail's own miss (`-1`) and the play entry takes retail's own out-of-bounds arm. Both are
// SEAMS declared in `ElysiumNpcKernelSounds10.inl` and both record what they were asked for, which
// is the recovered half: the concept, the channel, the volume and the fifth argument.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"

namespace
{
	// ---------------------------------------------------------------------------------------
	// The concept names, read out of the pinned `vampire.dll` at the addresses beside them.
	// `vtmb_string` does not hold these — they are `.rdata` cells the corpus named as symbols —
	// so they were read from the image itself, which is how `Pain` (`0x105d8c6c`) and `Flee`
	// (`0x105d8cd0`) stopped being "unnamed in the corpus".
	//
	// Every one carries an UNDERSCORE. The checklist's one-line walks spelled ten of them with a
	// space ("Target Suspect", "Idle Calm", …); the bytes do not.
	// ---------------------------------------------------------------------------------------
	const TCHAR* const GSounds10ConceptDeath = TEXT("Death");                       // 0x105d8c30
	const TCHAR* const GSounds10ConceptTargetSuspect = TEXT("Target_Suspect");      // 0x105d8c38
	const TCHAR* const GSounds10ConceptIdleCalm = TEXT("Idle_Calm");                // 0x105d8c60
	const TCHAR* const GSounds10ConceptPain = TEXT("Pain");                         // 0x105d8c6c
	const TCHAR* const GSounds10ConceptFearStart = TEXT("Fear_Start");              // 0x105d8c74
	const TCHAR* const GSounds10ConceptTargetLost = TEXT("Target_Lost");            // 0x105d8c84
	const TCHAR* const GSounds10ConceptTargetReacquired = TEXT("Target_Reacquired");// 0x105d8c94
	const TCHAR* const GSounds10ConceptSurprised = TEXT("Surprised");               // 0x105d8cac
	const TCHAR* const GSounds10ConceptTargetAcquired = TEXT("Target_Acquired");    // 0x105d8cb8
	const TCHAR* const GSounds10ConceptFlee = TEXT("Flee");                         // 0x105d8cd0
	const TCHAR* const GSounds10ConceptIdleAgitated = TEXT("Idle_Agitated");        // 0x105d8cd8
	const TCHAR* const GSounds10ConceptRiled = TEXT("Riled");                       // 0x105d8ce8
	const TCHAR* const GSounds10ConceptComfort = TEXT("Comfort");                   // 0x105d8cf0
	const TCHAR* const GSounds10ConceptUpset = TEXT("Upset");                       // 0x105d8cfc
	const TCHAR* const GSounds10ConceptTargetGiveUp = TEXT("Target_GiveUp");        // 0x105d8d04
	const TCHAR* const GSounds10ConceptFloat = TEXT("Float");                       // 0x105d8d14
	const TCHAR* const GSounds10ConceptExertHeavy = TEXT("Exert_Heavy");            // 0x1057a1a0
	const TCHAR* const GSounds10ConceptExertLight = TEXT("Exert_Light");            // 0x1057a1b0

	// Source's `CHAN_VOICE`. Every hook in this family passes `2` as `0x101f5950`'s third argument.
	constexpr int32 GSounds10ChanVoice = 2;
	// `0x3f800000`, the fourth argument on every hook in this family.
	constexpr float GSounds10Volume = 1.0f;
	// `0x3fa00000`, the fifth argument on sixteen of the seventeen Troika hooks.
	constexpr float GSounds10Attenuation = 1.25f;
	// Both `CNPC_VWerewolf` arms push a literal `0` where the Troika body pushes `0x3fa00000`.
	constexpr float GSounds10WerewolfAttenuation = 0.0f;

	// `CAI_BaseNPCTroika::FUN_102944c0` (slot 493) `1029450a`: `RandomInt(0, 99) < 0x19`.
	constexpr int32 GSounds10LostEnemyRollMax = 99;
	constexpr int32 GSounds10LostEnemyRollThreshold = 0x19;

	// `Float_Sound_Info` (`vdata/system/rules_tables.txt`), the table slot 507 re-arms from.
	// Row 1 is `FloatSoundMinDelay -- Minimum delay in seconds before next float sound`, 5.0.
	const TCHAR* const GSounds10FloatSoundTable = TEXT("Float_Sound_Info");
	constexpr int32 GSounds10FloatSoundMinDelayRow = 1;
	constexpr float GSounds10FloatSoundMinDelayFallback = 5.0f;

	// `CNPC_Crow::vfunc511` `10357800`: the one string it stops.
	const TCHAR* const GSounds10CrowFlapSound = TEXT("NPC_Crow.Flap");

	// --- `FireBullets`' recovered constants ------------------------------------------------

	// `10268900` @ `10268a3a`: the filter word is `ammoFlags | 0x1000`.
	constexpr int32 GSounds10FireBulletsFilterBit = 0x1000;
	// The ammo-def flag that SUPPRESSES spread for everyone but a player with a zeroed `+0x1e78`.
	constexpr int32 GSounds10AmmoFlagNoNpcSpread = 0x2000000;
	// `info+0xa0` bit 0, which latches the tracer arm on its own.
	constexpr int32 GSounds10TracerFlagAlways = 0x1;
	// The skill above which an NPC shooter latches the tracer arm (`2 < stat`).
	constexpr int32 GSounds10TracerSkillThreshold = 2;
	// `DAT_104994c8`, six floats read out of the pinned image: the divisor `info+0xa4` is divided
	// by, indexed by the ranged skill clamped to 0..5.
	constexpr float GSounds10SkillDivisor[6] = { 1.0f, 1.0f, 1.0f, 1.1f, 1.3f, 1.5f };
	// `_DAT_104454c0`, the numerator of the tracer's per-shot fraction AND the rejection sampler's
	// radius test. 1.0 in the image.
	constexpr float GSounds10One = 1.0f;
	// `0x10268170`'s four draws are `RandomFloat(0xbf000000, 0x3f000000)`.
	constexpr float GSounds10SpreadDrawMin = -0.5f;
	constexpr float GSounds10SpreadDrawMax = 0.5f;

	// NAMED DECISION: `FireBullets`' spread draws come off `EElysiumRngStream::Reaction`.
	// Retail draws from the one engine stream (`DAT_1070b244`); this runtime splits streams so two
	// systems cannot walk each other's position, and `Reaction` is the combat-side stream (the
	// damage flinch's coin and jitter). The alternative — `NpcSchedule` — would move the idle
	// branch's position by four draws per bullet. Nothing fires a weapon in this runtime yet, so
	// the choice costs nothing today and is recorded here rather than rediscovered.
	constexpr EElysiumRngStream GSounds10SpreadStream = EElysiumRngStream::Reaction;
	// Slot 493's roll is an NPC-think decision and draws beside the rest of the idle branch, which
	// is family **Sounds**' own reading for every other roll in the sound band.
	constexpr EElysiumRngStream GSounds10HookStream = EElysiumRngStream::NpcSchedule;

	// `gpGlobals->curtime`, and the engine `Time()` (`DAT_1070b22c` vtable `+0x1dc`) slot 507 reads.
	// One clock in this substrate.
	double Sounds10Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}
}

// =================================================================================================
// Slots 108 / 109 / 110 — `KeyValue`
// =================================================================================================

FString FElysiumNpc::FormatKeyValueVector(const FVector& Value)
{
	// `CAISound::FUN_1009eca0` (`0x1009eca0`), 212 bytes. Past the `CBaseEntity::KeyValue`
	// scope-trace push (`s_CBaseEntity__KeyValue_105555f4`, the entity's `m_iName`, or
	// `"NULL ENTITY"` when `this` is null and the empty string when unnamed — this runtime has no
	// scope-trace stack, and family Facing already recorded that the push is a crash-report
	// breadcrumb with no game-visible effect), the WHOLE body is
	// `Q_snprintf(buf, 256, "%f %f %f", (double)x, (double)y, (double)z)` with the literal at
	// `0x10555584`, then a dispatch of this object's OWN slot 110 (`vtable +0x1b8`) with the buffer.
	//
	// The three floats are widened to `double` by the varargs call, and `%f` is C's six-decimal
	// default; `FString::Printf` matches both. The 256-byte buffer is retail's stack frame and has
	// no observable effect for three finite floats.
	return FString::Printf(TEXT("%f %f %f"), static_cast<float>(Value.X),
		static_cast<float>(Value.Y), static_cast<float>(Value.Z));
}

FString FElysiumNpc::FormatKeyValueFloat(float Value)
{
	// `CAISound::FUN_1009ebb0` (`0x1009ebb0`), 190 bytes and the same shape with the format at
	// `0x10554f28`, which is `"%f"`.
	return FString::Printf(TEXT("%f"), Value);
}

// `CAI_BaseNPC::FUN_1004fbb0` (`0x1004fbb0`), slot 108, and CAI_BaseNPCTroika shares it. THIRTY-EIGHT
// bytes: the whole body is a tail call into `CAISound::FUN_1009eca0` — it does not format anything
// itself. The checklist's walk described the callee's body as this one's; the correction is that
// slot 108 on the NPC line is a pure forward and the formatting plus the slot-110 dispatch both
// belong to `0x1009eca0`.
//
// The forward is a `__thiscall` to a DIFFERENT class's body on the same object, so the slot 110 it
// reaches is THIS object's — `FElysiumNpc::KeyValue(const TCHAR*, const TCHAR*)` below.
bool FElysiumNpc::KeyValue(const TCHAR* Key, FVector Value)
{
	return KeyValue(Key, *FormatKeyValueVector(Value));
}

// `CAI_BaseNPC::FUN_1004fbf0` (`0x1004fbf0`), slot 109. THIRTEEN bytes — the same pure forward, into
// `CAISound::FUN_1009ebb0`.
//
// Both `0x1009eca0` and `0x1009ebb0` return `void` in retail and both of these slots are declared
// `bool`; the forwarded value is whatever slot 110 answered, which is what this returns.
bool FElysiumNpc::KeyValue(const TCHAR* Key, float Value)
{
	return KeyValue(Key, *FormatKeyValueFloat(Value));
}

// `CAI_BaseNPC::FUN_101c1480` (`0x101c1480`), slot 110 on the Troika line (77 classes). 114 bytes,
// three arms, read off the LISTING because the decompiled C loses which store is which:
//
//   1. `__strcmpi(key, "lip")` (`0x10561fc0`) → `FSTP [ESI + 0x504]` (`101c14a4`), `return true`.
//   2. `__strcmpi(key, "distance")` (`0x1053f4c4`) → `FSTP [ESI + 0x4fc]` (`101c14d0`),
//      `return true`.
//   3. anything else → `CBaseEntity::KeyValue` (`0x1009e430`), whose answer is returned verbatim.
//
// The value is `atof`'d (`0x1043136f`) in both writing arms, so a non-numeric value writes 0.0 and
// still answers true — retail's own behaviour, kept.
//
// `__strcmpi` is case-INSENSITIVE, which is why `Lip` and `DISTANCE` match.
bool FElysiumNpc::KeyValue(const TCHAR* Key, const TCHAR* Value)
{
	if (Key == nullptr)
	{
		// Retail would fault on a null key inside `__strcmpi`. CRASH GUARD: refuse instead, which
		// is the arm a key nothing matched takes anyway.
		return false;
	}
	if (FCString::Stricmp(Key, TEXT("lip")) == 0)
	{
		Lip = Value != nullptr ? FCString::Atof(Value) : 0.f;
		return true;
	}
	if (FCString::Stricmp(Key, TEXT("distance")) == 0)
	{
		MoveDistance = Value != nullptr ? FCString::Atof(Value) : 0.f;
		return true;
	}
	return BaseEntityKeyValue(Key, Value);
}

bool FElysiumNpc::BaseEntityKeyValue(const TCHAR* Key, const TCHAR* Value)
{
	// `CBaseEntity::KeyValue` (`0x1009e430`). Family **Lifecycle** recovered the classification and
	// the `#` truncation retail performs FIRST; this is that classification, dispatched.
	FString Truncated;
	const EKeyValueArm Arm = ClassifyKeyValue(FString(Key != nullptr ? Key : TEXT("")), Truncated);
	if (Arm != EKeyValueArm::DataMap)
	{
		// The nine literal arms — `rendercolor`, `renderamt`, `disableshadows`,
		// `disablereceiveshadows`, `mins`, `maxs`, `angle`, `angles`, `origin`. Every one of them
		// writes a `CBaseEntity` word (`m_clrRender`, `m_fEffects`, the collision bounds, the abs
		// angles, the abs origin) and returns true. They are `CBaseEntity`'s story, NOT a row of
		// this kernel's closure, so none is run here — but the ANSWER is retail's, true, because a
		// key that matched is a key the caller must not treat as unhandled.
		return true;
	}
	// The `DataMap` arm: retail walks `GetDataDescMap()` (`vtable +0x148`) down `baseMap` and offers
	// the key to each level's `ParseKeyvalue`. `FElysiumEntity::Construct` applies a map's raw
	// keyvalues through the class-chain field table, which is the same walk over the same data, so
	// this arm IS that table.
	if (Class == nullptr || Key == nullptr)
	{
		return false;
	}
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	const FElysiumFieldAccessor* Acc = Reg.FindField(*Class, FName(*Truncated));
	if (Acc == nullptr || !Acc->Set)
	{
		// Retail's own "no level of the chain claimed it" answer.
		return false;
	}
	Acc->Set(*this, FElysiumVariant::String(FString(Value != nullptr ? Value : TEXT(""))));
	return true;
}

// =================================================================================================
// The VSound concept seam — what every hook below ends in
// =================================================================================================

int32 FElysiumNpc::VSoundConceptId(const TCHAR* ConceptName)
{
	// The walk retail performs, over a list this runtime does not load:
	//
	//     for (i = 0; i < DAT_1073dc3c; ++i)
	//         name = ((Entry**)DAT_1073dc40)[i]->Name;   // null read as ""
	//         if (__strcmpi(name, Concept) == 0) return ((Entry**)DAT_1073dc40)[i]->Id;
	//     return 0xffffffff;
	//
	// The count is zero here, so the loop body never runs and the answer is the fall-through.
	// `-1` is what retail then hands the play entry, unchanged: a miss does NOT suppress the call.
	(void)ConceptName;
	return INDEX_NONE;
}

void FElysiumNpc::SpeakVSound(const TCHAR* ConceptName, int32 ConceptId, int32 Channel,
	float Volume, float Attenuation)
{
	FVSoundSpeak Call;
	Call.Concept = ConceptName;
	Call.ConceptId = ConceptId;
	Call.Channel = Channel;
	Call.Volume = Volume;
	Call.Attenuation = Attenuation;
	VSoundSpeakCalls.Add(Call);

	// `0x101f5950`'s first arm: `GetVSoundTableIdx(entity) >= this->m_nTables` takes the
	// `"ERROR: VSnd: Play: %s Table out of bounds: %d\n"` branch and returns. With no table array
	// at all every entity is out of bounds, so this IS the arm retail takes, and the log is
	// retail's own.
	static bool bReported = false;
	if (!bReported)
	{
		bReported = true;
		UE_LOG(LogElysiumNpcEnt, Verbose,
			TEXT("%s VSnd: Play refused: this runtime loads no VSound table (retail 0x101f5950); "
				"concept \"%s\" id %d chan %d vol %.2f attn %.2f"),
			*DebugString(), ConceptName != nullptr ? ConceptName : TEXT(""), ConceptId, Channel,
			Volume, Attenuation);
	}
}

void FElysiumNpc::SpeakSoundConcept(const TCHAR* ConceptName, float Attenuation)
{
	SpeakVSound(ConceptName, VSoundConceptId(ConceptName), GSounds10ChanVoice, GSounds10Volume,
		Attenuation);
}

// =================================================================================================
// The seventeen sound hooks, slots 488–507
// =================================================================================================

// `CAI_BaseNPCTroika::FUN_10293ec0` (`0x10293ec0`), slot 488 `DeathSound`. 138 bytes, and the plain
// shape: once-flag `DAT_10923f0d` over the concept `"Death"` into `DAT_10924d64`, then the speak.
//
// The vtable dispatch in front of it (`CNPC_VTzimisce` `0x103b92a0`, `CGeneric_NPC` `0x1035a500`,
// `CNPC_VCamera` `0x103680b0`, …) is `FElysiumNpc::DeathSound` in family **Sounds**' file, which is
// where story 29c-1 put the prologue; this is the arm it falls through to.
void FElysiumNpc::TroikaDeathSound()
{
	SpeakSoundConcept(GSounds10ConceptDeath, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10293f80` (`0x10293f80`), slot 489 `AlertSound`. 138 bytes, the plain
// shape, concept `"Target_Suspect"` (`0x105d8c38`) through `DAT_10924330` / `DAT_109241f0`.
void FElysiumNpc::AlertSound()
{
	SpeakSoundConcept(GSounds10ConceptTargetSuspect, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294280` (`0x10294280`), slot 490 `IdleSound`. 138 bytes, concept
// `"Idle_Calm"` (`0x105d8c60`) through `DAT_109240c0` / `DAT_10924504`.
//
// NOTE what is NOT here: retail's slot 490 has no `FOkToMakeSound()` gate. The rate limit lives on
// the CALLER — slot 509 `ShouldPlayIdleSound` (family **Sounds**) is what rolls — and the two
// species overrides that DO gate (`CNPC_VTest` `0x103b4600`, `CNPC_VTzimisce` `0x103b9380`) carry
// the gate themselves, in the vocalization table.
void FElysiumNpc::IdleSound()
{
	SpeakSoundConcept(GSounds10ConceptIdleCalm, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294340` (`0x10294340`), slot 491 `PainSound`. 138 bytes, concept
// `"Pain"` — the string at `0x105d8c6c` the corpus left unnamed, read out of the pinned image —
// through `DAT_1092482c` / `DAT_1092442c`.
//
// SPECIES ARM: `CNPC_VWerewolf::vfunc491` (`0x103d87a0`), 245 bytes. It matches the SAME concept
// through its own guard `DAT_1093d634` and its own cache `DAT_1093d6ec`, pushes a scope-trace frame
// the base has none of — and the frame's string is the copy-pasted
// `"CNPC_VWerewolf::ExertHvySound"` (`0x10663248`), a RETAIL MISLABEL a debug dump reproduces —
// and passes `0` as the play entry's fifth argument where the base passes `1.25`. It does not chain
// to the base.
void FElysiumNpc::PainSound()
{
	if (IsRetailClass(TEXT("CNPC_VWerewolf")))
	{
		SpeakSoundConcept(GSounds10ConceptPain, GSounds10WerewolfAttenuation);
		return;
	}
	SpeakSoundConcept(GSounds10ConceptPain, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294400` (`0x10294400`), slot 492 `FearSound`. 138 bytes, concept
// `"Fear_Start"` (`0x105d8c74`) through `DAT_10924934` / `DAT_10924e88`.
void FElysiumNpc::FearSound()
{
	SpeakSoundConcept(GSounds10ConceptFearStart, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_102944c0` (`0x102944c0`), slot 493 `LostEnemySound`. 160 bytes — the ONE
// hook with a roll in front of it:
//
//     if (RandomInt(0, 99) >= 0x19) return;      // `1029450a`, a 25-in-100 chance
//     <the plain shape, concept "Target_Lost" (0x105d8c84)>
//
// The roll happens FIRST and UNCONDITIONALLY, so the stream advances on every call whether or not
// the sound is spoken. That position is the observable part and is what the test pins.
void FElysiumNpc::LostEnemySound()
{
	const int32 Roll = ElysiumRng::Stream(GSounds10HookStream)
		.RandRange(0, GSounds10LostEnemyRollMax);
	if (Roll >= GSounds10LostEnemyRollThreshold)
	{
		return;
	}
	SpeakSoundConcept(GSounds10ConceptTargetLost, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294590` (`0x10294590`), slot 494 `FoundEnemySound`. 147 bytes:
//
//     if (IsBusyWithDiscipline()) return;        // `CBaseCombatCharacter::IsBusyWithDiscipline`
//     <the plain shape, concept "Target_Reacquired" (0x105d8c94)>
//
// Slot 506 (`0x10294e70`) is the same gate over the same concept through a DIFFERENT cache
// (`DAT_10924240` / `DAT_10924830` rather than `DAT_109240c8` / `DAT_10924a14`). Two caches, one
// concept: with the lookup pure, the two are the same answer, which is why this port re-resolves
// instead of carrying two cells.
void FElysiumNpc::FoundEnemySound()
{
	if (IsBusyWithDiscipline())
	{
		return;
	}
	SpeakSoundConcept(GSounds10ConceptTargetReacquired, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294660` (`0x10294660`), slot 495 `SurprisedSound`. 138 bytes, concept
// `"Surprised"` (`0x105d8cac`) through `DAT_10923f0c` / `DAT_10924f64`.
void FElysiumNpc::SurprisedSound()
{
	SpeakSoundConcept(GSounds10ConceptSurprised, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294720` (`0x10294720`), slot 496 `TargetAcquiredSound`. 138 bytes,
// concept `"Target_Acquired"` (`0x105d8cb8`) through `DAT_10923dde` / `DAT_1092423c`. No caller of
// any kind in the image, and it fills slot 496 on 57 census classes, so it is reached through the
// vtable and is not dead.
void FElysiumNpc::TargetAcquiredSound()
{
	SpeakSoundConcept(GSounds10ConceptTargetAcquired, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294870` (`0x10294870`), slot 498 `FleeSound`. 138 bytes, concept
// `"Flee"` — the second string the corpus left unnamed (`0x105d8cd0`), read out of the image, one
// cell past the three-byte placeholder `"???"` at `0x105d8ccc` — through `DAT_10923dd5` /
// `DAT_10924e84`.
void FElysiumNpc::FleeSound()
{
	SpeakSoundConcept(GSounds10ConceptFlee, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294930` (`0x10294930`), slot 499 `IdleAgitatedSound`. 138 bytes, concept
// `"Idle_Agitated"` (`0x105d8cd8`) through `DAT_10923ddd` / `DAT_10923e30`.
void FElysiumNpc::IdleAgitatedSound()
{
	SpeakSoundConcept(GSounds10ConceptIdleAgitated, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_102949f0` (`0x102949f0`), slot 500 `ExertHvySound`. 138 bytes, concept
// `"Exert_Heavy"` (`0x1057a1a0`, NOT in the `0x105d8c30` block with the other sixteen) through
// `DAT_109241ec` / `DAT_109240bc`.
//
// SPECIES ARM: `CNPC_VWerewolf::vfunc500` (`0x103d8660`), 245 bytes — the same concept through its
// own guard `DAT_1093f99c` and cache `DAT_1093fa30`, inside a `"CNPC_VWerewolf::ExertHvySound"`
// scope-trace frame, with `0` as the fifth argument. It does not chain to the base.
void FElysiumNpc::ExertHvySound()
{
	if (IsRetailClass(TEXT("CNPC_VWerewolf")))
	{
		SpeakSoundConcept(GSounds10ConceptExertHeavy, GSounds10WerewolfAttenuation);
		return;
	}
	SpeakSoundConcept(GSounds10ConceptExertHeavy, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294ab0` (`0x10294ab0`), slot 501 `ExertLightSound`. 138 bytes, concept
// `"Exert_Light"` (`0x1057a1b0`) through `DAT_10923f0f` / `DAT_10924838`.
void FElysiumNpc::ExertLightSound()
{
	SpeakSoundConcept(GSounds10ConceptExertLight, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294b70` (`0x10294b70`), slot 502 `RiledSound`. 138 bytes, concept
// `"Riled"` (`0x105d8ce8`) through `DAT_10924aac` / `DAT_10924244`.
void FElysiumNpc::RiledSound()
{
	SpeakSoundConcept(GSounds10ConceptRiled, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294c30` (`0x10294c30`), slot 503 `ComfortSound`. 138 bytes, concept
// `"Comfort"` (`0x105d8cf0`) through `DAT_1092497c` / `DAT_10924624`.
void FElysiumNpc::ComfortSound()
{
	SpeakSoundConcept(GSounds10ConceptComfort, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294cf0` (`0x10294cf0`), slot 504 `UpsetSound`. 138 bytes, concept
// `"Upset"` (`0x105d8cfc`) through `DAT_10924242` / `DAT_10924fb4`.
void FElysiumNpc::UpsetSound()
{
	SpeakSoundConcept(GSounds10ConceptUpset, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294db0` (`0x10294db0`), slot 505 `TargetGiveUpSound`. 138 bytes, concept
// `"Target_GiveUp"` (`0x105d8d04`) through `DAT_10923dd6` / `DAT_10923d7c`.
void FElysiumNpc::TargetGiveUpSound()
{
	SpeakSoundConcept(GSounds10ConceptTargetGiveUp, GSounds10Attenuation);
}

// `CAI_BaseNPCTroika::FUN_10294e70` (`0x10294e70`), slot 506's Troika-line body. 147 bytes and
// byte-for-byte slot 494's shape: the `IsBusyWithDiscipline` gate, then the SAME concept
// `"Target_Reacquired"` through its own guard and cache.
//
// The dispatch in front (`CNPC_VCamera` `0x103682f0`, an empty body) is `FElysiumNpc::Slot506` in
// family **Sounds**' file.
void FElysiumNpc::TroikaSlot506()
{
	if (IsBusyWithDiscipline())
	{
		return;
	}
	SpeakSoundConcept(GSounds10ConceptTargetReacquired, GSounds10Attenuation);
}

float FElysiumNpc::FloatSoundAttenuation(bool bHasDialogName)
{
	// `10294f9f`..`10294fdc`, from the listing:
	//
	//     EAX = m_iDialog; NEG EAX; SBB EAX,EAX; AND EAX,0xe      ; 0xe when set, 0 when not
	//     ADD EAX,0x42; CMP EAX,0x32; JLE -> FLD double [0x10449148]   ; 4.0, UNREACHABLE
	//     LEA ECX,[EAX-0x32]; EAX=0x14; CDQ; IDIV ECX; FILD            ; (int)(20 / (t + 0x10))
	//
	// `0x42` and `0x50` are both above `0x32`, so the constant arm cannot be taken by either value
	// of `t`; the answer is the integer quotient, widened.
	const int32 Term = bHasDialogName ? 0xe : 0;
	if (Term + 0x42 <= 0x32)
	{
		// Retail's `_DAT_10449148`, 4.0 — read out of the pinned image, and unreachable.
		return 4.0f;
	}
	return static_cast<float>(0x14 / (Term + 0x10));
}

int32 FElysiumNpc::FloatSoundMinDelaySeconds() const
{
	// `1029502c`: `PUSH 1` — row **1** of `Float_Sound_Info`, which the authored table names
	// `FloatSoundMinDelay -- Minimum delay in seconds before next float sound` and sets to 5.0.
	// `0x1006c9d0` runs the row through `__ftol`, so what reaches `m_flNextFloatSoundTime` is an
	// INT; `1029505f` then adds it with `FIADD`, an integer add. Both truncations are retail's.
	float Row = GSounds10FloatSoundMinDelayFallback;
	if (World != nullptr)
	{
		if (UElysiumSessionSubsystem* GameState = World->GetGameState())
		{
			if (UElysiumRulebookSubsystem* Rules = GameState->Rulebook())
			{
				if (const FElysiumRuleTable* Table = Rules->Rules().Table(GSounds10FloatSoundTable))
				{
					Row = Table->Lookup(GSounds10FloatSoundMinDelayRow,
						GSounds10FloatSoundMinDelayFallback);
				}
			}
		}
	}
	return static_cast<int32>(Row);
}

// `CAI_BaseNPCTroika::FUN_10294f40` (`0x10294f40`), slot 507 `FloatSound`. 302 bytes — the only hook
// with a computed fifth argument and the only one that writes state. In retail's order:
//
//   1. the concept lookup, `"Float"` (`0x105d8d14`) behind bit 0 of `DAT_1092488d`;
//   2. the fifth argument, `FloatSoundAttenuation(m_iDialog != 0)` — 1.0 idle, 0.0 in dialogue;
//   3. the speak, channel 2, volume 1.0;
//   4. bit 1 of the same flag caches the `Float_Sound_Info` table id and bit 2 its row-1 value;
//   5. `m_flNextFloatSoundTime` (`+0x10ec`) = engine `Time()` + that INT value.
//
// Step 5 is the re-arm family **Sounds**' `BaseShouldPlayFloatSound` (`0x1027a530`) reads: until
// this story it had no writer at all, which is what `ElysiumNpcKernelSounds.inl` recorded beside
// `NextFloatSoundTime`. It has one now.
//
// `m_iFloatSoundFrequency` is NOT written here — the keyfield `floatfreq` is its only source, and
// `0x1027a530`'s `RandomInt(0, frequency)` is what spends it.
void FElysiumNpc::FloatSound()
{
	const float Attenuation = FloatSoundAttenuation(!DialogName.IsEmpty());
	SpeakVSound(GSounds10ConceptFloat, VSoundConceptId(GSounds10ConceptFloat), GSounds10ChanVoice,
		GSounds10Volume, Attenuation);

	NextFloatSoundTime = Sounds10Now(*this) + static_cast<double>(FloatSoundMinDelaySeconds());
}

// =================================================================================================
// `CNPC_Crow`'s slot 511 arm
// =================================================================================================

void FElysiumNpc::StopNamedSound(const TCHAR* SoundScript)
{
	StopNamedSoundCalls.Add(FString(SoundScript != nullptr ? SoundScript : TEXT("")));
}

// =================================================================================================
// Slot 185 `FireBullets` — `0x10268900`
// =================================================================================================

bool FElysiumNpc::FireBulletsShooterIsNpc() const
{
	// `+0x0098 m_pBaseNPCTroika`. Every entity standing on this leaf is an NPC.
	return true;
}

bool FElysiumNpc::FireBulletsShooterIsPlayer() const
{
	// `+0x00a8 m_pPlayer`. Never an NPC. `FElysiumPlayer` is a different leaf entirely and does not
	// dispatch this slot.
	return false;
}

int32 FElysiumNpc::AmmoDefFlags(int32 AmmoTypeIndex) const
{
	// `0x104276a0`: `(0 < i && i < m_nAmmoIndex) ? m_AmmoType[i].nFlags : 0`. No `CAmmoDef` here,
	// so `m_nAmmoIndex` is 0 and every index is out of range.
	(void)AmmoTypeIndex;
	return 0;
}

int32 FElysiumNpc::ShooterRangedSkill() const
{
	// `0x101cda50` → the local player, then its type-3 stat list's stat 3, else the empty static
	// list's `0`. Unjoined here; `0` is retail's own no-list answer.
	return 0;
}

void FElysiumNpc::VectorVectors(const FVector& Forward, FVector& OutRight, FVector& OutUp)
{
	// `0x10138a90`, Source's `VectorVectors`, with `_DAT_104454c4` (0.0) folded into the cross
	// products as the constant `up` axis's x and y.
	if (Forward.X == 0.0 && Forward.Y == 0.0)
	{
		// Retail's degenerate arm, verbatim: `right = (1,0,0)`, `up = (0, -forward.z, 0)`. Neither
		// is normalized and `up` is not perpendicular to anything; it is what the body writes.
		OutRight = FVector(1.0, 0.0, 0.0);
		OutUp = FVector(0.0, -Forward.Z, 0.0);
		return;
	}
	OutRight = FVector(Forward.Y, -Forward.X, 0.0);
	OutRight.Normalize();
	// `up = cross(right, forward)`, then normalized.
	OutUp = FVector(
		Forward.Z * OutRight.Y - Forward.Y * OutRight.Z,
		Forward.X * OutRight.Z - Forward.Z * OutRight.X,
		OutRight.X * Forward.Y - OutRight.Y * Forward.X);
	OutUp.Normalize();
}

FVector FElysiumNpc::BulletSpreadOffset(const FVector& SpreadUnits, const FVector& RightAxis,
	const FVector& UpAxis)
{
	// `0x10268170`. The rejection sampler first, exactly as retail writes it: FOUR draws per
	// attempt, summed in pairs, redrawn while the pair is outside the unit disc.
	FRandomStream& Stream = ElysiumRng::Stream(GSounds10SpreadStream);
	float X = 0.f;
	float Y = 0.f;
	do
	{
		X = Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax)
			+ Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax);
		Y = Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax)
			+ Stream.FRandRange(GSounds10SpreadDrawMin, GSounds10SpreadDrawMax);
	}
	while (X * X + Y * Y > GSounds10One);

	// `this+0xa8` (`m_pPlayer`): a PLAYER shooter replaces BOTH spread components with
	// `ScaleField_0x1ddc()` (`0x10160680`, family **Lifecycle**). This leaf is never the player, so
	// the info's own pair stands and the arm is named rather than run.
	const float SpreadX = FireBulletsShooterIsPlayer()
		? static_cast<float>(ScaleField_0x1ddc()) : static_cast<float>(SpreadUnits.X);
	const float SpreadY = FireBulletsShooterIsPlayer()
		? static_cast<float>(ScaleField_0x1ddc()) : static_cast<float>(SpreadUnits.Y);

	return RightAxis * (SpreadX * X) + UpAxis * (SpreadY * Y);
}

void FElysiumNpc::FireBulletsTracePass(FElysiumFireBulletsInfo& Info)
{
	FFireBulletsTrace Trace;
	Trace.SrcUnits = Info.SrcUnits;
	Trace.EndUnits = Info.EndUnits;
	Trace.DirUnits = Info.DirCurrent;
	Trace.TracerScale = Info.TracerScale;
	Trace.FilterWord = FireBulletsFilterWord;
	FireBulletsTraces.Add(Trace);

	// `0x10267b60` writes `info+0xbc` with whatever it hit. Nothing traces here, so it stays unset —
	// the same word a bullet that hit world geometry leaves.
	Info.LastVictim = FElysiumEntityHandle();
}

void FElysiumNpc::EmitBulletTracer(const FElysiumFireBulletsInfo& Info, float Fraction)
{
	FBulletTracerCall Call;
	Call.Name = Info.TracerName;
	Call.EndUnits = Info.EndUnits;
	Call.Fraction = Fraction;
	Call.TracerScale = Info.TracerScale;
	BulletTracerCalls.Add(MoveTemp(Call));
}

void FElysiumNpc::RangedDamagePerVictim(const FElysiumEntityHandle& Victim,
	const FElysiumFireBulletsInfo& Info, float Fraction)
{
	(void)Info;
	FRangedDamagePerVictimCall Call;
	Call.Victim = Victim;
	Call.Fraction = Fraction;
	RangedDamagePerVictimCalls.Add(Call);
}

// `CAISound::FUN_10268900` (`0x10268900`), slot 185, 1212 bytes, shared by `CAI_BaseNPC#185` and
// `CAI_BaseNPCTroika#185`. Ten steps, in retail's order:
//
//   1. `m_pBaseNPCTroika` (`+0x98`) set → `--m_iFakeReloadCount` (`+0x65f0`) on it. Always, here.
//   2. `info.m_iFlags (+0x58) = GetAmmoDef()->Flags(info.m_iAmmoType (+0x8c))`.
//   3. `info.m_pAttacker (+0x94)` defaults to the shooter when unset.
//   4. the two trace filters (`0x101c2c60`, `0x101c2c30`) and `DAT_1072cb48 = flags | 0x1000`.
//   5. `VectorVectors(info+0x14, right, up)` (`0x10138a90`).
//   6. open the per-victim tally (`0x1027f940`).
//   7. `for (repeat : info[0]) for (bullet : info[1])` — the pass below.
//   8. slot 186 (`vtable +0x2e8`) false → the tracer arm OR the trace-and-damage pass.
//   9. after EACH repeat, `RangedDamagePerVictim(victim, info, hits / info[1])` for every victim in
//      the tally — which is NOT cleared between repeats, so repeat 2 re-pays repeat 1's victims
//      with their accumulated counts. Retail's own behaviour, reproduced.
//  10. `0x10160560(m_pPlayer)` and free the tally.
//
// Step 8's gate is slot 186, `FElysiumNpc::Slot186` (family **Closure**), whose whole retail body
// (`0x100270c0`) is `return false;` — so the pass below always runs.
void FElysiumNpc::FireBullets(void* InInfo)
{
	FElysiumFireBulletsInfo* Info = static_cast<FElysiumFireBulletsInfo*>(InInfo);
	if (Info == nullptr)
	{
		// CRASH GUARD: retail dereferences the packet unconditionally. No caller in this runtime
		// passes null yet; refusing is the only answer that is not a fault.
		return;
	}

	// 1. The outstanding-shot counter, on the shooter's own NPC self-pointer.
	if (FireBulletsShooterIsNpc())
	{
		--FakeReloadCount;
	}

	// 2. The ammo flags, WRITTEN back into the packet — every later arm reads them from there.
	Info->AmmoFlags = AmmoDefFlags(Info->AmmoType);

	// 3. The attacker default.
	if (!Info->Attacker.IsSet())
	{
		Info->Attacker = Handle;
	}

	// 4. The filter word. `DAT_1072cb48` is a file static in retail with this as its only writer in
	// the closure.
	FireBulletsFilterWord = Info->AmmoFlags | GSounds10FireBulletsFilterBit;

	// 5. The basis, from the SHOOTING DIRECTION and not from a set of angles: `0x10138a90` is
	// `VectorVectors`, not `AngleVectors`. (The checklist's walk named it `AngleVectors`; the body
	// at `0x10138a90` is the cross-product basis builder, and `info+0x14` is the same word the
	// per-shot direction is copied from two lines later, which a set of Euler angles could not be.)
	FVector RightAxis = FVector::ZeroVector;
	FVector UpAxis = FVector::ZeroVector;
	VectorVectors(Info->DirShooting, RightAxis, UpAxis);

	// 6. The tally: one `{ victim, hits }` pair per distinct victim, appended in first-hit order.
	TArray<FRangedDamagePerVictimCall> Tally;

	for (int32 Repeat = 0; Repeat < Info->Repeats; ++Repeat)
	{
		for (int32 Bullet = 0; Bullet < Info->Bullets; ++Bullet)
		{
			// 7a. The working direction starts as the forward, unchanged.
			Info->DirCurrent = Info->DirShooting;

			// 7b. The spread gate. `(flags & 0x2000000) == 0 || (m_pPlayer && m_pPlayer->+0x1e78
			// == 0)` — for an NPC shooter the second disjunct is dead, so the bit alone decides.
			// The offset is added UNSCALED: retail does not multiply it by the distance.
			const bool bNoSpreadBit = (Info->AmmoFlags & GSounds10AmmoFlagNoNpcSpread) != 0;
			if (!bNoSpreadBit || FireBulletsShooterIsPlayer())
			{
				Info->DirCurrent += BulletSpreadOffset(Info->Spread, RightAxis, UpAxis);
			}

			// 7c. The endpoint. `end = src + dir * distance`, component by component, in retail's
			// own x/z/y store order (which is unobservable and is written x/y/z here).
			Info->EndUnits = Info->SrcUnits + Info->DirCurrent * Info->DistanceUnits;

			// 7d. The ranged skill and the tracer latch.
			const int32 Skill = ShooterRangedSkill();
			bool bTracer = false;
			if ((Info->TracerFlags & GSounds10TracerFlagAlways) != 0
				|| (Skill > GSounds10TracerSkillThreshold && !FireBulletsShooterIsPlayer()))
			{
				bTracer = true;
				// `uVar11 & ((int)uVar11 < 0) - 1` below 6, `5` at or above it: the clamp to 0..5.
				const int32 Index = Skill < 6 ? (Skill < 0 ? 0 : Skill) : 5;
				Info->TracerScale = Info->TracerScale / GSounds10SkillDivisor[Index];
			}

			// 8. Slot 186 gates the whole emission half.
			if (Slot186())
			{
				continue;
			}
			if (bTracer && !Info->TracerName.IsEmpty())
			{
				// The tracer arm REPLACES the trace-and-damage pass: a bullet that draws a tracer
				// does no damage in this body at all.
				EmitBulletTracer(*Info, GSounds10One / static_cast<float>(Info->Bullets));
			}
			else
			{
				FireBulletsTracePass(*Info);
				if (Info->LastVictim.IsSet())
				{
					FRangedDamagePerVictimCall* Row = Tally.FindByPredicate(
						[Info](const FRangedDamagePerVictimCall& Candidate)
						{
							return Candidate.Victim.Index == Info->LastVictim.Index;
						});
					if (Row == nullptr)
					{
						FRangedDamagePerVictimCall New;
						New.Victim = Info->LastVictim;
						New.Fraction = 0.f;
						Row = &Tally.Add_GetRef(New);
					}
					// `*(int*)(entry + 4) += 1` — the hit count, carried in `Fraction` until the
					// division below turns it into one.
					Row->Fraction += 1.f;
				}
			}
		}

		// 9. The per-victim payout, once per REPEAT and guarded on a non-zero bullet count (the
		// guard is retail's own divide-by-zero check).
		if (Info->Bullets != 0)
		{
			for (const FRangedDamagePerVictimCall& Row : Tally)
			{
				RangedDamagePerVictim(Row.Victim, *Info,
					Row.Fraction / static_cast<float>(Info->Bullets));
			}
		}
	}

	// 10. `0x10160560(m_pPlayer)` — unreachable for an NPC shooter, and named so.
	if (FireBulletsShooterIsPlayer())
	{
		// SEAM: `0x10160560`, the player-side post-fire bookkeeping. No NPC reaches it.
	}
}
