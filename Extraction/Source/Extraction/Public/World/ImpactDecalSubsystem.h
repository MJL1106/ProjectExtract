// UImpactDecalSubsystem — pooled bullet-hole decals. Weapons fire >10 shots/sec, so decal
// components are a fixed ring buffer (oldest hole recycled at the cap) instead of per-shot spawns.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ImpactDecalSubsystem.generated.h"

class UDecalComponent;
class UMaterialInterface;

UCLASS()
class EXTRACTION_API UImpactDecalSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Stamp a bullet-hole decal at Hit's impact point, oriented into the surface, attached to the
	 *  hit component so holes ride moving geometry (doors). Size gets slight per-hit jitter and a
	 *  random roll so repeated holes don't tile. Lifetime is a soft fade — the ring recycles the
	 *  oldest hole regardless once the cap is reached. */
	void SpawnBulletHole(UMaterialInterface* DecalMaterial, const FHitResult& Hit, float Size, float Lifetime);

private:
	/** Ring-buffer cap: the maximum bullet holes alive in the world at once. */
	static constexpr int32 MaxPooledDecals = 64;

	/** Seconds the soft-lifetime fade-out takes once it starts. */
	static constexpr float FadeOutDuration = 2.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UDecalComponent>> DecalPool;

	int32 NextDecalIndex = 0;

	/** Returns the ring slot's component, creating or recreating it when empty/stale (the hit
	 *  component a decal was attached to may have been destroyed, taking the decal with it). */
	UDecalComponent* AcquireDecal();
};
