// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "CoverSystem.h"
#include "CoverSystemPublicData.h"
#include "StaticCoverArchive.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeCoverSystem() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_ACoverSystem();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_ACoverSystem_NoRegister();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverBoundBoxComponent();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverBoundBoxComponent_NoRegister();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverPartitionInvokerComponent_NoRegister();
AICOVERSYSTEM_API UEnum* Z_Construct_UEnum_AICoverSystem_ECoverSystemMode();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCover();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverBuildParams();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverData();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverHandle();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverPartitionHash();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverSerializedArchive();
COREUOBJECT_API UClass* Z_Construct_UClass_UObject_NoRegister();
COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FBoxSphereBounds();
COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FVector();
ENGINE_API UClass* Z_Construct_UClass_AActor();
ENGINE_API UClass* Z_Construct_UClass_AController_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_UBillboardComponent_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_UBoxComponent();
ENGINE_API UClass* Z_Construct_UClass_USceneComponent_NoRegister();
ENGINE_API UEnum* Z_Construct_UEnum_Engine_ECollisionChannel();
NAVIGATIONSYSTEM_API UClass* Z_Construct_UClass_ANavigationData_NoRegister();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Enum ECoverSystemMode **********************************************************
static FEnumRegistrationInfo Z_Registration_Info_UEnum_ECoverSystemMode;
static UEnum* ECoverSystemMode_StaticEnum()
{
	if (!Z_Registration_Info_UEnum_ECoverSystemMode.OuterSingleton)
	{
		Z_Registration_Info_UEnum_ECoverSystemMode.OuterSingleton = GetStaticEnum(Z_Construct_UEnum_AICoverSystem_ECoverSystemMode, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("ECoverSystemMode"));
	}
	return Z_Registration_Info_UEnum_ECoverSystemMode.OuterSingleton;
}
template<> AICOVERSYSTEM_NON_ATTRIBUTED_API UEnum* StaticEnum<ECoverSystemMode>()
{
	return ECoverSystemMode_StaticEnum();
}
struct Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Enum_MetaDataParams[] = {
		{ "BlueprintType", "true" },
		{ "Dynamic.Comment", "// Covers are generated in runtime\n" },
		{ "Dynamic.Name", "ECoverSystemMode::Dynamic" },
		{ "Dynamic.ToolTip", "Covers are generated in runtime" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
		{ "Static.Comment", "// Covers are generated offline and loaded.\n" },
		{ "Static.Name", "ECoverSystemMode::Static" },
		{ "Static.ToolTip", "Covers are generated offline and loaded." },
	};
#endif // WITH_METADATA
	static constexpr UECodeGen_Private::FEnumeratorParam Enumerators[] = {
		{ "ECoverSystemMode::Dynamic", (int64)ECoverSystemMode::Dynamic },
		{ "ECoverSystemMode::Static", (int64)ECoverSystemMode::Static },
	};
	static const UECodeGen_Private::FEnumParams EnumParams;
}; // struct Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics 
const UECodeGen_Private::FEnumParams Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics::EnumParams = {
	(UObject*(*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	"ECoverSystemMode",
	"ECoverSystemMode",
	Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics::Enumerators,
	RF_Public|RF_Transient|RF_MarkAsNative,
	UE_ARRAY_COUNT(Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics::Enumerators),
	EEnumFlags::None,
	(uint8)UEnum::ECppForm::EnumClass,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics::Enum_MetaDataParams), Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics::Enum_MetaDataParams)
};
UEnum* Z_Construct_UEnum_AICoverSystem_ECoverSystemMode()
{
	if (!Z_Registration_Info_UEnum_ECoverSystemMode.InnerSingleton)
	{
		UECodeGen_Private::ConstructUEnum(Z_Registration_Info_UEnum_ECoverSystemMode.InnerSingleton, Z_Construct_UEnum_AICoverSystem_ECoverSystemMode_Statics::EnumParams);
	}
	return Z_Registration_Info_UEnum_ECoverSystemMode.InnerSingleton;
}
// ********** End Enum ECoverSystemMode ************************************************************

// ********** Begin Class UCoverBoundBoxComponent **************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UCoverBoundBoxComponent;
UClass* UCoverBoundBoxComponent::GetPrivateStaticClass()
{
	using TClass = UCoverBoundBoxComponent;
	if (!Z_Registration_Info_UClass_UCoverBoundBoxComponent.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("CoverBoundBoxComponent"),
			Z_Registration_Info_UClass_UCoverBoundBoxComponent.InnerSingleton,
			StaticRegisterNativesUCoverBoundBoxComponent,
			sizeof(TClass),
			alignof(TClass),
			TClass::StaticClassFlags,
			TClass::StaticClassCastFlags(),
			TClass::StaticConfigName(),
			(UClass::ClassConstructorType)InternalConstructor<TClass>,
			(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
			UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
			&TClass::Super::StaticClass,
			&TClass::WithinClass::StaticClass
		);
	}
	return Z_Registration_Info_UClass_UCoverBoundBoxComponent.InnerSingleton;
}
UClass* Z_Construct_UClass_UCoverBoundBoxComponent_NoRegister()
{
	return UCoverBoundBoxComponent::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UCoverBoundBoxComponent_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "HideCategories", "Physics Actor Navigation Collision Rendering PathTracing Object LOD Lighting TextureStreaming Object LOD Lighting TextureStreaming Activation Components|Activation Trigger VirtualTexture" },
		{ "IncludePath", "CoverSystem.h" },
		{ "IsBlueprintBase", "false" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
	};
#endif // WITH_METADATA

// ********** Begin Class UCoverBoundBoxComponent constinit property declarations ******************
// ********** End Class UCoverBoundBoxComponent constinit property declarations ********************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UCoverBoundBoxComponent>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UCoverBoundBoxComponent_Statics
UObject* (*const Z_Construct_UClass_UCoverBoundBoxComponent_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UBoxComponent,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverBoundBoxComponent_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UCoverBoundBoxComponent_Statics::ClassParams = {
	&UCoverBoundBoxComponent::StaticClass,
	"Engine",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x00A010A4u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverBoundBoxComponent_Statics::Class_MetaDataParams), Z_Construct_UClass_UCoverBoundBoxComponent_Statics::Class_MetaDataParams)
};
void UCoverBoundBoxComponent::StaticRegisterNativesUCoverBoundBoxComponent()
{
}
UClass* Z_Construct_UClass_UCoverBoundBoxComponent()
{
	if (!Z_Registration_Info_UClass_UCoverBoundBoxComponent.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UCoverBoundBoxComponent.OuterSingleton, Z_Construct_UClass_UCoverBoundBoxComponent_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UCoverBoundBoxComponent.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UCoverBoundBoxComponent);
UCoverBoundBoxComponent::~UCoverBoundBoxComponent() {}
// ********** End Class UCoverBoundBoxComponent ****************************************************

// ********** Begin Class ACoverSystem Function BakeStaticCovers ***********************************
#if WITH_EDITOR
struct Z_Construct_UFunction_ACoverSystem_BakeStaticCovers_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Bake the covers when CoverSystemMode is \"Static\".\n\x09 * Baked covers are loaded instead of generated\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Bake the covers when CoverSystemMode is \"Static\".\nBaked covers are loaded instead of generated" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function BakeStaticCovers constinit property declarations **********************
// ********** End Function BakeStaticCovers constinit property declarations ************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_BakeStaticCovers_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "BakeStaticCovers", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x20020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_BakeStaticCovers_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_BakeStaticCovers_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_ACoverSystem_BakeStaticCovers()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_BakeStaticCovers_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execBakeStaticCovers)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->BakeStaticCovers();
	P_NATIVE_END;
}
#endif // WITH_EDITOR
// ********** End Class ACoverSystem Function BakeStaticCovers *************************************

// ********** Begin Class ACoverSystem Function CleanPreview ***************************************
#if WITH_EDITOR
struct Z_Construct_UFunction_ACoverSystem_CleanPreview_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Debug" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Cleans up preview covers from debug draw\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Cleans up preview covers from debug draw" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function CleanPreview constinit property declarations **************************
// ********** End Function CleanPreview constinit property declarations ****************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_CleanPreview_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "CleanPreview", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x20020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_CleanPreview_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_CleanPreview_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_ACoverSystem_CleanPreview()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_CleanPreview_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execCleanPreview)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->CleanPreview();
	P_NATIVE_END;
}
#endif // WITH_EDITOR
// ********** End Class ACoverSystem Function CleanPreview *****************************************

