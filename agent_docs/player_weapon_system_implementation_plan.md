# Hybrid Player Weapon System - Implementation and Handoff Plan

Last reconciled: 2026-07-19

## Goal

Deliver a scalable hybrid player weapon system:

- ProjectExtract remains authoritative for input, firing, cadence, ammunition, damage, reload state, and gameplay recoil.
- Procedural FPS Kit retains movement sway, sprint/lean presentation, and turn-in-place.
- Retargeted Kinemation character animations drive first-person idle, draw, holster, fire response, and reloads.
- Kinemation weapon meshes and mechanical animations run through project-owned passive weapon views.
- Stable data-driven attachments cannot break ADS.
- New weapons are onboarded through data, a passive view, markers, and action mappings instead of player-Blueprint surgery.

This supersedes the abandoned pre-pivot design where Procedural FPS owned the arms/actions. Do not restore that direction.

## Locked architecture

### Gameplay authority

AWeaponBase, UWeaponComponent, and the existing ProjectExtract weapon path remain authoritative.

- Kinemation marketplace weapon Blueprints do not own input, firing, ammunition, damage, or reload state.
- Camera-forward ProjectExtract hitscan remains the ballistic path.
- ProjectExtract camera recoil remains active.
- Kinemation recoil is visual arms recoil only.
- Presentation notifies must not independently grant ammunition when the profile action lane owns reload progression.

### Presentation split

- Procedural FPS owns locomotion-driven motion and turn-in-place.
- Kinemation owns arms poses and weapon actions.
- Project-owned APlayerWeaponView Blueprints play matching Kinemation weapon sequences for mechanical parts.
- UPlayerWeaponPresentationComponent adapts authoritative gameplay state to both presentation lanes.
- ABP_FP_ArmsProcedural composes Procedural movement with the Kinemation arms/action layer.
- AC_ProceduralAnimation remains initialization-safe when ProceduralValues is unavailable.

### Data-driven onboarding

Each weapon uses:

- one ProjectExtract UWeaponDataAsset;
- one UPlayerWeaponPresentationProfile;
- one passive APlayerWeaponView Blueprint;
- explicit weapon-seat, muzzle, AimPoint, and moving-part markers;
- Kinemation arms action mappings plus matching project-owned weapon animation copies;
- zero or more UPlayerWeaponAttachmentDefinition assets selected by stable IDs.

C++ stays asset-agnostic: no hard-coded /Game/... paths.

## Current state

Legend: [x] implemented and statically verified, [~] implemented but awaiting user PIE acceptance, [ ] incomplete.

### Native system

- [x] UPlayerWeaponPresentationComponent is integrated.
- [x] Equip, aim, accepted shot, reload, ammo, and action state are bridged from ProjectExtract gameplay.
- [x] Local-player gating, delegate cleanup, teardown, repeated-equip safety, and cached camera access are implemented.
- [x] Profile-driven views replace marketplace gameplay ownership.
- [x] Stable APIs exist for SetSelectedOpticId, SetSelectedGripId, and SetSelectedMuzzleId.
- [x] ADS resolves explicit view/attachment markers, not attachment array order.
- [x] The profile lane blocks the legacy shell notify from double-granting ammunition.

### Arms, motion, recoil, and reloads

- [x] Kinemation character animations were retargeted to Character_03 under /Game/Core/Anims/Player.
- [x] Kinemation arms drive idle, draw, holster, fire response, and reload actions.
- [x] Procedural movement, sway, sprint/lean response, and turn-in-place remain wired.
- [x] Kinemation visual recoil is separate from ProjectExtract gameplay/camera recoil.
- [x] Project-owned weapon sequences drive magazine, shell, cylinder, bolt, and other mechanical motion.
- [x] Herrington advances one shell per authored loop through action completion.
- [x] R08 maps reload variants for 1 through 8 rounds missing.
- [x] Mk14 permits the weapon mechanical track to finish before the longer arms tail.
- [~] Final pose, blend, and moving-part contact quality requires PIE acceptance.

### Attachments and ADS

- [x] TR15 supports XPS2, XPS2+G33, irons, grip, and silencer.
- [x] SRM-12 supports Phantom TAC.
- [x] Optic, grip, and muzzle selection are independent.
- [x] Attachment reordering cannot silently change the selected sight.
- [~] Every sight requires hip/ADS visual acceptance.

