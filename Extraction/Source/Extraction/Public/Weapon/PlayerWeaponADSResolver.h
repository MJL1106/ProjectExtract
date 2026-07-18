// Pure geometry and settings resolver for player weapon sights.

#pragma once

#include "CoreMinimal.h"
#include "Data/PlayerWeaponPresentationTypes.h"

class USceneComponent;

/**
 * Converts authored sight markers into one stable, hand-local ADS contract.
 * Runtime selection and view ownership remain in the presentation component.
 */
struct EXTRACTION_API FPlayerWeaponADSResolver
{
	static bool ResolveIrons(
		const FTransform& RearInHandSpace,
		const FTransform& FrontInHandSpace,
		const FPlayerWeaponADSDefaults& BaseADS,
		FPlayerWeaponResolvedSight& OutSight);

	static bool ResolveOptic(
		FName OpticId,
		const FTransform& AimPointInHandSpace,
		const FPlayerWeaponADSDefaults& BaseADS,
		const FPlayerWeaponOpticOverride& OpticOverride,
		FPlayerWeaponResolvedSight& OutSight);

	static bool ResolveAttachmentRootTransform(
		const FTransform& WeaponMountInWeaponRootSpace,
		const FTransform& AttachmentMountInAttachmentRootSpace,
		FTransform& OutAttachmentRootInWeaponRootSpace);

	/** Temporary exact-name importer for the Procedural FPS Kit iron sockets. */
	static bool ResolveLegacyIronSockets(
		const USceneComponent& RearComponent,
		const USceneComponent& FrontComponent,
		const FTransform& HandSocketWorld,
		const FPlayerWeaponADSDefaults& BaseADS,
		FPlayerWeaponResolvedSight& OutSight);

	/** Temporary exact-name importer for a kit OpticAimpoint socket. */
	static bool ResolveLegacyOpticSocket(
		FName OpticId,
		const USceneComponent& OpticComponent,
		const FTransform& HandSocketWorld,
		const FPlayerWeaponADSDefaults& BaseADS,
		const FPlayerWeaponOpticOverride& OpticOverride,
		FPlayerWeaponResolvedSight& OutSight);

	static FPlayerWeaponADSDefaults MergeADSSettings(
		const FPlayerWeaponADSDefaults& BaseADS,
		const FPlayerWeaponOpticOverride& OpticOverride);
};
