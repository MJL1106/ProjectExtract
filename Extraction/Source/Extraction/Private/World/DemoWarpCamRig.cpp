// ADemoWarpCamRig — demo filming helper: one roaming camera cycled through placed warp points.

#include "World/DemoWarpCamRig.h"
#include "Camera/CameraActor.h"
#include "Components/HealthComponent.h"
#include "Data/WeaponDataAsset.h"
#include "Enemy/EnemyCharacter.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Weapon/WeaponBase.h"

ADemoWarpCamRig::ADemoWarpCamRig()
{
	PrimaryActorTick.bCanEverTick = false;
}

void ADemoWarpCamRig::BeginPlay()
{
	Super::BeginPlay();

	APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
	if (!IsValid(PC)) return;

	// Rides the player's existing input stack — IMC_DemoCams is already active for the numbered
	// demo cams, so the assigned actions just need a component to bind on.
	EnableInput(PC);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!IsValid(EIC)) return;

	if (IsValid(NextAction))
		EIC->BindAction(NextAction, ETriggerEvent::Triggered, this, &ADemoWarpCamRig::WarpNext);
	if (IsValid(PrevAction))
		EIC->BindAction(PrevAction, ETriggerEvent::Triggered, this, &ADemoWarpCamRig::WarpPrev);
	// Triggered, same as the warp keys: the mappings carry explicit Pressed triggers, so Triggered
	// fires exactly once per press. (Started does NOT fire here — with an instant trigger the
	// None->Triggered transition emits a single Triggered event, which is why Started bindings
	// silently never ran.)
	if (IsValid(FireToggleAction))
		EIC->BindAction(FireToggleAction, ETriggerEvent::Triggered, this, &ADemoWarpCamRig::ToggleShowcaseFire);
	if (IsValid(ReloadAction))
		EIC->BindAction(ReloadAction, ETriggerEvent::Triggered, this, &ADemoWarpCamRig::ShowcaseReload);
}

AEnemyCharacter* ADemoWarpCamRig::FindShowcaseEnemy() const
{
	if (!IsValid(RoamingCamera)) return nullptr;

	const FVector CamLoc = RoamingCamera->GetActorLocation();
	AEnemyCharacter* Best = nullptr;
	float BestDistSq = FMath::Square(ShowcaseRadius);

	for (TActorIterator<AEnemyCharacter> It(GetWorld()); It; ++It)
	{
		AEnemyCharacter* Enemy = *It;
		const UHealthComponent* Health = IsValid(Enemy) ? Enemy->GetHealthComponent() : nullptr;
		if (!IsValid(Health) || Health->IsDead()) continue;

		const float DistSq = FVector::DistSquared(CamLoc, Enemy->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Enemy;
		}
	}
	return Best;
}

void ADemoWarpCamRig::ToggleShowcaseFire()
{
	if (ActiveShowcaseEnemy.IsValid())
	{
		StopShowcaseFire();
		return;
	}

	AEnemyCharacter* Enemy = FindShowcaseEnemy();
	AWeaponBase* Weapon = IsValid(Enemy) ? Enemy->GetCurrentWeapon() : nullptr;
	UE_LOG(LogTemp, Log, TEXT("[WARPRIG] fire-toggle: enemy=%s weapon=%s"),
		*GetNameSafe(Enemy), *GetNameSafe(Weapon));
	if (!IsValid(Weapon)) return;

	// Aim override down the enemy's own facing: the AnimBP raises the weapon into the combat aim
	// pose toward it (instead of idle hip fire), and PerformHitscan shoots at the same point —
	// pose and bullets agree by construction. Mute keeps neighbouring pens from hearing it.
	Enemy->SetAimLocationOverride(Enemy->GetPawnViewLocation() + Enemy->GetActorForwardVector() * 3000.f);
	Weapon->bDemoMuteNoise = true;
	ActiveShowcaseEnemy = Enemy;

	// ADS beat first, then sustained fire — the raise reads on camera before the muzzle lights up.
	GetWorldTimerManager().SetTimer(ShowcaseAimDelayHandle, this,
		&ADemoWarpCamRig::BeginShowcaseFire, FMath::Max(ShowcaseAimDelay, 0.01f), false);
}

