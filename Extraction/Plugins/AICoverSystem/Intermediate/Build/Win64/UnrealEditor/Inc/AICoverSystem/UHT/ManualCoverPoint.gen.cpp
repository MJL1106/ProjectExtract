// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "ManualCoverPoint.h"
#include "CoverSystemPublicData.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeManualCoverPoint() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_AManualCoverPoint();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_AManualCoverPoint_NoRegister();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverData();
ENGINE_API UClass* Z_Construct_UClass_AActor();
ENGINE_API UClass* Z_Construct_UClass_UArrowComponent_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_UCapsuleComponent_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_USceneComponent_NoRegister();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class AManualCoverPoint Function GetCoverData **********************************
struct Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics
{
	struct ManualCoverPoint_eventGetCoverData_Parms
	{
		FCoverData ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Manual Cover Point" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09 * Returns cover data of this cover point.\n\x09 * This is injected into octree in cover generation\n\x09 */" },
#endif
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Returns cover data of this cover point.\nThis is injected into octree in cover generation" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function GetCoverData constinit property declarations **************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetCoverData constinit property declarations ****************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetCoverData Property Definitions *************************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(ManualCoverPoint_eventGetCoverData_Parms, ReturnValue), Z_Construct_UScriptStruct_FCoverData, METADATA_PARAMS(0, nullptr) }; // 2527779184
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::PropPointers) < 2048);
// ********** End Function GetCoverData Property Definitions ***************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_AManualCoverPoint, nullptr, "GetCoverData", 	Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::ManualCoverPoint_eventGetCoverData_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::Function_MetaDataParams), Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::ManualCoverPoint_eventGetCoverData_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_AManualCoverPoint_GetCoverData()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_AManualCoverPoint_GetCoverData_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(AManualCoverPoint::execGetCoverData)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	*(FCoverData*)Z_Param__Result=P_THIS->GetCoverData();
	P_NATIVE_END;
}
// ********** End Class AManualCoverPoint Function GetCoverData ************************************

