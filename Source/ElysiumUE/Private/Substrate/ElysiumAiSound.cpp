// ai_sound -- `CAISound` (`walks/L0-r017.md`, story L0.entity_core.datamap-keyvalues): the point entity
// whose one map key is `soundtype` and whose one input, `InsertSound`, puts a sound of that type on the
// sound list (`CSoundEnt::InsertSound` 0x101bac90). Its datamap `datamap_CAISound` (0x10599f48; the
// class's slot 82 is `MOV EAX,0x10599f48; RET`, the builder `datamap_CAISound_builder` 0x101bb500 fills
// `+0` / `+4` at run time) holds two rows and chains to
// `datamap_CBaseEntity` (0x10552e18): `m_iSoundType` (INTEGER, +0x450, flags 6 SAVE|KEY, external
// `soundtype`) and `InputInsertSound` (INPUT, external `InsertSound`, inputFunc 0x1000f781 ->
// `CAISound::InputInsertSound` 0x101bb530). The classname string `ai_sound` is 0x1059a4cc, linked by
// `FUN_101bb430`.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumGameSound.h"

class FElysiumAiSound final : public FElysiumEntity
{
public:
	// `m_iSoundType` (+0x450): the `SOUND_*` type mask `InsertSound` files the sound under. Written by the
	// `soundtype` key through the datamap walker 0x101a5a80 (INTEGER: `atoi`, so `3.9` is 3).
	int32 SoundType = 0;

	// slot 82, `CAISound`'s own body: `return &datamap_CAISound` (0x10599f48) -- this class's
	// descriptor, whose `BaseName` is the `baseMap` link (+0xc = 0x10552e18) to `CBaseEntity`'s. The
	// base body already answers the leaf descriptor through the dispatch; this override stands where
	// retail's does.
	virtual void* GetDataDescMap() override { return const_cast<FElysiumClassDesc*>(Class); }

	// `CAISound::InputInsertSound` 0x101bb530: `value = (variant.type == 4) ? variant.int : 0`;
	// `CSoundEnt::InsertSound(m_iSoundType, GetAbsOrigin() (slot 217), value, 0.2f, 0, this)`. The
	// value is the sound's volume (its reach in Source units); the duration is the constant 0.2 s.
	void InputInsertSound(const FElysiumInputArgs& Args)
	{
		const int32 Volume = Args.Param.IsInt() ? Args.Param.AsInt : 0;        // 101bb53a: type 4 only, else 0
		if (World != nullptr)
		{
			// `CSoundEnt::InsertSound` 0x101bac90 is the game-sound bus here (`audit.tsv` row `ported`):
			// no category row (the authored reach is the input's own value), the type mask is the key's
			// word, the stealth subtrahend 0 for a world-made noise.
			World->EmitGameSound(GetAbsOrigin(), NAME_None, static_cast<float>(Volume) * ElysiumMove::U, Handle, 0.f,
				static_cast<uint32>(SoundType), 0.2);                           // 101bb55d
			World->EmitRetailSite(*this, TEXT("ai_sound_input"), TEXT("CAISound::InputInsertSound"), 0x101bb530u, TEXT("call"),
				FString::Printf(TEXT("fn=CSoundEnt::InsertSound va=0x101bac90 type=%d volume=%d duration=0.2 flags=0"),
					SoundType, Volume));
		}
	}
};

static TUniquePtr<FElysiumEntity> MakeAiSound()
{
	return MakeUnique<FElysiumAiSound>();
}

static FElysiumClassRegistrar GRegAiSound(
	TEXT("ai_sound"), ElysiumBaseClassName(), &MakeAiSound,
	[](FElysiumClassDesc& D)
	{
		// `datamap_CAISound` row 0 (0x10599f8c): `m_iSoundType` INTEGER +0x450, flags 0x6 SAVE|KEY,
		// external `soundtype`. The one KEY row of the class; the walker and `ReadKeyField` find it at
		// this level before the base's 110 rows.
		ElysiumAddClassField(D, TEXT("soundtype"), &FElysiumAiSound::SoundType, EElysiumField::Save | EElysiumField::MapKey);
		// Row 1 (0x10599fb8): `InputInsertSound`, flags 0x8 INPUT, external `InsertSound`, the row's own
		// type INTEGER (the variant the body tests for).
		D.TypedInput(TEXT("InsertSound"), EElysiumVariantType::Int,
			[](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAiSound&>(E).InputInsertSound(A); });
	});
