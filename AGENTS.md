# AGENTS.md — ProjectExtract

Multiplayer first-person shooter on Unreal Engine 5.7 with an AI companion system. Single C++ module: `Extraction`.

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

- **In-engine work = invoke `inengine-checklist` skill EVERY TIME — no exceptions.** Whenever the response would tell the user to do something in the Unreal editor (open a BP, set a property, place an actor, add a component, edit a DataAsset, create a child class), invoke `inengine-checklist` first. The skill enforces a tight numbered-list format with exact menu paths and verbatim values, and strips all code/architecture commentary. Even a single editor step goes through it. If the work is large (BP graph edits, repetitive bulk, many reference wires), use `ue5-inengine-agent` instead — do not freehand prose for editor instructions.

**The loop:** Plan → Implement → Review → Fix-review-findings → Review → ... → Done.
Continue the implement/review loop until no `CRITICAL` or `WARNING` findings remain.

### Role of the main chat
The main Codex chat is the **senior-dev watchdog running on the model the user chose for the chat**.

- Plans the work and waits for approval on anything non-trivial
- Breaks tasks into file-ownership slices
- Coordinates subagents by direct dispatch, or a team workflow when `ue5-team` recommends one
- Reviews implementer output before declaring "done"
- **Never downshifts planning, architecture, or final judgement to a weaker model**
- **Never writes substantive code itself** — that's the implementer's job. Trivial typos / renames / single-line tweaks are the only main-chat code exception.

### Codex model policy by dispatch

Codex agent TOML files define roles only; **model choice happens when the main chat dispatches the agent**.

| Role | Dispatch model | Notes |
|---|---|---|
| Plan / architecture | Current chat model | Always keep the user's chosen strong model in the main chat |
| Review / final judgement | Inherit current chat model | Use `ue5-reviewer` without a model downgrade |
| C++ implementation | `gpt-5.6-terra` | Default for `ue5-cpp-implementer` after plan approval |
| Test writing | `gpt-5.6-terra` | `ue5-qa-tester` |
| Research / docs | `gpt-5.6-terra` | Escalate to inherited model only when reasoning, not lookup, is the bottleneck |
| In-engine read-only scout | `gpt-5.6-terra` | Cheap, parallel, read-only editor reconnaissance |
| Normal in-engine editing / build fixes | `gpt-5.6-terra` | Use `ue5-inengine-agent` or `ue5-build-specialist` for bounded implementation-style work |
| Hard debugging / escalation | Inherit current chat model | Use inherited model when the prior Terra pass is stuck or the issue is architecture-heavy |

### Subagent preference

Prefer custom subagents wherever the task matches an agent description. Main chat should rarely be the one writing code — its role is orchestration and review.

- Solo C++ work → dispatch `ue5-cpp-implementer` on `gpt-5.6-terra` after plan approval. Never freelance edits from main chat for anything beyond trivial typos / renames / single-line tweaks.
- For parallelisable dispatches (reviewer + multiple plan agents, or several scouts), issue them in a single message, not sequentially.
- Pure research / "where does X live" → use `rg` / graph tools directly. Don't spawn an agent for a one-line answer.

### Engine state is a source of truth — check it, don't guess

When planning, investigating a bug, or forming a picture of "what does this actually do right now" for anything in-engine (current BP graph wiring, DataAsset/DataTable values, live AnimBP state, level actor placement, EQS/BT authoring, widget layout) — this applies beyond explicit "wire this up" requests, including planning and bug investigation:
- If the answer would be more reliable read from the running editor than inferred from `.uasset` diffs or memory, **check the engine** — dispatch `ue5-inengine-scout` (read-only, usually `gpt-5.6-terra`) rather than guessing from C++ or asking the user to go look. If the inspection turns into a bounded editor edit, dispatch `ue5-inengine-agent` on `gpt-5.6-terra`; inherit the current chat model only when that pass is stuck or the issue is architecture-heavy.
- **Scale scouts to the question, favoring speed over conservatism.** If the recon splits into independent inspection threads (e.g. "check the BT, the Blackboard, and the EQS query" or "inspect 3 unrelated widgets"), dispatch one scout per thread **in a single message** so they run concurrently — up to 5 at once. Don't default to 1 scout doing everything sequentially when 3-5 parallel scouts would answer it faster; don't over-split a single-thread question either. Judge the split by independence of the sub-questions, not by a fixed count.
- If the editor isn't running, **boot it first** — route through `ue5-inengine-agent` (it self-boots via the `boot-engine` skill) since scouts can't boot the editor themselves. Don't skip the check just because the editor is currently down, and don't ask the user to open it.
- This is read-only reconnaissance for planning purposes — it does not replace the implement/review/build loop, and it does not authorize edits during an investigation.

### The loop in detail

0. **`ue5-team` skill** — decide solo vs team (mandatory Step 0 for every non-trivial task)
1. **Implement:**
   - Solo → `ue5-cpp-implementer` on `gpt-5.6-terra`
   - Team → compose agents from `ue5-team`'s recommendation with explicit file ownership and dispatch-time models
