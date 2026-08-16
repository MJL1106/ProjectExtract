// ADemoWarpCamRig — demo filming helper: one roaming camera cycled through placed warp points.

#include "World/DemoWarpCamRig.h"
#include "Camera/CameraActor.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"

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

	CurrentIndex = Index;
	RoamingCamera->SetActorLocationAndRotation(Point->GetActorLocation(), Point->GetActorRotation());

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		PC->SetViewTargetWithBlend(RoamingCamera, BlendTime);
}
