// P4.10 — `game_sign`: VtMB's full-screen sign/popup window, the tutorial's teaching layer.
//
// 71 of the game's 73 game_sign sit on sp_tutorial_1 as popup_1..popup_59, and the tutorial's
// whole progression runs through them: the player dismisses a panel, its OnUseEnd fires, and
// that wire opens the next popup / unhides the next door / runs the next Python beat. Without
// this class those 51 OpenWindow wires drop on the floor.
//
// Provenance: the entity half is vampire.dll (`CGameSign::LoadSignData` @0x10212da0, factory
// @0x10212430, `definition_file` @+0x454); the panel it opens is client.dll's CSignUI, whose
// parse + coordinate model live in ElysiumSignData.h/.cpp. Retail's `Sign { dependency }`
// redirect is evaluated with Py_eval_input (error-to-false), which our EvalCondition matches.
//
// `prop_sign` shares the parser but lives beside the model-bearing prop family. NewspaperData
// multi-column layout and executing the Rules block's ClientCommand remain outside this leaf.

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumSignData.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSignEnt, Log, All);

namespace
{
	// Register a field backed by a subclass member (the base FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddSubclassField / AddLogicField / AddDoorSubclassField in
	// the sibling class files — file-unique name so all of them can land in one unity blob.
	template <typename TClass, typename TMember>
	void AddSignField(FElysiumClassDesc& D, const TCHAR* Name, TMember TClass::* Member, EElysiumField Flags = ElysiumFieldDefault)
	{
		static_assert(std::is_base_of_v<FElysiumEntity, TClass>, "TClass must derive from FElysiumEntity");
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, bool>)
		{
			Acc.Type = EElysiumVariantType::Bool;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Bool(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToInt() != 0; };
		}
		else if constexpr (std::is_same_v<TMember, float>)
		{
			Acc.Type = EElysiumVariantType::Float;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Float(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToFloat(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const TClass&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<TClass&>(E).*Member = V.ToString(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddSignField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}
}

// ============================================================================================
// game_sign — CGameSign. Bodiless (no brush, no model): it exists only to own a definition_file
// and put that panel on screen when something fires OpenWindow.
// ============================================================================================

class FElysiumGameSign final : public FElysiumEntity
{
public:
	FString DefinitionFile;        // definition_file — "vdata/Signs/<name>.txt"
	float   FadeIn = 0.0f;         // fade_in  (seconds)
	float   FadeOut = 0.0f;        // fade_out (seconds)
	bool    bPause = false;        // pause — 1 on all 71 tutorial popups

	// Parsed lazily on the first open and cached: a popup the tutorial reopens (or ChangeFile
	// rewrites) should not re-read the file every time. Shared with the world so the HUD can draw
	// it without seeing this class.
	TSharedPtr<const FElysiumSignData> Panel;

	void InputOpenWindow(const FElysiumInputArgs&)
	{
		if (!World || IsInert())
		{
			return;   // a ScriptHidden/dead sign cannot be opened (R6)
		}
		if (!EnsurePanel())
		{
			return;
		}
		World->OpenSign(Handle, Panel, FadeIn);

		static const FName OnUseBegin(TEXT("OnUseBegin"));
		FireOutput(OnUseBegin, FElysiumEntityHandle());
		UE_LOG(LogElysiumSignEnt, Verbose, TEXT("%s OpenWindow '%s'"), *DebugString(), *DefinitionFile);
	}

	void InputCloseWindow(const FElysiumInputArgs&)
	{
		// An explicit CloseWindow still counts as a dismissal — retail's popup chains are driven by
		// OnUseEnd whether the player clicked or a script closed the panel.
		if (World && World->GetOpenSign() == Handle)
		{
			World->CloseSign(/*bSilent*/ false);
		}
	}

	// ChangeFile retargets the sign at another definition (tutorial.py's SetClanPopups rewrites 22
	// popups this way to show the clan-appropriate text). Drops the cache; the next OpenWindow
	// re-parses. If the panel is currently up, it is re-opened with the new content.
	void InputChangeFile(const FElysiumInputArgs& Args)
	{
		const FString NewFile = Args.Param.ToString();
		if (NewFile.IsEmpty())
		{
			return;
		}
		DefinitionFile = NewFile;
		Panel.Reset();

		if (World && World->GetOpenSign() == Handle && EnsurePanel())
		{
			World->OpenSign(Handle, Panel, FadeIn);
		}
	}

	virtual void Spawn() override
	{
		// Keyfields are already applied by the registry; nothing to wire. The panel is parsed on
		// first open so a map full of signs costs no file I/O at load.
	}

	// A killed/hidden sign must not leave its panel on screen. Kill() and ScriptHide() both land
	// here through the base dormancy switch.
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		if (IsInert() && World && World->GetOpenSign() == Handle)
		{
			World->CloseSign(/*bSilent*/ true);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Definition"), DefinitionFile.IsEmpty() ? TEXT("(none)") : DefinitionFile);
		Out.Emplace(TEXT("Fade"), FString::Printf(TEXT("in %.2f · out %.2f"), FadeIn, FadeOut));
		Out.Emplace(TEXT("Pause"), bPause ? TEXT("yes") : TEXT("no"));
		if (Panel.IsValid())
		{
			Out.Emplace(TEXT("Resolved file"), Panel->SourceFile);
			Out.Emplace(TEXT("Panel"), FString::Printf(TEXT("bg %s · %d text block(s)%s"),
				Panel->Background.bValid ? *Panel->Background.ImageName : TEXT("(none)"),
				Panel->Blocks.Num(), Panel->bHideHUD ? TEXT(" · HideHUD") : TEXT("")));
			Out.Emplace(TEXT("Rules"), FString::Printf(TEXT("close-on-click %s · min show %.2fs"),
				Panel->bCloseOnLeftClick ? TEXT("yes") : TEXT("no"), Panel->MinShowTime));
		}
		else
		{
			Out.Emplace(TEXT("Panel"), TEXT("(not parsed yet)"));
		}
		const bool bOpen = World && World->GetOpenSign() == Handle;
		Out.Emplace(TEXT("On screen"), bOpen ? TEXT("YES") : TEXT("no"));
	}

private:
	// Parse on demand. A failed parse is cached as a failure by leaving Panel null and logging
	// once per attempt — the sign then simply never opens, matching retail's
	// "Could not load data for sign: %s" warning path.
	bool EnsurePanel()
	{
		if (Panel.IsValid())
		{
			return true;
		}
		if (DefinitionFile.IsEmpty())
		{
			return false;
		}
		TSharedPtr<FElysiumSignData> Parsed = MakeShared<FElysiumSignData>();
		if (!FElysiumSignData::Load(DefinitionFile, *Parsed, World))
		{
			UE_LOG(LogElysiumSignEnt, Warning, TEXT("%s: could not load data for sign '%s'"),
				*DebugString(), *DefinitionFile);
			return false;
		}
		Panel = Parsed;
		return true;
	}
};

static TUniquePtr<FElysiumEntity> MakeGameSign() { return MakeUnique<FElysiumGameSign>(); }

static FElysiumClassRegistrar GRegGameSign(
	TEXT("game_sign"), ElysiumBaseClassName(), &MakeGameSign,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("OpenWindow"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			static_cast<FElysiumGameSign&>(E).InputOpenWindow(Args);
		});
		D.Input(TEXT("CloseWindow"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			static_cast<FElysiumGameSign&>(E).InputCloseWindow(Args);
		});
		D.Input(TEXT("ChangeFile"), [](FElysiumEntity& E, const FElysiumInputArgs& Args)
		{
			static_cast<FElysiumGameSign&>(E).InputChangeFile(Args);
		});

		AddSignField(D, TEXT("definition_file"), &FElysiumGameSign::DefinitionFile);
		AddSignField(D, TEXT("fade_in"),         &FElysiumGameSign::FadeIn);
		AddSignField(D, TEXT("fade_out"),        &FElysiumGameSign::FadeOut);
		AddSignField(D, TEXT("pause"),           &FElysiumGameSign::bPause);
	});
