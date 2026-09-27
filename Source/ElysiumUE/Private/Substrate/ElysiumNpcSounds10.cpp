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
//   * the **species arms** `CNPC_VWerewolf#500` (`0x103d8660`) and `CNPC_VWerewolf#491`
//     (`0x103d87a0`). `CNPC_Crow#511` (`0x10357800`) carries no arm: no map stands that class.
//
// The walked prose is `docs/vtmb/npc-ai/senses.md` § "Story 29d, family Sounds10 — …".
//
// **What this family emits through.** Every hook here ends in the VSound play entry `0x101f5950`,
// which resolves a wav out of the per-entity VSound table and hands it to `IEngineSound::EmitSound`.
// This runtime parses no VSound table and no concept list — `PrecacheSoundTable` (slot 71) is still
// a generated stub and `m_iVSoundTableIdx` (`+0x00bc`) has no writer — so the concept lookup answers
// retail's own miss (`-1`) and the play entry takes retail's own out-of-bounds arm. Both are
// SEAMS declared in `ElysiumNpcSounds10.inl` and both record what they were asked for, which
// is the recovered half: the concept, the channel, the volume and the fifth argument.

#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSounds10Shared.h"

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
	const TCHAR* const GSounds10ConceptExertLight = TEXT("Exert_Light");            // 0x1057a1b0

	// Source's `CHAN_VOICE`. Every hook in this family passes `2` as `0x101f5950`'s third argument.
	constexpr int32 GSounds10ChanVoice = 2;
	// `0x3f800000`, the fourth argument on every hook in this family.
	constexpr float GSounds10Volume = 1.0f;
	// `0x3fa00000`, the fifth argument on sixteen of the seventeen Troika hooks.
	constexpr float GSounds10Attenuation = 1.25f;

	// `CAI_BaseNPCTroika::FUN_102944c0` (slot 493) `1029450a`: `RandomInt(0, 99) < 0x19`.
	constexpr int32 GSounds10LostEnemyRollMax = 99;
	constexpr int32 GSounds10LostEnemyRollThreshold = 0x19;

	// `Float_Sound_Info` (`vdata/system/rules_tables.txt`), the table slot 507 re-arms from.
	// Row 1 is `FloatSoundMinDelay -- Minimum delay in seconds before next float sound`, 5.0.
	const TCHAR* const GSounds10FloatSoundTable = TEXT("Float_Sound_Info");
	constexpr int32 GSounds10FloatSoundMinDelayRow = 1;
	constexpr float GSounds10FloatSoundMinDelayFallback = 5.0f;

	// --- `FireBullets`' recovered constants ------------------------------------------------

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
// The vtable dispatch in front of it (`CNPC_VTzimisce` `0x103b92a0`, `CNPC_VCamera` `0x103680b0`,
// …) is `FElysiumNpc::DeathSound` in family **Sounds**' file, which is
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
// the CALLER — slot 509 `ShouldPlayIdleSound` (family **Sounds**) is what rolls — and the species
// override that DOES gate (`CNPC_VTzimisce` `0x103b9380`) carries the gate itself, in the
// vocalization table.
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
	SpeakSoundConcept(NpcKernelSounds10Shared::GSounds10ConceptPain, GSounds10Attenuation);
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
	SpeakSoundConcept(NpcKernelSounds10Shared::GSounds10ConceptExertHeavy, GSounds10Attenuation);
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
// Step 5 is the re-arm family **Sounds**' `FElysiumNpcBase::ShouldPlayFloatSound` (`0x1027a530`) reads: until
// this story it had no writer at all, which is what `ElysiumNpcSounds.inl` recorded beside
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
// Slot 185 `FireBullets` — `0x10268900`
// =================================================================================================

