# Project Status (Last Updated: Session 2)

## Project Overview
- **Name:** ProjectExtract — First-person extraction shooter
- **Engine:** Unreal Engine 5.7
- **Module:** `Extraction` (single module)
- **Purpose:** MSc Game Programming degree project

## Current State: Playable Foundation
All core base classes are implemented in C++ with replication support. Fresh Blueprints created from C++ classes (old template BPs replaced). Animation Blueprint with locomotion blendspace is working. Character moves, looks, jumps in-game. Sprint and crouch are C++ ready but need IA assets created in editor + added to IMC.

## What Works
- Project compiles and plays cleanly
- Fresh BPs: `BP_ExtractionCharacter`, `BP_ExtractionPlayerController`, `BP_ExtractionGameMode` (all from C++ parents)
- `ABP_ExtractionAnimBp` — parent class `UExtractionAnimInstance`, uses `BS_Idle_Walk_Run` blendspace driven by Speed/Direction
- Controllable first-person character with Enhanced Input (9 input actions bound in C++)
- Movement: WASD (600 cm/s walk), mouse look, pitch clamp (-70, +80)
- Jump: JumpZVelocity 500, AirControl 0.2
- Replication: bReplicates = true, bIsSprinting replicated with COND_SkipOwner
- Sprint system in C++ (locally controlled only, forward-only, cancel on stop/crouch/airborne)
- Crouch in C++ (built-in UE5 crouch, bCanCrouch = true)
- GameMode sets default pawn + controller in C++ constructor
- GameInstance class configured in DefaultEngine.ini
- C++ animation foundation: AnimInstance, AnimDataAsset, EWeaponType enum
- AnimInstance reads bIsSprinting from character's replicated state
- Subfolder source structure (Core, Character, Animation, Game)
- Level uses PlayerStart for character spawning (no manually placed character)

## What's Next
- **Editor tasks (immediate):**
  - Create IA_Sprint (bool), IA_Crouch (bool), IA_Slide (bool), IA_Vault (bool), IA_Interact (bool) Input Action assets
  - Add them to IMC_Default with key bindings (Shift, Ctrl, C, V, E)
  - Assign them to BP_ExtractionCharacter Input properties
  - Test sprint and crouch in-game
- **ABP expansion:**
  - Add jump states to ABP state machine (using bIsInAir/bIsFalling transitions)
  - Create DataAsset instances (DA_Anim_Unarmed, DA_Anim_Pistol, DA_Anim_Rifle) and assign animation refs
  - Set up per-weapon blendspaces for 8-dir locomotion
  - Add Layered Blend per Bone (spine_03) for upper body weapon layer
  - Switch to dynamic blendspace via GetActiveLocomotionBlendSpace()
- **Future systems:**
  - Implement slide system
  - Implement vault system
  - Implement interaction system
  - Build weapon system (C++)
  - Build AI system
  - Build health/damage system
  - Build game mode / level flow
  - Build UI system (HUD, menus)

## File Map

### Source/Extraction/Public/Core/
| File | Purpose |
|------|---------|
| `Extraction.h` | Module log category declaration (`LogExtraction`) |
| `ExtractionTypes.h` | Shared enums — `EWeaponType` (Unarmed, Pistol, Rifle) |

### Source/Extraction/Private/Core/
| File | Purpose |
|------|---------|
| `Extraction.cpp` | Module implementation (IMPLEMENT_PRIMARY_GAME_MODULE) |

### Source/Extraction/Public/Animation/
| File | Purpose |
|------|---------|
| `ExtractionAnimDataAsset.h` | UDataAsset holding all anim refs for one weapon type (blendspace, idles, jumps, montages) |
| `ExtractionAnimInstance.h` | Core AnimInstance — locomotion props, state flags, aim offset, montage API, weapon switching |

### Source/Extraction/Private/Animation/
| File | Purpose |
|------|---------|
| `ExtractionAnimInstance.cpp` | AnimInstance impl — caches character/movement, zero-alloc update, reads bIsSprinting from character, montage helpers, named constants |

### Source/Extraction/Public/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.h` | FP character — replication, 9 input actions (TObjectPtr), sprint system, movement config (WalkSpeed/SprintSpeed UPROPERTY), crouch, stub handlers |
| `ExtractionCameraManager.h` | Camera manager with pitch limits (-70, 80) |

### Source/Extraction/Private/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.cpp` | Character impl — constructor uses WalkSpeed member, replication (COND_SkipOwner), sprint only on IsLocallyControlled(), named constants, crouch, stubs |
| `ExtractionCameraManager.cpp` | Sets pitch limits in constructor |

