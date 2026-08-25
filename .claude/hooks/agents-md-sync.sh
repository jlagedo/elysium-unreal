#!/usr/bin/env bash
# PostToolUse hook: refresh every AGENTS.md from the CLAUDE.md beside it.
#
# AGENTS.md is a byte copy, never a variant. Pairing is by directory: a CLAUDE.md is mirrored
# only where an AGENTS.md already sits beside it, so this never invents a mirror in a directory
# that does not want one. To opt a new directory in, create its AGENTS.md once (empty is fine)
# and it stays synced from then on.
#
# The candidate list comes from `git ls-files`, so the walk is over tracked files rather than the
# whole checkout -- an Unreal working tree carries far too much Intermediate/ and Saved/ to scan
# on every tool call.
#
# It runs on Bash as well as Edit/Write because a CLAUDE.md rewritten through a shell redirect or
# a cp would otherwise leave its mirror stale, which is the drift the guard hook cannot see.
# The whole pass is a diff-then-copy over a handful of files, so a no-op tick costs nothing.

cd "${CLAUDE_PROJECT_DIR:-.}" || exit 0
command -v git >/dev/null 2>&1 || exit 0

copied=""
while IFS= read -r claude; do
  [ -f "$claude" ] || continue
  agents="${claude%CLAUDE.md}AGENTS.md"
  [ -f "$agents" ] || continue
  cmp -s "$claude" "$agents" && continue
  cp -- "$claude" "$agents" || continue
  copied="$copied $agents"
done <<EOF
$(git ls-files '*CLAUDE.md' 'CLAUDE.md' 2>/dev/null | sort -u)
EOF

[ -z "$copied" ] && exit 0

python - "$copied" <<'PY' 2>/dev/null || exit 0
import json, sys
files = sys.argv[1].split()
print(json.dumps({"hookSpecificOutput": {
    "hookEventName": "PostToolUse",
    "additionalContext": (
        "Mirrored CLAUDE.md -> " + ", ".join(files) +
        ". AGENTS.md is generated; do not edit it directly."),
}}))
PY
