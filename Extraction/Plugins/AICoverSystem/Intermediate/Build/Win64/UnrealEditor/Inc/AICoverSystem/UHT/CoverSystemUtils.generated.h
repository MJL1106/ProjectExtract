// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "CoverSystemUtils.h"

#ifdef AICOVERSYSTEM_CoverSystemUtils_generated_h
#error "CoverSystemUtils.generated.h already included, missing '#pragma once' in CoverSystemUtils.h"
#endif
#define AICOVERSYSTEM_CoverSystemUtils_generated_h

#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class UBTNode;
class UEnvQueryInstanceBlueprintWrapper;
struct FBlackboardKeySelector;
struct FCover;
struct FCoverHandle;

// ********** Begin Class UCoverSystemUtils ********************************************************
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_RPC_WRAPPERS_NO_PURE_DECLS \
	DECLARE_FUNCTION(execGetEnvironmentQueryResultsAsCovers); \
	DECLARE_FUNCTION(execIsCoverHandleValid); \
	DECLARE_FUNCTION(execIsCoverValid); \
	DECLARE_FUNCTION(execSetBlackboardValueAsCover); \
	DECLARE_FUNCTION(execGetBlackboardValueAsCover);


struct Z_Construct_UClass_UCoverSystemUtils_Statics;
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverSystemUtils_NoRegister();

#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesUCoverSystemUtils(); \
	friend struct ::Z_Construct_UClass_UCoverSystemUtils_Statics; \
	static UClass* GetPrivateStaticClass(); \
	friend AICOVERSYSTEM_API UClass* ::Z_Construct_UClass_UCoverSystemUtils_NoRegister(); \
public: \
	DECLARE_CLASS2(UCoverSystemUtils, UBlueprintFunctionLibrary, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/AICoverSystem"), Z_Construct_UClass_UCoverSystemUtils_NoRegister) \
	DECLARE_SERIALIZER(UCoverSystemUtils)


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_ENHANCED_CONSTRUCTORS \
	/** Standard constructor, called after all reflected properties have been initialized */ \
	NO_API UCoverSystemUtils(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get()); \
	/** Deleted move- and copy-constructors, should never be used */ \
	UCoverSystemUtils(UCoverSystemUtils&&) = delete; \
	UCoverSystemUtils(const UCoverSystemUtils&) = delete; \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UCoverSystemUtils); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UCoverSystemUtils); \
	DEFINE_DEFAULT_OBJECT_INITIALIZER_CONSTRUCTOR_CALL(UCoverSystemUtils) \
	NO_API virtual ~UCoverSystemUtils();


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_25_PROLOG
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_RPC_WRAPPERS_NO_PURE_DECLS \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_INCLASS_NO_PURE_DECLS \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h_28_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class UCoverSystemUtils;

// ********** End Class UCoverSystemUtils **********************************************************

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h

PRAGMA_ENABLE_DEPRECATION_WARNINGS