// ********** Begin Class AManualCoverPoint ********************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_AManualCoverPoint;
UClass* AManualCoverPoint::GetPrivateStaticClass()
{
	using TClass = AManualCoverPoint;
	if (!Z_Registration_Info_UClass_AManualCoverPoint.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("ManualCoverPoint"),
			Z_Registration_Info_UClass_AManualCoverPoint.InnerSingleton,
			StaticRegisterNativesAManualCoverPoint,
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
	return Z_Registration_Info_UClass_AManualCoverPoint.InnerSingleton;
}
UClass* Z_Construct_UClass_AManualCoverPoint_NoRegister()
{
	return AManualCoverPoint::GetPrivateStaticClass();
}
struct Z_Construct_UClass_AManualCoverPoint_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n* Manual cover point that can be hand-placed to the level.\n* Registers to CoverSystem which takes this into generation.\n*/" },
#endif
		{ "IncludePath", "ManualCoverPoint.h" },
		{ "IsBlueprintBase", "true" },
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Manual cover point that can be hand-placed to the level.\nRegisters to CoverSystem which takes this into generation." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bLeftCoverStanding_MetaData[] = {
		{ "Category", "Manual Cover Point" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Left cover (can lean on left) */" },
#endif
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Left cover (can lean on left)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bRightCoverStanding_MetaData[] = {
		{ "Category", "Manual Cover Point" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Right cover (can lean on Right) */" },
#endif
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Right cover (can lean on Right)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bLeftCoverCrouched_MetaData[] = {
		{ "Category", "Manual Cover Point" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Left cover (can lean on left) */" },
#endif
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Left cover (can lean on left)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bRightCoverCrouched_MetaData[] = {
		{ "Category", "Manual Cover Point" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Right cover (can lean on Right) */" },
#endif
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Right cover (can lean on Right)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bFrontCoverCrouched_MetaData[] = {
		{ "Category", "Manual Cover Point" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a front cover, so crouch gives cover and while standing there's line of sight for shooting */" },
#endif
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a front cover, so crouch gives cover and while standing there's line of sight for shooting" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_RootSceneComponent_MetaData[] = {
		{ "Category", "Manual Cover Point" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
	};
#if WITH_EDITORONLY_DATA
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PreviewCapsule_MetaData[] = {
		{ "Category", "Manual Cover Point" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_PreviewArrow_MetaData[] = {
		{ "Category", "Manual Cover Point" },
		{ "EditInline", "true" },
		{ "ModuleRelativePath", "Public/ManualCoverPoint.h" },
	};
#endif // WITH_EDITORONLY_DATA
#endif // WITH_METADATA

// ********** Begin Class AManualCoverPoint constinit property declarations ************************
	static void NewProp_bLeftCoverStanding_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bLeftCoverStanding;
	static void NewProp_bRightCoverStanding_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bRightCoverStanding;
	static void NewProp_bLeftCoverCrouched_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bLeftCoverCrouched;
	static void NewProp_bRightCoverCrouched_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bRightCoverCrouched;
	static void NewProp_bFrontCoverCrouched_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bFrontCoverCrouched;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_RootSceneComponent;
#if WITH_EDITORONLY_DATA
	static const UECodeGen_Private::FObjectPropertyParams NewProp_PreviewCapsule;
	static const UECodeGen_Private::FObjectPropertyParams NewProp_PreviewArrow;
#endif // WITH_EDITORONLY_DATA
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class AManualCoverPoint constinit property declarations **************************
	static constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {
		{ .NameUTF8 = UTF8TEXT("GetCoverData"), .Pointer = &AManualCoverPoint::execGetCoverData },
	};
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
		{ &Z_Construct_UFunction_AManualCoverPoint_GetCoverData, "GetCoverData" }, // 104638988
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<AManualCoverPoint>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_AManualCoverPoint_Statics

// ********** Begin Class AManualCoverPoint Property Definitions ***********************************
void Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverStanding_SetBit(void* Obj)
{
	((AManualCoverPoint*)Obj)->bLeftCoverStanding = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverStanding = { "bLeftCoverStanding", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(AManualCoverPoint), &Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverStanding_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bLeftCoverStanding_MetaData), NewProp_bLeftCoverStanding_MetaData) };
void Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverStanding_SetBit(void* Obj)
{
	((AManualCoverPoint*)Obj)->bRightCoverStanding = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverStanding = { "bRightCoverStanding", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(AManualCoverPoint), &Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverStanding_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bRightCoverStanding_MetaData), NewProp_bRightCoverStanding_MetaData) };
void Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverCrouched_SetBit(void* Obj)
{
	((AManualCoverPoint*)Obj)->bLeftCoverCrouched = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverCrouched = { "bLeftCoverCrouched", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(AManualCoverPoint), &Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverCrouched_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bLeftCoverCrouched_MetaData), NewProp_bLeftCoverCrouched_MetaData) };
void Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverCrouched_SetBit(void* Obj)
{
	((AManualCoverPoint*)Obj)->bRightCoverCrouched = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverCrouched = { "bRightCoverCrouched", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(AManualCoverPoint), &Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverCrouched_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bRightCoverCrouched_MetaData), NewProp_bRightCoverCrouched_MetaData) };
void Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bFrontCoverCrouched_SetBit(void* Obj)
{
	((AManualCoverPoint*)Obj)->bFrontCoverCrouched = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bFrontCoverCrouched = { "bFrontCoverCrouched", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(AManualCoverPoint), &Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bFrontCoverCrouched_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bFrontCoverCrouched_MetaData), NewProp_bFrontCoverCrouched_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_RootSceneComponent = { "RootSceneComponent", nullptr, (EPropertyFlags)0x00200800000a001d, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AManualCoverPoint, RootSceneComponent), Z_Construct_UClass_USceneComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_RootSceneComponent_MetaData), NewProp_RootSceneComponent_MetaData) };
#if WITH_EDITORONLY_DATA
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_PreviewCapsule = { "PreviewCapsule", nullptr, (EPropertyFlags)0x00200808000b001d, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AManualCoverPoint, PreviewCapsule), Z_Construct_UClass_UCapsuleComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PreviewCapsule_MetaData), NewProp_PreviewCapsule_MetaData) };
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_PreviewArrow = { "PreviewArrow", nullptr, (EPropertyFlags)0x00200808000b001d, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(AManualCoverPoint, PreviewArrow), Z_Construct_UClass_UArrowComponent_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_PreviewArrow_MetaData), NewProp_PreviewArrow_MetaData) };
#endif // WITH_EDITORONLY_DATA
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_AManualCoverPoint_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bLeftCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bRightCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_bFrontCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_RootSceneComponent,
#if WITH_EDITORONLY_DATA
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_PreviewCapsule,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_AManualCoverPoint_Statics::NewProp_PreviewArrow,
#endif // WITH_EDITORONLY_DATA
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_AManualCoverPoint_Statics::PropPointers) < 2048);
// ********** End Class AManualCoverPoint Property Definitions *************************************
UObject* (*const Z_Construct_UClass_AManualCoverPoint_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_AActor,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_AManualCoverPoint_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_AManualCoverPoint_Statics::ClassParams = {
	&AManualCoverPoint::StaticClass,
	"Engine",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	Z_Construct_UClass_AManualCoverPoint_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	UE_ARRAY_COUNT(Z_Construct_UClass_AManualCoverPoint_Statics::PropPointers),
	0,
	0x009000A4u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_AManualCoverPoint_Statics::Class_MetaDataParams), Z_Construct_UClass_AManualCoverPoint_Statics::Class_MetaDataParams)
};
void AManualCoverPoint::StaticRegisterNativesAManualCoverPoint()
{
	UClass* Class = AManualCoverPoint::StaticClass();
	FNativeFunctionRegistrar::RegisterFunctions(Class, MakeConstArrayView(Z_Construct_UClass_AManualCoverPoint_Statics::Funcs));
}
UClass* Z_Construct_UClass_AManualCoverPoint()
{
	if (!Z_Registration_Info_UClass_AManualCoverPoint.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_AManualCoverPoint.OuterSingleton, Z_Construct_UClass_AManualCoverPoint_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_AManualCoverPoint.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, AManualCoverPoint);
AManualCoverPoint::~AManualCoverPoint() {}
// ********** End Class AManualCoverPoint **********************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_ManualCoverPoint_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_AManualCoverPoint, AManualCoverPoint::StaticClass, TEXT("AManualCoverPoint"), &Z_Registration_Info_UClass_AManualCoverPoint, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(AManualCoverPoint), 3177075194U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_ManualCoverPoint_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_ManualCoverPoint_h__Script_AICoverSystem_3187766899{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_ManualCoverPoint_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_ManualCoverPoint_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
