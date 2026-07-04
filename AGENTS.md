# AGENTS.md — ProjectExtract

Multiplayer first-person shooter on Unreal Engine 5.7 with an AI companion system. Single C++ module: `Extraction`.

## Stack
- **Engine:** Unreal Engine 5.7
- **Language:** UE5 C++
- **Module:** `Extraction` (`Extraction/Source/Extraction/`)
- **In-engine tooling:** **VibeUE** (MCP HTTP :8088, primary — Blueprints/materials/UMG/assets via `execute_python_code` + `manage_skills`) + **NeoStack** (MCP HTTP :9315 — fallback when VibeUE lacks the capability, especially in-engine AI systems: Behavior Trees, Blackboards, EQS, AI Blueprint wiring). Full tooling map + gotchas: `agent_docs/UnrealWorkflow.md`.
- **API macro:** `EXTRACTION_API`
- **Solution:** `Extraction/Extraction.sln`

## Identity — are you the in-engine agent?

If the user has told you **"you are the in-engine agent"**, OR you are running inside the Unreal Editor's AIK (Agent Integration Kit) chat window, you are **not** Codex CLI. The Workflow / Custom Subagents / `agent-teams:*` / `Bash` / build-script sections below do **not** apply to you — those tools do not exist in your environment.

**Stop here and read** `.Codex/skills/aik-in-engine-agent/SKILL.md` — it's your capability reference. Do **not** read C++ source files unless the user explicitly asks; your work is in-editor (Blueprints, materials, behaviour trees, level sequences, Niagara, data assets, asset import, level editing).

If you are Codex CLI (terminal / Desktop / VS Code), ignore this section and continue.

> Shared UE5 Hard Rules, Soft Rules, and Architectural Taste live in global `~/.Codex/AGENTS.md`. Only project-specific additions and overrides are listed below.

## Hard Rules (project-specific)
- MUST never hardcode `/Game/...` asset paths via `ConstructorHelpers::FObjectFinder` — designer assigns assets in Blueprint subclasses (this project uses an in-editor MCP agent for asset wiring; C++ stays asset-agnostic)
- MUST mark replicated UPROPERTY `Replicated` or `ReplicatedUsing=OnRep_*` AND add `DOREPLIFETIME[_CONDITION]` in `GetLifetimeReplicatedProps`
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

- **In-engine work = invoke `inengine-checklist` skill EVERY TIME — no exceptions.** Whenever the response would tell the user to do something in the Unreal editor (open a BP, set a property, place an actor, add a component, edit a DataAsset, create a child class), invoke `inengine-checklist` first. The skill enforces a tight numbered-list format with exact menu paths and verbatim values, and strips all code/architecture commentary. Even a single editor step goes through it. If the work is large (BP graph edits, repetitive bulk, many reference wires), use `inengine-prompt` instead — but never freehand prose for editor instructions.

**The loop:** Plan → Implement → Review → Fix-review-findings → Review → ... → Done.
Continue the implement/review loop until no `CRITICAL` or `WARNING` findings remain.

### Role of the main chat
The main Codex chat = **senior-dev watchdog (Opus 4.8, 1M context)**.

- Plans the work
- Breaks tasks into file-ownership slices
- Coordinates the team (team-lead role for `agent-teams:team-spawn`, or direct dispatch for solo work)
- Reviews implementer output before declaring "done"
- **Never writes substantive code itself** — that's the implementer's job. Trivial typos / renames / single-line tweaks are the only main-chat code exception.

### Model policy by role

| Role | Model | Notes |
|---|---|---|
| Plan | **Opus 4.8 (1M context)** | Always |
| Review | **Opus 4.8 (1M context)** | Always |
| Coding | **Sonnet** | Default for implementer agents |
| Bug-fixer | **Opus 4.8 (1M context)** | Debugging is reasoning-heavy |
| Architecture-planner | **Opus 4.8 (1M context)** | Cross-system design |
| Research / docs | Sonnet | Opus 4.8 (1M context) only if reasoning is the bottleneck |

Current agent model assignments live in `.Codex/agents/*.md` frontmatter.

### Subagent preference

Prefer custom subagents wherever the task matches an agent description. Main chat should rarely be the one writing code — its role is orchestration and review.

- Solo C++ work → dispatch `ue5-cpp-implementer`. Never freelance edits from main chat for anything beyond trivial typos / renames / single-line tweaks.
- For parallelisable dispatches (reviewer + multiple plan agents), issue them in a single message, not sequentially.
- Pure research / "where does X live" → use `Glob` / `Grep` directly. Don't spawn an agent for a one-line answer.

### The loop in detail

0. **`ue5-team` skill** — decide solo vs team (mandatory Step 0 for every non-trivial task)
1. **Implement:**
   - Solo → `ue5-cpp-implementer`
   - Team → `agent-teams:team-spawn` preset `feature` (parallel implementers with file ownership)
2. **Review (single consolidated reviewer, covers safety + performance + edge-case in one pass):**
   - `ue5-reviewer` — always, every C++ change. MUST be briefed with: (a) the task goal in one or two sentences, (b) the list of changed files with brief description, (c) the plan file path if one exists. Without the goal the edge-case dimension is degraded (safety/performance still run).
   - `ue5-ui-specialist` — if UMG / Slate / widget code touched
   - `ue5-build-specialist` — if `Build.cs` / `Target.cs` / `.uproject` / plugin config / include paths touched
