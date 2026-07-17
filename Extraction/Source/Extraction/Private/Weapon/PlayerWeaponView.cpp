// Passive project-owned wrappers for first-person weapon and attachment art.

#include "Weapon/PlayerWeaponView.h"

#include "Components/PrimitiveComponent.h"
#include "Core/Extraction.h"

namespace
{
	void ConfigurePassiveView(AActor& View)
	{
		View.PrimaryActorTick.bCanEverTick = false;
		View.PrimaryActorTick.bStartWithTickEnabled = false;
		View.SetActorTickEnabled(false);
		View.SetReplicates(false);
		View.SetReplicateMovement(false);
		View.AutoReceiveInput = EAutoReceiveInput::Disabled;
		View.SetCanBeDamaged(false);
		View.SetActorEnableCollision(false);
	}

	bool ConfigureComposedPrimitives(AActor& View, USceneComponent& ArtRoot)
	{
		TInlineComponentArray<UPrimitiveComponent*, 16> Primitives;
		View.GetComponents(Primitives);
		bool bValidHierarchy = true;
		for (UPrimitiveComponent* Primitive : Primitives)
		{
			if (!IsValid(Primitive)) continue;
			USceneComponent* AuthoredParent = Primitive->GetAttachParent();
			const bool bUnderArtRoot = IsValid(AuthoredParent)
				&& Primitive->IsAttachedTo(&ArtRoot);
			const FTransform WorldTransform = Primitive->GetComponentTransform();
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetGenerateOverlapEvents(false);
			Primitive->SetSimulatePhysics(false);
			if (bUnderArtRoot && Primitive->GetAttachParent() != AuthoredParent)
			{
				if (!Primitive->AttachToComponent(
					AuthoredParent, FAttachmentTransformRules::KeepWorldTransform))
				{
					UE_LOG(LogExtraction, Warning,
						TEXT("%s could not restore art primitive %s to its authored parent."),
						*GetNameSafe(&View), *GetNameSafe(Primitive));
					bValidHierarchy = false;
				}
			}
			Primitive->SetWorldTransform(
				WorldTransform, false, nullptr, ETeleportType::TeleportPhysics);
			Primitive->SetCanEverAffectNavigation(false);
			if (bUnderArtRoot) continue;
			UE_LOG(LogExtraction, Warning,
				TEXT("%s art primitive %s is detached or outside ArtRoot; authored hierarchy cannot be recovered."),
				*GetNameSafe(&View), *GetNameSafe(Primitive));
			bValidHierarchy = false;
		}
		return bValidHierarchy;
	}

	bool IsMarkerValueValid(
		const AActor& View, const UPlayerWeaponMarkerComponent* Marker)
	{
		if (!IsValid(Marker))
		{
			UE_LOG(LogExtraction, Warning,
				TEXT("%s has an invalid player-weapon marker component."),
				*GetNameSafe(&View));
			return false;
		}
		const EPlayerWeaponMarkerKind Kind = Marker->GetMarkerKind();
		if (!StaticEnum<EPlayerWeaponMarkerKind>()->IsValidEnumValue(
			static_cast<int64>(Kind)))
		{
			UE_LOG(LogExtraction, Warning,
				TEXT("%s has an invalid marker kind."), *GetNameSafe(&View));
			return false;
		}
		if (Kind != EPlayerWeaponMarkerKind::MovingPart) return true;
		if (StaticEnum<EPlayerWeaponMovingPart>()->IsValidEnumValue(
			static_cast<int64>(Marker->GetMovingPart())))
			return true;
		UE_LOG(LogExtraction, Warning,
			TEXT("%s has an invalid moving-part marker."), *GetNameSafe(&View));
		return false;
	}

	bool RegisterMarker(
		const AActor& View,
		UPlayerWeaponMarkerComponent& Marker,
		TSet<EPlayerWeaponMarkerKind>& SeenKinds,
		TMap<EPlayerWeaponMovingPart, TWeakObjectPtr<UPlayerWeaponMarkerComponent>>&
			MovingPartMarkers)
	{
		const EPlayerWeaponMarkerKind Kind = Marker.GetMarkerKind();
		if (Kind == EPlayerWeaponMarkerKind::MovingPart)
		{
			const EPlayerWeaponMovingPart Part = Marker.GetMovingPart();
			if (!MovingPartMarkers.Contains(Part))
			{
				MovingPartMarkers.Add(Part, &Marker);
				return true;
			}
			UE_LOG(LogExtraction, Warning,
				TEXT("%s has duplicate moving-part marker value %d."),
				*GetNameSafe(&View), static_cast<int32>(Part));
			return false;
		}
		if (!SeenKinds.Contains(Kind))
		{
			SeenKinds.Add(Kind);
			return true;
		}
		UE_LOG(LogExtraction, Warning,
			TEXT("%s has duplicate marker kind %d."),
			*GetNameSafe(&View), static_cast<int32>(Kind));
		return false;
	}

