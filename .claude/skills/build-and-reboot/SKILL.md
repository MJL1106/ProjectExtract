---
name: build-and-reboot
description: The close-editor → build → verify → reboot-editor loop for ProjectExtract. Invoke whenever a C++ change needs compiling or testing, whenever you are about to close the Unreal Editor, whenever a build fails, or whenever you are about to tell the user something is "ready" / "done" / "you can test it now". Also invoke on "build it", "compile", "does it build", "rebuild", "verify the build", "Live Coding", "Result: Succeeded", "close the editor", "boot it back up". Covers the strict review-before-build ordering, the project-scoped editor kill (never blanket-kill UnrealEditor), the Build.bat command, why exit 0 is not proof of a build, and the definition of done.
---

# Build & Reboot Loop (ProjectExtract)

You own this loop end to end. Never make the user the build or editor operator, and never hand them a "now go build it" instruction.

## Ordering — non-negotiable

Build only AFTER the review round is clean. Never start a build before the reviewer, and never run one in parallel with it. The reviewer reads source, not binaries: a finding means edits and a second build, so a pre-review build is wasted compile time and a wasted editor close.

Strict sequence: review → fix findings → re-review if the fix was non-trivial → THEN close, build, reboot once.

## (a) Close the editor

**ALWAYS confirm first, every time, no exceptions.** Before touching the process, call `AskUserQuestion`:

- Question: "Close the Unreal Editor to build?"
- Options: **"Yes, close now"** / **"No, hold off — another chat is still working"**

Never force-close on the assumption that a prior approval still applies. Ask fresh each time a close is about to happen. If the user picks hold off, wait and re-ask later rather than proceeding.

Check for unsaved editor work before closing.

⚠️ **NEVER `Stop-Process -Name UnrealEditor`.** The user keeps other projects' editors open at the same time, and a blanket kill terminates all of them, losing unsaved work. Scope the kill by the `.uproject` in the process command line:

```
Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object { $_.CommandLine -like '*Extraction.uproject*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

## (b) Build with the editor closed

```
"/c/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" ExtractionEditor Win64 Development -Project="C:/Users/matth/Documents/Github/ProjectExtract/Extraction/Extraction.uproject" -WaitMutex
```

Run with `run_in_background: true` (UE builds take 30-300s), redirect to a temp file in the scratchpad, then grep for `Result:` and `error`.

**Confirm `Result: Succeeded` in the log.** Exit 0 from the `Build.bat` wrapper is NOT proof: a Live Coding lock exits fast without compiling anything.

On failure, dispatch by error class:
- Linker / IWYU / `Build.cs` / unresolved externals / missing API macro → `ue5-build-specialist`
- Semantic / template / engine-API misuse → `ue5-cpp-implementer`

Then re-build.

## (c) Reboot the editor

On green, re-boot this project's editor via the `boot-engine` skill (plus the VibeUE proxy) and wait for `vibeue_status` / `unreal_status` to respond.

## Definition of done

**"Ready" / "done" = the user can literally press Play.** Build-green is a mid-step, not a stopping point. Never report ready while the editor is closed.

Surface the result and the reviewer summaries only once ALL of these hold:
- no outstanding `CRITICAL` or `WARNING` review findings
- the build hit `Result: Succeeded`
- this project's editor is re-booted and sitting where pressing Play works
