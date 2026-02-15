# Architecture Overview

## Module Structure
Single module: `Extraction`. All gameplay code lives here.

```
Source/Extraction/
  Public/
    Core/         — Shared types, module header
    Character/    — Player character, camera manager
    Animation/    — AnimInstance, AnimDataAsset
    Game/         — GameMode, PlayerController, GameInstance
  Private/
    Core/         — Module implementation
    Character/    — Character/camera .cpp files
    Animation/    — AnimInstance .cpp
    Game/         — GameMode/Controller/GameInstance .cpp files
```

## System Dependency Graph

```
ExtractionGameInstance (persistent across levels)
  |-- will hold: player loadouts, progression, session data

ExtractionGameMode
  |-- sets default Pawn -> ExtractionCharacter (C++ constructor)
  |-- sets default Controller -> ExtractionPlayerController (C++ constructor)
  |-- sets default CameraManager -> ExtractionCameraManager (via Controller)

ExtractionPlayerController
  |-- owns InputMappingContexts (Enhanced Input)
  |-- sets PlayerCameraManagerClass -> ExtractionCameraManager
  |-- spawns mobile controls widget (optional)

ExtractionCharacter (bReplicates = true)
  |-- owns FirstPersonMesh (USkeletalMeshComponent, owner-only)
  |-- owns FirstPersonCameraComponent (UCameraComponent, on FP mesh head socket)
  |-- reads 9 InputActions: Move, Look, MouseLook, Jump, Sprint, Crouch, Slide, Vault, Interact
  |-- movement config: WalkSpeed 600, SprintSpeed 900, Accel 2048, BrakingDecel 2048
  |-- jump config: JumpZVelocity 500, AirControl 0.2
  |-- sprint system: bWantsToSprint (input) -> UpdateSprint() in Tick -> bIsSprinting (replicated)
  |-- crouch: built-in UE5 crouch (bCanCrouch = true)
  |-- provides GetExtractionAnimInstance() -> AnimInstance on GetMesh()
  |-- provides GetIsSprinting() -> replicated sprint state

ExtractionAnimInstance (on third-person skeletal mesh)
  |-- reads from: ExtractionCharacter (velocity, state, aim, sprint)
  |-- reads from: UCharacterMovementComponent (falling, crouching, speed)
  |-- owns: TMap<EWeaponType, ExtractionAnimDataAsset*>
  |-- provides: locomotion data (Speed, Direction, NormalizedSpeed)
  |-- provides: state flags (bIsInAir, bIsFalling, bIsCrouching, bIsSprinting, bIsADS, bIsAlive, etc.)
  |-- provides: aim offset (AimPitch, AimYaw)
  |-- provides: montage API (Fire, Reload, Equip, HitReact, Death)

ExtractionAnimDataAsset (one per weapon type)
  |-- LocomotionBlendSpace (8-dir)
  |-- IdleAnim
  |-- Jump anims (Start, Apex, FallLoop, Land, RecoveryAdditive)
  |-- Weapon montages (ADS, AimOffset, Fire, DryFire, Reload, Equip)
  |-- Damage montages (HitReact[], Death[])
```

## Character Input Flow

```
PlayerController::SetupInputComponent()
  -> Adds DefaultMappingContexts (IMC assets) to Enhanced Input Subsystem

Character::SetupPlayerInputComponent()
  -> Binds 9 InputActions to handlers:
     IA_Move      -> MoveInput()     -> DoMove()
     IA_Look      -> LookInput()     -> DoAim()
     IA_MouseLook -> LookInput()     -> DoAim()
     IA_Jump      -> DoJumpStart() / DoJumpEnd()
     IA_Sprint    -> SprintStart() / SprintStop()  -> UpdateSprint() in Tick
     IA_Crouch    -> CrouchStart() / CrouchStop()  -> built-in Crouch/UnCrouch
     IA_Slide     -> SlideStart()    [stub]
     IA_Vault     -> VaultStart()    [stub]
     IA_Interact  -> InteractStart() [stub]
```

## Sprint System

```
SprintStart()  -> bWantsToSprint = true
SprintStop()   -> bWantsToSprint = false

Tick() -> UpdateSprint():
  Conditions: bWantsToSprint && bHasVelocity && bOnGround && bNotCrouching && bMovingForward
  On state change: bIsSprinting updated -> ApplySprintSpeed()
  ApplySprintSpeed(): MaxWalkSpeed = bIsSprinting ? SprintSpeed : WalkSpeed

Replication:
  bIsSprinting is DOREPLIFETIME -> OnRep_IsSprinting() -> ApplySprintSpeed()
```

## Animation System Design

### Data Flow
```
Character velocity/state/sprint
        |
        v
AnimInstance::NativeUpdateAnimation()  (reads cached refs, zero alloc)
  |-- Speed, Direction, NormalizedSpeed from velocity
  |-- bIsInAir, bIsFalling, bIsCrouching from movement component
  |-- bIsSprinting from OwningCharacter->GetIsSprinting()
  |-- AimPitch, AimYaw from GetBaseAimRotation() delta
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
GameInstance (persistent, survives level transitions)
  GameMode (server authority, per-level)
    -> GameState (replicated game data)
      -> PlayerController (per-player input/UI)
        -> PlayerState (per-player replicated data)
          -> Pawn/Character (physical presence)
```

## Future Systems (Planned, Not Implemented)
- Slide System (C++ in character, needs animation)
- Vault System (C++ in character, needs traces + animation)
- Interaction System (C++ in character, trace-based)
- Weapon System (inventory, equip/unequip, firing)
- AI System (enemies, companions)
- Health/Damage System
- Game Mode variants (extraction objectives)
- Level System (multiple maps)
- UI System (HUD, menus)
