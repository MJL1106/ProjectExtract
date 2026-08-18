#include "Audio/GameAudioSubsystem.h"
#include "Audio/ExtractionAudioSettings.h"
#include "Audio/MusicSubsystem.h"
#include "Audio/SurfaceAudioBank.h"
#include "Components/AudioComponent.h"
#include "Core/Extraction.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundMix.h"
#include "TimerManager.h"
#include "Engine/Engine.h"

// NB not "audio.*" — UE5.7 treats that prefix as deprecated aliases and refuses the command.
static TAutoConsoleVariable<int32> CVarWorldAudioDebug(
	TEXT("x.AudioDebug"), 0,
	TEXT("1 = on-screen + log line for every world-audio play (footsteps, foley, impacts, fire reports)."),
	ECVF_Default);

// A CVar rather than a bank/config value because this is a "does it still drop out?" number: it wants
// tuning live in PIE mid-firefight, which a DataAsset edit (PIE stop, reload) can't give.
static TAutoConsoleVariable<float> CVarCombatCullRange(
	TEXT("x.AudioCombatCullRange"), 7000.f,
	TEXT("Distance in cm from the listener past which bullet impacts and flybys are skipped instead of taking a voice. 0 = never cull."),
	ECVF_Default);

static const FName AmbientReverbTag(TEXT("ExtractionAmbientReverb"));

bool UGameAudioSubsystem::IsAudioDebugEnabled()
{
	return CVarWorldAudioDebug.GetValueOnGameThread() != 0;
}

void UGameAudioSubsystem::DebugPlay(const TCHAR* Tag, const USoundBase* Sound, const FString& Context)
{
	if (!IsAudioDebugEnabled()) return;
	const FString Line = FString::Printf(TEXT("[Audio] %s %s %s"), Tag, *GetNameSafe(Sound), *Context);
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Yellow, Line);
	UE_LOG(LogExtraction, Log, TEXT("%s"), *Line);
}

void UGameAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UExtractionAudioSettings* Settings = GetDefault<UExtractionAudioSettings>();
	if (!Settings || Settings->AudioBank.IsNull())
	{
		UE_LOG(LogExtraction, Warning, TEXT("GameAudioSubsystem: no AudioBank set in Project Settings -> Extraction Audio — world audio silent."));
		return;
	}

	// Small asset (sound refs only) — a sync load at world init is cheap and keeps every
	// caller free of async plumbing.
	Bank = Settings->AudioBank.LoadSynchronous();
}

void UGameAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// Global room reverb — without an active reverb effect every attenuation's reverb send is a
	// no-op and all VO/gunfire plays bone dry. Activated at begin-play (audio device is up by then).
	if (IsValid(Bank) && IsValid(Bank->AmbientReverb))
		UGameplayStatics::ActivateReverbEffect(&InWorld, Bank->AmbientReverb, AmbientReverbTag,
			/*Priority=*/0.f, Bank->AmbientReverbVolume, /*FadeTime=*/1.f);
}

void UGameAudioSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
		if (IsValid(Bank ? Bank->AmbientReverb.Get() : nullptr))
			UGameplayStatics::DeactivateReverbEffect(World, AmbientReverbTag);
		if (bDBNOMixPushed && IsValid(Bank ? Bank->DBNOSoundMix.Get() : nullptr))
			UGameplayStatics::PopSoundMixModifier(World, Bank->DBNOSoundMix);
	}
	if (IsValid(HeartbeatComponent))
		HeartbeatComponent->Stop();
	HeartbeatComponent = nullptr;
	bDBNOMixPushed = false;

	Super::Deinitialize();
}

const FSurfaceSoundSet* UGameAudioSubsystem::GetSurfaceSet(EPhysicalSurface Surface) const
{
	if (!IsValid(Bank)) return nullptr;
	if (const FSurfaceSoundSet* Set = Bank->Surfaces.Find(Surface)) return Set;
	return &Bank->DefaultSurface;
}

EPhysicalSurface UGameAudioSubsystem::SurfaceFromHit(const FHitResult& Hit)
{
	const UPhysicalMaterial* PhysMat = Hit.PhysMaterial.Get();
	return PhysMat ? PhysMat->SurfaceType.GetValue() : SurfaceType_Default;
}

