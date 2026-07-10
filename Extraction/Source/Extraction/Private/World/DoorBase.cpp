// ADoorBase — shared AI auto-open trigger + generic breach geometry.

#include "World/DoorBase.h"
#include "World/DoorRegistrySubsystem.h"
#include "Companion/CompanionCharacter.h"
#include "Enemy/EnemyCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogDoorBase, Log, All);

ADoorBase::ADoorBase()
{
	PrimaryActorTick.bCanEverTick = false;

	// No SetupAttachment here — the root doesn't exist yet (subclasses create it). Each subclass
	// ctor MUST attach DoorwayTrigger to its root; an unattached scene component does NOT
	// auto-parent at registration and ends up stuck at the world origin (verified in-engine).
	DoorwayTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("DoorwayTrigger"));
	DoorwayTrigger->SetBoxExtent(FVector(150.f, 150.f, 110.f));
	DoorwayTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	DoorwayTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	DoorwayTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	DoorwayTrigger->SetGenerateOverlapEvents(true);
	DoorwayTrigger->SetCanEverAffectNavigation(false);
}

void ADoorBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Bind before BeginPlay so initial overlaps (an AI pawn spawned inside the trigger) still
	// fire. Bound unconditionally — bAutoOpenForAI is checked in the handler, so a runtime
	// flag flip works both ways.
	if (DoorwayTrigger)
		DoorwayTrigger->OnComponentBeginOverlap.AddDynamic(this, &ADoorBase::OnDoorwayOverlap);
}

void ADoorBase::BeginPlay()
{
	Super::BeginPlay();

	if (IsClosedAtBeginPlay())
		SnapshotClosedBounds();

	if (UDoorRegistrySubsystem* Registry = GetWorld()->GetSubsystem<UDoorRegistrySubsystem>())
		Registry->Register(this);
}

void ADoorBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
		if (UDoorRegistrySubsystem* Registry = World->GetSubsystem<UDoorRegistrySubsystem>())
			Registry->Unregister(this);

	Super::EndPlay(EndPlayReason);
}

FVector ADoorBase::GetAcousticPortalPoint() const
{
	FBox LocalBox(ForceInit);
	if (GetDoorLocalBounds(LocalBox))
		return GetActorTransform().TransformPosition(LocalBox.GetCenter());
	return GetActorLocation();
}

void ADoorBase::SnapshotClosedBounds()
{
	// Capture the mesh bounds while the door is closed — an open leaf makes the live assembly
	// roughly square in plan, flipping the thin-axis pick in the geometry queries.
	ClosedDoorLocalBounds.Init();
	const FTransform WorldToActor = GetActorTransform().Inverse();
	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->GetStaticMesh()) continue;
		ClosedDoorLocalBounds += Mesh->CalcBounds(Mesh->GetComponentTransform() * WorldToActor).GetBox();
	}
}

void ADoorBase::OnDoorwayOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	TryAutoOpenFor(OtherActor);
}

void ADoorBase::TryAutoOpenFor(AActor* OtherActor)
{
	if (!bAutoOpenForAI || bAutoOpenSuppressed) return;
	if (!Cast<AEnemyCharacter>(OtherActor) && !Cast<ACompanionCharacter>(OtherActor)) return;
	if (!Execute_CanBreach(this)) return;

	Execute_Breach(this, OtherActor);
}

void ADoorBase::RescanDoorwayForAutoOpen()
{
	if (!DoorwayTrigger) return;

	TArray<AActor*> Overlapping;
	DoorwayTrigger->GetOverlappingActors(Overlapping, APawn::StaticClass());
	for (AActor* Actor : Overlapping)
		TryAutoOpenFor(Actor);
}

// --- External gate ---

void ADoorBase::SetExternalGateLocked(bool bLocked)
{
	if (!HasAuthority()) return;
	bExternalGateLocked = bLocked;
}

void ADoorBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADoorBase, bExternalGateLocked);
}

// --- IBreachable defaults ---

void ADoorBase::Breach_Implementation(AActor* Breacher)
{
	UE_LOG(LogDoorBase, Warning, TEXT("%s: Breach reached ADoorBase — subclass forgot to override Breach."), *GetName());
}

bool ADoorBase::CanBreach_Implementation() const
{
	return false;
}

