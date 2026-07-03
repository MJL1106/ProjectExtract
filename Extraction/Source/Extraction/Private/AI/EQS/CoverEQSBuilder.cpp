// CoverEQSBuilder -- editor-only: creates/overwrites four cover EQS query assets.

#include "CoverEQSBuilder.h"

#if WITH_EDITOR

#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"

// Plugin (AICoverSystem)
#include "AI/EnvQueryGenerator_Covers.h"
#include "AI/Tests/EnvQueryTest_FreeCover.h"
#include "AI/Tests/EnvQueryTest_ParallelToCover.h"
#include "AI/Tests/EnvQueryTest_ProvidesCover.h"

// Project
#include "EnvQueryTest_CoverPostVacate.h"
#include "EnvQueryTest_CoverArc.h"
#include "EnvQueryTest_CoverPeekable.h"
#include "EnvQueryTest_CoverAllySpacing.h"
#include "EnvQueryTest_CoverSameWall.h"
#include "EnvQueryTest_CoverSideFlag.h"
#include "EnvQueryContext_CombatTarget.h"
#include "EnvQueryTest_DistanceBand.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogCoverEQSBuilder, Log, All);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static UEnvQuery* GetOrCreateQuery(const FString& AssetName, const FString& BasePath, bool& bOutExisted, FString& OutLine)
{
	const FString PackagePath = BasePath / AssetName;
	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutLine = FString::Printf(TEXT("ERROR: could not create package %s"), *PackagePath);
		bOutExisted = false;
		return nullptr;
	}
	Package->FullyLoad();

	// Try to find an existing asset inside this package
	UEnvQuery* Query = FindObject<UEnvQuery>(Package, *AssetName);
	if (Query)
	{
		// Overwrite in-place: clear options
		Query->GetOptionsMutable().Empty();
		bOutExisted = true;
	}
	else
	{
		Query = NewObject<UEnvQuery>(Package, *AssetName, RF_Public | RF_Standalone);
		bOutExisted = false;
	}
	return Query;
}

static void FinaliseAsset(UEnvQuery* Query, UPackage* Package, const FString& AssetName, bool bExisted, int32 TestCount, FString& OutLine)
{
	FAssetRegistryModule::AssetCreated(Query);
	Package->MarkPackageDirty();

	const FString Filename = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	const bool bSaved = UPackage::SavePackage(Package, Query, *Filename, SaveArgs);

	const TCHAR* Verb = bExisted ? TEXT("updated") : TEXT("created");
	if (bSaved)
	{
		OutLine = FString::Printf(TEXT("%s: %s, %d tests"), *AssetName, Verb, TestCount);
	}
	else
	{
		OutLine = FString::Printf(TEXT("%s: %s in memory (%d tests) but SAVE FAILED"), *AssetName, Verb, TestCount);
	}
}

// ---------------------------------------------------------------------------
// Generator factory
// ---------------------------------------------------------------------------

static UEnvQueryGenerator_Covers* MakeGenerator(UEnvQueryOption* Opt, float BoundSize, float BoundHeight)
{
	UEnvQueryGenerator_Covers* Gen = NewObject<UEnvQueryGenerator_Covers>(Opt);
	Gen->QueryBoundSize.DefaultValue = BoundSize;
	Gen->QueryBoundHeight.DefaultValue = BoundHeight;
	Gen->GenerateAround = UEnvQueryContext_Querier::StaticClass();
	return Gen;
}

// ---------------------------------------------------------------------------
// Test factories
// ---------------------------------------------------------------------------

static UEnvQueryTest_FreeCover* MakeFreeCover(UEnvQueryOption* Opt, bool bSelfOccupy)
{
	UEnvQueryTest_FreeCover* T = NewObject<UEnvQueryTest_FreeCover>(Opt);
	// bFreeOnSelfOccupy is protected in the plugin header; set via reflection.
	FBoolProperty* Prop = CastField<FBoolProperty>(
		T->GetClass()->FindPropertyByName(TEXT("bFreeOnSelfOccupy")));
	if (Prop)
	{
		Prop->SetPropertyValue_InContainer(T, bSelfOccupy);
	}
	T->TestPurpose = EEnvTestPurpose::Filter;
	return T;
}