bool UGameAudioSubsystem::IsBeyondCombatCullRange(const FVector& Location) const
{
	const float Range = CVarCombatCullRange.GetValueOnGameThread();
	if (Range <= 0.f) return false;

	// The camera is the listener. No PC or no camera manager means we can't judge distance — never
	// cull on a guess, a missing listener must not silence combat.
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	const APlayerCameraManager* Cam = PC ? PC->PlayerCameraManager : nullptr;
	if (!IsValid(Cam)) return false;

	return FVector::DistSquared(Cam->GetCameraLocation(), Location) > static_cast<double>(Range) * Range;
}

void UGameAudioSubsystem::StampCombatActivity() const
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	if (UMusicSubsystem* Music = World->GetSubsystem<UMusicSubsystem>())
		Music->NotifyCombatActivity();
}

void UGameAudioSubsystem::PlayWorldImpact(const FHitResult& Hit)
{
	StampCombatActivity();
	if (IsBeyondCombatCullRange(Hit.ImpactPoint)) return;

	const FSurfaceSoundSet* Set = GetSurfaceSet(SurfaceFromHit(Hit));
	if (Set) PlayAt(Set->BulletImpact, Hit.ImpactPoint);
}

void UGameAudioSubsystem::PlayFleshImpact(const FVector& Location, bool bAsLocal2D, bool bHeadshot, bool bAllyVictim)
{
	StampCombatActivity();
	if (!IsValid(Bank)) return;

	USoundBase* Crack = nullptr;
	USoundBase* Layer = nullptr;
	float Volume2D = Bank->FleshFeedbackVolume;
	// Enemy hits keep their historical unattenuated 3D volume; only ally hits are pulled down.
	float Volume3D = 1.f;

	if (bAllyVictim)
	{
		// Fall back to the enemy crack when no ally variant is authored: a silent hit on the player
		// is worse than an indistinct one.
		Crack = IsValid(Bank->AllyFleshImpact) ? Bank->AllyFleshImpact.Get() : Bank->FleshImpact.Get();
		Layer = Bank->AllyFleshImpactLayer.Get();
		Volume2D = Bank->AllyFleshFeedbackVolume;
		Volume3D = Bank->AllyFleshFeedbackVolume;
	}
	else
	{
		Crack = (bHeadshot && IsValid(Bank->HeadshotImpact)) ? Bank->HeadshotImpact.Get() : Bank->FleshImpact.Get();
		Layer = Bank->FleshImpactLayer.Get();
	}

	if (bAsLocal2D)
	{
		Play2D(Crack, Volume2D);
		Play2D(Layer, Volume2D);
		return;
	}
	PlayAt(Crack, Location, Volume3D);
	PlayAt(Layer, Location, Volume3D);
}

void UGameAudioSubsystem::PlayAIFireReport(USoundBase* Sound, const FVector& Location) const
{
	StampCombatActivity();
	if (!IsValid(Sound)) return;
	DebugPlay(TEXT("AI-FIRE"), Sound, Location.ToCompactString());
	USoundAttenuation* Attenuation = IsValid(Bank) ? Bank->GunfireAttenuation.Get() : nullptr;
	const float Volume = IsValid(Bank) ? Bank->AIFireVolume : 1.f;
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location, FRotator::ZeroRotator,
		Volume, 1.f, 0.f, Attenuation);
}

void UGameAudioSubsystem::Play2D(USoundBase* Sound, float Volume) const
{
	if (!IsValid(Sound)) return;
	DebugPlay(TEXT("2D"), Sound, FString::Printf(TEXT("vol=%.2f"), Volume));
	UGameplayStatics::PlaySound2D(GetWorld(), Sound, Volume);
}

