// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "CoverSystemUtils.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "CoverSystemPublicData.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeCoverSystemUtils() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverSystemUtils();
AICOVERSYSTEM_API UClass* Z_Construct_UClass_UCoverSystemUtils_NoRegister();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCover();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverHandle();
AIMODULE_API UClass* Z_Construct_UClass_UBTNode_NoRegister();
AIMODULE_API UClass* Z_Construct_UClass_UEnvQueryInstanceBlueprintWrapper_NoRegister();
AIMODULE_API UScriptStruct* Z_Construct_UScriptStruct_FBlackboardKeySelector();
ENGINE_API UClass* Z_Construct_UClass_UBlueprintFunctionLibrary();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin Class UCoverSystemUtils Function GetBlackboardValueAsCover *********************
struct Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics
{
	struct CoverSystemUtils_eventGetBlackboardValueAsCover_Parms
	{
		UBTNode* NodeOwner;
		FBlackboardKeySelector Key;
		FCover ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Gets blackboard value as cover by selected key\n" },
#endif
		{ "DefaultToSelf", "NodeOwner" },
		{ "HidePin", "NodeOwner" },
		{ "ModuleRelativePath", "Public/CoverSystemUtils.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Gets blackboard value as cover by selected key" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Key_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function GetBlackboardValueAsCover constinit property declarations *************
	static const UECodeGen_Private::FObjectPropertyParams NewProp_NodeOwner;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Key;
	static const UECodeGen_Private::FStructPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetBlackboardValueAsCover constinit property declarations ***************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetBlackboardValueAsCover Property Definitions ************************
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::NewProp_NodeOwner = { "NodeOwner", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventGetBlackboardValueAsCover_Parms, NodeOwner), Z_Construct_UClass_UBTNode_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::NewProp_Key = { "Key", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventGetBlackboardValueAsCover_Parms, Key), Z_Construct_UScriptStruct_FBlackboardKeySelector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Key_MetaData), NewProp_Key_MetaData) }; // 3145079323
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventGetBlackboardValueAsCover_Parms, ReturnValue), Z_Construct_UScriptStruct_FCover, METADATA_PARAMS(0, nullptr) }; // 1149314207
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::NewProp_NodeOwner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::NewProp_Key,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::PropPointers) < 2048);
// ********** End Function GetBlackboardValueAsCover Property Definitions **************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverSystemUtils, nullptr, "GetBlackboardValueAsCover", 	Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::CoverSystemUtils_eventGetBlackboardValueAsCover_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x14422401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::CoverSystemUtils_eventGetBlackboardValueAsCover_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverSystemUtils::execGetBlackboardValueAsCover)
{
	P_GET_OBJECT(UBTNode,Z_Param_NodeOwner);
	P_GET_STRUCT_REF(FBlackboardKeySelector,Z_Param_Out_Key);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(FCover*)Z_Param__Result=UCoverSystemUtils::GetBlackboardValueAsCover(Z_Param_NodeOwner,Z_Param_Out_Key);
	P_NATIVE_END;
}
// ********** End Class UCoverSystemUtils Function GetBlackboardValueAsCover ***********************

// ********** Begin Class UCoverSystemUtils Function GetEnvironmentQueryResultsAsCovers ************
struct Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics
{
	struct CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms
	{
		UEnvQueryInstanceBlueprintWrapper* QueryInstance;
		TArray<FCover> ResultCovers;
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09* Blueprint utility function to fetch result from EQS instance as covers.\n\x09* Usage:\n\x09* 1. RunEQSQuery -> returns query instance\n\x09* 2. Bind event into OnQueryFinished\n\x09* 3. If the query status was success, call this function to fetch all results\n\x09* Note that run mode affects if single or multiple results are returned\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemUtils.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Blueprint utility function to fetch result from EQS instance as covers.\nUsage:\n1. RunEQSQuery -> returns query instance\n2. Bind event into OnQueryFinished\n3. If the query status was success, call this function to fetch all results\nNote that run mode affects if single or multiple results are returned" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Function GetEnvironmentQueryResultsAsCovers constinit property declarations ****
	static const UECodeGen_Private::FObjectPropertyParams NewProp_QueryInstance;
	static const UECodeGen_Private::FStructPropertyParams NewProp_ResultCovers_Inner;
	static const UECodeGen_Private::FArrayPropertyParams NewProp_ResultCovers;
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function GetEnvironmentQueryResultsAsCovers constinit property declarations ******
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function GetEnvironmentQueryResultsAsCovers Property Definitions ***************
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_QueryInstance = { "QueryInstance", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms, QueryInstance), Z_Construct_UClass_UEnvQueryInstanceBlueprintWrapper_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ResultCovers_Inner = { "ResultCovers", nullptr, (EPropertyFlags)0x0000000000000000, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, 0, Z_Construct_UScriptStruct_FCover, METADATA_PARAMS(0, nullptr) }; // 1149314207
const UECodeGen_Private::FArrayPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ResultCovers = { "ResultCovers", nullptr, (EPropertyFlags)0x0010000000000180, UECodeGen_Private::EPropertyGenFlags::Array, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms, ResultCovers), EArrayPropertyFlags::None, METADATA_PARAMS(0, nullptr) }; // 1149314207
void Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms), &Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_QueryInstance,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ResultCovers_Inner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ResultCovers,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::PropPointers) < 2048);
// ********** End Function GetEnvironmentQueryResultsAsCovers Property Definitions *****************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverSystemUtils, nullptr, "GetEnvironmentQueryResultsAsCovers", 	Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04422401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::CoverSystemUtils_eventGetEnvironmentQueryResultsAsCovers_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverSystemUtils::execGetEnvironmentQueryResultsAsCovers)
{
	P_GET_OBJECT(UEnvQueryInstanceBlueprintWrapper,Z_Param_QueryInstance);
	P_GET_TARRAY_REF(FCover,Z_Param_Out_ResultCovers);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=UCoverSystemUtils::GetEnvironmentQueryResultsAsCovers(Z_Param_QueryInstance,Z_Param_Out_ResultCovers);
	P_NATIVE_END;
}
// ********** End Class UCoverSystemUtils Function GetEnvironmentQueryResultsAsCovers **************

