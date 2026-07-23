#include "Audio/MusicSubsystem.h"
#include "Audio/ExtractionAudioSettings.h"
#include "Audio/MusicBankData.h"
#include "Components/AudioComponent.h"
#include "Core/Extraction.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

// NB not "audio.*" — UE5.7 treats that prefix as deprecated aliases and refuses the command.
static TAutoConsoleVariable<int32> CVarMusicDebug(
	TEXT("x.MusicDebug"), 0,
	TEXT("1 = on-screen + log line for every music state transition and track change."),
	ECVF_Default);

namespace
{
	const TCHAR* MusicStateName(EMusicState S)
	{
		switch (S)
		{
		case EMusicState::Explore:   return TEXT("Explore");
		case EMusicState::Suspicion: return TEXT("Suspicion");
		case EMusicState::Combat:    return TEXT("Combat");
		case EMusicState::Wave:      return TEXT("Wave");
		case EMusicState::Relief:    return TEXT("Relief");
		default:                     return TEXT("?");
		}
	}
}

void UMusicSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UExtractionAudioSettings* Settings = GetDefault<UExtractionAudioSettings>();
	if (!Settings || Settings->MusicBank.IsNull())
	{
		UE_LOG(LogExtraction, Log, TEXT("MusicSubsystem: no MusicBank set in Project Settings -> Extraction Audio — music idle."));
		return;
	}

	// Small asset (sound refs only) — sync load at world init, same pattern as the audio bank.
	Bank = Settings->MusicBank.LoadSynchronous();
}

void UMusicSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	if (!IsValid(Bank)) return;

	if (UEnemyDirectorSubsystem* Director = InWorld.GetSubsystem<UEnemyDirectorSubsystem>())
	{
		if (!Director->OnDirectorWaveStarted.IsAlreadyBound(this, &UMusicSubsystem::HandleWaveStarted))
			Director->OnDirectorWaveStarted.AddDynamic(this, &UMusicSubsystem::HandleWaveStarted);
		if (!Director->OnDirectorWaveCompleted.IsAlreadyBound(this, &UMusicSubsystem::HandleWaveCompleted))
			Director->OnDirectorWaveCompleted.AddDynamic(this, &UMusicSubsystem::HandleWaveCompleted);
	}

	// Stealth bed starts at once — any opening gap read as "the music is late" on PIE entry, and the
	// always-on design has no intro silence. The bank's silence cadence still governs later gaps.
	NextExploreTime = InWorld.GetTimeSeconds();
	StateEnterTime = InWorld.GetTimeSeconds();

	InWorld.GetTimerManager().SetTimer(PollTimerHandle, this, &UMusicSubsystem::Poll, PollInterval, true);

	// Kick the first poll now so the explore track starts this frame instead of up to PollInterval later.
	Poll();
}

void UMusicSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PollTimerHandle);

		if (UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
		{
			Director->OnDirectorWaveStarted.RemoveDynamic(this, &UMusicSubsystem::HandleWaveStarted);
			Director->OnDirectorWaveCompleted.RemoveDynamic(this, &UMusicSubsystem::HandleWaveCompleted);
		}
	}

	if (IsValid(CurrentTrack)) CurrentTrack->Stop();
	if (IsValid(OutgoingTrack)) OutgoingTrack->Stop();
	if (IsValid(OverlayComponent)) OverlayComponent->Stop();
	CurrentTrack = nullptr;
	OutgoingTrack = nullptr;
	OverlayComponent = nullptr;

	Super::Deinitialize();
}

// ============================================================
// State machine
// ============================================================

void UMusicSubsystem::Poll()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Bank)) return;

	const double Now = World->GetTimeSeconds();

	// Suspicion is count-based off the director's 1s sweep; stamp it here so the hold
	// window survives the count flickering to zero between sweeps.
	if (const UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
	{
		if (Director->GetSearchingEnemyCount() > 0)
			LastSuspicionSignalTime = Now;
	}

	const EMusicState Desired = ComputeDesiredState(Now);

	if (Desired != State)
	{
		const bool bInFight = State == EMusicState::Combat || State == EMusicState::Wave;
		const bool bDesiredFight = Desired == EMusicState::Combat || Desired == EMusicState::Wave;

		if (bInFight && !bDesiredFight)
		{
			// Fights always wind down through Relief, never snap to ambience.
			TransitionTo(EMusicState::Relief, Now);
		}
		else if (State == EMusicState::Relief && !bDesiredFight && Now < ReliefUntilTime)
		{
			// Hold the quiet — only a new fight may interrupt relief.
		}
		else
		{
			TransitionTo(Desired, Now);
		}
	}

	UpdateTrackForState();
	if (State == EMusicState::Explore) UpdateExplore(Now);
	UpdatePeakOverlay();
}

