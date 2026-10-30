// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Names and slots the toon C++ shares with the toon materials, in one place.
 *
 * Materials find these by name (collection parameters) or by index (Custom Primitive Data), never by reference,
 * so renaming one here means renaming it in the materials too:
 *   - MPC_ToonLighting parameters the C++ reads or writes (MF_ToonLit, M_Toon_* read them).
 *   - Custom Primitive Data slots written per primitive. They share one 36-float block per primitive, so a new
 *     feature must pick a free slot here rather than reuse one.
 */
namespace VrrToonLighting
{
	/** The collection every toon material reads its light, shadow map and boil clock from. */
	inline const TCHAR* const CollectionPath = TEXT("/MaterialLibrary/Masters/Toon/MPC_ToonLighting.MPC_ToonLighting");

	// MPC_ToonLighting parameters

	/** Direction to the fake light (read by AVrr_ToonShadowCaster). */
	inline const FName LightDirection(TEXT("LightDirection"));

	/** Shadow map view, written by AVrr_ToonShadowCaster for Vrr Toon Shadow (layout documented there). */
	inline const FName ShadowOrigin(TEXT("ShadowOrigin"));
	inline const FName ShadowRight(TEXT("ShadowRight"));
	inline const FName ShadowUp(TEXT("ShadowUp"));
	inline const FName ShadowForward(TEXT("ShadowForward"));

	/** Camera boil clock, written by UVrr_ToonBoilSubsystem. */
	inline const FName BoilTime(TEXT("BoilTime"));

	/** Custom Primitive Data slots (material parameter in brackets). */
	namespace CPD
	{
		/** Vrr Blob Shadow box: opacity, faded while the owner is in the air [BlobOpacity]. */
		constexpr int32 BlobOpacity = 0;

		/** Vrr Toon Boil: the actor's own boil clock [ToonBoilTime]. */
		constexpr int32 BoilTime = 30;

		/** Vrr Toon Boil: 1 while the component drives the clock, 0 = use the camera clock [ToonBoilActive].
		 *  Must directly follow BoilTime: both are written in one call. */
		constexpr int32 BoilActive = BoilTime + 1;
	}
}