	bool BuildMarkerCache(
		AActor& View,
		TMap<EPlayerWeaponMovingPart, TWeakObjectPtr<UPlayerWeaponMarkerComponent>>&
			MovingPartMarkers,
		int32& OutMarkerCount)
	{
		TInlineComponentArray<UPlayerWeaponMarkerComponent*, 16> Markers;
		View.GetComponents(Markers);
		TSet<EPlayerWeaponMarkerKind> SeenKinds;
		SeenKinds.Reserve(Markers.Num());
		MovingPartMarkers.Reserve(Markers.Num());

		for (UPlayerWeaponMarkerComponent* Marker : Markers)
			if (!IsMarkerValueValid(View, Marker)
				|| !RegisterMarker(View, *Marker, SeenKinds, MovingPartMarkers))
				return false;
		OutMarkerCount = Markers.Num();
		return true;
	}

	bool HasRequiredMarker(
		const AActor& View,
		const UPlayerWeaponMarkerComponent* Marker,
		EPlayerWeaponMarkerKind ExpectedKind,
		const TCHAR* MarkerName)
	{
		if (IsValid(Marker) && Marker->GetMarkerKind() == ExpectedKind)
			return true;
		UE_LOG(LogExtraction, Warning,
			TEXT("%s has an invalid required %s marker."),
			*GetNameSafe(&View), MarkerName);
		return false;
	}

	bool HasRequiredWeaponMarkers(const APlayerWeaponView& View)
	{
		return HasRequiredMarker(View, View.GetWeaponSeatMarker(),
			EPlayerWeaponMarkerKind::WeaponSeat, TEXT("WeaponSeat"))
			&& HasRequiredMarker(View, View.GetSupportHandTargetMarker(),
				EPlayerWeaponMarkerKind::SupportHandTarget, TEXT("SupportHandTarget"))
			&& HasRequiredMarker(View, View.GetSupportHandHintMarker(),
				EPlayerWeaponMarkerKind::SupportHandHint, TEXT("SupportHandHint"))
			&& HasRequiredMarker(View, View.GetIronRearMarker(),
				EPlayerWeaponMarkerKind::IronRear, TEXT("IronRear"))
			&& HasRequiredMarker(View, View.GetIronFrontMarker(),
				EPlayerWeaponMarkerKind::IronFront, TEXT("IronFront"))
			&& HasRequiredMarker(View, View.GetOpticMountMarker(),
				EPlayerWeaponMarkerKind::OpticMount, TEXT("OpticMount"))
			&& HasRequiredMarker(View, View.GetMuzzleMarker(),
				EPlayerWeaponMarkerKind::Muzzle, TEXT("Muzzle"))
			&& HasRequiredMarker(View, View.GetCasingMarker(),
				EPlayerWeaponMarkerKind::Casing, TEXT("Casing"));
	}

	bool HasRequiredAttachmentMarkers(const APlayerWeaponAttachmentView& View)
	{
		return HasRequiredMarker(View, View.GetAttachmentMountMarker(),
			EPlayerWeaponMarkerKind::AttachmentMount, TEXT("AttachmentMount"))
			&& HasRequiredMarker(View, View.GetAimPointMarker(),
				EPlayerWeaponMarkerKind::AimPoint, TEXT("AimPoint"))
			&& HasRequiredMarker(View, View.GetSupportHandTargetMarker(),
				EPlayerWeaponMarkerKind::SupportHandTarget, TEXT("SupportHandTarget"))
			&& HasRequiredMarker(View, View.GetSupportHandHintMarker(),
				EPlayerWeaponMarkerKind::SupportHandHint, TEXT("SupportHandHint"));
	}
}

UPlayerWeaponMarkerComponent::UPlayerWeaponMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(false);
}

void UPlayerWeaponMarkerComponent::ConfigureMarker(
	EPlayerWeaponMarkerKind InKind, EPlayerWeaponMovingPart InMovingPart)
{
	MarkerKind = InKind;
	MovingPart = InMovingPart;
}

FVector UPlayerWeaponMarkerComponent::GetForwardAxis() const
{
	return GetComponentQuat().GetAxisX().GetSafeNormal();
}

FVector UPlayerWeaponMarkerComponent::GetRightAxis() const
{
	return GetComponentQuat().GetAxisY().GetSafeNormal();
}

FVector UPlayerWeaponMarkerComponent::GetUpAxis() const
{
	return GetComponentQuat().GetAxisZ().GetSafeNormal();
}