void ADemoWarpCamRig::BeginShowcaseFire()
{
	AEnemyCharacter* Enemy = ActiveShowcaseEnemy.Get();
	AWeaponBase* Weapon = IsValid(Enemy) ? Enemy->GetCurrentWeapon() : nullptr;
	if (!IsValid(Weapon)) { StopShowcaseFire(); return; }

	Weapon->StartFiring();

	// Automatics stream on their own internal timer; semi-autos fire once per trigger press, so
	// re-pulse them at their own fire rate until toggled off.
	const UWeaponDataAsset* Data = Weapon->GetWeaponData();
	if (IsValid(Data) && !Data->bIsAutomatic)
	{
		const float Interval = FMath::Max(1.f / FMath::Max(Data->FireRate, 0.5f), 0.15f);
		GetWorldTimerManager().SetTimer(ShowcaseFirePulseHandle, this,
			&ADemoWarpCamRig::TickShowcaseFirePulse, Interval, true);
	}
}

void ADemoWarpCamRig::TickShowcaseFirePulse()
{
	AEnemyCharacter* Enemy = ActiveShowcaseEnemy.Get();
	AWeaponBase* Weapon = IsValid(Enemy) ? Enemy->GetCurrentWeapon() : nullptr;
	if (!IsValid(Weapon)) { StopShowcaseFire(); return; }

	// Release-and-squeeze: StartFiring early-outs while bWantsToFire is still set from the
	// previous pulse, so clear it first. CanFire gates the actual shot, so pulsing faster than
	// the refire/reload window is harmless.
	Weapon->StopFiring();
	Weapon->bDemoMuteNoise = true;
	Weapon->StartFiring();
}

void ADemoWarpCamRig::StopShowcaseFire()
{
	GetWorldTimerManager().ClearTimer(ShowcaseAimDelayHandle);
	GetWorldTimerManager().ClearTimer(ShowcaseFirePulseHandle);
	if (AEnemyCharacter* Enemy = ActiveShowcaseEnemy.Get())
	{
		Enemy->ClearAimLocationOverride();
		if (AWeaponBase* Weapon = Enemy->GetCurrentWeapon())
		{
			Weapon->StopFiring();
			Weapon->bDemoMuteNoise = false;
		}
	}
	ActiveShowcaseEnemy.Reset();
}

void ADemoWarpCamRig::ShowcaseReload()
{
	AEnemyCharacter* Enemy = ActiveShowcaseEnemy.Get();
	const bool bWasFiring = IsValid(Enemy);
	if (!bWasFiring)
		Enemy = FindShowcaseEnemy();

	AWeaponBase* Weapon = IsValid(Enemy) ? Enemy->GetCurrentWeapon() : nullptr;
	UE_LOG(LogTemp, Log, TEXT("[WARPRIG] reload: enemy=%s weapon=%s"),
		*GetNameSafe(Enemy), *GetNameSafe(Weapon));
	if (!IsValid(Weapon)) return;

	if (bWasFiring)
		StopShowcaseFire();

	Weapon->bDemoMuteNoise = true;
	if (Weapon->CanReload())
		Weapon->Reload();
}

void ADemoWarpCamRig::WarpNext()
{
	if (WarpPoints.Num() == 0) return;
	WarpToIndex((CurrentIndex + 1) % WarpPoints.Num());
}

void ADemoWarpCamRig::WarpPrev()
{
	if (WarpPoints.Num() == 0) return;
	WarpToIndex((CurrentIndex - 1 + WarpPoints.Num()) % WarpPoints.Num());
}

void ADemoWarpCamRig::WarpToIndex(int32 Index)
{
	if (!IsValid(RoamingCamera) || !WarpPoints.IsValidIndex(Index)) return;

	const AActor* Point = WarpPoints[Index];
	if (!IsValid(Point)) return;

	// Leaving a lane ends its demonstration — the previous enemy must not keep firing off-screen.
	StopShowcaseFire();

	CurrentIndex = Index;
	RoamingCamera->SetActorLocationAndRotation(Point->GetActorLocation(), Point->GetActorRotation());

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		PC->SetViewTargetWithBlend(RoamingCamera, BlendTime);
}
