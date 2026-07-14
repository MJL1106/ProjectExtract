#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCompanionDownedDirectionSmoothingContractTest,
	"Extraction.Companion.Animation.DownedDirectionSmoothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FCompanionDownedDirectionSmoothingContractTest::RunTest(const FString& Parameters)
{
	const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),
		TEXT("Source/Extraction/Private/Animation/CompanionAnimInstance.cpp")));
	FString Source;
	if (!TestTrue(TEXT("Companion animation source is readable"), FFileHelper::LoadFileToString(Source, *SourcePath)))
	{
		return false;
	}

	const int32 DirectionAssignment = Source.Find(TEXT("Direction ="));
	const int32 ShortestDelta = Source.Find(TEXT("FindDeltaAngleDegrees"), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, DirectionAssignment);
	const int32 Interpolation = Source.Find(TEXT("FInterpTo"), ESearchCase::CaseSensitive,
		ESearchDir::FromStart, ShortestDelta);

	TestTrue(TEXT("Direction smoothing uses the shortest angular path"),
		DirectionAssignment != INDEX_NONE && ShortestDelta > DirectionAssignment);
	TestTrue(TEXT("Direction smoothing interpolates instead of snapping"),
		ShortestDelta != INDEX_NONE && Interpolation > ShortestDelta);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