// ********** Begin Class ACoverSystem Function GenerateCovers *************************************
struct Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics
{
	struct CoverSystem_eventGenerateCovers_Parms
	{
		bool bForceRegenerate;
		bool bAsync;
		bool bDeferGenerationIfBusy;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Launches generation process of covers manually.\n\x09 * Runs in async mode by default, so the result are not available immediately.\n\x09 * To access generated data immediately, use sync mode (bAsync=false)\n\x09 * Always runs in sync mode, if bEnableAsyncMode is set to false\n\x09 * NOTE! Sync mode will not run, if there's already async build task running.\n\x09 */" },
#endif
		{ "CPP_Default_bAsync", "true" },
		{ "CPP_Default_bDeferGenerationIfBusy", "true" },
		{ "CPP_Default_bForceRegenerate", "false" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Launches generation process of covers manually.\nRuns in async mode by default, so the result are not available immediately.\nTo access generated data immediately, use sync mode (bAsync=false)\nAlways runs in sync mode, if bEnableAsyncMode is set to false\nNOTE! Sync mode will not run, if there's already async build task running." },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function GenerateCovers constinit property declarations ************************
	static void NewProp_bForceRegenerate_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bForceRegenerate;
	static void NewProp_bAsync_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bAsync;
	static void NewProp_bDeferGenerationIfBusy_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bDeferGenerationIfBusy;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GenerateCovers constinit property declarations **************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GenerateCovers Property Definitions ***********************************
void Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bForceRegenerate_SetBit(void* Obj)
{
	((CoverSystem_eventGenerateCovers_Parms*)Obj)->bForceRegenerate = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bForceRegenerate = { "bForceRegenerate", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventGenerateCovers_Parms), &Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bForceRegenerate_SetBit, METADATA_PARAMS(0, nullptr) };
void Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bAsync_SetBit(void* Obj)
{
	((CoverSystem_eventGenerateCovers_Parms*)Obj)->bAsync = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bAsync = { "bAsync", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventGenerateCovers_Parms), &Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bAsync_SetBit, METADATA_PARAMS(0, nullptr) };
void Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bDeferGenerationIfBusy_SetBit(void* Obj)
{
	((CoverSystem_eventGenerateCovers_Parms*)Obj)->bDeferGenerationIfBusy = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bDeferGenerationIfBusy = { "bDeferGenerationIfBusy", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventGenerateCovers_Parms), &Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bDeferGenerationIfBusy_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bForceRegenerate,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bAsync,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::NewProp_bDeferGenerationIfBusy,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::PropPointers) < 2048);
// ********** End Function GenerateCovers Property Definitions *************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "GenerateCovers", 	Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::CoverSystem_eventGenerateCovers_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::CoverSystem_eventGenerateCovers_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_GenerateCovers()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_GenerateCovers_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execGenerateCovers)
{
	P_GET_UBOOL(Z_Param_bForceRegenerate);
	P_GET_UBOOL(Z_Param_bAsync);
	P_GET_UBOOL(Z_Param_bDeferGenerationIfBusy);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->GenerateCovers(Z_Param_bForceRegenerate,Z_Param_bAsync,Z_Param_bDeferGenerationIfBusy);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function GenerateCovers ***************************************

// ********** Begin Class ACoverSystem Function GetCoverData ***************************************
struct Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics
{
	struct CoverSystem_eventGetCoverData_Parms
	{
		FCoverHandle CoverHandle;
		FCoverData OutData;
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Outputs data of given cover by handle.\n\x09 * Return true if the data was found and is valid\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Outputs data of given cover by handle.\nReturn true if the data was found and is valid" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverHandle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function GetCoverData constinit property declarations **************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_CoverHandle;
	static const UECodeGen_Private::FStructPropertyParams NewProp_OutData;
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetCoverData constinit property declarations ****************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetCoverData Property Definitions *************************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_CoverHandle = { "CoverHandle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoverData_Parms, CoverHandle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverHandle_MetaData), NewProp_CoverHandle_MetaData) }; // 1932581828
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_OutData = { "OutData", nullptr, (EPropertyFlags)0x0010000000000180, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoverData_Parms, OutData), Z_Construct_UScriptStruct_FCoverData, METADATA_PARAMS(0, nullptr) }; // 2527779184
void Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystem_eventGetCoverData_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventGetCoverData_Parms), &Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_CoverHandle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_OutData,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::PropPointers) < 2048);
// ********** End Function GetCoverData Property Definitions ***************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "GetCoverData", 	Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::CoverSystem_eventGetCoverData_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54420401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::CoverSystem_eventGetCoverData_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_GetCoverData()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_GetCoverData_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execGetCoverData)
{
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_CoverHandle);
	P_GET_STRUCT_REF(FCoverData,Z_Param_Out_OutData);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=P_THIS->GetCoverData(Z_Param_Out_CoverHandle,Z_Param_Out_OutData);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function GetCoverData *****************************************

// ********** Begin Class ACoverSystem Function GetCoverDataWithinBounds ***************************
struct Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics
{
	struct CoverSystem_eventGetCoverDataWithinBounds_Parms
	{
		FBoxSphereBounds InBounds;
		TArray<FCover> OutCovers;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Runs a query to octree and retreves list of covers from entity database that are inside given bounds\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Runs a query to octree and retreves list of covers from entity database that are inside given bounds" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_InBounds_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function GetCoverDataWithinBounds constinit property declarations **************
	static const UECodeGen_Private::FStructPropertyParams NewProp_InBounds;
	static const UECodeGen_Private::FStructPropertyParams NewProp_OutCovers_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_OutCovers;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetCoverDataWithinBounds constinit property declarations ****************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetCoverDataWithinBounds Property Definitions *************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::NewProp_InBounds = { "InBounds", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoverDataWithinBounds_Parms, InBounds), Z_Construct_UScriptStruct_FBoxSphereBounds, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_InBounds_MetaData), NewProp_InBounds_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::NewProp_OutCovers_Inner = { "OutCovers", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FCover, METADATA_PARAMS(0, nullptr) }; // 1149314207
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::NewProp_OutCovers = { "OutCovers", nullptr, (EPropertyFlags)0x0010000000000180, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoverDataWithinBounds_Parms, OutCovers), EArrayPropertyFlags::None, METADATA_PARAMS(0, nullptr) }; // 1149314207
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::NewProp_InBounds,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::NewProp_OutCovers_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::NewProp_OutCovers,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::PropPointers) < 2048);
// ********** End Function GetCoverDataWithinBounds Property Definitions ***************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "GetCoverDataWithinBounds", 	Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::CoverSystem_eventGetCoverDataWithinBounds_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54C20401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::CoverSystem_eventGetCoverDataWithinBounds_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execGetCoverDataWithinBounds)
{
	P_GET_STRUCT_REF(FBoxSphereBounds,Z_Param_Out_InBounds);
	P_GET_TARRAY_REF(FCover,Z_Param_Out_OutCovers);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->GetCoverDataWithinBounds(Z_Param_Out_InBounds,Z_Param_Out_OutCovers);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function GetCoverDataWithinBounds *****************************

// ********** Begin Class ACoverSystem Function GetCoversWithinBounds ******************************
struct Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics
{
	struct CoverSystem_eventGetCoversWithinBounds_Parms
	{
		FBoxSphereBounds InBounds;
		TArray<FCoverHandle> OutCoverHandles;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Outputs all cover handles that are inside given bounds\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Outputs all cover handles that are inside given bounds" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_InBounds_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function GetCoversWithinBounds constinit property declarations *****************
	static const UECodeGen_Private::FStructPropertyParams NewProp_InBounds;
	static const UECodeGen_Private::FStructPropertyParams NewProp_OutCoverHandles_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_OutCoverHandles;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetCoversWithinBounds constinit property declarations *******************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetCoversWithinBounds Property Definitions ****************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::NewProp_InBounds = { "InBounds", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoversWithinBounds_Parms, InBounds), Z_Construct_UScriptStruct_FBoxSphereBounds, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_InBounds_MetaData), NewProp_InBounds_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::NewProp_OutCoverHandles_Inner = { "OutCoverHandles", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(0, nullptr) }; // 1932581828
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::NewProp_OutCoverHandles = { "OutCoverHandles", nullptr, (EPropertyFlags)0x0010000000000180, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoversWithinBounds_Parms, OutCoverHandles), EArrayPropertyFlags::None, METADATA_PARAMS(0, nullptr) }; // 1932581828
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::NewProp_InBounds,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::NewProp_OutCoverHandles_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::NewProp_OutCoverHandles,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::PropPointers) < 2048);
// ********** End Function GetCoversWithinBounds Property Definitions ******************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "GetCoversWithinBounds", 	Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::CoverSystem_eventGetCoversWithinBounds_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54C20401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::CoverSystem_eventGetCoversWithinBounds_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execGetCoversWithinBounds)
{
	P_GET_STRUCT_REF(FBoxSphereBounds,Z_Param_Out_InBounds);
	P_GET_TARRAY_REF(FCoverHandle,Z_Param_Out_OutCoverHandles);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->GetCoversWithinBounds(Z_Param_Out_InBounds,Z_Param_Out_OutCoverHandles);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function GetCoversWithinBounds ********************************

// ********** Begin Class ACoverSystem Function GetCoverSystem *************************************
struct Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics
{
	struct CoverSystem_eventGetCoverSystem_Parms
	{
		const UObject* WorldContext;
		ACoverSystem* ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Returns cover system from the level.\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Returns cover system from the level." },
#endif
		{ "WorldContext", "WorldContext" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_WorldContext_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function GetCoverSystem constinit property declarations ************************
	static const UECodeGen_Private::FObjectPropertyParams NewProp_WorldContext;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetCoverSystem constinit property declarations **************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetCoverSystem Property Definitions ***********************************
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::NewProp_WorldContext = { "WorldContext", nullptr, (EPropertyFlags)0x0010000000000082, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoverSystem_Parms, WorldContext), Z_Construct_UClass_UObject_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_WorldContext_MetaData), NewProp_WorldContext_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetCoverSystem_Parms, ReturnValue), Z_Construct_UClass_ACoverSystem_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::NewProp_WorldContext,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::PropPointers) < 2048);
// ********** End Function GetCoverSystem Property Definitions *************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "GetCoverSystem", 	Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::CoverSystem_eventGetCoverSystem_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04022401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::CoverSystem_eventGetCoverSystem_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_GetCoverSystem()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_GetCoverSystem_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execGetCoverSystem)
{
	P_GET_OBJECT(UObject,Z_Param_WorldContext);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(ACoverSystem**)Z_Param__Result=ACoverSystem::GetCoverSystem(Z_Param_WorldContext);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function GetCoverSystem ***************************************

// ********** Begin Class ACoverSystem Function GetOccupyingController *****************************
struct Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics
{
	struct CoverSystem_eventGetOccupyingController_Parms
	{
		FCoverHandle CoverHandle;
		AController* ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Returns a controller that is currently occupying given cover (null, if the cover is empty or invalid)\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Returns a controller that is currently occupying given cover (null, if the cover is empty or invalid)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverHandle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function GetOccupyingController constinit property declarations ****************
	static const UECodeGen_Private::FStructPropertyParams NewProp_CoverHandle;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetOccupyingController constinit property declarations ******************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetOccupyingController Property Definitions ***************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::NewProp_CoverHandle = { "CoverHandle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetOccupyingController_Parms, CoverHandle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverHandle_MetaData), NewProp_CoverHandle_MetaData) }; // 1932581828
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventGetOccupyingController_Parms, ReturnValue), Z_Construct_UClass_AController_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::NewProp_CoverHandle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::PropPointers) < 2048);
// ********** End Function GetOccupyingController Property Definitions *****************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "GetOccupyingController", 	Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::CoverSystem_eventGetOccupyingController_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54420401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::CoverSystem_eventGetOccupyingController_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_GetOccupyingController()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_GetOccupyingController_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execGetOccupyingController)
{
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_CoverHandle);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(AController**)Z_Param__Result=P_THIS->GetOccupyingController(Z_Param_Out_CoverHandle);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function GetOccupyingController *******************************

// ********** Begin Class ACoverSystem Function InvalidatePartitions *******************************
struct Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics
{
	struct CoverSystem_eventInvalidatePartitions_Parms
	{
		FBoxSphereBounds InBounds;
		bool bRegenerateImmediately;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * When using partition, invalidates an area, so it will be regenerated.\n\x09 * When bRegenerateImmediately is false, the partitions are marked dirty and are rebuilt later, when true, generation is started right away.\n\x09 */" },
#endif
		{ "CPP_Default_bRegenerateImmediately", "false" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "When using partition, invalidates an area, so it will be regenerated.\nWhen bRegenerateImmediately is false, the partitions are marked dirty and are rebuilt later, when true, generation is started right away." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_InBounds_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function InvalidatePartitions constinit property declarations ******************
	static const UECodeGen_Private::FStructPropertyParams NewProp_InBounds;
	static void NewProp_bRegenerateImmediately_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bRegenerateImmediately;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function InvalidatePartitions constinit property declarations ********************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function InvalidatePartitions Property Definitions *****************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::NewProp_InBounds = { "InBounds", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventInvalidatePartitions_Parms, InBounds), Z_Construct_UScriptStruct_FBoxSphereBounds, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_InBounds_MetaData), NewProp_InBounds_MetaData) };
void Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::NewProp_bRegenerateImmediately_SetBit(void* Obj)
{
	((CoverSystem_eventInvalidatePartitions_Parms*)Obj)->bRegenerateImmediately = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::NewProp_bRegenerateImmediately = { "bRegenerateImmediately", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventInvalidatePartitions_Parms), &Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::NewProp_bRegenerateImmediately_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::NewProp_InBounds,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::NewProp_bRegenerateImmediately,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::PropPointers) < 2048);
