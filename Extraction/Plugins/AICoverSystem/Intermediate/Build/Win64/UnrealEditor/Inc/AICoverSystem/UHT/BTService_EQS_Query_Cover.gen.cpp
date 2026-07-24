// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "BTService_EQS_Query_Cover.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "EnvironmentQuery/EnvQueryTypes.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeBTService_EQS_Query_Cover() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBTService_EQS_Query_Cover();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBTService_EQS_Query_Cover_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UBTService_BlackboardBase();
AIMODULE_API UScriptStruct* Z_Construct_UScriptStruct_FBlackboardKeySelector();
AIMODULE_API UScriptStruct* Z_Construct_UScriptStruct_FEQSParametrizedQueryExecutionRequest();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UBTService_EQS_Query_Cover ***********************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UBTService_EQS_Query_Cover;
UClass* UBTService_EQS_Query_Cover::GetPrivateStaticClass()
{
	using TClass = UBTService_EQS_Query_Cover;
	if (!Z_Registration_Info_UClass_UBTService_EQS_Query_Cover.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("BTService_EQS_Query_Cover"),
			Z_Registration_Info_UClass_UBTService_EQS_Query_Cover.InnerSingleton,
			StaticRegisterNativesUBTService_EQS_Query_Cover,
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
	return Z_Registration_Info_UClass_UBTService_EQS_Query_Cover.InnerSingleton;
}
UClass* Z_Construct_UClass_UBTService_EQS_Query_Cover_NoRegister()
{
	return UBTService_EQS_Query_Cover::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Runs environment query in intervals and stores cover and its location into a separate blackboard keys\n */" },
#endif
		{ "IncludePath", "AI/Services/BTService_EQS_Query_Cover.h" },
		{ "ModuleRelativePath", "Public/AI/Services/BTService_EQS_Query_Cover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Runs environment query in intervals and stores cover and its location into a separate blackboard keys" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_EQSRequest_MetaData[] = {
		{ "Category", "EQS" },
		{ "ModuleRelativePath", "Public/AI/Services/BTService_EQS_Query_Cover.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bUpdateBBOnFail_MetaData[] = {
		{ "Category", "EQS" },
		{ "ModuleRelativePath", "Public/AI/Services/BTService_EQS_Query_Cover.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_LocationBlackboardKey_MetaData[] = {
		{ "Category", "Blackboard" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** blackboard key selector. Location of the cover is stored here */" },
#endif
		{ "ModuleRelativePath", "Public/AI/Services/BTService_EQS_Query_Cover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "blackboard key selector. Location of the cover is stored here" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UBTService_EQS_Query_Cover constinit property declarations ***************
	static const UECodeGen_Private::FStructPropertyParams NewProp_EQSRequest;
	static void NewProp_bUpdateBBOnFail_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bUpdateBBOnFail;
	static const UECodeGen_Private::FStructPropertyParams NewProp_LocationBlackboardKey;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UBTService_EQS_Query_Cover constinit property declarations *****************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBTService_EQS_Query_Cover>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics

// ********** Begin Class UBTService_EQS_Query_Cover Property Definitions **************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_EQSRequest = { "EQSRequest", nullptr, (EPropertyFlags)0x0020080000000001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UBTService_EQS_Query_Cover, EQSRequest), Z_Construct_UScriptStruct_FEQSParametrizedQueryExecutionRequest, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_EQSRequest_MetaData), NewProp_EQSRequest_MetaData) }; // 169663680
void Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_bUpdateBBOnFail_SetBit(void* Obj)
{
	((UBTService_EQS_Query_Cover*)Obj)->bUpdateBBOnFail = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_bUpdateBBOnFail = { "bUpdateBBOnFail", nullptr, (EPropertyFlags)0x0020080000000001, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(UBTService_EQS_Query_Cover), &Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_bUpdateBBOnFail_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bUpdateBBOnFail_MetaData), NewProp_bUpdateBBOnFail_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_LocationBlackboardKey = { "LocationBlackboardKey", nullptr, (EPropertyFlags)0x0010000000000001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UBTService_EQS_Query_Cover, LocationBlackboardKey), Z_Construct_UScriptStruct_FBlackboardKeySelector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_LocationBlackboardKey_MetaData), NewProp_LocationBlackboardKey_MetaData) }; // 3145079323
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_EQSRequest,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_bUpdateBBOnFail,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::NewProp_LocationBlackboardKey,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::PropPointers) < 2048);
// ********** End Class UBTService_EQS_Query_Cover Property Definitions ****************************
UObject* (*const Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UBTService_BlackboardBase,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::ClassParams = {
	&UBTService_EQS_Query_Cover::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::PropPointers),
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::Class_MetaDataParams), Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::Class_MetaDataParams)
};
void UBTService_EQS_Query_Cover::StaticRegisterNativesUBTService_EQS_Query_Cover()
{
}
UClass* Z_Construct_UClass_UBTService_EQS_Query_Cover()
{
	if (!Z_Registration_Info_UClass_UBTService_EQS_Query_Cover.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UBTService_EQS_Query_Cover.OuterSingleton, Z_Construct_UClass_UBTService_EQS_Query_Cover_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UBTService_EQS_Query_Cover.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UBTService_EQS_Query_Cover);
UBTService_EQS_Query_Cover::~UBTService_EQS_Query_Cover() {}
// ********** End Class UBTService_EQS_Query_Cover *************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_EQS_Query_Cover_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UBTService_EQS_Query_Cover, UBTService_EQS_Query_Cover::StaticClass, TEXT("UBTService_EQS_Query_Cover"), &Z_Registration_Info_UClass_UBTService_EQS_Query_Cover, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UBTService_EQS_Query_Cover), 2279159306U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_EQS_Query_Cover_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_EQS_Query_Cover_h__Script_AICoverSystem_4212897720{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_EQS_Query_Cover_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_EQS_Query_Cover_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
