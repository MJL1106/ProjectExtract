# Enemy wiring via NeoStack curl loop — HANDOFF

Proven 2026-06-10 overnight. The full **code→build→boot→wire→close** loop works, driven over raw HTTP/curl (NOT Claude Code's MCP client, which won't attach after a mid-session editor reboot). This doc lets a fresh session finish the wiring fast.

## How to drive the editor
- Editor must be **open** (NeoStack MCP server lives on `http://127.0.0.1:9315/mcp`, server `unreal-editor`).
- Helper: `bash agent_docs/neostack_loop/ns.sh <lua-file>` — re-handshakes each call, prints `print()` output. Write Lua to a Windows-path file (e.g. `C:/Users/matth/AppData/Local/Temp/nsloop/script.lua`) — MSYS `/tmp` is invisible to the Windows curl/python the helper uses.
- Launch editor: `Start-Process "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "<uproject>"`; ready when `curl 127.0.0.1:9315/mcp` returns any HTTP code. Close: `Stop-Process -Name UnrealEditor -Force`. Full C++ builds need the editor CLOSED.

## NeoStack `execute_script` (Lua) cheatsheet — learned gotchas
- `create_asset(path, type, opts?)` — types: `blackboard`, `behaviortree`, `EnemyArchetypeData` (custom DA class name works), `Blueprint` (`{ParentClass="EnemyCharacter"}` — C++ class by name).
- `open_asset(path)` → enriched object. Generic: `set(prop, value)`, `get(prop)`, `add(type, params)`, `configure(type, id, params)`, `list(type)`, `save()`, `compile()` (Blueprints).
- **Object refs**: `set("CombatSubtree", "/Game/.../BT_x")` (path). **Class refs (TSubclassOf)**: append `_C` → `set("WeaponClass", "/Game/Core/Weapons/BP_Rifle.BP_Rifle_C")`.
- **Blackboard keys**: `bb:add("key", {name=..., type="Object"|"Vector"|"Bool"|"Enum", base_class="Actor", enum_type="EEnemyAwarenessState"})`. (SelfActor auto-exists.)
- **BT nodes**: `bt:configure("blackboard", "/Game/.../BB")`; root composite needs `add("composite", {parent="Root", class="BTComposite_Selector"})`; children `parent=<guid>`; `add("task"|"service"|"decorator", {parent=<guid>, class=...})`. Returns `{guid=...}`.
- **Blackboard decorator** (gate a branch by enum): `bt:configure("decorator", <guid>, {BlackboardKey="AwarenessState", IntValue=<n>, FlowAbortMode="Both"})`. Enum ints: Unaware=0, Suspicious=1, Searching=2, Combat=3. Default op = Equal.
- **RunBehaviorDynamic tag**: `bt:configure("node", <guid>, {InjectionTag="BT.EnemyCombat"})`.
- **BT child order** is graph-X-position based; `reorder_children` had no visible effect (all nodes at x=0). Doesn't matter functionally if branches use mutually-exclusive decorators and the undecorated fallback (Patrol) is last.
- `list("nodes")` reads the tree back (incl. decorators/services). `read_graph(path)` gives main nodes (not decorators). Global `list_node_properties`/`set_node_property` are Blueprint/Material only — they FAIL on BT element guids.
- **Inherited component props (unsolved here)**: `set("Mesh.SkeletalMeshAsset", ...)` FAILS (CDO dot-path doesn't reach the inherited component); `get_object` isn't a Blueprint method. Needs the SCS/inherited-component-override API — find it, or set the mesh manually.

## DONE & verified (all under /Game/Enemy/)
| Asset | State |
|---|---|
| `AI/BB_Enemy` | 9 keys + SelfActor; AwarenessState→EEnemyAwarenessState ✓ |
| `AI/BT_EnemyCombat_Grunt` | Selector→Sequence[EnemyMoveToCover, EnemyCombatFire]→EnemyCombatFire ✓ |
| `AI/BT_EnemyBase` | Combat/Search/Suspicious/Patrol; 3 Blackboard decorators set; Enemy Combat Service; RunBehaviorDynamic InjectionTag=BT.EnemyCombat. Incl. Phase-2 Suspicious branch (EnemyFaceSuspicion). Cosmetic order only. |
| `AI/AIC_Enemy` | EnemyAIController child; `EnemyBehaviorTree`=BT_EnemyBase ✓ |
| `AI/DA_Enemy_Grunt` | EnemyArchetypeData; `CombatSubtree`=BT_EnemyCombat_Grunt; `WeaponClass`=BP_Rifle_C ✓ (`BarkSet` pending) |
| `BP_Enemy_Grunt` | EnemyCharacter child; `AIControllerClass`=AIC_Enemy_C; `ArchetypeData`=DA_Enemy_Grunt ✓ — **mesh/anim NOT set** |

C++ property names: controller `EnemyBehaviorTree`; DA `CombatSubtree`/`WeaponClass`/`BarkSet`; character `ArchetypeData`/`PatrolRoute`.
Reuse assets: mesh `/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple`; anim `/Game/Core/Blueprints/AI/Companion/ABP_CompanionMain` (_C for AnimClass); weapon `/Game/Core/Weapons/BP_Rifle`.

## Phase 2 — DONE (2026-06-10)
- `Enemy/AI/DA_Barks_Grunt` (BarkSetData) — 5 entries (map_set with ImportText struct literals, FText auto→NSLOCTEXT); wired into `DA_Enemy_Grunt.BarkSet`.
- `Core/Input/Actions/IA_Takedown` (InputAction, Boolean) + mapped **F** in `Core/Input/IMC_Default` (the applied supplementary context, alongside lean) + `BP_FPCharacter.TakedownAction` set (save-only, NO compile — compiling that 908-node BP crashed the editor).
- `Core/UI/WBP_BarkFeed` (parent BarkFeedWidget) — CanvasPanel→bottom-center VerticalBox→`BarkText` TextBlock; `OnBarkReceived` event → `SetText(BarkText, Line)` wired + compiled. (Fade-after-3s polish not added.)

## REMAINING (manual — small)
1. **BP_Enemy_Grunt mesh/anim** — `SKM_Quinn_Simple` + AnimClass `ABP_CompanionMain`, Mesh RelLoc Z=-90, Yaw=-90. **NeoStack can't set an inherited component template via the CDO** (set("Mesh.X"), get_object, Reflection all fail) — 30s manual in the BP.
2. **Suppressed weapon DA variant** — duplicate `Core/Weapons/DA_AssaultRifle` → set `bSuppressed=true`, low `NoiseLoudness`/`NoiseRange`. `duplicate_asset` timed out (60s cap) twice and was crash-adjacent — do it manually.
3. **Level placement** — `APatrolRoute` (3+ points) + 2× BP_Enemy_Grunt in a test level (e.g. Lvl_FirstPerson); set each grunt's `PatrolRoute`; build navmesh. (User is handling this.)

## Crash note
Programmatic `compile()` on a large BP (the 908-node `BP_FPCharacter`) crashed the editor once. The curl loop recovered cleanly (kill + relaunch + re-handshake; all saved assets intact). Avoid `compile()` on heavy BPs — `set`+`save` persists CDO props without it.

QA acceptance for Phase 1 is in `agent_docs/enemy_code_plan.md` (§Phase 1 QA).
