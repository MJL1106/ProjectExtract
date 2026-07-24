// UFootstepNoiseComponent — distance-accumulating AI-noise emitter + surface-aware footstep audio.

#include "FootstepNoiseComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Audio/GameAudioSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "Character/ExtractionPlayerInterface.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"
#include "Engine/World.h"

UFootstepNoiseComponent::UFootstepNoiseComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFootstepNoiseComponent::BeginPlay()
{
	Super::BeginPlay();

	ACharacter* Owner = Cast<ACharacter>(GetOwner());
	if (!IsValid(Owner) || !Owner->HasAuthority()) return;

	UWorld* World = GetWorld();
	if (!IsValid(World)) return;

	LastLocation = Owner->GetActorLocation();
	Owner->LandedDelegate.AddDynamic(this, &UFootstepNoiseComponent::OnOwnerLanded);
	Owner->MovementModeChangedDelegate.AddDynamic(this, &UFootstepNoiseComponent::OnOwnerMovementModeChanged);

	const float InitialDelay = FMath::RandRange(0.f, UpdateInterval);
	World->GetTimerManager().SetTimer(
		NoiseTimerHandle,
		this,
		&UFootstepNoiseComponent::TickNoise,
		UpdateInterval,
		true,
		InitialDelay);
}

void UFootstepNoiseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
		World->GetTimerManager().ClearAllTimersForObject(this);

	if (ACharacter* Owner = Cast<ACharacter>(GetOwner()))
	{
		Owner->LandedDelegate.RemoveDynamic(this, &UFootstepNoiseComponent::OnOwnerLanded);
		Owner->MovementModeChangedDelegate.RemoveDynamic(this, &UFootstepNoiseComponent::OnOwnerMovementModeChanged);
	}

	if (IsValid(SprintRattleComp))
		SprintRattleComp->Stop();
	SprintRattleComp = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UFootstepNoiseComponent::TickNoise()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Extraction_Audio_FootstepPolling);

	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!IsValid(OwnerChar)) return;

	const FVector CurrentLocation = OwnerChar->GetActorLocation();
	const FVector Delta = CurrentLocation - LastLocation;
	LastLocation = CurrentLocation;

	const UCharacterMovementComponent* MoveComp = OwnerChar->GetCharacterMovement();
	if (!IsValid(MoveComp) || !MoveComp->IsMovingOnGround())
	{
		UpdateGearRattle(ERattleState::None);
		return;
	}

	// A delta larger than any single interval of walking = teleport (TeleportVolume), not footsteps.
	if (Delta.Size2D() > StepDistance * 2.f)
	{
		AccumulatedDistance = 0.f;
		AudioAccumulatedDistance = 0.f;
		return;
	}

	const EStepTier Tier = PickTier();
	const float Speed = OwnerChar->GetVelocity().Size2D();
	ERattleState DesiredRattle = ERattleState::None;
	if (Tier == EStepTier::Sprint) DesiredRattle = ERattleState::Sprint;
	else if (Tier == EStepTier::Quiet && Speed >= CrouchRattleMinSpeed) DesiredRattle = ERattleState::Crouch;
	UpdateGearRattle(DesiredRattle);

	AccumulatedDistance += Delta.Size2D();
	AudioAccumulatedDistance += Delta.Size2D();

	if (AccumulatedDistance >= StepDistance)
	{
		AccumulatedDistance = FMath::Fmod(AccumulatedDistance, StepDistance);
		if (bEmitAINoise)
			EmitAINoise(CurrentLocation, Tier);
	}

	// Anim-synced steps: fire exactly when a foot plants. The distance accumulator stays as a
	// backstop (widened while tracking is live) so movement never goes silent if plants stop
	// being detected (missing bones, URO-frozen offscreen NPC anim, prone crawl).
	if (TickFootPlants(*OwnerChar, Tier))
		AudioAccumulatedDistance = 0.f;

	const UWorld* World = GetWorld();
	const bool bFootTrackingLive = World && (World->GetTimeSeconds() - LastFootPlantTime) < 2.f;

	// Crouch/prone strides are shorter — the shared distance leaves visual steps silent.
	float StepAudioDistance = (Tier == EStepTier::Quiet) ? QuietAudioStepDistance : AudioStepDistance;
	if (bFootTrackingLive) StepAudioDistance *= BackstopDistanceMult;
	if (AudioAccumulatedDistance >= StepAudioDistance)
	{
		AudioAccumulatedDistance = FMath::Fmod(AudioAccumulatedDistance, StepAudioDistance);
		PlayFootstepAudio(Tier);
	}
}

