#include "Debug/ElysiumArenaScenario.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumNpcConditions.h"   // `has_condition`: a condition name read against the table

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include <initializer_list>

// The record reader. Strict on purpose: an unknown field is an error rather than ignored, because a
// misspelled `withn` that silently means "no deadline" is a scenario that passes for the wrong reason.
// Qualified namespace rather than an anonymous one: a unity build concatenates translation units.
namespace ElysiumArenaScenarioParse
{
	// The kinds of `seam.md` § "Kinds and their text", in its order, then `script`: no tap emits it,
	// the runner writes one per action it runs (`FElysiumArenaScenarioRunner::FireDueActions`).
	const TCHAR* const GTraceKinds[] =
	{
		TEXT("schedule"), TEXT("task"), TEXT("taskdone"), TEXT("taskfail"), TEXT("break"),
		TEXT("cond+"), TEXT("cond-"), TEXT("state"), TEXT("sequence"), TEXT("seqfinished"),
		TEXT("animevent"), TEXT("move"), TEXT("damage"), TEXT("death"), TEXT("corpse"),
		TEXT("hint+"), TEXT("hint-"), TEXT("output"), TEXT("input"), TEXT("stealthkill"), TEXT("swinghit"), TEXT("meleeimpact"), TEXT("deathcaller"),
		TEXT("removed"), TEXT("thinkfence"), TEXT("makerattempt"), // synchronous refusal tap 0x1034b580/0x101cc9e0
		TEXT("script"),
		TEXT("reload"),
		// Emitted at the port line that carries a retail address: `tag=<tag> fn=<symbol> va=0x<VA>
		// phase=<phase> <payload>` (`FElysiumEntityWorld::EmitRetailSite`, `docs/specs/layers/harness.md`).
		TEXT("retail_site"),
	};

	// The `fixtures` catalog's kinds. A story adds its own in the slice that stages it.
	const TCHAR* const GFixtureKinds[] = { TEXT("keyvalues"), TEXT("text") };

	enum class ENeed : uint8 { Optional, Required };

	struct FReader
	{
		explicit FReader(const FString& InFile) : File(InFile) {}

		const FString& File;
		FString Error;

		// Records the first failure only: the first is the one that is true, the rest often follow
		// from it.
		bool Fail(const FString& Field, const FString& Message)
		{
			if (Error.IsEmpty())
			{
				Error = FString::Printf(TEXT("%s: %s: %s"), *File, *Field, *Message);
			}
			return false;
		}
	};

	FString Field(const FString& Path, const TCHAR* Key)
	{
		return Path.IsEmpty() ? FString(Key) : FString::Printf(TEXT("%s.%s"), *Path, Key);
	}

	FString Indexed(const TCHAR* Key, int32 Index)
	{
		return FString::Printf(TEXT("%s[%d]"), Key, Index);
	}

	const TSharedPtr<FJsonValue>* FindValue(const FJsonObject& Object, const TCHAR* Key)
	{
		// The map's key is UE 5.8's `UE::FSharedString`, which converts from a `TCHAR*` (not an FString).
		const TSharedPtr<FJsonValue>* Value = Object.Values.Find(Key);
		if (Value == nullptr || !Value->IsValid() || (*Value)->Type == EJson::Null)
		{
			return nullptr;
		}
		return Value;
	}

