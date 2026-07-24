// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "CoverSystemPublicData.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
static_assert(!UE_WITH_CONSTINIT_UOBJECT, "This generated code can only be compiled with !UE_WITH_CONSTINIT_OBJECT");
void EmptyLinkFunctionForGeneratedCodeCoverSystemPublicData() {}

// ********** Begin Cross Module References ********************************************************
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCover();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverBuildParams();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverData();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverHandle();
AICOVERSYSTEM_API UScriptStruct* Z_Construct_UScriptStruct_FCoverPartitionHash();
COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FRotator();
COREUOBJECT_API UScriptStruct* Z_Construct_UScriptStruct_FVector();
UPackage* Z_Construct_UPackage__Script_AICoverSystem();
// ********** End Cross Module References **********************************************************

// ********** Begin ScriptStruct FCoverBuildParams *************************************************
struct Z_Construct_UScriptStruct_FCoverBuildParams_Statics
{
	static inline consteval int32 GetStructSize() { return sizeof(FCoverBuildParams); }
	static inline consteval int16 GetStructAlignment() { return alignof(FCoverBuildParams); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n* Build parameters for the cover generation\n*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Build parameters for the cover generation" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SegmentLength_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Length of nav mesh sub segments. Smaller value is more accurate but consumes more CPU\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Length of nav mesh sub segments. Smaller value is more accurate but consumes more CPU" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TraceLength_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Length of trace when trying to find blocking geometry in direction of nav segment\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Length of trace when trying to find blocking geometry in direction of nav segment" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_TraceStepDistance_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Raytrace substep distance when finding geometry that may provide cover\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Raytrace substep distance when finding geometry that may provide cover" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_SweepTraceRadius_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** \n\x09* Size of the sphere sweep used to determine if cover can be used to shoot to certain directions\n\x09* Use 0 to check it with raycasts which is faster but may not be as accurate as sweep\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Size of the sphere sweep used to determine if cover can be used to shoot to certain directions\nUse 0 to check it with raycasts which is faster but may not be as accurate as sweep" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_MinSpaceBetweenValidPoints_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Minimum space between generated covers\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Minimum space between generated covers" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_AgentMaxWidth_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n\x09* The maximum width of the agents.\n\x09* Generation ensures that agent with this width is fully covered behind an object if standing at cover\n\x09*/" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "The maximum width of the agents.\nGeneration ensures that agent with this width is fully covered behind an object if standing at cover" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_AgentMaxHeightCrouch_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** The maximum height of the agents when crouching */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "The maximum height of the agents when crouching" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_AgentMaxHeightStanding_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** The maximum height of the agents when standing */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "The maximum height of the agents when standing" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_OffsetWhenLeaningSides_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** The distance between in cover position and leaning out of the cover on the sides */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "The distance between in cover position and leaning out of the cover on the sides" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_OffsetFrontAim_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** The distance in front of a shooting position that must be free */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "The distance in front of a shooting position that must be free" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FCoverBuildParams constinit property declarations *****************
	static const UECodeGen_Private::FFloatPropertyParams NewProp_SegmentLength;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_TraceLength;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_TraceStepDistance;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_SweepTraceRadius;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_MinSpaceBetweenValidPoints;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_AgentMaxWidth;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_AgentMaxHeightCrouch;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_AgentMaxHeightStanding;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_OffsetWhenLeaningSides;
	static const UECodeGen_Private::FFloatPropertyParams NewProp_OffsetFrontAim;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FCoverBuildParams constinit property declarations *******************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FCoverBuildParams>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct Z_Construct_UScriptStruct_FCoverBuildParams_Statics
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FCoverBuildParams;
class UScriptStruct* FCoverBuildParams::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverBuildParams.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_FCoverBuildParams.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FCoverBuildParams, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("CoverBuildParams"));
	}
	return Z_Registration_Info_UScriptStruct_FCoverBuildParams.OuterSingleton;
	}

