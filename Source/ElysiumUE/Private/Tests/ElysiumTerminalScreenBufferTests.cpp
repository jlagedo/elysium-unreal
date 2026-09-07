// The terminal cell screen against the client put-char / scroll / print bodies
// (docs/vtmb/computer-terminals.md §8.3, TERM15; docs/project/plans/terminals.md slice A).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumTerminalScreenBuffer.h"

namespace
{
static constexpr EAutomationTestFlags GFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalScreenBufferTest,
	"Elysium.Substrate.TerminalScreenBuffer", GFlags)
bool FElysiumTerminalScreenBufferTest::RunTest(const FString&)
{
	FElysiumTerminalScreenBuffer Screen;
	Screen.Reset(36, 24);
	TestEqual(TEXT("a fresh screen is blank"), Screen.RowTextTrimmed(0), FString());
	TestTrue(TEXT("blank cells carry the default style bit"), Screen.StyleAt(0, 0));
	TestEqual(TEXT("the cursor starts at the origin"), Screen.CursorColumn() + Screen.CursorRow(), 0);

	// Put-char: printable, wrap at the column count, newline back to the margin.
	for (int32 I = 0; I < 40; ++I)
	{
		Screen.PutChar('a' + (I % 26));
	}
	TestEqual(TEXT("36 characters fill row 0"), Screen.RowText(0), TEXT("abcdefghijklmnopqrstuvwxyzabcdefghij"));
	TestEqual(TEXT("the 37th character wrapped to row 1"), Screen.RowTextTrimmed(1), TEXT("klmn"));
	TestEqual(TEXT("the cursor sits after the wrapped text"), Screen.CursorColumn(), 4);
	Screen.PutChar(10);
	TestEqual(TEXT("newline moves to row 2 at the margin"), Screen.CursorRow() * 100 + Screen.CursorColumn(), 200);

	// Backspace: `FUN_100c8060`'s first arm stores 0 into the cell at `(column, row)` **before**
	// either column update, so it clears the cell the cursor is sitting on — one to the right of
	// the character just typed, which therefore survives the first backspace and is erased by the
	// second. Then, if the column is at or left of the margin, the cursor climbs to the last
	// column of the previous row; otherwise it steps one left.
	Screen.PutChar('x');
	Screen.PutChar(0x7f);
	TestEqual(TEXT("backspace clears the cell under the cursor, not the one before it"),
		Screen.RowTextTrimmed(2), TEXT("x"));
	TestEqual(TEXT("backspace steps left onto the typed character"), Screen.CursorColumn(), 0);
	Screen.PutChar(0x7f);
	TestEqual(TEXT("so the next backspace is the one that erases it"),
		Screen.RowTextTrimmed(2), FString());
	TestEqual(TEXT("backspace at the margin climbs to the previous row's last column"),
		Screen.CursorRow() * 100 + Screen.CursorColumn(), 135);

	// Style: the alternate style clears the bit; the default restores it.
	Screen.SetCursor(0, 5);
	Screen.SetStyleAlternate();
	Screen.PutChar('S');
	Screen.SetStyleDefault();
	Screen.PutChar('N');
	TestFalse(TEXT("the alternate style writes without the bit"), Screen.StyleAt(0, 5));
	TestTrue(TEXT("the default style writes with the bit"), Screen.StyleAt(1, 5));
	TestEqual(TEXT("the characters are unaffected by style"), Screen.RowTextTrimmed(5), TEXT("SN"));

	// Scroll at rows - 1: a newline from row 22 scrolls, so row 23 is reachable only by cursor set.
	Screen.Clear();
	Screen.SetCursor(0, 22);
	Screen.Print(TEXT("last"));
	Screen.PutChar(10);
	TestEqual(TEXT("the newline from row 22 scrolled the text to row 21"), Screen.RowTextTrimmed(21), TEXT("last"));
	TestEqual(TEXT("the cursor stays on rows - 1"), Screen.CursorRow(), 23);
	Screen.SetCursor(3, 23);
	Screen.Print(TEXT("P"));
	TestEqual(TEXT("cursor set reaches the last row"), Screen.RowTextTrimmed(23), TEXT("   P"));

	// Scroll-up by n, and by >= rows (a clear).
	Screen.Clear();
	Screen.SetCursor(0, 3);
	Screen.Print(TEXT("three"));
	Screen.ScrollUp(2);
	TestEqual(TEXT("scroll-up moves rows up"), Screen.RowTextTrimmed(1), TEXT("three"));
	TestEqual(TEXT("scroll-up moves the cursor up"), Screen.CursorRow(), 1);
	Screen.ScrollUp(24);
	TestEqual(TEXT("scroll by the row count clears"), Screen.RowTextTrimmed(1), FString());

	// Margins (type 7) and cursor set relative to the left margin.
	Screen.SetMargins(2, 2);
	TestEqual(TEXT("margins take"), Screen.LeftMargin() * 10 + Screen.RightMargin(), 22);
	Screen.SetCursor(0, 0);
	TestEqual(TEXT("cursor set is offset by the left margin"), Screen.CursorColumn(), 2);
	Screen.SetMargins(40, 40);
	TestTrue(TEXT("oversized margins reduce until they fit"),
		Screen.LeftMargin() + Screen.RightMargin() < 34);
	Screen.SetMargins(0, 0);

	// Word-wrapping print (type 2): a word that would cross the right edge starts a new row, and
	// the space after it is dropped when the cursor is already past the edge.
	Screen.Clear();
	Screen.Print(TEXT("A password is required to enter this subdirectory. Safe"));
	// "this" ends exactly on column 36, the space after it is dropped, "subdirectory." wraps.
	TestEqual(TEXT("print wraps at the word boundary"), Screen.RowText(0),
		TEXT("A password is required to enter this"));
	TestEqual(TEXT("the next word starts the next row"), Screen.RowTextTrimmed(1),
		TEXT("subdirectory. Safe"));
	Screen.Clear();
	Screen.Print(TEXT("abcdefghijklmnopqrstuvwxyzabcdefghijklmnop"));
	TestEqual(TEXT("a word longer than the row wraps by put-char"), Screen.RowText(0),
		TEXT("abcdefghijklmnopqrstuvwxyzabcdefghij"));
	TestEqual(TEXT("the overflow continues on the next row"), Screen.RowTextTrimmed(1), TEXT("klmnop"));
	Screen.Clear();
	Screen.Print(TEXT("one\ntwo"));
	TestEqual(TEXT("an embedded newline is honoured"), Screen.RowTextTrimmed(1), TEXT("two"));

	// Echo / backspace (types 9 / 8) drop the left margin.
	Screen.SetMargins(3, 0);
	Screen.Echo('e');
	TestEqual(TEXT("echo drops the left margin"), Screen.LeftMargin(), 0);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
