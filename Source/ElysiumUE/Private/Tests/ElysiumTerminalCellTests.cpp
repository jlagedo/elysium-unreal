// The terminal cell painter.
//
// Every automation tier runs `-nullrhi`, so nothing here renders a frame: the assertions are on the
// pure `ElysiumTerminalPaint::BuildDrawPlan`, which is the whole picture — metrics, palette, the
// style-bit runs, one glyph per non-space cell and the blinking block cursor — computed outside
// `OnPaint` precisely so it can be asserted at all. The rasterized result (the grid crisp inside the
// bezel at 1080p and 4K, and the calibration pattern read back off the render target) is Play-tier
// and owner-piloted.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumViewState.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "UI/ElysiumUIStyle.h"
#include "UI/SElysiumTerminalCells.h"

namespace ElysiumTerminalCellTests
{
static constexpr EAutomationTestFlags GCellFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The retail grid on the project's render target: 36x24 cells, every one cleared to
// `style | ' '`, which is exactly what `FElysiumTerminalScreenBuffer::Clear` leaves behind.
static FElysiumTerminalView GridView()
{
	FElysiumTerminalView View;
	View.Owner = FElysiumEntityHandle(5, 1);
	View.Columns = 36;
	View.Rows = 24;
	View.Cells.Init(static_cast<uint16>(0x80 | TCHAR(' ')), View.Columns * View.Rows);
	return View;
}

static void PutText(FElysiumTerminalView& View, int32 Column, int32 Row, const TCHAR* Text,
	bool bStyleSet = true)
{
	for (int32 Offset = 0; Text[Offset] != TCHAR('\0'); ++Offset)
	{
		const uint16 Style = bStyleSet ? 0x80 : 0;
		View.Cells[Row * View.Columns + Column + Offset] =
			static_cast<uint16>(static_cast<uint16>(Text[Offset]) | Style);
	}
}

// The project's render target: retail's 512x512 client texture at 2x. `FUN_100c77f0` refuses any
// other shape (`0x100c7806` / `0x100c7818` both compare `0x200`).
static const FVector2D Surface1024Square(1024.0, 1024.0);
}

using namespace ElysiumTerminalCellTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalCellMetricsTest,
	"Elysium.Substrate.Terminal.CellMetrics", GCellFlags)
