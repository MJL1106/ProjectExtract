// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "BTService_OccupyCover.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeBTService_OccupyCover() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBTService_OccupyCover();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBTService_OccupyCover_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UBTService_BlackboardBase();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UBTService_OccupyCover ***************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UBTService_OccupyCover;
UClass* UBTService_OccupyCover::GetPrivateStaticClass()
{
	using TClass = UBTService_OccupyCover;
	if (!Z_Registration_Info_UClass_UBTService_OccupyCover.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("BTService_OccupyCover"),
			Z_Registration_Info_UClass_UBTService_OccupyCover.InnerSingleton,
			StaticRegisterNativesUBTService_OccupyCover,
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
	return Z_Registration_Info_UClass_UBTService_OccupyCover.InnerSingleton;
}
UClass* Z_Construct_UClass_UBTService_OccupyCover_NoRegister()
{
	return UBTService_OccupyCover::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UBTService_OccupyCover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Occupies cover for an AI as long as the service is active\n */" },
#endif
		{ "HideCategories", "Service" },
		{ "IncludePath", "AI/Services/BTService_OccupyCover.h" },
		{ "ModuleRelativePath", "Public/AI/Services/BTService_OccupyCover.h" },
		{ "ObjectInitializerConstructorDeclared", "" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Occupies cover for an AI as long as the service is active" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bObserveBlackboardValue_MetaData[] = {
		{ "Category", "Blackboard" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** if the selected cover blackboard value changes, should the occupy state updated? */" },
#endif
		{ "ModuleRelativePath", "Public/AI/Services/BTService_OccupyCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "if the selected cover blackboard value changes, should the occupy state updated?" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UBTService_OccupyCover constinit property declarations *******************
	static void NewProp_bObserveBlackboardValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bObserveBlackboardValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UBTService_OccupyCover constinit property declarations *********************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBTService_OccupyCover>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UBTService_OccupyCover_Statics

// ********** Begin Class UBTService_OccupyCover Property Definitions ******************************
void Z_Construct_UClass_UBTService_OccupyCover_Statics::NewProp_bObserveBlackboardValue_SetBit(void* Obj)
{
	((UBTService_OccupyCover*)Obj)->bObserveBlackboardValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_UBTService_OccupyCover_Statics::NewProp_bObserveBlackboardValue = { "bObserveBlackboardValue", nullptr, (EPropertyFlags)0x0020080000000001, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(UBTService_OccupyCover), &Z_Construct_UClass_UBTService_OccupyCover_Statics::NewProp_bObserveBlackboardValue_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bObserveBlackboardValue_MetaData), NewProp_bObserveBlackboardValue_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UBTService_OccupyCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UBTService_OccupyCover_Statics::NewProp_bObserveBlackboardValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCover_Statics::PropPointers) < 2048);
// ********** End Class UBTService_OccupyCover Property Definitions ********************************
UObject* (*const Z_Construct_UClass_UBTService_OccupyCover_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UBTService_BlackboardBase,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCover_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UBTService_OccupyCover_Statics::ClassParams = {
	&UBTService_OccupyCover::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UBTService_OccupyCover_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCover_Statics::PropPointers),
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCover_Statics::Class_MetaDataParams), Z_Construct_UClass_UBTService_OccupyCover_Statics::Class_MetaDataParams)
};
void UBTService_OccupyCover::StaticRegisterNativesUBTService_OccupyCover()
{
}
UClass* Z_Construct_UClass_UBTService_OccupyCover()
{
	if (!Z_Registration_Info_UClass_UBTService_OccupyCover.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UBTService_OccupyCover.OuterSingleton, Z_Construct_UClass_UBTService_OccupyCover_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UBTService_OccupyCover.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UBTService_OccupyCover);
UBTService_OccupyCover::~UBTService_OccupyCover() {}
// ********** End Class UBTService_OccupyCover *****************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCover_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UBTService_OccupyCover, UBTService_OccupyCover::StaticClass, TEXT("UBTService_OccupyCover"), &Z_Registration_Info_UClass_UBTService_OccupyCover, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UBTService_OccupyCover), 191029849U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCover_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCover_h__Script_AICoverSystem_4206955871{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCover_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCover_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
