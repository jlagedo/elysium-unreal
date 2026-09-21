#include "UI/ElysiumUIStrings.h"

#include "ElysiumContentPaths.h"
#include "ElysiumKeyValues.h"

#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumUIStrings, Log, All);

FElysiumUIStrings& FElysiumUIStrings::Get()
{
	static FElysiumUIStrings Instance;
	Instance.LoadOnce();
	return Instance;
}

void FElysiumUIStrings::LoadOnce()
{
	if (bTried)
	{
		return;
	}
	bTried = true;

	const FString Path = FElysiumContentPaths::UiStrings();
	FString Text;
	// `resource/gameui_english.txt` is UTF-16 LE with a byte-order mark; LoadFileToString reads the
	// mark and decodes accordingly, so nothing here has to know the encoding. 0018 story 21-6 put
	// the install's own file in the corpus in place of a flat `strings.json` an offline extractor
	// used to derive from it, which is why this reads KeyValues now.
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogElysiumUIStrings, Warning,
			TEXT("no UI string table at %s — falling back to built-in English. "
			     "Run: uv run elysium import ui-strings"), *Path);
		return;
	}

	Table = ParseTable(Text);
	if (Table.IsEmpty())
	{
		UE_LOG(LogElysiumUIStrings, Warning,
			TEXT("UI string table has no lang/Tokens block: %s"), *Path);
		return;
	}

	bLoaded = true;
	UE_LOG(LogElysiumUIStrings, Log, TEXT("UI string table: %d entries"), Table.Num());
}

TMap<FString, FString> FElysiumUIStrings::ParseTable(const FString& Text)
{
	// `"lang" { "Language" "English" "Tokens" { "<token>" "<text>" ... } }`. `Language` is a leaf of
	// the root beside the `Tokens` block, so reading the block rather than the root is what keeps it
	// out of the table — the legacy extractor dropped it by name instead, which also meant a token
	// that happened to be called `Language` would have been dropped with it.
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	const ElysiumKeyValues::FKvNode* Lang = Root.IsValid() ? Root->Child(TEXT("lang")) : nullptr;
	const ElysiumKeyValues::FKvNode* Tokens = Lang ? Lang->Child(TEXT("Tokens")) : nullptr;
	// `Values` is already folded and last-wins, which is the rule the file needs: `GameUI_Advanced`
	// is authored twice ("Advanced..." then "Advanced") and the later one is what retail shows.
	// Source KeyValues is case-insensitive, so `Resolve` folds its token to match.
	return Tokens ? Tokens->Values : TMap<FString, FString>();
}

FText FElysiumUIStrings::Resolve(const TCHAR* Token, const TCHAR* Fallback) const
{
	if (const FString* Found = Table.Find(FString(Token).ToLower()))
	{
		return FText::FromString(*Found);
	}
	return FText::FromString(Fallback);
}
