# Input & Movement Architecture — Overview

> Orientation doc, not a spec. It maps *how player input and movement are actually wired right now* and — more importantly — the non-obvious gotchas that cost a long debug session (2026-06-15, the "takedown does nothing" hunt). Read this before touching any input mapping, adding a new key, or wondering why a C++ input binding "isn't firing."

## TL;DR — the one thing to remember

The player pawn runs on **two stacked Enhanced Input contexts**, and the **marketplace kit's context wins almost every key**:

- **`IMC_Player`** (`/Game/ProceduralFPSKIT/Input/`, ~48 mappings) — the ProceduralFPSKIT's full input set. **Applied and takes precedence** on contested keys.
- **`IMC_Default`** (`/Game/Core/Input/`, ~23 mappings) — the *project's* context (lean, project-interact on E, anything we add). Also applied, but **loses contested keys to the kit**.

Both are added at **priority 0**, so for any key both contexts map, the kit's mapping fires and the project's is silently shadowed. A C++ `BindAction` can be 100% correct and still never fire because the key it's bound to is consumed by the kit's context first. **If a new project key "does nothing," first check whether the kit already owns that key.**

The kit owns essentially the entire left-hand cluster (WASDQERFCVGBZX…). Free keys for new project actions live on the right side (T, Y, H[debug], I, J, K, L, M, N, O, U) and most mouse/number keys. New project-only keys should be mapped into **the winning context (`IMC_Player`)**, or onto a key the kit doesn't already use.

## Where contexts get applied (the apply sites)

There is no single owner of input setup — it's spread across three places:

1. **`AExtractionPlayerController`** (C++) — has a `DefaultMappingContexts` array (currently `[IMC_Default]`), applied at priority 0.
2. **`AExtractionPlayer::PawnClientRestart`** (C++) — applies its `DefaultMappingContext` (= `IMC_Default`) at priority 0. *This was added recently; before it, the pawn added no IMC in C++.*
3. **`BP_FPCharacter` (kit BP, `BeginSetup` graph)** — applies the kit's `IMC_Player`. This is why the kit context is live even though no C++ references it.

Because two of these add `IMC_Default` and the kit adds `IMC_Player`, all three sit at priority 0 and the kit wins ties. Raising the project context's priority would make it win — but it would then override the kit's locomotion too, so don't do that casually.

## UE 5.7 mapping-data gotcha (this burned an hour)

In 5.7 an `InputMappingContext` stores key mappings in **`default_key_mappings`** (a struct holding the real array). The old flat **`mappings`** array is **deprecated and can be stale/out of sync** — editing or reading it lies to you. On `IMC_Default` the deprecated array showed `Q/E/F → lean/lean/takedown` while the *real* `default_key_mappings` had a completely different set (E→Interact, Z→LeanRight, no F at all).

- **Always read/write `default_key_mappings`**, never the deprecated `mappings`.
- From Python use the proper API: `imc.map_key(action, key)` / `imc.unmap_key(action, key)` (build a key with `k = unreal.Key(); k.set_editor_property('key_name','T')`). These write the canonical structure.
- When auditing "what maps key X," iterate every IMC's `default_key_mappings`, not `mappings`.

## Who handles what input (C++ vs kit BP)

Input handling is split, and the split is not obvious from C++ alone:

- **Core locomotion (move / look / fire / sprint / crouch / vault / interact / melee)** → driven by the **kit BP_FPCharacter's own Enhanced Input event nodes** reading kit IAs, *not* the C++ handlers. 
- **Lean (Q / Z)** → kit BP event nodes that drive `AC_ProceduralAnimation` (see `[[reference_lean_wiring]]`).
- **Takedown** → **C++** `AExtractionPlayer::TakedownInput`, bound to `IA_Takedown` (`ETriggerEvent::Started`). Now mapped to **T** in `IMC_Player`.
- The C++ `AExtractionPlayer` *also* declares `MoveAction`, `LookAction`, `FireAction`, etc. and binds them — but **`MoveAction` is intentionally null** (the log line `"MoveAction is null — assign in BP child class"` is EXPECTED, not a bug). The kit drives movement; those C++ locomotion bindings are largely vestigial. Don't "fix" the null warning.

