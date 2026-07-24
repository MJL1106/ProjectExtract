// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "BlackboardKeyType_Cover.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeBlackboardKeyType_Cover() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBlackboardKeyType_Cover();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBlackboardKeyType_Cover_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UBlackboardKeyType();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UBlackboardKeyType_Cover *************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UBlackboardKeyType_Cover;
UClass* UBlackboardKeyType_Cover::GetPrivateStaticClass()
{
	using TClass = UBlackboardKeyType_Cover;
	if (!Z_Registration_Info_UClass_UBlackboardKeyType_Cover.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("BlackboardKeyType_Cover"),
			Z_Registration_Info_UClass_UBlackboardKeyType_Cover.InnerSingleton,
			StaticRegisterNativesUBlackboardKeyType_Cover,
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
	return Z_Registration_Info_UClass_UBlackboardKeyType_Cover.InnerSingleton;
}
UClass* Z_Construct_UClass_UBlackboardKeyType_Cover_NoRegister()
{
	return UBlackboardKeyType_Cover::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UBlackboardKeyType_Cover_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "AutoExpandCategories", "Blackboard" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Custom BB key type to store covers into AI blackboard\n */" },
#endif
		{ "DisplayName", "Cover" },
		{ "IncludePath", "AI/BlackboardKeyType_Cover.h" },
		{ "ModuleRelativePath", "Public/AI/BlackboardKeyType_Cover.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Custom BB key type to store covers into AI blackboard" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UBlackboardKeyType_Cover constinit property declarations *****************
// ********** End Class UBlackboardKeyType_Cover constinit property declarations *******************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBlackboardKeyType_Cover>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UBlackboardKeyType_Cover_Statics
UObject* (*const Z_Construct_UClass_UBlackboardKeyType_Cover_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UBlackboardKeyType,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBlackboardKeyType_Cover_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UBlackboardKeyType_Cover_Statics::ClassParams = {
	&UBlackboardKeyType_Cover::StaticClass,
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
	0x001030A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UBlackboardKeyType_Cover_Statics::Class_MetaDataParams), Z_Construct_UClass_UBlackboardKeyType_Cover_Statics::Class_MetaDataParams)
};
void UBlackboardKeyType_Cover::StaticRegisterNativesUBlackboardKeyType_Cover()
{
}
UClass* Z_Construct_UClass_UBlackboardKeyType_Cover()
{
	if (!Z_Registration_Info_UClass_UBlackboardKeyType_Cover.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UBlackboardKeyType_Cover.OuterSingleton, Z_Construct_UClass_UBlackboardKeyType_Cover_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UBlackboardKeyType_Cover.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UBlackboardKeyType_Cover);
UBlackboardKeyType_Cover::~UBlackboardKeyType_Cover() {}
// ********** End Class UBlackboardKeyType_Cover ***************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_BlackboardKeyType_Cover_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UBlackboardKeyType_Cover, UBlackboardKeyType_Cover::StaticClass, TEXT("UBlackboardKeyType_Cover"), &Z_Registration_Info_UClass_UBlackboardKeyType_Cover, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UBlackboardKeyType_Cover), 282131426U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_BlackboardKeyType_Cover_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_BlackboardKeyType_Cover_h__Script_AICoverSystem_3217409759{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_BlackboardKeyType_Cover_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_BlackboardKeyType_Cover_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