// ********** Begin ScriptStruct FCoverBuildParams Property Definitions ****************************
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_SegmentLength = { "SegmentLength", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, SegmentLength), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SegmentLength_MetaData), NewProp_SegmentLength_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_TraceLength = { "TraceLength", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, TraceLength), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TraceLength_MetaData), NewProp_TraceLength_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_TraceStepDistance = { "TraceStepDistance", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, TraceStepDistance), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_TraceStepDistance_MetaData), NewProp_TraceStepDistance_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_SweepTraceRadius = { "SweepTraceRadius", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, SweepTraceRadius), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_SweepTraceRadius_MetaData), NewProp_SweepTraceRadius_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_MinSpaceBetweenValidPoints = { "MinSpaceBetweenValidPoints", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, MinSpaceBetweenValidPoints), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_MinSpaceBetweenValidPoints_MetaData), NewProp_MinSpaceBetweenValidPoints_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_AgentMaxWidth = { "AgentMaxWidth", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, AgentMaxWidth), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_AgentMaxWidth_MetaData), NewProp_AgentMaxWidth_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_AgentMaxHeightCrouch = { "AgentMaxHeightCrouch", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, AgentMaxHeightCrouch), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_AgentMaxHeightCrouch_MetaData), NewProp_AgentMaxHeightCrouch_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_AgentMaxHeightStanding = { "AgentMaxHeightStanding", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, AgentMaxHeightStanding), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_AgentMaxHeightStanding_MetaData), NewProp_AgentMaxHeightStanding_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_OffsetWhenLeaningSides = { "OffsetWhenLeaningSides", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, OffsetWhenLeaningSides), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_OffsetWhenLeaningSides_MetaData), NewProp_OffsetWhenLeaningSides_MetaData) };
const UECodeGen_Private::FFloatPropertyParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_OffsetFrontAim = { "OffsetFrontAim", nullptr, (EPropertyFlags)0x0010000000000005, UECodeGen_Private::EPropertyGenFlags::Float, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverBuildParams, OffsetFrontAim), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_OffsetFrontAim_MetaData), NewProp_OffsetFrontAim_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FCoverBuildParams_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_SegmentLength,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_TraceLength,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_TraceStepDistance,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_SweepTraceRadius,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_MinSpaceBetweenValidPoints,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_AgentMaxWidth,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_AgentMaxHeightCrouch,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_AgentMaxHeightStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_OffsetWhenLeaningSides,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewProp_OffsetFrontAim,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverBuildParams_Statics::PropPointers) < 2048);
// ********** End ScriptStruct FCoverBuildParams Property Definitions ******************************
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FCoverBuildParams_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	&NewStructOps,
	"CoverBuildParams",
	Z_Construct_UScriptStruct_FCoverBuildParams_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverBuildParams_Statics::PropPointers),
	sizeof(FCoverBuildParams),
	alignof(FCoverBuildParams),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverBuildParams_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FCoverBuildParams_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FCoverBuildParams()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverBuildParams.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FCoverBuildParams.InnerSingleton, Z_Construct_UScriptStruct_FCoverBuildParams_Statics::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FCoverBuildParams.InnerSingleton);
}
// ********** End ScriptStruct FCoverBuildParams ***************************************************

