# AGENTS.md — ProjectExtract

Single-player first-person shooter on Unreal Engine 5.7 with an AI companion system. Single C++ module: `Extraction`.

## Stack
- **Engine:** Unreal Engine 5.7
- **Language:** UE5 C++
- **Module:** `Extraction` (`Extraction/Source/Extraction/`)
- **In-engine tooling:** **VibeUE** (editor MCP HTTP :8088; Codex connects through the :8089 proxy in `.codex/config.toml`, primary — Blueprints/materials/UMG/assets via `execute_python_code` + `manage_skills`) + **NeoStack** (MCP HTTP :9315 — fallback when VibeUE lacks the capability, especially in-engine AI systems: Behavior Trees, Blackboards, EQS, AI Blueprint wiring). Full tooling map + gotchas: `agent_docs/UnrealWorkflow.md`.
- **API macro:** `EXTRACTION_API`
- **Solution:** `Extraction/Extraction.sln`

## Identity — are you the in-engine agent?

If the user has told you **"you are the in-engine agent"**, OR you are running inside the Unreal Editor's AIK (Agent Integration Kit) chat window, you are **not** Codex CLI. The Workflow / Custom Subagents / Codex multi-agent tools / `Bash` / build-script sections below do **not** apply to you — those tools do not exist in your environment.

**Stop here and read** `.agents/skills/aik-in-engine-agent/SKILL.md` — it's your capability reference. Do **not** read C++ source files unless the user explicitly asks; your work is in-editor (Blueprints, materials, behaviour trees, level sequences, Niagara, data assets, asset import, level editing).

If you are Codex CLI (terminal / Desktop / VS Code), ignore this section and continue.

> Shared UE5 Hard Rules, Soft Rules, and Architectural Taste live in global `~/.codex/AGENTS.md`. Only project-specific additions and overrides are listed below.

## Hard Rules (project-specific)
- MUST never hardcode `/Game/...` asset paths via `ConstructorHelpers::FObjectFinder` — designer assigns assets in Blueprint subclasses (this project uses an in-editor MCP agent for asset wiring; C++ stays asset-agnostic)
- MUST register new `Public/<Subfolder>/` and `Private/<Subfolder>/` paths in `Extraction.Build.cs`'s include arrays — the project uses explicit subfolder paths

## Architectural Taste (project-specific overrides)
- **State machines for AI and game state.** Behaviour trees are fine when designers iterate (the companion uses one) — enums + state machines for fixed flow. This overrides global's stricter anti-BT default.

---

## Communication style (user preferences — defer to these)

The user is the director, not the engineer. Cut everything that isn't load-bearing. When in doubt, cut.

### Hard rules — non-negotiable
- **Lead with the fix or the question.** No preamble ("Looking at the screenshot…", "Per Epic docs…", "Good news / bad news…", "Diagnosis:"). No restating what the user just told you.
- **No diagnosis recap when delivering the fix.** If you already named the cause earlier in the turn or prior turn, don't name it again. The fix sentence is enough.
- **One fix at a time.** Don't dump three options when one is clearly right. Side-issues get one trailing line at the very end — never woven through the answer, never their own section.
- **In-editor / in-engine steps: numbered, one line each. No "why this works".** Open editor → step 1 → step 2 → done. If the user wants the rationale, they'll ask.
- **No closing wrap-up paragraphs.** End-of-turn: one sentence or nothing. Never "Once you've done X, send me Y so we can do Z and then verify W."
- **No emoji. No stacked headers. No restating the same thing two ways. No 'Caveat:' / 'Note:' / 'Good signs:' / 'Bad signs:' labelled blocks appended to fixes.**
- **Tables only for ≥3 rows of parallel data.** Two-row tables are noise — fold into prose.

### Forms
- **Plans:** what it delivers, how it meets the goal, edge cases worth pushback. No code blocks, no class signatures, no file paths in the body — paths in one trailing note.
- **In-editor instructions:** numbered steps, one line each. No preamble, no rationale, no closer.
- **Test instructions:** scenario + expected outcome, one line each.
- **Edge cases worth surfacing:** one line each, no rationale.

### Catch yourself before sending
- Paragraph where a sentence works → cut.
- Summarising what you found before giving the fix → cut the summary.
- Caveating with "but if X then Y" → cut unless X is likely.
- Wrote "Good signs:" / "Bad signs:" / "Untouched:" / "Diagnosis:" → delete the labels, fold the one load-bearing point into a sentence.
- Wrote "This is the canonical pattern for…" / "Per Epic docs…" / "Now I have a strong picture…" → delete.

"Too much fluff" is the recurring issue the user has flagged. This section overrides the global default style.

---

## Workflow (this is the workflow — follow it, do not freelance)

