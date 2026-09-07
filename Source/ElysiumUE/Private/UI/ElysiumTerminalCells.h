#pragma once

#include "CoreMinimal.h"

struct FElysiumTerminalView;

// The local line editor's half of the terminal picture.
//
// Retail's glass is NOT the server's cell grid: the client composes the typed line into its own
// copy of that grid before the rasterizer runs (`docs/vtmb/computer-terminals.md` §8.1/§8.3,
// TERM13). Entity message type 3 (`FUN_100c82e0`) opens an edit by saving the cursor's whole cell
// row into `+0xe90`, recording the cursor column as the edit origin `+0xe8c` and clearing the local
// line `+0xed8`; every keystroke afterwards (`0x100c7090` for backspace, `FUN_100c6d50` for an
// insert) restores that saved row, walks the whole line back through put-char from the edit origin
// and leaves the cursor at `origin + caret`.
//
// Three consequences the port reproduces here rather than approximating:
//
//  * **The fit guard.** Both client paths wrap the entire re-render in
//    `if (strlen(line) + editOrigin < columns - rightMargin)`, and the edited line is only written
//    back to `+0xed8` INSIDE it. A keystroke that would push the line past the right margin is
//    therefore discarded whole — retail has no wrap and no scroll in the local editor.
//  * **One row.** The composition only ever touches the authority's cursor row.
//  * **The caret is the end of the line.** Left/Right/Home/End move it only while `m_bAllowDirKeys`
//    is set, and that field is zeroed at `CBaseTerminal::Spawn` `0x10217880` and written nowhere
//    else in vampire.dll.
//
// Pure: no widget, no world, no engine state. The projection composes with it to put the typed line
// on the glass, and the key tests assert against it directly.
namespace ElysiumTerminalCells
{
	struct FElysiumTerminalComposed
	{
		// Row-major `Rows * Columns`, the same encoding as `FElysiumTerminalView::Cells`.
		TArray<uint16> Cells;
		int32 Columns = 0;
		int32 Rows = 0;
		int32 CursorColumn = 0;
		int32 CursorRow = 0;
		// False when the fit guard refused the line, which is retail's silent "the keystroke did
		// not happen" and the reason the composed grid can equal the authority's.
		bool bDraftDrawn = false;

		uint16 Cell(int32 Column, int32 Row) const;
		TCHAR CharAt(int32 Column, int32 Row) const;
		// One padded string per row, for readers that do not draw cells (and for the interim
		// projection surface until slice D's cell painter lands).
		FString RowText(int32 Row) const;
		TArray<FString> RowTexts() const;
	};

	// `strlen(line) + editOrigin < columns - rightMargin`, the guard both client input paths share.
	// The widget applies it to refuse the keystroke, exactly as retail discards the edit.
	bool DraftFits(const FElysiumTerminalView& View, int32 DraftLength);

	// The authority's grid with the local draft put-charred from the authority's cursor.
	FElysiumTerminalComposed ComposeDraft(const FElysiumTerminalView& View, const FString& Draft);
}