// ********** Begin ScriptStruct FCoverData ********************************************************
struct Z_Construct_UScriptStruct_FCoverData_Statics
{
	static inline consteval int32 GetStructSize() { return sizeof(FCoverData); }
	static inline consteval int16 GetStructAlignment() { return alignof(FCoverData); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Represents data of single cover\n */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Represents data of single cover" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Location_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**  Location of the cover */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Location of the cover" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Rotation_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Rotator from X of direction to wall */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Rotator from X of direction to wall" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_DirectionToWall_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Direction to wall (Perpendicular to cover) */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Direction to wall (Perpendicular to cover)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bLeftCoverStanding_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Left cover (can lean on left) */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Left cover (can lean on left)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bRightCoverStanding_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Right cover (can lean on Right) */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Right cover (can lean on Right)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bLeftCoverCrouched_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Left cover (can lean on left) */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Left cover (can lean on left)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bRightCoverCrouched_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a Right cover (can lean on Right) */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a Right cover (can lean on Right)" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bFrontCoverCrouched_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a front cover, so crouch gives cover and while standing there's line of sight for shooting */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a front cover, so crouch gives cover and while standing there's line of sight for shooting" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_bCrouchedCover_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/** Is it a cover requiring crouch */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Is it a cover requiring crouch" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FCoverData constinit property declarations ************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_Location;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Rotation;
	static const UECodeGen_Private::FStructPropertyParams NewProp_DirectionToWall;
	static void NewProp_bLeftCoverStanding_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bLeftCoverStanding;
	static void NewProp_bRightCoverStanding_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bRightCoverStanding;
	static void NewProp_bLeftCoverCrouched_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bLeftCoverCrouched;
	static void NewProp_bRightCoverCrouched_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bRightCoverCrouched;
	static void NewProp_bFrontCoverCrouched_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bFrontCoverCrouched;
	static void NewProp_bCrouchedCover_SetBit(void* Obj);
	static const UECodeGen_Private::FBoolPropertyParams NewProp_bCrouchedCover;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FCoverData constinit property declarations **************************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FCoverData>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct Z_Construct_UScriptStruct_FCoverData_Statics
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FCoverData;
class UScriptStruct* FCoverData::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverData.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_FCoverData.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FCoverData, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("CoverData"));
	}
	return Z_Registration_Info_UScriptStruct_FCoverData.OuterSingleton;
	}

// ********** Begin ScriptStruct FCoverData Property Definitions ***********************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_Location = { "Location", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverData, Location), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Location_MetaData), NewProp_Location_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_Rotation = { "Rotation", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverData, Rotation), Z_Construct_UScriptStruct_FRotator, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Rotation_MetaData), NewProp_Rotation_MetaData) };
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_DirectionToWall = { "DirectionToWall", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverData, DirectionToWall), Z_Construct_UScriptStruct_FVector, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_DirectionToWall_MetaData), NewProp_DirectionToWall_MetaData) };
void Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverStanding_SetBit(void* Obj)
{
	((FCoverData*)Obj)->bLeftCoverStanding = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverStanding = { "bLeftCoverStanding", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FCoverData), &Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverStanding_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bLeftCoverStanding_MetaData), NewProp_bLeftCoverStanding_MetaData) };
void Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverStanding_SetBit(void* Obj)
{
	((FCoverData*)Obj)->bRightCoverStanding = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverStanding = { "bRightCoverStanding", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FCoverData), &Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverStanding_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bRightCoverStanding_MetaData), NewProp_bRightCoverStanding_MetaData) };
void Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverCrouched_SetBit(void* Obj)
{
	((FCoverData*)Obj)->bLeftCoverCrouched = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverCrouched = { "bLeftCoverCrouched", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FCoverData), &Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverCrouched_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bLeftCoverCrouched_MetaData), NewProp_bLeftCoverCrouched_MetaData) };
void Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverCrouched_SetBit(void* Obj)
{
	((FCoverData*)Obj)->bRightCoverCrouched = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverCrouched = { "bRightCoverCrouched", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FCoverData), &Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverCrouched_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bRightCoverCrouched_MetaData), NewProp_bRightCoverCrouched_MetaData) };
void Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bFrontCoverCrouched_SetBit(void* Obj)
{
	((FCoverData*)Obj)->bFrontCoverCrouched = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bFrontCoverCrouched = { "bFrontCoverCrouched", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FCoverData), &Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bFrontCoverCrouched_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bFrontCoverCrouched_MetaData), NewProp_bFrontCoverCrouched_MetaData) };
void Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bCrouchedCover_SetBit(void* Obj)
{
	((FCoverData*)Obj)->bCrouchedCover = 1;
}
const UECodeGen_Private::FBoolPropertyParams Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bCrouchedCover = { "bCrouchedCover", nullptr, (EPropertyFlags)0x0010000000020015, UECodeGen_Private::EPropertyGenFlags::Bool | UECodeGen_Private::EPropertyGenFlags::NativeBool, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, sizeof(bool), sizeof(FCoverData), &Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bCrouchedCover_SetBit, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_bCrouchedCover_MetaData), NewProp_bCrouchedCover_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FCoverData_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_Location,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_Rotation,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_DirectionToWall,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverStanding,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bLeftCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bRightCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bFrontCoverCrouched,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverData_Statics::NewProp_bCrouchedCover,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverData_Statics::PropPointers) < 2048);
// ********** End ScriptStruct FCoverData Property Definitions *************************************
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FCoverData_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	&NewStructOps,
	"CoverData",
	Z_Construct_UScriptStruct_FCoverData_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverData_Statics::PropPointers),
	sizeof(FCoverData),
	alignof(FCoverData),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverData_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FCoverData_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FCoverData()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverData.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FCoverData.InnerSingleton, Z_Construct_UScriptStruct_FCoverData_Statics::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FCoverData.InnerSingleton);
}
// ********** End ScriptStruct FCoverData **********************************************************

// ********** Begin ScriptStruct FCoverPartitionHash ***********************************************
struct Z_Construct_UScriptStruct_FCoverPartitionHash_Statics
{
	static inline consteval int32 GetStructSize() { return sizeof(FCoverPartitionHash); }
	static inline consteval int16 GetStructAlignment() { return alignof(FCoverPartitionHash); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Handle to access certain partition in cover system\n * Each partition has own cover system proxy\n */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Handle to access certain partition in cover system\nEach partition has own cover system proxy" },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_X_MetaData[] = {
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Y_MetaData[] = {
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Z_MetaData[] = {
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FCoverPartitionHash constinit property declarations ***************
	static const UECodeGen_Private::FInt16PropertyParams NewProp_X;
	static const UECodeGen_Private::FInt16PropertyParams NewProp_Y;
	static const UECodeGen_Private::FInt16PropertyParams NewProp_Z;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FCoverPartitionHash constinit property declarations *****************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FCoverPartitionHash>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct Z_Construct_UScriptStruct_FCoverPartitionHash_Statics
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FCoverPartitionHash;
class UScriptStruct* FCoverPartitionHash::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverPartitionHash.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_FCoverPartitionHash.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FCoverPartitionHash, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("CoverPartitionHash"));
	}
	return Z_Registration_Info_UScriptStruct_FCoverPartitionHash.OuterSingleton;
	}

// ********** Begin ScriptStruct FCoverPartitionHash Property Definitions **************************
const UECodeGen_Private::FInt16PropertyParams Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewProp_X = { "X", nullptr, (EPropertyFlags)0x0040000000000000, UECodeGen_Private::EPropertyGenFlags::Int16, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverPartitionHash, X), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_X_MetaData), NewProp_X_MetaData) };
const UECodeGen_Private::FInt16PropertyParams Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewProp_Y = { "Y", nullptr, (EPropertyFlags)0x0040000000000000, UECodeGen_Private::EPropertyGenFlags::Int16, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverPartitionHash, Y), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Y_MetaData), NewProp_Y_MetaData) };
const UECodeGen_Private::FInt16PropertyParams Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewProp_Z = { "Z", nullptr, (EPropertyFlags)0x0040000000000000, UECodeGen_Private::EPropertyGenFlags::Int16, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCoverPartitionHash, Z), METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Z_MetaData), NewProp_Z_MetaData) };
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewProp_X,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewProp_Y,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewProp_Z,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::PropPointers) < 2048);
// ********** End ScriptStruct FCoverPartitionHash Property Definitions ****************************
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	&NewStructOps,
	"CoverPartitionHash",
	Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::PropPointers),
	sizeof(FCoverPartitionHash),
	alignof(FCoverPartitionHash),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FCoverPartitionHash()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverPartitionHash.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FCoverPartitionHash.InnerSingleton, Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FCoverPartitionHash.InnerSingleton);
}
// ********** End ScriptStruct FCoverPartitionHash *************************************************