	bool CheckFields(FReader& R, const FJsonObject& Object, const FString& Path,
		std::initializer_list<const TCHAR*> Allowed)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
		{
			bool bKnown = false;
			for (const TCHAR* Name : Allowed)
			{
				// Case-sensitive: the schema spells every field one way.
				if (Pair.Key.Equals(Name, ESearchCase::CaseSensitive))
				{
					bKnown = true;
					break;
				}
			}
			if (!bKnown)
			{
				TArray<FString> Names;
				for (const TCHAR* Name : Allowed)
				{
					Names.Add(Name);
				}
				return R.Fail(Field(Path, *Pair.Key), FString::Printf(TEXT("unknown field (this object takes %s)"),
					*FString::Join(Names, TEXT(", "))));
			}
		}
		return true;
	}

	bool ReadString(FReader& R, const FJsonObject& Object, const TCHAR* Key, const FString& Path,
		ENeed Need, FString& Out)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, Key);
		if (Value == nullptr)
		{
			return Need == ENeed::Required ? R.Fail(Field(Path, Key), TEXT("required")) : true;
		}
		if ((*Value)->Type != EJson::String)
		{
			return R.Fail(Field(Path, Key), TEXT("must be a string"));
		}
		Out = (*Value)->AsString();
		if (Need == ENeed::Required && Out.IsEmpty())
		{
			return R.Fail(Field(Path, Key), TEXT("must not be empty"));
		}
		return true;
	}

	// `bOutPresent` says whether the field was there at all, which is what an optional deadline is.
	bool ReadNumber(FReader& R, const FJsonObject& Object, const TCHAR* Key, const FString& Path,
		ENeed Need, double& Out, bool& bOutPresent)
	{
		bOutPresent = false;
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, Key);
		if (Value == nullptr)
		{
			return Need == ENeed::Required ? R.Fail(Field(Path, Key), TEXT("required")) : true;
		}
		if ((*Value)->Type != EJson::Number)
		{
			return R.Fail(Field(Path, Key), TEXT("must be a number"));
		}
		Out = (*Value)->AsNumber();
		if (!FMath::IsFinite(Out) || Out < 0.0)
		{
			return R.Fail(Field(Path, Key), TEXT("must be a finite, non-negative number"));
		}
		bOutPresent = true;
		return true;
	}

	bool ReadBool(FReader& R, const FJsonObject& Object, const TCHAR* Key, const FString& Path, bool& Out)
	{
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, Key);
		if (Value == nullptr)
		{
			return true;
		}
		if ((*Value)->Type != EJson::Boolean)
		{
			return R.Fail(Field(Path, Key), TEXT("must be true or false"));
		}
		Out = (*Value)->AsBool();
		return true;
	}

	bool ReadObject(FReader& R, const FJsonObject& Object, const TCHAR* Key, const FString& Path,
		TSharedPtr<FJsonObject>& Out)
	{
		Out.Reset();
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, Key);
		if (Value == nullptr)
		{
			return true;
		}
		if ((*Value)->Type != EJson::Object)
		{
			return R.Fail(Field(Path, Key), TEXT("must be an object"));
		}
		Out = (*Value)->AsObject();
		return true;
	}

	bool ReadArray(FReader& R, const FJsonObject& Object, const TCHAR* Key, const FString& Path,
		const TArray<TSharedPtr<FJsonValue>>*& Out)
	{
		Out = nullptr;
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, Key);
		if (Value == nullptr)
		{
			return true;
		}
		if ((*Value)->Type != EJson::Array)
		{
			return R.Fail(Field(Path, Key), TEXT("must be an array"));
		}
		Out = &(*Value)->AsArray();
		return true;
	}

	// One element of an array as an object, or a failure naming its index.
	bool ElementObject(FReader& R, const TSharedPtr<FJsonValue>& Value, const FString& Path,
		TSharedPtr<FJsonObject>& Out)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			return R.Fail(Path, TEXT("must be an object"));
		}
		Out = Value->AsObject();
		return Out.IsValid() || R.Fail(Path, TEXT("must be an object"));
	}

	bool ReadAt(FReader& R, const FJsonObject& Object, const TCHAR* Key, const FString& Path,
		FElysiumArenaAt& Out)
	{
		Out = FElysiumArenaAt();
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, Key);
		if (Value == nullptr)
		{
			return true;
		}
		if ((*Value)->Type == EJson::String)
		{
			Out.Name = (*Value)->AsString();
			if (Out.Name.IsEmpty())
			{
				return R.Fail(Field(Path, Key), TEXT("an empty name places nothing"));
			}
			Out.bSet = true;
			return true;
		}
		if ((*Value)->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Items = (*Value)->AsArray();
			if (Items.Num() != 3)
			{
				return R.Fail(Field(Path, Key), TEXT("coordinates are [x, y, z]"));
			}
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (!Items[Axis].IsValid() || Items[Axis]->Type != EJson::Number)
				{
					return R.Fail(Field(Path, Key), TEXT("coordinates are three numbers, centimetres"));
				}
				Out.Coordinates[Axis] = Items[Axis]->AsNumber();
			}
			Out.bCoordinates = true;
			Out.bSet = true;
			return true;
		}
		return R.Fail(Field(Path, Key), TEXT("a place is a name or [x, y, z]"));
	}

	bool ReadFace(FReader& R, const FJsonObject& Object, const FString& Path, FElysiumArenaFace& Out)
	{
		Out = FElysiumArenaFace();
		const TSharedPtr<FJsonValue>* Value = FindValue(Object, TEXT("face"));
		if (Value == nullptr)
		{
			return true;
		}
		if ((*Value)->Type == EJson::Number)
		{
			Out.Kind = FElysiumArenaFace::EKind::Yaw;
			Out.Yaw = static_cast<float>((*Value)->AsNumber());
			return true;
		}
		if ((*Value)->Type == EJson::String && !(*Value)->AsString().IsEmpty())
		{
			const FString Text = (*Value)->AsString();
			if (Text.Equals(TEXT("player"), ESearchCase::IgnoreCase))
			{
				Out.Kind = FElysiumArenaFace::EKind::Player;
				return true;
			}
			Out.Kind = FElysiumArenaFace::EKind::Name;
			Out.Name = Text;
			return true;
		}
		return R.Fail(Field(Path, TEXT("face")), TEXT("`player`, a place name, or a yaw in degrees"));
	}

	bool ReadValue(FReader& R, const TSharedPtr<FJsonValue>& Value, const FString& Path,
		FElysiumArenaValue& Out)
	{
		Out = FElysiumArenaValue();
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return true;
		}
		switch (Value->Type)
		{
		case EJson::Boolean:
			Out.Type = FElysiumArenaValue::EType::Bool;
			Out.bBool = Value->AsBool();
			return true;
		case EJson::Number:
			Out.Type = FElysiumArenaValue::EType::Number;
			Out.Number = Value->AsNumber();
			return true;
		case EJson::String:
			Out.Type = FElysiumArenaValue::EType::String;
			Out.String = Value->AsString();
			return true;
		default:
			return R.Fail(Path, TEXT("must be a string, a number or a boolean"));
		}
	}

	// A keyvalue as the map row would spell it. A number keeps no decimals it does not have, so an
	// authored `3` arrives as `"3"`, not `"3.000000"`.
	bool KeyText(FReader& R, const TSharedPtr<FJsonValue>& Value, const FString& Path, FString& Out)
	{
		if (!Value.IsValid() || Value->Type == EJson::Null)
		{
			return R.Fail(Path, TEXT("a keyvalue needs a value"));
		}
		switch (Value->Type)
		{
		case EJson::String:
			Out = Value->AsString();
			return true;
		case EJson::Number:
		{
			const double Number = Value->AsNumber();
			Out = FMath::IsNearlyEqual(Number, FMath::RoundToDouble(Number))
				? FString::Printf(TEXT("%lld"), static_cast<long long>(FMath::RoundToDouble(Number)))
				: FString::Printf(TEXT("%.9g"), Number);
			return true;
		}
		case EJson::Boolean:
			Out = Value->AsBool() ? TEXT("1") : TEXT("0");
			return true;
		default:
			return R.Fail(Path, TEXT("a keyvalue is a string, a number or a boolean"));
		}
	}

	bool ReadKeys(FReader& R, const FJsonObject& Object, const FString& Path, TMap<FString, FString>& Out)
	{
		TSharedPtr<FJsonObject> Keys;
		if (!ReadObject(R, Object, TEXT("keys"), Path, Keys))
		{
			return false;
		}
		if (!Keys.IsValid())
		{
			return true;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Keys->Values)
		{
			const FString KeyPath = Field(Field(Path, TEXT("keys")), *Pair.Key);
			// The row's own fields state these; a second spelling in `keys` would be two answers.
			if (Pair.Key.Equals(TEXT("origin"), ESearchCase::IgnoreCase)
				|| Pair.Key.Equals(TEXT("angles"), ESearchCase::IgnoreCase))
			{
				return R.Fail(KeyPath, TEXT("is stated by the row's `at` and `face`"));
			}
			if (Pair.Key.Equals(TEXT("targetname"), ESearchCase::IgnoreCase)
				|| Pair.Key.Equals(TEXT("classname"), ESearchCase::IgnoreCase))
			{
				return R.Fail(KeyPath, TEXT("is stated by the row's `name` and `classname`"));
			}
			if (Pair.Key.Equals(TEXT("model"), ESearchCase::IgnoreCase) && Object.Values.Contains(TEXT("body")))
			{
				return R.Fail(KeyPath, TEXT("is stated by the row's `body`; give one of the two"));
			}
			FString Text;
			if (!KeyText(R, Pair.Value, KeyPath, Text))
			{
				return false;
			}
			Out.Add(Pair.Key, Text);
		}
		return true;
	}

	enum class ERowKind : uint8 { ArenaRow, MapCast };

	bool ReadRow(FReader& R, const FJsonObject& Object, const FString& Path, ERowKind Kind,
		FElysiumArenaRow& Out)
	{
		Out = FElysiumArenaRow();
		if (Kind == ERowKind::MapCast)
		{
			// A map host stages nothing: its cast row names an entity the map already has.
			if (!CheckFields(R, Object, Path, { TEXT("name") }))
			{
				return false;
			}
			return ReadString(R, Object, TEXT("name"), Path, ENeed::Required, Out.Name);
		}
		if (!CheckFields(R, Object, Path,
				{ TEXT("name"), TEXT("classname"), TEXT("at"), TEXT("face"), TEXT("body"), TEXT("keys"), TEXT("outputs") }))
		{
			return false;
		}
		if (!ReadString(R, Object, TEXT("name"), Path, ENeed::Optional, Out.Name)
			|| !ReadString(R, Object, TEXT("classname"), Path, ENeed::Required, Out.Classname)
			|| !ReadAt(R, Object, TEXT("at"), Path, Out.At)
			|| !ReadFace(R, Object, Path, Out.Face)
			|| !ReadString(R, Object, TEXT("body"), Path, ENeed::Optional, Out.Body)
			|| !ReadKeys(R, Object, Path, Out.Keys))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Wires = nullptr;
		if (!ReadArray(R, Object, TEXT("outputs"), Path, Wires)) return false;
		for (int32 WireIndex = 0; Wires && WireIndex < Wires->Num(); ++WireIndex)
		{
			const FString WirePath = Field(Path, *Indexed(TEXT("outputs"), WireIndex));
			TSharedPtr<FJsonObject> Wire;
			FElysiumOutputDef& Output = Out.Outputs.AddDefaulted_GetRef();
			if (!ElementObject(R, (*Wires)[WireIndex], WirePath, Wire)
				|| !CheckFields(R, *Wire, WirePath, {TEXT("name"),TEXT("target"),TEXT("input"),TEXT("param"),TEXT("delay"),TEXT("times")})
				|| !ReadString(R, *Wire, TEXT("name"), WirePath, ENeed::Required, Output.Name)
				|| !ReadString(R, *Wire, TEXT("target"), WirePath, ENeed::Required, Output.Target)
				|| !ReadString(R, *Wire, TEXT("input"), WirePath, ENeed::Required, Output.Input)
				|| !ReadString(R, *Wire, TEXT("param"), WirePath, ENeed::Optional, Output.Param)) return false;
			double Delay = 0.0, Times = -1.0; bool bValue = false;
			if (!ReadNumber(R, *Wire, TEXT("delay"), WirePath, ENeed::Optional, Delay, bValue)
				|| !ReadNumber(R, *Wire, TEXT("times"), WirePath, ENeed::Optional, Times, bValue)) return false;
			if (Delay < 0 || Times < -1 || Times > MAX_int32 || Times != FMath::RoundToDouble(Times)) return R.Fail(WirePath, TEXT("nonnegative delay and integer times>=-1 required"));
			Output.Delay = static_cast<float>(Delay); Output.Times = Times == 0.0 ? -1 : static_cast<int32>(Times); // retail authored zero means unlimited
		}
		if (!Out.At.bSet)
		{
			return R.Fail(Field(Path, TEXT("at")), TEXT("required: where the row stands"));
		}
		return true;
	}

	bool IsTraceKind(const FString& Kind)
	{
		for (const TCHAR* Known : GTraceKinds)
		{
			if (Kind.Equals(Known, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	enum class EMatchKind : uint8 { Expect, Never };

	// A `never`'s window start (`from` or `after` plus `delay`) and its count (`at_most`). The label
	// `after` names is resolved by `ReadRecord`, which has the expectations.
	bool ReadNeverWindow(FReader& R, const FJsonObject& Object, const FString& Path, FElysiumArenaMatch& Out)
	{
		bool bDelay = false;
		bool bAtMost = false;
		double AtMost = 0.0;
		if (!ReadNumber(R, Object, TEXT("from"), Path, ENeed::Optional, Out.From, Out.bFrom)
			|| !ReadString(R, Object, TEXT("after"), Path, ENeed::Optional, Out.After)
			|| !ReadNumber(R, Object, TEXT("delay"), Path, ENeed::Optional, Out.Delay, bDelay)
			|| !ReadNumber(R, Object, TEXT("at_most"), Path, ENeed::Optional, AtMost, bAtMost))
		{
			return false;
		}
		if (Out.After.IsEmpty() && FindValue(Object, TEXT("after")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("after")), TEXT("an empty label names no expectation"));
		}
		if (Out.bFrom && !Out.After.IsEmpty())
		{
			return R.Fail(Path, TEXT("the window opens `from` a time or `after` a label, not both"));
		}
		if (bDelay && Out.After.IsEmpty())
		{
			return R.Fail(Field(Path, TEXT("delay")), TEXT("`delay` counts from an `after` label's match"));
		}
		// `within`: the window closes that many seconds after it opens at its label (match + `delay`).
		if (Out.bWithin && Out.After.IsEmpty())
		{
			return R.Fail(Field(Path, TEXT("within")),
				TEXT("`within` closes the window relative to an `after` label's match"));
		}
		if (Out.bWithin && Out.bUntil)
		{
			return R.Fail(Path, TEXT("the window closes `until` a time or `within` seconds of its label, not both"));
		}
		if (Out.bWithin && Out.Within < 0.0)
		{
			return R.Fail(Field(Path, TEXT("within")), TEXT("must be >= 0"));
		}
		if (Out.bFrom && Out.bUntil && Out.From > Out.Until)
		{
			return R.Fail(Field(Path, TEXT("from")), FString::Printf(
				TEXT("%.2f is past `until` %.2f: the window would be empty"), Out.From, Out.Until));
		}
		if (bAtMost)
		{
			if (AtMost != FMath::RoundToDouble(AtMost) || AtMost > static_cast<double>(MAX_int32))
			{
				return R.Fail(Field(Path, TEXT("at_most")), TEXT("must be a whole number >= 0"));
			}
			Out.AtMost = static_cast<int32>(AtMost);
		}
		return true;
	}

	bool ReadMatch(FReader& R, const FJsonObject& Object, const FString& Path, EMatchKind Kind,
		FElysiumArenaMatch& Out)
	{
		Out = FElysiumArenaMatch();
		const bool bFields = Kind == EMatchKind::Expect
			? CheckFields(R, Object, Path, { TEXT("label"), TEXT("who"), TEXT("kind"), TEXT("site"), TEXT("match"),
				TEXT("regex"), TEXT("by"), TEXT("within") })
			: CheckFields(R, Object, Path, { TEXT("who"), TEXT("kind"), TEXT("site"), TEXT("match"), TEXT("regex"),
				TEXT("until"), TEXT("from"), TEXT("after"), TEXT("delay"), TEXT("within"), TEXT("at_most") });
		if (!bFields)
		{
			return false;
		}
		FString KindText;
		if (!ReadString(R, Object, TEXT("label"), Path, ENeed::Optional, Out.Label)
			|| !ReadString(R, Object, TEXT("who"), Path, ENeed::Optional, Out.Who)
			|| !ReadString(R, Object, TEXT("kind"), Path, ENeed::Required, KindText)
			|| !ReadString(R, Object, TEXT("site"), Path, ENeed::Optional, Out.Site)
			|| !ReadString(R, Object, TEXT("match"), Path, ENeed::Optional, Out.Match)
			|| !ReadBool(R, Object, TEXT("regex"), Path, Out.bRegex)
			|| !ReadNumber(R, Object, TEXT("by"), Path, ENeed::Optional, Out.By, Out.bBy)
			|| !ReadNumber(R, Object, TEXT("within"), Path, ENeed::Optional, Out.Within, Out.bWithin)
			|| !ReadNumber(R, Object, TEXT("until"), Path, ENeed::Optional, Out.Until, Out.bUntil))
		{
			return false;
		}
		if (!IsTraceKind(KindText))
		{
			TArray<FString> Names;
			for (const TCHAR* Known : GTraceKinds)
			{
				Names.Add(Known);
			}
			return R.Fail(Field(Path, TEXT("kind")), FString::Printf(TEXT("'%s' is not a trace kind (%s)"),
				*KindText, *FString::Join(Names, TEXT(", "))));
		}
		Out.Kind = FName(*KindText);
		if (FindValue(Object, TEXT("site")) != nullptr && Out.Kind != FName(TEXT("retail_site")))
		{
			return R.Fail(Field(Path, TEXT("site")), TEXT("only a `retail_site` matcher takes a `site` tag"));
		}
		if (Out.Site.IsEmpty() && FindValue(Object, TEXT("site")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("site")), TEXT("an empty tag names no site"));
		}
		if (Out.bRegex && Out.Match.IsEmpty())
		{
			return R.Fail(Field(Path, TEXT("match")), TEXT("`regex` needs a pattern"));
		}
		return Kind == EMatchKind::Expect || ReadNeverWindow(R, Object, Path, Out);
	}

	bool ReadProbeName(FReader& R, const FString& Text, const FString& Path, EElysiumArenaProbe& Out)
	{
		const EElysiumArenaProbe All[] =
		{
			EElysiumArenaProbe::Alive, EElysiumArenaProbe::Schedule, EElysiumArenaProbe::State,
			EElysiumArenaProbe::Health, EElysiumArenaProbe::Enemy, EElysiumArenaProbe::Hint,
			EElysiumArenaProbe::HasCondition, EElysiumArenaProbe::OnGround, EElysiumArenaProbe::DistanceTo,
			EElysiumArenaProbe::PlayerWeapon, EElysiumArenaProbe::PlayerCrouched,
			EElysiumArenaProbe::PlayerGrappling, EElysiumArenaProbe::Speed2d, EElysiumArenaProbe::MoveYaw,
			EElysiumArenaProbe::GroundSpeed, EElysiumArenaProbe::CorpseOnFloor, EElysiumArenaProbe::Exists,
			EElysiumArenaProbe::EntityField,
			EElysiumArenaProbe::SameTeam,
			EElysiumArenaProbe::SwingRecordedHit,
			EElysiumArenaProbe::OneHitKill,
			EElysiumArenaProbe::TeamSymbol,
			EElysiumArenaProbe::Wounds,
			EElysiumArenaProbe::HealthCap,
			EElysiumArenaProbe::NpcFlags1,
			EElysiumArenaProbe::Witness,
			EElysiumArenaProbe::SpawnFlags,
			EElysiumArenaProbe::RenderAlpha,
			EElysiumArenaProbe::RenderMode,
			EElysiumArenaProbe::Activity,

		};
		TArray<FString> Names;
		for (const EElysiumArenaProbe Probe : All)
		{
			if (Text.Equals(ElysiumArenaScenario::ProbeName(Probe), ESearchCase::CaseSensitive))
			{
				Out = Probe;
				return true;
			}
			Names.Add(ElysiumArenaScenario::ProbeName(Probe));
		}
		return R.Fail(Path, FString::Printf(TEXT("'%s' is not a probe (%s)"), *Text,
			*FString::Join(Names, TEXT(", "))));
	}

	// A condition by its table name, with or without `COND_`, or by number (`0x2a`, `42`).
	bool ReadCondition(FReader& R, const FString& Text, const FString& Path, int32& OutId)
	{
		const FString Trimmed = Text.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase))
		{
			OutId = static_cast<int32>(FCString::Strtoi(*Trimmed + 2, nullptr, 16));
			return (OutId >= 0 && OutId <= 255) || R.Fail(Path, TEXT("a condition number is 0..0xff"));
		}
		if (Trimmed.IsNumeric())
		{
			OutId = FCString::Atoi(*Trimmed);
			return (OutId >= 0 && OutId <= 255) || R.Fail(Path, TEXT("a condition number is 0..255"));
		}
		for (int32 Id = 0; Id <= 255; ++Id)
		{
			const FString Name = ElysiumNpcCondName(static_cast<EElysiumNpcCond>(Id));
			if (Name == TEXT("COND_?"))
			{
				continue;
			}
			if (Trimmed.Equals(Name, ESearchCase::IgnoreCase)
				|| Trimmed.Equals(FString(TEXT("COND_")) + Name, ESearchCase::IgnoreCase))
			{
				OutId = Id;
				return true;
			}
		}
		return R.Fail(Path, FString::Printf(TEXT("'%s' names no condition in the table (ElysiumNpcCondName)"),
			*Text));
	}

	// Harness field list, strictly typed; capture is later at 0x1027bc60, not parse time.
	bool ReadWitnesses(FReader& R, const FJsonObject& Object, const FString& Path,
		TArray<FElysiumArenaWitness>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
		if (!ReadArray(R, Object, TEXT("fields"), Path, Fields)) return false;
		if (Fields == nullptr || Fields->IsEmpty()) return R.Fail(Path, TEXT("fields must be a nonempty typed witness list"));
		TSet<FString> Seen;
		for (int32 WitnessIndex = 0; WitnessIndex < Fields->Num(); ++WitnessIndex)
		{
			const FString WitnessPath = Field(Path, *Indexed(TEXT("fields"), WitnessIndex));
			TSharedPtr<FJsonObject> Item;
			FElysiumArenaWitness Word;
			bool bTolerance = false;
			FElysiumArenaValue::EType WordType;
			if (!ElementObject(R, (*Fields)[WitnessIndex], WitnessPath, Item)
				|| !CheckFields(R, *Item, WitnessPath, { TEXT("who"), TEXT("field"), TEXT("tolerance") })
				|| !ReadString(R, *Item, TEXT("who"), WitnessPath, ENeed::Required, Word.Who)
				|| !ReadString(R, *Item, TEXT("field"), WitnessPath, ENeed::Required, Word.Field)
				|| !ReadNumber(R, *Item, TEXT("tolerance"), WitnessPath, ENeed::Optional, Word.Tolerance, bTolerance)) return false;
			if (!ElysiumArenaScenario::WitnessType(Word.Field, WordType)) return R.Fail(WitnessPath, TEXT("unknown or excluded witness field"));
			if (Word.Field == TEXT("world_generation") || Word.Field == TEXT("senses.pass") || Word.Field == TEXT("senses.sighted")) return R.Fail(WitnessPath, TEXT("epoch/pass/Sighted are transient literal probes, excluded from snapshot equality"));
			if (bTolerance && WordType != FElysiumArenaValue::EType::Number) return R.Fail(WitnessPath, TEXT("tolerance requires a number"));
			const FString Key = Word.Who + TEXT("\n") + Word.Field;
			if (Seen.Contains(Key)) return R.Fail(WitnessPath, TEXT("duplicate witness"));
			Seen.Add(Key);
			Out.Add(MoveTemp(Word));
		}
		return true;
	}

	bool ReadProbe(FReader& R, const FJsonObject& Object, const FString& Path, double Duration,
		FElysiumArenaProbeSpec& Out)
	{
		Out = FElysiumArenaProbeSpec();
		if (!CheckFields(R, Object, Path, { TEXT("at"), TEXT("who"), TEXT("probe"), TEXT("equals"),
				TEXT("match"), TEXT("less"), TEXT("greater"), TEXT("condition"), TEXT("to"),
				TEXT("max_height"), TEXT("field"), TEXT("checkpoint"), TEXT("tolerance"), TEXT("index"),
				TEXT("member") }))
		{
			return false;
		}
		const TSharedPtr<FJsonValue>* At = FindValue(Object, TEXT("at"));
		if (At == nullptr)
		{
			return R.Fail(Field(Path, TEXT("at")), TEXT("required: `end` or a scenario time"));
		}
		if ((*At)->Type == EJson::String && (*At)->AsString().Equals(TEXT("end"), ESearchCase::IgnoreCase))
		{
			Out.bAtEnd = true;
		}
		else if ((*At)->Type == EJson::String && ((*At)->AsString() == TEXT("pre_init")
			|| (*At)->AsString() == TEXT("captured") || (*At)->AsString() == TEXT("applied") || (*At)->AsString() == TEXT("ready")))
		{
			Out.bAtEnd = false;
			Out.Fence = (*At)->AsString();
		}
		else if ((*At)->Type == EJson::Number && (*At)->AsNumber() >= 0.0)
		{
			Out.bAtEnd = false;
			Out.Time = (*At)->AsNumber();
			if (Out.Time > Duration)
			{
				return R.Fail(Field(Path, TEXT("at")), FString::Printf(
					TEXT("%.2f is past the record's duration %.2f; the probe would never be read"),
					Out.Time, Duration));
			}
		}
		else
		{
			return R.Fail(Field(Path, TEXT("at")), TEXT("`end` or a non-negative scenario time"));
		}

		FString ProbeText;
		if (!ReadString(R, Object, TEXT("who"), Path, ENeed::Required, Out.Who)
			|| !ReadString(R, Object, TEXT("probe"), Path, ENeed::Required, ProbeText)
			|| !ReadProbeName(R, ProbeText, Field(Path, TEXT("probe")), Out.Probe))
		{
			return false;
		}
		const bool bPlayerProbe = Out.Probe == EElysiumArenaProbe::PlayerWeapon
			|| Out.Probe == EElysiumArenaProbe::PlayerCrouched || Out.Probe == EElysiumArenaProbe::PlayerGrappling;
		if (bPlayerProbe && !Out.Who.Equals(TEXT("player"), ESearchCase::IgnoreCase))
		{
			return R.Fail(Field(Path, TEXT("who")), FString::Printf(TEXT("`%s` reads the player: `who` is `player`"),
				ElysiumArenaScenario::ProbeName(Out.Probe)));
		}

		// Saved-word equality is explicit and typed (0x1027bf50); no opaque byte comparison.
		if (Out.Probe == EElysiumArenaProbe::Witness)
		{
			bool bTolerance = false;
			FElysiumArenaValue::EType WordType;
			if (!ReadString(R, Object, TEXT("field"), Path, ENeed::Required, Out.Field)
				|| !ReadString(R, Object, TEXT("checkpoint"), Path, ENeed::Optional, Out.Checkpoint)
				|| !ReadNumber(R, Object, TEXT("tolerance"), Path, ENeed::Optional, Out.Tolerance, bTolerance)) return false;
			if (!ElysiumArenaScenario::WitnessType(Out.Field, WordType)) return R.Fail(Path, TEXT("unknown or excluded witness field"));
			if (bTolerance && WordType != FElysiumArenaValue::EType::Number) return R.Fail(Path, TEXT("tolerance requires a number"));
			if (!Out.Checkpoint.IsEmpty())
			{
				for (const TCHAR* Key : { TEXT("equals"), TEXT("match"), TEXT("less"), TEXT("greater") })
					if (FindValue(Object, Key)) return R.Fail(Path, TEXT("checkpoint equality excludes literal comparisons"));
				for (const TCHAR* Key : { TEXT("condition"), TEXT("to"), TEXT("max_height") })
					if (FindValue(Object, Key)) return R.Fail(Path, TEXT("field is incompatible with witness"));
				return true;
			}
		}
		else if (Out.Probe == EElysiumArenaProbe::EntityField)
		{
			// A retail field by its retail name (`m_iHealth`); `to`, `index` and `member` select within it.
			if (!ReadString(R, Object, TEXT("field"), Path, ENeed::Required, Out.Field)
				|| !ReadString(R, Object, TEXT("index"), Path, ENeed::Optional, Out.Index)
				|| !ReadString(R, Object, TEXT("member"), Path, ENeed::Optional, Out.Member)
				|| !ReadAt(R, Object, TEXT("to"), Path, Out.To)) return false;
			for (const TCHAR* Key : { TEXT("checkpoint"), TEXT("tolerance"), TEXT("condition"), TEXT("max_height") })
				if (FindValue(Object, Key)) return R.Fail(Field(Path, Key), TEXT("`entity_field` does not take this field"));
		}
		else if (FindValue(Object, TEXT("field")) || FindValue(Object, TEXT("checkpoint")) || FindValue(Object, TEXT("tolerance")))
			return R.Fail(Path, TEXT("field/checkpoint/tolerance require witness"));
		if (Out.Probe != EElysiumArenaProbe::EntityField)
			for (const TCHAR* Key : { TEXT("index"), TEXT("member") })
				if (FindValue(Object, Key)) return R.Fail(Field(Path, Key), TEXT("only `entity_field` takes this selector"));

		// Exactly one comparison.
		const TCHAR* const Compares[] = { TEXT("equals"), TEXT("match"), TEXT("less"), TEXT("greater") };
		const EElysiumArenaCompare CompareKinds[] =
		{
			EElysiumArenaCompare::Equals, EElysiumArenaCompare::Match, EElysiumArenaCompare::Less,
			EElysiumArenaCompare::Greater,
		};
		int32 Found = 0;
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Compares)); ++Index)
		{
			if (const TSharedPtr<FJsonValue>* Value = FindValue(Object, Compares[Index]))
			{
				++Found;
				Out.Compare = CompareKinds[Index];
				if (!ReadValue(R, *Value, Field(Path, Compares[Index]), Out.Value))
				{
					return false;
				}
			}
		}
		if (Found != 1)
		{
			return R.Fail(Path, TEXT("a probe takes exactly one of equals, match, less, greater"));
		}

		// The answer's type is the probe's, so a comparison that cannot hold is refused here.
		FElysiumArenaValue::EType Answer = FElysiumArenaValue::EType::String;
		switch (Out.Probe)
		{
		case EElysiumArenaProbe::Witness:
			ElysiumArenaScenario::WitnessType(Out.Field, Answer);
			break;
		case EElysiumArenaProbe::EntityField:
			// The field's own type is known only when it is read: the comparison's value states it.
			if (Out.Value.Type == FElysiumArenaValue::EType::None)
				return R.Fail(Path, TEXT("`entity_field` compares with a boolean, a number or a string"));
			Answer = Out.Value.Type;
			break;
		case EElysiumArenaProbe::Alive:
		case EElysiumArenaProbe::HasCondition:
		case EElysiumArenaProbe::OnGround:
		case EElysiumArenaProbe::PlayerCrouched:
		case EElysiumArenaProbe::PlayerGrappling:
		case EElysiumArenaProbe::CorpseOnFloor:
				case EElysiumArenaProbe::SameTeam:
		case EElysiumArenaProbe::SwingRecordedHit:
		case EElysiumArenaProbe::OneHitKill:
		case EElysiumArenaProbe::Exists:
			Answer = FElysiumArenaValue::EType::Bool;
			break;
				case EElysiumArenaProbe::TeamSymbol:
		case EElysiumArenaProbe::Wounds:
		case EElysiumArenaProbe::HealthCap:
		case EElysiumArenaProbe::NpcFlags1:
		case EElysiumArenaProbe::SpawnFlags:
		case EElysiumArenaProbe::RenderAlpha:
		case EElysiumArenaProbe::RenderMode:
		case EElysiumArenaProbe::Activity:
		case EElysiumArenaProbe::Health:
		case EElysiumArenaProbe::DistanceTo:
		case EElysiumArenaProbe::Speed2d:
		case EElysiumArenaProbe::MoveYaw:
		case EElysiumArenaProbe::GroundSpeed:
			Answer = FElysiumArenaValue::EType::Number;
			break;
		default:
			break;
		}
		const bool bOrdered = Out.Compare == EElysiumArenaCompare::Less
			|| Out.Compare == EElysiumArenaCompare::Greater;
		if ((bOrdered && Answer != FElysiumArenaValue::EType::Number)
			|| (Out.Compare == EElysiumArenaCompare::Match && Answer != FElysiumArenaValue::EType::String)
			|| Out.Value.Type != (bOrdered ? FElysiumArenaValue::EType::Number
				: Out.Compare == EElysiumArenaCompare::Match ? FElysiumArenaValue::EType::String : Answer))
		{
			return R.Fail(Path, FString::Printf(TEXT("`%s` answers a %s; `%s %s` cannot compare with it"),
				ElysiumArenaScenario::ProbeName(Out.Probe),
				Answer == FElysiumArenaValue::EType::Bool ? TEXT("boolean")
					: Answer == FElysiumArenaValue::EType::Number ? TEXT("number") : TEXT("string"),
				ElysiumArenaScenario::CompareName(Out.Compare), *Out.Value.Describe()));
		}

		if (Out.Probe == EElysiumArenaProbe::HasCondition)
		{
			if (!ReadString(R, Object, TEXT("condition"), Path, ENeed::Required, Out.ConditionName)
				|| !ReadCondition(R, Out.ConditionName, Field(Path, TEXT("condition")), Out.Condition))
			{
				return false;
			}
		}
		else if (FindValue(Object, TEXT("condition")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("condition")), TEXT("only `has_condition` takes a condition"));
		}
		if (Out.Probe == EElysiumArenaProbe::DistanceTo || Out.Probe == EElysiumArenaProbe::SameTeam || Out.Probe == EElysiumArenaProbe::SwingRecordedHit)
		{
			if (!ReadAt(R, Object, TEXT("to"), Path, Out.To))
			{
				return false;
			}
			if (!Out.To.bSet)
			{
				return R.Fail(Field(Path, TEXT("to")), TEXT("required: a targetname, `player`, or a place"));
			}
		}
		else if (Out.Probe != EElysiumArenaProbe::EntityField && FindValue(Object, TEXT("to")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("to")), TEXT("only `distance_to` takes `to`"));
		}
		if (Out.Probe == EElysiumArenaProbe::CorpseOnFloor)
		{
			// The bound is the record's, per its body: no one number serves every rig.
			const TSharedPtr<FJsonValue>* Height = FindValue(Object, TEXT("max_height"));
			if (Height == nullptr || (*Height)->Type != EJson::Number || (*Height)->AsNumber() <= 0.0)
			{
				return R.Fail(Field(Path, TEXT("max_height")),
					TEXT("required: the pelvis bone's height bound over the floor, centimetres (> 0)"));
			}
			Out.MaxHeightCm = (*Height)->AsNumber();
		}
		else if (FindValue(Object, TEXT("max_height")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("max_height")), TEXT("only `corpse_on_floor` takes `max_height`"));
		}
		return true;
	}

	bool ReadActionName(FReader& R, const FString& Text, const FString& Path, EElysiumArenaAction& Out)
	{
		const EElysiumArenaAction All[] =
		{
			EElysiumArenaAction::PlayerTeleport, EElysiumArenaAction::PlayerWalk, EElysiumArenaAction::Fire,
			EElysiumArenaAction::Console, EElysiumArenaAction::Spawn, EElysiumArenaAction::Kill,
			EElysiumArenaAction::PlayerCrouch, EElysiumArenaAction::LightPin, EElysiumArenaAction::DialogChoose, EElysiumArenaAction::SeedHealth,
			EElysiumArenaAction::DamagePacket, EElysiumArenaAction::EntityCall, EElysiumArenaAction::Save, EElysiumArenaAction::Load,
			EElysiumArenaAction::FreshMap, EElysiumArenaAction::Travel, EElysiumArenaAction::RestoreCompare,
			EElysiumArenaAction::NpcSingleRoundFinishReload, EElysiumArenaAction::CorruptCheckpoint,
			EElysiumArenaAction::InvalidMarker, EElysiumArenaAction::RestoreBase,
			EElysiumArenaAction::NoRagdollDeath, EElysiumArenaAction::DamageMemory,
			EElysiumArenaAction::ReserveSpot, EElysiumArenaAction::StartNpcGroundGate,
		};
		TArray<FString> Names;
		for (const EElysiumArenaAction Action : All)
		{
			if (Text.Equals(ElysiumArenaScenario::ActionName(Action), ESearchCase::CaseSensitive))
			{
				Out = Action;
				return true;
			}
			Names.Add(ElysiumArenaScenario::ActionName(Action));
		}
		return R.Fail(Path, FString::Printf(TEXT("'%s' is not an action (%s)"), *Text,
			*FString::Join(Names, TEXT(", "))));
	}

	bool ReadAction(FReader& R, const FJsonObject& Object, const FString& Path, ERowKind RowKind,
		FElysiumArenaAction& Out)
	{
		Out = FElysiumArenaAction();
		if (!CheckFields(R, Object, Path, { TEXT("t"), TEXT("after"), TEXT("delay"), TEXT("do"), TEXT("at"),
				TEXT("face"), TEXT("target"), TEXT("input"), TEXT("param"), TEXT("command"), TEXT("row"),
				TEXT("on"), TEXT("value"), TEXT("index"), TEXT("end"), TEXT("attacker"), TEXT("activator"), TEXT("inflictor"),
				TEXT("function"), TEXT("args"), TEXT("slot"), TEXT("map"), TEXT("landmark"), TEXT("checkpoint"), TEXT("fields"), TEXT("control"), TEXT("timeout") }))
		{
			return false;
		}
		bool bDelay = false;
		FString DoText;
		if (!ReadNumber(R, Object, TEXT("t"), Path, ENeed::Optional, Out.Time, Out.bAtTime)
			|| !ReadString(R, Object, TEXT("after"), Path, ENeed::Optional, Out.After)
			|| !ReadNumber(R, Object, TEXT("delay"), Path, ENeed::Optional, Out.Delay, bDelay)
			|| !ReadString(R, Object, TEXT("do"), Path, ENeed::Required, DoText)
			|| !ReadActionName(R, DoText, Field(Path, TEXT("do")), Out.Do))
		{
			return false;
		}
		if (Out.bAtTime == !Out.After.IsEmpty())
		{
			return R.Fail(Path, TEXT("an action runs at `t` or `after` a label, exactly one of the two"));
		}
		if (bDelay && Out.After.IsEmpty())
		{
			return R.Fail(Field(Path, TEXT("delay")), TEXT("`delay` counts from an `after` label's match"));
		}
		// No irrelevant transaction keys may be silently swallowed (0x20096010 harness schema).
		TArray<FString> V6Allowed = { TEXT("t"), TEXT("after"), TEXT("delay"), TEXT("do") };
		switch (Out.Do)
		{
		case EElysiumArenaAction::Save: V6Allowed.Append({ TEXT("slot"), TEXT("checkpoint"), TEXT("fields"), TEXT("timeout") }); break;
		case EElysiumArenaAction::Load: V6Allowed.Append({ TEXT("slot"), TEXT("checkpoint"), TEXT("timeout") }); break;
		case EElysiumArenaAction::FreshMap: V6Allowed.Add(TEXT("map")); break;
		case EElysiumArenaAction::Travel: V6Allowed.Append({ TEXT("map"), TEXT("landmark") }); break;
		case EElysiumArenaAction::RestoreCompare: V6Allowed.Append({ TEXT("checkpoint"), TEXT("fields") }); break;
		case EElysiumArenaAction::CorruptCheckpoint: V6Allowed.Append({ TEXT("checkpoint"), TEXT("control") }); break;
		case EElysiumArenaAction::RestoreBase: V6Allowed.Append({ TEXT("checkpoint"), TEXT("param") }); break;
		case EElysiumArenaAction::NpcSingleRoundFinishReload:
		case EElysiumArenaAction::InvalidMarker: V6Allowed.Append({ TEXT("target"), TEXT("control") }); break;
		case EElysiumArenaAction::NoRagdollDeath: V6Allowed.Add(TEXT("target")); break;
		case EElysiumArenaAction::DamageMemory: V6Allowed.Append({ TEXT("target"), TEXT("attacker"), TEXT("control"), TEXT("param"), TEXT("inflictor") }); break;
		case EElysiumArenaAction::ReserveSpot: V6Allowed.Append({ TEXT("target"), TEXT("attacker"), TEXT("control") }); break;
		case EElysiumArenaAction::StartNpcGroundGate: V6Allowed.Append({ TEXT("target"), TEXT("control") }); break;
		default: break;
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object.Values)
		{
			if (Out.Do >= EElysiumArenaAction::Save && !V6Allowed.Contains(Pair.Key)) return R.Fail(Field(Path, *Pair.Key), TEXT("field is incompatible with this transaction/fixture"));
			if (Out.Do < EElysiumArenaAction::Save)
				for (const TCHAR* Key : { TEXT("slot"), TEXT("map"), TEXT("landmark"), TEXT("checkpoint"), TEXT("fields"), TEXT("control"), TEXT("timeout") })
					if (Pair.Key == Key) return R.Fail(Field(Path, Key), TEXT("field requires a transaction/fixture"));
			if (Pair.Key == TEXT("inflictor") && Out.Do != EElysiumArenaAction::DamagePacket && Out.Do != EElysiumArenaAction::DamageMemory) return R.Fail(Field(Path, TEXT("inflictor")), TEXT("inflictor requires scalar damage"));
		}
		// `value` may be a JSON null (the release), so its presence is read off the object itself.
		const bool bHasValue = Object.Values.Contains(TEXT("value"));
		if (Out.Do != EElysiumArenaAction::Fire && FindValue(Object, TEXT("activator")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("activator")), TEXT("only `fire` takes `activator`"));
		}
		if (Out.Do != EElysiumArenaAction::PlayerCrouch && FindValue(Object, TEXT("on")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("on")), TEXT("only `player_crouch` takes `on`"));
		}
		if (Out.Do != EElysiumArenaAction::LightPin && bHasValue)
		{
			return R.Fail(Field(Path, TEXT("value")), TEXT("only `light_pin` takes `value`"));
		}
		for (const TCHAR* Key : { TEXT("function"), TEXT("args") })
		{
			if (Out.Do != EElysiumArenaAction::EntityCall && FindValue(Object, Key) != nullptr)
			{
				return R.Fail(Field(Path, Key), FString::Printf(TEXT("only `entity_call` takes `%s`"), Key));
			}
		}
		for (const TCHAR* Key : { TEXT("index"), TEXT("end") })
		{
			if (Out.Do != EElysiumArenaAction::DialogChoose && FindValue(Object, Key) != nullptr)
			{
				return R.Fail(Field(Path, Key), FString::Printf(TEXT("only `dialog_choose` takes `%s`"), Key));
			}
		}

		switch (Out.Do)
		{
		// Transaction staging only; runtime fences are 0x200975f0/0x1011a620.
		case EElysiumArenaAction::Save:
		case EElysiumArenaAction::Load:
		{
			bool bTimeout = false;
			if (!ReadString(R, Object, TEXT("slot"), Path, ENeed::Required, Out.Slot)
				|| !ReadString(R, Object, TEXT("checkpoint"), Path, ENeed::Optional, Out.Checkpoint)
				|| !ReadNumber(R, Object, TEXT("timeout"), Path, ENeed::Optional, Out.Timeout, bTimeout)) return false;
			if (Out.Timeout <= 0.0 || Out.Timeout > 60.0) return R.Fail(Path, TEXT("timeout must be in (0,60] wall seconds"));
			if (Out.Slot.Contains(TEXT("/")) || Out.Slot.Contains(TEXT("\\")) || Out.Slot.Contains(TEXT(".."))) return R.Fail(Path, TEXT("slot must be a plain slot name"));
			if (Out.Do == EElysiumArenaAction::Save && !Out.Checkpoint.IsEmpty()) return ReadWitnesses(R, Object, Path, Out.Fields);
			if (FindValue(Object, TEXT("fields"))) return R.Fail(Path, TEXT("fields require a save checkpoint"));
			return true;
		}
		case EElysiumArenaAction::FreshMap:
		case EElysiumArenaAction::Travel:
		{
			if (RowKind != ERowKind::MapCast) return R.Fail(Path, TEXT("map transaction requires a map host"));
			if (!ReadString(R, Object, TEXT("map"), Path, ENeed::Required, Out.Map)) return false;
			if (Out.Do == EElysiumArenaAction::Travel) return ReadString(R, Object, TEXT("landmark"), Path, ENeed::Optional, Out.Landmark);
			return FindValue(Object, TEXT("landmark")) ? R.Fail(Path, TEXT("fresh_map takes no landmark")) : true;
		}
		case EElysiumArenaAction::RestoreCompare:
			return ReadString(R, Object, TEXT("checkpoint"), Path, ENeed::Required, Out.Checkpoint)
				&& ReadWitnesses(R, Object, Path, Out.Fields);
		case EElysiumArenaAction::NpcSingleRoundFinishReload:
		case EElysiumArenaAction::NoRagdollDeath:
			if (RowKind == ERowKind::MapCast) return R.Fail(Path, TEXT("fixture requires Green Room"));
			return ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target);
		case EElysiumArenaAction::InvalidMarker:
			if (RowKind == ERowKind::MapCast) return R.Fail(Path, TEXT("fixture requires Green Room"));
			if (!ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target)
				|| !ReadString(R, Object, TEXT("control"), Path, ENeed::Required, Out.Control)) return false;
			return Out.Control == TEXT("missing_place") || Out.Control == TEXT("missing_occupant") || Out.Control == TEXT("valid")
				? true : R.Fail(Path, TEXT("unknown marker control"));
		case EElysiumArenaAction::CorruptCheckpoint:
		case EElysiumArenaAction::RestoreBase:
		{
			if (RowKind == ERowKind::MapCast) return R.Fail(Path, TEXT("fixture requires Green Room"));
			if (!ReadString(R, Object, TEXT("checkpoint"), Path, ENeed::Required, Out.Checkpoint)) return false;
			if (Out.Do == EElysiumArenaAction::CorruptCheckpoint)
			{
				if (!ReadString(R, Object, TEXT("control"), Path, ENeed::Required, Out.Control)) return false;
				return Out.Control == TEXT("crc") || Out.Control == TEXT("missing_cine") || Out.Control == TEXT("missing_target") || Out.Control == TEXT("missing_path")
					? true : R.Fail(Path, TEXT("unknown checkpoint corruption"));
			}
			const TSharedPtr<FJsonValue>* Base = FindValue(Object, TEXT("param"));
			return Base && ReadValue(R, *Base, Field(Path, TEXT("param")), Out.Param) && Out.Param.Type == FElysiumArenaValue::EType::Number
				? true : R.Fail(Path, TEXT("restore_base requires numeric param"));
		}
		case EElysiumArenaAction::DamageMemory:
		case EElysiumArenaAction::ReserveSpot:
		case EElysiumArenaAction::StartNpcGroundGate:
		{
			if (RowKind == ERowKind::MapCast) return R.Fail(Path, TEXT("fixture requires Green Room"));
			if (!ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target)
				|| !ReadString(R, Object, TEXT("control"), Path, ENeed::Required, Out.Control)) return false;
			const bool bValid = Out.Do == EElysiumArenaAction::DamageMemory ? (Out.Control == TEXT("unknown_no_see") || Out.Control == TEXT("known") || Out.Control == TEXT("see_enemy"))
				: Out.Do == EElysiumArenaAction::ReserveSpot ? (Out.Control == TEXT("full") || Out.Control == TEXT("occupied") || Out.Control == TEXT("exhausted_clearance") || Out.Control == TEXT("normal"))
				: (Out.Control == TEXT("fly") || Out.Control == TEXT("swim") || Out.Control == TEXT("capability4") || Out.Control == TEXT("normal"));
			if (!bValid) return R.Fail(Path, TEXT("unknown fixture control"));
			if (Out.Do == EElysiumArenaAction::DamageMemory)
			{
				const TSharedPtr<FJsonValue>* Damage = FindValue(Object, TEXT("param"));
				if (!Damage || !ReadValue(R, *Damage, Field(Path, TEXT("param")), Out.Param)
					|| Out.Param.Type != FElysiumArenaValue::EType::Number || Out.Param.Number < 0.0)
					return R.Fail(Path, TEXT("damage_memory requires nonnegative numeric param"));
				if (!ReadString(R, Object, TEXT("inflictor"), Path, ENeed::Optional, Out.Inflictor)) return false;
			}
			return Out.Do == EElysiumArenaAction::StartNpcGroundGate
				? true : ReadString(R, Object, TEXT("attacker"), Path, ENeed::Required, Out.Attacker);
		}
		case EElysiumArenaAction::PlayerTeleport:
		case EElysiumArenaAction::PlayerWalk:
			if (!ReadAt(R, Object, TEXT("at"), Path, Out.At) || !ReadFace(R, Object, Path, Out.Face))
			{
				return false;
			}
			if (!Out.At.bSet)
			{
				return R.Fail(Field(Path, TEXT("at")), TEXT("required: where the player goes"));
			}
			return true;
		case EElysiumArenaAction::Fire:
		{
			if (!ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target)
				|| !ReadString(R, Object, TEXT("input"), Path, ENeed::Required, Out.Input)
				|| !ReadString(R, Object, TEXT("activator"), Path, ENeed::Optional, Out.Activator))
			{
				return false;
			}
			const TSharedPtr<FJsonValue>* Param = FindValue(Object, TEXT("param"));
			return Param == nullptr || ReadValue(R, *Param, Field(Path, TEXT("param")), Out.Param);
		}
		case EElysiumArenaAction::SeedHealth:
		{
			if (RowKind == ERowKind::MapCast) return R.Fail(Path, TEXT("seed_health is an arena fixture action"));
			if (!ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target)) return false;
			const TSharedPtr<FJsonValue>* Param = FindValue(Object, TEXT("param"));
			if (Param == nullptr || !ReadValue(R, *Param, Field(Path, TEXT("param")), Out.Param)) return false;
			if (Out.Param.Type != FElysiumArenaValue::EType::Number || Out.Param.Number <= 0
				|| Out.Param.Number > MAX_int32 || FMath::FloorToDouble(Out.Param.Number) != Out.Param.Number)
				return R.Fail(Path, TEXT("seed_health param must be a positive integer cap"));
			return true;
		}
		case EElysiumArenaAction::DamagePacket:
		{
			if (!ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target)
				|| !ReadString(R, Object, TEXT("attacker"), Path, ENeed::Optional, Out.Attacker)
				|| !ReadString(R, Object, TEXT("inflictor"), Path, ENeed::Optional, Out.Inflictor)) return false;
			if (Out.Attacker.IsEmpty()) Out.Attacker = TEXT("none"); // absent-attacker retail arm, 0x10265ed0
			const TSharedPtr<FJsonValue>* PacketAmount = FindValue(Object, TEXT("param"));
			if (!PacketAmount || !ReadValue(R, *PacketAmount, Field(Path, TEXT("param")), Out.Param)) return false;
			return Out.Param.Type == FElysiumArenaValue::EType::Number && Out.Param.Number >= 0
				? true : R.Fail(Path, TEXT("damage_packet param must be a nonnegative amount"));
		}
		case EElysiumArenaAction::EntityCall:
		{
			// The function's allowlist verdict is the run's (a refused call is a script failure the
			// record can state with `expect_fail`), not a parse error: the record must still run.
			if (!ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target)
				|| !ReadString(R, Object, TEXT("function"), Path, ENeed::Required, Out.Function))
			{
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* Args = nullptr;
			if (!ReadArray(R, Object, TEXT("args"), Path, Args)) return false;
			for (int32 ArgIndex = 0; Args && ArgIndex < Args->Num(); ++ArgIndex)
			{
				const FString ArgPath = Field(Path, *Indexed(TEXT("args"), ArgIndex));
				FElysiumArenaCallArg& Arg = Out.Args.AddDefaulted_GetRef();
				if ((*Args)[ArgIndex].IsValid() && (*Args)[ArgIndex]->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> Reference = (*Args)[ArgIndex]->AsObject();
					if (!CheckFields(R, *Reference, ArgPath, { TEXT("fixture") })
						|| !ReadString(R, *Reference, TEXT("fixture"), ArgPath, ENeed::Required, Arg.Fixture)) return false;
				}
				else if (!ReadValue(R, (*Args)[ArgIndex], ArgPath, Arg.Value) || Arg.Value.Type == FElysiumArenaValue::EType::None)
				{
					return R.Fail(ArgPath, TEXT("an argument is a boolean, a number, a string or {\"fixture\": id}"));
				}
			}
			return true;
		}
		case EElysiumArenaAction::Kill:
			return ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target);
		case EElysiumArenaAction::Console:
			return ReadString(R, Object, TEXT("command"), Path, ENeed::Required, Out.Command);
		case EElysiumArenaAction::Spawn:
		{
			TSharedPtr<FJsonObject> Row;
			if (!ReadObject(R, Object, TEXT("row"), Path, Row))
			{
				return false;
			}
			if (!Row.IsValid())
			{
				return R.Fail(Field(Path, TEXT("row")), TEXT("required: the row to spawn"));
			}
			if (!ReadRow(R, *Row, Field(Path, TEXT("row")), ERowKind::ArenaRow, Out.Row)) return false;
			// Disposable map witness must state real coordinates; 0x101a2e40 factory chain.
			return RowKind != ERowKind::MapCast || Out.Row.At.bCoordinates
				? true : R.Fail(Path, TEXT("map spawn requires explicit coordinates"));
		}
		case EElysiumArenaAction::PlayerCrouch:
			if (FindValue(Object, TEXT("on")) == nullptr)
			{
				return R.Fail(Field(Path, TEXT("on")), TEXT("required: true to crouch, false to stand"));
			}
			return ReadBool(R, Object, TEXT("on"), Path, Out.bOn);
		case EElysiumArenaAction::LightPin:
		{
			if (!bHasValue)
			{
				return R.Fail(Field(Path, TEXT("value")), TEXT("required: the light in [0, 1], or null to release"));
			}
			const TSharedPtr<FJsonValue>* Value = FindValue(Object, TEXT("value"));
			if (Value == nullptr)
			{
				Out.bLightRelease = true;
				return true;
			}
			if ((*Value)->Type != EJson::Number || !FMath::IsFinite((*Value)->AsNumber())
				|| (*Value)->AsNumber() < 0.0 || (*Value)->AsNumber() > 1.0)
			{
				return R.Fail(Field(Path, TEXT("value")), TEXT("a normalized light in [0, 1], or null to release"));
			}
			Out.Light = (*Value)->AsNumber();
			return true;
		}
		case EElysiumArenaAction::DialogChoose:
		{
			bool bHasIndex = false;
			double Index = 0.0;
			if (!ReadNumber(R, Object, TEXT("index"), Path, ENeed::Optional, Index, bHasIndex)
				|| !ReadBool(R, Object, TEXT("end"), Path, Out.bDialogEnd))
			{
				return false;
			}
			if (FindValue(Object, TEXT("end")) != nullptr && !Out.bDialogEnd)
			{
				return R.Fail(Field(Path, TEXT("end")), TEXT("`end` is only ever true: give `index` to pick a row"));
			}
			if (bHasIndex == Out.bDialogEnd)
			{
				return R.Fail(Path, TEXT("`dialog_choose` takes `index` (a response row) or `end: true`, exactly one"));
			}
			if (bHasIndex)
			{
				if (FMath::Frac(Index) != 0.0 || Index > static_cast<double>(MAX_int32))
				{
					return R.Fail(Field(Path, TEXT("index")), TEXT("a whole number >= 0, the row as the turn lists it"));
				}
				Out.ChoiceIndex = static_cast<int32>(Index);
			}
			return true;
		}
		default:
			return R.Fail(Field(Path, TEXT("do")), TEXT("unhandled action"));
		}
	}

	bool ReadPlayer(FReader& R, const FJsonObject& Root, FElysiumArenaPlayer& Out)
	{
		Out = FElysiumArenaPlayer();
		TSharedPtr<FJsonObject> Player;
		if (!ReadObject(R, Root, TEXT("player"), FString(), Player))
		{
			return false;
		}
		if (!Player.IsValid())
		{
			return true;
		}
		const FString Path(TEXT("player"));
		if (!CheckFields(R, *Player, Path, { TEXT("at"), TEXT("face"), TEXT("armed"), TEXT("notarget") })
			|| !ReadAt(R, *Player, TEXT("at"), Path, Out.At)
			|| !ReadFace(R, *Player, Path, Out.Face)
			|| !ReadBool(R, *Player, TEXT("notarget"), Path, Out.bNoTarget))
		{
			return false;
		}
		if (Out.Face.Kind == FElysiumArenaFace::EKind::Player)
		{
			return R.Fail(Field(Path, TEXT("face")), TEXT("the player cannot face itself"));
		}
		if (const TSharedPtr<FJsonValue>* Armed = FindValue(*Player, TEXT("armed")))
		{
			if ((*Armed)->Type == EJson::Boolean)
			{
				Out.bArmed = (*Armed)->AsBool();
			}
			else if ((*Armed)->Type == EJson::String && !(*Armed)->AsString().IsEmpty())
			{
				Out.ArmedItem = (*Armed)->AsString();
			}
			else
			{
				return R.Fail(Field(Path, TEXT("armed")), TEXT("true, false, or one item classname"));
			}
		}
		return true;
	}

	bool ReadFromMap(FReader& R, const FJsonObject& Root, FElysiumArenaFromMap& Out)
	{
		Out = FElysiumArenaFromMap();
		TSharedPtr<FJsonObject> FromMap;
		if (!ReadObject(R, Root, TEXT("from_map"), FString(), FromMap))
		{
			return false;
		}
		if (!FromMap.IsValid())
		{
			return true;
		}
		const FString Path(TEXT("from_map"));
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (!CheckFields(R, *FromMap, Path, { TEXT("map"), TEXT("names"), TEXT("anchor"), TEXT("at") })
			|| !ReadString(R, *FromMap, TEXT("map"), Path, ENeed::Required, Out.Map)
			|| !ReadArray(R, *FromMap, TEXT("names"), Path, Names)
			|| !ReadString(R, *FromMap, TEXT("anchor"), Path, ENeed::Required, Out.Anchor)
			|| !ReadAt(R, *FromMap, TEXT("at"), Path, Out.At))
		{
			return false;
		}
		if (Names == nullptr || Names->IsEmpty())
		{
			return R.Fail(Field(Path, TEXT("names")), TEXT("required: the targetnames to stand"));
		}
		for (int32 Index = 0; Index < Names->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Name = (*Names)[Index];
			if (!Name.IsValid() || Name->Type != EJson::String || Name->AsString().IsEmpty())
			{
				return R.Fail(Field(Path, *Indexed(TEXT("names"), Index)), TEXT("must be a targetname"));
			}
			Out.Names.Add(Name->AsString());
		}
		if (!Out.Names.ContainsByPredicate([&Out](const FString& Name)
			{ return Name.Equals(Out.Anchor, ESearchCase::IgnoreCase); }))
		{
			return R.Fail(Field(Path, TEXT("anchor")), TEXT("must be one of `names`"));
		}
		if (!Out.At.bSet)
		{
			return R.Fail(Field(Path, TEXT("at")), TEXT("required: where the anchor row lands"));
		}
		Out.bSet = true;
		return true;
	}

	bool IsValidName(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return false;
		}
		for (const TCHAR Char : Name)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_') && Char != TEXT('-'))
			{
				return false;
			}
		}
		return true;
	}

	// The typed `fixtures` catalog: an id, a kind and that kind's configuration, strictly.
	bool ReadFixtures(FReader& R, const FJsonObject& Root, TArray<FElysiumArenaFixture>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!ReadArray(R, Root, TEXT("fixtures"), FString(), Items)) return false;
		for (int32 Index = 0; Items && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("fixtures"), Index);
			TSharedPtr<FJsonObject> Item;
			FElysiumArenaFixture& Fixture = Out.AddDefaulted_GetRef();
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !CheckFields(R, *Item, Path, { TEXT("id"), TEXT("kind"), TEXT("values"), TEXT("text") })
				|| !ReadString(R, *Item, TEXT("id"), Path, ENeed::Required, Fixture.Id)
				|| !ReadString(R, *Item, TEXT("kind"), Path, ENeed::Required, Fixture.Kind))
			{
				return false;
			}
			for (int32 Earlier = 0; Earlier < Index; ++Earlier)
			{
				if (Out[Earlier].Id.Equals(Fixture.Id, ESearchCase::CaseSensitive))
				{
					return R.Fail(Field(Path, TEXT("id")), FString::Printf(TEXT("'%s' is already fixtures[%d]'s id"), *Fixture.Id, Earlier));
				}
			}
			bool bKnownKind = false;
			for (const TCHAR* Known : GFixtureKinds)
			{
				bKnownKind |= Fixture.Kind.Equals(Known, ESearchCase::CaseSensitive);
			}
			if (!bKnownKind)
			{
				TArray<FString> Names;
				for (const TCHAR* Known : GFixtureKinds)
				{
					Names.Add(Known);
				}
				return R.Fail(Field(Path, TEXT("kind")), FString::Printf(TEXT("'%s' is not a fixture kind (%s)"),
					*Fixture.Kind, *FString::Join(Names, TEXT(", "))));
			}
			if (Fixture.Kind == TEXT("text"))
			{
				// `text`: raw text a reader is handed whole (`entity_call KeyValues_Lex` / `KeyValues_Parse`).
				if (FindValue(*Item, TEXT("values")) != nullptr)
				{
					return R.Fail(Field(Path, TEXT("values")), TEXT("a `text` fixture takes `text`, not `values`"));
				}
				if (!ReadString(R, *Item, TEXT("text"), Path, ENeed::Required, Fixture.Text)) return false;
				continue;
			}
			if (FindValue(*Item, TEXT("text")) != nullptr)
			{
				return R.Fail(Field(Path, TEXT("text")), TEXT("only a `text` fixture takes `text`"));
			}
			// `keyvalues`: a controlled KeyValues table, read back by `entity_field` on `fixture:<id>`.
			TSharedPtr<FJsonObject> Values;
			if (!ReadObject(R, *Item, TEXT("values"), Path, Values)) return false;
			if (!Values.IsValid() || Values->Values.IsEmpty())
			{
				return R.Fail(Field(Path, TEXT("values")), TEXT("required: the table's keys and values"));
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Values->Values)
			{
				FString Text;
				if (!KeyText(R, Pair.Value, Field(Field(Path, TEXT("values")), *Pair.Key), Text)) return false;
				Fixture.Values.Add(Pair.Key, Text);
			}
		}
		return true;
	}

	bool ReadRecord(FReader& R, const FJsonObject& Root, FElysiumArenaScenario& Out)
	{
		if (!CheckFields(R, Root, FString(), { TEXT("name"), TEXT("about"), TEXT("stage"), TEXT("seed"),
				TEXT("duration"), TEXT("known_red"), TEXT("expect_fail"), TEXT("shares_map"), TEXT("player"),
				TEXT("cast"), TEXT("rows"), TEXT("from_map"), TEXT("script"), TEXT("expect"), TEXT("never"),
				TEXT("probes"), TEXT("notes"), TEXT("initial_weapon_state"), TEXT("fixtures") }))
		{
			return false;
		}
		double Seed = 0.0;
		bool bSeed = false;
		bool bDuration = false;
		FString Notes;
		if (!ReadString(R, Root, TEXT("name"), FString(), ENeed::Required, Out.Name)
			|| !ReadString(R, Root, TEXT("about"), FString(), ENeed::Required, Out.About)
			|| !ReadString(R, Root, TEXT("stage"), FString(), ENeed::Optional, Out.Stage)
			|| !ReadNumber(R, Root, TEXT("seed"), FString(), ENeed::Optional, Seed, bSeed)
			|| !ReadNumber(R, Root, TEXT("duration"), FString(), ENeed::Required, Out.Duration, bDuration)
			|| !ReadString(R, Root, TEXT("known_red"), FString(), ENeed::Optional, Out.KnownRed)
			|| !ReadBool(R, Root, TEXT("expect_fail"), FString(), Out.bExpectFail)
			|| !ReadBool(R, Root, TEXT("shares_map"), FString(), Out.bSharesMap)
			|| !ReadString(R, Root, TEXT("notes"), FString(), ENeed::Optional, Notes))
		{
			return false;
		}
		if (!IsValidName(Out.Name))
		{
			return R.Fail(TEXT("name"), TEXT("letters, digits, `_` and `-` only: it names the trace file"));
		}
		if (Out.Duration <= 0.0)
		{
			return R.Fail(TEXT("duration"), TEXT("must be positive"));
		}
		if (bSeed)
		{
			if (Seed != FMath::RoundToDouble(Seed) || Seed > static_cast<double>(MAX_int32))
			{
				return R.Fail(TEXT("seed"), TEXT("must be a whole number"));
			}
			Out.Seed = static_cast<int32>(Seed);
		}
		if (Out.Stage.IsEmpty())
		{
			Out.Stage = TEXT("arena");
		}
		if (Out.Stage.Equals(TEXT("arena"), ESearchCase::CaseSensitive))
		{
			Out.StageMap.Reset();
		}
		else if (Out.Stage.StartsWith(TEXT("map:"), ESearchCase::CaseSensitive) && Out.Stage.Len() > 4)
		{
			Out.StageMap = Out.Stage.Mid(4);
		}
		else
		{
			return R.Fail(TEXT("stage"), FString::Printf(TEXT("'%s' is neither `arena` nor `map:<map>`"), *Out.Stage));
		}
		const bool bMapHost = !Out.StageMap.IsEmpty();
		if (Out.bSharesMap && !bMapHost)
		{
			return R.Fail(TEXT("shares_map"), TEXT("only a map record shares its map"));
		}

		if (!ReadPlayer(R, Root, Out.Player))
		{
			return false;
		}

		const ERowKind CastKind = bMapHost ? ERowKind::MapCast : ERowKind::ArenaRow;
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!ReadArray(R, Root, TEXT("cast"), FString(), Items))
		{
			return false;
		}
		for (int32 Index = 0; Items != nullptr && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("cast"), Index);
			TSharedPtr<FJsonObject> Item;
			FElysiumArenaRow& Row = Out.Cast.AddDefaulted_GetRef();
			if (!ElementObject(R, (*Items)[Index], Path, Item) || !ReadRow(R, *Item, Path, CastKind, Row))
			{
				return false;
			}
			if (Row.Name.IsEmpty())
			{
				return R.Fail(Field(Path, TEXT("name")), TEXT("required: a cast row is addressed by name"));
			}
		}

		if (!ReadArray(R, Root, TEXT("initial_weapon_state"), FString(), Items)) return false;
		if (Items && bMapHost) return R.Fail(TEXT("initial_weapon_state"), TEXT("explicit fixture requires Green Room"));
		TSet<FString> FixtureNames;
		for (int32 Index = 0; Items && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("initial_weapon_state"), Index);
			TSharedPtr<FJsonObject> Item;
			FElysiumArenaInitialWeaponState& Initial = Out.InitialWeaponState.AddDefaulted_GetRef();
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !CheckFields(R, *Item, Path, {TEXT("who"), TEXT("weapon"), TEXT("magazine"), TEXT("reserve"), TEXT("fake_reload_count")})
				|| !ReadString(R, *Item, TEXT("who"), Path, ENeed::Required, Initial.Who)
				|| !ReadString(R, *Item, TEXT("weapon"), Path, ENeed::Required, Initial.Weapon)) return false;
			const FString Folded = Initial.Who.ToLower();
			if (FixtureNames.Contains(Folded)) return R.Fail(Path, TEXT("duplicate weapon fixture actor"));
			FixtureNames.Add(Folded);
			for (const TCHAR* Key : {TEXT("magazine"), TEXT("reserve"), TEXT("fake_reload_count")})
			{
				double Number = 0.0; bool bPresent = false;
				if (!ReadNumber(R, *Item, Key, Path, ENeed::Required, Number, bPresent)) return false;
				if (Number < 0.0 || Number > MAX_int32 || Number != FMath::RoundToDouble(Number)) return R.Fail(Field(Path, Key), TEXT("nonnegative integer required"));
				if (FCString::Strcmp(Key, TEXT("magazine")) == 0) Initial.Magazine = static_cast<int32>(Number);
				else if (FCString::Strcmp(Key, TEXT("reserve")) == 0) Initial.Reserve = static_cast<int32>(Number);
				else Initial.FakeReloadCount = static_cast<int32>(Number);
			}
		}

		if (!ReadArray(R, Root, TEXT("rows"), FString(), Items))
		{
			return false;
		}
		if (Items != nullptr && bMapHost)
		{
			return R.Fail(TEXT("rows"), TEXT("a map host stages no rows: its entities are the map's"));
		}
		for (int32 Index = 0; Items != nullptr && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("rows"), Index);
			TSharedPtr<FJsonObject> Item;
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !ReadRow(R, *Item, Path, ERowKind::ArenaRow, Out.Rows.AddDefaulted_GetRef()))
			{
				return false;
			}
		}

		if (!ReadFromMap(R, Root, Out.FromMap))
		{
			return false;
		}
		if (Out.FromMap.bSet && bMapHost)
		{
			return R.Fail(TEXT("from_map"), TEXT("a map host stages no rows: its entities are the map's"));
		}

		if (!ReadFixtures(R, Root, Out.Fixtures)) return false;

		if (!ReadArray(R, Root, TEXT("expect"), FString(), Items))
		{
			return false;
		}
		for (int32 Index = 0; Items != nullptr && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("expect"), Index);
			TSharedPtr<FJsonObject> Item;
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !ReadMatch(R, *Item, Path, EMatchKind::Expect, Out.Expect.AddDefaulted_GetRef()))
			{
				return false;
			}
			const FString& Label = Out.Expect.Last().Label;
			for (int32 Earlier = 0; !Label.IsEmpty() && Earlier < Index; ++Earlier)
			{
				if (Out.Expect[Earlier].Label.Equals(Label, ESearchCase::CaseSensitive))
				{
					return R.Fail(Field(Path, TEXT("label")), FString::Printf(
						TEXT("'%s' is already expect[%d]'s label"), *Label, Earlier));
				}
			}
		}

		if (!ReadArray(R, Root, TEXT("never"), FString(), Items))
		{
			return false;
		}
		for (int32 Index = 0; Items != nullptr && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("never"), Index);
			TSharedPtr<FJsonObject> Item;
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !ReadMatch(R, *Item, Path, EMatchKind::Never, Out.Never.AddDefaulted_GetRef()))
			{
				return false;
			}
			FElysiumArenaMatch& Never = Out.Never.Last();
			if (Never.bFrom && Never.From > Out.Duration)
			{
				return R.Fail(Field(Path, TEXT("from")), FString::Printf(
					TEXT("%.2f is past the record's duration %.2f; the window would never open"),
					Never.From, Out.Duration));
			}
			if (!Never.After.IsEmpty())
			{
				Never.AfterIndex = Out.Expect.IndexOfByPredicate([&Never](const FElysiumArenaMatch& M)
					{ return M.Label.Equals(Never.After, ESearchCase::CaseSensitive); });
				if (Never.AfterIndex == INDEX_NONE)
				{
					return R.Fail(Field(Path, TEXT("after")), FString::Printf(
						TEXT("'%s' labels no expectation"), *Never.After));
				}
			}
		}

		if (!ReadArray(R, Root, TEXT("probes"), FString(), Items))
		{
			return false;
		}
		for (int32 Index = 0; Items != nullptr && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("probes"), Index);
			TSharedPtr<FJsonObject> Item;
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !ReadProbe(R, *Item, Path, Out.Duration, Out.Probes.AddDefaulted_GetRef()))
			{
				return false;
			}
		}

		if (!ReadArray(R, Root, TEXT("script"), FString(), Items))
		{
			return false;
		}
		for (int32 Index = 0; Items != nullptr && Index < Items->Num(); ++Index)
		{
			const FString Path = Indexed(TEXT("script"), Index);
			TSharedPtr<FJsonObject> Item;
			if (!ElementObject(R, (*Items)[Index], Path, Item)
				|| !ReadAction(R, *Item, Path, CastKind, Out.Script.AddDefaulted_GetRef()))
			{
				return false;
			}
			const FElysiumArenaAction& Action = Out.Script.Last();
			for (const FElysiumArenaCallArg& Arg : Action.Args)
			{
				if (!Arg.Fixture.IsEmpty() && !Out.Fixtures.ContainsByPredicate([&Arg](const FElysiumArenaFixture& F)
					{ return F.Id.Equals(Arg.Fixture, ESearchCase::CaseSensitive); }))
				{
					return R.Fail(Field(Path, TEXT("args")), FString::Printf(TEXT("'%s' names no fixture"), *Arg.Fixture));
				}
			}
			if (Action.Do >= EElysiumArenaAction::Save && Action.bAtTime && Action.Time > Out.Duration)
				return R.Fail(Field(Path, TEXT("t")), TEXT("transaction action is past duration"));
			if (!Action.After.IsEmpty() && !Out.Expect.ContainsByPredicate([&Action](const FElysiumArenaMatch& M)
				{ return M.Label.Equals(Action.After, ESearchCase::CaseSensitive); }))
			{
				return R.Fail(Field(Path, TEXT("after")), FString::Printf(
					TEXT("'%s' labels no expectation"), *Action.After));
			}
		}

		// Checkpoint names are single-assignment and references must name a captured field (0x1027bf50).
		TMap<FString, const FElysiumArenaAction*> Checkpoints;
		for (const FElysiumArenaAction& Action : Out.Script)
		{
			if (Action.Do == EElysiumArenaAction::Save && !Action.Checkpoint.IsEmpty())
			{
				if (Checkpoints.Contains(Action.Checkpoint)) return R.Fail(TEXT("script"), TEXT("duplicate checkpoint"));
				Checkpoints.Add(Action.Checkpoint, &Action);
			}
		}
		auto HasSavedWord = [&Checkpoints](const FString& Name, const FString& Who, const FString& Word)
		{
			const FElysiumArenaAction* const* Saved = Checkpoints.Find(Name);
			return Saved && (*Saved)->Fields.ContainsByPredicate([&](const FElysiumArenaWitness& Entry) { return Entry.Who == Who && Entry.Field == Word; });
		};
		for (const FElysiumArenaAction& Action : Out.Script)
		{
			if (Action.Do == EElysiumArenaAction::Save || Action.Checkpoint.IsEmpty()) continue;
			if (!Checkpoints.Contains(Action.Checkpoint)) return R.Fail(TEXT("script"), TEXT("missing checkpoint"));
			for (const FElysiumArenaWitness& Word : Action.Fields)
				if (!HasSavedWord(Action.Checkpoint, Word.Who, Word.Field)) return R.Fail(TEXT("script"), TEXT("comparison field was not captured"));
		}
		for (const FElysiumArenaProbeSpec& Probe : Out.Probes)
			if (Probe.Probe == EElysiumArenaProbe::EntityField && Probe.Who.StartsWith(TEXT("fixture:"))
				&& !Out.Fixtures.ContainsByPredicate([&Probe](const FElysiumArenaFixture& F)
					{ return F.Id.Equals(Probe.Who.Mid(8), ESearchCase::CaseSensitive); }))
				return R.Fail(TEXT("probes"), FString::Printf(TEXT("'%s' names no fixture"), *Probe.Who));
		for (const FElysiumArenaProbeSpec& Probe : Out.Probes)
			if (!Probe.Checkpoint.IsEmpty() && !HasSavedWord(Probe.Checkpoint, Probe.Who, Probe.Field)) return R.Fail(TEXT("probes"), TEXT("missing checkpoint or uncaptured field"));

		if (Out.Expect.IsEmpty() && Out.Never.IsEmpty() && Out.Probes.IsEmpty())
		{
			return R.Fail(TEXT("expect"), TEXT("a record with no expect, never or probes asserts nothing"));
		}
		return true;
	}
}

