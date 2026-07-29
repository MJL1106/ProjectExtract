// AInteractionVolume implementation.

#include "World/InteractionVolume.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "World/InteractionEventSubsystem.h"

DEFINE_LOG_CATEGORY(LogInteractionVolume);

namespace
{
	/** Default half-extents (cm) — roughly a doorway panel, big enough to aim at from a stride away
	 *  without the designer resizing every placement. */
	constexpr float DefaultBoxExtent = 60.f;
}

AInteractionVolume::AInteractionVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	SetRootComponent(InteractionBox);
	InteractionBox->SetBoxExtent(FVector(DefaultBoxExtent));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionObjectType(ECC_WorldStatic);

	// Interact channel ONLY. Blocking ECC_Visibility here would also block enemy line-of-sight,
	// companion muzzle clearance and cover generation — every one of those traces Visibility, and an
	// invisible box draped over a console would silently break all three.
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ExtractionInteraction::InteractTraceChannel, ECR_Block);
	InteractionBox->SetGenerateOverlapEvents(false);
	InteractionBox->SetCanEverAffectNavigation(false);
	InteractionBox->ShapeColor = FColor::Cyan;
}

void AInteractionVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	// Seeded HERE, not in BeginPlay. An objective chain's entry step activates from its own BeginPlay
	// and can switch this volume on through an ActivateActor effect, and actor BeginPlay order is
	// unspecified — seeding that late would quietly stamp the authored default back over it.
	bInteractionEnabled = bStartsEnabled;
	ApplyCollisionState();
}

void AInteractionVolume::BeginPlay()
{
	Super::BeginPlay();

#if WITH_EDITOR
	ValidateConfig();
#endif
}

// ------------------------------------------------------------------
// IWorldInteractable
// ------------------------------------------------------------------

bool AInteractionVolume::CanWorldInteract_Implementation(AActor* /*Interactor*/) const
{
	return CanBeUsed();
}

FText AInteractionVolume::GetWorldInteractionPrompt_Implementation(AActor* /*Interactor*/) const
{
	return InteractionPrompt;
}

float AInteractionVolume::GetWorldInteractHoldSeconds_Implementation(AActor* /*Interactor*/) const
{
	return FMath::Max(HoldSeconds, 0.f);
}

void AInteractionVolume::WorldInteract_Implementation(AActor* Interactor)
{
	// Re-checked here as well as by the caller: the player re-reads CanWorldInteract every frame of a
	// hold, but a Blueprint or a second interactor can reach this directly.
	if (!CanBeUsed())
	{
		UE_LOG(LogInteractionVolume, Verbose, TEXT("%s: refused interaction by %s (enabled=%d used=%d)"),
			*GetName(), *GetNameSafe(Interactor), bInteractionEnabled, bUsed);
		return;
	}

	// Spent volumes go dark immediately rather than waiting for a beat to switch them off — the
	// player holding the key down must not be able to fire a second use into the same frame.
	if (bSingleUse)
	{
		bUsed = true;
		SetInteractionEnabled(false);
	}

	UE_LOG(LogInteractionVolume, Log, TEXT("%s: used by %s"), *GetName(), *GetNameSafe(Interactor));

	// Raised LAST, from the success branch only. WorldInteract has no return value and implementers
	// routinely accept the call and refuse the action, so a listener watching "the player used THAT
	// actor" must never hear about a refused press.
	if (UInteractionEventSubsystem* Events = GetWorld() ? GetWorld()->GetSubsystem<UInteractionEventSubsystem>() : nullptr)
		Events->NotifyWorldInteract(this, Interactor);
}

// ------------------------------------------------------------------
// Enable / disable
// ------------------------------------------------------------------

void AInteractionVolume::SetInteractionEnabled(bool bNewEnabled)
{
	if (bInteractionEnabled == bNewEnabled) return;

	bInteractionEnabled = bNewEnabled;
	ApplyCollisionState();

	UE_LOG(LogInteractionVolume, Log, TEXT("%s: interaction %s"),
		*GetName(), bNewEnabled ? TEXT("enabled") : TEXT("disabled"));
}

void AInteractionVolume::ApplyCollisionState()
{
	if (!IsValid(InteractionBox)) return;
	InteractionBox->SetCollisionEnabled(CanBeUsed() ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

// ------------------------------------------------------------------
// Validation
// ------------------------------------------------------------------

#if WITH_EDITOR
void AInteractionVolume::ValidateConfig() const
{
	if (InteractionPrompt.IsEmpty())
		UE_LOG(LogInteractionVolume, Warning,
			TEXT("%s: InteractionPrompt is empty — the player gets a hold with no idea what they are doing"),
			*GetName());
}
#endif