// ********** End Function InvalidatePartitions Property Definitions *******************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "InvalidatePartitions", 	Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::CoverSystem_eventInvalidatePartitions_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04C20401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::CoverSystem_eventInvalidatePartitions_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_InvalidatePartitions()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_InvalidatePartitions_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execInvalidatePartitions)
{
	P_GET_STRUCT_REF(FBoxSphereBounds,Z_Param_Out_InBounds);
	P_GET_UBOOL(Z_Param_bRegenerateImmediately);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->InvalidatePartitions(Z_Param_Out_InBounds,Z_Param_bRegenerateImmediately);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function InvalidatePartitions *********************************

// ********** Begin Class ACoverSystem Function IsAsyncWorkerRunning *******************************
struct Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics
{
	struct CoverSystem_eventIsAsyncWorkerRunning_Parms
	{
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Checks if async worker task is currently running\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Checks if async worker task is currently running" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function IsAsyncWorkerRunning constinit property declarations ******************
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function IsAsyncWorkerRunning constinit property declarations ********************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function IsAsyncWorkerRunning Property Definitions *****************************
void Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystem_eventIsAsyncWorkerRunning_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventIsAsyncWorkerRunning_Parms), &Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::PropPointers) < 2048);
// ********** End Function IsAsyncWorkerRunning Property Definitions *******************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "IsAsyncWorkerRunning", 	Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::CoverSystem_eventIsAsyncWorkerRunning_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::CoverSystem_eventIsAsyncWorkerRunning_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execIsAsyncWorkerRunning)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=P_THIS->IsAsyncWorkerRunning();
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function IsAsyncWorkerRunning *********************************

// ********** Begin Class ACoverSystem Function IsCoverOccupied ************************************
struct Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics
{
	struct CoverSystem_eventIsCoverOccupied_Parms
	{
		FCoverHandle CoverHandle;
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* Checks if cover is occupied\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* Checks if cover is occupied" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverHandle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function IsCoverOccupied constinit property declarations ***********************
	static const UECodeGen_Private::FStructPropertyParams NewProp_CoverHandle;
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function IsCoverOccupied constinit property declarations *************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function IsCoverOccupied Property Definitions **********************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::NewProp_CoverHandle = { "CoverHandle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventIsCoverOccupied_Parms, CoverHandle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverHandle_MetaData), NewProp_CoverHandle_MetaData) }; // 1932581828
void Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystem_eventIsCoverOccupied_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventIsCoverOccupied_Parms), &Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::NewProp_CoverHandle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::PropPointers) < 2048);
// ********** End Function IsCoverOccupied Property Definitions ************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "IsCoverOccupied", 	Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::CoverSystem_eventIsCoverOccupied_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54420401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::CoverSystem_eventIsCoverOccupied_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_IsCoverOccupied()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_IsCoverOccupied_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execIsCoverOccupied)
{
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_CoverHandle);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=P_THIS->IsCoverOccupied(Z_Param_Out_CoverHandle);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function IsCoverOccupied **************************************

