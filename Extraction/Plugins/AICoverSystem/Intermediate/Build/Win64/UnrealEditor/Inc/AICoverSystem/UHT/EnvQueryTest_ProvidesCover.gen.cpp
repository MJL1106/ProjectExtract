// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "EnvQueryTest_ProvidesCover.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeEnvQueryTest_ProvidesCover() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_ProvidesCover();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_ProvidesCover_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryContext_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryTest();
COREUOBJECT_API UClass* Z_Construct_UClass_UClass_NoRegister();
ENGINE_API UEnum* Z_Construct_UEnum_Engine_ECollisionChannel();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UEnvQueryTest_ProvidesCover **********************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover;
UClass* UEnvQueryTest_ProvidesCover::GetPrivateStaticClass()
{
	using TClass = UEnvQueryTest_ProvidesCover;
	if (!Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("EnvQueryTest_ProvidesCover"),
			Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover.InnerSingleton,
			StaticRegisterNativesUEnvQueryTest_ProvidesCover,
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
	return Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover.InnerSingleton;
}
UClass* Z_Construct_UClass_UEnvQueryTest_ProvidesCover_NoRegister()
{
	return UEnvQueryTest_ProvidesCover::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Tests if cover item is currently giving cover from given contex(s)\n */" },
#endif
		{ "IncludePath", "AI/Tests/EnvQueryTest_ProvidesCover.h" },
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ProvidesCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Tests if cover item is currently giving cover from given contex(s)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TraceChannel_MetaData[] = {
		{ "Category", "Trace" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Trace channel to use for testing if there's blocking geometry between cover and context\n" },
#endif
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ProvidesCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Trace channel to use for testing if there's blocking geometry between cover and context" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Context_MetaData[] = {
		{ "Category", "Trace" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Context which we are looking for protection from\n" },
#endif
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ProvidesCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Context which we are looking for protection from" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UEnvQueryTest_ProvidesCover constinit property declarations **************
	static const UECodeGen_Private::FBytePropertyParams NewProp_TraceChannel;
	static const UECodeGen_Private::FClassPropertyParams NewProp_Context;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UEnvQueryTest_ProvidesCover constinit property declarations ****************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UEnvQueryTest_ProvidesCover>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics

// ********** Begin Class UEnvQueryTest_ProvidesCover Property Definitions *************************
const UECodeGen_Private::FBytePropertyParams Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::NewProp_TraceChannel = { "TraceChannel", nullptr, (EPropertyFlags)0x0010000000010001, UECodeGen_Private::EPropertyGenFlags::Byte, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryTest_ProvidesCover, TraceChannel), Z_Construct_UEnum_Engine_ECollisionChannel, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TraceChannel_MetaData), NewProp_TraceChannel_MetaData) }; // 838391399
const UECodeGen_Private::FClassPropertyParams Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::NewProp_Context = { "Context", nullptr, (EPropertyFlags)0x0014000000010001, UECodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryTest_ProvidesCover, Context), Z_Construct_UClass_UClass_NoRegister, Z_Construct_UClass_UEnvQueryContext_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Context_MetaData), NewProp_Context_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::NewProp_TraceChannel,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::NewProp_Context,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::PropPointers) < 2048);
// ********** End Class UEnvQueryTest_ProvidesCover Property Definitions ***************************
UObject* (*const Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEnvQueryTest,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::ClassParams = {
	&UEnvQueryTest_ProvidesCover::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::PropPointers),
	0,
	0x009000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::Class_MetaDataParams), Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::Class_MetaDataParams)
};
void UEnvQueryTest_ProvidesCover::StaticRegisterNativesUEnvQueryTest_ProvidesCover()
{
}
UClass* Z_Construct_UClass_UEnvQueryTest_ProvidesCover()
{
	if (!Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover.OuterSingleton, Z_Construct_UClass_UEnvQueryTest_ProvidesCover_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UEnvQueryTest_ProvidesCover);
UEnvQueryTest_ProvidesCover::~UEnvQueryTest_ProvidesCover() {}
// ********** End Class UEnvQueryTest_ProvidesCover ************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ProvidesCover_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UEnvQueryTest_ProvidesCover, UEnvQueryTest_ProvidesCover::StaticClass, TEXT("UEnvQueryTest_ProvidesCover"), &Z_Registration_Info_UClass_UEnvQueryTest_ProvidesCover, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UEnvQueryTest_ProvidesCover), 2449288327U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ProvidesCover_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ProvidesCover_h__Script_AICoverSystem_734990085{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ProvidesCover_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ProvidesCover_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
