#include "UI/SElysiumTerminalCells.h"

#include "UI/ElysiumTerminalCells.h"

#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElementTypes.h"
#include "Styling/CoreStyle.h"

namespace ElysiumTerminalPaint
{
	namespace
	{
		// `FElysiumTerminalScreenBuffer::StyleBit`, restated rather than included: presentation does
		// not reach into `Private/Substrate/`, and `FElysiumTerminalView` publishes the cells with
		// this encoding as its contract (ASCII in the low 7 bits, `0x80` the style bit).
		constexpr uint16 StyleBit = 0x80;

		// The one solid brush every box in the plan is drawn with. The picture is flat colour: a
		// ground, the reverse-video runs and the cursor. Anything with a border or a gradient would
		// be a modern UI panel rather than a phosphor screen.
		const FSlateBrush* SolidBrush()
		{
			return FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
		}

		// Terminus is drawn near the cell height and never scaled by the UI's virtual canvas: this
		// grid's unit is the render target's own pixel, not the 768-tall authoring canvas.
		float GlyphSizeFor(const FCellMetrics& Metrics)
		{
			return FMath::Max(1.0f, FMath::FloorToFloat(Metrics.CellHeight * 0.94f));
		}

		// `TOP-LEFT` in the corner, a one-cell border, and a digit ruler on both axes. Read back off
		// the render target this answers the two questions the authored material cannot: which way
		// U runs on this model, and which way V does.
		void FillCalibration(FDrawPlan& Plan)
		{
			const FCellMetrics& Metrics = Plan.Metrics;
			auto Put = [&Plan](TCHAR Character, int32 Column, int32 Row, bool bInverse)
			{
				if (Character != TCHAR(' '))
				{
					Plan.Glyphs.Add(FGlyph{ Character, Column, Row, bInverse });
				}
			};
			for (int32 Column = 0; Column < Metrics.Columns; ++Column)
			{
				Put(TCHAR('-'), Column, 0, false);
				Put(TCHAR('-'), Column, Metrics.Rows - 1, false);
				// The column ruler: the units digit of the column index, on row 1.
				Put(static_cast<TCHAR>(TCHAR('0') + Column % 10), Column, 1, Column % 10 == 0);
			}
			for (int32 Row = 0; Row < Metrics.Rows; ++Row)
			{
				Put(TCHAR('|'), 0, Row, false);
				Put(TCHAR('|'), Metrics.Columns - 1, Row, false);
				Put(static_cast<TCHAR>(TCHAR('0') + Row % 10), 1, Row, Row % 10 == 0);
			}
			// The corner label, which is the whole point: it is only legible the right way round.
			const TCHAR* Label = TEXT("TOP-LEFT");
			for (int32 Offset = 0; Label[Offset] != TCHAR('\0'); ++Offset)
			{
				const int32 Column = 3 + Offset;
				if (Column < Metrics.Columns - 1)
				{
					Put(Label[Offset], Column, 2, false);
				}
			}
			Plan.InverseRuns.Add(FRun{ 3, 2, FMath::Min(8, FMath::Max(0, Metrics.Columns - 4)) });
		}
	}

	FCellMetrics MetricsFor(const FVector2D& SurfacePx, int32 Columns, int32 Rows)
	{
		FCellMetrics Metrics;
		Metrics.Columns = Columns;
		Metrics.Rows = Rows;
		const int32 SurfaceW = FMath::FloorToInt(SurfacePx.X);
		const int32 SurfaceH = FMath::FloorToInt(SurfacePx.Y);
		if (Columns <= 0 || Rows <= 0 || SurfaceW <= 0 || SurfaceH <= 0)
		{
			return Metrics;   // bValid stays false; nothing is drawn
		}
		Metrics.CellWidth = FMath::FloorToInt(static_cast<float>(SurfaceW) / Columns);
		Metrics.CellHeight = FMath::FloorToInt(static_cast<float>(SurfaceH) / Rows);
		if (Metrics.CellWidth <= 0 || Metrics.CellHeight <= 0)
		{
			Metrics.CellWidth = 0;
			Metrics.CellHeight = 0;
			return Metrics;   // more cells than pixels: no grid is drawable at all
		}
		// The remainder is split evenly, so the block is centred and the margin is the same on both
		// sides. 1024 / 36 = 28 leaves 16 px, which is the authored 8 px side margin.
		Metrics.OriginX = (SurfaceW - Metrics.BlockWidth()) / 2;
		Metrics.OriginY = (SurfaceH - Metrics.BlockHeight()) / 2;
		Metrics.bValid = true;
		return Metrics;
	}