void UGameAudioSubsystem::PlayFoleyFor(const AActor* Owner, USoundBase* Sound, float Volume) const
{
	if (!IsValid(Sound) || !IsValid(Owner)) return;

	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && PC->GetPawn() == Owner)
	{
		// Every foley wav is stereo and the footstep attenuation spreads stereo sources (non-zero
		// StereoSpread, no stereo normalization, no non-spatialized radius). Played positionally
		// at the listener that splits one step into two virtual sources straddling the head — heard
		// as a doubled step orbiting the player. Play2D disables spatialization outright, so the
		// wav's own L/R imaging plays as authored.
		Play2D(Sound, Volume);
		return;
	}
	PlayAt(Sound, Owner->GetActorLocation(), Volume);
}

void UGameAudioSubsystem::PlayFlyby(const FVector& Location)
{
	StampCombatActivity();
	if (IsBeyondCombatCullRange(Location)) return;
	if (IsValid(Bank)) PlayAt(Bank->Flyby, Location);
}

void UGameAudioSubsystem::PlayShellDrop(const FVector& Location)
{
	// Player shots only reach the hub through their impacts, so suppressive fire into open space
	// registered as nothing at all. A shell drops per shot whatever the round hits.
	StampCombatActivity();

	UWorld* World = GetWorld();
	if (!World || !IsValid(Bank) || !IsValid(Bank->ShellBounce)) return;

	const float Delay = FMath::RandRange(Bank->ShellDelayMin, FMath::Max(Bank->ShellDelayMin, Bank->ShellDelayMax));
	FTimerHandle Handle; // fire-and-forget: Deinitialize clears all timers for this object
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
		[this, Location] { PlayAt(Bank->ShellBounce, Location, Bank->ShellVolume); }), Delay, false);
}

void UGameAudioSubsystem::PlayThrowFoley()
{
	// The only grenade event C++ sees (the detonation is BP-side), so without this a grenade-led
	// exchange with no gunfire reads as a quiet fight and the music winds down mid-throw.
	StampCombatActivity();

	UWorld* World = GetWorld();
	if (!World || !IsValid(Bank) || !IsValid(Bank->ThrowFoley)) return;

	// The throw press routes to a BP event that can reject (mid-throw re-press, empty slot) —
	// debounce so spam presses can't stack delayed foley.
	if (World->GetTimeSeconds() - LastThrowFoleyTime < Bank->ThrowFoleyCooldown) return;
	LastThrowFoleyTime = World->GetTimeSeconds();

	// Player-only caller — own-body foley plays 2D (positional pans oddly at the listener).
	if (Bank->ThrowFoleyDelay <= 0.f)
	{
		Play2D(Bank->ThrowFoley);
		return;
	}
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this,
		[this] { Play2D(Bank->ThrowFoley); }), Bank->ThrowFoleyDelay, false);
}

void UGameAudioSubsystem::PlayAt(USoundBase* Sound, const FVector& Location, float Volume) const
{
	if (!IsValid(Sound)) return;
	DebugPlay(TEXT("3D"), Sound, FString::Printf(TEXT("vol=%.2f at=%s"), Volume, *Location.ToCompactString()));
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location, Volume);
}

void UGameAudioSubsystem::StartDBNOAudio()
{
	UWorld* World = GetWorld();
	if (!World || !IsValid(Bank)) return;

	if (IsValid(Bank->DBNOSoundMix) && !bDBNOMixPushed)
	{
		UGameplayStatics::PushSoundMixModifier(World, Bank->DBNOSoundMix);
		bDBNOMixPushed = true;
	}

	if (IsValid(Bank->HeartbeatLoop) && !IsValid(HeartbeatComponent))
	{
		HeartbeatComponent = UGameplayStatics::SpawnSound2D(World, Bank->HeartbeatLoop);
		if (IsValid(HeartbeatComponent))
			HeartbeatComponent->FadeIn(1.f);
	}
}

void UGameAudioSubsystem::StopDBNOAudio()
{
	UWorld* World = GetWorld();
	if (World && bDBNOMixPushed && IsValid(Bank ? Bank->DBNOSoundMix.Get() : nullptr))
		UGameplayStatics::PopSoundMixModifier(World, Bank->DBNOSoundMix);
	bDBNOMixPushed = false;

	if (IsValid(HeartbeatComponent))
		HeartbeatComponent->FadeOut(0.8f, 0.f);
	HeartbeatComponent = nullptr;
}
