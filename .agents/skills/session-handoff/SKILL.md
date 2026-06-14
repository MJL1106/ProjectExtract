---
name: session-handoff
description: Use when the user says "session handoff", "wrap up session", "hand off", "handoff summary", or wants a structured end-of-session summary before clearing context on UE5 work. Produces a chat-only handoff covering decisions, shipped code, modules touched, editor/build state, verification steps, deferrals, and open questions so a fresh agent can continue seamlessly.
---

# Session Handoff (UE5)

Produce a repeatable end-of-session summary so the user can `/clear` and start a fresh agent without losing continuity. The next agent should be able to pick up by reading this summary alone.

This is a **context-handoff artifact**, not a status report. The audience is a future instance of you working on this Unreal Engine 5 project, not a stakeholder.

## When to invoke

User says: "session handoff", "wrap up session", "hand off", "handoff summary", "let's wrap up", "summarize before I clear", or any near-equivalent. Also invoke proactively if the user says they're about to `/clear` without having run it yet.

## How to produce the summary

1. **Review the full conversation**, not just the last few turns. Handoffs miss things when they only summarize recent context.
2. **Pull state from these sources (in order):**
   - Plan files referenced this session (check `C:\Users\matth\.Codex\plans\` if a plan was mentioned).
   - TodoWrite state — any in-progress or pending tasks.
   - Background processes you started with `run_in_background` — shell IDs are load-bearing for the next agent (builds, hot-reload compiles, automation test runs).
   - C++ files created or modified this session — note `.h`/`.cpp` pairs, and any `Build.cs` / `Target.cs` edits.
   - Assets touched in the editor (Blueprints, DataAssets, DataTables, Maps, input mapping contexts) — the next agent can't diff these, so list them explicitly.
   - New `UCLASS`, `USTRUCT`, `UENUM`, `UFUNCTION`, or module boundaries introduced.
   - Module / plugin config changes — `.uproject`, `.uplugin`, module `PublicDependencyModuleNames` / `PrivateDependencyModuleNames` edits.
   - Reflection-affecting changes — anything that requires an editor restart vs. Live Coding vs. a full rebuild.
   - Unresolved questions — things you asked the user that never got a clear answer, or things the user asked that got deflected.
3. **Do NOT audit the filesystem.** This is synthesis of what happened in THIS session. No broad `Glob` sweeps, no `git log`. If you didn't touch it this session, it doesn't belong here.
4. **Produce the output in chat.** Do not write a file. Do not update memory. Chat-only.

## Output template — use exactly this structure, every time

```
# Session Handoff — <one-line title of what this session was about>

## Where it started
<2-3 sentences: what the user asked for, key framing or constraints that emerged>

## Decisions locked + what shipped
- <decision or change> — <why, and where it lives (absolute path if a file, asset path if in-editor)>
- ...

## Key files for next session
- `<absolute path>` — <why the next agent should read this first>
- Plan file: `<path>` (if a plan drove the session)
- Header/impl pairs touched: `<ClassName.h>` + `<ClassName.cpp>` — <one-line purpose>
- Build config touched: `<Module.Build.cs>` / `<Project.Target.cs>` — <what changed>

## Assets modified in-editor
- `<ContentBrowserPath>` (<asset type>) — <what changed and why>
- ...
- Or "none" if only C++ was touched.

## Running state
- Background processes: <shell IDs + what they are + how to kill> — or "none"
- Unreal Editor open: <yes/no, which project, PIE running?> — or "none"
- Build state: <clean / dirty / mid-rebuild / Live Coding patch pending> — or "none"
- UBT / compile jobs: <running? log path?> — or "none"
- Open worktrees / branches: <paths> — or "none"

## Rebuild / reload requirements
- <Requires editor restart? Live Coding sufficient? Full rebuild needed? Hot reload unsafe?>
- <Any header changes that affect BP-exposed reflection (UPROPERTY/UFUNCTION signatures)? Those force an editor close.>
- Or "no rebuild needed — code unchanged this session".

## Verification — how to confirm things still work
- `<command>` — <expected outcome, e.g. UBT build from CLI, automation test run>
- PIE smoke test: <what to click / spawn / check> — <expected result>
- Automation tests: `<test spec to run>` — <expected result>
- ...

## Deferred + open questions
- Deferred: <item> — <why pushed to later>
- Open: <question needing the user's input> — <context>

## Pick up here
<1-2 sentences: the single most likely next action for a fresh agent>
```

## Hard rules

1. **Chat output only.** Never write the handoff to a file. Never update memory from this skill.
2. **Never invent state.** If a section has nothing to report, write "none" — do not omit the section. Structure stability is the whole point.
3. **Absolute paths always** for source files. **Content Browser paths** (`/Game/...`) for assets. The next agent may have a different working directory.
4. **If a plan file drove the session, name it first** in "Key files" so the next agent reads it before anything else.
5. **No emojis, no hype, no "great job" summaries.** Terse and concrete — paths, commands, shell IDs, decisions. Match the tone of a seasoned engineer handing off at end-of-shift.
6. **Background process IDs are critical.** If you started any `run_in_background` shells (UBT, cook, package, automation), their IDs must appear in "Running state" with the kill command — the next agent cannot find them otherwise.
7. **Reflection state is load-bearing.** If the session edited `UPROPERTY`/`UFUNCTION`/`UCLASS` signatures, say so. The next agent must know whether the editor needs a full restart before opening affected assets.
8. **Assets are invisible to the next agent's diff.** If you modified a Blueprint, DataAsset, map, or input mapping, list it under "Assets modified in-editor" — otherwise the next agent will think nothing changed.

## Anti-patterns — do not do these

- Summarizing the last 3 turns and calling it a handoff.
- Listing source files by relative path, or assets by disk path instead of Content Browser path.
- Skipping the "Running state" or "Rebuild / reload requirements" sections because "nothing is running" — write "none" instead.
- Writing the summary to `~/.Codex/handoffs/` or any file. This is chat-only by design.
- Adding a "what went well / what went poorly" retrospective. This isn't a retro.
- Recommending next steps beyond the single "Pick up here" line. The next agent decides; you just hand off.
- Forgetting to note editor restart requirements after header signature changes — this is the single most common UE5 handoff failure.
