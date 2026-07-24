// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "EnvQueryTest_ParallelToCover.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeEnvQueryTest_ParallelToCover() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_ParallelToCover();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryTest_ParallelToCover_NoRegister();
AICOVERSYSTEM_API UEnum* Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryContext_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryTest();
COREUOBJECT_API UClass* Z_Construct_UClass_UClass_NoRegister();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Enum EEnvTestParallerCover *****************************************************
static FEnumRegistrationInfo Z_Registration_Info_UEnum_EEnvTestParallerCover;
static UEnum* EEnvTestParallerCover_StaticEnum()
{
	if (!Z_Registration_Info_UEnum_EEnvTestParallerCover.OuterSingleton)
	{
		Z_Registration_Info_UEnum_EEnvTestParallerCover.OuterSingleton = GetStaticEnum(Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("EEnvTestParallerCover"));
	}
	return Z_Registration_Info_UEnum_EEnvTestParallerCover.OuterSingleton;
}
template<> AICOVERSYSTEM_NON_ATTRIBUTED_API UEnum* StaticEnum<EEnvTestParallerCover>()
{
	return EEnvTestParallerCover_StaticEnum();
}
struct Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Enum_MetaDataParams[] = {
		{ "BlueprintType", "true" },
		{ "Dot2D.DisplayName", "Dot 2D (Heading)" },
		{ "Dot2D.Name", "EEnvTestParallerCover::Dot2D" },
		{ "Dot3D.DisplayName", "Dot (3D)" },
		{ "Dot3D.Name", "EEnvTestParallerCover::Dot3D" },
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ParallelToCover.h" },
	};
#endif // WITH_METADATA
	static constexpr UECodeGen_Private::FEnumeratorParam Enumerators[] = {
		{ "EEnvTestParallerCover::Dot3D", (int64)EEnvTestParallerCover::Dot3D },
		{ "EEnvTestParallerCover::Dot2D", (int64)EEnvTestParallerCover::Dot2D },
	};
	static const UECodeGen_Private::FEnumParams EnumParams;
}; // struct Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics 
const UECodeGen_Private::FEnumParams Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics::EnumParams = {
	(UObject*(*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	"EEnvTestParallerCover",
	"EEnvTestParallerCover",
	Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics::Enumerators,
	RF_Public|RF_Transient|RF_MarkAsNative,
	UE_ARRAY_COUNT(Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics::Enumerators),
	EEnumFlags::None,
	(uint8)UEnum::ECppForm::EnumClass,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics::Enum_MetaDataParams), Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics::Enum_MetaDataParams)
};
UEnum* Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover()
{
	if (!Z_Registration_Info_UEnum_EEnvTestParallerCover.InnerSingleton)
	{
		UECodeGen_Private::ConstructUEnum(Z_Registration_Info_UEnum_EEnvTestParallerCover.InnerSingleton, Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover_Statics::EnumParams);
	}
	return Z_Registration_Info_UEnum_EEnvTestParallerCover.InnerSingleton;
}
// ********** End Enum EEnvTestParallerCover *******************************************************

