# Project Status (Last Updated: Session 3)

## Project Overview
- **Name:** ProjectExtract — First-person extraction shooter
- **Engine:** Unreal Engine 5.7
- **Module:** `Extraction` (single module)
- **Purpose:** MSc Game Programming degree project

## Current State: Core Movement Complete
All locomotion systems are implemented and polished: walk, sprint, crouch (toggle with smooth camera), jump, and slide (curve-based decel, steering, ledge traversal). Animation Blueprint has full state machine with Inertialization blending. Character is playable with responsive movement feel.

## What Works
- Project compiles and plays cleanly
- Fresh BPs: `BP_ExtractionCharacter`, `BP_ExtractionPlayerController`, `BP_ExtractionGameMode`
- `ABP_ExtractionAnimBp` — full state machine: Idle/Walk/Run, Crouch, JumpStart, FallLoop, JumpEnd, Slide
- Inertialization node in AnimGraph for smooth state transitions
- Enhanced Input: 9 input actions bound (Move, Look, MouseLook, Jump, Sprint, Crouch, Slide, Vault, Interact)
- Movement speeds matched to blendspaces (WalkSpeed=300, SprintSpeed=600, CrouchSpeed=120 on BP)
- **Sprint:** Forward-only, locally controlled, replicated (COND_SkipOwner), cancels on crouch/slide/airborne
- **Crouch:** Toggle on IA_Crouch (Pressed trigger), smooth camera interp via BaseEyeHeight + FInterpTo, geometry blocking, sprint cancel
- **Jump:** JumpStart → FallLoop → JumpEnd states, bIsInAir/bIsFalling transitions
- **Slide:** Curve-based deceleration (SlidePeakSpeed → SlideEndSpeed over SlideDuration with power curve exponent), steerable mid-slide (SlideSteerRate), slides off ledges (bCanWalkOffLedgesWhenCrouching toggled), holds animation in air, sprint resumes after slide if key held, smooth camera snap on exit
- BeginPlay re-applies BP movement speeds (fixes constructor using C++ defaults instead of BP overrides)
- Replication: bReplicates, bIsSprinting + bIsSliding with COND_SkipOwner, OnRep functions
- Blendspaces: idle/walk/run (0-480 Speed axis), crouch (0-120 Speed axis)
- AnimInstance reads all state from character: Speed, Direction, bIsSprinting, bIsSliding, bIsCrouching, bIsInAir, bIsFalling, AimPitch, AimYaw
- Tooltips on non-obvious slide config properties (exponent, steer rate)

## What's Next
- **ABP expansion:**
  - Create DataAsset instances (DA_Anim_Unarmed, DA_Anim_Rifle) and assign animation refs
  - Set up per-weapon blendspaces for 8-dir locomotion
  - Add Layered Blend per Bone (spine_03) for upper body weapon layer
  - Switch to dynamic blendspace via GetActiveLocomotionBlendSpace()
- **Future systems:**
  - Implement vault system (stub exists)
  - Implement interaction system (stub exists)
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
| `ExtractionAnimInstance.h` | Core AnimInstance — locomotion props, state flags (bIsSliding, bIsSprinting, bIsCrouching, etc.), aim offset, montage API, weapon switching |

### Source/Extraction/Private/Animation/
| File | Purpose |
|------|---------|
| `ExtractionAnimInstance.cpp` | AnimInstance impl — caches character/movement, zero-alloc update, reads bIsSprinting + bIsSliding from character, montage helpers |

### Source/Extraction/Public/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.h` | FP character — replication, 9 input actions, sprint, crouch (toggle + camera interp), slide (curve decel, steering, ledge traversal), movement config with tooltips |
| `ExtractionCameraManager.h` | Camera manager with pitch limits (-70, 80) |