	FDrawPlan BuildDrawPlan(const FElysiumTerminalView& View, const FString& Draft,
		const FVector2D& SurfacePx, float BlinkPhase, bool bCalibration)
	{
		FDrawPlan Plan;
		Plan.Metrics = MetricsFor(SurfacePx, View.Columns, View.Rows);
		Plan.Palette = ElysiumUI::TerminalPalette(View.ColorScheme);
		Plan.bCalibration = bCalibration;
		if (!Plan.Metrics.bValid)
		{
			return Plan;
		}
		if (bCalibration)
		{
			FillCalibration(Plan);
			return Plan;
		}

		// The COMPOSED grid: the authority's cells with the local draft put-charred from the edit
		// origin, and only while the client's line editor is open. That composition is retail's own
		// (`FUN_100c6d50` -> `FUN_100c8060`), which is why the typed line is in the cells here
		// rather than in an overlay.
		const ElysiumTerminalCells::FElysiumTerminalComposed Composed =
			ElysiumTerminalCells::ComposeDraft(View, Draft);
		if (Composed.Cells.Num() < Plan.Metrics.Columns * Plan.Metrics.Rows)
		{
			// An unpublished or truncated grid. Drawing it would read every missing cell as zero —
			// a cleared style bit — and paint the whole screen as one reverse-video block.
			return Plan;
		}

		for (int32 Row = 0; Row < Plan.Metrics.Rows; ++Row)
		{
			int32 RunStart = INDEX_NONE;
			for (int32 Column = 0; Column < Plan.Metrics.Columns; ++Column)
			{
				// Bit SET is the resting style; a CLEAR bit is the reverse-video block the mail
				// list's `[N]` brackets and the screensaver's alternate rows are drawn in.
				const bool bInverse =
					(Composed.Cell(Column, Row) & StyleBit) == 0;
				if (bInverse && RunStart == INDEX_NONE)
				{
					RunStart = Column;
				}
				else if (!bInverse && RunStart != INDEX_NONE)
				{
					Plan.InverseRuns.Add(FRun{ RunStart, Row, Column - RunStart });
					RunStart = INDEX_NONE;
				}
				const TCHAR Character = Composed.CharAt(Column, Row);
				if (Character != TCHAR(' '))
				{
					Plan.Glyphs.Add(FGlyph{ Character, Column, Row, bInverse });
				}
			}
			if (RunStart != INDEX_NONE)
			{
				Plan.InverseRuns.Add(FRun{ RunStart, Row, Plan.Metrics.Columns - RunStart });
			}
		}

		// The cursor. `bLineEditActive` is retail's `+0xe88` — with it clear no key inserts and no
		// caret exists to draw — and acknowledge mode takes no characters at all, so neither draws
		// one. Serial 0 is an idle monitor nobody is standing at.
		constexpr uint8 AcknowledgeMode = 2;
		Plan.CursorColumn = Composed.CursorColumn;
		Plan.CursorRow = Composed.CursorRow;
		Plan.bCursorVisible = View.IsOpen() && View.bLineEditActive
			&& View.InputMode != AcknowledgeMode && BlinkLit(BlinkPhase)
			&& Plan.CursorColumn >= 0 && Plan.CursorColumn < Plan.Metrics.Columns
			&& Plan.CursorRow >= 0 && Plan.CursorRow < Plan.Metrics.Rows;
		return Plan;
	}
}

void SElysiumTerminalCells::Construct(const FArguments&)
{
	SetCanTick(false);
}

void SElysiumTerminalCells::SetView(const FElysiumTerminalView& InView, const FString& InDraft)
{
	View = InView;
	Draft = InDraft;
}

FVector2D SElysiumTerminalCells::ComputeDesiredSize(float) const
{
	// The glass is whatever the render target is; the widget never asks for a size of its own.
	return FVector2D(1.0, 1.0);
}