// ********** Begin ScriptStruct FCoverHandle ******************************************************
struct Z_Construct_UScriptStruct_FCoverHandle_Statics
{
	static inline consteval int32 GetStructSize() { return sizeof(FCoverHandle); }
	static inline consteval int16 GetStructAlignment() { return alignof(FCoverHandle); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Unique handle to access certain cover in the memory\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Unique handle to access certain cover in the memory" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FCoverHandle constinit property declarations **********************
// ********** End ScriptStruct FCoverHandle constinit property declarations ************************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FCoverHandle>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct Z_Construct_UScriptStruct_FCoverHandle_Statics
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FCoverHandle;
class UScriptStruct* FCoverHandle::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverHandle.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_FCoverHandle.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FCoverHandle, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("CoverHandle"));
	}
	return Z_Registration_Info_UScriptStruct_FCoverHandle.OuterSingleton;
	}
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FCoverHandle_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	&NewStructOps,
	"CoverHandle",
	nullptr,
	0,
	sizeof(FCoverHandle),
	alignof(FCoverHandle),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCoverHandle_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FCoverHandle_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FCoverHandle()
{
	if (!Z_Registration_Info_UScriptStruct_FCoverHandle.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FCoverHandle.InnerSingleton, Z_Construct_UScriptStruct_FCoverHandle_Statics::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FCoverHandle.InnerSingleton);
}
// ********** End ScriptStruct FCoverHandle ********************************************************

// ********** Begin ScriptStruct FCover ************************************************************
struct Z_Construct_UScriptStruct_FCover_Statics
{
	static inline consteval int32 GetStructSize() { return sizeof(FCover); }
	static inline consteval int16 GetStructAlignment() { return alignof(FCover); }
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Struct_MetaDataParams[] = {
		{ "BlueprintType", "true" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n * Represents a single cover.\n */" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Represents a single cover." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Handle_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Handle to the cover.\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Handle to the cover." },
#endif
	};
	static constexpr UECodeGen_Private::FMetaDataPairParam NewProp_Data_MetaData[] = {
		{ "Category", "Cover" },
#if !UE_BUILD_SHIPPING
		{ "Comment", "// Data of the cover\n" },
#endif
		{ "ModuleRelativePath", "Public/CoverSystemPublicData.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Data of the cover" },
#endif
	};
#endif // WITH_METADATA

// ********** Begin ScriptStruct FCover constinit property declarations ****************************
	static const UECodeGen_Private::FStructPropertyParams NewProp_Handle;
	static const UECodeGen_Private::FStructPropertyParams NewProp_Data;
	static const UECodeGen_Private::FPropertyParamsBase* const PropPointers[];
// ********** End ScriptStruct FCover constinit property declarations ******************************
	static void* NewStructOps()
	{
		return (UScriptStruct::ICppStructOps*)new UScriptStruct::TCppStructOps<FCover>();
	}
	static const UECodeGen_Private::FStructParams StructParams;
}; // struct Z_Construct_UScriptStruct_FCover_Statics
static FStructRegistrationInfo Z_Registration_Info_UScriptStruct_FCover;
class UScriptStruct* FCover::StaticStruct()
{
	if (!Z_Registration_Info_UScriptStruct_FCover.OuterSingleton)
	{
		Z_Registration_Info_UScriptStruct_FCover.OuterSingleton = GetStaticStruct(Z_Construct_UScriptStruct_FCover, (UObject*)Z_Construct_UPackage__Script_AICoverSystem(), TEXT("Cover"));
	}
	return Z_Registration_Info_UScriptStruct_FCover.OuterSingleton;
	}

// ********** Begin ScriptStruct FCover Property Definitions ***************************************
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FCover_Statics::NewProp_Handle = { "Handle", nullptr, (EPropertyFlags)0x0010000000020005, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCover, Handle), Z_Construct_UScriptStruct_FCoverHandle, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Handle_MetaData), NewProp_Handle_MetaData) }; // 1932581828
const UECodeGen_Private::FStructPropertyParams Z_Construct_UScriptStruct_FCover_Statics::NewProp_Data = { "Data", nullptr, (EPropertyFlags)0x0010000000020005, UECodeGen_Private::EPropertyGenFlags::Struct, RF_Public|RF_Transient|RF_MarkAsNative, nullptr, nullptr, 1, STRUCT_OFFSET(FCover, Data), Z_Construct_UScriptStruct_FCoverData, METADATA_PARAMS(UE_ARRAY_COUNT(NewProp_Data_MetaData), NewProp_Data_MetaData) }; // 2527779184
const UECodeGen_Private::FPropertyParamsBase* const Z_Construct_UScriptStruct_FCover_Statics::PropPointers[] = {
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCover_Statics::NewProp_Handle,
	(const UECodeGen_Private::FPropertyParamsBase*)&Z_Construct_UScriptStruct_FCover_Statics::NewProp_Data,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCover_Statics::PropPointers) < 2048);