// ********** Begin Class UCoverSystemUtils Function IsCoverHandleValid ****************************
struct Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics
{
	struct CoverSystemUtils_eventIsCoverHandleValid_Parms
	{
		FCoverHandle Handle;
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Checks if a given handle is valid\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemUtils.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Checks if a given handle is valid" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Handle_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function IsCoverHandleValid constinit property declarations ********************
	static const UECodeGen_Private::FStructPropertyParams NewProp_Handle;
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function IsCoverHandleValid constinit property declarations **********************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function IsCoverHandleValid Property Definitions *******************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::NewProp_Handle = { "Handle", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventIsCoverHandleValid_Parms, Handle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Handle_MetaData), NewProp_Handle_MetaData) }; // 1932581828
void Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystemUtils_eventIsCoverHandleValid_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystemUtils_eventIsCoverHandleValid_Parms), &Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::NewProp_Handle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::PropPointers) < 2048);
// ********** End Function IsCoverHandleValid Property Definitions *********************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverSystemUtils, nullptr, "IsCoverHandleValid", 	Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::CoverSystemUtils_eventIsCoverHandleValid_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x14422401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::CoverSystemUtils_eventIsCoverHandleValid_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverSystemUtils::execIsCoverHandleValid)
{
	P_GET_STRUCT_REF(FCoverHandle,Z_Param_Out_Handle);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=UCoverSystemUtils::IsCoverHandleValid(Z_Param_Out_Handle);
	P_NATIVE_END;
}
// ********** End Class UCoverSystemUtils Function IsCoverHandleValid ******************************

// ********** Begin Class UCoverSystemUtils Function IsCoverValid **********************************
struct Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics
{
	struct CoverSystemUtils_eventIsCoverValid_Parms
	{
		FCover Cover;
		bool ReturnValue;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Checks if cover is valid\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemUtils.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Checks if cover is valid" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Cover_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function IsCoverValid constinit property declarations **************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_Cover;
	static void NewProp_ReturnValue_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_ReturnValue;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function IsCoverValid constinit property declarations ****************************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function IsCoverValid Property Definitions *************************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::NewProp_Cover = { "Cover", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventIsCoverValid_Parms, Cover), Z_Construct_UScriptStruct_FCover, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Cover_MetaData), NewProp_Cover_MetaData) }; // 1149314207
void Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::NewProp_ReturnValue_SetBit(void* Obj)
{
	((CoverSystemUtils_eventIsCoverValid_Parms*)Obj)->ReturnValue = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::NewProp_ReturnValue = { "ReturnValue", nullptr, (EPropertyFlags)0x0010000000000580, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(CoverSystemUtils_eventIsCoverValid_Parms), &Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::NewProp_ReturnValue_SetBit, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::NewProp_Cover,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::NewProp_ReturnValue,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::PropPointers) < 2048);
// ********** End Function IsCoverValid Property Definitions ***************************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverSystemUtils, nullptr, "IsCoverValid", 	Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::CoverSystemUtils_eventIsCoverValid_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x14422401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::CoverSystemUtils_eventIsCoverValid_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverSystemUtils::execIsCoverValid)
{
	P_GET_STRUCT_REF(FCover,Z_Param_Out_Cover);
	P_FINISH;
	P_NATIVE_BEGIN;
	*(bool*)Z_Param__Result=UCoverSystemUtils::IsCoverValid(Z_Param_Out_Cover);
	P_NATIVE_END;
}
// ********** End Class UCoverSystemUtils Function IsCoverValid ************************************

