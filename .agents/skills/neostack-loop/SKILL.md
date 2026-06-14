---
name: neostack-loop
description: Drive the running Unreal Editor's NeoStack MCP from Codex CLI over raw curl — author/edit in-engine assets (Blackboards, Behavior Trees, Blueprints, DataAssets, Widgets, Input) directly from the terminal, and run the autonomous code→build→boot→wire→close loop across editor reboots. Use when the task needs editor-side wiring done from the CLI (not handed to the in-engine chat agent), when execute_script must survive an editor restart, or whenever you'd otherwise tell the user to do manual BP/asset wiring that the loop can do instead. Triggers on "neostack", "ns.sh", "drive the editor", "in-engine wiring from CLI", "execute_script", "the loop".
---

# NeoStack editor loop (curl-driven)

The NeoStackAI plugin runs an MCP server inside the open editor at `http://127.0.0.1:9315/mcp` (server name `unreal-editor`), exposing one Lua tool: **`execute_script`**. This skill drives it **over raw curl from Bash** — NOT Codex's MCP client, which is launch-bound and won't reattach after the editor reboots. Curl re-handshakes on demand, so this works through the close/reopen cycles a C++ build requires.

The helper is `.Codex/skills/neostack-loop/ns.sh`. Prereq: the editor is **open** (server only exists while it runs). Port can auto-scan off 9315 if busy — re-read the NeoStack "MCP Server" panel if curl gets connection-refused.

## Run a Lua script

Write Lua to a **Windows-path** file (MSYS `/tmp` is invisible to the Windows curl/python the helper shells out to), then:

```bash
WD="C:/Users/matth/AppData/Local/Temp/nsloop"; mkdir -p "$WD"
cat > "$WD/script.lua" <<'LUA'
print("hello " .. (6*7))      -- print() output returns in result.content[0].text
LUA
bash .Codex/skills/neostack-loop/ns.sh "$WD/script.lua"
```

`ns.sh` does initialize → notifications/initialized → tools/call execute_script each invocation (stateless, so it survives reboots). NeoStack caps each `execute_script` at **60s** — keep scripts short; heavy ops (big-BP `compile()`, `duplicate_asset` of dep-heavy assets) time out and can crash the editor.

## Editor lifecycle

```bash
# launch
powershell.exe -NoProfile -Command "Start-Process 'C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe' -ArgumentList '\"C:\Users\matth\Documents\Github\ProjectExtract\Extraction\Extraction.uproject\"'"
# poll until ready (port answers AND handshake returns a session id), then drive
for i in $(seq 1 150); do c=$(curl -s -o /dev/null -w "%{http_code}" --max-time 3 http://127.0.0.1:9315/mcp); [ -n "$c" ] && [ "$c" != "000" ] && { echo READY; break; }; sleep 4; done
# close (required before a full C++ build — Live Coding blocks Build.bat)
powershell.exe -NoProfile -Command "Stop-Process -Name UnrealEditor -Force"
```

**The loop:** edit C++ → close editor → `Build.bat` (confirm `Result: Succeeded`) → relaunch editor → poll ready → drive wiring via `ns.sh` → close → repeat. Editor **closed** for builds, **open** for wiring.

## Crash recovery (expected, not exceptional)

If `execute_script` returns empty / handshake yields no session id, the editor likely crashed or hung. Recover — saved assets survive on disk:

```bash
powershell.exe -NoProfile -Command "Get-Process UnrealEditor,CrashReportClientEditor -ErrorAction SilentlyContinue | Stop-Process -Force"
# relaunch + poll ready (above), then re-handshake (ns.sh does this automatically) and re-verify state
```
A `CrashReportClientEditor` appearing right after a relaunch is the harmless "report previous crash?" prompt — kill it; if `print()` round-trips, the editor is fine.

## execute_script (Lua) API cheatsheet