bool FElysiumTerminalCellMetricsTest::RunTest(const FString&)
{
	using namespace ElysiumTerminalPaint;

	// The retail grid where retail's own rasterizer puts it, at 2x. `FUN_100c77f0` stamps a fixed
	// 14x16 glyph cell and centres the `columns*14 x rows*16` block in the 512x512 texture
	// (`0x100c7870` y, `0x100c7884` x): (4, 64) for 36x24. Doubled onto a 1024x1024 target that is
	// a 28x32 cell, a 1008x768 block and an origin of (8, 128) — which is the window the models'
	// `screen` UVs were cut against.
	const FCellMetrics Retail = MetricsFor(Surface1024Square, 36, 24);
	TestTrue(TEXT("the 36x24 grid on a 1024x1024 target is drawable"), Retail.bValid);
	TestEqual(TEXT("cell width is retail's 14 at 2x"), Retail.CellWidth, 28);
	TestEqual(TEXT("cell height is retail's 16 at 2x"), Retail.CellHeight, 32);
	TestEqual(TEXT("the text block is 1008 wide"), Retail.BlockWidth(), 1008);
	TestEqual(TEXT("and 768 tall"), Retail.BlockHeight(), 768);
	TestEqual(TEXT("centred as retail centres it, so the x origin is 2*4"), Retail.OriginX, 8);
	TestEqual(TEXT("and the y origin is 2*64"), Retail.OriginY, 128);
	TestTrue(TEXT("cell (0,0) sits at the origin"),
		Retail.TopLeftOf(0, 0).Equals(FVector2f(8.0f, 128.0f)));
	TestTrue(TEXT("and the last cell at the far corner of the block"),
		Retail.TopLeftOf(35, 23).Equals(FVector2f(8.0f + 35 * 28, 128.0f + 23 * 32)));
	// The block is exactly the sub-window of the texture the mesh samples: U 0.0078..0.9922,
	// V 0.125..0.875. Painting 24 rows over the whole target instead put rows 0-2 and 21-23 outside
	// it, which is the 18-of-24 crop the owner saw on `sp_tutorial_1`.
	TestEqual(TEXT("the block's V window starts an eighth down the texture"),
		Retail.OriginY * 8, 1024);
	TestEqual(TEXT("and ends an eighth up from the bottom"),
		Retail.OriginY + Retail.BlockHeight(), 1024 - 128);

	// sm_bailbonds_1's `apple_monitor_screen` authors a REAL 33x23, inside the clamp, and shares the
	// 512x512 client texture with every other monitor model. Retail draws it with the same 14x16
	// pitch in the same texture, so the block is 462x368 at (25, 72) — smaller, still centred, still
	// on the same grid. At 2x: 924x736 at (50, 144). The pitch must not move with the grid, or the
	// apple monitor's glyphs would be a different size from every other machine's.
	const FCellMetrics Apple = MetricsFor(Surface1024Square, 33, 23);
	TestTrue(TEXT("a 33x23 grid is drawable"), Apple.bValid);
	TestEqual(TEXT("its cell is the same 28 wide"), Apple.CellWidth, Retail.CellWidth);
	TestEqual(TEXT("and the same 32 tall"), Apple.CellHeight, Retail.CellHeight);
	TestEqual(TEXT("its block is 924 wide"), Apple.BlockWidth(), 924);
	TestEqual(TEXT("and 736 tall"), Apple.BlockHeight(), 736);
	TestEqual(TEXT("centred at 2*25"), Apple.OriginX, 50);
	TestEqual(TEXT("and 2*72"), Apple.OriginY, 144);

	// The cell is retail's, NOT the surface divided by the grid: a `textcolumns 4` / `textrows 2`
	// terminal — the low end of the `[4,36] x [2,24]` clamp `CBaseTerminal::Spawn` applies — draws
	// the same 28x32 glyphs in a small block in the middle of the texture, exactly as retail does.
	const FCellMetrics Small = MetricsFor(Surface1024Square, 4, 2);
	TestEqual(TEXT("a 4x2 grid keeps the 28-wide cell"), Small.CellWidth, 28);
	TestEqual(TEXT("and the 32-tall one"), Small.CellHeight, 32);
	TestEqual(TEXT("its 112-wide block is centred"), Small.OriginX, (1024 - 112) / 2);
	TestEqual(TEXT("and so is its 64-tall one"), Small.OriginY, (1024 - 64) / 2);

	// Degenerate inputs draw nothing rather than dividing by zero.
	TestFalse(TEXT("a zero column count is refused"),
		MetricsFor(Surface1024Square, 0, 24).bValid);
	TestFalse(TEXT("a zero row count is refused"), MetricsFor(Surface1024Square, 36, 0).bValid);
	TestFalse(TEXT("a zero-sized surface is refused"),
		MetricsFor(FVector2D::ZeroVector, 36, 24).bValid);
	TestFalse(TEXT("a surface the block does not fit in is refused"),
		MetricsFor(FVector2D(16.0, 16.0), 36, 24).bValid);
	TestFalse(TEXT("and so is one a pixel too short for it"),
		MetricsFor(FVector2D(1024.0, 767.0), 36, 24).bValid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalPaletteTest,
	"Elysium.Substrate.Terminal.Palette", GCellFlags)
bool FElysiumTerminalPaletteTest::RunTest(const FString&)
{
	using namespace ElysiumUI;

	// Four schemes, four pictures. `colorscheme` is an index into exactly four records in retail
	// (`0x10233378`, clamped `[0,3]` at spawn), so four distinct answers is the contract; the
	// colours themselves are the project's own (§6.4).
	TSet<FString> Distinct;
	for (int32 Scheme = 0; Scheme <= 3; ++Scheme)
	{
		const FElysiumTerminalPalette& Palette = TerminalPalette(Scheme);
		Distinct.Add(Palette.Foreground.ToString() + Palette.Background.ToString());
		// The style bit is an XOR on the foreground/background pair, so the alternate is that pair
		// swapped and not two more authored colours.
		TestTrue(*FString::Printf(TEXT("scheme %d's alternate background is its foreground"), Scheme),
			Palette.AlternateBackground.Equals(Palette.Foreground));
		TestTrue(*FString::Printf(TEXT("scheme %d's alternate foreground is its background"), Scheme),
			Palette.AlternateForeground.Equals(Palette.Background));
		TestFalse(*FString::Printf(TEXT("scheme %d's ink is not its ground"), Scheme),
			Palette.Foreground.Equals(Palette.Background));
	}
	TestEqual(TEXT("the four schemes are four different pictures"), Distinct.Num(), 4);

	// Out of range clamps rather than reading off the end. The authority clamps at spawn, but a
	// projection can be handed a stale or hand-built view and must still draw.
	TestTrue(TEXT("a negative scheme clamps to 0"),
		TerminalPalette(-1).Foreground.Equals(TerminalPalette(0).Foreground));
	TestTrue(TEXT("and 9 clamps to 3"),
		TerminalPalette(9).Foreground.Equals(TerminalPalette(3).Foreground));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalCellPaintTest,
	"Elysium.Substrate.Terminal.CellPaint", GCellFlags)
bool FElysiumTerminalCellPaintTest::RunTest(const FString&)
{
	using namespace ElysiumTerminalPaint;

	// --- one glyph per NON-SPACE cell ------------------------------------------------------------
	FElysiumTerminalView View = GridView();
	PutText(View, 2, 3, TEXT("HOME MENU"));
	{
		const FDrawPlan Plan = BuildDrawPlan(View, FString(), Surface1024Square, 0.0f, false);
		TestTrue(TEXT("the plan is drawable"), Plan.Metrics.bValid);
		// Nine characters with one space among them: the space is a cell, not a glyph.
		TestEqual(TEXT("a cleared grid with one 9-character word draws 8 glyphs"),
			Plan.Glyphs.Num(), 8);
		TestEqual(TEXT("the first glyph is the H"), Plan.Glyphs[0].Character, TCHAR('H'));
		TestEqual(TEXT("in its own cell column"), Plan.Glyphs[0].Column, 2);
		TestEqual(TEXT("and row"), Plan.Glyphs[0].Row, 3);
		TestFalse(TEXT("drawn in normal video, because the style bit is SET"),
			Plan.Glyphs[0].bInverse);
		TestEqual(TEXT("a cleared grid has no reverse-video runs at all"),
			Plan.InverseRuns.Num(), 0);
		TestFalse(TEXT("and an idle grid draws no cursor"), Plan.bCursorVisible);
		TestTrue(TEXT("the palette follows the view's colorscheme"),
			Plan.Palette.Foreground.Equals(ElysiumUI::TerminalPalette(0).Foreground));
		View.ColorScheme = 2;
		TestTrue(TEXT("...and follows it when it changes"),
			BuildDrawPlan(View, FString(), Surface1024Square, 0.0f, false)
				.Palette.Foreground.Equals(ElysiumUI::TerminalPalette(2).Foreground));
		View.ColorScheme = 0;
	}

	// --- the style bit becomes one background run per span ---------------------------------------
	// The mail list draws `[N]` with the style byte cleared and restores it after (entity messages
	// 6 then 5), and the rasterizer XORs on bit 7 — so a cleared bit is the reverse-video block.
	PutText(View, 1, 5, TEXT("[3]"), /*bStyleSet*/ false);
	PutText(View, 4, 5, TEXT("Mail03"));
	// A second, separate span on the same row must not join the first.
	PutText(View, 20, 5, TEXT("NEW"), /*bStyleSet*/ false);
	{
		const FDrawPlan Plan = BuildDrawPlan(View, FString(), Surface1024Square, 0.0f, false);
		TestEqual(TEXT("two cleared spans on one row are two runs, never one"),
			Plan.InverseRuns.Num(), 2);
		TestEqual(TEXT("the first run starts at the bracket"), Plan.InverseRuns[0].Column, 1);
		TestEqual(TEXT("and is three cells long"), Plan.InverseRuns[0].Length, 3);
		TestEqual(TEXT("on the bracket's row"), Plan.InverseRuns[0].Row, 5);
		TestEqual(TEXT("the second run starts at the second span"), Plan.InverseRuns[1].Column, 20);
		TestEqual(TEXT("and is three cells long too"), Plan.InverseRuns[1].Length, 3);
		const FGlyph* Bracket = Plan.Glyphs.FindByPredicate(
			[](const FGlyph& G) { return G.Row == 5 && G.Column == 1; });
		const FGlyph* Subject = Plan.Glyphs.FindByPredicate(
			[](const FGlyph& G) { return G.Row == 5 && G.Column == 4; });
		if (TestNotNull(TEXT("the bracket is drawn"), Bracket)
			&& TestNotNull(TEXT("and the subject after it"), Subject))
		{
			TestTrue(TEXT("the bracket's glyph is inverse"), Bracket->bInverse);
			TestFalse(TEXT("the subject's is not"), Subject->bInverse);
		}
	}
	// A run that reaches the right edge closes at the edge rather than running off it.
	{
		FElysiumTerminalView EdgeView = GridView();
		PutText(EdgeView, 33, 7, TEXT("END"), /*bStyleSet*/ false);
		const FDrawPlan Plan = BuildDrawPlan(EdgeView, FString(), Surface1024Square, 0.0f, false);
		TestEqual(TEXT("one run against the right edge"), Plan.InverseRuns.Num(), 1);
		TestEqual(TEXT("closed at the last column"),
			Plan.InverseRuns[0].Column + Plan.InverseRuns[0].Length, 36);
	}

	// --- an unpublished grid draws nothing rather than one screen-wide inverse block --------------
	{
		FElysiumTerminalView Empty;
		const FDrawPlan Plan = BuildDrawPlan(Empty, FString(), Surface1024Square, 0.0f, false);
		TestTrue(TEXT("an unpublished grid still has metrics"), Plan.Metrics.bValid);
		TestEqual(TEXT("but draws no glyphs"), Plan.Glyphs.Num(), 0);
		TestEqual(TEXT("and no runs — zero cells would read as a cleared style bit everywhere"),
			Plan.InverseRuns.Num(), 0);
	}

	// --- the local draft and the block cursor ----------------------------------------------------
	FElysiumTerminalView Session = GridView();
	Session.SessionSerial = 4;
	Session.CursorRow = 23;
	Session.CursorColumn = 1;
	Session.RightMargin = 1;
	Session.bLineEditActive = true;
	Session.EditOriginRow = 23;
	Session.EditOriginColumn = 1;
	{
		const FDrawPlan Plan = BuildDrawPlan(Session, TEXT("cho"), Surface1024Square, 0.0f, false);
		TestEqual(TEXT("the typed line is three glyphs on the glass"), Plan.Glyphs.Num(), 3);
		TestEqual(TEXT("starting at the edit origin"), Plan.Glyphs[0].Column, 1);
		TestEqual(TEXT("on the edit row"), Plan.Glyphs[0].Row, 23);
		TestEqual(TEXT("the third is the o"), Plan.Glyphs[2].Character, TCHAR('o'));
		TestTrue(TEXT("the cursor is lit at phase 0"), Plan.bCursorVisible);
		TestEqual(TEXT("and sits after the typed line"), Plan.CursorColumn, 4);
		TestEqual(TEXT("on the edit row"), Plan.CursorRow, 23);
	}
	{
		// ~1.6 Hz: lit for the first half of the period, dark for the second.
		TestTrue(TEXT("phase 0.49 is lit"), BlinkLit(0.49f));
		TestFalse(TEXT("phase 0.50 is dark"), BlinkLit(0.50f));
		TestTrue(TEXT("the phase wraps"), BlinkLit(1.25f));
		TestFalse(TEXT("the cursor is dark on the second half of the blink"),
			BuildDrawPlan(Session, TEXT("cho"), Surface1024Square, 0.6f, false).bCursorVisible);
	}
	{
		// Acknowledge mode takes no characters, so it shows no caret; a closed client editor
		// (`+0xe88`) shows none either, independently of the mode; and neither does an idle
		// monitor, which is every terminal on the map nobody is standing at.
		FElysiumTerminalView Acknowledge = Session;
		Acknowledge.InputMode = 2;
		TestFalse(TEXT("acknowledge mode draws no cursor"),
			BuildDrawPlan(Acknowledge, FString(), Surface1024Square, 0.0f, false).bCursorVisible);
		FElysiumTerminalView Closed = Session;
		Closed.bLineEditActive = false;
		TestFalse(TEXT("a closed client line editor draws no cursor"),
			BuildDrawPlan(Closed, FString(), Surface1024Square, 0.0f, false).bCursorVisible);
		FElysiumTerminalView Idle = Session;
		Idle.SessionSerial = 0;
		TestFalse(TEXT("an idle monitor draws no cursor"),
			BuildDrawPlan(Idle, FString(), Surface1024Square, 0.0f, false).bCursorVisible);
	}

	// --- the calibration pattern -----------------------------------------------------------------
	{
		const FDrawPlan Plan = BuildDrawPlan(Session, TEXT("cho"), Surface1024Square, 0.0f, true);
		TestTrue(TEXT("the calibration plan says so"), Plan.bCalibration);
		TestTrue(TEXT("it replaces the grid rather than overlaying it"), Plan.Glyphs.Num() > 36);
		const bool bHasCorner = Plan.Glyphs.ContainsByPredicate(
			[](const FGlyph& G) { return G.Row == 2 && G.Column == 3 && G.Character == TCHAR('T'); });
		TestTrue(TEXT("and starts its TOP-LEFT label in the top-left corner"), bHasCorner);
	}
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