static UEnvQueryTest_CoverPostVacate* MakePostVacate(UEnvQueryOption* Opt, float Cooldown)
{
	UEnvQueryTest_CoverPostVacate* T = NewObject<UEnvQueryTest_CoverPostVacate>(Opt);
	T->CooldownSeconds.DefaultValue = Cooldown;
	T->TestPurpose = EEnvTestPurpose::Filter;
	return T;
}

static UEnvQueryTest_CoverArc* MakeCoverArc(UEnvQueryOption* Opt, float HalfAngleDeg)
{
	UEnvQueryTest_CoverArc* T = NewObject<UEnvQueryTest_CoverArc>(Opt);
	T->ArcHalfAngleDeg.DefaultValue = HalfAngleDeg;
	T->TestPurpose = EEnvTestPurpose::Filter;
	return T;
}

static UEnvQueryTest_CoverPeekable* MakePeekable(UEnvQueryOption* Opt)
{
	UEnvQueryTest_CoverPeekable* T = NewObject<UEnvQueryTest_CoverPeekable>(Opt);
	T->TestPurpose = EEnvTestPurpose::Filter;
	return T;
}

static UEnvQueryTest_ParallelToCover* MakeParallel(UEnvQueryOption* Opt)
{
	UEnvQueryTest_ParallelToCover* T = NewObject<UEnvQueryTest_ParallelToCover>(Opt);
	T->Context = UEnvQueryContext_CombatTarget::StaticClass();
	T->TestMode = EEnvTestParallerCover::Dot2D;
	T->TestPurpose = EEnvTestPurpose::FilterAndScore;
	T->FilterType = EEnvTestFilterType::Minimum;
	T->FloatValueMin.DefaultValue = 0.f;
	T->ScoringEquation = EEnvTestScoreEquation::Linear;
	T->ScoringFactor.DefaultValue = 1.f;
	return T;
}

static UEnvQueryTest_Distance* MakeDistanceQuerier(UEnvQueryOption* Opt, float Factor)
{
	UEnvQueryTest_Distance* T = NewObject<UEnvQueryTest_Distance>(Opt);
	T->TestMode = EEnvTestDistance::Distance2D;
	T->DistanceTo = UEnvQueryContext_Querier::StaticClass();
	T->TestPurpose = EEnvTestPurpose::Score;
	T->ScoringEquation = EEnvTestScoreEquation::InverseLinear;
	T->ScoringFactor.DefaultValue = Factor;
	return T;
}

static UEnvQueryTest_CoverAllySpacing* MakeAllySpacing(UEnvQueryOption* Opt, float Spacing, float Factor)
{
	UEnvQueryTest_CoverAllySpacing* T = NewObject<UEnvQueryTest_CoverAllySpacing>(Opt);
	T->MinSpacing.DefaultValue = Spacing;
	T->TestPurpose = EEnvTestPurpose::Score;
	T->ScoringFactor.DefaultValue = Factor;
	return T;
}

static UEnvQueryTest_ProvidesCover* MakeProvidesCover(UEnvQueryOption* Opt)
{
	UEnvQueryTest_ProvidesCover* T = NewObject<UEnvQueryTest_ProvidesCover>(Opt);
	T->Context = UEnvQueryContext_CombatTarget::StaticClass();
	T->TraceChannel = ECC_Visibility;
	T->TestPurpose = EEnvTestPurpose::Filter;
	return T;
}

