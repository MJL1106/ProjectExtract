// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "CoverPartitionInvokerComponent.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeCoverPartitionInvokerComponent() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverPartitionInvokerComponent();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverPartitionInvokerComponent_NoRegister();
ENGINE_API UClass* Z_Construct_UClass_UActorComponent();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UCoverPartitionInvokerComponent Function HasRegistedInvoker **************
struct Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics
{
	struct CoverPartitionInvokerComponent_eventHasRegistedInvoker_Parms
	{
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Is the invoker currently registered to cover system as invoker?\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverPartitionInvokerComponent.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is the invoker currently registered to cover system as invoker?" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function HasRegistedInvoker constinit property declarations ********************
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function HasRegistedInvoker constinit property declarations **********************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function HasRegistedInvoker Property Definitions *******************************
void Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverPartitionInvokerComponent_eventHasRegistedInvoker_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverPartitionInvokerComponent_eventHasRegistedInvoker_Parms), &Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::PropPointers) < 2048);
// ********** End Function HasRegistedInvoker Property Definitions *********************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverPartitionInvokerComponent, nullptr, "HasRegistedInvoker", 	Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::CoverPartitionInvokerComponent_eventHasRegistedInvoker_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x54020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::CoverPartitionInvokerComponent_eventHasRegistedInvoker_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverPartitionInvokerComponent::execHasRegistedInvoker)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=P_THIS->HasRegistedInvoker();
	P_NATIVE_END;
}
// ********** End Class UCoverPartitionInvokerComponent Function HasRegistedInvoker ****************

// ********** Begin Class UCoverPartitionInvokerComponent Function RegisterInvoker *****************
struct Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Registers this invoker to cover system to enable it\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverPartitionInvokerComponent.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Registers this invoker to cover system to enable it" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function RegisterInvoker constinit property declarations ***********************
// ********** End Function RegisterInvoker constinit property declarations *************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverPartitionInvokerComponent, nullptr, "RegisterInvoker", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverPartitionInvokerComponent::execRegisterInvoker)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->RegisterInvoker();
	P_NATIVE_END;
}
// ********** End Class UCoverPartitionInvokerComponent Function RegisterInvoker *******************

// ********** Begin Class UCoverPartitionInvokerComponent Function UnregisterInvoker ***************
struct Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Unregisters this invoker from cover system to disable it\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverPartitionInvokerComponent.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Unregisters this invoker from cover system to disable it" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function UnregisterInvoker constinit property declarations *********************
// ********** End Function UnregisterInvoker constinit property declarations ***********************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverPartitionInvokerComponent, nullptr, "UnregisterInvoker", 	nullptr, 
	0, 
0,
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04020401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker_Statics::Function_MetaDataParams)},  };
UFunction* Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverPartitionInvokerComponent::execUnregisterInvoker)
{
	P_FINISH;
	P_NATIVE_BEGIN;
	P_THIS->UnregisterInvoker();
	P_NATIVE_END;
}
// ********** End Class UCoverPartitionInvokerComponent Function UnregisterInvoker *****************

