// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "StaticCoverArchive.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeStaticCoverArchive() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverSerializedArchive();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin ScriptStruct FCoverSerializedArchive *******************************************
struct Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics
{
	static inline consteval int32 GetStructSize() { return sizeof(FCoverSerializedArchive); }
	static inline consteval int16 GetStructAlignment() { return alignof(FCoverSerializedArchive); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Archive that is saved as bytes and deserialized into inner FCoverArchive on load\n */" },
#endif
		{ "ModuleRelativePath", "Public/StaticCoverArchive.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Archive that is saved as bytes and deserialized into inner FCoverArchive on load" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_ByteData_MetaData[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Bulk data that is serialized to disk\n" },
#endif
		{ "ModuleRelativePath", "Public/StaticCoverArchive.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Bulk data that is serialized to disk" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FCoverSerializedArchive constinit property declarations ***********
	static const UECodeGen_Private::FBytePropertyParams NewProp_ByteData_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_ByteData;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FCoverSerializedArchive constinit property declarations *************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FCoverSerializedArchive>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FCoverSerializedArchive;
class UScriptStruct* FCoverSerializedArchive::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverSerializedArchive.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_FCoverSerializedArchive.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FCoverSerializedArchive, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("CoverSerializedArchive"));
	}
	return Z_Registration_Info_UScriptStruct_FCoverSerializedArchive.OuterSingleton;
	}

// ********** Begin ScriptStruct FCoverSerializedArchive Property Definitions **********************
const UECodeGen_Private::FBytePropertyParams Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::NewProp_ByteData_Inner = { "ByteData", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Byte, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, nullptr, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::NewProp_ByteData = { "ByteData", nullptr, (EPropertyFlags)0x0040000000000000, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverSerializedArchive, ByteData), EArrayPropertyFlags::None, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_ByteData_MetaData), NewProp_ByteData_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::NewProp_ByteData_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::NewProp_ByteData,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::PropPointers) < 2048);
// ********** End ScriptStruct FCoverSerializedArchive Property Definitions ************************
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	&NewStructOps,
	"CoverSerializedArchive",
	Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::PropPointers),
	sizeof(FCoverSerializedArchive),
	alignof(FCoverSerializedArchive),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FCoverSerializedArchive()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverSerializedArchive.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FCoverSerializedArchive.InnerSingleton, Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FCoverSerializedArchive.InnerSingleton);
}
// ********** End ScriptStruct FCoverSerializedArchive *********************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_StaticCoverArchive_h__Script_AICoverSystem_Statics
{
	static constexpr FStructRegisterCompiledInInfo ScriptStructInfo[] = {
		{ FCoverSerializedArchive::StaticStruct, Z_Construct_UScriptStruct_FCoverSerializedArchive_Statics::NewStructOps, TEXT("CoverSerializedArchive"),&Z_Registration_Info_UScriptStruct_FCoverSerializedArchive, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FCoverSerializedArchive), 1241907070U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_StaticCoverArchive_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_StaticCoverArchive_h__Script_AICoverSystem_748706014{
	TEXT("/Script/AICoverSystem"),
	nullptr, 0,
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_StaticCoverArchive_h__Script_AICoverSystem_Statics::ScriptStructInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_StaticCoverArchive_h__Script_AICoverSystem_Statics::ScriptStructInfo),
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
