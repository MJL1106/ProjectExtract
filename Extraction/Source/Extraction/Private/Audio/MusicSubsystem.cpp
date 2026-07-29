#include "Audio/MusicSubsystem.h"
#include "Audio/ExtractionAudioSettings.h"
#include "Audio/MusicBankData.h"
#include "Components/AudioComponent.h"
#include "Components/HealthComponent.h"
#include "Core/Extraction.h"
#include "Enemy/EnemyDirectorSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

// NB not "audio.*" — UE5.7 treats that prefix as deprecated aliases and refuses the command.
static TAutoConsoleVariable<int32> CVarMusicDebug(
	TEXT("x.MusicDebug"), 0,
	TEXT("1 = on-screen + log line for every music state transition and track change."),
	ECVF_Default);

namespace
{
	/** Call sites check this before building their FString::Printf — the formatting and GetNameSafe
	 *  allocations are the cost, not the log call. */
	bool IsMusicDebugOn()
	{
		return CVarMusicDebug.GetValueOnGameThread() != 0;
	}

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
	bReliefDucked = false;
	bOutgoingFightBed = false;
	PendingAllClearTime = 0.0;

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

	// A bed that ran out on its own (a fight track authored non-looping) leaves its flag stale —
	// drop it so the handoff/resume paths don't read a dead component as available.
	if (bReliefDucked && !(IsValid(CurrentTrack) && CurrentTrack->IsPlaying())) bReliefDucked = false;
	if (bOutgoingFightBed && !(IsValid(OutgoingTrack) && OutgoingTrack->IsPlaying())) bOutgoingFightBed = false;

	// Before the counts are read: this is a combat-activity source in its own right and the gate
	// below consumes it the same poll.
	PollPlayerDamage(*World, Now);

