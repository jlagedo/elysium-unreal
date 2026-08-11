#include "ElysiumExpr.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumStub.h"
#include "Scripting/ElysiumScriptNatives.h"

#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumExpr, Log, All);

// The whole machine — lexer, parser, AST, and tree-walking interpreter — lives here. It is a
// self-contained subset interpreter (no UObject, no reflection, no C++ exceptions): errors ride a
// flag on the FEnv and collapse the result to Void (error-to-false). Grammar + binding surface are
// documented in ElysiumExpr.h and python_bridge.md.
namespace
{
	using namespace ElysiumExpr;

	// --- Tokens --------------------------------------------------------------------------------
	enum class ETok : uint8 { End, Int, Float, Str, Name, Punct };

	struct FToken
	{
		ETok Type = ETok::End;
		FString Text;        // identifier text, operator/delimiter text, or raw string value
		int32 IntVal = 0;
		double FloatVal = 0.0;
	};

	// Lexer: source text -> token stream. On a bad character it records an error and stops; the
	// parser then sees End and reports the parse as failed.
	class FLexer
	{
	public:
		FLexer(const FString& InSrc, FString& InError, bool& InErr)
			: Src(InSrc), Chars(*InSrc), Len(InSrc.Len()), Error(InError), bErr(InErr) {}

		void Tokenize(TArray<FToken>& Out)
		{
			while (!bErr)
			{
				SkipSpace();
				if (Pos >= Len)
				{
					Out.Add(FToken{ ETok::End });
					return;
				}
				const TCHAR C = Chars[Pos];
				if (FChar::IsDigit(C) || (C == TEXT('.') && Pos + 1 < Len && FChar::IsDigit(Chars[Pos + 1])))
				{
					Out.Add(LexNumber());
				}
				else if (C == TEXT('\'') || C == TEXT('"'))
				{
					Out.Add(LexString(C));
				}
				else if (FChar::IsAlpha(C) || C == TEXT('_'))
				{
					Out.Add(LexName());
				}
				else
				{
					Out.Add(LexPunct());
				}
			}
		}

	private:
		void SkipSpace() { while (Pos < Len && FChar::IsWhitespace(Chars[Pos])) { ++Pos; } }

		void Fail(const FString& Msg) { if (!bErr) { bErr = true; Error = Msg; } }

		FToken LexNumber()
		{
			const int32 Start = Pos;
			bool bFloat = false;
			// Hex integer.
			if (Chars[Pos] == TEXT('0') && Pos + 1 < Len && (Chars[Pos + 1] == TEXT('x') || Chars[Pos + 1] == TEXT('X')))
			{
				Pos += 2;
				const int32 HexStart = Pos;
				while (Pos < Len && FChar::IsHexDigit(Chars[Pos])) { ++Pos; }
				if (Pos == HexStart) { Fail(TEXT("malformed hex literal")); return FToken{ ETok::End }; }
				FToken T{ ETok::Int };
				T.IntVal = (int32)FCString::Strtoi64(*Src.Mid(HexStart, Pos - HexStart), nullptr, 16);
				return T;
			}
			while (Pos < Len && FChar::IsDigit(Chars[Pos])) { ++Pos; }
			if (Pos < Len && Chars[Pos] == TEXT('.'))
			{
				bFloat = true;
				++Pos;
				while (Pos < Len && FChar::IsDigit(Chars[Pos])) { ++Pos; }
			}
			if (Pos < Len && (Chars[Pos] == TEXT('e') || Chars[Pos] == TEXT('E')))
			{
				bFloat = true;
				++Pos;
				if (Pos < Len && (Chars[Pos] == TEXT('+') || Chars[Pos] == TEXT('-'))) { ++Pos; }
				while (Pos < Len && FChar::IsDigit(Chars[Pos])) { ++Pos; }
			}
			const FString Num = Src.Mid(Start, Pos - Start);
			if (bFloat)
			{
				FToken T{ ETok::Float };
				T.FloatVal = FCString::Atod(*Num);
				return T;
			}
			FToken T{ ETok::Int };
			T.IntVal = FCString::Atoi(*Num);
			return T;
		}

		FToken LexString(TCHAR Quote)
		{
			++Pos;   // opening quote
			FString Value;
			while (Pos < Len && Chars[Pos] != Quote)
			{
				TCHAR C = Chars[Pos];
				if (C == TEXT('\\') && Pos + 1 < Len)
				{
					const TCHAR N = Chars[Pos + 1];
					switch (N)
					{
					case TEXT('n'): Value.AppendChar(TEXT('\n')); break;
					case TEXT('t'): Value.AppendChar(TEXT('\t')); break;
					case TEXT('r'): Value.AppendChar(TEXT('\r')); break;
					case TEXT('\\'): Value.AppendChar(TEXT('\\')); break;
					case TEXT('\''): Value.AppendChar(TEXT('\'')); break;
					case TEXT('"'): Value.AppendChar(TEXT('"')); break;
					default: Value.AppendChar(N); break;   // unknown escape: keep the char (lenient)
					}
					Pos += 2;
					continue;
				}
				Value.AppendChar(C);
				++Pos;
			}
			if (Pos >= Len) { Fail(TEXT("unterminated string")); return FToken{ ETok::End }; }
			++Pos;   // closing quote
			FToken T{ ETok::Str };
			T.Text = MoveTemp(Value);
			return T;
		}

		FToken LexName()
		{
			const int32 Start = Pos;
			while (Pos < Len && (FChar::IsAlnum(Chars[Pos]) || Chars[Pos] == TEXT('_'))) { ++Pos; }
			FToken T{ ETok::Name };
			T.Text = Src.Mid(Start, Pos - Start);
			return T;
		}