// ********** Begin Class ACoverSystem Function NotifyNavigationRebuilt ****************************
struct Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09* Manually notifies that navigation has been rebuilt which triggers regeneration.\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Manually notifies that navigation has been rebuilt which triggers regeneration." },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function NotifyNavigationRebuilt constinit property declarations ***************
// ********** End Function NotifyNavigationRebuilt constinit property declarations *****************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "NotifyNavigationRebuilt", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execNotifyNavigationRebuilt)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->NotifyNavigationRebuilt();
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function NotifyNavigationRebuilt ******************************

// ********** Begin Class ACoverSystem Function OccupyCover ****************************************
struct Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics
{
	struct CoverSystem_eventOccupyCover_Parms
	{
		AController* Controller;
		FCoverHandle CoverHandle;
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* Occupies cover for given controller\n\x09* AI's can test, if certain cover is already in use\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* Occupies cover for given controller\n* AI's can test, if certain cover is already in use" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverHandle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function OccupyCover constinit property declarations ***************************
	static const UECodeGen_Private::FObjectPropertyParams NewProp_Controller;
	static const UECodeGen_Private::FStructPropertyParams NewProp_CoverHandle;
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function OccupyCover constinit property declarations *****************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function OccupyCover Property Definitions **************************************
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_Controller = { "Controller", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventOccupyCover_Parms, Controller), Z_Construct_UClass_AController_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_CoverHandle = { "CoverHandle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventOccupyCover_Parms, CoverHandle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverHandle_MetaData), NewProp_CoverHandle_MetaData) }; // 1932581828
void Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystem_eventOccupyCover_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventOccupyCover_Parms), &Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_Controller,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_CoverHandle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::PropPointers) < 2048);
// ********** End Function OccupyCover Property Definitions ****************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "OccupyCover", 	Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::CoverSystem_eventOccupyCover_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04420401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::CoverSystem_eventOccupyCover_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_OccupyCover()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_OccupyCover_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execOccupyCover)
{
	P_GET_OBJECT(AController,Z_Param_Controller);
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_CoverHandle);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=P_THIS->OccupyCover(Z_Param_Controller,Z_Param_Out_CoverHandle);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function OccupyCover ******************************************

// ********** Begin Class ACoverSystem Function OnNavigationGenerationFinished *********************
struct Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics
{
	struct CoverSystem_eventOnNavigationGenerationFinished_Parms
	{
		ANavigationData* NavData;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
	};
#endif // WITH_METADATA

// ********** Begin Function OnNavigationGenerationFinished constinit property declarations ********
	static const UECodeGen_Private::FObjectPropertyParams NewProp_NavData;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function OnNavigationGenerationFinished constinit property declarations **********
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function OnNavigationGenerationFinished Property Definitions *******************
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::NewProp_NavData = { "NavData", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventOnNavigationGenerationFinished_Parms, NavData), Z_Construct_UClass_ANavigationData_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::NewProp_NavData,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::PropPointers) < 2048);
// ********** End Function OnNavigationGenerationFinished Property Definitions *********************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "OnNavigationGenerationFinished", 	Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::CoverSystem_eventOnNavigationGenerationFinished_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x00040401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::CoverSystem_eventOnNavigationGenerationFinished_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execOnNavigationGenerationFinished)
{
	P_GET_OBJECT(ANavigationData,Z_Param_NavData);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->OnNavigationGenerationFinished(Z_Param_NavData);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function OnNavigationGenerationFinished ***********************

// ********** Begin Class ACoverSystem Function Preview ********************************************
#if WITH_EDITOR
struct Z_Construct_UFunction_ACoverSystem_Preview_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Debug" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Runs generation process in the editor to preview covers.\n\x09 * When the cover mode is \"Static\" this will only debug draw covers from baked data.\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Runs generation process in the editor to preview covers.\nWhen the cover mode is \"Static\" this will only debug draw covers from baked data." },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function Preview constinit property declarations *******************************
// ********** End Function Preview constinit property declarations *********************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_Preview_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "Preview", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x20020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_Preview_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_Preview_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_ACoverSystem_Preview()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_Preview_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execPreview)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->Preview();
	P_NATIVE_END;
}
#endif // WITH_EDITOR
// ********** End Class ACoverSystem Function Preview **********************************************

// ********** Begin Class ACoverSystem Function RebaseOrigin ***************************************
struct Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics
{
	struct CoverSystem_eventRebaseOrigin_Parms
	{
		FVector NewOrigin;
		bool bRegenerate;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Rebases the origin of the system.\n\x09 * Use this in large worlds to move cover generation with the player\n\x09 */" },
#endif
		{ "CPP_Default_bRegenerate", "true" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Rebases the origin of the system.\nUse this in large worlds to move cover generation with the player" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_NewOrigin_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function RebaseOrigin constinit property declarations **************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_NewOrigin;
	static void NewProp_bRegenerate_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bRegenerate;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function RebaseOrigin constinit property declarations ****************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function RebaseOrigin Property Definitions *************************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::NewProp_NewOrigin = { "NewOrigin", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventRebaseOrigin_Parms, NewOrigin), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_NewOrigin_MetaData), NewProp_NewOrigin_MetaData) };
void Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::NewProp_bRegenerate_SetBit(void* Obj)
{
	((CoverSystem_eventRebaseOrigin_Parms*)Obj)->bRegenerate = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::NewProp_bRegenerate = { "bRegenerate", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystem_eventRebaseOrigin_Parms), &Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::NewProp_bRegenerate_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::NewProp_NewOrigin,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::NewProp_bRegenerate,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::PropPointers) < 2048);
// ********** End Function RebaseOrigin Property Definitions ***************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "RebaseOrigin", 	Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::CoverSystem_eventRebaseOrigin_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04C20401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::CoverSystem_eventRebaseOrigin_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_RebaseOrigin()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_RebaseOrigin_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execRebaseOrigin)
{
	P_GET_STRUCT_REF(FVector,Z_Param_Out_NewOrigin);
	P_GET_UBOOL(Z_Param_bRegenerate);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->RebaseOrigin(Z_Param_Out_NewOrigin,Z_Param_bRegenerate);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function RebaseOrigin *****************************************

// ********** Begin Class ACoverSystem Function ResetStaticCovers **********************************
#if WITH_EDITOR
struct Z_Construct_UFunction_ACoverSystem_ResetStaticCovers_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "CallInEditor", "true" },
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09* Empties static covers that were earlier baked\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Empties static covers that were earlier baked" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function ResetStaticCovers constinit property declarations *********************
// ********** End Function ResetStaticCovers constinit property declarations ***********************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_ResetStaticCovers_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "ResetStaticCovers", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x20020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_ResetStaticCovers_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_ResetStaticCovers_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_ACoverSystem_ResetStaticCovers()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_ResetStaticCovers_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execResetStaticCovers)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->ResetStaticCovers();
	P_NATIVE_END;
}
#endif // WITH_EDITOR
// ********** End Class ACoverSystem Function ResetStaticCovers ************************************

// ********** Begin Class ACoverSystem Function UnOccupyCover **************************************
struct Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics
{
	struct CoverSystem_eventUnOccupyCover_Parms
	{
		FCoverHandle CoverHandle;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Releases cover, so it is no more occupied\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Releases cover, so it is no more occupied" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverHandle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function UnOccupyCover constinit property declarations *************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_CoverHandle;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function UnOccupyCover constinit property declarations ***************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function UnOccupyCover Property Definitions ************************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::NewProp_CoverHandle = { "CoverHandle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventUnOccupyCover_Parms, CoverHandle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverHandle_MetaData), NewProp_CoverHandle_MetaData) }; // 1932581828
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::NewProp_CoverHandle,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::PropPointers) < 2048);
// ********** End Function UnOccupyCover Property Definitions **************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "UnOccupyCover", 	Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::CoverSystem_eventUnOccupyCover_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04420401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::CoverSystem_eventUnOccupyCover_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_UnOccupyCover()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_UnOccupyCover_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execUnOccupyCover)
{
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_CoverHandle);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->UnOccupyCover(Z_Param_Out_CoverHandle);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function UnOccupyCover ****************************************

