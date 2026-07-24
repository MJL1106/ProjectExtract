// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "CoverSystem.h"

#ifdef AICOVERSYSTEM_CoverSystem_generated_h
#error "CoverSystem.generated.h already included, missing '#pragma once' in CoverSystem.h"
#endif
#define AICOVERSYSTEM_CoverSystem_generated_h

#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
class AController;
class ACoverSystem;
class ANavigationData;
class UObject;
struct FCover;
struct FCoverData;
struct FCoverHandle;

// ********** Begin Class UCoverBoundBoxComponent **************************************************
struct Z_Construct_UClass_UCoverBoundBoxComponent_Statics;
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverBoundBoxComponent_NoRegister();

#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_33_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesUCoverBoundBoxComponent(); \
	friend struct ::Z_Construct_UClass_UCoverBoundBoxComponent_Statics; \
	static UClass* GetPrivateStaticClass(); \
	friend AICOVERSYSTEM_API UClass* ::Z_Construct_UClass_UCoverBoundBoxComponent_NoRegister(); \
public: \
	DECLARE_CLASS2(UCoverBoundBoxComponent, UBoxComponent, COMPILED_IN_FLAGS(0 | CLASS_Config), CASTCLASS_None, TEXT("/Script/AICoverSystem"), Z_Construct_UClass_UCoverBoundBoxComponent_NoRegister) \
	DECLARE_SERIALIZER(UCoverBoundBoxComponent)


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_33_ENHANCED_CONSTRUCTORS \
	/** Deleted move- and copy-constructors, should never be used */ \
	UCoverBoundBoxComponent(UCoverBoundBoxComponent&&) = delete; \
	UCoverBoundBoxComponent(const UCoverBoundBoxComponent&) = delete; \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, UCoverBoundBoxComponent); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(UCoverBoundBoxComponent); \
	DEFINE_DEFAULT_CONSTRUCTOR_CALL(UCoverBoundBoxComponent) \
	NO_API virtual ~UCoverBoundBoxComponent();


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_30_PROLOG
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_33_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_33_INCLASS_NO_PURE_DECLS \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_33_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class UCoverBoundBoxComponent;

// ********** End Class UCoverBoundBoxComponent ****************************************************

// ********** Begin Class ACoverSystem *************************************************************
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_RPC_WRAPPERS_NO_PURE_DECLS \
	DECLARE_FUNCTION(execIsAsyncWorkerRunning); \
	DECLARE_FUNCTION(execIsCoverOccupied); \
	DECLARE_FUNCTION(execGetOccupyingController); \
	DECLARE_FUNCTION(execUnOccupyCoverFromController); \
	DECLARE_FUNCTION(execUnOccupyCover); \
	DECLARE_FUNCTION(execOccupyCover); \
	DECLARE_FUNCTION(execGetCoverDataWithinBounds); \
	DECLARE_FUNCTION(execGetCoversWithinBounds); \
	DECLARE_FUNCTION(execGetCoverData); \
	DECLARE_FUNCTION(execRebaseOrigin); \
	DECLARE_FUNCTION(execNotifyNavigationRebuilt); \
	DECLARE_FUNCTION(execInvalidatePartitions); \
	DECLARE_FUNCTION(execGenerateCovers); \
	DECLARE_FUNCTION(execGetCoverSystem); \
	DECLARE_FUNCTION(execOnNavigationGenerationFinished);


#if WITH_EDITOR
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_RPC_WRAPPERS_NO_PURE_DECLS_EOD \
	DECLARE_FUNCTION(execResetStaticCovers); \
	DECLARE_FUNCTION(execBakeStaticCovers); \
	DECLARE_FUNCTION(execPreview); \
	DECLARE_FUNCTION(execCleanPreview);
#else // WITH_EDITOR
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_RPC_WRAPPERS_NO_PURE_DECLS_EOD
#endif // WITH_EDITOR


struct Z_Construct_UClass_ACoverSystem_Statics;
AICOVERSYSTEM_API UClass* Z_Construct_UClass_ACoverSystem_NoRegister();

#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesACoverSystem(); \
	friend struct ::Z_Construct_UClass_ACoverSystem_Statics; \
	static UClass* GetPrivateStaticClass(); \
	friend AICOVERSYSTEM_API UClass* ::Z_Construct_UClass_ACoverSystem_NoRegister(); \
public: \
	DECLARE_CLASS2(ACoverSystem, AActor, COMPILED_IN_FLAGS(0 | CLASS_Config), CASTCLASS_None, TEXT("/Script/AICoverSystem"), Z_Construct_UClass_ACoverSystem_NoRegister) \
	DECLARE_SERIALIZER(ACoverSystem)


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_ENHANCED_CONSTRUCTORS \
	/** Deleted move- and copy-constructors, should never be used */ \
	ACoverSystem(ACoverSystem&&) = delete; \
	ACoverSystem(const ACoverSystem&) = delete; \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, ACoverSystem); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(ACoverSystem); \
	DEFINE_DEFAULT_CONSTRUCTOR_CALL(ACoverSystem) \
	NO_API virtual ~ACoverSystem();


#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_50_PROLOG
#define FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_RPC_WRAPPERS_NO_PURE_DECLS \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_RPC_WRAPPERS_NO_PURE_DECLS_EOD \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_INCLASS_NO_PURE_DECLS \
	FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h_53_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class ACoverSystem;

// ********** End Class ACoverSystem ***************************************************************

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h

// ********** Begin Enum ECoverSystemMode **********************************************************
#define FOREACH_ENUM_ECOVERSYSTEMMODE(op) \
	op(ECoverSystemMode::Dynamic) \
	op(ECoverSystemMode::Static) 

enum class ECoverSystemMode : uint8;
template<> struct TIsUEnumClass<ECoverSystemMode> { enum { Value = true }; };
template<> AICOVERSYSTEM_NON_ATTRIBUTED_API UEnum* StaticEnum<ECoverSystemMode>();
// ********** End Enum ECoverSystemMode ************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
