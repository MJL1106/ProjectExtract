// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "EnvQueryGenerator_Covers.h"
#include "DataProviders/AIDataProvider.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeEnvQueryGenerator_Covers() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryGenerator_Covers();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryGenerator_Covers_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryContext_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryGenerator();
AIMODULE_API UScriptStruct* Z_Construct_UScriptStruct_FAIDataProviderBoolValue();
AIMODULE_API UScriptStruct* Z_Construct_UScriptStruct_FAIDataProviderFloatValue();
COREUOBJECT_API UClass* Z_Construct_UClass_UClass_NoRegister();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UEnvQueryGenerator_Covers ************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UEnvQueryGenerator_Covers;
UClass* UEnvQueryGenerator_Covers::GetPrivateStaticClass()
{
	using TClass = UEnvQueryGenerator_Covers;
	if (!Z_Registration_Info_UClass_UEnvQueryGenerator_Covers.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("EnvQueryGenerator_Covers"),
			Z_Registration_Info_UClass_UEnvQueryGenerator_Covers.InnerSingleton,
			StaticRegisterNativesUEnvQueryGenerator_Covers,
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
	return Z_Registration_Info_UClass_UEnvQueryGenerator_Covers.InnerSingleton;
}
UClass* Z_Construct_UClass_UEnvQueryGenerator_Covers_NoRegister()
{
	return UEnvQueryGenerator_Covers::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Generates set of covers inside given bounds\n */" },
#endif
		{ "IncludePath", "AI/EnvQueryGenerator_Covers.h" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Generates set of covers inside given bounds" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_QueryBoundSize_MetaData[] = {
		{ "Category", "Generator" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** X-Y extents of bounding box to search for covers */" },
#endif
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "X-Y extents of bounding box to search for covers" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_QueryBoundHeight_MetaData[] = {
		{ "Category", "Generator" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Z extent of bounding box to search for covers */" },
#endif
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Z extent of bounding box to search for covers" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_GenerateAround_MetaData[] = {
		{ "Category", "Generator" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Context(s) that is used as origin of generation bounds */" },
#endif
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Context(s) that is used as origin of generation bounds" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_IncludeLeftCoverStanding_MetaData[] = {
		{ "Category", "Generator" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_IncludeRightCoverStanding_MetaData[] = {
		{ "Category", "Generator" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_IncludeLeftCoverCrouched_MetaData[] = {
		{ "Category", "Generator" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_IncludeRightCoverCrouched_MetaData[] = {
		{ "Category", "Generator" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_IncludeFrontCoverCrouched_MetaData[] = {
		{ "Category", "Generator" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_IncludeOnlyCrouched_MetaData[] = {
		{ "Category", "Generator" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryGenerator_Covers.h" },
	};
#endif // WITH_METADATA

// ********** Begin Class UEnvQueryGenerator_Covers constinit property declarations ****************
	static const UECodeGen_Private::FStructPropertyParams NewProp_QueryBoundSize;
	static const UECodeGen_Private::FStructPropertyParams NewProp_QueryBoundHeight;
	static const UECodeGen_Private::FClassPropertyParams NewProp_GenerateAround;
	static const UECodeGen_Private::FStructPropertyParams NewProp_IncludeLeftCoverStanding;
	static const UECodeGen_Private::FStructPropertyParams NewProp_IncludeRightCoverStanding;
	static const UECodeGen_Private::FStructPropertyParams NewProp_IncludeLeftCoverCrouched;
	static const UECodeGen_Private::FStructPropertyParams NewProp_IncludeRightCoverCrouched;
	static const UECodeGen_Private::FStructPropertyParams NewProp_IncludeFrontCoverCrouched;
	static const UECodeGen_Private::FStructPropertyParams NewProp_IncludeOnlyCrouched;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UEnvQueryGenerator_Covers constinit property declarations ******************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UEnvQueryGenerator_Covers>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics

// ********** Begin Class UEnvQueryGenerator_Covers Property Definitions ***************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_QueryBoundSize = { "QueryBoundSize", nullptr, (EPropertyFlags)0x0010008000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, QueryBoundSize), Z_Construct_UScriptStruct_FAIDataProviderFloatValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_QueryBoundSize_MetaData), NewProp_QueryBoundSize_MetaData) }; // 1266521968
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_QueryBoundHeight = { "QueryBoundHeight", nullptr, (EPropertyFlags)0x0010008000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, QueryBoundHeight), Z_Construct_UScriptStruct_FAIDataProviderFloatValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_QueryBoundHeight_MetaData), NewProp_QueryBoundHeight_MetaData) }; // 1266521968
const UECodeGen_Private::FClassPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_GenerateAround = { "GenerateAround", nullptr, (EPropertyFlags)0x0014000000010001, UECodeGen_Private::EPropertyGenFlags::Class, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, GenerateAround), Z_Construct_UClass_UClass_NoRegister, Z_Construct_UClass_UEnvQueryContext_NoRegister, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_GenerateAround_MetaData), NewProp_GenerateAround_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeLeftCoverStanding = { "IncludeLeftCoverStanding", nullptr, (EPropertyFlags)0x0010048000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, IncludeLeftCoverStanding), Z_Construct_UScriptStruct_FAIDataProviderBoolValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_IncludeLeftCoverStanding_MetaData), NewProp_IncludeLeftCoverStanding_MetaData) }; // 4163314725
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeRightCoverStanding = { "IncludeRightCoverStanding", nullptr, (EPropertyFlags)0x0010048000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, IncludeRightCoverStanding), Z_Construct_UScriptStruct_FAIDataProviderBoolValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_IncludeRightCoverStanding_MetaData), NewProp_IncludeRightCoverStanding_MetaData) }; // 4163314725
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeLeftCoverCrouched = { "IncludeLeftCoverCrouched", nullptr, (EPropertyFlags)0x0010048000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, IncludeLeftCoverCrouched), Z_Construct_UScriptStruct_FAIDataProviderBoolValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_IncludeLeftCoverCrouched_MetaData), NewProp_IncludeLeftCoverCrouched_MetaData) }; // 4163314725
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeRightCoverCrouched = { "IncludeRightCoverCrouched", nullptr, (EPropertyFlags)0x0010048000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, IncludeRightCoverCrouched), Z_Construct_UScriptStruct_FAIDataProviderBoolValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_IncludeRightCoverCrouched_MetaData), NewProp_IncludeRightCoverCrouched_MetaData) }; // 4163314725
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeFrontCoverCrouched = { "IncludeFrontCoverCrouched", nullptr, (EPropertyFlags)0x0010048000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, IncludeFrontCoverCrouched), Z_Construct_UScriptStruct_FAIDataProviderBoolValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_IncludeFrontCoverCrouched_MetaData), NewProp_IncludeFrontCoverCrouched_MetaData) }; // 4163314725
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeOnlyCrouched = { "IncludeOnlyCrouched", nullptr, (EPropertyFlags)0x0010048000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UEnvQueryGenerator_Covers, IncludeOnlyCrouched), Z_Construct_UScriptStruct_FAIDataProviderBoolValue, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_IncludeOnlyCrouched_MetaData), NewProp_IncludeOnlyCrouched_MetaData) }; // 4163314725
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_QueryBoundSize,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_QueryBoundHeight,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_GenerateAround,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeLeftCoverStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeRightCoverStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeLeftCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeRightCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeFrontCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::NewProp_IncludeOnlyCrouched,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::PropPointers) < 2048);
// ********** End Class UEnvQueryGenerator_Covers Property Definitions *****************************
UObject* (*const Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEnvQueryGenerator,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::ClassParams = {
	&UEnvQueryGenerator_Covers::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::PropPointers),
	0,
	0x009010A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::Class_MetaDataParams), Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::Class_MetaDataParams)
};
void UEnvQueryGenerator_Covers::StaticRegisterNativesUEnvQueryGenerator_Covers()
{
}
UClass* Z_Construct_UClass_UEnvQueryGenerator_Covers()
{
	if (!Z_Registration_Info_UClass_UEnvQueryGenerator_Covers.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UEnvQueryGenerator_Covers.OuterSingleton, Z_Construct_UClass_UEnvQueryGenerator_Covers_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UEnvQueryGenerator_Covers.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UEnvQueryGenerator_Covers);
UEnvQueryGenerator_Covers::~UEnvQueryGenerator_Covers() {}
// ********** End Class UEnvQueryGenerator_Covers **************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryGenerator_Covers_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UEnvQueryGenerator_Covers, UEnvQueryGenerator_Covers::StaticClass, TEXT("UEnvQueryGenerator_Covers"), &Z_Registration_Info_UClass_UEnvQueryGenerator_Covers, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UEnvQueryGenerator_Covers), 3670883272U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryGenerator_Covers_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryGenerator_Covers_h__Script_AICoverSystem_202394332{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryGenerator_Covers_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryGenerator_Covers_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
