#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **Precache10** — slot 104 `Precache`: the `CAI_BaseNPC` base body, the
// `CAI_BaseNPCTroika` body that owns the slot, and its species arms. The three `CNPCMaker*` bodies
// are their classes' overrides (`ElysiumNpcMaker*.cpp`, story 5 fold A4); the
// arms of the classes with no instance (`population.md` § "NPC classes with no instance") are not
// ported.
//
// Every constant below was read off the decompiled C and, where the decompiler folded an argument,
// off the listing. `ElysiumNpcPrecache10.inl` carries the family's reading notes; the walked
// prose is `docs/vtmb/npc-ai/lifecycle.md`.
//
// **How a pointer table's order was read.** Eleven of these bodies walk a `.rdata` pointer table
// with a byte loop (`uVar = 0; ... uVar += 4; while (uVar < N)`), and the corpus names such a table
// only by its first entry's string. The order is recovered from the layout: MSVC emits string
// literals in REVERSE order of first appearance, so the entry declared first carries the HIGHEST
// address — `character/monster/gargoyle/stomp_1.wav` is `0x10639814` and `stomp_4` is `0x10639784`.
// The disassembly's own annotation of each indexed load confirms it on the four tables where Ghidra
// resolved two entries (`wingflap_1` then `wingflap_2`; `Foot_Step1` then `Foot_Step2`;
// `pain1` then `pain2`; `ThrowTaxi.mdl` then `supportb.mdl`), and the three tables the port already
// carries as vocalization data (`ElysiumNpcSounds.cpp`, `ElysiumFootsteps.cpp`) agree.

namespace
{
	// --- The words the base and Troika `Precache` bodies read ----------------------------------
	// `s_models_error_error_mdl_105d90c4` — the Troika body's model fallback.
	const TCHAR* const GErrorModel = TEXT("models/error/error.mdl");
	// `s_Normal_105c89dc` — the attack-coordinator name slot 608 is dispatched with, last.
	const TCHAR* const GNormalCoordinator = TEXT("Normal");
	// `0x101d0f10`'s two reject literals. **UNRECOVERED**: `DAT_105a0410` is compared over three
	// characters and `DAT_105a040c` over one, and the corpus holds neither's bytes. Carried empty,
	// which makes `Q_strnicmp(dir, "", n)` non-zero for any non-empty directory and so refuses
	// nothing — and none of this family's six call sites passes a directory either could match.
	const TCHAR* const GDirectoryRejectPrefix3 = TEXT("");   // DAT_105a0410
	const TCHAR* const GDirectoryRejectPrefix1 = TEXT("");   // DAT_105a040c
	// `LEA ECX,[EBP + 0x6]` at `101d0fb9`/`101d0fc4` — every hit is precached under the directory
	// MINUS its first six characters, which is the literal `"sound/"` all three call sites open
	// with. Recorded on the op rather than applied, because the op names the directory retail was
	// handed.
	constexpr int32 GDirectorySoundPrefixLength = 6;

	// --- The four request shapes, spelled once --------------------------------------------------
	//
	// Unit-prefixed because the module builds adaptive-unity and this anonymous namespace is
	// regularly merged with others.

}

// -------------------------------------------------------------------------------------------------
// The seam.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::VSoundGroupRowFor(const TCHAR* GroupName)
{
	// SEAM for `thunk_FUN_101f55a0(&DAT_1073dc28, this, group, 0)`. Nothing in this runtime parses
	// a VSound concept list (family Sounds10), so the table is empty and this takes retail's own
	// count-zero miss.
	(void)GroupName;
	return INDEX_NONE;
}

FString FElysiumNpc::CharTemplateModelName() const
{
	// SEAM for `FUN_10207e60`: `GetCharTemplate(this)` (`0x10207c40`) then the template's `+0x78`
	// (`0x101d4f20`). `+0x78` has no recovered column name and this runtime's template records
	// expose none, so this answers the empty string — which is also what retail precaches when the
	// template pointer is null (`DAT_106b8540`).
	return FString();
}

// -------------------------------------------------------------------------------------------------
// `CAI_BaseNPCTroika::Precache` — `0x10298ad0`, slot 104.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::PrecacheDirectory(const FString& Directory, const TCHAR* Extension,
	bool bStarPrefix, int32 Flag)
{
	// `0x101d0f10`, arm by arm.
	//
	// A null directory answers 0 (`TEST EBP,EBP / JZ`), and so does an EMPTY one (`REPNE SCASB /
	// NOT ECX / DEC ECX / JNZ` — the `DEC` sets ZF when the length is zero).
	if (Directory.IsEmpty())
	{
		return;
	}
	// The two reject prefixes. Both literals are unrecovered and carried empty, so neither matches;
	// see the `.inl`.
	if (*GDirectoryRejectPrefix3 != TEXT('\0')
		&& Directory.Left(3).Equals(GDirectoryRejectPrefix3, ESearchCase::IgnoreCase))
	{
		return;
	}
	if (*GDirectoryRejectPrefix1 != TEXT('\0')
		&& Directory.Left(1).Equals(GDirectoryRejectPrefix1, ESearchCase::IgnoreCase))
	{
		return;
	}
	// SEAM: `FindFirst("<dir>/*<ext>")` / `FindNext`, precaching every non-directory hit as
	// `"*%s/%s"` (or `"%s/%s"`) of `dir + 6` and the hit's name, with `Flag` as the precache flag.
	// This runtime enumerates no `sound/` tree at kernel level — the bake resolves a soundscript by
	// name and the corpus is not walked per NPC — so the walk finds nothing and the whole REQUEST is
	// recorded instead: the directory, the extension and both flags, which is every input retail's
	// own answer depends on.
	(void)GDirectorySoundPrefixLength;
	FPrecacheOp Op;
	Op.Channel = EPrecacheChannel::Directory;
	Op.Name = Directory;
	Op.Extension = Extension;
	Op.bStarPrefix = bStarPrefix;
	Op.Flag = Flag;
	IssuePrecache(Op);
}

