// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "KitWeaponInterface.generated.h"

class UDataAsset;

UINTERFACE(MinimalAPI, BlueprintType)
class UKitWeaponInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Blueprint-native dispatch surface used by the AgentIntegrationKit's BP_FPCharacter
 * to drive a held weapon without hard-binding to BP_Item_Base. Both AWeaponBase (C++)
 * and BP_Item_Base (in-editor) implement this so the kit can route via interface
 * message sends instead of typed direct calls.
 *
 * "Kit" prefix avoids collision with AWeaponBase's existing Fire/Reload/Inspect API.
 */
class EXTRACTION_API IKitWeaponInterface
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitReload();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitBeginFire();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitStopFire();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitFire_HitScan();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitInspect();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitMelee();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitChangeFireMode();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitBurstFire();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitFinishFire();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitTrigger();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitSpawnAttachments();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitUnequip();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	UDataAsset* GetKitProceduralValues() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	FTransform GetKitIK_HandGunSocketOffset() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	FTransform GetKitIK_HandRSocketOffset() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	FTransform GetKitIK_HandLSocketOffset() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	float GetKitAimDistanceFromCamera() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	FVector GetKitMuzzleRingScale() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	bool GetKitReloading() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	bool GetKitIsFire() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	void KitSetAmmo(int32 AmmoCount, int32 MaxAmmo);

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Kit Weapon Bridge")
	TSubclassOf<AActor> GetKitVisualWeaponClass() const;
};