FString FElysiumArenaValue::Describe() const
{
	switch (Type)
	{
	case EType::Bool:   return bBool ? TEXT("true") : TEXT("false");
	case EType::Number: return FString::Printf(TEXT("%g"), Number);
	case EType::String: return FString::Printf(TEXT("\"%s\""), *String);
	default:            return TEXT("null");
	}
}

namespace ElysiumArenaScenario
{

FString ScenarioRoot()
{
	return FPaths::Combine(FPaths::ProjectDir(), TEXT("Arena"), TEXT("scenarios"));
}

TArrayView<const TCHAR* const> TraceKinds()
{
	return MakeArrayView(ElysiumArenaScenarioParse::GTraceKinds);
}

bool ParseText(const FString& Text, const FString& File, FElysiumArenaScenario& Out, FString& OutError)
{
	Out = FElysiumArenaScenario();
	Out.File = File;
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = FString::Printf(TEXT("%s: not a JSON object (%s)"), *File, *Reader->GetErrorMessage());
		return false;
	}
	ElysiumArenaScenarioParse::FReader R(File);
	if (!ElysiumArenaScenarioParse::ReadRecord(R, *Root, Out))
	{
		OutError = R.Error;
		return false;
	}
	return true;
}

bool LoadFile(const FString& Path, FElysiumArenaScenario& Out, FString& OutError)
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("%s: could not be read"), *Path);
		return false;
	}
	return ParseText(Text, Path, Out, OutError);
}

