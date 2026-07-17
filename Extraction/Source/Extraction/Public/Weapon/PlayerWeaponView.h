// Passive project-owned wrappers for first-person weapon and attachment art.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Data/PlayerWeaponPresentationTypes.h"
#include "GameFramework/Actor.h"
#include "PlayerWeaponView.generated.h"

UENUM(BlueprintType)
enum class EPlayerWeaponMarkerKind : uint8
{
	WeaponSeat			UMETA(DisplayName = "Weapon Seat"),
	SupportHandTarget	UMETA(DisplayName = "Support Hand Target"),
	SupportHandHint		UMETA(DisplayName = "Support Hand Hint"),
	IronRear			UMETA(DisplayName = "Iron Rear"),
	IronFront			UMETA(DisplayName = "Iron Front"),
	OpticMount			UMETA(DisplayName = "Optic Mount"),
	Muzzle				UMETA(DisplayName = "Muzzle"),
	Casing				UMETA(DisplayName = "Casing"),
	AttachmentMount		UMETA(DisplayName = "Attachment Mount"),
	AimPoint			UMETA(DisplayName = "Aim Point"),
	MovingPart			UMETA(DisplayName = "Moving Part"),
};

/**
 * Typed transform marker. Local +X is downrange, +Y is right, and +Z is up.
 */
UCLASS(ClassGroup = (Weapon), meta = (BlueprintSpawnableComponent))
class EXTRACTION_API UPlayerWeaponMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:

	UPlayerWeaponMarkerComponent();

	void ConfigureMarker(
		EPlayerWeaponMarkerKind InKind,
		EPlayerWeaponMovingPart InMovingPart = EPlayerWeaponMovingPart::Magazine);

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Marker")
	EPlayerWeaponMarkerKind GetMarkerKind() const { return MarkerKind; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Marker")
	EPlayerWeaponMovingPart GetMovingPart() const { return MovingPart; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Marker")
	FVector GetForwardAxis() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Marker")
	FVector GetRightAxis() const;

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Marker")
	FVector GetUpAxis() const;

private:

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Marker",
		meta = (AllowPrivateAccess = "true"))
	EPlayerWeaponMarkerKind MarkerKind = EPlayerWeaponMarkerKind::WeaponSeat;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Marker",
		meta = (AllowPrivateAccess = "true",
			EditCondition = "MarkerKind == EPlayerWeaponMarkerKind::MovingPart",
			EditConditionHides))
	EPlayerWeaponMovingPart MovingPart = EPlayerWeaponMovingPart::Magazine;
};

UCLASS(Blueprintable)
class EXTRACTION_API APlayerWeaponView : public AActor
{
	GENERATED_BODY()

public:

	APlayerWeaponView();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Presentation")
	bool InitializeView();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Presentation")
	void ReleaseView();

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsViewInitialized() const { return bViewInitialized; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	int32 GetRegisteredMarkerCount() const { return RegisteredMarkerCount; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	USceneComponent* GetViewRoot() const { return ViewRoot; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	USceneComponent* GetArtRoot() const { return ArtRoot; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetWeaponSeatMarker() const { return WeaponSeatMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetSupportHandTargetMarker() const
	{
		return SupportHandTargetMarker;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetSupportHandHintMarker() const
	{
		return SupportHandHintMarker;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetIronRearMarker() const { return IronRearMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetIronFrontMarker() const { return IronFrontMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetOpticMountMarker() const { return OpticMountMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetMuzzleMarker() const { return MuzzleMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetCasingMarker() const { return CasingMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetMovingPartMarker(EPlayerWeaponMovingPart Part) const;

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PreInitializeComponents() override;
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation")
	TObjectPtr<USceneComponent> ViewRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation")
	TObjectPtr<USceneComponent> ArtRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> WeaponSeatMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> SupportHandTargetMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> SupportHandHintMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> IronRearMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> IronFrontMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> OpticMountMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> MuzzleMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> CasingMarker;

private:

	TMap<EPlayerWeaponMovingPart, TWeakObjectPtr<UPlayerWeaponMarkerComponent>>
		MovingPartMarkers;
	bool bViewInitialized = false;
	int32 RegisteredMarkerCount = 0;
};

UCLASS(Blueprintable)
class EXTRACTION_API APlayerWeaponAttachmentView : public AActor
{
	GENERATED_BODY()

public:

	APlayerWeaponAttachmentView();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Presentation")
	bool InitializeView();

	UFUNCTION(BlueprintCallable, Category = "Weapon|Presentation")
	void ReleaseView();

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	bool IsViewInitialized() const { return bViewInitialized; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	int32 GetRegisteredMarkerCount() const { return RegisteredMarkerCount; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	USceneComponent* GetViewRoot() const { return ViewRoot; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation")
	USceneComponent* GetArtRoot() const { return ArtRoot; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetAttachmentMountMarker() const
	{
		return AttachmentMountMarker;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetAimPointMarker() const { return AimPointMarker; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetSupportHandTargetMarker() const
	{
		return SupportHandTargetMarker;
	}

	UFUNCTION(BlueprintPure, Category = "Weapon|Presentation|Markers")
	UPlayerWeaponMarkerComponent* GetSupportHandHintMarker() const
	{
		return SupportHandHintMarker;
	}

protected:

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PreInitializeComponents() override;
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation")
	TObjectPtr<USceneComponent> ViewRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation")
	TObjectPtr<USceneComponent> ArtRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> AttachmentMountMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> AimPointMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> SupportHandTargetMarker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Presentation|Markers")
	TObjectPtr<UPlayerWeaponMarkerComponent> SupportHandHintMarker;

private:

	TMap<EPlayerWeaponMovingPart, TWeakObjectPtr<UPlayerWeaponMarkerComponent>>
		MovingPartMarkers;
	bool bViewInitialized = false;
	int32 RegisteredMarkerCount = 0;
};