`create_asset(path, type, opts?)` / `open_asset(path)` → enriched object. Generic methods: `set(prop,val)`, `get(prop)`, `add(type,params)`, `configure(type,id,params)`, `list(type)`, `map_set/map_get/map_count(prop,...)`, `array_add/array_count(prop)`, `configure_at(arrayProp,idx,{...})`, `save()`, `compile()` (Blueprints). Discover live with `help()`, `help('Domain')`, `obj:help()`, `list_node_properties(handle)` (Blueprint/Material graphs only), `list_asset_types()`, Reflection `class_properties(class)`.

- **Asset types:** `blackboard`, `behaviortree`, `Blueprint` (`{ParentClass="CppClassName"}`), `WidgetBlueprint` (`{ParentClass=...}`), `InputAction`, custom DataAsset class name works directly (e.g. `"EnemyArchetypeData"`).
- **Object refs:** `set("Prop","/Game/.../Asset")`. **Class refs (TSubclassOf):** append `_C` → `set("WeaponClass","/Game/.../BP_X.BP_X_C")`.
- **Blackboard keys:** `bb:add("key",{name=...,type="Object"|"Vector"|"Bool"|"Enum",base_class="Actor",enum_type="EMyEnum"})`. SelfActor auto-exists.
- **Behavior Tree:** `bt:configure("blackboard","/Game/.../BB")`; root composite needs `add("composite",{parent="Root",class="BTComposite_Selector"})`; children `parent=<guid>`; `add("task"|"service"|"decorator",{parent=<guid>,class=...})`. Blackboard decorator: `bt:configure("decorator",<guid>,{BlackboardKey="State",IntValue=<enumInt>,FlowAbortMode="Both"})` (enum compare defaults to Equal). `list("nodes")` reads it back. **BT child order = graph X-position** (all new nodes land at x=0); `reorder_children` is unreliable — design branches to be order-independent (mutually-exclusive decorators + the undecorated fallback last).
- **TMap of struct values:** `d:map_set("Map","EnumKeyName",[[(Field=("a","b"),Num=6.0)]])` — use Lua `[[ ]]` strings so inner `"` and `'` survive. FText auto-converts to NSLOCTEXT.
- **Widgets (UWidgetBlueprint):** `add_widget("Type",{name=,parent=,is_variable=true})` (first call, no parent, installs root); `configure_widget(name,{...,slot={LayoutData="(Anchors=...,Offsets=...,Alignment=...)"}})` (CanvasPanel anchors live in `LayoutData`); events via `override_function("OnX")` for BlueprintImplementableEvents, then `read_graph(path,"EventGraph")` for handles, `find_nodes`/`add_node`/`connect` to wire. Member-function nodes (e.g. `SetText (Text)`) only surface in `find_nodes` once you know the class context.
- **Input mapping context:** `imc:array_add("Mappings")` then `imc:configure_at("Mappings",idx,{Action="/Game/.../IA_X",Key="F"})`.
- **Verify everything by read-back** (`list`, `get`, `read_graph`) before claiming done — typed/enum/ref values silently no-op when wrong.

## Failure modes

| Symptom | Cause / fix |
|---|---|
| `ns.sh` prints `RAW:` (empty) | execute_script hit 60s cap or the editor hung/crashed → recover. |
| `python ... No such file` / curl writes nowhere | MSYS `/tmp` vs Windows path. Use `C:/Users/.../Temp/...` for any file curl/python touch. |
| Editor crashed mid-wiring | Almost always a programmatic `compile()` on a large existing BP. Use `set`+`save` (CDO props persist without compile). Recover + re-verify. |
| `set("Comp.Prop")` fails on an inherited component | NeoStack can't set an inherited component template via the CDO (dot-path, `get_object`, Reflection all fail). Flag it as a manual editor step. |
| Tools not in ToolSearch after `/mcp` | Don't fight it — drive over curl instead; Codex's MCP client won't reattach a server that was down at launch. |
| `add("composite")` "parent and class required" on the root | The first composite needs `parent="Root"`. |

## Notes

- Betide/NeoStack auth: `BETIDE_API_TOKEN` user env var (shared across projects).
- The original write-up + project-specific asset paths live in `agent_docs/neostack_loop/HANDOFF.md`.
- This skill is for **asset/BP/widget wiring** from the CLI. C++ is still written/built by Codex the normal way — never write or compile C++ through the editor MCP.
