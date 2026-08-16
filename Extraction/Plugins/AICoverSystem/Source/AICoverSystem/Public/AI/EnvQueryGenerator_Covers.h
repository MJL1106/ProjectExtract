//// Copyright, (c) Sami Kangasmaa 2022

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "DataProviders/AIDataProvider.h"
#include "CoverSystemPublicData.h"
#include "EnvQueryGenerator_Covers.generated.h"

/**
 * Generates set of covers inside given bounds
 */
UCLASS()
class AICOVERSYSTEM_API UEnvQueryGenerator_Covers : public UEnvQueryGenerator
{
	GENERATED_BODY()

public:

	UEnvQueryGenerator_Covers();

	/** X-Y extents of bounding box to search for covers */
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue QueryBoundSize;

	/** Z extent of bounding box to search for covers */
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue QueryBoundHeight;

	/** Max |Z| delta (cm) between a cover point and the context location. Covers past this are
	 *  dropped even when inside the query box — keeps autonomous cover picks on the context's
	 *  storey (spacing ~400cm). 0 disables the filter. Applied at generation time so existing
	 *  query assets (serialized before this property existed) inherit the default. */
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	FAIDataProviderFloatValue MaxZDeltaFromContext;

	/** Context(s) that is used as origin of generation bounds */
	UPROPERTY(EditDefaultsOnly, Category = Generator)
	TSubclassOf<UEnvQueryContext> GenerateAround;

	UPROPERTY(EditDefaultsOnly, Category = Generator, AdvancedDisplay)
	FAIDataProviderBoolValue IncludeLeftCoverStanding;

	UPROPERTY(EditDefaultsOnly, Category = Generator, AdvancedDisplay)
	FAIDataProviderBoolValue IncludeRightCoverStanding;

	UPROPERTY(EditDefaultsOnly, Category = Generator, AdvancedDisplay)
	FAIDataProviderBoolValue IncludeLeftCoverCrouched;

	UPROPERTY(EditDefaultsOnly, Category = Generator, AdvancedDisplay)
	FAIDataProviderBoolValue IncludeRightCoverCrouched;

	UPROPERTY(EditDefaultsOnly, Category = Generator, AdvancedDisplay)
	FAIDataProviderBoolValue IncludeFrontCoverCrouched;

	UPROPERTY(EditDefaultsOnly, Category = Generator, AdvancedDisplay)
	FAIDataProviderBoolValue IncludeOnlyCrouched;

	virtual void GenerateItems(FEnvQueryInstance& QueryInstance) const override;

	virtual FText GetDescriptionTitle() const override;
	virtual FText GetDescriptionDetails() const override;
	
private:

	void FilterCovers(TArray<FCover>& InOut) const;
};