## Supported weapon matrix

| Weapon | Actions | Selectable kit attachments | Special handling | Runtime |
|---|---:|---|---|---|
| TR15 | 8 | XPS2, XPS2+G33, irons, grip, silencer | Reference rifle | Awaiting PIE |
| AK105 | 7 | None | Tactical and empty reload | Awaiting PIE |
| Herrington 11-87 | 10 | None | Shell start, loop, end, empty start | Awaiting PIE |
| Mk14EBR | 7 | None | Independent arms/weapon end times | Awaiting PIE |
| R08 | 15 | None | Reload variants for 1-8 rounds missing | Awaiting PIE |
| SRM-12 | 7 | Phantom TAC | Magazine-fed; profile/gameplay type must agree | Awaiting PIE |
| WK-11 Viper | 8 | None | Empty-fire plus tactical/empty reload | Awaiting PIE |

AK105, Herrington, Mk14EBR, R08, and WK-11 Viper do not ship selectable attachment art in this kit. Their magazines and moving parts are reload geometry, not attachments.

## Remaining plan

### Gate 1 - TR15 runtime acceptance

- [ ] User starts PIE and equips TR15.
- [ ] Hip idle has correctly seated weapon and untwisted arms.
- [ ] Walk, sprint, lean, and turn-in-place preserve Procedural motion without detachment.
- [ ] XPS2 ADS aligns to camera and returns cleanly to hip.
- [ ] Hip/ADS fire keeps ProjectExtract authority and adds Kinemation visual recoil.
- [ ] Tactical reload removes and reseats the magazine cleanly with one correct ammo result.
- [ ] Empty reload uses its distinct action with one correct ammo result.
- [ ] Draw, holster, swap away, and re-equip return to correct idle.
- [ ] Stop PIE and inspect for new Blueprint Runtime Error, Accessed None, or player-presentation warnings.

On failure, fix the first reproducible issue and repeat this gate. Do not stack speculative transform offsets.

### Gate 2 - TR15 attachments

- [ ] Test XPS2, XPS2+G33, and irons in hip and ADS.
- [ ] Confirm grip changes do not move the sight line.
- [ ] Confirm silencer changes refresh the muzzle without moving ADS.
- [ ] Confirm repeated swaps do not duplicate view actors/components.

### Gate 3 - remaining weapons

- [ ] AK105: idle, draw, holster, fire, ADS, tactical reload, empty reload.
- [ ] Herrington: fire/empty-fire, multi-shell reload, interrupt, resume, final chamber/ammo state.
- [ ] Mk14EBR: fire, ADS, both reloads, clean arms/weapon tail synchronization.
- [ ] R08: fire and reload variants for 1-8 rounds missing; cylinder/round visuals match.
- [ ] SRM-12: fire, Phantom TAC ADS, tactical reload, empty reload.
- [ ] WK-11 Viper: fire/empty-fire, ADS, tactical reload, empty reload.

### Gate 4 - final readiness

- [ ] Fix and re-review every runtime finding.
- [ ] Recompile affected Blueprints/AnimBPs with zero errors.
- [ ] Run Data Validation with zero invalid assets.
- [ ] Run an editor-closed build and require Result: Succeeded.
- [ ] Reopen ProjectExtract and confirm VibeUE plus NeoStack without starting PIE.
- [ ] Complete the final user-owned seven-weapon/attachment PIE pass.
- [ ] Mark complete only after user runtime acceptance.

## Verification completed

- [x] ExtractionEditor Win64 Development build succeeded on the current implementation.
- [x] C:\Users\matth\AppData\Local\Temp\extraction-build-20260719-092041.out.log contains Result: Succeeded.
- [x] 24 affected Blueprints/AnimBPs compiled successfully through NeoStack after rebuild.
- [x] Full Data Validation: 244 valid, 0 invalid, 0 warnings, 0 skipped, 0 unable.
- [x] C++ review has no outstanding CRITICAL or WARNING findings.
- [x] git diff --check -- Extraction/Source/Extraction is clean apart from expected line-ending warnings.
- [x] ProjectExtract editor was reopened; PID 23328 was responding when this plan was updated.
- [ ] No post-pivot user PIE acceptance has been received.

