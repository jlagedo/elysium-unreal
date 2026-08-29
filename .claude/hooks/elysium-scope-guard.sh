#!/usr/bin/env bash
# PreToolUse(Bash) hook: gate the expensive and destructive elysium commands.
#
# `pipeline/CLAUDE.md` requires explicit owner acceptance before a whole-profile export, a
# reconstruct, a broad --force/--clean, or the complete Python suite. Prose is advisory; this
# turns the same rule into a permission prompt that states the cost, so the acceptance is
# recorded as an approval rather than assumed.
#
# The decision is "ask", never "deny": the owner may well want the run, and denying would make
# the approved path unreachable.

payload=$(cat)

# Pull the command string out of the tool input, honouring JSON escapes.
cmd=$(printf '%s' "$payload" |
  sed -n 's/.*"command"[[:space:]]*:[[:space:]]*"\(\([^"\]\|\.\)*\)".*/\1/p' |
  head -n 1)

[ -z "$cmd" ] && exit 0

reason=""
case "$cmd" in
  *"elysium reconstruct"*)
    reason="reconstruct rebuilds the whole project from its declared inputs." ;;
  *"elysium export grid"*|*"elysium export all"*)
    reason="a whole-profile export runs every map through export and bake." ;;
  *"export characters"*)
    # Unscoped only: a trailing stem argument is a focused, allowed run.
    if printf '%s' "$cmd" | grep -Eq 'export characters[[:space:]]*(--[a-z-]+([[:space:]]+|=)?)*$'; then
      reason="unscoped 'export characters' bakes the whole cast."
    fi ;;
esac

if [ -z "$reason" ]; then
  case "$cmd" in
    *elysium*--force*|*elysium*--clean*)
      reason="--force/--clean discards generated state and forces a full regeneration." ;;
  esac
fi

# A bare `pytest` is the whole suite; a run that names a path, a node id or a `-k` selection
# is a focused one and is not gated.
if [ -z "$reason" ]; then
  case "$cmd" in
    *pytest*)
      if ! printf '%s' "$cmd" | grep -Eq 'pytest[[:space:]]+[^-]|::|-k[[:space:]=]|--last-failed|--lf'; then
        reason="this runs the complete Python suite."
      fi ;;
  esac
fi

[ -z "$reason" ] && exit 0

python - "$reason" <<'PY' 2>/dev/null || printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"ask","permissionDecisionReason":"Scope gate: this is a broad export/bake/reconstruct operation. State the command, scope, reason and expected cost, and get explicit owner acceptance first (pipeline/CLAUDE.md)."}}\n'
import json, sys
print(json.dumps({"hookSpecificOutput": {
    "hookEventName": "PreToolUse",
    "permissionDecision": "ask",
    "permissionDecisionReason": (
        "Scope gate: " + sys.argv[1] +
        " State the command, scope, reason and expected cost, and get explicit owner"
        " acceptance first (pipeline/CLAUDE.md)."),
}}))
PY
