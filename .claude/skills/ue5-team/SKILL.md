---
name: ue5-team
description: Auto-decision engine for spawning UE5 agent teams in ProjectExtract. Evaluates the current task against heuristics and creates the optimal team composition using project-specific agent roles. Use proactively at the start of every non-trivial task — not only when the user mentions "team" or "parallel".
compatibility: Claude Code. Requires CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS=1 in settings.
metadata:
  author: Matthew
  version: 2.0.0
  category: ue5-development
  project: ProjectExtract
---

# UE5 Agent Team Orchestration (ProjectExtract)

## Purpose
Evaluate whether the current task warrants spawning an agent team, and if so, create the optimal team composition using ProjectExtract's custom agents in `.claude/agents/`.

This skill should be invoked **at the start of every non-trivial task**, before planning. It's a meta-skill: it decides whether the work goes solo (single `ue5-cpp-implementer`) or fans out to a team.

## Auto-Decision Heuristics

Run through these checks in order — first match wins:

### Spawn a Team When:

1. **Feature Implementation Team** — Task creates or substantially modifies a system spanning 3+ ProjectExtract subsystem directories.
   - ProjectExtract subsystems: `Character`, `Animation`, `Companion`, `Enemy`, `Weapon`, `AI`, `AI/Tasks`, `AI/BTS`, `AI/EQS`, `Components`, `UI`, `Game`, `Data`, `Core`
   - Examples: "add a new enemy archetype with AI + perception + animation", "implement squad coordination across 2 companions", "build the loadout system spanning weapon + UI + save"

2. **QA Review Team** — Task is branch / PR review AND `git diff --stat` shows 10+ changed files.
   - Examples: "review this branch before merge", "QFULL the branch", "review PR #N"

3. **Performance Audit Team** — Task explicitly mentions performance, frame time, optimisation, or profiling across the codebase (not a single file).
   - Examples: "audit performance before milestone", "find all Tick bottlenecks", "QPERF the whole project"

4. **Bug Investigation Team** — Bug report mentions symptoms spanning 2+ systems (e.g., "companion stops firing after revive", "client desync on weapon reload mid-vault").
   - Examples: "weapon damage not registering against an enemy", "companion AI freezes when player dies during traversal"

5. **Architecture Planning Team** — Task is designing a new system that needs UE5 API research.
   - Examples: "design the squad command system", "architect the extraction-zone capture mechanic", "design persistent loadouts"

### Do NOT spawn a team when:
- Task touches fewer than 3 files
- Task is a single bug fix in one system
- Task is scaffolding (use `ue5-class-scaffold` skill instead)
- Task is a simple code review of 1–5 files (single `ue5-reviewer` dispatch is enough)
- Task is a question or explanation request

## Team Compositions (use ProjectExtract's custom agents)

### Feature Implementation Team (3–4 agents)

```
Lead (you): Coordinator. Owns AExtractionPlayerController / AExtractionGameMode changes
            and cross-system integration. Writes the plan, integrates parts.
Implementer A: subagent_type=ue5-cpp-implementer. Owns primary system files (backend / logic).
Implementer B (if UI work):  subagent_type=ue5-cpp-implementer. Owns UI widgets and HUD.
Reviewer: subagent_type=ue5-reviewer. Plan mode. Reviews work as it progresses (safety + performance + edge-case in one pass).
```

Use 3 agents (no Implementer B) if the feature has minimal UI. Use 4 when there's substantial widget work.

### QA Review Team (2 agents — reviewer in plan mode)

```
Lead (you): Collates findings, prioritises fixes, re-dispatches ue5-cpp-implementer to apply.
Reviewer: subagent_type=ue5-reviewer. Plan mode. Safety, performance, and edge-case in a single consolidated pass.
```

Add a 2nd/3rd agent (`ue5-ui-specialist` or `ue5-build-specialist`) when the diff includes substantial widget or build-config changes.

### Performance Audit Team (2 agents)

```
Lead (you): Identifies hot paths via grep for Tick/Timer patterns, coordinates review.
Auditor: subagent_type=ue5-reviewer. Plan mode. Systematic audit (performance dimension).
```

### Bug Investigation Team (2–3 agents — parallel hypotheses)

```
Lead (you): Primary investigator. Synthesises findings, applies the fix.
Hypothesis A: subagent_type=ue5-cpp-implementer. Plan mode. Investigates one root-cause hypothesis.
Hypothesis B: subagent_type=ue5-cpp-implementer. Plan mode. Investigates a competing hypothesis.
```

For multi-hypothesis bugs, use the `agent-teams:team-debug` preset which is purpose-built for this. Prefer parallel hypotheses over a single linear investigation when 2+ plausible root causes exist.

### Architecture Planning Team (2 agents)

```
Lead (you): Designs the system architecture. Produces plan document.
Researcher: subagent_type=ue5-doc-researcher. Fetches UE5 API docs, finds patterns in codebase, validates feasibility.
```

## File Ownership Rules

These prevent merge conflicts when teammates work in parallel:

1. **System boundary ownership**: Each ProjectExtract subsystem directory (`Public/<System>/` + `Private/<System>/`) is assigned to exactly one teammate. Never have two agents editing the same subsystem directory.

2. **PlayerController bottleneck**: `AExtractionPlayerController.h/.cpp` is the central god-object that touches every input + UI system. **Only the lead agent edits the PlayerController.** Teammates provide code snippets (new methods, new members) and the lead integrates.

3. **GameMode bottleneck**: Same rule for `AExtractionGameMode.h/.cpp` — central spawn / match logic, lead-only.

4. **Build.cs bottleneck**: Only the lead edits `Extraction.Build.cs`. Teammates request new module dependencies / include paths via message.

5. **Reviewers never edit**: All reviewer agents run in plan mode. They flag issues; the lead re-dispatches `ue5-cpp-implementer` to apply fixes.

6. **Worktree isolation**: When parallel implementation teammates would touch the same area despite ownership rules, use worktrees (`isolation: "worktree"`) so each agent works on an isolated copy and the lead merges after review.

## Task Assignment Pattern

When creating a team, the lead should:

1. Analyse the task scope and identify which ProjectExtract subsystems are involved
2. Create the team with the appropriate composition (`agent-teams:team-spawn` with the right preset, or compose custom)
3. Create tasks with clear file ownership boundaries:
   ```
   Task 1 (Implementer A): "Implement FooComponent in Public/Components/Foo* and Private/Components/Foo*"
   Task 2 (Implementer B): "Implement Foo HUD widget in Public/UI/Foo* and Private/UI/Foo*"
   Task 3 (Reviewer): "Review all changes for safety + gameplay correctness, plan-mode only"
   ```
4. The lead handles `AExtractionPlayerController` / `AExtractionGameMode` integration, `Build.cs` updates, and cross-system wiring

## Team Lifecycle

- Teams are short-lived and task-specific
- Create the team, assign tasks, let teammates work, review results, dissolve
- Do not keep teams alive for "future work" — recreate when needed
- Always clean up with `agent-teams:team-shutdown` when done

## Decision Output Format

When this skill is invoked, finish with one of:

- **TEAM:** `<composition>` — ready to dispatch via `agent-teams:team-spawn` (state which preset / custom roster)
- **SOLO:** single `ue5-cpp-implementer` is the right call — proceed with the standard plan → implement → review flow
- **NO AGENT:** trivial task, handle in main session
