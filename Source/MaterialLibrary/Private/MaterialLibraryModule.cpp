/*
 * MaterialLibrary runtime module.
 *
 * Holds the VRR material nodes (MF_*: UMaterialExpressionVrr*, all built on UMaterialExpressionVrrBase and the
 * VrrMaterialMath helpers) and the C++ that feeds them:
 *   - Vrr_ToonShadowCaster: renders the toon shadow map and writes its view to MPC_ToonLighting.
 *   - Vrr_ToonBoil: a subsystem and a component that make the toon noise animate only while things move.
 *   - Vrr_BlobShadow / Vrr_CelLayer: components for blob shadows under characters and the cel shadow layer.
 * The names and Custom Primitive Data slots they share with the materials are in VrrToonLighting.h.
 * Nothing needs to run at startup: classes are found by reflection, so the default module implementation is enough.
 */

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, MaterialLibrary)