static UEnvQueryTest_DistanceBand* MakeDistanceBand(UEnvQueryOption* Opt,
	TSubclassOf<UEnvQueryContext> Context, float Min, float Max,
	EEnvTestPurpose::Type Purpose, float FilterMin, float Factor)
{
	UEnvQueryTest_DistanceBand* T = NewObject<UEnvQueryTest_DistanceBand>(Opt);
	T->DistanceTo = Context;
	T->BandMin.DefaultValue = Min;
	T->BandMax.DefaultValue = Max;
	T->TestPurpose = Purpose;
	T->FilterType = EEnvTestFilterType::Minimum;
	T->FloatValueMin.DefaultValue = FilterMin;
	T->ScoringFactor.DefaultValue = Factor;
	return T;
}

static UEnvQueryTest_CoverSameWall* MakeSameWall(UEnvQueryOption* Opt)
{
	UEnvQueryTest_CoverSameWall* T = NewObject<UEnvQueryTest_CoverSameWall>(Opt);
	T->TestPurpose = EEnvTestPurpose::Filter;
	return T;
}

static UEnvQueryTest_CoverSideFlag* MakeSideFlag(UEnvQueryOption* Opt, float Factor)
{
	UEnvQueryTest_CoverSideFlag* T = NewObject<UEnvQueryTest_CoverSideFlag>(Opt);
	T->TestPurpose = EEnvTestPurpose::Score;
	T->ScoringEquation = EEnvTestScoreEquation::Linear;
	T->ScoringFactor.DefaultValue = Factor;
	return T;
}

// ---------------------------------------------------------------------------
// Query builders
// ---------------------------------------------------------------------------

static constexpr const TCHAR* CoverAssetPath = TEXT("/Game/Core/AI/Cover");

static bool BuildDefensive(FString& OutLine)
{
	const FString Name = TEXT("EQS_Cover_Defensive");
	bool bExisted = false;
	UEnvQuery* Q = GetOrCreateQuery(Name, CoverAssetPath, bExisted, OutLine);
	if (!Q) return false;

	UEnvQueryOption* Opt = NewObject<UEnvQueryOption>(Q);
	Opt->Generator = MakeGenerator(Opt, 1200.f, 500.f);

	Opt->Tests.Add(MakeFreeCover(Opt, true));              // 1
	Opt->Tests.Add(MakePostVacate(Opt, 6.f));              // 2
	Opt->Tests.Add(MakeCoverArc(Opt, 75.f));               // 3  (cheap dot math — before traces)
	Opt->Tests.Add(MakePeekable(Opt));                     // 4
	Opt->Tests.Add(MakeParallel(Opt));                     // 5
	Opt->Tests.Add(MakeSideFlag(Opt, 1.5f));               // 6
	Opt->Tests.Add(MakeDistanceQuerier(Opt, 1.f));         // 7
	Opt->Tests.Add(MakeAllySpacing(Opt, 300.f, 1.f));      // 8
	Opt->Tests.Add(MakeProvidesCover(Opt));                // 9

	Q->GetOptionsMutable().Add(Opt);

	FinaliseAsset(Q, Q->GetPackage(), Name, bExisted, Opt->Tests.Num(), OutLine);
	return true;
}

static bool BuildAdvance(FString& OutLine)
{
	const FString Name = TEXT("EQS_Cover_Advance");
	bool bExisted = false;
	UEnvQuery* Q = GetOrCreateQuery(Name, CoverAssetPath, bExisted, OutLine);
	if (!Q) return false;

	UEnvQueryOption* Opt = NewObject<UEnvQueryOption>(Q);
	Opt->Generator = MakeGenerator(Opt, 1600.f, 500.f);

	Opt->Tests.Add(MakeFreeCover(Opt, true));                                            // 1
	Opt->Tests.Add(MakePostVacate(Opt, 6.f));                                            // 2
	Opt->Tests.Add(MakePeekable(Opt));                                                   // 3
	Opt->Tests.Add(MakeProvidesCover(Opt));                                              // 4
	Opt->Tests.Add(MakeParallel(Opt));                                                   // 5
	Opt->Tests.Add(MakeSideFlag(Opt, 1.5f));                                             // 6
	Opt->Tests.Add(MakeDistanceBand(Opt,                                                 // 7
		UEnvQueryContext_CombatTarget::StaticClass(),
		500.f, 1200.f,
		EEnvTestPurpose::FilterAndScore, 0.15f, 1.5f));
	Opt->Tests.Add(MakeDistanceQuerier(Opt, 0.3f));                                      // 8
	Opt->Tests.Add(MakeAllySpacing(Opt, 300.f, 1.f));                                    // 9

	Q->GetOptionsMutable().Add(Opt);

	FinaliseAsset(Q, Q->GetPackage(), Name, bExisted, Opt->Tests.Num(), OutLine);
	return true;
}

