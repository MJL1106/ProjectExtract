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
	TestFalse(TEXT("reset clears the warned latch"), ResetAcc.bWarned);
	TestFalse(TEXT("reset clears the escalated latch"), ResetAcc.bEscalated);

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
