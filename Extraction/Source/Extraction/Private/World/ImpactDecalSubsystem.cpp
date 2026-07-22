#include "World/ImpactDecalSubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

UDecalComponent* UImpactDecalSubsystem::AcquireDecal()
{
	if (DecalPool.Num() < MaxPooledDecals)
	{
		if (DecalPool.Num() == 0) DecalPool.Reserve(MaxPooledDecals);
		UDecalComponent* Decal = NewObject<UDecalComponent>(this);
		Decal->SetFlags(RF_Transient);
		// Engine default (0.01) screen-size-culls an 8cm hole within a few metres — keep holes
		// visible to realistic engagement range, cull only once truly subpixel.
		Decal->SetFadeScreenSize(0.001f);
		Decal->RegisterComponentWithWorld(GetWorld());
		DecalPool.Add(Decal);
		return Decal;
	}

	const int32 Slot = NextDecalIndex;
	NextDecalIndex = (NextDecalIndex + 1) % MaxPooledDecals;

	UDecalComponent* Decal = DecalPool[Slot];
	// The component a decal was attached to may have been destroyed and taken the decal with it.
	if (!IsValid(Decal) || Decal->IsBeingDestroyed())
	{
		Decal = NewObject<UDecalComponent>(this);
		Decal->SetFlags(RF_Transient);
		Decal->SetFadeScreenSize(0.001f);
		Decal->RegisterComponentWithWorld(GetWorld());
		DecalPool[Slot] = Decal;
	}
	return Decal;
}

void UImpactDecalSubsystem::SpawnBulletHole(UMaterialInterface* DecalMaterial, const FHitResult& Hit, float Size, float Lifetime)
{
	if (!IsValid(DecalMaterial)) return;
	UPrimitiveComponent* HitComp = Hit.GetComponent();
	if (!IsValid(HitComp)) return;
	UWorld* World = GetWorld();
	if (!World) return;

	UDecalComponent* Decal = AcquireDecal();
	if (!IsValid(Decal)) return;

	// Decals project along -X: face the projector into the surface, then randomise roll so
	// repeated holes from the same material don't visibly tile.
	FRotator DecalRot = FRotationMatrix::MakeFromX(-Hit.ImpactNormal).Rotator();
	DecalRot.Roll = FMath::FRandRange(0.f, 360.f);

	const float JitteredSize = Size * FMath::FRandRange(0.85f, 1.15f);
	constexpr float ProjectionDepth = 4.f;

	Decal->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	Decal->SetDecalMaterial(DecalMaterial);
	Decal->DecalSize = FVector(ProjectionDepth, JitteredSize, JitteredSize);
	Decal->SetWorldLocationAndRotation(Hit.ImpactPoint, DecalRot);
	// Ride moving geometry (doors) instead of floating where the surface used to be.
	Decal->AttachToComponent(HitComp, FAttachmentTransformRules::KeepWorldTransform);
	// Soft lifetime: fade without destroying — the ring slot gets recycled, never deleted.
	Decal->SetFadeOut(Lifetime, FadeOutDuration, /*DestroyOwnerAfterFade*/ false);
	Decal->SetVisibility(true);
	Decal->MarkRenderStateDirty();
}
