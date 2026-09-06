// How long one turn of a gait fan takes, between its spokes.
//
// A VtMB locomotion fan is nine clips on `move_yaw` and they do NOT share a length: the female
// bank's `walk` runs 1.500 s at -180 degrees, 1.333 s at -135 and 0.667 s at -90. So a heading
// between two spokes has to answer a length neither cell states, and WHICH mean is taken is a
// visible rule, not a rounding choice -- it sets the cycle rate every event window, every footfall
// and the mover's own ground speed are derived from.
//
// Retail blends the DURATIONS. `Studio_Duration` accumulates `Sum(w_i * (n_i - 1) / fps_i)` over the
// cells the parameter lands between (`docs/vtmb/animation_and_movers.md` -> "A fan's cycle is the
// weighted mean of its cells' DURATIONS"). Unreal 5.8 blends the play RATES and inverts the sum --
// `UBlendSpace::GetAnimationLengthFromSampleData` computes `1 / Sum(w_i / len_i)`, the harmonic
// mean -- unless the asset carries `bUseLegacySamplePointAnimationLengthCalculations`, whose branch
// (`BlendSpace.cpp:2037`) is retail's formula exactly.
//
// The two agree wherever the cells agree, and only there. They are furthest apart at the cell
// boundaries that matter most: on the female `walk` fan at `move_yaw = -120` degrees, two thirds of
// the way from the 1.333 s cell toward the 0.667 s one, the weighted mean is 1.111 s and the
// harmonic mean is 1.000 s -- a 10 percent error in the cycle rate on an ordinary strafing heading.
//
// **This is a bake assertion, not a runtime one.** Nothing here proposes that the runtime carry a
// VtMB rule: the engine already owns both formulas and the bake picks which one the asset declares
// (root `CLAUDE.md` -> "Poses are baked native"). So the measurement is taken at the seam the
// animation graph reads -- `UpdateBlendSamples` for the weights, `GetAnimationLengthFromSampleData`
// for the answer -- and the expectation is rebuilt independently from the sidecar the bake read.
//
// **The expected value is rebuilt from the sidecar, never from the asset.** `blends/<owner>.json`
// states each cell's `motion.cycle_seconds`, which is `(numframes - 1) / fps` as the decoder read it
// off the model. Deriving the expectation from the baked sequences instead would let a bake that
// wrote every clip at the wrong length agree with itself. The two are cross-checked per sample and
// a disagreement is its own named failure, so a length defect cannot be read as a blend defect.
//
// **Nine positions, six on the spokes and three between them.** A fan sampled only on its cells
// cannot distinguish the two rules at all: where one sample holds the whole weight both formulas
// return that sample's own length. The three interior positions are the assertion; the six spokes
// are the control that says the fan is wired to the cells the sidecar names.
//
// Scoped to the fans whose cells DISAGREE about length, because those are the only ones where the
// two means differ. The fans whose cells agree are counted and reported -- they are covered by the
// spoke positions and they cannot fail this assertion, and saying so keeps the coverage number
// honest.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumNpcClips.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"
#include "Animation/BlendSpace.h"
#include "Misc/PackageName.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumFanDurationFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The axis a gait fan is steered by. A grid on `aim_yaw`/`aim_pitch` is a pose fan whose cells
	// carry no authored displacement and no cycle to blend, so it is not in scope here.
	const TCHAR* const GGaitAxis = TEXT("move_yaw");

	// How close the asset has to land on retail's weighted mean. A tenth of a millisecond is two
	// orders below the ~110 ms the harmonic mean is off by at the worst interior position, and well
	// above float accumulation over nine cells.
	constexpr double GDurationTolerance = 1e-3;

	// How many diverging fans are reported cell by cell before the sweep reports only its totals.
	// The count of every fan examined and every fan that diverged is always stated, so the cap
	// bounds the transcript and never the coverage.
	constexpr int32 GMaxReportedFans = 4;

	// One position across a fan, with both candidate answers beside what the asset actually said.
	struct FFanSample
	{
		double MoveYaw = 0.0;
		// What `UBlendSpace` answers through the pair the animation graph calls.
		double Engine = 0.0;
		// Retail: the weighted mean of the cells' own durations.
		double Weighted = 0.0;
		// Unreal's default derivation: the inverse of the weighted mean of the cells' play rates.
		double Harmonic = 0.0;
		// The blend weights this position resolved to, summed. Anything but 1 means the position
		// fell outside the triangulation and neither mean is meaningful there.
		double TotalWeight = 0.0;
		int32 CellsBlended = 0;
		bool bOnSpoke = false;

		double Error() const { return FMath::Abs(Engine - Weighted); }
	};

	// The span between two adjacent cells whose lengths differ most, as the index of its lower cell.
	// Every fan in scope has cells that disagree, so this span always exists -- and it is the only
	// place a fan is guaranteed to tell the two means apart, because the two agree exactly wherever
	// the cells they blend agree.
	int32 WidestSpan(const FElysiumBlendGrid& Grid)
	{
		int32 Widest = 0;
		double WidestGap = -1.0;
		for (int32 Index = 0; Index + 1 < Grid.GroupSize[0]; ++Index)
		{
			const FElysiumBlendCell* Low = Grid.CellAt(Index, 0);
			const FElysiumBlendCell* High = Grid.CellAt(Index + 1, 0);
			if (Low == nullptr || High == nullptr)
			{
				continue;
			}
			const double Gap = FMath::Abs(
				static_cast<double>(Low->Motion.CycleSeconds)
				- static_cast<double>(High->Motion.CycleSeconds));
			if (Gap > WidestGap)
			{
				WidestGap = Gap;
				Widest = Index;
			}
		}
		return Widest;
	}

	// One diverging fan, held until the sweep is over so the fans reported in full are the WORST
	// ones rather than whichever owner sorts first. A fan costs nine samples to hold, so keeping
	// every one of the corpus's ~200 is cheaper than a second pass over the mount.
	struct FFanReport
	{
		FString Owner;
		FString Label;
		int32 CellCount = 0;
		double ShortestCell = 0.0;
		double LongestCell = 0.0;
		int32 DivergedPositions = 0;
		int32 WorstIndex = 0;
		TArray<FFanSample> Row;

		double WorstError() const
		{
			return Row.IsValidIndex(WorstIndex) ? Row[WorstIndex].Error() : 0.0;
		}
	};

	// Nine `move_yaw` positions across one fan, in CELL coordinates: six on the cells themselves and
	// three between them, at a third, a half and a half of a span.
	//
	// Expressed against the fan's own cell count rather than as nine literal degrees, so the shape
	// of the sampling survives a fan that is not the corpus's uniform nine. Every gait fan shipped
	// today is 9x1 over -180..180, which puts the first interior sample at exactly -120 degrees --
	// two thirds of the way from the female `walk` fan's 1.333 s cell toward its 0.667 s one, and
	// the position where its two means are furthest apart.
	//
	// The THIRD interior sample is placed on the fan's own widest span rather than at a fixed
	// coordinate. Two fixed interior positions leave the discrimination to where a fan happens to
	// put its short cells: the two means agree exactly between two cells of equal length, so a
	// sample landing there passes for either rule and proves nothing. Aiming one sample at the
	// widest gap puts the fan's best available evidence in every row.
	TArray<double> SampleCells(const FElysiumBlendGrid& Grid)
	{
		const double Last = static_cast<double>(Grid.GroupSize[0] - 1);
		const double Mid = FMath::FloorToDouble(Last * 0.5);
		TArray<double> Cells;
		// AddUnique on exact values: on a short fan several of these collapse onto the same cell,
		// and sampling one position twice would inflate the coverage count without covering more.
		Cells.AddUnique(0.0);
		Cells.AddUnique(FMath::Min(1.0, Last));
		Cells.AddUnique(FMath::Min(2.0, Last));
		Cells.AddUnique(Mid);
		Cells.AddUnique(FMath::Max(Last - 1.0, 0.0));
		Cells.AddUnique(Last);
		Cells.AddUnique(FMath::Min(1.0 + 1.0 / 3.0, Last));
		Cells.AddUnique(FMath::Min(Mid + 0.5, Last));
		Cells.AddUnique(FMath::Min(static_cast<double>(WidestSpan(Grid)) + 0.5, Last));
		Cells.Sort();
		return Cells;
	}

	// Whether this grid is a gait fan: one axis, steered by `move_yaw`, more than one cell, and
	// every cell carrying an authored cycle. The last is what makes the expectation rebuildable --
	// a cell with no `motion` states no length, and a fan holding one cannot be scored at all.
	bool IsGaitFan(const FElysiumBlendGrid& Grid, const FElysiumBlendTable& Table)
	{
		const FElysiumPoseParamDesc* Axis = Table.Param(Grid.ParamIndex[0]);
		if (Axis == nullptr || Axis->Name != GGaitAxis)
		{
			return false;
		}
		if (!Grid.IsMultiCell() || Grid.GroupSize[0] < 2)
		{
			return false;
		}
		if (Grid.GroupSize[1] > 1 && Grid.ParamIndex[1] != INDEX_NONE)
		{
			// A two-axis grid blends over a face rather than a segment, so its weights come from a
			// triangle and the "between two spokes" sampling below does not describe it.
			return false;
		}
		for (const FElysiumBlendCell& Cell : Grid.Cells)
		{
			if (Cell.Clip.IsEmpty() || !(Cell.Motion.CycleSeconds > 0.f))
			{
				return false;
			}
		}
		return true;
	}

	// The spread between the longest and shortest cell. Zero means the two means cannot disagree on
	// this fan whatever the asset declares.
	void CellLengthRange(const FElysiumBlendGrid& Grid, double& OutLow, double& OutHigh)
	{
		OutLow = static_cast<double>(Grid.Cells[0].Motion.CycleSeconds);
		OutHigh = OutLow;
		for (const FElysiumBlendCell& Cell : Grid.Cells)
		{
			OutLow = FMath::Min(OutLow, static_cast<double>(Cell.Motion.CycleSeconds));
			OutHigh = FMath::Max(OutHigh, static_cast<double>(Cell.Motion.CycleSeconds));
		}
	}

	// The baked asset for one grid, addressed the way `ElysiumNpcVisual::LoadBakedBlendSpace` does --
	// the bank namespace first, then the owner's own -- but without a body to hang it on, because
	// this sweep is over what the bake wrote rather than over what a particular character can play.
	UBlendSpace* LoadFan(const FString& Owner, const FString& Label)
	{
		const FString BankPath = ElysiumCharacterAssets::AnimationPath(Owner, Label, true);
		if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(BankPath)))
		{
			return LoadObject<UBlendSpace>(nullptr, *BankPath);
		}
		const FString OwnPath = ElysiumCharacterAssets::AnimationPath(Owner, Label, true);
		if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(OwnPath)))
		{
			return LoadObject<UBlendSpace>(nullptr, *OwnPath);
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFanDurationTest,
	"Elysium.Content.FanDuration", GElysiumFanDurationFlags)