## Movement specifics

- Pawn chain: **`BP_FPCharacter` → `AExtractionPlayer` → `ACharacter`**. (`AExtractionCharacter` is a *separate, sibling* `ACharacter` subclass — a legacy/parallel player class. Don't confuse the two; the live pawn is `AExtractionPlayer`.)
- Base locomotion is standard `CharacterMovementComponent`, driven by the kit's input/anim graph.
- Traversal (vault / climb / mantle) lives in **`UTraversalComponent`** on the pawn. Landing/slip-off fixes and the FP-camera-rides-head-bone behaviour have their own hard-won notes — see `[[pitfall_companion_traversal_landing]]` and `[[pitfall_fp_camera_head_bone]]`.
- First-person arms/weapon attachment has a separate scale-mismatch pitfall — `[[pitfall_fp_weapon_mesh]]`.

## Input Action gotchas

- **Triggers matter.** An `InputAction` with **no triggers** does not deliver a clean single `Started` event the way a `Pressed`-triggered one does. The kit's actions use `InputTriggerPressed`; `IA_Takedown` had none, so we added `InputTriggerPressed` to it. When making a "press once" action, give the IA a `Pressed` trigger.
- **Two `IA_Interact` assets exist** and are easy to mix up:
  - kit's `/Game/ProceduralFPSKIT/Input/Actions/IA_Interact` → mapped to **F** in `IMC_Player` (kit interact).
  - project's `/Game/Core/Input/Actions/IA_Interact` → the pawn's C++ `InteractAction`, mapped to **E** in `IMC_Default`.
- `consume_input = true` on an action means once it fires for the winning context, lower/other contexts don't see that key.

## VibeUE proxy token gotcha (tooling, adjacent)

If `vibeue_status` says "setup incomplete" even though the in-editor API key is set, the `vibeue-proxy.json` `bearer_token` is blank — it's a *separate* value that must match the editor key. See `[[pitfall_vibeue_proxy_token]]`.

## How to debug "key does nothing" (recipe)

1. **Is the pawn the one you think?** In PIE: `unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()` → `GameplayStatics.get_player_controller(world,0).get_controlled_pawn()` → check class.
2. **Does the key collide with the kit?** Scan **every** IMC's `default_key_mappings` for that key. If `IMC_Player` maps it, the kit wins — pick a free key or map into `IMC_Player`.
3. **Spare-key test.** Map the action to a guaranteed-free key and test. If the free key works but the original doesn't → contention. If neither works → the context/binding itself isn't live (or PIE wasn't restarted — **IMC edits only apply on a fresh Play**).
4. **Layer isolation.** Drop a temporary BP `Enhanced Input Action` event node with Print Strings on its `Started`/`Triggered` pins. If the BP node fires but the C++ handler doesn't → C++ binding issue. If neither fires → the action isn't triggering (key not delivered / consumed / not mapped in the applied context).
5. PIE must be **fully stopped and restarted** for any IMC/IA/BP change to take effect — editing while playing tests the stale session.

## Quick reference (current, abridged)

| Key | Action | Context | Handler |
|---|---|---|---|
| WASD / mouse / LMB / RMB / Shift / C | move / look / fire / ADS / sprint / crouch | IMC_Player (kit) | kit BP event nodes |
| Q / Z | lean L / R | IMC_Player + IMC_Default | kit BP → AC_ProceduralAnimation |
| F | kit Interact | IMC_Player (kit) | kit BP |
| E | project Interact | IMC_Default | C++ `InteractStart` |
| V | kit Melee | IMC_Player (kit) | kit BP |
| **T** | **Takedown** | **IMC_Player** | **C++ `AExtractionPlayer::TakedownInput`** |
| H | debug: apply 25 dmg | (C++ `BindKey`, legacy) | C++ `DebugApplyDamage` |

*Keys/handlers drift — re-scan `default_key_mappings` before trusting this table.*