bool UFootstepNoiseComponent::TickFootPlants(const ACharacter& OwnerChar, EStepTier Tier)
{
	const USkeletalMeshComponent* Mesh = OwnerChar.GetMesh();
	const UWorld* World = GetWorld();
	if (!IsValid(Mesh) || !World) return false;
	if (!Mesh->DoesSocketExist(LeftFootBone) || !Mesh->DoesSocketExist(RightFootBone)) return false;

	const float Now = World->GetTimeSeconds();
	const FName Bones[2] = { LeftFootBone, RightFootBone };
	bool bStepped = false;

	for (int32 Foot = 0; Foot < 2; ++Foot)
	{
		const FVector FootLocation = Mesh->GetSocketLocation(Bones[Foot]);

		if (!bFootLocationsValid)
		{
			LastFootLocation[Foot] = FootLocation;
			continue;
		}

		const float FootSpeed = (FootLocation - LastFootLocation[Foot]).Size() / UpdateInterval;
		LastFootLocation[Foot] = FootLocation;

		if (!bFootPlanted[Foot])
		{
			FootPeakSwingSpeed[Foot] = FMath::Max(FootPeakSwingSpeed[Foot], FootSpeed);

			if (FootSpeed <= FootPlantSpeedMax)
			{
				bFootPlanted[Foot] = true;

				// Only a real stride sounds: the foot must have genuinely swung since its last plant,
				// and the same foot can't retrigger inside half a fast cadence.
				if (FootPeakSwingSpeed[Foot] >= FootSwingSpeedMin
					&& Now - LastFootStepTime[Foot] >= FootRetriggerSeconds)
				{
					LastFootStepTime[Foot] = Now;
					LastFootPlantTime = Now;
					PlayFootstepAudio(Tier);
					bStepped = true;
				}
				FootPeakSwingSpeed[Foot] = 0.f;
			}
		}
		else if (FootSpeed > FootPlantSpeedMax * 2.f)
		{
			// Hysteresis: lift-off needs clearly-above-plant speed so sole roll doesn't flicker.
			bFootPlanted[Foot] = false;
			FootPeakSwingSpeed[Foot] = FootSpeed;
		}
	}

	bFootLocationsValid = true;
	return bStepped;
}

UFootstepNoiseComponent::EStepTier UFootstepNoiseComponent::PickTier() const
{
	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());

	bool bQuiet = OwnerChar && OwnerChar->bIsCrouched;
	if (const IExtractionPlayerInterface* PlayerInterface = Cast<IExtractionPlayerInterface>(OwnerChar))
		bQuiet |= PlayerInterface->GetIsProne();
	if (bQuiet) return EStepTier::Quiet;

	const float Speed = OwnerChar ? OwnerChar->GetVelocity().Size2D() : 0.f;
	if (Speed >= SprintSpeedThreshold) return EStepTier::Sprint;
	if (Speed < WalkSpeedThreshold) return EStepTier::SlowWalk;
	return EStepTier::Walk;
}

void UFootstepNoiseComponent::EmitAINoise(const FVector& Location, EStepTier Tier) const
{
	// A takedown-volume enemy muffles quiet noise but still reacts to a sprint (see
	// UEnemyAwarenessComponent::HandleHearingStimulus), so sprint steps carry a distinct tag.
	static const FName WalkTag(TEXT("Footstep"));
	static const FName SprintTag(TEXT("FootstepSprint"));

	float Loudness = WalkLoudness;
	float Range = WalkRange;
	FName Tag = WalkTag;

	switch (Tier)
	{
	case EStepTier::Quiet:    Loudness = QuietLoudness;    Range = QuietRange;    break;
	case EStepTier::SlowWalk: Loudness = SlowWalkLoudness; Range = SlowWalkRange; break;
	case EStepTier::Sprint:   Loudness = SprintLoudness;   Range = SprintRange;   Tag = SprintTag; break;
	case EStepTier::Walk: break;
	}
	if (Range <= 0.f || Loudness <= 0.f) return;

	UAISense_Hearing::ReportNoiseEvent(GetWorld(), Location, Loudness, Cast<ACharacter>(GetOwner()), Range, Tag);
}

void UFootstepNoiseComponent::PlayFootstepAudio(EStepTier Tier) const
{
	const UWorld* World = GetWorld();
	UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
	if (!AudioSys) return;

	const FSurfaceSoundSet* Set = AudioSys->GetSurfaceSet(TraceGroundSurface());
	if (!Set) return;

	USoundBase* Cue = Set->FootstepWalk;
	float Volume = AudioVolume;

	switch (Tier)
	{
	case EStepTier::Sprint:
		if (IsValid(Set->FootstepRun)) Cue = Set->FootstepRun;
		break;
	case EStepTier::Quiet:
		if (IsValid(Set->FootstepCrouch)) Cue = Set->FootstepCrouch;
		else Volume *= CrouchVolumeMult;
		break;
	case EStepTier::SlowWalk:
		Volume *= SlowWalkVolumeMult;
		break;
	case EStepTier::Walk: break;
	}

	// Own steps 2D: a positional one-shot at the capsule sits off-centre from the camera ear
	// and pans; NPC steps stay positional (they ARE somewhere).
	if (UGameAudioSubsystem::IsAudioDebugEnabled())
		UGameAudioSubsystem::DebugPlay(TEXT("STEP"), Cue,
			FString::Printf(TEXT("owner=%s tier=%d vol=%.2f"), *GetNameSafe(GetOwner()), (int32)Tier, Volume));
	AudioSys->PlayFoleyFor(GetOwner(), Cue, Volume);
}