// ********** Begin Class ACoverSystem Function UnOccupyCoverFromController ************************
struct Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics
{
	struct CoverSystem_eventUnOccupyCoverFromController_Parms
	{
		FCoverHandle CoverHandle;
		AController* Controller;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Releases cover, so it is no more occupied\n\x09 * Specialized version of UnOccupyCover which unoccupies the cover only, if it is already occupied by given controller\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Releases cover, so it is no more occupied\nSpecialized version of UnOccupyCover which unoccupies the cover only, if it is already occupied by given controller" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverHandle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function UnOccupyCoverFromController constinit property declarations ***********
	static const UECodeGen_Private::FStructPropertyParams NewProp_CoverHandle;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_Controller;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function UnOccupyCoverFromController constinit property declarations *************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function UnOccupyCoverFromController Property Definitions **********************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::NewProp_CoverHandle = { "CoverHandle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventUnOccupyCoverFromController_Parms, CoverHandle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverHandle_MetaData), NewProp_CoverHandle_MetaData) }; // 1932581828
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::NewProp_Controller = { "Controller", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystem_eventUnOccupyCoverFromController_Parms, Controller), Z_Construct_UClass_AController_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::NewProp_CoverHandle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::NewProp_Controller,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::PropPointers) < 2048);
// ********** End Function UnOccupyCoverFromController Property Definitions ************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_ACoverSystem, nullptr, "UnOccupyCoverFromController", 	Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::CoverSystem_eventUnOccupyCoverFromController_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04420401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::Function_MetaDataParams), Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::CoverSystem_eventUnOccupyCoverFromController_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(ACoverSystem::execUnOccupyCoverFromController)
{
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_CoverHandle);
	P_GET_OBJECT(AController,Z_Param_Controller);
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->UnOccupyCoverFromController(Z_Param_Out_CoverHandle,Z_Param_Controller);
	P_NATIVE_END;
}
// ********** End Class ACoverSystem Function UnOccupyCoverFromController **************************