		FToken LexPunct()
		{
			// Two-char operators first, then single-char.
			static const TCHAR* Two[] = { TEXT("=="), TEXT("!="), TEXT("<="), TEXT(">="),
				TEXT("<<"), TEXT(">>"), TEXT("**") };
			if (Pos + 1 < Len)
			{
				const FString Pair = Src.Mid(Pos, 2);
				for (const TCHAR* Op : Two)
				{
					if (Pair == Op) { Pos += 2; FToken T{ ETok::Punct }; T.Text = Pair; return T; }
				}
			}
			const TCHAR C = Chars[Pos];
			static const FString Singles = TEXT("()[].,+-*/%|&^~<>=");
			int32 FoundAt = INDEX_NONE;
			if (Singles.FindChar(C, FoundAt))
			{
				++Pos;
				FToken T{ ETok::Punct };
				T.Text = FString::Chr(C);
				return T;
			}
			Fail(FString::Printf(TEXT("unexpected character '%c'"), C));
			return FToken{ ETok::End };
		}

		const FString& Src;
		const TCHAR* Chars;
		int32 Len;
		int32 Pos = 0;
		FString& Error;
		bool& bErr;
	};

	// --- AST -----------------------------------------------------------------------------------
	enum class ENode : uint8
	{
		IntLit, FloatLit, StrLit, NoneLit,
		Name,        // Str = identifier
		Attr,        // Kids[0] = object, Str = attribute name
		Call,        // Kids[0] = callee, Kids[1..] = args
		Unary,       // Op = "-" / "+" / "~", Kids[0]
		Binary,      // Op = arithmetic/bitwise, Kids[0], Kids[1]
		BoolOp,      // Op = "and" / "or", Kids[0], Kids[1] (short-circuit)
		Not,         // Kids[0]
		Compare,     // Kids[0] = first, Kids[1..] = operands, CmpOps[i] relates Kids[i]..Kids[i+1]
		Assign,      // Kids[0] = target (Name/Attr), Kids[1] = value
	};

	struct FNode
	{
		ENode Type;
		int32 IntVal = 0;
		double FloatVal = 0.0;
		FString Str;                            // literal string / name / attribute name
		FString Op;                             // operator token
		TArray<TUniquePtr<FNode>> Kids;
		TArray<FString> CmpOps;                 // Compare only

		explicit FNode(ENode InType) : Type(InType) {}
	};

	using FNodePtr = TUniquePtr<FNode>;

	FNodePtr MakeNode(ENode T) { return MakeUnique<FNode>(T); }

	// Recursive-descent parser: token stream -> AST. Python operator precedence (low -> high):
	// or / and / not / comparison / | / ^ / & / << >> / + - / * / % / unary / ** / trailer / atom.
	class FParser
	{
	public:
		FParser(const TArray<FToken>& InToks, FString& InError, bool& InErr)
			: Toks(InToks), Error(InError), bErr(InErr) {}

		// A statement: assignment (target '=' value) or a bare expression.
		FNodePtr ParseStatement()
		{
			FNodePtr Expr = ParseExpr();
			if (bErr) { return nullptr; }
			if (IsPunct(TEXT("=")))   // lone '=' (==, <=, >= are already distinct tokens)
			{
				if (Expr->Type != ENode::Name && Expr->Type != ENode::Attr)
				{
					Fail(TEXT("cannot assign to this expression"));
					return nullptr;
				}
				Advance();
				FNodePtr Value = ParseExpr();
				if (bErr) { return nullptr; }
				FNodePtr Node = MakeNode(ENode::Assign);
				Node->Kids.Add(MoveTemp(Expr));
				Node->Kids.Add(MoveTemp(Value));
				return Node;
			}
			return Expr;
		}

		bool AtEnd() const { return Peek().Type == ETok::End; }
		bool ExpectEndOrSemicolon()
		{
			if (AtEnd()) { return true; }
			if (IsPunct(TEXT(";"))) { return true; }
			Fail(TEXT("unexpected trailing tokens"));
			return false;
		}
		bool ConsumeSemicolon()
		{
			if (IsPunct(TEXT(";"))) { Advance(); return true; }
			return false;
		}

	private:
		FNodePtr ParseExpr() { return ParseOr(); }

		FNodePtr ParseOr()
		{
			FNodePtr L = ParseAnd();
			while (!bErr && IsName(TEXT("or")))
			{
				Advance();
				FNodePtr R = ParseAnd();
				FNodePtr N = MakeNode(ENode::BoolOp);
				N->Op = TEXT("or");
				N->Kids.Add(MoveTemp(L));
				N->Kids.Add(MoveTemp(R));
				L = MoveTemp(N);
			}
			return L;
		}

		FNodePtr ParseAnd()
		{
			FNodePtr L = ParseNot();
			while (!bErr && IsName(TEXT("and")))
			{
				Advance();
				FNodePtr R = ParseNot();
				FNodePtr N = MakeNode(ENode::BoolOp);
				N->Op = TEXT("and");
				N->Kids.Add(MoveTemp(L));
				N->Kids.Add(MoveTemp(R));
				L = MoveTemp(N);
			}
			return L;
		}

		FNodePtr ParseNot()
		{
			if (IsName(TEXT("not")))
			{
				Advance();
				FNodePtr N = MakeNode(ENode::Not);
				N->Kids.Add(ParseNot());
				return N;
			}
			return ParseComparison();
		}

		bool IsRelop() const
		{
			const FToken& T = Peek();
			return T.Type == ETok::Punct
				&& (T.Text == TEXT("==") || T.Text == TEXT("!=") || T.Text == TEXT("<")
					|| T.Text == TEXT("<=") || T.Text == TEXT(">") || T.Text == TEXT(">="));
		}

		FNodePtr ParseComparison()
		{
			FNodePtr First = ParseBitOr();
			if (!IsRelop()) { return First; }
			FNodePtr N = MakeNode(ENode::Compare);
			N->Kids.Add(MoveTemp(First));
			while (!bErr && IsRelop())
			{
				N->CmpOps.Add(Peek().Text);
				Advance();
				N->Kids.Add(ParseBitOr());
			}
			return N;
		}

