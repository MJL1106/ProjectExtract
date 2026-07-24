// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "BTService_OccupyCoversInPlace.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeBTService_OccupyCoversInPlace() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBTService_OccupyCoversInPlace();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UBTService_OccupyCoversInPlace_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UBTService();
COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FVector();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UBTService_OccupyCoversInPlace *******************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace;
UClass* UBTService_OccupyCoversInPlace::GetPrivateStaticClass()
{
	using TClass = UBTService_OccupyCoversInPlace;
	if (!Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("BTService_OccupyCoversInPlace"),
			Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace.InnerSingleton,
			StaticRegisterNativesUBTService_OccupyCoversInPlace,
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
	return Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace.InnerSingleton;
}
UClass* Z_Construct_UClass_UBTService_OccupyCoversInPlace_NoRegister()
{
	return UBTService_OccupyCoversInPlace::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Service that automatically occupies all covers inside character bounds\n */" },
#endif
		{ "IncludePath", "AI/Services/BTService_OccupyCoversInPlace.h" },
		{ "ModuleRelativePath", "Public/AI/Services/BTService_OccupyCoversInPlace.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Service that automatically occupies all covers inside character bounds" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bUseCharacterCapsuleBounds_MetaData[] = {
		{ "Category", "Service" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Should the service use character capsule bounds when occupying covers in pawn location?\n" },
#endif
		{ "ModuleRelativePath", "Public/AI/Services/BTService_OccupyCoversInPlace.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should the service use character capsule bounds when occupying covers in pawn location?" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_BoundExtents_MetaData[] = {
		{ "Category", "Service" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Bounds to use as fallback or when bUseCapsuleBounds is set to false\n" },
#endif
		{ "ModuleRelativePath", "Public/AI/Services/BTService_OccupyCoversInPlace.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Bounds to use as fallback or when bUseCapsuleBounds is set to false" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UBTService_OccupyCoversInPlace constinit property declarations ***********
	static void NewProp_bUseCharacterCapsuleBounds_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bUseCharacterCapsuleBounds;
	static const UECodeGen_Private::FStructPropertyParams NewProp_BoundExtents;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UBTService_OccupyCoversInPlace constinit property declarations *************
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UBTService_OccupyCoversInPlace>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics

// ********** Begin Class UBTService_OccupyCoversInPlace Property Definitions **********************
void Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::NewProp_bUseCharacterCapsuleBounds_SetBit(void* Obj)
{
	((UBTService_OccupyCoversInPlace*)Obj)->bUseCharacterCapsuleBounds = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::NewProp_bUseCharacterCapsuleBounds = { "bUseCharacterCapsuleBounds", nullptr, (EPropertyFlags)0x0020080000010001, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(UBTService_OccupyCoversInPlace), &Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::NewProp_bUseCharacterCapsuleBounds_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bUseCharacterCapsuleBounds_MetaData), NewProp_bUseCharacterCapsuleBounds_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::NewProp_BoundExtents = { "BoundExtents", nullptr, (EPropertyFlags)0x0020080000010001, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(UBTService_OccupyCoversInPlace, BoundExtents), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_BoundExtents_MetaData), NewProp_BoundExtents_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::NewProp_bUseCharacterCapsuleBounds,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::NewProp_BoundExtents,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::PropPointers) < 2048);
// ********** End Class UBTService_OccupyCoversInPlace Property Definitions ************************
UObject* (*const Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UBTService,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::ClassParams = {
	&UBTService_OccupyCoversInPlace::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::PropPointers),
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::Class_MetaDataParams), Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::Class_MetaDataParams)
};
void UBTService_OccupyCoversInPlace::StaticRegisterNativesUBTService_OccupyCoversInPlace()
{
}
UClass* Z_Construct_UClass_UBTService_OccupyCoversInPlace()
{
	if (!Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace.OuterSingleton, Z_Construct_UClass_UBTService_OccupyCoversInPlace_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UBTService_OccupyCoversInPlace);
UBTService_OccupyCoversInPlace::~UBTService_OccupyCoversInPlace() {}
// ********** End Class UBTService_OccupyCoversInPlace *********************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCoversInPlace_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UBTService_OccupyCoversInPlace, UBTService_OccupyCoversInPlace::StaticClass, TEXT("UBTService_OccupyCoversInPlace"), &Z_Registration_Info_UClass_UBTService_OccupyCoversInPlace, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UBTService_OccupyCoversInPlace), 2325073297U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCoversInPlace_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCoversInPlace_h__Script_AICoverSystem_48347778{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCoversInPlace_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_AI_Services_BTService_OccupyCoversInPlace_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