void LoadAll(TArray<FElysiumArenaScenario>& Out, TArray<FString>& OutErrors)
{
	Out.Reset();
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *ScenarioRoot(), TEXT("*.json"),
		/*Files=*/true, /*Directories=*/false);
	Files.Sort();
	for (const FString& File : Files)
	{
		FElysiumArenaScenario Record;
		FString Error;
		if (!LoadFile(File, Record, Error))
		{
			OutErrors.Add(Error);
			continue;
		}
		if (const FElysiumArenaScenario* Same = Out.FindByPredicate([&Record](const FElysiumArenaScenario& Other)
			{ return Other.Name.Equals(Record.Name, ESearchCase::IgnoreCase); }))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: name: '%s' is already %s's name"), *File, *Record.Name,
				*Same->File));
			continue;
		}
		Out.Add(MoveTemp(Record));
	}
	Out.Sort([](const FElysiumArenaScenario& A, const FElysiumArenaScenario& B) { return A.Name < B.Name; });
}


// Typed harness vocabulary for the retained retail words; 0x1027bf50/0x102998c0/0x102d9240/0x10265ed0/0x10255077.
bool WitnessType(const FString& Field, FElysiumArenaValue::EType& Out)
{
	for (const TCHAR* Name : {
		TEXT("clock"), TEXT("world_generation"), TEXT("task.id"), TEXT("life.state"), TEXT("render.alpha"), TEXT("solid.type"), TEXT("solid.flags"), TEXT("effects"), TEXT("outputs.remaining"), TEXT("weapon.clip"), TEXT("weapon.reserve"), TEXT("floor_drop.performed"), TEXT("floor_drop.skipped"),
		TEXT("task.index"), TEXT("task.status"), TEXT("task.failure"),
		TEXT("task.started"), TEXT("task.task_started"), TEXT("task.wait"),
		TEXT("task.move_wait"), TEXT("think.next"), TEXT("anim.sequence"),
		TEXT("anim.cycle"), TEXT("anim.rate"), TEXT("anim.time"),
		TEXT("anim.previous_time"), TEXT("anim.last_event"), TEXT("anim.ground_speed"),
		TEXT("anim.yaw_speed"), TEXT("move_shoot.next"), TEXT("move_shoot.burst"),
		TEXT("move_shoot.min_burst"), TEXT("move_shoot.max_burst"), TEXT("move_shoot.pause_min"),
		TEXT("move_shoot.pause_max"), TEXT("move_shoot.initial_delay"), TEXT("weapon.next_primary"),
		TEXT("weapon.next_secondary"), TEXT("weapon.idle"), TEXT("nav.type"),
		TEXT("nav.flags"), TEXT("nav.arrival"), TEXT("nav.retry_interval"),
		TEXT("nav.retry_duration"), TEXT("nav.retry_next"), TEXT("nav.timeout"),
		TEXT("memory.last_seen"), TEXT("damage.sum"), TEXT("damage.time"),
		TEXT("place.capacity"), TEXT("place.count"), TEXT("place.failed_attempts"),
		TEXT("place.in_use"), TEXT("place.ring_index"), TEXT("place.reservations"),
		TEXT("place.releases"), TEXT("senses.pass"), TEXT("los.cache"),
		TEXT("los.last_clear"), TEXT("coordinator.normal.count"), TEXT("coordinator.player.count"),
		TEXT("coordinator.boss.count"), TEXT("coordinator.normal.cap"), TEXT("coordinator.player.cap"),
		TEXT("coordinator.boss.cap")
	})
		if (Field == Name) { Out = FElysiumArenaValue::EType::Number; return true; }
	for (const TCHAR* Name : {
		TEXT("hidden"), TEXT("life.dead"), TEXT("anim.finished"), TEXT("anim.past_half"),
		TEXT("move_shoot.active"), TEXT("weapon.reload"), TEXT("weapon.jam"),
		TEXT("weapon.interrupt"), TEXT("senses.can_sense"), TEXT("senses.gathered"),
		TEXT("dialog.open"), TEXT("dialog.partner_live"), TEXT("los.player"),
		TEXT("los.pvs")
	})
		if (Field == Name) { Out = FElysiumArenaValue::EType::Bool; return true; }
	for (const TCHAR* Name : {
		TEXT("map"), TEXT("identity"), TEXT("callback"),
		TEXT("callback.saved"), TEXT("script.owner"), TEXT("weapon.owner"),
		TEXT("nav.target"), TEXT("memory.enemy"), TEXT("damage.attacker"),
		TEXT("place.identity"), TEXT("senses.sighted"), TEXT("dialog.partner"),
		TEXT("coordinator.normal.members"), TEXT("coordinator.player.members"), TEXT("coordinator.boss.members")
	})
		if (Field == Name) { Out = FElysiumArenaValue::EType::String; return true; }
	for (const TCHAR* Prefix : { TEXT("origin"), TEXT("nav.goal"), TEXT("memory.position"), TEXT("damage.position"), TEXT("place.destination"), TEXT("place.bounds_min"), TEXT("place.bounds_max") })
		for (const TCHAR* Axis : { TEXT("x"), TEXT("y"), TEXT("z") })
			if (Field == FString(Prefix) + TEXT(".") + Axis) { Out = FElysiumArenaValue::EType::Number; return true; }
	for (int32 LayerIndex = 0; LayerIndex < 4; ++LayerIndex)
	{
		const FString Prefix = FString::Printf(TEXT("layer.%d."), LayerIndex);
		for (const TCHAR* Word : { TEXT("finished"), TEXT("flags"), TEXT("sequence"), TEXT("cycle"), TEXT("rate"), TEXT("weight"), TEXT("weight_max"), TEXT("blend_in"), TEXT("blend_out"), TEXT("activity"), TEXT("last_event") })
			if (Field == Prefix + Word) { Out = FElysiumArenaValue::EType::Number; return true; }
		if (Field == Prefix + TEXT("auto_kill")) { Out = FElysiumArenaValue::EType::Bool; return true; }
		const FString Ring = FString::Printf(TEXT("place.ring.%d."), LayerIndex);
		if (Field == Ring + TEXT("time")) { Out = FElysiumArenaValue::EType::Number; return true; }
		for (const TCHAR* Bound : { TEXT("min"), TEXT("max") })
			for (const TCHAR* Axis : { TEXT("x"), TEXT("y"), TEXT("z") })
				if (Field == Ring + Bound + TEXT(".") + Axis) { Out = FElysiumArenaValue::EType::Number; return true; }
	}
	// Variable marker rows retain identity + both absolute bounds (0x102d9240).
	TArray<FString> Parts;
	Field.ParseIntoArray(Parts, TEXT("."));
	if (Parts.Num() >= 4 && Parts[0] == TEXT("place") && Parts[1] == TEXT("marker") && Parts[2].IsNumeric() && Parts[2] == LexToString(FCString::Atoi(*Parts[2])) && FCString::Atoi(*Parts[2]) >= 0)
	{
		if (Parts.Num() == 4 && Parts[3] == TEXT("occupant")) { Out = FElysiumArenaValue::EType::String; return true; }
		if (Parts.Num() == 5 && (Parts[3] == TEXT("min") || Parts[3] == TEXT("max")) && (Parts[4] == TEXT("x") || Parts[4] == TEXT("y") || Parts[4] == TEXT("z")))
		{ Out = FElysiumArenaValue::EType::Number; return true; }
	}
	return false;
}

