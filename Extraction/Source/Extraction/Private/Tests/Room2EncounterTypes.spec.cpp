// Automation coverage for the pure Room 2 encounter state types: finite-wave squad/member tracking,
// stealth pressure accumulation, and Director profile-selection priority. No engine world required.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DirectorConfigData.h"
#include "DirectorWaveTypes.h"
#include "StealthDisciplineTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorWaveProgressTest, "Extraction.Room2.EncounterTypes.WaveProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDirectorWaveProgressTest::RunTest(const FString& Parameters)
{
	FDirectorWaveProgress Progress;
	Progress.Begin(1);

	TestFalse(TEXT("zero-member squad is not counted"), Progress.RecordSuccessfulSquad(0));
	TestEqual(TEXT("zero-member squad leaves spawned count untouched"), Progress.SpawnedSquads, 0);

	TestTrue(TEXT("one-member squad is recorded"), Progress.RecordSuccessfulSquad(1));
	TestTrue(TEXT("target reached but living members blocks completion"), !Progress.IsComplete());
	TestFalse(TEXT("no more squads to spawn once target reached"), Progress.CanSpawnMore());

	Progress.RecordMemberEnded();
	TestTrue(TEXT("completion requires target squads and zero members"), Progress.IsComplete());

	Progress.RecordMemberEnded();
	TestEqual(TEXT("member-ended clamps remaining at zero"), Progress.RemainingMembers, 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStealthPressureAccumulatorTest, "Extraction.Room2.EncounterTypes.StealthPressure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FStealthPressureAccumulatorTest::RunTest(const FString& Parameters)
{
	FStealthDisciplineSettings Settings;
	Settings.SprintGraceSeconds = 1.5f;
	Settings.SprintPressurePerSecond = 18.f;
	Settings.PressurePerShot = 15.f;
	Settings.SuppressedPressurePerShot = 3.f;
	Settings.DecayPerSecond = 5.f;
	Settings.DecayDelaySeconds = 0.f;
	Settings.WarningThreshold = 25.f;
	Settings.EscalationThreshold = 75.f;

	FStealthPressureAccumulator Acc;

	Acc.Advance(0.5f, true, 0, 0, Settings);
	TestEqual(TEXT("brief sprint remains pressure-free"), Acc.Pressure, 0.f);

	TestEqual(TEXT("exempt shot contributes no pressure"), Acc.Advance(0.25f, false, 0, 0, Settings), EStealthPressureTransition::None);
	TestEqual(TEXT("exempt shot leaves pressure at zero"), Acc.Pressure, 0.f);

	EStealthPressureTransition Transition = EStealthPressureTransition::None;
	for (int32 i = 0; i < 6 && Transition == EStealthPressureTransition::None; ++i)
	{
		Transition = Acc.Advance(1.f, false, 1, 0, Settings);
	}
	TestEqual(TEXT("warning fires once"), Transition, EStealthPressureTransition::Warned);

	const float PressureAfterWarning = Acc.Pressure;
	Transition = Acc.Advance(1.f, false, 1, 0, Settings);
	TestNotEqual(TEXT("warning does not re-fire on the following sample"), Transition, EStealthPressureTransition::Warned);
	TestTrue(TEXT("pressure still accumulates after warning"), Acc.Pressure >= PressureAfterWarning);

	for (int32 i = 0; i < 12 && Transition != EStealthPressureTransition::Escalated; ++i)
	{
		Transition = Acc.Advance(1.f, false, 1, 0, Settings);
	}
	TestEqual(TEXT("escalation fires once"), Transition, EStealthPressureTransition::Escalated);
	TestEqual(TEXT("pressure clamps to the escalation threshold"), Acc.Pressure, Settings.EscalationThreshold);

	Transition = Acc.Advance(1.f, false, 1, 0, Settings);
	TestNotEqual(TEXT("escalation does not re-fire on the following sample"), Transition, EStealthPressureTransition::Escalated);

	FStealthPressureAccumulator DecayAcc;
	DecayAcc.Advance(1.f, false, 5, 0, Settings);
	const float PressureBeforeDecay = DecayAcc.Pressure;
	DecayAcc.Advance(1.f, false, 0, 0, Settings);
	TestTrue(TEXT("pressure decays when neither sprinting nor firing"), DecayAcc.Pressure < PressureBeforeDecay);

	FStealthPressureAccumulator ResetAcc;
	ResetAcc.Advance(5.f, true, 3, 0, Settings);
	ResetAcc.Advance(0.5f, false, 0, 0, Settings);
	ResetAcc.Reset();
	TestEqual(TEXT("reset zeroes pressure"), ResetAcc.Pressure, 0.f);
	TestEqual(TEXT("reset zeroes continuous sprint time"), ResetAcc.ContinuousSprintSeconds, 0.f);
	TestEqual(TEXT("reset zeroes seconds-since-sprint"), ResetAcc.SecondsSinceSprint, 0.f);
	TestEqual(TEXT("reset zeroes seconds-since-pressure-gain"), ResetAcc.SecondsSincePressureGain, 0.f);
	TestFalse(TEXT("reset clears the warned latch"), ResetAcc.bWarned);
	TestFalse(TEXT("reset clears the escalated latch"), ResetAcc.bEscalated);

	// -- Suppressed shots cost less than unsuppressed --
	{
		FStealthPressureAccumulator UnsuppAcc;
		UnsuppAcc.Advance(0.25f, false, 1, 0, Settings);
		const float UnsuppressedPressure = UnsuppAcc.Pressure;

		FStealthPressureAccumulator SuppAcc;
		SuppAcc.Advance(0.25f, false, 0, 1, Settings);
		const float SuppressedPressure = SuppAcc.Pressure;

		TestTrue(TEXT("suppressed shot costs less than unsuppressed"),
			SuppressedPressure < UnsuppressedPressure);
		TestTrue(TEXT("suppressed shot is not free"),
			SuppressedPressure > 0.f);
		TestEqual(TEXT("unsuppressed shot applies PressurePerShot"),
			UnsuppressedPressure, Settings.PressurePerShot);
		TestEqual(TEXT("suppressed shot applies SuppressedPressurePerShot"),
			SuppressedPressure, Settings.SuppressedPressurePerShot);
	}

	// -- Suppressed shots suspend decay --
	{
		FStealthPressureAccumulator SuppDecayAcc;
		SuppDecayAcc.Advance(0.25f, false, 3, 0, Settings);
		const float PressureBefore = SuppDecayAcc.Pressure;
		// Fire suppressed shots: should add pressure, NOT decay.
		SuppDecayAcc.Advance(1.f, false, 0, 1, Settings);
		TestTrue(TEXT("suppressed shots suspend decay"),
			SuppDecayAcc.Pressure >= PressureBefore);
	}

	// -- Pressure persists (no reset on simulated exit/re-entry) --
	{
		FStealthPressureAccumulator PersistAcc;
		PersistAcc.Advance(0.25f, false, 2, 0, Settings);
		const float PressureBeforeExit = PersistAcc.Pressure;
		TestTrue(TEXT("persist: pressure accrued before exit"), PressureBeforeExit > 0.f);

		// Simulate outside: decay-only ticks (no sprint, no shots).
		PersistAcc.Advance(0.5f, false, 0, 0, Settings);
		TestTrue(TEXT("persist: pressure decays but does not reset"),
			PersistAcc.Pressure > 0.f && PersistAcc.Pressure < PressureBeforeExit);

		// Simulate re-entry: resume sprinting.
		const float PressureBeforeReEntry = PersistAcc.Pressure;
		PersistAcc.Advance(2.f, true, 0, 0, Settings);
		TestTrue(TEXT("persist: pressure accumulates on re-entry from surviving base"),
			PersistAcc.Pressure > PressureBeforeReEntry);
	}

	// -- Lapse tolerance: sub-grace dip preserves streak, decay still runs --
	{
		FStealthDisciplineSettings LapseSettings;
		LapseSettings.SprintGraceSeconds = 1.5f;
		LapseSettings.SprintPressurePerSecond = 18.f;
		LapseSettings.PressurePerShot = 15.f;
		LapseSettings.SuppressedPressurePerShot = 3.f;
		LapseSettings.DecayPerSecond = 5.f;
		LapseSettings.DecayDelaySeconds = 0.f;
		LapseSettings.WarningThreshold = 25.f;
		LapseSettings.EscalationThreshold = 75.f;
		LapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator LapseAcc;
		LapseAcc.Advance(3.f, true, 0, 0, LapseSettings);
		TestTrue(TEXT("lapse: sprint past grace builds pressure"), LapseAcc.Pressure > 0.f);

		const float PressureBeforeDip = LapseAcc.Pressure;
		const float StreakBeforeDip = LapseAcc.ContinuousSprintSeconds;
		LapseAcc.Advance(0.5f, false, 0, 0, LapseSettings);
		TestEqual(TEXT("lapse: sub-grace dip preserves sprint streak"),
			LapseAcc.ContinuousSprintSeconds, StreakBeforeDip);
		TestTrue(TEXT("lapse: sub-grace dip still decays pressure"),
			LapseAcc.Pressure < PressureBeforeDip);

		const float PressureAfterDip = LapseAcc.Pressure;
		LapseAcc.Advance(0.25f, true, 0, 0, LapseSettings);
		TestTrue(TEXT("lapse: resumed sprint gains pressure immediately (no re-paying grace)"),
			LapseAcc.Pressure > PressureAfterDip);
	}

	// -- Lapse tolerance: over-grace dip resets streak and resumes decay --
	{
		FStealthDisciplineSettings LapseSettings;
		LapseSettings.SprintGraceSeconds = 1.5f;
		LapseSettings.SprintPressurePerSecond = 18.f;
		LapseSettings.PressurePerShot = 15.f;
		LapseSettings.SuppressedPressurePerShot = 3.f;
		LapseSettings.DecayPerSecond = 5.f;
		LapseSettings.DecayDelaySeconds = 0.f;
		LapseSettings.WarningThreshold = 25.f;
		LapseSettings.EscalationThreshold = 75.f;
		LapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator LapseAcc;
		LapseAcc.Advance(2.f, true, 0, 0, LapseSettings);
		const float PressureBeforeLongDip = LapseAcc.Pressure;
		LapseAcc.Advance(1.f, false, 0, 0, LapseSettings);
		TestEqual(TEXT("lapse: over-grace dip resets sprint streak"),
			LapseAcc.ContinuousSprintSeconds, 0.f);
		TestTrue(TEXT("lapse: over-grace dip resumes decay"),
			LapseAcc.Pressure < PressureBeforeLongDip);
	}

	// -- Stop-start sprinting (2s run / 0.5s stop) reaches Warned via lapse tolerance --
	{
		FStealthDisciplineSettings LapseSettings;
		LapseSettings.SprintGraceSeconds = 1.5f;
		LapseSettings.SprintPressurePerSecond = 18.f;
		LapseSettings.PressurePerShot = 15.f;
		LapseSettings.SuppressedPressurePerShot = 3.f;
		LapseSettings.DecayPerSecond = 5.f;
		LapseSettings.DecayDelaySeconds = 0.f;
		LapseSettings.WarningThreshold = 25.f;
		LapseSettings.EscalationThreshold = 75.f;
		LapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator StopStartAcc;
		EStealthPressureTransition StopStartResult = EStealthPressureTransition::None;

		for (int32 Cycle = 0; Cycle < 20 && StopStartResult != EStealthPressureTransition::Warned; ++Cycle)
		{
			for (int32 j = 0; j < 8; ++j)
			{
				const EStealthPressureTransition R = StopStartAcc.Advance(0.25f, true, 0, 0, LapseSettings);
				if (R == EStealthPressureTransition::Warned) { StopStartResult = R; break; }
			}
			if (StopStartResult == EStealthPressureTransition::Warned) break;

			for (int32 j = 0; j < 2; ++j)
			{
				const EStealthPressureTransition R = StopStartAcc.Advance(0.25f, false, 0, 0, LapseSettings);
				if (R == EStealthPressureTransition::Warned) { StopStartResult = R; break; }
			}
		}

		TestEqual(TEXT("stop-start sprinting reaches Warned via lapse tolerance"),
			StopStartResult, EStealthPressureTransition::Warned);
	}

	// -- Zero lapse grace: old hard-reset behaviour (regression guard) --
	{
		FStealthDisciplineSettings ZeroGraceSettings;
		ZeroGraceSettings.SprintGraceSeconds = 1.5f;
		ZeroGraceSettings.SprintPressurePerSecond = 18.f;
		ZeroGraceSettings.PressurePerShot = 15.f;
		ZeroGraceSettings.SuppressedPressurePerShot = 3.f;
		ZeroGraceSettings.DecayPerSecond = 5.f;
		ZeroGraceSettings.DecayDelaySeconds = 0.f;
		ZeroGraceSettings.WarningThreshold = 25.f;
		ZeroGraceSettings.EscalationThreshold = 75.f;
		ZeroGraceSettings.SprintLapseGraceSeconds = 0.f;

		FStealthPressureAccumulator ZeroGraceAcc;
		ZeroGraceAcc.Advance(2.f, true, 0, 0, ZeroGraceSettings);
		const float PressureBeforeDip = ZeroGraceAcc.Pressure;
		ZeroGraceAcc.Advance(0.5f, false, 0, 0, ZeroGraceSettings);
		TestEqual(TEXT("zero-grace: dip resets sprint streak"),
			ZeroGraceAcc.ContinuousSprintSeconds, 0.f);
		TestTrue(TEXT("zero-grace: dip decays pressure"),
			ZeroGraceAcc.Pressure < PressureBeforeDip);
	}

	// -- Shot accrual during tolerated lapse --
	{
		FStealthDisciplineSettings ShotLapseSettings;
		ShotLapseSettings.SprintGraceSeconds = 1.5f;
		ShotLapseSettings.SprintPressurePerSecond = 18.f;
		ShotLapseSettings.PressurePerShot = 15.f;
		ShotLapseSettings.SuppressedPressurePerShot = 3.f;
		ShotLapseSettings.DecayPerSecond = 5.f;
		ShotLapseSettings.DecayDelaySeconds = 0.f;
		ShotLapseSettings.WarningThreshold = 25.f;
		ShotLapseSettings.EscalationThreshold = 75.f;
		ShotLapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator ShotLapseAcc;
		ShotLapseAcc.Advance(3.f, true, 0, 0, ShotLapseSettings);
		const float PressureBeforeShots = ShotLapseAcc.Pressure;

		// 1 shot at 15 keeps us below the 75 escalation threshold.
		ShotLapseAcc.Advance(0.25f, false, 1, 0, ShotLapseSettings);
		const float ExpectedShotPressure = ShotLapseSettings.PressurePerShot * 1.f;
		TestEqual(TEXT("shot-lapse: shots during tolerated lapse add correct pressure"),
			ShotLapseAcc.Pressure, PressureBeforeShots + ExpectedShotPressure);

		// 1 more shot pushes past 75 -> Escalated.
		const EStealthPressureTransition EscResult = ShotLapseAcc.Advance(0.25f, false, 1, 0, ShotLapseSettings);
		TestEqual(TEXT("shot-lapse: escalation fires during tolerated lapse"),
			EscResult, EStealthPressureTransition::Escalated);
	}

	// -- Decay delay: idle tick shortly after a gain does not decay; decay resumes once the delay elapses --
	{
		FStealthDisciplineSettings DelaySettings;
		DelaySettings.SprintGraceSeconds = 1.5f;
		DelaySettings.SprintPressurePerSecond = 18.f;
		DelaySettings.PressurePerShot = 15.f;
		DelaySettings.SuppressedPressurePerShot = 3.f;
		DelaySettings.DecayPerSecond = 5.f;
		DelaySettings.DecayDelaySeconds = 3.f;
		DelaySettings.WarningThreshold = 25.f;
		DelaySettings.EscalationThreshold = 75.f;
		DelaySettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator DelayAcc;
		DelayAcc.Advance(0.25f, false, 1, 0, DelaySettings);
		const float PressureAfterShot = DelayAcc.Pressure;

		// Well under the 3s delay: must not decay yet.
		DelayAcc.Advance(1.f, false, 0, 0, DelaySettings);
		TestEqual(TEXT("decay-delay: idle tick inside the delay window does not decay"),
			DelayAcc.Pressure, PressureAfterShot);

		// Cumulative idle time now exceeds the delay: decay resumes.
		DelayAcc.Advance(2.5f, false, 0, 0, DelaySettings);
		TestTrue(TEXT("decay-delay: decay resumes once cumulative idle time exceeds the delay"),
			DelayAcc.Pressure < PressureAfterShot);
	}

	// -- Decay delay: a tick that adds shot pressure resets the delay window --
	{
		FStealthDisciplineSettings DelaySettings;
		DelaySettings.SprintGraceSeconds = 1.5f;
		DelaySettings.SprintPressurePerSecond = 18.f;
		DelaySettings.PressurePerShot = 15.f;
		DelaySettings.SuppressedPressurePerShot = 3.f;
		DelaySettings.DecayPerSecond = 5.f;
		DelaySettings.DecayDelaySeconds = 3.f;
		DelaySettings.WarningThreshold = 25.f;
		DelaySettings.EscalationThreshold = 75.f;
		DelaySettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator ResetWindowAcc;
		ResetWindowAcc.Advance(0.25f, false, 1, 0, DelaySettings);
		ResetWindowAcc.Advance(2.9f, false, 0, 0, DelaySettings);

		// Second shot should reset the delay window, not just add pressure.
		ResetWindowAcc.Advance(0.25f, false, 1, 0, DelaySettings);
		const float PressureAfterSecondShot = ResetWindowAcc.Pressure;
		TestEqual(TEXT("decay-delay: second shot adds pressure on top of the first"),
			PressureAfterSecondShot, DelaySettings.PressurePerShot * 2.f);

		// Idle time that would have exceeded the ORIGINAL window (2.9 + 2.9 > 3) must not decay,
		// because the second shot reset the window.
		ResetWindowAcc.Advance(2.9f, false, 0, 0, DelaySettings);
		TestEqual(TEXT("decay-delay: a pressure-gaining tick resets the delay window"),
			ResetWindowAcc.Pressure, PressureAfterSecondShot);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDirectorProfileSelectorTest, "Extraction.Room2.EncounterTypes.ProfileSelector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FDirectorProfileSelectorTest::RunTest(const FString& Parameters)
{
	UDirectorConfigData* WaveConfig = NewObject<UDirectorConfigData>();
	UDirectorConfigData* PunishmentConfig = NewObject<UDirectorConfigData>();
	UDirectorConfigData* BaseConfig = NewObject<UDirectorConfigData>();

	TestEqual(TEXT("wave config has highest priority"),
		FDirectorProfileSelector::SelectConfig(true, WaveConfig, true, PunishmentConfig, BaseConfig),
		static_cast<const UDirectorConfigData*>(WaveConfig));

	TestEqual(TEXT("punishment restores after wave"),
		FDirectorProfileSelector::SelectConfig(false, WaveConfig, true, PunishmentConfig, BaseConfig),
		static_cast<const UDirectorConfigData*>(PunishmentConfig));

	TestEqual(TEXT("base restores after removing punishment"),
		FDirectorProfileSelector::SelectConfig(false, WaveConfig, false, PunishmentConfig, BaseConfig),
		static_cast<const UDirectorConfigData*>(BaseConfig));

	TestEqual(TEXT("null wave config falls through to punishment"),
		FDirectorProfileSelector::SelectConfig(true, nullptr, true, PunishmentConfig, BaseConfig),
		static_cast<const UDirectorConfigData*>(PunishmentConfig));

	TestEqual(TEXT("wave phase has highest priority"),
		FDirectorProfileSelector::SelectPhase(true, EMissionPhase::Extraction, true, EMissionPhase::Objective, EMissionPhase::Infiltration),
		EMissionPhase::Extraction);

	TestEqual(TEXT("punishment phase beats base phase"),
		FDirectorProfileSelector::SelectPhase(false, EMissionPhase::Extraction, true, EMissionPhase::Objective, EMissionPhase::Infiltration),
		EMissionPhase::Objective);

	TestEqual(TEXT("base phase restores after removing punishment"),
		FDirectorProfileSelector::SelectPhase(false, EMissionPhase::Extraction, false, EMissionPhase::Objective, EMissionPhase::Infiltration),
		EMissionPhase::Infiltration);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