APlayerWeaponView::APlayerWeaponView()
{
	ConfigurePassiveView(*this);
	ViewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot"));
	SetRootComponent(ViewRoot);
	ArtRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArtRoot"));
	ArtRoot->SetupAttachment(ViewRoot);

	WeaponSeatMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("WeaponSeat"));
	WeaponSeatMarker->SetupAttachment(ViewRoot);
	WeaponSeatMarker->ConfigureMarker(EPlayerWeaponMarkerKind::WeaponSeat);
	SupportHandTargetMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("SupportHandTarget"));
	SupportHandTargetMarker->SetupAttachment(ViewRoot);
	SupportHandTargetMarker->ConfigureMarker(EPlayerWeaponMarkerKind::SupportHandTarget);
	SupportHandHintMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("SupportHandHint"));
	SupportHandHintMarker->SetupAttachment(ViewRoot);
	SupportHandHintMarker->ConfigureMarker(EPlayerWeaponMarkerKind::SupportHandHint);
	IronRearMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("IronRear"));
	IronRearMarker->SetupAttachment(ViewRoot);
	IronRearMarker->ConfigureMarker(EPlayerWeaponMarkerKind::IronRear);
	IronFrontMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("IronFront"));
	IronFrontMarker->SetupAttachment(ViewRoot);
	IronFrontMarker->ConfigureMarker(EPlayerWeaponMarkerKind::IronFront);
	OpticMountMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("OpticMount"));
	OpticMountMarker->SetupAttachment(ViewRoot);
	OpticMountMarker->ConfigureMarker(EPlayerWeaponMarkerKind::OpticMount);
	MuzzleMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("Muzzle"));
	MuzzleMarker->SetupAttachment(ViewRoot);
	MuzzleMarker->ConfigureMarker(EPlayerWeaponMarkerKind::Muzzle);
	CasingMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("Casing"));
	CasingMarker->SetupAttachment(ViewRoot);
	CasingMarker->ConfigureMarker(EPlayerWeaponMarkerKind::Casing);
}

void APlayerWeaponView::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitializeView();
}

void APlayerWeaponView::PreInitializeComponents()
{
	ConfigurePassiveView(*this);
	Super::PreInitializeComponents();
}

void APlayerWeaponView::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	InitializeView();
}

void APlayerWeaponView::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseView();
	Super::EndPlay(EndPlayReason);
}

bool APlayerWeaponView::InitializeView()
{
	ConfigurePassiveView(*this);
	ReleaseView();
	if (!IsValid(ArtRoot)) return false;
	if (!ConfigureComposedPrimitives(*this, *ArtRoot)
		|| !BuildMarkerCache(*this, MovingPartMarkers, RegisteredMarkerCount)
		|| !HasRequiredWeaponMarkers(*this))
	{
		ReleaseView();
		return false;
	}
	bViewInitialized = true;
	return true;
}

void APlayerWeaponView::ReleaseView()
{
	MovingPartMarkers.Reset();
	bViewInitialized = false;
	RegisteredMarkerCount = 0;
}

UPlayerWeaponMarkerComponent* APlayerWeaponView::GetMovingPartMarker(
	EPlayerWeaponMovingPart Part) const
{
	const TWeakObjectPtr<UPlayerWeaponMarkerComponent>* Marker =
		MovingPartMarkers.Find(Part);
	return Marker ? Marker->Get() : nullptr;
}

APlayerWeaponAttachmentView::APlayerWeaponAttachmentView()
{
	ConfigurePassiveView(*this);
	ViewRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot"));
	SetRootComponent(ViewRoot);
	ArtRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArtRoot"));
	ArtRoot->SetupAttachment(ViewRoot);

	AttachmentMountMarker =
		CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("AttachmentMount"));
	AttachmentMountMarker->SetupAttachment(ViewRoot);
	AttachmentMountMarker->ConfigureMarker(EPlayerWeaponMarkerKind::AttachmentMount);
	AimPointMarker = CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("AimPoint"));
	AimPointMarker->SetupAttachment(ViewRoot);
	AimPointMarker->ConfigureMarker(EPlayerWeaponMarkerKind::AimPoint);
	SupportHandTargetMarker =
		CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("SupportHandTarget"));
	SupportHandTargetMarker->SetupAttachment(ViewRoot);
	SupportHandTargetMarker->ConfigureMarker(EPlayerWeaponMarkerKind::SupportHandTarget);
	SupportHandHintMarker =
		CreateDefaultSubobject<UPlayerWeaponMarkerComponent>(TEXT("SupportHandHint"));
	SupportHandHintMarker->SetupAttachment(ViewRoot);
	SupportHandHintMarker->ConfigureMarker(EPlayerWeaponMarkerKind::SupportHandHint);
}

void APlayerWeaponAttachmentView::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitializeView();
}

void APlayerWeaponAttachmentView::PreInitializeComponents()
{
	ConfigurePassiveView(*this);
	Super::PreInitializeComponents();
}

void APlayerWeaponAttachmentView::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	InitializeView();
}

void APlayerWeaponAttachmentView::EndPlay(
	const EEndPlayReason::Type EndPlayReason)
{
	ReleaseView();
	Super::EndPlay(EndPlayReason);
}

bool APlayerWeaponAttachmentView::InitializeView()
{
	ConfigurePassiveView(*this);
	ReleaseView();
	if (!IsValid(ArtRoot)) return false;
	if (!ConfigureComposedPrimitives(*this, *ArtRoot)
		|| !BuildMarkerCache(*this, MovingPartMarkers, RegisteredMarkerCount)
		|| !HasRequiredAttachmentMarkers(*this))
	{
		ReleaseView();
		return false;
	}
	bViewInitialized = true;
	return true;
}

void APlayerWeaponAttachmentView::ReleaseView()
{
	MovingPartMarkers.Reset();
	bViewInitialized = false;
	RegisteredMarkerCount = 0;
}