### Source/Extraction/Public/Game/
| File | Purpose |
|------|---------|
| `ExtractionGameMode.h` | GameMode — sets default pawn and player controller classes in C++ |
| `ExtractionPlayerController.h` | Player controller — input mapping contexts, mobile controls support |
| `ExtractionGameInstance.h` | Persistent GameInstance for loadouts, progression, session data |

### Source/Extraction/Private/Game/
| File | Purpose |
|------|---------|
| `ExtractionGameMode.cpp` | Constructor sets DefaultPawnClass and PlayerControllerClass |
| `ExtractionPlayerController.cpp` | Input context setup, mobile controls, BeginPlay logic |
| `ExtractionGameInstance.cpp` | Empty constructor (ready for future systems) |

### Blueprint Assets (Editor)
| Asset | Purpose |
|-------|---------|
| `BP_ExtractionCharacter` | Character BP — FP arms mesh (SKM_Manny_Simple), 9 IA assigned, ABP assigned to both meshes |
| `BP_ExtractionPlayerController` | Player controller BP — IMC_Default assigned in DefaultMappingContexts |
| `BP_ExtractionGameMode` | GameMode BP — DefaultPawn = BP_ExtractionCharacter, Controller = BP_ExtractionPlayerController |
| `ABP_ExtractionAnimBp` | Animation Blueprint — parent UExtractionAnimInstance, BS_Idle_Walk_Run blendspace, Direction/Speed driven |
| `BS_Idle_Walk_Run` | Blendspace — 8-dir locomotion (from template) |
| `IMC_Default` | Input Mapping Context — key bindings for movement (from template, needs sprint/crouch/etc added) |

### Config
| File | Purpose |
|------|---------|
| `Extraction.Build.cs` | Module deps: Core, CoreUObject, Engine, InputCore, EnhancedInput, AnimGraphRuntime, UMG, Slate. Include paths for all subfolders. |
| `DefaultEngine.ini` | GameInstanceClass = ExtractionGameInstance, GameMode override in World Settings = BP_ExtractionGameMode |

## Key Design Decisions
1. **Enums over GameplayTags** for weapon types — known finite set, compile-time safety, simpler debugging
2. **Data-driven animation** — one UExtractionAnimDataAsset per weapon type, no code changes to add new weapons
3. **TMap<EWeaponType, DataAsset>** in AnimInstance — weapon switching is a map lookup
4. **Composition over inheritance** — flat class hierarchy, no deep chains
5. **Zero-allocation NativeUpdateAnimation** — reads cached refs only, no TArray copies or string ops
6. **Subfolder organisation** — Core, Character, Animation, Game — keeps related files grouped
7. **Replication-ready from day one** — bReplicates, COND_SkipOwner on bIsSprinting, future multiplayer support
8. **Sprint only on locally controlled** — avoids wasted Tick work on proxies, prevents fighting replication
9. **Config-driven movement values** — WalkSpeed/SprintSpeed as EditDefaultsOnly UPROPERTY, constructor uses member not literal
10. **Fresh BPs from C++ classes** — no inherited template component baggage

## Code Quality (from QCHECK review)
- All magic numbers extracted to named constants (ExtractionCharacterConstants, ExtractionAnimConstants)
- PlayRate zero-division guard in montage playback
- Dead LogTemplateCharacter category removed
- Redundant InitCapsuleSize call removed
- DOREPLIFETIME_CONDITION with COND_SkipOwner for bandwidth efficiency

## Build Notes
- MSVC toolchain 14.44.35207 required (14.40-14.43 banned by UE 5.7)
- Toolchain lives in VS 2022 directory (UBT only searches there)
- If Live Coding mutex blocks builds, delete `Intermediate/Build/Win64/x64/ExtractionEditor/Development/Makefile.bin`

## Session Log
- **Session 1:** Fixed MSVC toolchain, stripped template, restructured source into subfolders, built animation C++ foundation (AnimInstance, AnimDataAsset, EWeaponType), created agent_docs
- **Session 2:** GameMode sets C++ defaults, created GameInstance, added replication (COND_SkipOwner), added 5 new input actions with bindings + stubs, configured movement (600/2048/2048) and jump (500/0.2), implemented sprint system (locally controlled only), crouch via built-in UE5, QCHECK review fixed 6 issues (sprint proxy guard, WalkSpeed member usage, PlayRate div guard, dead log category, redundant capsule init, magic numbers), created fresh BPs from C++ classes, set up ABP with BS_Idle_Walk_Run blendspace, level cleaned up with PlayerStart spawning