// ********** End ScriptStruct FCover Property Definitions *****************************************
const UECodeGen_Private::FStructParams Z_Construct_UScriptStruct_FCover_Statics::StructParams = {
	(UObject* (*)())Z_Construct_UPackage__Script_AICoverSystem,
	nullptr,
	&NewStructOps,
	"Cover",
	Z_Construct_UScriptStruct_FCover_Statics::PropPointers,
	UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCover_Statics::PropPointers),
	sizeof(FCover),
	alignof(FCover),
	RF_Public|RF_Transient|RF_MarkAsNative,
	EStructFlags(0x00000201),
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UScriptStruct_FCover_Statics::Struct_MetaDataParams), Z_Construct_UScriptStruct_FCover_Statics::Struct_MetaDataParams)
};
UScriptStruct* Z_Construct_UScriptStruct_FCover()
{
	if (!Z_Registration_Info_UScriptStruct_FCover.InnerSingleton)
	{
		UECodeGen_Private::ConstructUScriptStruct(Z_Registration_Info_UScriptStruct_FCover.InnerSingleton, Z_Construct_UScriptStruct_FCover_Statics::StructParams);
	}
	return CastChecked<UScriptStruct>(Z_Registration_Info_UScriptStruct_FCover.InnerSingleton);
}
// ********** End ScriptStruct FCover **************************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemPublicData_h__Script_AICoverSystem_Statics
{
	static constexpr FStructRegisterCompiledInInfo ScriptStructInfo[] = {
		{ FCoverBuildParams::StaticStruct, Z_Construct_UScriptStruct_FCoverBuildParams_Statics::NewStructOps, TEXT("CoverBuildParams"),&Z_Registration_Info_UScriptStruct_FCoverBuildParams, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FCoverBuildParams), 300256297U) },
		{ FCoverData::StaticStruct, Z_Construct_UScriptStruct_FCoverData_Statics::NewStructOps, TEXT("CoverData"),&Z_Registration_Info_UScriptStruct_FCoverData, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FCoverData), 2527779184U) },
		{ FCoverPartitionHash::StaticStruct, Z_Construct_UScriptStruct_FCoverPartitionHash_Statics::NewStructOps, TEXT("CoverPartitionHash"),&Z_Registration_Info_UScriptStruct_FCoverPartitionHash, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FCoverPartitionHash), 240377161U) },
		{ FCoverHandle::StaticStruct, Z_Construct_UScriptStruct_FCoverHandle_Statics::NewStructOps, TEXT("CoverHandle"),&Z_Registration_Info_UScriptStruct_FCoverHandle, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FCoverHandle), 1932581828U) },
		{ FCover::StaticStruct, Z_Construct_UScriptStruct_FCover_Statics::NewStructOps, TEXT("Cover"),&Z_Registration_Info_UScriptStruct_FCover, CONSTRUCT_RELOAD_VERSION_INFO(FStructReloadVersionInfo, sizeof(FCover), 1149314207U) },
	};
}; // Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemPublicData_h__Script_AICoverSystem_Statics 
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemPublicData_h__Script_AICoverSystem_3535202710{
	TEXT("/Script/AICoverSystem"),
	nullptr, 0,
	Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemPublicData_h__Script_AICoverSystem_Statics::ScriptStructInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID_Users_matth_Documents_Github_ProjectExtract_Extraction_Plugins_AICoverSystem_Source_AICoverSystem_Public_CoverSystemPublicData_h__Script_AICoverSystem_Statics::ScriptStructInfo),
	nullptr, 0,
};
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