bool ADoorBase::GetBreachStandPoint_Implementation(const AActor* Breacher, FVector& OutLocation, FRotator& OutFacing) const
{
	FBox LocalBox(ForceInit);
	if (!IsValid(Breacher) || !GetDoorLocalBounds(LocalBox)) return false;

	// The door assembly is thin: its plane normal is whichever horizontal actor axis spans the
	// smaller extent. Measured in actor space so a yawed placement can't inflate the world AABB
	// and flip the axis pick.
	const FVector Center = GetActorTransform().TransformPosition(LocalBox.GetCenter());
	const FVector Extent = LocalBox.GetExtent();
	FVector Normal = (Extent.X <= Extent.Y) ? GetActorForwardVector() : GetActorRightVector();
	const float HalfThickness = FMath::Min(Extent.X, Extent.Y);

	// Flip the normal onto the breacher's side of the door.
	if (FVector::DotProduct(Breacher->GetActorLocation() - Center, Normal) < 0.f)
		Normal = -Normal;

	OutLocation = Center + Normal * (HalfThickness + GetBreacherCapsuleRadius(Breacher) + BreachReachGap);
	OutLocation.Z = Breacher->GetActorLocation().Z; // caller keeps its own floor height
	OutFacing = (-Normal).Rotation();
	return true;
}

bool ADoorBase::GetPostBreachPoint_Implementation(const AActor* Breacher, bool bEnterRoom, FVector& OutLocation) const
{
	FBox LocalBox(ForceInit);
	if (!IsValid(Breacher) || !GetDoorLocalBounds(LocalBox)) return false;

	const FVector Center = GetActorTransform().TransformPosition(LocalBox.GetCenter());
	const FVector Extent = LocalBox.GetExtent();
	FVector Normal = (Extent.X <= Extent.Y) ? GetActorForwardVector() : GetActorRightVector();
	if (FVector::DotProduct(Breacher->GetActorLocation() - Center, Normal) < 0.f)
		Normal = -Normal;

	// Generic door: where the leaf comes to rest is unknown here, so the sidestep direction is
	// arbitrary. Subclasses with real swing knowledge (ABreachableDoor) override this.
	const FVector Lateral = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();

	if (bEnterRoom)
		return PickNavigableEnterPoint(Center, Normal, Lateral, 1.f, Breacher->GetActorLocation().Z, OutLocation);

	const float HalfThickness = FMath::Min(Extent.X, Extent.Y);
	const float Standoff = HalfThickness + GetBreacherCapsuleRadius(Breacher) + BreachReachGap;
	OutLocation = Center + Normal * (Standoff + 40.f) + Lateral * PostBreachLateralOffset;
	OutLocation.Z = Breacher->GetActorLocation().Z;
	return true;
}

bool ADoorBase::PickNavigableEnterPoint(const FVector& Center, const FVector& Normal, const FVector& Lateral,
	float PreferredLateralSign, float BreacherZ, FVector& OutLocation) const
{
	const float Signs[3] = { PreferredLateralSign, -PreferredLateralSign, 0.f };
	const float LateralQueryExtent = 120.f;
	const float SameFloorQueryExtent = 150.f;   // capsule-center tolerance for "this floor"
	const float CrossFloorQueryExtent = 400.f;  // reaches a landing one flight up/down

	auto CandidateAt = [&](float Sign)
	{
		FVector Candidate = Center - Normal * PostBreachEnterDepth + Lateral * Sign * PostBreachLateralOffset;
		Candidate.Z = BreacherZ;
		return Candidate;
	};

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		// No nav system: hand back the raw preferred candidate and let the move request decide.
		OutLocation = CandidateAt(PreferredLateralSign);
		return true;
	}

	// Two passes: same-floor first, so a preferred candidate hanging over a stair void yields to
	// the mirrored landing on THIS floor before any candidate is allowed onto a flight above/below.
	const float VerticalExtents[2] = { SameFloorQueryExtent, CrossFloorQueryExtent };
	for (const float VerticalExtent : VerticalExtents)
	{
		for (const float Sign : Signs)
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(CandidateAt(Sign), Projected,
					FVector(LateralQueryExtent, LateralQueryExtent, VerticalExtent)))
			{
				OutLocation = Projected.Location;
				return true;
			}
		}
	}
	return false;
}

// --- Helpers ---

bool ADoorBase::GetDoorLocalBounds(FBox& OutLocalBox) const
{
	// Prefer the closed-state snapshot; live compute only covers pre-BeginPlay callers.
	if (ClosedDoorLocalBounds.IsValid)
	{
		OutLocalBox = ClosedDoorLocalBounds;
		return true;
	}

	OutLocalBox.Init();
	const FTransform WorldToActor = GetActorTransform().Inverse();

	TInlineComponentArray<UStaticMeshComponent*> Meshes(this);
	for (const UStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh || !Mesh->GetStaticMesh()) continue;
		OutLocalBox += Mesh->CalcBounds(Mesh->GetComponentTransform() * WorldToActor).GetBox();
	}
	return OutLocalBox.IsValid != 0;
}

float ADoorBase::GetBreacherCapsuleRadius(const AActor* Breacher)
{
	if (const ACharacter* Character = Cast<ACharacter>(Breacher))
		if (const UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			return Capsule->GetScaledCapsuleRadius();
	return 35.f;
}