FElysiumArenaValue RebaseWitness(const FString& Field, const FElysiumArenaValue& Saved, double SaveBase, double RestoreBase)
{
	FElysiumArenaValue Expected = Saved;
	if (Saved.Type != FElysiumArenaValue::EType::Number) return Expected;
	EElysiumTimePolicy Policy = EElysiumTimePolicy::Ordinary;
	bool bTime = Field == TEXT("clock") || Field == TEXT("task.started") || Field == TEXT("task.task_started")
		|| Field == TEXT("task.move_wait") || Field == TEXT("nav.retry_next") || Field == TEXT("nav.timeout") || Field == TEXT("anim.time") || Field == TEXT("anim.previous_time")
		|| Field == TEXT("anim.last_event") || Field == TEXT("memory.last_seen") || Field == TEXT("damage.time") || Field == TEXT("weapon.idle");
	if (Field == TEXT("task.wait")) { bTime = true; Policy = EElysiumTimePolicy::Zero; }
	if (Field == TEXT("move_shoot.next")) { bTime = true; Policy = EElysiumTimePolicy::MaxFloat; }
	if (Field == TEXT("think.next"))
	{
		// Entity NextThinkSR's exceptional negative/zero/never encoding is separate (0x100aa140).
		bTime = Saved.Number > 0.0 && Saved.Number != static_cast<double>(MAX_flt);
	}
	// Layer cursor/next weapon attacks and LOS FLOAT words do not shift with the save epoch.
	if (bTime) Expected.Number = FElysiumSaveArchive::DecodeTime(FElysiumSaveArchive::EncodeTime(Saved.Number, SaveBase, Policy), RestoreBase, Policy);
	return Expected;
}