2. **Review (single consolidated reviewer, covers safety + performance + edge-case in one pass):**
   - `ue5-reviewer` — always, every C++ change, inheriting the current chat model. MUST be briefed with: (a) the task goal in one or two sentences, (b) the list of changed files with brief description, (c) the plan file path if one exists. Without the goal the edge-case dimension is degraded (safety/performance still run).
   - `ue5-ui-specialist` — if UMG / Slate / widget code touched
   - `ue5-build-specialist` — if `Build.cs` / `Target.cs` / `.uproject` / plugin config / include paths touched
3. **Fix review findings:** any `CRITICAL` or `WARNING` → re-dispatch `ue5-cpp-implementer` with the consolidated findings. **Do NOT fix in main chat.** Loop back to step 2 if the fix is non-trivial.
4. **Own the close→build→reboot loop yourself — never make the user the build/editor operator.** **Build only AFTER the review round is clean — never start (or run in parallel with) the reviewer.** The reviewer reads source, not binaries; a finding means edits and a second build, so a pre-review build is wasted compile time and a wasted editor close. Sequence is strictly: review → fix findings → re-review if non-trivial → THEN close/build/reboot once. For any C++ change that needs testing:
   - **(a) ALWAYS confirm before closing — no exceptions, every time.** Before touching the process, ask the user: "Close the Unreal Editor to build?" with choices **"Yes, close now"** / **"No, hold off — another chat is still working"**. Never force-close autonomously on the assumption a prior approval still applies — ask fresh each time a close is about to happen. If the user picks hold off, wait and re-ask later instead of proceeding. Only on an explicit "yes, close now" do you proceed. Then close ONLY this project's editor, scoped by the `.uproject` in the process command line. **NEVER `Stop-Process -Name UnrealEditor`** — the user keeps other projects' editors open at the same time, and blanket-kill terminates all of them. Use:
     ```
     Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*Extraction.uproject*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
     ```
   - **(b) Build with the editor closed and confirm `Result: Succeeded` in the log** — exit 0 from the `Build.bat` wrapper is NOT proof (a Live Coding lock exits fast without compiling). If it fails, dispatch `ue5-build-specialist` (linker/IWYU/Build.cs) or `ue5-cpp-implementer` (semantic/template/API), then re-build. Standard command:
     ```
     "/c/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ExtractionEditor Win64 Development -Project="C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject" -WaitMutex
     ```
     Run in the background when available (UE builds take 30-300s), redirect to a temp file, grep for `Result:` / `error`.
   - **(c) On green, re-boot this project's editor** via the `boot-engine` skill (+ VibeUE proxy); wait for `vibeue_status` and a trivial `mcp__unreal-editor__execute_script` NeoStack check. Reviewers read; they don't compile.
5. **"Ready" / "done" = the user can literally press Play.** Build-green is a mid-step, not a stopping point — never report ready while the editor is closed. Only surface the result + reviewer summaries once ALL hold: no outstanding `CRITICAL`/`WARNING` findings, the build hit `Result: Succeeded`, AND this project's editor is re-booted and sitting where pressing Play works.

### Other agents (dispatch on match)

| Task | Agent |
|---|---|
| In-engine asset/BP/material/UMG/Niagara/DataAsset/level wiring, asset import, Behavior Trees/Blackboards/EQS — via MCP, no C++ | `ue5-inengine-agent` |
| Read-only in-engine recon — "what's wired", "what does X currently look like", pre-flight brief before an edit, planning/bug-investigation lookups. Cheap, parallelizable (up to 5 at once) | `ue5-inengine-scout` |
| Unresolved externals, IWYU warnings, missing API macros, Build.cs edits, linker errors | `ue5-build-specialist` |
| Writing automation tests, scaffolding a test module | `ue5-qa-tester` |
| UE5 API behaviour unclear / new engine feature / want to confirm best practice | `ue5-doc-researcher` |

### Team-spawning shortcuts (when `ue5-team` returns TEAM)

- Feature work → compose a custom team from `ue5-team`'s recommendation
- Branch/PR review → dispatch `ue5-reviewer` plus any specialists that match changed files
- Multi-hypothesis bug → run competing investigations in parallel, then arbitrate evidence in the main chat
- API research / feasibility study → parallel `ue5-doc-researcher` or `ue5-inengine-scout` agents if the questions are independent
- Always shut down any explicit team workflow when the task completes

---

## Skill Invocation (be aggressive — invoke whenever the topic is mentioned)

Local skills under `.agents/skills/` are loaded on demand and cheap. **Invoke a skill the moment its subsystem is mentioned**, not just when writing code.

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
| Boot / reopen the ProjectExtract editor and bring VibeUE + NeoStack online | `boot-engine` |
| In-engine editor work via MCP (BP/material/UMG/Niagara/DataAsset/level/asset import) and in-engine AI systems (Behavior Trees / Blackboards / EQS → NeoStack) — done autonomously | **dispatch `ue5-inengine-agent`** |
| Drive the editor from the CLI yourself — tooling map, screenshots, the code→build→boot→wire→close loop, gotchas | **read `agent_docs/UnrealWorkflow.md`** (VibeUE editor :8088 / Codex proxy :8089 + NeoStack :9315) |
| End of session — summarising for handoff | `session-handoff` |
| **Start of any non-trivial task — decide solo vs team** | **`ue5-team` (mandatory Step 0)** |