// ********** Begin Class UCoverSystemUtils Function SetBlackboardValueAsCover *********************
struct Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics
{
	struct CoverSystemUtils_eventSetBlackboardValueAsCover_Parms
	{
		UBTNode* NodeOwner;
		FBlackboardKeySelector Key;
		FCover Value;
	};
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Function_MetaDataParams[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Sets blackboard value as cover by selected key\n" },
#endif
		{ "DefaultToSelf", "NodeOwner" },
		{ "HidePin", "NodeOwner" },
		{ "ModuleRelativePath", "Public/CoverSystemUtils.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Sets blackboard value as cover by selected key" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Key_MetaData[] = {
		{ "NativeConst", "" },
	};
#endif // WITH_METADATA

// ********** Begin Function SetBlackboardValueAsCover constinit property declarations *************
	static const UECodeGen_Private::FObjectPropertyParams NewProp_NodeOwner;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Key;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Value;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End Function SetBlackboardValueAsCover constinit property declarations ***************
	static const UECodeGen_Private::FFunctionParams FuncParams;
};

// ********** Begin Function SetBlackboardValueAsCover Property Definitions ************************
const UECodeGen_Private::FObjectPropertyParams Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::NewProp_NodeOwner = { "NodeOwner", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Object, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventSetBlackboardValueAsCover_Parms, NodeOwner), Z_Construct_UClass_UBTNode_NoRegister, METADATA_PARAMS(0, nullptr) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::NewProp_Key = { "Key", nullptr, (EPropertyFlags)0x0010000008000182, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventSetBlackboardValueAsCover_Parms, Key), Z_Construct_UScriptStruct_FBlackboardKeySelector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Key_MetaData), NewProp_Key_MetaData) }; // 3145079323
const UECodeGen_Private::FStructPropertyParams Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::NewProp_Value = { "Value", nullptr, (EPropertyFlags)0x0010000000000080, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(CoverSystemUtils_eventSetBlackboardValueAsCover_Parms, Value), Z_Construct_UScriptStruct_FCover, METADATA_PARAMS(0, nullptr) }; // 1149314207
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::NewProp_NodeOwner,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::NewProp_Key,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::NewProp_Value,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::PropPointers) < 2048);
// ********** End Function SetBlackboardValueAsCover Property Definitions **************************
const UECodeGen_Private::FFunctionParams Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::FuncParams = { { (UObject*(*)())Z_Construct_UClass_UCoverSystemUtils, nullptr, "SetBlackboardValueAsCover", 	Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::PropPointers, 
	UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::PropPointers), 
sizeof(Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::CoverSystemUtils_eventSetBlackboardValueAsCover_Parms),
RF_Public|RF_Transient|RF_MarkAsNative, (EFunctionFlags)0x04422401, 0, 0, METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::Function_MetaDataParams), Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::Function_MetaDataParams)},  };
static_assert(sizeof(Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::CoverSystemUtils_eventSetBlackboardValueAsCover_Parms) < MAX_uint16);
UFunction* Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover()
{
	static UFunction* ReturnFunction = nullptr;
	if (!ReturnFunction)
	{
		UECodeGen_Private::ConstructUFunction(&ReturnFunction, Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover_Statics::FuncParams);
	}
	return ReturnFunction;
}
DEFINE_FUNCTION(UCoverSystemUtils::execSetBlackboardValueAsCover)
{
	P_GET_OBJECT(UBTNode,Z_Param_NodeOwner);
	P_GET_STRUCT_REF(FBlackboardKeySelector,Z_Param_Out_Key);
	P_GET_STRUCT(FCover,Z_Param_Value);
	P_FINISH;
	P_NATIVE_BEGIN;
	UCoverSystemUtils::SetBlackboardValueAsCover(Z_Param_NodeOwner,Z_Param_Out_Key,Z_Param_Value);
	P_NATIVE_END;
}
// ********** End Class UCoverSystemUtils Function SetBlackboardValueAsCover ***********************