bool WitnessEqual(const FElysiumArenaValue& Saved, const FElysiumArenaValue& Applied, double Tolerance)
{
	if (Saved.Type != Applied.Type) return false;
	switch (Saved.Type)
	{
	case FElysiumArenaValue::EType::Number: return FMath::IsFinite(Saved.Number) && FMath::IsFinite(Applied.Number) && FMath::Abs(Saved.Number - Applied.Number) <= Tolerance;
	case FElysiumArenaValue::EType::Bool: return Saved.bBool == Applied.bBool;
	case FElysiumArenaValue::EType::String: return Saved.String.Equals(Applied.String, ESearchCase::CaseSensitive);
	default: return false;
	}
}

const TCHAR* ActionName(EElysiumArenaAction Action)
{
	switch (Action)
	{
	case EElysiumArenaAction::PlayerTeleport: return TEXT("player_teleport");
	case EElysiumArenaAction::PlayerWalk:     return TEXT("player_walk");
	case EElysiumArenaAction::Fire:           return TEXT("fire");
	case EElysiumArenaAction::Console:        return TEXT("console");
	case EElysiumArenaAction::Spawn:          return TEXT("spawn");
	case EElysiumArenaAction::Kill:           return TEXT("kill");
	case EElysiumArenaAction::PlayerCrouch:   return TEXT("player_crouch");
	case EElysiumArenaAction::LightPin:       return TEXT("light_pin");
	case EElysiumArenaAction::DialogChoose:   return TEXT("dialog_choose");
	case EElysiumArenaAction::SeedHealth: return TEXT("seed_health");
	case EElysiumArenaAction::DamagePacket: return TEXT("damage_packet");
	case EElysiumArenaAction::EntityCall: return TEXT("entity_call");
	case EElysiumArenaAction::Save: return TEXT("save");
	case EElysiumArenaAction::Load: return TEXT("load");
	case EElysiumArenaAction::FreshMap: return TEXT("fresh_map");
	case EElysiumArenaAction::Travel: return TEXT("travel");
	case EElysiumArenaAction::RestoreCompare: return TEXT("restore_compare");
	case EElysiumArenaAction::NpcSingleRoundFinishReload: return TEXT("npc_single_round_finish_reload");
	case EElysiumArenaAction::CorruptCheckpoint: return TEXT("corrupt_checkpoint");
	case EElysiumArenaAction::InvalidMarker: return TEXT("invalid_marker");
	case EElysiumArenaAction::RestoreBase: return TEXT("restore_base");
	case EElysiumArenaAction::NoRagdollDeath: return TEXT("no_ragdoll_death");
	case EElysiumArenaAction::DamageMemory: return TEXT("damage_memory");
	case EElysiumArenaAction::ReserveSpot: return TEXT("reserve_spot");
	case EElysiumArenaAction::StartNpcGroundGate: return TEXT("startnpc_ground_gate");
	default:                                 return TEXT("?");
	}
}

