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
	Settings.SprintPressurePerSecond = 8.f;
	Settings.PressurePerShot = 6.f;
	Settings.DecayPerSecond = 10.f;
	Settings.WarningThreshold = 35.f;
	Settings.EscalationThreshold = 70.f;

	FStealthPressureAccumulator Acc;

	Acc.Advance(0.5f, true, 0, Settings);
	TestEqual(TEXT("brief sprint remains pressure-free"), Acc.Pressure, 0.f);

	TestEqual(TEXT("exempt shot contributes no pressure"), Acc.Advance(0.25f, false, 0, Settings), EStealthPressureTransition::None);
	TestEqual(TEXT("exempt shot leaves pressure at zero"), Acc.Pressure, 0.f);

	EStealthPressureTransition Transition = EStealthPressureTransition::None;
	for (int32 i = 0; i < 6 && Transition == EStealthPressureTransition::None; ++i)
	{
		Transition = Acc.Advance(1.f, false, 1, Settings);
	}
	TestEqual(TEXT("warning fires once"), Transition, EStealthPressureTransition::Warned);

	const float PressureAfterWarning = Acc.Pressure;
	Transition = Acc.Advance(1.f, false, 1, Settings);
	TestNotEqual(TEXT("warning does not re-fire on the following sample"), Transition, EStealthPressureTransition::Warned);
	TestTrue(TEXT("pressure still accumulates after warning"), Acc.Pressure >= PressureAfterWarning);

	for (int32 i = 0; i < 12 && Transition != EStealthPressureTransition::Escalated; ++i)
	{
		Transition = Acc.Advance(1.f, false, 1, Settings);
	}
	TestEqual(TEXT("escalation fires once"), Transition, EStealthPressureTransition::Escalated);
	TestEqual(TEXT("pressure clamps to the escalation threshold"), Acc.Pressure, Settings.EscalationThreshold);

	Transition = Acc.Advance(1.f, false, 1, Settings);
	TestNotEqual(TEXT("escalation does not re-fire on the following sample"), Transition, EStealthPressureTransition::Escalated);

	FStealthPressureAccumulator DecayAcc;
	DecayAcc.Advance(1.f, false, 5, Settings);
	const float PressureBeforeDecay = DecayAcc.Pressure;
	DecayAcc.Advance(1.f, false, 0, Settings);
	TestTrue(TEXT("pressure decays when neither sprinting nor firing"), DecayAcc.Pressure < PressureBeforeDecay);

	FStealthPressureAccumulator ResetAcc;
	ResetAcc.Advance(5.f, true, 3, Settings);
	ResetAcc.Reset();
	TestEqual(TEXT("reset zeroes pressure"), ResetAcc.Pressure, 0.f);
	TestEqual(TEXT("reset zeroes continuous sprint time"), ResetAcc.ContinuousSprintSeconds, 0.f);
	TestEqual(TEXT("reset zeroes seconds-since-sprint"), ResetAcc.SecondsSinceSprint, 0.f);
	TestFalse(TEXT("reset clears the warned latch"), ResetAcc.bWarned);
	TestFalse(TEXT("reset clears the escalated latch"), ResetAcc.bEscalated);

	// -- Lapse tolerance: sub-grace dip preserves streak, decay still runs --
	{
		FStealthDisciplineSettings LapseSettings;
		LapseSettings.SprintGraceSeconds = 1.5f;
		LapseSettings.SprintPressurePerSecond = 8.f;
		LapseSettings.PressurePerShot = 6.f;
		LapseSettings.DecayPerSecond = 10.f;
		LapseSettings.WarningThreshold = 35.f;
		LapseSettings.EscalationThreshold = 70.f;
		LapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator LapseAcc;
		LapseAcc.Advance(3.f, true, 0, LapseSettings);
		TestTrue(TEXT("lapse: sprint past grace builds pressure"), LapseAcc.Pressure > 0.f);

		const float PressureBeforeDip = LapseAcc.Pressure;
		const float StreakBeforeDip = LapseAcc.ContinuousSprintSeconds;
		LapseAcc.Advance(0.5f, false, 0, LapseSettings);
		TestEqual(TEXT("lapse: sub-grace dip preserves sprint streak"),
			LapseAcc.ContinuousSprintSeconds, StreakBeforeDip);
		TestTrue(TEXT("lapse: sub-grace dip still decays pressure"),
			LapseAcc.Pressure < PressureBeforeDip);

		const float PressureAfterDip = LapseAcc.Pressure;
		LapseAcc.Advance(0.25f, true, 0, LapseSettings);
		TestTrue(TEXT("lapse: resumed sprint gains pressure immediately (no re-paying grace)"),
			LapseAcc.Pressure > PressureAfterDip);
	}

	// -- Lapse tolerance: over-grace dip resets streak and resumes decay --
	{
		FStealthDisciplineSettings LapseSettings;
		LapseSettings.SprintGraceSeconds = 1.5f;
		LapseSettings.SprintPressurePerSecond = 8.f;
		LapseSettings.PressurePerShot = 6.f;
		LapseSettings.DecayPerSecond = 10.f;
		LapseSettings.WarningThreshold = 35.f;
		LapseSettings.EscalationThreshold = 70.f;
		LapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator LapseAcc;
		LapseAcc.Advance(2.f, true, 0, LapseSettings);
		const float PressureBeforeLongDip = LapseAcc.Pressure;
		LapseAcc.Advance(1.f, false, 0, LapseSettings);
		TestEqual(TEXT("lapse: over-grace dip resets sprint streak"),
			LapseAcc.ContinuousSprintSeconds, 0.f);
		TestTrue(TEXT("lapse: over-grace dip resumes decay"),
			LapseAcc.Pressure < PressureBeforeLongDip);
	}

	// -- Stop-start sprinting (2s run / 0.5s stop) reaches Warned via lapse tolerance --
	{
		FStealthDisciplineSettings LapseSettings;
		LapseSettings.SprintGraceSeconds = 1.5f;
		LapseSettings.SprintPressurePerSecond = 8.f;
		LapseSettings.PressurePerShot = 6.f;
		LapseSettings.DecayPerSecond = 10.f;
		LapseSettings.WarningThreshold = 35.f;
		LapseSettings.EscalationThreshold = 70.f;
		LapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator StopStartAcc;
		EStealthPressureTransition StopStartResult = EStealthPressureTransition::None;

		for (int32 Cycle = 0; Cycle < 20 && StopStartResult != EStealthPressureTransition::Warned; ++Cycle)
		{
			for (int32 j = 0; j < 8; ++j)
			{
				const EStealthPressureTransition R = StopStartAcc.Advance(0.25f, true, 0, LapseSettings);
				if (R == EStealthPressureTransition::Warned) { StopStartResult = R; break; }
			}
			if (StopStartResult == EStealthPressureTransition::Warned) break;

			for (int32 j = 0; j < 2; ++j)
			{
				const EStealthPressureTransition R = StopStartAcc.Advance(0.25f, false, 0, LapseSettings);
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
		ZeroGraceSettings.SprintPressurePerSecond = 8.f;
		ZeroGraceSettings.PressurePerShot = 6.f;
		ZeroGraceSettings.DecayPerSecond = 10.f;
		ZeroGraceSettings.WarningThreshold = 35.f;
		ZeroGraceSettings.EscalationThreshold = 70.f;
		ZeroGraceSettings.SprintLapseGraceSeconds = 0.f;

		FStealthPressureAccumulator ZeroGraceAcc;
		ZeroGraceAcc.Advance(2.f, true, 0, ZeroGraceSettings);
		const float PressureBeforeDip = ZeroGraceAcc.Pressure;
		ZeroGraceAcc.Advance(0.5f, false, 0, ZeroGraceSettings);
		TestEqual(TEXT("zero-grace: dip resets sprint streak"),
			ZeroGraceAcc.ContinuousSprintSeconds, 0.f);
		TestTrue(TEXT("zero-grace: dip decays pressure"),
			ZeroGraceAcc.Pressure < PressureBeforeDip);
	}

	// -- Shot accrual during tolerated lapse --
	{
		FStealthDisciplineSettings ShotLapseSettings;
		ShotLapseSettings.SprintGraceSeconds = 1.5f;
		ShotLapseSettings.SprintPressurePerSecond = 8.f;
		ShotLapseSettings.PressurePerShot = 6.f;
		ShotLapseSettings.DecayPerSecond = 10.f;
		ShotLapseSettings.WarningThreshold = 35.f;
		ShotLapseSettings.EscalationThreshold = 70.f;
		ShotLapseSettings.SprintLapseGraceSeconds = 0.75f;

		FStealthPressureAccumulator ShotLapseAcc;
		ShotLapseAcc.Advance(3.f, true, 0, ShotLapseSettings);
		const float PressureBeforeShots = ShotLapseAcc.Pressure;

		ShotLapseAcc.Advance(0.25f, false, 6, ShotLapseSettings);
		const float ExpectedShotPressure = ShotLapseSettings.PressurePerShot * 6.f;
		TestEqual(TEXT("shot-lapse: shots during tolerated lapse add correct pressure"),
			ShotLapseAcc.Pressure, PressureBeforeShots + ExpectedShotPressure);

		const EStealthPressureTransition EscResult = ShotLapseAcc.Advance(0.25f, false, 3, ShotLapseSettings);
		TestEqual(TEXT("shot-lapse: escalation fires during tolerated lapse"),
			EscResult, EStealthPressureTransition::Escalated);
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