3. **Fix review findings:** any `CRITICAL` or `WARNING` → re-dispatch `ue5-cpp-implementer` with the consolidated findings. **Do NOT fix in main chat.** Loop back to step 2 if the fix is non-trivial.
4. **Verify the build before reporting back.** Run a UE5 build and confirm exit 0 before telling the user anything is "done", "ready to test", or compiled. Reviewers read; they don't compile. If the build fails, dispatch `ue5-build-specialist` (linker/IWYU/Build.cs) or `ue5-cpp-implementer` (semantic/template/API errors) with the error log, then re-build. Standard command:
   ```
   "/c/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ExtractionEditor Win64 Development -Project="C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject" -WaitMutex
   ```
   Run with `run_in_background: true` (UE builds take 30-300s), redirect output to a temp file, grep for `error` after completion.
5. **Only then surface result + reviewer summaries to the user.** Never declare a task complete while there are outstanding `CRITICAL` or `WARNING` findings, or while the build is failing.

### Other agents (dispatch on match)

| Task | Agent |
|---|---|
| Unresolved externals, IWYU warnings, missing API macros, Build.cs edits, linker errors | `ue5-build-specialist` |
| Writing automation tests, scaffolding a test module | `ue5-qa-tester` |
| UE5 API behaviour unclear / new engine feature / want to confirm best practice | `ue5-doc-researcher` |

### Team-spawning shortcuts (when `ue5-team` returns TEAM)

- Feature work → `agent-teams:team-spawn` preset `feature` (or compose custom from `ue5-team`'s recommendation)
- Branch/PR review → `agent-teams:team-spawn` preset `review`
- Multi-hypothesis bug → `agent-teams:team-spawn` preset `debug`
- API research / feasibility study → `agent-teams:team-spawn` preset `research`
- Always shut down the team with `agent-teams:team-shutdown` when the task completes

---

## Skill Invocation (be aggressive — invoke whenever the topic is mentioned)

Local skills under `.Codex/skills/` are loaded on demand and cheap. **Invoke a skill the moment its subsystem is mentioned**, not just when writing code.

| Topic | Skill |
|---|---|
| Replication / RPC / net roles / authority | `ue-networking-replication`, `ue5-multiplayer-helper` |
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
| Drive the editor from the CLI — in-engine asset/BP/widget/material/UMG wiring, screenshots, the code→build→boot→wire→close loop | **read `agent_docs/UnrealWorkflow.md`** (VibeUE :8088 + NeoStack :9315) |
| End of session — summarising for handoff | `session-handoff` |
| **Start of any non-trivial task — decide solo vs team** | **`ue5-team` (mandatory Step 0)** |

If unsure whether to invoke, **bias toward invoking the skill** (cheap — just loads reference) and **against dispatching a subagent** (costly — spawns a full session).

---

## Model rule — no Haiku for substantive work

The custom subagents in `.Codex/agents/` (Opus 4.8 / Sonnet 5, both 1M context) are the right tool for any UE5 work. The default `Explore` and `general-purpose` agents fall back to Haiku, which is too weak for this codebase.

- **For UE5 codebase exploration / research / implementation / review** → use the custom agents above. Never use the generic `Explore` agent for substantive work.
- **For trivial file-path lookups** ("what file lives at X", "find all callers of Y") → `Glob` / `Grep` directly in the main session, no agent needed.
- **If a custom agent doesn't fit and you must spawn a generic one**, override the model with `model: "sonnet"` on the `Agent` tool call. Never let it default to Haiku.

---

## Project-Specific Notes

- **In-engine asset/BP/montage/Blueprint/material/UMG work** is driven from the CLI via **VibeUE** (primary, MCP :8088 — `execute_python_code`, `manage_skills`, `manage_asset`). **Read `agent_docs/UnrealWorkflow.md` before any in-engine work** — it is the tooling map, the mandatory VibeUE skill-loading rule (load the matching skill before the first edit in a domain), and the hard-won gotchas (PIE locks BP edits, runtime spawn/world-lifecycle calls crash the editor, FBX import must defer to a tick callback, sampler-type↔compression mismatch renders grey, etc.). Alternatively hand off to a human via `inengine-checklist` in plain English. Either way, C++ stays code-only — no `/Game/` paths in C++, and never write or compile C++ through the editor MCP. **NeoStack (:9315) is the fallback when VibeUE lacks the capability — especially in-engine AI systems: Behavior Trees, Blackboards, EQS, and AI Blueprint wiring.**
- **Companion manual QA scenarios** live in `agent_docs/companion_testing.md` — refer there before claiming an AI feature works. When automation tests land, mirror the scenarios.
- **Branching:** feature-by-feature on user-managed branches. User handles PRs to `main`. No CI/CD assumptions. Don't auto-merge or push without explicit instruction.
- **Commits:** never add `Co-Authored-By: Codex` trailer. Never `git push` without explicit instruction.

## Shortcuts (project-specific — `QPLAN`/`QCHECK`/`QPERF` in global)

- **QSAFETY / QEDGE / QFULL**: Run `ue5-reviewer` over the current change (single consolidated pass covers safety + performance + edge-case). Brief it with the task goal for a full edge-case pass.

## Session Start

At session start, on a fresh task, do this in order before responding:
1. Check `agent_docs/` for any topic-relevant docs — **`UnrealWorkflow.md` before any in-engine/editor work** (VibeUE + NeoStack tooling map + gotchas); `companion_testing.md` for companion QA
2. Confirm the active branch matches the feature being worked on (`git status`)
3. If the task touches AI / movement / animation / replication / UI, **invoke the matching skill from the table above before any tool calls**
4. **Invoke `ue5-team`** to decide solo vs team for the task — this is Step 0 of the workflow

## Required environment

For team workflows: `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1` must be set in your Codex settings (the `agent-teams:*` tools depend on it). If the env var is missing, fall back to solo flow and tell the user to enable it.