const TCHAR* ProbeName(EElysiumArenaProbe Probe)
{
	switch (Probe)
	{
	case EElysiumArenaProbe::Alive:           return TEXT("alive");
	case EElysiumArenaProbe::Schedule:        return TEXT("schedule");
	case EElysiumArenaProbe::State:           return TEXT("state");
	case EElysiumArenaProbe::Health:          return TEXT("health");
	case EElysiumArenaProbe::Enemy:           return TEXT("enemy");
	case EElysiumArenaProbe::Hint:            return TEXT("hint");
	case EElysiumArenaProbe::HasCondition:    return TEXT("has_condition");
	case EElysiumArenaProbe::OnGround:        return TEXT("on_ground");
	case EElysiumArenaProbe::DistanceTo:      return TEXT("distance_to");
	case EElysiumArenaProbe::SameTeam: return TEXT("same_team");
	case EElysiumArenaProbe::SwingRecordedHit: return TEXT("swing_recorded_hit");
	case EElysiumArenaProbe::OneHitKill: return TEXT("one_hit_kill");
	case EElysiumArenaProbe::TeamSymbol: return TEXT("team_symbol");
	case EElysiumArenaProbe::Wounds: return TEXT("wounds");
	case EElysiumArenaProbe::HealthCap: return TEXT("health_cap");
	case EElysiumArenaProbe::NpcFlags1: return TEXT("npc_flags1");
	case EElysiumArenaProbe::Witness: return TEXT("witness");
	case EElysiumArenaProbe::SpawnFlags: return TEXT("spawn_flags");
	case EElysiumArenaProbe::RenderAlpha: return TEXT("render_alpha");
	case EElysiumArenaProbe::RenderMode: return TEXT("render_mode");
	case EElysiumArenaProbe::Activity: return TEXT("activity");

	case EElysiumArenaProbe::PlayerWeapon:    return TEXT("player_weapon");
	case EElysiumArenaProbe::PlayerCrouched:  return TEXT("player_crouched");
	case EElysiumArenaProbe::PlayerGrappling: return TEXT("player_grappling");
	case EElysiumArenaProbe::Speed2d:         return TEXT("speed2d");
	case EElysiumArenaProbe::MoveYaw:         return TEXT("move_yaw");
	case EElysiumArenaProbe::GroundSpeed:     return TEXT("ground_speed");
	case EElysiumArenaProbe::CorpseOnFloor:   return TEXT("corpse_on_floor");
	case EElysiumArenaProbe::Exists:          return TEXT("exists");
	case EElysiumArenaProbe::EntityField:     return TEXT("entity_field");
	default:                                  return TEXT("?");
	}
}

