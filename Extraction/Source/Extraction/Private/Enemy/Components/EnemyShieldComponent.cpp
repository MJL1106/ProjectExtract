// UEnemyShieldComponent — breakable frontal static-mesh shield for the Shield archetype.

#include "EnemyShieldComponent.h"
#include "EnemyAIController.h"
#include "EnemyArchetypeData.h"
#include "Engine/AssetManager.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"

UEnemyShieldComponent::UEnemyShieldComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Shield mesh must block Visibility so weapon traces hit this component directly.
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	SetCollisionResponseToAllChannels(ECR_Block);
	SetCollisionObjectType(ECC_WorldDynamic);
}

void UEnemyShieldComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (MeshLoadHandle.IsValid())
	{
		MeshLoadHandle->CancelHandle();
		MeshLoadHandle.Reset();
	}

	Super::EndPlay(EndPlayReason);
}

void UEnemyShieldComponent::InitFromArchetype(const UEnemyArchetypeData* Data)
{
	if (!Data) return;

	ShieldMaxHealth = Data->ShieldHealth;
	ShieldCurrentHealth = ShieldMaxHealth;
	bInitialized = true;

	// Null ShieldMesh → warn and immediately mark broken so the enemy falls back to grunt behaviour.
	if (Data->ShieldMesh.IsNull())
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("%s: ShieldMesh not assigned in ArchetypeData — shield disabled."), *GetName());
		bShieldBroken = true;
		SetHiddenInGame(true);
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	bShieldBroken = false;

	// If the asset is already in memory, apply it immediately; otherwise kick off an async load
	// so we never hitch the game thread during possession.
	if (Data->ShieldMesh.IsValid())
	{
		SetStaticMesh(Data->ShieldMesh.Get());
		SetHiddenInGame(false);
		SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		return;
	}

	// Mesh not yet in memory — collision is enabled now but the static mesh shape isn't present yet,
	// so weapon traces won't hit this component until OnMeshLoaded sets the mesh. Known gap: the
	// shield provides no physical protection during the async load window. Mesh visual and hit
	// detection both arrive together when the load completes.
	SetHiddenInGame(false);
	SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	TSoftObjectPtr<UStaticMesh> SoftMesh = Data->ShieldMesh;
	TWeakObjectPtr<UEnemyShieldComponent> WeakThis(this);

	MeshLoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		SoftMesh.ToSoftObjectPath(),
		[WeakThis, SoftMesh]()
		{
			UEnemyShieldComponent* Shield = WeakThis.Get();
			if (!IsValid(Shield)) return;
			Shield->OnMeshLoaded(SoftMesh);
		});
}

void UEnemyShieldComponent::OnMeshLoaded(TSoftObjectPtr<UStaticMesh> SoftMesh)
{
	UStaticMesh* Mesh = SoftMesh.Get();
	if (!IsValid(Mesh))
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("%s: ShieldMesh async load failed — shield disabled."), *GetName());
		bShieldBroken = true;
		SetHiddenInGame(true);
		SetCollisionEnabled(ECollisionEnabled::NoCollision);
		return;
	}

	// Only apply the mesh if the shield hasn't already been broken while the load was in flight.
	if (!bShieldBroken)
		SetStaticMesh(Mesh);

	MeshLoadHandle.Reset();
}

float UEnemyShieldComponent::ProcessIncomingDamage(float Damage, const FDamageEvent& DamageEvent, AActor* DamageCauser)
{
	if (!bInitialized || bShieldBroken) return Damage;

	// Takedown (generic FDamageEvent — not point, not radial) passes through untouched.
	const bool bIsPoint = DamageEvent.IsOfType(FPointDamageEvent::ClassID);
	const bool bIsRadial = DamageEvent.IsOfType(FRadialDamageEvent::ClassID);

	if (!bIsPoint && !bIsRadial) return Damage;

	if (bIsPoint)
	{
		const FPointDamageEvent& PointDmg = static_cast<const FPointDamageEvent&>(DamageEvent);

		// Hit this component directly → fully absorbed into shield HP.
		if (PointDmg.HitInfo.Component.Get() == this)
		{
			ApplyDamageToShield(Damage);
			return 0.f;
		}

		// Point damage that hit a different component passes through; shield not in the way.
		return Damage;
	}

	// Radial damage (grenade): damage shield heavily AND pass original damage through.
	ApplyDamageToShield(Damage * RadialShieldDamageMultiplier);
	return Damage;
}

void UEnemyShieldComponent::ApplyDamageToShield(float Damage)
{
	if (bShieldBroken) return;

	ShieldCurrentHealth = FMath::Max(0.f, ShieldCurrentHealth - Damage);
	if (ShieldCurrentHealth <= 0.f)
		BreakShield();
}

void UEnemyShieldComponent::BreakShield()
{
	if (bShieldBroken) return;

	bShieldBroken = true;
	SetHiddenInGame(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OnShieldBroken.Broadcast();
}