// ********** Begin Class ACoverSystem *************************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_ACoverSystem;
UClass* ACoverSystem::GetPrivateStaticClass()
{
	using TClass = ACoverSystem;
	if (!Z_Registration_Info_UClass_ACoverSystem.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("CoverSystem"),
			Z_Registration_Info_UClass_ACoverSystem.InnerSingleton,
			StaticRegisterNativesACoverSystem,
			sizeof(TClass),
			alignof(TClass),
			TClass::StaticClassFlags,
			TClass::StaticClassCastFlags(),
			TClass::StaticConfigName(),
			(UClass::ClassConstructorType)InternalConstructor<TClass>,
			(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
			UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
			&TClass::Super::StaticClass,
			&TClass::WithinClass::StaticClass
		);
	}
	return Z_Registration_Info_UClass_ACoverSystem.InnerSingleton;
}
UClass* Z_Construct_UClass_ACoverSystem_NoRegister()
{
	return ACoverSystem::GetPrivateStaticClass();
}
struct Z_Construct_UClass_ACoverSystem_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Manager class of the cover system.\n * Place this into the persistent level\n */" },
#endif
		{ "HideCategories", "Navigation Physics Collision Input Movement Rendering HLOD WorldPartition" },
		{ "IncludePath", "CoverSystem.h" },
		{ "IsBlueprintBase", "true" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Manager class of the cover system.\nPlace this into the persistent level" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_CoverSystemMode_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09* Mode of the cover system (dynamic or static)\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Mode of the cover system (dynamic or static)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bUsePartitions_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* Should the system be split into partitions?\n\x09* Use this for large open world levels to generate smaller chunks at time.\n\x09* When partitioning is toggled on, cover system generates areas only around partition invokers and supports parallel generation of multiple partitions\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* Should the system be split into partitions?\n* Use this for large open world levels to generate smaller chunks at time.\n* When partitioning is toggled on, cover system generates areas only around partition invokers and supports parallel generation of multiple partitions" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionSize_MetaData[] = {
		{ "Category", "Settings" },
		{ "ClampMin", "4096.0" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Size of single partition in world units.\n" },
#endif
		{ "EditCondition", "bUsePartitions" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Size of single partition in world units." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionGenerationRange_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Generation range of the partitions from any partition invoker\n" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Generation range of the partitions from any partition invoker" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionGenerationRangeZ_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Generation range in up/down axis of the partitions from any partition invoker\n" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Generation range in up/down axis of the partitions from any partition invoker" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bUsePlayerPawnsAsPartitionInvokers_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* When toggled on, player pawns are used to activate partition generation around generation radius.\n\x09* Use cover partition invoker component to manually specify generation origins if you turn this off\n\x09*/" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* When toggled on, player pawns are used to activate partition generation around generation radius.\n* Use cover partition invoker component to manually specify generation origins if you turn this off" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bGenerateOnBeginPlay_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Should generation process started on begin play?\n" },
#endif
		{ "EditCondition", "CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should generation process started on begin play?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bGenerateOnNavigationRebuild_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Should generation process started automatically after navigation rebuild?\n" },
#endif
		{ "EditCondition", "CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should generation process started automatically after navigation rebuild?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_MaxNavigationRegenerateInterval_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// When bGenerateOnNavigationRebuild is toggled on, how often the regeneration can be started?\n" },
#endif
		{ "EditCondition", "CoverSystemMode==ECoverSystemMode::Dynamic && bGenerateOnNavigationRebuild" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "When bGenerateOnNavigationRebuild is toggled on, how often the regeneration can be started?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bEnableAsyncMode_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Should async build tasks used to generate covers? You should disable this only for debugging or when you generate covers manually during loading screens.\n" },
#endif
		{ "EditCondition", "CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should async build tasks used to generate covers? You should disable this only for debugging or when you generate covers manually during loading screens." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bAsyncLoadStaticCovers_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* Should baked covers loaded and inserted into octree in background thread when level is started?\n\x09* When this is toggled off, cover data is ready on first frame but may cause a hitch in larger levels when starting up the level.\n\x09*/" },
#endif
		{ "EditCondition", "CoverSystemMode==ECoverSystemMode::Static" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* Should baked covers loaded and inserted into octree in background thread when level is started?\n* When this is toggled off, cover data is ready on first frame but may cause a hitch in larger levels when starting up the level." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionBuildTaskQueueSize_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* When using partitions, how many partition build task can be in queue?\n\x09* Use negative or zero to allow unlimited queue size\n\x09*/" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* When using partitions, how many partition build task can be in queue?\n* Use negative or zero to allow unlimited queue size" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bSortPartitionsByPriority_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Should dirty partitions sorted in order, so newly activated partitions or partitions that have longest time since regeneration are queued first?\n\x09 */" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should dirty partitions sorted in order, so newly activated partitions or partitions that have longest time since regeneration are queued first?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionEvaluateInterval_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// How often active partitions are updated around invokers?\n" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "How often active partitions are updated around invokers?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionRegenerateInterval_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// How often partition regenerate is evaluated. All dirty partitions (or lately activated) are queued to task manager for processing.\n" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "How often partition regenerate is evaluated. All dirty partitions (or lately activated) are queued to task manager for processing." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_DeactivePartitionDestroyDelay_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * When partition goes deactive, it will be removed from the system if it doesn't come active again in set time.\n\x09 * This prevents partitions to be destroyed and created immediately again when invoker is moving back and forth\n\x09 */" },
#endif
		{ "EditCondition", "bUsePartitions && CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "When partition goes deactive, it will be removed from the system if it doesn't come active again in set time.\nThis prevents partitions to be destroyed and created immediately again when invoker is moving back and forth" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_WorkerThreadTimeBudgetPerFrame_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* How much we give time budget for worker thread per frame to work on threaded tasks in milliseconds.\n\x09* We want to prevent cover generator task running simultaneously with physics as they both require access to physics scene which slowdown the both.\n\x09* Cover system ticks in post physics, so the generator starts running in background after physics have been calculated.\n\x09* Optimal time budget is leftover time of your frame after physics simulation (PostPhysicsTick + Idle), so background process would run when game thread is running post physics or is idle and waiting for GPU.\n\x09* Giving less time budget generate covers slower but may help to improve performance if your game is running heavy physics simulation or you do lot of physics related actions in pre-physics tick.\n\x09* Time budgeting is not used when loading static covers, as they aren't dependant on physics scene.\n\x09*/" },
#endif
		{ "EditCondition", "CoverSystemMode==ECoverSystemMode::Dynamic" },
		{ "EditConditionHides", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* How much we give time budget for worker thread per frame to work on threaded tasks in milliseconds.\n* We want to prevent cover generator task running simultaneously with physics as they both require access to physics scene which slowdown the both.\n* Cover system ticks in post physics, so the generator starts running in background after physics have been calculated.\n* Optimal time budget is leftover time of your frame after physics simulation (PostPhysicsTick + Idle), so background process would run when game thread is running post physics or is idle and waiting for GPU.\n* Giving less time budget generate covers slower but may help to improve performance if your game is running heavy physics simulation or you do lot of physics related actions in pre-physics tick.\n* Time budgeting is not used when loading static covers, as they aren't dependant on physics scene." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_BuildParams_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Parameters for cover generation\n" },
#endif
		{ "ExposeOnSpawn", "" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Parameters for cover generation" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TraceChannel_MetaData[] = {
		{ "Category", "Settings" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/*\n\x09* Trace channel to use when finding blocking geometry\n\x09* To customize cover generation, you can create a new trace channel in project settings\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "* Trace channel to use when finding blocking geometry\n* To customize cover generation, you can create a new trace channel in project settings" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bDebugDraw_MetaData[] = {
		{ "Category", "Debug" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Toggles debug draw on/off\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Toggles debug draw on/off" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_DebugDrawDistance_MetaData[] = {
		{ "Category", "Debug" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Culling distance for debug draw\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Culling distance for debug draw" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bDrawOctreeBounds_MetaData[] = {
		{ "Category", "Debug" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Should debug draw octree root node bounds?\n" },
#endif
		{ "EditCondition", "bDebugDraw" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should debug draw octree root node bounds?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RootSceneComponent_MetaData[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09* Root component of the cover system\n\x09*/" },
#endif
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Root component of the cover system" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SystemBoundsComponent_MetaData[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Component that defines bounds where covers are generated\n\x09 */" },
#endif
		{ "DisplayName", "Cover System Bounds" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Component that defines bounds where covers are generated" },
#endif
	};
#if WITH_EDITORONLY_DATA
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SpriteComponent_MetaData[] = {
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
	};
#endif // WITH_EDITORONLY_DATA
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_StaticCoverArchive_MetaData[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Archive where the static covers are stored (serialization/deserialization)\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Archive where the static covers are stored (serialization/deserialization)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionedStaticCoverArchives_MetaData[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Archive where the static covers are stored when using partition (serialization/deserialization)\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Archive where the static covers are stored when using partition (serialization/deserialization)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PartitionInvokers_MetaData[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Registered invokers\n" },
#endif
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/CoverSystem.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Registered invokers" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class ACoverSystem constinit property declarations *****************************
	static const UECodeGen_Private::FBytePropertyParams NewProp_CoverSystemMode_Underlying;
	static const UECodeGen_Private::FEnumPropertyParams NewProp_CoverSystemMode;
	static void NewProp_bUsePartitions_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bUsePartitions;
	static const UECodeGen_Private::FDoublePropertyParams NewProp_PartitionSize;
	static const UECodeGen_Private::FDoublePropertyParams NewProp_PartitionGenerationRange;
	static const UECodeGen_Private::FDoublePropertyParams NewProp_PartitionGenerationRangeZ;
	static void NewProp_bUsePlayerPawnsAsPartitionInvokers_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bUsePlayerPawnsAsPartitionInvokers;
	static void NewProp_bGenerateOnBeginPlay_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bGenerateOnBeginPlay;
	static void NewProp_bGenerateOnNavigationRebuild_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bGenerateOnNavigationRebuild;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_MaxNavigationRegenerateInterval;
	static void NewProp_bEnableAsyncMode_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bEnableAsyncMode;
	static void NewProp_bAsyncLoadStaticCovers_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bAsyncLoadStaticCovers;
	static const UECodeGen_Private::FIntPropertyParams NewProp_PartitionBuildTaskQueueSize;
	static void NewProp_bSortPartitionsByPriority_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bSortPartitionsByPriority;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_PartitionEvaluateInterval;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_PartitionRegenerateInterval;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_DeactivePartitionDestroyDelay;
	static const UECodeGen_Private::FDoublePropertyParams NewProp_WorkerThreadTimeBudgetPerFrame;
	static const UECodeGen_Private::FStructPropertyParams NewProp_BuildParams;
	static const UECodeGen_Private::FBytePropertyParams NewProp_TraceChannel;
	static void NewProp_bDebugDraw_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bDebugDraw;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_DebugDrawDistance;
	static void NewProp_bDrawOctreeBounds_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bDrawOctreeBounds;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_RootSceneComponent;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_SystemBoundsComponent;
#if WITH_EDITORONLY_DATA
	static const UECodeGen_Private::FObjectPropertyParams NewProp_SpriteComponent;
#endif // WITH_EDITORONLY_DATA
	static const UECodeGen_Private::FStructPropertyParams NewProp_StaticCoverArchive;
	static const UECodeGen_Private::FStructPropertyParams NewProp_PartitionedStaticCoverArchives_ValueProp;
	static const UECodeGen_Private::FStructPropertyParams NewProp_PartitionedStaticCoverArchives_Key_KeyProp;
	static const UECodeGen_Private::FMapPropertyParams NewProp_PartitionedStaticCoverArchives;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_PartitionInvokers_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_PartitionInvokers;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class ACoverSystem constinit property declarations *******************************
	static constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {
#if WITH_EDITOR
		{ .NameUTF8 = UTF8TEXT("BakeStaticCovers"), .Pointer = &ACoverSystem::execBakeStaticCovers },
		{ .NameUTF8 = UTF8TEXT("CleanPreview"), .Pointer = &ACoverSystem::execCleanPreview },
#endif // WITH_EDITOR
		{ .NameUTF8 = UTF8TEXT("GenerateCovers"), .Pointer = &ACoverSystem::execGenerateCovers },
		{ .NameUTF8 = UTF8TEXT("GetCoverData"), .Pointer = &ACoverSystem::execGetCoverData },
		{ .NameUTF8 = UTF8TEXT("GetCoverDataWithinBounds"), .Pointer = &ACoverSystem::execGetCoverDataWithinBounds },
		{ .NameUTF8 = UTF8TEXT("GetCoversWithinBounds"), .Pointer = &ACoverSystem::execGetCoversWithinBounds },
		{ .NameUTF8 = UTF8TEXT("GetCoverSystem"), .Pointer = &ACoverSystem::execGetCoverSystem },
		{ .NameUTF8 = UTF8TEXT("GetOccupyingController"), .Pointer = &ACoverSystem::execGetOccupyingController },
		{ .NameUTF8 = UTF8TEXT("InvalidatePartitions"), .Pointer = &ACoverSystem::execInvalidatePartitions },
		{ .NameUTF8 = UTF8TEXT("IsAsyncWorkerRunning"), .Pointer = &ACoverSystem::execIsAsyncWorkerRunning },
		{ .NameUTF8 = UTF8TEXT("IsCoverOccupied"), .Pointer = &ACoverSystem::execIsCoverOccupied },
		{ .NameUTF8 = UTF8TEXT("NotifyNavigationRebuilt"), .Pointer = &ACoverSystem::execNotifyNavigationRebuilt },
		{ .NameUTF8 = UTF8TEXT("OccupyCover"), .Pointer = &ACoverSystem::execOccupyCover },
		{ .NameUTF8 = UTF8TEXT("OnNavigationGenerationFinished"), .Pointer = &ACoverSystem::execOnNavigationGenerationFinished },
#if WITH_EDITOR
		{ .NameUTF8 = UTF8TEXT("Preview"), .Pointer = &ACoverSystem::execPreview },
#endif // WITH_EDITOR
		{ .NameUTF8 = UTF8TEXT("RebaseOrigin"), .Pointer = &ACoverSystem::execRebaseOrigin },
#if WITH_EDITOR
		{ .NameUTF8 = UTF8TEXT("ResetStaticCovers"), .Pointer = &ACoverSystem::execResetStaticCovers },
#endif // WITH_EDITOR
		{ .NameUTF8 = UTF8TEXT("UnOccupyCover"), .Pointer = &ACoverSystem::execUnOccupyCover },
		{ .NameUTF8 = UTF8TEXT("UnOccupyCoverFromController"), .Pointer = &ACoverSystem::execUnOccupyCoverFromController },
	};
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
#if WITH_EDITOR
		{ &Z_Construct_UFunction_ACoverSystem_BakeStaticCovers, "BakeStaticCovers" }, // 669458379
		{ &Z_Construct_UFunction_ACoverSystem_CleanPreview, "CleanPreview" }, // 2589802266
#endif // WITH_EDITOR
		{ &Z_Construct_UFunction_ACoverSystem_GenerateCovers, "GenerateCovers" }, // 2705415920
		{ &Z_Construct_UFunction_ACoverSystem_GetCoverData, "GetCoverData" }, // 4240445995
		{ &Z_Construct_UFunction_ACoverSystem_GetCoverDataWithinBounds, "GetCoverDataWithinBounds" }, // 977820260
		{ &Z_Construct_UFunction_ACoverSystem_GetCoversWithinBounds, "GetCoversWithinBounds" }, // 3263511765
		{ &Z_Construct_UFunction_ACoverSystem_GetCoverSystem, "GetCoverSystem" }, // 1838004994
		{ &Z_Construct_UFunction_ACoverSystem_GetOccupyingController, "GetOccupyingController" }, // 202634562
		{ &Z_Construct_UFunction_ACoverSystem_InvalidatePartitions, "InvalidatePartitions" }, // 3286387733
		{ &Z_Construct_UFunction_ACoverSystem_IsAsyncWorkerRunning, "IsAsyncWorkerRunning" }, // 4258948801
		{ &Z_Construct_UFunction_ACoverSystem_IsCoverOccupied, "IsCoverOccupied" }, // 1565956514
		{ &Z_Construct_UFunction_ACoverSystem_NotifyNavigationRebuilt, "NotifyNavigationRebuilt" }, // 3770901779
		{ &Z_Construct_UFunction_ACoverSystem_OccupyCover, "OccupyCover" }, // 1551895535
		{ &Z_Construct_UFunction_ACoverSystem_OnNavigationGenerationFinished, "OnNavigationGenerationFinished" }, // 1432656485
#if WITH_EDITOR
		{ &Z_Construct_UFunction_ACoverSystem_Preview, "Preview" }, // 2089412330
#endif // WITH_EDITOR
		{ &Z_Construct_UFunction_ACoverSystem_RebaseOrigin, "RebaseOrigin" }, // 3906564273
#if WITH_EDITOR
		{ &Z_Construct_UFunction_ACoverSystem_ResetStaticCovers, "ResetStaticCovers" }, // 3192122541
#endif // WITH_EDITOR
		{ &Z_Construct_UFunction_ACoverSystem_UnOccupyCover, "UnOccupyCover" }, // 4264124279
		{ &Z_Construct_UFunction_ACoverSystem_UnOccupyCoverFromController, "UnOccupyCoverFromController" }, // 3111298330
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<ACoverSystem>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_ACoverSystem_Statics

// ********** Begin Class ACoverSystem Property Definitions ****************************************
const UECodeGen_Private::FBytePropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_CoverSystemMode_Underlying = { "UnderlyingType", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Byte, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, nullptr, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FEnumPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_CoverSystemMode = { "CoverSystemMode", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Enum, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, CoverSystemMode), Z_Construct_UEnum_AICoverSystem_ECoverSystemMode, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_CoverSystemMode_MetaData), NewProp_CoverSystemMode_MetaData) }; // 3461850975
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePartitions_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bUsePartitions = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePartitions = { "bUsePartitions", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePartitions_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bUsePartitions_MetaData), NewProp_bUsePartitions_MetaData) };
const UECodeGen_Private::FDoublePropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionSize = { "PartitionSize", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Double, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionSize), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionSize_MetaData), NewProp_PartitionSize_MetaData) };
const UECodeGen_Private::FDoublePropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionGenerationRange = { "PartitionGenerationRange", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Double, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionGenerationRange), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionGenerationRange_MetaData), NewProp_PartitionGenerationRange_MetaData) };
const UECodeGen_Private::FDoublePropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionGenerationRangeZ = { "PartitionGenerationRangeZ", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Double, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionGenerationRangeZ), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionGenerationRangeZ_MetaData), NewProp_PartitionGenerationRangeZ_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePlayerPawnsAsPartitionInvokers_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bUsePlayerPawnsAsPartitionInvokers = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePlayerPawnsAsPartitionInvokers = { "bUsePlayerPawnsAsPartitionInvokers", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePlayerPawnsAsPartitionInvokers_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bUsePlayerPawnsAsPartitionInvokers_MetaData), NewProp_bUsePlayerPawnsAsPartitionInvokers_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnBeginPlay_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bGenerateOnBeginPlay = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnBeginPlay = { "bGenerateOnBeginPlay", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnBeginPlay_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bGenerateOnBeginPlay_MetaData), NewProp_bGenerateOnBeginPlay_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnNavigationRebuild_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bGenerateOnNavigationRebuild = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnNavigationRebuild = { "bGenerateOnNavigationRebuild", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnNavigationRebuild_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bGenerateOnNavigationRebuild_MetaData), NewProp_bGenerateOnNavigationRebuild_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_MaxNavigationRegenerateInterval = { "MaxNavigationRegenerateInterval", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, MaxNavigationRegenerateInterval), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_MaxNavigationRegenerateInterval_MetaData), NewProp_MaxNavigationRegenerateInterval_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bEnableAsyncMode_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bEnableAsyncMode = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bEnableAsyncMode = { "bEnableAsyncMode", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bEnableAsyncMode_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bEnableAsyncMode_MetaData), NewProp_bEnableAsyncMode_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bAsyncLoadStaticCovers_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bAsyncLoadStaticCovers = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bAsyncLoadStaticCovers = { "bAsyncLoadStaticCovers", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bAsyncLoadStaticCovers_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bAsyncLoadStaticCovers_MetaData), NewProp_bAsyncLoadStaticCovers_MetaData) };
const UECodeGen_Private::FIntPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionBuildTaskQueueSize = { "PartitionBuildTaskQueueSize", nullptr, (EPropertyFlags)0x00200c0000000015, UECodeGen_Private::EPropertyGenFlags::Int, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionBuildTaskQueueSize), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionBuildTaskQueueSize_MetaData), NewProp_PartitionBuildTaskQueueSize_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bSortPartitionsByPriority_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bSortPartitionsByPriority = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bSortPartitionsByPriority = { "bSortPartitionsByPriority", nullptr, (EPropertyFlags)0x00200c0000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bSortPartitionsByPriority_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bSortPartitionsByPriority_MetaData), NewProp_bSortPartitionsByPriority_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionEvaluateInterval = { "PartitionEvaluateInterval", nullptr, (EPropertyFlags)0x00200c0000000015, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionEvaluateInterval), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionEvaluateInterval_MetaData), NewProp_PartitionEvaluateInterval_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionRegenerateInterval = { "PartitionRegenerateInterval", nullptr, (EPropertyFlags)0x00200c0000000015, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionRegenerateInterval), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionRegenerateInterval_MetaData), NewProp_PartitionRegenerateInterval_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_DeactivePartitionDestroyDelay = { "DeactivePartitionDestroyDelay", nullptr, (EPropertyFlags)0x00200c0000000015, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, DeactivePartitionDestroyDelay), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_DeactivePartitionDestroyDelay_MetaData), NewProp_DeactivePartitionDestroyDelay_MetaData) };
const UECodeGen_Private::FDoublePropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_WorkerThreadTimeBudgetPerFrame = { "WorkerThreadTimeBudgetPerFrame", nullptr, (EPropertyFlags)0x00200c0000000015, UECodeGen_Private::EPropertyGenFlags::Double, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, WorkerThreadTimeBudgetPerFrame), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_WorkerThreadTimeBudgetPerFrame_MetaData), NewProp_WorkerThreadTimeBudgetPerFrame_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_BuildParams = { "BuildParams", nullptr, (EPropertyFlags)0x0021080000000015, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, BuildParams), Z_Construct_UScriptStruct_FCoverBuildParams, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_BuildParams_MetaData), NewProp_BuildParams_MetaData) }; // 300256297
const UECodeGen_Private::FBytePropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_TraceChannel = { "TraceChannel", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Byte, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, TraceChannel), Z_Construct_UEnum_Engine_ECollisionChannel, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TraceChannel_MetaData), NewProp_TraceChannel_MetaData) }; // 838391399
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDebugDraw_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bDebugDraw = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDebugDraw = { "bDebugDraw", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDebugDraw_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bDebugDraw_MetaData), NewProp_bDebugDraw_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_DebugDrawDistance = { "DebugDrawDistance", nullptr, (EPropertyFlags)0x0020080000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, DebugDrawDistance), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_DebugDrawDistance_MetaData), NewProp_DebugDrawDistance_MetaData) };
void Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDrawOctreeBounds_SetBit(void* Obj)
{
	((ACoverSystem*)Obj)->bDrawOctreeBounds = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDrawOctreeBounds = { "bDrawOctreeBounds", nullptr, (EPropertyFlags)0x0020080000000005, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(ACoverSystem), &Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDrawOctreeBounds_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bDrawOctreeBounds_MetaData), NewProp_bDrawOctreeBounds_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_RootSceneComponent = { "RootSceneComponent", nullptr, (EPropertyFlags)0x01240800000a001d, UECodeGen_Private::EPropertyGenFlags::Object | UECodeGen_Private::EPropertyGenFlags::ObjectPtr, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, RootSceneComponent), Z_Construct_UClass_USceneComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RootSceneComponent_MetaData), NewProp_RootSceneComponent_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_SystemBoundsComponent = { "SystemBoundsComponent", nullptr, (EPropertyFlags)0x01240800000a001d, UECodeGen_Private::EPropertyGenFlags::Object | UECodeGen_Private::EPropertyGenFlags::ObjectPtr, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, SystemBoundsComponent), Z_Construct_UClass_UCoverBoundBoxComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SystemBoundsComponent_MetaData), NewProp_SystemBoundsComponent_MetaData) };
