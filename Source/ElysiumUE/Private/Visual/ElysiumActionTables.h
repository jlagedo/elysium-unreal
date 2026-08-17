#pragma once

#include "CoreMinimal.h"

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "Containers/UnrealString.h"
#include "Templates/Function.h"

// The recovered VtMB activity-translation tables, as project source (LIFE2).
//
// A translation table is a game *rule*, the same category as the `CGameMovement` constants in
// `ElysiumMoveSolve.h` and the compiled slot tables in `ElysiumSheetSlots.h`: it is written down
// once, reviewed, and maintained here. `research/tooling/gen_action_tables.py` produces the data
// from the pinned retail binary as owner-run archaeology; nothing in the build or the export reads
// `vampire.dll`, and no runtime path re-derives a row.
//
// The recovered behaviour is `docs/vtmb/animation_and_movers.md` A.3, and the design is
// `docs/architecture/animation-architecture.md` section 3.4. This header declares the shape; the
// generated data and its accessors live in `ElysiumWeaponActivityTables.cpp`, and the pure
// functions over them live in `ElysiumActionTables.cpp`.
//
// **Rows are never materialised.** Retail's `ActivityOverride` walks a weapon's ladder front to
// back and takes the first translated activity the model can play, so `Resolve` synthesizes one
// candidate per rung and tests it against the body's own clip vocabulary. Expanding the 9,214 rows
// would produce ~9,000 target strings with no consumer.
namespace ElysiumActionTables
{
	// One rung of a weapon's ladder: an ordered base sequence walked under one animation family.
	// The same base sequence backs many blocks — 110 blocks draw on 18 sequences — because block 1
	// is the weapon's own animation set, block 2 the shared class set and block 3 a cousin weapon,
	// and those share their base vocabulary.
	struct FActionBlock
	{
		const TCHAR* const* Bases = nullptr;
		int32 BaseCount = 0;
		// Positions in `Bases` carrying the authored `required` bit, ascending. Provenance only:
		// the pinned server translator never reads the flag, so a flagged row follows the same
		// path as any other.
		const int32* RequiredPositions = nullptr;
		int32 RequiredCount = 0;
		// The animation family this block decorates its bases with. Empty on the blocks that
		// translate a base to a literal and nothing else, where every row is an exception.
		const TCHAR* Family = nullptr;
	};

	// A literal target the block's family does not produce.
	struct FActionException
	{
		int32 Block = 0;
		const TCHAR* Base = nullptr;
		const TCHAR* Target = nullptr;
	};

	// One weapon class's whole ladder.
	struct FWeaponLadder
	{
		// The retail C++ class the table hangs off, which is the identity the RTTI decode recovers.
		const TCHAR* CppClass = nullptr;
		// The entity classnames a map's `additionalequipment`/`alternateequipment` can name
		// (`item_w_katana`). This is the key authored content actually spells.
		const TCHAR* const* EntityClassnames = nullptr;
		int32 EntityClassnameCount = 0;
		const FActionBlock* Blocks = nullptr;
		int32 BlockCount = 0;
		// Sorted by (Block, Base).
		const FActionException* Exceptions = nullptr;
		int32 ExceptionCount = 0;
	};

	// A rename replaces the whole base literal rather than decorating it.
	struct FActionRename
	{
		const TCHAR* Base = nullptr;
		const TCHAR* Prefix = nullptr;
	};

	// What the generator stored, so a malformed regeneration fails a test rather than a pose.
	struct FActionTableCensus
	{
		int32 WeaponClasses = 0;
		int32 BaseSequences = 0;
		int32 BaseEntries = 0;
		int32 Blocks = 0;
		int32 Exceptions = 0;
		int32 RequiredFlags = 0;
		// What the model must expand back into: the recovered row stream's length, its `required`
		// count, and an FNV-1a 64 digest of it. The digest is computed from the *retail decode* at
		// generation time and re-computed from the *committed model* by the round-trip test, so the
		// two can only agree if the compression is lossless.
		int32 RetailRows = 0;
		int32 RetailRequired = 0;
		uint64 RetailRowDigest = 0;
	};