- **In-engine work = invoke `inengine-checklist` skill EVERY TIME — no exceptions.** Whenever the response would tell the user to do something in the Unreal editor (open a BP, set a property, place an actor, add a component, edit a DataAsset, create a child class), invoke `inengine-checklist` first. The skill enforces a tight numbered-list format with exact menu paths and verbatim values, and strips all code/architecture commentary. Even a single editor step goes through it. If the work is large (BP graph edits, repetitive bulk, many reference wires), use `ue5-inengine-agent` instead — do not freehand prose for editor instructions.
- **Codex MUST NOT start PIE, Simulate, inject gameplay input, or autonomously playtest.** The user owns all gameplay playtesting. Codex may build, boot the editor, inspect/edit assets at edit time, and provide concise test scenarios, but must stop before pressing Play.
- **Do not use test-driven development for Unreal Engine work unless the user explicitly asks for it.** Implement from the real source and in-engine asset state, then use focused automation only as a final regression check where it adds value.

**The loop:** Plan → Execute → Review → Fix-findings → Review → ... → Done.
Continue the implement/review loop until no `CRITICAL` or `WARNING` findings remain.

### Role of the main chat
The main Codex chat is the **senior-dev watchdog running on the model the user chose for the chat**.

- Plans the work and waits for approval on anything non-trivial
- Reviews the work against the stated goal before declaring it done
- Chooses directly between local work, available tools, and optional custom agents based on the task

### Engine state is a source of truth — check it, don't guess

When planning, investigating a bug, or forming a picture of current in-engine state (Blueprint wiring, DataAsset/DataTable values, AnimBP state, level actors, EQS/BT authoring, or widget layout), check the running editor when it is more reliable than source or asset diffs. Use the appropriate available tooling for inspection or editing, and keep the implementation/review loop intact.

### The loop in detail

1. **Plan:** define the goal, scope, approach, and verification.
2. **Execute:** make the focused change using the approach that best fits the task.
3. **Review:** check safety, performance, architecture, edge cases, and goal coverage.
4. **Fix findings:** address every `CRITICAL` or `WARNING`, then return to the review step.
5. **Build and verify when the change requires it.** Build only after the review round is clean. For any C++ change that needs testing:
   - **(a) Standing permission to close and reopen ProjectExtract.** The user has granted Codex free rein to close, build, and reopen this project's editor without asking again. Before touching the process, use the active engine guard when available, then close ONLY this project's editor, scoped by the `.uproject` in the process command line. **NEVER `Stop-Process -Name UnrealEditor`** — the user keeps other projects' editors open at the same time, and blanket-kill terminates all of them. Use:
     ```
     Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*Extraction.uproject*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
     ```
   - **(b) Build with the editor closed and confirm `Result: Succeeded` in the log** — exit 0 from the `Build.bat` wrapper is NOT proof (a Live Coding lock exits fast without compiling). If it fails, diagnose and fix the build issue, then re-build. Standard command:
     ```
     "/c/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ExtractionEditor Win64 Development -Project="C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject" -WaitMutex
     ```
     Run in the background when available (UE builds take 30-300s), redirect to a temp file, grep for `Result:` / `error`.
   - **(c) On green, re-boot this project's editor** via the `boot-engine` skill (+ VibeUE proxy); wait for `vibeue_status` and a trivial `mcp__unreal-editor__execute_script` NeoStack check. Reviewers read; they don't compile.
6. **"Ready" / "done" = the user can literally press Play.** Build-green is a mid-step, not a stopping point — never report ready while the editor is closed. Only report ready once there are no outstanding `CRITICAL`/`WARNING` findings, the build hit `Result: Succeeded`, AND this project's editor is re-booted and sitting where pressing Play works.

## Skill Invocation (be aggressive — invoke whenever the topic is mentioned)

Local skills under `.agents/skills/` are loaded on demand and cheap. **Invoke a skill the moment its subsystem is mentioned**, not just when writing code.