		// Left-associative binary level helper.
		FNodePtr ParseBinLevel(TFunctionRef<FNodePtr()> Next, std::initializer_list<const TCHAR*> Ops)
		{
			FNodePtr L = Next();
			while (!bErr)
			{
				const FToken& T = Peek();
				bool bMatch = false;
				for (const TCHAR* Op : Ops) { if (T.Type == ETok::Punct && T.Text == Op) { bMatch = true; break; } }
				if (!bMatch) { break; }
				const FString Op = T.Text;
				Advance();
				FNodePtr R = Next();
				FNodePtr N = MakeNode(ENode::Binary);
				N->Op = Op;
				N->Kids.Add(MoveTemp(L));
				N->Kids.Add(MoveTemp(R));
				L = MoveTemp(N);
			}
			return L;
		}

		FNodePtr ParseBitOr()  { return ParseBinLevel([this]{ return ParseBitXor(); }, { TEXT("|") }); }
		FNodePtr ParseBitXor() { return ParseBinLevel([this]{ return ParseBitAnd(); }, { TEXT("^") }); }
		FNodePtr ParseBitAnd() { return ParseBinLevel([this]{ return ParseShift(); },  { TEXT("&") }); }
		FNodePtr ParseShift()  { return ParseBinLevel([this]{ return ParseArith(); },  { TEXT("<<"), TEXT(">>") }); }
		FNodePtr ParseArith()  { return ParseBinLevel([this]{ return ParseTerm(); },   { TEXT("+"), TEXT("-") }); }
		FNodePtr ParseTerm()   { return ParseBinLevel([this]{ return ParseFactor(); }, { TEXT("*"), TEXT("/"), TEXT("%") }); }

		FNodePtr ParseFactor()
		{
			const FToken& T = Peek();
			if (T.Type == ETok::Punct && (T.Text == TEXT("-") || T.Text == TEXT("+") || T.Text == TEXT("~")))
			{
				const FString Op = T.Text;
				Advance();
				FNodePtr N = MakeNode(ENode::Unary);
				N->Op = Op;
				N->Kids.Add(ParseFactor());
				return N;
			}
			return ParsePower();
		}

		FNodePtr ParsePower()
		{
			FNodePtr Base = ParseTrailer();
			if (IsPunct(TEXT("**")))
			{
				Advance();
				FNodePtr N = MakeNode(ENode::Binary);
				N->Op = TEXT("**");
				N->Kids.Add(MoveTemp(Base));
				N->Kids.Add(ParseFactor());   // right-associative
				return N;
			}
			return Base;
		}

		FNodePtr ParseTrailer()
		{
			FNodePtr Node = ParseAtom();
			while (!bErr)
			{
				if (IsPunct(TEXT(".")))
				{
					Advance();
					const FToken& NameTok = Peek();
					if (NameTok.Type != ETok::Name) { Fail(TEXT("expected attribute name after '.'")); return nullptr; }
					FNodePtr A = MakeNode(ENode::Attr);
					A->Str = NameTok.Text;
					A->Kids.Add(MoveTemp(Node));
					Advance();
					Node = MoveTemp(A);
				}
				else if (IsPunct(TEXT("(")))
				{
					Advance();
					FNodePtr Call = MakeNode(ENode::Call);
					Call->Kids.Add(MoveTemp(Node));
					if (!IsPunct(TEXT(")")))
					{
						for (;;)
						{
							Call->Kids.Add(ParseExpr());
							if (bErr) { return nullptr; }
							if (IsPunct(TEXT(","))) { Advance(); continue; }
							break;
						}
					}
					if (!IsPunct(TEXT(")"))) { Fail(TEXT("expected ')'")); return nullptr; }
					Advance();
					Node = MoveTemp(Call);
				}
				else
				{
					break;
				}
			}
			return Node;
		}

		FNodePtr ParseAtom()
		{
			const FToken& T = Peek();
			switch (T.Type)
			{
			case ETok::Int:   { Advance(); FNodePtr N = MakeNode(ENode::IntLit);   N->IntVal = T.IntVal;     return N; }
			case ETok::Float: { Advance(); FNodePtr N = MakeNode(ENode::FloatLit); N->FloatVal = T.FloatVal; return N; }
			case ETok::Str:   { Advance(); FNodePtr N = MakeNode(ENode::StrLit);   N->Str = T.Text;          return N; }
			case ETok::Name:
				if (T.Text == TEXT("None")) { Advance(); return MakeNode(ENode::NoneLit); }
				if (T.Text == TEXT("and") || T.Text == TEXT("or") || T.Text == TEXT("not"))
				{
					Fail(FString::Printf(TEXT("unexpected keyword '%s'"), *T.Text));
					return nullptr;
				}
				{ Advance(); FNodePtr N = MakeNode(ENode::Name); N->Str = T.Text; return N; }
			case ETok::Punct:
				if (T.Text == TEXT("("))
				{
					Advance();
					FNodePtr Inner = ParseExpr();
					if (bErr) { return nullptr; }
					if (!IsPunct(TEXT(")"))) { Fail(TEXT("expected ')'")); return nullptr; }
					Advance();
					return Inner;
				}
				Fail(FString::Printf(TEXT("unexpected token '%s'"), *T.Text));
				return nullptr;
			default:
				Fail(TEXT("unexpected end of expression"));
				return nullptr;
			}
		}

		// --- token cursor ---
		const FToken& Peek() const { return Toks[FMath::Min(Cursor, Toks.Num() - 1)]; }
		void Advance() { if (Cursor < Toks.Num() - 1) { ++Cursor; } }
		bool IsPunct(const TCHAR* Text) const { const FToken& T = Peek(); return T.Type == ETok::Punct && T.Text == Text; }
		bool IsName(const TCHAR* Text) const { const FToken& T = Peek(); return T.Type == ETok::Name && T.Text == Text; }
		void Fail(const FString& Msg) { if (!bErr) { bErr = true; Error = Msg; } }

