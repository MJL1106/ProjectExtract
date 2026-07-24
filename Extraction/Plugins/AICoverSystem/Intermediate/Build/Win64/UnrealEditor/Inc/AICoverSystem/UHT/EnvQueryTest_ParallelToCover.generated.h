// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "AI/Tests/EnvQueryTest_ParallelToCover.h"

#ifdef AICOVERSYSTEM_EnvQueryTest_ParallelToCover_generated_h
#error "EnvQueryTest_ParallelToCover.generated.h already included, missing '#pragma once' in EnvQueryTest_ParallelToCover.h"
#endif
#define AICOVERSYSTEM_EnvQueryTest_ParallelToCover_generated_h

#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

// ********** Begin Class UEnvQueryTest_ParallelToCover ********************************************
struct Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics;
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_ParallelToCover_NoRegister();

#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h_23_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesUEnvQueryTest_ParallelToCover(); \
	friend struct ::Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics; \
	static UClass* GetPrivateStaticClass(); \
	friend AICOVERSYSTEM_API UClass* ::Z_Construct_UClass_UEnvQueryTest_ParallelToCover_NoRegister(); \
public: \
	DECLARE_CLASS2(UEnvQueryTest_ParallelToCover, UEnvQueryTest, COMPILED_IN_FLAGS(0), CASTCLASS_None, TEXT("/Script/AICoverSystem"), Z_Construct_UClass_UEnvQueryTest_ParallelToCover_NoRegister) \
	DECLARE_SERIALIZER(UEnvQueryTest_ParallelToCover)


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h_23_ENHANCED_CONSTRUCTORS \
	/** Deleted move- and copy-constructors, should never be used */ \
	UEnvQueryTest_ParallelToCover(UEnvQueryTest_ParallelToCover&&) = delete; \
	UEnvQueryTest_ParallelToCover(const UEnvQueryTest_ParallelToCover&) = delete; \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UEnvQueryTest_ParallelToCover); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UEnvQueryTest_ParallelToCover); \
	DEFINE_DEFAULT_CONSTRUCTOR_CALL(UEnvQueryTest_ParallelToCover) \
	NO_API virtual ~UEnvQueryTest_ParallelToCover();


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h_20_PROLOG
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h_23_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h_23_INCLASS_NO_PURE_DECLS \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h_23_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class UEnvQueryTest_ParallelToCover;

// ********** End Class UEnvQueryTest_ParallelToCover **********************************************

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h

// ********** Begin Enum EEnvTestParallerCover *****************************************************
#define FOREACH_ENUM_EENVTESTPARALLERCOVER(op) \
	op(EEnvTestParallerCover::Dot3D) \
	op(EEnvTestParallerCover::Dot2D) 

enum class EEnvTestParallerCover : uint8;
template<> struct TIsUEnumClass<EEnvTestParallerCover> { enum { Value = true }; };
template<> AICOVERSYSTEM_NON_ATTRIBUTED_API UEnum* StaticEnum<EEnvTestParallerCover>();
// ********** End Enum EEnvTestParallerCover *******************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