If unsure whether to invoke, **bias toward invoking the skill** (cheap — just loads reference) and **against dispatching a subagent** (costly — spawns a full session).

---

## Model rule — custom agents plus explicit dispatch model

The custom subagents in `.codex/agents/` are the right tool for any UE5 work. These TOML files do not pin a model; the main chat chooses the model at dispatch time.

- **For UE5 codebase exploration / research / implementation / review** → use the custom agents above. Never use a generic explore/general agent for substantive work.
- **For trivial file-path lookups** ("what file lives at X", "find all callers of Y") → `rg` directly in the main session, no agent needed.
- **If a custom agent doesn't fit and you must spawn a generic one**, set an explicit model. Use `gpt-5.6-terra` for ordinary implementation, research, and debugging work. Keep architecture in the main chat. Inherit the current chat model only for review, final judgement, or hard escalation after a Terra pass is stuck.

---

## Code orientation — query the graph before exploring

A persistent `code-review-graph` knowledge graph of the repo is built and kept current by hooks. To understand changed-code impact or review context, query it before broad manual exploration when the tools are exposed:
- `mcp__code_review_graph__detect_changes_tool` — risk-scored change detection and impact guidance
- `mcp__code_review_graph__get_review_context_tool` — focused context for changed files
- `mcp__code_review_graph__list_repos_tool` — confirm repo registration if graph tools do not auto-detect the repo

If richer graph tools are exposed in a future session, use them. Otherwise fall back to `rg` or a custom agent only when the graph cannot answer.

---

## Project-Specific Notes

- **In-engine asset/BP/montage/Blueprint/material/UMG work** is driven from the CLI via **VibeUE** (primary, editor MCP :8088; Codex proxy :8089 — `execute_python_code`, `manage_skills`, `manage_asset`). **Read `agent_docs/UnrealWorkflow.md` before any in-engine work** — it is the tooling map, the mandatory VibeUE skill-loading rule (load the matching skill before the first edit in a domain), and the hard-won gotchas (PIE locks BP edits, runtime spawn/world-lifecycle calls crash the editor, FBX import must defer to a tick callback, sampler-type↔compression mismatch renders grey, etc.). To get it done autonomously, **dispatch `ue5-inengine-agent`**. Alternatively hand off to a human via `inengine-checklist` in plain English. Either way, C++ stays code-only — no `/Game/` paths in C++, and never write or compile C++ through the editor MCP. **NeoStack (:9315) is the fallback when VibeUE lacks the capability — especially in-engine AI systems: Behavior Trees, Blackboards, EQS, and AI Blueprint wiring (VibeUE has no BT/Blackboard skill).** Drive it via the `neostack-loop` / `neostack-blueprint` skills.
- **Companion manual QA scenarios** live in `agent_docs/companion_testing.md` — refer there before claiming an AI feature works. When automation tests land, mirror the scenarios.
- **Roadmap / feature checklist:** `agent_docs/project_roadmap.md` is the live build checklist — every remaining feature broken down by system with status (`[ ]` to-do / `[~]` in progress / `[x]` done) and soft week tags. Consult it for current state, and **tick items off as you complete them**. It also auto-reconciles from each commit via a git `post-commit` hook (`.githooks/roadmap-update.sh`); if hooks ever get reset, re-run `sh .githooks/install.sh`.
- **Branching:** feature-by-feature on user-managed branches. User handles PRs to `main`. No CI/CD assumptions. Don't auto-merge or push without explicit instruction.
- **Commits:** never add `Co-Authored-By: Codex` trailer. Never `git push` without explicit instruction. **Before any commit, reconcile `agent_docs/project_roadmap.md`** against the staged diff (flip items to `[~]`/`[x]`) so the checklist ships inside the work commit when the task changes roadmap items.

## Shortcuts (project-specific — `QPLAN`/`QCHECK`/`QPERF` in global)

- **QSAFETY / QEDGE / QFULL**: Run `ue5-reviewer` over the current change (single consolidated pass covers safety + performance + edge-case). Brief it with the task goal for a full edge-case pass.

## Session Start

At session start, on a fresh task, do this in order before responding:
1. Check `agent_docs/` for any topic-relevant docs — **`UnrealWorkflow.md` before any in-engine/editor work** (VibeUE + NeoStack tooling map + gotchas); `companion_testing.md` for companion QA; **`project_roadmap.md` for the live feature checklist**
2. Confirm the active branch matches the feature being worked on (`git status`)
3. If the task touches AI / movement / animation / replication / UI, **invoke the matching skill from the table above before any tool calls**
4. **Invoke `ue5-team`** to decide solo vs team for the task — this is Step 0 of the workflow

## Required environment

For Codex multi-agent workflows, `.codex/config.toml` must contain:

```
[features]
multi_agent = true
```

If multi-agent tools are unavailable in a session, fall back to solo flow and tell the user the Codex multi-agent feature is not enabled.
