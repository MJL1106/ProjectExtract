# Weapon Migration Phase 2 — In-Engine Handoff

C++ side is landed and built. Below are the editor-side steps for the in-engine AIK agent. Paste this entire file into the in-engine chat.

## A. BP_Item_Base — implement IKitWeaponInterface

1. Open `/Game/ProceduralFPSKIT/Blueprints/Interactables/BP_Item_Base`.
2. Class Settings → Details → Interfaces → Add → select `KitWeaponInterface`.
3. Compile.
4. Save.

## B. BP_FPCharacter — retype slot variables

1. Open `/Game/ProceduralFPSKIT/Blueprints/BP_FPCharacter`.
2. My Blueprint panel → Variables → for each of `PrimarySlot`, `SecondarySlot`, `HandsSlot`, `MeleeSlot`, `ThrowableSlot`: Variable Type → change `BP_Item_Base` (Class Reference) to `Actor` (Class Reference).
3. For `SpawnedItem` and `SpawnedItemRef`: Variable Type → change `BP_Item_Base` (Object Reference) to `Actor` (Object Reference).
4. Compile (errors on direct-call nodes are expected — fixed in section C).

## C. BP_FPCharacter — convert direct calls to interface message sends

1. In each event graph that calls any of the following on `SpawnedItem` / `SpawnedItemRef`: `Reload`, `BeginFire`, `StopFire`, `Fire_HitScan`, `Inspect`, `Melee`, `ChangeFireMode`, `BurstFire`, `FinishFire`, `Trigger`, `SpawnAttachments`, `Unequip`.
2. Right-click the broken `K2Node_CallFunction` → Convert Call to Message.
3. If Convert Call to Message is unavailable: delete the node, right-click drag from the actor pin → search `Kit<MethodName>` (Message) → place it.
4. Wire the actor ref (SpawnedItem / SpawnedItemRef) into the Target pin of the message node.
5. Reconnect all input pins and exec pins.
6. Compile clean.
7. Save.

## D. AC_ProceduralAnimation — read ProceduralValues via interface

1. Open `/Game/ProceduralFPSKIT/Blueprints/Components/AC_ProceduralAnimation`.
2. Locate the Equip custom event graph.
3. Find the node reading `WeaponActorRef.ProceduralValues`.
4. Delete that node.
5. Right-click drag from `WeaponActorRef` → search `Get Kit Procedural Values` (Message) → place it.
6. Wire `WeaponActorRef` to the Target pin.
7. Connect the returned DataAsset output to the existing `Set ProceduralValues` node input.
8. Compile.
9. Save.

## E. Project-side weapon BPs (OPTIONAL — skip if no project-side weapon BPs exist yet)

1. For each project-side weapon BP (e.g., `BP_OurWeapon_Rifle`): open it.
2. Class Settings → Parent Class → confirm `WeaponBase`.
3. Class Defaults → category `Kit Weapon Bridge` → `KitWeaponPoseAsset` → assign the matching kit DataAsset (e.g., `/Game/ProceduralFPSKIT/Blueprints/Interactables/AmericanRifle/DT_RifleAnimationValues` for a rifle archetype).
4. Compile.
5. Save.

## F. Verification scenarios

1. Place `BP_FPCharacter` in a test map; Details → Pawn → Auto Possess Player → Player 0.
2. Class Defaults → `PrimarySlot` → assign `BP_Weapon_AmericanRifle` (kit weapon).
3. PIE → equip → expected: procedural arm pose loads, fire works, reload works.
4. Stop PIE.
5. Class Defaults → `PrimarySlot` → swap to a project-side weapon BP (parent `WeaponBase`, with `KitWeaponPoseAsset` assigned per section E).
6. PIE → equip → expected: procedural arm pose loads from the assigned DataAsset, fire uses our trace + damage path, reload uses our state machine.