### Source/Extraction/Private/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.cpp` | Character impl — BeginPlay re-applies BP speeds, sprint (locally controlled, forward-only), crouch (toggle + smooth camera), slide (EnterSlide/UpdateSlide/EndSlide with curve decel + steering + ledge support), replication (COND_SkipOwner) |
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
| `BP_ExtractionCharacter` | Character BP — FP arms mesh, 9 IA assigned, ABP assigned, movement speeds tuned (Walk=300, Sprint=600, Crouch=120), slide config (PeakSpeed, EndSpeed, Duration, Exponent, SteerRate) |
| `BP_ExtractionPlayerController` | Player controller BP — IMC_Default assigned |
| `BP_ExtractionGameMode` | GameMode BP — DefaultPawn = BP_ExtractionCharacter |
| `ABP_ExtractionAnimBp` | Animation Blueprint — 6-state machine (Idle/Walk/Run, Crouch, JumpStart, FallLoop, JumpEnd, Slide), Inertialization node, transitions guard bIsSliding for air states |
| `BS_Idle_Walk_Run` | Blendspace — Speed 0-480, Direction -180/180 (idle=0, walk=120, jog=240, sprint=480) |
| `BS_Crouch` | Blendspace — Speed 0-120, Direction -180/180 |
| `IMC_Default` | Input Mapping Context — WASD, mouse, Shift(sprint), Ctrl(crouch Pressed trigger), C(slide), Space(jump), V(vault), E(interact) |

### Config
| File | Purpose |
|------|---------|
| `Extraction.Build.cs` | Module deps: Core, CoreUObject, Engine, InputCore, EnhancedInput, AnimGraphRuntime, UMG, Slate. Include paths for all subfolders. |
| `DefaultEngine.ini` | GameInstanceClass = ExtractionGameInstance, GameMode override |

## Key Design Decisions
1. **Enums over GameplayTags** for weapon types — known finite set, compile-time safety
2. **Data-driven animation** — one UExtractionAnimDataAsset per weapon type
3. **TMap<EWeaponType, DataAsset>** in AnimInstance — weapon switching is a map lookup
4. **Composition over inheritance** — flat class hierarchy
5. **Zero-allocation NativeUpdateAnimation** — reads cached refs only
6. **Subfolder organisation** — Core, Character, Animation, Game
7. **Replication-ready** — bReplicates, COND_SkipOwner on bIsSprinting/bIsSliding
8. **Sprint only on locally controlled** — avoids wasted Tick on proxies
9. **Config-driven movement** — EditDefaultsOnly UPROPERTYs with tooltips on non-obvious values
10. **Curve-based slide decel** over friction hack — predictable, tuneable via exponent
11. **BeginPlay speed sync** — re-applies BP-overridden movement speeds after constructor
12. **bCanWalkOffLedgesWhenCrouching** toggled per-slide — crouched chars can't normally walk off ledges, slides override this

## Code Quality
- All magic numbers extracted to named constants
- PlayRate zero-division guard in montage playback
- Named constants in ExtractionCharacterConstants / ExtractionAnimConstants namespaces
- DOREPLIFETIME_CONDITION with COND_SkipOwner for bandwidth efficiency
- Tooltips on non-obvious designer-facing UPROPERTYs (slide exponent, steer rate)
- Unused MaxDeltaDeg local removed from UpdateSlide steer block

## Build Notes
- MSVC toolchain 14.44.35207 required (14.40-14.43 banned by UE 5.7)
- Toolchain lives in VS 2022 directory (UBT only searches there)
- If Live Coding mutex blocks builds, delete `Intermediate/Build/Win64/x64/ExtractionEditor/Development/Makefile.bin`

## Session Log
- **Session 1:** Fixed MSVC toolchain, stripped template, restructured source into subfolders, built animation C++ foundation (AnimInstance, AnimDataAsset, EWeaponType), created agent_docs
- **Session 2:** GameMode C++ defaults, GameInstance, replication, 5 new input actions + stubs, movement config, sprint system, crouch, QCHECK review, fresh BPs, ABP with blendspace, PlayerStart spawning
- **Session 3:** Implemented toggle crouch with smooth camera interp (BaseEyeHeight + FInterpTo). Set up ABP states for crouch, jump (Start/FallLoop/End), and slide with Inertialization blending. Fixed crouch input (needed Pressed trigger on IA_Crouch). Implemented full slide system: initially friction-based, then refactored to curve-based deceleration (SlidePeakSpeed→SlideEndSpeed with power exponent). Added slide steering (SlideSteerRate). Fixed sprint-on-spawn (BeginPlay re-applies BP speeds). Fixed crouch-faster-than-walk (same root cause). Fixed slide ledge blocking (bCanWalkOffLedgesWhenCrouching). Removed ground check from slide so it holds in air. Added ABP transition guards (NOT bIsSliding on jump/fall transitions). Added tooltips to non-obvious slide properties. Updated ue5_conventions.md with tooltip convention. Tuned movement speeds to match blendspace axes (Walk=300, Sprint=600, Crouch=120). Renamed Content/FirstPerson to Content/Core in-editor.