	// --- the generated data -------------------------------------------------------------------
	TArrayView<const FWeaponLadder> WeaponLadders();
	TArrayView<const FActionRename> RenameRules();
	TArrayView<const TCHAR* const> SubstituteBases();
	const FActionTableCensus& Census();

	// --- the rules over it --------------------------------------------------------------------

	// Which shape a base takes under a family. Reported so a conformance run can show that no kind
	// is dead: a misdecoded rule shows up as a kind that resolves nothing.
	enum class ERewriteKind : uint8
	{
		// The block carries no family; the base translates to a literal or to itself.
		Identity,
		// `ACT_RUN` + `KATANA` -> `ACT_RUN_KATANA`. The ordinary case.
		Append,
		// The base's trailing token is a family slot the family replaces:
		// `ACT_SNEAKATTACK_..._BACK` + `KATANA` -> `ACT_SNEAKATTACK_..._KATANA`. The mirror case
		// `ACT_KNOCKBACK_..._BACK` is a literal direction and appends.
		Substitute,
		// The whole base is replaced: `ACT_AIM` -> `ACT_READY_<F>`.
		Rename,
		// The row states its target outright.
		Exception,
	};

	// `base` + `family` -> the translated activity. Pure, and the only place the recovered rewrite
	// is spelled.
	FString Rewrite(const FString& Base, const FString& Family);
	ERewriteKind KindOf(const FString& Base, const FString& Family);

	// The ladder for a retail class name, or for an entity classname a map can author. Null when
	// the class carries no table, which is the ordinary answer for 108 of the 169 subclasses and
	// means "translate nothing", exactly as an unarmed body does.
	const FWeaponLadder* FindLadder(const FString& CppClass);
	const FWeaponLadder* FindLadderByEntityClass(const FString& EntityClassname);

	// The literal a ladder states for one (block, base), or null.
	const TCHAR* FindException(const FWeaponLadder& Ladder, int32 Block, const FString& Base);

	// What one translation answered.
	struct FTranslation
	{
		// The translated activity, or the untranslated base when no rung could be played.
		FString Activity;
		bool bTranslated = false;
		// Which rung answered, counted over the rungs that declare this base rather than over every
		// block — a base absent from block 1 is not a rung the walk skipped, it is a rung the walk
		// never had. 1-based; 0 when nothing resolved.
		int32 Rung = 0;
		// How many rungs declared the base at all.
		int32 ApplicableRungs = 0;
		int32 Block = INDEX_NONE;
		ERewriteKind Kind = ERewriteKind::Identity;
		// The answering row's authored `required` bit. Reported, never acted on.
		bool bRequired = false;
	};

	// Retail's `ActivityOverride`, expressed over the ladder: walk front to back and take the first
	// translated activity `HasActivity` says the body can play. A miss returns the base untranslated
	// and `bTranslated == false`, which is the same answer retail's own empty table gives.
	FTranslation Translate(const FWeaponLadder& Ladder, const FString& Base,
		TFunctionRef<bool(const FString&)> HasActivity);

	// Every base a ladder can be asked for, in first-declared order.
	void CollectBases(const FWeaponLadder& Ladder, TArray<FString>& OutBases);

	// One expanded row, for the round-trip proof. Nothing in the runtime consumes these.
	struct FExpandedRow
	{
		FString CppClass;
		FString Base;
		FString Target;
		bool bRequired = false;
	};

	// The whole recovered stream, in the recovered walk order. Only the round-trip test calls this.
	void ExpandAll(TArray<FExpandedRow>& OutRows);
	// FNV-1a 64 over the expanded stream, byte for byte what the generator hashes.
	uint64 DigestOf(const TArray<FExpandedRow>& Rows);
}
