#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/SearchRoomExposure.h"
#include "AI/CompanionSearchRoomPolicy.h"

namespace SearchRoomExposureTests
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext
	| EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSearchRoomExposureLifecycleTest,
	"Extraction.Companion.SearchExposure.LifecycleAndScope", SearchRoomExposureTests::TestFlags)

bool FSearchRoomExposureLifecycleTest::RunTest(const FString& Parameters)
{
	FSearchRoomExposureState Exposure;
	TestFalse(TEXT("Exposure starts inactive"), Exposure.IsActive());
	TestEqual(TEXT("Inactive exposure has no active generation"), Exposure.GetActiveGeneration(), uint32{0});

	Exposure.Begin(FVector(100.f, 200.f, 0.f), 300.f, true);
	const uint32 FirstGeneration = Exposure.GetActiveGeneration();
	TestTrue(TEXT("Begin activates exposure"), Exposure.IsActive());
	TestTrue(TEXT("Begin assigns a non-zero generation"), FirstGeneration != 0);
	TestTrue(TEXT("Observer on the radius boundary is in scope"),
		Exposure.IsObserverInScope(FVector(400.f, 200.f, 0.f)));
	TestFalse(TEXT("Observer outside the radius is out of scope"),
		Exposure.IsObserverInScope(FVector(401.f, 200.f, 0.f)));
	TestTrue(TEXT("Silent startle belongs to the active generation"),
		Exposure.HasSilentStartle(FirstGeneration));

	Exposure.End();
	TestFalse(TEXT("End deactivates exposure"), Exposure.IsActive());
	TestEqual(TEXT("Ended exposure has no active generation"), Exposure.GetActiveGeneration(), uint32{0});
	TestFalse(TEXT("Ended exposure has no in-scope observers"),
		Exposure.IsObserverInScope(FVector(100.f, 200.f, 0.f)));

	Exposure.Begin(FVector::ZeroVector, 100.f, false);
	TestTrue(TEXT("A new begin advances the generation"),
		Exposure.GetActiveGeneration() > FirstGeneration);
	TestFalse(TEXT("Non-silent exposure has no visual startle"),
		Exposure.HasSilentStartle(Exposure.GetActiveGeneration()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSearchRoomExposureStartleFloorTest,
	"Extraction.Enemy.SearchExposure.StartleFloor", SearchRoomExposureTests::TestFlags)

bool FSearchRoomExposureStartleFloorTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Weak breach noise receives the configured floor"),
		SearchRoomExposure::ApplyStartleSuspicion(10.f, 5.f, 75.f, 99.f), 75.f);
	TestEqual(TEXT("Normal gain above the floor is preserved"),
		SearchRoomExposure::ApplyStartleSuspicion(80.f, 10.f, 75.f, 99.f), 90.f);
	TestEqual(TEXT("Startle remains below automatic hearing-only confirmation"),
		SearchRoomExposure::ApplyStartleSuspicion(98.f, 10.f, 75.f, 99.f), 99.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompanionSearchRoomEngagementPolicyTest,
	"Extraction.Companion.SearchExposure.EngagementPolicy", SearchRoomExposureTests::TestFlags)

bool FCompanionSearchRoomEngagementPolicyTest::RunTest(const FString& Parameters)
{
	using namespace CompanionSearchRoomPolicy;

	TestFalse(TEXT("Normal does not acquire an unaware room enemy"),
		CanEngageUnawareEnemy(ECompanionMode::Normal));
	TestFalse(TEXT("Stealth does not acquire an unaware room enemy"),
		CanEngageUnawareEnemy(ECompanionMode::Stealth));
	TestTrue(TEXT("Combat remains weapons-free against an unaware room enemy"),
		CanEngageUnawareEnemy(ECompanionMode::Combat));

	TestFalse(TEXT("A visible unaware enemy does not make a Normal room hot"),
		ShouldTreatRoomAsHot(ECompanionMode::Normal, false, true));
	TestFalse(TEXT("A visible unaware enemy does not make a Stealth room hot"),
		ShouldTreatRoomAsHot(ECompanionMode::Stealth, false, true));
	TestTrue(TEXT("A visible enemy makes a Combat room hot"),
		ShouldTreatRoomAsHot(ECompanionMode::Combat, false, true));
	TestTrue(TEXT("A real combat target makes every mode's room hot"),
		ShouldTreatRoomAsHot(ECompanionMode::Stealth, true, false));

	// Full acquisition gate. ECompanionMode::Normal is the mode the player sees as "Defensive".
	TestFalse(TEXT("Defensive ignores an enemy that knows neither the player nor the companion"),
		CanAcquireTarget(ECompanionMode::Normal, false, false));
	TestTrue(TEXT("Defensive acquires an enemy pressuring the player"),
		CanAcquireTarget(ECompanionMode::Normal, true, false));
	TestTrue(TEXT("Defensive acquires an enemy that has engaged the companion itself"),
		CanAcquireTarget(ECompanionMode::Normal, false, true));

	// Stealth's third argument is forced false by the caller (bWidenGate), so this asserts the value
	// the call site actually produces — not a mode-blind evaluation of the pure function.
	TestFalse(TEXT("Unbroken Stealth acquires nothing off companion awareness"),
		CanAcquireTarget(ECompanionMode::Stealth, false, false));

	TestTrue(TEXT("Combat stays weapons-free with no knowledge on either side"),
		CanAcquireTarget(ECompanionMode::Combat, false, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompanionLootThreatFacingPolicyTest,
	"Extraction.Companion.SearchExposure.LootThreatFacingPolicy", SearchRoomExposureTests::TestFlags)

bool FCompanionLootThreatFacingPolicyTest::RunTest(const FString& Parameters)
{
	using namespace CompanionSearchRoomPolicy;

	TestFalse(TEXT("Idle follow does not block threat-facing"),
		CommandYieldsThreatFacing(ECompanionCommand::None, false, false));
	TestTrue(TEXT("A moving Loot command owns facing"),
		CommandYieldsThreatFacing(ECompanionCommand::Loot, true, true));
	TestFalse(TEXT("Stationary chained Loot permits threat-facing"),
		CommandYieldsThreatFacing(ECompanionCommand::Loot, false, true));
	TestTrue(TEXT("Stationary manual Loot keeps existing facing ownership"),
		CommandYieldsThreatFacing(ECompanionCommand::Loot, false, false));
	TestTrue(TEXT("Search always owns facing"),
		CommandYieldsThreatFacing(ECompanionCommand::Explore, false, true));
	TestTrue(TEXT("Breach always owns facing"),
		CommandYieldsThreatFacing(ECompanionCommand::Breach, false, true));

	TestTrue(TEXT("Normal posture owns a ready threat while stationary looting"),
		PostureOwnsReadyThreatFacing(false, ECompanionCommand::Loot, false, true));
	TestFalse(TEXT("Stealth leaves a ready threat to low-ready watch"),
		PostureOwnsReadyThreatFacing(true, ECompanionCommand::Loot, false, true));
	TestFalse(TEXT("Movement keeps a ready threat from back-walking Loot"),
		PostureOwnsReadyThreatFacing(false, ECompanionCommand::Loot, true, true));
	return true;
}

#endif
