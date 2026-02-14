# Architecture Overview

## Module Structure
Single module: `Extraction`. All gameplay code lives here.

```
Source/Extraction/
  Public/
    Core/         — Shared types, module header
    Character/    — Player character, camera manager
    Animation/    — AnimInstance, AnimDataAsset
    Game/         — GameMode, PlayerController
  Private/
    Core/         — Module implementation
    Character/    — Character/camera .cpp files
    Animation/    — AnimInstance .cpp
    Game/         — GameMode/Controller .cpp files
```

## System Dependency Graph

```
ExtractionGameMode
  |-- sets default Pawn -> ExtractionCharacter
  |-- sets default Controller -> ExtractionPlayerController
  |-- sets default CameraManager -> ExtractionCameraManager

ExtractionPlayerController
  |-- owns InputMappingContexts (Enhanced Input)
  |-- spawns mobile controls widget (optional)

ExtractionCharacter
  |-- owns FirstPersonMesh (USkeletalMeshComponent)
  |-- owns FirstPersonCameraComponent (UCameraComponent)
  |-- reads InputActions (Jump, Move, Look, MouseLook)
  |-- provides GetExtractionAnimInstance() -> AnimInstance on GetMesh()

ExtractionAnimInstance (on third-person skeletal mesh)
  |-- reads from: ExtractionCharacter (velocity, state, aim)
  |-- reads from: UCharacterMovementComponent (falling, crouching, speed)
  |-- owns: TMap<EWeaponType, ExtractionAnimDataAsset*>
  |-- provides: locomotion data (Speed, Direction, NormalizedSpeed)
  |-- provides: state flags (bIsInAir, bIsFalling, bIsCrouching, etc.)
  |-- provides: aim offset (AimPitch, AimYaw)
  |-- provides: montage API (Fire, Reload, Equip, HitReact, Death)

ExtractionAnimDataAsset (one per weapon type)
  |-- LocomotionBlendSpace (8-dir)
  |-- IdleAnim
  |-- Jump anims (Start, Apex, FallLoop, Land, RecoveryAdditive)
  |-- Weapon montages (ADS, AimOffset, Fire, DryFire, Reload, Equip)
  |-- Damage montages (HitReact[], Death[])
```

## Animation System Design

### Data Flow
```
Character velocity/state
        |
        v
AnimInstance::NativeUpdateAnimation()  (reads cached refs, zero alloc)
        |
        v
Blueprint-readable properties (Speed, Direction, bIsInAir, etc.)
        |
        v
Animation Blueprint (ABP) reads properties
        |
        +-- Base Layer: LocomotionBlendSpace (from active DataAsset)
        |     driven by Speed + Direction
        |
        +-- Upper Body Layer: Layered Blend per Bone (spine_03)
        |     ADS pose, AimOffset, weapon montages
        |
        +-- Full Body Override: Death montages
```

### Weapon Switching
```
SetWeaponType(EWeaponType)
  -> validates type exists in WeaponAnimSets map
  -> sets CurrentWeaponType
  -> ABP reads new blendspace/anims from GetActiveAnimData()
```

### Adding a New Weapon Type
1. Add entry to `EWeaponType` enum in `ExtractionTypes.h`
2. Create new `UExtractionAnimDataAsset` in Content Browser
3. Assign animation references in the DataAsset
4. Add entry to WeaponAnimSets map in ABP class defaults
5. No C++ changes to AnimInstance needed

## Framework Hierarchy (UE5 Standard)
```
GameMode (server authority)
  -> GameState (replicated game data)
    -> PlayerController (per-player input/UI)
      -> PlayerState (per-player replicated data)
        -> Pawn/Character (physical presence)
```

## Future Systems (Planned, Not Implemented)
- Weapon System (inventory, equip/unequip, firing)
- AI System (enemies, companions)
- Health/Damage System
- Game Mode variants (extraction objectives)
- Level System (multiple maps)
- UI System (HUD, menus)
