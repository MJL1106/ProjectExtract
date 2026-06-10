// UEnemyArmourComponent — directional damage reduction for the Heavy archetype.

#include "EnemyArmourComponent.h"
#include "EnemyArchetypeData.h"
#include "EnemyCharacter.h"
#include "HealthComponent.h"
#include "Engine/DamageEvents.h"

UEnemyArmourComponent::UEnemyArmourComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnemyArmourComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UEnemyArmourComponent::InitFromArchetype(const UEnemyArchetypeData* Data)
{
	if (!Data) return;

	FrontalMultiplier = Data->ArmourFrontalMultiplier;
	RearMultiplier = Data->ArmourRearMultiplier;
	TotalPlates = FMath::Max(1, Data->ArmourPlateCount);
	PlatesRemaining = TotalPlates;

	// Half-angle cosine for frontal arc check: hit origin must be within this arc off the owner's forward.
	FrontalArcHalfCosine = FMath::Cos(FMath::DegreesToRadians(Data->ArmourFrontalArcDeg * 0.5f));

	// Plate break threshold = owner max health / plate count.
	// Use the owning character's health component if available.
	const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
	if (IsValid(Enemy))
	{
		const UHealthComponent* HC = Enemy->GetHealthComponent();
		if (IsValid(HC))
			OwnerMaxHealth = HC->GetMaxHealth();
	}

	PlateBreakThreshold = (TotalPlates > 0) ? (OwnerMaxHealth / static_cast<float>(TotalPlates)) : OwnerMaxHealth;
	FrontalDamageAccumulated = 0.f;
	bInitialized = true;
}

float UEnemyArmourComponent::ModifyIncomingDamage(float Damage, const FDamageEvent& DamageEvent, AActor* DamageCauser)
{
	if (!bInitialized || Damage <= 0.f) return Damage;

	// Head region hits bypass frontal reduction — full damage + any hitbox bonus already applied upstream.
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const AEnemyCharacter* Enemy = Cast<AEnemyCharacter>(GetOwner());
		if (IsValid(Enemy) && Enemy->ResolveHitRegion(DamageEvent) == EHitRegion::Head)
			return Damage;
	}

	bool bDirectionResolved = false;
	const bool bFrontal = IsFrontalHit(DamageEvent, DamageCauser, bDirectionResolved);

	// Direction unresolvable (no causer, invalid owner) — no reduction, no rear bonus.
	if (!bDirectionResolved) return Damage;

	if (bFrontal && PlatesRemaining > 0)
	{
		const float ModifiedDamage = Damage * FrontalMultiplier;
		FrontalDamageAccumulated += Damage; // accumulate raw damage against plate threshold

		// Check if the accumulated raw damage crosses the next plate break threshold.
		const float ThresholdForNextBreak = PlateBreakThreshold * static_cast<float>(TotalPlates - PlatesRemaining + 1);
		if (FrontalDamageAccumulated >= ThresholdForNextBreak)
		{
			PlatesRemaining = FMath::Max(0, PlatesRemaining - 1);
			OnPlateBroken.Broadcast(PlatesRemaining);
		}

		// All plates gone — frontal multiplier degrades toward 1.0.
		if (PlatesRemaining <= 0)
			return Damage; // full damage once plates are broken

		return ModifiedDamage;
	}

	// Confirmed rear or side hit.
	return Damage * RearMultiplier;
}

bool UEnemyArmourComponent::IsFrontalHit(const FDamageEvent& DamageEvent, AActor* DamageCauser, bool& bOutResolved) const
{
	bOutResolved = false;

	const AActor* Owner = GetOwner();
	if (!IsValid(Owner)) return false;

	// Determine the direction the shot came *from* relative to the owner.
	// For point damage, ShotDirection is the bullet travel direction (toward the target).
	// Negating it gives the direction back toward the attacker.
	// For radial/fallback, use the vector from owner to causer.
	FVector FromAttacker = FVector::ZeroVector;

	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& PointDmg = static_cast<const FPointDamageEvent&>(DamageEvent);
		FromAttacker = -PointDmg.ShotDirection;
	}
	else if (IsValid(DamageCauser))
	{
		FromAttacker = (DamageCauser->GetActorLocation() - Owner->GetActorLocation()).GetSafeNormal2D();
	}
	else
	{
		return false; // bOutResolved stays false — caller applies neutral damage
	}

	bOutResolved = true;
	const FVector FromAttackerFlat = FromAttacker.GetSafeNormal2D();
	const float Dot = FVector::DotProduct(Owner->GetActorForwardVector(), FromAttackerFlat);

	// Frontal if the attacker is within the frontal half-arc ahead of the owner.
	return Dot >= FrontalArcHalfCosine;
}
