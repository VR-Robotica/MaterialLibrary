/*
 * VRR_Materials runtime module.
 *
 * Holds the VRR material nodes (MF_*: UMaterialExpressionVrr*, all built on UMaterialExpressionVrrBase and the
 * VrrMaterialMath helpers), the toon shadow caster actor, and the toon boil clock (Vrr_ToonBoil: a subsystem and
 * a component that make the toon noise animate only while the camera or objects move). Nothing needs to run at
 * startup: classes are found by reflection, so the default module implementation is enough.
 */

#include "Modules/ModuleManager.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, VRR_Materials)
