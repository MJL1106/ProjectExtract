#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EnemyPostureComponent.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace EnemyPosturePolicyTests
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPressInitialDelayTest,
	"Extraction.Enemy.Posture.InitialDelayBlocksPress", EnemyPosturePolicyTests::TestFlags)

bool FEnemyPressInitialDelayTest::RunTest(const FString& Parameters)
{
	FEnemyPressCadence Cadence;
	Cadence.Configure(true, 6.f);
	Cadence.ResetForCombat(10.f, 4.f);

	TestFalse(TEXT("Press is blocked before the initial opportunity"), Cadence.CanEnterPress(13.99f));
	TestTrue(TEXT("Press is allowed when the initial opportunity arrives"), Cadence.CanEnterPress(14.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPressExpiryTest,
	"Extraction.Enemy.Posture.EpisodeExpires", EnemyPosturePolicyTests::TestFlags)

bool FEnemyPressExpiryTest::RunTest(const FString& Parameters)
{
	FEnemyPressCadence Cadence;
	Cadence.Configure(true, 6.f);
	Cadence.ResetForCombat(0.f, 0.f);
	TestTrue(TEXT("Episode can enter"), Cadence.TryEnterPress(0.f));

	TestFalse(TEXT("Episode remains active before its duration"), Cadence.HasEpisodeExpired(5.99f));
	TestTrue(TEXT("Episode expires at its configured duration"), Cadence.HasEpisodeExpired(6.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPressCommittedAdvanceTest,
	"Extraction.Enemy.Posture.CommittedAdvanceEndsEpisode", EnemyPosturePolicyTests::TestFlags)

bool FEnemyPressCommittedAdvanceTest::RunTest(const FString& Parameters)
{
	FEnemyPressCadence Cadence;
	Cadence.Configure(true, 6.f);
	Cadence.ResetForCombat(0.f, 0.f);
	TestTrue(TEXT("Episode can enter"), Cadence.TryEnterPress(0.f));
	Cadence.CompleteAdvanceAndScheduleRecovery(2.f, 10.f);

	TestFalse(TEXT("Committed advance ends the episode"), Cadence.IsEpisodeActive());
	TestFalse(TEXT("A second Press is blocked during recovery"), Cadence.CanEnterPress(11.99f));
	TestTrue(TEXT("Press becomes available after recovery"), Cadence.CanEnterPress(12.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPressInterruptionRecoveryTest,
	"Extraction.Enemy.Posture.InterruptionSchedulesRecovery", EnemyPosturePolicyTests::TestFlags)

bool FEnemyPressInterruptionRecoveryTest::RunTest(const FString& Parameters)
{
	FEnemyPressCadence Cadence;
	Cadence.Configure(true, 6.f);
	Cadence.ResetForCombat(0.f, 0.f);
	TestTrue(TEXT("Episode can enter"), Cadence.TryEnterPress(0.f));
	Cadence.EndEpisodeAndScheduleRecovery(1.f, 16.f);

	TestFalse(TEXT("Interrupted Press is blocked throughout recovery"), Cadence.CanEnterPress(16.99f));
	TestTrue(TEXT("Interrupted Press can return after recovery"), Cadence.CanEnterPress(17.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyPressLegacyDefaultsTest,
	"Extraction.Enemy.Posture.ZeroTuningPreservesLegacy", EnemyPosturePolicyTests::TestFlags)

bool FEnemyPressLegacyDefaultsTest::RunTest(const FString& Parameters)
{
	FEnemyPressCadence Cadence;
	Cadence.Configure(false, 0.f);
	Cadence.ResetForCombat(0.f, 0.f);

	TestTrue(TEXT("Legacy Press is immediately available"), Cadence.TryEnterPress(0.f));
	TestFalse(TEXT("Legacy Press does not expire"), Cadence.HasEpisodeExpired(1000.f));
	Cadence.CompleteAdvanceAndScheduleRecovery(1.f, 0.f);
	TestTrue(TEXT("Legacy completion does not impose a cadence block"), Cadence.CanEnterPress(1.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyAdvanceDistanceTest,
	"Extraction.Enemy.Posture.AdvanceDistancePredicate", EnemyPosturePolicyTests::TestFlags)

bool FEnemyAdvanceDistanceTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("Candidate inside threat standoff is rejected"),
		FEnemyPressCadence::IsAdvanceCandidateDistanceValid(1400.f, 799.f, 200.f, 800.f));
	TestFalse(TEXT("Candidate without minimum gain is rejected"),
		FEnemyPressCadence::IsAdvanceCandidateDistanceValid(1400.f, 1250.f, 200.f, 800.f));
	TestTrue(TEXT("Candidate outside standoff with minimum gain is accepted"),
		FEnemyPressCadence::IsAdvanceCandidateDistanceValid(1400.f, 1100.f, 200.f, 800.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEnemyFlankOfficerGateContractTest,
	"Extraction.Enemy.Posture.FlankRequiresLivingOfficer", EnemyPosturePolicyTests::TestFlags)

bool FEnemyFlankOfficerGateContractTest::RunTest(const FString& Parameters)
{
	const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),
		TEXT("Source/Extraction/Private/AI/Tasks/BTTask_EnemyFlank.cpp")));
	FString Source;
	if (!TestTrue(TEXT("Flank task source is readable"), FFileHelper::LoadFileToString(Source, *SourcePath)))
	{
		return false;
	}

	const int32 OfficerGateIndex = Source.Find(TEXT("HasLivingOfficer()"));
	const int32 CooldownIndex = Source.Find(TEXT("GetLastFlankAttemptTime"));
	const int32 RoleClaimIndex = Source.Find(TEXT("TryClaimRole(EEnemySquadRole::Flanker"));
	TestTrue(TEXT("Living-officer gate exists"), OfficerGateIndex != INDEX_NONE);
	TestTrue(TEXT("Living-officer gate precedes cooldown mutation path"),
		OfficerGateIndex != INDEX_NONE && CooldownIndex != INDEX_NONE && OfficerGateIndex < CooldownIndex);
	TestTrue(TEXT("Living-officer gate precedes Flanker role claim"),
		OfficerGateIndex != INDEX_NONE && RoleClaimIndex != INDEX_NONE && OfficerGateIndex < RoleClaimIndex);
	return true;
}

#endif