Older SetupFireAlign warnings concern enemy meshes missing WeaponSocket_Fire. They are outside this player-presentation plan unless reproduced on the player path.

## Key native files

- Extraction/Source/Extraction/Public/Components/PlayerWeaponPresentationComponent.h
- Extraction/Source/Extraction/Private/Components/PlayerWeaponPresentationComponent.cpp
- Extraction/Source/Extraction/Public/Data/PlayerWeaponPresentationTypes.h
- Extraction/Source/Extraction/Public/Data/PlayerWeaponPresentationProfile.h
- Extraction/Source/Extraction/Private/Data/PlayerWeaponPresentationProfile.cpp
- Extraction/Source/Extraction/Public/Data/PlayerWeaponAttachmentDefinition.h
- Extraction/Source/Extraction/Private/Data/PlayerWeaponAttachmentDefinition.cpp
- Extraction/Source/Extraction/Public/Weapon/PlayerWeaponView.h
- Extraction/Source/Extraction/Private/Weapon/PlayerWeaponView.cpp
- Extraction/Source/Extraction/Public/Data/WeaponDataAsset.h
- Extraction/Source/Extraction/Private/Data/WeaponDataAsset.cpp
- Extraction/Source/Extraction/Public/Animation/AnimNotifyState_MagazineSwap.h
- Extraction/Source/Extraction/Private/Animation/AnimNotifyState_MagazineSwap.cpp
- Extraction/Source/Extraction/Public/Animation/AnimNotify_EnemyShellInserted.h
- Extraction/Source/Extraction/Private/Animation/AnimNotify_EnemyShellInserted.cpp
- Extraction/Source/Extraction/Public/Character/ExtractionPlayer.h
- Extraction/Source/Extraction/Private/Character/ExtractionPlayer.cpp
- Extraction/Source/Extraction/Public/Weapon/WeaponBase.h

## Key in-engine assets

- /Game/ProceduralFPSKIT/Blueprints/BP_ExtractionCharacter
- /Game/ProceduralFPSKIT/Blueprints/Components/AC_ProceduralAnimation
- /Game/ProceduralFPSKIT/Character/ABP_FP_ArmsProcedural
- /Game/Core/Anims/Player
- /Game/Core/Weapons/Kinemation
- /Game/Core/Weapons/Presentation/Kinemation/BP_AC_PlayerVisualRecoil
- /Game/Core/Weapons/Presentation/Kinemation/TR15
- /Game/Core/Weapons/Presentation/Kinemation/AK105
- /Game/Core/Weapons/Presentation/Kinemation/Herrington_11_87
- /Game/Core/Weapons/Presentation/Kinemation/Mk14EBR
- /Game/Core/Weapons/Presentation/Kinemation/R08
- /Game/Core/Weapons/Presentation/Kinemation/SRM_12
- /Game/Core/Weapons/Presentation/Kinemation/WK_11_Viper
- /Game/Core/Weapons/Presentation/Kinemation/Attachments/TR15
- /Game/Core/Weapons/Presentation/Kinemation/Attachments/SRM_12

## Repository and editor state

- Branch: Player-Setup, seven commits ahead of origin/Player-Setup when reconciled.
- HEAD: 2b9bd03a4eea; substantial implementation remains in the working tree after that commit.
- Working tree is intentionally dirty; do not discard/reset weapon-system or user/vendor changes.
- ProjectExtract editor is open after the green rebuild; PIE is not running.
- No background build or test process remains.
- Do not use test-driven development; focused automation is only a final regression aid.
- Codex must not start PIE, Simulate, or inject gameplay input; the user owns gameplay testing.

## Rules for the next chat

1. Read this file before changing anything.
2. Read agent_docs/UnrealWorkflow.md before in-engine work.
3. Inspect/edit Blueprints and DataAssets through VibeUE or NeoStack, not desktop UI automation.
4. Preserve ProjectExtract gameplay authority and the Kinemation-arms/Procedural-movement split.
5. Do not revert the dirty working tree or vendor/user edits.
6. Review in-engine edits by re-reading state and compiling affected assets.
7. Resume at TR15 runtime acceptance before expanding scope.

## Pick up here

The system is build-green and statically verified. Obtain the user's TR15 hip/ADS screenshots, tactical/empty reload result, and new runtime errors. Fix the first concrete runtime failure before testing the remaining six weapons.