void UFootstepNoiseComponent::UpdateGearRattle(ERattleState NewState)
{
	if (NewState == RattleState) return;
	const ERattleState OldState = RattleState;
	RattleState = NewState;

	if (NewState == ERattleState::None)
	{
		if (IsValid(SprintRattleComp))
			SprintRattleComp->FadeOut(0.4f, 0.f);
		SprintRattleComp = nullptr;
		return;
	}

	const float TargetVolume = AudioVolume * (NewState == ERattleState::Crouch ? CrouchRattleVolumeMult : 1.f);

	// Sprint <-> crouch while the loop is already running: retarget the volume, don't restart.
	if (OldState != ERattleState::None && IsValid(SprintRattleComp))
	{
		SprintRattleComp->AdjustVolume(0.4f, TargetVolume);
		return;
	}

	const UWorld* World = GetWorld();
	const UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
	const USurfaceAudioBank* Bank = AudioSys ? AudioSys->GetBank() : nullptr;
	AActor* Owner = GetOwner();
	if (!Bank || !IsValid(Bank->SprintRattleLoop) || !IsValid(Owner)) return;

	SprintRattleComp = UGameplayStatics::SpawnSoundAttached(Bank->SprintRattleLoop, Owner->GetRootComponent());
	if (IsValid(SprintRattleComp))
		SprintRattleComp->FadeIn(0.4f, TargetVolume);
}

EPhysicalSurface UFootstepNoiseComponent::TraceGroundSurface() const
{
	const ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	if (!IsValid(OwnerChar) || !World) return SurfaceType_Default;

	const UCapsuleComponent* Capsule = OwnerChar->GetCapsuleComponent();
	const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.f;

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(FootstepSurface), false, OwnerChar);
	Params.bReturnPhysicalMaterial = true;

	const FVector Start = OwnerChar->GetActorLocation();
	const FVector End = Start - FVector(0.f, 0.f, HalfHeight + 60.f);
	if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		return SurfaceType_Default;

	return UGameAudioSubsystem::SurfaceFromHit(Hit);
}

void UFootstepNoiseComponent::OnOwnerMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	// Jump push-off: ground -> airborne with meaningful upward velocity. Walking off a ledge
	// (downward Z) stays silent. ACharacter::OnJumped proved unreliable with the kit's jump flow.
	if (PrevMovementMode != MOVE_Walking && PrevMovementMode != MOVE_NavWalking) return;
	if (!IsValid(Character) || !Character->GetCharacterMovement()->IsFalling()) return;
	if (Character->GetVelocity().Z < 250.f) return;

	const UWorld* World = GetWorld();
	UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
	if (!AudioSys || !AudioSys->GetBank()) return;

	UGameAudioSubsystem::DebugPlay(TEXT("JUMP"), AudioSys->GetBank()->JumpFoley,
		FString::Printf(TEXT("owner=%s vz=%.0f"), *GetNameSafe(Character), Character->GetVelocity().Z));
	AudioSys->PlayFoleyFor(Character, AudioSys->GetBank()->JumpFoley, AudioVolume);
}

void UFootstepNoiseComponent::OnOwnerLanded(const FHitResult& Hit)
{
	const UWorld* World = GetWorld();
	UGameAudioSubsystem* AudioSys = World ? World->GetSubsystem<UGameAudioSubsystem>() : nullptr;
	if (!AudioSys || !IsValid(GetOwner())) return;

	// Stair/bump micro-landings during a run stay silent — only a real jump/drop thumps.
	if (GetOwner()->GetVelocity().Z > -LandAudioMinFallSpeed) return;
	if (UGameAudioSubsystem::IsAudioDebugEnabled())
		UGameAudioSubsystem::DebugPlay(TEXT("LAND"), nullptr,
			FString::Printf(TEXT("owner=%s vz=%.0f"), *GetNameSafe(GetOwner()), GetOwner()->GetVelocity().Z));

	// The CMC landing hit rarely carries a phys material — do the surface trace ourselves.
	const FSurfaceSoundSet* Set = AudioSys->GetSurfaceSet(TraceGroundSurface());
	if (Set) AudioSys->PlayFoleyFor(GetOwner(), Set->Land, AudioVolume);

	if (const USurfaceAudioBank* Bank = AudioSys->GetBank())
		AudioSys->PlayFoleyFor(GetOwner(), Bank->LandRattleLayer, LandRattleVolume * AudioVolume);
}