void FElysiumNpc::TroikaPrecache()
{
	// `0x10298ad0`, in retail's order: the three writes (model fallback, disposition row, attack
	// coordinator). The precache requests between them are the bake's.
	//
	// The model keyfield is read through slot 9 `GetModelName` (vtable `+0x24`) three times to ask
	// one question: "is it unset or empty". This runtime carries it as `FElysiumEntity::Model`
	// (`ElysiumNpcKernelShapeMap` binds the same word), and slot 9 / slot 212 are still generated
	// `CBaseEntity` stubs that no story has verdicted — calling them would tally a stub and lose the
	// write, so the word is read and written directly with the slots named here.
	if (Model.IsEmpty())
	{
		// `SetModelName(-(uint)(s_models_error_error_mdl[0] != 0) & 0x105d90c4)` — vtable `+0x350`,
		// slot 212. The `-(x != 0) &` idiom yields a NULL pointer for an empty literal; the literal
		// is not empty, so the fallback is always the string.
		Model = GErrorModel;
	}

	// The acquisitions retail makes next are gone (0019/6, verdict `mechanism`, service `Bake`: this
	// runtime's assets are baked package references resolved at bake and load, never per NPC):
	// `PrecacheModel(model, 0)` and slot 10, `UTIL_PrecacheOther(m_altEquipment)` unless it is `"0"`
	// or `"item_w_unarmed"`, the direct call into `CAI_BaseNPC::Precache` (which only
	// precaches `m_spawnEquipment`), and the two `sound/character/<dialog>` globs (`.wav`, `.mp3`)
	// through `0x101d0f10`. What survives is every WORD the body writes that a later rule reads.

	// `*(int*)(this + 0x64e8) = thunk_FUN_100ec640(this)` — `CDispositionTable::PrecacheModel`
	// (`0x100ec640`) on the singleton at `0x10924980`. Retail walks its rows for this entity's model
	// index, and on a miss builds a new row: per stance, three `stance_%s_idle_%d`, three
	// `stance_%s_fidget_%d` and three-by-three `stance_%s_trans_%d_%d` sequence lookups, a missing
	// idle falling back to idle 1 and a missing fidget or transition to the matching idle. The row
	// index is the answer, and `+0x64e8` is `m_iDispositionModelIndex`.
	//
	// This runtime's CDispositionTable is `FElysiumNpc::EnsureStanceResolved`, and the shape map
	// binds `+0x64e8` to its cache key (`StanceResolvedFor`, "the port keys the row by name and
	// level, not by a table row index"). So the write IS that call: the stance clips and the
	// disposition tuning for this body's model are resolved here, at precache, exactly where retail
	// builds them. Headless (no embodiment) it answers false and leaves the key set, which is
	// retail's own "the model has no sequences" outcome.
	EnsureStanceResolved();

	// `(**(code **)(*(int *)this + 0x980))("Normal")` — vtable `+0x980` is slot 608,
	// `SetAttackCoordinator(const char*)` (family TroikaHelpers' `Slot608`). EVERY Troika NPC is
	// bound to the coordinator named `"Normal"` at precache, and the bind is what makes the
	// coordinator's five entry points reachable at all.
	Slot608(GNormalCoordinator);
}

// -------------------------------------------------------------------------------------------------
// Slot 104 `Precache` — the species prologue, then the Troika body.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::Precache()
{
	// Nineteen species classes override slot 104 on their C++ classes (story 5 step 3); each species
	// body calls `TroikaPrecache` first, retail's direct call into `0x10298ad0`. The three maker
	// bodies are their classes' overrides (story 5 fold A4), each chaining `CAI_BaseNPC::Precache`.
	//
	// Its caller is slot 103: `CAI_BaseNPCTroika::Spawn` (`0x10298d9c`, `TroikaSpawnBody`) and
	// `CNPC_VCamera::Spawn` (`0x10368b85`), live since story 8 (Spawn19). What runs is the body's
	// writes (the error-model name, the disposition index, the coordinator bind); every asset it
	// names is the bake's (`FElysiumMapActor::PreparePropAndWieldModels` holds the map epoch's).
	TroikaPrecache();
}
