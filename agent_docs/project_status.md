# Project Status (Last Updated: Session 4)

## Project Overview
- **Name:** ProjectExtract — First-person extraction shooter
- **Engine:** Unreal Engine 5.7
- **Module:** `Extraction` (single module)
- **Purpose:** MSc Game Programming degree project

## Current State: Prone System C++ Complete (Needs Editor Setup)
All locomotion systems are implemented: walk, sprint, crouch (toggle with smooth camera), jump, slide (curve-based decel), and **prone** (C++ complete, editor assets pending). Crouch and slide merged onto C key (context-sensitive). Prone system supports 4 context-sensitive entry transitions (idle, walk, sprint knee-slide, crouch), two exit paths (Z = full stand, C = exit to crouch), 8-directional prone locomotion blendspace, root-motion transitions, and capsule/camera management.

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
- **Prone (C++ complete, editor assets pending):** Toggle on Z key. 4 context-sensitive entry transitions (idle, walk, sprint knee-slide, crouch — determined by DetermineProneTransitionType). Two exit paths: Z = full prone-to-stand, C = partial prone-to-crouch (stops montage early). Root-motion-driven transitions via montages. Capsule resizes (30 half-height, 40 radius). Camera interp to prone height (ProneCameraInterpSpeed). Geometry overlap test blocks exit under low ceilings. MaxWalkSpeedProne = 70 cm/s. Crouch-to-prone skips into idle-to-prone montage (CrouchToProneStartPosition = 0.35). Prone-to-crouch stops exit montage at ProneToStandCrouchPosition = 0.6. All state guards: prone blocks sprint, jump, slide, crouch.
- **Merged C key:** CrouchSlideAction replaces separate CrouchAction + SlideAction. Sprint+C = slide, walk+C = crouch toggle, prone+C = exit to crouch.
- BeginPlay re-applies BP movement speeds (fixes constructor using C++ defaults instead of BP overrides)
- Replication: bReplicates, bIsSprinting + bIsSliding + bIsProne with COND_SkipOwner, OnRep functions
- Blendspaces: idle/walk/run (0-480 Speed axis), crouch (0-120 Speed axis)
- AnimInstance reads all state from character: Speed, Direction, bIsSprinting, bIsSliding, bIsCrouching, bIsProne, bIsTransitioningToProne, bIsTransitioningFromProne, bIsInAir, bIsFalling, AimPitch, AimYaw
- Tooltips on non-obvious slide config properties (exponent, steer rate)

## What's Next
- **Prone editor setup (immediate):**
  - Create `IA_Prone` input action (Digital bool) in editor, map Z in `IMC_Default`
  - Create `IA_CrouchSlide` input action (Digital bool) in editor, map C in `IMC_Default` (replaces IA_Crouch + IA_Slide)
  - Assign `IA_Prone` → ProneAction and `IA_CrouchSlide` → CrouchSlideAction on BP_ExtractionCharacter
  - Create 5 transition montages from LAMP Vol 2 UnarmedProne anims (enable root motion):
    - `AM_IdleToProne_Rifle` ← `anim_Stand_To_Prone`
    - `AM_WalkToProne_Rifle` ← `anim_Run_To_Prone_02_R`
    - `AM_SprintToProne_Rifle` ← `anim_Run_To_Prone_01_R`
    - `AM_CrouchToProne_Rifle` ← same as idle (reused, code skips partway)
    - `AM_ProneToStand_Rifle` ← `anim_Prone_To_Stand`
  - Create `BS_RifleProne` blendspace (Speed 0-80, Direction -180/180) with 8-dir prone loops
  - Create/update `DA_Anim_Rifle` data asset with all prone references assigned
  - Add "Prone" state to ABP state machine (uses GetActiveProneLocomotionBlendSpace)
  - Add ABP transitions: Idle/Walk/Run → Prone (bIsProne), Crouch → Prone (bIsProne), Prone → Idle/Walk/Run (!bIsProne && !bIsTransitioningFromProne), Prone → Crouch (!bIsProne && !bIsTransitioningFromProne && bIsCrouching)
  - Add `!bIsProne` guards to existing ABP transitions to Jump/Slide states
  - Test all transitions and tune CrouchToProneStartPosition / ProneToStandCrouchPosition values
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
| `ExtractionTypes.h` | Shared enums — `EWeaponType` (Unarmed, Pistol, Rifle), `EProneTransitionType` (None, FromIdle, FromWalk, FromSprint, FromCrouch) |

