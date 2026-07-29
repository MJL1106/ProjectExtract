// ULootMarkerComponent implementation — availability polling and visibility gating.

#include "UI/LootMarkerComponent.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "World/Lootable.h"
#include "World/WorldInteractable.h"

DEFINE_LOG_CATEGORY_STATIC(LogLootMarker, Log, All);

ULootMarkerComponent::ULootMarkerComponent()
{
	SetWidgetSpace(EWidgetSpace::Screen);
	SetDrawSize(FVector2D(32.f, 32.f));
	SetPivot(FVector2D(0.5f, 0.5f));
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetTwoSided(false);
	SetVisibility(false);
	SetUsingAbsoluteScale(true);
}

void ULootMarkerComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!GetWidgetClass())
	{
		UE_LOG(LogLootMarker, Warning, TEXT("ULootMarkerComponent on %s: WidgetClass is null -- "
			"marker will be invisible. Assign it in the BP subclass defaults."), *GetNameSafe(GetOwner()));
	}

	// Compute relative Z so the world-space offset equals MarkerWorldZOffset regardless of
	// the attach parent's scale. Preserves any designer-authored X/Y nudge.
	const FVector Rel = GetRelativeLocation();
	if (const USceneComponent* ParentComponent = GetAttachParent())
	{
		constexpr float MinParentScale = 0.01f;
		const float RawScaleZ = ParentComponent->GetComponentScale().Z;
		const float SignZ = (RawScaleZ >= 0.f) ? 1.f : -1.f;
		const float SafeScaleZ = SignZ * FMath::Max(FMath::Abs(RawScaleZ), MinParentScale);
		SetRelativeLocation(FVector(Rel.X, Rel.Y, MarkerWorldZOffset / SafeScaleZ));
	}
	else
	{
		SetRelativeLocation(FVector(Rel.X, Rel.Y, MarkerWorldZOffset));
	}

	// Poll availability on a looping timer rather than every tick.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			AvailabilityTimerHandle,
			this,
			&ULootMarkerComponent::RefreshAvailability,
			AvailabilityPollInterval,
			/*bLoop=*/ true);
	}

	// Resolve once immediately so the first frame is correct.
	RefreshAvailability();
}

void ULootMarkerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AvailabilityTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void ULootMarkerComponent::SetMarkerAvailable(bool bAvailable)
{
	bMarkerAvailable = bAvailable;
	RefreshAvailability();
}

void ULootMarkerComponent::RefreshMarkerNow()
{
	RefreshAvailability();
}

void ULootMarkerComponent::RefreshAvailability()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return;

	const bool bAvailable = ResolveAvailability(Owner);
	const bool bWasVisible = IsVisible();

	// Reset parent's stale occlusion state on hidden-to-visible so the first visible
	// tick traces immediately and does not pop or draw through walls.
	if (bAvailable && !bWasVisible)
	{
		ResetOcclusionState();
	}

	SetVisibility(bAvailable);
}

bool ULootMarkerComponent::ResolveAvailability(AActor* Owner) const
{
	// Distance gate first — cheap rejection before interface queries.
	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!IsValid(PlayerPawn))
	{
		return false;
	}

	const float DistSq = FVector::DistSquared(GetComponentLocation(), PlayerPawn->GetActorLocation());
	if (DistSq > FMath::Square(MaxVisibleDistance))
	{
		return false;
	}

	// Interface-based availability, AND'd with the manual toggle.
	if (Owner->GetClass()->ImplementsInterface(ULootable::StaticClass()))
	{
		return bMarkerAvailable && ILootable::Execute_CanLoot(Owner);
	}

	if (Owner->GetClass()->ImplementsInterface(UWorldInteractable::StaticClass()))
	{
		return bMarkerAvailable && IWorldInteractable::Execute_CanWorldInteract(Owner, PlayerPawn);
	}

	return bMarkerAvailable;
}