		const TArray<FToken>& Toks;
		int32 Cursor = 0;
		FString& Error;
		bool& bErr;
	};

	// --- Native binding surface (5.3) ----------------------------------------------------------
	// The engine `vampire` module — the table, the stub defaults, and the call log — lives in
	// ElysiumScriptNatives, shared with the CPython host so a name cannot stub differently
	// depending on which host is installed. The evaluator only adapts its own object model to it.
	using ElysiumScriptNatives::IsCharacterMethod;
	using ElysiumScriptNatives::IsNativeGlobal;

	// --- Runtime values ------------------------------------------------------------------------
	// The evaluator carries values richer than FElysiumVariant during a walk: besides a plain
	// variant it can hold the `G` bag, a bound entity input (a callable that fires the input), one of
	// G's own methods, a `vampire`-module global (5.3), a character object (the PC or an NPC handle),
	// or a bound Character method. Non-variant kinds are Python "objects" — truthy, and an error to
	// coerce into arithmetic.
	struct FVal
	{
		enum class EKind : uint8 { Var, GBag, BoundInput, GMethod, NativeGlobal, Character, CharMethod } Kind = EKind::Var;
		FElysiumVariant Var;
		FElysiumEntityHandle Self;   // BoundInput/CharMethod: the target entity (Character: NPC, or Invalid = the PC)
		FName Method;                // BoundInput input / GMethod / NativeGlobal / CharMethod name

		static FVal FromVar(const FElysiumVariant& V) { FVal R; R.Kind = EKind::Var; R.Var = V; return R; }
		static FVal MakeGBag() { FVal R; R.Kind = EKind::GBag; return R; }
		static FVal MakeNativeGlobal(FName N) { FVal R; R.Kind = EKind::NativeGlobal; R.Method = N; return R; }
		static FVal MakeCharacter(const FElysiumEntityHandle& H) { FVal R; R.Kind = EKind::Character; R.Self = H; return R; }
		static FVal MakeCharMethod(const FElysiumEntityHandle& H, FName N) { FVal R; R.Kind = EKind::CharMethod; R.Self = H; R.Method = N; return R; }
		bool IsVar() const { return Kind == EKind::Var; }
	};

	// Interpreter over the AST. All entry points are total; any failure sets Env.bError + Error and
	// returns a Void FVal, which the callers propagate as error-to-false.
	class FInterp
	{
	public:
		explicit FInterp(FEnv& InEnv) : Env(InEnv) {}

		FElysiumVariant EvalNodeToVariant(const FNode& N)
		{
			const FVal V = EvalNode(N);
			return ToVariant(V);
		}

	private:
		void Fail(const FString& Msg) { if (!Env.bError) { Env.bError = true; Env.Error = Msg; } }
		FVal Void() { return FVal::FromVar(FElysiumVariant::Void()); }

		FElysiumEntityWorld* World() const { return Env.Ctx.World; }

		// Collapse a runtime value to a storable/marshalled variant. The bag and callables have no
		// value form — using one where a value is needed is a type error.
		FElysiumVariant ToVariant(const FVal& V)
		{
			if (Env.bError) { return FElysiumVariant::Void(); }
			switch (V.Kind)
			{
			case FVal::EKind::Var: return V.Var;
			case FVal::EKind::GBag: Fail(TEXT("cannot use 'G' as a value")); return FElysiumVariant::Void();
			case FVal::EKind::Character: Fail(TEXT("cannot use a Character object as a value")); return FElysiumVariant::Void();
			default: Fail(TEXT("cannot use a function as a value")); return FElysiumVariant::Void();
			}
		}

		bool IsTruthy(const FVal& V)
		{
			if (V.Kind != FVal::EKind::Var) { return true; }   // objects are truthy
			return V.Var.ToBool();
		}

		static bool IsNumeric(const FElysiumVariant& V)
		{
			return V.IsBool() || V.IsInt() || V.IsFloat();
		}
		static bool IsIntegral(const FElysiumVariant& V)
		{
			return V.IsBool() || V.IsInt();
		}

		FVal EvalNode(const FNode& N)
		{
			if (Env.bError) { return Void(); }
			switch (N.Type)
			{
			case ENode::IntLit:   return FVal::FromVar(FElysiumVariant::Int(N.IntVal));
			case ENode::FloatLit: return FVal::FromVar(FElysiumVariant::Float((float)N.FloatVal));
			case ENode::StrLit:   return FVal::FromVar(FElysiumVariant::String(N.Str));
			case ENode::NoneLit:  return Void();   // None -> Void (falsy; deletes a G key on assign)
			case ENode::Name:     return EvalName(N.Str);
			case ENode::Attr:     return EvalAttr(N);
			case ENode::Call:     return EvalCall(N);
			case ENode::Unary:    return EvalUnary(N);
			case ENode::Binary:   return EvalBinary(N);
			case ENode::BoolOp:   return EvalBoolOp(N);
			case ENode::Not:      return FVal::FromVar(FElysiumVariant::Bool(!IsTruthy(EvalNode(*N.Kids[0]))));
			case ENode::Compare:  return EvalCompare(N);
			case ENode::Assign:   return EvalAssign(N);
			default:              Fail(TEXT("internal: bad node")); return Void();
			}
		}

