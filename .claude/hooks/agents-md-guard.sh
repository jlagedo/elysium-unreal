#!/usr/bin/env bash
# PreToolUse hook: AGENTS.md is generated, never edited.
#
# Every AGENTS.md in this repo is a byte copy of the CLAUDE.md beside it, refreshed by
# agents-md-sync.sh on PostToolUse. A hand edit to AGENTS.md is therefore lost on the next
# CLAUDE.md write, and the pair silently drifts in the meantime -- which is the exact failure
# this pair of hooks exists to end. So the edit is denied at the door with the real target named.
#
# The decision is "deny", not "ask": there is no case where editing the generated copy is the
# right move, and approving one would still be overwritten.
#
# Case matters. The guard matches the literal AGENTS.md, so invoking the lowercase
# .claude/hooks/agents-md-*.sh scripts is not itself blocked.

payload=$(cat)

json_field() {
  printf '%s' "$payload" |
    sed -n "s/.*\"$1\"[[:space:]]*:[[:space:]]*\"\(\([^\"\\\\]\|\\\\.\)*\)\".*/\1/p" |
    head -n 1
}

deny() {
  printf '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"%s"}}\n' "$1"
  exit 0
}

tool=$(json_field tool_name)

case "$tool" in
  Edit|Write|NotebookEdit|MultiEdit)
    path=$(json_field file_path)
    case "$path" in
      *AGENTS.md|*AGENTS.md\"*)
        deny "AGENTS.md is generated from the CLAUDE.md beside it and is overwritten on the next CLAUDE.md write. Edit that CLAUDE.md instead; the sync hook mirrors it." ;;
    esac ;;
  Bash)
    cmd=$(json_field command)
    # Only a write TARGET is refused. Naming the file anywhere else -- a grep, a diff, or the text
    # of an edit to a CLAUDE.md that talks about its own mirror -- has to stay allowed. So match
    # the file as a redirect target or as the final argument of a write command, never merely
    # as a substring of the command.
    target='(^|[|;&[:space:]])(>>?|tee([[:space:]]+-a)?)[[:space:]]*([^[:space:]]*/)?AGENTS\.md([[:space:]]|$|[|;&])'
    lastarg='(^|[|;&[:space:]])(cp|mv|rm|truncate|sed[[:space:]]+-i)([^|;&]*[[:space:]])([^[:space:]]*/)?AGENTS\.md[[:space:]]*($|[|;&])'
    if printf '%s' "$cmd" | grep -Eq "$target" || printf '%s' "$cmd" | grep -Eq "$lastarg"; then
      deny "That writes AGENTS.md, which is generated from the CLAUDE.md beside it. Edit that CLAUDE.md instead; the sync hook mirrors it."
    fi ;;
esac

exit 0