EMusicState UMusicSubsystem::ComputeDesiredState(double Now) const
{
	if (bWaveActive) return EMusicState::Wave;

	const UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	if (IsValid(Director))
	{
		const bool bCombat = Director->GetCombatEnemyCount() > 0
			|| (Now - Director->GetLastCombatReportTime()) < Bank->CombatHoldSeconds;
		if (bCombat) return EMusicState::Combat;
	}

	if ((Now - LastSuspicionSignalTime) < Bank->SuspicionHoldSeconds) return EMusicState::Suspicion;

	return EMusicState::Explore;
}

void UMusicSubsystem::TransitionTo(EMusicState NewState, double Now)
{
	const EMusicState OldState = State;
	State = NewState;
	StateEnterTime = Now;

	switch (NewState)
	{
	case EMusicState::Combat:
		if (OldState != EMusicState::Wave) PlayStinger(Bank->StingerCombatIn);
		StartTrack(TrackForState(EMusicState::Combat), Bank->CrossfadeSeconds);
		break;

	case EMusicState::Wave:
		StartTrack(TrackForState(EMusicState::Wave), Bank->CrossfadeSeconds);
		break;

	case EMusicState::Suspicion:
		StartTrack(TrackForState(EMusicState::Suspicion), Bank->CrossfadeSeconds);
		break;

	case EMusicState::Relief:
		ReliefUntilTime = Now + Bank->ReliefSeconds;
		// Wave completion plays its victory stinger from the delegate; a combat wind-down
		// gets the all-clear resolve.
		if (OldState == EMusicState::Combat) PlayStinger(Bank->StingerAllClear);
		StopTrack(Bank->StopFadeSeconds);
		break;

	case EMusicState::Explore:
	default:
		StopTrack(Bank->StopFadeSeconds);
		NextExploreTime = 0.0;
		break;
	}

	DebugState(MusicStateName(OldState));
}

void UMusicSubsystem::UpdateTrackForState()
{
	// Looping states re-check their prescribed track each poll — this is what swaps the
	// palette mid-state when the mission phase flips (e.g. combat rolls into Extraction).
	if (State != EMusicState::Suspicion && State != EMusicState::Combat && State != EMusicState::Wave) return;

	USoundBase* Target = TrackForState(State);
	if (!IsValid(Target)) return;
	if (IsValid(CurrentTrack) && CurrentTrack->Sound == Target && CurrentTrack->IsPlaying()) return;

	StartTrack(Target, Bank->CrossfadeSeconds);
}

void UMusicSubsystem::UpdateExplore(double Now)
{
	if (IsValid(CurrentTrack) && CurrentTrack->IsPlaying()) return;

	if (NextExploreTime <= 0.0)
	{
		NextExploreTime = Now + FMath::FRandRange(Bank->ExploreSilenceMinSeconds,
			FMath::Max(Bank->ExploreSilenceMinSeconds, Bank->ExploreSilenceMaxSeconds));
		return;
	}

	if (Now < NextExploreTime) return;

	const UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	const EMissionPhase Phase = IsValid(Director) ? Director->GetMissionPhase() : EMissionPhase::Infiltration;

	TArray<USoundBase*> Candidates;
	Candidates.Reserve(Bank->SetForPhase(Phase).ExploreTracks.Num());
	for (const TObjectPtr<USoundBase>& Track : Bank->SetForPhase(Phase).ExploreTracks)
	{
		if (IsValid(Track)) Candidates.Add(Track);
	}

	NextExploreTime = 0.0;
	if (Candidates.IsEmpty()) return;

	StartTrack(Candidates[FMath::RandRange(0, Candidates.Num() - 1)], Bank->CrossfadeSeconds);
}