// ********** Begin Class UCoverSystemUtils ********************************************************
FClassRegistrationInfo Z_Registration_Info_UClass_UCoverSystemUtils;
UClass* UCoverSystemUtils::GetPrivateStaticClass()
{
	using TClass = UCoverSystemUtils;
	if (!Z_Registration_Info_UClass_UCoverSystemUtils.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			TClass::StaticPackage(),
			TEXT("CoverSystemUtils"),
			Z_Registration_Info_UClass_UCoverSystemUtils.InnerSingleton,
			StaticRegisterNativesUCoverSystemUtils,
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
	return Z_Registration_Info_UClass_UCoverSystemUtils.InnerSingleton;
}
UClass* Z_Construct_UClass_UCoverSystemUtils_NoRegister()
{
	return UCoverSystemUtils::GetPrivateStaticClass();
}
struct Z_Construct_UClass_UCoverSystemUtils_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Utility library for cover system\n */" },
#endif
		{ "IncludePath", "CoverSystemUtils.h" },
		{ "ModuleRelativePath", "Public/CoverSystemUtils.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Utility library for cover system" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin Class UCoverSystemUtils constinit property declarations ************************
// ********** End Class UCoverSystemUtils constinit property declarations **************************
	static constexpr UE::CodeGen::FClassNativeFunction Funcs[] = {
		{ .NameUTF8 = UTF8TEXT("GetBlackboardValueAsCover"), .Pointer = &UCoverSystemUtils::execGetBlackboardValueAsCover },
		{ .NameUTF8 = UTF8TEXT("GetEnvironmentQueryResultsAsCovers"), .Pointer = &UCoverSystemUtils::execGetEnvironmentQueryResultsAsCovers },
		{ .NameUTF8 = UTF8TEXT("IsCoverHandleValid"), .Pointer = &UCoverSystemUtils::execIsCoverHandleValid },
		{ .NameUTF8 = UTF8TEXT("IsCoverValid"), .Pointer = &UCoverSystemUtils::execIsCoverValid },
		{ .NameUTF8 = UTF8TEXT("SetBlackboardValueAsCover"), .Pointer = &UCoverSystemUtils::execSetBlackboardValueAsCover },
	};
	static UObject* (*const DependentSingletons[])();
	static constexpr FClassFunctionLinkInfo FuncInfo[] = {
		{ &Z_Construct_UFunction_UCoverSystemUtils_GetBlackboardValueAsCover, "GetBlackboardValueAsCover" }, // 1838710826
		{ &Z_Construct_UFunction_UCoverSystemUtils_GetEnvironmentQueryResultsAsCovers, "GetEnvironmentQueryResultsAsCovers" }, // 939865408
		{ &Z_Construct_UFunction_UCoverSystemUtils_IsCoverHandleValid, "IsCoverHandleValid" }, // 496447172
		{ &Z_Construct_UFunction_UCoverSystemUtils_IsCoverValid, "IsCoverValid" }, // 4172606307
		{ &Z_Construct_UFunction_UCoverSystemUtils_SetBlackboardValueAsCover, "SetBlackboardValueAsCover" }, // 2678452401
	};
	static_assert(UE_ARRAY_COUNT(FuncInfo) < 2048);
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<UCoverSystemUtils>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
}; // struct Z_Construct_UClass_UCoverSystemUtils_Statics
UObject* (*const Z_Construct_UClass_UCoverSystemUtils_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_UBlueprintFunctionLibrary,
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverSystemUtils_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_UCoverSystemUtils_Statics::ClassParams = {
	&UCoverSystemUtils::StaticClass,
	nullptr,
	&StaticCppClassTypeInfo,
	DependentSingletons,
	FuncInfo,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	UE_ARRAY_COUNT(FuncInfo),
	0,
	0,
	0x001000A0u,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_UCoverSystemUtils_Statics::Class_MetaDataParams), Z_Construct_UClass_UCoverSystemUtils_Statics::Class_MetaDataParams)
};
void UCoverSystemUtils::StaticRegisterNativesUCoverSystemUtils()
{
	UClass* Class = UCoverSystemUtils::StaticClass();
	FNativeFunctionRegistrar::RegisterFunctions(Class, MakeConstArrayView(Z_Construct_UClass_UCoverSystemUtils_Statics::Funcs));
}
UClass* Z_Construct_UClass_UCoverSystemUtils()
{
	if (!Z_Registration_Info_UClass_UCoverSystemUtils.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_UCoverSystemUtils.OuterSingleton, Z_Construct_UClass_UCoverSystemUtils_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_UCoverSystemUtils.OuterSingleton;
}
UCoverSystemUtils::UCoverSystemUtils(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}
DEFINE_VTABLE_PTR_HELPER_CTOR_NS(, UCoverSystemUtils);
UCoverSystemUtils::~UCoverSystemUtils() {}
// ********** End Class UCoverSystemUtils **********************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h__Script_AICoverSystem_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_UCoverSystemUtils, UCoverSystemUtils::StaticClass, TEXT("UCoverSystemUtils"), &Z_Registration_Info_UClass_UCoverSystemUtils, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(UCoverSystemUtils), 3213181049U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h__Script_AICoverSystem_4145843801{
	TEXT("/Script/AICoverSystem"),
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h__Script_AICoverSystem_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemUtils_h__Script_AICoverSystem_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
