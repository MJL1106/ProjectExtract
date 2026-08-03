//// Copyright, (c) Sami Kangasmaa 2023

#include "ManualCoverPoint.h"
#include "CoverSystem.h"

AManualCoverPoint::AManualCoverPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComp"));
	RootComponent = RootSceneComponent;
	RootSceneComponent->Mobility = EComponentMobility::Static;

#if WITH_EDITOR
	PreviewCapsule = CreateEditorOnlyDefaultSubobject<UCapsuleComponent>(TEXT("PreviewCapsuleComp"));
	if (PreviewCapsule)
	{
		PreviewCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewCapsule->ShapeColor = FColor::Blue;
		PreviewCapsule->Mobility = EComponentMobility::Static;
		PreviewCapsule->InitCapsuleSize(12.0f, 88.0f);
		PreviewCapsule->SetupAttachment(RootSceneComponent);
		PreviewCapsule->SetRelativeLocation(FVector(0.0f, 0.0f, 88.0f));
	}

	PreviewArrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("PreviewArrowComp"));
	if (PreviewArrow)
	{
		PreviewArrow->ArrowColor = FColor::Blue;
		PreviewArrow->SetupAttachment(PreviewCapsule);
	}
#endif
}

void AManualCoverPoint::BeginPlay()
{
	Super::BeginPlay();

	// Cache cover data
	CachedCoverData = BakeCoverData();
	bHasCachedCoverData = true;

	RegisterToCoverSystem();
}

void AManualCoverPoint::EndPlay(EEndPlayReason::Type Reason)
{
	UnregisterFromCoverSystem();

	Super::EndPlay(Reason);
}

FCoverData AManualCoverPoint::GetCoverData() const
{
	if (bHasCachedCoverData)
	{
		return CachedCoverData;
	}
	else
	{
		return BakeCoverData();
	}
}

void AManualCoverPoint::RegisterToCoverSystem()
{
	if (bIsRegistered)
	{
		return;
	}
	if (ACoverSystem* CoverSystem = ACoverSystem::GetCoverSystem(this))
	{
		CoverSystem->RegisterManualCoverPoint(this);
		bIsRegistered = true;
	}
}

void AManualCoverPoint::UnregisterFromCoverSystem()
{
	if (!bIsRegistered)
	{
		return;
	}
	if (ACoverSystem* CoverSystem = ACoverSystem::GetCoverSystem(this))
	{
		CoverSystem->UnregisterManualCoverPoint(this);
	}
	bIsRegistered = false;
}


FCoverData AManualCoverPoint::BakeCoverData() const
{
	FCoverData Data;

	Data.Location = GetActorLocation();
	Data.SetDirectionToWall(GetActorQuat().Vector());

	Data.bCrouchedCover = bLeftCoverCrouched || bRightCoverCrouched || bFrontCoverCrouched;
	Data.bFrontCoverCrouched = bFrontCoverCrouched;
	Data.bLeftCoverCrouched = bLeftCoverCrouched;
	Data.bRightCoverCrouched = bRightCoverCrouched;
	Data.bLeftCoverStanding = bLeftCoverStanding;
	Data.bRightCoverStanding = bRightCoverStanding;
	
	return Data;
}