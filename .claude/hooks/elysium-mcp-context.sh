#!/usr/bin/env bash
# SessionStart hook: name this project's Unreal Editor MCP server, and say what a probe of it is
# evidence of.
#
# Epic's unreal-engine-skills-for-claude-code plugin injects its own SessionStart line pointing
# at the `unreal-mcp` skill, and that skill instructs the agent to treat a missing MCP server
# named `unreal-mcp` as proof the editor is down. Here the same kind of server is named
# `elysium` and is reached through a stdio proxy, so this hook states that outright. Project
# hooks run after plugin hooks, so the correction lands in the same context block.
#
# The payload is a static JSON literal: no probe and no shell string escaping, because both are
# ways to fail at session start for a fact that never varies. In particular a closed loopback
# port on Windows times out rather than refusing, so probing the game would risk the hook's
# timeout to report something the proxy already reports on the first tool call.
#
# The closing sentence exists because Epic's skill says to use the live editor "instead of" other
# means, and an agent reads that as: a readout that agrees with the code is proof. It is not.

cat <<'JSON'
{"hookSpecificOutput":{"hookEventName":"SessionStart","additionalContext":"This project's Unreal Editor MCP server is named `elysium`, not `unreal-mcp`.\nIt is the same kind of server the `unreal-mcp` skill describes: tool search is on, so `list_toolsets`, `describe_toolset` and `call_tool` are the only advertised tools and every other tool dispatches server-side through the `ToolsetRegistry`. Calls run on the game thread and must be issued sequentially, never in parallel.\n`.mcp.json` registers it as a stdio server running the reconnecting proxy `pipeline/src/elysium_pipeline/devtools/mcp_proxy.py`, which bridges to the in-process HTTP endpoint at http://127.0.0.1:8000/mcp and survives a rebuild-and-relaunch on its own.\nSo no server literally named `unreal-mcp` will ever appear, and its absence is not evidence that the editor is down. Do not run `ModelContextProtocol.GenerateClientConfig` and do not follow the skill's setup reference: `.mcp.json` is hand-written around the proxy, and regenerating it would overwrite that.\nWhen the game is down the proxy still answers, serving `tools/list` from its on-disk cache and returning a \"game not running\" result from `tools/call`. Relaunch with `uv run elysium run play` or `uv run elysium run editor`; no `/mcp` reconnect is needed.\nIn this repository a live MCP probe is a diagnosis aid, never acceptance: a readout is the runtime's claim about itself and locates the link that broke. Acceptance is a measurement at the seam the owner sees — the tiers and harnesses the `elysium-testing` skill names."}}
JSON