### Source/Extraction/Private/Core/
| File | Purpose |
|------|---------|
| `Extraction.cpp` | Module implementation (IMPLEMENT_PRIMARY_GAME_MODULE) |

### Source/Extraction/Public/Animation/
| File | Purpose |
|------|---------|
| `ExtractionAnimDataAsset.h` | UDataAsset holding all anim refs for one weapon type (blendspace, idles, jumps, prone blendspace + transitions, montages) |
| `ExtractionAnimInstance.h` | Core AnimInstance — locomotion props, state flags (bIsSliding, bIsSprinting, bIsCrouching, bIsProne, bIsTransitioningToProne/FromProne, etc.), aim offset, montage API (incl. PlayProneTransitionMontage, PlayProneExitMontage), weapon switching, GetActiveProneLocomotionBlendSpace |

### Source/Extraction/Private/Animation/
| File | Purpose |
|------|---------|
| `ExtractionAnimInstance.cpp` | AnimInstance impl — caches character/movement, zero-alloc update, reads bIsSprinting + bIsSliding + bIsProne + transition flags from character, montage helpers incl. prone transition/exit montage playback |

### Source/Extraction/Public/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.h` | FP character — replication, input actions (CrouchSlideAction merged, ProneAction added), sprint, crouch, slide, prone (toggle, 4 entry transitions, 2 exit paths, capsule/camera management, geometry checks), movement config with tooltips |
| `ExtractionCameraManager.h` | Camera manager with pitch limits (-70, 80) |

### Source/Extraction/Private/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.cpp` | Character impl — BeginPlay re-applies BP speeds, sprint (locally controlled, forward-only), CrouchSlideInput (merged C key: sprint+C=slide, walk+C=crouch, prone+C=exit-to-crouch), slide (EnterSlide/UpdateSlide/EndSlide), prone (ProneToggle/DetermineTransitionType/EnterProne/ExitProne/ExitProneToCrouch/UpdateProne with montage callbacks and geometry overlap checks), replication (COND_SkipOwner) |
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
| `ABP_ExtractionAnimBp` | Animation Blueprint — 6-state machine (Idle/Walk/Run, Crouch, JumpStart, FallLoop, JumpEnd, Slide) + Prone state pending. Inertialization node, transitions guard bIsSliding for air states |
| `BS_Idle_Walk_Run` | Blendspace — Speed 0-480, Direction -180/180 (idle=0, walk=120, jog=240, sprint=480) |
| `BS_Crouch` | Blendspace — Speed 0-120, Direction -180/180 |
| `IMC_Default` | Input Mapping Context — WASD, mouse, Shift(sprint), C(crouch/slide merged — pending remap from Ctrl+C to just C), Z(prone — pending), Space(jump), V(vault), E(interact) |

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
7. **Replication-ready** — bReplicates, COND_SkipOwner on bIsSprinting/bIsSliding/bIsProne
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
- **Session 4:** Full prone system C++ implementation. Added EProneTransitionType enum. Extended ExtractionAnimDataAsset with prone blendspace + 5 transition montage fields. Extended ExtractionAnimInstance with bIsProne/bIsTransitioningToProne/bIsTransitioningFromProne flags, GetActiveProneLocomotionBlendSpace getter, PlayProneTransitionMontage/PlayProneExitMontage API. Major ExtractionCharacter changes: merged CrouchAction+SlideAction into CrouchSlideAction (context-sensitive C key), added ProneAction (Z key toggle), added prone config (MaxWalkSpeedProne, ProneHalfHeight, ProneCapsuleRadius, ProneCameraInterpSpeed, CrouchToProneStartPosition, ProneToStandCrouchPosition), replicated bIsProne with COND_SkipOwner. Implemented full prone lifecycle: ProneToggle → DetermineProneTransitionType → EnterProne (montage + Montage_SetPosition for crouch skip) → OnProneTransitionFinished (capsule shrink, speed set) → ExitProne/ExitProneToCrouch (geometry overlap test, montage, timer-based early stop for crouch exit) → OnProneExitFinished/OnProneExitToCrouchFinished. Extended Tick camera interp for prone height. Added state guards to sprint/jump/slide/crouch. Editor assets pending: IA_Prone, IA_CrouchSlide, BS_RifleProne, 5 transition montages, DA_Anim_Rifle, ABP Prone state + transitions.