void UMusicSubsystem::UpdatePeakOverlay()
{
	const UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;

	const bool bWantOverlay =
		(State == EMusicState::Combat || State == EMusicState::Wave)
		&& IsValid(Director) && Director->GetDirectorState() == EDirectorState::Peak
		&& IsValid(Bank->PeakOverlayLoop);

	if (bWantOverlay == bOverlayOn) return;
	bOverlayOn = bWantOverlay;

	if (bWantOverlay)
	{
		if (!IsValid(OverlayComponent))
		{
			// bAutoDestroy off — the overlay fades in and out many times per fight.
			OverlayComponent = UGameplayStatics::CreateSound2D(GetWorld(), Bank->PeakOverlayLoop,
				Bank->OverlayVolume, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
		}
		if (IsValid(OverlayComponent))
			OverlayComponent->FadeIn(Bank->OverlayFadeSeconds, Bank->OverlayVolume);
		DebugState(TEXT("overlay-in"));
	}
	else if (IsValid(OverlayComponent))
	{
		OverlayComponent->FadeOut(Bank->OverlayFadeSeconds, 0.f);
		DebugState(TEXT("overlay-out"));
	}
}

// ============================================================
// Playback
// ============================================================

void UMusicSubsystem::StartTrack(USoundBase* Sound, float FadeSeconds)
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	if (IsValid(CurrentTrack) && CurrentTrack->Sound == Sound && CurrentTrack->IsPlaying()) return;

	// Coming in from silence snaps to level fast — a full crossfade-length fade-in on the quiet
	// stealth bed reads as "the music is late." Track-to-track swaps keep the smooth crossfade.
	const bool bFromSilence = !(IsValid(CurrentTrack) && CurrentTrack->IsPlaying());

	StopTrack(FadeSeconds);
	if (!IsValid(Sound)) return;

	CurrentTrack = UGameplayStatics::CreateSound2D(World, Sound, Bank->MusicVolume, 1.f, 0.f, nullptr,
		/*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/true);
	if (IsValid(CurrentTrack)) CurrentTrack->FadeIn(bFromSilence ? Bank->EntryFadeSeconds : FadeSeconds, Bank->MusicVolume);

	if (CVarMusicDebug.GetValueOnGameThread() != 0)
	{
		const FString Line = FString::Printf(TEXT("[Music] track -> %s"), *GetNameSafe(Sound));
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Line);
		UE_LOG(LogExtraction, Log, TEXT("%s"), *Line);
	}
}

void UMusicSubsystem::StopTrack(float FadeSeconds)
{
	// A previous fade-out still running gets cut hard — two tracks fading atop a third reads as mud.
	if (IsValid(OutgoingTrack)) OutgoingTrack->Stop();
	OutgoingTrack = nullptr;

	if (IsValid(CurrentTrack) && CurrentTrack->IsPlaying())
	{
		OutgoingTrack = CurrentTrack;
		OutgoingTrack->FadeOut(FadeSeconds, 0.f);
	}
	CurrentTrack = nullptr;
}

void UMusicSubsystem::PlayStinger(USoundBase* Sound) const
{
	if (!IsValid(Sound)) return;
	UGameplayStatics::PlaySound2D(GetWorld(), Sound, Bank->StingerVolume);
}

USoundBase* UMusicSubsystem::TrackForState(EMusicState ForState) const
{
	const UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	const EMissionPhase Phase = IsValid(Director) ? Director->GetMissionPhase() : EMissionPhase::Infiltration;
	const FMusicPhaseSet& Set = Bank->SetForPhase(Phase);

	switch (ForState)
	{
	case EMusicState::Suspicion: return Set.SuspicionTrack;
	case EMusicState::Combat:    return Set.CombatTrack;
	case EMusicState::Wave:      return IsValid(Set.WaveTrack) ? Set.WaveTrack.Get() : Set.CombatTrack.Get();
	default:                     return nullptr;
	}
}

// ============================================================
// Wave events
// ============================================================

void UMusicSubsystem::HandleWaveStarted(FName WaveId)
{
	bWaveActive = true;
	Poll();
}

void UMusicSubsystem::HandleWaveCompleted(FName WaveId)
{
	bWaveActive = false;
	if (IsValid(Bank)) PlayStinger(Bank->StingerWaveVictory);
	Poll();
}

// ============================================================
// Debug
// ============================================================

void UMusicSubsystem::DebugState(const TCHAR* What) const
{
	if (CVarMusicDebug.GetValueOnGameThread() == 0) return;
	const FString Line = FString::Printf(TEXT("[Music] %s -> %s"), What, MusicStateName(State));
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Line);
	UE_LOG(LogExtraction, Log, TEXT("%s"), *Line);
}
