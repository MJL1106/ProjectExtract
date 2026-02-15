# Project Status (Last Updated: Session 2)

## Project Overview
- **Name:** ProjectExtract — First-person extraction shooter
- **Engine:** Unreal Engine 5.7
- **Module:** `Extraction` (single module)
- **Purpose:** MSc Game Programming degree project

## Current State: Core Systems In Place
The project has been stripped from the UE5 First Person template. All core base classes are implemented in C++ with replication support. Movement, sprint, crouch, and jump are functional. A scalable C++ animation system has been built but the Animation Blueprint has **not yet been created** in-editor. Input Actions for future systems (slide, vault, interact) are bound with stub handlers.

## What Works
- Project compiles cleanly
- Controllable first-person character with Enhanced Input (9 input actions bound)
- Replication enabled (bReplicates, GetLifetimeReplicatedProps)
- Movement: WASD (600 cm/s walk), sprint (900 cm/s, hold shift, forward-only, cancel on stop/crouch/airborne), crouch (built-in UE5 crouch)
- Jump: JumpZVelocity 500, AirControl 0.2
- Mouse look with pitch clamp (-70, +80)
- GameMode sets default pawn + controller in C++
- GameInstance class created and configured in DefaultEngine.ini
- C++ animation foundation: AnimInstance, AnimDataAsset, weapon type enums
- AnimInstance reads bIsSprinting from character's replicated state
- Subfolder source structure (Core, Character, Animation, Game)

## What's Next (Not Started)
- Create Animation Blueprint (ABP) in editor, parent to UExtractionAnimInstance
- Create DataAsset instances (Unarmed, Pistol, Rifle) and assign animation references
- Set up blendspaces for 8-dir locomotion per weapon type
- Wire up upper/lower body layer blend in ABP (Layered Blend per Bone on spine_03)
- Create Input Action assets in editor (IA_Move, IA_Look, IA_Jump, IA_Sprint, IA_Crouch, IA_Slide, IA_Vault, IA_Interact) and assign to character BP
- Create Input Mapping Context (IMC_Default) and assign key bindings
- Implement slide system
- Implement vault system
- Implement interact system
- Build weapon system (C++)
- Build AI system
- Build health/damage system
- Build game mode / level flow

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
| `ExtractionAnimInstance.cpp` | AnimInstance impl — caches character/movement, zero-alloc update, reads bIsSprinting from character, montage helpers |

### Source/Extraction/Public/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.h` | FP character — replication, 9 input actions, sprint system, movement config, crouch, stub handlers for slide/vault/interact |
| `ExtractionCameraManager.h` | Camera manager with pitch limits (-70, 80) |

### Source/Extraction/Private/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.cpp` | Character impl — constructor (movement tuning, replication), input binding, sprint logic (UpdateSprint, ApplySprintSpeed), crouch, stubs |
| `ExtractionCameraManager.cpp` | Sets pitch limits in constructor |

### Source/Extraction/Public/Game/
| File | Purpose |
|------|---------|
| `ExtractionGameMode.h` | GameMode — sets default pawn and player controller classes in C++ |
| `ExtractionPlayerController.h` | Player controller — input mapping contexts, mobile controls support |
| `ExtractionGameInstance.h` | **NEW** — Persistent GameInstance for loadouts, progression, session data |

### Source/Extraction/Private/Game/
| File | Purpose |
|------|---------|
| `ExtractionGameMode.cpp` | Constructor sets DefaultPawnClass and PlayerControllerClass |
| `ExtractionPlayerController.cpp` | Input context setup, mobile controls, BeginPlay logic |
| `ExtractionGameInstance.cpp` | **NEW** — Empty constructor (ready for future systems) |

### Config
| File | Purpose |
|------|---------|
| `Extraction.Build.cs` | Module deps: Core, CoreUObject, Engine, InputCore, EnhancedInput, AnimGraphRuntime, UMG, Slate. Include paths for all subfolders. |
| `DefaultEngine.ini` | GameInstanceClass = ExtractionGameInstance, GameMode = BP_FirstPersonGameMode (Blueprint), renderer/platform settings |

## Key Design Decisions
1. **Enums over GameplayTags** for weapon types — known finite set, compile-time safety, simpler debugging
2. **Data-driven animation** — one UExtractionAnimDataAsset per weapon type, no code changes to add new weapons
3. **TMap<EWeaponType, DataAsset>** in AnimInstance — weapon switching is a map lookup
4. **Composition over inheritance** — flat class hierarchy, no deep chains
5. **Zero-allocation NativeUpdateAnimation** — reads cached refs only, no TArray copies or string ops
6. **Subfolder organisation** — Core, Character, Animation, Game — keeps related files grouped
7. **Replication-ready from day one** — bReplicates, DOREPLIFETIME on bIsSprinting, future multiplayer support
8. **Sprint as Tick evaluation** — bWantsToSprint (input) + conditions check each frame, only applies speed change on state transition
9. **Config-driven movement values** — WalkSpeed/SprintSpeed as EditDefaultsOnly UPROPERTY, not hardcoded

## Build Notes
- MSVC toolchain 14.44.35207 required (14.40-14.43 banned by UE 5.7)
- Toolchain lives in VS 2022 directory (UBT only searches there)
- If Live Coding mutex blocks builds, delete `Intermediate/Build/Win64/x64/ExtractionEditor/Development/Makefile.bin`

## Session Log
- **Session 1:** Fixed MSVC toolchain, stripped template, restructured source, built animation C++ foundation, created agent_docs
- **Session 2:** GameMode sets C++ defaults, created GameInstance, added replication to character, added 5 new input actions (Sprint/Crouch/Slide/Vault/Interact) with bindings, configured movement (600/2048/2048), jump (500/0.2), implemented full sprint system with replicated state, crouch using built-in UE5 system, stub handlers for slide/vault/interact, updated DefaultEngine.ini