	// Both holds are count-based off the director's 1s sweep; stamp them here so the windows
	// survive the counts flickering to zero between sweeps.
	if (const UEnemyDirectorSubsystem* Director = World->GetSubsystem<UEnemyDirectorSubsystem>())
	{
		if (Director->GetSearchingEnemyCount() > 0)
			LastSuspicionSignalTime = Now;

		// The director's LastCombatReportTime fires on combat ENTRY only. Through the back half of a
		// long engagement nobody new enters, so gating on it alone would leave the hold already
		// expired by the time the counts drop — the wind-down would start the instant LOS broke.
		// The activity gate is what stops the opposite failure: the count outlives the fight by the
		// archetype's LostContactGrace, and an ungated stamp here would keep refreshing the hold off
		// that stale awareness so combat music never wound down at all.
		if (Director->GetCombatEnemyCount() > 0 && HasRecentCombatActivity(Now))
			LastCombatSignalTime = Now;
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

	// Resolved after the transitions so a fight re-igniting this same poll cancels it first. The cue
	// waits for the duck to settle — fired at wind-down start it lands over full-volume combat music.
	if (PendingAllClearTime > 0.0 && Now >= PendingAllClearTime)
	{
		PendingAllClearTime = 0.0;
		PlayStinger(Bank->StingerAllClear);
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
		// Most recent of the two: our sweep stamp carries the body of a sustained fight, the
		// director's entry stamp covers an enemy that engages and dies between two sweeps. The live
		// count stays in front so a designer's zero hold still plays combat music during combat.
		const double EntryStamp = static_cast<double>(Director->GetLastCombatReportTime());
		const double LastCombat = FMath::Max(LastCombatSignalTime, EntryStamp);
		// The live count only counts while something is actually happening — on its own it stays true
		// for the whole LostContactGrace window and pins the music in Combat for up to 45s of silence.
		const bool bCombat = (Director->GetCombatEnemyCount() > 0 && HasRecentCombatActivity(Now))
			|| (Now - LastCombat) < Bank->CombatHoldSeconds;
		if (bCombat) return EMusicState::Combat;
	}

	if ((Now - LastSuspicionSignalTime) < Bank->SuspicionHoldSeconds) return EMusicState::Suspicion;

	return EMusicState::Explore;
}

void UMusicSubsystem::NotifyCombatActivity()
{
	const UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	LastCombatActivityTime = World->GetTimeSeconds();
}

void UMusicSubsystem::PollPlayerDamage(const UWorld& World, double Now)
{
	if (!PlayerHealth.IsValid())
	{
		const APlayerController* PC = World.GetFirstPlayerController();
		const APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
		PlayerHealth = IsValid(PlayerPawn) ? PlayerPawn->FindComponentByClass<UHealthComponent>() : nullptr;
		if (!PlayerHealth.IsValid()) return;
	}

	// Damage taken is the combat signal the world-audio hub can't see: melee lands with no impact
	// cue and a grenade exchange fires no bullet report, so a fight the player is losing would wind
	// the music down while they're being hit. Any source stamps it — TakeDamage records the time
	// before the shield/health split, so a shield-only hit counts too.
	const float LastDamage = PlayerHealth->GetLastDamageWorldTime();
	if (LastDamage <= LastSeenPlayerDamageTime) return;

	LastSeenPlayerDamageTime = LastDamage;
	LastCombatActivityTime = Now;
}

bool UMusicSubsystem::HasRecentCombatActivity(double Now) const
{
	// 0 = gate off, matching x.AudioCombatCullRange's "0 = never cull". Without this a designer
	// zeroing the window would silently kill combat music outright instead of the new gate.
	if (Bank->CombatActivityWindowSeconds <= 0.f) return true;
	return (Now - LastCombatActivityTime) < Bank->CombatActivityWindowSeconds;
}

void UMusicSubsystem::TransitionTo(EMusicState NewState, double Now)
{
	const EMusicState OldState = State;
	State = NewState;
	StateEnterTime = Now;

	switch (NewState)
	{
	case EMusicState::Combat:
	case EMusicState::Wave:
		// Nothing cleared after all — drop any resolve cue still queued from the wind-down.
		PendingAllClearTime = 0.0;
		// A lull that re-ignites rides the same bed back up — restarting the track and
		// re-hitting the stinger is what made short gaps read as a stop/start.
		if (TryResumeDuckedTrack(NewState)) break;
		if (NewState == EMusicState::Combat && OldState != EMusicState::Wave) PlayStinger(Bank->StingerCombatIn);
		StartTrack(TrackForState(NewState), Bank->CrossfadeSeconds);
		break;

	case EMusicState::Suspicion:
		// Enemies are hunting again — nothing is clear, so drop a resolve cue the wind-down queued.
		PendingAllClearTime = 0.0;
		// Straight out of a relief bed this is the same slow palette handoff Explore gets — a 2s
		// swap off the ducked fight bed reads as a cut next to it.
		StartTrack(TrackForState(EMusicState::Suspicion), FadeSecondsLeavingFightBed());
		break;

	case EMusicState::Relief:
	{
		ReliefUntilTime = Now + Bank->ReliefSeconds;
		// Wave completion plays its victory stinger from the delegate; a combat wind-down queues the
		// all-clear for once the bed has ducked away. The fight has to be OVER, not merely quiet:
		// the activity gate de-escalates the bed through any lull, and a "you're safe now" resolve
		// over a cautious flank reads far worse than the long tail it replaced.
		const UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
		const bool bNoneEngaged = !IsValid(Director) || Director->GetCombatEnemyCount() == 0;
		if (OldState == EMusicState::Combat && bNoneEngaged) PendingAllClearTime = Now + Bank->ReliefDuckFadeSeconds;
		DuckOrStopForRelief(OldState);
		break;
	}

	case EMusicState::Explore:
	default:
		// The wind-down completed, so the resolve cue is owed — but it can never outlive relief, or a
		// duck fade tuned longer than ReliefSeconds would fire it well into the stealth bed. Clamping
		// (not clearing) lands it on the handoff seam, which is the latest point it still reads.
		PendingAllClearTime = FMath::Min(PendingAllClearTime, ReliefUntilTime);
		// Relief hands its ducked bed over to the stealth bed; only fall back to silence when
		// there's nothing to hand off.
		if (TryReliefHandoff()) break;
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
	// The relief handoff bed is already playing when we land here — it holds off the next gap
	// until it ends, at which point the normal silence cadence resumes.
	if (IsValid(CurrentTrack) && CurrentTrack->IsPlaying()) return;

	if (NextExploreTime <= 0.0)
	{
		NextExploreTime = Now + FMath::FRandRange(Bank->ExploreSilenceMinSeconds,
			FMath::Max(Bank->ExploreSilenceMinSeconds, Bank->ExploreSilenceMaxSeconds));
		return;
	}

	if (Now < NextExploreTime) return;

	USoundBase* Bed = PickExploreTrack();
	NextExploreTime = 0.0;
	if (!IsValid(Bed)) return;

	StartTrack(Bed, Bank->CrossfadeSeconds);
}

USoundBase* UMusicSubsystem::PickExploreTrack() const
{
	const UEnemyDirectorSubsystem* Director = GetWorld() ? GetWorld()->GetSubsystem<UEnemyDirectorSubsystem>() : nullptr;
	const EMissionPhase Phase = IsValid(Director) ? Director->GetMissionPhase() : EMissionPhase::Infiltration;
	const FMusicPhaseSet& Set = Bank->SetForPhase(Phase);

	TArray<USoundBase*> Candidates;
	Candidates.Reserve(Set.ExploreTracks.Num());
	for (const TObjectPtr<USoundBase>& Track : Set.ExploreTracks)
	{
		if (IsValid(Track)) Candidates.Add(Track);
	}

	if (Candidates.IsEmpty()) return nullptr;
	return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
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
// Relief duck / handoff
// ============================================================

void UMusicSubsystem::DuckOrStopForRelief(EMusicState OldState)
{
	const bool bFromFight = OldState == EMusicState::Combat || OldState == EMusicState::Wave;
	if (!bFromFight || !(IsValid(CurrentTrack) && CurrentTrack->IsPlaying()))
	{
		StopTrack(Bank->StopFadeSeconds);
		return;
	}

	// AdjustVolume reads a near-zero target as a fade-out request: it flags the sound EFadeOut::User
	// and schedules it to stop when the fade lands. Floor the level so a designer's low duck stays a
	// duck instead of silently terminating the bed we intend to resume.
	const float DuckLevel = FMath::Max(UE_KINDA_SMALL_NUMBER, Bank->MusicVolume * Bank->ReliefDuckVolume);
	CurrentTrack->AdjustVolume(Bank->ReliefDuckFadeSeconds, DuckLevel);
	bReliefDucked = true;

	if (IsMusicDebugOn())
		DebugLine(FString::Printf(TEXT("[Music] duck %s to %.2f over %.1fs"),
			*GetNameSafe(CurrentTrack->Sound), DuckLevel, Bank->ReliefDuckFadeSeconds));
}

bool UMusicSubsystem::TryResumeDuckedTrack(EMusicState ForState)
{
	// The mission phase can flip during the lull — an Infiltration bed is the wrong palette for an
	// Extraction fight, so that case takes the normal crossfade instead.
	USoundBase* Target = TrackForState(ForState);
	if (!IsValid(Target)) return false;

	// Still ducked and ours: ride it straight back up.
	if (bReliefDucked && IsValid(CurrentTrack) && CurrentTrack->IsPlaying() && CurrentTrack->Sound == Target)
	{
		CurrentTrack->AdjustVolume(Bank->CrossfadeSeconds, Bank->MusicVolume);
		bReliefDucked = false;
		// A phase flip mid-relief can leave an older fight bed still fading out on the outgoing slot.
		// We've reclaimed the one we want, so retire its claim — it's no longer a bed we'd go back to.
		bOutgoingFightBed = false;

		if (IsMusicDebugOn())
			DebugLine(FString::Printf(TEXT("[Music] unduck %s to %.2f over %.1fs"),
				*GetNameSafe(Target), Bank->MusicVolume, Bank->CrossfadeSeconds));
		return true;
	}

	// Already handed off, but the fight bed is still audible under the bed replacing it for the whole
	// handoff fade — reclaim it rather than hard-cutting and restarting the fight from bar 1.
	if (!bOutgoingFightBed) return false;
	if (!(IsValid(OutgoingTrack) && OutgoingTrack->IsPlaying())) return false;
	if (OutgoingTrack->Sound != Target) return false;

	// Straight swap of the two slots — the half-risen replacement bed becomes the one going out.
	UAudioComponent* Replacement = CurrentTrack;
	CurrentTrack = OutgoingTrack;
	OutgoingTrack = Replacement;
	bOutgoingFightBed = false;
	bReliefDucked = false;

	// Adjusting up clears the component's pending fade-out, so the reclaimed bed stops being
	// scheduled to stop and simply rises from wherever its fade had reached.
	CurrentTrack->AdjustVolume(Bank->CrossfadeSeconds, Bank->MusicVolume);

	if (IsValid(OutgoingTrack) && OutgoingTrack->IsPlaying()) OutgoingTrack->FadeOut(Bank->CrossfadeSeconds, 0.f);
	else OutgoingTrack = nullptr;

	if (IsMusicDebugOn())
		DebugLine(FString::Printf(TEXT("[Music] reclaim %s mid-handoff to %.2f over %.1fs"),
			*GetNameSafe(Target), Bank->MusicVolume, Bank->CrossfadeSeconds));
	return true;
}

float UMusicSubsystem::FadeSecondsLeavingFightBed() const
{
	const bool bDuckedBedLive = bReliefDucked && IsValid(CurrentTrack) && CurrentTrack->IsPlaying();
	const bool bOutgoingBedLive = bOutgoingFightBed && IsValid(OutgoingTrack) && OutgoingTrack->IsPlaying();
	return (bDuckedBedLive || bOutgoingBedLive) ? Bank->ReliefHandoffFadeSeconds : Bank->CrossfadeSeconds;
}

bool UMusicSubsystem::TryReliefHandoff()
{
	if (!bReliefDucked) return false;
	if (!(IsValid(CurrentTrack) && CurrentTrack->IsPlaying())) return false;

	USoundBase* Bed = PickExploreTrack();
	if (!IsValid(Bed)) return false;

	// A bank listing the fight track among its explore beds would hand the ducked bed to itself:
	// StartTrack early-returns on a matching sound and unducks it to full, so combat music would run
	// at level through Explore forever. Refuse the handoff and let the caller stop it normally.
	if (Bed == CurrentTrack->Sound) return false;

	if (IsMusicDebugOn())
		DebugLine(FString::Printf(TEXT("[Music] handoff %s -> %s over %.1fs"),
			*GetNameSafe(CurrentTrack->Sound), *GetNameSafe(Bed), Bank->ReliefHandoffFadeSeconds));

	// StartTrack pushes the ducked bed to OutgoingTrack and fades it out over the same long
	// duration the stealth bed fades in — one slow crossfade, no silence between them. It stays
	// tagged as the fight bed the whole way out so a re-ignition can reclaim it.
	StartTrack(Bed, Bank->ReliefHandoffFadeSeconds);

	// This bed replaces the first scheduled ambience; UpdateExplore re-arms the gap once it ends.
	NextExploreTime = 0.0;
	return true;
}

// ============================================================
// Playback
// ============================================================

void UMusicSubsystem::StartTrack(USoundBase* Sound, float FadeSeconds)
{
	UWorld* World = GetWorld();
	if (!IsValid(World)) return;
	if (IsValid(CurrentTrack) && CurrentTrack->Sound == Sound && CurrentTrack->IsPlaying())
	{
		// Already the right bed — but if it's the ducked one, it still owes us its level back,
		// otherwise the state would run on at relief volume forever.
		if (bReliefDucked)
		{
			CurrentTrack->AdjustVolume(FadeSeconds, Bank->MusicVolume);
			bReliefDucked = false;
			if (IsMusicDebugOn())
				DebugLine(FString::Printf(TEXT("[Music] unduck %s to %.2f over %.1fs"),
					*GetNameSafe(Sound), Bank->MusicVolume, FadeSeconds));
		}
		return;
	}

	// Coming in from silence snaps to level fast — a full crossfade-length fade-in on the quiet
	// stealth bed reads as "the music is late." Track-to-track swaps keep the smooth crossfade
	// (a ducked bed still counts as playing, so the relief handoff gets its long fade).
	const bool bFromSilence = !(IsValid(CurrentTrack) && CurrentTrack->IsPlaying());

	StopTrack(FadeSeconds);
	if (!IsValid(Sound)) return;

	CurrentTrack = UGameplayStatics::CreateSound2D(World, Sound, Bank->MusicVolume, 1.f, 0.f, nullptr,
		/*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/true);
	if (IsValid(CurrentTrack)) CurrentTrack->FadeIn(bFromSilence ? Bank->EntryFadeSeconds : FadeSeconds, Bank->MusicVolume);

	if (IsMusicDebugOn())
		DebugLine(FString::Printf(TEXT("[Music] track -> %s over %.1fs"), *GetNameSafe(Sound),
			bFromSilence ? Bank->EntryFadeSeconds : FadeSeconds));
}

void UMusicSubsystem::StopTrack(float FadeSeconds)
{
	// A previous fade-out still running gets cut hard — two tracks fading atop a third reads as mud.
	if (IsValid(OutgoingTrack)) OutgoingTrack->Stop();
	OutgoingTrack = nullptr;
	bOutgoingFightBed = false;

	if (IsValid(CurrentTrack) && CurrentTrack->IsPlaying())
	{
		OutgoingTrack = CurrentTrack;
		OutgoingTrack->FadeOut(FadeSeconds, 0.f);
		// The relief bed carries its fight-bed identity out with it, so a fight re-igniting before
		// the fade lands can reclaim it instead of restarting the combat track.
		bOutgoingFightBed = bReliefDucked;
	}
	CurrentTrack = nullptr;
	bReliefDucked = false;
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
	if (!IsMusicDebugOn()) return;

	const TCHAR* BedNote = bReliefDucked ? TEXT(" (ducked)") : (bOutgoingFightBed ? TEXT(" (handing off)") : TEXT(""));
	DebugLine(FString::Printf(TEXT("[Music] %s -> %s%s"), What, MusicStateName(State), BedNote));
}

void UMusicSubsystem::DebugLine(const FString& Line) const
{
	if (!IsMusicDebugOn()) return;
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 4.f, FColor::Cyan, Line);
	UE_LOG(LogExtraction, Log, TEXT("%s"), *Line);
}
