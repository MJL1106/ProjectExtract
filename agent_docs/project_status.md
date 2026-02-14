# Project Status (Last Updated: Session 1)

## Project Overview
- **Name:** ProjectExtract — First-person extraction shooter
- **Engine:** Unreal Engine 5.7
- **Module:** `Extraction` (single module)
- **Purpose:** MSc Game Programming degree project

## Current State: Foundation Complete
The project has been stripped from the UE5 First Person template down to a bare controllable FP character. A scalable C++ animation system has been built but the Animation Blueprint has **not yet been created** in-editor.

## What Works
- Project compiles cleanly
- Controllable first-person character with Enhanced Input
- C++ animation foundation: AnimInstance, AnimDataAsset, weapon type enums
- Subfolder source structure (Core, Character, Animation, Game)

## What's Next (Not Started)
- Create Animation Blueprint (ABP) in editor, parent to UExtractionAnimInstance
- Create DataAsset instances (Unarmed, Pistol, Rifle) and assign animation references
- Set up blendspaces for 8-dir locomotion per weapon type
- Wire up upper/lower body layer blend in ABP (Layered Blend per Bone on spine_03)
- Build weapon system (C++)
- Build AI system
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
| `ExtractionAnimInstance.cpp` | AnimInstance implementation — caches character/movement, zero-alloc NativeUpdateAnimation, montage helpers |

### Source/Extraction/Public/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.h` | FP character — first-person mesh, camera, Enhanced Input actions, GetExtractionAnimInstance() getter |
| `ExtractionCameraManager.h` | Camera manager with pitch limits (-70, 80) |

### Source/Extraction/Private/Character/
| File | Purpose |
|------|---------|
| `ExtractionCharacter.cpp` | Character implementation — input binding, movement, jump, AnimInstance getter |
| `ExtractionCameraManager.cpp` | Sets pitch limits in constructor |

### Source/Extraction/Public/Game/
| File | Purpose |
|------|---------|
| `ExtractionGameMode.h` | Minimal GameMode — sets default pawn and player controller classes |
| `ExtractionPlayerController.h` | Player controller — input mapping contexts, mobile controls support |

### Source/Extraction/Private/Game/
| File | Purpose |
|------|---------|
| `ExtractionGameMode.cpp` | GameMode constructor — assigns default classes |
| `ExtractionPlayerController.cpp` | Input context setup, mobile controls, BeginPlay logic |

### Config
| File | Purpose |
|------|---------|
| `Extraction.Build.cs` | Module deps: Core, CoreUObject, Engine, InputCore, EnhancedInput, AnimGraphRuntime, UMG, Slate. Include paths for all subfolders. |

## Key Design Decisions
1. **Enums over GameplayTags** for weapon types — known finite set, compile-time safety, simpler debugging
2. **Data-driven animation** — one UExtractionAnimDataAsset per weapon type, no code changes to add new weapons
3. **TMap<EWeaponType, DataAsset>** in AnimInstance — weapon switching is a map lookup
4. **Composition over inheritance** — flat class hierarchy, no deep chains
5. **Zero-allocation NativeUpdateAnimation** — reads cached refs only, no TArray copies or string ops
6. **Subfolder organisation** — Core, Character, Animation, Game — keeps related files grouped

## Build Notes
- MSVC toolchain 14.44.35207 required (14.40-14.43 banned by UE 5.7)
- Toolchain lives in VS 2022 directory (UBT only searches there)
- If Live Coding mutex blocks builds, delete `Intermediate/Build/Win64/x64/ExtractionEditor/Development/Makefile.bin`

## Session Log
- **Session 1:** Fixed MSVC toolchain, stripped template, restructured source, built animation C++ foundation, created agent_docs