ElysiumTerminalPaint::FDrawPlan SElysiumTerminalCells::DrawPlan(const FVector2D& SurfacePx) const
{
	return ElysiumTerminalPaint::BuildDrawPlan(View, Draft, SurfacePx, BlinkPhase, bCalibration);
}

int32 SElysiumTerminalCells::OnPaint(const FPaintArgs&, const FGeometry& AllottedGeometry,
	const FSlateRect&, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle&, bool) const
{
	using namespace ElysiumTerminalPaint;

	const FVector2D Surface(AllottedGeometry.GetLocalSize());
	const FVector2f SurfacePx(static_cast<float>(Surface.X), static_cast<float>(Surface.Y));
	const FDrawPlan Plan = DrawPlan(Surface);
	const FSlateBrush* Brush = SolidBrush();

	// The ground first, over the WHOLE surface rather than over the text block: the side margin is
	// part of the screen, not a hole in it.
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
		AllottedGeometry.ToPaintGeometry(SurfacePx,
			FSlateLayoutTransform(FVector2f(0.0f, 0.0f))),
		Brush, ESlateDrawEffect::None, Plan.Palette.Background);
	if (!Plan.Metrics.bValid)
	{
		return LayerId;
	}

	const int32 RunLayer = LayerId + 1;
	for (const FRun& Run : Plan.InverseRuns)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, RunLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(static_cast<float>(Run.Length * Plan.Metrics.CellWidth),
					static_cast<float>(Plan.Metrics.CellHeight)),
				FSlateLayoutTransform(Plan.Metrics.TopLeftOf(Run.Column, Run.Row))),
			Brush, ESlateDrawEffect::None, Plan.Palette.AlternateBackground);
	}

	// The cursor sits UNDER the glyph: retail's block cursor is a filled cell and the character on
	// it stays legible in the inverse colour.
	const int32 CursorLayer = RunLayer + 1;
	if (Plan.bCursorVisible)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, CursorLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(static_cast<float>(Plan.Metrics.CellWidth),
					static_cast<float>(Plan.Metrics.CellHeight)),
				FSlateLayoutTransform(Plan.Metrics.TopLeftOf(Plan.CursorColumn, Plan.CursorRow))),
			Brush, ESlateDrawEffect::None, Plan.Palette.Cursor);
	}

	const int32 GlyphLayer = CursorLayer + 1;
	const FSlateFontInfo Font = ElysiumUIFonts().Font(EElysiumFontRole::Mono,
		EElysiumFontWeight::Regular, GlyphSizeFor(Plan.Metrics), 1.0f);
	// One measurement for the whole grid: the face is monospaced, so every cell's inset is the same
	// and measuring per glyph would be 864 measure calls per frame.
	FVector2f Inset(0.0f, 0.0f);
	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer() != nullptr)
	{
		const TSharedRef<FSlateFontMeasure> Measure =
			FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2D Advance(Measure->Measure(FString(TEXT("M")), Font, 1.0f));
		Inset = FVector2f(
			FMath::Max(0.0f, (Plan.Metrics.CellWidth - static_cast<float>(Advance.X)) * 0.5f),
			FMath::Max(0.0f, (Plan.Metrics.CellHeight - static_cast<float>(Advance.Y)) * 0.5f));
	}
	FString OneGlyph;
	for (const FGlyph& Glyph : Plan.Glyphs)
	{
		OneGlyph.Reset();
		OneGlyph.AppendChar(Glyph.Character);
		const bool bOnCursor = Plan.bCursorVisible
			&& Glyph.Column == Plan.CursorColumn && Glyph.Row == Plan.CursorRow;
		const FLinearColor Ink = (Glyph.bInverse || bOnCursor)
			? Plan.Palette.AlternateForeground : Plan.Palette.Foreground;
		FSlateDrawElement::MakeText(OutDrawElements, GlyphLayer,
			AllottedGeometry.ToPaintGeometry(
				FVector2f(static_cast<float>(Plan.Metrics.CellWidth),
					static_cast<float>(Plan.Metrics.CellHeight)),
				FSlateLayoutTransform(Plan.Metrics.TopLeftOf(Glyph.Column, Glyph.Row) + Inset)),
			OneGlyph, Font, ESlateDrawEffect::None, Ink);
	}
	return GlyphLayer;
}
