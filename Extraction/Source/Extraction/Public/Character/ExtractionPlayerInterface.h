// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Movement/TraversalTypes.h"
#include "ExtractionPlayerInterface.generated.h"

class UHealthComponent;
class UWeaponComponent;
class UTraversalComponent;
class UExtractionAnimInstance;
class AWeaponBase;
class USceneComponent;

UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UExtractionPlayerInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface implemented by both AExtractionPlayer (new kit-migrated class) and
 * AExtractionCharacter (legacy class kept alive during Phase 1-4 transition).
 *
 * All AI, weapon, animation, and UI code that previously hard-cast to
 * AExtractionCharacter should cast to IExtractionPlayerInterface instead.
 */
class EXTRACTION_API IExtractionPlayerInterface
{
	GENERATED_BODY()

public:

	virtual UHealthComponent* GetHealthComponent() const = 0;
	virtual UWeaponComponent* GetWeaponComponent() const = 0;
	virtual UTraversalComponent* GetTraversalComponent() const = 0;
	virtual UExtractionAnimInstance* GetExtractionAnimInstance() const = 0;
	virtual bool GetIsDBNO() const = 0;
	virtual void ExitDBNO() = 0;
	virtual ETraversalType GetActiveTraversalType() const = 0;
	virtual bool IsInTraversal() const = 0;
	virtual bool GetIsVaulting() const = 0;
	virtual FVector GetVaultTargetLocation() const = 0;
	virtual float GetVaultSurfaceHeight() const = 0;

	/** Apply yaw/pitch aim input — used by recoil and recovery systems. */
	virtual void DoAim(float Yaw, float Pitch) = 0;

	/** Camera-space scene component where weapons are attached. Returns nullptr on
	 *  player classes where the kit BP owns weapon attachment directly. */
	virtual USceneComponent* GetWeaponSpawn() const = 0;

	/** Locomotion state queries. Default false; AExtractionCharacter overrides with replicated state;
	 *  AExtractionPlayer inherits the default since the kit BP owns sprint/slide/prone. */
	virtual bool GetIsSprinting() const { return false; }
	virtual bool GetIsSliding() const { return false; }
	virtual bool GetIsProne() const { return false; }

	/** Notify the implementing character that a weapon was equipped — kit BPs
	 *  use this to push pose data into AC_ProceduralAnimation. Default no-op. */
	virtual void NotifyWeaponEquipped(AWeaponBase* /*EquippedWeapon*/) {}

	/** Notify the implementing character that ADS state changed. Default no-op. */
	virtual void NotifyADSChanged(bool /*bIsADS*/) {}
};
