#!/usr/bin/env bash
# PreToolUse hook: editing CLAUDE.md always asks for confirmation first.
#
# CLAUDE.md carries standing project instructions that steer every later turn. A silent allow
# lets that steering change without the user noticing; a silent deny blocks legitimate fixes.
# Neither default is right, so the call is routed to the normal permission prompt instead.

payload=$(cat)

json_field() {
  printf '%s' "$payload" |
    sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\(\([^\"\\\\]\|\\\\.\)*\)\".*/\1/p" |
    head -n 1
}

ask() {
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"ask","permissionDecisionReason":"%s"}}\n' "$1"
  exit 0
}

tool=$(json_field tool_name)

case "$tool" in
  Edit|Write|NotebookEdit|MultiEdit)
    path=$(json_field file_path)
    case "$path" in
      *CLAUDE.md|*CLAUDE.md\"*)
        ask "CLAUDE.md holds standing project instructions. Confirm this edit is intended." ;;
    esac ;;
  Bash)
    cmd=$(json_field command)
    target='(^|[|;&[:space:]])(>>?|tee([[:space:]]+-a)?)[[:space:]]*([^[:space:]]*/)?CLAUDE\.md([[:space:]]|$|[|;&])'
    lastarg='(^|[|;&[:space:]])(cp|mv|rm|truncate|sed[[:space:]]+-i)([^|;&]*[[:space:]])([^[:space:]]*/)?CLAUDE\.md[[:space:]]*($|[|;&])'
    if printf '%s' "$cmd" | grep -Eq "$target" || printf '%s' "$cmd" | grep -Eq "$lastarg"; then
      ask "That writes CLAUDE.md, which holds standing project instructions. Confirm this is intended."
    fi ;;
esac

exit 0
