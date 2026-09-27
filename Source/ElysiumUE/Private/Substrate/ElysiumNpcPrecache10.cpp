#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"

#include "ElysiumEntityDefs.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **Precache10** — slot 104 `Precache`: the `CAI_BaseNPC` base body, the
// `CAI_BaseNPCTroika` body that owns the slot, and its species arms. The three `CNPCMaker*` bodies
// are in `Substrate/ElysiumNpcMaker.cpp`, on the separate type this port models a maker as; the
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
	// --- The words `0x1027bb50` and `0x10298ad0` read -----------------------------------------
	// `s_models_error_error_mdl_105d90c4` — the Troika body's model fallback.
	const TCHAR* const GErrorModel = TEXT("models/error/error.mdl");
	// `s_item_w_unarmed_1055f6bc`, the 15-byte compare (14 characters plus the NUL) the Troika body
	// runs on `m_altEquipment` after the sentinel test.
	const TCHAR* const GUnarmedItem = TEXT("item_w_unarmed");
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

FString FElysiumNpc::DialogueSoundDirectory(const FString& Dialog)
{
	// `Q_snprintf(buf, 0x104, "sound/character/%s", m_iDialog)` — `s_sound_character__s_105622e4`,
	// spelled at the call site because `FString::Printf` requires a literal format.
	FString Buffer = FString::Printf(TEXT("sound/character/%s"), *Dialog);

	// `buf[strlen(buf) - 4] = 0`, read off the listing at `10298bf8`..`10298c04`:
	//
	//     LEA EDI,[ESP+0x1c]      ; EDI = buf
	//     OR ECX,-1 / XOR EAX,EAX / REPNE SCASB / NOT ECX / DEC ECX     ; ECX = strlen(buf)
	//     LEA EDX,[ESP+0x1c] / SUB EDX,0x4                              ; EDX = buf - 4
	//     MOV byte ptr [ECX + EDX*1],AL                                 ; buf[strlen - 4] = 0
	//
	// **FOUR** characters, not the five the checklist's walk claims. For `m_iDialog` = `"foo.dlg"`
	// the directory becomes `"sound/character/foo"` — the extension and the dot come off, which is
	// what makes the glob land on the character's own conversation directory.
	//
	// DIVERGENCE, named: a dialog name shorter than four characters makes `strlen(buf) - 4` index
	// BEFORE the buffer, and retail writes a NUL over its own stack. This port clamps at zero and
	// answers the empty directory, which `PrecacheDirectory` then refuses on its own empty-name arm.
	// The prefix is 16 characters long, so `strlen(buf)` is at least 16 for any dialog name at all
	// and the clamp is unreachable for every authored `dialogname` — it exists so a fixture cannot
	// corrupt the heap, not to change an answer.
	const int32 Chop = Buffer.Len() - 4;
	Buffer = Chop > 0 ? Buffer.Left(Chop) : FString();

	// `Q_strnlwr(buf, strlen(buf))`, recomputed AFTER the chop.
	return Buffer.ToLower();
}

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
	// `0x10298ad0`, in retail's order.
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

	// `PrecacheModel(model, 0)` and the returned index to slot 10 `SetModelIndex` (vtable `+0x28`).
	// The index is the engine's; this runtime has no model-index space, so nothing is stored and the
	// slot is named rather than dispatched — slot 10 is a generated `CBaseEntity` stub too.
	NpcKernelPrecache10Shared::Precache10Model(*this, *Model, /*Preload=*/0);

	// `m_altEquipment` (`+0x1a98`, this runtime's `AlternateEquipment`): precached unless it is the
	// sentinel `"0"` or the literal `"item_w_unarmed"`. TWO exclusions here where the base body has
	// one, and the unarmed exclusion is exact (a 15-byte compare, the string plus its NUL).
	if (!AlternateEquipment.IsEmpty()
		&& AlternateEquipment != NpcKernelPrecache10Shared::GNoneSentinel
		&& AlternateEquipment != GUnarmedItem)
	{
		NpcKernelPrecache10Shared::Precache10Other(*this, AlternateEquipment);
	}

	// `CAI_BaseNPC::thunk_FUN_1027bb50(this)` — a DIRECT call with no argument (`MOV ECX,EBX /
	// CALL 0x1000eaf7`; the decompiler's `param_1` is the dead `EAX` of the previous call).
	FElysiumNpcBase::Precache();

	// `m_iDialog` (`+0x0128`, this runtime's `DialogName`): the conversation directory, globbed
	// twice — `.wav` first, then `.mp3`. Both with `bStarPrefix` SET and the precache flag 0, which
	// is the opposite pair from `CNPC_VWerewolf`'s two calls to the same function.
	const FString Dialog = DialogName;
	if (!Dialog.IsEmpty())
	{
		const FString Directory = DialogueSoundDirectory(Dialog);
		PrecacheDirectory(Directory, NpcKernelPrecache10Shared::GExtWav, /*bStarPrefix=*/true, /*Flag=*/0);
		PrecacheDirectory(Directory, NpcKernelPrecache10Shared::GExtMp3, /*bStarPrefix=*/true, /*Flag=*/0);
	}

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
	// bodies are `FElysiumNpcMaker::Precache`'s.
	//
	// NOTHING IN THIS RUNTIME CALLS `Precache()` YET, and that is deliberate rather than an
	// oversight: this substrate acquires assets for the whole map epoch before any NPC stands
	// (`FElysiumMapActor::PreparePropAndWieldModels`), so wiring a per-entity precache into
	// `Spawn` would ADD an event retail's order does not have here. The body is ported whole and is
	// driven by `Elysium.Substrate.NpcKernelPrecache10.*`; the caller lands with the asset path.
	TroikaPrecache();
}
