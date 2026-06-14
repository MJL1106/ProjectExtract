---
name: neostack-mcp-connect
description: Use when the unreal-editor / NeoStack bridge tool (mcp__unreal-editor__execute_script) is missing, disconnected, or ToolSearch can't find it — documents the live MCP endpoint and the reconnect procedure that restores editor-driving from this Codex session. Trigger when execute_script calls fail, the bridge "dropped", or after the editor was closed/reopened mid-session.
---

# Reconnecting the NeoStack `unreal-editor` MCP bridge

The Pebble editor exposes a live MCP server (NeoStack plugin → **Details ▸ Agent Chat ▸ "MCP Server"** panel) that lets *this* Codex session drive the editor via `mcp__unreal-editor__execute_script`.

## Endpoint (from the NeoStack "MCP Server" panel)
- **Server name:** `unreal-editor`
- **Primary URL (Streamable HTTP):** `http://127.0.0.1:9315/mcp`
- **Alternate localhost:** `http://localhost:9315/mcp`
- **Legacy SSE (older clients):** `http://127.0.0.1:9315/sse`
- The plugin may **auto-scan to a nearby port** if 9315 was busy — re-read the panel for the actual port before trusting 9315.

## Register it (one-time, only if `Codex mcp list` doesn't show it)
```
Codex mcp add --transport http unreal-editor http://127.0.0.1:9315/mcp
```
Confirm: `Codex mcp list` → expect `unreal-editor: http://127.0.0.1:9315/mcp (HTTP) - ✔ Connected`.

## The stale-session symptom (the common one)
`Codex mcp list` shows `unreal-editor … ✔ Connected`, **but** `mcp__unreal-editor__execute_script` is absent from the session toolset and `ToolSearch select:mcp__unreal-editor__execute_script` returns *"No matching deferred tools found."* This happens when the editor (and its MCP server) restarted **mid-session**: the server is healthy again, but the already-running CLI never re-fetched its tool list. Retrying ToolSearch will not fix it — the server is "connected," not "connecting," so there's nothing to wait on.

> The in-editor **"Agent Chat — Connected"** panel is a *separate* ACP connection (a Codex instance inside Unreal). Its green dot does **not** mean this session has the MCP tools. Two different links.

### Fix — must be done by the user (the agent cannot reconnect an MCP server from inside a turn)
1. Run **`/mcp`** in this Codex session → select **`unreal-editor`** → **Reconnect**.
   — or — **restart the CLI** (conversation context is preserved via the session summary).
2. Verify the tool is back with a ping:
   ```lua
   print("PING_OK"); return 1
   ```

## Notes
- Project `.mcp.json` holds only `code-review-graph`; `unreal-editor` is registered at **user/local scope** (so `Codex mcp list` still shows it even though it's not in the repo file).
- If the panel shows a different port, update the registration URL to match: `Codex mcp remove unreal-editor` then re-add with the new URL.
- Health check anytime: `Codex mcp list`.