		FVal EvalName(const FString& Name)
		{
			if (Name == TEXT("G")) { return FVal::MakeGBag(); }

			// The `vampire`-module globals (FindPlayer, FindEntityByName, ...) resolve as bare names,
			// ahead of targetnames — no entity would carry such a name (5.3).
			if (IsNativeGlobal(Name)) { return FVal::MakeNativeGlobal(FName(*Name)); }

			// `self`/`activator` — the evaluation's I/O provenance, when bound (a live field-6 delivery).
			// They fall below the module globals so nothing real is shadowed; Invalid context reads
			// through to a targetname / NameError, exactly as a hand-run probe eval has no self.
			if (Name == TEXT("self") && Env.Ctx.Self.IsSet()) { return FVal::FromVar(FElysiumVariant::Handle(Env.Ctx.Self)); }
			if (Name == TEXT("activator") && Env.Ctx.Activator.IsSet()) { return FVal::FromVar(FElysiumVariant::Handle(Env.Ctx.Activator)); }

			// `pc` = the player entity (11.4 — an ordinary handle, like every other entity);
			// `npc` = the firing entity (Self) in a dialogue/entity context. The two names the dialogue
			// gates and level scripts actually read, mirroring the CPython host. With no player (a
			// menu backdrop, a bare probe world) `pc` falls through to a Character receiver on the
			// Invalid handle, which is the sheet-less stub surface — error-to-false's spirit.
			if (Name == TEXT("pc"))
			{
				FElysiumEntityWorld* W = World();
				const FElysiumPlayer* P = W ? W->FindPlayer() : nullptr;
				return P ? FVal::FromVar(FElysiumVariant::Handle(P->Handle))
				         : FVal::MakeCharacter(FElysiumEntityHandle::Invalid());
			}
			if (Name == TEXT("npc") && Env.Ctx.Self.IsSet()) { return FVal::FromVar(FElysiumVariant::Handle(Env.Ctx.Self)); }

			// Otherwise a bare name resolves to an entity by targetname (the one namespace, 5.2). Level-
			// script functions/constants (cCelerity, the level's On* callbacks) are 5.5 — NameError here.
			if (FElysiumEntityWorld* W = World())
			{
				if (FElysiumEntity* E = W->FindByName(Name))
				{
					return FVal::FromVar(FElysiumVariant::Handle(E->Handle));
				}
			}
			Fail(FString::Printf(TEXT("name '%s' is not defined"), *Name));
			return Void();
		}

		FVal EvalAttr(const FNode& N)
		{
			const FVal Obj = EvalNode(*N.Kids[0]);
			if (Env.bError) { return Void(); }
			const FString& Attr = N.Str;

			if (Obj.Kind == FVal::EKind::GBag)
			{
				// G's own methods, else a flag read (default-on-miss integer 0).
				if (Attr == TEXT("has_key") || Attr == TEXT("keys") || Attr == TEXT("ClearAll"))
				{
					FVal M; M.Kind = FVal::EKind::GMethod; M.Method = FName(*Attr); return M;
				}
				const FElysiumVariant V = Env.State ? Env.State->GetGlobal(Attr) : FElysiumVariant::Int(0);
				return FVal::FromVar(V);
			}

			// A character object (FindPlayer() -> the PC): any attribute manufactures a bound Character
			// method — a name outside the known 24 still binds so an unlisted call (ClearActiveDisciplines)
			// dispatches to the generic stub rather than raising, matching retail's forgiving surface.
			if (Obj.Kind == FVal::EKind::Character)
			{
				return FVal::MakeCharMethod(Obj.Self, FName(*Attr));
			}

			if (Obj.IsVar() && Obj.Var.IsHandle())
			{
				FElysiumEntityWorld* W = World();
				FElysiumEntity* E = W ? W->Resolve(Obj.Var.AsHandle) : nullptr;
				if (!E) { Fail(TEXT("game entity has been deleted")); return Void(); }
				return EvalEntityAttr(*E, Attr);
			}

			Fail(FString::Printf(TEXT("value has no attribute '%s'"), *Attr));
			return Void();
		}

		// One namespace, no new dispatch (python_bridge.md): an input name -> a bound callable that
		// fires the input; a field name -> its marshalled value; miss -> AttributeError.
		FVal EvalEntityAttr(FElysiumEntity& E, const FString& Attr)
		{
			const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
			const FName AttrName(*Attr);
			if (E.Class)
			{
				if (Reg.FindInput(*E.Class, AttrName) != nullptr)
				{
					FVal M; M.Kind = FVal::EKind::BoundInput; M.Self = E.Handle; M.Method = AttrName; return M;
				}
				if (const FElysiumFieldAccessor* F = Reg.FindField(*E.Class, AttrName))
				{
					return FVal::FromVar(F->Get(E));
				}
			}
			// The vdata-driven half of the character sheet (11.4): `pc.base_Celerity` reads a number
			// rather than binding as a method. Same position in the order as the CPython host's.
			{
				FElysiumVariant Dynamic;
				if (E.GetDynamicField(AttrName, Dynamic))
				{
					return FVal::FromVar(Dynamic);
				}
			}
			// A Character method invoked on an NPC entity handle (FindEntityByName("bob").SetExpression(...)):
			// bind the method against that entity so it dispatches through the same stub as the PC (5.3).
			if (IsCharacterMethod(Attr))
			{
				return FVal::MakeCharMethod(E.Handle, AttrName);
			}
			// Same report as the CPython host's miss, so the work list does not depend on which
			// host is installed. The failure itself is unchanged (error-to-false).
			ElysiumStub::Fired(TEXT("attr"),
				FString::Printf(TEXT("%s.%s"), E.Def ? *E.Def->Classname : TEXT("?"), *Attr),
				E.DebugString(), FString(),
				TEXT("no field, input or Character method of this name — evaluates to false"));
			Fail(FString::Printf(TEXT("entity has no attribute '%s'"), *Attr));
			return Void();
		}