bool FElysiumFanDurationTest::RunTest(const FString&)
{
	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no native character cast (run: uv run elysium import characters)"));
		return true;
	}

	FElysiumNpcIndex Index;
	FString IndexError;
	if (!ElysiumNativeTest::Load(Index, IndexError) || !Index.IsValid())
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no native cast view (%s) ")
			TEXT("(run: uv run elysium import characters)"), *IndexError));
		return true;
	}

	// Every owner that declares a sidecar, each visited once. A bank is its own owner here rather
	// than a property of the bodies that resolve it, so the two shared `move_and_ranged` banks --
	// which own most of the cast's locomotion -- are scored once instead of ~1,400 times.
	TMap<FString, FString> BlendsByOwner;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Entry : Index.Npcs)
	{
		if (!Entry.Value.Blends.IsEmpty())
		{
			BlendsByOwner.Add(Entry.Key, Entry.Value.Blends);
		}
	}
	for (const TPair<FString, FElysiumNpcIndexEntry>& Entry : Index.Banks)
	{
		if (!Entry.Value.Blends.IsEmpty())
		{
			BlendsByOwner.Add(Entry.Key, Entry.Value.Blends);
		}
	}
	TArray<FString> Owners;
	BlendsByOwner.GetKeys(Owners);
	Owners.Sort([](const FString& A, const FString& B) { return A < B; });

	int32 GaitFans = 0;
	int32 UniformFans = 0;
	int32 VaryingFans = 0;
	int32 ScoredFans = 0;
	int32 DivergedFans = 0;
	// Fans where at least one sampled heading blends cells of DIFFERENT length, which is the only
	// kind of heading at which the weighted and harmonic means can disagree. A scored fan that is
	// not one of these is scored and not tested, and is named rather than counted as coverage.
	int32 DiscriminatingFans = 0;
	TArray<FString> BlindFans;
	int32 Positions = 0;
	int32 DivergedPositions = 0;
	int32 UnmountedFans = 0;
	int32 ClipLengthFaults = 0;
	int32 ReportedClipLengthFaults = 0;

	// Every diverging fan, reported after the sweep worst first.
	TArray<FFanReport> Diverging;

	for (const FString& Owner : Owners)
	{
		FElysiumBlendTable Table;
		FString TableError;
		if (!ElysiumNativeTest::Load(Table, BlendsByOwner[Owner], TableError))
		{
			// A sidecar the index names and the export did not write is a gap in the export, not in
			// the bake, and the parity test already owns that report.
			AddInfo(FString::Printf(TEXT("%s: no blends sidecar (%s)"), *Owner, *TableError));
			continue;
		}

		TArray<FString> Labels;
		Table.Grids.GetKeys(Labels);
		Labels.Sort([](const FString& A, const FString& B) { return A < B; });

		for (const FString& Label : Labels)
		{
			const FElysiumBlendGrid& Grid = Table.Grids[Label];
			if (!IsGaitFan(Grid, Table))
			{
				continue;
			}
			++GaitFans;
			double ShortestCell = 0.0;
			double LongestCell = 0.0;
			CellLengthRange(Grid, ShortestCell, LongestCell);
			if (LongestCell - ShortestCell <= GDurationTolerance)
			{
				// Every cell the same length: both means return that length, so this fan cannot
				// tell the two rules apart and asserting on it would read as coverage it is not.
				++UniformFans;
				continue;
			}
			++VaryingFans;

			TStrongObjectPtr<UBlendSpace> Space(LoadFan(Owner, Label));
			if (!Space.IsValid())
			{
				// The sidecar declares a multi-cell fan and the mount carries no space for it, so
				// every label naming this grid collapses onto one cell and blends nothing at all.
				++UnmountedFans;
				AddError(FString::Printf(
					TEXT("%s fan '%s': the sidecar declares %d cells of differing length and no ")
					TEXT("blend space is on the mount, so nothing blends their durations"),
					*Owner, *Label, Grid.Cells.Num()));
				continue;
			}

			const FBlendParameter& Axis = Space->GetBlendParameter(0);
			const int32 CellCount = Grid.GroupSize[0];
			const double Step = CellCount > 1
				? (static_cast<double>(Axis.Max) - static_cast<double>(Axis.Min))
					/ static_cast<double>(CellCount - 1)
				: 0.0;
			if (!(Step > 0.0))
			{
				AddError(FString::Printf(
					TEXT("%s fan '%s': axis 0 spans %.3f..%.3f over %d cells, which is no range to ")
					TEXT("blend across"), *Owner, *Label, Axis.Min, Axis.Max, CellCount));
				continue;
			}

			const TArray<double> Cells = SampleCells(Grid);
			TArray<FFanSample> Row;
			Row.Reserve(Cells.Num());
			bool bFanScorable = true;

			for (double Cell : Cells)
			{
				FFanSample Sample;
				Sample.MoveYaw = static_cast<double>(Axis.Min) + Cell * Step;
				Sample.bOnSpoke = FMath::IsNearlyEqual(Cell, FMath::RoundToDouble(Cell), 1e-6);

				// The pair the animation graph itself calls. `UpdateBlendSamples` is what
				// `FAnimNode_BlendSpacePlayer` reaches through `TickAssetPlayer`, and the length is
				// read off its cache by the same function the node uses. The cache starts empty and
				// the step is large so that an asset carrying target-weight interpolation settles
				// on its steady state rather than reporting a first-frame ramp.
				TArray<FBlendSampleData> Blend;
				int32 TriangulationIndex = 0;
				if (!Space->UpdateBlendSamples(
					FVector(Sample.MoveYaw, 0.0, 0.0), 1000.f, Blend, TriangulationIndex))
				{
					AddError(FString::Printf(
						TEXT("%s fan '%s': move_yaw %.2f deg resolved no sample at all, so the ")
						TEXT("triangulation does not cover the range the axis declares"),
						*Owner, *Label, Sample.MoveYaw));
					bFanScorable = false;
					break;
				}
				Sample.Engine = static_cast<double>(Space->GetAnimationLengthFromSampleData(Blend));

				double InverseSum = 0.0;
				const TArray<FBlendSample>& Placed = Space->GetBlendSamples();
				for (const FBlendSampleData& Data : Blend)
				{
					if (!Placed.IsValidIndex(Data.SampleDataIndex)
						|| Placed[Data.SampleDataIndex].Animation == nullptr)
					{
						continue;
					}
					const FBlendSample& Placement = Placed[Data.SampleDataIndex];
					// Which cell this sample IS, from where the bake put it on the axis. Addressed
					// by position rather than by clip name because a wrapping fan duplicates its
					// endpoint clip at both ends, so a name matches two cells.
					const int32 CellIndex = FMath::RoundToInt32(
						(static_cast<double>(Placement.SampleValue.X) - static_cast<double>(Axis.Min))
						/ Step);
					const FElysiumBlendCell* Declared = Grid.CellAt(CellIndex, 0);
					if (Declared == nullptr || !(Declared->Motion.CycleSeconds > 0.f))
					{
						AddError(FString::Printf(
							TEXT("%s fan '%s': a sample sits at %.3f on axis 0, which is cell %d, ")
							TEXT("and the sidecar declares no cycle there -- the asset carries a ")
							TEXT("sample the grid does not"),
							*Owner, *Label, Placement.SampleValue.X, CellIndex));
						bFanScorable = false;
						break;
					}

					const double DeclaredSeconds = static_cast<double>(Declared->Motion.CycleSeconds);
					const double Weight = static_cast<double>(Data.GetClampedWeight());
					Sample.Weighted += Weight * DeclaredSeconds;
					InverseSum += Weight / DeclaredSeconds;
					Sample.TotalWeight += Weight;
					++Sample.CellsBlended;

					// The other half of the contract, and a separate defect: the sequence the bake
					// wrote has to be as long as the sidecar says its cell is, or the weighted mean
					// above is measured against a length the asset never plays.
					const double Baked = static_cast<double>(Placement.GetSamplePlayLength());
					if (FMath::Abs(Baked - DeclaredSeconds) > GDurationTolerance)
					{
						++ClipLengthFaults;
						if (ReportedClipLengthFaults < GMaxReportedFans)
						{
							++ReportedClipLengthFaults;
							AddError(FString::Printf(
								TEXT("%s fan '%s' cell %d ('%s'): the sidecar states %.4f s and the ")
								TEXT("baked sequence plays %.4f s, so the cell's own length is wrong ")
								TEXT("before any blend is taken"),
								*Owner, *Label, CellIndex, *Declared->Clip, DeclaredSeconds, Baked));
						}
					}
				}
				if (!bFanScorable)
				{
					break;
				}
				Sample.Harmonic = InverseSum > 0.0 ? 1.0 / InverseSum : 0.0;

				if (Sample.CellsBlended == 0
					|| !FMath::IsNearlyEqual(Sample.TotalWeight, 1.0, 1e-3))
				{
					AddError(FString::Printf(
						TEXT("%s fan '%s': move_yaw %.2f deg blends %d cell(s) to a total weight of ")
						TEXT("%.5f -- a mean over weights that do not sum to one is not a duration"),
						*Owner, *Label, Sample.MoveYaw, Sample.CellsBlended, Sample.TotalWeight));
					bFanScorable = false;
					break;
				}
				Row.Add(Sample);
			}

			if (!bFanScorable)
			{
				continue;
			}
			++ScoredFans;

			int32 DivergedHere = 0;
			bool bDiscriminates = false;
			const FFanSample* Worst = nullptr;
			for (const FFanSample& Sample : Row)
			{
				++Positions;
				if (Sample.Error() > GDurationTolerance)
				{
					++DivergedPositions;
					++DivergedHere;
				}
				// Whether this position can tell the two rules apart at all. Where the cells it
				// blends are the same length both means return that length, so the assertion there
				// passes for either rule and proves nothing -- a fan with no discriminating
				// position is carried by the sweep's count without being covered by it.
				if (FMath::Abs(Sample.Weighted - Sample.Harmonic) > GDurationTolerance)
				{
					bDiscriminates = true;
				}
				if (Worst == nullptr || Sample.Error() > Worst->Error())
				{
					Worst = &Sample;
				}
			}
			if (bDiscriminates)
			{
				++DiscriminatingFans;
			}
			else
			{
				BlindFans.Add(FString::Printf(TEXT("%s '%s'"), *Owner, *Label));
			}
			if (DivergedHere == 0)
			{
				continue;
			}
			++DivergedFans;

			FFanReport& Report = Diverging.AddDefaulted_GetRef();
			Report.Owner = Owner;
			Report.Label = Label;
			Report.CellCount = Grid.Cells.Num();
			Report.ShortestCell = ShortestCell;
			Report.LongestCell = LongestCell;
			Report.DivergedPositions = DivergedHere;
			Report.WorstIndex = static_cast<int32>(Worst - Row.GetData());
			Report.Row = MoveTemp(Row);
		}
	}

	if (GaitFans == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no gait fan in any exported blends sidecar ")
			TEXT("(run: uv run elysium import characters, then uv run elysium import characters)"));
		return true;
	}

	// Worst first, so the fans printed in full are the ones carrying the largest error rather than
	// the ones whose owner sorts first alphabetically. Ties fall back to the name, so the order is
	// stable across runs.
	Diverging.Sort([](const FFanReport& A, const FFanReport& B)
	{
		if (!FMath::IsNearlyEqual(A.WorstError(), B.WorstError(), UE_DOUBLE_KINDA_SMALL_NUMBER))
		{
			return A.WorstError() > B.WorstError();
		}
		return A.Owner == B.Owner ? A.Label < B.Label : A.Owner < B.Owner;
	});
	const int32 ReportedFans = FMath::Min(Diverging.Num(), GMaxReportedFans);
	for (int32 Rank = 0; Rank < ReportedFans; ++Rank)
	{
		const FFanReport& Report = Diverging[Rank];
		const FFanSample& Worst = Report.Row[Report.WorstIndex];
		AddError(FString::Printf(
			TEXT("%s fan '%s': %d of %d sampled headings blend the cells' durations wrongly; ")
			TEXT("worst at move_yaw %.2f deg, where the asset answers %.4f s and retail's ")
			TEXT("weighted mean of the cells is %.4f s (the harmonic mean is %.4f s)"),
			*Report.Owner, *Report.Label, Report.DivergedPositions, Report.Row.Num(),
			Worst.MoveYaw, Worst.Engine, Worst.Weighted, Worst.Harmonic));
		AddInfo(FString::Printf(
			TEXT("%s fan '%s': %d cells, %.4f s to %.4f s"), *Report.Owner, *Report.Label,
			Report.CellCount, Report.ShortestCell, Report.LongestCell));
		AddInfo(TEXT("    move_yaw     asset   retail (weighted)   unreal (harmonic)  cells"));
		for (const FFanSample& Sample : Report.Row)
		{
			AddInfo(FString::Printf(
				TEXT("  %s%8.2f  %8.4f s        %8.4f s          %8.4f s      %d%s"),
				Sample.bOnSpoke ? TEXT(" ") : TEXT("*"), Sample.MoveYaw, Sample.Engine,
				Sample.Weighted, Sample.Harmonic, Sample.CellsBlended,
				Sample.Error() > GDurationTolerance ? TEXT("  <-- diverges") : TEXT("")));
		}
		AddInfo(TEXT("  (* is a heading between two spokes, which is where the rules differ)"));
	}

	AddInfo(FString::Printf(
		TEXT("%d gait fan(s) across %d owner(s): %d have cells of differing length and %d do not; ")
		TEXT("%d scored over %d sampled headings, %d fan(s) not on the mount"),
		GaitFans, Owners.Num(), VaryingFans, UniformFans, ScoredFans, Positions, UnmountedFans));
	AddInfo(FString::Printf(
		TEXT("%d of %d scored fan(s) were sampled at a heading that blends cells of differing ")
		TEXT("length, which is where the two rules can differ at all"),
		DiscriminatingFans, ScoredFans));
	if (!BlindFans.IsEmpty())
	{
		// Not an error, and not a sampling weakness: on these fans no two ADJACENT cells differ
		// enough for the two means to separate by the tolerance anywhere on the fan, so no position
		// could discriminate. Named rather than folded into the pass, because a count alone reads
		// as coverage.
		BlindFans.Sort([](const FString& A, const FString& B) { return A < B; });
		AddInfo(FString::Printf(
			TEXT("%d fan(s) no sampled heading can discriminate on: %s"),
			BlindFans.Num(), *FString::Join(BlindFans, TEXT(", "))));
	}

	if (ClipLengthFaults > ReportedClipLengthFaults)
	{
		AddError(FString::Printf(
			TEXT("%d baked cell(s) disagree with the sidecar about their own length; %d reported ")
			TEXT("above"), ClipLengthFaults, ReportedClipLengthFaults));
	}

	if (DivergedFans > 0)
	{
		AddError(FString::Printf(
			TEXT("%d of %d scored gait fan(s) answer a blended duration that is not the weighted ")
			TEXT("mean of their cells' durations, over %d of %d sampled headings; %d reported in ")
			TEXT("full above. Worst: %s '%s' at move_yaw %.2f deg, %.4f s against %.4f s. Retail ")
			TEXT("blends the durations (`Studio_Duration`); UE 5.8 blends the play rates unless the ")
			TEXT("asset declares `bUseLegacySamplePointAnimationLengthCalculations`"),
			DivergedFans, ScoredFans, DivergedPositions, Positions, ReportedFans,
			*Diverging[0].Owner, *Diverging[0].Label,
			Diverging[0].Row[Diverging[0].WorstIndex].MoveYaw,
			Diverging[0].Row[Diverging[0].WorstIndex].Engine,
			Diverging[0].Row[Diverging[0].WorstIndex].Weighted));
	}
	else if (ScoredFans > 0)
	{
		AddInfo(FString::Printf(
			TEXT("every one of %d scored gait fan(s) blends its cells' durations to within %.4f s"),
			ScoredFans, GDurationTolerance));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