// ********** Begin Class UCoverPartitionInvokerComponent ******************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UCoverPartitionInvokerComponent;
UClass* UCoverPartitionInvokerComponent::GetPrivateStaticClass()
{
	using TClass = UCoverPartitionInvokerComponent;
	if (!Z_Registration_Info_UClass_UCoverPartitionInvokerComponent.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("CoverPartitionInvokerComponent"),
			Z_Registration_Info_UClass_UCoverPartitionInvokerComponent.InnerSingleton,
			StaticRegisterNativesUCoverPartitionInvokerComponent,
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
	return Z_Registration_Info_UClass_UCoverPartitionInvokerComponent.InnerSingleton;
}
UClass* Z_Construct_UClass_UCoverPartitionInvokerComponent_NoRegister()
{
	return UCoverPartitionInvokerComponent::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
		{ "BlueprintSpawnableComponent", "" },
		{ "ClassGroupNames", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Component that registers to Cover System and acts as partition invoker, so partitions around the owner of this component are generated\n */" },
#endif
		{ "IncludePath", "CoverPartitionInvokerComponent.h" },
		{ "ModuleRelativePath", "Public/CoverPartitionInvokerComponent.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Component that registers to Cover System and acts as partition invoker, so partitions around the owner of this component are generated" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bAutoRegisterInvoker_MetaData[] = {
		{ "Category", "Cover System" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Should this automatically register as invoker on begin play?\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverPartitionInvokerComponent.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Should this automatically register as invoker on begin play?" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UCoverPartitionInvokerComponent constinit property declarations **********
	static void NewProp_bAutoRegisterInvoker_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bAutoRegisterInvoker;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Class UCoverPartitionInvokerComponent constinit property declarations ************
	static constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {
		{ .NameUTF8 = UTF8TEXT("HasRegistedInvoker"), .Pointer = &UCoverPartitionInvokerComponent::execHasRegistedInvoker },
		{ .NameUTF8 = UTF8TEXT("RegisterInvoker"), .Pointer = &UCoverPartitionInvokerComponent::execRegisterInvoker },
		{ .NameUTF8 = UTF8TEXT("UnregisterInvoker"), .Pointer = &UCoverPartitionInvokerComponent::execUnregisterInvoker },
	};
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
		{ &Z_Construct_UFunction_UCoverPartitionInvokerComponent_HasRegistedInvoker, "HasRegistedInvoker" }, // 3244263838
		{ &Z_Construct_UFunction_UCoverPartitionInvokerComponent_RegisterInvoker, "RegisterInvoker" }, // 814014706
		{ &Z_Construct_UFunction_UCoverPartitionInvokerComponent_UnregisterInvoker, "UnregisterInvoker" }, // 392080915
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UCoverPartitionInvokerComponent>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics

// ********** Begin Class UCoverPartitionInvokerComponent Property Definitions *********************
void Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::NewProp_bAutoRegisterInvoker_SetBit(void* Obj)
{
	((UCoverPartitionInvokerComponent*)Obj)->bAutoRegisterInvoker = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::NewProp_bAutoRegisterInvoker = { "bAutoRegisterInvoker", nullptr, (EPropertyFlags)0x0020080000000015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(UCoverPartitionInvokerComponent), &Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::NewProp_bAutoRegisterInvoker_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bAutoRegisterInvoker_MetaData), NewProp_bAutoRegisterInvoker_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::NewProp_bAutoRegisterInvoker,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::PropPointers) < 2048);
// ********** End Class UCoverPartitionInvokerComponent Property Definitions ***********************
UObject* (*const Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UActorComponent,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::ClassParams = {
	&UCoverPartitionInvokerComponent::StaticClass,
	"Engine",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::PropPointers,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	UE_ARRAY_COUNT(Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::PropPointers),
	0,
	0x00B000A4u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::Class_MetaDataParams), Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::Class_MetaDataParams)
};
void UCoverPartitionInvokerComponent::StaticRegisterNativesUCoverPartitionInvokerComponent()
{
	UClass* Class = UCoverPartitionInvokerComponent::StaticClass();
	FNativeFunctionRegistrar::RegisterFunctions(Class, MakeConstArrayView(Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::Funcs));
}
UClass* Z_Construct_UClass_UCoverPartitionInvokerComponent()
{
	if (!Z_Registration_Info_UClass_UCoverPartitionInvokerComponent.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UCoverPartitionInvokerComponent.OuterSingleton, Z_Construct_UClass_UCoverPartitionInvokerComponent_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UCoverPartitionInvokerComponent.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UCoverPartitionInvokerComponent);
UCoverPartitionInvokerComponent::~UCoverPartitionInvokerComponent() {}
// ********** End Class UCoverPartitionInvokerComponent ********************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverPartitionInvokerComponent_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UCoverPartitionInvokerComponent, UCoverPartitionInvokerComponent::StaticClass, TEXT("UCoverPartitionInvokerComponent"), &Z_Registration_Info_UClass_UCoverPartitionInvokerComponent, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UCoverPartitionInvokerComponent), 1573850542U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverPartitionInvokerComponent_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverPartitionInvokerComponent_h__Script_AICoverSystem_364929961{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverPartitionInvokerComponent_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverPartitionInvokerComponent_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