#if WITH_EDITORONLY_DATA
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_SpriteComponent = { "SpriteComponent", nullptr, (EPropertyFlags)0x0124080800080008, UECodeGen_Private::EPropertyGenFlags::Object | UECodeGen_Private::EPropertyGenFlags::ObjectPtr, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, SpriteComponent), Z_Construct_UClass_UBillboardComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SpriteComponent_MetaData), NewProp_SpriteComponent_MetaData) };
#endif // WITH_EDITORONLY_DATA
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_StaticCoverArchive = { "StaticCoverArchive", nullptr, (EPropertyFlags)0x0040000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, StaticCoverArchive), Z_Construct_UScriptStruct_FCoverSerializedArchive, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_StaticCoverArchive_MetaData), NewProp_StaticCoverArchive_MetaData) }; // 1241907070
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionedStaticCoverArchives_ValueProp = { "PartitionedStaticCoverArchives", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 1, Z_Construct_UScriptStruct_FCoverSerializedArchive, METADATA_PARAMS(0, nullptr) }; // 1241907070
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionedStaticCoverArchives_Key_KeyProp = { "PartitionedStaticCoverArchives_Key", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FCoverPartitionHash, METADATA_PARAMS(0, nullptr) }; // 240377161
const UECodeGen_Private::FMapPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionedStaticCoverArchives = { "PartitionedStaticCoverArchives", nullptr, (EPropertyFlags)0x0040000000000000, UECodeGen_Private::EPropertyGenFlags::Map, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionedStaticCoverArchives), EMapPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionedStaticCoverArchives_MetaData), NewProp_PartitionedStaticCoverArchives_MetaData) }; // 240377161 1241907070
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionInvokers_Inner = { "PartitionInvokers", nullptr, (EPropertyFlags)0x0104000000080008, UECodeGen_Private::EPropertyGenFlags::Object | UECodeGen_Private::EPropertyGenFlags::ObjectPtr, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UClass_UCoverPartitionInvokerComponent_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionInvokers = { "PartitionInvokers", nullptr, (EPropertyFlags)0x0144008000002008, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ACoverSystem, PartitionInvokers), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PartitionInvokers_MetaData), NewProp_PartitionInvokers_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_ACoverSystem_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_CoverSystemMode_Underlying,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_CoverSystemMode,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePartitions,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionSize,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionGenerationRange,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionGenerationRangeZ,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bUsePlayerPawnsAsPartitionInvokers,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnBeginPlay,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bGenerateOnNavigationRebuild,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_MaxNavigationRegenerateInterval,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bEnableAsyncMode,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bAsyncLoadStaticCovers,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionBuildTaskQueueSize,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bSortPartitionsByPriority,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionEvaluateInterval,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionRegenerateInterval,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_DeactivePartitionDestroyDelay,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_WorkerThreadTimeBudgetPerFrame,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_BuildParams,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_TraceChannel,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDebugDraw,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_DebugDrawDistance,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_bDrawOctreeBounds,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_RootSceneComponent,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_SystemBoundsComponent,
#if WITH_EDITORONLY_DATA
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_SpriteComponent,
#endif // WITH_EDITORONLY_DATA
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_StaticCoverArchive,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionedStaticCoverArchives_ValueProp,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionedStaticCoverArchives_Key_KeyProp,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionedStaticCoverArchives,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionInvokers_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_ACoverSystem_Statics::NewProp_PartitionInvokers,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_ACoverSystem_Statics::PropPointers) < 2048);
// ********** End Class ACoverSystem Property Definitions ******************************************
UObject* (*const Z_Construct_UClass_ACoverSystem_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_AActor,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_ACoverSystem_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_ACoverSystem_Statics::ClassParams = {
	&ACoverSystem::StaticClass,
	"Engine",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	Z_Construct_UClass_ACoverSystem_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	UE_ARRAY_COUNT(Z_Construct_UClass_ACoverSystem_Statics::PropPointers),
	0,
	0x009000A4u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_ACoverSystem_Statics::Class_MetaDataParams), Z_Construct_UClass_ACoverSystem_Statics::Class_MetaDataParams)
};
void ACoverSystem::StaticRegisterNativesACoverSystem()
{
	UClass* Class = ACoverSystem::StaticClass();
	FNativeFunctionRegistrar::RegisterFunctions(Class, MakeConstArrayView(Z_Construct_UClass_ACoverSystem_Statics::Funcs));
}
UClass* Z_Construct_UClass_ACoverSystem()
{
	if (!Z_Registration_Info_UClass_ACoverSystem.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_ACoverSystem.OuterSingleton, Z_Construct_UClass_ACoverSystem_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_ACoverSystem.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, ACoverSystem);
ACoverSystem::~ACoverSystem() {}
// ********** End Class ACoverSystem ***************************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_Statics
{
	static constexpr FEnumRegisterCompiledInInfo EnumInfo[] = {
		{ ECoverSystemMode_StaticEnum, TEXT("ECoverSystemMode"), &Z_Registration_Info_UEnum_ECoverSystemMode, CONSTRUCT_RELOAD_VERSION_INFO(FEnumReloadVersionInfo, 3461850975U) },
	};
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UCoverBoundBoxComponent, UCoverBoundBoxComponent::StaticClass, TEXT("UCoverBoundBoxComponent"), &Z_Registration_Info_UClass_UCoverBoundBoxComponent, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UCoverBoundBoxComponent), 4241620697U) },
		{ Z_Construct_UClass_ACoverSystem, ACoverSystem::StaticClass, TEXT("ACoverSystem"), &Z_Registration_Info_UClass_ACoverSystem, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(ACoverSystem), 311352611U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_3351575103{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_Statics::EnumInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystem_h__Script_AICoverSystem_Statics::EnumInfo),
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
