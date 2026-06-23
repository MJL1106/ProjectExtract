---
name: inengine-agent
description: Use when in-engine Unreal Editor work needs doing via MCP — Blueprint graphs, materials, UMG/HUD widgets, Niagara, DataAsset/DataTable population, Behavior Trees, Blackboards, EQS, animation BPs, asset import, level-actor placement/wiring — and you want it driven autonomously rather than handed to the user as a manual checklist. Triggers on "wire up", "set up the BP", "build the widget", "author the behavior tree", "create the material", "place the actors", "populate the data asset", or any editor-side asset work.
metadata:
  author: Matthew
  category: ue5-development
  project: ProjectExtract
---

# Spawn the In-Engine Agent (ProjectExtract)

## Purpose
Dispatch the `ue5-inengine-agent` (Opus) to drive the running Unreal Editor via VibeUE / NeoStack MCP and do the in-editor work itself — instead of the main chat doing fiddly MCP edits inline, or handing the user a manual checklist.

This packages all the in-engine pointers (tooling map, VibeUE + local skill libraries, the `agent_docs/UnrealWorkflow.md` gotchas) into one pre-briefed agent, so editor work is faster and the main chat stays free to plan/review.

## In-engine work has three paths — pick one

| Path | When |
|---|---|
| **`inengine-agent`** (this skill → dispatch `ue5-inengine-agent`) | You want the editor work **done autonomously** via MCP. The editor is (or can be) running. Default for any real wiring/authoring task. |
| `inengine-checklist` | You want a **human** to do it — short manual task (place an actor, set a property, one child BP). Plain-English numbered steps. |
| `inengine-prompt` | You want a **human** to do a **large/repetitive** manual task (BP graph edits, >5 reference wires, bulk same-property edits). |

If the user prefers to do editor work themselves, use the checklist/prompt skills. Otherwise dispatch the agent.

## Preconditions
- **Editor running.** The agent drives live MCP servers. If the editor is down, the agent will boot it (`boot-engine` skill) — but if a full C++ rebuild is pending, build with the editor CLOSED first.
- **No pending C++.** This agent does **not** write C++. If the task needs a new C++ class/property, do that via `ue5-cpp-implementer` + build first, then dispatch this agent to wire the assets.

## How to dispatch
Use the `Agent` tool with `subagent_type: ue5-inengine-agent`. Brief it with:
1. **The goal** in one or two sentences.
2. **Exact asset paths / names** to create or edit (e.g. `/Game/AI/BT_Grunt`, `WBP_HUD`, `M_Shield`), and which **domain** (BT / material / UMG / DataAsset / level).
3. **The values to set** — verbatim. Don't make the agent guess tuning numbers, colours, key names.
4. **Any C++ surface it wires against** (e.g. "the Blackboard key `TargetActor` is read by `UBTTask_Engage`") so it knows the contract.

For Behavior Tree / Blackboard / EQS work, say so explicitly — the agent will route through **NeoStack** (VibeUE can't author BTs).

## After it returns
- The agent reports assets touched (full paths) + how it verified (compile clean / read-back / screenshot / PIE).
- Main chat reviews the report; if it flagged a missing C++ surface, dispatch `ue5-cpp-implementer` for that, rebuild, then re-dispatch the in-engine agent to finish wiring.
- Don't declare the in-engine task done until the agent's verification (read-back / screenshot) confirms it.

## Don't dispatch for
- Pure C++ work (→ `ue5-cpp-implementer`).
- A one-line question about an asset (just answer / use `manage_asset` lookup).
- Manual work the user has said they'll do themselves (→ `inengine-checklist` / `inengine-prompt`).
