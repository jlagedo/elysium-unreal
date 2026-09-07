#pragma once

#include "CoreMinimal.h"

#include "ElysiumViewState.h"
#include "UI/ElysiumUIStyle.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SLeafWidget.h"

// The character-cell painter: the one leaf that turns a terminal's `uint16` grid into pixels on the
// monitor's glass (`docs/project/plans/terminals.md`, slice D).
//
// It is a LEAF and not a composition of text rows on purpose. Retail's screen is a fixed grid — the
// rasterizer walks `rows * columns` cells and stamps one glyph per cell at
// `(col * cellW, row * cellH)` — so a joined row of proportional-ish text would drift off the grid
// by the end of the row and the column-aligned draws (the framed boxes, the `[N]` mail brackets,
// the prompt) would not line up. One glyph per non-space cell reproduces the grid exactly, and the
// style bit's inverse video becomes a background run under the glyphs of that run.
//
// The whole picture is computed by the pure `ElysiumTerminalPaint::BuildDrawPlan`, outside
// `OnPaint`, so every automation tier can assert it: no tier renders a frame (`-nullrhi`), and a
// painter whose only expression is `OnPaint` would be unassertable by construction.
namespace ElysiumTerminalPaint
{
	// The project's own metrics, integer-derived from the render target so the grid never lands on a
	// half pixel: `FloorToInt(surface / columns)` per axis, then the whole text block centred in
	// what is left. 36x24 on 1024x768 gives 28x32 with an 8 px side margin and no vertical one.
	struct FCellMetrics
	{
		int32 CellWidth = 0;
		int32 CellHeight = 0;
		int32 OriginX = 0;
		int32 OriginY = 0;
		int32 Columns = 0;
		int32 Rows = 0;
		// False when the surface or the grid is degenerate (a zero column count, a zero-sized
		// target). Nothing is drawn then — an unpublished grid is not an error.
		bool bValid = false;

		int32 BlockWidth() const { return CellWidth * Columns; }
		int32 BlockHeight() const { return CellHeight * Rows; }
		FVector2f TopLeftOf(int32 Column, int32 Row) const
		{
			return FVector2f(static_cast<float>(OriginX + Column * CellWidth),
				static_cast<float>(OriginY + Row * CellHeight));
		}
	};

	FCellMetrics MetricsFor(const FVector2D& SurfacePx, int32 Columns, int32 Rows);

	// One drawn character. `bInverse` is the cell's style bit CLEAR — the reverse-video block the
	// mail list draws its `[N]` brackets in.
	struct FGlyph
	{
		TCHAR Character = TCHAR(' ');
		int32 Column = 0;
		int32 Row = 0;
		bool bInverse = false;
	};

	// A horizontal run of reverse-video cells, drawn as one background box under its glyphs. Runs
	// rather than per-cell boxes because retail's style spans are whole words and a run is one
	// batched quad.
	struct FRun
	{
		int32 Column = 0;
		int32 Row = 0;
		int32 Length = 0;
	};

	struct FDrawPlan
	{
		FCellMetrics Metrics;
		ElysiumUI::FElysiumTerminalPalette Palette;
		TArray<FRun> InverseRuns;
		TArray<FGlyph> Glyphs;
		// The block cursor, at the COMPOSED cursor (the authority's, walked right by the local
		// draft). Hidden in acknowledge mode, hidden while the client's line editor is closed, and
		// hidden for half of every blink period.
		bool bCursorVisible = false;
		int32 CursorColumn = 0;
		int32 CursorRow = 0;
		// The calibration pattern replaces the grid's content: a one-cell border, `TOP-LEFT` at
		// (0,0) and numbered rulers down the first column and along the first row. Read back off the
		// render target, it says which way the authored UVs run on a given model.
		bool bCalibration = false;
	};

	// ~1.6 Hz, phase in `[0,1)`: the cursor is lit for the first half of the period. The blink has
	// no retail counterpart — `C_BaseTerminal` draws no cursor at all, and the player's own typed
	// characters are the only feedback — so this is a named modernization, and the phase is driven
	// from the presentation redraw pass rather than from a widget tick so it survives `-nullrhi`.
	inline constexpr float BlinkHz = 1.6f;
	inline bool BlinkLit(float Phase) { return FMath::Frac(FMath::Max(0.0f, Phase)) < 0.5f; }

	// The whole picture, pure. `Draft` is composed onto the grid through
	// `ElysiumTerminalCells::ComposeDraft`, so the typed line is genuinely in the cells and the
	// cursor sits after it, exactly as retail's client composes before it rasterizes.
	FDrawPlan BuildDrawPlan(const FElysiumTerminalView& View, const FString& Draft,
		const FVector2D& SurfacePx, float BlinkPhase, bool bCalibration);
}

class SElysiumTerminalCells final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SElysiumTerminalCells) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// The authority's grid plus the local draft, as one publication. Both halves change the picture
	// and neither is meaningful without the other.
	void SetView(const FElysiumTerminalView& InView, const FString& InDraft);
	void SetBlinkPhase(float InPhase) { BlinkPhase = InPhase; }
	void SetCalibration(bool bInCalibration) { bCalibration = bInCalibration; }

	// The plan this widget would paint at the given surface size. Public because it is the whole of
	// what a headless tier can assert.
	ElysiumTerminalPaint::FDrawPlan DrawPlan(const FVector2D& SurfacePx) const;

private:
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override;

	FElysiumTerminalView View;
	FString Draft;
	float BlinkPhase = 0.0f;
	bool bCalibration = false;
};