TArrayView<const TCHAR* const> FixtureKinds()
{
	return MakeArrayView(ElysiumArenaScenarioParse::GFixtureKinds);
}

const TArray<FString>& EntityCallAllowlist()
{
	// Each entry is a retail entry point a story's records drive, added with its dispatch in
	// `FElysiumArenaScenarioRunner::RunAction` (`harness.md`: "keyvalue lex/parse/access").
	static const TArray<FString> Allowed = {
		// L0.audio.keyvalues-lexer: the token wrapper `0x101f2f30` over a `text` fixture until its
		// cursor is NULL -- what `0x101f2360` / `0x101f2180` do to a buffer.
		TEXT("KeyValues_Lex"),
		// L0.audio.keyvalues-tree: `0x101f2180`'s root loop over a `text` fixture (the file's bytes),
		// with an optional second argument naming the target node `0x101f2e20` hands it.
		TEXT("KeyValues_Parse"),
		// L0.audio.scheme-file-load: the KeyValues FILE loader `0x102480f0` (`ElysiumKeyValuesLoader.h`)
		// on the node `target` names, over a file name whose bytes a `text` fixture supplies (or the
		// deployed corpus when none does), with the cache flag -- the call `CSoundScheme::Precache`
		// makes through `0x1022a930`.
		TEXT("KeyValues_LoadFile"),
		// L0.audio.keyvalues-access: the loader class's typed accessors on a loaded node --
		// `GetInt 0x10248bb0`, `GetString 0x10248cd0`, `SetString 0x102490e0` (each through
		// `FindKey 0x10248900`) -- what every one of the loader's sixteen callers reads with; and the
		// harness-built fallback link (`+0x18`, whose retail writer the corpus does not show).
		TEXT("KeyValues_GetInt"),
		TEXT("KeyValues_GetString"),
		TEXT("KeyValues_SetString"),
		TEXT("KeyValues_Chain"),
	};
	return Allowed;
}

const TCHAR* CompareName(EElysiumArenaCompare Compare)
{
	switch (Compare)
	{
	case EElysiumArenaCompare::Equals:  return TEXT("equals");
	case EElysiumArenaCompare::Match:   return TEXT("match");
	case EElysiumArenaCompare::Less:    return TEXT("less");
	case EElysiumArenaCompare::Greater: return TEXT("greater");
	default:                            return TEXT("?");
	}
}

FString DescribeMatch(const FElysiumArenaMatch& Match)
{
	const FString Site = Match.Site.IsEmpty() ? FString() : FString::Printf(TEXT("[%s]"), *Match.Site);
	return FString::Printf(TEXT("%s %s%s \"%s\"%s"), Match.Who.IsEmpty() ? TEXT("*") : *Match.Who,
		*Match.Kind.ToString(), *Site, *Match.Match, Match.bRegex ? TEXT(" (regex)") : TEXT(""));
}

} // namespace ElysiumArenaScenario

#endif // !UE_BUILD_SHIPPING
