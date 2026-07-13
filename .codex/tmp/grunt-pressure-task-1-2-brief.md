# Grunt pressure implementation brief

Implement Tasks 1 and 2 from `docs/superpowers/plans/2026-07-11-grunt-pressure.md` as one cohesive TDD change because they share the posture policy and files.

Read first:

- `docs/superpowers/specs/2026-07-11-grunt-pressure-design.md`
- `docs/superpowers/plans/2026-07-11-grunt-pressure.md`
- `AGENTS.md`

Required behavior:

- Preserve shared squad sightings and focus targeting exactly as they are.
- `BTTask_EnemyFlank` must fail before cooldown/role mutation when the squad has no living officer.
- Preserve existing officer-led behavior and Rusher behavior.
- Add DataAsset-driven Press cadence with legacy-preserving zero defaults for all existing archetypes until the grunt asset is wired.
- A configured Press episode gets one committed advance at most, expires after its configured duration, and schedules randomized recovery after completion or interruption.
- Add a DataAsset-driven minimum threat distance for posture advance candidates; exclude too-close candidates before scoring so another valid medium-range candidate can win.
- Add focused Unreal automation tests first and report the observed RED result before production edits.
- Do not modify assets, build files, roadmap files, plan/spec files, or any unrelated dirty files.
- Do not commit; the main chat owns integration and review.

Files owned:

- `Extraction/Source/Extraction/Public/Enemy/EnemyPostureComponent.h`
- `Extraction/Source/Extraction/Private/Enemy/EnemyPostureComponent.cpp`
- `Extraction/Source/Extraction/Public/Enemy/EnemyArchetypeData.h`
- `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_EnemyFlank.cpp`
- `Extraction/Source/Extraction/Private/AI/Tasks/BTTask_EnemyCombatFire.cpp`
- `Extraction/Source/Extraction/Private/Tests/EnemyPosturePolicy.spec.cpp`

Write the implementation report to `.codex/tmp/grunt-pressure-task-1-2-report.md`. Include changed files, RED evidence, tests run, results, self-review, and concerns. Return only DONE / DONE_WITH_CONCERNS / NEEDS_CONTEXT / BLOCKED plus a one-line summary.