| Topic | Skill |
|---|---|
| GameplayAbilities / GAS / AttributeSet / GameplayEffect | `ue-gameplay-abilities` |
| CharacterMovementComponent / custom movement / vault / climb | `ue-character-movement` |
| DataAsset / DataTable / async loading / soft refs | `ue-data-assets-tables` |
| AnimInstance / montage / state machine / IK / blendspace | `ue-animation-system` |
| AI / Behavior Tree / EQS / Blackboard / perception | `ue-ai-navigation`, `ue5-ai-systems` |
| State Tree | `ue-state-trees` |
| Enhanced Input / InputAction / IMC | `ue-input-system` |
| Build.cs / module / plugin setup / linker errors | `ue-module-build-system` |
| UMG / Slate / widget / HUD | `ue-ui-umg-slate` |
| Editor tooling / Blutility / detail customisation | `ue-editor-tools` |
| Game Features / modular gameplay / Lyra-style | `ue-game-features` |
| Physics / collision / traces / overlaps | `ue-physics-collision` |
| Automation tests / logging / Insights / profiling | `ue-testing-debugging` |
| Actor lifecycle / component composition | `ue-actor-component-architecture` |
| UObject macros / UPROPERTY / containers / delegates | `ue-cpp-foundations` |
| New UE5 class from scratch | `ue5-class-scaffold` |
| Any UE5 C++ review request | `ue5-code-review` |
| Crash, freeze, compile error | `ue5-crash-debug` |
| Boot / reopen the ProjectExtract editor and bring VibeUE + NeoStack online | `boot-engine` |
| In-engine editor work via MCP (BP/material/UMG/Niagara/DataAsset/level/asset import) and in-engine AI systems (Behavior Trees / Blackboards / EQS → NeoStack) | Use the appropriate in-engine tooling and matching skill; an agent is optional |
| Drive the editor from the CLI yourself — tooling map, screenshots, the code→build→boot→wire→close loop, gotchas | **read `agent_docs/UnrealWorkflow.md`** (VibeUE editor :8088 / Codex proxy :8089 + NeoStack :9315) |
| End of session — summarising for handoff | `session-handoff` |
If unsure whether to invoke a skill or agent, choose based on the task's actual needs and keep the workflow proportionate.

---

## Code orientation — query the graph before exploring

A persistent `code-review-graph` knowledge graph of the repo is built and kept current by hooks. To understand changed-code impact or review context, query it before broad manual exploration when the tools are exposed:
- `mcp__code_review_graph__detect_changes_tool` — risk-scored change detection and impact guidance
- `mcp__code_review_graph__get_review_context_tool` — focused context for changed files
- `mcp__code_review_graph__list_repos_tool` — confirm repo registration if graph tools do not auto-detect the repo

If richer graph tools are exposed in a future session, use them. Otherwise fall back to `rg` or a custom agent only when the graph cannot answer.

---

## Project-Specific Notes

- **In-engine asset/BP/montage/Blueprint/material/UMG work** is driven from the CLI via **VibeUE** (primary, editor MCP :8088; Codex proxy :8089 — `execute_python_code`, `manage_skills`, `manage_asset`). **Read `agent_docs/UnrealWorkflow.md` before any in-engine work** — it is the tooling map, the mandatory VibeUE skill-loading rule (load the matching skill before the first edit in a domain), and the hard-won gotchas (PIE locks BP edits, runtime spawn/world-lifecycle calls crash the editor, FBX import must defer to a tick callback, sampler-type↔compression mismatch renders grey, etc.). Use the appropriate in-engine tooling for the task; use `inengine-checklist` when giving manual editor steps. C++ stays code-only — no `/Game/` paths in C++, and never write or compile C++ through the editor MCP. **NeoStack (:9315) is the fallback when VibeUE lacks the capability — especially in-engine AI systems: Behavior Trees, Blackboards, EQS, and AI Blueprint wiring (VibeUE has no BT/Blackboard skill).** Drive it via the `neostack-loop` / `neostack-blueprint` skills.
- **Companion manual QA scenarios** live in `agent_docs/companion_testing.md` — refer there before claiming an AI feature works. When automation tests land, mirror the scenarios.
- **Roadmap / feature checklist:** `agent_docs/project_roadmap.md` is the live build checklist — every remaining feature broken down by system with status (`[ ]` to-do / `[~]` in progress / `[x]` done) and soft week tags. Consult it for current state, and **tick items off as you complete them**. It also auto-reconciles from each commit via a git `post-commit` hook (`.githooks/roadmap-update.sh`); if hooks ever get reset, re-run `sh .githooks/install.sh`.
- **Branching:** feature-by-feature on user-managed branches. User handles PRs to `main`. No CI/CD assumptions. Don't auto-merge or push without explicit instruction.
- **Commits:** never add `Co-Authored-By: Codex` trailer. Never `git push` without explicit instruction. **Before any commit, reconcile `agent_docs/project_roadmap.md`** against the staged diff (flip items to `[~]`/`[x]`) so the checklist ships inside the work commit when the task changes roadmap items.

## Shortcuts (project-specific — `QPLAN`/`QCHECK`/`QPERF` in global)

- **QSAFETY / QEDGE / QFULL**: Apply a consolidated safety + performance + edge-case review to the current change, using the task goal to check feature coverage.

## Session Start

At session start, on a fresh task, do this in order before responding:
1. Check `agent_docs/` for any topic-relevant docs — **`UnrealWorkflow.md` before any in-engine/editor work** (VibeUE + NeoStack tooling map + gotchas); `companion_testing.md` for companion QA; **`project_roadmap.md` for the live feature checklist**
2. Confirm the active branch matches the feature being worked on (`git status`)
3. If the task touches AI / movement / animation / UI, **invoke the matching skill from the table above before any tool calls**
