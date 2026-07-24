// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "EnvQueryItemType_CoverBase.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeEnvQueryItemType_CoverBase() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryItemType_CoverBase();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UEnvQueryItemType_CoverBase_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryItemType_VectorBase();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UEnvQueryItemType_CoverBase **********************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase;
UClass* UEnvQueryItemType_CoverBase::GetPrivateStaticClass()
{
	using TClass = UEnvQueryItemType_CoverBase;
	if (!Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("EnvQueryItemType_CoverBase"),
			Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase.InnerSingleton,
			StaticRegisterNativesUEnvQueryItemType_CoverBase,
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
	return Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase.InnerSingleton;
}
UClass* Z_Construct_UClass_UEnvQueryItemType_CoverBase_NoRegister()
{
	return UEnvQueryItemType_CoverBase::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Type of an item in env query system to query and test covers\n */" },
#endif
		{ "IncludePath", "AI/EnvQueryItemType_CoverBase.h" },
		{ "ModuleRelativePath", "Public/AI/EnvQueryItemType_CoverBase.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Type of an item in env query system to query and test covers" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UEnvQueryItemType_CoverBase constinit property declarations **************
// ********** End Class UEnvQueryItemType_CoverBase constinit property declarations ****************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UEnvQueryItemType_CoverBase>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics
UObject* (*const Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UEnvQueryItemType_VectorBase,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics::ClassParams = {
	&UEnvQueryItemType_CoverBase::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics::Class_MetaDataParams), Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics::Class_MetaDataParams)
};
void UEnvQueryItemType_CoverBase::StaticRegisterNativesUEnvQueryItemType_CoverBase()
{
}
UClass* Z_Construct_UClass_UEnvQueryItemType_CoverBase()
{
	if (!Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase.OuterSingleton, Z_Construct_UClass_UEnvQueryItemType_CoverBase_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UEnvQueryItemType_CoverBase);
UEnvQueryItemType_CoverBase::~UEnvQueryItemType_CoverBase() {}
// ********** End Class UEnvQueryItemType_CoverBase ************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryItemType_CoverBase_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UEnvQueryItemType_CoverBase, UEnvQueryItemType_CoverBase::StaticClass, TEXT("UEnvQueryItemType_CoverBase"), &Z_Registration_Info_UClass_UEnvQueryItemType_CoverBase, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UEnvQueryItemType_CoverBase), 1837463990U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryItemType_CoverBase_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryItemType_CoverBase_h__Script_AICoverSystem_1063852863{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryItemType_CoverBase_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_EnvQueryItemType_CoverBase_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