static bool BuildShuffle(FString& OutLine)
{
	const FString Name = TEXT("EQS_Cover_Shuffle");
	bool bExisted = false;
	UEnvQuery* Q = GetOrCreateQuery(Name, CoverAssetPath, bExisted, OutLine);
	if (!Q) return false;

	UEnvQueryOption* Opt = NewObject<UEnvQueryOption>(Q);
	Opt->Generator = MakeGenerator(Opt, 300.f, 200.f);

	// 1: FreeCover with bFreeOnSelfOccupy = FALSE
	Opt->Tests.Add(MakeFreeCover(Opt, false));

	// 2: CoverSameWall (defaults)
	Opt->Tests.Add(MakeSameWall(Opt));

	// 3: DistanceBand to Querier
	Opt->Tests.Add(MakeDistanceBand(Opt,
		UEnvQueryContext_Querier::StaticClass(),
		75.f, 125.f,
		EEnvTestPurpose::FilterAndScore, 0.1f, 1.f));

	// 4: ProvidesCover
	Opt->Tests.Add(MakeProvidesCover(Opt));

	Q->GetOptionsMutable().Add(Opt);

	FinaliseAsset(Q, Q->GetPackage(), Name, bExisted, Opt->Tests.Num(), OutLine);
	return true;
}

static bool BuildProtective(FString& OutLine)
{
	const FString Name = TEXT("EQS_Cover_Protective");
	bool bExisted = false;
	UEnvQuery* Q = GetOrCreateQuery(Name, CoverAssetPath, bExisted, OutLine);
	if (!Q) return false;

	UEnvQueryOption* Opt = NewObject<UEnvQueryOption>(Q);
	Opt->Generator = MakeGenerator(Opt, 1200.f, 500.f);

	Opt->Tests.Add(MakeFreeCover(Opt, true));              // 1
	Opt->Tests.Add(MakePostVacate(Opt, 6.f));              // 2
	Opt->Tests.Add(MakeParallel(Opt));                     // 3
	Opt->Tests.Add(MakeDistanceQuerier(Opt, 1.f));         // 4
	Opt->Tests.Add(MakeProvidesCover(Opt));                // 5

	Q->GetOptionsMutable().Add(Opt);

	FinaliseAsset(Q, Q->GetPackage(), Name, bExisted, Opt->Tests.Num(), OutLine);
	return true;
}

#endif // WITH_EDITOR

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool UCoverEQSBuilder::BuildCoverQueries(FString& OutReport)
{
#if WITH_EDITOR
	OutReport.Empty();
	bool bAllOK = true;

	auto AppendResult = [&](bool bOK, const FString& Line)
	{
		if (!bOK) bAllOK = false;
		if (!OutReport.IsEmpty()) OutReport += TEXT("\n");
		OutReport += Line;
		if (bOK)
		{
			UE_LOG(LogCoverEQSBuilder, Log, TEXT("%s"), *Line);
		}
		else
		{
			UE_LOG(LogCoverEQSBuilder, Error, TEXT("%s"), *Line);
		}
	};

	FString Line;

	AppendResult(BuildDefensive(Line), Line);
	AppendResult(BuildAdvance(Line), Line);
	AppendResult(BuildShuffle(Line), Line);
	AppendResult(BuildProtective(Line), Line);

	return bAllOK;
#else
	OutReport = TEXT("CoverEQSBuilder is editor-only.");
	return false;
#endif
}
