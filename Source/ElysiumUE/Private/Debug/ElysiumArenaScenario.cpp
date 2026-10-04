#include "Debug/ElysiumArenaScenario.h"

#if !UE_BUILD_SHIPPING

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
		TEXT("hint+"), TEXT("hint-"), TEXT("output"), TEXT("input"), TEXT("stealthkill"),
		TEXT("script"),
	};

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
				{ TEXT("name"), TEXT("classname"), TEXT("at"), TEXT("face"), TEXT("body"), TEXT("keys") }))
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
			? CheckFields(R, Object, Path, { TEXT("label"), TEXT("who"), TEXT("kind"), TEXT("match"),
				TEXT("regex"), TEXT("by"), TEXT("within") })
			: CheckFields(R, Object, Path, { TEXT("who"), TEXT("kind"), TEXT("match"), TEXT("regex"),
				TEXT("until"), TEXT("from"), TEXT("after"), TEXT("delay"), TEXT("at_most") });
		if (!bFields)
		{
			return false;
		}
		FString KindText;
		if (!ReadString(R, Object, TEXT("label"), Path, ENeed::Optional, Out.Label)
			|| !ReadString(R, Object, TEXT("who"), Path, ENeed::Optional, Out.Who)
			|| !ReadString(R, Object, TEXT("kind"), Path, ENeed::Required, KindText)
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
			EElysiumArenaProbe::PlayerGrappling,
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

	bool ReadProbe(FReader& R, const FJsonObject& Object, const FString& Path, double Duration,
		FElysiumArenaProbeSpec& Out)
	{
		Out = FElysiumArenaProbeSpec();
		if (!CheckFields(R, Object, Path, { TEXT("at"), TEXT("who"), TEXT("probe"), TEXT("equals"),
				TEXT("match"), TEXT("less"), TEXT("greater"), TEXT("condition"), TEXT("to") }))
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
		case EElysiumArenaProbe::Alive:
		case EElysiumArenaProbe::HasCondition:
		case EElysiumArenaProbe::OnGround:
		case EElysiumArenaProbe::PlayerCrouched:
		case EElysiumArenaProbe::PlayerGrappling:
			Answer = FElysiumArenaValue::EType::Bool;
			break;
		case EElysiumArenaProbe::Health:
		case EElysiumArenaProbe::DistanceTo:
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
		if (Out.Probe == EElysiumArenaProbe::DistanceTo)
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
		else if (FindValue(Object, TEXT("to")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("to")), TEXT("only `distance_to` takes `to`"));
		}
		return true;
	}

	bool ReadActionName(FReader& R, const FString& Text, const FString& Path, EElysiumArenaAction& Out)
	{
		const EElysiumArenaAction All[] =
		{
			EElysiumArenaAction::PlayerTeleport, EElysiumArenaAction::PlayerWalk, EElysiumArenaAction::Fire,
			EElysiumArenaAction::Console, EElysiumArenaAction::Spawn, EElysiumArenaAction::Kill,
			EElysiumArenaAction::PlayerCrouch, EElysiumArenaAction::LightPin, EElysiumArenaAction::DialogChoose,
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
				TEXT("on"), TEXT("value"), TEXT("index"), TEXT("end") }))
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
		// `value` may be a JSON null (the release), so its presence is read off the object itself.
		const bool bHasValue = Object.Values.Contains(TEXT("value"));
		if (Out.Do != EElysiumArenaAction::PlayerCrouch && FindValue(Object, TEXT("on")) != nullptr)
		{
			return R.Fail(Field(Path, TEXT("on")), TEXT("only `player_crouch` takes `on`"));
		}
		if (Out.Do != EElysiumArenaAction::LightPin && bHasValue)
		{
			return R.Fail(Field(Path, TEXT("value")), TEXT("only `light_pin` takes `value`"));
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
				|| !ReadString(R, Object, TEXT("input"), Path, ENeed::Required, Out.Input))
			{
				return false;
			}
			const TSharedPtr<FJsonValue>* Param = FindValue(Object, TEXT("param"));
			return Param == nullptr || ReadValue(R, *Param, Field(Path, TEXT("param")), Out.Param);
		}
		case EElysiumArenaAction::Kill:
			return ReadString(R, Object, TEXT("target"), Path, ENeed::Required, Out.Target);
		case EElysiumArenaAction::Console:
			return ReadString(R, Object, TEXT("command"), Path, ENeed::Required, Out.Command);
		case EElysiumArenaAction::Spawn:
		{
			if (RowKind == ERowKind::MapCast)
			{
				return R.Fail(Field(Path, TEXT("do")), TEXT("a map host spawns nothing; stage it in the arena"));
			}
			TSharedPtr<FJsonObject> Row;
			if (!ReadObject(R, Object, TEXT("row"), Path, Row))
			{
				return false;
			}
			if (!Row.IsValid())
			{
				return R.Fail(Field(Path, TEXT("row")), TEXT("required: the row to spawn"));
			}
			return ReadRow(R, *Row, Field(Path, TEXT("row")), ERowKind::ArenaRow, Out.Row);
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

	bool ReadRecord(FReader& R, const FJsonObject& Root, FElysiumArenaScenario& Out)
	{
		if (!CheckFields(R, Root, FString(), { TEXT("name"), TEXT("about"), TEXT("stage"), TEXT("seed"),
				TEXT("duration"), TEXT("known_red"), TEXT("expect_fail"), TEXT("shares_map"), TEXT("player"),
				TEXT("cast"), TEXT("rows"), TEXT("from_map"), TEXT("script"), TEXT("expect"), TEXT("never"),
				TEXT("probes"), TEXT("notes") }))
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
			if (!Action.After.IsEmpty() && !Out.Expect.ContainsByPredicate([&Action](const FElysiumArenaMatch& M)
				{ return M.Label.Equals(Action.After, ESearchCase::CaseSensitive); }))
			{
				return R.Fail(Field(Path, TEXT("after")), FString::Printf(
					TEXT("'%s' labels no expectation"), *Action.After));
			}
		}

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
	case EElysiumArenaProbe::PlayerWeapon:    return TEXT("player_weapon");
	case EElysiumArenaProbe::PlayerCrouched:  return TEXT("player_crouched");
	case EElysiumArenaProbe::PlayerGrappling: return TEXT("player_grappling");
	default:                                  return TEXT("?");
	}
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
	return FString::Printf(TEXT("%s %s \"%s\"%s"), Match.Who.IsEmpty() ? TEXT("*") : *Match.Who,
		*Match.Kind.ToString(), *Match.Match, Match.bRegex ? TEXT(" (regex)") : TEXT(""));
}

} // namespace ElysiumArenaScenario

#endif // !UE_BUILD_SHIPPING