// ********** Begin Class UEnvQueryTest_ParallelToCover ********************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover;
UClass* UEnvQueryTest_ParallelToCover::GetPrivateStaticClass()
{
	using TClass = UEnvQueryTest_ParallelToCover;
	if (!Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("EnvQueryTest_ParallelToCover"),
			Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover.InnerSingleton,
			StaticRegisterNativesUEnvQueryTest_ParallelToCover,
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
	return Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover.InnerSingleton;
}
UClass* Z_Construct_UClass_UEnvQueryTest_ParallelToCover_NoRegister()
{
	return UEnvQueryTest_ParallelToCover::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Tests if cover forward is in parallel with direction vector from cover to context using dot product of the vectors\n * Use this test to filter covers, so the AI can get behind objects (relative to given context such as player)\n */" },
#endif
		{ "IncludePath", "AI/Tests/EnvQueryTest_ParallelToCover.h" },
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ParallelToCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Tests if cover forward is in parallel with direction vector from cover to context using dot product of the vectors\nUse this test to filter covers, so the AI can get behind objects (relative to given context such as player)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Context_MetaData[] = {
		{ "Category", "Test" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** \n\x09* Context(s) to check against if the cover is in parallel direction\n\x09* Typically this would be context of the AI's target that it wants to shoot or hide from, such as player character\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ParallelToCover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Context(s) to check against if the cover is in parallel direction\nTypically this would be context of the AI's target that it wants to shoot or hide from, such as player character" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TestMode_MetaData[] = {
		{ "Category", "Test" },
		{ "ModuleRelativePath", "Public/AI/Tests/EnvQueryTest_ParallelToCover.h" },
	};
#endif // WITH_METADATA

// ********** Begin Class UEnvQueryTest_ParallelToCover constinit property declarations ************
	static const UECodeGen_Private::FClassPropertyParams NewProp_Context;
	static const UECodeGen_Private::FBytePropertyParams NewProp_TestMode_Underlying;
	static const UECodeGen_Private::FEnumPropertyParams NewProp_TestMode;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UEnvQueryTest_ParallelToCover constinit property declarations **************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UEnvQueryTest_ParallelToCover>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics

// ********** Begin Class UEnvQueryTest_ParallelToCover Property Definitions ***********************
const UECodeGen_Private::FClassPropertyParams Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::NewProp_Context = { "Context", nullptr, (EPropertyFlags)0x0014000000010001, UECodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryTest_ParallelToCover, Context), Z_Construct_UClass_UClass_NoRegister, Z_Construct_UClass_UEnvQueryContext_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Context_MetaData), NewProp_Context_MetaData) };
const UECodeGen_Private::FBytePropertyParams Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::NewProp_TestMode_Underlying = { "UnderlyingType", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Byte, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, nullptr, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FEnumPropertyParams Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::NewProp_TestMode = { "TestMode", nullptr, (EPropertyFlags)0x0010000000010001, UECodeGen_Private::EPropertyGenFlags::Enum, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryTest_ParallelToCover, TestMode), Z_Construct_UEnum_AICoverSystem_EEnvTestParallerCover, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TestMode_MetaData), NewProp_TestMode_MetaData) }; // 1052490101
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::NewProp_Context,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::NewProp_TestMode_Underlying,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::NewProp_TestMode,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::PropPointers) < 2048);
// ********** End Class UEnvQueryTest_ParallelToCover Property Definitions *************************
UObject* (*const Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEnvQueryTest,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::ClassParams = {
	&UEnvQueryTest_ParallelToCover::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::PropPointers),
	0,
	0x009000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::Class_MetaDataParams), Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::Class_MetaDataParams)
};
void UEnvQueryTest_ParallelToCover::StaticRegisterNativesUEnvQueryTest_ParallelToCover()
{
}
UClass* Z_Construct_UClass_UEnvQueryTest_ParallelToCover()
{
	if (!Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover.OuterSingleton, Z_Construct_UClass_UEnvQueryTest_ParallelToCover_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UEnvQueryTest_ParallelToCover);
UEnvQueryTest_ParallelToCover::~UEnvQueryTest_ParallelToCover() {}
// ********** End Class UEnvQueryTest_ParallelToCover **********************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_Statics
{
	static constexpr FEnumRegisterCompiledInInfo EnumInfo[] = {
		{ EEnvTestParallerCover_StaticEnum, TEXT("EEnvTestParallerCover"), &Z_Registration_Info_UEnum_EEnvTestParallerCover, CONSTRUCT_RELOAD_VERSION_INFO(FEnumReloadVersionInfo, 1052490101U) },
	};
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UEnvQueryTest_ParallelToCover, UEnvQueryTest_ParallelToCover::StaticClass, TEXT("UEnvQueryTest_ParallelToCover"), &Z_Registration_Info_UClass_UEnvQueryTest_ParallelToCover, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UEnvQueryTest_ParallelToCover), 3518893540U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_1912227898{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_Statics::EnumInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Tests_EnvQueryTest_ParallelToCover_h__Script_AICoverSystem_Statics::EnumInfo),
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