		FVal EvalCall(const FNode& N)
		{
			const FVal Callee = EvalNode(*N.Kids[0]);
			if (Env.bError) { return Void(); }

			TArray<FElysiumVariant> Args;
			for (int32 i = 1; i < N.Kids.Num(); ++i)
			{
				Args.Add(ToVariant(EvalNode(*N.Kids[i])));
				if (Env.bError) { return Void(); }
			}

			switch (Callee.Kind)
			{
			case FVal::EKind::BoundInput:
			{
				// The reflected input call is SYNCHRONOUS (`docs/vtmb/python_bridge.md` → "Synchronous
				// calls versus queued Python"): the argument is marshalled and the entity's AcceptInput
				// runs to completion before the script resumes, with null activator and null caller, so
				// the script observes its own mutation mid-handler. Outputs the body fires still enter
				// the ordinary queue, behind the pending equal-time cohort. Same by-handle chokepoint
				// as the CPython host, so the two hosts cannot drift. A Python call returns None.
				if (FElysiumEntityWorld* W = World())
				{
					const FElysiumVariant Param = Args.Num() > 0 ? Args[0] : FElysiumVariant::Void();
					W->AcceptInput(Callee.Self, Callee.Method, Param,
						FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
				}
				return Void();
			}
			case FVal::EKind::GMethod:
				return EvalGMethod(Callee.Method, Args);
			case FVal::EKind::NativeGlobal:
				return EvalNativeGlobal(Callee.Method, Args);
			case FVal::EKind::CharMethod:
				return EvalCharMethod(Callee.Self, Callee.Method, Args);
			default:
				Fail(TEXT("object is not callable"));
				return Void();
			}
		}

		FVal EvalGMethod(FName Method, const TArray<FElysiumVariant>& Args)
		{
			UElysiumGameStateSubsystem* S = Env.State;
			if (Method == FName(TEXT("has_key")))
			{
				const bool bHas = S && Args.Num() > 0 && S->HasGlobal(Args[0].ToString());
				return FVal::FromVar(FElysiumVariant::Bool(bHas));
			}
			if (Method == FName(TEXT("ClearAll")))
			{
				if (S) { S->ClearAllGlobals(); }
				return Void();
			}
			if (Method == FName(TEXT("keys")))
			{
				// No list type in the variant model yet; report the count so the call is observable.
				const int32 Count = S ? S->GlobalKeys().Num() : 0;
				return FVal::FromVar(FElysiumVariant::Int(Count));
			}
			Fail(TEXT("unknown G method"));
			return Void();
		}

		// --- Native binding dispatch (5.3) -----------------------------------------------------
		// Only the two globals whose result is an evaluator-side object (the PC object and an entity
		// handle) are resolved here; everything else — ScheduleTask, ChangeMap, the unbacked stubs,
		// and the whole Character surface — goes through the shared ElysiumScriptNatives module, so
		// the CPython host and this one cannot drift.
		FVal EvalNativeGlobal(FName Name, const TArray<FElysiumVariant>& Args)
		{
			if (Name == FName(TEXT("FindPlayer")))
			{
				const FString Display = FString::Printf(TEXT("FindPlayer(%s)"), *ElysiumScriptNatives::DescribeArgs(Args));
				ElysiumScriptNatives::Record(Env.State, Name, Display,
					FElysiumVariant::String(TEXT("<player>")), /*bStub*/ false);
				return FVal::MakeCharacter(FElysiumEntityHandle::Invalid());   // the PC (no player entity yet)
			}
			if (Name == FName(TEXT("FindEntityByName")))
			{
				const FString Display = FString::Printf(TEXT("FindEntityByName(%s)"), *ElysiumScriptNatives::DescribeArgs(Args));
				FElysiumEntity* E = (Args.Num() > 0 && World()) ? World()->FindByName(Args[0].ToString()) : nullptr;
				const FElysiumVariant R = E ? FElysiumVariant::Handle(E->Handle) : FElysiumVariant::Void();
				ElysiumScriptNatives::Record(Env.State, Name, Display, R, /*bStub*/ false);
				return FVal::FromVar(R);
			}
			return FVal::FromVar(ElysiumScriptNatives::CallSimpleGlobal(Env.State, World(), Env.Ctx, Name, Args));
		}

		FVal EvalCharMethod(const FElysiumEntityHandle& Self, FName Method, const TArray<FElysiumVariant>& Args)
		{
			return FVal::FromVar(ElysiumScriptNatives::CallCharacterMethod(Env.State, World(), Self, Method, Args));
		}

		FVal EvalUnary(const FNode& N)
		{
			const FElysiumVariant V = ToVariant(EvalNode(*N.Kids[0]));
			if (Env.bError) { return Void(); }
			if (N.Op == TEXT("+")) { return FVal::FromVar(V); }
			if (N.Op == TEXT("-"))
			{
				if (V.IsFloat()) { return FVal::FromVar(FElysiumVariant::Float(-V.AsFloat)); }
				if (IsIntegral(V)) { return FVal::FromVar(FElysiumVariant::Int(-V.ToInt())); }
				Fail(TEXT("bad operand for unary '-'")); return Void();
			}
			if (N.Op == TEXT("~"))
			{
				if (!IsIntegral(V)) { Fail(TEXT("bad operand for unary '~'")); return Void(); }
				return FVal::FromVar(FElysiumVariant::Int(~V.ToInt()));
			}
			Fail(TEXT("bad unary operator")); return Void();
		}

		FVal EvalBoolOp(const FNode& N)
		{
			// Python short-circuit: 'and'/'or' return one of the operands (not a coerced bool).
			const FVal L = EvalNode(*N.Kids[0]);
			if (Env.bError) { return Void(); }
			const bool bL = IsTruthy(L);
			if (N.Op == TEXT("or")) { return bL ? L : EvalNode(*N.Kids[1]); }
			return bL ? EvalNode(*N.Kids[1]) : L;   // and
		}

		FVal EvalCompare(const FNode& N)
		{
			// Chained: a <op0> b <op1> c  ==  (a<op0>b) and (b<op1>c). Any false short-circuits.
			FElysiumVariant Prev = ToVariant(EvalNode(*N.Kids[0]));
			if (Env.bError) { return Void(); }
			for (int32 i = 0; i < N.CmpOps.Num(); ++i)
			{
				const FElysiumVariant Cur = ToVariant(EvalNode(*N.Kids[i + 1]));
				if (Env.bError) { return Void(); }
				if (!CompareOne(Prev, N.CmpOps[i], Cur))
				{
					return FVal::FromVar(FElysiumVariant::Bool(false));
				}
				Prev = Cur;
			}
			return FVal::FromVar(FElysiumVariant::Bool(true));
		}

		bool CompareOne(const FElysiumVariant& A, const FString& Op, const FElysiumVariant& B)
		{
			const bool bEq = Op == TEXT("==");
			const bool bNe = Op == TEXT("!=");
			if (bEq || bNe)
			{
				bool bEqual;
				if (IsNumeric(A) && IsNumeric(B)) { bEqual = A.ToFloat() == B.ToFloat(); }
				else if (A.IsString() && B.IsString()) { bEqual = A.AsString.Equals(B.AsString, ESearchCase::CaseSensitive); }
				else { bEqual = (A == B); }   // cross-type / handle / void
				return bEq ? bEqual : !bEqual;
			}
			// Ordering: numeric or string only.
			double Rel = 0.0;
			if (IsNumeric(A) && IsNumeric(B)) { const double da = A.ToFloat(), db = B.ToFloat(); Rel = da < db ? -1 : (da > db ? 1 : 0); }
			else if (A.IsString() && B.IsString()) { Rel = (double)FCString::Strcmp(*A.AsString, *B.AsString); }
			else { Fail(TEXT("unorderable types in comparison")); return false; }
			if (Op == TEXT("<"))  { return Rel < 0; }
			if (Op == TEXT("<=")) { return Rel <= 0; }
			if (Op == TEXT(">"))  { return Rel > 0; }
			if (Op == TEXT(">=")) { return Rel >= 0; }
			Fail(TEXT("bad comparison operator"));
			return false;
		}

		FVal EvalBinary(const FNode& N)
		{
			const FElysiumVariant A = ToVariant(EvalNode(*N.Kids[0]));
			if (Env.bError) { return Void(); }
			const FElysiumVariant B = ToVariant(EvalNode(*N.Kids[1]));
			if (Env.bError) { return Void(); }
			const FString& Op = N.Op;

			// String '+' concatenation and string '%' formatting (Python str ops).
			if (Op == TEXT("+") && A.IsString() && B.IsString())
			{
				return FVal::FromVar(FElysiumVariant::String(A.AsString + B.AsString));
			}
			if (Op == TEXT("%") && A.IsString())
			{
				return FVal::FromVar(FElysiumVariant::String(FormatString(A.AsString, B)));
			}

			// Bitwise / shift require integers.
			if (Op == TEXT("|") || Op == TEXT("&") || Op == TEXT("^") || Op == TEXT("<<") || Op == TEXT(">>"))
			{
				if (!IsIntegral(A) || !IsIntegral(B)) { Fail(TEXT("bitwise operand must be integer")); return Void(); }
				const int32 a = A.ToInt(), b = B.ToInt();
				int32 R = 0;
				if (Op == TEXT("|")) { R = a | b; }
				else if (Op == TEXT("&")) { R = a & b; }
				else if (Op == TEXT("^")) { R = a ^ b; }
				else if (Op == TEXT("<<")) { R = a << b; }
				else { R = a >> b; }
				return FVal::FromVar(FElysiumVariant::Int(R));
			}

			if (!IsNumeric(A) || !IsNumeric(B)) { Fail(TEXT("unsupported operand type for arithmetic")); return Void(); }

			const bool bFloat = A.IsFloat() || B.IsFloat() || Op == TEXT("**");
			if (bFloat)
			{
				const double a = A.ToFloat(), b = B.ToFloat();
				double R = 0.0;
				if (Op == TEXT("+")) { R = a + b; }
				else if (Op == TEXT("-")) { R = a - b; }
				else if (Op == TEXT("*")) { R = a * b; }
				else if (Op == TEXT("/")) { if (b == 0.0) { Fail(TEXT("division by zero")); return Void(); } R = a / b; }
				else if (Op == TEXT("%")) { if (b == 0.0) { Fail(TEXT("modulo by zero")); return Void(); } R = FMath::Fmod(a, b); }
				else if (Op == TEXT("**")) { R = FMath::Pow(a, b); }
				else { Fail(TEXT("bad operator")); return Void(); }
				// ** on two ints with a non-negative exponent yields an int (Python 2 semantics).
				if (Op == TEXT("**") && IsIntegral(A) && IsIntegral(B) && B.ToInt() >= 0)
				{
					return FVal::FromVar(FElysiumVariant::Int((int32)FMath::RoundToDouble(R)));
				}
				return FVal::FromVar(FElysiumVariant::Float((float)R));
			}

			const int32 a = A.ToInt(), b = B.ToInt();
			int32 R = 0;
			if (Op == TEXT("+")) { R = a + b; }
			else if (Op == TEXT("-")) { R = a - b; }
			else if (Op == TEXT("*")) { R = a * b; }
			else if (Op == TEXT("/")) { if (b == 0) { Fail(TEXT("integer division by zero")); return Void(); } R = PyFloorDiv(a, b); }
			else if (Op == TEXT("%")) { if (b == 0) { Fail(TEXT("integer modulo by zero")); return Void(); } R = PyMod(a, b); }
			else { Fail(TEXT("bad operator")); return Void(); }
			return FVal::FromVar(FElysiumVariant::Int(R));
		}

		// Python 2 integer division/modulo floor toward negative infinity (C++ truncates toward zero).
		static int32 PyFloorDiv(int32 a, int32 b)
		{
			int32 q = a / b;
			if ((a % b != 0) && ((a < 0) != (b < 0))) { --q; }
			return q;
		}
		static int32 PyMod(int32 a, int32 b)
		{
			int32 r = a % b;
			if (r != 0 && ((r < 0) != (b < 0))) { r += b; }
			return r;
		}

		// Minimal single-argument printf-style '%' formatting (level scripts use `"name_%i" % i`).
		// Substitutes the first conversion spec; unknown specs pass through.
		FString FormatString(const FString& Fmt, const FElysiumVariant& Arg)
		{
			FString Out;
			const TCHAR* P = *Fmt;
			bool bUsed = false;
			while (*P)
			{
				if (*P == TEXT('%') && *(P + 1))
				{
					const TCHAR C = *(P + 1);
					if (C == TEXT('%')) { Out.AppendChar(TEXT('%')); P += 2; continue; }
					if (!bUsed)
					{
						switch (C)
						{
						case TEXT('d'): case TEXT('i'): case TEXT('u'): Out += FString::FromInt(Arg.ToInt()); bUsed = true; P += 2; continue;
						case TEXT('x'): Out += FString::Printf(TEXT("%x"), Arg.ToInt()); bUsed = true; P += 2; continue;
						case TEXT('s'): Out += Arg.ToString(); bUsed = true; P += 2; continue;
						case TEXT('f'): Out += FString::Printf(TEXT("%f"), Arg.ToFloat()); bUsed = true; P += 2; continue;
						case TEXT('g'): Out += FString::SanitizeFloat(Arg.ToFloat()); bUsed = true; P += 2; continue;
						default: break;
						}
					}
				}
				Out.AppendChar(*P);
				++P;
			}
			return Out;
		}

		FVal EvalAssign(const FNode& N)
		{
			const FNode& Target = *N.Kids[0];
			const FVal Value = EvalNode(*N.Kids[1]);
			if (Env.bError) { return Void(); }
			const FElysiumVariant V = ToVariant(Value);
			if (Env.bError) { return Void(); }

			if (Target.Type == ENode::Attr)
			{
				const FVal Obj = EvalNode(*Target.Kids[0]);
				if (Env.bError) { return Void(); }
				const FString& Attr = Target.Str;

				if (Obj.Kind == FVal::EKind::GBag)
				{
					// G.<flag> = value; assigning None (Void) deletes the key (tp_setattr semantics).
					if (Env.State) { Env.State->SetGlobal(Attr, V); }
					return FVal::FromVar(V);
				}
				if (Obj.IsVar() && Obj.Var.IsHandle())
				{
					FElysiumEntityWorld* W = World();
					FElysiumEntity* E = W ? W->Resolve(Obj.Var.AsHandle) : nullptr;
					if (!E) { Fail(TEXT("game entity has been deleted")); return Void(); }
					const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
					const FName AttrName(*Attr);
					const FElysiumFieldAccessor* F = E->Class ? Reg.FindField(*E->Class, AttrName) : nullptr;
					if (!F)
					{
						// The sheet bag (11.4), mirroring the read path.
						if (E->SetDynamicField(AttrName, V)) { return FVal::FromVar(V); }
						Fail(FString::Printf(TEXT("entity has no writable attribute '%s'"), *Attr));
						return Void();
					}
					if (!F->bKeyable) { Fail(FString::Printf(TEXT("'%s' is read only"), *Attr)); return Void(); }
					F->Set(*E, V);
					return FVal::FromVar(V);
				}
				Fail(TEXT("cannot assign to attribute of this value"));
				return Void();
			}

			// Bare-name target (`x = ...`) would write __main__; no module dict modelled in 5.2.
			Fail(TEXT("assignment to a bare name is not supported yet"));
			return Void();
		}

		FEnv& Env;
	};
}

namespace ElysiumExpr
{
	FElysiumVariant Eval(const FString& Source, FEnv& Env)
	{
		Env.bError = false;
		Env.Error.Reset();

		TArray<FToken> Toks;
		FLexer Lex(Source, Env.Error, Env.bError);
		Lex.Tokenize(Toks);
		if (Env.bError) { return FElysiumVariant::Void(); }

		FParser Parser(Toks, Env.Error, Env.bError);
		FNodePtr Node = Parser.ParseStatement();
		if (Env.bError) { return FElysiumVariant::Void(); }
		if (Node && Node->Type == ENode::Assign)
		{
			Env.bError = true;
			Env.Error = TEXT("assignment is not an expression (use Exec)");
			return FElysiumVariant::Void();
		}
		if (!Parser.ExpectEndOrSemicolon()) { return FElysiumVariant::Void(); }

		FInterp Interp(Env);
		const FElysiumVariant Result = Interp.EvalNodeToVariant(*Node);
		if (Env.bError)
		{
			UE_LOG(LogElysiumExpr, Verbose, TEXT("eval error: %s  <=  %s"), *Env.Error, *Source);
			return FElysiumVariant::Void();
		}
		return Result;
	}

	FElysiumVariant Exec(const FString& Source, FEnv& Env)
	{
		Env.bError = false;
		Env.Error.Reset();

		TArray<FToken> Toks;
		FLexer Lex(Source, Env.Error, Env.bError);
		Lex.Tokenize(Toks);
		if (Env.bError) { return FElysiumVariant::Void(); }

		FParser Parser(Toks, Env.Error, Env.bError);
		FInterp Interp(Env);
		FElysiumVariant Last = FElysiumVariant::Void();

		// One or more ';'-separated statements; an error aborts the rest (error-to-false).
		for (;;)
		{
			if (Parser.AtEnd()) { break; }
			FNodePtr Node = Parser.ParseStatement();
			if (Env.bError) { break; }
			Last = Interp.EvalNodeToVariant(*Node);
			if (Env.bError) { break; }
			if (!Parser.ConsumeSemicolon()) { break; }
		}
		if (!Env.bError) { Parser.ExpectEndOrSemicolon(); }

		if (Env.bError)
		{
			UE_LOG(LogElysiumExpr, Verbose, TEXT("exec error: %s  <=  %s"), *Env.Error, *Source);
			return FElysiumVariant::Void();
		}
		return Last;
	}
}
