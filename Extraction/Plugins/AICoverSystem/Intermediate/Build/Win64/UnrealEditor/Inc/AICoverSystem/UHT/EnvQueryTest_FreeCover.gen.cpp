// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "EnvQueryTest_FreeCover.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeEnvQueryTest_FreeCover() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_FreeCover();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_FreeCover_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryTest();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UEnvQueryTest_FreeCover **************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UEnvQueryTest_FreeCover;
UClass* UEnvQueryTest_FreeCover::GetPrivateStaticClass()
{
	using TClass = UEnvQueryTest_FreeCover;
	if (!Z_Registration_Info_UClass_UEnvQueryTest_FreeCover.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("EnvQueryTest_FreeCover"),
			Z_Registration_Info_UClass_UEnvQueryTest_FreeCover.InnerSingleton,
			StaticRegisterNativesUEnvQueryTest_FreeCover,
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
	return Z_Registration_Info_UClass_UEnvQueryTest_FreeCover.InnerSingleton;
}
UClass* Z_Construct_UClass_UEnvQueryTest_FreeCover_NoRegister()
{
	return UEnvQueryTest_FreeCover::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Tests that the cover item is not currently occupied\n */" },
#endif
		{ "IncludePath", "AI/Tests/EnvQueryTest_FreeCover.h" },
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_FreeCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Tests that the cover item is not currently occupied" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bFreeOnSelfOccupy_MetaData[] = {
		{ "Category", "Test" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Consider the cover unoccupied, if the querier is occupying it\n" },
#endif
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_FreeCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Consider the cover unoccupied, if the querier is occupying it" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UEnvQueryTest_FreeCover constinit property declarations ******************
	static void NewProp_bFreeOnSelfOccupy_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bFreeOnSelfOccupy;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UEnvQueryTest_FreeCover constinit property declarations ********************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UEnvQueryTest_FreeCover>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics

// ********** Begin Class UEnvQueryTest_FreeCover Property Definitions *****************************
void Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::NewProp_bFreeOnSelfOccupy_SetBit(void* Obj)
{
	((UEnvQueryTest_FreeCover*)Obj)->bFreeOnSelfOccupy = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::NewProp_bFreeOnSelfOccupy = { "bFreeOnSelfOccupy", nullptr, (EPropertyFlags)0x0020080000000001, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(UEnvQueryTest_FreeCover), &Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::NewProp_bFreeOnSelfOccupy_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bFreeOnSelfOccupy_MetaData), NewProp_bFreeOnSelfOccupy_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::NewProp_bFreeOnSelfOccupy,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::PropPointers) < 2048);
// ********** End Class UEnvQueryTest_FreeCover Property Definitions *******************************
UObject* (*const Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEnvQueryTest,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::ClassParams = {
	&UEnvQueryTest_FreeCover::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::PropPointers),
	0,
	0x009000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::Class_MetaDataParams), Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::Class_MetaDataParams)
};
void UEnvQueryTest_FreeCover::StaticRegisterNativesUEnvQueryTest_FreeCover()
{
}
UClass* Z_Construct_UClass_UEnvQueryTest_FreeCover()
{
	if (!Z_Registration_Info_UClass_UEnvQueryTest_FreeCover.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UEnvQueryTest_FreeCover.OuterSingleton, Z_Construct_UClass_UEnvQueryTest_FreeCover_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UEnvQueryTest_FreeCover.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UEnvQueryTest_FreeCover);
UEnvQueryTest_FreeCover::~UEnvQueryTest_FreeCover() {}
// ********** End Class UEnvQueryTest_FreeCover ****************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_FreeCover_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UEnvQueryTest_FreeCover, UEnvQueryTest_FreeCover::StaticClass, TEXT("UEnvQueryTest_FreeCover"), &Z_Registration_Info_UClass_UEnvQueryTest_FreeCover, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UEnvQueryTest_FreeCover), 2262938444U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_FreeCover_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_FreeCover_h__Script_AICoverSystem_434778426{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_FreeCover_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_FreeCover_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
